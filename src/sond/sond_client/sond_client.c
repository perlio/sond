#include "sond_client.h"
#include "sond_client_dok.h"
#include "../../misc.h"

#include <gtk/gtk.h>


static gboolean
sond_client_close( GtkWidget* app_window, GdkEvent* event, gpointer data )
{
    SondClient* sond_client = (SondClient*) data;
    g_free( sond_client->user );
    g_free( sond_client->password );

    gtk_widget_destroy( sond_client->app_window );

    return TRUE;
}


void
sond_client_quit( SondClient* sond_client )
{
    sond_client_close( NULL, NULL, sond_client );

    return;
}


static void
sond_client_auth_user( SondClient* sond_client )
{
    sond_client->user = g_strdup( "krieger@rubarth-krieger.de" );
    abfrage_frage( sond_client->app_window, "Passwort eingeben. User:",
            sond_client->user, &sond_client->password );

    return;
}


static void
sond_client_init_app_window( GtkApplication* app, SondClient* sond_client )
{
    GtkWidget* grid = NULL;
    GtkWidget* frame_dok = NULL;
    GtkWidget* entry_dok = NULL;

    sond_client->app_window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_title( GTK_WINDOW(sond_client->app_window), "SondClient" );
    gtk_widget_set_size_request( sond_client->app_window, 250, 40 );
    gtk_application_add_window( app, GTK_WINDOW(sond_client->app_window) );

    grid = gtk_grid_new( );
    gtk_container_add( GTK_CONTAINER(sond_client->app_window), grid );

    frame_dok = gtk_frame_new( "Dokumentenverzeichnis öffnen" );
    entry_dok = gtk_entry_new( );
    gtk_container_add( GTK_CONTAINER(frame_dok), entry_dok );
    gtk_grid_attach( GTK_GRID(grid), frame_dok, 0, 0, 1, 1 );

    gtk_widget_show_all( sond_client->app_window );

    g_signal_connect( sond_client->app_window, "delete-event", G_CALLBACK(sond_client_close), sond_client );
    g_signal_connect( entry_dok, "activate", G_CALLBACK(sond_client_dok_entry_activate), sond_client );

    return;
}

static void
sond_client_init( GtkApplication* app, SondClient* sond_client )
{
    sond_client_init_app_window( app, sond_client );
    sond_client_auth_user( sond_client );

    return;
}


static void
activate_app( GtkApplication* app, gpointer data )
{
    SondClient* sond_client = (SondClient*) data;

    gtk_window_present( GTK_WINDOW(sond_client->app_window) );

    return;
}


static void
startup_app( GtkApplication* app, gpointer data )
{
    SondClient* sond_client = (SondClient*) data;

    sond_client_init( app, sond_client );

    return;
}


gint
main( int argc, char **argv)
{
    GtkApplication* app = NULL;
    SondClient sond_client = { 0 };
    gint status = 0;

    //ApplicationApp erzeugen
    app = gtk_application_new ( "de.rubarth-krieger.sond_client", G_APPLICATION_DEFAULT_FLAGS );

    //und starten
    g_signal_connect( app, "startup", G_CALLBACK(startup_app), &sond_client );
    g_signal_connect( app, "activate", G_CALLBACK (activate_app), &sond_client );

    status = g_application_run( G_APPLICATION (app), argc, argv );

    g_object_unref( app );

    return status;
}
