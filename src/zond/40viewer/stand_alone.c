/*
zond (stand_alone.c) - Akten, Beweisstücke, Unterlagen
Copyright (C) 2020  pelo america

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "../global_types.h"
#include "../error.h"

#include "../99conv/mupdf.h"
#include "../99conv/general.h"

#include "viewer.h"
#include "document.h"

#include "../../misc.h"

#include <gtk/gtk.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>



static void
pv_activate_widgets( PdfViewer* pv, gboolean activ )
{
    gtk_widget_set_sensitive( pv->entry, activ );
    gtk_widget_set_sensitive( pv->entry_search, activ );
    gtk_widget_set_sensitive( pv->button_vorher, activ );
    gtk_widget_set_sensitive( pv->button_nachher, activ );
    gtk_widget_set_sensitive( pv->item_drehen, activ );
    gtk_widget_set_sensitive( pv->item_einfuegen, activ );
    gtk_widget_set_sensitive( pv->item_loeschen, activ );
    gtk_widget_set_sensitive( pv->item_entnehmen, activ );
    gtk_widget_set_sensitive( pv->item_ocr, activ );

    gtk_widget_set_sensitive( pv->item_schliessen, activ );

    if ( !activ )
    {
        gtk_entry_set_text( GTK_ENTRY(pv->entry), "" );
        gtk_entry_set_text( GTK_ENTRY(pv->entry_search), "" );
        gtk_label_set_text( GTK_LABEL(pv->label_anzahl), "" );
    }

    return;
}


static void
pv_schliessen_datei( PdfViewer* pv )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    if ( gtk_widget_get_sensitive( pv->button_speichern ) )
    {
        rc = abfrage_frage( pv->vf, "PDF geändert", "Speichern?", NULL );
        if ( rc == GTK_RESPONSE_YES )
        {
            rc = mupdf_save_document( pv->dd->document, &errmsg );
            if ( rc )
            {
                errmsg = add_string( g_strdup( "Dokument kann nicht gespeichert "
                        "werden: " ), errmsg );
                rc = abfrage_frage( pv->vf, errmsg, "Trotzdem schließen?", NULL );
                g_free( errmsg );

                if ( rc == GTK_RESPONSE_NO )
                {
                    pv->dd->document->doc =
                            mupdf_dokument_oeffnen( pv->dd->document->ctx,
                            pv->dd->document->path, &errmsg );
                    if ( !pv->dd->document->doc )
                    {
                        meldung( pv->vf, "Dokument konnte nicht erneut geöffnet werden -\n\n"
                                "Bei Aufruf mupdf_dokument_oeffnen:\n", errmsg,
                                "\n\nViewer wird geschlossen", NULL );
                        g_free( errmsg );
                    }
                    else return;
                }
            }
            pv->dd->document->doc = NULL;
        }

        pv->dd->document->dirty = FALSE;
        gtk_widget_set_sensitive( pv->button_speichern, FALSE );
    }

    viewer_close_thread_pools( pv );

    //thumbs leeren
    GtkListStore* store_thumb =
            GTK_LIST_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(pv->tree_thumb) ));
    gtk_list_store_clear( store_thumb );

    gtk_layout_set_size( GTK_LAYOUT(pv->layout), 0, 0 );
    if ( pv->dd ) document_free_displayed_documents( pv->zond, pv->dd );
    pv->dd = NULL;

    pv_activate_widgets( pv, FALSE );

    //Arrays zurücksetzen
    g_array_remove_range( pv->arr_text_treffer, 0, pv->arr_text_treffer->len );
    g_ptr_array_remove_range( pv->arr_pages, 0, pv->arr_pages->len );

    return;
}


static gint
pv_oeffnen_datei( PdfViewer* pv, gchar* path, gchar** errmsg )
{
    DisplayedDocument* dd = document_new_displayed_document( pv->zond, path, NULL, errmsg );
    if ( !dd ) ERROR_PAO( "document_new_displayed_document" )

    viewer_display_document( pv, dd, 0, 0 );

    pv_activate_widgets( pv, TRUE );

    return 0;
}


void
cb_pv_sa_beenden( GtkWidget* item, gpointer data )
{
    PdfViewer* pv = (PdfViewer*) data;

    Projekt* zond = pv->zond;

    if ( pv->dd ) pv_schliessen_datei( pv );

    g_ptr_array_unref( pv->arr_pages );
    g_array_unref( pv->arr_text_treffer );

    gtk_widget_destroy( pv->vf );
    g_free( pv );

    if ( !zond->arr_pv->len ) //falls letztes viewer-Fenster:
    {
        g_ptr_array_unref( zond->arr_docs );
        g_ptr_array_unref( zond->arr_pv );

        fz_drop_context( zond->ctx );
        g_free( zond );
    }

    return;
}


void
cb_datei_schliessen( GtkWidget* item, gpointer data )
{
    PdfViewer* pv = (PdfViewer*) data;

    pv_schliessen_datei( pv );

    return;
}


void
cb_datei_oeffnen( GtkWidget* item, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    PdfViewer* pv = (PdfViewer*) data;

    if ( pv->dd )
    {
        //Abfrage, ob Datei geschlossen werden soll
        rc = abfrage_frage( pv->vf, "PDF öffnen", "Geöffnete PDF-Datei schließen?", NULL );
        if ( rc != GTK_RESPONSE_YES ) return;
    }

    gchar* filename = filename_oeffnen( GTK_WINDOW(pv->vf) );
    if ( !filename ) return;

    pv_schliessen_datei( pv );
    rc = pv_oeffnen_datei( pv, filename, &errmsg );
    g_free( filename );
    if ( rc )
    {
        meldung( pv->vf, "Fehler - Datei öffnen\n\n"
                "Bei Aufruf pv_oeffnen_datei:\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


static PdfViewer*
init( GtkApplication* app, Projekt* zond )
{
    PdfViewer* pv = viewer_start_pv( zond );

    g_signal_connect( pv->vf, "delete-event", G_CALLBACK(cb_pv_sa_beenden),
            (gpointer) pv );

    gtk_application_add_window( app, GTK_WINDOW(pv->vf) );

    pv_activate_widgets( pv, FALSE );

    return pv;
}


static void
open_app( GtkApplication* app, gpointer files, gint n_files, gchar *hint,
        gpointer user_data )
{
    Projekt** zond = (Projekt**) user_data;

    PdfViewer* pv = init( app, *zond );
    if ( !pv ) return;

    gint rc = 0;
    gchar* errmsg = NULL;

    GFile** g_file;
    g_file = (GFile**) files;

    gchar* uri = g_file_get_uri( g_file[0] );
    gchar* uri_unesc = g_uri_unescape_string( uri, NULL );
    g_free( uri );

    rc = pv_oeffnen_datei( pv, uri_unesc + 8, &errmsg );
    g_free( uri_unesc );
    if ( rc )
    {
        meldung( pv->vf, "Fehler - Datei öffnen:\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


static void
activate_app (GtkApplication* app, gpointer user_data)
{
    Projekt** zond = (Projekt**) user_data;

    if ( (*zond)->arr_pv->len ) return;

    PdfViewer* pv = init( app, *zond );
    if ( !pv ) return;

    return;
}


static void
startup_app( GtkApplication* app, gpointer user_data )
{
    Projekt** zond = (Projekt**) user_data;

    *zond = g_malloc0( sizeof( Projekt ) );

    (*zond)->ctx = mupdf_init( NULL);
    if ( !((*zond)->ctx) )
    {
        g_free( *zond );
        return;
    }

    (*zond)->settings = g_settings_new( "de.perlio.zondPV" );

    (*zond)->arr_pv = g_ptr_array_new( );
    (*zond)->arr_docs = g_ptr_array_new( );

    return;
}


gint
main( gint argc, gchar** argv )
{
    Projekt* zond = NULL;
    GtkApplication* app = NULL;

    //ApplicationApp erzeugen
    app = gtk_application_new ( "de.perlio.zondPV", G_APPLICATION_HANDLES_OPEN );

    //und starten
    g_signal_connect( app, "startup", G_CALLBACK (startup_app), &zond );
    g_signal_connect( app, "activate", G_CALLBACK (activate_app), &zond );
    g_signal_connect( app, "open", G_CALLBACK (open_app), &zond );

    gint status = g_application_run( G_APPLICATION (app), argc, argv );

    g_object_unref (app);

    return status;
}
