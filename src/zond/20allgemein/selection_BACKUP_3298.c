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
    for ( gint i = 0; i < arr_new_nodes->len; i++ )
    {
        gint rc = 0;
        gint node_id_new = 0;
        DbToBaum db_to_baum = { zond, zond->inhalt, 0, 0, iter, { 0 } };
        gint ebene = 0;

        node_id_new = g_array_index( arr_new_nodes, gint, i );

        //datei in baum_inhalt einfügen
<<<<<<< HEAD
        rc = zond_dbase_walk_tree( zond->dbase_zond->zond_dbase_work, node_id_new,
                &ebene, (ZondDBaseWalkTreeForeachFunc) treeviews_db_to_baum, &db_to_baum, errmsg );
        if ( rc ) ERROR_S
=======
        rc = treeviews_db_to_baum_rec( zond, FALSE, BAUM_INHALT, node_id_new,
                ((!iter && kind) ? NULL : &iter_loop), kind, &iter_new, errmsg );
        if ( rc ) ERROR_S_VAL( NULL )
>>>>>>> 1dc3db5 (links angeklemmt; BugFix Parameter zond_tree_store)

        sond_treeview_expand_row( SOND_TREEVIEW(zond->inhalt), db_to_baum.anchor_iter );
        gtk_tree_view_columns_autosize( GTK_TREE_VIEW(((Projekt*) zond)->inhalt) );

        kind = FALSE;
    }

    return gtk_tree_iter_copy( &iter_loop );
}


static const gchar*
selection_get_icon_name( Projekt* zond, GFile* file )
{
    GFileInfo* info = NULL;
    gchar* content_type = NULL;
    GIcon* icon = NULL;
    const gchar* icon_name = NULL;

    info = g_file_query_info( file, "*", G_FILE_QUERY_INFO_NONE, NULL, NULL );
    if ( !info ) return 0;

    content_type = (gchar*) g_file_info_get_content_type( info );
    if ( !content_type )
    {
        g_object_unref( info );
        return 0;
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
    gint new_node_id = 0;
    const gchar* icon_name = NULL;
    gint fundstelle_id = 0;
    gchar* basename = NULL;

    icon_name = selection_get_icon_name( zond, (GFile*) file );

    fundstelle_id = zond_dbase_insert_fundstelle( zond->dbase_zond->zond_dbase_work, rel_path, NULL, errmsg );
    if ( fundstelle_id == -1 ) ERROR_S

    basename = g_file_get_basename( (GFile*) file );
    new_node_id = zond_dbase_insert_node( zond->dbase_zond->zond_dbase_work,
            node_id, child, icon_name, basename, NULL, fundstelle_id, REF_TYPE_FUNDSTELLE, errmsg );

    g_free( basename );

    if ( new_node_id == -1 ) ERROR_S

    return new_node_id;
}


/** Fehler: -1
    eingefügt: node_id
    nicht eingefügt, weil schon angebunden: 0 **/
static gint
selection_datei_anbinden( Projekt* zond, InfoWindow* info_window, GFile* file,
        gint anchor_id, gboolean child, gint* zaehler, gchar** errmsg )
{
    gint rc = 0;
    gint new_node_id = 0;

    if ( info_window->cancel ) return -2;

    //Prüfen, ob Datei schon angebunden
    gchar* rel_path = get_rel_path_from_file( zond->dbase_zond->project_dir, file );
    rc = zond_dbase_get_fundstellen_for_rel_path( zond->dbase_zond->zond_dbase_work, rel_path, NULL, errmsg );
    if ( rc == -1 )
    {
        g_free( rel_path );
        ERROR_S
    }
    else if ( rc > 0 )
    {
        gchar* text = add_string( rel_path, g_strdup( " ...bereits angebunden" ) );
        info_window_set_message( info_window, text );
        g_free( text );

        return 0; //Wenn angebunden: nix machen
    }

    info_window_set_message( info_window, rel_path );

    new_node_id = selection_datei_einfuegen_in_db( zond, rel_path, file, anchor_id,
            child, errmsg );
    g_free( rel_path );
    if ( new_node_id == -1 ) ERROR_S

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

    new_node_id = zond_dbase_insert_node( zond->dbase_zond->zond_dbase_work,
            node_id, child, zond->icon[ICON_ORDNER].icon_name, basename, NULL, 0, 0, errmsg );

    text = g_strconcat( "Verzeichnis eingefügt: ", basename, NULL );
    info_window_set_message( info_window, text );
    g_free( text );

    g_free( basename );

    if ( new_node_id == -1 ) ERROR_S

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
    gint node_anchor;
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
    gchar* full_path = sond_treeviewfm_get_full_path( SOND_TREEVIEWFM(s_selection->zond->file_manager), iter );

    GFile* file = g_file_new_for_path( full_path );
    g_free( full_path );
    gint new_node_id = 0;

    if ( g_file_query_file_type( file, G_FILE_QUERY_INFO_NONE, NULL ) ==
            G_FILE_TYPE_DIRECTORY )
    {
        new_node_id = selection_ordner_anbinden_rekursiv( s_selection->zond,
                s_selection->info_window, file, s_selection->node_anchor,
                s_selection->kind, &s_selection->zaehler, errmsg );
        g_object_unref( file );
        if ( new_node_id == -1 ) ERROR_S
    }
    else
    {
        new_node_id = selection_datei_anbinden( s_selection->zond,
                s_selection->info_window, file, s_selection->node_anchor, s_selection->kind,
                &s_selection->zaehler, errmsg );
        g_object_unref( file );
        if ( new_node_id == -1 ) ERROR_S
        else if ( new_node_id == 0 ) return 0;
    }

    if ( new_node_id == -2 ) return 1; //abgebrochen!

    g_array_append_val( s_selection->arr_new_nodes, new_node_id );

    s_selection->node_anchor = new_node_id;
    s_selection->kind = FALSE;

    return 0;
}


static gint
three_treeviews_clipboard_anbinden( Projekt* zond, ZondTreeStore* tree_store_anchor, GtkTreeIter* iter_anchor,
        gboolean kind, GArray* arr_new_nodes, InfoWindow* info_window, gchar** errmsg )
{
    gint rc = 0;
    gboolean success = FALSE;
    SSelectionAnbinden s_selection = { 0 };
    GtkTreeIter* iter_last_inserted = NULL;

    s_selection.node_anchor = treeviews_get_node_id( tree_store_anchor, iter_anchor );

    s_selection.zond = zond;
    s_selection.arr_new_nodes = arr_new_nodes;
    s_selection.kind = kind;
    s_selection.zaehler = 0;
    s_selection.info_window = info_window;

    rc = zond_dbase_begin( zond->dbase_zond->zond_dbase_work, errmsg );
    if ( rc ) ERROR_S

    rc = sond_treeview_clipboard_foreach( SOND_TREEVIEW(zond->file_manager),
            three_treeviews_clipboard_anbinden_foreach, &s_selection, errmsg );
    if ( rc == -1 ) ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )

<<<<<<< HEAD
    rc = selection_anbinden_zu_baum( zond, (success) ? iter_anchor : NULL, kind, arr_new_nodes, errmsg );
    if ( rc ) ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )
=======
    iter_last_inserted = selection_anbinden_zu_baum( zond, (success) ? &iter : NULL, kind, arr_new_nodes, errmsg );
    if ( !iter_last_inserted ) ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )
>>>>>>> 1dc3db5 (links angeklemmt; BugFix Parameter zond_tree_store)

    rc = zond_dbase_commit( zond->dbase_zond->zond_dbase_work, errmsg );
    if ( rc )
    {
        gtk_tree_iter_free( iter_last_inserted );
        ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )
    }

<<<<<<< HEAD
    if ( iter_anchor ) sond_treeview_set_cursor( SOND_TREEVIEW(zond->inhalt), iter_anchor );
=======
    if ( success ) sond_treeview_set_cursor( zond->treeview[BAUM_INHALT], iter_last_inserted );
    gtk_tree_iter_free( iter_last_inserted );
>>>>>>> 1dc3db5 (links angeklemmt; BugFix Parameter zond_tree_store)

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
    ZondTreeStore* tree_store_anchor = NULL;
    GtkTreeIter* iter_anchor = NULL;
    gboolean success = FALSE;

    clipboard = sond_treeview_get_clipboard( SOND_TREEVIEW(zond->activ_treeview) );

    //Todo: kopieren so ändern, daß zukünftig diese Beschränkung nur für
    //ausschneiden erforderlich ist (erst Knoten mit Kindern komplett kopieren,
    //und dann erst einfügen
    //Muß auf jeden Fall geprüft werden (derzeit)
            //will ich das wirklich?

    if ( SOND_TREEVIEW(zond->activ_treeview) == clipboard->tree_view ) //wenn innerhalb des gleichen Baums
    {
        gboolean desc = FALSE;

        desc = sond_treeview_test_cursor_descendant( SOND_TREEVIEW(zond->activ_treeview) );
        if ( desc ) ERROR_S_MESSAGE( "Unzulässiges Ziel: Abkömmling von zu verschiebendem "
                "Knoten" )
    }

    if ( zond->activ_treeview != zond->file_manager )
    {
        success = treeviews_get_anchor_treestore_and_iter( zond, &kind,
                &tree_store_anchor, &iter );
        if ( success ) iter_anchor = &iter;
    }

    //wenn in baum_inhalt eingefügt werden soll...
    if ( success && tree_store_anchor ==
                ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(zond->inhalt) )) )
    {
        gint rc = 0;

        //...dann Test, ob Ziel unter Datei liegt
        rc = treeviews_hat_vorfahre_datei( zond, iter_anchor, kind, errmsg );
        if ( rc == -1 ) ERROR_S
        else if ( rc == 1 ) ERROR_S_MESSAGE( "Unzulässiges Ziel: "
                "Abkömmling von Anbindung" )
    }

    //Jetzt die einzelnen Varianten
    if ( clipboard->tree_view == SOND_TREEVIEW(zond->file_manager) )
    {
        if ( zond->activ_treeview == zond->file_manager )
        {
            gint rc = 0;

            rc = sond_treeviewfm_paste_clipboard( SOND_TREEVIEWFM(zond->file_manager),
                    kind, errmsg );
            if ( rc ) ERROR_S
        }
        else if ( tree_store_anchor == ZOND_TREE_STORE(gtk_tree_view_get_model(
                GTK_TREE_VIEW(zond->inhalt) )) )
        {
            gint rc = 0;
            InfoWindow* info_window = NULL;
            GArray* arr_new_nodes = NULL;

            arr_new_nodes = g_array_new( FALSE, FALSE, sizeof( gint ) );
            info_window = info_window_open( zond->app_window, "Dateien anbinden" );

            rc = three_treeviews_clipboard_anbinden( zond, tree_store_anchor,
                    iter_anchor, kind, arr_new_nodes, info_window, errmsg );
            if ( rc == -1 )
            {
                info_window_set_message( info_window, *errmsg );
                g_clear_pointer( errmsg, g_free );
            }

            g_array_unref( arr_new_nodes );
            info_window_close( info_window );
        }
    }
    else if ( !clipboard->ausschneiden && !link )
    {
        gint rc = 0;

        //in baum_inhalt kopieren geht nicht
        if ( tree_store_anchor == ZOND_TREE_STORE(gtk_tree_view_get_model(
                GTK_TREE_VIEW(zond->inhalt) )) ) return 0;

        rc = treeviews_clipboard_kopieren( zond, tree_store_anchor, iter_anchor,
                kind, errmsg );
        if ( rc == -1 ) ERROR_S
    }
    else if ( clipboard->ausschneiden && !link )
    {
        gint rc = 0;

        rc = treeviews_clipboard_verschieben( zond, tree_store_anchor, iter_anchor, kind, errmsg );
        if ( rc == -1 ) ERROR_S
    }
    else if ( !clipboard->ausschneiden && link )
    {
        gint rc = 0;

        //link in baum_inhalt geht nicht
        if ( tree_store_anchor == ZOND_TREE_STORE(gtk_tree_view_get_model(
                GTK_TREE_VIEW(zond->inhalt) )) ) return 0;

        rc = treeviews_paste_clipboard_as_link( zond, tree_store_anchor, iter_anchor, kind, errmsg );
        if ( rc ) ERROR_S
    }
    else //ausschneiden und link
    {
        ERROR_S_MESSAGE( "Verschieben als link nicht möglich" )
    }
/*
    sond_treeview_expand_row( SOND_TREEVIEW(zond->activ_treeview), iter_anchor );
    sond_treeview_set_cursor( SOND_TREEVIEW(zond->activ_treeview), iter_anchor );
*/
    return 0;
}


