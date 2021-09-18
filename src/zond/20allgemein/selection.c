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
#include "../error.h"

#include "../99conv/baum.h"
#include "../99conv/general.h"
#include "../99conv/db_read.h"
#include "../99conv/db_write.h"
#include "../99conv/db_zu_baum.h"

#include "project.h"
#include "dbase_full.h"

#include "../../misc.h"
#include "../../eingang.h"
#include "../../sond_treeviewfm.h"
#include "../../sond_treeview.h"
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
        gint typ = db_knotentyp_abfragen( s_selection->zond, s_selection->baum,
                node_id, errmsg );
        if ( typ == -1 ) ERROR_PAO( "db_knotentyp_abfragen" )
        else if ( typ == 2 ) return 0;
    }

    rc = knoten_verschieben( s_selection->zond, s_selection->baum, node_id, s_selection->parent_id,
            s_selection->older_sibling_id, errmsg );
    if ( rc ) ERROR_PAO ( "knoten_verschieben" )

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
        s_selection.parent_id = db_get_parent( zond, baum, anchor_id, errmsg );
        if ( s_selection.parent_id < 0 ) ERROR_PAO( "db_get_parent" )

        s_selection.older_sibling_id = anchor_id;
    }

    rc = sond_treeview_clipboard_foreach( zond->treeview[baum],
            selection_foreach_verschieben, &s_selection, errmsg );
    if ( rc == -1 ) ERROR_PAO( "somd_treeview_selection_foreach" )

    //Alte Auswahl löschen
    clipboard = sond_treeview_get_clipboard( zond->treeview[baum] );
    if ( clipboard->arr_ref->len > 0 ) g_ptr_array_remove_range( clipboard->arr_ref,
            0, clipboard->arr_ref->len );

    gtk_widget_queue_draw( GTK_WIDGET(zond->treeview[baum]) );

    GtkTreeIter* iter = baum_abfragen_iter( zond->treeview[baum], s_selection.older_sibling_id );

    if ( iter )
    {
        sond_treeview_expand_row( zond->treeview[baum], iter );
        sond_treeview_set_cursor( zond->treeview[baum], iter );

        gtk_tree_iter_free( iter );
    }

    return 0;
}


typedef struct {
    Projekt* zond;
    Baum baum;
    GtkTreeIter* iter_dest;
    gint anchor_id;
    gboolean kind;
} SSelectionKopieren;


gint static
selection_foreach_kopieren( SondTreeview* tree_view, GtkTreeIter* iter, gpointer data, gchar** errmsg )
{
    gint rc = 0;
    GtkTreeIter* iter_new = NULL;
    gint node_id = 0;
    gint new_node_id = 0;

    SSelectionKopieren* s_selection = (SSelectionKopieren*) data;

    rc = dbase_begin( (DBase*) s_selection->zond->dbase_zond->dbase_work, errmsg );
    if ( rc ) ERROR_SOND( "db_begin" )

    gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(tree_view) ), iter,
            2, &node_id, -1 );

    new_node_id = db_kopieren_nach_auswertung_mit_kindern( s_selection->zond, FALSE, s_selection->baum,
            node_id, s_selection->anchor_id, s_selection->kind, errmsg );
    if ( new_node_id == -1 ) ERROR_ROLLBACK( (DBase*) s_selection->zond->dbase_zond->dbase_work,
            "db_kopieren_nach_auswertung_mit_kindern (urspr. Aufruf)" )

    iter_new = db_baum_knoten_mit_kindern( s_selection->zond, FALSE,
            BAUM_AUSWERTUNG, new_node_id, s_selection->iter_dest, s_selection->kind, errmsg );
    if ( !iter_new ) ERROR_ROLLBACK( (DBase*) s_selection->zond->dbase_zond->dbase_work,
            "db_baum_knoten_mit_kindern (urspr. Aufruf)" )

    rc = dbase_commit( (DBase*) s_selection->zond->dbase_zond->dbase_work, errmsg );
    if ( rc ) ERROR_ROLLBACK( (DBase*) s_selection->zond->dbase_zond->dbase_work,
            "db_commit" )

    if ( s_selection->iter_dest ) gtk_tree_iter_free( s_selection->iter_dest );
    s_selection->iter_dest = gtk_tree_iter_copy( iter_new );
    gtk_tree_iter_free( iter_new );

    s_selection->anchor_id = new_node_id;
    s_selection->kind = FALSE;

    return 0;
}


//Ziel ist immer BAUM_AUSWERTUNG
static gint
selection_kopieren( Projekt* zond, Baum baum_von, gint anchor_id, gboolean kind, gchar** errmsg )
{
    gint rc = 0;

    SSelectionKopieren s_selection = { zond, baum_von, NULL, anchor_id, kind };
    s_selection.iter_dest = sond_treeview_get_cursor( zond->treeview[BAUM_AUSWERTUNG] );

    rc = sond_treeview_clipboard_foreach( zond->treeview[baum_von],
            selection_foreach_kopieren, &s_selection, errmsg );

    sond_treeview_expand_row( zond->treeview[BAUM_AUSWERTUNG], s_selection.iter_dest );
    sond_treeview_set_cursor( zond->treeview[BAUM_AUSWERTUNG], s_selection.iter_dest );
    if ( s_selection.iter_dest ) gtk_tree_iter_free( s_selection.iter_dest );

    if ( rc == -1 ) ERROR_PAO( "treeview_selection_foreach" )

    return 0;
}


/** Dateien oder Ordner anbinden **/
static gint
selection_anbinden_zu_baum( Projekt* zond, GtkTreeIter** iter, gboolean kind,
        GArray* arr_new_nodes, gchar** errmsg )
{
    for ( gint i = 0; i < arr_new_nodes->len; i++ )
    {
        gint node_id_new = 0;
        GtkTreeIter* iter_new = NULL;

        node_id_new = g_array_index( arr_new_nodes, gint, i );

        //datei in baum_inhalt einfügen
        iter_new = db_baum_knoten_mit_kindern( zond, FALSE, BAUM_INHALT, node_id_new,
                *iter, kind, errmsg );
        if ( !iter_new ) ERROR_PAO( "db_baum_knoten" )

        sond_treeview_expand_row( zond->treeview[BAUM_INHALT], iter_new );
        gtk_tree_view_columns_autosize( GTK_TREE_VIEW(((Projekt*) zond)->treeview[BAUM_INHALT]) );

        if ( *iter ) gtk_tree_iter_free( *iter );
        *iter = gtk_tree_iter_copy( iter_new );
        gtk_tree_iter_free( iter_new );

        kind = FALSE;
    }

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

    new_node_id = dbase_full_insert_node( zond->dbase_zond->dbase_work, BAUM_INHALT, node_id, child, icon_name,
            basename, errmsg );

    g_free( basename );
    g_free( icon_name );

    if ( new_node_id == -1 )
    {
        if ( errmsg ) *errmsg = prepend_string( *errmsg,
                g_strdup( "Bei Aufruf dbase_full_insert_node:\n" ) );

        return -1;
    }

    rc = db_set_datei( zond, new_node_id, rel_path, errmsg );
    if ( rc ) ERROR_SOND( "db_set_datei" )

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
    rc = db_get_node_id_from_rel_path( zond, rel_path, errmsg );
    if ( rc == -1 )
    {
        g_free( rel_path );
        ERROR_PAO( "db_get_node_id_from_rel_path" )
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
    if ( new_node_id == -1 ) ERROR_PAO( "selection_datei_einfuegen_in_db" )

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

    new_node_id = dbase_full_insert_node( zond->dbase_zond->dbase_work, BAUM_INHALT, node_id, child, "folder",
            basename, errmsg );

    text = g_strconcat( "Verzeichnis eingefügt: ", basename, NULL );
    info_window_set_message( info_window, text );
    g_free( text );

    g_free( basename );

    if ( new_node_id == -1 ) ERROR_SQL( "dbase_full_insert_node" )

    GFileEnumerator* enumer = g_file_enumerate_children( file, "*", G_FILE_QUERY_INFO_NONE, NULL, &error );
    if ( !enumer )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerate_children:\n",
                error->message, NULL );
        g_error_free( error );

        return -1;
    }

    //new_anchor kopieren, da in der Schleife verändert wird
    //es soll aber der soeben erzeugt Punkt zurückgegegen werden
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

                    if ( errmsg ) *errmsg = prepend_string( *errmsg, g_strdup(
                            "Bei Aufruf datei_anbinden:\n" ) );

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
                ERROR_PAO( "selection_datei_ordner_anbinden_rekursiv" )
    }
    else
    {
        new_node_id = selection_datei_anbinden( s_selection->zond,
                s_selection->info_window, file, s_selection->anchor_id, s_selection->kind,
                &s_selection->zaehler, errmsg );
        g_object_unref( file );
        if ( new_node_id == -1 ) ERROR_PAO( "selection_datei_anbinden" )
        else if ( new_node_id == 0 ) return 0;
    }

    if ( new_node_id == -2 ) return 1; //abgebrochen!

    g_array_append_val( s_selection->arr_new_nodes, new_node_id );

    s_selection->kind = FALSE;
    s_selection->anchor_id = new_node_id;

    return 0;
}


static gint
selection_anbinden( Projekt* zond, gint anchor_id, gboolean kind, GArray* arr_new_nodes,
        InfoWindow* info_window, gchar** errmsg )
{
    gint rc = 0;
    GtkTreeIter* iter = NULL;
    SSelectionAnbinden s_selection = { 0 };

    //cursor in baum_inhalt ermitteln
    iter = sond_treeview_get_cursor( zond->treeview[BAUM_INHALT] );

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

    rc = selection_anbinden_zu_baum( zond, &iter, kind, arr_new_nodes, errmsg );
    if ( rc ) ERROR_ROLLBACK( (DBase*) zond->dbase_zond->dbase_work, "selection_anbinden_zu_baum" );

    rc = dbase_commit( (DBase*) zond->dbase_zond->dbase_work, errmsg );
    if ( rc ) ERROR_ROLLBACK( (DBase*) zond->dbase_zond->dbase_work, "db_commit" )

    if ( iter )
    {
        sond_treeview_set_cursor( zond->treeview[BAUM_INHALT], iter );
        gtk_tree_iter_free( iter );
    }

    gchar* text = g_strdup_printf( "%i Datei(en) angebunden", s_selection.zaehler );
    info_window_set_message( info_window, text );
    g_free( text );

    return s_selection.zaehler;
}


void
selection_paste( Projekt* zond, gboolean kind )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    Baum baum = KEIN_BAUM;
    Clipboard* clipboard = NULL;

    baum = baum_abfragen_aktiver_treeview( zond );

    if ( baum == KEIN_BAUM ) return;

    clipboard = sond_treeview_get_clipboard( zond->treeview[baum] );

    Baum baum_selection = baum_get_baum_from_treeview( zond,
            GTK_WIDGET(clipboard->tree_view) );

    //Todo: kopieren so ändern, daß zukünftig diese Beschränkung nur für
    //ausschneiden erforderlich ist (erst Knoten mit Kindern komplett kopieren,
    //und dann erst einfügen
    //Muß auf jeden Fall geprüft werden (derzeit)
            //will ich das wirklich?

    if ( baum == baum_selection ) //wenn innerhalb des gleichen Baums
    {
        rc = sond_treeview_test_cursor_descendant( zond->treeview[baum] );
        if ( rc == 1 )
        {
            meldung( zond->app_window, "Clipboard kann dorthin nicht "
                    "verschoben werden\nAbkömmling von zu verschiebendem "
                    "Knoten", NULL );
            return;
        }
        else if ( rc == 2 ) return;
    }

    gint anchor_id = 0;
    if ( baum != BAUM_FS ) anchor_id =
            baum_abfragen_aktuelle_node_id( zond->treeview[baum] );

    //Jetzt die einzelnen Varianten
    if ( baum_selection == BAUM_FS )
    {
        if ( baum == BAUM_FS )
        {
            rc = sond_treeviewfm_paste_clipboard( SOND_TREEVIEWFM(zond->treeview[BAUM_FS]),
                    kind, &errmsg );
            if ( rc )
            {
                meldung( zond->app_window, "Selection kann nicht kopiert/verschoben "
                        "werden\n\nBei Aufruf fm_paste_clipboard:\n",
                        kind, errmsg, NULL );
                g_free( errmsg );

                return;
            }
        }
        else if ( baum == BAUM_INHALT && !clipboard->ausschneiden )
        {
            InfoWindow* info_window = NULL;
            GArray* arr_new_nodes = NULL;

            if ( anchor_id == 0 ) kind = TRUE;
            else
            {
                rc = hat_vorfahre_datei( zond, baum, anchor_id, kind, &errmsg );
                if ( rc == -1 )
                {
                    meldung( zond->app_window, "Bei Aufruf hat_vorfahre_datei:\n\n",
                            errmsg, NULL );
                    g_free( errmsg );

                    return;
                }
                else if ( rc == 1 )
                {
                    meldung( zond->app_window, "Clipboard kann dorthin nicht "
                            "verschoben werden\nAbkömmling von Anbindung",
                            NULL );

                    return;
                }
            }

            arr_new_nodes = g_array_new( FALSE, FALSE, sizeof( gint ) );
            info_window = info_window_open( zond->app_window, "Dateien anbinden" );

            rc = selection_anbinden( zond, anchor_id, kind, arr_new_nodes, info_window, &errmsg );
            if ( rc == -1 )
            {
                info_window_set_message( info_window, errmsg );
                g_free( errmsg );
            }

            g_array_unref( arr_new_nodes );
            info_window_close( info_window );
        }
    }
    else if ( baum_selection == BAUM_INHALT )
    {
        if ( baum == BAUM_INHALT && clipboard->ausschneiden )
        {
            if ( anchor_id == 0 ) kind = TRUE;
            else
            {
                rc = hat_vorfahre_datei( zond, baum, anchor_id, kind, &errmsg );
                if ( rc == -1 )
                {
                    meldung( zond->app_window, "Bei Aufruf selection_testen_"
                            "zulaessige_vorfahren:\n\n", errmsg, NULL );
                    g_free( errmsg );

                    return;
                }
                else if ( rc == 1 )
                {
                    meldung( zond->app_window, "Clipboard kann dorthin nicht "
                            "verschoben werden\nAbkömmling von Anbindung",
                            NULL );

                    return;
                }
            }

            rc = selection_verschieben( zond, baum_selection, anchor_id, kind, &errmsg );
            if ( rc == -1 )
            {
                meldung( zond->app_window, "Verschieben nicht möglich -\n\n"
                        "Bei Aufruf selection_verschieben:\n", errmsg, NULL );
                g_free( errmsg );

                return;
            }
        }
        else if ( baum == BAUM_INHALT && !clipboard->ausschneiden )
        {//kopieren innerhalb BAUM_INHALT = verschieben von Anbindungen

        }
        else if ( baum == BAUM_AUSWERTUNG && !clipboard->ausschneiden )
        {
            rc = selection_kopieren( zond, baum_selection, anchor_id, kind, &errmsg );
            if ( rc == -1 )
            {
                meldung( zond->app_window, "Fehler in Kopieren:\n\n"
                        "Bei Aufruf selection_kopieren:\n", errmsg, NULL );
                g_free( errmsg );

                return;
            }
        }
    }
    else if ( baum_selection == BAUM_AUSWERTUNG )
    {
        if ( baum == BAUM_AUSWERTUNG && clipboard->ausschneiden )
        {
            rc = selection_verschieben( zond, baum_selection, anchor_id, kind, &errmsg );
            if ( rc == -1 )
            {
                meldung( zond->app_window, "Bei Aufruf selection_"
                        "verschieben:\n\n", errmsg, NULL );
                g_free( errmsg );

                return;
            }
        }
        else if ( baum == BAUM_AUSWERTUNG && !clipboard->ausschneiden )
        {
            rc = selection_kopieren( zond, baum_selection, anchor_id, kind, &errmsg );
            if ( rc == -1 )
            {
                meldung( zond->app_window, "Fehler in Kopieren:\n\n"
                        "Bei Aufruf selection_kopieren:\n", errmsg, NULL );
                g_free( errmsg );

                return;
            }
        }
    }

    return;
}


static gint
selection_foreach_entfernen_anbindung( SondTreeview* stv,
        GtkTreeIter* iter, gpointer data, gchar** errmsg )
{
    gint rc = 0;
    gint typ = 0;
    gint older_sibling = 0;
    gint parent = 0;
    gint child = 0;
    gint node_id = 0;

    Projekt* zond = (Projekt*) data;

    gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(stv) ), iter,
            2, &node_id, -1 );

    typ = db_knotentyp_abfragen( zond, BAUM_INHALT, node_id, errmsg );
    if ( typ == -1 ) ERROR_PAO ( "db_knotentyp_abfragen" )
    if ( typ != 2 ) return 0;

    //herausfinden, ob zu löschender Knoten älteres Geschwister hat
    older_sibling = db_get_older_sibling( zond, BAUM_INHALT, node_id, errmsg );
    if ( older_sibling < 0 ) ERROR_PAO( "db_get_older_sibling" )

    //Elternknoten ermitteln
    parent = db_get_parent( zond, BAUM_INHALT, node_id, errmsg );
    if ( parent < 0 ) ERROR_PAO( "db_get_parent" )

    rc = dbase_begin( (DBase*) zond->dbase_zond->dbase_work, errmsg );
    if ( rc ) ERROR_SOND( "db_begin" )

    child = 0;
    while ( (child = db_get_first_child( zond, BAUM_INHALT, node_id,
            errmsg )) )
    {
        if ( child < 0 ) ERROR_ROLLBACK( (DBase*) zond->dbase_zond->dbase_work,
                "db_get_first_child" )

        rc = knoten_verschieben( zond, BAUM_INHALT, child, parent,
                older_sibling, errmsg );
        if ( rc == -1 ) ERROR_ROLLBACK( (DBase*) zond->dbase_zond->dbase_work,
                "knoten_verschieben" )

        older_sibling = child;
    }

    rc = db_remove_node( zond, BAUM_INHALT, node_id, errmsg );
    if ( rc ) ERROR_ROLLBACK( (DBase*) zond->dbase_zond->dbase_work, "db_remove_node" )

    gtk_tree_store_remove( GTK_TREE_STORE(gtk_tree_view_get_model(
            GTK_TREE_VIEW(stv) )), iter );

    rc = dbase_commit( (DBase*) zond->dbase_zond->dbase_work, errmsg );
    if ( rc ) ERROR_ROLLBACK( (DBase*) zond->dbase_zond->dbase_work, "db_commit" )

    return 0;
}


//Funktioniert nur im BAUM_INHALT - Abfrage im cb
gint
selection_entfernen_anbindung( Projekt* zond, gchar** errmsg )
{
    gint rc = 0;

    rc = sond_treeview_selection_foreach( zond->treeview[BAUM_INHALT],
            selection_foreach_entfernen_anbindung, zond, errmsg );
    if ( rc == -1 ) ERROR_PAO( "treeview_selection_foreach" )

    return 0;
}


static gint
selection_foreach_loeschen( SondTreeview* tree_view, GtkTreeIter* iter,
        gpointer data, gchar** errmsg )
{
    gint node_id = 0;
    Baum baum = KEIN_BAUM;

    Projekt* zond = (Projekt*) data;

    gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(tree_view) ), iter,
            2, &node_id, -1 );
    baum = baum_get_baum_from_treeview( zond, GTK_WIDGET(tree_view) );

    gint rc = db_remove_node( zond, baum, node_id, errmsg );
    if ( rc ) ERROR_PAO ( "db_remove_node" )

    gtk_tree_store_remove( GTK_TREE_STORE(gtk_tree_view_get_model(
            GTK_TREE_VIEW(zond->treeview[baum]) )), iter );

    return 0;
}

gint
selection_loeschen( Projekt* zond, Baum baum, gchar** errmsg )
{
    gint rc = 0;

    if ( baum == BAUM_INHALT || baum == BAUM_AUSWERTUNG ) rc =
            sond_treeview_selection_foreach( zond->treeview[baum],
            selection_foreach_loeschen, zond, errmsg );
    else rc = sond_treeviewfm_selection_loeschen( SOND_TREEVIEWFM(zond->treeview[baum]), errmsg );

    if ( rc == -1 ) ERROR_SOND( "treeview_selection_foreach" )

    return 0;
}


typedef struct _S_Selection_Change_Icon
{
    Projekt* zond;
    Baum baum;
    const gchar* icon_name;
} SSelectionChangeIcon;

static gint
selection_foreach_change_icon_id( SondTreeview* tree_view, GtkTreeIter* iter,
        gpointer data, gchar** errmsg )
{
    gint rc = 0;
    gint node_id = 0;
    SSelectionChangeIcon* s_selection = (SSelectionChangeIcon*) data;

    gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(tree_view) ), iter,
            2, &node_id, -1 );

    rc = dbase_full_set_icon_id( s_selection->zond->dbase_zond->dbase_work, s_selection->baum, node_id, s_selection->icon_name, errmsg );
    if ( rc ) ERROR_PAO( "dbase_full_set_icon_id" )

    //neuen icon_name im tree speichern
    gtk_tree_store_set( GTK_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(s_selection->
            zond->treeview[s_selection->baum]) )), iter, 0, s_selection->icon_name, -1 );

    return 0;
}


void
selection_change_icon_id( Projekt* zond, const gchar* icon_name )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    SSelectionChangeIcon s_selection = { 0 };

    Baum baum = baum_abfragen_aktiver_treeview( zond );
    if ( baum == KEIN_BAUM || baum == BAUM_FS ) return;

    s_selection.zond = zond;
    s_selection.baum = baum;
    s_selection.icon_name = icon_name;

    rc = sond_treeview_selection_foreach( zond->treeview[baum],
            selection_foreach_change_icon_id, (gpointer) &s_selection, &errmsg );
    if ( rc == -1 )
    {
        meldung( zond->app_window, "Fehler Ändern Icons - \n\nBei Aufruf "
                "treeview_selection_foreach:\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


