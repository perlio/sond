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

#include "../../sond_log_and_error.h"
#include "../../sond_fileparts.h"
#include "../../sond_ocr.h"
#include "../../sond_pdf_helper.h"
#include "../../misc.h"

#include "../zond_init.h"

#include "../zond_pdf_document.h"

#include "../zond_dbase.h"

#include "../99conv/general.h"

#include "../20allgemein/ziele.h"
#include "../20allgemein/project.h"

#include "viewer.h"
#include "viewer_render.h"
#include "document.h"

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
// Hilfsfunktion: Pixmap zu GdkPixbuf konvertieren
static GdkPixbuf* pixmap_to_pixbuf(fz_context *ctx, fz_pixmap *pix)
{
    GdkPixbuf *pixbuf = NULL;
    int width, height, stride;
    guchar *pixels;
    gboolean has_alpha;

    if (!pix)
        return NULL;

    width = pix->w;
    height = pix->h;

    // Nur RGB (n=3) oder RGBA (n=4)
    if (pix->n != 3 && pix->n != 4) {
        fz_pixmap *rgb_pix = fz_convert_pixmap(ctx, pix,
                                                fz_device_rgb(ctx), NULL,
                                                NULL, fz_default_color_params, 1);
        pixbuf = pixmap_to_pixbuf(ctx, rgb_pix);
        fz_drop_pixmap(ctx, rgb_pix);
        return pixbuf;
    }

    has_alpha = (pix->n == 4);

    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, has_alpha, 8, width, height);
    if (!pixbuf)
        return NULL;

    pixels = gdk_pixbuf_get_pixels(pixbuf);
    stride = gdk_pixbuf_get_rowstride(pixbuf);

    for (int y = 0; y < height; y++) {
        unsigned char *src = pix->samples + y * pix->stride;
        unsigned char *dst = pixels + y * stride;
        memcpy(dst, src, width * pix->n);
    }

    return pixbuf;
}

static fz_buffer*
get_content_stream_as_buffer(fz_context *ctx, pdf_obj *page_ref,
		GError **error) {
	pdf_obj *obj_contents = NULL;
	fz_stream *stream = NULL;
	fz_buffer *buf = NULL;

	//Stream doc_text

	fz_try( ctx ) {
		obj_contents = pdf_dict_get(ctx, page_ref, PDF_NAME(Contents));
		stream = pdf_open_contents_stream(ctx,
				pdf_get_bound_document(ctx, page_ref), obj_contents);
		buf = fz_read_all(ctx, stream, 1024);
	}
	fz_always( ctx )
		fz_drop_stream(ctx, stream);
	fz_catch ( ctx )
		ERROR_PDF_VAL(NULL)

	return buf;
}

void cb_pv_seiten_ocr(GtkMenuItem *item, gpointer data) {
	gint rc = 0;
	GError* error = NULL;
	gchar *title = NULL;
	GPtrArray *arr_document_page = NULL;
	InfoWindow *info_window = NULL;
	gchar* datadir = NULL;
	SondOcrPool* pool = NULL;
	gint progress = 0;

	PdfViewer *pv = (PdfViewer*) data;

	//zu OCRende Seiten holen
	title = g_strdup_printf("Seiten OCR (1 - %i):", pv->arr_pages->len);
	arr_document_page = seiten_abfrage_seiten(pv, title, NULL, TRUE);
	g_free(title);

	if (!arr_document_page)
		return;

	info_window = info_window_open(pv->vf, "OCR");

	datadir = g_build_filename(pv->zond->exe_dir, "../share/tessdata", NULL);
	pool = sond_ocr_pool_new(datadir, "deu", 1, pv->zond->ctx,
			(void(*)(void*, gchar const*, ...)) info_window_set_message_from_thread,
					(gpointer) info_window, &info_window->cancel, &progress, &error);
	g_free(datadir);
	if (!pool) {
		info_window_set_message(info_window,
				"Thread-Pool konnte nicht initialisiert werden: %s",
				error->message, NULL);
		g_error_free(error);
		g_ptr_array_unref(arr_document_page);

		return;
	}

	for (gint i = 0; i < arr_document_page->len; i++) {
		pdf_obj* font_ref = NULL;
		gint font_num = 0;
		JournalEntry entry = { 0 };
		GArray* arr_entries = NULL;
		fz_buffer* buf_content = NULL;
		GPtrArray* arr_tasks = NULL;
		SondOcrTask* task = NULL;

		PdfDocumentPage *pdf_document_page = g_ptr_array_index(
				arr_document_page, i);

		fz_context *ctx = zond_pdf_document_get_ctx(
				pdf_document_page->document);
		pool->ctx = ctx; //Trick, um einheitlichn context für Dokument zu bekommen

		//entry vorbereiten
		entry.type = JOURNAL_TYPE_OCR;
		entry.pdf_document_page = pdf_document_page;

		buf_content = get_content_stream_as_buffer(ctx, pdf_document_page->page->obj, &error);
		if (!buf_content) {
			info_window_set_message(info_window, "Alter Content-Stream nicht gefunden: %s",
					error->message, NULL);
			g_clear_error(&error);
			continue;
		}

		entry.ocr.buf_old = buf_content; //übernimmt ref

		if ((font_num = zond_pdf_document_get_ocr_num(
				pdf_document_page->document))) {
			fz_try(ctx)
				font_ref = pdf_new_indirect(ctx, zond_pdf_document_get_pdf_doc(
						pdf_document_page->document), font_num, 0);
			fz_catch(ctx) {
				info_window_set_message(info_window, "Konnte Font-Objekt nicht erzeugen: %s",
						fz_caught_message(ctx));
				fz_drop_buffer(ctx, entry.ocr.buf_old);
				continue;
			}
		}
		else {
			gint rc = 0;

			rc  = pdf_get_sond_font(ctx,
					zond_pdf_document_get_pdf_doc(pdf_document_page->document),
					&font_ref, &error);
			if (rc) {
				info_window_set_message(info_window, "Suchen Font-Objekt fehlgeschlagen: %s",
						error->message);
				g_clear_error(&error);
				fz_drop_buffer(ctx, entry.ocr.buf_old);
				continue;
			}

			if (!font_ref) {
				font_ref = pdf_put_sond_font(ctx,
						zond_pdf_document_get_pdf_doc(
								pdf_document_page->document), &error);
				if (!font_ref) {
					info_window_set_message(info_window,
							"SOND-Schriftart konnte nicht erzeugt werden: %s",
							error->message, NULL);
					g_clear_error(&error);
					fz_drop_buffer(ctx, entry.ocr.buf_old);
					continue;
				}

				fz_try(ctx)
					font_num = pdf_to_num(ctx, font_ref);
				fz_catch(ctx) {
					info_window_set_message(info_window,
							"Num des Font_Objektes konnte nicht ermittelt werden:\n",
							fz_caught_message(ctx), NULL);
					pdf_drop_obj(ctx, font_ref);
					fz_drop_buffer(ctx, entry.ocr.buf_old);
					continue;
				}

				zond_pdf_document_set_ocr_num(pdf_document_page->document, font_num);
			}
		}

		task = sond_ocr_task_new(pool,
				zond_pdf_document_get_pdf_doc(pdf_document_page->document),
				pdf_document_page->page_akt, font_ref, &error);
		pdf_drop_obj(ctx, font_ref);
		if (!task) {
			info_window_set_message(info_window,
					"OCR-Task konnte nicht erzeugt werden: %s",
					error->message);
			g_clear_error(&error);
			fz_drop_buffer(ctx, entry.ocr.buf_old);
			continue;
		}

		arr_tasks = g_ptr_array_new_with_free_func((GDestroyNotify) sond_ocr_task_free);
		g_ptr_array_add(arr_tasks, task);

		//OCR
		rc = sond_ocr_do_tasks(arr_tasks, &error);
		g_ptr_array_unref(arr_tasks);
		if (rc) { //Fähler
			fz_drop_buffer(ctx, entry.ocr.buf_old);

			if (rc == -1) {
				info_window_set_message(info_window, "OCR-Task gescheitert: %s",
						error->message);
				g_clear_error(&error);
				continue;
			}
			else if (rc == 1)
				break;
		}

		buf_content = get_content_stream_as_buffer(ctx, pdf_document_page->page->obj, &error);
		if (!buf_content) {
			info_window_set_message(info_window,
					"Neuer Content-Streams konnte nicht ermittelt werden: %s",
					error->message);
			g_clear_error(&error);
			fz_drop_buffer(ctx, entry.ocr.buf_old);
			continue;
		}

		entry.ocr.buf_new = buf_content; //übernimmt ref

		arr_entries = zond_pdf_document_get_arr_journal(pdf_document_page->document);
		g_array_append_val(arr_entries, entry);

		//fz_stext_list droppen und auf NULL setzen
		viewer_render_wait_for_transfer(pdf_document_page);

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
		viewer_foreach(pv, pdf_document_page, NULL, NULL);
	}

	g_ptr_array_unref(arr_document_page);
	info_window_close(info_window);

	//damit Text von Cursor "erkannt" wird
	g_signal_emit_by_name(pv->v_adj, "value-changed", NULL);

	return;
}

static void seiten_refresh_layouts(GPtrArray *arr_pv) {
	for (gint i = 0; i < arr_pv->len; i++) {
		PdfViewer *pv = g_ptr_array_index(arr_pv, i);

		if (g_object_get_data(G_OBJECT(pv->layout), "dirty")) {
			viewer_refresh_layout(pv, 0);
			g_signal_emit_by_name(pv->v_adj, "value-changed", NULL);

			g_object_set_data(G_OBJECT(pv->layout), "dirty", NULL);
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

static gint seiten_drehen_foreach(PdfViewer *pv, ViewerPageNew* viewer_page,
		gint page_pv, gpointer data) {
	gint winkel = 0;
	GtkTreeIter iter = { 0 };
	winkel = GPOINTER_TO_INT(data);

	viewer_close_thread_pool_and_transfer(pv);

	//damit in Seite gezeichnete Markierungen nach dem Drehen nicht an falscher Stelle sind
	pv->clicked_annot = NULL;
	pv->highlight.page[0] = -1;

	if (viewer_page->image_page)
		gtk_image_clear(GTK_IMAGE(viewer_page->image_page));
	viewer_page->pixbuf_page = NULL;

	viewer_get_iter_thumb(pv, page_pv, &iter);

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
	//g_signal_emit_by_name nicht erforderlich. da layout refreshed wird

	return 1;
}

static gint seiten_drehen_pdf(PdfDocumentPage *pdf_document_page, gint winkel,
		gchar **errmsg) {
	gint rotate = 0;
	GError *error = NULL;
	pdf_obj* obj = NULL;

	fz_context *ctx = zond_pdf_document_get_ctx(pdf_document_page->document);

	obj = pdf_document_page_get_page_obj(pdf_document_page, &error);
	if (!obj) {
		if (errmsg) *errmsg = g_strdup_printf("%s\n%s", __func__,
				error->message);
		g_error_free(error);

		return -1;
	}

	rotate = pdf_page_rotate(ctx, obj, winkel, &error);
	if (rotate == -1) {
		if (errmsg) *errmsg = g_strdup_printf("%s\n%s", __func__,
				error->message);
		g_error_free(error);

		return -1;
	}

	pdf_document_page->rotate = rotate;

	return 0;
}

static gint seiten_drehen(PdfViewer *pv, GPtrArray *arr_document_page,
		gint winkel, gchar **errmsg) {
	for (gint i = 0; i < arr_document_page->len; i++) {
		gint rc = 0;
		GError* error = NULL;
		JournalEntry entry = { 0 };
		GArray* arr_journal = NULL;

		PdfDocumentPage *pdf_document_page = g_ptr_array_index(
				arr_document_page, i);

		zond_pdf_document_mutex_lock(pdf_document_page->document);
		rc = seiten_drehen_pdf(pdf_document_page, winkel, errmsg);
		zond_pdf_document_mutex_unlock(pdf_document_page->document);
		if (rc == -1) ERROR_S

		viewer_render_wait_for_transfer(pdf_document_page);

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
		rc = zond_pdf_document_page_load_annots(pdf_document_page, &error);
		if (rc) {
			if (errmsg) *errmsg = g_strdup_printf("%s\n%s", __func__,
					error->message);
			g_error_free(error);

			return -1;
		}

		pdf_document_page->thread &= 2;

		arr_journal = zond_pdf_document_get_arr_journal(
				pdf_document_page->document);
		entry.pdf_document_page = pdf_document_page;
		entry.type = JOURNAL_TYPE_ROTATE;
		entry.rotate.winkel = winkel;
		g_array_append_val(arr_journal, entry);

		viewer_foreach(pv, pdf_document_page, seiten_drehen_foreach,
				GINT_TO_POINTER(winkel));
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
static gint seiten_cb_loesche_seite(PdfViewer *pv, ViewerPageNew* viewer_page,
		gint page_pv, gpointer data) {
	GtkTreeIter iter = { 0 };

	//highlights und Markierungen Text löschen
	pv->highlight.page[0] = -1;
	pv->text_occ.index_act = -1;

	viewer_close_thread_pool_and_transfer(pv);

	if (viewer_page->image_page)
		gtk_widget_destroy(viewer_page->image_page);

	g_ptr_array_remove_index(pv->arr_pages, page_pv); //viewer_page wird g_freed!

	viewer_get_iter_thumb(pv, page_pv, &iter);

	gtk_list_store_remove(GTK_LIST_STORE(
			gtk_tree_view_get_model(GTK_TREE_VIEW(pv->tree_thumb))), &iter);

	//pv muß neues layout haben!
	g_object_set_data(G_OBJECT(pv->layout), "dirty", GINT_TO_POINTER(1));

	return 1;
}

#ifndef VIEWER
static gint seiten_anbindung_int(ZondDBase* zond_dbase, gint attached,
		PdfDocumentPage* pdf_document_page, GError** error) {
	gint rc = 0;
	GArray* arr_sections = NULL;
	gchar* filepart = NULL;

	filepart = sond_file_part_get_filepart(SOND_FILE_PART(
			zond_pdf_document_get_sfp_pdf(pdf_document_page->document)));

	rc = zond_dbase_get_arr_sections(zond_dbase, filepart, &arr_sections, error);
	g_free(filepart);
	if (rc) {
		g_prefix_error(error, "%s\n", __func__);

		return -1;
	}

	for (gint u = 0; u < arr_sections->len; u++) {
		Section section = { 0, };
		Anbindung anbindung = { 0, };

		section = g_array_index(arr_sections, Section, u);
		anbindung_parse_file_section(section.section, &anbindung);

		anbindung_aktualisieren(pdf_document_page->document, &anbindung);

		if ((pdf_document_page->page_akt == anbindung.von.seite) ||
				(!anbindung_is_pdf_punkt(anbindung) &&
				pdf_document_page->page_akt == anbindung.bis.seite)) {
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

		rc = seiten_anbindung_int(pv->zond->dbase_zond->zond_dbase_store, 0,
				pdf_document_page, error);
		if (rc) {
			if (rc == -1) g_prefix_error(error, "%s\n", __func__);

			return (rc == -1) ? -1 : 1;
		}

		rc = seiten_anbindung_int(pv->zond->dbase_zond->zond_dbase_store, 1,
				pdf_document_page, error);
		if (rc) {
			if (rc == -1) g_prefix_error(error, "%s\n", __func__);

			return (rc == -1) ? -1 : 2;
		}
	}

	return 0;
}
#endif // VIEWER

static gint seiten_loeschen(PdfViewer *pv, GPtrArray *arr_document_page,
		GError **error) {
	gboolean page_deleted = FALSE;

	for (gint i = 0; i < arr_document_page->len; i++) {
		PdfDocumentPage* pdf_document_page = NULL;
		JournalEntry entry = { 0, };
		GArray* arr_journal = NULL;
		GPtrArray* arr_pages = NULL;
		gint count = 0;

		pdf_document_page = g_ptr_array_index(arr_document_page, i);

		//Prüfen, ob letzte (nicht gelöschte) Seite des Dokuments
		arr_pages = zond_pdf_document_get_arr_pages(
				pdf_document_page->document);
		for (gint j = 0; j < arr_pages->len; j++) {
			PdfDocumentPage* pdf_document_page_tmp = NULL;

			pdf_document_page_tmp = g_ptr_array_index(arr_pages, j);
			if (!pdf_document_page_tmp || !pdf_document_page_tmp->deleted)
				count++;
		}

		if (count == 1) continue;

		pdf_document_page->deleted = TRUE; //als zu löschend markieren
		page_deleted = TRUE;

		//Seite wird aus pv->arr_pages gelöscht
		viewer_foreach(pv, pdf_document_page, seiten_cb_loesche_seite,
				NULL);

		arr_journal = zond_pdf_document_get_arr_journal(pdf_document_page->document);

		entry.pdf_document_page = pdf_document_page;
		entry.type = JOURNAL_TYPE_PAGE_DELETED;

		g_array_append_val(arr_journal, entry);
	}

	if (page_deleted) {
		seiten_refresh_layouts(pv->zond->arr_pv);

		gtk_tree_selection_unselect_all(
				gtk_tree_view_get_selection(GTK_TREE_VIEW(pv->tree_thumb)));
	}

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
	DisplayedDocument* dd;
	gint page_doc;
	gint count;
	gboolean after_last;
} DataInsert;

static gint seiten_einfuegen_foreach(PdfViewer *pv, ViewerPageNew* viewer_page,
		gint page_pv, gpointer data) {
	DataInsert* data_insert = (DataInsert*) data;

	//Wenn vor erster oder nach letzter Seite des vorliegenden dd eingefügt werden soll:
	//Prüfen, ob dd so "weit" ist wie das dd, in das eingefügt wurde
	if (viewer_page->dd->zpdfd_part->first_page == viewer_page->pdf_document_page &&
			!data_insert->after_last) { //Seite liegt am Anfang ...
		gint last_page_pv_dd = 0;
		gint last_page_pv_entry = 0;

		//wenn vorliegendes dd "unterseitig" anfängt:
		if (viewer_page->dd->zpdfd_part->first_index)
			return 0;

		//sonst weiter untersuchen:
		last_page_pv_dd = viewer_page->dd->zpdfd_part->last_page->page_akt;
		last_page_pv_entry = data_insert->dd->zpdfd_part->last_page->page_akt;

		//vorliegendes dd kürzer: Ende!
		if (last_page_pv_dd < last_page_pv_entry) return 0;
		else if (last_page_pv_dd == last_page_pv_entry)
			if (viewer_page->dd->zpdfd_part->last_index < data_insert->dd->zpdfd_part->last_index)
				return 0;

		//dd soll jetzt auch eingefügte Seiten umfassen
		//d.h. erste Seite anpassen
		viewer_page->dd->zpdfd_part->first_page =
				zond_pdf_document_get_pdf_document_page(viewer_page->dd->zpdfd_part->zond_pdf_document,
				data_insert->page_doc);
	} else if (viewer_page->dd->zpdfd_part->last_page ==
			viewer_page->pdf_document_page && data_insert->after_last) {
		gint first_page_pv_dd = 0;
		gint first_page_pv_entry = 0;

		if (viewer_page->dd->zpdfd_part->last_index < EOP) return 0;

		first_page_pv_dd = viewer_page->dd->zpdfd_part->first_page->page_akt;
		first_page_pv_entry = data_insert->dd->zpdfd_part->first_page->page_akt;

		if (first_page_pv_dd > first_page_pv_entry) return 0;
		else if (first_page_pv_dd == first_page_pv_entry)
			if (viewer_page->dd->zpdfd_part->first_index > data_insert->dd->zpdfd_part->last_index)
				return 0;

		viewer_page->dd->zpdfd_part->last_page =
				zond_pdf_document_get_pdf_document_page(viewer_page->dd->zpdfd_part->zond_pdf_document,
				data_insert->page_doc + data_insert->count - 1);
	}

	//highlights und Markierungen Text löschen
	pv->highlight.page[0] = -1;
	pv->text_occ.index_act = -1;

	//jetzt in viewer einfügen
	for (gint u = 0; u < data_insert->count; u++) {
		ViewerPageNew *viewer_page_insert = NULL;
		GtkTreeIter iter_tmp;

		viewer_page_insert = viewer_new_page (pv, viewer_page->dd,
				viewer_page->pdf_document_page->page_akt + u -
				((data_insert->after_last) ? -1 : data_insert->count));

		g_ptr_array_insert(pv->arr_pages, page_pv + u + ((data_insert->after_last) ? 1 : 0),
				viewer_page_insert);

		gtk_list_store_insert(GTK_LIST_STORE(
				gtk_tree_view_get_model( GTK_TREE_VIEW(pv->tree_thumb) )),
				&iter_tmp, page_pv + u + ((data_insert->after_last) ? 1 : 0));
	}

	g_object_set_data(G_OBJECT(pv->layout), "dirty", GINT_TO_POINTER(1));

	return 1;
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
	GError *error = NULL;
	ViewerPageNew* viewer_page = NULL;
	JournalEntry entry = { 0 };
	gint page_doc = 0;
	gint count = 0;
	DataInsert data_insert = { 0 };
	SondFilePart* sfp = NULL;

	if (pv->dd->next) {
		display_message(pv->vf,
				"Virtuelles PDF -\n\n"
						"Einfügen noch nicht implementiert",
				NULL);

		return;
	}

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
		if (viewer_page->dd->zpdfd_part->first_index != 0) {
			display_message(pv->vf,
					"Abschnitt beginnt nicht am Beginn der Seite -\n"
							"Einfügen vor erster Seite daher nicht möglich",
					NULL);

			return;
		}
	}
	else if (pos == pv->arr_pages->len) {
		if (viewer_page->dd->zpdfd_part->last_index != EOP) {//einfügen nach letzter Seite
			display_message(pv->vf, "Abschnitt Ende nicht am Schluß der Seite -\n"
					"Einfügen nach Ende eines Abschnitts daher nicht möglich", NULL);

			return;
		}
	}

	//komplette Datei wird eingefügt
	if (ret == 1) {
		gchar *path_merge = NULL;

		//Datei auswählen
		path_merge = filename_oeffnen(GTK_WINDOW(pv->vf));

		sfp = sond_file_part_from_filepart(path_merge, &error);
		g_free(path_merge);
		if (!sfp) {
			display_error(pv->vf, "Datei einfügen", error->message);
			g_error_free(error);

			return;
		}

		if (!SOND_IS_FILE_PART_PDF(sfp)) {
			display_message(pv->vf, "Keine PDF-Datei", NULL);
			g_object_unref(sfp);

			return;
		}

		doc_merge = sond_file_part_pdf_open_document(pv->zond->ctx,
				SOND_FILE_PART_PDF(sfp), TRUE, TRUE, &error);
		if (!doc_merge) {
			display_error(pv->vf, "Datei einfügen\n\n"
					"Einzufügende Datei konnte nicht geöffnet werden:\n",
					error->message);
			g_error_free(error);
			g_object_unref(sfp);

			return;
		}
	} else if (ret == 2)
		doc_merge = pdf_keep_document(pv->zond->ctx, pv->zond->pv_clip); //Clipboard

	fz_try(pv->zond->ctx)
		count = pdf_count_pages(pv->zond->ctx, doc_merge);
	fz_catch( pv->zond->ctx) {
		gchar* err_message = NULL;

		pdf_drop_document(pv->zond->ctx, doc_merge);
		if (ret == 1)
			g_object_unref(sfp);

		err_message = g_strdup_printf("%s\n%s", __func__, fz_caught_message(pv->zond->ctx));
		display_error(pv->vf, "Einfügen nicht möglich", err_message);
		g_free(err_message);

		return;
	}

	page_doc = viewer_page->pdf_document_page->page_akt;
	if (pos == pv->arr_pages->len) page_doc++;

	//Seiten werden eingefügt, so daß Seite an Position page_doc nach hinten geschoben wird;
	//page_doc dieser Seite ist hinterher page_doc+count
	rc = zond_pdf_document_insert_pages(viewer_page->dd->zpdfd_part->zond_pdf_document,
			page_doc, viewer_page->dd->zpdfd_part, doc_merge, &error);
	pdf_drop_document(pv->zond->ctx, doc_merge);
	g_object_unref(sfp);
	if (rc) {
		display_message(pv->vf, "Fehler Einfügen\n\n", error->message,
				"\n\nViewer wird geschlossen", NULL);
		g_error_free(error);
		viewer_schliessen(pv);

		return;
	}

	//betroffene viewer-seiten einfügen - kann keinen Fehler zurückgeben
	//vorangehende pdf_document_page wird übergeben - außer wenn Einfügen nach letzter Seite
	data_insert.dd = viewer_page->dd;
	data_insert.page_doc = page_doc; //ist die Position der ersten eingefügten Seite!
	data_insert.count = count;
	data_insert.after_last = (pos == pv->arr_pages->len) ? TRUE : FALSE;

	entry.type = JOURNAL_TYPE_PAGES_INSERTED;
	entry.pages_inserted.count = count;
	entry.pages_inserted.zpdfd_part = zpdfd_part_ref(viewer_page->dd->zpdfd_part);

	//viewer_page->pdf_document_page ist - wenn nicht Einfügen nach letzter Seite -
	//im zond_pdf_document nach hinten gerutscht
	viewer_foreach(pv, viewer_page->pdf_document_page,
			seiten_einfuegen_foreach, &data_insert); //seiten_einfügen_foreach gibt nie Fehler zurück

	seiten_refresh_layouts(pv->zond->arr_pv);

	//erste eingefügte Seite!
	entry.pdf_document_page =
			zond_pdf_document_get_pdf_document_page(viewer_page->dd->zpdfd_part->zond_pdf_document, page_doc);

	g_array_append_val(zond_pdf_document_get_arr_journal(viewer_page->dd->zpdfd_part->zond_pdf_document), entry);

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

		gint page_pv = g_array_index(arr_page_pv, gint, i);
		viewer_page = g_ptr_array_index(pv->arr_pages, page_pv);
		pdf_document_page = viewer_page->pdf_document_page;

		zond_pdf_document_mutex_lock(pdf_document_page->document);
		rc = pdf_copy_page(pv->zond->ctx,
				zond_pdf_document_get_pdf_doc(pdf_document_page->document),
				pdf_document_page->page_akt, pdf_document_page->page_akt, doc_dest, -1, &errmsg);
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
