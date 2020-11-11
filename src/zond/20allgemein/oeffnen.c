/*
zond (oeffnen.c) - Akten, Beweisstücke, Unterlagen
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
#include "../99conv/pdf.h"
#include "../99conv/db_read.h"
#include "../99conv/general.h"

#include "ziele.h"

#include "../40viewer/document.h"
#include "../40viewer/viewer.h"

#include <gtk/gtk.h>
#include <mupdf/fitz.h>

#ifdef _WIN32
#include <windows.h>
//#include <shellapi.h>
#include <shlwapi.h>
#endif // _WIN32


static gboolean
oeffnen_dd_sind_gleich( DisplayedDocument* dd1, DisplayedDocument* dd2 )
{
    DisplayedDocument* dd1_ptr = dd1;
    DisplayedDocument* dd2_ptr = dd2;

    do
    {
        if ( g_strcmp0( dd1_ptr->document->path, dd2_ptr->document->path ) ) return FALSE;
        if ( (dd1_ptr->anbindung && !dd2_ptr->anbindung) ||
                (!dd1_ptr->anbindung && dd2_ptr->anbindung) ) return FALSE;
        if ( (dd1_ptr->anbindung && dd2_ptr->anbindung) &&
                !ziele_1_gleich_2( *dd1_ptr->anbindung, *dd2_ptr->anbindung) ) return FALSE;
        if ( (dd1_ptr->next && !dd2_ptr->next) || (!dd1_ptr->next && dd2_ptr->next) ) return FALSE;

        if ( !dd1_ptr->next && !dd2_ptr->next ) break;

        dd1_ptr = dd1_ptr->next;
        dd2_ptr = dd2_ptr->next;
    }
    while ( 1 );

    return TRUE;
}


static gint
oeffnen_auszug( Projekt* zond, gint node_id, gchar** errmsg )
{
    gint rc = 0;
    gint first_child = 0;
    gint younger_sibling = 0;
    DisplayedDocument* dd = NULL;
    DisplayedDocument* dd_next = NULL;

    first_child = db_get_first_child( zond, BAUM_AUSWERTUNG, node_id, errmsg );

    if ( first_child < 0 ) ERROR_PAO( "db_get_first_child" )
    else if ( first_child == 0 )
    {
        if ( errmsg ) *errmsg = g_strdup( "Auszug anzeigen nicht möglich:\n"
                "Knoten hat keine Kinder" );

        return -1;
    }

    younger_sibling = first_child;
    do
    {
        gchar* rel_path = NULL;
        Anbindung* anbindung = NULL;

        rc = abfragen_rel_path_and_anbindung( zond, BAUM_AUSWERTUNG,
                younger_sibling, &rel_path, &anbindung, errmsg );
        if ( rc == -1 ) ERROR_PAO( "abfragen_rel_path_and_anbindung" )

        if ( rc < 2 && is_pdf( rel_path ) )//rel_path existiert
        {
            DisplayedDocument* dd_new = NULL;

            dd_new = document_new_displayed_document( zond, rel_path, anbindung, errmsg ); //reference "anbindung" wird übernommen
            g_free( rel_path );
            g_free( anbindung );
            if ( !dd_new )
            {
                document_free_displayed_documents( zond, dd );
                ERROR_PAO( "document_new_displayed_document" );
            }

            if ( !dd )
            {
                dd = dd_new;
                dd_next = dd_new;
            }
            else
            {
                dd_next->next = dd_new;
                dd_next = dd_next->next;
            }
        }

        first_child = younger_sibling;

        younger_sibling = db_get_younger_sibling( zond, BAUM_AUSWERTUNG,
                first_child, errmsg );
        if ( younger_sibling < 0 ) ERROR_PAO( "db_get_younger_sibling" )
    }
    while ( younger_sibling > 0 );

    if ( !dd )
    {
        if ( errmsg ) *errmsg = add_string( *errmsg,
                g_strdup( "Keine Dateien/Anbindungen als Kinder" ) );

        return -1;
    }

    //auf schon geöffnet prüfen
    if ( !(zond->state & GDK_SHIFT_MASK) )
    {
        for ( gint i = 0; i < zond->arr_pv->len; i++ )
        {
            PdfViewer* pv_vergleich = g_ptr_array_index( zond->arr_pv, i );

            if ( oeffnen_dd_sind_gleich( pv_vergleich->dd, dd ) )
            {
                gtk_window_present( GTK_WINDOW(pv_vergleich->vf) );
                document_free_displayed_documents( zond, dd );

                return 0;
            }
        }
    }

    PdfViewer* pv = viewer_start_pv( zond );
    viewer_display_document( pv, dd );

    return 0;
}


static gint
oeffnen_internal_viewer( Projekt* zond, const gchar* rel_path, Anbindung* anbindung,
        const PdfPos* pos_pdf, gchar** errmsg )
{
    PdfPos pos_von = { 0 };

    //Neue Instanz oder bestehende?
    if ( !(zond->state & GDK_SHIFT_MASK) )
    {
        //Testen, ob pv mit rel_path schon geöffnet
        for ( gint i = 0; i < zond->arr_pv->len; i++ )
        {
            PdfViewer* pv = g_ptr_array_index( zond->arr_pv, i );
            if ( pv->dd->next == NULL &&
                    !g_strcmp0( rel_path, pv->dd->document->path ) )
            {
                if ( (!pv->dd->anbindung && !anbindung) ||
                        (pv->dd->anbindung && anbindung &&
                        ziele_1_gleich_2( *(pv->dd->anbindung), *anbindung )) )
                {
                    if ( pos_pdf ) pos_von = *pos_pdf;

                    gtk_window_present( GTK_WINDOW(pv->vf) );

                    if ( pos_von.seite > (pv->arr_pages->len - 1) )
                            pos_von.seite = pv->arr_pages->len - 1;

                    viewer_springen_zu_pos_pdf( pv, pos_von, 0.0 );

                    return 0;
                }
            }
        }
    }

    DisplayedDocument* dd = document_new_displayed_document( zond,
            rel_path, anbindung, errmsg );
    if ( !dd ) ERROR_PAO( "document_new_displayed_document" );

    if ( pos_pdf ) pos_von = *pos_pdf;

    PdfViewer* pv = viewer_start_pv( zond );
    viewer_display_document( pv, dd );

    if ( pos_von.seite > (pv->arr_pages->len - 1) ) pos_von.seite = pv->arr_pages->len - 1;
    viewer_springen_zu_pos_pdf( pv, pos_von, 0.0 );

    return 0;
}


static void
close_pid( GPid pid, gint status, gpointer user_data )
{
    g_spawn_close_pid( pid );

    return;
}


gint
oeffnen_datei( Projekt* zond, const gchar* rel_path, Anbindung* anbindung,
        const PdfPos* pos_pdf, gchar** errmsg )
{
    gint rc = 0;
    GError* error = NULL;
    GPid g_pid = 0;

    //Typ der Datei ermitteln
    //Sonderbehandung, falls pdf-Datei
    if ( is_pdf( rel_path ) )
    {
        //Wenn pdf-Datei und internalviewer gewählt
        if ( g_settings_get_boolean( zond->settings, "internalviewer" ) )
        {
            rc = oeffnen_internal_viewer( zond, rel_path, anbindung, pos_pdf, errmsg );
            if ( rc ) ERROR_PAO( "oeffnen_internal_viewer" )

            return 0;
        }
//Falls WIN32 (und Application ist Acrobat (Reader oder nicht): Argument-String
//bilden und starten
        else //herausfinden, ob Adobe-Produkt in registry gepseichertes Anzeigeprogramm ist
            //und zu einer dest gesprungen werden soll: dann nicht ShellExecute
        {
#ifdef _WIN32
            HRESULT res = 0;
            DWORD dwSize = MAX_PATH;

            gchar exe[512] = { 0 };

            res = AssocQueryString( ASSOCF_REMAPRUNDLL || ASSOCF_NOTRUNCATE,
                    ASSOCSTR_FRIENDLYAPPNAME, ".pdf", NULL, (LPSTR) exe,
                    &dwSize );
            if ( res != S_OK )
            {
                if ( errmsg ) *errmsg = g_strdup_printf( "Bei Aufruf AssocQueryString:\n"
                        "Error Code: %li", res );

                return -1;
            }

            if ( g_str_has_prefix( exe, "Adobe" ) )
            {
                dwSize = MAX_PATH;

                res = AssocQueryString( ASSOCF_REMAPRUNDLL || ASSOCF_NOTRUNCATE,
                        ASSOCSTR_EXECUTABLE, ".pdf", NULL, (LPSTR) exe,
                        &dwSize );
                if ( res != S_OK )
                {
                    if ( errmsg ) *errmsg = g_strdup_printf( "Bei Aufruf AssocQueryString:\n"
                            "Error Code: %li", res );

                    return -1;
                }

                gchar* argv[6] = { NULL };
                argv[0] = exe;

                argv[1] = "/A";
                if ( anbindung )
                {
                    argv[2] = g_strdup_printf( "page=%i", anbindung->von.seite + 1 );
                    argv[3] = "/n";
                    argv[4] = (gchar*) rel_path;
                }
                else if ( pos_pdf )
                {
                    argv[2] = g_strdup_printf( "page=%i", pos_pdf->seite + 1 );
                    argv[3] = "/n";
                    argv[4] = (gchar*) rel_path;
                }
                else
                {
                    argv[2] = g_strdup( "/n" );
                    argv[3] = (gchar*) rel_path;
                }

                gboolean rc = g_spawn_async( NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD,
                        NULL, NULL, &g_pid, &error );
                g_free( argv[2] );
                if ( !rc )
                {
                    if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_spawn_async:\n",
                            error->message, NULL );
                    g_error_free( error );

                    return -1;
                }

                g_child_watch_add( g_pid, (GChildWatchFunc) close_pid, NULL );

                return 0;
            }
#endif // _WIN32

        }
    }

    //wenn keine pdf-Datei oder nicht win32+Adobe:
#ifdef _WIN32 //glib funktioniert nicht; daher Windows-Api verwenden
    HINSTANCE ret = 0;

    gchar* current_dir = g_get_current_dir( );
    gchar* rel_path_win32 = g_strdelimit( g_strdup( rel_path ), "/", '\\' );
    gchar* local_rel_path_win32 = utf8_to_local_filename( rel_path_win32 );
    g_free( rel_path_win32 );
    ret = ShellExecute( NULL, NULL, local_rel_path_win32, current_dir, NULL, SW_SHOWNORMAL );
    g_free( current_dir );
    g_free( local_rel_path_win32 );
    if ( ret == (HINSTANCE) 31 ) //no app associated
    {
printf("ShellExecute: noassoc\n");
    }
    else if ( ret <= (HINSTANCE) 32 )
    {
        if ( errmsg ) *errmsg = g_strdup_printf( "Bei Aufruf "
                "ShellExecute:\nErrCode: %p", ret );
        return -1;
    }
#else

    // Hier für Linux/Mac mit Glib executable ermitteln
    //Mit xdg-open, möglicherweise

    gchar* argv[3] = { NULL };
    argv[0] = exe;
    argv[1] = rel_path;

    gboolean rc = g_spawn_async( NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD,
            NULL, NULL, &g_pid, &error );
    if ( !rc )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_spawn_async:\n",
                error->message, NULL );
        g_error_free( error );

        return -1;
    }

    g_child_watch_add( g_pid, (GChildWatchFunc) close_pid, NULL );

#endif // _WIN32

    return 0;
}


gint
oeffnen_node( Projekt* zond, Baum baum, gint node_id, gchar** errmsg )
{
    gint rc = 0;
    gchar* rel_path = NULL;
    Anbindung* anbindung = NULL;
    PdfPos pos_pdf = { 0 };

    rc = abfragen_rel_path_and_anbindung( zond, baum, node_id, &rel_path,
            &anbindung, errmsg );
    if ( rc == -1 ) ERROR_PAO( "abfragen_rel_path_and_anbindung" )

    if ( rc == 2 && baum == BAUM_AUSWERTUNG )
    {
        rc = oeffnen_auszug( zond, node_id, errmsg );
        if ( rc ) ERROR_PAO( "oeffnen_auszug" )

        return 0;
    }

    if ( rc == 0 && !(zond->state & GDK_CONTROL_MASK) )
    {
        if ( zond->state & GDK_MOD1_MASK )
        {
            pos_pdf.seite = anbindung->bis.seite;
            pos_pdf.index = anbindung->bis.index;
        }
        else
        {
            pos_pdf.seite = anbindung->von.seite;
            pos_pdf.index = anbindung->von.index;
        }

        g_free( anbindung );
        anbindung = NULL;
    }

    if ( rc == 0 && (zond->state & GDK_CONTROL_MASK) && (zond->state & GDK_MOD1_MASK) )
    {
        pos_pdf.seite = EOP;
        pos_pdf.index = EOP;
    }

    if ( rc == 1 && (zond->state & GDK_MOD1_MASK) ) //am Ende öffnen
    {
        pos_pdf.seite = EOP;
        pos_pdf.index = EOP;
    }

    rc = oeffnen_datei( zond, rel_path, anbindung, &pos_pdf, errmsg );
    g_free( rel_path );
    g_free( anbindung );
    if ( rc ) ERROR_PAO( "oeffnen_datei" )

    return 0;
}
