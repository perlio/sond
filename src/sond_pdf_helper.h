/*
 sond (sond_pdf_helper.h) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2026  peloamerica

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

#ifndef SRC_SOND_PDF_HELPER_H_
#define SRC_SOND_PDF_HELPER_H_

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

typedef int gint;
typedef char gchar;
typedef void* gpointer;
typedef int gboolean;

typedef struct _GBytes GBytes;
typedef struct _GError GError;

#define ERROR_MUPDF_R(x,y) { if ( errmsg ) *errmsg = add_string( *errmsg, g_strconcat( \
                        "Bei Aufruf ", __func__, ":\nBei Aufruf " x ":\n", \
                        fz_caught_message( ctx ), NULL ) ); \
                        return y; }

#define ERROR_MUPDF(x) ERROR_MUPDF_R(x,-1)

#define ERROR_PDF_VAL(x) { if (error) *error = g_error_new( \
						g_quark_from_static_string("mupdf"), fz_caught(ctx), \
						"%s\n%s", __func__, fz_caught_message(ctx)); \
						return x; }

#define ERROR_PDF ERROR_PDF_VAL(-1)


fz_buffer* pdf_text_filter_page(fz_context*, pdf_page*, gint, gchar**);

gint pdf_copy_page(fz_context*, pdf_document*, gint, gint, pdf_document*, gint,
		gchar**);

gint pdf_page_rotate(fz_context*, pdf_obj*, gint, GError**);

fz_buffer* pdf_doc_to_buf(fz_context* ctx, pdf_document* doc, GError** error);

pdf_obj* pdf_get_EF_F(fz_context* ctx, pdf_obj* val, gchar const** path, GError** error);

gint pdf_walk_embedded_files(fz_context*, pdf_document*,
		gint (*) (fz_context*, pdf_obj*, pdf_obj*, pdf_obj*, gpointer, GError**),
		gpointer, GError**);

gint pdf_insert_emb_file(fz_context* ctx, pdf_document* doc,
		fz_buffer* buf, gchar const* filename,
		gchar const* mime_type, GError** error);

fz_pixmap* pdf_render_pixmap(fz_context *ctx, pdf_page* page,
		float scale, GError** error);

gint pdf_set_content_stream(fz_context*, pdf_page*, fz_buffer*, GError**);

gint pdf_get_sond_font(fz_context* ctx, pdf_document* doc, pdf_obj**, GError** error);

pdf_obj* pdf_put_sond_font(fz_context* ctx, pdf_document* doc, GError** error);

gint pdf_page_has_hidden_text(fz_context* ctx, pdf_page* page,
		gboolean* hidden, GError** error);

pdf_annot* pdf_annot_lookup_index(fz_context*, pdf_page*, gint);

/**
 * Erzeugt einen seekbaren fz_stream aus einem GBytes-Puffer.
 * Der Stream hält eine eigene Kopie der Daten (via fz_buffer).
 */
fz_stream* sond_gbytes_to_fz_stream(fz_context* ctx, GBytes* bytes, GError** error);

#endif /* SRC_SOND_PDF_HELPER_H_ */
