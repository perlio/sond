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

#include "../../treeview.h"
#include "../../fm.h"

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
#include "../20allgemein/fs_tree.h"

#include "../40viewer/viewer.h"



static void
cb_row_activated( GtkTreeView* tv, GtkTreePath* tp, GtkTreeViewColumn* tvc,
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
cb_cursor_changed( GtkTreeView* treeview, gpointer user_data )
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
    else if ( rc == 1 ) text_label = g_strconcat( "Datei: ", rel_path, NULL );
    else if ( rc == 0 )
    {
        text_label = g_strdup_printf( "Datei: %s, von Seite %i, "
                "Index %i, bis Seite %i, index %i", rel_path,
                anbindung->von.seite, anbindung->von.index, anbindung->bis.seite,
                anbindung->bis.index );
        g_free( anbindung );
    }

    gtk_label_set_text( zond->label_status, text_label );
    g_free( rel_path );

    if ( baum == BAUM_INHALT ) return;

    gtk_widget_set_sensitive( GTK_WIDGET(zond->textview), TRUE );

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

    g_object_set(zond->renderer_text[baum], "editable", FALSE, NULL);

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

    g_object_set(zond->renderer_text[baum], "editable", TRUE, NULL);

    if ( baum != BAUM_AUSWERTUNG )
            gtk_widget_set_sensitive( GTK_WIDGET(zond->textview), FALSE );
    else g_signal_emit_by_name( zond->treeview[baum], "cursor-changed" );

    return FALSE;
}


static void
treeviews_cb_cell_edited( GtkCellRenderer* cell, gchar* path_string, gchar* new_text,
        gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    Projekt* zond = (Projekt*) user_data;
    Baum baum = baum_get_baum_from_treeview( zond,
            g_object_get_data( G_OBJECT(cell), "tree-view" ) );

    GtkTreeModel* model = gtk_tree_view_get_model( zond->treeview[baum] );

    GtkTreeIter iter;
    gtk_tree_model_get_iter_from_string( model, &iter, path_string );

    //node_id holen, node_text in db ändern
    gint node_id = baum_get_node_id( model, &iter );

    rc = db_set_node_text( zond, baum, node_id, new_text, &errmsg );
    if ( rc )
    {
        meldung( zond->app_window, "Knoten umbenennen nicht möglich\n\n"
                "Bei Aufruf db_set_node_text:\n", errmsg, NULL );
        g_free( errmsg );

        return;
    }

    gtk_tree_store_set( GTK_TREE_STORE(model), &iter, 1, new_text, -1 );
    gtk_tree_view_columns_autosize( zond->treeview[baum] );

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


void
treeviews_render_node_text( GtkTreeViewColumn* column, GtkCellRenderer* renderer,
        GtkTreeModel* treemodel, GtkTreeIter* iter, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    GtkTreeView* tree_view = NULL;

    Projekt* zond = (Projekt*) user_data;

    tree_view = g_object_get_data( G_OBJECT(renderer), "tree-view" );
    Baum baum = baum_get_baum_from_treeview( zond, GTK_WIDGET(tree_view) );

    treeview_underline_cursor( column, renderer, iter );
    treeview_zelle_ausgrauen( tree_view, iter, renderer, zond->clipboard );

    if ( baum == BAUM_AUSWERTUNG )
    {
        gchar* text = NULL;

        //Hintergrund icon rot wenn Text in textview
        gint node_id = baum_get_node_id( gtk_tree_view_get_model( tree_view ), iter );

        rc = db_get_text( zond, node_id, &text, &errmsg );
        if ( rc )
        {
            meldung( zond->app_window, "Warnung -\n\nBei Aufruf db_get_text:\n",
                    errmsg, NULL );
            g_free( errmsg );
        }
        else if ( !text || !g_strcmp0( text, "" ) ) g_object_set( G_OBJECT(renderer),
                "background-set", FALSE, NULL );
        else g_object_set( G_OBJECT(renderer), "background-set", TRUE, NULL );

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
        zond->treeview[baum] = GTK_TREE_VIEW(gtk_tree_view_new( ));

        //Tree-Model erzeugen und verbinden
        GtkTreeStore* tree_store = gtk_tree_store_new( 3, G_TYPE_STRING,
                G_TYPE_STRING, G_TYPE_INT );
        gtk_tree_view_set_model( zond->treeview[baum], GTK_TREE_MODEL(tree_store) );
        g_object_unref( tree_store );

        gtk_tree_view_set_headers_visible( zond->treeview[baum], FALSE );
        gtk_tree_view_set_fixed_height_mode( zond->treeview[baum], TRUE );
        gtk_tree_view_set_enable_tree_lines( zond->treeview[baum], TRUE );
        gtk_tree_view_set_enable_search( zond->treeview[baum], FALSE );

        //die renderer
        GtkCellRenderer* renderer_pixbuf = gtk_cell_renderer_pixbuf_new();
        zond->renderer_text[baum] = gtk_cell_renderer_text_new();

        g_object_set(zond->renderer_text[baum], "editable", TRUE, NULL);
        g_object_set( zond->renderer_text[baum], "underline", PANGO_UNDERLINE_SINGLE, NULL );
        if ( baum == BAUM_AUSWERTUNG )
        {
            GdkRGBA gdkrgba;
            gdkrgba.alpha = 1.0;
            gdkrgba.red = 0.95;
            gdkrgba.blue = 0.95;
            gdkrgba.green = 0.95;

            g_object_set( G_OBJECT(zond->renderer_text[baum]), "background-rgba", &gdkrgba,
                    NULL );
        }

        g_object_set_data( G_OBJECT(zond->renderer_text[baum]), "tree-view",
                zond->treeview[baum] );

        //die column
        GtkTreeViewColumn* column = gtk_tree_view_column_new();
        gtk_tree_view_column_set_resizable(column, FALSE);
        gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);

        gtk_tree_view_column_set_cell_data_func( column, zond->renderer_text[baum],
                (GtkTreeCellDataFunc) treeviews_render_node_text, (gpointer) zond,
                NULL );

        gtk_tree_view_column_pack_start( column, renderer_pixbuf, FALSE );
        gtk_tree_view_column_pack_start( column, zond->renderer_text[baum], TRUE );

        gtk_tree_view_column_set_attributes(column, renderer_pixbuf, "icon-name", 0, NULL);
        gtk_tree_view_column_set_attributes(column, zond->renderer_text[baum], "text", 1, NULL);

        gtk_tree_view_append_column(zond->treeview[baum], column);

        //die Selection
        zond->selection[baum] = gtk_tree_view_get_selection(
                zond->treeview[baum] );
        gtk_tree_selection_set_mode( zond->selection[baum],
                GTK_SELECTION_MULTIPLE );
        gtk_tree_selection_set_select_function( zond->selection[baum],
                (GtkTreeSelectionFunc) treeview_selection_select_func, zond, NULL );

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
        g_signal_connect(zond->renderer_text[baum], "edited", G_CALLBACK(treeviews_cb_cell_edited),
                (gpointer) zond); //Klick in textzelle = Inhalt editieren

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

    gtk_tree_view_set_reorderable( zond->treeview[BAUM_INHALT], FALSE );
    gtk_tree_view_set_reorderable( zond->treeview[BAUM_AUSWERTUNG], FALSE );

    return;
}


void
treeviews_init_fs_tree( Projekt* zond )
{
    GtkCellRenderer* cell = NULL;

    static ModifyFile modify_file = { update_db_before_path_change,
            update_db_after_path_change, test_rel_path, NULL };

    modify_file.data = (gpointer) zond;

    zond->treeview[BAUM_FS] = fm_create_tree_view( );
    g_object_set_data( G_OBJECT(zond->treeview[BAUM_FS]), "modify-file", &modify_file );
    g_object_set_data( G_OBJECT(zond->treeview[BAUM_FS]), "clipboard", zond->clipboard );
    //die Selection
    zond->selection[BAUM_FS] = gtk_tree_view_get_selection(
            zond->treeview[BAUM_FS] );

    cell = g_object_get_data( G_OBJECT(zond->treeview[BAUM_FS]), "renderer-text" );
    zond->renderer_text[BAUM_FS] = cell;

    GdkRGBA gdkrgba;
    gdkrgba.alpha = 1.0;
    gdkrgba.red = 0.95;
    gdkrgba.blue = 0.95;
    gdkrgba.green = 0.95;

    g_object_set( cell, "background-rgba", &gdkrgba, NULL );

    //focus-in
    zond->treeview_focus_in_signal[BAUM_FS] = g_signal_connect( zond->treeview[BAUM_FS],
            "focus-in-event", G_CALLBACK(cb_focus_in), (gpointer) zond );
    g_signal_connect( zond->treeview[BAUM_FS], "focus-out-event",
            G_CALLBACK(cb_focus_out), (gpointer) zond );

    return;
}
