/*
zond (suchen.c) - Akten, Beweisstücke, Unterlagen
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
#include <sqlite3.h>

#include "../../misc.h"
#include "../zond_dbase.h"
#include "../zond_treeview.h"

#include "../global_types.h"
#include "../zond_dbase.h"
#include "../zond_treeview.h"

#include "../99conv/general.h"
#include "../20allgemein/ziele.h"

#include "project.h"


//Prototype
void zond_treeview_cursor_changed( ZondTreeview*, gpointer );


typedef enum _Zond_Suchen
{
    ZOND_SUCHEN_DATEINAME,
    ZOND_SUCHEN_NODE_TEXT_BAUM_INHALT,
    ZOND_SUCHEN_NODE_TEXT_BAUM_AUSWERTUNG,
    ZOND_SUCHEN_TEXT,
    N_ZOND_SUCHEN
} ZondSuchen;

typedef struct _Node
{
    ZondSuchen zond_suchen;
    gint node_id;
} Node;


static gint
suchen_kopieren_listenpunkt( Projekt* zond, GList* list, GtkTreeIter* iter, gint* anchor_id,
        gboolean* child, GtkTreeIter* iter_new, GError** error )
{
    gint rc = 0;
    gint node_id = 0;
    gint node_id_new = 0;

    node_id = GPOINTER_TO_INT(g_object_get_data( G_OBJECT(list->data), "node-id" ));

    rc = zond_dbase_begin( zond->dbase_zond->zond_dbase_work, error );
    if ( rc ) ERROR_Z

    rc = zond_treeview_walk_tree( ZOND_TREEVIEW(zond->treeview[BAUM_AUSWERTUNG]),
            FALSE, node_id, iter, *child, iter_new, *anchor_id, &node_id_new,
            zond_treeview_copy_node_to_baum_auswertung, error );
    if ( rc ) ERROR_ROLLBACK_Z( zond->dbase_zond->zond_dbase_work )

    rc = zond_dbase_commit( zond->dbase_zond->zond_dbase_work, error );
    if ( rc ) ERROR_ROLLBACK_Z( zond->dbase_zond->zond_dbase_work )

    sond_treeview_expand_row( zond->treeview[BAUM_AUSWERTUNG], iter_new );
    sond_treeview_set_cursor( zond->treeview[BAUM_AUSWERTUNG], iter_new );

    *anchor_id = node_id_new;
    *child = FALSE;

    return 0;
}


static void
cb_suchen_nach_auswertung( GtkMenuItem* item, gpointer user_data )
{
    GList* selected = NULL;
    GList* list = NULL;
    gint anchor_id = 0;
    GtkTreeIter iter = { 0, };
    GtkTreeIter iter_new = { 0, };

    Projekt* zond =(Projekt*) user_data;

    GtkWidget* list_box = g_object_get_data( G_OBJECT(item), "listbox" );
    gboolean child = (gboolean) GPOINTER_TO_INT(g_object_get_data(
            G_OBJECT(item), "child" ));

    selected = gtk_list_box_get_selected_rows( GTK_LIST_BOX(list_box) );

    if ( !selected )
    {
        display_message( zond->app_window, "Kopieren nicht möglich - keine Punkte "
                "ausgewählt", NULL );

        return;
    }

    //aktuellen cursor im BAUM_AUSWERTUNG: node_id und iter abfragen
    if ( !zond_treeview_get_anchor( zond, child, &iter, NULL, &anchor_id ) ) return;

    list = selected;
    do
    {
        gint rc = 0;
        GError* error = NULL;

        rc = suchen_kopieren_listenpunkt( zond, list, (anchor_id ? &iter : NULL),
                &anchor_id, &child, &iter_new, &error);
        if ( rc )
        {
            display_message( zond->app_window, "Fehler in Suchen/Kopieren in Auswertung -\n\n"
                    "Bei Aufruf suchen_kopieren_listenpunkt:\n", error->message, NULL );
            g_error_free( error );

            return;
        }

        iter = iter_new;
    } while ( (list = list->next) );

    g_list_free( selected );

    return;
}


static void
cb_lb_row_activated( GtkWidget* listbox, GtkWidget* row, gpointer user_data )
{
    Projekt* zond = (Projekt*) user_data;

    Baum baum = (Baum) GPOINTER_TO_INT(g_object_get_data( G_OBJECT(row),
            "baum" ));
    gint node_id = GPOINTER_TO_INT(g_object_get_data( G_OBJECT(row),
            "node-id" ));

    gtk_tree_selection_unselect_all( zond->selection[BAUM_INHALT] );
    gtk_tree_selection_unselect_all( zond->selection[BAUM_AUSWERTUNG] );

    GtkTreePath* path = zond_treeview_get_path( zond->treeview[baum], node_id );
    gtk_tree_view_expand_to_path( GTK_TREE_VIEW(zond->treeview[baum]), path );

    //kurz Signal verbinden, damit label und textview angezeigt werden
    gulong signal = g_signal_connect( zond->treeview[baum], "cursor-changed",
            G_CALLBACK(zond_treeview_cursor_changed), zond );
    gtk_tree_view_set_cursor( GTK_TREE_VIEW(zond->treeview[baum]), path, NULL, FALSE );
    g_signal_handler_disconnect( zond->treeview[baum], signal );

    gtk_tree_path_free( path );

    return;
}


static gint
suchen_fuellen_row( Projekt* zond, GtkWidget* list_box, ZondSuchen zond_suchen, gint node_id,
        gchar** errmsg )
{
    gint rc = 0;
    Baum baum = KEIN_BAUM;
    //Beschriftung
    gchar* text_label = NULL;
    GtkWidget* hbox = NULL;

    if ( zond_suchen == ZOND_SUCHEN_DATEINAME ||
            zond_suchen == ZOND_SUCHEN_NODE_TEXT_BAUM_INHALT )
            baum = BAUM_INHALT;
    else baum = BAUM_AUSWERTUNG;

    hbox = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 0 );

    if ( zond_suchen == ZOND_SUCHEN_DATEINAME )
    {
        gint rc = 0;
        gchar* rel_path = NULL;

//        rc = treeviews_get_rel_path_and_anbindung( zond, baum, node_id, &rel_path,
//                NULL, errmsg );
        if ( rc == -1 ) ERROR_S

        text_label = add_string( g_strdup( "Dateiname: "), rel_path );
    }
    else if ( zond_suchen == ZOND_SUCHEN_NODE_TEXT_BAUM_INHALT ||
            zond_suchen == ZOND_SUCHEN_NODE_TEXT_BAUM_AUSWERTUNG )
    {
        gchar* node_text = NULL;
        GError* error = NULL;

        rc = zond_dbase_get_node( zond->dbase_zond->zond_dbase_work, node_id,
                NULL, NULL, NULL, NULL, NULL, &node_text, NULL, &error );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
            g_error_free( error );

            return -1;
        }

        if ( zond_suchen == ZOND_SUCHEN_NODE_TEXT_BAUM_INHALT )
                text_label = add_string( g_strdup( "BAUM_INHALT: " ), node_text );
        else text_label = add_string( g_strdup( "BAUM_AUSWERTUNG: " ), node_text );
    }
    else text_label = g_strdup( "TextView" ); // == ZOND_SUCHEN_TEXT

    GtkWidget* label = gtk_label_new( (const gchar*) text_label );
    g_free( text_label );
    gtk_box_pack_start( GTK_BOX(hbox), label, FALSE, FALSE, 0 );

    gtk_list_box_insert( GTK_LIST_BOX(list_box), hbox, -1 );
    GtkWidget* list_box_row = gtk_widget_get_parent( hbox );

    g_object_set_data( G_OBJECT(list_box_row), "baum", GINT_TO_POINTER(baum) );
    g_object_set_data( G_OBJECT(list_box_row), "node-id", GINT_TO_POINTER(node_id) );

    return 0;
}


static gint
suchen_fuellen_ergebnisfenster( Projekt* zond, GtkWidget* ergebnisfenster,
        GArray* arr_treffer, gchar** errmsg )
{
    gint rc = 0;
    GtkWidget* list_box = NULL;
    Node node = { 0 };

    list_box = g_object_get_data( G_OBJECT(ergebnisfenster), "listbox" );

    for ( gint i = 0; i < arr_treffer->len; i++ )
    {
        node = g_array_index( arr_treffer, Node, i );
        rc = suchen_fuellen_row( zond, list_box, node.zond_suchen, node.node_id, errmsg );
        if ( rc ) ERROR_S
    }

    gtk_widget_show_all( list_box );

    return 0;
}


static GtkWidget*
suchen_erzeugen_ergebnisfenster( Projekt* zond, const gchar* titel )
{
    GtkWidget* window = NULL;
    GtkWidget* listbox = NULL;
    GtkWidget* headerbar = NULL;

    //Fenster erzeugen
    window = result_listbox_new( GTK_WINDOW(zond->app_window), titel, GTK_SELECTION_MULTIPLE );

    //Menu Button
    GtkWidget* suchen_menu_button = gtk_menu_button_new( );

    //Menu erzeugen
    GtkWidget* suchen_menu = gtk_menu_new( );

    //Items erzeugen
    GtkWidget* suchen_nach_auswertung =
            gtk_menu_item_new_with_label( "In Baum Auswertung kopieren" );

    //Füllen
    gtk_menu_shell_append( GTK_MENU_SHELL(suchen_menu), suchen_nach_auswertung );

    //Untermenu
    GtkWidget* suchen_nach_auswertung_ebene = gtk_menu_new( );

    GtkWidget* gleiche_ebene = gtk_menu_item_new_with_label( "Gleiche Ebene" );
    GtkWidget* unterpunkt = gtk_menu_item_new_with_label( "Unterpunkt" );

    //Füllen
    gtk_menu_shell_append( GTK_MENU_SHELL(suchen_nach_auswertung_ebene),
            gleiche_ebene );
    gtk_menu_shell_append( GTK_MENU_SHELL(suchen_nach_auswertung_ebene),
            unterpunkt );

    gtk_menu_item_set_submenu( GTK_MENU_ITEM(suchen_nach_auswertung),
            suchen_nach_auswertung_ebene );

    //menu sichtbar machen
    gtk_widget_show_all( suchen_menu );

    listbox = (GtkWidget*) g_object_get_data( G_OBJECT(window), "listbox" );
    g_object_set_data( G_OBJECT(gleiche_ebene), "listbox", listbox );
    g_object_set_data( G_OBJECT(unterpunkt), "listbox", listbox );
    g_object_set_data( G_OBJECT(unterpunkt), "child", GINT_TO_POINTER(1) );

    //einfügen
    headerbar = (GtkWidget*) g_object_get_data( G_OBJECT(window), "headerbar" );
    gtk_menu_button_set_popup( GTK_MENU_BUTTON(suchen_menu_button), suchen_menu );
    gtk_header_bar_pack_start( GTK_HEADER_BAR(headerbar), suchen_menu_button );

    gtk_widget_show_all( window );

    g_signal_connect( listbox, "row-activated", G_CALLBACK(cb_lb_row_activated),
            (gpointer) zond );
    g_signal_connect( gleiche_ebene, "activate",
            G_CALLBACK(cb_suchen_nach_auswertung), (gpointer) zond );
    g_signal_connect( unterpunkt, "activate",
            G_CALLBACK(cb_suchen_nach_auswertung), (gpointer) zond );

    return window;
}


static gint
suchen_anzeigen_ergebnisse( Projekt* zond, const gchar* titel, GArray* arr_treffer,
        gchar** errmsg )
{
    gint rc = 0;
    GtkWidget* ergebnisfenster = 0;

    ergebnisfenster = suchen_erzeugen_ergebnisfenster( zond, titel );

    rc = suchen_fuellen_ergebnisfenster( zond, ergebnisfenster, arr_treffer,
            errmsg );
    if ( rc ) ERROR_S

    return 0;
}


#define ERROR_SQL(x) { if ( errmsg ) *errmsg = add_string( g_strconcat( "Bei Aufruf " x ":\n", \
                       sqlite3_errmsg(zond_dbase_get_dbase( zond->dbase_zond->zond_dbase_work ) ), NULL ), *errmsg ); \
                       return -1; }

static gint
suchen_db( Projekt* zond, const gchar* text, GArray* arr_treffer, gchar** errmsg )
{
    gint rc = 0;
    Node node = { 0, };
    sqlite3_stmt* stmt = NULL;

    rc = sqlite3_prepare_v2( zond_dbase_get_dbase( zond->dbase_zond->zond_dbase_work ),
            "SELECT ?2, node_id FROM dateien WHERE LOWER(rel_path) LIKE LOWER(?1) "
            "UNION "
            "SELECT ?3, node_id FROM baum_inhalt WHERE LOWER(node_text) LIKE LOWER(?1) "
            "UNION "
            "SELECT ?4, node_id FROM baum_auswertung WHERE LOWER(node_text) LIKE LOWER(?1) "
            "UNION "
            "SELECT ?5, node_id FROM baum_auswertung WHERE LOWER(text) LIKE LOWER(?1) ",
            -1, &stmt, NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2" )

    rc = sqlite3_bind_text( stmt, 1, text, -1, NULL );
    if ( rc != SQLITE_OK )
    {
        sqlite3_finalize( stmt );
        ERROR_SQL( "sqlite3_bind_text" )
    }

    rc = sqlite3_bind_int( stmt, 2, ZOND_SUCHEN_DATEINAME );
    if ( rc != SQLITE_OK )
    {
        sqlite3_finalize( stmt );
        ERROR_SQL( "sqlite3_bind_int" )
    }

    rc = sqlite3_bind_int( stmt, 3, ZOND_SUCHEN_NODE_TEXT_BAUM_INHALT );
    if ( rc != SQLITE_OK )
    {
        sqlite3_finalize( stmt );
        ERROR_SQL( "sqlite3_bind_int" )
    }

    rc = sqlite3_bind_int( stmt, 4, ZOND_SUCHEN_NODE_TEXT_BAUM_AUSWERTUNG );
    if ( rc != SQLITE_OK )
    {
        sqlite3_finalize( stmt );
        ERROR_SQL( "sqlite3_bind_int" )
    }

    rc = sqlite3_bind_int( stmt, 5, ZOND_SUCHEN_TEXT );
    if ( rc != SQLITE_OK )
    {
        sqlite3_finalize( stmt );
        ERROR_SQL( "sqlite3_bind_int" )
    }

    do
    {
        rc = sqlite3_step( stmt );
        if ( (rc != SQLITE_ROW) && rc != SQLITE_DONE )
        {
            sqlite3_finalize( stmt );
            ERROR_SQL( "sqlite3_step" )
        }
        else if ( rc == SQLITE_ROW )
        {
            node.zond_suchen = sqlite3_column_int( stmt, 0 );
            node.node_id = sqlite3_column_int( stmt, 1 );
            g_array_append_val( arr_treffer, node );
        }
    } while ( rc == SQLITE_ROW );

    sqlite3_finalize( stmt );

    return 0;
}


gint
suchen_treeviews( Projekt* zond, const gchar* text, gchar** errmsg )
{
    gint rc = 0;
    GArray* arr_treffer = NULL;
    gchar* titel = NULL;

    arr_treffer = g_array_new( FALSE, FALSE, sizeof( Node ) );

    rc = suchen_db( zond, text, arr_treffer, errmsg );
    if ( rc )
    {
        g_array_unref( arr_treffer );
        ERROR_S
    }

    if ( arr_treffer->len )
    {
        titel = g_strconcat( "Suche nach: '", text, "'", NULL );
        rc = suchen_anzeigen_ergebnisse( zond, titel, arr_treffer, errmsg );
    }
    g_array_unref( arr_treffer );
    if ( rc ) ERROR_S

    return 0;
}
