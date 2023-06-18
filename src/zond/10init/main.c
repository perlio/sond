/*
zond (main.c) - Akten, Beweisstücke, Unterlagen
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

//#include <stdio.h>
#include <unistd.h>
#include <gtk/gtk.h>
#include <glib/gstdio.h>

#ifdef __WIN32
#include <libloaderapi.h>
#include <errhandlingapi.h>
#endif // __WIN32

#include "../global_types.h"

#include "../../misc.h"
#include "../20allgemein/project.h"
#include "../20allgemein/zieleplus.h"

#include "icons.h"
#include "app_window.h"
#include "headerbar.h"


static void
recover( Projekt* zond, gchar* project, GApplication* app )
{
    gint rc = 0;
    gchar* path_bak = NULL;
    gchar* path_tmp = NULL;

    gchar* text_abfrage = g_strconcat( "Projekt ", project, " wurde nicht richtig "
            "geschlossen. Wiederherstellen?", NULL );
    gint res = abfrage_frage( zond->app_window, "Wiederherstellen", text_abfrage, NULL );
    g_free( text_abfrage );

    if ( res == GTK_RESPONSE_YES )
    {
        path_bak = g_strconcat( project, ".bak", NULL);
        rc = g_rename( project, path_bak );
        if ( rc ) display_message( zond->app_window, "g_rename .ZND to .bak:\n",
                strerror( errno ), NULL );
        g_free( path_bak );

        path_tmp = g_strconcat( project, ".tmp", NULL );
        rc = g_rename( path_tmp, project );
        if ( rc ) display_message( zond->app_window, "rename .tmp to .ZND:\n",
                strerror( errno ), NULL );
        else display_message( zond->app_window, project, " erfolgreich wiederhergestellt",
                NULL );
        g_free( path_tmp );
    }
    else if ( res != GTK_RESPONSE_NO) g_application_quit( app );

    g_settings_set_string( zond->settings, "project", "" );
    g_settings_set_boolean( zond->settings, "speichern", FALSE );

    return;
}


static void
set_icon( Icon* icon, const gchar* icon_name,
        const gchar* display_name )
{
    icon->icon_name = icon_name;
    icon->display_name = display_name;

    return;
}



static void
init_icons( Projekt* zond )
{
    GResource* resource = icons_get_resource( );
    g_resources_register( resource );

    GtkIconTheme* icon_theme = gtk_icon_theme_get_default( );
    gtk_icon_theme_add_resource_path( icon_theme, "/icons" );

//    zond->icon[ICON_NOTHING] = { "dialog-error", "Nix" };
    set_icon( &zond->icon[ICON_NOTHING], "dialog-error", "Nix" );
    set_icon( &zond->icon[ICON_NORMAL], "media-record", "Punkt" );
    set_icon( &zond->icon[ICON_ORDNER], "folder", "Ordner" );
    set_icon( &zond->icon[ICON_DATEI], "document-open", "Datei" );
    set_icon( &zond->icon[ICON_PDF], "pdf", "PDF" );
    set_icon( &zond->icon[ICON_ANBINDUNG], "anbindung", "Anbindung" );
    set_icon( &zond->icon[ICON_AKTE], "akte", "Akte" );
    set_icon( &zond->icon[ICON_EXE], "application-x-executable", "Ausführbar" );
    set_icon( &zond->icon[ICON_TEXT], "text-x-generic", "Text" );
    set_icon( &zond->icon[ICON_DOC], "x-office-document", "Writer/Word" );
    set_icon( &zond->icon[ICON_PPP], "x-office-presentation", "PowerPoint" );
    set_icon( &zond->icon[ICON_SPREAD], "x-office-spreadsheet", "Tabelle" );
    set_icon( &zond->icon[ICON_IMAGE], "emblem-photos", "Bild" );
    set_icon( &zond->icon[ICON_VIDEO], "video-x-generic", "Video" );
    set_icon( &zond->icon[ICON_AUDIO], "audio-x-generic", "Audio" );
    set_icon( &zond->icon[ICON_EMAIL], "mail-unread", "E-Mail" );
    set_icon( &zond->icon[ICON_HTML], "emblem-web", "HTML" ); //16

    //Platzhalter
    set_icon( &zond->icon[17], "process-stop", "Frei" );
    set_icon( &zond->icon[18], "process-stop", "Frei" );
    set_icon( &zond->icon[19], "process-stop", "Frei" );
    set_icon( &zond->icon[20], "process-stop", "Frei" );
    set_icon( &zond->icon[21], "process-stop", "Frei" );
    set_icon( &zond->icon[22], "process-stop", "Frei" );
    set_icon( &zond->icon[23], "process-stop", "Frei" );
    set_icon( &zond->icon[24], "process-stop", "Frei" );

    set_icon( &zond->icon[ICON_DURCHS], "system-log-out", "Durchsuchung" );
    set_icon( &zond->icon[ICON_ORT], "mark-location", "Ort" );
    set_icon( &zond->icon[ICON_PHONE], "phone", "TKÜ" );
    set_icon( &zond->icon[ICON_WICHTIG], "emblem-important", "Wichtig" );
    set_icon( &zond->icon[ICON_OBS], "camera-web", "Observation" );
    set_icon( &zond->icon[ICON_CD], "media-optical", "CD" );
    set_icon( &zond->icon[ICON_PERSON], "user-info", "Person" );
    set_icon( &zond->icon[ICON_PERSONEN], "system-users", "Personen" );
    set_icon( &zond->icon[ICON_ORANGE], "orange", "Orange" );
    set_icon( &zond->icon[ICON_BLAU], "blau", "Blau" );
    set_icon( &zond->icon[ICON_ROT], "rot", "Rot" );
    set_icon( &zond->icon[ICON_GRUEN], "gruen", "Grün" );
    set_icon( &zond->icon[ICON_TUERKIS], "tuerkis", "Türkis" );
    set_icon( &zond->icon[ICON_MAGENTA], "magenta", "Magenta" );

    return;
}


static void
log_init( Projekt* zond )
{
    gchar* logfile = NULL;
    FILE* file = NULL;
    FILE* file_tmp = NULL;
    GDateTime* date_time = NULL;

    date_time = g_date_time_new_now_local( );
    logfile = g_strdup_printf( "%slogs/log_%i_%i.log", zond->base_dir,
            g_date_time_get_year( date_time ), g_date_time_get_month( date_time ) );
    g_date_time_unref( date_time );

    file = freopen( logfile, "a", stdout );
    if ( !file )
    {
        display_message( zond->app_window, "stout konnte nicht in Datei %s "
                "umgeleitet werden:\n%s", logfile, strerror( errno ), NULL );
        g_free( logfile );
        exit( -1 );
    }

    file_tmp = freopen( logfile, "a", stderr );
    if ( !file_tmp )
    {
        display_message( zond->app_window, "sterr konnte nicht in "
                "Datei %s umgeleitet werden:\n%s", logfile, strerror( errno ), NULL );
        g_free( logfile );
        exit( -1 );
    }
/*
    ret = dup2( fileno( stdout ), fileno( stderr ) );
    if ( ret == -1 )
    {
        display_message( zond->app_window, "stderr konnte nicht auf stdout "
                "umgeleitet werden: %s", strerror( errno ) );
        exit( -1 );
    }
    */
    g_free( logfile );

    return;
}


static void
init( GtkApplication* app, Projekt* zond )
{
    zond->base_dir = get_base_dir( );

#ifndef TESTING
    log_init( zond );
#endif // TESTING

    //benötigte Arrays erzeugen
    zond->arr_pv = g_ptr_array_new( );

    init_icons( zond );

    init_app_window( zond );
    gtk_application_add_window( app, GTK_WINDOW(zond->app_window) );

    init_headerbar( zond );

    //GSettings
    zond->settings = g_settings_new( "de.perlio.zond" );
    g_settings_bind( zond->settings, "internalviewer",
            G_OBJECT(zond->menu.internal_vieweritem), "active",
            G_SETTINGS_BIND_DEFAULT );

    //Wiederherstellung bei Absturz
    //(d.h. in den Settings wurde project nicht auf "" gesetzt)
    gchar* proj_settings = g_settings_get_string( zond->settings, "project" );
    gboolean speichern = g_settings_get_boolean( zond->settings, "speichern" );
    if ( g_strcmp0( proj_settings, "" ) != 0 && speichern ) recover( zond,
            proj_settings, G_APPLICATION(app) );
    g_free( proj_settings );

    projekt_set_widgets_sensitiv( zond, FALSE );
    gtk_widget_set_sensitive( zond->menu.speichernitem, FALSE );
    g_settings_set_boolean( zond->settings, "speichern", FALSE );

    DisableDebug( );

    gtk_widget_show_all( zond->app_window );
    gtk_widget_hide( gtk_paned_get_child1( GTK_PANED(zond->hpaned) ) );

    zond->ctx = fz_new_context( NULL, NULL, FZ_STORE_UNLIMITED );
    if ( !zond->ctx )
    {
        display_message( zond->app_window, "zond->ctx konnte nicht initialisiert werden",
                NULL );
        gboolean ret = FALSE;
        g_signal_emit_by_name( zond->app_window, "delete-event", NULL, &ret );

        return;
    }

    return;
}


static void
open_app( GtkApplication* app, gpointer files, gint n_files, gchar *hint,
        gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    GFile** g_file;
    gchar* uri = NULL;
    gchar* uri_unesc = NULL;

    Projekt* zond = (Projekt*) user_data;

//    if ( zond->dbase_zond ) return; //Verhindert, daß neue Datei geladen wird,
//wenn bereits Project geöffnet

    g_file = (GFile**) files;

    uri = g_file_get_uri( g_file[0] );
    uri_unesc = g_uri_unescape_string( uri, NULL );
    g_free( uri );

    rc = project_oeffnen( zond, uri_unesc + 8, FALSE, &errmsg );
    g_free( uri_unesc );
    if ( rc == -1 )
    {
        display_message( zond->app_window, "Fehler - Projekt kann nicht geöffnet "
                "werden\n\nBei Aufruf projekt_oeffnen:\n", errmsg, NULL );
        g_free( errmsg );

        return;
    }

    return;
}


static void
activate_app( GtkApplication* app, gpointer data )
{
    Projekt* zond = (Projekt*) data;

    gtk_window_present( GTK_WINDOW(zond->app_window) );

    return;
}


static void
startup_app( GtkApplication* app, gpointer data )
{
    Projekt* zond = (Projekt*) data;

    init( app, zond );

    g_message( "zond gestartet" );

    return;
}


int main(int argc, char **argv)
{
    GtkApplication* app = NULL;
    Projekt zond = { 0 };

    //ApplicationApp erzeugen
    app = gtk_application_new ( "de.perlio.zond", G_APPLICATION_HANDLES_OPEN );

    //und starten
    g_signal_connect( app, "startup", G_CALLBACK (startup_app), &zond );
    g_signal_connect( app, "activate", G_CALLBACK (activate_app), &zond );
    g_signal_connect( app, "open", G_CALLBACK (open_app), &zond );

    gint status = g_application_run( G_APPLICATION(app), argc, argv );

    g_message( "zond beendet" );

    g_object_unref(app);

    return status;
}
