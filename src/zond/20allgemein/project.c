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

#include "../global_types.h"
#include "../error.h"

#include "../../misc.h"
#include "../../fm.h"

#include "../99conv/general.h"
#include "../99conv/db_zu_baum.h"

#include "fs_tree.h"
#include "project_db.h"


void
project_set_changed( gpointer user_data )
{
    Projekt* zond = (Projekt*) user_data;

    zond->changed = TRUE;
    gtk_widget_set_sensitive( zond->menu.speichernitem, TRUE );
    g_settings_set_boolean( zond->settings, "speichern", TRUE );

    return;
}


void
reset_project_changed( Projekt* zond )
{
    zond->changed = FALSE;
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
    gtk_widget_set_sensitive( GTK_WIDGET(zond->menu.suchen), active );
    gtk_widget_set_sensitive( GTK_WIDGET(zond->menu.ansicht), active );
    gtk_widget_set_sensitive( GTK_WIDGET(zond->fs_button), active );
//    gtk_widget_set_sensitive( GTK_WIDGET(zond->menu.extras), TRUE );

    return;
}


gint
projekt_aktivieren( Projekt* zond, gchar* project, gboolean disc, gchar** errmsg )
{
    //Pfad aus filename entfernen
    gchar* mark = strrchr( project, '/' );
    zond->project_name = g_strdup( mark  + 1 );

    //project_dir
    zond->project_dir = g_strndup( project, strlen( project ) - strlen(
            mark ) );

    //zum Arbeitsverzeichnis machen
    g_chdir( zond->project_dir );

//Datenbankdateien öffnen
    //Ursprungsdatei
    zond->dbase = project_db_init_database( zond->project_name,
            zond->project_dir, disc, errmsg );
    if ( !zond->dbase )
    {
        g_free( zond->project_name );
        g_free( zond->project_dir );

        zond->project_name = NULL;
        zond->project_dir = NULL;

        ERROR_PAO( "project_db_init_database" )
    }

    zond->db = zond->dbase->db;
    zond->db_store = zond->dbase->db_store;

    sqlite3_update_hook( zond->dbase->db, (void*) project_set_changed, (gpointer) zond );

    projekt_set_widgets_sensitiv( zond, TRUE );

    //project_name als Titel Headerbar
    gtk_header_bar_set_title(
            GTK_HEADER_BAR(gtk_window_get_titlebar(
            GTK_WINDOW(zond->app_window) )), zond->project_name );

    //project_name in settings schreiben
    gchar* set = g_strconcat( zond->project_dir, "/", zond->project_name, NULL );
    g_settings_set_string( zond->settings, "project", set );
    g_free( set );

    reset_project_changed( zond );

    return 0;
}


void projekt_schliessen( Projekt* zond )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    if ( !zond->project_name ) return;

    //Menus aktivieren/ausgrauen
    projekt_set_widgets_sensitiv( zond, FALSE );

    //damit focus aus text_view rauskommt und changed-signal abgeschaltet wird
    gtk_widget_grab_focus( GTK_WIDGET(zond->treeview[BAUM_INHALT]) );

    //textview leeren
    GtkTextBuffer* buffer = gtk_text_view_get_buffer( zond->textview );
    gtk_text_buffer_set_text( buffer, "", -1 );

    g_object_set_data( G_OBJECT(buffer), "changed", NULL );
    g_object_set_data( G_OBJECT(zond->textview), "node-id", NULL );

    reset_project_changed( zond );

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

    //prepared statements zerstören
    rc = project_db_finish_database( zond->dbase, &errmsg );
    if ( rc )
    {
        meldung( zond->app_window, errmsg, NULL );
        g_free( errmsg );
    }

    gchar* working_copy = g_strconcat( zond->project_dir, "/",
            zond->project_name, ".tmp", NULL );
    gint res = g_remove( working_copy );
    if ( res == -1) meldung( zond->app_window, "Fehler beim Löschen der "
            "temporären Datenbank:\n", strerror( errno ), NULL );
    g_free( working_copy );

    //project-Namen zurücksetzen
    g_free( zond->project_name );
    g_free( zond->project_dir );
    zond->project_name = NULL;
    zond->project_dir = NULL;

    gtk_header_bar_set_title(
            GTK_HEADER_BAR(gtk_window_get_titlebar(
            GTK_WINDOW(zond->app_window) )), "" );

    //project in settings auf leeren String setzen
    g_settings_set_string( zond->settings, "project", "" );

    return;
}


void
cb_menu_datei_speichern_activate( GtkMenuItem* item, gpointer user_data )
{
    Projekt* zond = (Projekt*) user_data;

    if ( !(zond->changed) ) return;

    gint rc = 0;
    gchar* errmsg = NULL;

    rc = project_db_backup( zond->db, zond->db_store, &errmsg );
    if ( rc )
    {
        meldung( zond->app_window, "Fehler beim Speichern:\n Bei Aufruf "
                "db_backup\n", errmsg, NULL );
        g_free( errmsg );
    }
    else reset_project_changed( zond );

    return;
}


void
cb_menu_datei_schliessen_activate( GtkMenuItem* item, gpointer user_data )
{
    Projekt* zond = (Projekt*) user_data;

    if ( zond->changed )
    {
        gint rc = 0;
        rc = abfrage_frage( zond->app_window, "Datei schließen", "Änderungen "
                "aktuelles Projekt speichern?", NULL );

        if ( rc == GTK_RESPONSE_YES ) cb_menu_datei_speichern_activate( NULL, zond );
        else if ( rc != GTK_RESPONSE_NO) return;
    }

    projekt_schliessen( (Projekt*) zond );

    return;
}


void
cb_menu_datei_oeffnen_activate( GtkMenuItem* item, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    Projekt* zond = (Projekt*) user_data;

    //nachfragen, ob aktuelles Projekt gespeichert werden soll
    if ( zond->project_name != NULL )
    {
        rc = abfrage_frage( zond->app_window, "Datei öffnen", "Projekt wechseln?", NULL );
        if( (rc != GTK_RESPONSE_YES) ) return; //Abbrechen -> nicht öffnen
    }

    if ( zond->changed )
    {
        rc = abfrage_frage( zond->app_window, "Datei öffnen", "Änderungen "
                "aktuelles Projekt speichern?", NULL );

        if ( rc == GTK_RESPONSE_YES ) cb_menu_datei_speichern_activate( NULL, zond );
        else if ( rc != GTK_RESPONSE_NO) return;
    }

    gchar* abs_path = filename_oeffnen( GTK_WINDOW(zond->app_window) );
    if ( !abs_path ) return;

    projekt_schliessen( zond );
    rc = projekt_aktivieren( zond, abs_path, FALSE, &errmsg );
    g_free( abs_path );
    if ( rc )
    {
        meldung( zond->app_window, "Bei Aufruf projekt_aktivieren:\n",
                errmsg, NULL );
        g_free( errmsg );

        return;
    }

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

    //nachfragen, ob aktuelles Projekt gespeichert werden soll
    if ( zond->project_name != NULL )
    {
        rc = abfrage_frage( zond->app_window, "Neues Projekt", "Projekt wechseln?", NULL );
        if( rc != GTK_RESPONSE_YES ) return; //kein neues Projekt anlegen
    }

    if ( zond->changed )
    {
        rc = abfrage_frage( zond->app_window, "Neues Projekt", "Änderungen "
                "aktuelles Projekt speichern?", NULL );

        if ( rc == GTK_RESPONSE_YES ) cb_menu_datei_speichern_activate( NULL, zond );
        else if ( rc != GTK_RESPONSE_NO) return;

        reset_project_changed( zond );
    }

    gchar* abs_path = filename_speichern( GTK_WINDOW(zond->app_window),
            "Projekt anlegen" );
    if ( !abs_path ) return;

    projekt_schliessen( zond );
    rc = projekt_aktivieren( zond, abs_path, TRUE, &errmsg );
    g_free( abs_path );
    if ( rc )
    {
        meldung( zond->app_window, "Bei Aufruf projekt_aktivieren:\n",
                errmsg, NULL );
        g_free( errmsg );

        return;
    }

    reset_project_changed( zond );

    return;
}


