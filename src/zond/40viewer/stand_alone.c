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

#include "../zond_pdf_document.h"
#include "../global_types.h"

#include "../99conv/general.h"

#include "viewer.h"
#include "document.h"

#ifdef VIEWER
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
			rc = zond_pdf_document_save(pv->dd->zond_pdf_document, &error);
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

static gint pv_oeffnen_datei(PdfViewer *pv, gchar *path, gchar **errmsg) {
	DisplayedDocument *dd = NULL;
	gchar *file_part = NULL;
	GError *error = NULL;
	gint rc = 0;
	SondFilePartPDFPageTree* sfp_pdf_page_tree = NULL;

	sfp_pdf_page_tree = sond_file_part_from_filepart(path,)


	dd = document_new_displayed_document(sfp_pdf_page_tree, NULL, errmsg);
	g_free(file_part);
	if (!dd && *errmsg)
		ERROR_S
	else if (!dd)
		return 0;

	rc = viewer_display_document(pv, dd, 0, 0, &error);
	if (rc) {
		if (errmsg) *errmsg = g_strdup_printf("%s\n%s", __func__, error->message);
		g_error_free(error);
		document_free_displayed_documents(dd);

		return -1;
	}

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
	gchar *errmsg = NULL;

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
	rc = pv_oeffnen_datei(pv, filename, &errmsg);
	g_free(filename);
	if (rc) {
		display_message(pv->vf, "Fehler - Datei öffnen\n\n"
				"Bei Aufruf pv_oeffnen_datei:\n", errmsg, NULL);
		g_free(errmsg);
	}

	return;
}

static PdfViewer*
init(GtkApplication *app, Projekt *zond) {
	zond->base_dir = get_base_dir();

	PdfViewer *pv = viewer_start_pv(zond);
	pv->zoom = 140;

	gtk_application_add_window(app, GTK_WINDOW(pv->vf));

	pv_activate_widgets(pv, FALSE);

	return pv;
}

static void open_app(GtkApplication *app, gpointer files, gint n_files,
		gchar *hint, gpointer user_data) {
	Projekt **zond = (Projekt**) user_data;

	PdfViewer *pv = init(app, *zond);
	if (!pv)
		return;

	gint rc = 0;
	gchar *errmsg = NULL;

	GFile **g_file = NULL;
	g_file = (GFile**) files;

	gchar *uri = g_file_get_uri(g_file[0]);
	gchar *uri_unesc = g_uri_unescape_string(uri, NULL);
	g_free(uri);

	rc = pv_oeffnen_datei(pv, uri_unesc + 8, &errmsg);
	g_free(uri_unesc);
	if (rc) {
		display_message(pv->vf, "Fehler - Datei öffnen:\n", errmsg, NULL);
		g_free(errmsg);
	}

	return;
}

static void activate_app(GtkApplication *app, gpointer user_data) {
	Projekt **zond = (Projekt**) user_data;

	if ((*zond)->arr_pv->len)
		return;

	PdfViewer *pv = init(app, *zond);
	if (!pv)
		return;

	return;
}

static void startup_app(GtkApplication *app, gpointer user_data) {
	Projekt **zond = (Projekt**) user_data;

	*zond = g_malloc0(sizeof(Projekt));

	(*zond)->ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
	if (!((*zond)->ctx)) {
		g_free(*zond);
		return;
	}

	(*zond)->settings = g_settings_new("de.perlio.zondPV");

	(*zond)->arr_pv = g_ptr_array_new();

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

	gint status = g_application_run(G_APPLICATION(app), argc, argv);

	g_object_unref(app);

	return status;
}
#endif // VIEWER
