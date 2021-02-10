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
#include "../../fm.h"
#include "../../dbase.h"

#include "../global_types.h"
#include "../error.h"

#include "../99conv/general.h"
#include "../99conv/db_zu_baum.h"

#include "../40viewer/viewer.h"

#include "fs_tree.h"
#include "project.h"
#include "project_db.h"
#include "dbase_full.h"



static void
project_set_changed( gpointer user_data )
{
    Projekt* zond = (Projekt*) user_data;

    zond->dbase_zond->changed = TRUE;
    gtk_widget_set_sensitive( zond->menu.speichernitem, TRUE );
    g_settings_set_boolean( zond->settings, "speichern", TRUE );

    return;
}


static void
project_reset_changed( Projekt* zond )
{
    zond->dbase_zond->changed = FALSE;
    gtk_widget_set_sensitive( zond->menu.speichernitem, FALSE );
    g_settings_set_boolean( zond->settings, "speichern", FALSE );

    return;
}


gint
project_test_rel_path( const GFile* file, gpointer data, gchar** errmsg )
{
    gint rc = 0;
    gchar* rel_path = NULL;

    Projekt* zond = (Projekt*) data;

    rel_path = fm_get_rel_path_from_file( zond->dbase_zond->project_dir, file );

    rc = dbase_test_path( zond->dbase_zond->dbase_store, rel_path, errmsg );
    if ( rc ) g_free( rel_path );
    if ( rc == -1 ) ERROR( "dbase_test_path" )
    else if ( rc == 1 ) return 1;

    rc = dbase_test_path( (DBase*) zond->dbase_zond->dbase_work, rel_path, errmsg );
    g_free( rel_path );
    if ( rc == -1 ) ERROR( "dbase_test_path" )
    else if ( rc == 1 ) return 1;

    return 0;
}


gint
project_before_move( const GFile* file_source, const GFile* file_dest,
        gpointer data, gchar** errmsg )
{
    gint rc = 0;
    gboolean changed = FALSE;

    Projekt* zond = (Projekt*) data;

    rc = dbase_begin( zond->dbase_zond->dbase_store, errmsg );
    if ( rc ) ERROR( "dbase_begin (dbase_store" )

    rc = dbase_begin( (DBase*) zond->dbase_zond->dbase_work, errmsg );
    if ( rc ) ERROR_ROLLBACK( zond->dbase_zond->dbase_store, "dbase_begin (dbase_work)" )

    gchar* rel_path_source = fm_get_rel_path_from_file( zond->dbase_zond->project_dir, file_source );
    gchar* rel_path_dest = fm_get_rel_path_from_file( zond->dbase_zond->project_dir, file_dest );

    changed = zond->dbase_zond->changed;

    rc = dbase_update_path( zond->dbase_zond->dbase_store, rel_path_source, rel_path_dest, errmsg );
    if ( rc )
    {
        g_free( rel_path_source );
        g_free( rel_path_dest );

        ERROR_ROLLBACK_BOTH( zond->dbase_zond->dbase_store,
            (DBase*) zond->dbase_zond->dbase_work, "dbase_update_path (dbase_store)" )
    }

    rc = dbase_update_path( (DBase*) zond->dbase_zond->dbase_work, rel_path_source, rel_path_dest, errmsg );

    g_free( rel_path_source );
    g_free( rel_path_dest );

    if ( !changed ) project_reset_changed( zond );

    if ( rc ) ERROR_ROLLBACK_BOTH( zond->dbase_zond->dbase_store,
            (DBase*) zond->dbase_zond->dbase_work, "dbase_update_path (dbase_work)" )

    return 0;
}


gint
project_after_move( const gint rc_edit, gpointer data, gchar** errmsg )
{
    gint rc = 0;

    Projekt* zond = (Projekt*) data;

    if ( rc_edit == 1 )
    {
        gint rc1 = 0;
        gint rc2 = 0;
        gchar* err_rollback = NULL;

        rc1 = dbase_rollback( zond->dbase_zond->dbase_store, &err_rollback );
        if ( rc1 && errmsg ) *errmsg = g_strconcat( "Bei Aufruf dbase_rollback "
                "(dbase_store):\n", err_rollback, NULL );
        g_free( err_rollback );

        rc2 = dbase_rollback  ( (DBase*) zond->dbase_zond->dbase_work, &err_rollback );
        if ( rc2 && errmsg ) *errmsg = g_strconcat( "Bei Aufruf dbase_rollback "
                "(dbase_work):\n", err_rollback, NULL );
        g_free( err_rollback );

        if ( (rc1 || rc2) )
        {
            if ( errmsg ) *errmsg = add_string( *errmsg,
                    g_strdup( "\n\nDatenbank inkonsistent" ) );

            return -1;
        }
    }
    else
    {
        rc = dbase_commit( zond->dbase_zond->dbase_store, errmsg );
        if ( rc ) ERROR_ROLLBACK_BOTH( zond->dbase_zond->dbase_store,
                (DBase*) zond->dbase_zond->dbase_work, "dbase_commit (dbase_store)" )

        rc = dbase_commit( (DBase*) zond->dbase_zond->dbase_work, errmsg );
        if ( rc ) ERROR_ROLLBACK_BOTH( zond->dbase_zond->dbase_store,
                (DBase*) zond->dbase_zond->dbase_work, "dbase_commit (dbase_work)" )
    }

    return 0;
}


void
projekt_set_widgets_sensitiv( Projekt* zond, gboolean active )
{
    gtk_widget_set_sensitive( GTK_WIDGET(zond->menu.schliessenitem), active );
    gtk_widget_set_sensitive( GTK_WIDGET(zond->menu.exportitem), active );
    gtk_widget_set_sensitive( GTK_WIDGET(zond->menu.pdf), active );
    gtk_widget_set_sensitive( GTK_WIDGET(zond->menu.struktur), active );
    gtk_widget_set_sensitive( GTK_WIDGET(zond->menu.suchen), active );
    gtk_widget_set_sensitive( GTK_WIDGET(zond->menu.ansicht), active );
    gtk_widget_set_sensitive( GTK_WIDGET(zond->fs_button), active );
//    gtk_widget_set_sensitive( GTK_WIDGET(zond->menu.extras), TRUE );

    return;
}


static gint
project_backup( sqlite3* db_orig, sqlite3* db_dest, gchar** errmsg )
{
    gint rc = 0;

    //Datenbank öffnen
    sqlite3_backup* backup = NULL;
    backup = sqlite3_backup_init( db_dest, "main", db_orig, "main" );

    if ( !backup )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_backup_init\nresult code: ",
                sqlite3_errstr( sqlite3_errcode( db_dest ) ), "\n",
                sqlite3_errmsg( db_dest ), NULL );

        return -1;
    }
    rc = sqlite3_backup_step( backup, -1 );
    sqlite3_backup_finish( backup );
    if ( rc != SQLITE_DONE )
    {
        if ( errmsg && rc == SQLITE_NOTADB ) *errmsg = g_strdup( "Datei ist "
                "keine SQLITE-Datenbank" );
        else if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_backup_step:\nresult code: ",
                sqlite3_errstr( rc ), "\n", sqlite3_errmsg( db_dest ), NULL );

        return -1;
    }

    return 0;
}


static gint
project_create_dbase_zond( Projekt* zond, const gchar* path, gboolean create,
        DBaseZond** dbase_zond, gchar** errmsg )
{
    gint rc = 0;
    DBase* dbase = NULL;
    gchar* path_tmp = NULL;
    DBaseFull* dbase_full = NULL;

    rc = dbase_create_with_stmts( path, &dbase, create, FALSE, errmsg );
    if ( rc == -1 ) ERROR( "dbase_create" )
    else if ( rc == 1 ) return 1;

    path_tmp = g_strconcat( path, ".tmp", NULL );
    rc = dbase_full_create( path_tmp, &dbase_full, FALSE, TRUE, errmsg );
    if ( rc )
    {
        dbase_destroy( dbase );
        g_free( path_tmp );
        if ( rc == -1 ) ERROR( "dbase_full_create" )
        else if ( rc == 1 ) return 1;
    }

    rc = project_backup( dbase->db, dbase_full->dbase.db, errmsg );
    if ( rc )
    {
        dbase_destroy( dbase );
        dbase_destroy( (DBase*) dbase_full );
        ERROR( "project_backup" )
    }

    rc = dbase_full_prepare_stmts( dbase_full, errmsg );
    if ( rc )
    {
        dbase_destroy( (DBase*) dbase_full );
        ERROR( "dbase_full_prepare_stmts" )
    }

    sqlite3_update_hook( dbase_full->dbase.db, (void*) project_set_changed, (gpointer) zond );

    *dbase_zond = g_malloc0( sizeof( DBaseZond ) );

    (*dbase_zond)->dbase_store = dbase;
    (*dbase_zond)->dbase_work = dbase_full;
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
    dbase_destroy( (DBase*) (*dbase_zond)->dbase_work );
    dbase_destroy( (*dbase_zond)->dbase_store );

    g_free( (*dbase_zond)->project_dir );
    g_free( (*dbase_zond)->project_name );

    g_free( *dbase_zond );

    *dbase_zond = NULL;

    return;
}


static gint
project_speichern( Projekt* zond, gchar** errmsg )
{
    gint rc = 0;

    if ( !(zond->dbase_zond->changed) ) return 0;

    rc = project_backup( zond->dbase_zond->dbase_work->dbase.db,
            zond->dbase_zond->dbase_store->db,errmsg );
    if ( rc ) ERROR( "project_backup" )

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
        meldung( zond->app_window, "Fehler beim Speichern -\n\nBei Aufruf "
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
            if ( rc ) ERROR( "projekt_speichern" )
        }
        else if ( rc != GTK_RESPONSE_NO) return 1;
    }

    for ( gint i = 0; i < zond->arr_pv->len; i++ )
            viewer_schliessen( g_ptr_array_index( zond->arr_pv, i ) );

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
    gtk_tree_store_clear( GTK_TREE_STORE(gtk_tree_view_get_model(
            zond->treeview[BAUM_INHALT] )) );
    gtk_tree_store_clear( GTK_TREE_STORE(gtk_tree_view_get_model(
            zond->treeview[BAUM_AUSWERTUNG] )) );

    //Wieder anschalten
    g_signal_handler_unblock( zond->treeview[BAUM_INHALT],
            zond->treeview_focus_in_signal[BAUM_INHALT] );
    g_signal_handler_unblock( zond->treeview[BAUM_AUSWERTUNG],
            zond->treeview_focus_in_signal[BAUM_AUSWERTUNG] );

    //muß vor project_destroy..., weil callback ausgelöst wird, der db_get_node_id... aufruft
    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(zond->fs_button), FALSE );

    gchar* working_copy = g_strconcat( zond->dbase_zond->project_dir, "/",
            zond->dbase_zond->project_name, ".tmp", NULL );

    project_clear_dbase_zond( &(zond->dbase_zond) );

    //legacy...
    g_free( zond->dbase );
    zond->dbase = NULL;

    gint res = g_remove( working_copy );
    if ( res == -1 ) meldung( zond->app_window, "Fehler beim Löschen der "
            "temporären Datenbank:\n", strerror( errno ), NULL );
    g_free( working_copy );

    gtk_header_bar_set_title(
            GTK_HEADER_BAR(gtk_window_get_titlebar(
            GTK_WINDOW(zond->app_window) )), "" );

    //project in settings auf leeren String setzen
    g_settings_set_string( zond->settings, "project", "" );

    return 0;
}


void
cb_menu_datei_schliessen_activate( GtkMenuItem* item, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    Projekt* zond = (Projekt*) user_data;

    rc = projekt_schliessen( zond, &errmsg );
    if ( rc )
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

    rc = project_create_dbase_zond( zond, abs_path, FALSE, &dbase_zond, errmsg );
    if ( rc == -1 ) ERROR( "project_create_dbase_zond" )
    else if ( rc == 1 ) return 1;

    rc = projekt_schliessen( zond, errmsg );
    if ( rc )
    {
        project_clear_dbase_zond( &dbase_zond );
        if ( rc == -1 ) ERROR( "project_schliessen" )
        else return 1;
    }

    zond->dbase_zond = dbase_zond;

    projekt_aktivieren( zond );

    //legacy...
    Database* dbase = g_malloc0( sizeof( Database ) );
    dbase->db_store = zond->dbase_zond->dbase_store->db;
    dbase->db = zond->dbase_zond->dbase_work->dbase.db;
    rc = project_db_create_stmts( dbase, errmsg );
    if ( rc ) ERROR( "project_db_create_stmts" )

    zond->dbase = dbase;
    zond->db = zond->dbase->db;
    zond->db_store = zond->dbase->db_store;

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
        meldung( zond->app_window, "Fehler beim Öffnen-\n\nBei Aufruf "
                "project_oeffnen:\n", errmsg, NULL );
        g_free( errmsg );

        return;
    }
    else if ( rc == 1 ) return;

    rc = db_baum_refresh( zond, &errmsg );
    if ( rc == -1 )
    {
        meldung( zond->app_window, "Fehler beim Öffnen des Projekts:\nBei "
                "Aufruf db_baum_refresh:\n", errmsg, NULL );
        g_free( errmsg );
    }

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
            "Projekt anlegen" );
    if ( !abs_path ) return;

    rc = project_oeffnen( zond, abs_path, TRUE, &errmsg );
    g_free( abs_path );
    if ( rc == -1 )
    {
        meldung( zond->app_window, "Fehler beim Öffnen-\n\nBei Aufruf "
                "project_oeffnen:\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


