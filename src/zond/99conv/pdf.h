#ifndef PDF_DATEIEN_H_INCLUDED
#define PDF_DATEIEN_H_INCLUDED

#define BIG_PDF 2000

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#include "../../misc.h"
#include "../zond_pdf_document.h"

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

typedef struct _Projekt Projekt;
typedef struct _Pdf_Document_Page PdfDocumentPage;
typedef int gint;
typedef char gchar;
typedef void *gpointer;
typedef int gboolean;
typedef struct pdf_obj pdf_obj;
typedef struct pdf_document pdf_document;
typedef struct _SondFilePartPDF SondFilePartPDF;

gint pdf_document_get_dest(fz_context*, pdf_document*, gint, gpointer*,
		gboolean, gchar**);

gint pdf_annot_change(fz_context*, pdf_annot*, gint, Annot, GError**);

pdf_annot* pdf_annot_create(fz_context*, pdf_page*, gint, Annot, GError**);

pdf_annot* pdf_annot_lookup_index(fz_context*, pdf_page*, gint);

#endif // PDF_DATEIEN_H_INCLUDED
