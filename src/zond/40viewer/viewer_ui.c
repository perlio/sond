/*
 zond (viewer_ui.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2026  pelo america

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU Affero General Public License as
 published by the Free Software Foundation, either version 3 of the
 License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU Affero General Public License for more details.

 You should have received a copy of the GNU Affero General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <mupdf/pdf.h>
#include <mupdf/fitz.h>

#include "../../misc.h"
#include "../../sond_fileparts.h"
#include "../../sond_log_and_error.h"

#include "../zond_init.h"
#include "../zond_pdf_document.h"
#include "../20allgemein/project.h"

#include "document.h"
#include "seiten.h"
#include "viewer.h"
#include "viewer_ui.h"
#include "viewer_render.h"
#include "viewer_annot.h"

static void cb_thumb_sel_changed(GtkTreeSelection *sel, gpointer data) {
	gboolean active = FALSE;
	PdfViewer *pv = (PdfViewer*) data;

	active = (gtk_tree_selection_count_selected_rows(sel) == 0) ? FALSE : TRUE;

	gtk_widget_set_sensitive(pv->item_kopieren, active);
	gtk_widget_set_sensitive(pv->item_ausschneiden, active);

	return;
}

static void cb_thumb_activated(GtkTreeView *tv, GtkTreePath *path,
		GtkTreeViewColumn *column, gpointer data) {
	gint *indices = NULL;
	PdfPos pos = { 0 };
	PdfViewer *pv = (PdfViewer*) data;

	indices = gtk_tree_path_get_indices(path);
	pos.seite = indices[0];
	pos.index = 0;

	viewer_springen_zu_pos_pdf(pv, pos, 0.0);

	return;
}

#ifndef VIEWER
static void cb_viewer_loeschen_anbindung_button_clicked(GtkButton *button,
		gpointer data) {
	PdfViewer *pv = (PdfViewer*) data;

	// Anbindung.von "löschen"
	pv->anbindung.von.index = -1;

	// Anzeige Beginn rückgängig machen
	gtk_widget_set_tooltip_text(pv->button_anbindung, "Anbindung Anfang löschen");
	gtk_widget_set_sensitive(pv->button_anbindung, FALSE);

	return;
}
#endif

static void cb_viewer_auswahlwerkzeug(GtkButton *button, gpointer data) {
	gint button_ID = 0;
	PdfViewer *pv = (PdfViewer*) data;

	button_ID = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "ID"));
	pv->state = button_ID;

	return;
}

static void cb_pv_speichern(GtkButton *button, gpointer data) {
	PdfViewer *pv = (PdfViewer*) data;
	GError* error = NULL;
	gint rc = 0;

	rc = viewer_save_dirty_dds(pv, &error);
	if (rc) {
		display_error(pv->vf, "Speichern nicht erfolgreich", error->message);
		g_error_free(error);
	}

	return;
}

static void cb_tree_thumb(GtkToggleButton *button, gpointer data) {
	PdfViewer *pdfv = (PdfViewer*) data;

	if (gtk_toggle_button_get_active(button)) {
		gtk_widget_show(pdfv->swindow_tree);
		cb_viewer_render_visible_thumbs(pdfv);
	} else {
		gtk_widget_hide(pdfv->swindow_tree);
		GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(pdfv->tree_thumb));
		gtk_tree_selection_unselect_all(sel);
	}

	return;
}

static void cb_viewer_text_search_entry_buffer_changed(gpointer data) {
	PdfViewer *pv = (PdfViewer*) data;

	g_array_remove_range(pv->text_occ.arr_quad, 0, pv->text_occ.arr_quad->len);
	pv->text_occ.not_found = FALSE;
	pv->text_occ.index_act = -1;

	return;
}

static void cb_viewer_text_search(GtkWidget *widget, gpointer data) {
	gint rc = 0;
	GError* error = NULL;

	PdfViewer *pv = (PdfViewer*) data;

	// Wrapper: Delegiert die Logik an viewer.c
	rc = viewer_handle_text_search(pv, widget, &error);
	if (rc) {
		display_error(pv->vf, "Fehler bei Textsuche", error->message);
		g_error_free(error);
	}

	return;
}

static void cb_viewer_spinbutton_value_changed(GtkSpinButton *spin_button,
		gpointer user_data) {
	PdfViewer *pv = (PdfViewer*) user_data;

	pv->zoom = gtk_spin_button_get_value(spin_button);

	viewer_close_thread_pool_and_transfer(pv);

	for (gint i = 0; i < pv->arr_pages->len; i++) {
		ViewerPageNew *viewer_page = NULL;

		viewer_page = g_ptr_array_index(pv->arr_pages, i);
		if (viewer_page->image_page)
			gtk_image_clear(GTK_IMAGE(viewer_page->image_page));
		viewer_page->pixbuf_page = NULL;
		viewer_page->thread &= 4; // thumb bleibt
	}

	// Alte Position merken
	gdouble v_pos = gtk_adjustment_get_value(pv->v_adj) / gtk_adjustment_get_upper(pv->v_adj);
	gdouble h_pos = gtk_adjustment_get_value(pv->h_adj) / gtk_adjustment_get_upper(pv->h_adj);

	viewer_refresh_layout(pv, 0);

	gtk_adjustment_set_value(pv->v_adj, gtk_adjustment_get_upper(pv->v_adj) * v_pos);
	gtk_adjustment_set_value(pv->h_adj, gtk_adjustment_get_upper(pv->h_adj) * h_pos);

	g_signal_emit_by_name(pv->v_adj, "value-changed", NULL);

	gtk_widget_grab_focus(pv->layout);

	return;
}

static void cb_viewer_page_entry_activated(GtkEntry *entry, gpointer user_data) {
	PdfViewer *pv = (PdfViewer*) user_data;

	viewer_handle_page_entry_activated(pv, entry);

	return;
}

static void cb_pv_copy_text(GtkMenuItem *item, gpointer data) {
	gchar *text = NULL;
	gint i = 0;
	gint page_prev = -1;
	gint start = 0;
	gint end = 0;
	PdfViewer *pv = (PdfViewer*) data;

	do {
		if (pv->highlight.page[i] != page_prev) {
			if (page_prev != -1) {
				gchar *add = NULL;
				ViewerPageNew *viewer_page = NULL;
				fz_context *ctx = NULL;

				end = i - 1;
				viewer_page = g_ptr_array_index(pv->arr_pages, page_prev);

				viewer_render_wait_for_transfer(viewer_page->pdf_document_page);

				ctx = zond_pdf_document_get_ctx(viewer_page->pdf_document_page->document);

				if (viewer_page->pdf_document_page->thread & 8) {
					add = fz_copy_selection(ctx,
							viewer_page->pdf_document_page->stext_page,
							pv->highlight.quad[start].ul,
							pv->highlight.quad[end].lr, FALSE);
					text = add_string(text, add);
				}
			}

			page_prev = pv->highlight.page[i];
			start = i;
		}
	} while (pv->highlight.page[i++] != -1);

	GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
	gtk_clipboard_set_text(clipboard, text, -1);
	g_free(text);

	return;
}

static gboolean cb_viewer_swindow_key_press(GtkWidget *swindow,
		GdkEvent *event, gpointer user_data) {
	gint rc = 0;
	GError* error = NULL;

	PdfViewer *pv = (PdfViewer*) user_data;

	// UI-Vorbedingung prüfen
	if (!(pv->clicked_annot))
		return FALSE;

	// Delete-Taste behandeln
	if (event->key.keyval != GDK_KEY_Delete)
		return FALSE;

	rc = viewer_annot_handle_delete(pv, &error);
	if (rc) {
		display_error(pv->vf, "Fehler beim Löschen der Anmerkung", error->message);
		g_error_free(error);
	}

	return FALSE;
}

static gboolean cb_viewer_layout_release_button(GtkWidget *layout,
		GdkEvent *event, gpointer data) {
	gint rc = 0;
	ViewerPageNew *viewer_page = NULL;
	PdfPunkt pdf_punkt = { 0 };

	PdfViewer *pv = (PdfViewer*) data;

	// Nur primärer Button
	if (event->button.button != GDK_BUTTON_PRIMARY)
		return FALSE;

	// UI-Vorbedingung prüfen
	if (!(pv->dd))
		return TRUE;

	if (event->button.button != GDK_BUTTON_PRIMARY)
		return FALSE;
	if (!(pv->dd))
		return TRUE;

	rc = viewer_abfragen_pdf_punkt(pv,
			fz_make_point(event->motion.x, event->motion.y), &pdf_punkt);
	viewer_page = g_ptr_array_index(pv->arr_pages, pdf_punkt.seite);
	viewer_render_wait_for_transfer(viewer_page->pdf_document_page);

	pv->click_on_text = FALSE;

	viewer_set_cursor(pv, rc, viewer_page, pv->clicked_annot, pdf_punkt);

	//Text ist markiert
	if (pv->highlight.page[0] != -1) {
		//Annot ist gewählt
		if (pv->state == 1 || pv->state == 2) {
			gint rc = 0;
			GError* error = NULL;

			rc = viewer_annot_create_markup(pv, viewer_page, pdf_punkt, &error);
			if (rc) {
				display_error(pv->vf, "Annotation konnte nicht erstellt werden",
						error->message);
				g_error_free(error);
				return TRUE;
			}

			pv->highlight.page[0] = -1;
		}
		else
			gtk_widget_set_sensitive(pv->item_copy, TRUE);
	}

	//Button wird losgelassen, nachdem auf Text-Annot geklickt wurde
	if (pv->clicked_annot && pv->clicked_annot->annot.type == PDF_ANNOT_TEXT) {
		gint rc = 0;
		GError* error = NULL;

		rc = viewer_annot_handle_release_clicked_annot(pv, viewer_page, pdf_punkt, &error);
		if (rc) {
			display_error(pv->vf, "Annotation konnte nicht bearbeitet werden",
					error->message);
			g_error_free(error);
		}
	}

	return TRUE;
}

static gboolean cb_viewer_motion_notify(GtkWidget *window, GdkEvent *event,
		gpointer data) {
	PdfViewer *pv = (PdfViewer*) data;

	// Signal wird nur durchgelassen, wenn layout keines erhält
	// Cursor außerhalb des Layouts zurücksetzen
	gdk_window_set_cursor(pv->gdk_window, pv->cursor_default);

	return FALSE;
}

static gboolean cb_viewer_layout_motion_notify(GtkWidget *layout,
		GdkEvent *event, gpointer data) {
	PdfViewer *pv = (PdfViewer*) data;

	// UI-Vorbedingung prüfen
	if (!(pv->dd))
		return TRUE;

	viewer_handle_layout_motion_notify(pv, event);

	return TRUE;
}

static gboolean cb_viewer_layout_press_button(GtkWidget *layout,
		GdkEvent *event, gpointer user_data) {
	GError* error = NULL;
	gint rc = 0;

	PdfViewer *pv = (PdfViewer*) user_data;

	// UI-Vorbedingung prüfen
	if (!(pv->dd))
		return TRUE;

	// UI: Focus setzen
	gtk_widget_grab_focus(pv->layout);

	// Wrapper: Delegiert die Logik an viewer.c
	rc = viewer_handle_button_press(pv, event, &error);
	if (rc) {
		display_error(pv->vf, "Fehler bei Maus-Button-Event", error->message);
		g_error_free(error);
	}

	return TRUE;
}

static void cb_viewer_annot_edit_closed(GtkWidget *popover, gpointer data) {
	PdfViewer *pdfv = (PdfViewer*) data;
	GError* error = NULL;
	gint rc = 0;

	rc = viewer_annot_handle_edit_closed(pdfv, popover, &error);
	if (rc) {
		display_message(pdfv->vf, "Annotation editieren fehlgeschlagen\n",
				error->message, NULL);
		g_error_free(error);
	}

	return;
}

static void viewer_thumblist_render_textcell(GtkTreeViewColumn *column,
		GtkCellRenderer *cell, GtkTreeModel *model, GtkTreeIter *iter,
		gpointer data) {
	GtkTreePath *path = NULL;
	gint *indices = NULL;
	gchar *text = NULL;

	path = gtk_tree_model_get_path(model, iter);
	indices = gtk_tree_path_get_indices(path);

	text = g_strdup_printf("%i", indices[0] + 1);
	gtk_tree_path_free(path);
	g_object_set(G_OBJECT(cell), "text", text, NULL);
	g_free(text);

	return;
}

// UI-Aufbau-Funktion
static void viewer_einrichten_fenster(PdfViewer *pv) {
	pv->vf = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(pv->vf), VIEWER_WIDTH, VIEWER_HEIGHT);

	GtkAccelGroup *accel_group = gtk_accel_group_new();
	gtk_window_add_accel_group(GTK_WINDOW(pv->vf), accel_group);

	// Menu
#ifdef VIEWER
	GtkWidget* item_oeffnen = gtk_menu_item_new_with_label("Datei öffnen");
	pv->item_schliessen = gtk_menu_item_new_with_label("Datei schließen");
	GtkWidget* item_beenden = gtk_menu_item_new_with_label("Beenden");
	GtkWidget* item_sep1 = gtk_separator_menu_item_new();
#endif
	pv->item_kopieren = gtk_menu_item_new_with_label("Seiten kopieren");
	gtk_widget_add_accelerator(pv->item_kopieren, "activate", accel_group,
			GDK_KEY_c, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	pv->item_ausschneiden = gtk_menu_item_new_with_label("Seiten ausschneiden");
	gtk_widget_add_accelerator(pv->item_ausschneiden, "activate", accel_group,
			GDK_KEY_x, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	pv->item_drehen = gtk_menu_item_new_with_label("Seiten drehen");
	pv->item_einfuegen = gtk_menu_item_new_with_label("Seiten einfügen");
	pv->item_loeschen = gtk_menu_item_new_with_label("Seiten löschen");
	pv->item_entnehmen = gtk_menu_item_new_with_label("Entnehmen");
	pv->item_ocr = gtk_menu_item_new_with_label("OCR");

	GtkWidget *sep0 = gtk_separator_menu_item_new();

	pv->item_copy = gtk_menu_item_new_with_label("Text kopieren");

	gtk_widget_set_sensitive(pv->item_kopieren, FALSE);
	gtk_widget_set_sensitive(pv->item_ausschneiden, FALSE);
	gtk_widget_set_sensitive(pv->item_copy, FALSE);

	// Menu erzeugen
	GtkWidget *menu_viewer = gtk_menu_new();

	// Füllen
#ifdef VIEWER
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_viewer), item_oeffnen);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_viewer), pv->item_schliessen);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_viewer), item_beenden);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_viewer), item_sep1);
#endif
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_viewer), pv->item_kopieren);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_viewer), pv->item_ausschneiden);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_viewer), pv->item_einfuegen);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_viewer), pv->item_drehen);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_viewer), pv->item_loeschen);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_viewer), pv->item_entnehmen);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_viewer), pv->item_ocr);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_viewer), sep0);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_viewer), pv->item_copy);

	gtk_widget_show_all(menu_viewer);

	// Menu Button
	GtkWidget *button_menu_viewer = gtk_menu_button_new();
	gtk_menu_button_set_popup(GTK_MENU_BUTTON(button_menu_viewer), menu_viewer);

	// Headerbar
	pv->headerbar = gtk_header_bar_new();
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(pv->headerbar), TRUE);
	gtk_header_bar_set_has_subtitle(GTK_HEADER_BAR(pv->headerbar), TRUE);
	gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(pv->headerbar), ":minimize,maximize,close");
	gtk_window_set_titlebar(GTK_WINDOW(pv->vf), pv->headerbar);

	// Toolbar
	GtkWidget *button_thumb = gtk_toggle_button_new();
	GtkWidget *image_thumb = gtk_image_new_from_icon_name("go-next", GTK_ICON_SIZE_BUTTON);
	gtk_button_set_image(GTK_BUTTON(button_thumb), image_thumb);

	pv->button_speichern = gtk_button_new_from_icon_name("document-save", GTK_ICON_SIZE_BUTTON);
	gtk_widget_set_sensitive(pv->button_speichern, FALSE);
	GtkWidget *button_print = gtk_button_new_from_icon_name("document-print", GTK_ICON_SIZE_BUTTON);
	pv->button_zeiger = gtk_radio_button_new(NULL);
	GtkWidget *button_highlight = gtk_radio_button_new_from_widget(GTK_RADIO_BUTTON(pv->button_zeiger));
	GtkWidget *button_underline = gtk_radio_button_new_from_widget(GTK_RADIO_BUTTON(button_highlight));
	GtkWidget *button_paint = gtk_radio_button_new_from_widget(GTK_RADIO_BUTTON(button_underline));

	GtkWidget *image_zeiger = gtk_image_new_from_icon_name("accessories-text-editor", GTK_ICON_SIZE_BUTTON);
	gtk_button_set_image(GTK_BUTTON(pv->button_zeiger), image_zeiger);
	GtkWidget *image_highlight = gtk_image_new_from_icon_name("edit-select-all", GTK_ICON_SIZE_BUTTON);
	gtk_button_set_image(GTK_BUTTON(button_highlight), image_highlight);
	GtkWidget *image_underline = gtk_image_new_from_icon_name("format-text-underline", GTK_ICON_SIZE_BUTTON);
	gtk_button_set_image(GTK_BUTTON(button_underline), image_underline);
	GtkWidget *image_paint = gtk_image_new_from_icon_name("edit-paste", GTK_ICON_SIZE_BUTTON);
	gtk_button_set_image(GTK_BUTTON(button_paint), image_paint);

	// SpinButton für Zoom
	GtkWidget *spin_button = gtk_spin_button_new_with_range(ZOOM_MIN, ZOOM_MAX, 5.0);
	gtk_orientable_set_orientation(GTK_ORIENTABLE(spin_button), GTK_ORIENTATION_VERTICAL);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_button), (gdouble)pv->zoom);

	GtkWidget *frame_spin = gtk_frame_new("Zoom");
	gtk_container_add(GTK_CONTAINER(frame_spin), spin_button);

	g_signal_connect(spin_button, "value-changed",
			G_CALLBACK(cb_viewer_spinbutton_value_changed), (gpointer)pv);

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pv->button_zeiger), TRUE);

	g_object_set_data(G_OBJECT(button_highlight), "ID", GINT_TO_POINTER(1));
	g_object_set_data(G_OBJECT(button_underline), "ID", GINT_TO_POINTER(2));
	g_object_set_data(G_OBJECT(button_paint), "ID", GINT_TO_POINTER(3));

#ifndef VIEWER
	pv->button_anbindung = gtk_button_new_from_icon_name("edit-delete", GTK_ICON_SIZE_BUTTON);
	gtk_widget_set_sensitive(pv->button_anbindung, FALSE);
	gtk_widget_set_tooltip_text(pv->button_anbindung, "Anbindung Anfang löschen");
#endif

	// vbox Tools
	GtkWidget *vbox_tools = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start(GTK_BOX(vbox_tools), button_menu_viewer, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox_tools), pv->button_speichern, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox_tools), button_print, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox_tools), button_thumb, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox_tools), pv->button_zeiger, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox_tools), button_highlight, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox_tools), button_underline, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox_tools), button_paint, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox_tools), frame_spin, FALSE, FALSE, 0);
#ifndef VIEWER
	gtk_box_pack_start(GTK_BOX(vbox_tools), pv->button_anbindung, FALSE, FALSE, 0);
#endif

	// Box mit Eingabemöglichkeiten
	pv->entry = gtk_entry_new();
	gtk_entry_set_input_purpose(GTK_ENTRY(pv->entry), GTK_INPUT_PURPOSE_DIGITS);
	gtk_entry_set_width_chars(GTK_ENTRY(pv->entry), 9);

	pv->label_anzahl = gtk_label_new("");

	GtkWidget *box_seiten = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(box_seiten), pv->entry, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box_seiten), pv->label_anzahl, FALSE, FALSE, 0);
	GtkWidget *frame_seiten = gtk_frame_new("Seiten");
	gtk_container_add(GTK_CONTAINER(frame_seiten), box_seiten);

	// Textsuche
	pv->button_vorher = gtk_button_new_from_icon_name("go-previous", GTK_ICON_SIZE_SMALL_TOOLBAR);
	pv->entry_search = gtk_entry_new();
	gtk_entry_set_width_chars(GTK_ENTRY(pv->entry_search), 15);
	pv->button_nachher = gtk_button_new_from_icon_name("go-next", GTK_ICON_SIZE_SMALL_TOOLBAR);

	GtkWidget *box_text = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(box_text), pv->button_vorher, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box_text), pv->entry_search, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box_text), pv->button_nachher, FALSE, FALSE, 0);
	GtkWidget *frame_search = gtk_frame_new("Text suchen");
	gtk_container_add(GTK_CONTAINER(frame_search), box_text);

	gtk_header_bar_pack_start(GTK_HEADER_BAR(pv->headerbar), frame_seiten);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(pv->headerbar), frame_search);

	// layout
	pv->layout = gtk_layout_new(NULL, NULL);
	gtk_widget_set_can_focus(pv->layout, TRUE);
	gtk_widget_add_events(pv->layout, GDK_POINTER_MOTION_MASK);
	gtk_widget_add_events(pv->layout, GDK_BUTTON_PRESS_MASK);
	gtk_widget_add_events(pv->layout, GDK_BUTTON_RELEASE_MASK);

	// Scrolled window
	GtkWidget *swindow = gtk_scrolled_window_new(NULL, NULL);
	pv->v_adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(swindow));
	pv->h_adj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(swindow));

	gtk_container_add(GTK_CONTAINER(swindow), pv->layout);
	gtk_widget_set_halign(pv->layout, GTK_ALIGN_CENTER);

	// Scrolled window für thumbnail_tree
	pv->tree_thumb = gtk_tree_view_new();
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(pv->tree_thumb), FALSE);
	GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(pv->tree_thumb));
	gtk_tree_selection_set_mode(sel, GTK_SELECTION_MULTIPLE);

	GtkTreeViewColumn *column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_resizable(column, FALSE);
	gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);

	GtkCellRenderer *renderer_text = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(column, renderer_text, FALSE);
	gtk_tree_view_column_set_cell_data_func(column, renderer_text,
			viewer_thumblist_render_textcell, NULL, NULL);

	GtkCellRenderer *renderer_pixbuf = gtk_cell_renderer_pixbuf_new();
	gtk_tree_view_column_pack_start(column, renderer_pixbuf, TRUE);
	gtk_tree_view_column_set_attributes(column, renderer_pixbuf, "pixbuf", 0, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(pv->tree_thumb), column);

	GtkListStore *store_thumbs = gtk_list_store_new(1, GDK_TYPE_PIXBUF);
	gtk_tree_view_set_model(GTK_TREE_VIEW(pv->tree_thumb), GTK_TREE_MODEL(store_thumbs));
	g_object_unref(store_thumbs);

	pv->swindow_tree = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(pv->swindow_tree), pv->tree_thumb);
	GtkAdjustment *vadj_thumb = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(pv->swindow_tree));

	GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_paned_pack1(GTK_PANED(paned), swindow, TRUE, TRUE);
	gtk_paned_pack2(GTK_PANED(paned), pv->swindow_tree, FALSE, FALSE);
	gtk_paned_set_position(GTK_PANED(paned), 760);

	// hbox erstellen
	GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(hbox), vbox_tools, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), paned, TRUE, TRUE, 0);

	gtk_container_add(GTK_CONTAINER(pv->vf), hbox);

	// Popover für Annot-Inhalte
	pv->annot_pop = gtk_popover_new(pv->layout);
	pv->annot_label = gtk_label_new(NULL);
	gtk_widget_show(pv->annot_label);
	gtk_container_add(GTK_CONTAINER(pv->annot_pop), pv->annot_label);
	gtk_popover_set_modal(GTK_POPOVER(pv->annot_pop), FALSE);

	// popover mit textview
	pv->annot_pop_edit = gtk_popover_new(pv->layout);
	pv->annot_textview = gtk_text_view_new();
	gtk_widget_show(pv->annot_textview);
	gtk_container_add(GTK_CONTAINER(pv->annot_pop_edit), pv->annot_textview);
	gtk_popover_set_modal(GTK_POPOVER(pv->annot_pop_edit), TRUE);
	g_signal_connect(pv->annot_pop_edit, "closed",
			G_CALLBACK(cb_viewer_annot_edit_closed), pv);

	gtk_widget_show_all(pv->vf);
	gtk_widget_hide(pv->swindow_tree);

	pv->gdk_window = gtk_widget_get_window(pv->vf);
	GdkDisplay *display = gdk_window_get_display(pv->gdk_window);
	pv->cursor_default = gdk_cursor_new_from_name(display, "default");
	pv->cursor_text = gdk_cursor_new_from_name(display, "text");
	pv->cursor_vtext = gdk_cursor_new_from_name(display, "vertical-text");
	pv->cursor_grab = gdk_cursor_new_from_name(display, "grab");
	pv->cursor_annot = gdk_cursor_new_from_name(display, "pointer");

	// Signale Menu
#ifdef VIEWER
	g_signal_connect(item_oeffnen, "activate", G_CALLBACK(cb_datei_oeffnen), pv);
	g_signal_connect(pv->item_schliessen, "activate", G_CALLBACK(cb_datei_schliessen), pv);
	g_signal_connect_swapped(item_beenden, "activate", G_CALLBACK(viewer_save_and_close), pv);
#endif

	g_signal_connect(pv->item_kopieren, "activate", G_CALLBACK(cb_seiten_kopieren), pv);
	g_signal_connect(pv->item_ausschneiden, "activate", G_CALLBACK(cb_seiten_ausschneiden), pv);
	g_signal_connect(pv->item_loeschen, "activate", G_CALLBACK(cb_pv_seiten_loeschen), pv);
	g_signal_connect(pv->item_einfuegen, "activate", G_CALLBACK(cb_pv_seiten_einfuegen), pv);
	g_signal_connect(pv->item_drehen, "activate", G_CALLBACK(cb_pv_seiten_drehen), pv);
	g_signal_connect(pv->item_ocr, "activate", G_CALLBACK(cb_pv_seiten_ocr), pv);
	g_signal_connect(pv->item_copy, "activate", G_CALLBACK(cb_pv_copy_text), pv);

	g_signal_connect(pv->entry, "activate", G_CALLBACK(cb_viewer_page_entry_activated), (gpointer)pv);

	g_signal_connect(pv->entry_search, "activate", G_CALLBACK(cb_viewer_text_search), pv);
	g_signal_connect(pv->button_nachher, "clicked", G_CALLBACK(cb_viewer_text_search), pv);
	g_signal_connect(pv->button_vorher, "clicked", G_CALLBACK(cb_viewer_text_search), pv);
	g_signal_connect_swapped(gtk_entry_get_buffer(GTK_ENTRY(pv->entry_search)),
			"deleted-text", G_CALLBACK(cb_viewer_text_search_entry_buffer_changed), pv);
	g_signal_connect_swapped(gtk_entry_get_buffer(GTK_ENTRY(pv->entry_search)),
			"inserted-text", G_CALLBACK(cb_viewer_text_search_entry_buffer_changed), pv);

	// Signale Toolbox
	g_signal_connect(pv->button_speichern, "clicked", G_CALLBACK(cb_pv_speichern), pv);
	g_signal_connect(button_print, "clicked", G_CALLBACK(cb_viewer_render_print), pv);
	g_signal_connect(button_thumb, "toggled", G_CALLBACK(cb_tree_thumb), pv);
	g_signal_connect(pv->button_zeiger, "toggled", G_CALLBACK(cb_viewer_auswahlwerkzeug), (gpointer)pv);
	g_signal_connect(button_highlight, "toggled", G_CALLBACK(cb_viewer_auswahlwerkzeug), (gpointer)pv);
	g_signal_connect(button_underline, "toggled", G_CALLBACK(cb_viewer_auswahlwerkzeug), (gpointer)pv);
	g_signal_connect(button_paint, "toggled", G_CALLBACK(cb_viewer_auswahlwerkzeug), (gpointer)pv);
#ifndef VIEWER
	g_signal_connect(pv->button_anbindung, "clicked",
			G_CALLBACK(cb_viewer_loeschen_anbindung_button_clicked), pv);
#endif

	g_signal_connect_swapped(pv->v_adj, "value-changed",
			G_CALLBACK(cb_viewer_render_visible_pages), pv);
	g_signal_connect_swapped(vadj_thumb, "value-changed",
			G_CALLBACK(cb_viewer_render_visible_thumbs), pv);

	g_signal_connect(pv->tree_thumb, "row-activated", G_CALLBACK(cb_thumb_activated), pv);
	g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(pv->tree_thumb)),
			"changed", G_CALLBACK(cb_thumb_sel_changed), pv);

	g_signal_connect(pv->layout, "button-press-event",
			G_CALLBACK(cb_viewer_layout_press_button), (gpointer)pv);
	g_signal_connect(pv->layout, "button-release-event",
			G_CALLBACK(cb_viewer_layout_release_button), (gpointer)pv);
	g_signal_connect(pv->layout, "motion-notify-event",
			G_CALLBACK(cb_viewer_layout_motion_notify), (gpointer)pv);
	g_signal_connect(pv->layout, "key-press-event",
			G_CALLBACK(cb_viewer_swindow_key_press), (gpointer)pv);

	g_signal_connect(pv->vf, "motion-notify-event",
			G_CALLBACK(cb_viewer_motion_notify), (gpointer)pv);

	g_signal_connect_swapped(pv->vf, "delete-event",
			G_CALLBACK(viewer_save_and_close), (gpointer)pv);

	return;
}

PdfViewer*
viewer_init(Projekt *zond) {
	PdfViewer *pv = g_malloc0(sizeof(PdfViewer));

	pv->zond = zond;
	pv->zoom = g_settings_get_double(zond->settings, "zoom");

	g_ptr_array_add(zond->arr_pv, pv);

	pv->arr_pages = g_ptr_array_new_with_free_func(g_free);

	//highlight Sentinel an den Anfang setzen
	pv->highlight.page[0] = -1;
	pv->anbindung.von.index = -1;
	pv->anbindung.bis.index = EOP + 1;

	pv->text_occ.arr_quad = g_array_new( FALSE, FALSE, sizeof(fz_quad));

	pv->arr_rendered = g_array_new( FALSE, FALSE, sizeof(RenderResponse));
	g_array_set_clear_func(pv->arr_rendered,
			(GDestroyNotify) viewer_render_response_free);

	g_mutex_init(&pv->mutex_arr_rendered);

	//  Fenster erzeugen und anzeigen
	viewer_einrichten_fenster(pv);

	return pv;
}
