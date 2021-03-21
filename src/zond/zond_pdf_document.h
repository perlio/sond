#ifndef ZOND_PDF_DOCUMENT_H_INCLUDED
#define ZOND_PDF_DOCUMENT_H_INCLUDED

#include <glib-object.h>
#include <gtk/gtk.h>

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>


G_BEGIN_DECLS

#define ZOND_TYPE_PDF_DOCUMENT zond_pdf_document_get_type( )
G_DECLARE_DERIVABLE_TYPE (ZondPdfDocument, zond_pdf_document, ZOND, PDF_DOCUMENT, GObject)



typedef struct _Pdf_Document_Page_Annot
{
    gint idx;
    enum pdf_annot_type type;

    fz_rect rect;

    gint n_quad;
    GArray* arr_quads;
} PdfDocumentPageAnnot;

typedef struct _Document Document;

typedef struct _Pdf_Document_Page
{
    ZondPdfDocument* document; //erhält keine ref
    fz_page* page;
    fz_rect rect;
    fz_display_list* display_list;
    fz_stext_page* stext_page;
    GPtrArray* arr_annots;
} PdfDocumentPage;


struct _ZondPdfDocumentClass
{
    GObjectClass parent_class;

    GPtrArray* arr_pdf_documents;
};


ZondPdfDocument* zond_pdf_document_open( gchar*, gchar** );

void zond_pdf_document_close( ZondPdfDocument* );

G_END_DECLS

#endif // ZOND_PDF_DOCUMENT_H_INCLUDED



