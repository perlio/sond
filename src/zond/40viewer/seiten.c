/*
 zond (seiten.c) - Akten, Beweisstücke, Unterlagen
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

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <gtk/gtk.h>
#include <tesseract/capi.h>

#include "../zond_pdf_document.h"

#include "../global_types.h"
#include "../zond_dbase.h"

#include "../99conv/general.h"
#include "../99conv/pdf.h"
#include "../pdf_ocr.h"

#include "../20allgemein/ziele.h"
#include "../20allgemein/project.h"

#include "viewer.h"
#include "render.h"
#include "document.h"

#include "../../misc.h"

static GPtrArray*
seiten_get_document_pages(PdfViewer *pv, GArray *arr_seiten_pv) {
	GPtrArray *arr_document_page = NULL;

	if (arr_seiten_pv) {
		arr_document_page = g_ptr_array_new();

		for (gint i = 0; i < arr_seiten_pv->len; i++) {
			ViewerPageNew *viewer_page = g_ptr_array_index(pv->arr_pages,
					g_array_index( arr_seiten_pv, gint, i ));

			if (!g_ptr_array_find(arr_document_page,
					viewer_page->pdf_document_page, NULL))
				g_ptr_array_add(arr_document_page,
						(gpointer) viewer_page->pdf_document_page);
		}
	}

	return arr_document_page;
}

static GArray*
seiten_markierte_thumbs(PdfViewer *pv) {
	GList *selected = NULL;
	GList *list = NULL;
	GArray *arr_page_pv = NULL;
	gint *index = NULL;

	selected = gtk_tree_selection_get_selected_rows(
			gtk_tree_view_get_selection(GTK_TREE_VIEW(pv->tree_thumb)), NULL);

	if (!selected)
		return NULL;

	arr_page_pv = g_array_new( FALSE, FALSE, sizeof(gint));
	list = selected;
	do {
		index = gtk_tree_path_get_indices(list->data);
		g_array_append_val(arr_page_pv, index[0]);
	} while ((list = list->next));

	g_list_free_full(selected, (GDestroyNotify) gtk_tree_path_free);

	return arr_page_pv;
}

static gint compare_gint(gconstpointer a, gconstpointer b) {
	const gint *_a = a;
	const gint *_b = b;

	return *_a - *_b;
}

static GArray*
seiten_parse_text(PdfViewer *pv, gint max, const gchar *text) {
	gint start = 0;
	gint end = 0;
	const gchar *range = NULL;
	gint i = 0;
	gint page = 0;
	gint last_inserted = -1;

	if (!fz_is_page_range( NULL, text))
		return NULL;

	GArray *arr_tmp = g_array_new( FALSE, FALSE, sizeof(gint));
	GArray *arr_pages = g_array_new( FALSE, FALSE, sizeof(gint));

	range = text;
	while ((range = fz_parse_page_range( NULL, range, &start, &end, max))) {
		if (start < end) {
			for (i = start; i <= end; i++)
				g_array_append_val(arr_tmp, i);
		} else if (start > end) {
			for (i = start; i >= end; i--)
				g_array_append_val(arr_tmp, i);
		} else
			g_array_append_val(arr_tmp, start);
	}

	g_array_sort(arr_tmp, (GCompareFunc) compare_gint);

	for (i = 0; i < arr_tmp->len; i++) {
		page = g_array_index( arr_tmp, gint, i ) - 1;
		if (page != last_inserted) {
			g_array_append_val(arr_pages, page);
			last_inserted = page;
		}
	}

	g_array_free(arr_tmp, TRUE);

	return arr_pages;
}

static void cb_seiten_drehen_entry(GtkEntry *entry, gpointer dialog) {
	gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

	return;
}

static void cb_radio_auswahl_toggled(GtkToggleButton *button, gpointer data) {
	gtk_widget_set_sensitive((GtkWidget*) data,
			gtk_toggle_button_get_active(button));

	return;
}

static GPtrArray*
seiten_abfrage_seiten(PdfViewer *pv, const gchar *title, gint *winkel,
		gboolean mit_alles) {
	gint rc = 0;
	GtkWidget *radio_90_UZS = NULL;
	GtkWidget *radio_180 = NULL;
	GtkWidget *radio_90_gegen_UZS = NULL;
	gchar *text = NULL;
	GPtrArray *arr_document_pages = NULL;

	GtkWidget *dialog = gtk_dialog_new_with_buttons(title, GTK_WINDOW(pv->vf),
			GTK_DIALOG_MODAL, "Ok", GTK_RESPONSE_OK, "Abbrechen",
			GTK_RESPONSE_CANCEL, NULL);

	GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

	if (winkel) {
		radio_90_UZS = gtk_radio_button_new_with_label( NULL,
				"90° im Uhrzeigersinn");
		radio_180 = gtk_radio_button_new_with_label( NULL, "180°");
		radio_90_gegen_UZS = gtk_radio_button_new_with_label( NULL,
				"90° gegen Uhrzeigersinn");

		gtk_radio_button_join_group(GTK_RADIO_BUTTON(radio_180),
				GTK_RADIO_BUTTON(radio_90_UZS));
		gtk_radio_button_join_group(GTK_RADIO_BUTTON(radio_90_gegen_UZS),
				GTK_RADIO_BUTTON(radio_180));

		gtk_box_pack_start(GTK_BOX(content_area), radio_90_UZS, FALSE, FALSE,
				0);
		gtk_box_pack_start(GTK_BOX(content_area), radio_180, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(content_area), radio_90_gegen_UZS, FALSE,
				FALSE, 0);
	}

	GtkWidget *radio_alle = gtk_radio_button_new_with_label( NULL, "Alle");
	GtkWidget *radio_mark = gtk_radio_button_new_with_label( NULL, "Markierte");
	GtkWidget *radio_auswahl = gtk_radio_button_new_with_label( NULL,
			"Seiten auswählen");

	gtk_radio_button_join_group(GTK_RADIO_BUTTON(radio_alle),
			GTK_RADIO_BUTTON(radio_auswahl));
	gtk_radio_button_join_group(GTK_RADIO_BUTTON(radio_mark),
			GTK_RADIO_BUTTON(radio_alle));

	GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(hbox), radio_auswahl, FALSE, FALSE, 0);

	GtkWidget *entry = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(hbox), entry, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(content_area), radio_alle, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(content_area), radio_mark, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(content_area), hbox, FALSE, FALSE, 0);

	g_signal_connect(radio_auswahl, "toggled",
			G_CALLBACK( cb_radio_auswahl_toggled), entry);

	if (!gtk_tree_selection_count_selected_rows(
			gtk_tree_view_get_selection(GTK_TREE_VIEW(pv->tree_thumb)))) {
		gtk_widget_set_sensitive(radio_mark, FALSE);
		gtk_widget_grab_focus(entry);
	} else
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio_mark), TRUE);

	if (!mit_alles)
		gtk_widget_set_sensitive(radio_alle, FALSE);

	g_signal_connect(entry, "activate", G_CALLBACK(cb_seiten_drehen_entry),
			(gpointer ) dialog);

	gtk_widget_show_all(dialog);

	rc = gtk_dialog_run(GTK_DIALOG(dialog));

	if (rc == GTK_RESPONSE_OK) {
		GArray *arr_seiten_pv = NULL;

		if (winkel) {
			if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radio_90_UZS)))
				*winkel = 90;
			else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radio_180)))
				*winkel = 180;
			else
				*winkel = -90;
		}

		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radio_auswahl))) {
			text = g_strdup(gtk_entry_get_text( GTK_ENTRY(entry) ));
			if (text)
				arr_seiten_pv = seiten_parse_text(pv, pv->arr_pages->len, text);
			g_free(text);
		} else if (gtk_toggle_button_get_active(
				GTK_TOGGLE_BUTTON(radio_alle))) {
			arr_seiten_pv = g_array_new( FALSE, FALSE, sizeof(gint));
			for (gint i = 0; i < pv->arr_pages->len; i++)
				g_array_append_val(arr_seiten_pv, i);
		} else if (gtk_toggle_button_get_active(
				GTK_TOGGLE_BUTTON(radio_mark))) {
			arr_seiten_pv = seiten_markierte_thumbs(pv);
		}

		if (arr_seiten_pv && arr_seiten_pv->len) {
			arr_document_pages = seiten_get_document_pages(pv, arr_seiten_pv);
			g_array_unref(arr_seiten_pv);
		}
	}

	gtk_widget_destroy(dialog);

	return arr_document_pages;
}

/*
 **  Seiten OCR
 */
void cb_pv_seiten_ocr(GtkMenuItem *item, gpointer data) {
	gint rc = 0;
	gchar *errmsg = NULL;
	gchar *title = NULL;
	GPtrArray *arr_document_page = NULL;
	InfoWindow *info_window = NULL;

	PdfViewer *pv = (PdfViewer*) data;

	//zu OCRende Seiten holen
	title = g_strdup_printf("Seiten OCR (1 - %i):", pv->arr_pages->len);
	arr_document_page = seiten_abfrage_seiten(pv, title, NULL, TRUE);
	g_free(title);

	if (!arr_document_page)
		return;

	info_window = info_window_open(pv->vf, "OCR");

	rc = pdf_ocr_pages(pv->zond, info_window, arr_document_page, &errmsg);
	info_window_close(info_window);

	if (rc == -1) {
		display_message(pv->vf, "Fehler - OCR\n\nBei Aufruf pdf_ocr_pages:\n",
				errmsg,
				NULL);
		g_free(errmsg);
		g_ptr_array_unref(arr_document_page);

		return;
	}

	for (gint i = 0; i < arr_document_page->len; i++) {
		PdfDocumentPage *pdf_document_page = g_ptr_array_index(
				arr_document_page, i);

		//fz_text_list droppen und auf NULL setzen
		while (pdf_document_page->thread & 1)
			viewer_transfer_rendered(pdf_document_page->thread_pv, TRUE);

		fz_drop_display_list(
				zond_pdf_document_get_ctx(pdf_document_page->document),
				pdf_document_page->display_list);
		pdf_document_page->display_list = NULL;

		fz_drop_stext_page(
				zond_pdf_document_get_ctx(pdf_document_page->document),
				pdf_document_page->stext_page);
		pdf_document_page->stext_page = NULL;

		pdf_document_page->thread &= 2;

		//Damit speichern angeht - gibt keinen Fehler zurück, wenn func == NULL
		viewer_foreach(pv, pdf_document_page, NULL, NULL, &errmsg);
	}

	//damit Text von Cursor "erkannt" wird
	g_signal_emit_by_name(pv->v_adj, "value-changed", NULL);

	g_ptr_array_unref(arr_document_page);

	return;
}

static void seiten_refresh_layouts(GPtrArray *arr_pv) {
	for (gint i = 0; i < arr_pv->len; i++) {
		PdfViewer *pv = g_ptr_array_index(arr_pv, i);

		if (g_object_get_data(G_OBJECT(pv->layout), "dirty")) {
			viewer_refresh_layout(pv, 0);
			g_object_set_data(G_OBJECT(pv->layout), "dirty", NULL);

			g_signal_emit_by_name(pv->v_adj, "value-changed", NULL);
		}
	}

	return;
}

/*
 **      Seiten drehen
 */
static void seiten_page_tilt(ViewerPageNew *viewer_page) {
	float x1_tmp = 0.0;
	float y1_tmp = 0.0;

	x1_tmp = viewer_page->crop.x0
			+ (viewer_page->crop.y1 - viewer_page->crop.y0);
	y1_tmp = viewer_page->crop.y0
			+ (viewer_page->crop.x1 - viewer_page->crop.x0);

	viewer_page->crop.x1 = x1_tmp;
	viewer_page->crop.y1 = y1_tmp;

	return;
}

static gint seiten_drehen_foreach(PdfViewer *pv, gint page_pv,
		DisplayedDocument *dd, gpointer data, gchar **errmsg) {
	gint winkel = 0;
	gint rc = 0;
	GtkTreeIter iter = { 0 };
	winkel = GPOINTER_TO_INT(data);
	ViewerPageNew *viewer_page = NULL;

	viewer_close_thread_pool_and_transfer(pv);

	//damit in Seite gezeichnete Markierungen nach dem Drehen nicht an falscher Stelle sind
	pv->clicked_annot = NULL;
	pv->highlight.page[0] = -1;

	viewer_page = g_ptr_array_index(pv->arr_pages, page_pv);

	if (viewer_page->image_page)
		gtk_image_clear(GTK_IMAGE(viewer_page->image_page));
	viewer_page->pixbuf_page = NULL;

	rc = viewer_get_iter_thumb(pv, page_pv, &iter);
	if (rc)
		ERROR_S_MESSAGE("Bei Aufruf viewer_get_iter:\niter konnte "
				"nicht ermittelt werden");

	gtk_list_store_set(
			GTK_LIST_STORE(
					gtk_tree_view_get_model( GTK_TREE_VIEW(pv->tree_thumb) )),
			&iter, 0, NULL, -1);
	viewer_page->pixbuf_thumb = NULL;

	if (winkel == 90 || winkel == -90) {
		seiten_page_tilt(viewer_page);
		g_object_set_data(G_OBJECT(pv->layout), "dirty", GINT_TO_POINTER(1));
	}

	viewer_page->thread = 0;
//    g_signal_emit_by_name( pv->v_adj, "value-changed", NULL );

	return 0;
}

static gint seiten_drehen_pdf(PdfDocumentPage *pdf_document_page, gint winkel,
		gchar **errmsg) {
	pdf_obj *page_obj = NULL;
	pdf_obj *rotate_obj = NULL;
	gint rotate = 0;

	fz_context *ctx = zond_pdf_document_get_ctx(pdf_document_page->document);

	page_obj = pdf_document_page->obj;

	fz_try(ctx)
		rotate_obj = pdf_dict_get_inheritable(ctx, page_obj, PDF_NAME(Rotate));
	fz_catch(ctx)
		ERROR_MUPDF("pdf_dict_get_inheritable")

	if (!rotate_obj) {
		rotate_obj = pdf_new_int(ctx, (int64_t) winkel);
		fz_try(ctx)
			pdf_dict_put_drop(ctx, page_obj, PDF_NAME(Rotate), rotate_obj);
		fz_catch(ctx)
			ERROR_MUPDF("pdf_dict_put_drop")
	} else {
		rotate = pdf_to_int(ctx, rotate_obj);
		rotate += winkel;
		if (rotate < 0)
			rotate += 360;
		else if (rotate > 360)
			rotate -= 360;
		else if (rotate == 360)
			rotate = 0;

		pdf_set_int(ctx, rotate_obj, (int64_t) rotate);
	}

	pdf_document_page->rotate = rotate;

	return 0;
}

static gint seiten_drehen(PdfViewer *pv, GPtrArray *arr_document_page,
		gint winkel, gchar **errmsg) {
	for (gint i = 0; i < arr_document_page->len; i++) {
		gint rc = 0;

		PdfDocumentPage *pdf_document_page = g_ptr_array_index(
				arr_document_page, i);

		zond_pdf_document_mutex_lock(pdf_document_page->document);

		rc = seiten_drehen_pdf(pdf_document_page, winkel, errmsg);
		zond_pdf_document_mutex_unlock(pdf_document_page->document);
		if (rc == -1) ERROR_S

		while (pdf_document_page->thread & 1)
			viewer_transfer_rendered(pdf_document_page->thread_pv,
					TRUE);

		fz_drop_display_list(
				zond_pdf_document_get_ctx(pdf_document_page->document),
				pdf_document_page->display_list);
		pdf_document_page->display_list = NULL;

		fz_drop_stext_page(
				zond_pdf_document_get_ctx(pdf_document_page->document),
				pdf_document_page->stext_page);
		pdf_document_page->stext_page = NULL;

		//page_annots müssen neu geladen werden, weil quads und rects sonst verdreht sind
		g_ptr_array_remove_range(pdf_document_page->arr_annots,
				0, pdf_document_page->arr_annots->len);
		zond_pdf_document_page_load_annots(pdf_document_page);

		pdf_document_page->thread &= 2;

		rc = viewer_foreach(pv, pdf_document_page, seiten_drehen_foreach,
				GINT_TO_POINTER(winkel), errmsg);
		if (rc) {
			viewer_save_and_close(pv);

			ERROR_S
		}
	}

	return 0;
}

void cb_pv_seiten_drehen(GtkMenuItem *item, gpointer data) {
	gint rc = 0;
	gchar *errmsg = NULL;
	gint winkel = 0;
	gchar *title = NULL;
	GPtrArray *arr_document_page = NULL;

	PdfViewer *pv = (PdfViewer*) data;

	//zu drehende Seiten holen
	title = g_strdup_printf("Seiten drehen (1 - %i):", pv->arr_pages->len);
	arr_document_page = seiten_abfrage_seiten(pv, title, &winkel, TRUE);
	g_free(title);

	if (!arr_document_page)
		return;

	rc = seiten_drehen(pv, arr_document_page, winkel, &errmsg);
	g_ptr_array_unref(arr_document_page);
	if (rc) {
		display_message(pv->vf, "Fehler in Seiten drehen -\n\nBei Aufruf "
				"seiten_drehen\n", errmsg, NULL);
		g_free(errmsg);

		return;
	}

	seiten_refresh_layouts(pv->zond->arr_pv);

	return;
}

/*
 **      Seiten löschen
 */
static gint seiten_cb_loesche_seite(PdfViewer *pv, gint page_pv,
		DisplayedDocument *dd, gpointer data, gchar **errmsg) {
	gint rc = 0;
	GtkTreeIter iter;
	ViewerPageNew *viewer_page = NULL;

	viewer_close_thread_pool_and_transfer(pv);

	//pv muß neues layout haben!
	g_object_set_data(G_OBJECT(pv->layout), "dirty", GINT_TO_POINTER(1));

	viewer_page = g_ptr_array_index(pv->arr_pages, page_pv);
	if (viewer_page->image_page)
		gtk_widget_destroy(viewer_page->image_page);
	g_ptr_array_remove_index(pv->arr_pages, page_pv); //viewer_page wird freed!

	rc = viewer_get_iter_thumb(pv, page_pv, &iter);
	if (rc)
		ERROR_S_MESSAGE("Bei Aufruf viewer_get_iter_thumb:\n"
				"Iter konnte nicht ermittelt werden");

	gtk_list_store_remove(
			GTK_LIST_STORE(
					gtk_tree_view_get_model( GTK_TREE_VIEW(pv->tree_thumb) )),
			&iter);

	return 0;
}

static gint seiten_anbindung_int(ZondDBase* zond_dbase,
		PdfDocumentPage* pdf_document_page, GError** error) {
	gint rc = 0;
	GArray* arr_sections = NULL;
	gint page_doc = 0;

	rc = zond_dbase_get_arr_sections(zond_dbase,
			zond_pdf_document_get_file_part(pdf_document_page->document),
			&arr_sections, error);
	if (rc) {
		g_prefix_error(error, "%s\n", __func__);

		return -1;
	}

	page_doc = pdf_document_page_get_index(pdf_document_page);

	for (gint u = 0; u < arr_sections->len; u++) {
		Section section = { 0, };
		Anbindung anbindung = { 0, };

		section = g_array_index(arr_sections, Section, u);
		anbindung_parse_file_section(section.section, &anbindung);

		anbindung_aktualisieren_insert_pages(pdf_document_page->document, &anbindung);

		if ((page_doc == anbindung.von.seite) ||
				(!anbindung_is_pdf_punkt(anbindung) &&
				page_doc == anbindung.bis.seite)) {
			g_array_unref(arr_sections);

			return 1;
		}
	}

	g_array_unref(arr_sections);

	return 0;
}

static gint seiten_anbindung(PdfViewer *pv, GPtrArray *arr_document_page,
		GError **error) {
	for (gint i = 0; i < arr_document_page->len; i++) {
		PdfDocumentPage *pdf_document_page = NULL;
		gint rc = 0;

		pdf_document_page = g_ptr_array_index(arr_document_page, i);

		rc = seiten_anbindung_int(pv->zond->dbase_zond->zond_dbase_work,
				pdf_document_page, error);
		if (rc) {
			if (rc == -1) g_prefix_error(error, "%s\n", __func__);

			return (rc == -1) ? -1 : 1;
		}

		rc = seiten_anbindung_int(pv->zond->dbase_zond->zond_dbase_store,
				pdf_document_page, error);
		if (rc) {
			if (rc == -1) g_prefix_error(error, "%s\n", __func__);

			return (rc == -1) ? -1 : 2;
		}
	}

	return 0;
}

static gint seiten_loeschen(PdfViewer *pv, GPtrArray *arr_document_page,
		GError **error) {
	for (gint i = 0; i < arr_document_page->len; i++) {
		gint rc = 0;
		PdfDocumentPage* pdf_document_page = NULL;
		gchar* errmsg = NULL;
		JournalEntry entry = { 0, };
		GArray* arr_journal = NULL;

		pdf_document_page = g_ptr_array_index(arr_document_page, i);
		pdf_document_page->to_be_deleted = TRUE;

		//macht - sofern noch nicht geschehen - thread_pool des pv dicht, in dem Seite angezeigt wird
		//Dann wird Seite aus pv->arr_pages gelöscht
		//ToDo: Vielleicht Schließen des thread-pools nicht erforderlich?
		rc = viewer_foreach(pv, pdf_document_page, seiten_cb_loesche_seite,
				NULL, &errmsg);
		if (rc) {
			if (error)
				*error = g_error_new( ZOND_ERROR, 0, "%s\n%s", __func__,
						errmsg);
			g_free(errmsg);

			return -1;
		}

		arr_journal = zond_pdf_document_get_arr_journal(pdf_document_page->document);

		entry.pdf_document_page = pdf_document_page;
		entry.type = JOURNAL_TYPE_PAGE_DELETED;

		g_array_append_val(arr_journal, entry);
	}
	seiten_refresh_layouts(pv->zond->arr_pv);

	gtk_tree_selection_unselect_all(
			gtk_tree_view_get_selection(GTK_TREE_VIEW(pv->tree_thumb)));

	return 0;
}

void cb_pv_seiten_loeschen(GtkMenuItem *item, gpointer data) {
	gint rc = 0;
	GError *error = NULL;
	gchar *title = NULL;
	GPtrArray *arr_document_page = NULL;

	PdfViewer *pv = (PdfViewer*) data;

	//zu löschende Seiten holen
	title = g_strdup_printf("Seiten löschen (1 - %i):", pv->arr_pages->len);
	arr_document_page = seiten_abfrage_seiten(pv, title, NULL, FALSE);
	g_free(title);

	if (!arr_document_page)
		return;

#ifndef VIEWER
	//Abfrage, ob Anbindung mit Seite verknüpft
	rc = seiten_anbindung(pv, arr_document_page, &error);
	if (rc) {
		if (rc == -1) {
			display_error(pv->vf, "Fehler Seiten Löschen", error->message);
			g_error_free(error);
		} else if (rc == 1)
			display_message(pv->vf, "Seiten enthalten Anbindungen - \n"
					"Löschen nicht zulässig", NULL);
		else if (rc == 2)
			display_message(pv->vf, "Gespeichertes Projekt: Seiten enthalten Anbindungen"
					"Zunächst Projekt speichern", NULL);

		g_ptr_array_unref(arr_document_page);

		return;
	}
#endif // VIEWER

	rc = seiten_loeschen(pv, arr_document_page, &error);
	g_ptr_array_unref(arr_document_page);
	if (rc == -1) {
		display_error(pv->vf, "Fehler Seiten Löschen", error->message);
		g_error_free(error);

		viewer_save_and_close(pv);

		return;
	}

	return;
}

/*
 **      Seiten einfügen
 */
typedef struct _DataInsert {
	PdfDocumentPage* dd_first_page;
	gint dd_first_index;
	PdfDocumentPage* dd_last_page;
	gint dd_last_index;
	gint page_doc;
	gint count;
	gboolean after_last;
} DataInsert;

static gint seiten_einfuegen_foreach(PdfViewer *pv, gint page_pv,
		DisplayedDocument *dd, gpointer data, gchar **errmsg) {
	ViewerPageNew *viewer_page = NULL;

	DataInsert* data_insert = (DataInsert*) data;

	viewer_page = g_ptr_array_index(pv->arr_pages, page_pv);

	//Wenn vor erster oder nach letzter Seite des vorliegenden dd eingefügt werden soll:
	//Prüfen, ob dd so "weit" ist wie das dd, in das eingefügt wurde
	if (dd->first_page == viewer_page->pdf_document_page &&
			!data_insert->after_last) { //Seite liegt am Anfang ...
		gint last_page_pv_dd = 0;
		gint last_page_pv_entry = 0;

		//wenn vorliegendes dd "unterseitig" anfängt:
		if (dd->first_index) return 0;

		//sonst weiter untersuchen:
		last_page_pv_dd = pdf_document_page_get_index(dd->last_page);
		last_page_pv_entry = pdf_document_page_get_index(data_insert->dd_last_page);

		//vorliegendes dd kürzer: Ende!
		if (last_page_pv_dd < last_page_pv_entry) return 0;
		else if (last_page_pv_dd == last_page_pv_entry) {
			if (dd->last_index < data_insert->dd_last_index) return 0;
		}

		//dd soll jetzt auch eingefügte Seiten umfassen
		//d.h. erste Seite anpassen
		//
		dd->first_page = zond_pdf_document_get_pdf_document_page(viewer_page->dd->zond_pdf_document,
				data_insert->page_doc);
	} else if (dd->last_page == viewer_page->pdf_document_page && data_insert->after_last) {
		gint first_page_pv_dd = 0;
		gint first_page_pv_entry = 0;

		if ( dd->last_index < EOP) return 0;

		first_page_pv_dd = pdf_document_page_get_index(dd->first_page);
		first_page_pv_entry = pdf_document_page_get_index(data_insert->dd_first_page);

		if (first_page_pv_dd > first_page_pv_entry) return 0;
		else if (first_page_pv_dd == first_page_pv_entry) {
			if ( dd->first_index > data_insert->dd_last_index) return 0;
		}

		dd->last_page = zond_pdf_document_get_pdf_document_page(viewer_page->dd->zond_pdf_document,
				data_insert->page_doc + data_insert->count - 1);
	}

	//jetzt in viewer einfügen
	for (gint u = 0; u < data_insert->count; u++) {
		ViewerPageNew *viewer_page_insert = NULL;
		GtkTreeIter iter_tmp;

		viewer_page_insert = viewer_new_page (pv, dd,
				pdf_document_page_get_index(viewer_page->pdf_document_page) + u -
				((data_insert->after_last) ? -1 : data_insert->count));

		g_ptr_array_insert(pv->arr_pages, page_pv + u + ((data_insert->after_last) ? 1 : 0),
				viewer_page_insert);

		gtk_list_store_insert(GTK_LIST_STORE(
				gtk_tree_view_get_model( GTK_TREE_VIEW(pv->tree_thumb) )),
				&iter_tmp, page_pv + u + ((data_insert->after_last) ? 1 : 0));
	}

	g_object_set_data(G_OBJECT(pv->layout), "dirty", GINT_TO_POINTER(1));

	return 0;
}

static void cb_pv_seiten_entry(GtkEntry *entry, gpointer button_datei) {
	gtk_widget_grab_focus((GtkWidget*) button_datei);

	return;
}

static gint seiten_abfrage_seitenzahl(PdfViewer *pv, guint *num) {
	gint res = 0;
	gint rc = 0;

	// Dialog erzeugen
	GtkWidget *dialog = gtk_dialog_new_with_buttons("Seiten einfügen:",
			GTK_WINDOW(pv->vf), GTK_DIALOG_MODAL, "Datei", 1, "Clipboard", 2,
			"Abbrechen", GTK_RESPONSE_CANCEL, NULL);

	GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

	GtkWidget *frame = gtk_frame_new("nach Seite:");
	GtkWidget *entry = gtk_entry_new();
	gtk_container_add(GTK_CONTAINER(frame), entry);
	gtk_box_pack_start(GTK_BOX(content_area), frame, TRUE, TRUE, 0);

	GtkWidget *button_clipboard = gtk_dialog_get_widget_for_response(
			GTK_DIALOG(dialog), 2);
	if (!pv->zond->pv_clip)
		gtk_widget_set_sensitive(button_clipboard, FALSE);

	GtkWidget *button_datei = gtk_dialog_get_widget_for_response(
			GTK_DIALOG(dialog), 1);
	g_signal_connect(entry, "activate", G_CALLBACK(cb_pv_seiten_entry),
			button_datei);

	gtk_widget_show_all(dialog);
	gtk_widget_grab_focus(entry);

	res = gtk_dialog_run(GTK_DIALOG(dialog));
	rc = string_to_guint(gtk_entry_get_text(GTK_ENTRY(entry)), num);
	if (rc)
		res = -1;

	gtk_widget_destroy(dialog);

	if (res != 1 && res != 2)
		return -1;

	return res;
}

void cb_pv_seiten_einfuegen(GtkMenuItem *item, gpointer data) {
	PdfViewer *pv = (PdfViewer*) data;
	gint ret = 0;
	gint rc = 0;
	guint pos = 0;
	pdf_document *doc_merge = NULL;
	gchar *errmsg = NULL;
	GError *error = NULL;
	ViewerPageNew* viewer_page = NULL;
	JournalEntry entry = { 0 };
	gint page_doc = 0;
	gint count = 0;
	DataInsert data_insert = { 0 };

	ret = seiten_abfrage_seitenzahl(pv, &pos);
	if (ret == -1)
		return;

	if (pos > pv->arr_pages->len)
		pos = pv->arr_pages->len;
	if (pos < 0)
		pos = 0;

	viewer_page = g_ptr_array_index(pv->arr_pages, (pos == pv->arr_pages->len) ? pos - 1 : pos);

	//verschiedene Tests...
	if (pos == 0) {
		if (viewer_page->dd->first_index != 0) {
			display_message(pv->vf,
					"Abschnitt beginnt nicht am Beginn der Seite -\n"
							"Einfügen vor erster Seite daher nicht möglich",
					NULL);

			return;
		}
	} else if (pos < pv->arr_pages->len) {
		ViewerPageNew* viewer_page_vor = NULL;

		viewer_page_vor = g_ptr_array_index(pv->arr_pages, pos - 1);

		if (viewer_page->dd != viewer_page_vor->dd) {
			//es handelt sich um ein virtuelles PDF
			//zwischen den Grenzen verschiedener Abschnitte sollte nichts eingefügt werden
			display_message(pv->vf,
					"Virtuelles PDF - zwischen den Abschnitten "
							"darf nichts eingefügt werden", NULL);

			return;
		}
	} else if (pos == pv->arr_pages->len) {
		if (viewer_page->dd->last_index != EOP) {//einfügen nach letzter Seite
			display_message(pv->vf, "Abschnitt Ende nicht am Schluß der Seite -\n"
					"Einfügen nach Ende eines Abschnitts daher nicht möglich", NULL);

			return;
		}
	}

	//komplette Datei wird eingefügt
	if (ret == 1) {
		gint rc = 0;
		gchar *path_merge = NULL;
		gchar *file_part = NULL;

		//Datei auswählen
		path_merge = filename_oeffnen(GTK_WINDOW(pv->vf));
		file_part = g_strdup_printf("/%s//", path_merge);
		g_free(path_merge);
		if (!is_pdf(file_part)) {
			display_message(pv->vf, "Keine PDF-Datei", NULL);
			g_free(file_part);

			return;
		}

		rc = pdf_open_and_authen_document(pv->zond->ctx, TRUE, TRUE, file_part,
				NULL, &doc_merge, NULL, &error);
		if (rc) {
			display_error(pv->vf, "Datei einfügen", error->message);
			g_error_free(error);

			return;
		}
	} else if (ret == 2)
		doc_merge = pdf_keep_document(pv->zond->ctx, pv->zond->pv_clip); //Clipboard

	fz_try(pv->zond->ctx) count = pdf_count_pages(pv->zond->ctx, doc_merge);
	fz_catch( pv->zond->ctx) {
		gchar* err_message = NULL;

		pdf_drop_document(pv->zond->ctx, doc_merge);

		err_message = g_strdup_printf("%s\n%s", __func__, fz_caught_message(pv->zond->ctx));
		display_error(pv->vf, "Einfügen nicht möglich", err_message);
		g_free(err_message);

		return;
	}

	page_doc = pdf_document_page_get_index(viewer_page->pdf_document_page);
	if (pos == pv->arr_pages->len) page_doc++;

	//Seiten werden eingefügt, so daß Seite an Position page_doc nach hinten geschoben wird;
	//page_doc dieser Seite ist hinterher page_doc+count
	rc = zond_pdf_document_insert_pages(viewer_page->dd->zond_pdf_document,
			page_doc, doc_merge, &errmsg);
	pdf_drop_document(pv->zond->ctx, doc_merge);
	if (rc) {
		display_message(pv->vf, "Fehler Einfügen\n\n", errmsg,
				"\n\nViewer wird geschlossen", NULL);
		g_free(errmsg);
		viewer_schliessen(pv);

		return;
	}

	//betroffene viewer-seiten einfügen - kann keinen Fehler zurückgeben
	//vorangehende pdf_document_page wird übergeben - außer wenn Einfügen nach letzter Seite
	data_insert.dd_first_page = viewer_page->dd->first_page;
	data_insert.dd_last_page = viewer_page->dd->last_page;
	data_insert.dd_first_index = viewer_page->dd->first_index;
	data_insert.dd_last_index = viewer_page->dd->last_index;
	data_insert.page_doc = page_doc; //ist die erste eingefügte Seite!
	data_insert.count = count;
	data_insert.after_last = (pos == pv->arr_pages->len) ? TRUE : FALSE;

	entry.type = JOURNAL_TYPE_PAGES_INSERTED;
	entry.pages_inserted.count = count;
	if (pos == 0 || pos == pv->arr_pages->len) { //am Anfang oder Ende des dd wird eingefügt
		entry.pages_inserted.size_dd_pages =
				pdf_document_page_get_index(viewer_page->dd->last_page) -
				pdf_document_page_get_index(viewer_page->dd->first_page);
		if (pos == 0) {
			entry.pages_inserted.pos_dd = -1;
			entry.pages_inserted.size_dd_index = viewer_page->dd->last_index;
		}
		else if (pos == pv->arr_pages->len) { //nach letzter Seite des dd wird eingefügt
			entry.pages_inserted.pos_dd = 1;
			entry.pages_inserted.size_dd_index = viewer_page->dd->first_index;
		}
	}

	//viewer_page->pdf_document_page ist - wenn nicht Einfügen nach letzter Seite -
	//im zond_pdf_document nach hinten gerutscht
	viewer_foreach(pv, viewer_page->pdf_document_page,
			seiten_einfuegen_foreach, &data_insert, &errmsg);

	seiten_refresh_layouts(pv->zond->arr_pv);

	//erste eingefügte Seite!
	entry.pdf_document_page =
			zond_pdf_document_get_pdf_document_page(viewer_page->dd->zond_pdf_document, page_doc);

	g_array_append_val(zond_pdf_document_get_arr_journal(viewer_page->dd->zond_pdf_document), entry);

	return;
}

static pdf_document*
seiten_create_document(PdfViewer *pv, GArray *arr_page_pv, GError **error) {
	pdf_document *doc_dest = NULL;
	gint rc = 0;

	fz_try( pv->zond->ctx )
		doc_dest = pdf_create_document(pv->zond->ctx);
fz_catch	( pv->zond->ctx ) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("MUPDF"),
					fz_caught(pv->zond->ctx), "%s\n%s", __func__,
					fz_caught_message(pv->zond->ctx));

		return NULL;
	}

	for (gint i = 0; i < arr_page_pv->len; i++) {
		ViewerPageNew *viewer_page = NULL;
		PdfDocumentPage *pdf_document_page = NULL;
		gchar *errmsg = NULL;
		gint page_doc = 0;

		gint page_pv = g_array_index(arr_page_pv, gint, i);
		viewer_page = g_ptr_array_index(pv->arr_pages, page_pv);
		pdf_document_page = viewer_page->pdf_document_page;

		page_doc = pdf_document_page_get_index(pdf_document_page);

		zond_pdf_document_mutex_lock(pdf_document_page->document);
		rc = pdf_copy_page(pv->zond->ctx,
				zond_pdf_document_get_pdf_doc(pdf_document_page->document),
				page_doc, page_doc, doc_dest, -1, &errmsg);
		zond_pdf_document_mutex_unlock(pdf_document_page->document);
		if (rc) {
			if (error)
				*error = g_error_new( ZOND_ERROR, 0, "%s\n%s", __func__,
						errmsg);
			g_free(errmsg);
			pdf_drop_document(pv->zond->ctx, doc_dest);

			return NULL;
		}
	}

	return doc_dest;
}

static gint seiten_set_clipboard(PdfViewer *pv, GArray *arr_page_pv,
		GError **error) {
	pdf_drop_document(pv->zond->ctx, pv->zond->pv_clip);
	pv->zond->pv_clip = NULL;

	pv->zond->pv_clip = seiten_create_document(pv, arr_page_pv, error);
	if (!pv->zond->pv_clip)
		ERROR_Z

	return 0;
}

void cb_seiten_kopieren(GtkMenuItem *item, gpointer data) {
	gint rc = 0;
	GError *error = NULL;

	PdfViewer *pv = (PdfViewer*) data;

	GArray *arr_page_pv = seiten_markierte_thumbs(pv);
	if (!arr_page_pv)
		return;

	rc = seiten_set_clipboard(pv, arr_page_pv, &error);
	if (rc) {
		display_error(pv->vf, "Fehler Kopieren Seiten", error->message);
		g_error_free(error);
	}

	g_array_unref(arr_page_pv);

	return;
}

void cb_seiten_ausschneiden(GtkMenuItem *item, gpointer data) {
	gint rc = 0;
	GError *error = NULL;

	PdfViewer *pv = (PdfViewer*) data;

	GArray *arr_page_pv = seiten_markierte_thumbs(pv);
	if (!arr_page_pv)
		return;

	rc = seiten_set_clipboard(pv, arr_page_pv, &error);
	if (rc) {
		display_error(pv->vf, "Fehler Kopieren Seiten", error->message);
		g_error_free(error);
		g_array_unref(arr_page_pv);

		return;
	}

	GPtrArray *arr_document_page = seiten_get_document_pages(pv, arr_page_pv);
	g_array_unref(arr_page_pv);
	rc = seiten_loeschen(pv, arr_document_page, &error);
	g_ptr_array_unref(arr_document_page);
	if (rc == -1) {
		display_error(pv->vf, "Fehler Ausschneiden Seiten", error->message);
		g_error_free(error);
	} else if (rc == 1)
		display_message(pv->vf,
				"Fehler Ausschneiden -\n\nSeiten enthalten Anbindungen\n",
				NULL);

	return;
}
