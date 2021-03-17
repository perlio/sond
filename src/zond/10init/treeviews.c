/*
zond (treeviews.c) - Akten, Beweisstücke, Unterlagen
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

#include <gtk/gtk.h>
#include <mupdf/fitz.h>

#include "../../sond_treeview.h"
#include "../../sond_treeviewfm.h"

#include "../global_types.h"
#include "../error.h"

#include "../99conv/baum.h"
#include "../99conv/general.h"
#include "../99conv/db_read.h"
#include "../99conv/db_write.h"
#include "../99conv/mupdf.h"

#include "../20allgemein/oeffnen.h"
#include "../20allgemein/ziele.h"
#include "../20allgemein/project.h"
#include "../20allgemein/dbase_full.h"

#include "../40viewer/viewer.h"



static void
cb_row_activated( SondTreeview* tv, GtkTreePath* tp, GtkTreeViewColumn* tvc,
        gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    Projekt* zond = (Projekt*) user_data;
    Baum baum = baum_get_baum_from_treeview( zond, GTK_WIDGET(tv) );

    //aktuellen Knoten abfragen
    gint node_id = baum_abfragen_aktuelle_node_id( tv );
    if ( !node_id )
    {
        meldung( zond->app_window, "Kein Punkt ausgewählt", NULL );

        return;
    }

    rc = oeffnen_node( zond, baum, node_id, &errmsg );
    if ( rc )
    {
        meldung( zond->app_window, "Fehler - Öffnen\n\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


void
cb_cursor_changed( SondTreeview* treeview, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    Projekt* zond = (Projekt*) user_data;

    gint node_id = baum_abfragen_aktuelle_node_id( treeview );
    if ( node_id == 0 ) //letzter Knoten wird gelöscht
    {
        gtk_widget_set_sensitive( GTK_WIDGET(zond->textview), FALSE );
        gtk_text_buffer_set_text( gtk_text_view_get_buffer( zond->textview ), "", -1 );

        return;
    }

    Baum baum = baum_get_baum_from_treeview( zond, GTK_WIDGET(treeview) );

//status_label setzen
    gchar* rel_path = NULL;
    Anbindung* anbindung = NULL;
    gchar* text_label = NULL;
    gchar* text = NULL;

    rc = abfragen_rel_path_and_anbindung( zond, baum, node_id, &rel_path,
            &anbindung, &errmsg );
    if ( rc == -1 )
    {
        meldung( zond->app_window, "Fehler in cb_cursor_changed:\n\n"
                "Bei Aufruf db_get_rel_path:\n", errmsg, NULL );
        g_free( errmsg );

        return;
    }

    if ( rc == 2 ) text_label = g_strdup( "" );
    else if ( rc == 1 ) text_label = g_strdup( rel_path );
    else if ( rc == 0 )
    {
        text_label = g_strdup_printf( "%s, von Seite %i, "
                "Index %i, bis Seite %i, index %i", rel_path,
                anbindung->von.seite + 1, anbindung->von.index, anbindung->bis.seite + 1,
                anbindung->bis.index );
        g_free( anbindung );
    }

    gtk_label_set_text( zond->label_status, text_label );
    g_free( rel_path );

    if ( baum == BAUM_INHALT ) return;

    //TextBuffer laden
    GtkTextBuffer* buffer = gtk_text_view_get_buffer( zond->textview );

    //neuen text einfügen
    rc = db_get_text( zond, node_id, &text, &errmsg );
    if ( rc )
    {
        meldung( zond->app_window, "Fehler in cb_cursor_changed:\n\nBei Aufruf "
                "db_get_text\n", errmsg, NULL );
        g_free( errmsg );

        return;
    }

    if ( text )
    {
        gtk_text_buffer_set_text( buffer, text, -1 );
        g_free( text );
    }
    else gtk_text_buffer_set_text( buffer, "", -1 );

    g_object_set_data( G_OBJECT(gtk_text_view_get_buffer( zond->textview )),
            "changed", NULL );
    g_object_set_data( G_OBJECT(zond->textview),
            "node-id", GINT_TO_POINTER(node_id) );

    return;
}


gboolean
cb_focus_out( GtkWidget* treeview, GdkEvent* event, gpointer user_data )
{
    Projekt* zond = (Projekt*) user_data;

    Baum baum = baum_get_baum_from_treeview( zond, treeview );

    //cursor-changed-signal ausschalten
    if ( baum != BAUM_FS && zond->cursor_changed_signal )
    {
        g_signal_handler_disconnect( treeview, zond->cursor_changed_signal );
        zond->cursor_changed_signal = 0;
    }

    g_object_set( sond_treeview_get_cell_renderer_text( zond->treeview[baum] ),
            "editable", FALSE, NULL);

    zond->last_baum = baum;

    return FALSE;
}


gboolean
cb_focus_in( GtkWidget* treeview, GdkEvent* event, gpointer user_data )
{
    Projekt* zond = (Projekt*) user_data;

    Baum baum = baum_get_baum_from_treeview( zond, treeview );

    //cursor-changed-signal für den aktivierten treeview anschalten
    if ( baum != BAUM_FS ) zond->cursor_changed_signal =
            g_signal_connect( treeview, "cursor-changed",
            G_CALLBACK(cb_cursor_changed), zond );

    if ( baum != zond->last_baum )
    {
        //selection in "altem" treeview löschen
        gtk_tree_selection_unselect_all( zond->selection[zond->last_baum] );

        //Cursor gewählter treeview selektieren
        GtkTreePath* path = NULL;
        GtkTreeViewColumn* focus_column = NULL;
        gtk_tree_view_get_cursor( GTK_TREE_VIEW(treeview), &path, &focus_column );
        if ( path )
        {
            gtk_tree_view_set_cursor( GTK_TREE_VIEW(treeview), path,
                    focus_column, FALSE );
            gtk_tree_path_free( path );
        }
    }

    g_object_set( sond_treeview_get_cell_renderer_text( zond->treeview[baum] ),
            "editable", TRUE, NULL);

    if ( baum != BAUM_AUSWERTUNG )
            gtk_widget_set_sensitive( GTK_WIDGET(zond->textview), FALSE );
    else gtk_widget_set_sensitive( GTK_WIDGET(zond->textview), TRUE );

    return FALSE;
}


static void
treeviews_cb_cell_edited( GtkCellRenderer* cell, gchar* path_string, gchar* new_text,
        gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    Baum baum = KEIN_BAUM;

    SondTreeview* stv = (SondTreeview*) user_data;
    Projekt* zond = g_object_get_data( G_OBJECT(stv), "zond" );

    baum = baum_get_baum_from_treeview( zond, GTK_WIDGET(stv) );
    GtkTreeModel* model = gtk_tree_view_get_model( GTK_TREE_VIEW(stv) );

    GtkTreeIter iter;
    gtk_tree_model_get_iter_from_string( model, &iter, path_string );

    //node_id holen, node_text in db ändern
    gint node_id = baum_get_node_id( model, &iter );

    rc = dbase_full_set_node_text( zond->dbase_zond->dbase_work, baum, node_id, new_text, &errmsg );
    if ( rc )
    {
        meldung( gtk_widget_get_toplevel( GTK_WIDGET(stv) ), "Knoten umbenennen nicht möglich\n\n"
                "Bei Aufruf dbase_full_set_node_text:\n", errmsg, NULL );
        g_free( errmsg );

        return;
    }

    gtk_tree_store_set( GTK_TREE_STORE(model), &iter, 1, new_text, -1 );
    gtk_tree_view_columns_autosize( GTK_TREE_VIEW(stv) );

    return;
}


/*
static gboolean
cb_show_popupmenu( GtkTreeView* treeview, GdkEventButton* event,
        GtkMenu* contextmenu_tv )
{
    //Rechtsklick
    if ( ((event->button) == 3) && (event->type == GDK_BUTTON_PRESS) )
    {
        GtkTreePath* path;
        gtk_tree_view_get_cursor( treeview, &path, NULL );
        if ( !path ) return FALSE;
        gtk_tree_path_free( path );

        gtk_menu_popup_at_pointer( contextmenu_tv, NULL );

        return TRUE;
    }

    return FALSE;
}
*/


static void
treeviews_render_node_text( SondTreeview* stv, GtkTreeIter* iter, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    Projekt* zond = (Projekt*) data;

    Baum baum = baum_get_baum_from_treeview( zond, GTK_WIDGET(stv) );

    if ( baum == BAUM_AUSWERTUNG )
    {
        gchar* text = NULL;

        //Hintergrund icon rot wenn Text in textview
        gint node_id = baum_get_node_id( gtk_tree_view_get_model( GTK_TREE_VIEW(stv) ), iter );

        rc = db_get_text( zond, node_id, &text, &errmsg );
        if ( rc )
        {
            meldung( zond->app_window, "Warnung -\n\nBei Aufruf db_get_text:\n",
                    errmsg, NULL );
            g_free( errmsg );
        }
        else if ( !text || !g_strcmp0( text, "" ) )
                g_object_set( G_OBJECT(sond_treeview_get_cell_renderer_text( stv )),
                "background-set", FALSE, NULL );
        else g_object_set( G_OBJECT(sond_treeview_get_cell_renderer_text( stv )),
                "background-set", TRUE, NULL );

        g_free( text );
    }

    return;
}


void
init_treeviews( Projekt* zond )
{
    for ( Baum baum = BAUM_INHALT; baum <= BAUM_AUSWERTUNG; baum++ )
    {
        //der treeview
        zond->treeview[baum] = sond_treeview_new( );

        sond_treeview_set_clipboard( zond->treeview[baum], zond->clipboard );
        sond_treeview_set_render_text_cell_func( zond->treeview[baum],
                treeviews_render_node_text, zond );

        //Tree-Model erzeugen und verbinden
        GtkTreeStore* tree_store = gtk_tree_store_new( 3, G_TYPE_STRING,
                G_TYPE_STRING, G_TYPE_INT );
        gtk_tree_view_set_model( GTK_TREE_VIEW(zond->treeview[baum]), GTK_TREE_MODEL(tree_store) );
        g_object_unref( tree_store );

        gtk_tree_view_set_headers_visible( GTK_TREE_VIEW(zond->treeview[baum]), FALSE );

        gtk_tree_view_column_set_attributes(
                sond_treeview_get_column( zond->treeview[baum] ),
                sond_treeview_get_cell_renderer_icon( zond->treeview[baum] ),
                "icon-name", 0, NULL);
        gtk_tree_view_column_set_attributes(
                sond_treeview_get_column(zond->treeview[baum] ),
                sond_treeview_get_cell_renderer_text( zond->treeview[baum] ),
                "text", 1, NULL);

                g_object_set_data( G_OBJECT(zond->treeview[baum]), "zond", zond );
        //die Selection
        zond->selection[baum] = gtk_tree_view_get_selection(
                GTK_TREE_VIEW(zond->treeview[baum]) );

/*        //Kontextmenu erzeugen, welches bei Rechtsklick auf treeview angezeigt wird
        GtkWidget* contextmenu_tv = gtk_menu_new();

        GtkWidget* datei_oeffnen_item = gtk_menu_item_new_with_label( "Öffnen" );
        gtk_menu_shell_append( GTK_MENU_SHELL(contextmenu_tv), datei_oeffnen_item );
        gtk_widget_show( datei_oeffnen_item );

        //Die Signale
        //Rechtsklick - Kontextmenu
        g_signal_connect( zond->treeview[BAUM_AUSWERTUNG], "button-press-event",
                G_CALLBACK(cb_show_popupmenu), (gpointer) contextmenu_tv );

        g_signal_connect( datei_oeffnen_item, "activate",
                G_CALLBACK(cb_datei_oeffnen), (gpointer) zond );
    */
        //Text-Spalte wird editiert
        g_signal_connect( sond_treeview_get_cell_renderer_text( zond->treeview[baum] ),
                "edited", G_CALLBACK(treeviews_cb_cell_edited), (gpointer) zond->treeview[baum] ); //Klick in textzelle = Inhalt editieren

        // Doppelklick = angebundene Datei anzeigen
        g_signal_connect( zond->treeview[baum], "row-activated",
                G_CALLBACK(cb_row_activated), (gpointer) zond );

        //Zeile expandiert oder kollabiert
        g_signal_connect( zond->treeview[baum], "row-expanded",
                G_CALLBACK(gtk_tree_view_columns_autosize), NULL );
        g_signal_connect( zond->treeview[baum], "row-collapsed",
                G_CALLBACK(gtk_tree_view_columns_autosize), NULL );

        //focus-in
        zond->treeview_focus_in_signal[baum] = g_signal_connect( zond->treeview[baum],
                "focus-in-event", G_CALLBACK(cb_focus_in), (gpointer) zond );
        g_signal_connect( zond->treeview[baum], "focus-out-event",
                G_CALLBACK(cb_focus_out), (gpointer) zond );
    }

    return;
}


void
treeviews_init_fs_tree( Projekt* zond )
{
    zond->treeview[BAUM_FS] = SOND_TREEVIEW(sond_treeviewfm_new( ));

    sond_treeview_set_clipboard( zond->treeview[BAUM_FS], zond->clipboard );
    sond_treeviewfm_set_funcs( SOND_TREEVIEWFM(zond->treeview[BAUM_FS]), project_before_move,
            project_after_move, project_test_rel_path, zond );

    //die Selection
    zond->selection[BAUM_FS] = gtk_tree_view_get_selection(
            GTK_TREE_VIEW(zond->treeview[BAUM_FS]) );

    //focus-in
    zond->treeview_focus_in_signal[BAUM_FS] = g_signal_connect( zond->treeview[BAUM_FS],
            "focus-in-event", G_CALLBACK(cb_focus_in), (gpointer) zond );
    g_signal_connect( zond->treeview[BAUM_FS], "focus-out-event",
            G_CALLBACK(cb_focus_out), (gpointer) zond );

    return;
}
