/*
 zond (pdf_ocr.c) - Akten, Beweisstücke, Unterlagen
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
#include <glib.h>
#include <glib/gstdio.h>

#include "../misc.h"
#include "../sond_fileparts.h"
#include "../sond_log_and_error.h"
#include "../sond_pdf_helper.h"

#include "zond_pdf_document.h"

#include "99conv/general.h"
#include "99conv/test.h"

#define TESS_SCALE 5

gint pdf_ocr_update_content_stream(fz_context *ctx, pdf_obj *page_ref,
		fz_buffer *buf, gchar **errmsg) {
	pdf_obj *obj_content_stream = NULL;
	pdf_document *doc = NULL;

	doc = pdf_pin_document(ctx, page_ref);
	if (!doc)
		ERROR_S_MESSAGE("pdf_pin_document gibt NULL zurück")

	obj_content_stream = pdf_dict_get(ctx, page_ref, PDF_NAME(Contents));

	/* If contents is not a stream it's an array of streams or missing. */
	if (!pdf_is_stream(ctx, obj_content_stream)) {
		/* Create a new stream object to replace the array of streams or missing object. */
		fz_try(ctx) {
			obj_content_stream = pdf_add_object_drop(ctx, doc,
					pdf_new_dict(ctx, doc, 1));
			pdf_dict_put_drop(ctx, page_ref, PDF_NAME(Contents),
					obj_content_stream);
		}
		fz_catch(ctx) {
			ERROR_MUPDF("pdf_add_object_drop")
		}
	}

	fz_try( ctx )
		pdf_update_stream(ctx, doc, obj_content_stream, buf, 0);
	fz_always(ctx)
		pdf_drop_document(ctx, doc);
	fz_catch(ctx)
		ERROR_MUPDF("pdf_update_stream")

	return 0;
}

static gint pdf_ocr_filter_content_stream(fz_context *ctx, pdf_page *page,
		gint flags, gchar **errmsg) {
	fz_buffer *buf = NULL;
	gint rc = 0;

	buf = pdf_text_filter_page(ctx, page, flags, errmsg);
	if (!buf)
		ERROR_S

	rc = pdf_ocr_update_content_stream(ctx, page->obj, buf, errmsg);
	fz_drop_buffer(ctx, buf);
	if (rc)
		ERROR_S

	return 0;
}

static gchar*
pdf_ocr_find_BT(gchar *buf, size_t size) {
	gchar *ptr = NULL;

	ptr = buf;
	while (ptr < buf + size - 1) {
		if (*ptr == 'B' && *(ptr + 1) == 'T')
			return ptr;
		ptr++;
	}

	return NULL;
}

fz_buffer*
pdf_ocr_get_content_stream_as_buffer(fz_context *ctx, pdf_obj *page_ref,
		gchar **errmsg) {
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
		ERROR_MUPDF_R("open and read stream", NULL)

	return buf;
}

static fz_buffer*
pdf_ocr_process_tess_tmp(fz_context *ctx, pdf_obj *page_ref, fz_matrix ctm,
		gchar **errmsg) {
	fz_buffer *buf = NULL;
	fz_buffer *buf_new = NULL;
	size_t size = 0;
	gchar *data = NULL;
	gchar *cm = NULL;
	gchar *BT = NULL;

	buf = pdf_ocr_get_content_stream_as_buffer(ctx, page_ref, errmsg);
	if (!buf)
		ERROR_S_VAL(NULL)

	size = fz_buffer_storage(ctx, buf, (guchar**) &data);

	BT = pdf_ocr_find_BT(data, size);
	if (!BT) {
		fz_drop_buffer(ctx, buf);
		if (errmsg)
			*errmsg = g_strdup(
					"Bei Aufruf pdf_ocr_find_BT:\nKein BT-Token gefunden");

		return NULL;
	}

	fz_try( ctx )
		buf_new = fz_new_buffer(ctx, size + 64);
	fz_catch(ctx) {
		fz_drop_buffer(ctx, buf);
		ERROR_MUPDF_R("fz_new_buffer", NULL);
	}

	cm = g_strdup_printf("\nq\n%g %g %g %g %g %g cm\nBT", ctm.a, ctm.b, ctm.c,
			ctm.d, ctm.e, ctm.f);

	//Komma durch Punkt ersetzen
	for (gint i = 0; i < strlen(cm); i++)
		if (*(cm + i) == ',')
			*(cm + i) = '.';

	fz_try( ctx ) {
		fz_append_data(ctx, buf_new, cm, strlen(cm));
		fz_append_data(ctx, buf_new, BT + 2, size - (BT + 2 - data));
		fz_append_data(ctx, buf_new, "\nQ", 2);
	}fz_always( ctx ) {
		g_free(cm);
		fz_drop_buffer(ctx, buf);
	}fz_catch( ctx ) {
		fz_drop_buffer(ctx, buf_new);
		ERROR_MUPDF_R("fz_append_data", NULL)
	}

	return buf_new;
}

static fz_matrix pdf_ocr_create_matrix(fz_context *ctx, fz_rect rect,
		gfloat scale, gint rotate) {
	gfloat shift_x = 0;
	gfloat shift_y = 0;
	gfloat width = 0;
	gfloat height = 0;

	width = rect.x1 - rect.x0;
	height = rect.y1 - rect.y0;

	fz_matrix ctm1 = fz_scale(scale, scale);
	fz_matrix ctm2 = fz_rotate((float) rotate);

	if (rotate == 90)
		shift_x = height;
	else if (rotate == 180) {
		shift_x = height;
		shift_y = width;
	} else if (rotate == 270)
		shift_y = width;

	fz_matrix ctm = fz_concat(ctm1, ctm2);

	ctm.e = shift_x;
	ctm.f = shift_y;

	return ctm;
}

typedef struct _Tess_Recog {
	TessBaseAPI *handle;
	ETEXT_DESC *monitor;
} TessRecog;

static gpointer pdf_ocr_tess_recog(gpointer data) {
	gint rc = 0;

	TessRecog *tess_recog = (TessRecog*) data;

	rc = TessBaseAPIRecognize(tess_recog->handle, tess_recog->monitor);

	if (rc)
		return GINT_TO_POINTER(1);

	return NULL;
}

static gboolean pdf_ocr_cancel(void *cancel_this) {
	volatile gboolean *cancelFlag = (volatile gboolean*) cancel_this;
	return *cancelFlag;
}

static gint pdf_ocr_tess_page(InfoWindow *info_window, TessBaseAPI *handle,
		fz_pixmap *pixmap, gchar **errmsg) {
	gint rc = 0;
	ETEXT_DESC *monitor = NULL;
	gint progress = 0;

	monitor = TessMonitorCreate();
	TessMonitorSetCancelThis(monitor, &(info_window->cancel));
	TessMonitorSetCancelFunc(monitor, (TessCancelFunc) pdf_ocr_cancel);

	TessRecog tess_recog = { handle, monitor };
	GThread *thread_recog = g_thread_new("recog", pdf_ocr_tess_recog,
			&tess_recog);

	info_window_set_progress_bar(info_window);

	while (progress < 100 && !(info_window->cancel)) {
		progress = TessMonitorGetProgress(monitor);
		info_window_set_progress_bar_fraction(info_window,
				((gdouble) progress) / 100);
	}

	rc = GPOINTER_TO_INT(g_thread_join(thread_recog));
	TessMonitorDelete(monitor);

	if (rc && !(info_window->cancel)) {
		if (errmsg) *errmsg = g_strdup(
				"Tesseract: Fehler bei Aufruf von Recognize");

		return -1;
	}

	LOG_INFO("Mean Confidence: %u", TessBaseAPIMeanTextConf(handle));

	return 0;
}

static fz_pixmap*
pdf_ocr_render_pixmap(fz_context *ctx, pdf_document *doc, float scale,
		gchar **errmsg) {
	pdf_page *page = NULL;
	fz_pixmap *pixmap = NULL;

	page = pdf_load_page(ctx, doc, 0);

	fz_rect rect = pdf_bound_page(ctx, page, FZ_CROP_BOX);
	fz_matrix ctm = pdf_ocr_create_matrix(ctx, rect, scale, 0);

	rect = fz_transform_rect(rect, ctm);

	//per draw-device to pixmap
	fz_try( ctx )
		pixmap = fz_new_pixmap_with_bbox(ctx, fz_device_rgb(ctx),
				fz_irect_from_rect(rect), NULL, 0);
	fz_catch(ctx) {
		fz_drop_page(ctx, &page->super);
		ERROR_MUPDF_R("fz_new_pixmap_with_bbox", NULL)
	}

	fz_try( ctx)
		fz_clear_pixmap_with_value(ctx, pixmap, 255);
	fz_catch(ctx) {
		fz_drop_page(ctx, &page->super);
		fz_drop_pixmap(ctx, pixmap);

		ERROR_MUPDF_R("fz_clear_pixmap_with_value", NULL)
	}

	fz_device *draw_device = NULL;
	fz_try(ctx)
		draw_device = fz_new_draw_device(ctx, ctm, pixmap);
	fz_catch(ctx) {
		fz_drop_page(ctx, &page->super);
		fz_drop_pixmap(ctx, pixmap);

		ERROR_MUPDF_R("fz_new_draw_device", NULL)
	}

	fz_try(ctx)
		pdf_run_page(ctx, page, draw_device, fz_identity, NULL);
	fz_always(ctx) {
		fz_close_device(ctx, draw_device);
		fz_drop_device(ctx, draw_device);
		fz_drop_page(ctx, &page->super);
	}
	fz_catch(ctx) {
		fz_drop_pixmap(ctx, pixmap);

		ERROR_MUPDF_R("fz_new_draw_device", NULL)
	}

	return pixmap;
}

//thread-safe
static pdf_document*
pdf_ocr_create_doc_from_page(PdfDocumentPage *pdf_document_page, gint flag,
		gchar **errmsg) {
	gint rc = 0;
	pdf_document *doc_new = NULL;
	pdf_page *page = NULL;

	fz_context *ctx = zond_pdf_document_get_ctx(pdf_document_page->document);
	pdf_document *doc = zond_pdf_document_get_pdf_doc(
			pdf_document_page->document);

	fz_try( ctx )
		doc_new = pdf_create_document(ctx);
	fz_catch(ctx)
		ERROR_MUPDF_R("pdf_create_document", NULL)

	zond_pdf_document_mutex_lock(pdf_document_page->document);
	rc = pdf_copy_page(ctx, doc, pdf_document_page->page_akt,
			pdf_document_page->page_akt, doc_new, 0, errmsg);
	zond_pdf_document_mutex_unlock(pdf_document_page->document);
	if (rc) {
		pdf_drop_document(ctx, doc_new);
		ERROR_S_VAL(NULL)
	}

	fz_try(ctx)
		page = pdf_load_page(ctx, doc_new, 0);
	fz_catch(ctx) {
		pdf_drop_document(ctx, doc_new);
		ERROR_MUPDF_R("pdf_lookup_page_obj", NULL)
	}

	//neues dokument mit einer Seite filtern
	rc = pdf_ocr_filter_content_stream(ctx, page, flag, errmsg);
	fz_drop_page(ctx, &page->super);
	if (rc) {
		pdf_drop_document(ctx, doc_new);
		ERROR_MUPDF_R("pdf_zond_filter_content_stream", NULL);
	}
	return doc_new;
}

//thread-safe
static gint pdf_ocr_page(PdfDocumentPage *pdf_document_page,
		InfoWindow *info_window, TessBaseAPI *handle, gchar **errmsg) {
	gint rc = 0;
	fz_pixmap *pixmap = NULL;
	pdf_document *doc_new = NULL;

	doc_new = pdf_ocr_create_doc_from_page(pdf_document_page, 3, errmsg); //thread-safe
	if (!doc_new)
		ERROR_S

	fz_context *ctx = zond_pdf_document_get_ctx(pdf_document_page->document);

	pixmap = pdf_ocr_render_pixmap(ctx, doc_new, TESS_SCALE, errmsg);
	pdf_drop_document(ctx, doc_new);
	if (!pixmap)
		ERROR_S

	rc = pdf_ocr_tess_page(info_window, handle, pixmap, errmsg);
	fz_drop_pixmap(ctx, pixmap);
	if (rc)
		ERROR_S

	return 0;
}

static GtkWidget*
pdf_ocr_create_dialog(InfoWindow *info_window, gint page) {
	gchar *titel = g_strdup_printf("Seite %i enthält bereits "
			"versteckten Text - Text löschen?", page);
	GtkWidget *dialog = gtk_dialog_new_with_buttons(titel,
			GTK_WINDOW(info_window->dialog), GTK_DIALOG_MODAL, "Ja", 1,
			"Ja für alle", 2, "Nein", 3, "Nein für alle", 4, "Anzeigen", 5,
			"Abbrechen", GTK_RESPONSE_CANCEL, NULL);
	g_free(titel);

	return dialog;
}

//thread-safe
static fz_pixmap*
pdf_ocr_render_images(PdfDocumentPage *pdf_document_page, gchar **errmsg) {
	pdf_document *doc_tmp_orig = NULL;
	fz_pixmap *pixmap = NULL;

	doc_tmp_orig = pdf_ocr_create_doc_from_page(pdf_document_page, 3, errmsg); //thread-safe
	if (!doc_tmp_orig)
		ERROR_S_VAL(NULL)

	fz_context *ctx = zond_pdf_document_get_ctx(pdf_document_page->document);

	pixmap = pdf_ocr_render_pixmap(ctx, doc_tmp_orig, 1.2, errmsg);
	pdf_drop_document(ctx, doc_tmp_orig);
	if (!pixmap)
		ERROR_S_VAL(NULL)

	return pixmap;
}

static gchar*
pdf_ocr_get_text_from_stext_page(fz_context *ctx, fz_stext_page *stext_page,
		gchar **errmsg) {
	gchar *text = "";
	guchar *text_tmp = NULL;
	fz_buffer *buf = NULL;
	fz_output *out = NULL;

	fz_try( ctx )
		buf = fz_new_buffer(ctx, 1024);
	fz_catch(ctx)
		ERROR_MUPDF_R("fz_new_buffer", NULL);

	fz_try(ctx)
		out = fz_new_output_with_buffer(ctx, buf);
	fz_catch(ctx) {
		fz_drop_buffer(ctx, buf);
		ERROR_MUPDF_R("fz_new_output_with_buffer", NULL);
	}

	fz_try(ctx)
		fz_print_stext_page_as_text(ctx, out, stext_page);
	fz_always	( ctx ) {
		fz_close_output(ctx, out);
		fz_drop_output(ctx, out);
	}
	fz_catch( ctx ) {
		fz_drop_buffer(ctx, buf);
		ERROR_MUPDF_R("fz_print_stext_page_as_text", NULL)
	}

	fz_try( ctx )
		fz_terminate_buffer(ctx, buf);
fz_catch	( ctx ) {
		fz_drop_buffer(ctx, buf);
		ERROR_MUPDF_R("fz_terminate_buffer", NULL);
	}

	fz_buffer_storage(ctx, buf, &text_tmp);
	text = g_strdup((gchar* ) text_tmp);
	fz_drop_buffer(ctx, buf);

	return text;
}

//thread-safe
static gint pdf_ocr_show_text(InfoWindow *info_window,
		PdfDocumentPage *pdf_document_page, gchar *text_alt,
		TessBaseAPI *handle, gchar **errmsg) {
	gint rc = 0;
	fz_pixmap *pixmap_orig = NULL;
	gchar *text_neu = NULL;

	fz_context *ctx = zond_pdf_document_get_ctx(pdf_document_page->document);

	//Bisherigen versteckten Text
	//gerenderte Seite ohne sichtbaren Text
	pixmap_orig = pdf_ocr_render_images(pdf_document_page, errmsg); //thread-safe
	if (!pixmap_orig)
		ERROR_S

			//Eigene OCR
			//Wenn angezeigt werden soll, dann muß Seite erstmal OCRed werden
			//Um Vergleich zu haben
	rc = pdf_ocr_page(pdf_document_page, info_window, handle, errmsg); //thread-safe
	if (rc) {
		fz_drop_pixmap(ctx, pixmap_orig);
		ERROR_S
	}
	text_neu = TessBaseAPIGetUTF8Text(handle);

	//dialog erzeugen und erweitern
	GtkWidget *label_alt = gtk_label_new("Gespeicherter Text");
	GtkWidget *label_neu = gtk_label_new("Neuer Text");
	GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(hbox), label_alt, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(hbox), label_neu, FALSE, FALSE, 0);

	GtkWidget *text_view_alt = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view_alt), FALSE);
	gtk_text_buffer_set_text(
			gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view_alt)), text_alt,
			-1);

	GtkWidget *text_view_neu = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view_neu), FALSE);
	gtk_text_buffer_set_text(
			gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view_neu)), text_neu,
			-1);
	TessDeleteText(text_neu);

	GtkWidget *swindow_alt = gtk_scrolled_window_new( NULL, NULL);

	GtkWidget *image_orig = gtk_image_new();
	GdkPixbuf *pixbuf_orig = gdk_pixbuf_new_from_data(pixmap_orig->samples,
			GDK_COLORSPACE_RGB, FALSE, 8, pixmap_orig->w, pixmap_orig->h,
			pixmap_orig->stride, NULL, NULL);
	gtk_image_set_from_pixbuf(GTK_IMAGE(image_orig), pixbuf_orig);

	GtkWidget *swindow_neu = gtk_scrolled_window_new( NULL, NULL);

	gtk_container_add(GTK_CONTAINER(swindow_alt), text_view_alt);
	gtk_container_add(GTK_CONTAINER(swindow_neu), text_view_neu);

	GtkWidget *hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(hbox2), swindow_alt, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(hbox2), image_orig, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox2), swindow_neu, TRUE, TRUE, 0);

	GtkWidget *swindow = gtk_scrolled_window_new( NULL, NULL);
	gtk_scrolled_window_set_propagate_natural_height(
			GTK_SCROLLED_WINDOW(swindow), TRUE);
	gtk_container_add(GTK_CONTAINER(swindow), hbox2);

	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), swindow, FALSE, FALSE, 0);

	GtkWidget *dialog = pdf_ocr_create_dialog(info_window,
			pdf_document_page->page_akt + 1);

	GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	gtk_box_pack_start(GTK_BOX(content_area), vbox, FALSE, FALSE, 0);

	gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), 5, FALSE);

	//anzeigen
	gtk_window_maximize(GTK_WINDOW(dialog));
	gtk_widget_show_all(dialog);

	//neue Abfrage
	rc = gtk_dialog_run(GTK_DIALOG(dialog));

	gtk_widget_destroy(dialog);
	fz_drop_pixmap(ctx, pixmap_orig); //wird nicht (!) mit widget zerstört

	return rc;
}



/*
 // EXPERIMENTELL!!!
 gint
 pdf_change_hidden_text( fz_context* ctx, pdf_obj* page_ref, gchar** errmsg )
 {
 pdf_obj* obj_contents = NULL;
 fz_stream* stream = NULL;
 pdf_token tok = PDF_TOK_NULL;
 gint idx = -1;
 GArray* arr_zond_token = NULL;
 fz_buffer* buf = NULL;
 gint rc = 0;

 //Stream doc_text
 obj_contents = pdf_dict_get( ctx, page_ref, PDF_NAME(Contents) );

 fz_try( ctx ) stream = pdf_open_contents_stream( ctx, pdf_get_bound_document( ctx, page_ref ), obj_contents );
 fz_catch( ctx ) ERROR_MUPDF( "pdf_open_contents_stream" )

 arr_zond_token = g_array_new( FALSE, FALSE, sizeof( ZondToken ) );
 g_array_set_clear_func( arr_zond_token, pdf_ocr_free_zond_token );

 while ( tok != PDF_TOK_EOF )
 {
 ZondToken zond_token = { 0, };
 pdf_lexbuf lxb;

 pdf_lexbuf_init( ctx, &lxb, PDF_LEXBUF_SMALL );

 tok = pdf_lex( ctx, stream, &lxb );
 zond_token.tok = tok;

 if ( tok == PDF_TOK_REAL ) zond_token.f = lxb.f;
 else if ( tok == PDF_TOK_INT ) zond_token.i = lxb.i;
 else if ( tok == PDF_TOK_NAME || tok == PDF_TOK_KEYWORD )
 zond_token.s = g_strdup( lxb.scratch );
 else if ( tok == PDF_TOK_STRING )
 {
 zond_token.gba = g_byte_array_new( );
 g_byte_array_append( zond_token.gba, (guint8*) lxb.scratch, lxb.len );
 }

 pdf_lexbuf_fin( ctx, &lxb );

 g_array_append_val( arr_zond_token, zond_token );
 idx++;

 if ( tok == PDF_TOK_KEYWORD && !g_strcmp0( zond_token.s, "Tr" ) )
 {
 if ( g_array_index( arr_zond_token, ZondToken, idx - 1 ).i == 3 )
 g_array_index( arr_zond_token, ZondToken, idx - 1 ).i = 0;
 }
 }

 buf = pdf_ocr_reassemble_buffer( ctx, arr_zond_token, errmsg );
 g_array_unref( arr_zond_token );
 if ( !buf ) ERROR_S

 rc = pdf_ocr_update_content_stream( ctx, page_ref, buf, errmsg );
 fz_drop_buffer( ctx, buf );
 if ( rc ) ERROR_S


 //Dann Font-Dict
 pdf_obj* f_0_0 = NULL;

 fz_try( ctx ) pdf_flatten_inheritable_page_items( ctx, page_ref );
 fz_catch( ctx ) ERROR_MUPDF( "get page_ref" )

 pdf_obj* resources = pdf_dict_get( ctx, page_ref, PDF_NAME(Resources) );
 pdf_obj* font = pdf_dict_get( ctx, resources, PDF_NAME(Font) );

 pdf_obj* f_0_0_name = pdf_new_name( ctx, "f-0-0" );
 f_0_0 = pdf_dict_get( ctx, font, f_0_0_name );
 pdf_drop_obj( ctx, f_0_0_name );

 if ( !f_0_0 ) return 0; //Font nicht vorhanden - nix mehr zu tun

 //Einträge löschen
 gint len = pdf_dict_len( ctx, f_0_0 );
 for ( gint i = 0; i < len; i++ )
 {
 pdf_dict_del( ctx, f_0_0, pdf_dict_get_key( ctx, f_0_0, 0 ) );
 }

 pdf_dict_put( ctx, f_0_0, PDF_NAME(Type), PDF_NAME(Font) );
 pdf_dict_put( ctx, f_0_0, PDF_NAME(Subtype), PDF_NAME(Type1) );
 pdf_obj* font_name = pdf_new_name( ctx, "Times-Roman" );
 pdf_dict_put( ctx, f_0_0, PDF_NAME(BaseFont), font_name );
 //    pdf_dict_put( ctx, f_0_0, PDF_NAME(Encoding), PDF_NAME(WinAnsiEndcoding) );
 pdf_drop_obj( ctx, font_name );

 return 0;
 }
 */

