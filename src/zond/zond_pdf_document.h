#ifndef ZOND_PDF_DOCUMENT_H_INCLUDED
#define ZOND_PDF_DOCUMENT_H_INCLUDED

#include <glib-object.h>
#include <gtk/gtk.h>

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

typedef struct _Anbindung Anbindung;

G_BEGIN_DECLS

#define ZOND_TYPE_PDF_DOCUMENT zond_pdf_document_get_type( )
G_DECLARE_DERIVABLE_TYPE (ZondPdfDocument, zond_pdf_document, ZOND, PDF_DOCUMENT, GObject)

typedef struct _Pdf_Document_Page
{
    ZondPdfDocument* document; //erhält keine ref - muß das mal mit dem const kapieren...
    gint page_doc;
    pdf_obj* obj;
    fz_rect rect;
    gint rotate;
    pdf_page* page;
    fz_display_list* display_list;
    fz_stext_page* stext_page;
    gint thread;
    gpointer thread_pv;
    GPtrArray* arr_annots;
} PdfDocumentPage;

typedef struct _Annot_Text_Markup
{
    gint n_quad;
    GArray* arr_quads;
} AnnotTextMarkup;

typedef struct _Annot_Text
{
    fz_rect rect;
    gboolean open;
    const gchar* name;
} AnnotText;

typedef struct _Pdf_Document_Page_Annot
{
    PdfDocumentPage* pdf_document_page;
    pdf_annot* annot;
    enum pdf_annot_type type;
    gint flags;
    const gchar* content; //Text der Annot gem. Dict-Entry /Content

    union
    {
        AnnotTextMarkup annot_text_markup;
        AnnotText annot_text;
//        AnnotFreeText annot_free_text;
    };
} PdfDocumentPageAnnot;

struct PagesDeleted
{
    gint* pages_remaining;
    fz_context* ctx;
    pdf_document* doc; //hier sollen später die gelöschten Seiten gespeichert sein, um undo zu ermöglichen
};
struct PagesInserted
{
    gint pos;
    gint count;
    Anbindung* anbindung;
};
struct AnnotDeleted
{
    gint index;
};

typedef enum _Journal_Type
{
    JOURNAL_TYPE_PAGES_DELETED,
    JOURNAL_TYPE_PAGES_INSERTED,
    JOURNAL_TYPE_ANNOT_CREATED,
    JOURNAL_TYPE_ANNOT_CHANGED,
    JOURNAL_TYPE_ANNOT_DELETED,
    JOURNAL_TYPE_OCR
} JournalType;

typedef struct _Journal_Entry
{
    JournalType type;
    union
    {
        struct PagesDeleted PagesDeleted;
        struct PagesInserted PagesInserted;
        struct AnnotDeleted AnnotDeleted;
    };
} JournalEntry;

void
free_journal_entry( JournalEntry* entry )
{
    if ( entry->type == JOURNAL_TYPE_PAGES_DELETED )
    {
        g_free( entry->PagesDeleted.pages_remaining );
        pdf_drop_document( entry->PagesDeleted.ctx, entry->PagesDeleted.doc );
        fz_drop_context( entry->PagesDeleted.ctx );
    }
    else if ( entry->type == JOURNAL_TYPE_PAGES_INSERTED )
    {
        g_free( entry->PagesInserted.anbindung );
    }

    return;
}

struct _ZondPdfDocumentClass
{
    GObjectClass parent_class;

    GPtrArray* arr_pdf_documents;
};

void zond_pdf_document_page_load_annots( PdfDocumentPage* );

gint zond_pdf_document_load_page( PdfDocumentPage*, gchar** );

ZondPdfDocument* zond_pdf_document_open( const gchar*, gint, gint, gchar** );

//Gibt Zeiger auf geöffnetes document mit gchar* == path zurück; keine neue ref!
const ZondPdfDocument* zond_pdf_document_is_open( const gchar* );

void zond_pdf_document_unload_page( PdfDocumentPage* );

gint zond_pdf_document_save( ZondPdfDocument*, gchar** );

void zond_pdf_document_close( ZondPdfDocument* );

pdf_document* zond_pdf_document_get_pdf_doc( ZondPdfDocument* );

GPtrArray* zond_pdf_document_get_arr_pages( ZondPdfDocument* );

GArray* zond_pdf_document_get_arr_journal( ZondPdfDocument* );

PdfDocumentPage* zond_pdf_document_get_pdf_document_page( ZondPdfDocument*, gint );

gint zond_pdf_document_get_number_of_pages( ZondPdfDocument* );

fz_context* zond_pdf_document_get_ctx( ZondPdfDocument* );

const gchar* zond_pdf_document_get_file_part( ZondPdfDocument* );

void zond_pdf_document_mutex_lock( const ZondPdfDocument* );

void zond_pdf_document_mutex_unlock( const ZondPdfDocument* );

gint zond_pdf_document_insert_pages( ZondPdfDocument*, gint, fz_context*,
        pdf_document*, gchar** );

G_END_DECLS

#endif // ZOND_PDF_DOCUMENT_H_INCLUDED



