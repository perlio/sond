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

gint pdf_open_and_authen_document(fz_context*, gboolean, gboolean, SondFilePartPDFPageTree*,
		gchar**, pdf_document**, gint*, GError**);

gint pdf_save(fz_context*, pdf_document*, SondFilePartPDFPageTree*, GError**);

gint pdf_clean(fz_context*, SondFilePartPDFPageTree*, GError**);

gchar* pdf_get_string_from_line(fz_context*, fz_stext_line*, gchar**);

pdf_processor* pdf_new_text_filter_processor(fz_context*, fz_buffer**, gint,
		gchar**);

fz_buffer* pdf_text_filter_page(fz_context*, pdf_obj*, gint, gchar**);

gint pdf_annot_delete(fz_context*, pdf_annot*, GError**);

gint pdf_annot_change(fz_context*, pdf_annot*, gint, Annot, GError**);

pdf_annot* pdf_annot_create(fz_context*, pdf_page*, gint, Annot, GError**);

gboolean pdf_annot_get_annot(fz_context*, pdf_annot*, Annot*, GError**);

pdf_annot* pdf_annot_lookup_obj(fz_context*, pdf_page*, pdf_obj*);

gint pdf_page_rotate(fz_context*, pdf_obj*, gint, GError**);

gint pdf_get_names_tree_dict(fz_context*, pdf_document*,
		pdf_obj*, pdf_obj**, GError**);

gint pdf_walk_names_dict(fz_context*, pdf_obj*, pdf_cycle_list*,
		gint (*) (fz_context*, pdf_obj*, gchar const*,
				gpointer, GError**), gpointer, GError**);

#endif // PDF_DATEIEN_H_INCLUDED
