#include <gtk/gtk.h>

#include "viewer.h"
#include "document.h"
#include "../zond_pdf_document.h"

#include "../99conv/general.h"
#include "../99conv/pdf.h"

#include "../global_types.h"

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
document_new_displayed_document( const gchar* file_part,
        Anbindung* anbindung, gchar** errmsg )
{
    ZondPdfDocument* zond_pdf_document = NULL;
    DisplayedDocument* dd = NULL;

    zond_pdf_document = zond_pdf_document_open( file_part,
                (anbindung) ? anbindung->von.seite : 0, (anbindung) ?
                anbindung->bis.seite : -1, errmsg );
    if ( !zond_pdf_document )
    {
        if ( errmsg && *errmsg ) ERROR_S_MESSAGE_VAL( "zond_pdf_document_open", NULL )
        else return NULL; //Fehler: Passwort funktioniert nicht
    }

    dd = g_malloc0( sizeof( DisplayedDocument ) );
    dd->zond_pdf_document = zond_pdf_document;

    if ( anbindung )
    {
        dd->anbindung = g_malloc0( sizeof( Anbindung ) );

        *(dd->anbindung) = *anbindung;
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


gint
document_save_dd( DisplayedDocument* dd, gboolean ask, GError** error )
{
    //ToDo: Test, ob dd geändert
    //derzeit nur gucken, ob Änderungen irgendwo im Dokument

    GArray* arr_journal = NULL;

    arr_journal = zond_pdf_document_get_arr_journal( dd->zond_pdf_document );

    if ( arr_journal->len > 0 ) //es wurde etwas in diesem doc geändert
    {
        gint rc = 0;

        if ( ask )
        {
            gchar* text_frage = g_strconcat( "PDF-Datei ",
                    zond_pdf_document_get_file_part( dd->zond_pdf_document ),
                    " geändert", NULL );
            rc = abfrage_frage( NULL, text_frage, "Speichern?", NULL );
            g_free( text_frage );
        }
        else rc = GTK_RESPONSE_YES;

        if ( rc == GTK_RESPONSE_YES )
        {
            gint ret = 0;
            gchar* errmsg = NULL;

            zond_pdf_document_mutex_lock( dd->zond_pdf_document );
            ret = zond_pdf_document_save( dd->zond_pdf_document, &errmsg );
            zond_pdf_document_mutex_unlock( dd->zond_pdf_document );
            if ( ret )
            {
                if ( error ) *error = g_error_new( ZOND_ERROR, 0, "%s\n%s", __func__, errmsg );
                g_free( errmsg );

                return -1;
            }
        }
        else if ( rc == GTK_RESPONSE_DELETE_EVENT ) return 1;
    }

    //doc_disk laden

    //Seiten des dd->Anbindung herauslöschen

    //Seiten des dd->Anbindung an die Stelle kopieren

    return 0;
}

