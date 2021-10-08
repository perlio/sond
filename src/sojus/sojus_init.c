/*
sojus (sojus.c) - softkanzlei
Copyright (C) 2021  pelo america

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

#include "../misc.h"

#include "sojus_init.h"
#include "sojus_adressen.h"
#include "sojus_file_manager.h"

#include "20Einstellungen/db.h"
#include "20Einstellungen/einstellungen.h"


static gboolean
cb_socket_incoming(GSocketService *service,
                    GSocketConnection *connection,
                    GObject *source_object,
                    gpointer data)
{
    gssize ret = 0;
    gchar* buffer = NULL;
    GError* error = NULL;

    Sojus* sojus = (Sojus*) data;

    GOutputStream * ostream = g_io_stream_get_output_stream(G_IO_STREAM (connection));

    gchar* host = g_settings_get_string( sojus->settings, "host" );
    gint port = g_settings_get_int( sojus->settings, "port" );
    gchar* user = g_settings_get_string( sojus->settings, "user" );
    gchar* password = g_settings_get_string( sojus->settings, "password" );
    gchar* db_name = g_settings_get_string( sojus->settings, "dbname" );

    buffer = g_strdup_printf( "%s;%i;%s;%s;%s", host, port, user, password, db_name );

    g_free( host );
    g_free( user );
    g_free( password );
    g_free( db_name );

    ret = g_output_stream_write( ostream, buffer, strlen( buffer ), NULL, &error );
    g_free( buffer );
    if ( ret == -1 )
    {
        display_message( sojus->app_window, "Fehler -\n\n"
                "Zugangsdaten SQL-Database konnten nicht gesendet werden:\n"
                "Bei Aufruf g_output_stream_write:\n", error->message, NULL );
        g_error_free( error );
    }

    return FALSE;
}


static gint
sojus_init_socket( Sojus* sojus, gchar** errmsg )
{
    GError* error = NULL;

    sojus->socket = g_socket_service_new ( );

    if ( !g_socket_listener_add_inet_port( G_SOCKET_LISTENER(sojus->socket),
            61339, NULL, &error ) )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_socket_listener_add_inet_port:\n",
                error->message, NULL );
        g_error_free( error );

        return -1;
    }

    g_signal_connect (sojus->socket, "incoming", G_CALLBACK (cb_socket_incoming), sojus );

    return 0;
}


static gboolean
cb_desktop_delete_event( GtkWidget* app_window, GdkEvent* event, gpointer data )
{
    Sojus* sojus = (Sojus*) data;

    gtk_widget_destroy( app_window );

    mysql_close( sojus->con );

    g_object_unref( sojus->socket );
    g_object_unref( sojus->settings );

    g_ptr_array_unref( sojus->arr_open_fm );

    g_free( sojus );

    return TRUE;
}


static void
sojus_init_app_window( Sojus* sojus )
{
/*
**  Widgets erzeugen  */
    //app-window
    sojus->app_window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
//    gtk_window_set_keep_above( GTK_WINDOW(sojus->app_window), TRUE );
    GtkWidget* grid = gtk_grid_new( );
    gtk_container_add( GTK_CONTAINER(sojus->app_window), grid );

    GtkWidget* frame_dok = gtk_frame_new( "Dokumentenverzeichnis öffnen" );
    GtkWidget* entry_dok = gtk_entry_new( );
    gtk_container_add( GTK_CONTAINER(frame_dok), entry_dok );

    //Akten
    GtkWidget* bu_akte_fenster = gtk_button_new_with_mnemonic( "_Aktenfenster" );
    GtkWidget* bu_akte_suchen = gtk_button_new_with_mnemonic( "Akte _suchen" );

    //Adressen
    GtkWidget* bu_adressen_fenster = gtk_button_new_with_mnemonic( "A_dressenfenster" );
    GtkWidget* bu_adresse_suchen = gtk_button_new_with_mnemonic( "Adresse s_uchen" );

    //Termine
    GtkWidget* bu_kalender = gtk_button_new_with_mnemonic( "_Kalender" );
    GtkWidget* bu_termine_zur_akte = gtk_button_new_with_mnemonic( "_Termine zur Akte" );
    GtkWidget* bu_fristen = gtk_button_new_with_mnemonic( "_Fristen" );
    GtkWidget* bu_wiedervorlagen = gtk_button_new_with_mnemonic( "_Wiedervorlagen" );

    //Einstellungen
    GtkWidget* bu_einstellungen = gtk_button_new_with_mnemonic( "_Einstellungen" );

/*
**  in grid einfügen  */
    gtk_grid_attach( GTK_GRID(grid), frame_dok, 0, 0, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), bu_akte_fenster, 0, 1, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), bu_akte_suchen, 0, 2, 1, 1 );

    gtk_grid_attach( GTK_GRID(grid), bu_adressen_fenster, 0, 4, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), bu_adresse_suchen, 0, 5, 1, 1 );

    gtk_grid_attach( GTK_GRID(grid), bu_kalender, 0, 6, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), bu_termine_zur_akte, 0, 7, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), bu_fristen, 0, 8, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), bu_wiedervorlagen, 0, 9, 1, 1 );

    gtk_grid_attach( GTK_GRID(grid), bu_einstellungen, 0, 10, 1, 1 );

/*
**  callbacks  */
    g_signal_connect( entry_dok, "activate",
            G_CALLBACK(file_manager_entry_activate), sojus );
/*    g_signal_connect( bu_akte_fenster, "clicked",
            G_CALLBACK(cb_button_aktenfenster_clicked), sojus );
    g_signal_connect( bu_akte_suchen, "clicked",
            G_CALLBACK(cb_button_akte_suchen_clicked), sojus );
*/
    g_signal_connect( bu_adressen_fenster, "clicked",
            G_CALLBACK(sojus_adressen_cb_fenster), sojus );
/*    g_signal_connect( bu_adresse_suchen, "clicked",
            G_CALLBACK(cb_bu_adresse_suchen_clicked), sojus );
*/
    g_signal_connect( bu_einstellungen, "clicked",
            G_CALLBACK(einstellungen), sojus );

    //Signal für App-Fenster schließen
    g_signal_connect( sojus->app_window, "delete-event",
            G_CALLBACK(cb_desktop_delete_event), sojus );

    gtk_widget_show_all( GTK_WIDGET(sojus->app_window) );

    return;
}


Sojus*
sojus_init( GtkApplication* app )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    Sojus* sojus = g_malloc0( sizeof( Sojus ) );

    sojus_init_app_window( sojus );
    gtk_application_add_window( app, GTK_WINDOW(sojus->app_window) );

    rc = sojus_init_socket( sojus, &errmsg );
    if ( rc )
    {
        display_message( sojus->app_window, "Fehler Init -\n\nBei Aufruf "
                "sojus_init_socket:\n", errmsg, NULL );
        g_free( errmsg );
        gboolean ret = FALSE;
        g_signal_emit_by_name( sojus->app_window, "delete-event", NULL, &ret );

        return NULL;
    }

    sojus->settings = g_settings_new( "de.perlio.Sojus" );

    db_connect_database( sojus );
    if ( !sojus->con )
    {
        gboolean ret = FALSE;
        g_signal_emit_by_name( sojus->app_window, "delete-event", NULL, &ret );

        return NULL;
    }

    sojus->arr_open_fm = g_ptr_array_new_with_free_func( (GDestroyNotify) g_free );

    return sojus;
}


