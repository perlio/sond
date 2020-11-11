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

#include "../../misc.h"

#include "../global_types.h"
#include "../enums.h"
#include "../error.h"

#include "../99conv/db_read.h"
#include "../99conv/general.h"
#include "../20allgemein/ziele.h"
#include "../99conv/baum.h"
#include "../99conv/db_write.h"
#include "../99conv/db_zu_baum.h"

#include <gtk/gtk.h>
#include <sqlite3.h>

//Prototype
void cb_cursor_changed( GtkTreeView*, gpointer );


typedef struct _Node
{
    Baum baum;
    gint node_id;
} Node;


static gint
suchen_fuellen_row( Projekt* zond, GtkWidget* list_box, Baum baum, gint node_id,
        gchar** errmsg )
{
    gint rc = 0;
    gchar* icon_name = NULL;
    gchar* node_text = NULL;

    rc = db_get_icon_name_and_node_text( zond, baum, node_id,
            &icon_name, &node_text, errmsg );
    if ( rc ) ERROR_PAO( "db_get_icon_id_and_node_text" )

    //Beschriftung
    gchar* text_label = NULL;
    GtkWidget* label_anbindung = NULL;

    if ( baum == BAUM_INHALT )
    {
        gchar* rel_path = NULL;
        Anbindung* anbindung = NULL;

        rc = abfragen_rel_path_and_anbindung( zond, baum, node_id, &rel_path,
                &anbindung, errmsg );
        if ( rc == -1 )
        {
            g_free( icon_name );
            g_free( node_text );
            ERROR_PAO( "abfragen_rel_path_and_anbindung" )
        }

        text_label = g_strdup( rel_path );
        if ( anbindung ) text_label = add_string( text_label,
                g_strdup_printf( ", S. %i - %i)", anbindung->von.seite,
                anbindung->bis.seite ) );

        g_free( anbindung );
        g_free( rel_path );
    }

    GtkWidget* hbox = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 0 );

    if ( baum == BAUM_AUSWERTUNG )
    {
        GtkWidget* fill = gtk_image_new_from_icon_name( "go-next", GTK_ICON_SIZE_BUTTON );
        gtk_box_pack_start( GTK_BOX(hbox), fill, FALSE, FALSE, 4 );
    }

    GtkWidget* icon = gtk_image_new_from_icon_name( icon_name, GTK_ICON_SIZE_BUTTON );
    g_free( icon_name );
    gtk_box_pack_start( GTK_BOX(hbox), icon, FALSE, FALSE, 0 );
    GtkWidget* label = gtk_label_new( (const gchar*) node_text );
    g_free( node_text );
    gtk_box_pack_start( GTK_BOX(hbox), label, FALSE, FALSE, 0 );

    if ( text_label )
    {
        label_anbindung = gtk_label_new( text_label );
        g_free( text_label );
        gtk_box_pack_end( GTK_BOX(hbox), label_anbindung, FALSE, FALSE, 0 );
    }

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

    list_box = g_object_get_data( G_OBJECT(ergebnisfenster), "list-box" );

    for ( gint i = 0; i < arr_treffer->len; i++ )
    {
        node = g_array_index( arr_treffer, Node, i );
        rc = suchen_fuellen_row( zond, list_box, node.baum, node.node_id, errmsg );
        if ( rc ) ERROR_PAO( "suchen_fuellen_row" )
    }

    gtk_widget_show_all( list_box );

    return 0;
}


static void
cb_suchen_nach_auswertung( GtkMenuItem* item, gpointer user_data )
{
    GList* selected = NULL;
    GList* list = NULL;

    Projekt* zond =(Projekt*) user_data;
    GtkWidget* list_box = g_object_get_data( G_OBJECT(item), "list-box" );
    gboolean child = (gboolean) GPOINTER_TO_INT(g_object_get_data(
            G_OBJECT(item), "child" ));

    selected = gtk_list_box_get_selected_rows( GTK_LIST_BOX(list_box) );

    if ( !selected )
    {
        meldung( zond->app_window, "Kopieren nicht möglich - keine Punkte "
                "ausgewählt", NULL );

        return;
    }

    //aktuellen cursor im BAUM_AUSWERTUNG: node_id und iter abfragen
    gint anchor_id = baum_abfragen_aktuelle_node_id( zond->treeview[BAUM_AUSWERTUNG] );

    gint rc = 0;
    gint node_id = 0;
    Baum baum = KEIN_BAUM;
    gint new_node_id = 0;
    gchar* errmsg = NULL;

    list = selected;
    do
    {
        baum = (Baum) GPOINTER_TO_INT(g_object_get_data( G_OBJECT(list->data), "baum" ));
        node_id = GPOINTER_TO_INT(g_object_get_data( G_OBJECT(list->data), "node-id" ));

        rc = db_begin( zond, &errmsg );
        if ( rc )
        {
            meldung( zond->app_window, "Fehler in Suchen/Kopieren in Auswertung -\n\n"
                    "Bei Aufruf db_begin:\n", errmsg, NULL );
            g_free( errmsg );

            return;
        }

        new_node_id = db_kopieren_nach_auswertung( zond, baum, node_id,
                anchor_id, child, &errmsg );
        if ( new_node_id == -1 )
        {
            meldung( zond->app_window, "Fehler in Suchen/Kopieren in Auswertung -\n\n"
                    "Bei Aufruf db_kopieren_nach_auswertung:\n", errmsg, NULL );
            g_free( errmsg );

            return;
        }

        rc = db_commit( zond, &errmsg );
        if ( rc )
        {
            meldung( zond->app_window, "Fehler in kopieren nach BAUM_AUSWERTUNG:\n\n"
                        "Bei Aufruf db_commit:\n", errmsg, NULL );

            return;
        }

        GtkTreeIter* iter = baum_abfragen_iter( zond->treeview[BAUM_AUSWERTUNG],
                anchor_id );

        GtkTreeIter* new_iter = db_baum_knoten( zond, BAUM_AUSWERTUNG,
                new_node_id, iter, child, &errmsg );
        if ( iter ) gtk_tree_iter_free( iter );
        if ( !new_iter )
        {
            meldung( zond->app_window, "Fehler in kopieren nach BAUM_AUSWERTUNG:\n\n"
                    "Bei Aufruf db_baum_knoten:\n", errmsg, NULL );
            g_free( errmsg );

            return;
        }

        expand_row( zond, BAUM_AUSWERTUNG, new_iter );
        baum_setzen_cursor( zond, BAUM_AUSWERTUNG, new_iter );

        gtk_tree_iter_free( new_iter );

        anchor_id = new_node_id;
        child = FALSE;
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

    GtkTreePath* path = baum_abfragen_path( zond->treeview[baum], node_id );
    gtk_tree_view_expand_to_path( zond->treeview[baum], path );

    //kurz Signal verbinden, damit label und textview angezeigt werden
    gulong signal = g_signal_connect( zond->treeview[baum], "cursor-changed",
            G_CALLBACK(cb_cursor_changed), zond );
    gtk_tree_view_set_cursor( zond->treeview[baum], path, NULL, FALSE );
    g_signal_handler_disconnect( zond->treeview[baum], signal );

    gtk_tree_path_free( path );

    return;
}


static GtkWidget*
suchen_erzeugen_ergebnisfenster( Projekt* zond, const gchar* titel )
{
    //Fenster erzeugen
    GtkWidget* window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_default_size( GTK_WINDOW(window), 1000, 400 );
    gtk_window_set_transient_for( GTK_WINDOW(window), GTK_WINDOW(zond->app_window) );

    GtkWidget* swindow = gtk_scrolled_window_new( NULL, NULL );
    GtkWidget* list_box = gtk_list_box_new( );
    gtk_list_box_set_selection_mode( GTK_LIST_BOX(list_box),
            GTK_SELECTION_MULTIPLE );
    gtk_list_box_set_activate_on_single_click( GTK_LIST_BOX(list_box), FALSE );

    g_object_set_data( G_OBJECT(window), "list-box", list_box );

    gtk_container_add( GTK_CONTAINER(swindow), list_box );
    gtk_container_add( GTK_CONTAINER(window), swindow );

    //Headerbar erzeugen
    GtkWidget* headerbar = gtk_header_bar_new( );
    gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(headerbar), ":minimize,close");
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
    gtk_header_bar_set_title( GTK_HEADER_BAR(headerbar), titel );

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

    g_object_set_data( G_OBJECT(gleiche_ebene), "list-box", list_box );
    g_object_set_data( G_OBJECT(unterpunkt), "list-box", list_box );
    g_object_set_data( G_OBJECT(unterpunkt), "child", GINT_TO_POINTER(1) );

    //menu sichtbar machen
    gtk_widget_show_all( suchen_menu );

    //einfügen
    gtk_menu_button_set_popup( GTK_MENU_BUTTON(suchen_menu_button), suchen_menu );
    gtk_header_bar_pack_start( GTK_HEADER_BAR(headerbar), suchen_menu_button );
    gtk_window_set_titlebar( GTK_WINDOW(window), headerbar );

    gtk_widget_show_all( window );

    g_signal_connect( list_box, "row-activated", G_CALLBACK(cb_lb_row_activated),
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
    if ( rc ) ERROR_PAO( "suchen_fuellen_ergebnisfenster" )

    return 0;
}


static gint
suchen_abfragen_db_path( Projekt* zond, const gchar* suchtext,
        GArray* arr_treffer, gchar** errmsg )
{
    gint rc = 0;
    Node node = { 0 };
    sqlite3_stmt* stmt = NULL;

    rc = sqlite3_prepare_v2( zond->db,
            "SELECT node_id FROM dateien WHERE LOWER(rel_path) LIKE LOWER(?)",
            -1, &stmt, NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2" )

    rc = sqlite3_bind_text( stmt, 1, suchtext, -1, NULL );
    if ( rc != SQLITE_OK )
    {
        sqlite3_finalize( stmt );
        ERROR_SQL( "sqlite3_bind_text" )
    }

    node.baum = BAUM_INHALT;

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
            node.node_id = sqlite3_column_int( stmt, 0 );
            g_array_append_val( arr_treffer, node );
        }
    } while ( rc == SQLITE_ROW );

    sqlite3_finalize( stmt );

    return 0;
}


gint
suchen_path( Projekt* zond, const gchar* suchtext, gchar** errmsg )
{
    gint rc = 0;
    GArray* arr_treffer = NULL;
    gchar* titel = NULL;

    arr_treffer = g_array_new( FALSE, FALSE, sizeof( Node ) );
    rc = suchen_abfragen_db_path( zond, suchtext, arr_treffer, errmsg );
    if ( rc )
    {
        g_array_free( arr_treffer, TRUE );
        ERROR_PAO( "suchen_abfragen_db_path" )
    }

    if ( arr_treffer->len == 0 )
    {
        g_array_free( arr_treffer, TRUE );
        return -2;
    }

    titel = g_strconcat( "In Dateinamen suchen: '", suchtext, "'", NULL );
    rc = suchen_anzeigen_ergebnisse( zond, titel, arr_treffer, errmsg );
    g_array_free( arr_treffer, TRUE );
    if ( rc ) ERROR_PAO( "suchen_anzeigen_ergebnisse" );

    return 0;
}


static gint
suchen_abfragen_db_text( Projekt* zond, const gchar* suchtext,
        GArray* arr_treffer, gchar** errmsg )
{
    gint rc = 0;
    Node node = { 0 };
    sqlite3_stmt* stmt = NULL;

    rc = sqlite3_prepare_v2( zond->db,
            "SELECT node_id FROM baum_auswertung WHERE LOWER(text) LIKE LOWER(?)",
            -1, &stmt, NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2" )

    rc = sqlite3_bind_text( stmt, 1, suchtext, -1, NULL );
    if ( rc != SQLITE_OK )
    {
        sqlite3_finalize( stmt );
        ERROR_SQL( "sqlite3_bind_text" )
    }

    node.baum = BAUM_AUSWERTUNG;

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
            node.node_id = sqlite3_column_int( stmt, 0 );
            g_array_append_val( arr_treffer, node );
        }
    } while ( rc == SQLITE_ROW );

    sqlite3_finalize( stmt );

    return 0;
}


gint
suchen_text( Projekt* zond, const gchar* suchtext, gchar** errmsg )
{
    gint rc = 0;
    GArray* arr_treffer = NULL;
    gchar* titel = NULL;

    arr_treffer = g_array_new( FALSE, FALSE, sizeof( Node ) );
    rc = suchen_abfragen_db_text( zond, suchtext, arr_treffer, errmsg );
    if ( rc )
    {
        g_array_free( arr_treffer, TRUE );
        ERROR_PAO( "suchen_abfragen_db_text" )
    }

    if ( arr_treffer->len == 0 )
    {
        g_array_free( arr_treffer, TRUE );
        return -2;
    }

    titel = g_strconcat( "In TextViews suchen: '", suchtext, "'", NULL );
    rc = suchen_anzeigen_ergebnisse( zond, titel, arr_treffer, errmsg );
    g_array_free( arr_treffer, TRUE );
    if ( rc ) ERROR_PAO( "suchen_anzeigen_ergebnisse" );

    return 0;
}


static gint
suchen_abfragen_db_node_text( Projekt* zond, const gchar* suchtext,
        GArray* arr_treffer, gchar** errmsg )
{
    gint rc = 0;
    Node node = { 0 };
    sqlite3_stmt* stmt = NULL;

    rc = sqlite3_prepare_v2( zond->db,
            "SELECT node_id FROM baum_inhalt WHERE LOWER(node_text) LIKE LOWER(?)",
            -1, &stmt, NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2" )

    rc = sqlite3_bind_text( stmt, 1, suchtext, -1, NULL );
    if ( rc != SQLITE_OK )
    {
        sqlite3_finalize( stmt );
        ERROR_SQL( "sqlite3_bind_text" )
    }

    node.baum = BAUM_INHALT;

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
            node.node_id = sqlite3_column_int( stmt, 0 );
            g_array_append_val( arr_treffer, node );
        }
    } while ( rc == SQLITE_ROW );

    sqlite3_finalize( stmt );

    rc = sqlite3_prepare_v2( zond->db,
            "SELECT node_id FROM baum_auswertung WHERE LOWER(node_text) LIKE LOWER(?)",
            -1, &stmt, NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2" )

    rc = sqlite3_bind_text( stmt, 1, suchtext, -1, NULL );
    if ( rc != SQLITE_OK )
    {
        sqlite3_finalize( stmt );
        ERROR_SQL( "sqlite3_bind_text" )
    }

    //Das gleiche für baum_auswertung
    node.baum = BAUM_AUSWERTUNG;

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
            node.node_id = sqlite3_column_int( stmt, 0 );
            g_array_append_val( arr_treffer, node );
        }
    } while ( rc == SQLITE_ROW );

    sqlite3_finalize( stmt );

    return 0;
}


gint
suchen_node_text( Projekt* zond, const gchar* suchtext, gchar** errmsg )
{
    gint rc = 0;
    GArray* arr_treffer = NULL;
    gchar* titel = NULL;

    arr_treffer = g_array_new( FALSE, FALSE, sizeof( Node ) );

    rc = suchen_abfragen_db_node_text( zond, suchtext, arr_treffer, errmsg );
    if ( rc ) ERROR_PAO( "suchen_abfragen_db_node_text" )

    if ( arr_treffer->len == 0 )
    {
        g_array_free( arr_treffer, TRUE );
        return -2;
    }

    titel = g_strconcat( "In node_text suchen: '", suchtext, "'", NULL );
    rc = suchen_anzeigen_ergebnisse( zond, titel, arr_treffer, errmsg );
    g_array_free( arr_treffer, TRUE );
    if ( rc ) ERROR_PAO( "suchen_anzeigen_ergebnisse" );

    return 0;
}
