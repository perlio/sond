#ifndef PDF_DATEIEN_H_INCLUDED
#define PDF_DATEIEN_H_INCLUDED

#define BIG_PDF 2000

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#include "../../misc.h"

#define ERROR_MUPDF_R(x,y) { if ( errmsg ) *errmsg = add_string( *errmsg, g_strconcat( \
                        "Bei Aufruf ", __func__, ":\nBei Aufruf " x ":\n", \
                        fz_caught_message( ctx ), NULL ) ); \
                        return y; }

#define ERROR_MUPDF(x) ERROR_MUPDF_R(x,-1)

typedef struct _Projekt Projekt;
typedef struct _Pdf_Document_Page PdfDocumentPage;
typedef int gint;
typedef char gchar;
typedef void *gpointer;
typedef int gboolean;
typedef struct pdf_obj pdf_obj;
typedef struct pdf_document pdf_document;

gint pdf_document_get_dest(fz_context*, pdf_document*, gint, gpointer*,
		gboolean, gchar**);

gint pdf_copy_page(fz_context*, pdf_document*, gint, gint, pdf_document*, gint,
		gchar**);

gint pdf_open_and_authen_document(fz_context*, gboolean, gboolean, const gchar*,
		gchar**, pdf_document**, gint*, GError**);

gint pdf_save(fz_context*, pdf_document*, const gchar*, GError**);

gint pdf_clean(fz_context*, const gchar*, GError**);

gchar* pdf_get_string_from_line(fz_context*, fz_stext_line*, gchar**);

pdf_processor* pdf_new_text_filter_processor(fz_context*, fz_buffer**, gint,
		gchar**);

fz_buffer* pdf_text_filter_page(fz_context*, pdf_obj*, gint, gchar**);
#endif // PDF_DATEIEN_H_INCLUDED
