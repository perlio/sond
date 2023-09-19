/*
sond (sond_client_akte.c) - Akten, Beweisstücke, Unterlagen
Copyright (C) 2023  pelo america

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

#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include "../../misc.h"
#include "../sond_akte.h"

#include "sond_client.h"
#include "sond_client_misc.h"
#include "sond_client_connection.h"


typedef struct _SondClientAkte
{
    SondClient* sond_client;
    GtkWidget* window;
    GtkWidget* entry_reg_nr;
    GtkWidget* button_neu;
    GtkWidget* button_ok;

    GtkWidget* entry_aktenrubrum;
    GtkWidget* entry_aktenkurzbez;
    gulong signal_changed;

    gboolean neu;
    gboolean locked;
    gint reg_nr;
    gint reg_jahr;
    gboolean changed;
} SondClientAkte;


static gint
sond_client_akte_release( SondClientAkte* sond_client_akte, GError** error )
{

    return 0;
}


static void
sond_client_akte_close( SondClientAkte* sond_client_akte )
{
    GtkWidget* window = NULL;

    //wenn lock geholt wurde, dann freigeben
    if ( gtk_widget_get_sensitive( sond_client_akte->button_ok ) )
    {
        gint rc = 0;
        GError* error = NULL;

        rc = sond_client_akte_release( sond_client_akte, &error );
        if ( rc )
        {
            display_message( sond_client_akte->window,
                    "Aktenlock konnte nicht gelöst werden\n\n%s", error->message, NULL );
            g_error_free( error );
        }
    }

    window = sond_client_akte->window;

    g_free( sond_client_akte );

    gtk_widget_destroy( window );

    return;
}


static void
sond_client_akte_button_abbrechen_clicked( GtkButton* button, gpointer data )
{
    sond_client_akte_close( (SondClientAkte*) data );

    return;
}


static gboolean
sond_client_akte_delete_event( GtkWindow* window, GdkEvent* event, gpointer data )
{
    sond_client_akte_close( (SondClientAkte*) data );

    return TRUE;
}


static void
sond_client_akte_button_ok_clicked( GtkButton* button, gpointer data )
{
    SondClientAkte* sond_client_akte = (SondClientAkte*) data;

    if ( sond_client_akte->changed )
    {
        gchar* params = NULL;
        GError* error = NULL;
        gchar* resp = NULL;
        SondAkte* sond_akte = NULL;

        if ( !g_strcmp0( gtk_entry_get_text( GTK_ENTRY(sond_client_akte->entry_aktenrubrum) ), "" ) )
        {
            display_message( sond_client_akte->window, "Aktenrubrum darf nicht leer sein", NULL );

            return;
        }

        sond_akte = sond_akte_new( );

        sond_akte->reg_nr = sond_client_akte->reg_nr;
        sond_akte->reg_jahr = sond_client_akte->reg_jahr;
        sond_akte->aktenrubrum =
                g_strdup( gtk_entry_get_text( GTK_ENTRY(sond_client_akte->entry_aktenrubrum) ) );
        sond_akte->aktenkurzbez =
                g_strdup( gtk_entry_get_text( GTK_ENTRY(sond_client_akte->entry_aktenkurzbez) ) );

        params = sond_akte_to_json( sond_akte );

        resp = sond_client_connection_send_and_read( sond_client_akte->sond_client,
                "AKTE_SCHREIBEN", params, &error );
        g_free( params );
        if ( !resp )
        {
            display_message( sond_client_akte->window, "Fehler\n\n", error->message, NULL );
            g_error_free( error );

            return; //Fenster bleibt geöffnet; je nach Fehler kann man es nochmal versuchen
        }
        else if ( g_str_has_prefix( resp, "ERROR ***" ) )
        {
            display_message( sond_client_akte->window, "Fehler\n\nServer antwortet:\n", resp, NULL );
            g_free( resp );

            return; //Fenster bleibt geöffnet; je nach Fehler kann man es nochmal versuchen
        }

        if ( sond_client_akte->neu )
        {
            gchar** v_resp = NULL;

            v_resp = g_strsplit( resp, "-", 2 );

            sond_client_akte->sond_client->reg_nr_akt = atoi( v_resp[0] );
            sond_client_akte->sond_client->reg_jahr_akt = atoi( v_resp[1] );

            g_strfreev( v_resp );
        }
        g_free( resp );
    }

    sond_client_akte_close( sond_client_akte );

    return;
}


static void
sond_client_akte_entry_changed( GtkEditable* entry, gpointer data )
{
    SondClientAkte* sond_client_akte = (SondClientAkte*) data;

    sond_client_akte->changed = TRUE;

    g_signal_handler_disconnect( entry, sond_client_akte->signal_changed );

    return;
}


static void
sond_client_akte_start_editing( SondClientAkte* sond_client_akte )
{
    gtk_widget_set_sensitive( sond_client_akte->button_neu, FALSE );
    gtk_widget_set_sensitive( sond_client_akte->entry_reg_nr, FALSE );

    gtk_widget_set_sensitive( gtk_widget_get_parent( sond_client_akte->entry_aktenrubrum ), TRUE );
    gtk_widget_set_sensitive( gtk_widget_get_parent( sond_client_akte->entry_aktenkurzbez ), TRUE );
    gtk_widget_set_sensitive( gtk_widget_get_parent( sond_client_akte->button_ok ), TRUE );

    return;
}


static void
sond_client_akte_entry_reg_nr_activate( GtkEntry* entry, gpointer data )
{
    SondClientAkte* sond_client_akte = (SondClientAkte*) data;

    if ( strlen( gtk_entry_get_text( entry ) ) < 3 )
    {
        display_message( sond_client_akte->window, "Mindestens drei Zeichen", NULL );

        return;
    }

    if ( sond_client_misc_regnr_wohlgeformt( gtk_entry_get_text( entry ) ) )
    {
        gint reg_jahr = 0;
        gint reg_nr = 0;
        gchar* params = NULL;
        GError* error = NULL;
        gchar* resp = NULL;
        gboolean locked = FALSE;
        gchar* json_string = NULL;
        SondAkte* sond_akte = NULL;

        sond_client_misc_parse_regnr( gtk_entry_get_text( entry ), &reg_nr, &reg_jahr );

        params = g_strdup_printf( "%i-%i", reg_nr, reg_jahr );

        resp = sond_client_connection_send_and_read( sond_client_akte->sond_client,
                "AKTE_HOLEN", params, &error );
        g_free( params );
        if ( !resp )
        {
            display_message( sond_client_akte->window, "Fehler\n\n", error->message, NULL );
            g_error_free( error );

            return; //Fenster bleibt geöffnet; je nach Fehler kann man es nochmal versuchen
        }
        //error Akte nicht gefunden
        else if ( g_str_has_prefix( resp, "ERROR ***" ) )
        {
            display_message( sond_client_akte->window, "Fehler\n\nServer antwortet:\n", resp, NULL );
            g_free( resp );

            return; //Fenster bleibt geöffnet; je nach Fehler kann man es nochmal versuchen
        }

        sond_client_akte->reg_nr = reg_nr;
        sond_client_akte->reg_jahr = reg_jahr;

        if ( g_str_has_prefix( resp, "LOCKED" ) )
        {
            locked = TRUE;
            json_string = resp + 6;
        }
        else json_string = resp;

        sond_akte = sond_akte_new_from_json( json_string, &error );
        g_free( resp );
        if ( !sond_akte )
        {
            display_message( sond_client_akte->window, "Fehler beim Parsen Antwort Server\n\n",
                    error->message, NULL );
            g_error_free( error );
        }

        gtk_entry_set_text( GTK_ENTRY(sond_client_akte->entry_aktenrubrum), sond_akte->aktenrubrum );
        gtk_entry_set_text( GTK_ENTRY(sond_client_akte->entry_aktenkurzbez), sond_akte->aktenkurzbez );

        if ( locked ) sond_client_akte_start_editing( sond_client_akte );
        else
        {
            gtk_widget_set_sensitive( sond_client_akte->button_neu, FALSE );
            gtk_widget_set_sensitive( sond_client_akte->entry_reg_nr, FALSE );
        }
    }
    else //text in aktenrubrum suchen
    {
        gchar* resp = NULL;
        GError* error = NULL;
        gint rc = 0;

        resp = sond_client_connection_send_and_read( sond_client_akte->sond_client,
                "AKTE_SUCHEN", gtk_entry_get_text( entry ), &error );
        if ( !resp )
        {
            display_message( sond_client_akte->window, "Fehler\n\n", error->message, NULL );
            g_error_free( error );

            return; //Fenster bleibt geöffnet; je nach Fehler kann man es nochmal versuchen
        }
        else if ( g_str_has_prefix( resp, "ERROR ***" ) )
        {
            display_message( sond_client_akte->window, "Fehler\n\nServer antwortet:\n", resp, NULL );
            g_free( resp );

            return; //Fenster bleibt geöffnet; je nach Fehler kann man es nochmal versuchen
        }

//        rc = sond_client_akte_auswahlfenster( sond_client_akte, resp, &error );
        if ( rc )
        {
            display_message( sond_client_akte->window,
                    "Auswahlfenster konnte nicht erstellt werden\n\n", error->message, NULL );
            g_error_free( error );

            return;
        }
    }

    return;
}


static void
sond_client_akte_button_neu_clicked( GtkButton* button, gpointer data )
{
    SondClientAkte* sond_client_akte = (SondClientAkte*) data;

    sond_client_akte->neu = TRUE;

    gtk_entry_set_text( GTK_ENTRY(sond_client_akte->entry_reg_nr), "-- Neu --" );

    sond_client_akte_start_editing( sond_client_akte );

    return;
}


void
sond_client_akte_init( GtkButton* button, gpointer data )
{
    SondClientAkte* sond_client_akte = NULL;

    sond_client_akte = g_malloc0( sizeof( SondClientAkte ) );
    sond_client_akte->sond_client = (SondClient*) data;

    sond_client_akte->window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_default_size( GTK_WINDOW(sond_client_akte->window), 500, 300 );

    GtkWidget* akten_headerbar = gtk_header_bar_new( );
    gtk_header_bar_set_show_close_button( GTK_HEADER_BAR(akten_headerbar), TRUE );
    gtk_header_bar_set_title( GTK_HEADER_BAR(akten_headerbar), "Aktenfenster" );
    gtk_window_set_titlebar( GTK_WINDOW(sond_client_akte->window), akten_headerbar );

    GtkWidget* grid = gtk_grid_new( );
    gtk_container_add( GTK_CONTAINER(sond_client_akte->window), grid );

    //RegNr
    GtkWidget* frame_regnr = gtk_frame_new( "Register-Nr." );
    sond_client_akte->entry_reg_nr = gtk_entry_new( );
    if ( (sond_client_akte->sond_client->reg_jahr_akt) && (sond_client_akte->sond_client->reg_nr_akt) )
    {
        gchar* text_regnr = g_strdup_printf( "%i/%i", sond_client_akte->sond_client->reg_nr_akt,
                sond_client_akte->sond_client->reg_jahr_akt % 100 );
        gtk_entry_set_text( GTK_ENTRY(sond_client_akte->entry_reg_nr), text_regnr );
        g_free( text_regnr );
    }

    gtk_container_add( GTK_CONTAINER(frame_regnr), sond_client_akte->entry_reg_nr );
    gtk_header_bar_pack_start( GTK_HEADER_BAR(akten_headerbar), frame_regnr );

    //Button Neue Akte
    sond_client_akte->button_neu = gtk_button_new_with_label( "Neue Akte" );
    gtk_header_bar_pack_start( GTK_HEADER_BAR(akten_headerbar), sond_client_akte->button_neu );

    //Entry "Aktenrubrum"
    GtkWidget* frame_aktenrubrum = gtk_frame_new( "Rubrum" );
    sond_client_akte->entry_aktenrubrum = gtk_entry_new( );
    gtk_container_add( GTK_CONTAINER(frame_aktenrubrum), sond_client_akte->entry_aktenrubrum );
    gtk_grid_attach( GTK_GRID(grid), frame_aktenrubrum, 1, 2, 1, 1 );
    gtk_widget_set_sensitive( frame_aktenrubrum, FALSE );

    //Entry "Aktenkurzbez"
    GtkWidget* frame_aktenkurzbez = gtk_frame_new( "Kurzbezeichnung" );
    sond_client_akte->entry_aktenkurzbez = gtk_entry_new( );
    gtk_container_add( GTK_CONTAINER(frame_aktenkurzbez), sond_client_akte->entry_aktenkurzbez );
    gtk_grid_attach( GTK_GRID(grid), frame_aktenkurzbez, 1, 3, 1, 1 );
    gtk_widget_set_sensitive( frame_aktenkurzbez, FALSE );


    sond_client_akte->button_ok = gtk_button_new_with_label(
            "OK" );
    gtk_grid_attach( GTK_GRID(grid), sond_client_akte->button_ok, 1, 12, 1, 1 );

    GtkWidget* button_abbrechen = gtk_button_new_with_label(
            "Abbrechen" );
    gtk_grid_attach( GTK_GRID(grid), button_abbrechen, 3, 12, 1, 1 );

    //Signale
    g_signal_connect( sond_client_akte->button_neu, "clicked",
            G_CALLBACK(sond_client_akte_button_neu_clicked), sond_client_akte );

    g_signal_connect( sond_client_akte->entry_reg_nr, "activate",
            G_CALLBACK(sond_client_akte_entry_reg_nr_activate), sond_client_akte );

    sond_client_akte->signal_changed = g_signal_connect( sond_client_akte->entry_aktenrubrum,
            "changed", G_CALLBACK(sond_client_akte_entry_changed), sond_client_akte );

    g_signal_connect( sond_client_akte->button_ok, "clicked",
            G_CALLBACK(sond_client_akte_button_ok_clicked), sond_client_akte );

    g_signal_connect( button_abbrechen, "clicked",
            G_CALLBACK(sond_client_akte_button_abbrechen_clicked),
            sond_client_akte );

    g_signal_connect( sond_client_akte->window, "delete-event",
            G_CALLBACK(sond_client_akte_delete_event), sond_client_akte );

    gtk_widget_show_all( sond_client_akte->window );

    return;
}
