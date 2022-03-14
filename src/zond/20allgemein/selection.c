/*
zond (selection.c) - Akten, Beweisstücke, Unterlagen
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

#include "../global_types.h"
#include "../zond_tree_store.h"
#include "../zond_dbase.h"

#include "../99conv/general.h"

#include "project.h"
#include "treeviews.h"

#include "../../misc.h"
#include "../../eingang.h"
#include "../../sond_treeviewfm.h"
#include "../zond_treeview.h"
#include "../../dbase.h"

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <sqlite3.h>


typedef struct {
    Projekt* zond;
    Baum baum;
    gint parent_id;
    gint older_sibling_id;
} SSelectionVerschieben;


static gint
selection_foreach_verschieben( SondTreeview* tree_view, GtkTreeIter* iter,
        gpointer data, gchar** errmsg )
{
    gint rc = 0;
    gint node_id = 0;

    SSelectionVerschieben* s_selection = (SSelectionVerschieben*) data;

    gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(tree_view) ), iter,
            2, &node_id, -1 );

    if ( s_selection->baum == BAUM_INHALT )
    {
        gint rc = 0;

        rc = zond_dbase_get_ziel( s_selection->zond->dbase_zond->zond_dbase_work, s_selection->baum,
                node_id, NULL, errmsg );
        if ( rc == -1 ) ERROR_SOND( "zond_dbase_get_ziel" )
        else if ( rc == 1 ) return 0;
    }

    rc = treeviews_knoten_verschieben( s_selection->zond, s_selection->baum, node_id, s_selection->parent_id,
            s_selection->older_sibling_id, errmsg );
    if ( rc ) ERROR_SOND ( "knoten_verschieben" )

    s_selection->older_sibling_id = node_id;

    return 0;
}


//Ist immer verschieben innerhalb des Baums
static gint
selection_verschieben( Projekt* zond, Baum baum, gint anchor_id, gboolean kind,
        gchar** errmsg )
{
    gint rc = 0;
    Clipboard* clipboard = NULL;
    SSelectionVerschieben s_selection = { zond, baum, 0, 0 };

    if ( kind ) s_selection.parent_id = anchor_id;
    else
    {
        s_selection.parent_id = zond_dbase_get_parent( zond->dbase_zond->zond_dbase_work, baum, anchor_id, errmsg );
        if ( s_selection.parent_id < 0 ) ERROR_SOND( "zond_dbase_get_parent" )

        s_selection.older_sibling_id = anchor_id;
    }

    rc = sond_treeview_clipboard_foreach( zond->treeview[baum],
            selection_foreach_verschieben, &s_selection, errmsg );
    if ( rc == -1 ) ERROR_SOND( "somd_treeview_selection_foreach" )

    //Alte Auswahl löschen
    clipboard = sond_treeview_get_clipboard( zond->treeview[baum] );
    if ( clipboard->arr_ref->len > 0 ) g_ptr_array_remove_range( clipboard->arr_ref,
            0, clipboard->arr_ref->len );

    gtk_widget_queue_draw( GTK_WIDGET(zond->treeview[baum]) );

    GtkTreeIter* iter = zond_treeview_abfragen_iter( ZOND_TREEVIEW(zond->treeview[baum]), s_selection.older_sibling_id );

    if ( iter )
    {
        sond_treeview_expand_row( zond->treeview[baum], iter );
        sond_treeview_set_cursor( zond->treeview[baum], iter );

        gtk_tree_iter_free( iter );
    }

    return 0;
}


/** Dateien oder Ordner anbinden **/
static gint
selection_anbinden_zu_baum( Projekt* zond, GtkTreeIter* iter, gboolean kind,
        GArray* arr_new_nodes, gchar** errmsg )
{
    GtkTreeIter* iter_loop = NULL;

    for ( gint i = 0; i < arr_new_nodes->len; i++ )
    {
        gint rc = 0;
        gint node_id_new = 0;
        GtkTreeIter iter_new = { 0, };

        node_id_new = g_array_index( arr_new_nodes, gint, i );

        //datei in baum_inhalt einfügen
        rc = treeviews_db_to_baum_rec( zond, FALSE, BAUM_INHALT, node_id_new,
                iter_loop, kind, &iter_new, errmsg );
        if ( iter_loop ) gtk_tree_iter_free( iter_loop );
        if ( rc ) ERROR_SOND( "db_baum_knoten_mit_kindern" )

        sond_treeview_expand_row( zond->treeview[BAUM_INHALT], &iter_new );
        gtk_tree_view_columns_autosize( GTK_TREE_VIEW(((Projekt*) zond)->treeview[BAUM_INHALT]) );

        iter_loop = gtk_tree_iter_copy( &iter_new );
        kind = FALSE;
    }

    if ( iter_loop ) gtk_tree_iter_free( iter_loop );

    return 0;
}


static gchar*
selection_get_icon_name( Projekt* zond, GFile* file )
{
    GFileInfo* info = NULL;
    GIcon* icon = NULL;
    gchar* icon_name = NULL;
    gchar* content_type = NULL;

    info = g_file_query_info( file, "*", G_FILE_QUERY_INFO_NONE, NULL, NULL );
    if ( !info ) return g_strdup( "dialog-error" );

    content_type = (gchar*) g_file_info_get_content_type( info );
    if ( !content_type )
    {
        g_object_unref( info );
        return g_strdup( "dialog-error" );
    }

    if ( g_content_type_is_mime_type( content_type, "application/pdf" ) ) icon_name = g_strdup( "pdf" );
    else if ( g_content_type_is_a( content_type, "audio" ) ) icon_name = g_strdup( "audio-x-generic" );
    else
    {
        icon = g_file_info_get_icon( info );
        icon_name = g_icon_to_string( icon );
        g_object_unref( icon );
    }

    g_free( content_type );
//    g_object_unref( info );

    return icon_name;
}


static gint
selection_datei_einfuegen_in_db( Projekt* zond, gchar* rel_path, GFile* file, gint node_id,
        gboolean child, gchar** errmsg )
{
    gint rc = 0;
    gint new_node_id = 0;

    gchar* icon_name = selection_get_icon_name( zond, (GFile*) file );
    gchar* basename = g_file_get_basename( (GFile*) file );

    new_node_id = zond_dbase_insert_node( zond->dbase_zond->zond_dbase_work, BAUM_INHALT, node_id, child, icon_name,
            basename, errmsg );

    g_free( basename );
    g_free( icon_name );

    if ( new_node_id == -1 )
    {
        if ( errmsg ) *errmsg = add_string( g_strdup(
                "Bei Aufruf zond_dbase_insert_node:\n" ), *errmsg );

        return -1;
    }

    rc = zond_dbase_set_datei( zond->dbase_zond->zond_dbase_work, new_node_id, rel_path, errmsg );
    if ( rc ) ERROR_SOND( "zond_dbase_set_datei" )

    return new_node_id;
}


/** Fehler: -1
    eingefügt: node_id
    nicht eingefügt, weil schon angebunden: 0 **/
static gint
selection_datei_anbinden( Projekt* zond, InfoWindow* info_window, GFile* file, gint node_id,
        gboolean child, gint* zaehler, gchar** errmsg )
{
    gint rc = 0;
    gint new_node_id = 0;

    if ( info_window->cancel ) return -2;

    //Prüfen, ob Datei schon angebunden
    gchar* rel_path = get_rel_path_from_file( zond->dbase_zond->project_dir, file );
    rc = zond_dbase_get_node_id_from_rel_path( zond->dbase_zond->zond_dbase_work, rel_path, errmsg );
    if ( rc == -1 )
    {
        g_free( rel_path );
        ERROR_SOND( "zond_dbase_get_node_id_from_rel_path" )
    }
    else if ( rc > 0 )
    {
        gchar* text = add_string( rel_path, g_strdup( " ...bereits angebunden" ) );
        info_window_set_message( info_window, text );
        g_free( text );

        return 0; //Wenn angebunden: nix machen
    }

    info_window_set_message( info_window, rel_path );

    new_node_id = selection_datei_einfuegen_in_db( zond, rel_path, file, node_id,
            child, errmsg );
    g_free( rel_path );
    if ( new_node_id == -1 ) ERROR_SOND( "selection_datei_einfuegen_in_db" )

    (*zaehler)++;

    return new_node_id;
}


/*  Fehler: Rückgabe -1
**  ansonsten: Id des zunächst erzeugten Knotens  */
static gint
selection_ordner_anbinden_rekursiv( Projekt* zond, InfoWindow* info_window,
        GFile* file, gint node_id, gboolean child, gint* zaehler, gchar** errmsg )
{
    gint new_node_id = 0;
    gchar* text = 0;
    gchar* basename = NULL;
    GError* error = NULL;

    if ( info_window->cancel ) return -2;

    basename = g_file_get_basename( file );

    new_node_id = zond_dbase_insert_node( zond->dbase_zond->zond_dbase_work, BAUM_INHALT, node_id, child, "folder",
            basename, errmsg );

    text = g_strconcat( "Verzeichnis eingefügt: ", basename, NULL );
    info_window_set_message( info_window, text );
    g_free( text );

    g_free( basename );

    if ( new_node_id == -1 ) ERROR_SOND( "zond_dbase_insert_node" )

    GFileEnumerator* enumer = g_file_enumerate_children( file, "*", G_FILE_QUERY_INFO_NONE, NULL, &error );
    if ( !enumer )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerate_children:\n",
                error->message, NULL );
        g_error_free( error );

        return -1;
    }

    //new_anchor kopieren, da in der Schleife verändert wird
    //es soll aber der soeben erzeugte Punkt zurückgegegen werden
    GFile* file_child = NULL;
    GFileInfo* info_child = NULL;
    gint new_node_id_loop_tmp = 0;

    gint new_node_id_loop = new_node_id;

    child = TRUE;

    while ( 1 )
    {
        if ( !g_file_enumerator_iterate( enumer, &info_child, &file_child, NULL, &error ) )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerator_iterate:\n",
                    error->message, NULL );
            g_error_free( error );
            g_object_unref( enumer );

            return -1;
        }

        if ( file_child ) //es gibt noch Datei in Verzeichnis
        {
            GFileType type = g_file_info_get_file_type( info_child );
            if ( type == G_FILE_TYPE_DIRECTORY )
            {
                new_node_id_loop_tmp = selection_ordner_anbinden_rekursiv( zond,
                        info_window, file_child, new_node_id_loop, child,
                        zaehler, errmsg );

                if ( new_node_id_loop_tmp == -1 )
                {
                    g_object_unref( enumer );

                    return -1;
                }
                else if ( new_node_id_loop_tmp == -2 ) break; //abgebrochen
            }
            else if ( type == G_FILE_TYPE_REGULAR )
            {
                new_node_id_loop_tmp = selection_datei_anbinden( zond,
                        info_window, file_child, new_node_id_loop, child,
                        zaehler, errmsg );

                if ( new_node_id_loop_tmp == -1 )
                {
                    g_object_unref( enumer );

                    if ( errmsg ) *errmsg = add_string( g_strdup(
                            "Bei Aufruf datei_anbinden:\n" ), *errmsg );

                    return -1;
                }
                else if ( new_node_id_loop_tmp == -2 ) break; //abgebrochen
                else if ( new_node_id_loop_tmp == 0 ) continue;
            }

            new_node_id_loop = new_node_id_loop_tmp;
            child = FALSE;
        } //ende if ( child )
        else break;
    }

    g_object_unref( enumer );

    return new_node_id;
}


typedef struct {
    Projekt* zond;
    gint anchor_id;
    GArray* arr_new_nodes;
    gboolean kind;
    gint zaehler;
    InfoWindow* info_window;
} SSelectionAnbinden;


static gint
selection_foreach_anbinden( SondTreeview* tree_view, GtkTreeIter* iter,
        gpointer data, gchar** errmsg )
{
    SSelectionAnbinden* s_selection = (SSelectionAnbinden*) data;

    //datei ermitteln und anbinden
    gchar* full_path = sond_treeviewfm_get_full_path( SOND_TREEVIEWFM(s_selection->zond->treeview[BAUM_FS]), iter );

    GFile* file = g_file_new_for_path( full_path );
    g_free( full_path );
    gint new_node_id = 0;

    if ( g_file_query_file_type( file, G_FILE_QUERY_INFO_NONE, NULL ) ==
            G_FILE_TYPE_DIRECTORY )
    {
        new_node_id = selection_ordner_anbinden_rekursiv( s_selection->zond,
                s_selection->info_window, file, s_selection->anchor_id,
                s_selection->kind, &s_selection->zaehler, errmsg );
        g_object_unref( file );
        if ( new_node_id == -1 )
                ERROR_SOND( "selection_datei_ordner_anbinden_rekursiv" )
    }
    else
    {
        new_node_id = selection_datei_anbinden( s_selection->zond,
                s_selection->info_window, file, s_selection->anchor_id, s_selection->kind,
                &s_selection->zaehler, errmsg );
        g_object_unref( file );
        if ( new_node_id == -1 ) ERROR_SOND( "selection_datei_anbinden" )
        else if ( new_node_id == 0 ) return 0;
    }

    if ( new_node_id == -2 ) return 1; //abgebrochen!

    g_array_append_val( s_selection->arr_new_nodes, new_node_id );

    s_selection->kind = FALSE;
    s_selection->anchor_id = new_node_id;

    return 0;
}


static gint
three_treeviews_clipboard_anbinden( Projekt* zond, gint anchor_id, gboolean kind, GArray* arr_new_nodes,
        InfoWindow* info_window, gchar** errmsg )
{
    gint rc = 0;
    GtkTreeIter iter = { 0 };
    gboolean success = FALSE;
    SSelectionAnbinden s_selection = { 0 };

    //cursor in baum_inhalt ermitteln
    success = sond_treeview_get_cursor( zond->treeview[BAUM_INHALT], &iter );

    s_selection.zond = zond;
    s_selection.arr_new_nodes = arr_new_nodes;
    s_selection.anchor_id = anchor_id;
    s_selection.kind = kind;
    s_selection.zaehler = 0;
    s_selection.info_window = info_window;

    rc = dbase_begin( (DBase*) zond->dbase_zond->dbase_work, errmsg );
    if ( rc ) ERROR_SOND( "db_begin" )

    rc = sond_treeview_clipboard_foreach( zond->treeview[BAUM_FS],
            selection_foreach_anbinden, &s_selection, errmsg );
    if ( rc == -1 ) ERROR_ROLLBACK( (DBase*) zond->dbase_zond->dbase_work, "treeview_selection_foreach" )

    rc = selection_anbinden_zu_baum( zond, (success) ? &iter : NULL, kind, arr_new_nodes, errmsg );
    if ( rc ) ERROR_ROLLBACK( (DBase*) zond->dbase_zond->dbase_work, "selection_anbinden_zu_baum" );

    rc = dbase_commit( (DBase*) zond->dbase_zond->dbase_work, errmsg );
    if ( rc ) ERROR_ROLLBACK( (DBase*) zond->dbase_zond->dbase_work, "db_commit" )

    if ( success ) sond_treeview_set_cursor( zond->treeview[BAUM_INHALT], &iter );

    gchar* text = g_strdup_printf( "%i Datei(en) angebunden", s_selection.zaehler );
    info_window_set_message( info_window, text );
    g_free( text );

    return s_selection.zaehler;
}


gint
three_treeviews_paste_clipboard( Projekt* zond, gboolean kind, gboolean link, gchar** errmsg )
{
    Clipboard* clipboard = NULL;
    GtkTreeIter iter = { 0, };
    gint anchor_id = 0;

    if ( zond->baum_active == KEIN_BAUM ) return 0;

    clipboard = sond_treeview_get_clipboard( zond->treeview[zond->baum_active] );

    Baum baum_selection = (Baum) sond_treeview_get_id( clipboard->tree_view );

    //Todo: kopieren so ändern, daß zukünftig diese Beschränkung nur für
    //ausschneiden erforderlich ist (erst Knoten mit Kindern komplett kopieren,
    //und dann erst einfügen
    //Muß auf jeden Fall geprüft werden (derzeit)
            //will ich das wirklich?

    if ( zond->baum_active == baum_selection ) //wenn innerhalb des gleichen Baums
    {
        if ( sond_treeview_test_cursor_descendant( zond->treeview[zond->baum_active] ) )
                ERROR_S_MESSAGE( "Unzulässiges Ziel: Abkömmling von zu verschiebendem "
                "Knoten" )
    }

    if ( zond->baum_active != BAUM_FS &&
            !treeviews_get_anchor_id( zond, &kind, &iter, &anchor_id ) ) return 0;
    //else: anchor_id bleibt 0

    //Jetzt die einzelnen Varianten
    if ( baum_selection == BAUM_FS )
    {
        if ( zond->baum_active == BAUM_FS )
        {
            gint rc = 0;

            rc = sond_treeviewfm_paste_clipboard( SOND_TREEVIEWFM(zond->treeview[BAUM_FS]),
                    kind, errmsg );
            if ( rc ) ERROR_S
        }
        else if ( zond->baum_active == BAUM_INHALT && !clipboard->ausschneiden )
        {
            gint rc = 0;
            InfoWindow* info_window = NULL;
            GArray* arr_new_nodes = NULL;

            rc = treeviews_hat_vorfahre_datei( zond, zond->baum_active, anchor_id, kind, errmsg );
            if ( rc == -1 ) ERROR_S
            else if ( rc == 1 ) ERROR_S_MESSAGE( "Unzulässiges Ziel: "
                    "Abkömmling von Anbindung" )

            arr_new_nodes = g_array_new( FALSE, FALSE, sizeof( gint ) );
            info_window = info_window_open( zond->app_window, "Dateien anbinden" );

            rc = three_treeviews_clipboard_anbinden( zond, anchor_id, kind, arr_new_nodes, info_window, errmsg );
            if ( rc == -1 )
            {
                info_window_set_message( info_window, *errmsg );
                g_clear_pointer( errmsg, g_free );
            }

            g_array_unref( arr_new_nodes );
            info_window_close( info_window );
        }
    }
    else if ( baum_selection == BAUM_INHALT )
    {
        if ( zond->baum_active == BAUM_INHALT && clipboard->ausschneiden )
        {
            gint rc = 0;

            rc = treeviews_hat_vorfahre_datei( zond, zond->baum_active, anchor_id, kind, errmsg );
            if ( rc == -1 ) ERROR_S
            else if ( rc == 1 ) ERROR_S_MESSAGE( "Unzulässiges Ziel: "
                    "Abkömmling von Anbindung" )

            rc = selection_verschieben( zond, baum_selection, anchor_id, kind, errmsg );
            if ( rc == -1 ) ERROR_S
        }
        else if ( zond->baum_active == BAUM_INHALT && !clipboard->ausschneiden )
        {//kopieren innerhalb BAUM_INHALT = verschieben von Anbindungen

        }
        else if ( zond->baum_active == BAUM_AUSWERTUNG && !clipboard->ausschneiden && !link )
        {
            gint rc = 0;

            rc = treeviews_clipboard_kopieren( zond, BAUM_AUSWERTUNG, anchor_id,
                    kind, &iter, errmsg );
            if ( rc == -1 ) ERROR_S
        }
        else if ( zond->baum_active == BAUM_AUSWERTUNG && !clipboard->ausschneiden && link )
        {
            gint rc = 0;

            rc = treeviews_paste_clipboard_as_link( zond, zond->baum_active, anchor_id, kind, &iter, errmsg );
            if ( rc ) ERROR_S

            return 0;
        }
    }
    else if ( baum_selection == BAUM_AUSWERTUNG )
    {
        if ( zond->baum_active == BAUM_AUSWERTUNG && clipboard->ausschneiden && !link )
        {
            gint rc = 0;

            rc = selection_verschieben( zond, baum_selection, anchor_id, kind, errmsg );
            if ( rc == -1 ) ERROR_S
        }
        else if ( zond->baum_active == BAUM_AUSWERTUNG && !clipboard->ausschneiden && !link )
        {
            gint rc = 0;

            rc = treeviews_clipboard_kopieren( zond, BAUM_AUSWERTUNG, anchor_id,
                    kind, &iter, errmsg );
            if ( rc == -1 ) ERROR_S
        }
        else if ( zond->baum_active == BAUM_AUSWERTUNG && !clipboard->ausschneiden && link )
        {
            gint rc = 0;

            rc = treeviews_paste_clipboard_as_link( zond, zond->baum_active, anchor_id, kind, &iter, errmsg );
            if ( rc ) ERROR_S
        }
    }

    return 0;
}


