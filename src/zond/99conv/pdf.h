#ifndef PDF_DATEIEN_H_INCLUDED
#define PDF_DATEIEN_H_INCLUDED

#include <mupdf/fitz.h>

typedef struct fz_context fz_context;
typedef struct fz_document fz_document;
typedef struct _Projekt Projekt;
typedef struct _Pdf_Document_Page PdfDocumentPage;

typedef int gint;
typedef char gchar;
typedef void* gpointer;
typedef int gboolean;
typedef struct pdf_obj pdf_obj;
typedef struct pdf_document pdf_document;
typedef struct fz_page fz_page;


gint pdf_document_get_dest( fz_context*, pdf_document*, gint, gpointer*,
        gboolean, gchar** );

gint pdf_get_page_num_from_dest( fz_context*, const gchar*, const gchar*, gchar** );

gint pdf_copy_page( fz_context*, pdf_document*, gint, gint, pdf_document*,
        gint, gchar** );

gint pdf_render_stext_page_direct( PdfDocumentPage*, gchar** );

gint pdf_print_token( fz_context*, fz_stream*, gchar** );

#endif // PDF_DATEIEN_H_INCLUDED
