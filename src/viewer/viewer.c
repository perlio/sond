/*
 zond (viewer.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2020  pelo america

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

#include "viewer.h"

G_DEFINE_QUARK(viewer-error-quark, viewer_error)

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <mupdf/pdf.h>
#include <mupdf/fitz.h>

#include "../gtk3/sond_dialoge.h"
#include "../misc_stdlib.h"
#include "../sond_log_and_error.h"

#include "zond_pdf_document.h"

#include "../znd/anbindung.h"


#include "document.h"
#include "seiten.h"
#include "viewer_ui.h"
#include "viewer_render.h"
#include "viewer_search.h"
#include "viewer_annot.h"
#include "viewer_parts.h"
#include "viewer_save.h"

void viewer_springen_zu_pos_pdf(PdfViewer *pv, PdfPos pdf_pos, gdouble delta) {
	gdouble value = 0.0;
	ViewerPageNew *viewer_page = NULL;

	if (pv->arr_pages->len == 0) //z.B. Datei im eigenständigen Viewer inzwischen geschlossen
		return;

	if (pdf_pos.seite >= (gint) pv->arr_pages->len)
		pdf_pos.seite = pv->arr_pages->len - 1;
	if (pdf_pos.seite < 0)
		pdf_pos.seite = 0;

	viewer_page = g_ptr_array_index(pv->arr_pages, pdf_pos.seite);

	//Länge aktueller Seite ermitteln
	gdouble page = viewer_page->crop.y1 - viewer_page->crop.y0;
	if (pdf_pos.index <= page)
		value = viewer_page->y_pos + pdf_pos.index * pv->zoom / 100;
	else
		value = viewer_page->y_pos + page * pv->zoom / 100;

	if (pv->session->state & GDK_MOD1_MASK) {
		gdouble page_size = gtk_adjustment_get_page_size(pv->v_adj);
		value -= page_size;
	}
	gtk_adjustment_set_value(pv->v_adj, (value > delta) ? value - delta : 0);

	return;
}

void viewer_get_iter_thumb(PdfViewer *pv, gint page_pv, GtkTreeIter *iter) {
	GtkTreeModel *model = NULL;
	GtkTreePath *path = NULL;

	path = gtk_tree_path_new_from_indices(page_pv, -1);
	model = gtk_tree_view_get_model(GTK_TREE_VIEW(pv->tree_thumb));
	gtk_tree_model_get_iter(model, iter, path);
	gtk_tree_path_free(path);

	return;
}

GtkWidget* viewer_parent(PdfViewer *pv) {
	GtkWidget *top = gtk_widget_get_toplevel(pv->widget);

	return GTK_IS_WINDOW(top) ? top : NULL;
}

void viewer_fenster_praesentieren(PdfViewer *pv) {
	GtkWidget *fenster = viewer_parent(pv);

	if (fenster)
		gtk_window_present(GTK_WINDOW(fenster));

	return;
}

static void viewer_set_layout_size(PdfViewer* pv,
		gdouble x_max, gdouble y_pos) {
	x_max = x_max * pv->zoom / 100;

	y_pos -= PAGE_SPACE;

	gtk_layout_set_size(GTK_LAYOUT(pv->layout), (gint) (x_max + .5),
			(gint) (y_pos + .5));

	//label mit Gesamtseitenzahl erzeugen
	gchar *text = g_strdup_printf("/ %i ", pv->arr_pages->len);
	gtk_label_set_text(GTK_LABEL(pv->label_anzahl), text);
	g_free(text);

	gtk_widget_set_size_request(pv->layout, (gint) (x_max + 0.5),
			(gint) (y_pos + 0.5));

	gtk_adjustment_set_value(pv->h_adj, (x_max - VIEWER_WIDTH) / 2);

	return;
}

void viewer_refresh_layout(PdfViewer *pv, gint pos) {
	ViewerPageNew *viewer_page = NULL;
	gdouble y_pos = 0;
	gdouble x_max = 0;

	if (pv->arr_pages->len == 0)
		return;

	if (pos > 0) {
		viewer_page = g_ptr_array_index(pv->arr_pages, pos - 1);

		y_pos = viewer_page->y_pos
				+ (viewer_page->crop.y1 - viewer_page->crop.y0) * pv->zoom
						/ 100 + PAGE_SPACE;
	}

	for (gint u = pos; u < pv->arr_pages->len; u++) {
		viewer_page = g_ptr_array_index(pv->arr_pages, u);
		if ((viewer_page->crop.x1 - viewer_page->crop.x0) > x_max)
			x_max = viewer_page->crop.x1 - viewer_page->crop.x0;

		viewer_page->y_pos = (gint) (y_pos + .5);

		y_pos += (viewer_page->crop.y1 - viewer_page->crop.y0) * pv->zoom
				/ 100 + PAGE_SPACE;
	}

	viewer_set_layout_size(pv, x_max, y_pos);

	for (gint u = pos; u < pv->arr_pages->len; u++) {
		ViewerPageNew *viewer_page = NULL;

		viewer_page = g_ptr_array_index(pv->arr_pages, u);
		if (viewer_page->image_page)
			gtk_layout_move(GTK_LAYOUT(pv->layout), viewer_page->image_page,
					(gint) ((x_max - (viewer_page->crop.x1 - viewer_page->crop.x0))
					* pv->zoom / 100) / 2, viewer_page->y_pos);
	}

	return;
}

ViewerPageNew*
viewer_new_page(PdfViewer *pdfv, DisplayedDocument* dd,
		gint page_doc) {
	ViewerPageNew *viewer_page = NULL;
	PdfDocumentPage* pdf_document_page = NULL;

	pdf_document_page = zond_pdf_document_get_pdf_document_page(
			dd->zpdfd_part->zond_pdf_document, page_doc);
	if (pdf_document_page->deleted) return NULL;

	viewer_page = g_malloc0(sizeof(ViewerPageNew));

	viewer_page->pdfv = pdfv;
	viewer_page->dd = dd;
	viewer_page->pdf_document_page = pdf_document_page;
	viewer_page->crop = viewer_page->pdf_document_page->rect;

	return viewer_page;
}

static void viewer_create_layout(PdfViewer *pv) {
	DisplayedDocument *dd = pv->dd;
	gdouble x_max = 0;
	gdouble y_pos = 0;

	do {
		gint von = 0;
		gint bis = 0;

		von = dd->zpdfd_part->first_page->page_akt;
		bis = dd->zpdfd_part->last_page->page_akt;

		for (gint i = von; i <= bis; i++) {
			ViewerPageNew *viewer_page = NULL;
			GtkTreeIter iter_tmp;

			viewer_page = viewer_new_page(pv, dd, i);

			//viewer_new_page gibt NULL zurück, wenn pdf_document_page to_be_deleted ist
			if (!viewer_page) continue;

			viewer_page->y_pos = (gint) (y_pos + .5);

			viewer_page->crop.y0 =
					(i == von) ? (gfloat) dd->zpdfd_part->first_index :
							viewer_page->crop.y0;
			viewer_page->crop.y1 =
					((i == bis)
							&& (dd->zpdfd_part->last_index < EOP)) ?
							(gfloat) dd->zpdfd_part->last_index :
							viewer_page->crop.y1;

			g_ptr_array_add(pv->arr_pages, viewer_page);
			gtk_list_store_insert(
					GTK_LIST_STORE(
							gtk_tree_view_get_model( GTK_TREE_VIEW(pv->tree_thumb) )),
					&iter_tmp, -1);

			y_pos += ((viewer_page->crop.y1 - viewer_page->crop.y0) * pv->zoom
					/ 100) + PAGE_SPACE;
			if ((viewer_page->crop.x1 - viewer_page->crop.x0) > x_max)
				x_max = viewer_page->crop.x1 - viewer_page->crop.x0;
		}

	} while ((dd = dd->next));

	viewer_set_layout_size(pv, x_max, y_pos);

	return;
}

typedef struct {
	PdfViewer *pv;
	GtkWidget *widget; //Weak-Pointer: NULL, wenn der Viewer inzwischen geschlossen wurde
	PdfPos pdf_pos;
	gdouble delta;
} SSpringArgs;

static gboolean viewer_springen_idle(gpointer data) {
	SSpringArgs *args = (SSpringArgs*) data;

	if (args->widget) {
		g_object_remove_weak_pointer(G_OBJECT(args->widget), (gpointer*) &args->widget);
		viewer_springen_zu_pos_pdf(args->pv, args->pdf_pos, args->delta);
	}
	g_free(args);
	return G_SOURCE_REMOVE;
}

void viewer_springen_zu_pos_pdf_idle(PdfViewer *pv, PdfPos pdf_pos,
		gdouble delta) {
	SSpringArgs *args = g_new0(SSpringArgs, 1);
	args->pv = pv;
	args->widget = pv->widget;
	g_object_add_weak_pointer(G_OBJECT(args->widget), (gpointer*) &args->widget);
	args->pdf_pos = pdf_pos;
	args->delta = delta;
	g_idle_add(viewer_springen_idle, args);

	return;
}

void viewer_display_document(PdfViewer *pv, DisplayedDocument *dd, gint page,
		gint index) {
	PdfPos pdf_pos = { page, index };

	pv->dd = dd;
	viewer_dd_signale_verbinden(pv);

	viewer_create_layout(pv);

	if (page || index)
		viewer_springen_zu_pos_pdf_idle(pv, pdf_pos, 0.0);
	else
		g_signal_emit_by_name(pv->v_adj, "value-changed", NULL);

	//Test, ob in Viewer "schmutzige" dds angezeigt werden sollen - dann speichern-icon aktiv
	viewer_speichern_aktualisieren(pv);

	gtk_widget_grab_focus(pv->layout);

	return;
}

void viewer_schliessen(PdfViewer *pv) {
	viewer_close_thread_pool_and_transfer(pv); //..._and_transfer, damit etwaig noch gerenderte GdkPixbufs verarztet werden
	g_idle_remove_by_data(pv);
	viewer_text_search_cancel(pv);

	/* Beim Zerstören des Fensters können sich Bildlauf-Bereiche und Auswahl ändern; die Handler
	 * greifen auf pv zu, das unten teilweise schon freigegeben wird. */
	g_signal_handlers_disconnect_by_data(pv->widget, pv);
	g_signal_handlers_disconnect_by_data(pv->v_adj, pv);
	g_signal_handlers_disconnect_by_data(
			gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(pv->swindow_tree)), pv);
	g_signal_handlers_disconnect_by_data(
			gtk_tree_view_get_selection(GTK_TREE_VIEW(pv->tree_thumb)), pv);

	g_array_unref(pv->arr_rendered);
	g_mutex_clear(&pv->mutex_arr_rendered);

	g_ptr_array_unref(pv->arr_pages); //vor gtk_widget_destroy(vf), weil freeFunc gesetzt Nein! stimmt nicht! Keine free-func
	g_array_unref(pv->text_occ.arr_quad);
	g_array_unref(pv->text_occ.arr_hit);

	//gdk_cursor_new_from_name() liefert eine neue Referenz (transfer full,
	//s. GDK-Doku) - ohne diese unrefs leakt jedes Öffnen+Schließen eines
	//Viewers fünf GdkCursor-Objekte. g_clear_object() statt g_object_unref():
	//NULL-sicher und setzt den Pointer selbst zurück.
	g_clear_object(&pv->cursor_default);
	g_clear_object(&pv->cursor_text);
	g_clear_object(&pv->cursor_vtext);
	g_clear_object(&pv->cursor_grab);
	g_clear_object(&pv->cursor_annot);

	viewer_dd_signale_trennen(pv);

	/* Kürzel vom Fenster lösen, bevor das Widget (mit dem Fenster der Hülle) zerstört wird; die Gruppe
	 * selbst erst danach freigeben: die Menüeinträge (Accel-Beschriftungen) greifen beim Zerstören
	 * noch auf sie zu. */
	if (pv->accel_group) {
		GtkWidget *top = gtk_widget_get_toplevel(pv->widget);

		if (GTK_IS_WINDOW(top))
			gtk_window_remove_accel_group(GTK_WINDOW(top), pv->accel_group);
	}

	//mit eigenem Fenster dieses, sonst nur das Widget (es verschwindet beim Embedder)
	if (pv->fenster)
		gtk_widget_destroy(pv->fenster);
	else
		gtk_widget_destroy(pv->widget);

	//die Kopfzeile ist nur dann mit zerstört, wenn jemand sie eingebaut hat
	gtk_widget_destroy(pv->headerbar);
	g_object_unref(pv->headerbar);
	g_clear_object(&pv->accel_group);

	document_free_displayed_documents(pv->dd);

	//pv aus Liste der geöffneten pvs entfernen
	g_ptr_array_remove_fast(pv->session->arr_pv, pv);

	g_free(pv);

	return;
}

void viewer_handle_page_entry_activated(PdfViewer* pv, GtkEntry *entry) {
	guint page_num = 0;
	gint rc = 0;
	gint erste = 0;
	gint letzte = 0;

	const gchar *text_entry = gtk_entry_get_text(entry);

	rc = string_to_guint(text_entry, &page_num);
	if (rc || (page_num < 1) || (page_num > pv->arr_pages->len)) {
		viewer_render_get_visible_pages(pv, &erste, &letzte);
		gchar *text = NULL;
		text = g_strdup_printf("%i-%i", erste + 1, letzte + 1);
		gtk_entry_set_text(entry, (const gchar*) text);
		g_free(text);
	} else {
		ViewerPageNew *viewer_page = NULL;

		viewer_page = g_ptr_array_index(pv->arr_pages, page_num - 1);
		gtk_adjustment_set_value(pv->v_adj, viewer_page->y_pos);
	}

	gtk_widget_grab_focus(pv->layout);

	return;
}

void viewer_save_and_close(PdfViewer *pdfv) {
	gtk_popover_popdown(GTK_POPOVER(pdfv->annot_pop_edit));

	//ggf. fragen, ob gespeichert werden soll
	if (gtk_widget_get_sensitive(pdfv->button_speichern)) {
		gint rc = 0;

		rc = abfrage_frage( NULL, "Viewer enthält Änderungen", "Speichern?",
				NULL);

		if (rc == GTK_RESPONSE_YES) {
			gint ret = 0;
			GError* error = NULL;

			ret = viewer_save_dirty_dds(pdfv, &error);
			if (ret) {
				display_message(viewer_parent(pdfv), "Speichern nicht erfoglreich\n\n", error->message, NULL);
				g_error_free(error);

				return;
			}
		}
		else if (rc != GTK_RESPONSE_NO) return;
	}

	viewer_schliessen(pdfv);

	return;
}
