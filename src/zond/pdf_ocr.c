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
#include <glib/gstdio.h>
#include <tesseract/capi.h>

#include "../misc.h"
#include "../sond_fileparts.h"
#include "../sond_log_and_error.h"
#include "../sond_pdf_helper.h"

#include "zond_pdf_document.h"


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
//	rc = pdf_ocr_filter_content_stream(ctx, page, flag, errmsg);
	fz_drop_page(ctx, &page->super);
	if (rc) {
		pdf_drop_document(ctx, doc_new);
		ERROR_MUPDF_R("pdf_zond_filter_content_stream", NULL);
	}
	return doc_new;
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
//	pixmap_orig = pdf_ocr_render_images(pdf_document_page, errmsg); //thread-safe
	if (!pixmap_orig)
		ERROR_S

			//Eigene OCR
			//Wenn angezeigt werden soll, dann muß Seite erstmal OCRed werden
			//Um Vergleich zu haben
//	rc = pdf_ocr_page(pdf_document_page, info_window, handle, errmsg); //thread-safe
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

