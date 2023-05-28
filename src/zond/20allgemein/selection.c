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


/** Dateien oder Ordner anbinden **/
static GtkTreeIter*
selection_anbinden_zu_baum( Projekt* zond, GtkTreeIter* iter, gboolean kind,
        GArray* arr_new_nodes, gchar** errmsg )
{
    GtkTreeIter iter_loop = { 0, };

    if ( iter ) iter_loop = *iter;

    for ( gint i = 0; i < arr_new_nodes->len; i++ )
    {
        gint rc = 0;
        gint node_id_new = 0;
        GtkTreeIter iter_new = { 0, };

        node_id_new = g_array_index( arr_new_nodes, gint, i );

        //datei in baum_inhalt einfügen
        rc = treeviews_load_node( zond, FALSE, BAUM_INHALT, node_id_new,
                ((!iter && kind) ? NULL : &iter_loop), kind, &iter_new, errmsg );
        if ( rc ) ERROR_S_VAL( NULL )

        iter_loop = iter_new;
        kind = FALSE;
    }

    return gtk_tree_iter_copy( &iter_loop );
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
three_treeviews_clipboard_anbinden_foreach( SondTreeview* tree_view, GtkTreeIter* iter,
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
    GtkTreeIter* iter_last_inserted = NULL;

    //cursor in baum_inhalt ermitteln
    success = sond_treeview_get_cursor( zond->treeview[BAUM_INHALT], &iter );

    s_selection.zond = zond;
    s_selection.arr_new_nodes = arr_new_nodes;
    s_selection.anchor_id = anchor_id;
    s_selection.kind = kind;
    s_selection.zaehler = 0;
    s_selection.info_window = info_window;

    rc = zond_dbase_begin( zond->dbase_zond->zond_dbase_work, errmsg );
    if ( rc ) ERROR_S

    rc = sond_treeview_clipboard_foreach( three_treeviews_clipboard_anbinden_foreach, &s_selection, errmsg );
    if ( rc == -1 ) ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )

    iter_last_inserted = selection_anbinden_zu_baum( zond, (success) ? &iter : NULL, kind, arr_new_nodes, errmsg );
    if ( !iter_last_inserted ) ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )

    rc = zond_dbase_commit( zond->dbase_zond->zond_dbase_work, errmsg );
    if ( rc )
    {
        gtk_tree_iter_free( iter_last_inserted );
        ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )
    }

    if ( success ) sond_treeview_expand_row( zond->treeview[BAUM_INHALT], &iter );
    sond_treeview_set_cursor( zond->treeview[BAUM_INHALT], iter_last_inserted );
    gtk_tree_iter_free( iter_last_inserted );

    gtk_tree_view_columns_autosize( GTK_TREE_VIEW(((Projekt*) zond)->treeview[BAUM_INHALT]) );

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

    clipboard = ((SondTreeviewClass*) g_type_class_peek( SOND_TYPE_TREEVIEW ))->clipboard;

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
        if ( zond->baum_active == BAUM_INHALT && clipboard->ausschneiden && !link)
        {
            gint rc = 0;

            rc = treeviews_hat_vorfahre_datei( zond, zond->baum_active, anchor_id, kind, errmsg );
            if ( rc == -1 ) ERROR_S
            else if ( rc == 1 ) ERROR_S_MESSAGE( "Unzulässiges Ziel: "
                    "Abkömmling von Anbindung" )

            rc = treeviews_clipboard_verschieben( zond, &iter, anchor_id, kind, errmsg );
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

            rc = treeviews_clipboard_verschieben( zond, &iter, anchor_id, kind, errmsg );
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


