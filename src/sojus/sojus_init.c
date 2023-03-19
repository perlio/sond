/*
sojus (sojus_init.c) - softkanzlei
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

#include "../misc.h"

#include "global_types_sojus.h"
#include "sojus_dir.h"
#include "sojus_init.h"


static void
sojus_init_monitor_changed( GFileMonitor* monitor, GFile* file, GFile* other_file,
        GFileMonitorEvent type, gpointer data )
{
    gchar* str_file = NULL;
    gchar* str_other_file = NULL;
//    const gchar* str_type = NULL;

    Sojus* sojus = (Sojus*) data;

    if ( file ) str_file = g_file_get_basename( file );
    if ( other_file ) str_other_file = g_file_get_basename( other_file );

    if ( type == G_FILE_MONITOR_EVENT_CREATED )
    {
        const gchar* last_inserted = NULL;

        last_inserted = (const gchar*) g_object_get_data( G_OBJECT(monitor), "last-inserted" );

        //Bei SMB-Zugriff kommt es zur doppeltenAuslsung des created-Signals
        //war als letztes schon created - Doppel
        if ( !g_strcmp0( last_inserted, str_file ) ) g_object_set_data( G_OBJECT(monitor),
                "last-inserted", NULL );
        //wirklich neu
        else g_ptr_array_add( sojus->arr_dirs, g_strdup( str_file ) );
    }
    else if ( type == G_FILE_MONITOR_EVENT_DELETED || type == G_FILE_MONITOR_EVENT_RENAMED )
    {
        gint i = 0;

        //index ermitteln
        for ( i = 0; i < sojus->arr_dirs->len; i++ )
                if ( !g_strcmp0( g_ptr_array_index( sojus->arr_dirs, i ),
                str_file ) ) break;

        if ( i == sojus->arr_dirs->len ) //nicht gefunden
                display_message( sojus->app_window, "Verzeichnis """, str_file,
                """ wurde nicht gefunden", NULL );

        else
        {
            g_ptr_array_remove_index_fast( sojus->arr_dirs, i );

            if ( type == G_FILE_MONITOR_EVENT_RENAMED )
                    g_ptr_array_add( sojus->arr_dirs, g_strdup( str_other_file ) );
        }
    }

    g_free( str_file );
    g_free( str_other_file );

/*
    switch( type )
    {
        case G_FILE_MONITOR_EVENT_CHANGED: str_type = "G_FILE_MONITOR_EVENT_CHANGED"; break;
        case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT: str_type = "G_FILE_MONITOR_EVENT_CHANGES_NONE_HINT"; break;
        case G_FILE_MONITOR_EVENT_DELETED: str_type = "G_FILE_MONITOR_EVENT_DELETED"; break;
        case G_FILE_MONITOR_EVENT_CREATED: str_type = "G_FILE_MONITOR_EVENT_CREATED"; break;
        case G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED: str_type = "G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED"; break;
        case G_FILE_MONITOR_EVENT_PRE_UNMOUNT: str_type = "G_FILE_MONITOR_EVENT_PRE_UNMOUNT"; break;
        case G_FILE_MONITOR_EVENT_UNMOUNTED: str_type = "G_FILE_MONITOR_EVENT_UNMOUNTED"; break;
        case G_FILE_MONITOR_EVENT_MOVED: str_type = "G_FILE_MONITOR_EVENT_MOVED"; break;
        case G_FILE_MONITOR_EVENT_RENAMED: str_type = "G_FILE_MONITOR_EVENT_RENAMED"; break;
        case G_FILE_MONITOR_EVENT_MOVED_IN: str_type = "G_FILE_MONITOR_EVENT_MOVED_IN"; break;
        case G_FILE_MONITOR_EVENT_MOVED_OUT: str_type = "G_FILE_MONITOR_EVENT_MOVED_OUT"; break;
    }

    printf("%s  %s  %s\n", str_file, str_other_file, str_type );
*/
    return;
}


//Directory einlesen und Monitor installieren
static void
sojus_init_load_dirs( Sojus* sojus )
{
    //root-dir herausfinden
    gchar* base_dir = NULL;
    GKeyFile* key_file = NULL;
    gchar* conf_path = NULL;
    gboolean success = FALSE;
    GError* error = NULL;
    gchar* root_dir = NULL;
    gboolean ret = FALSE;

    key_file = g_key_file_new( );

    base_dir = get_base_dir( );
    conf_path = g_build_filename( base_dir, "Sojus.conf", NULL );
    g_free( base_dir );
    success = g_key_file_load_from_file( key_file, conf_path,
            G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS, &error );
    g_free( conf_path );
    if ( !success )
    {
        display_message( sojus->app_window, "Sojus.conf konnte nicht gelesen werden:\n",
                error->message, NULL );
        g_error_free( error );
        g_key_file_free( key_file );

        g_signal_emit_by_name( sojus->app_window, "delete-event", NULL, &ret );
    }

    root_dir = g_key_file_get_string( key_file, "SOJUS", "root", &error );
    if ( error )
    {
        display_message( sojus->app_window, "Sojus root-dir konnte nicht ermittelt werden:\n",
                error->message, NULL );
        g_error_free( error );
        g_key_file_free( key_file );

        g_signal_emit_by_name( sojus->app_window, "delete-event", NULL, &ret );
    }

    g_key_file_free( key_file );

    //Verzeichnisse einlesen
    GFile* root = NULL;
    GFileEnumerator* enumer = NULL;

    root = g_file_new_for_path( root_dir );

    enumer = g_file_enumerate_children( root, "*", G_FILE_QUERY_INFO_NONE,
            NULL, &error );
    if ( !enumer )
    {
        display_message( sojus->app_window, "Root-Verzeichnis ", root_dir,
                "kann nicht gelesen werden:\n", error->message, NULL );
        g_error_free( error );
        g_free( root_dir );
        g_object_unref( root );

        g_signal_emit_by_name( sojus->app_window, "delete-event", NULL, &ret );
    }

    g_free( root_dir );

    //durchgehen
    //new_anchor kopieren, da in der Schleife verändert wird
    //es soll aber der soeben erzeugte Punkt zurückgegegen werden

    while ( 1 )
    {
        GFile* file_child = NULL;

        if ( !g_file_enumerator_iterate( enumer, NULL, &file_child, NULL, &error ) )
        {
            display_message( sojus->app_window, "Bei Aufruf g_file_enumerator_iterate:\n",
                    error->message, NULL );
            g_error_free( error );
            g_object_unref( enumer );
            g_object_unref( root );

            g_signal_emit_by_name( sojus->app_window, "delete-event", NULL, &ret );
        }

        if ( file_child ) //es gibt noch Datei in Verzeichnis
        {
            GFileType type = g_file_query_file_type( file_child, G_FILE_QUERY_INFO_NONE, NULL );
            if ( type == G_FILE_TYPE_DIRECTORY ) g_ptr_array_add( sojus->arr_dirs,
                    g_file_get_basename( file_child ) );
            //else continue;
        } //ende if ( child )
        else break;
    }

    g_object_unref( enumer );

    //Monitor für Dir
    sojus->monitor = g_file_monitor_directory( root, G_FILE_MONITOR_WATCH_MOVES, NULL, &error );
    g_object_unref( root );
    if ( !(sojus->monitor) )
    {
        display_message( sojus->app_window, "Root-Dir kann nicht überwacht werden:\n",
                error->message, NULL );
        g_error_free( error );

        g_signal_emit_by_name( sojus->app_window, "delete-event", NULL, &ret );
    }

    g_signal_connect( sojus->monitor, "changed", G_CALLBACK(sojus_init_monitor_changed), sojus );




}


static void
sojus_init_entry_activated( GtkWidget* entry, gpointer data )
{
    sojus_dir_open( (Sojus*) data, gtk_entry_get_text( GTK_ENTRY(entry) ) );

    return;
}


static gboolean
cb_desktop_delete_event( GtkWidget* app_window, GdkEvent* event, gpointer data )
{
    Sojus* sojus = (Sojus*) data;

    gtk_widget_destroy( app_window );

    g_ptr_array_unref( sojus->arr_dirs );
    g_object_unref( sojus->monitor );

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
    GtkWidget* entry_dok = gtk_entry_new( );
    gtk_container_add( GTK_CONTAINER(sojus->app_window), entry_dok );

/*
**  callbacks  */
    g_signal_connect( entry_dok, "activate",
            G_CALLBACK(sojus_init_entry_activated), sojus );
    //Signal für App-Fenster schließen
    g_signal_connect( sojus->app_window, "delete-event",
            G_CALLBACK(cb_desktop_delete_event), sojus );

    gtk_widget_show_all( GTK_WIDGET(sojus->app_window) );

    return;
}


Sojus*
sojus_init( GtkApplication* app )
{
    Sojus* sojus = g_malloc0( sizeof( Sojus ) );

    sojus_init_app_window( sojus );
    gtk_application_add_window( app, GTK_WINDOW(sojus->app_window) );

    sojus->arr_dirs = g_ptr_array_new_full( 0, (GDestroyNotify) g_free );
    sojus_init_load_dirs( sojus );

    return sojus;
}


