/*
zond (project.c) - Akten, Beweisstücke, Unterlagen
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

#include <sqlite3.h>
#include <gtk/gtk.h>
#include <glib/gstdio.h>

#include "../../misc.h"
#include "../../dbase.h"
#include "../../sond_treeviewfm.h"

#include "../global_types.h"
#include "../zond_dbase.h"

#include "../10init/app_window.h"

#include "../99conv/general.h"

#include "../40viewer/viewer.h"
#include "../zond_tree_store.h"

#include "project.h"
#include "treeviews.h"



void
project_set_changed( gpointer user_data )
{
    Projekt* zond = (Projekt*) user_data;

    zond->dbase_zond->changed = TRUE;
    gtk_widget_set_sensitive( zond->menu.speichernitem, TRUE );
    g_settings_set_boolean( zond->settings, "speichern", TRUE );

    return;
}


void
project_reset_changed( Projekt* zond )
{
    zond->dbase_zond->changed = FALSE;
    gtk_widget_set_sensitive( zond->menu.speichernitem, FALSE );
    g_settings_set_boolean( zond->settings, "speichern", FALSE );

    return;
}


void
projekt_set_widgets_sensitiv( Projekt* zond, gboolean active )
{
    gtk_widget_set_sensitive( GTK_WIDGET(zond->menu.schliessenitem), active );
    gtk_widget_set_sensitive( GTK_WIDGET(zond->menu.exportitem), active );
    gtk_widget_set_sensitive( GTK_WIDGET(zond->menu.pdf), active );
    gtk_widget_set_sensitive( GTK_WIDGET(zond->menu.struktur), active );
    gtk_widget_set_sensitive( GTK_WIDGET(zond->menu.ansicht), active );
    gtk_widget_set_sensitive( GTK_WIDGET(zond->fs_button), active );
    gtk_widget_set_sensitive( GTK_WIDGET(zond->menu.extras), active );

    return;
}


static gint
project_create_dbase_zond( Projekt* zond, const gchar* path, gboolean create,
        DBaseZond** dbase_zond, gchar** errmsg )
{
    gint rc = 0;
    ZondDBase* zond_dbase_work = NULL;
    ZondDBase* zond_dbase_store = NULL;
    gchar* path_tmp = NULL;

    rc  = zond_dbase_new( path, FALSE, create, &zond_dbase_store, errmsg );
    if ( rc == -1 ) ERROR_SOND( "zond_dbase_new" )
    else if ( rc == 1 ) return 1;

    path_tmp = g_strconcat( path, ".tmp", NULL );

    rc = zond_dbase_new( path_tmp, TRUE, FALSE, &zond_dbase_work, errmsg );
    g_free( path_tmp );
    if ( rc )
    {
        zond_dbase_close( zond_dbase_store );
        if ( rc == -1 ) ERROR_SOND( "zond_dbase_new" )
        else if ( rc == 1 ) return 1;
    }

    rc = zond_dbase_backup( zond_dbase_store, zond_dbase_work, errmsg );
    if ( rc )
    {
        zond_dbase_close( zond_dbase_store );
        zond_dbase_close( zond_dbase_work );
        ERROR_S
    }

    sqlite3_update_hook( zond_dbase_get_dbase( zond_dbase_work ),
            (void*) project_set_changed, (gpointer) zond );


    *dbase_zond = g_malloc0( sizeof( DBaseZond ) );

    (*dbase_zond)->zond_dbase_store = zond_dbase_store;
    (*dbase_zond)->zond_dbase_work = zond_dbase_work;
    (*dbase_zond)->project_name = g_strdup( strrchr( path, '/' ) + 1 );
    (*dbase_zond)->project_dir = g_strndup( path, strlen( path ) - strlen(
            strrchr( path, '/' ) ) );

    return 0;
}


static void
projekt_aktivieren( Projekt* zond )
{
    //zum Arbeitsverzeichnis machen
    g_chdir( zond->dbase_zond->project_dir );

    projekt_set_widgets_sensitiv( zond, TRUE );

    //project_name als Titel Headerbar
    gtk_header_bar_set_title(
            GTK_HEADER_BAR(gtk_window_get_titlebar(
            GTK_WINDOW(zond->app_window) )), zond->dbase_zond->project_name );

    //project_name in settings schreiben
    gchar* set = g_strconcat( zond->dbase_zond->project_dir, "/",
            zond->dbase_zond->project_name, NULL );
    g_settings_set_string( zond->settings, "project", set );
    g_free( set );

    project_reset_changed( zond );

    return;
}


static void
project_clear_dbase_zond( DBaseZond** dbase_zond )
{
    g_free( (*dbase_zond)->project_dir );
    g_free( (*dbase_zond)->project_name );

    g_object_unref( (*dbase_zond)->zond_dbase_store );
    g_object_unref( (*dbase_zond)->zond_dbase_work );
    g_free( *dbase_zond );

    *dbase_zond = NULL;

    return;
}


gint
project_speichern( Projekt* zond, gchar** errmsg )
{
    gint rc = 0;

    if ( !(zond->dbase_zond->changed) ) return 0;

    rc = zond_dbase_backup( zond->dbase_zond->zond_dbase_work,
            zond->dbase_zond->zond_dbase_store, errmsg );
    if ( rc ) ERROR_S

    project_reset_changed( zond );

    return 0;
}


void
cb_menu_datei_speichern_activate( GtkMenuItem* item, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    Projekt* zond = (Projekt*) user_data;

    rc = project_speichern( zond, &errmsg );
    if ( rc )
    {
        display_message( zond->app_window, "Fehler beim Speichern -\n\nBei Aufruf "
                "project_speichern:\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


gint
projekt_schliessen( Projekt* zond, gchar** errmsg )
{
    if ( !zond->dbase_zond ) return 0;

    if ( zond->dbase_zond->changed )
    {
        gint rc = 0;

        rc = abfrage_frage( zond->app_window, "Datei schließen", "Änderungen "
                "aktuelles Projekt speichern?", NULL );

        if ( rc == GTK_RESPONSE_YES )
        {
            gint rc = 0;
            rc = project_speichern( zond, errmsg );
            if ( rc ) ERROR_SOND( "projekt_speichern" )
        }
        else if ( rc != GTK_RESPONSE_NO) return 1;
    }

    for ( gint i = 0; i < zond->arr_pv->len; i++ )
            viewer_save_and_close( g_ptr_array_index( zond->arr_pv, i ) );

    //Menus aktivieren/ausgrauen
    projekt_set_widgets_sensitiv( zond, FALSE );

    //damit focus aus text_view rauskommt und changed-signal abgeschaltet wird
    gtk_widget_grab_focus( GTK_WIDGET(zond->treeview[BAUM_INHALT]) );

    //textview leeren
    GtkTextBuffer* buffer = gtk_text_view_get_buffer( zond->textview );
    gtk_text_buffer_set_text( buffer, "", -1 );

    g_object_set_data( G_OBJECT(buffer), "changed", NULL );
    g_object_set_data( G_OBJECT(zond->textview), "node-id", NULL );

    project_reset_changed( zond );

    //Vor leeren der treeviews: focus-in-callback blocken
    //darin wird cursor-changed-callback angeschaltet --> Absturz
    g_signal_handler_block( zond->treeview[BAUM_INHALT],
            zond->treeview_focus_in_signal[BAUM_INHALT] );
    g_signal_handler_block( zond->treeview[BAUM_AUSWERTUNG],
            zond->treeview_focus_in_signal[BAUM_AUSWERTUNG] );

    //treeviews leeren
    zond_tree_store_clear( ZOND_TREE_STORE(gtk_tree_view_get_model(
            GTK_TREE_VIEW(zond->treeview[BAUM_INHALT]) )) );
    zond_tree_store_clear( ZOND_TREE_STORE(gtk_tree_view_get_model(
            GTK_TREE_VIEW(zond->treeview[BAUM_AUSWERTUNG]) )) );

    //Wieder anschalten
    g_signal_handler_unblock( zond->treeview[BAUM_INHALT],
            zond->treeview_focus_in_signal[BAUM_INHALT] );
    g_signal_handler_unblock( zond->treeview[BAUM_AUSWERTUNG],
            zond->treeview_focus_in_signal[BAUM_AUSWERTUNG] );

    //muß vor project_destroy..., weil callback ausgelöst wird, der db_get_node_id... aufruft
    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(zond->fs_button), FALSE );

    gchar* working_copy = g_strconcat( zond->dbase_zond->project_dir, "/",
            zond->dbase_zond->project_name, ".tmp", NULL );

    sond_treeviewfm_set_root( SOND_TREEVIEWFM(zond->treeview[BAUM_FS]), NULL, NULL );

    sond_treeviewfm_set_dbase( SOND_TREEVIEWFM(zond->treeview[BAUM_FS]), NULL );
    project_clear_dbase_zond( &(zond->dbase_zond) );

    gint res = g_remove( working_copy );
    if ( res == -1 ) display_message( zond->app_window, "Fehler beim Löschen der "
            "temporären Datenbank:\n", strerror( errno ), NULL );
    g_free( working_copy );

    gtk_header_bar_set_title(
            GTK_HEADER_BAR(gtk_window_get_titlebar(
            GTK_WINDOW(zond->app_window) )), "" );

    //project in settings auf leeren String setzen
    g_settings_set_string( zond->settings, "project", "" );

    g_signal_handler_disconnect( zond->app_window, zond->key_press_signal );

    return 0;
}


void
cb_menu_datei_schliessen_activate( GtkMenuItem* item, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    Projekt* zond = (Projekt*) user_data;

    rc = projekt_schliessen( zond, &errmsg );
    if ( rc == -1 )
    {
        display_message( zond->app_window, "Fehler bei Schließen des Projekts -\n\n"
                "Bei Aufruf projekt_schliessen:\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


gint
project_oeffnen( Projekt* zond, const gchar* abs_path, gboolean create,
        gchar** errmsg )
{
    gint rc = 0;
    DBaseZond* dbase_zond = NULL;

    rc = projekt_schliessen( zond, errmsg );
    if ( rc )
    {
        project_clear_dbase_zond( &dbase_zond );
        if ( rc == -1 ) ERROR_S
        else return 1;
    }

    rc = project_create_dbase_zond( zond, abs_path, create, &dbase_zond, errmsg );
    if ( rc ) ERROR_S

    sond_treeviewfm_set_dbase( SOND_TREEVIEWFM(zond->treeview[BAUM_FS]),
            dbase_zond->zond_dbase_work );

    zond->dbase_zond = dbase_zond;
    rc = sond_treeviewfm_set_root( SOND_TREEVIEWFM(zond->treeview[BAUM_FS]),
            zond->dbase_zond->project_dir, errmsg );
    if ( rc ) ERROR_S

    projekt_aktivieren( zond );

    //key_press-event-signal einschalten
    zond->key_press_signal = g_signal_connect( zond->app_window,
            "key-press-event", G_CALLBACK(cb_key_press), zond );

    if ( !create )
    {
        rc = treeviews_reload_baeume( zond, errmsg );
        if ( rc == -1 ) ERROR_S
    }

    return 0;
}


static gint
project_wechseln( Projekt* zond )
{
    gint rc = 0;
    //nachfragen, ob aktuelles Projekt gespeichert werden soll
    if ( !zond->dbase_zond ) return 0;

    rc = abfrage_frage( zond->app_window, zond->dbase_zond->project_name,
            "Projekt schließen?", NULL );
    if( (rc != GTK_RESPONSE_YES) ) return 1; //Abbrechen -> nicht öffnen

    return 0;
}


void
cb_menu_datei_oeffnen_activate( GtkMenuItem* item, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    Projekt* zond = (Projekt*) user_data;

    rc = project_wechseln( zond );
    if ( rc ) return;

    gchar* abs_path = filename_oeffnen( GTK_WINDOW(zond->app_window) );
    if ( !abs_path ) return;

    rc = project_oeffnen( zond, abs_path, FALSE, &errmsg );
    g_free( abs_path );
    if ( rc == -1 )
    {
        display_message( zond->app_window, "Fehler beim Öffnen-\n\nBei Aufruf "
                "project_oeffnen:\n", errmsg, NULL );
        g_free( errmsg );

        return;
    }
    else if ( rc == 1 ) return;

    return;
}


void
cb_menu_datei_neu_activate( GtkMenuItem* item, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    Projekt* zond = (Projekt*) user_data;

    rc = project_wechseln( zond );
    if ( rc ) return;

    gchar* abs_path = filename_speichern( GTK_WINDOW(zond->app_window),
            "Projekt anlegen", ".ZND" );
    if ( !abs_path ) return;

    rc = project_oeffnen( zond, abs_path, TRUE, &errmsg );
    g_free( abs_path );
    if ( rc == -1 )
    {
        display_message( zond->app_window, "Fehler beim Öffnen-\n\nBei Aufruf "
                "project_oeffnen:\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


