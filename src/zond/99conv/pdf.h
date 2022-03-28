#ifndef PDF_DATEIEN_H_INCLUDED
#define PDF_DATEIEN_H_INCLUDED

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
typedef void* gpointer;
typedef int gboolean;
typedef struct pdf_obj pdf_obj;
typedef struct pdf_document pdf_document;

//globale Variable!!! Böse!
pdf_write_options opts_default = {
        0, // do_incremental
        1, // do_pretty
        1, // do_ascii
        0, // do_compress
        1, // do_compress_images
        1, // do_compress_fonts
        0, // do_decompress
        1, // do_garbage
        0, // do_linear
        1, // do_clean
        1, // do_sanitize
        0, // do_appearance
        PDF_ENCRYPT_KEEP, // do_encrypt
        0, // dont_regenerate_id  Don't regenerate ID if set (used for clean)
        ~0, // permissions
        "", // opwd_utf8[128]
        "", // upwd_utf8[128]
        0 //do snapshot
        };

gint pdf_document_get_dest( fz_context*, pdf_document*, gint, gpointer*,
        gboolean, gchar** );

gint pdf_copy_page( fz_context*, pdf_document*, gint, gint, pdf_document*,
        gint, gchar** );

gint pdf_open_and_authen_document( fz_context*, gboolean, const gchar*, gchar**,
        pdf_document**, gint*, gchar** );

gint pdf_clean( fz_context*, const gchar*, gchar** );

#endif // PDF_DATEIEN_H_INCLUDED
