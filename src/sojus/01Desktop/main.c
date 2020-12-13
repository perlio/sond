#define MAIN_C

#include <gtk/gtk.h>

#include "../global_types_sojus.h"
#include <gio/gio.h>

#include "aktenschnellansicht.h"

#include "../../fm.h"
#include "../../misc.h"


static gboolean
cb_desktop_delete_event( GtkWidget* app_window, GdkEvent* event, gpointer data )
{
    Sojus* sojus = (Sojus*) data;

    gtk_widget_destroy( app_window );

    g_object_unref( sojus->socket );
    g_object_unref( sojus->settings );

    g_ptr_array_unref( sojus->sachgebiete );
    g_ptr_array_unref( sojus->beteiligtenart );
    g_ptr_array_unref( sojus->sachbearbeiter );

    g_free( sojus );

    return TRUE;
}


static void
init_db ( Sojus* sojus )
{
    //working dir = Sojus/
    gchar* program = g_find_program_in_path( "Sojus.exe" );
    gchar* wd_path = g_strndup( program, strlen( program ) - 15 );
    g_chdir( wd_path );
    g_free( program );
    g_free( wd_path );

    GSettings* settings = settings_open( );
    gchar* host = g_settings_get_string( settings, "host" );
    gint port = 0;
    port = g_settings_get_int( settings, "port" );
    gchar* user = g_settings_get_string( settings, "user" );
    gchar* password = g_settings_get_string( settings, "password" );
    g_object_unref( settings );

    sojus->db.host = host;
    sojus->db.port = port;
    sojus->db.user = user;
    sojus->db.password = password;

    gchar* errmsg = NULL;
    if ( g_strcmp0( host, "" ) && port && g_strcmp0( user, "" ) &&
            g_strcmp0( password, "" ) ) sojus->db.con = db_connect(
            sojus->app_window, host, user, password, port, &errmsg );

    //Wenn Verbindung nicht hergestellt werden konnte
    if ( !sojus->db.con )
    {
        widgets_desktop_db_name( sojus, FALSE );
        widgets_desktop_db_con( G_OBJECT(sojus->app_window), FALSE );

        display_message( sojus->app_window, "Fehler -\n\n"
                "In Einstellungen gespeicherte Verbindung konnte nicht "
                "hergestellt werden:\n", errmsg, NULL );
        g_free( errmsg );

        return;
    }

    //Verbindung wurde hergestellt - anzeigen
    widgets_desktop_label_con( G_OBJECT(sojus->app_window), sojus->db.host,
            sojus->db.port, sojus->db.user );

    //Settings-Datei öffnen und nach letztem Namen gucken
    settings = settings_open( );
    gchar* db_name = g_settings_get_string( settings, "dbname" );
    g_object_unref( settings );

    if ( !g_strcmp0( db_name, "" ) )
    {
        g_free( db_name );
        widgets_desktop_db_name( sojus, FALSE );

        return;
    }

    gint rc = db_active( sojus->app_window, db_name, &errmsg );
    g_free( errmsg );
    if ( rc )
    {
        widgets_desktop_db_name( sojus, FALSE );
        display_message( sojus->app_window, "Fehler -\n\n",
                "In Settings gespeicherte db kann nicht "
                "geöffnet werden:\n", errmsg, NULL );
        g_free( db_name );

        return;
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
    gchar* port_str = NULL;
    GError* error = NULL;

    Sojus* sojus = (Sojus*) data;

    GOutputStream * ostream = g_io_stream_get_output_stream(G_IO_STREAM (connection));

    port_str = g_strdup_printf( "%i", sojus->db.port );
    buffer = g_strconcat( sojus->db.host, ";", port_str, ";", sojus->db.user,
            ";", sojus->db.password, ";", sojus->db.db_name, NULL );
    g_free( port_str );
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
    GtkWidget* bu_db_con = gtk_button_new_with_mnemonic( "_Verbindung zu "
            "SQL-Server" );
    GtkWidget* bu_db_waehlen = gtk_button_new_with_mnemonic( "Daten_bank wählen" );
    GtkWidget* bu_db_erstellen = gtk_button_new_with_mnemonic( "Datenban_k erstellen" );
    GtkWidget* bu_sachbearbeiterverwaltung = gtk_button_new_with_mnemonic(
            "Sachbearbeiter_verwaltung" );
    sojus->widgets.Einstellungen.bu_dokument_dir = gtk_button_new_with_mnemonic(
            "Dokumentenver_zeichnis" );

    //Label connection/DB
    GtkWidget* label_con_title = gtk_label_new( "SQL-Server-Verbindung:" );
    GtkWidget* label_con = gtk_label_new( NULL );
    GtkWidget* label_db_title = gtk_label_new( "Datenbank:" );
    GtkWidget* label_db = gtk_label_new( NULL );

/*
**  in grid einfügen  */
    gtk_grid_attach( GTK_GRID(grid), bu_akte_fenster, 0, 1, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), bu_akte_suchen, 0, 2, 1, 1 );

    gtk_grid_attach( GTK_GRID(grid), bu_adresse_fenster, 0, 3, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), bu_adresse_suchen, 0, 4, 1, 1 );

    gtk_grid_attach( GTK_GRID(grid), bu_kalender, 0, 5, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), bu_termine_zur_akte, 0, 6, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), bu_fristen, 0, 7, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), bu_wiedervorlagen, 0, 8, 1, 1 );

    gtk_grid_attach( GTK_GRID(grid), bu_db_con, 0, 9, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), bu_db_waehlen, 0, 10, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), bu_db_erstellen, 0, 11, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), bu_sachbearbeiterverwaltung, 0, 12, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), sojus->widgets.Einstellungen.bu_dokument_dir,
            0, 13, 1, 1 );

    gtk_grid_attach( GTK_GRID(grid), label_con_title, 2, 0, 2, 1 );
    gtk_grid_attach( GTK_GRID(grid), label_con, 2, 1, 2, 2 );

    gtk_grid_attach( GTK_GRID(grid), label_db_title, 2, 3, 2, 1 );
    gtk_grid_attach( GTK_GRID(grid), label_db, 2, 4, 2, 2 );

    //Akten-Schnellübersicht
    GtkWidget* frame_akte = gtk_frame_new( "Aktenschnellansicht" );
    gtk_grid_attach( GTK_GRID(grid), frame_akte, 4, 2, 20, 50 );

    gtk_container_add( GTK_CONTAINER(frame_akte), aktenschnellansicht_create_window( sojus ) );

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

    g_object_set_data( G_OBJECT(sojus->app_window), "bu_db_con", (gpointer)
            bu_db_con );
    g_object_set_data( G_OBJECT(sojus->app_window), "bu_db_waehlen", (gpointer)
            bu_db_waehlen );
    g_object_set_data( G_OBJECT(sojus->app_window), "bu_db_erstellen",
            (gpointer) bu_db_erstellen );
    g_object_set_data( G_OBJECT(sojus->app_window), "bu_sachbearbeiterverwaltung",
            (gpointer) bu_sachbearbeiterverwaltung );

    g_object_set_data( G_OBJECT(sojus->app_window), "label_con",
            (gpointer) label_con );
    g_object_set_data( G_OBJECT(sojus->app_window), "label_db",
            (gpointer) label_db );

/*
**  callbacks  */
    g_signal_connect( bu_akte_fenster, "clicked",
            G_CALLBACK(cb_button_aktenfenster_clicked), sojus );
    g_signal_connect( bu_akte_suchen, "clicked",
            G_CALLBACK(cb_button_akte_suchen_clicked), sojus );

    g_signal_connect( bu_adresse_fenster, "clicked",
            G_CALLBACK(cb_button_adresse_fenster_clicked), sojus );
    g_signal_connect( bu_adresse_suchen, "clicked",
            G_CALLBACK(cb_bu_adresse_suchen_clicked), sojus );

    g_signal_connect( bu_db_con, "clicked",
            G_CALLBACK(cb_button_db_con_clicked), sojus );
    g_signal_connect( bu_db_waehlen, "clicked",
            G_CALLBACK(cb_button_db_waehlen_clicked), sojus );
    g_signal_connect( bu_db_erstellen, "clicked",
            G_CALLBACK(cb_button_db_erstellen_clicked), sojus );

    g_signal_connect( bu_sachbearbeiterverwaltung, "clicked",
            G_CALLBACK(cb_button_sachbearbeiterverwaltung), sojus );
    g_signal_connect( sojus->widgets.Einstellungen.bu_dokument_dir, "clicked",
            G_CALLBACK(cb_button_dokument_dir), sojus );

    //Signal für App-Fenster schließen
    g_signal_connect( sojus->app_window, "delete-event",
            G_CALLBACK(cb_desktop_delete_event), sojus );

    gtk_widget_show_all( GTK_WIDGET(sojus->app_window) );

    return;
}


static Sojus*
init_sojus( void )
{
    Sojus* sojus = g_malloc0( sizeof( Sojus ) );

    sojus->sachgebiete = g_ptr_array_new_with_free_func( (GDestroyNotify) g_free );
    sojus->beteiligtenart = g_ptr_array_new_with_free_func( (GDestroyNotify) g_free );
    sojus->sachbearbeiter = g_ptr_array_new_with_free_func( (GDestroyNotify) g_free );

    sojus->settings = settings_open( );

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
    init_app_window( app, *sojus );
    init_socket( *sojus );
    init_db( *sojus );


    gchar* dokument_dir = g_settings_get_string( (*sojus)->settings, "dokument-dir" );
    g_object_set_data( G_OBJECT((*sojus)->widgets.AppWindow.AktenSchnellansicht.treeview_fm), "root", dokument_dir );

    fm_load_dir( GTK_TREE_VIEW((*sojus)->widgets.AppWindow.AktenSchnellansicht.treeview_fm), NULL, NULL );

    return;
}


gint
main(int argc,
     char **argv)
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
