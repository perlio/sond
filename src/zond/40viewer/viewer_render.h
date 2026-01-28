/*
 zond (viewer_render.h) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2026  pelo america

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

#ifndef VIEWER_RENDER_H_INCLUDED
#define VIEWER_RENDER_H_INCLUDED

#include <mupdf/fitz.h>

typedef struct _Pdf_Viewer PdfViewer;
typedef struct _Pdf_Document_Page PdfDocumentPage;
typedef void* gpointer;
typedef int gint;
typedef char gchar;


void viewer_render_wait_for_transfer(PdfDocumentPage* pdf_document_page);

void viewer_close_thread_pool_and_transfer(PdfViewer *pdfv);

void viewer_render_response_free(gpointer data);

gint viewer_render_stext_page_fast(fz_context *ctx,
		PdfDocumentPage *pdf_document_page, gchar **errmsg);

void viewer_render_thread(PdfViewer *pv, gint page);

void cb_viewer_render_visible_thumbs(PdfViewer *pv);

void viewer_render_get_visible_pages(PdfViewer *pv, gint *von, gint *bis);

void cb_viewer_render_visible_pages(PdfViewer* pv);

void cb_viewer_render_print(GtkButton *button, gpointer data);

#endif // VIEWER_RENDER_H_INCLUDED
