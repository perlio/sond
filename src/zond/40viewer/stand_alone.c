/*
 zond (stand_alone.c) - Akten, Beweisstücke, Unterlagen
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

#include "../../gtk3/sond_dialoge.h"
#include "../../misc_stdlib.h"
#include "../../misc_stdlib.h"
#include "../../sond_fileparts.h"
#include "../../sond_log_and_error.h"

#include "../zond_pdf_document.h"

#include "../../znd/anbindung.h"

#include "viewer.h"
#include "viewer_ui.h"
#include "viewer_render.h"
#include "viewer_save.h"
#include "document.h"

static void pv_activate_widgets(PdfViewer *pv, gboolean activ) {
	gtk_widget_set_sensitive(pv->entry, activ);
	gtk_widget_set_sensitive(pv->entry_search, activ);
	gtk_widget_set_sensitive(pv->button_vorher, activ);
	gtk_widget_set_sensitive(pv->button_nachher, activ);
	gtk_widget_set_sensitive(pv->check_diakritika, activ);
	gtk_widget_set_sensitive(pv->item_drehen, activ);
	gtk_widget_set_sensitive(pv->item_einfuegen, activ);
	gtk_widget_set_sensitive(pv->item_loeschen, activ);
	gtk_widget_set_sensitive(pv->item_ocr, activ);

	gtk_widget_set_sensitive(pv->item_schliessen, activ);

	if (!activ) {
		gtk_entry_set_text(GTK_ENTRY(pv->entry), "");
		gtk_entry_set_text(GTK_ENTRY(pv->entry_search), "");
		gtk_label_set_text(GTK_LABEL(pv->label_anzahl), "");
	}

	return;
}

/* Rückgabe 0: Datei wurde tatsächlich geschlossen (pv->dd/arr_pages
 * zurückgesetzt). Rückgabe -1: Anwender hat "Trotzdem schließen?" nach
 * gescheitertem Speichern abgelehnt - die Datei bleibt bewußt unverändert
 * geöffnet (pv->dd zeigt weiter auf das alte Dokument mit den nicht
 * gespeicherten Änderungen). Aufrufer, die danach noch eine neue Datei in
 * denselben pv laden (cb_datei_oeffnen()), MÜSSEN diesen Rückgabewert
 * prüfen - sonst überschreibt viewer_display_document() pv->dd
 * ungeprüft, und die gerade eben geschützten ungespeicherten Änderungen
 * gehen doch verloren. */
static gint pv_schliessen_datei(PdfViewer *pv) {
	gint rc = 0;
	GError *error = NULL;

	viewer_close_thread_pool_and_transfer(pv);

	if (gtk_widget_get_sensitive(pv->button_speichern)) {
		rc = abfrage_frage(pv->vf, "PDF geändert", "Speichern?", NULL);
		if (rc == GTK_RESPONSE_CANCEL)
			return -1;

		if (rc == GTK_RESPONSE_YES) {
			rc = viewer_save_dirty_dds(pv, &error);
			if (rc) {
				error->message = add_string(g_strdup("Dokument kann nicht gespeichert "
						"werden: "), error->message);
				rc = abfrage_frage(pv->vf, error->message, "Trotzdem schließen?", NULL);
				g_error_free(error);

				if (rc != GTK_RESPONSE_YES)
					return -1;
			}
		}

		gtk_widget_set_sensitive(pv->button_speichern, FALSE);
	}

	//Array zurücksetzen
	g_ptr_array_remove_range(pv->arr_pages, 0, pv->arr_pages->len);

	//thumbs leeren
	//treeview bzw. treestore muß zerstört werden, da sonst irgendwie ref auf
	//einen GdkPixbuf kleben bleibt; die wird dann erst aufgegeben,
	//wenn die neuen GdkPixbufs eingefüllt werden
	//dann existiert der fz_context aber schon nicht mehr...
//    gtk_list_store_clear( GTK_LIST_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(pv->tree_thumb) )) );
	gtk_widget_destroy(pv->tree_thumb);
	viewer_ui_recreate_tree_thumb(pv);

	//layout von ViewerImages befreien
	gtk_container_foreach(GTK_CONTAINER(pv->layout),
			(GtkCallback) gtk_widget_destroy, NULL);
	gtk_layout_set_size(GTK_LAYOUT(pv->layout), 0, 0);

	if (pv->dd) {
		viewer_dd_signale_trennen(pv);
		document_free_displayed_documents(pv->dd);
		pv->dd = NULL;
	}

	gtk_header_bar_set_title(GTK_HEADER_BAR(pv->headerbar), "");
	gtk_header_bar_set_subtitle(GTK_HEADER_BAR(pv->headerbar), "");

	pv_activate_widgets(pv, FALSE);

	return 0;
}

static gint pv_oeffnen_datei(PdfViewer *pv, gchar const* path, GError**error) {
	DisplayedDocument *dd = NULL;
	SondFilePart* sfp_pdf_page_tree = NULL;

	sfp_pdf_page_tree = sond_file_part_from_filepart(path, error);
	if (!sfp_pdf_page_tree)
		return -1;

	dd = document_new_displayed_document(SOND_FILE_PART_PDF(sfp_pdf_page_tree),
			NULL, NULL, FALSE, NULL, error);
	g_object_unref(sfp_pdf_page_tree);
	if (!dd)
		return -1;

	viewer_display_document(pv, dd, 0, 0);
	pv_activate_widgets(pv, TRUE);

	return 0;
}

void cb_datei_schliessen(GtkWidget *item, gpointer data) {
	PdfViewer *pv = (PdfViewer*) data;

	pv_schliessen_datei(pv);

	return;
}

void cb_datei_oeffnen(GtkWidget *item, gpointer data) {
	gint rc = 0;
	GError* error = NULL;

	PdfViewer *pv = (PdfViewer*) data;

	if (pv->dd) {
		//Abfrage, ob Datei geschlossen werden soll
		rc = abfrage_frage(pv->vf, "PDF öffnen",
				"Geöffnete PDF-Datei schließen?", NULL);
		if (rc != GTK_RESPONSE_YES)
			return;
	}

	gchar *filename = filename_oeffnen(GTK_WINDOW(pv->vf), NULL);
	if (!filename)
		return;

	//nicht geladene SeaDrive-Datei: erst laden (Abbruch: nichts öffnen)
	if (!pdf_viewer_session_load_file(pv->session, GTK_WINDOW(pv->vf),
			filename)) {
		g_free(filename);

		return;
	}

	if (pv->dd) {
		rc = pv_schliessen_datei(pv);
		if (rc) { //Anwender hat "Trotzdem schließen?" abgelehnt - altes Dokument bleibt geöffnet
			g_free(filename);

			return;
		}
	}
	rc = pv_oeffnen_datei(pv, filename, &error);
	g_free(filename);
	if (rc) {
		display_message(pv->vf, "Fehler - Datei öffnen\n\n"
				"Bei Aufruf pv_oeffnen_datei:\n", error->message, NULL);
		g_error_free(error);
	}

	return;
}

static void open_app(GtkApplication *app, gpointer files, gint n_files,
		gchar *hint, gpointer user_data) {
	gint rc = 0;
	GError* error = NULL;

	PdfViewerSession **session = (PdfViewerSession**) user_data;

	PdfViewer *pv = viewer_init(*session);
	if (!pv)
		return;

	gtk_application_add_window(app, GTK_WINDOW(pv->vf));

	gchar *filename = g_file_get_path(((GFile**)files)[0]);
	if (!filename) {
		display_message(pv->vf, "Fehler - Datei öffnen:\n",
				"Kein lokaler Dateipfad ermittelbar", NULL);
		return;
	}

	rc = pv_oeffnen_datei(pv, filename, &error);
	g_free(filename);
	if (rc) {
		display_message(pv->vf, "Fehler - Datei öffnen:\n", error->message, NULL);
		g_error_free(error);
	}

	return;
}

static void activate_app(GtkApplication *app, gpointer user_data) {
	PdfViewer* pdfv = NULL;

	PdfViewerSession **session = (PdfViewerSession**) user_data;

	pdfv = viewer_init(*session);
	if (!pdfv)
		return;

	gtk_application_add_window(app, GTK_WINDOW(pdfv->vf));

	pv_activate_widgets(pdfv, FALSE);

	return;
}

static GSettings* init_schema(void) {
    GSettingsSchemaSource *source;
    GSettingsSchema *schema;
    GSettings *settings = NULL;
    GError *error = NULL;
    gchar* exe_dir = NULL;
    gchar* path_to_schema_source = NULL;

    exe_dir = get_exe_dir();
    path_to_schema_source = g_build_filename(exe_dir, "../share/glib-2.0/schemas", NULL);
    g_free(exe_dir);

    // Schema-Source aus lokalem Verzeichnis erstellen
    source = g_settings_schema_source_new_from_directory(
        path_to_schema_source,                                // Lokaler Pfad
        g_settings_schema_source_get_default(),        // Parent (Standard-Schemas)
        FALSE,                                         // trusted
        &error
    );
    g_free(path_to_schema_source);

    if (error) {
        LOG_ERROR("Fehler beim Laden der Schemas: %s", error->message);
        g_error_free(error);
        return NULL;
    }

    // Schema lookup
    schema = g_settings_schema_source_lookup(
        source,
        "de.perlio.zondPV",  // Schema-ID
        FALSE                 // recursive
    );
    g_settings_schema_source_unref(source);

    if (!schema) {
        LOG_ERROR("Schema nicht gefunden!");
        return NULL;
    }

	//GSettings
	settings = g_settings_new_full(schema, NULL, NULL);
	g_settings_schema_unref(schema);

	if (!settings)
		LOG_ERROR("Settings konnten nicht erzeugt werden");

	return settings;
}

static PdfViewerSession*
init(GtkApplication *app) {
	fz_context *ctx = NULL;

	ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
	if (!ctx)
		LOG_ERROR("fz_context konnte nicht initialisiert werden");

	SOND_FILE_PART_CLASS(g_type_class_get(SOND_TYPE_FILE_PART))->arr_opened_files =
			g_ptr_array_new( );

	return pdf_viewer_session_new(ctx, init_schema());
}

//Datei-Menü des eigenständigen Viewers: oben im Viewer-Menü einfügen
static void cb_viewer_created(PdfViewerSession *session, PdfViewer *pv,
		gpointer data) {
	GtkWidget *item_oeffnen = gtk_menu_item_new_with_label("Datei öffnen");
	GtkWidget *item_beenden = gtk_menu_item_new_with_label("Beenden");
	GtkWidget *item_sep1 = gtk_separator_menu_item_new();

	pv->item_schliessen = gtk_menu_item_new_with_label("Datei schließen");

	gtk_menu_shell_insert(GTK_MENU_SHELL(pv->menu_viewer), item_oeffnen, 0);
	gtk_menu_shell_insert(GTK_MENU_SHELL(pv->menu_viewer), pv->item_schliessen, 1);
	gtk_menu_shell_insert(GTK_MENU_SHELL(pv->menu_viewer), item_beenden, 2);
	gtk_menu_shell_insert(GTK_MENU_SHELL(pv->menu_viewer), item_sep1, 3);
	gtk_widget_show(item_oeffnen);
	gtk_widget_show(pv->item_schliessen);
	gtk_widget_show(item_beenden);
	gtk_widget_show(item_sep1);

	g_signal_connect(item_oeffnen, "activate", G_CALLBACK(cb_datei_oeffnen), pv);
	g_signal_connect(pv->item_schliessen, "activate", G_CALLBACK(cb_datei_schliessen), pv);
	g_signal_connect_swapped(item_beenden, "activate", G_CALLBACK(viewer_save_and_close), pv);

	return;
}

static void startup_app(GtkApplication *app, gpointer user_data) {
	PdfViewerSession **session = (PdfViewerSession**) user_data;

	*session = init(app);
	g_signal_connect(*session, "viewer-created", G_CALLBACK(cb_viewer_created), NULL);

	return;
}

gint main(gint argc, gchar **argv) {
	PdfViewerSession *session = NULL;
	GtkApplication *app = NULL;

	//ApplicationApp erzeugen
	app = gtk_application_new("de.perlio.zondPV", G_APPLICATION_HANDLES_OPEN);

	//und starten
	g_signal_connect(app, "startup", G_CALLBACK (startup_app), &session);
	g_signal_connect(app, "activate", G_CALLBACK (activate_app), &session);
	g_signal_connect(app, "open", G_CALLBACK (open_app), &session);

	logging_init("zond_viewer");
	install_crash_handler();

	gint status = g_application_run(G_APPLICATION(app), argc, argv);

	g_object_unref(app);

	LOG_INFO("zond_viewer beendet");
	logging_cleanup();

	return status;
}
