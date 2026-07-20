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

#include "../../misc.h"
#include "../../misc_stdlib.h"
#include "../../sond_fileparts.h"
#include "../../sond_log_and_error.h"

#include "../zond_pdf_document.h"

#include "../99conv/general.h"

#include "viewer.h"
#include "viewer_ui.h"
#include "viewer_render.h"
#include "document.h"

static void pv_activate_widgets(PdfViewer *pv, gboolean activ) {
	gtk_widget_set_sensitive(pv->entry, activ);
	gtk_widget_set_sensitive(pv->entry_search, activ);
	gtk_widget_set_sensitive(pv->button_vorher, activ);
	gtk_widget_set_sensitive(pv->button_nachher, activ);
	gtk_widget_set_sensitive(pv->item_drehen, activ);
	gtk_widget_set_sensitive(pv->item_einfuegen, activ);
	gtk_widget_set_sensitive(pv->item_loeschen, activ);
	gtk_widget_set_sensitive(pv->item_entnehmen, activ);
	gtk_widget_set_sensitive(pv->item_ocr, activ);

	gtk_widget_set_sensitive(pv->item_schliessen, activ);

	if (!activ) {
		gtk_entry_set_text(GTK_ENTRY(pv->entry), "");
		gtk_entry_set_text(GTK_ENTRY(pv->entry_search), "");
		gtk_label_set_text(GTK_LABEL(pv->label_anzahl), "");
	}

	return;
}

static void pv_schliessen_datei(PdfViewer *pv) {
	gint rc = 0;
	GError *error = NULL;

	viewer_close_thread_pool_and_transfer(pv);

	if (gtk_widget_get_sensitive(pv->button_speichern)) {
		rc = abfrage_frage(pv->vf, "PDF geändert", "Speichern?", NULL);
		if (rc == GTK_RESPONSE_YES) {
			rc = viewer_save_dirty_dds(pv, &error);
			if (rc) {
				error->message = add_string(g_strdup("Dokument kann nicht gespeichert "
						"werden: "), error->message);
				rc = abfrage_frage(pv->vf, error->message, "Trotzdem schließen?", NULL);
				g_error_free(error);

				if (rc == GTK_RESPONSE_NO)
					return;
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
	pv->tree_thumb = gtk_tree_view_new();
	GtkListStore *list_store = gtk_list_store_new(1, GDK_TYPE_PIXBUF);
	gtk_tree_view_set_model(GTK_TREE_VIEW(pv->tree_thumb),
			GTK_TREE_MODEL(list_store));
	g_object_unref(list_store);

	//layout von ViewerImages befreien
	gtk_container_foreach(GTK_CONTAINER(pv->layout),
			(GtkCallback) gtk_widget_destroy, NULL);
	gtk_layout_set_size(GTK_LAYOUT(pv->layout), 0, 0);

	if (pv->dd) {
		document_free_displayed_documents(pv->dd);
		pv->dd = NULL;
	}

	gtk_header_bar_set_title(GTK_HEADER_BAR(pv->headerbar), "");
	gtk_header_bar_set_subtitle(GTK_HEADER_BAR(pv->headerbar), "");

	pv_activate_widgets(pv, FALSE);

	return;
}

static gint pv_oeffnen_datei(PdfViewer *pv, gchar const* path, GError**error) {
	DisplayedDocument *dd = NULL;
	SondFilePart* sfp_pdf_page_tree = NULL;

	sfp_pdf_page_tree = sond_file_part_from_filepart(path, error);
	if (!sfp_pdf_page_tree)
		return -1;

	dd = document_new_displayed_document(SOND_FILE_PART_PDF(sfp_pdf_page_tree),
			NULL, NULL, FALSE, NULL, error);
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

	gchar *filename = filename_oeffnen(GTK_WINDOW(pv->vf));
	if (!filename)
		return;

	if (pv->dd)
		pv_schliessen_datei(pv);
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

	Projekt **zond = (Projekt**) user_data;

	PdfViewer *pv = viewer_init(*zond);
	if (!pv)
		return;

	gtk_application_add_window(app, GTK_WINDOW(pv->vf));

	gchar *uri = g_file_get_uri(((GFile**)files)[0]);
	gchar *uri_unesc = g_uri_unescape_string(uri, NULL);
	g_free(uri);

	rc = pv_oeffnen_datei(pv, uri_unesc + 8, &error);
	g_free(uri_unesc);
	if (rc) {
		display_message(pv->vf, "Fehler - Datei öffnen:\n", error->message, NULL);
		g_error_free(error);
	}

	return;
}

static void activate_app(GtkApplication *app, gpointer user_data) {
	PdfViewer* pdfv = NULL;

	Projekt **zond = (Projekt**) user_data;

	pdfv = viewer_init(*zond);
	if (!pdfv)
		return;

	gtk_application_add_window(app, GTK_WINDOW(pdfv->vf));

	pv_activate_widgets(pdfv, FALSE);

	return;
}

static void init_schema(Projekt* zond) {
    GSettingsSchemaSource *source;
    GSettingsSchema *schema;
    GError *error = NULL;
    gchar* path_to_schema_source = NULL;

    path_to_schema_source = g_build_filename(zond->exe_dir, "../share/glib-2.0/schemas", NULL);

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
        return;
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
        return;
    }

	//GSettings
	zond->settings = g_settings_new_full(schema, NULL, NULL);
	g_settings_schema_unref(schema);

	if (!zond->settings)
		LOG_ERROR("Settings konnten nicht erzeugt werden");

	return;
}

static void
init(GtkApplication *app, Projekt *zond) {
	zond->arr_pv = g_ptr_array_new();
	zond->exe_dir = get_exe_dir();
	zond->ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
	if (!zond->ctx)
		LOG_ERROR("fz_context konnte nicht initialisiert werden");

	init_schema(zond);

	SOND_FILE_PART_CLASS(g_type_class_get(SOND_TYPE_FILE_PART))->arr_opened_files =
			g_ptr_array_new( );

	return;
}

static void startup_app(GtkApplication *app, gpointer user_data) {
	Projekt **zond = (Projekt**) user_data;

	*zond = g_malloc0(sizeof(Projekt));

	init(app, *zond);

	return;
}

gint main(gint argc, gchar **argv) {
	Projekt *zond = NULL;
	GtkApplication *app = NULL;

	//ApplicationApp erzeugen
	app = gtk_application_new("de.perlio.zondPV", G_APPLICATION_HANDLES_OPEN);

	//und starten
	g_signal_connect(app, "startup", G_CALLBACK (startup_app), &zond);
	g_signal_connect(app, "activate", G_CALLBACK (activate_app), &zond);
	g_signal_connect(app, "open", G_CALLBACK (open_app), &zond);

	logging_init("zond_viewer");
	install_crash_handler();

	gint status = g_application_run(G_APPLICATION(app), argc, argv);

	g_object_unref(app);

	LOG_INFO("zond_viewer beendet");
	logging_cleanup();

	return status;
}
