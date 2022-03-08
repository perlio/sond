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

#include "../zond_treeviewfm.h"
#include "../zond_treeview.h"

#include "app_window.h"

#include "../../misc.h"
#include "../../dbase.h"

#include "../global_types.h"
#include "../error.h"
#include "../zond_tree_store.h"
#include "../zond_dbase.h"

#include "../99conv/baum.h"
#include "../99conv/general.h"

#include "../20allgemein/oeffnen.h"
#include "../20allgemein/ziele.h"
#include "../20allgemein/project.h"

#include "../40viewer/viewer.h"



gint
treeviews_get_baum_and_node_id( Projekt* zond, GtkTreeIter* iter, Baum* baum,
        gint* node_id )
{
    GtkTreeIter iter_orig = { 0, };
    ZondTreeStore* tree_store = NULL;

    zond_tree_store_get_target( iter, &iter_orig );
    tree_store = zond_tree_store_get_tree_store( iter_orig.user_data );

    if ( tree_store == ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[BAUM_INHALT]) )) ) *baum = BAUM_INHALT;
    else if ( tree_store == ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[BAUM_AUSWERTUNG]) )) ) *baum = BAUM_AUSWERTUNG;
    else return 1;

    gtk_tree_model_get( GTK_TREE_MODEL(tree_store), &iter_orig, 2, node_id, -1 );

    return 0;
}


static gint
treeviews_foreach_entfernen_anbindung( SondTreeview* stv,
        GtkTreeIter* iter, gpointer data, gchar** errmsg )
{
    gint rc = 0;
    gint older_sibling = 0;
    gint parent = 0;
    gint child = 0;
    gint node_id = 0;

    Projekt* zond = data;

    gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(stv) ), iter,
            2, &node_id, -1 );

    rc = zond_dbase_get_ziel( zond->dbase_zond->zond_dbase_work, BAUM_INHALT, node_id, NULL, errmsg );
    if ( rc == -1 ) ERROR_SOND ( "zond_dbase_get_ziel" )
    if ( rc == 1 ) return 0; //Knoten ist keine Anbindung

    //herausfinden, ob zu löschender Knoten älteres Geschwister hat
    older_sibling = zond_dbase_get_older_sibling( zond->dbase_zond->zond_dbase_work, BAUM_INHALT, node_id, errmsg );
    if ( older_sibling < 0 ) ERROR_SOND( "zond_dbase_get_older_sibling" )

    //Elternknoten ermitteln
    parent = zond_dbase_get_parent( zond->dbase_zond->zond_dbase_work, BAUM_INHALT, node_id, errmsg );
    if ( parent < 0 ) ERROR_SOND( "zond_dbase_get_parent" )

    rc = dbase_begin( (DBase*) zond->dbase_zond->dbase_work, errmsg );
    if ( rc ) ERROR_SOND( "db_begin" )

    child = 0;
    while ( (child = zond_dbase_get_first_child( zond->dbase_zond->zond_dbase_work, BAUM_INHALT, node_id,
            errmsg )) )
    {
        if ( child < 0 ) ERROR_ROLLBACK( (DBase*) zond->dbase_zond->dbase_work,
                "zond_dbase_get_first_child" )

        rc = knoten_verschieben( zond, BAUM_INHALT, child, parent,
                older_sibling, errmsg );
        if ( rc == -1 ) ERROR_ROLLBACK( (DBase*) zond->dbase_zond->dbase_work,
                "knoten_verschieben" )

        older_sibling = child;
    }

    rc = zond_dbase_remove_node( zond->dbase_zond->zond_dbase_work,BAUM_INHALT, node_id, errmsg );
    if ( rc ) ERROR_ROLLBACK( (DBase*) zond->dbase_zond->dbase_work, "zond_dbase_remove_node" )

    zond_tree_store_remove( iter );

    rc = dbase_commit( (DBase*) zond->dbase_zond->dbase_work, errmsg );
    if ( rc ) ERROR_ROLLBACK( (DBase*) zond->dbase_zond->dbase_work, "db_commit" )

    return 0;
}


//Funktioniert nur im BAUM_INHALT - Abfrage im cb schließt nur BAUM_FS aus
gint
treeviews_entfernen_anbindung( Projekt* zond, Baum baum_active, gchar** errmsg )
{
    gint rc = 0;

    if ( baum_active != BAUM_INHALT ) return 0;

    rc = sond_treeview_selection_foreach( zond->treeview[BAUM_INHALT],
            treeviews_foreach_entfernen_anbindung, zond, errmsg );
    if ( rc == -1 ) ERROR_SOND( "treeview_selection_foreach" )

    return 0;
}


typedef struct _S_Selection_Loeschen
{
    Projekt* zond;
    Baum baum_active;
} SSelectionLoeschen;


static gint
treeviews_foreach_selection_loeschen( SondTreeview* tree_view, GtkTreeIter* iter,
        gpointer data, gchar** errmsg )
{
    gint rc = 0;
    gint node_id = 0;
    gint head_nr = 0;
    Baum baum = KEIN_BAUM;

    SSelectionLoeschen* s_selection = data;

    rc = treeviews_get_baum_and_node_id( s_selection->zond, iter, &baum, &node_id );
    if ( rc ) return 0;

    //Nur "nomale" Knoten oder ...
    if ( !zond_tree_store_is_link( iter ) )
    {
        gint rc = 0;
        GList* list_links = NULL;

        list_links = zond_tree_store_get_linked_nodes( iter );

        if ( list_links )
        {
            GList* ptr = NULL;

            ptr = list_links;
            do
            {
                head_nr = zond_tree_store_get_link_head_nr( ptr->data );
                if ( head_nr )
                {
                    Baum baum_link = KEIN_BAUM;
                    ZondTreeStore* tree_store = NULL;

                    tree_store = zond_tree_store_get_tree_store( ptr->data );
                    if ( tree_store == ZOND_TREE_STORE(gtk_tree_view_get_model(
                            GTK_TREE_VIEW(s_selection->zond->treeview[BAUM_INHALT]) )) )
                            baum_link = BAUM_INHALT;
                    else if ( tree_store == ZOND_TREE_STORE(gtk_tree_view_get_model(
                            GTK_TREE_VIEW(s_selection->zond->treeview[BAUM_AUSWERTUNG]) )) )
                            baum_link = BAUM_AUSWERTUNG;
                    else return 0; //???

                    rc = dbase_begin( (DBase*) s_selection->zond->dbase_zond->dbase_work, errmsg );
                    if ( rc ) ERROR_SOND( "dbase_begin" )

                    rc = zond_dbase_remove_node( s_selection->zond->dbase_zond->zond_dbase_work, baum_link, head_nr, errmsg );
                    if ( rc ) ERROR_ROLLBACK( (DBase*) s_selection->zond->dbase_zond->dbase_work, "zond_dbase_remove_node" )

                    rc = zond_dbase_remove_link( s_selection->zond->dbase_zond->zond_dbase_work, baum_link, head_nr, errmsg );
                    if ( rc ) ERROR_ROLLBACK( (DBase*) s_selection->zond->dbase_zond->dbase_work, "zond_dbase_remove_link" )

                    rc = dbase_commit( (DBase*) s_selection->zond->dbase_zond->dbase_work, errmsg );
                    if ( rc ) ERROR_ROLLBACK( (DBase*) s_selection->zond->dbase_zond->dbase_work, "dbase_commit" )
                }

            } while ( (ptr = ptr->next) );

//            g_list_free( list_links );
        }

        rc = zond_dbase_remove_node( s_selection->zond->dbase_zond->zond_dbase_work, baum, node_id, errmsg );
        if ( rc ) ERROR_SOND ( "zond_dbase_remove_node" )

        zond_tree_store_remove( iter );
    }//... Gesamt-Links
    else if ( (head_nr = zond_tree_store_get_link_head_nr( iter->user_data )) )
    {
        gint rc = 0;

        rc = zond_dbase_remove_link( s_selection->zond->dbase_zond->zond_dbase_work,
                sond_treeview_get_id( tree_view ), node_id, errmsg );
        if ( rc ) ERROR_SOND("zond_dbase_remove_link" )

        zond_tree_store_remove_link( ZOND_TREE_STORE(gtk_tree_view_get_model(
        GTK_TREE_VIEW(tree_view) )), iter );
    }
    //else: link, aber nicht head->nix machen

    return 0;
}


gint
treeviews_selection_loeschen( Projekt* zond, Baum baum_active, gchar** errmsg )
{
    gint rc = 0;

    SSelectionLoeschen s_selection = { zond, baum_active };

    rc = sond_treeview_selection_foreach( zond->treeview[baum_active],
            treeviews_foreach_selection_loeschen, &s_selection, errmsg );
    if ( rc == -1 ) ERROR_SOND( "sond_treeview_selection_foreach" )

    return 0;
}


typedef struct _S_Selection_Change_Icon
{
    Projekt* zond;
    const gchar* icon_name;
} SSelectionChangeIcon;

static gint
treeviews_foreach_change_icon_id( SondTreeview* tree_view, GtkTreeIter* iter,
        gpointer data, gchar** errmsg )
{
    gint rc = 0;
    gint node_id = 0;
    Baum baum = KEIN_BAUM;
    SSelectionChangeIcon* s_selection = NULL;

    s_selection = data;

    rc = treeviews_get_baum_and_node_id( s_selection->zond, iter, &baum, &node_id );
    if ( rc ) return 0;

    rc = zond_dbase_set_icon_name( s_selection->zond->dbase_zond->zond_dbase_work, baum, node_id, s_selection->icon_name, errmsg );
    if ( rc ) ERROR_SOND( "zond_dbase_set_icon_name" )

    //neuen icon_name im tree speichern
    zond_tree_store_set( iter, s_selection->icon_name, NULL, 0 );

    return 0;
}


gint
treeviews_change_icon_id( Projekt* zond, Baum baum_active, const gchar* icon_name, gchar** errmsg )
{
    gint rc = 0;
    SSelectionChangeIcon s_selection = { zond, icon_name };

    rc = sond_treeview_selection_foreach( zond->treeview[baum_active],
            treeviews_foreach_change_icon_id, (gpointer) &s_selection, errmsg );
    if ( rc == -1 ) ERROR_SOND( "sond_treeview_selection_foreach" )

    return 0;
}


static gint
treeviews_node_text_nach_anbindung_foreach( SondTreeview* stv, GtkTreeIter* iter,
        gpointer data, gchar** errmsg )
{
    gint rc = 0;
    Baum baum = KEIN_BAUM;
    gint node_id = 0;
    gchar* node_text = NULL;
    gchar* rel_path = NULL;
    Anbindung* anbindung = NULL;

    Projekt* zond = data;

    rc = treeviews_get_baum_and_node_id( zond, iter, &baum, &node_id );
    if ( rc ) return 0;

    rc = abfragen_rel_path_and_anbindung( zond, baum, node_id, &rel_path,
            &anbindung, errmsg );
    if ( rc == -1 ) ERROR_SOND( "abfragen_rel_path_and_anbindung" )
    if ( rc == 2 ) return 0;
    else if ( rc == 0 )
    {
        node_text = g_strdup_printf( "%s, S. %i (%i) - S. %i (%i)", rel_path,
                anbindung->von.seite, anbindung->von.index, anbindung->bis.seite,
                anbindung->bis.index );

        g_free( anbindung );
    }
    else node_text = g_strdup_printf( "%s", rel_path );

    g_free( rel_path );

    rc = zond_dbase_set_node_text( zond->dbase_zond->zond_dbase_work, baum, node_id, node_text, errmsg );
    if ( rc )
    {
        g_free( node_text );
        ERROR_SOND( "zond_dbase_set_node_text" )
    }

    //neuen text im tree speichern
    zond_tree_store_set( iter, NULL, node_text, 0 );

    g_free( node_text );

    return 0;
}

gint
treeviews_node_text_nach_anbindung( Projekt* zond, Baum baum_active, gchar** errmsg )
{
    gint rc = 0;

    rc = sond_treeview_selection_foreach( zond->treeview[baum_active],
            treeviews_node_text_nach_anbindung_foreach, zond, errmsg );
    if ( rc ) ERROR_SOND( "sond_treeview_selection_foreach" )

    return 0;
}


gint
treeviews_insert_node( Projekt* zond, Baum baum_active, gboolean child, gchar** errmsg )
{
    Baum baum = KEIN_BAUM;
    gint node_id = 0;
    gint rc = 0;
    gint node_id_new = 0;
    GtkTreeIter iter = { 0 };
    GtkTreeIter new_iter = { 0 };
    gboolean success = FALSE;
    ZondTreeStore* tree_store = NULL;

    //Knoten in baum_inhalt einfuegen
    success = sond_treeview_get_cursor( zond->treeview[baum_active], &iter );

    if ( !success )
    {
        baum = baum_active;
        child = TRUE;
    }
    else //cursor zeigt auf row#
    {
        gint head_nr = 0;

        head_nr = zond_tree_store_get_link_head_nr( iter.user_data );
        if ( head_nr )
        {
            baum = baum_active;
            node_id = head_nr;
        }
        else
        {
            gint rc = 0;

            rc = treeviews_get_baum_and_node_id( zond, &iter, &baum, &node_id );
            if ( rc == 1 ) return 1; //weder BAUM_INHALT noch BAUM_AUSWERTUNG
        }
    }

    //child = TRUE; //, weil ja node_id schon der unmittelbare Vorfahre ist!
    if ( baum == BAUM_INHALT )
    {
        gint rc = 0;

        rc = hat_vorfahre_datei( zond, baum, node_id, child, errmsg );
        if ( rc == -1 ) ERROR_SOND( "hat_vorfahre_datei" )
        else if ( rc == 1 )
        {
            display_message( zond->app_window, "Einfügen als Unterpunkt von Datei nicht zulässig", NULL );

            return 1; //Abbruch ohne Fählermeldung
        }
    }

    rc = dbase_begin( (DBase*) zond->dbase_zond->dbase_work, errmsg );
    if ( rc ) ERROR_SOND( "dbase_begin" )

    //Knoten in Datenbank einfügen
    node_id_new = zond_dbase_insert_node( zond->dbase_zond->zond_dbase_work, baum,
            node_id, child, zond->icon[ICON_NORMAL].icon_name, "Neuer Punkt", errmsg );
    if ( node_id_new == -1 ) ERROR_ROLLBACK( (DBase*) zond->dbase_zond->dbase_work, "zond_dbase_insert_node" )

    rc = dbase_commit( (DBase*) zond->dbase_zond->dbase_work, errmsg );
    if ( rc ) ERROR_ROLLBACK( (DBase*) zond->dbase_zond->dbase_work, "dbase_commit" )

    //Knoten in baum_inhalt einfuegen
    //success = sond_treeview_get_cursor( zond->treeview[baum], &iter ); - falsch!!!

    tree_store = ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[baum]) ));
    zond_tree_store_insert( tree_store, (success) ? &iter : NULL, child, &new_iter );

    if ( child && success ) sond_treeview_expand_row( zond->treeview[baum], &iter );

    //Standardinhalt setzen
    zond_tree_store_set( &new_iter, zond->icon[ICON_NORMAL].icon_name, "Neuer Punkt", node_id_new );

    sond_treeview_set_cursor( zond->treeview[baum], &new_iter );

    return 0;
}


void
cb_cursor_changed( SondTreeview* treeview, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    gint node_id = 0;
    GtkTreeIter iter = { 0, };
    Baum baum = KEIN_BAUM;
    gchar* rel_path = NULL;
    Anbindung* anbindung = NULL;
    gchar* text_label = NULL;
    gchar* text = NULL;

    Projekt* zond = (Projekt*) user_data;

    //wenn kein cursor gesetzt ist
    if ( !sond_treeview_get_cursor( treeview, &iter ) || treeviews_get_baum_and_node_id( zond, &iter, &baum, &node_id ) )
    {
        gtk_label_set_text( zond->label_status, "" ); //statur-label leeren
        gtk_text_buffer_set_text( gtk_text_view_get_buffer( zond->textview ), "", -1 );
        gtk_widget_set_sensitive( GTK_WIDGET(zond->textview), FALSE );

        return;
    }
    else if ( baum == BAUM_AUSWERTUNG ) gtk_widget_set_sensitive( GTK_WIDGET(zond->textview), TRUE );
    else gtk_widget_set_sensitive( GTK_WIDGET(zond->textview), FALSE );

    //status_label setzen
    rc = abfragen_rel_path_and_anbindung( zond, baum, node_id, &rel_path,
            &anbindung, &errmsg );
    if ( rc == -1 )
    {
        text_label = g_strconcat( "Fehler in cb_cursor_changed: Bei Aufruf "
                "abfragen_rel_path_and_anbindung:", errmsg, NULL );
        g_free( errmsg );
    }

    if ( rc == 2 ) text_label = g_strdup( "Keine Anbindung" );
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
    g_free( text_label );
    g_free( rel_path );

    if ( baum == BAUM_INHALT || rc == -1 ) return;

    //TextBuffer laden
    GtkTextBuffer* buffer = gtk_text_view_get_buffer( zond->textview );

    //neuen text einfügen
    rc = zond_dbase_get_text( zond->dbase_zond->zond_dbase_work, node_id, &text, &errmsg );
    if ( rc )
    {
        text_label = g_strconcat( "Fehler in cb_cursor_changed: Bei Aufruf "
                "zond_dbase_get_text: ", errmsg, NULL );
        g_free( errmsg );
        gtk_label_set_text( zond->label_status, text_label );
        g_free( text_label );

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

    Baum baum = (Baum) sond_treeview_get_id( SOND_TREEVIEW(treeview) );

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

    Baum baum = (Baum) sond_treeview_get_id( SOND_TREEVIEW(treeview) );

    //cursor-changed-signal für den aktivierten treeview anschalten
    if ( baum != BAUM_FS )
    {
        zond->cursor_changed_signal = g_signal_connect( treeview, "cursor-changed",
            G_CALLBACK(cb_cursor_changed), zond );

        g_signal_emit_by_name( treeview, "cursor-changed", user_data, NULL );
    }

    if ( baum != zond->last_baum )
    {
        //selection in "altem" treeview löschen
        gtk_tree_selection_unselect_all( zond->selection[zond->last_baum] );
/* wennüberhaupt, dann in cb row_collapse oder _expand verschieben, aber eher besser so!!!
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
*/
    }

    g_object_set( sond_treeview_get_cell_renderer_text( zond->treeview[baum] ),
            "editable", TRUE, NULL);

    if ( baum != BAUM_AUSWERTUNG )
            gtk_widget_set_sensitive( GTK_WIDGET(zond->textview), FALSE );

    return FALSE;
}


void
treeviews_cb_editing_canceled( GtkCellRenderer* renderer,
                              gpointer data)
{
    Projekt* zond = (Projekt*) data;

    zond->key_press_signal = g_signal_connect( zond->app_window, "key-press-event",
            G_CALLBACK(cb_key_press), zond );

    return;
}


void
treeviews_cb_editing_started( GtkCellRenderer* renderer, GtkEditable* editable,
                             const gchar* path,
                             gpointer data )
{
    Projekt* zond = (Projekt*) data;

    g_signal_handler_disconnect( zond->app_window, zond->key_press_signal );
    zond->key_press_signal = 0;

    return;
}

void
init_treeviews( Projekt* zond )
{
    //der treeview
    zond->treeview[BAUM_FS] = SOND_TREEVIEW(zond_treeviewfm_new( zond ));
    zond->treeview[BAUM_INHALT] = SOND_TREEVIEW(zond_treeview_new( zond, (gint) BAUM_INHALT));
    zond->treeview[BAUM_AUSWERTUNG] = SOND_TREEVIEW(zond_treeview_new( zond, (gint) BAUM_AUSWERTUNG ));

    for ( Baum baum = BAUM_FS; baum < NUM_BAUM; baum++ )
    {
        //die Selection
        zond->selection[baum] = gtk_tree_view_get_selection(
                GTK_TREE_VIEW(zond->treeview[baum]) );

        //Text-Spalte wird editiert
        //Beginn
        g_signal_connect( sond_treeview_get_cell_renderer_text( zond->treeview[baum] ),
                "editing-started", G_CALLBACK(treeviews_cb_editing_started), zond );
        g_signal_connect( sond_treeview_get_cell_renderer_text( zond->treeview[baum] ),
                "editing-canceled", G_CALLBACK(treeviews_cb_editing_canceled), zond );

        //focus-in
        zond->treeview_focus_in_signal[baum] = g_signal_connect( zond->treeview[baum],
                "focus-in-event", G_CALLBACK(cb_focus_in), (gpointer) zond );
        g_signal_connect( zond->treeview[baum], "focus-out-event",
                G_CALLBACK(cb_focus_out), (gpointer) zond );
    }

    return;
}

