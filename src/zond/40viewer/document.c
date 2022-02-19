#include <gtk/gtk.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#include "../zond_pdf_document.h"

#include "../99conv/general.h"
#include "../99conv/pdf.h"

#include "../global_types.h"
#include "../error.h"

#include"../../misc.h"



void
document_free_displayed_documents( DisplayedDocument* dd )
{
    if ( !dd ) return;

    do
    {
        DisplayedDocument* next = dd->next;

        zond_pdf_document_close( dd->zond_pdf_document );
        g_free( dd->anbindung );
        g_free( dd );

        dd = next;
    }
    while ( dd );

    return;
}


DisplayedDocument*
document_new_displayed_document( const gchar* rel_path,
        Anbindung* anbindung, gchar** errmsg )
{
    ZondPdfDocument* zond_pdf_document = NULL;
    DisplayedDocument* dd = NULL;

    zond_pdf_document = zond_pdf_document_open( rel_path,
            (anbindung) ? anbindung->von.seite : 0, (anbindung) ? anbindung->bis.seite : -1, errmsg );
    if ( !zond_pdf_document ) ERROR_SOND_VAL( "zond_pdf_document_open", NULL )

    dd = g_malloc0( sizeof( DisplayedDocument ) );
    dd->zond_pdf_document = zond_pdf_document;

    if ( anbindung )
    {
        dd->anbindung = g_malloc0( sizeof( Anbindung ) );

        dd->anbindung->von = anbindung->von;
        dd->anbindung->bis = anbindung->bis;
    }

    return dd;
}


static gint
document_get_num_of_pages_of_dd( DisplayedDocument* dd )
{
    gint anz_seiten = 0;

    if ( dd->anbindung ) anz_seiten = dd->anbindung->bis.seite -
            dd->anbindung->von.seite + 1;
    else anz_seiten = zond_pdf_document_get_number_of_pages( dd->zond_pdf_document );

    return anz_seiten;
}


DisplayedDocument*
document_get_dd( PdfViewer* pv, gint page, PdfDocumentPage** pdf_document_page,
        gint* page_dd, gint* page_doc )
{
    gint zaehler = 0;
    DisplayedDocument* dd = NULL;

    dd = pv->dd;

    do
    {
        gint pages_dd = document_get_num_of_pages_of_dd( dd );

        if ( page < zaehler + pages_dd )
        {
            gint von = 0;
            if ( dd->anbindung ) von = dd->anbindung->von.seite;

            if ( page_dd ) *page_dd = page - zaehler;
            if ( pdf_document_page ) *pdf_document_page =
                    g_ptr_array_index( zond_pdf_document_get_arr_pages( dd->zond_pdf_document ), page - zaehler + von );
            if ( page_doc ) *page_doc = page - zaehler + von;

            return dd;
        }

        zaehler += pages_dd;
    } while ( (dd = dd->next) );

    return NULL;
}

