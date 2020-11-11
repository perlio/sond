#include "annot.h"

#include "../99conv/mupdf.h"
#include "../99conv/general.h"
#include "../99conv/pdf.h"

#include "../global_types.h"
#include "../error.h"

#include"../../misc.h"

#include <gtk/gtk.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>


static void
document_free_page( gpointer* data )
{
    if ( data == NULL ) return;

    DocumentPage* document_page = (DocumentPage*) data;

    fz_drop_page( document_page->document->ctx, document_page->page );
    fz_drop_stext_page( document_page->document->ctx, document_page->stext_page );
    fz_drop_display_list( document_page->document->ctx, document_page->display_list );
    annot_free_pv_annot_page( document_page->pv_annot_page );

    g_free( document_page );

    return;
}


static gint
document_load_page( Document* document, gint page_doc, gchar** errmsg )
{
    DocumentPage* document_page = NULL;

    if ( g_ptr_array_index( document->pages, page_doc ) ) return 1;

    document_page = g_malloc0( sizeof( DocumentPage ) );

    document_page->document = document;

    fz_try( document->ctx ) document_page->page =
            fz_load_page( document->ctx, document->doc, page_doc );
    fz_catch( document->ctx )
    {
        g_free( document_page );
        ERROR_MUPDF_CTX( "fz_load_page", document->ctx );
    }

    document_page->rect = fz_bound_page( document->ctx, document_page->page );

    fz_try( document->ctx ) document_page->display_list =
            fz_new_display_list( document->ctx, document_page->rect );
    fz_catch( document->ctx )
    {
        fz_drop_page( document->ctx, document_page->page );
        g_free( document_page );
        ERROR_MUPDF_CTX( "fz_new_display_list", document->ctx );
    }

    fz_try( document->ctx ) document_page->stext_page =
            fz_new_stext_page( document->ctx, document_page->rect );
    fz_catch( document->ctx )
    {
        fz_drop_display_list( document->ctx, document_page->display_list );
        fz_drop_page( document->ctx, document_page->page );
        g_free( document_page );
        ERROR_MUPDF_CTX( "fz_new_stext_page", document->ctx )
    }

    annot_load_pv_annot_page( document_page );

    ((document->pages)->pdata)[page_doc] = document_page;

    return 0;
}


/**
 ** page_doc: Stelle, an der Seite eingefügt wird
 ** -1: wird angehangen
 **/
gint
document_insert_pages( Document* document, gint page_doc,
        gint count, gchar** errmsg )
{
    for ( gint i = page_doc; i < document->pages->len; i++ )
    {
        fz_drop_page( document->ctx, ((DocumentPage*) g_ptr_array_index( document->pages, i ))->page );
        ((DocumentPage*) g_ptr_array_index( document->pages, i ))->page = NULL;
    }

    for ( gint i = 0; i < count; i++ )
    {
        gint rc = 0;

        g_ptr_array_insert( document->pages, page_doc + i, NULL );

        rc = document_load_page( document, page_doc + i, errmsg );
        if ( rc == -1 ) ERROR_PAO( "document_load_page" )
    }

    for ( gint i = page_doc + count; i < document->pages->len; i++ )
    {
        fz_try( document->ctx ) ((DocumentPage*) g_ptr_array_index( document->pages, i ))->page = fz_load_page( document->ctx, document->doc, i );
        fz_catch( document->ctx ) ERROR_MUPDF_CTX( "fz_load_page", document->ctx );
    }

    return 0;
}


Document*
document_geoeffnet( Projekt* zond, const gchar* rel_path )
{
    for ( gint u = 0; u < zond->arr_docs->len; u++ )
    {
        Document* document = g_ptr_array_index( zond->arr_docs, u );
        if ( !strcmp( document->path, rel_path ) ) return document;
    }

    return NULL;
}


static void
document_drop_document( Projekt* zond, Document* document )
{
    document->ref_count--;
    if ( document->ref_count == 0 )
    {
        g_ptr_array_unref( document->pages );

        g_free( document->path );

        g_mutex_clear( &document->mutex_doc );

        fz_drop_document( document->ctx, document->doc );
        mupdf_close_context( document->ctx );

        g_ptr_array_remove_fast( zond->arr_docs, document );

        g_free( document );
    }

    return;
}


static Document*
document_ref_document( Document* document )
{
    document->ref_count++;

    return document;
}


static Document*
document_new_document( const gchar* rel_path, gchar** errmsg )
{
    gint rc = 0;
    Document* document = 0;
    gint number_of_pages = 0;

    document = g_malloc0( sizeof( Document ) );

    document->ctx = mupdf_init( );
    if ( !document->ctx ) ERROR_PAO_R( "mupdf_init", NULL )

    document->doc = mupdf_dokument_oeffnen( document->ctx, rel_path, errmsg );
    if ( !document->doc )
    {
        mupdf_close_context( document->ctx );
        g_free( document );
        ERROR_PAO_R( "mupdf_dokument_oeffnen", NULL )
    }
    number_of_pages = fz_count_pages( document->ctx, document->doc );
    if ( number_of_pages == 0 )
    {
        fz_drop_document( document->ctx, document->doc );
        mupdf_close_context( document->ctx );
        g_free( document );
        if ( errmsg ) *errmsg = g_strdup( "Dokument enthält keine Seiten" );

        return NULL;
    }

    g_mutex_init( &document->mutex_doc );

    document->path = g_strdup( rel_path );
    document->ref_count = 1;

    document->pages = g_ptr_array_sized_new( number_of_pages );
    g_ptr_array_set_free_func( document->pages, (GDestroyNotify) document_free_page );

    for ( gint i = 0; i < number_of_pages; i++ )
            g_ptr_array_add( document->pages, NULL );

    for ( gint i = 0; i < document->pages->len; i++ )
    {
        rc = document_load_page( document, i, errmsg );
        if ( rc == -1 )
        {
            g_ptr_array_unref( document->pages );
            g_free( document->path );
            g_mutex_clear( &document->mutex_doc );
            fz_drop_document( document->ctx, document->doc );
            mupdf_close_context( document->ctx );
            g_free( document );

            ERROR_PAO_R( "document_load_page", NULL )
        }
        //Wenn 1 einfach weiter; Seite wurde halt schon geladen
    }

    return document;
}


static void
document_free_displayed_document( Projekt* zond, DisplayedDocument* dd )
{
    document_drop_document( zond, dd->document );
    g_free( dd->anbindung );
    g_free( dd );

    return;
}


void
document_free_displayed_documents( Projekt* zond, DisplayedDocument* dd )
{
    if ( !dd ) return;

    do
    {
        DisplayedDocument* next = dd->next;
        document_free_displayed_document( zond, dd );
        dd = next;
    }
    while ( dd );

    return;
}


DisplayedDocument*
document_new_displayed_document( Projekt* zond, const gchar* rel_path,
        Anbindung* anbindung, gchar** errmsg )
{
    DisplayedDocument* dd = NULL;
    Document* document = NULL;

    for ( gint i = 0; i < zond->arr_docs->len; i++ )
    {
        document = g_ptr_array_index( zond->arr_docs, i );
        if ( !g_strcmp0( document->path, rel_path ) ) break;
        else document = NULL;
    }

    dd = g_malloc0( sizeof( DisplayedDocument ) );

    if ( document ) dd->document = document_ref_document( document );
    else
    {
        dd->document = document_new_document( rel_path, errmsg );
        if ( !(dd->document) )
        {
            g_free( dd );
            ERROR_PAO_R( "document_new_document", NULL );
        }
        g_ptr_array_add( zond->arr_docs, dd->document );
    }

    if ( anbindung )
    {
        dd->anbindung = g_malloc0( sizeof( Anbindung ) );
        dd->anbindung->von = anbindung->von;
        dd->anbindung->bis = anbindung->bis;
    }

    return dd;
}


gint
document_get_num_of_pages_of_dd( DisplayedDocument* dd )
{
    gint anz_seiten = 0;

    if ( dd->anbindung ) anz_seiten = dd->anbindung->bis.seite -
            dd->anbindung->von.seite + 1;
    else anz_seiten = dd->document->pages->len;

    return anz_seiten;
}


gint
document_get_num_of_pages_of_pv( PdfViewer* pv )
{
    gint anz_seiten = 0;
    DisplayedDocument* dd = NULL;

    dd = pv->dd;
    do anz_seiten+= document_get_num_of_pages_of_dd( dd );
    while ( (dd = dd->next) );

    return anz_seiten;
}


DocumentPage*
document_get_document_page_from_pv( PdfViewer* pv, gint page )
{
    gint pages_dd = 0;
    gint zaehler = 0;
    DisplayedDocument* dd = NULL;

    dd = pv->dd;

    do
    {
        pages_dd = document_get_num_of_pages_of_dd( dd );

        if ( page < zaehler + pages_dd )
        {
            gint von = 0;
            if ( dd->anbindung ) von = dd->anbindung->von.seite;

            return (DocumentPage*) g_ptr_array_index( dd->document->pages,
                    page - zaehler + von );
        }

        zaehler += pages_dd;
    } while ( (dd = dd->next) );

    return NULL;
}


DisplayedDocument*
document_get_dd( PdfViewer* pv, gint page, DocumentPage** document_page,
        gint* page_dd, gint* page_doc )
{
    gint pages_dd = 0;
    gint zaehler = 0;
    DisplayedDocument* dd = NULL;

    dd = pv->dd;

    do
    {
        pages_dd = document_get_num_of_pages_of_dd( dd );

        if ( page < zaehler + pages_dd )
        {
            gint von = 0;
            if ( dd->anbindung ) von = dd->anbindung->von.seite;

            if ( page_dd ) *page_dd = page - zaehler;
            if ( document_page ) *document_page =
                    g_ptr_array_index( dd->document->pages, page - zaehler + von );
            if ( page_doc ) *page_doc = page - zaehler + von;

            return dd;
        }

        zaehler += pages_dd;
    } while ( (dd = dd->next) );

    return NULL;
}


gint
document_get_page_dd( DisplayedDocument* dd, gint page_doc )
{
    gint von = 0;
    gint bis = 0;

    if ( dd->anbindung )
    {
        von = dd->anbindung->von.seite;
        bis = dd->anbindung->bis.seite;
    }
    else bis = dd->document->pages->len - 1;

    if ( page_doc >= von && page_doc <= bis ) return page_doc - von;

    return -1;
}


gint
document_get_page_pv( PdfViewer* pv, DisplayedDocument* dd, gint page_dd )
{
    gint zaehler = 0;
    DisplayedDocument* dd_vergleich = NULL;

    dd_vergleich = pv->dd;

    do
    {
        if ( dd_vergleich == dd ) return zaehler + page_dd;
        zaehler += document_get_num_of_pages_of_dd( dd_vergleich );
    } while ( (dd_vergleich = dd_vergleich->next) );

    return -1;
}


gint
document_get_index_of_document_page( DocumentPage* document_page )
{
    guint index = 0;

    if ( g_ptr_array_find( document_page->document->pages, document_page, &index ) )
            return (gint) index;
    else return -1;
}
