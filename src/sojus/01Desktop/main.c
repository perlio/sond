#define MAIN_C

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <mariadb/mysql.h>

#include "../../treeview.h"
#include "../global_types_sojus.h"

#include "aktenschnellansicht.h"
#include "callbacks_akten.h"
#include "callbacks_adressen.h"
#include "callbacks_einstellungen.h"

#include "../00misc/settings.h"

#include "../20Einstellungen/db.h"
#include "../20Einstellungen/einstellungen.h"

#include"../06Dokumente/file_manager.h"

#include "../../misc.h"
#include "../../treeview.h"


static gboolean
cb_desktop_delete_event( GtkWidget* app_window, GdkEvent* event, gpointer data )
{
    Sojus* sojus = (Sojus*) data;

    gtk_widget_destroy( app_window );

    mysql_close( sojus->db.con );

    g_object_unref( sojus->socket );
    g_object_unref( sojus->settings );

    clipboard_free( sojus->clipboard );

    g_ptr_array_unref( sojus->sachgebiete );
    g_ptr_array_unref( sojus->beteiligtenart );
    g_ptr_array_unref( sojus->sachbearbeiter );
    g_ptr_array_unref( sojus->arr_open_fm );

    g_free( sojus );

    return TRUE;
}


static void
init_db ( Sojus* sojus )
{
    gint rc = 0;

    rc = db_get_connection( sojus, sojus->app_window );
    if ( rc )
    {
        gboolean ret = FALSE;
        g_signal_emit_by_name( sojus->app_window, "delete-event", NULL, &ret );
    }

    return;
}


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


static void
init_socket( Sojus* sojus )
{
    GError* error = NULL;

    sojus->socket = g_socket_service_new ( );

    if ( !g_socket_listener_add_inet_port( G_SOCKET_LISTENER(sojus->socket),
            61339, NULL, &error ) )
    {
        display_message( sojus->app_window, "Socket konnte nicht initialisiert "
                "werden:\n\nBei Aufruf g_socket_listener_add_inet_port:\n",
                error->message, NULL );
        g_error_free( error );

        return;
    }

    g_signal_connect (sojus->socket, "incoming", G_CALLBACK (cb_socket_incoming), sojus );

    return;
}


void
init_app_window( GtkApplication* app, Sojus* sojus )
{
/*
**  Widgets erzeugen  */
    //app-window
    sojus->app_window = gtk_application_window_new( app );
    GtkWidget* grid = gtk_grid_new( );
    gtk_container_add( GTK_CONTAINER(sojus->app_window), grid );

    GtkWidget* frame_dok = gtk_frame_new( "Dokumentenverzeichnis öffnen" );
    GtkWidget* entry_dok = gtk_entry_new( );
    gtk_container_add( GTK_CONTAINER(frame_dok), entry_dok );

    //Akten
    GtkWidget* bu_akte_fenster = gtk_button_new_with_mnemonic( "_Aktenfenster" );
    GtkWidget* bu_akte_suchen = gtk_button_new_with_mnemonic( "Akte _suchen" );

    //Adressen
    GtkWidget* bu_adresse_fenster = gtk_button_new_with_mnemonic( "A_dressenfenster" );
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

    gtk_grid_attach( GTK_GRID(grid), bu_adresse_fenster, 0, 4, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), bu_adresse_suchen, 0, 5, 1, 1 );

    gtk_grid_attach( GTK_GRID(grid), bu_kalender, 0, 6, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), bu_termine_zur_akte, 0, 7, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), bu_fristen, 0, 8, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), bu_wiedervorlagen, 0, 9, 1, 1 );

    gtk_grid_attach( GTK_GRID(grid), bu_einstellungen, 0, 10, 1, 1 );

/*
**  window mit widgets und sojus vollstopfen  */
    g_object_set_data( G_OBJECT(sojus->app_window), "sojus", (gpointer) sojus );

    g_object_set_data( G_OBJECT(sojus->app_window), "bu_akte_fenster",
            (gpointer) bu_akte_fenster );
    g_object_set_data( G_OBJECT(sojus->app_window), "bu_akte_suchen",
            (gpointer) bu_akte_suchen );

    g_object_set_data( G_OBJECT(sojus->app_window), "bu_adresse_fenster",
            (gpointer) bu_adresse_fenster );
    g_object_set_data( G_OBJECT(sojus->app_window), "bu_adresse_suchen",
            (gpointer) bu_adresse_suchen );

    g_object_set_data( G_OBJECT(sojus->app_window), "bu_kalender",
            (gpointer) bu_kalender );
    g_object_set_data( G_OBJECT(sojus->app_window), "bu_termine_zur_akte",
            (gpointer) bu_termine_zur_akte );
    g_object_set_data( G_OBJECT(sojus->app_window), "bu_fristen",
            (gpointer) bu_fristen );
    g_object_set_data( G_OBJECT(sojus->app_window), "bu_wiedervorlagen",
            (gpointer) bu_wiedervorlagen );

/*
**  callbacks  */
    g_signal_connect( entry_dok, "activate",
            G_CALLBACK(file_manager_entry_activate), sojus );
    g_signal_connect( bu_akte_fenster, "clicked",
            G_CALLBACK(cb_button_aktenfenster_clicked), sojus );
    g_signal_connect( bu_akte_suchen, "clicked",
            G_CALLBACK(cb_button_akte_suchen_clicked), sojus );

    g_signal_connect( bu_adresse_fenster, "clicked",
            G_CALLBACK(cb_button_adresse_fenster_clicked), sojus );
    g_signal_connect( bu_adresse_suchen, "clicked",
            G_CALLBACK(cb_bu_adresse_suchen_clicked), sojus );

    g_signal_connect( bu_einstellungen, "clicked",
            G_CALLBACK(einstellungen), sojus );

    //Signal für App-Fenster schließen
    g_signal_connect( sojus->app_window, "delete-event",
            G_CALLBACK(cb_desktop_delete_event), sojus );

    gtk_widget_show_all( GTK_WIDGET(sojus->app_window) );

    return;
}


static GSettings*
init_settings( void )
{
    GSettingsSchemaSource* schema_source = NULL;
    GSettingsSchema* schema = NULL;
    GSettings* settings = NULL;
    GError* error = NULL;


    schema_source = g_settings_schema_source_new_from_directory(
            "schemas/", NULL, FALSE, &error );
    if ( error )
    {
        printf( "%s\n", error->message );
        g_error_free( error );
        return NULL;
    }

    schema = g_settings_schema_source_lookup( schema_source,
            "de.perlio.Sojus", FALSE );
    g_settings_schema_source_unref( schema_source );

    settings = g_settings_new_full( schema, NULL, NULL );
    g_settings_schema_unref( schema );

    return settings;
}


static Sojus*
init_sojus( void )
{
    GSettings* settings = init_settings( );
    if ( !settings ) return NULL;

    Sojus* sojus = g_malloc0( sizeof( Sojus ) );

    sojus->settings = settings;

    sojus->clipboard = clipboard_init( );

    sojus->arr_open_fm = g_ptr_array_new_with_free_func( (GDestroyNotify) g_free );

    sojus->sachgebiete = g_ptr_array_new_with_free_func( (GDestroyNotify) g_free );
    sojus->beteiligtenart = g_ptr_array_new_with_free_func( (GDestroyNotify) g_free );
    sojus->sachbearbeiter = g_ptr_array_new_with_free_func( (GDestroyNotify) g_free );

    return sojus;
}


static void
activate_app( GtkApplication* app, gpointer data )
{
    return;
}


static void
startup_app( GtkApplication* app, gpointer data )
{
    Sojus** sojus = (Sojus**) data;

    *sojus = init_sojus( );
    if ( !(*sojus) ) return;

    init_app_window( app, *sojus );
    init_socket( *sojus );
    init_db( *sojus );

    return;
}


gint
main( int argc, char **argv)
{
    GtkApplication* app = NULL;
    Sojus* sojus = NULL;
    gint status = 0;

    //ApplicationApp erzeugen
    app = gtk_application_new ( "de.rubarth-krieger.sojus", G_APPLICATION_FLAGS_NONE );

    //und starten
    g_signal_connect( app, "startup", G_CALLBACK(startup_app), &sojus );
    g_signal_connect( app, "activate", G_CALLBACK (activate_app), &sojus );

    status = g_application_run( G_APPLICATION (app), argc, argv );

    g_object_unref( app );

    return status;
}
