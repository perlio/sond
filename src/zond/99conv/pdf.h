#ifndef PDF_DATEIEN_H_INCLUDED
#define PDF_DATEIEN_H_INCLUDED

#include <mupdf/fitz.h>
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
typedef void* gpointer;
typedef int gboolean;
typedef struct pdf_obj pdf_obj;
typedef struct pdf_document pdf_document;


gint pdf_document_get_dest( fz_context*, pdf_document*, gint, gpointer*,
        gboolean, gchar** );

gint pdf_copy_page( fz_context*, pdf_document*, gint, gint, pdf_document*,
        gint, gchar** );

gint pdf_clean( fz_context*, const gchar*, gchar** );

#endif // PDF_DATEIEN_H_INCLUDED
