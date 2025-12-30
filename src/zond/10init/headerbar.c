/*
 zond (headerbar.c) - Akten, Beweisstücke, Unterlagen
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

#include <gtk/gtk.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <sqlite3.h>
#include <glib/gstdio.h>
#include <tesseract/capi.h>

#include "../../misc.h"
#include "../../sond_ocr.h"
#include "../../sond_fileparts.h"
#include "../../sond_treeview.h"
#include "../../sond_treeviewfm.h"

#include "../zond_pdf_document.h"

#include "../global_types.h"
#include "../zond_tree_store.h"
#include "../zond_treeview.h"
#include "../zond_dbase.h"

#include "../99conv/pdf.h"
#include "../99conv/test.h"
#include "../pdf_ocr.h"

#include "../20allgemein/pdf_text.h"
#include "../20allgemein/ziele.h"
#include "../20allgemein/suchen.h"
#include "../20allgemein/project.h"
#include "../20allgemein/export.h"
#include "../20allgemein/oeffnen.h"
#include "../20allgemein/zond_update.h"

#include "../40viewer/document.h"

#include "app_window.h"

#include "../../misc.h"

/*
 *   Callbacks des Menus "Projekt"
 */
static void cb_item_search_fs_activate(GtkMenuItem *item, gpointer data) {
	gint sel = 0;
	gchar *key = NULL;

	Projekt *zond = (Projekt*) data;

	sel = GPOINTER_TO_INT(g_object_get_data( G_OBJECT(item), "sel" ));

	if (sel
			&& !gtk_toggle_button_get_active(
					GTK_TOGGLE_BUTTON(zond->fs_button)))
		return;
	else if (sel)
		key = "item-search-sel";
	else
		key = "item-search-all";

	gtk_menu_item_activate(
			g_object_get_data(
					G_OBJECT(
							sond_treeview_get_contextmenu(
									zond->treeview[BAUM_FS])), key));

	return;
}

static void cb_menu_datei_beenden_activate(gpointer data) {
	Projekt *zond = (Projekt*) data;

	gboolean ret = FALSE;
	g_signal_emit_by_name(zond->app_window, "delete-event", NULL, &ret);

	return;
}

static GPtrArray*
selection_abfragen_pdf(Projekt *zond, gchar **errmsg) {
	GList *selected = NULL;
	GList *list = NULL;

	GPtrArray *arr_sfp = g_ptr_array_new_with_free_func(
			(GDestroyNotify) g_object_unref);

	if (zond->baum_active == KEIN_BAUM)
		return NULL;

	selected = gtk_tree_selection_get_selected_rows(
			zond->selection[zond->baum_active], NULL);
	if (!selected)
		return NULL;

	list = selected;
	do //alle rows aus der Liste
	{
		gint rc = 0;
		GtkTreeIter iter = { 0, };
		gint node_id = 0;
		gchar *file_part = NULL;
		GError *error = NULL;
		SondFilePart* sfp = NULL;

		if (!gtk_tree_model_get_iter(
				gtk_tree_view_get_model(
						GTK_TREE_VIEW(zond->treeview[zond->baum_active])),
				&iter, list->data)) {
			g_list_free_full(selected, (GDestroyNotify) gtk_tree_path_free);
			g_ptr_array_unref(arr_sfp);

			if (errmsg)
				*errmsg = g_strdup("Bei Aufruf gtk_tree_model_get_iter:\n"
						"Es konnte kein gültiger iter ermittelt werden");

			return NULL;
		}

		gtk_tree_model_get(
				gtk_tree_view_get_model(
						GTK_TREE_VIEW(zond->treeview[zond->baum_active])),
				&iter, 2, &node_id, -1);

		rc = zond_dbase_get_node(zond->dbase_zond->zond_dbase_work, node_id,
				NULL, NULL, &file_part, NULL, NULL, NULL, NULL, &error);
		if (rc) {
			if (errmsg)
				*errmsg = g_strdup_printf("%s\n%s", __func__, error->message);
			g_error_free(error);
			g_list_free_full(selected, (GDestroyNotify) gtk_tree_path_free);
			g_ptr_array_unref(arr_sfp);

			return NULL;
		}

		if (!file_part)
			continue;

		//Sonderbehandung, falls pdf-Datei
		sfp = sond_file_part_from_filepart(zond->ctx, file_part, &error);
		if (!sfp) {
			if (errmsg) *errmsg = g_strdup_printf("%s\n%s", __func__, error->message);
			g_error_free(error);
			g_list_free_full(selected, (GDestroyNotify) gtk_tree_path_free);
			g_ptr_array_unref(arr_sfp);

			return NULL;
		}
		if (SOND_IS_FILE_PART_PDF(sfp) &&
				!g_ptr_array_find(arr_sfp, sfp, NULL))
			g_ptr_array_add(arr_sfp, sfp);
	} while ((list = list->next));

	g_list_free_full(selected, (GDestroyNotify) gtk_tree_path_free);

	return arr_sfp;
}

static void cb_item_clean_pdf(GtkMenuItem *item, gpointer data) {
	Projekt *zond = (Projekt*) data;

	gchar *errmsg = NULL;

	GPtrArray *arr_sfp = selection_abfragen_pdf(zond, &errmsg);

	if (!arr_sfp) {
		if (errmsg) {
			display_message(zond->app_window,
					"PDF kann nicht gereinigt werden\n\nBei "
							"Aufruf selection_abfragen_pdf:\n", errmsg, NULL);
			g_free(errmsg);
		}

		return;
	}

	if (arr_sfp->len == 0) {
		display_message(zond->app_window, "Keine PDF-Datei ausgewählt", NULL);
		g_ptr_array_unref(arr_sfp);

		return;
	}

	for (gint i = 0; i < arr_sfp->len; i++) {
		gint rc = 0;
		GError *error = NULL;

		rc = pdf_clean(zond->ctx, g_ptr_array_index(arr_sfp, i), &error);
		if (rc == -1) {
			display_message(zond->app_window, "PDF ",
					g_ptr_array_index(arr_sfp, i),
					" säubern nicht möglich\n\n", error->message, NULL);
			g_error_free(error);
		}
	}

	g_ptr_array_unref(arr_sfp);

	return;
}

static void cb_item_textsuche(GtkMenuItem *item, gpointer data) {
	Projekt *zond = (Projekt*) data;

	gint rc = 0;
	gchar *errmsg = NULL;
	GArray *arr_pdf_text_occ = NULL;

	GPtrArray *arr_sfp = selection_abfragen_pdf(zond, &errmsg);
	if (!arr_sfp) {
		if (errmsg) {
			display_message(zond->app_window, "Textsuche nicht möglich\n\nBei "
					"Aufruf selection_abfragen_pdf:\n", errmsg, NULL);
			g_free(errmsg);
		}

		return;
	}

	if (arr_sfp->len == 0) {
		display_message(zond->app_window, "Keine PDF-Datei ausgewählt", NULL);
		g_ptr_array_unref(arr_sfp);

		return;
	}

	gchar *search_text = NULL;
	rc = abfrage_frage(zond->app_window, "Textsuche", "Bitte Suchtext eingeben",
			&search_text);
	if (rc != GTK_RESPONSE_YES) {
		g_ptr_array_unref(arr_sfp);

		return;
	}
	if (!g_strcmp0(search_text, "")) {
		g_ptr_array_unref(arr_sfp);
		g_free(search_text);

		return;
	}

	InfoWindow *info_window = NULL;

	info_window = info_window_open(zond->app_window, "Textsuche");

	rc = pdf_textsuche(zond, info_window, arr_sfp, search_text,
			&arr_pdf_text_occ, &errmsg);
	if (rc) {
		display_message(zond->app_window, "Fehler in Textsuche in PDF -\n\n"
				"Bei Aufruf pdf_textsuche:\n", errmsg, NULL);
		g_ptr_array_unref(arr_sfp);
		g_free(errmsg);
		g_free(search_text);
		info_window_close(info_window);

		return;
	}

	info_window_close(info_window);

	if (arr_pdf_text_occ->len == 0) {
		display_message(zond->app_window, "Keine Treffer", NULL);
		g_ptr_array_unref(arr_sfp);
		g_array_unref(arr_pdf_text_occ);
		g_free(search_text);

		return;
	}

	//Anzeigefenster
	rc = pdf_text_anzeigen_ergebnisse(zond, search_text,
			arr_sfp, arr_pdf_text_occ, &errmsg);
	g_ptr_array_unref(arr_sfp);
	g_array_unref(arr_pdf_text_occ);
	if (rc) {
		display_message(zond->app_window, "Fehler in Textsuche in PDF -\n\n"
				"Bei Aufruf pdf_text_anzeigen_ergebnisse:\n", errmsg, NULL);
		g_free(errmsg);
		g_free(search_text);
	}

	g_free(search_text);

	return;
}

static void cb_datei_ocr(GtkMenuItem *item, gpointer data) {
	gint rc = 0;
	gchar* errmsg = NULL;
	InfoWindow *info_window = NULL;
	gchar *message = NULL;
	gchar *datadir = NULL;
	TessBaseAPI *ocr_api = NULL;
	TessBaseAPI *osd_api = NULL;
	GError* error = NULL;
	SondFilePartPDF* sfp_pdf_before = NULL;
	pdf_document* doc = NULL;

	Projekt *zond = (Projekt*) data;

	GPtrArray *arr_sfp = selection_abfragen_pdf(zond, &errmsg);
	if (!arr_sfp) {
		if (errmsg) {
			display_message(zond->app_window,
					"Texterkennung nicht möglich\n\nBei "
							"Aufruf selection_abfragen_pdf:\n", errmsg, NULL);
			g_free(errmsg);
		}

		return;
	}

	if (arr_sfp->len == 0) {
		display_message(zond->app_window, "Keine PDF-Datei ausgewählt", NULL);
		g_ptr_array_unref(arr_sfp);

		return;
	}

	//TessInit
	info_window = info_window_open(zond->app_window, "OCR");

	datadir = g_build_filename(zond->base_dir, "share/tessdata", NULL);
	rc = sond_ocr_init_tesseract(&ocr_api, &osd_api, datadir, &error);
	g_free(datadir);

	for (gint i = 0; i < arr_sfp->len; i++) {
		SondFilePartPDF* sfp_pdf = NULL;
		GError* error = NULL;

		sfp_pdf = g_ptr_array_index(arr_sfp, i);
		if (sfp_pdf != sfp_pdf_before) {
			info_window_set_message(info_window,
					sond_file_part_get_filepart(SOND_FILE_PART(sfp_pdf)));

			//prüfen, ob in Viewer geöffnet
			if (zond_pdf_document_is_open(sfp_pdf)) {
				info_window_set_message(info_window,
						"... in Viewer geöffnet - übersprungen");
				continue;
			}

			pdf_drop_document(zond->ctx, doc);

			doc = sond_file_part_pdf_open_document(zond->ctx, sfp_pdf,
					FALSE, FALSE, FALSE, &error);
			if (!doc) {
				message = g_strdup_printf(
						"Fehler bei Aufruf sond_file_part_pdf_open_document:\n%s",
						error->message);
				g_error_free(error);
				info_window_set_message(info_window, message);
				g_free(message);

				continue;
			}
		}

		rc = sond_ocr_pdf_doc(zond->ctx, doc, sfp_pdf, ocr_api, osd_api, NULL, NULL,
				(void (*)(gpointer, gchar const*, ...)) info_window_set_message,
				(void*) info_window, &error);
		if (rc) {
			info_window_set_message(info_window, "Fehler OCR - \n\n%s", error->message);
			g_error_free(error);

			continue;
		}

		rc = sond_file_part_pdf_save(zond->ctx, doc, sfp_pdf, &error);
	}

	info_window_close(info_window);

	g_ptr_array_unref(arr_sfp);
	TessBaseAPIEnd(ocr_api);
	TessBaseAPIDelete(osd_api);

	return;
}

/*  Callbacks des Menus "Struktur" */
static void cb_item_punkt_einfuegen_activate(GtkMenuItem *item,
		gpointer user_data) {
	gchar *key_item = NULL;

	Projekt *zond = (Projekt*) user_data;

	gboolean child = (gboolean) GPOINTER_TO_INT(
			g_object_get_data( G_OBJECT(item), "kind" ));

	if (zond->baum_active == KEIN_BAUM)
		return;

	if (!child)
		key_item = "item-punkt-einfuegen-ge";
	else
		key_item = "item-punkt-einfuegen-up";

	gtk_menu_item_activate(
			g_object_get_data(
					G_OBJECT(
							sond_treeview_get_contextmenu(
									zond->treeview[zond->baum_active])),
					key_item));

	return;
}

static void cb_kopieren_activate(GtkMenuItem *item, gpointer user_data) {
	Projekt *zond = (Projekt*) user_data;

	if (zond->baum_active == KEIN_BAUM)
		return;

	gtk_menu_item_activate(
			g_object_get_data(
					G_OBJECT(
							sond_treeview_get_contextmenu(
									zond->treeview[zond->baum_active])),
					"item-kopieren"));

	return;
}

static void cb_ausschneiden_activate(GtkMenuItem *item, gpointer user_data) {
	Projekt *zond = (Projekt*) user_data;

	if (zond->baum_active == KEIN_BAUM)
		return;

	gtk_menu_item_activate(
			g_object_get_data(
					G_OBJECT(
							sond_treeview_get_contextmenu(
									zond->treeview[zond->baum_active])),
					"item-ausschneiden"));

	return;
}

static void cb_clipboard_einfuegen_activate(GtkMenuItem *item,
		gpointer user_data) {
	Projekt *zond = (Projekt*) user_data;

	gboolean kind = (gboolean) GPOINTER_TO_INT(
			g_object_get_data( G_OBJECT(item), "kind" ));
	gboolean link = (gboolean) GPOINTER_TO_INT(
			g_object_get_data( G_OBJECT(item), "link" ));

	if (zond->baum_active == KEIN_BAUM)
		return;
	else if (zond->baum_active == BAUM_FS && link)
		return;
	else if (!link) {
		if (!kind)
			gtk_menu_item_activate(
					g_object_get_data(
							G_OBJECT(
									sond_treeview_get_contextmenu(
											zond->treeview[zond->baum_active])),
							"item-paste-ge"));
		else
			gtk_menu_item_activate(
					g_object_get_data(
							G_OBJECT(
									sond_treeview_get_contextmenu(
											zond->treeview[zond->baum_active])),
							"item-paste-up"));
	} else //baum == INHALT oder AUSWERTUNG
	{
		if (!kind)
			gtk_menu_item_activate(
					g_object_get_data(
							G_OBJECT(
									sond_treeview_get_contextmenu(
											zond->treeview[zond->baum_active])),
							"item-paste-as-link-ge"));
		else
			gtk_menu_item_activate(
					g_object_get_data(
							G_OBJECT(
									sond_treeview_get_contextmenu(
											zond->treeview[zond->baum_active])),
							"item-paste-as-link-up"));
	}

	return;
}

static void cb_loeschen_activate(GtkMenuItem *item, gpointer user_data) {
	Projekt *zond = (Projekt*) user_data;

	if (zond->baum_active == KEIN_BAUM)
		return;

	gtk_menu_item_activate(
			g_object_get_data(
					G_OBJECT(
							sond_treeview_get_contextmenu(
									zond->treeview[zond->baum_active])),
					"item-loeschen"));

	return;
}

static void cb_anbindung_entfernenitem_activate(GtkMenuItem *item,
		gpointer user_data) {
	Projekt *zond = (Projekt*) user_data;

	if (zond->baum_active == KEIN_BAUM || zond->baum_active == BAUM_FS)
		return;

	gtk_menu_item_activate(
			g_object_get_data(
					G_OBJECT(
							sond_treeview_get_contextmenu(
									zond->treeview[zond->baum_active])),
					"item-anbindung-entfernen"));

	return;
}

static void cb_jumpitem(GtkMenuItem *item, gpointer data) {
	Projekt *zond = (Projekt*) data;

	//nur bei "richtigen" Bäumen
	if (zond->baum_active != BAUM_INHALT
			&& zond->baum_active != BAUM_AUSWERTUNG)
		return;

	gtk_menu_item_activate(
			g_object_get_data(
					G_OBJECT(
							sond_treeview_get_contextmenu(
									zond->treeview[zond->baum_active])),
					"item-jump"));

	return;
}

static void cb_item_datei_oeffnen(GtkMenuItem *item, gpointer data) {
	Projekt *zond = (Projekt*) data;

	gtk_menu_item_activate(
			g_object_get_data(
					//BAUM_INHALT od. AUSWERTUNG
					G_OBJECT(
							sond_treeview_get_contextmenu(
									zond->treeview[zond->baum_active])),
					"item-datei-oeffnen"));

	return;
}

static void cb_item_datei_oeffnen_mit(GtkMenuItem *item, gpointer data) {
	Projekt *zond = (Projekt*) data;

	gtk_menu_item_activate(
			g_object_get_data(
					//BAUM_INHALT od. AUSWERTUNG
					G_OBJECT(
							sond_treeview_get_contextmenu(
									zond->treeview[zond->baum_active])),
					"item-datei-oeffnen-mit"));

	return;
}

static void cb_change_icon_item(GtkMenuItem *item, gpointer data) {
	gint icon_id = 0;
	Projekt *zond = NULL;
	gchar *key = NULL;

	zond = (Projekt*) data;

	if (zond->baum_active == KEIN_BAUM || zond->baum_active == BAUM_FS)
		return;

	icon_id = GPOINTER_TO_INT(g_object_get_data( G_OBJECT(item), "icon-id" ));

	key = g_strdup_printf("item-menu-icons-%i", icon_id);
	gtk_menu_item_activate(
			g_object_get_data(
					G_OBJECT(
							sond_treeview_get_contextmenu(
									zond->treeview[zond->baum_active])), key));
	g_free(key);

	return;
}

/*  Callbacks des Menus "Ansicht" */

static void cb_alle_erweitern_activated(GtkMenuItem *item, gpointer data) {
	Projekt *zond = (Projekt*) data;

	gtk_tree_view_expand_all(GTK_TREE_VIEW(zond->treeview[zond->baum_active]));

	return;
}

static void cb_aktueller_zweig_erweitern_activated(GtkMenuItem *item,
		gpointer data) {
	GtkTreePath *path;

	Projekt *zond = (Projekt*) data;

	gtk_tree_view_get_cursor(GTK_TREE_VIEW(zond->treeview[zond->baum_active]),
			&path, NULL);
	gtk_tree_view_expand_row(GTK_TREE_VIEW(zond->treeview[zond->baum_active]),
			path, TRUE);

	gtk_tree_path_free(path);

	return;
}

static void cb_reduzieren_activated(GtkMenuItem *item, gpointer data) {
	Projekt *zond = (Projekt*) data;

	gtk_tree_view_collapse_all(
			GTK_TREE_VIEW(zond->treeview[zond->baum_active]));

	return;
}

static void cb_refresh_view_activated(GtkMenuItem *item, gpointer data) {
	GError *error = NULL;
	gint rc = 0;

	Projekt *zond = (Projekt*) data;

	rc = project_load_baeume(zond, &error);
	if (rc == -1) {
		display_message(zond->app_window, "Fehler refresh\n\n", error->message,
				NULL);
		g_error_free(error);

		return;
	}

	return;
}

/*  Callbacks des Menus Extras */
static void cb_menu_test_activate(GtkMenuItem *item, gpointer zond) {
	gint rc = 0;
	gchar *errmsg = NULL;

	rc = test((Projekt*) zond, &errmsg);
	if (rc) {
		display_message(((Projekt*) zond)->app_window, "Test:\n\n", errmsg,
				NULL);
		g_free(errmsg);
	}

	return;
}

static void cb_menu_gemini_einlesen(GtkMenuItem *item, gpointer data) {
	Projekt *zond = (Projekt*) data;

	gint rc = 0;
	gchar *errmsg = NULL;

//    rc = zond_gemini_read_gemini( zond, &errmsg );
	if (rc) {
		display_message(zond->app_window, "Fehler bei Einlesen Gemini\n\n",
				errmsg, NULL);
		g_free(errmsg);
	}

	return;
}

static void cb_menu_gemini_select(GtkWidget *item, gpointer data) {
	Projekt *zond = (Projekt*) data;

	gint rc = 0;
	gchar *errmsg = NULL;

//    rc = zond_gemini_select( zond, &errmsg );
	if (rc) {
		display_message(zond->app_window, "Fehler bei Auswahl Gemini\n\n",
				errmsg, NULL);
		g_free(errmsg);
	}

	return;
}

/*  Callbacks des Menus Einstellungen */
static void cb_settings_zoom(GtkMenuItem *item, gpointer data) {
	gint rc = 0;
	Projekt *zond = (Projekt*) data;

	gchar *text = g_strdup_printf("%.0f",
			g_settings_get_double(zond->settings, "zoom"));
	rc = abfrage_frage(zond->app_window, "Zoom:", "Faktor eingeben", &text);
	if (!g_strcmp0(text, "")) {
		g_free(text);

		return;
	}

	guint zoom = 0;
	rc = string_to_guint(text, &zoom);
	if (rc == 0 && zoom >= ZOOM_MIN && zoom <= ZOOM_MAX)
		g_settings_set_double(zond->settings, "zoom", (gdouble) zoom);
	else
		display_message(zond->app_window, "Eingabe nicht gültig", NULL);

	g_free(text);

	return;
}

static void prefs_autosave_toggled(GtkCheckMenuItem *item, gpointer data) {
	Projekt *zond = (Projekt*) data;

	if (!zond->dbase_zond)
		return;

	if (gtk_check_menu_item_get_active(item))
		g_timeout_add_seconds(10 * 60, project_timeout_autosave, zond);
	else if (!g_source_remove_by_user_data(zond))
		display_message(zond->app_window,
				"autosave-Timeout konnte nicht entfernt werdern", NULL);

	return;
}

static void cb_textview_extra(GtkMenuItem *item, gpointer data) {
	Projekt *zond = (Projekt*) data;

	gtk_widget_show_all(zond->textview_window);

	//Menüpunkt deaktivieren
	gtk_widget_set_sensitive(zond->menu.textview_extra, FALSE);

	//TextView-extra laden
	zond->node_id_extra = zond->node_id_act;
	gtk_text_view_set_buffer(GTK_TEXT_VIEW(zond->textview_ii),
			gtk_text_view_get_buffer(GTK_TEXT_VIEW(zond->textview)));

	return;
}

static void headerbar_hilfe_about(GtkWidget *item, gpointer data) {
	Projekt *zond = (Projekt*) data;

	display_message(zond->app_window, "Version: " MAJOR "." MINOR "." PATCH,
			NULL);

	return;
}

static void headerbar_hilfe_update(GtkWidget *item, gpointer data) {
	gint rc = 0;
	GError *error = NULL;
	InfoWindow *info_window = NULL;

	Projekt *zond = (Projekt*) data;

	info_window = info_window_open(zond->app_window, "Zond Updater");

	rc = zond_update(zond, info_window, &error);

	info_window_kill(info_window);

	if (rc == -1) {
		display_message(zond->app_window, "Update fehlgeschlagen\n\n",
				error->message, NULL);
		g_error_free(error);
	} else if (rc == 1)
		display_message(zond->app_window, "Aktuelle Version installiert", NULL);

	return;
}

/*  Funktion init_menu - ganze Kopfzeile! */
static GtkWidget*
init_menu(Projekt *zond) {
	GtkAccelGroup *accel_group = gtk_accel_group_new();
	gtk_window_add_accel_group(GTK_WINDOW(zond->app_window), accel_group);

	/*  Menubar */
	GtkWidget *menubar = gtk_menu_bar_new();

	zond->menu.projekt = gtk_menu_item_new_with_label("Projekt");
	zond->menu.struktur = gtk_menu_item_new_with_label("Bearbeiten");
	zond->menu.pdf = gtk_menu_item_new_with_label("PDF-Dateien");
	zond->menu.ansicht = gtk_menu_item_new_with_label("Ansicht");
	zond->menu.extras = gtk_menu_item_new_with_label("Extras");
	GtkWidget *einstellungen = gtk_menu_item_new_with_label("Einstellungen");
	GtkWidget *hilfeitem = gtk_menu_item_new_with_label("Hilfe");

	//In die Menuleiste einfügen
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), zond->menu.projekt);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), zond->menu.struktur);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), zond->menu.pdf);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), zond->menu.ansicht);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), zond->menu.extras);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), einstellungen);
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar), hilfeitem);

	/*********************
	 *  Menu Projekt
	 *********************/
	GtkWidget *projektmenu = gtk_menu_new();

	GtkWidget *neuitem = gtk_menu_item_new_with_label("Neu");
	g_signal_connect(G_OBJECT(neuitem), "activate",
			G_CALLBACK(cb_menu_datei_neu_activate), (gpointer ) zond);

	GtkWidget *oeffnenitem = gtk_menu_item_new_with_label("Öffnen");
	g_signal_connect(G_OBJECT(oeffnenitem), "activate",
			G_CALLBACK(cb_menu_datei_oeffnen_activate), (gpointer ) zond);

	zond->menu.speichernitem = gtk_menu_item_new_with_label("Speichern");
	g_signal_connect(G_OBJECT(zond->menu.speichernitem), "activate",
			G_CALLBACK(cb_menu_datei_speichern_activate), (gpointer ) zond);

	zond->menu.schliessenitem = gtk_menu_item_new_with_label("Schliessen");
	g_signal_connect(G_OBJECT(zond->menu.schliessenitem), "activate",
			G_CALLBACK(cb_menu_datei_schliessen_activate), (gpointer ) zond);

	GtkWidget *sep_projekt1item = gtk_separator_menu_item_new();

	zond->menu.exportitem = gtk_menu_item_new_with_label(
			"Export als odt-Dokument");
	GtkWidget *exportmenu = gtk_menu_new();
	GtkWidget *ganze_struktur = gtk_menu_item_new_with_label("Ganze Struktur");
	g_object_set_data(G_OBJECT(ganze_struktur), "umfang", GINT_TO_POINTER(1));
	g_signal_connect(ganze_struktur, "activate",
			G_CALLBACK(cb_menu_datei_export_activate), (gpointer ) zond);
	GtkWidget *ausgewaehlte_punkte = gtk_menu_item_new_with_label(
			"Gewählte Zweige");
	g_object_set_data(G_OBJECT(ausgewaehlte_punkte), "umfang",
			GINT_TO_POINTER(2));
	g_signal_connect(ausgewaehlte_punkte, "activate",
			G_CALLBACK(cb_menu_datei_export_activate), (gpointer ) zond);
	GtkWidget *ausgewaehlte_zweige = gtk_menu_item_new_with_label(
			"Gewählte Punkte");
	g_object_set_data(G_OBJECT(ausgewaehlte_zweige), "umfang",
			GINT_TO_POINTER(3));
	g_signal_connect(ausgewaehlte_zweige, "activate",
			G_CALLBACK(cb_menu_datei_export_activate), (gpointer ) zond);
	gtk_menu_shell_append(GTK_MENU_SHELL(exportmenu), ganze_struktur);
	gtk_menu_shell_append(GTK_MENU_SHELL(exportmenu), ausgewaehlte_punkte);
	gtk_menu_shell_append(GTK_MENU_SHELL(exportmenu), ausgewaehlte_zweige);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(zond->menu.exportitem), exportmenu);

	//Durchsuchen
	zond->menu.item_search_fs = gtk_menu_item_new_with_label(
			"Projektverzeichnis durchsuchen");

	GtkWidget *menu_search_fs = gtk_menu_new();

	GtkWidget *item_all = gtk_menu_item_new_with_label("Vollständig");
	g_signal_connect(item_all, "activate",
			G_CALLBACK(cb_item_search_fs_activate), (gpointer ) zond);

	GtkWidget *item_sel = gtk_menu_item_new_with_label("Ausgewählte Punkte");
	g_object_set_data(G_OBJECT(item_sel), "sel", GINT_TO_POINTER(1));
	g_signal_connect(item_sel, "activate",
			G_CALLBACK(cb_item_search_fs_activate), (gpointer ) zond);

	gtk_menu_shell_append(GTK_MENU_SHELL(menu_search_fs), item_all);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_search_fs), item_sel);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(zond->menu.item_search_fs),
			menu_search_fs);

	GtkWidget *sep_projekt1item_2 = gtk_separator_menu_item_new();

	GtkWidget *beendenitem = gtk_menu_item_new_with_label("Beenden");
	gtk_widget_add_accelerator(beendenitem, "activate", accel_group, GDK_KEY_q,
			GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	g_signal_connect_swapped(beendenitem, "activate",
			G_CALLBACK(cb_menu_datei_beenden_activate), (gpointer ) zond);

	gtk_menu_shell_append(GTK_MENU_SHELL(projektmenu), neuitem);
	gtk_menu_shell_append(GTK_MENU_SHELL(projektmenu), oeffnenitem);
	gtk_menu_shell_append(GTK_MENU_SHELL(projektmenu),
			zond->menu.speichernitem);
	gtk_menu_shell_append(GTK_MENU_SHELL(projektmenu),
			zond->menu.schliessenitem);
	gtk_menu_shell_append(GTK_MENU_SHELL(projektmenu), sep_projekt1item);
	gtk_menu_shell_append(GTK_MENU_SHELL(projektmenu), zond->menu.exportitem);
	gtk_menu_shell_append(GTK_MENU_SHELL(projektmenu),
			zond->menu.item_search_fs);
	gtk_menu_shell_append(GTK_MENU_SHELL(projektmenu), sep_projekt1item_2);
	gtk_menu_shell_append(GTK_MENU_SHELL(projektmenu), beendenitem);

	/*********************
	 *  Menu Struktur
	 *********************/
	GtkWidget *strukturmenu = gtk_menu_new();

	//Punkt erzeugen
	GtkWidget *punkterzeugenitem = gtk_menu_item_new_with_label(
			"Punkt einfügen");

	GtkWidget *punkterzeugenmenu = gtk_menu_new();

	GtkWidget *ge_punkterzeugenitem = gtk_menu_item_new_with_label(
			"Gleiche Ebene");
	gtk_widget_add_accelerator(ge_punkterzeugenitem, "activate", accel_group,
	GDK_KEY_p, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	g_signal_connect(G_OBJECT(ge_punkterzeugenitem), "activate",
			G_CALLBACK(cb_item_punkt_einfuegen_activate), (gpointer ) zond);

	GtkWidget *up_punkterzeugenitem = gtk_menu_item_new_with_label(
			"Unterebene");
	g_object_set_data(G_OBJECT(up_punkterzeugenitem), "kind",
			GINT_TO_POINTER(1));
	gtk_widget_add_accelerator(up_punkterzeugenitem, "activate", accel_group,
	GDK_KEY_p, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);
	g_signal_connect(G_OBJECT(up_punkterzeugenitem), "activate",
			G_CALLBACK(cb_item_punkt_einfuegen_activate), (gpointer ) zond);

	gtk_menu_shell_append(GTK_MENU_SHELL(punkterzeugenmenu),
			ge_punkterzeugenitem);
	gtk_menu_shell_append(GTK_MENU_SHELL(punkterzeugenmenu),
			up_punkterzeugenitem);

	gtk_menu_item_set_submenu(GTK_MENU_ITEM(punkterzeugenitem),
			punkterzeugenmenu);

	//Datei öffnen
	GtkWidget *item_datei_oeffnen = gtk_menu_item_new_with_label("Öffnen");
	gtk_widget_add_accelerator(item_datei_oeffnen, "activate", accel_group,
	GDK_KEY_o, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	g_signal_connect(G_OBJECT(item_datei_oeffnen), "activate",
			G_CALLBACK(cb_item_datei_oeffnen), (gpointer ) zond);

	//Datei öffnen
	GtkWidget *item_datei_oeffnen_mit = gtk_menu_item_new_with_label(
			"Öffnen mit");
	g_signal_connect(G_OBJECT(item_datei_oeffnen_mit), "activate",
			G_CALLBACK(cb_item_datei_oeffnen_mit), (gpointer ) zond);

	//Icons ändern
	GtkWidget *icon_change_item = gtk_menu_item_new_with_label("Icon ändern");

	GtkWidget *icon_change_menu = gtk_menu_new();

	for (gint i = 0; i < NUMBER_OF_ICONS; i++) {
		GtkWidget *icon = gtk_image_new_from_icon_name(zond->icon[i].icon_name,
				GTK_ICON_SIZE_MENU);
		GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
		GtkWidget *label = gtk_label_new(zond->icon[i].display_name);
		GtkWidget *menu_item = gtk_menu_item_new();
		gtk_container_add(GTK_CONTAINER(box), icon);
		gtk_container_add(GTK_CONTAINER(box), label);
		gtk_container_add(GTK_CONTAINER(menu_item), box);

		g_object_set_data(G_OBJECT(menu_item), "icon-id", GINT_TO_POINTER(i));
		g_signal_connect(menu_item, "activate", G_CALLBACK(cb_change_icon_item),
				(gpointer ) zond);

		gtk_menu_shell_append(GTK_MENU_SHELL(icon_change_menu), menu_item);
	}

	gtk_menu_item_set_submenu(GTK_MENU_ITEM(icon_change_item),
			icon_change_menu);

	GtkWidget *sep_struktur0item = gtk_separator_menu_item_new();

	//Kopieren
	GtkWidget *kopierenitem = gtk_menu_item_new_with_label("Kopieren");
	gtk_widget_add_accelerator(kopierenitem, "activate", accel_group, GDK_KEY_c,
			GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	g_signal_connect(G_OBJECT(kopierenitem), "activate",
			G_CALLBACK(cb_kopieren_activate), (gpointer ) zond);

	//Verschieben
	GtkWidget *ausschneidenitem = gtk_menu_item_new_with_label("Ausschneiden");
	gtk_widget_add_accelerator(ausschneidenitem, "activate", accel_group,
	GDK_KEY_x, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	g_object_set_data(G_OBJECT(ausschneidenitem), "ausschneiden",
			GINT_TO_POINTER(1));
	g_signal_connect(G_OBJECT(ausschneidenitem), "activate",
			G_CALLBACK(cb_ausschneiden_activate), (gpointer ) zond);

	//Einfügen
	GtkWidget *pasteitem = gtk_menu_item_new_with_label("Einfügen");
	GtkWidget *pastemenu = gtk_menu_new();
	GtkWidget *alspunkt_einfuegenitem = gtk_menu_item_new_with_label(
			"Gleiche Ebene");
	GtkWidget *alsunterpunkt_einfuegenitem = gtk_menu_item_new_with_label(
			"Unterebene");
	gtk_menu_shell_append(GTK_MENU_SHELL(pastemenu), alspunkt_einfuegenitem);
	gtk_widget_add_accelerator(alspunkt_einfuegenitem, "activate", accel_group,
	GDK_KEY_v, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_menu_shell_append(GTK_MENU_SHELL(pastemenu),
			alsunterpunkt_einfuegenitem);
	g_object_set_data(G_OBJECT(alsunterpunkt_einfuegenitem), "kind",
			GINT_TO_POINTER(1));

	gtk_widget_add_accelerator(alsunterpunkt_einfuegenitem, "activate",
			accel_group, GDK_KEY_v, GDK_CONTROL_MASK | GDK_SHIFT_MASK,
			GTK_ACCEL_VISIBLE);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(pasteitem), pastemenu);

	g_signal_connect(G_OBJECT(alspunkt_einfuegenitem), "activate",
			G_CALLBACK(cb_clipboard_einfuegen_activate), (gpointer ) zond);
	g_signal_connect(G_OBJECT(alsunterpunkt_einfuegenitem), "activate",
			G_CALLBACK(cb_clipboard_einfuegen_activate), (gpointer ) zond);

	//Link Einfügen
	GtkWidget *pasteitem_link = gtk_menu_item_new_with_label(
			"Als Link einfügen");

	GtkWidget *pastemenu_link = gtk_menu_new();
	GtkWidget *alspunkt_einfuegenitem_link = gtk_menu_item_new_with_label(
			"Gleiche Ebene");
	GtkWidget *alsunterpunkt_einfuegenitem_link = gtk_menu_item_new_with_label(
			"Unterebene");
	gtk_menu_shell_append(GTK_MENU_SHELL(pastemenu_link),
			alspunkt_einfuegenitem_link);
	gtk_widget_add_accelerator(alspunkt_einfuegenitem_link, "activate",
			accel_group,
			GDK_KEY_l, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	gtk_widget_add_accelerator(alsunterpunkt_einfuegenitem_link, "activate",
			accel_group,
			GDK_KEY_l, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);
	gtk_menu_shell_append(GTK_MENU_SHELL(pastemenu_link),
			alsunterpunkt_einfuegenitem_link);
	g_object_set_data(G_OBJECT(alsunterpunkt_einfuegenitem_link), "kind",
			GINT_TO_POINTER(1));
	g_object_set_data(G_OBJECT(alsunterpunkt_einfuegenitem_link), "link",
			GINT_TO_POINTER(1));
	g_object_set_data(G_OBJECT(alspunkt_einfuegenitem_link), "link",
			GINT_TO_POINTER(1));
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(pasteitem_link), pastemenu_link);

	GtkWidget *sep_struktur1item = gtk_separator_menu_item_new();

	g_signal_connect(G_OBJECT(alspunkt_einfuegenitem_link), "activate",
			G_CALLBACK(cb_clipboard_einfuegen_activate), (gpointer ) zond);
	g_signal_connect(G_OBJECT(alsunterpunkt_einfuegenitem_link), "activate",
			G_CALLBACK(cb_clipboard_einfuegen_activate), (gpointer ) zond);

	GtkWidget *sep_struktur2item = gtk_separator_menu_item_new();

	//Punkt(e) löschen
	GtkWidget *loeschenitem = gtk_menu_item_new_with_label("Löschen");
	gtk_widget_add_accelerator(loeschenitem, "activate", accel_group,
	GDK_KEY_Delete, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	g_signal_connect(G_OBJECT(loeschenitem), "activate",
			G_CALLBACK(cb_loeschen_activate), (gpointer ) zond);

	//Speichern als Projektdatei
	GtkWidget *anbindung_entfernenitem = gtk_menu_item_new_with_label(
			"Anbindung entfernen");
	g_signal_connect(G_OBJECT(anbindung_entfernenitem), "activate",
			G_CALLBACK(cb_anbindung_entfernenitem_activate), zond);

	GtkWidget *suchenitem = gtk_menu_item_new_with_label("Suchen");
	g_signal_connect_swapped(suchenitem, "activate",
			G_CALLBACK(gtk_popover_popup), zond->popover);

	GtkWidget *jumpitem = gtk_menu_item_new_with_label("Zu Ursprung springen");
	gtk_widget_add_accelerator(jumpitem, "activate", accel_group,
	GDK_KEY_j, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
	g_signal_connect(jumpitem, "activate", G_CALLBACK(cb_jumpitem), zond);

	GtkWidget *sep_struktur3item = gtk_separator_menu_item_new();

	//Menus "Bearbeiten" anbinden
//Menus in dateienmenu
	gtk_menu_shell_append(GTK_MENU_SHELL(strukturmenu), punkterzeugenitem);
	gtk_menu_shell_append(GTK_MENU_SHELL(strukturmenu), sep_struktur0item);
	gtk_menu_shell_append(GTK_MENU_SHELL(strukturmenu), kopierenitem);
	gtk_menu_shell_append(GTK_MENU_SHELL(strukturmenu), ausschneidenitem);
	gtk_menu_shell_append(GTK_MENU_SHELL(strukturmenu), pasteitem);
	gtk_menu_shell_append(GTK_MENU_SHELL(strukturmenu), pasteitem_link);
	gtk_menu_shell_append(GTK_MENU_SHELL(strukturmenu), sep_struktur1item);
	gtk_menu_shell_append(GTK_MENU_SHELL(strukturmenu), loeschenitem);
	gtk_menu_shell_append(GTK_MENU_SHELL(strukturmenu),
			anbindung_entfernenitem);
	gtk_menu_shell_append(GTK_MENU_SHELL(strukturmenu), sep_struktur2item);
	gtk_menu_shell_append(GTK_MENU_SHELL(strukturmenu), icon_change_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(strukturmenu), sep_struktur3item);
	gtk_menu_shell_append(GTK_MENU_SHELL(strukturmenu), suchenitem);
	gtk_menu_shell_append(GTK_MENU_SHELL(strukturmenu), jumpitem);
	gtk_menu_shell_append(GTK_MENU_SHELL(strukturmenu), item_datei_oeffnen);
	gtk_menu_shell_append(GTK_MENU_SHELL(strukturmenu), item_datei_oeffnen_mit);

	/*********************
	 *  Menu Pdf-Dateien
	 *********************/
	GtkWidget *menu_dateien = gtk_menu_new();

	GtkWidget *item_sep_dateien0 = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_dateien), item_sep_dateien0);

	//PDF reparieren
	GtkWidget *item_clean_pdf = gtk_menu_item_new_with_label("PDF reparieren");
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_dateien), item_clean_pdf);
	g_signal_connect(item_clean_pdf, "activate", G_CALLBACK(cb_item_clean_pdf),
			zond);

	//Text-Suche
	GtkWidget *item_textsuche = gtk_menu_item_new_with_label("Text suchen");
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_dateien), item_textsuche);
	g_signal_connect(item_textsuche, "activate", G_CALLBACK(cb_item_textsuche),
			zond);

	GtkWidget *item_ocr = gtk_menu_item_new_with_label("OCR");
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_dateien), item_ocr);
	g_signal_connect(item_ocr, "activate", G_CALLBACK(cb_datei_ocr), zond);

	/*  Menu Ansicht */
	GtkWidget *ansichtmenu = gtk_menu_new();

	//Erweitern
	GtkWidget *erweiternitem = gtk_menu_item_new_with_label("Erweitern");

	GtkWidget *erweiternmenu = gtk_menu_new();
	GtkWidget *alle_erweiternitem = gtk_menu_item_new_with_label(
			"Ganze Struktur");
	GtkWidget *aktuellerzweig_erweiternitem = gtk_menu_item_new_with_label(
			"Aktueller Zweig");
	gtk_menu_shell_append(GTK_MENU_SHELL(erweiternmenu), alle_erweiternitem);
	gtk_menu_shell_append(GTK_MENU_SHELL(erweiternmenu),
			aktuellerzweig_erweiternitem);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(erweiternitem), erweiternmenu);

	g_signal_connect(G_OBJECT(alle_erweiternitem), "activate",
			G_CALLBACK(cb_alle_erweitern_activated), (gpointer ) zond);
	g_signal_connect(G_OBJECT(aktuellerzweig_erweiternitem), "activate",
			G_CALLBACK(cb_aktueller_zweig_erweitern_activated),
			(gpointer ) zond);

	//Alle reduzieren
	GtkWidget *einklappenitem = gtk_menu_item_new_with_label("Alle reduzieren");
	g_signal_connect(einklappenitem, "activate",
			G_CALLBACK(cb_reduzieren_activated), (gpointer ) zond);

	GtkWidget *sep_ansicht1item = gtk_separator_menu_item_new();

	//refresh view
	GtkWidget *refreshitem = gtk_menu_item_new_with_label("Refresh");
	g_signal_connect(refreshitem, "activate",
			G_CALLBACK(cb_refresh_view_activated), (gpointer ) zond);

	GtkWidget *sep_item_2 = gtk_separator_menu_item_new();

	//Menu-Punkt, mit dem Textfenster losgelöst werden kann
	//zunächst inaktiv, da bei Start auf baun_inhalt
	zond->menu.textview_extra = gtk_menu_item_new_with_label("Textfenster");
	gtk_widget_set_sensitive(zond->menu.textview_extra, FALSE);
	g_signal_connect(zond->menu.textview_extra, "activate",
			G_CALLBACK(cb_textview_extra), zond);

	gtk_menu_shell_append(GTK_MENU_SHELL(ansichtmenu), erweiternitem);
	gtk_menu_shell_append(GTK_MENU_SHELL(ansichtmenu), einklappenitem);
	gtk_menu_shell_append(GTK_MENU_SHELL(ansichtmenu), sep_ansicht1item);
	gtk_menu_shell_append(GTK_MENU_SHELL(ansichtmenu), refreshitem);
	gtk_menu_shell_append(GTK_MENU_SHELL(ansichtmenu), sep_item_2);
	gtk_menu_shell_append(GTK_MENU_SHELL(ansichtmenu),
			zond->menu.textview_extra);

	/*  Menu Extras */
	GtkWidget *extrasmenu = gtk_menu_new();

	//Gemini
	GtkWidget *item_gemini_read = gtk_menu_item_new_with_label(
			"Gemini-Ausdruck einlesen");
	g_signal_connect(item_gemini_read, "activate",
			G_CALLBACK(cb_menu_gemini_einlesen), (gpointer ) zond);

	GtkWidget *item_gemini_select = gtk_menu_item_new_with_label(
			"Gemini-Protokolle anzeigen");
	g_signal_connect(item_gemini_select, "activate",
			G_CALLBACK(cb_menu_gemini_select), (gpointer ) zond);

	//Test
	GtkWidget *testitem = gtk_menu_item_new_with_label("Test");
	g_signal_connect(testitem, "activate", G_CALLBACK(cb_menu_test_activate),
			(gpointer ) zond);

	gtk_menu_shell_append(GTK_MENU_SHELL(extrasmenu), item_gemini_read);
	gtk_menu_shell_append(GTK_MENU_SHELL(extrasmenu), item_gemini_select);
	gtk_menu_shell_append(GTK_MENU_SHELL(extrasmenu), testitem);

	/*  Menu Einstellungen */
	GtkWidget *einstellungenmenu = gtk_menu_new();

	GtkWidget *zoom_item = gtk_menu_item_new_with_label("Zoom Interner Viewer");
	g_signal_connect(zoom_item, "activate", G_CALLBACK(cb_settings_zoom), zond);
	GtkWidget *prefs_autosave = gtk_check_menu_item_new_with_label(
			"Automatisches Speichern");
	g_signal_connect(prefs_autosave, "toggled",
			G_CALLBACK(prefs_autosave_toggled), zond);
	g_settings_bind(zond->settings, "autosave", prefs_autosave, "active",
			G_SETTINGS_BIND_DEFAULT);

	gtk_menu_shell_append(GTK_MENU_SHELL(einstellungenmenu), zoom_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(einstellungenmenu), prefs_autosave);

	/*  Menu Hilfe  */
	GtkWidget *hilfemenu = gtk_menu_new();

	GtkWidget *hilfe_about = gtk_menu_item_new_with_label("Über");
	gtk_menu_shell_append(GTK_MENU_SHELL(hilfemenu), hilfe_about);
	g_signal_connect(hilfe_about, "activate", G_CALLBACK(headerbar_hilfe_about),
			zond);

	GtkWidget *hilfe_update = gtk_menu_item_new_with_label("Update");
	gtk_menu_shell_append(GTK_MENU_SHELL(hilfemenu), hilfe_update);
	g_signal_connect(hilfe_update, "activate",
			G_CALLBACK(headerbar_hilfe_update), zond);

	/*  Gesamtmenu:
	 *   Die erzeugten Menus als Untermenu der Menuitems aus der menubar
	 */
	// An menu aus menubar anbinden
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(zond->menu.projekt), projektmenu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(zond->menu.pdf), menu_dateien);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(zond->menu.struktur), strukturmenu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(zond->menu.ansicht), ansichtmenu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(zond->menu.extras), extrasmenu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(einstellungen), einstellungenmenu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(hilfeitem), hilfemenu);

	return menubar;
}

static void cb_button_mode_toggled(GtkToggleButton *button, gpointer data) {
	Projekt *zond = (Projekt*) data;

	if (gtk_toggle_button_get_active(button)) {
		GtkWidget *baum_fs = NULL;
		GtkWidget *baum_auswertung = NULL;

		baum_fs = gtk_paned_get_child1(GTK_PANED(zond->hpaned));
		gtk_widget_show_all(baum_fs);

		baum_auswertung = gtk_paned_get_child2(
				GTK_PANED(gtk_paned_get_child2( GTK_PANED(zond->hpaned) )));
		gtk_widget_hide(baum_auswertung);
	} else {
		//baum_inhalt und baum_auswertung anzeigen
		//zwischenspeichern
		//leeren
		GtkWidget *baum_fs = NULL;
		GtkWidget *baum_auswertung = NULL;

		baum_fs = gtk_paned_get_child1(GTK_PANED(zond->hpaned));
		gtk_widget_hide(baum_fs);

		baum_auswertung = gtk_paned_get_child2(
				GTK_PANED(gtk_paned_get_child2( GTK_PANED(zond->hpaned) )));
		gtk_widget_show_all(baum_auswertung);
	}

	return;
}

void init_headerbar(Projekt *zond) {
	//Menu erzeugen
	GtkWidget *menubar = init_menu(zond);

	//HeaderBar erzeugen
	GtkWidget *headerbar = gtk_header_bar_new();
	gtk_header_bar_set_has_subtitle(GTK_HEADER_BAR(headerbar), FALSE);
	gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(headerbar),
			":minimize,maximize,close");
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);

	//Umschaltknopf erzeugen
	zond->fs_button = gtk_toggle_button_new_with_label("FS");
	g_signal_connect(zond->fs_button, "toggled",
			G_CALLBACK(cb_button_mode_toggled), zond);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(headerbar), zond->fs_button);

	//alles in Headerbar packen
	gtk_header_bar_pack_start(GTK_HEADER_BAR(headerbar), menubar);

	//HeaderBar anzeigen
	gtk_window_set_titlebar(GTK_WINDOW(zond->app_window), headerbar);

	return;
}
