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
    ZondPdfDocument* document; //erhält keine ref - muß das mal mit dem const kapieren...
    pdf_page* page;
    fz_rect rect;
    fz_display_list* display_list;
    fz_stext_page* stext_page;
    GMutex mutex_page;
    GPtrArray* arr_annots;
} PdfDocumentPage;


struct _ZondPdfDocumentClass
{
    GObjectClass parent_class;

    GPtrArray* arr_pdf_documents;
};


ZondPdfDocument* zond_pdf_document_open( const gchar*, gchar** );

//Gibt Zeiger auf geöffnetes document mit gchar* == path zurück; keine neue ref!
const ZondPdfDocument* zond_pdf_document_is_open( const gchar* );

gboolean zond_pdf_document_is_dirty( ZondPdfDocument* );

void zond_pdf_document_set_dirty( ZondPdfDocument*, gboolean );

gint zond_pdf_document_reopen_doc_and_pages( ZondPdfDocument*, gchar** );

void zond_pdf_document_close_doc_and_pages( ZondPdfDocument* );

gint zond_pdf_document_save( ZondPdfDocument*, gchar** );

void zond_pdf_document_close( ZondPdfDocument* );

pdf_document* zond_pdf_document_get_pdf_doc( ZondPdfDocument* );

GPtrArray* zond_pdf_document_get_arr_pages( ZondPdfDocument* );

PdfDocumentPage* zond_pdf_document_get_pdf_document_page( ZondPdfDocument*, gint );

gint zond_pdf_document_get_number_of_pages( ZondPdfDocument* );

fz_context* zond_pdf_document_get_ctx( ZondPdfDocument* );

gchar* zond_pdf_document_get_path( ZondPdfDocument* );

gint zond_pdf_document_get_index( PdfDocumentPage* );

void zond_pdf_document_mutex_lock( const ZondPdfDocument* );

void zond_pdf_document_mutex_unlock( const ZondPdfDocument* );

gint zond_pdf_document_page_refresh( ZondPdfDocument*, gint, gint, gchar** );

gint zond_pdf_document_insert_pages( ZondPdfDocument*, gint, gint, fz_context*,
        pdf_document*, gchar** );


G_END_DECLS

#endif // ZOND_PDF_DOCUMENT_H_INCLUDED



