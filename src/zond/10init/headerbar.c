/*
zond (headerbar.c) - Akten, Beweisstücke, Unterlagen
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
#include <mupdf/pdf.h>
#include <sqlite3.h>
#include <tesseract/capi.h>
#include <glib/gstdio.h>

#include "../zond_pdf_document.h"

#include "../../sond_treeview.h"
#include "../../sond_treeviewfm.h"
#include "../../dbase.h"
#include "../../eingang.h"

#include "../global_types.h"
#include "../error.h"
#include "../zond_tree_store.h"

#include "../99conv/general.h"
#include "../99conv/baum.h"
#include "../99conv/db_write.h"
#include "../99conv/db_read.h"
#include "../99conv/db_zu_baum.h"
#include "../99conv/test.h"
#include "../99conv/pdf_ocr.h"

#include "../20allgemein/pdf_text.h"
#include "../20allgemein/ziele.h"
#include "../20allgemein/selection.h"
#include "../20allgemein/suchen.h"
#include "../20allgemein/project.h"
#include "../20allgemein/export.h"
#include "../20allgemein/dbase_full.h"

#include "../40viewer/document.h"

#include "app_window.h"

#include "../../misc.h"



/*
*   Callbacks des Menus "Projekt"
*/
static void
cb_menu_datei_beenden_activate( gpointer data )
{
    Projekt* zond = (Projekt*) data;

    gboolean ret = FALSE;
    g_signal_emit_by_name( zond->app_window, "delete-event", NULL, &ret );

    return;
}


/*  Callbacks des Menus Datei  */
static gboolean
pdf_rel_path_in_array( GPtrArray* arr_rel_path, gchar* rel_path )
{
    for ( gint i = 0; i < arr_rel_path->len; i++ )
    {
        if ( !g_strcmp0( g_ptr_array_index( arr_rel_path, i ), rel_path ) )
                return TRUE;
    }

    return FALSE;
}

static GPtrArray*
selection_abfragen_pdf( Projekt* zond, gchar** errmsg )
{
    gint rc = 0;
    Baum baum = KEIN_BAUM;
    GList* selected = NULL;
    GList* list = NULL;
    gint node_id = 0;
    gchar* rel_path = NULL;

    GPtrArray* arr_rel_path = g_ptr_array_new_with_free_func( (GDestroyNotify) g_free );

    baum = baum_abfragen_aktiver_treeview( zond );
    if ( baum == KEIN_BAUM ) return NULL;

    selected = gtk_tree_selection_get_selected_rows( zond->selection[baum], NULL );
    if( !selected ) return NULL;

    list = selected;
    do //alle rows aus der Liste
    {
        node_id = baum_abfragen_node_id( zond->treeview[baum], list->data,
                errmsg );
        if ( node_id == -1 )
        {
            g_list_free_full( selected, (GDestroyNotify) gtk_tree_path_free );
            g_ptr_array_free( arr_rel_path, TRUE );

            ERROR_PAO_R( "baum_abfragen_node_id", NULL )
        }

        rc = db_get_rel_path( zond, baum, node_id, &rel_path, errmsg );
        if ( rc == -2 ) continue;
        else if ( rc )
        {
            g_list_free_full( selected, (GDestroyNotify) gtk_tree_path_free );
            g_ptr_array_free( arr_rel_path, TRUE );

            ERROR_PAO_R( "db_get_rel_path", NULL )
        }

        //Sonderbehandung, falls pdf-Datei
        if ( is_pdf( rel_path ) && !pdf_rel_path_in_array( arr_rel_path, rel_path ) )
                g_ptr_array_add( arr_rel_path, g_strdup( rel_path ) );

        g_free( rel_path );
    }
    while ( (list = list->next) );

    g_list_free_full( selected, (GDestroyNotify) gtk_tree_path_free );

    return arr_rel_path;
}


static void
cb_item_clean_pdf( GtkMenuItem* item, gpointer data )
{
    Projekt* zond = (Projekt*) data;

    gchar* errmsg = NULL;

    GPtrArray* arr_rel_path = selection_abfragen_pdf( zond, &errmsg );

    if ( !arr_rel_path )
    {
        if ( errmsg )
        {
            meldung( zond->app_window, "PDF kann nicht gereinigt werden\n\nBei "
                    "Aufruf selection_abfragen_pdf:\n", errmsg, NULL );
            g_free( errmsg );
        }

        return;
    }

    if ( arr_rel_path->len == 0 )
    {
        meldung( zond->app_window, "Keine PDF-Datei ausgewählt", NULL );
        g_ptr_array_free( arr_rel_path, TRUE );

        return;
    }

    for ( gint i = 0; i < arr_rel_path->len; i++ )
    {
        gchar* path_tmp = NULL;
        pdf_document* doc = NULL;

        pdf_write_options opts = {
                0, // do_incremental
                1, // do_pretty
                1, // do_ascii
                0, // do_compress
                1, // do_compress_images
                1, // do_compress_fonts
                0, // do_decompress
                4, // do_garbage
                0, // do_linear
                1, // do_clean
                0, // do_sanitize
                0, // do_appearance
                0, // do_encrypt
                0, // dont_regenerate_id  Don't regenerate ID if set (used for clean)
                ~0, // permissions
                "", // opwd_utf8[128]
                "", // upwd_utf8[128]
                0 //do snapshot
                };

        //prüfen, ob in Viewer geöffnet
        if ( zond_pdf_document_is_open( g_ptr_array_index( arr_rel_path, i ) ) )
        {
            meldung( zond->app_window, "PDF ", g_ptr_array_index( arr_rel_path, i ), " säubern nicht möglich\n\n"
                    "PDF bereits geöffnet - zunächst schließen", NULL );

            continue;
        }

        fz_try( zond->ctx ) doc = pdf_open_document( zond->ctx, g_ptr_array_index( arr_rel_path, i ) );
        fz_catch( zond->ctx )
        {
            meldung( zond->app_window, "PDF ", g_ptr_array_index( arr_rel_path, i ), " säubern nicht möglich\n\n"
                    "Bei Aufruf pdf_open_document:\n", fz_caught_message( zond->ctx ), NULL );

            continue;
        }

        fz_try( zond->ctx ) pdf_clean_document( zond->ctx, doc );
        fz_catch( zond->ctx )
        {
            pdf_drop_document( zond->ctx, doc );
            meldung( zond->app_window, "PDF ", g_ptr_array_index( arr_rel_path, i ), " säubern nicht möglich\n\n"
                    "Bei Aufruf pdf_clean_document:\n", fz_caught_message( zond->ctx ), NULL );

            continue;
        }

        path_tmp = g_strconcat( g_ptr_array_index( arr_rel_path, i ), ".tmp_clean", NULL );

        fz_try( zond->ctx ) pdf_save_document( zond->ctx, doc, path_tmp, &opts );
        fz_always( zond->ctx ) pdf_drop_document( zond->ctx, doc );
        fz_catch( zond->ctx )
        {
            g_free( path_tmp );
            meldung( zond->app_window, "PDF ", g_ptr_array_index( arr_rel_path, i ), " säubern nicht möglich\n\n"
                    "Bei Aufruf pdf_save_document:\n", fz_caught_message( zond->ctx ), NULL );

            continue;
        }

        if ( g_remove( g_ptr_array_index( arr_rel_path, i ) ) )
        {
            g_free( path_tmp );
            meldung( zond->app_window, "PDF ", g_ptr_array_index( arr_rel_path, i ), " säubern nicht möglich\n\n"
                    "Bei Aufruf g_remove (old):\n", strerror( errno ), NULL );

            continue;
        }
        if ( g_rename( path_tmp, g_ptr_array_index( arr_rel_path, i ) ) )
        {
            g_free( path_tmp );
            meldung( zond->app_window, "PDF ", g_ptr_array_index( arr_rel_path, i ), " säubern nicht möglich\n\n"
                    "Bei Aufruf g_rename (*.tmp_clean->.pdf):\n", strerror( errno ), NULL );

            continue;
        }

        g_free( path_tmp );
    }

    g_ptr_array_free( arr_rel_path, TRUE );

    return;
}


static void
cb_item_textsuche( GtkMenuItem* item, gpointer data )
{
    Projekt* zond = (Projekt*) data;

    gint rc = 0;
    gchar* errmsg = NULL;
    GArray* arr_pdf_text_occ = NULL;

    GPtrArray* arr_rel_path = selection_abfragen_pdf( zond, &errmsg );
    if ( !arr_rel_path )
    {
        if ( errmsg )
        {
            meldung( zond->app_window, "Textsuche nicht möglich\n\nBei "
                    "Aufruf selection_abfragen_pdf:\n", errmsg, NULL );
            g_free( errmsg );
        }

        return;
    }

    if ( arr_rel_path->len == 0 )
    {
        meldung( zond->app_window, "Keine PDF-Datei ausgewählt", NULL );
        g_ptr_array_free( arr_rel_path, TRUE );

        return;
    }

    gchar* search_text = NULL;
    rc = abfrage_frage( zond->app_window, "Textsuche", "Bitte Suchtext eingeben", &search_text );
    if ( rc != GTK_RESPONSE_YES )
    {
        g_ptr_array_free( arr_rel_path, TRUE );

        return;
    }
    if ( !g_strcmp0( search_text, "" ) )
    {
        g_ptr_array_free( arr_rel_path, TRUE );
        g_free( search_text );

        return;
    }

    InfoWindow* info_window = NULL;

    info_window = info_window_open( zond->app_window, "Textsuche" );

    rc = pdf_textsuche( zond, info_window, arr_rel_path, search_text, &arr_pdf_text_occ, &errmsg );
    if ( rc )
    {
        if ( rc == -1 )
        {
            meldung( zond->app_window, "Fehler in Textsuche in PDF -\n\n"
                    "Bei Aufruf pdf_textsuche:\n", errmsg, NULL );
            g_free( errmsg );
        }
        g_free( search_text );
        info_window_close( info_window );

        return;
    }

    info_window_close( info_window );

    if ( arr_pdf_text_occ->len == 0 )
    {
        meldung( zond->app_window, "Keine Treffer", NULL );
        g_ptr_array_free( arr_rel_path, TRUE );
        g_array_free( arr_pdf_text_occ, TRUE );
        g_free( search_text );

        return;
    }

    //Anzeigefenster
    rc = pdf_text_anzeigen_ergebnisse( zond, search_text, arr_rel_path,
            arr_pdf_text_occ, &errmsg );
    if ( rc )
    {
        meldung( zond->app_window, "Fehler in Textsuche in PDF -\n\n"
                "Bei Aufruf pdf_text_anzeigen_ergebnisse:\n",
                errmsg, NULL );
        g_free( errmsg );
        g_ptr_array_free( arr_rel_path, TRUE );
        g_array_free( arr_pdf_text_occ, TRUE );
        g_free( search_text );
    }

    g_ptr_array_free( arr_rel_path, TRUE );
    g_free( search_text );

    return;
}


static void
cb_datei_ocr( GtkMenuItem* item, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    InfoWindow* info_window = NULL;
    gchar* message = NULL;

    Projekt* zond = (Projekt*) data;

    GPtrArray* arr_rel_path = selection_abfragen_pdf( zond, &errmsg );
    if ( !arr_rel_path )
    {
        if ( errmsg )
        {
            meldung( zond->app_window, "Texterkennung nicht möglich\n\nBei "
                    "Aufruf selection_abfragen_pdf:\n", errmsg, NULL );
            g_free( errmsg );
        }

        return;
    }

    if ( arr_rel_path->len == 0 )
    {
        meldung( zond->app_window, "Keine PDF-Datei ausgewählt", NULL );
        g_ptr_array_unref( arr_rel_path );

        return;
    }

    //TessInit
    info_window = info_window_open( zond->app_window, "OCR" );

    for ( gint i = 0; i < arr_rel_path->len; i++ )
    {
        info_window_set_message(info_window, g_ptr_array_index( arr_rel_path, i ) );

        //prüfen, ob in Viewer geöffnet
        if ( zond_pdf_document_is_open( g_ptr_array_index( arr_rel_path, i ) ) )
        {
            meldung( info_window->dialog, "Datei in Viewer geöffnet", NULL );

            continue;
        }

        DisplayedDocument* dd = document_new_displayed_document(
                g_ptr_array_index( arr_rel_path, i ), NULL, &errmsg );
        if ( !dd )
        {
            info_window_set_message( info_window, "OCR nicht möglich - "
                    "Fehler bei Aufruf document_new_displayed_document:\n" );
            info_window_set_message( info_window, errmsg );
            g_free( errmsg );

            continue;
        }

        GPtrArray* arr_pdf_document_pages = zond_pdf_document_get_arr_pages(dd->zond_pdf_document );
        GPtrArray* arr_document_pages_ocr = g_ptr_array_sized_new( arr_pdf_document_pages->len);
        for ( gint u = 0; u < arr_pdf_document_pages->len; u++ )
        {
            g_ptr_array_add( arr_document_pages_ocr, g_ptr_array_index( arr_pdf_document_pages, u ) );
        }

        rc = pdf_ocr_pages( zond, info_window, arr_document_pages_ocr, &errmsg );
        g_ptr_array_unref( arr_document_pages_ocr );
        document_free_displayed_documents( dd );
        if ( rc == -1 )
        {
            message = g_strdup_printf( "Fehler bei Aufruf pdf_ocr_pages:\n%s", errmsg );
            g_free( errmsg );
            info_window_set_message(info_window, message );
            g_free( message );

            continue;
        }
    }

    info_window_close( info_window );

    g_ptr_array_unref( arr_rel_path );

    return;
}


/*  Callbacks des Menus "Struktur" */
static void
cb_punkt_einfuegen_activate( GtkMenuItem* item, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    gint node_id = 0;
    gint new_node_id = 0;
    ZondTreeStore* tree_store = NULL;

    Projekt* zond = (Projekt*) user_data;

    gboolean child = (gboolean) GPOINTER_TO_INT(g_object_get_data(
            G_OBJECT(item), "kind" ));

    Baum baum = baum_abfragen_aktiver_treeview( zond );

    if ( baum == KEIN_BAUM ) return;
    if ( baum == BAUM_FS )
    {
        rc = sond_treeviewfm_create_dir( SOND_TREEVIEWFM(zond->treeview[BAUM_FS]), child, &errmsg );
        if ( rc )
        {
            meldung( zond->app_window, "Verzeichnis kann nicht eingefügt werden\n\n"
                    "Bei Aufruf fm_create_dir:\n", errmsg, NULL );
            g_free( errmsg );
        }

        return;
    }
    else if ( baum == BAUM_INHALT )
    {
        node_id = baum_abfragen_aktuelle_node_id( zond->treeview[BAUM_INHALT] );
        if ( node_id == 0 ) child = TRUE;
        else
        {
            rc = hat_vorfahre_datei( zond, baum, node_id, child, &errmsg );
            if ( rc == -1 )
            {
                meldung( zond->app_window, "Einfügen nicht möglich -\n\n"
                        "Bei Aufruf hat_vorfahre_datei:\n", errmsg, NULL );
                g_free( errmsg );

                return;
            }
            else if ( rc == 1 )
            {
                meldung( zond->app_window, "Einfügen nicht möglich -\n\n"
                        "Nicht zulässig als Unterpunkt von Datei:\n", errmsg, NULL );
                g_free( errmsg );

                return;
            }
        }
    }
    else if ( baum == BAUM_AUSWERTUNG )
    {
        node_id = baum_abfragen_aktuelle_node_id( zond->treeview[BAUM_AUSWERTUNG] );
        if ( node_id == 0 ) child = TRUE;
    }

    rc = dbase_begin( (DBase*) zond->dbase_zond->dbase_work, &errmsg );
    if ( rc )
    {
        meldung( zond->app_window, "Punkt einfügen nicht möglich -\n\nBei "
                "Aufruf db_begin:\n", errmsg, NULL );

        return;
    }

    //Knoten in Datenbank einfügen
    new_node_id = dbase_full_insert_node( zond->dbase_zond->dbase_work, baum,
            node_id, child, zond->icon[ICON_NORMAL].icon_name, "Neuer Punkt", &errmsg );
    if ( new_node_id == -1 )
    {
        meldung( zond->app_window, "Punkt einfügen nicht möglich -\n\nBei Aufruf "
                "dbase_full_insert_node:\n", errmsg, NULL );
        g_free( errmsg );

        return;
    }

    rc = dbase_commit( (DBase*) zond->dbase_zond->dbase_work, &errmsg );
    if ( rc )
    {
        meldung( zond->app_window, "Punkt einfügen nicht möglich -\n\nBei "
                "Aufruf db_commit:\n", errmsg, NULL );

        return;
    }

    //Knoten in baum_inhalt einfuegen
    GtkTreeIter* iter = sond_treeview_get_cursor( zond->treeview[baum] );

    tree_store = ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[baum]) ));
    GtkTreeIter* new_iter = zond_tree_store_insert_node( tree_store, iter, child );

    if ( child && iter ) sond_treeview_expand_row( zond->treeview[baum], iter );
    if ( iter ) gtk_tree_iter_free( iter );

    //Standardinhalt setzen
    zond_tree_store_set( ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[baum]) )),
            new_iter, zond->icon[ICON_NORMAL].icon_name, "Neuer Punkt", new_node_id );

    sond_treeview_set_cursor( zond->treeview[baum], new_iter );

    gtk_tree_iter_free( new_iter );

    return;
}


//Knoten-Text anpassen
static void
cb_item_text_anbindung( GtkMenuItem* item, gpointer data )
{
    Projekt* zond = (Projekt*) data;

    Baum baum = KEIN_BAUM;
    gint rc = 0;
    gchar* errmsg = NULL;
    GList* selected = NULL;
    GList* list = NULL;
    gint node_id = 0;
    gchar* node_text = NULL;
    GtkTreeIter* iter = NULL;
    gchar* rel_path = NULL;
    Anbindung* anbindung = NULL;

    baum = baum_abfragen_aktiver_treeview( zond );

    if ( baum == BAUM_FS ) return;

    selected = gtk_tree_selection_get_selected_rows( zond->selection[baum], NULL );
    if( !selected ) return;

    list = selected;
    do //alle rows aus der Liste
    {
        node_id = baum_abfragen_node_id( zond->treeview[baum], list->data,
                &errmsg );
        if ( node_id == -1 )
        {
            meldung( zond->app_window, "Fehler in Text anpassen (Anbindung):\n\n"
                    "Bei Aufruf baum_abfragen_node_id:\n", errmsg, NULL );
            g_free( errmsg );
            g_list_free_full( selected, (GDestroyNotify) gtk_tree_path_free );

            return;
        }

        rc = abfragen_rel_path_and_anbindung( zond, baum, node_id, &rel_path,
                &anbindung, &errmsg );
        if ( rc == -1 )
        {
            meldung( zond->app_window, "Fehler in Text anpassen (Anbindung):\n\n"
                    "Bei Aufruf abfragen_rel_path_and_anbindung:\n", errmsg, NULL );
            g_free( errmsg );
            g_list_free_full( selected, (GDestroyNotify) gtk_tree_path_free );

            return;
        }

        if ( rc == 2 ) continue;
        else if ( rc == 0 )
        {
            node_text = g_strdup_printf( "%s, S. %i (%i) - S. %i (%i)", rel_path,
                    anbindung->von.seite, anbindung->von.index, anbindung->bis.seite,
                    anbindung->bis.index );

            g_free( anbindung );
        }
        else node_text = g_strdup_printf( "%s", rel_path );

        g_free( rel_path );

        rc = dbase_full_set_node_text( zond->dbase_zond->dbase_work, baum, node_id, node_text, &errmsg );
        if ( rc )
        {
            meldung( zond->app_window, "Fehler in Text anpassen (Anbindung):\n\n"
                    "Bei Aufruf dbase_full_set_node_text:\n", errmsg, NULL );
            g_free( errmsg );
            g_free( node_text );
            g_list_free_full( selected, (GDestroyNotify) gtk_tree_path_free );

            return;
        }

        iter = baum_abfragen_iter( zond->treeview[baum], node_id );
        //neuen text im tree speichern
        zond_tree_store_set( ZOND_TREE_STORE(gtk_tree_view_get_model(
                GTK_TREE_VIEW(zond->treeview[baum]) )), iter, NULL, node_text, 0 );
        gtk_tree_iter_free( iter );
        g_free( node_text );
    }
    while ( (list = list->next) );

    g_list_free_full( selected, (GDestroyNotify) gtk_tree_path_free );

    return;
}


static void
cb_change_icon_item( GtkMenuItem* item, gpointer data )
{
    Projekt* zond = (Projekt*) data;

    gint icon_id = 0;

    icon_id = GPOINTER_TO_INT(g_object_get_data( G_OBJECT(item),
            "icon-id" ));

    selection_change_icon_id( zond, zond->icon[icon_id].icon_name );

    return;
}


static void
cb_eingang_activate( GtkMenuItem* item, gpointer data )
{
    Projekt* zond = (Projekt*) data;

    Baum baum = baum_abfragen_aktiver_treeview( zond );
    if ( baum == KEIN_BAUM ) return;
    else if ( baum == BAUM_FS )
    {
        gint rc = 0;
        gchar* errmsg = NULL;

        rc = eingang_set( SOND_TREEVIEWFM(zond->treeview[BAUM_FS]), &errmsg );
        if ( rc )
        {
            meldung( zond->app_window, "Eingang ändern fehlgeschlagen -\n\nBei Aufruf "
                    "fm_set_eingang:\n", errmsg, NULL );
            g_free( errmsg );
        }
    }
    else
    {

    }

    return;
}


static void
cb_kopieren_activate( GtkMenuItem* item, gpointer user_data )
{
    Projekt* zond = (Projekt*) user_data;

    Baum baum = baum_abfragen_aktiver_treeview( zond );
    if ( baum == KEIN_BAUM ) return;

    sond_treeview_copy_or_cut_selection( zond->treeview[baum], FALSE );

    return;
}


static void
cb_ausschneiden_activate( GtkMenuItem* item, gpointer user_data )
{
    Projekt* zond = (Projekt*) user_data;

    Baum baum = baum_abfragen_aktiver_treeview( zond );
    if ( baum == KEIN_BAUM ) return;

    sond_treeview_copy_or_cut_selection( zond->treeview[baum], TRUE );

    return;
}


static void
cb_clipboard_einfuegen_activate( GtkMenuItem* item, gpointer user_data )
{
    Projekt* zond = (Projekt*) user_data;

    gboolean kind = (gboolean) GPOINTER_TO_INT(g_object_get_data( G_OBJECT(item),
            "kind" ));

    selection_paste( zond, kind, FALSE );

    return;
}


static void
cb_loeschen_activate( GtkMenuItem* item, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    Projekt* zond = (Projekt*) user_data;
 //   static SFMForeachLoeschen s_fm_foreach_loeschen = {};

    Baum baum = baum_abfragen_aktiver_treeview( zond );
    if ( baum == KEIN_BAUM ) return;

    rc = selection_loeschen( zond, baum, &errmsg );
    if ( rc )
    {
        meldung( zond->app_window, "Löschen fehlgeschlagen -\n\nBei Aufruf "
                "selection_loeschen:\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


static void
cb_anbindung_entfernenitem_activate( GtkMenuItem* item, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    Baum baum = KEIN_BAUM;

    Projekt* zond = (Projekt*) user_data;

    baum = baum_abfragen_aktiver_treeview( zond );
    if ( baum != BAUM_INHALT )
    {
        meldung( zond->app_window, "Anbindungen können nur im Inhalts-Baum "
                "entfernt werden", NULL );
        return;
    }

    rc = selection_entfernen_anbindung( zond, &errmsg );
    if ( rc )
    {
        meldung( zond->app_window, "Fehler bei löschen von Anbindungen - \n\n",
                errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


static void
cb_suchen_text( GtkMenuItem* item, gpointer data )
{
    Projekt* zond = (Projekt*) data;

    gtk_popover_popup( GTK_POPOVER(zond->popover) );

    return;
}


/*  Callbacks des Menus "Ansicht" */

static void
cb_alle_erweitern_activated( GtkMenuItem* item, gpointer zond )
{
    gtk_tree_view_expand_all( GTK_TREE_VIEW(((Projekt*) zond)->treeview[baum_abfragen_aktiver_treeview(
            (Projekt*) zond )]) );

    return;
}


static void
cb_aktueller_zweig_erweitern_activated( GtkMenuItem* item, gpointer zond )
{
    GtkTreePath *path;

    gtk_tree_view_get_cursor( GTK_TREE_VIEW(((Projekt*) zond)->treeview[baum_abfragen_aktiver_treeview(
            (Projekt*) zond )]), &path, NULL );
    gtk_tree_view_expand_row( GTK_TREE_VIEW(((Projekt*) zond)->treeview[baum_abfragen_aktiver_treeview(
            (Projekt*) zond )]), path, TRUE );

    gtk_tree_path_free(path);

    return;
}


static void
cb_reduzieren_activated( GtkMenuItem* item, gpointer zond )
{
    gtk_tree_view_collapse_all( GTK_TREE_VIEW(((Projekt*) zond)->treeview[baum_abfragen_aktiver_treeview(
            (Projekt*) zond )]) );

    return;
}


static void
cb_refresh_view_activated( GtkMenuItem* item, gpointer zond )
{
    db_baum_refresh( (Projekt*) zond, NULL );

    return;
}


/*  Callbacks des Menus Extras */
static void
cb_menu_test_activate( GtkMenuItem* item, gpointer zond )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    rc = test( (Projekt*) zond, &errmsg );
    if ( rc )
    {
        meldung( ((Projekt*) zond)->app_window, "Test:\n\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


static void
cb_menu_convert_activate( GtkMenuItem* item, gpointer data )
{/*
    Projekt* zond = (Projekt*) data;

    gint rc = 0;
    gchar* errmsg = NULL;

    rc = convert_09_to_1( zond, &errmsg );
    if ( rc )
    {
        meldung( zond->app_window, "Konvertieren nicht möglich -\n\nBei "
                "Aufruf convert_09_to_1:\n", errmsg, NULL );
        g_free( errmsg );
    }
*/
    return;
}


static void
cb_menu_addeingang_activate( GtkMenuItem* item, gpointer data )
{/*
    Projekt* zond = (Projekt*) data;

    gint rc = 0;
    gchar* errmsg = NULL;

    rc = convert_addeingang( zond, &errmsg );
    if ( rc )
    {
        meldung( zond->app_window, "Unterstützung Eingang hinzufügen nicht möglich-\n\nBei "
                "Aufruf convert_addeingang:\n", errmsg, NULL );
        g_free( errmsg );
    }
*/
    return;
}


/*  Callbacks des Menus Einstellungen */
static void
cb_settings_zoom( GtkMenuItem* item, gpointer data )
{
    gint rc = 0;
    Projekt* zond = (Projekt*) data;

    gchar* text = g_strdup_printf( "%.0f", g_settings_get_double( zond->settings, "zoom" ) );
    rc = abfrage_frage( zond->app_window, "Zoom:", "Faktor eingeben", &text );
    if ( !g_strcmp0( text, "" ) )
    {
        g_free( text );

        return;
    }

    guint zoom = 0;
    rc = string_to_guint( text, &zoom );
    if ( rc == 0 && zoom >= ZOOM_MIN && zoom <= ZOOM_MAX )
            g_settings_set_double( zond->settings, "zoom", (gdouble) zoom );
    else meldung( zond->app_window, "Eingabe nicht gültig", NULL );

    g_free( text );

    return;
}


/*  Funktion init_menu - ganze Kopfzeile! */
static GtkWidget*
init_menu( Projekt* zond )
{
    GtkAccelGroup* accel_group = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(zond->app_window), accel_group);

/*  Menubar */
    GtkWidget* menubar = gtk_menu_bar_new();

    zond->menu.projekt = gtk_menu_item_new_with_label ( "Projekt" );
    zond->menu.pdf = gtk_menu_item_new_with_label("PDF-Dateien");
    zond->menu.struktur = gtk_menu_item_new_with_label( "Struktur" );
    zond->menu.ansicht = gtk_menu_item_new_with_label("Ansicht");
    zond->menu.extras = gtk_menu_item_new_with_label( "Extras" );
    GtkWidget* einstellungen = gtk_menu_item_new_with_label(
            "Einstellungen" );
    GtkWidget* hilfeitem = gtk_menu_item_new_with_label( "Hilfe" );

    //In die Menuleiste einfügen
    gtk_menu_shell_append ( GTK_MENU_SHELL(menubar), zond->menu.projekt );
    gtk_menu_shell_append ( GTK_MENU_SHELL(menubar), zond->menu.struktur );
    gtk_menu_shell_append ( GTK_MENU_SHELL(menubar), zond->menu.pdf );
    gtk_menu_shell_append ( GTK_MENU_SHELL(menubar), zond->menu.ansicht );
    gtk_menu_shell_append ( GTK_MENU_SHELL(menubar), zond->menu.extras );
    gtk_menu_shell_append ( GTK_MENU_SHELL(menubar), einstellungen );
    gtk_menu_shell_append ( GTK_MENU_SHELL(menubar), hilfeitem );

/*********************
*  Menu Projekt
*********************/
    GtkWidget* projektmenu = gtk_menu_new();

    GtkWidget* neuitem = gtk_menu_item_new_with_label ("Neu");
    g_signal_connect(G_OBJECT(neuitem), "activate", G_CALLBACK(cb_menu_datei_neu_activate), (gpointer) zond);

    GtkWidget* oeffnenitem = gtk_menu_item_new_with_label ("Öffnen");
    g_signal_connect( G_OBJECT(oeffnenitem), "activate",
            G_CALLBACK(cb_menu_datei_oeffnen_activate), (gpointer) zond );

    zond->menu.speichernitem = gtk_menu_item_new_with_label ("Speichern");
    g_signal_connect( G_OBJECT(zond->menu.speichernitem), "activate",
            G_CALLBACK(cb_menu_datei_speichern_activate), (gpointer) zond );

    zond->menu.schliessenitem = gtk_menu_item_new_with_label ("Schliessen");
    g_signal_connect( G_OBJECT(zond->menu.schliessenitem), "activate",
            G_CALLBACK(cb_menu_datei_schliessen_activate), (gpointer) zond );

    GtkWidget* sep_projekt1item = gtk_separator_menu_item_new();

    zond->menu.exportitem = gtk_menu_item_new_with_label( "Export als odt-Dokument" );
    GtkWidget* exportmenu = gtk_menu_new( );
    GtkWidget* ganze_struktur = gtk_menu_item_new_with_label( "Ganze Struktur" );
    g_object_set_data( G_OBJECT(ganze_struktur), "umfang", GINT_TO_POINTER(1) );
    g_signal_connect( ganze_struktur, "activate", G_CALLBACK(cb_menu_datei_export_activate), (gpointer) zond );
    GtkWidget* ausgewaehlte_punkte = gtk_menu_item_new_with_label( "Gewählte Zweige" );
    g_object_set_data( G_OBJECT(ausgewaehlte_punkte), "umfang", GINT_TO_POINTER(2) );
    g_signal_connect( ausgewaehlte_punkte, "activate", G_CALLBACK(cb_menu_datei_export_activate), (gpointer) zond );
    GtkWidget* ausgewaehlte_zweige = gtk_menu_item_new_with_label( "Gewählte Punkte" );
    g_object_set_data( G_OBJECT(ausgewaehlte_zweige), "umfang", GINT_TO_POINTER(3) );
    g_signal_connect( ausgewaehlte_zweige, "activate", G_CALLBACK(cb_menu_datei_export_activate), (gpointer) zond );
    gtk_menu_shell_append( GTK_MENU_SHELL(exportmenu), ganze_struktur );
    gtk_menu_shell_append( GTK_MENU_SHELL(exportmenu), ausgewaehlte_punkte );
    gtk_menu_shell_append( GTK_MENU_SHELL(exportmenu), ausgewaehlte_zweige );
    gtk_menu_item_set_submenu( GTK_MENU_ITEM(zond->menu.exportitem), exportmenu );

    GtkWidget* sep_projekt1item_2 = gtk_separator_menu_item_new();

    GtkWidget* beendenitem = gtk_menu_item_new_with_label ("Beenden");
    gtk_widget_add_accelerator(beendenitem, "activate", accel_group, GDK_KEY_q,
            GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    g_signal_connect_swapped(beendenitem, "activate",
            G_CALLBACK(cb_menu_datei_beenden_activate), (gpointer) zond );

    gtk_menu_shell_append ( GTK_MENU_SHELL(projektmenu), neuitem );
    gtk_menu_shell_append ( GTK_MENU_SHELL(projektmenu), oeffnenitem );
    gtk_menu_shell_append ( GTK_MENU_SHELL(projektmenu), zond->menu.speichernitem );
    gtk_menu_shell_append ( GTK_MENU_SHELL(projektmenu), zond->menu.schliessenitem );
    gtk_menu_shell_append ( GTK_MENU_SHELL(projektmenu), sep_projekt1item );
    gtk_menu_shell_append ( GTK_MENU_SHELL(projektmenu), zond->menu.exportitem );
    gtk_menu_shell_append ( GTK_MENU_SHELL(projektmenu), sep_projekt1item_2);
    gtk_menu_shell_append ( GTK_MENU_SHELL(projektmenu), beendenitem );

/*********************
*  Menu Struktur
*********************/
    GtkWidget* strukturmenu = gtk_menu_new();

    //Punkt erzeugen
    GtkWidget* punkterzeugenitem = gtk_menu_item_new_with_label(
            "Punkt einfügen" );

    GtkWidget* punkterzeugenmenu = gtk_menu_new();

    GtkWidget* ge_punkterzeugenitem = gtk_menu_item_new_with_label(
            "Gleiche Ebene" );
    gtk_widget_add_accelerator( ge_punkterzeugenitem, "activate", accel_group,
            GDK_KEY_p, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    g_signal_connect( G_OBJECT(ge_punkterzeugenitem), "activate",
            G_CALLBACK(cb_punkt_einfuegen_activate), (gpointer) zond );

    GtkWidget* up_punkterzeugenitem = gtk_menu_item_new_with_label(
            "Unterebene" );
    g_object_set_data( G_OBJECT(up_punkterzeugenitem), "kind", GINT_TO_POINTER(1) );
    gtk_widget_add_accelerator(up_punkterzeugenitem, "activate", accel_group,
            GDK_KEY_p, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);
    g_signal_connect( G_OBJECT(up_punkterzeugenitem), "activate",
            G_CALLBACK(cb_punkt_einfuegen_activate), (gpointer) zond );

    gtk_menu_shell_append( GTK_MENU_SHELL(punkterzeugenmenu),
            ge_punkterzeugenitem );
    gtk_menu_shell_append( GTK_MENU_SHELL(punkterzeugenmenu),
            up_punkterzeugenitem );

    gtk_menu_item_set_submenu( GTK_MENU_ITEM(punkterzeugenitem),
            punkterzeugenmenu );

    //node_text anpassen (rel_path/Anbindung)
    GtkWidget* item_text_anbindung = gtk_menu_item_new_with_label(
            "Text von Anbindung" );
    gtk_widget_add_accelerator(item_text_anbindung, "activate", accel_group,
            GDK_KEY_T, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    g_signal_connect( G_OBJECT(item_text_anbindung), "activate",
            G_CALLBACK(cb_item_text_anbindung), (gpointer) zond );

    //Icons ändern
    GtkWidget* icon_change_item = gtk_menu_item_new_with_label( "Icons" );

    GtkWidget* icon_change_menu = gtk_menu_new( );

    gint i = 0;
    for ( i = 0; i < NUMBER_OF_ICONS; i++ )
    {
        GtkWidget *icon = gtk_image_new_from_icon_name( zond->icon[i].icon_name, GTK_ICON_SIZE_MENU );
        GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget *label = gtk_label_new ( zond->icon[i].display_name );
        GtkWidget *menu_item = gtk_menu_item_new ( );
        gtk_container_add (GTK_CONTAINER (box), icon);
        gtk_container_add (GTK_CONTAINER (box), label);
        gtk_container_add (GTK_CONTAINER (menu_item), box);

        g_object_set_data( G_OBJECT(menu_item), "icon-id",
                GINT_TO_POINTER(i) );
        g_signal_connect( menu_item, "activate",
                G_CALLBACK(cb_change_icon_item), (gpointer) zond );

        gtk_menu_shell_append( GTK_MENU_SHELL(icon_change_menu), menu_item );
    }

    gtk_menu_item_set_submenu( GTK_MENU_ITEM(icon_change_item),
            icon_change_menu );

    GtkWidget* eingang_item = gtk_menu_item_new_with_label( "Eingangsdaten setzen" );
    gtk_widget_add_accelerator(eingang_item, "activate", accel_group, GDK_KEY_e,
            GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    g_signal_connect( G_OBJECT(eingang_item), "activate",
            G_CALLBACK(cb_eingang_activate), (gpointer) zond );


    GtkWidget* sep_struktur0item = gtk_separator_menu_item_new();

    //Kopieren
    GtkWidget* kopierenitem = gtk_menu_item_new_with_label("Kopieren");
    gtk_widget_add_accelerator(kopierenitem, "activate", accel_group, GDK_KEY_c,
            GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    g_signal_connect( G_OBJECT(kopierenitem), "activate",
            G_CALLBACK(cb_kopieren_activate), (gpointer) zond );

    //Verschieben
    GtkWidget* ausschneidenitem = gtk_menu_item_new_with_label("Ausschneiden");
    gtk_widget_add_accelerator( ausschneidenitem, "activate", accel_group,
            GDK_KEY_x, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE );
    g_object_set_data( G_OBJECT(ausschneidenitem), "ausschneiden",
            GINT_TO_POINTER(1) );
    g_signal_connect( G_OBJECT(ausschneidenitem), "activate",
            G_CALLBACK(cb_ausschneiden_activate), (gpointer) zond );

    //Einfügen
    GtkWidget* pasteitem = gtk_menu_item_new_with_label("Einfügen");
    GtkWidget* pastemenu = gtk_menu_new();
    GtkWidget* alspunkt_einfuegenitem = gtk_menu_item_new_with_label(
            "Gleiche Ebene");
    GtkWidget* alsunterpunkt_einfuegenitem = gtk_menu_item_new_with_label(
            "Unterebene");
    gtk_menu_shell_append(GTK_MENU_SHELL(pastemenu), alspunkt_einfuegenitem);
    gtk_widget_add_accelerator(alspunkt_einfuegenitem, "activate", accel_group,
            GDK_KEY_v, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_menu_shell_append(GTK_MENU_SHELL(pastemenu),
            alsunterpunkt_einfuegenitem);
    g_object_set_data( G_OBJECT(alsunterpunkt_einfuegenitem), "kind",
            GINT_TO_POINTER(1) );

    //Link Einfügen
    GtkWidget* pasteitem_link = gtk_menu_item_new_with_label("Link");
    GtkWidget* pastemenu_link = gtk_menu_new();
    GtkWidget* alspunkt_einfuegenitem_link = gtk_menu_item_new_with_label(
            "Gleiche Ebene");
    GtkWidget* alsunterpunkt_einfuegenitem_link = gtk_menu_item_new_with_label(
            "Unterebene");
    gtk_menu_shell_append(GTK_MENU_SHELL(pastemenu_link), alspunkt_einfuegenitem_link);
    gtk_widget_add_accelerator(alspunkt_einfuegenitem_link, "activate", accel_group,
            GDK_KEY_v, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_menu_shell_append(GTK_MENU_SHELL(pastemenu_link),
            alsunterpunkt_einfuegenitem_link);
    g_object_set_data( G_OBJECT(alsunterpunkt_einfuegenitem_link), "kind",
            GINT_TO_POINTER(1) );
    g_object_set_data( G_OBJECT(alsunterpunkt_einfuegenitem_link), "link",
            GINT_TO_POINTER(1) );
    g_object_set_data( G_OBJECT(alspunkt_einfuegenitem_link), "link",
            GINT_TO_POINTER(1) );

    GtkWidget* sep_struktur1item = gtk_separator_menu_item_new();

    gtk_widget_add_accelerator(alsunterpunkt_einfuegenitem, "activate",
            accel_group, GDK_KEY_v, GDK_CONTROL_MASK | GDK_SHIFT_MASK,
            GTK_ACCEL_VISIBLE);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(pasteitem), pastemenu);

    g_signal_connect( G_OBJECT(alspunkt_einfuegenitem), "activate",
            G_CALLBACK(cb_clipboard_einfuegen_activate), (gpointer) zond );
    g_signal_connect( G_OBJECT(alsunterpunkt_einfuegenitem), "activate",
            G_CALLBACK(cb_clipboard_einfuegen_activate),
            (gpointer) zond );

    g_signal_connect( G_OBJECT(alspunkt_einfuegenitem_link), "activate",
            G_CALLBACK(cb_clipboard_einfuegen_activate), (gpointer) zond );
    g_signal_connect( G_OBJECT(alsunterpunkt_einfuegenitem_link), "activate",
            G_CALLBACK(cb_clipboard_einfuegen_activate),
            (gpointer) zond );

    GtkWidget* sep_struktur2item = gtk_separator_menu_item_new();

    //Punkt(e) löschen
    GtkWidget* loeschenitem = gtk_menu_item_new_with_label("Punkte löschen");
    gtk_widget_add_accelerator(loeschenitem, "activate", accel_group,
            GDK_KEY_Delete, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    g_signal_connect( G_OBJECT(loeschenitem), "activate",
            G_CALLBACK(cb_loeschen_activate), (gpointer) zond );

    //Speichern als Projektdatei
    GtkWidget* anbindung_entfernenitem = gtk_menu_item_new_with_label(
            "Anbindung entfernen");
    g_signal_connect( G_OBJECT(anbindung_entfernenitem), "activate",
            G_CALLBACK(cb_anbindung_entfernenitem_activate), zond );

    GtkWidget* suchenitem = gtk_menu_item_new_with_label(
            "Suchen");
    GtkWidget* sep_struktur3item = gtk_separator_menu_item_new();
    g_signal_connect( suchenitem, "activate", G_CALLBACK(cb_suchen_text), zond );

    //Menus "Bearbeiten" anbinden
//Menus in dateienmenu
    gtk_menu_shell_append( GTK_MENU_SHELL(strukturmenu), punkterzeugenitem );
    gtk_menu_shell_append( GTK_MENU_SHELL(strukturmenu), sep_struktur0item );
    gtk_menu_shell_append( GTK_MENU_SHELL(strukturmenu), kopierenitem );
    gtk_menu_shell_append( GTK_MENU_SHELL(strukturmenu), ausschneidenitem );
    gtk_menu_shell_append( GTK_MENU_SHELL(strukturmenu), pasteitem );
    gtk_menu_shell_append( GTK_MENU_SHELL(strukturmenu), pasteitem_link );
    gtk_menu_shell_append( GTK_MENU_SHELL(strukturmenu), sep_struktur1item );
    gtk_menu_shell_append( GTK_MENU_SHELL(strukturmenu), loeschenitem );
    gtk_menu_shell_append( GTK_MENU_SHELL(strukturmenu), anbindung_entfernenitem );
    gtk_menu_shell_append( GTK_MENU_SHELL(strukturmenu), sep_struktur2item );
    gtk_menu_shell_append( GTK_MENU_SHELL(strukturmenu), suchenitem );
    gtk_menu_shell_append( GTK_MENU_SHELL(strukturmenu), sep_struktur3item );
    gtk_menu_shell_append( GTK_MENU_SHELL(strukturmenu), item_text_anbindung );
    gtk_menu_shell_append( GTK_MENU_SHELL(strukturmenu), icon_change_item );
    gtk_menu_shell_append( GTK_MENU_SHELL(strukturmenu), eingang_item );

/*********************
*  Menu Pdf-Dateien
*********************/
    GtkWidget* menu_dateien = gtk_menu_new();

    GtkWidget* item_sep_dateien0 = gtk_separator_menu_item_new( );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_dateien), item_sep_dateien0 );

    //PDF reparieren
    GtkWidget* item_clean_pdf = gtk_menu_item_new_with_label( "PDF reparieren" );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_dateien), item_clean_pdf );
    g_signal_connect( item_clean_pdf, "activate", G_CALLBACK(cb_item_clean_pdf), zond );

    //Text-Suche
    GtkWidget* item_textsuche = gtk_menu_item_new_with_label( "Text suchen" );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_dateien), item_textsuche);
    g_signal_connect( item_textsuche, "activate", G_CALLBACK(cb_item_textsuche), zond );

    GtkWidget* item_ocr = gtk_menu_item_new_with_label( "OCR" );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_dateien), item_ocr );
    g_signal_connect( item_ocr, "activate", G_CALLBACK(cb_datei_ocr), zond );

/*  Menu Ansicht */
    GtkWidget* ansichtmenu = gtk_menu_new();

    //Erweitern
    GtkWidget* erweiternitem = gtk_menu_item_new_with_label ("Erweitern");

    GtkWidget* erweiternmenu = gtk_menu_new();
    GtkWidget* alle_erweiternitem = gtk_menu_item_new_with_label("Ganze Struktur");
    GtkWidget* aktuellerzweig_erweiternitem = gtk_menu_item_new_with_label("Aktueller Zweig");
    gtk_menu_shell_append(GTK_MENU_SHELL(erweiternmenu), alle_erweiternitem);
    gtk_menu_shell_append(GTK_MENU_SHELL(erweiternmenu), aktuellerzweig_erweiternitem);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(erweiternitem), erweiternmenu);

    g_signal_connect( G_OBJECT(alle_erweiternitem), "activate", G_CALLBACK(cb_alle_erweitern_activated), (gpointer) zond );
    g_signal_connect( G_OBJECT(aktuellerzweig_erweiternitem), "activate", G_CALLBACK(cb_aktueller_zweig_erweitern_activated), (gpointer) zond );

    //Alle reduzieren
    GtkWidget* einklappenitem = gtk_menu_item_new_with_label ("Alle reduzieren");
    g_signal_connect( einklappenitem, "activate", G_CALLBACK(cb_reduzieren_activated), (gpointer) zond );

    GtkWidget* sep_ansicht1item = gtk_separator_menu_item_new();

    //refresh view
    GtkWidget* refreshitem = gtk_menu_item_new_with_label ("Refresh");
    g_signal_connect( refreshitem, "activate", G_CALLBACK(cb_refresh_view_activated), (gpointer) zond );

    gtk_menu_shell_append( GTK_MENU_SHELL(ansichtmenu), erweiternitem);
    gtk_menu_shell_append( GTK_MENU_SHELL(ansichtmenu), einklappenitem);
    gtk_menu_shell_append( GTK_MENU_SHELL(ansichtmenu), sep_ansicht1item);
    gtk_menu_shell_append( GTK_MENU_SHELL(ansichtmenu), refreshitem);

/*  Menu Extras */
    GtkWidget* extrasmenu = gtk_menu_new( );

    //Test
    GtkWidget* testitem = gtk_menu_item_new_with_label ("Test");
    g_signal_connect( testitem, "activate", G_CALLBACK(cb_menu_test_activate), (gpointer) zond );

    //convert
    GtkWidget* convertitem = gtk_menu_item_new_with_label( "Konvertieren .9->1.0" );
    g_signal_connect( convertitem, "activate", G_CALLBACK(cb_menu_convert_activate), (gpointer) zond );

    GtkWidget* addeingangitem = gtk_menu_item_new_with_label( "Konvertieren ->eingang" );
    g_signal_connect( addeingangitem , "activate", G_CALLBACK(cb_menu_addeingang_activate), (gpointer) zond );

    gtk_menu_shell_append( GTK_MENU_SHELL(extrasmenu), testitem);
    gtk_menu_shell_append( GTK_MENU_SHELL(extrasmenu), convertitem);
    gtk_menu_shell_append( GTK_MENU_SHELL(extrasmenu), addeingangitem );

/*  Menu Einstellungen */
    GtkWidget* einstellungenmenu = gtk_menu_new( );

    zond->menu.internal_vieweritem = gtk_check_menu_item_new_with_label(
            "Interner PDF-Viewer" );

    GtkWidget* zoom_item = gtk_menu_item_new_with_label( "Zoom Interner Viewer" );

    GtkWidget* root_item = gtk_menu_item_new_with_label( "root-Verzeichnis" );

    gtk_menu_shell_append( GTK_MENU_SHELL(einstellungenmenu),
            zond->menu.internal_vieweritem );
    gtk_menu_shell_append( GTK_MENU_SHELL(einstellungenmenu),
            zoom_item );
    gtk_menu_shell_append( GTK_MENU_SHELL(einstellungenmenu),
            root_item );

    g_signal_connect( zoom_item, "activate", G_CALLBACK(cb_settings_zoom), zond );

/*  Gesamtmenu:
*   Die erzeugten Menus als Untermenu der Menuitems aus der menubar
*/
    // An menu aus menubar anbinden
    gtk_menu_item_set_submenu( GTK_MENU_ITEM(zond->menu.projekt), projektmenu );
    gtk_menu_item_set_submenu( GTK_MENU_ITEM(zond->menu.pdf), menu_dateien );
    gtk_menu_item_set_submenu( GTK_MENU_ITEM(zond->menu.struktur), strukturmenu );
    gtk_menu_item_set_submenu( GTK_MENU_ITEM(zond->menu.ansicht), ansichtmenu );
    gtk_menu_item_set_submenu( GTK_MENU_ITEM(zond->menu.extras), extrasmenu );
    gtk_menu_item_set_submenu( GTK_MENU_ITEM(einstellungen),
            einstellungenmenu );

    return menubar;
}


static void
cb_button_mode_toggled( GtkToggleButton* button, gpointer data )
{
    Projekt* zond = (Projekt*) data;

    if ( gtk_toggle_button_get_active( button ) )
    {
        gint rc = 0;
        gchar* errmsg = NULL;

        GtkWidget* baum_fs = NULL;
        GtkWidget* baum_auswertung = NULL;

        baum_fs = gtk_paned_get_child1( GTK_PANED(zond->hpaned) );
        gtk_widget_show_all( baum_fs );

        baum_auswertung = gtk_paned_get_child2( GTK_PANED(gtk_paned_get_child2( GTK_PANED(zond->hpaned) )) );
        gtk_widget_hide( baum_auswertung );

        rc = sond_treeviewfm_set_root( SOND_TREEVIEWFM(zond->treeview[BAUM_FS]),
                zond->dbase_zond->project_dir, &errmsg );
        if ( rc )
        {
            meldung( zond->app_window, "Fehler beim Laden Root-Verzeichnis -\n\n",
                    "Bei Aufruf fm_set_root:\n", errmsg, NULL );
            g_free( errmsg );
        }
    }
    else
    {
        //baum_inhalt und baum_auswertung anzeigen
        //zwischenspeichern
        //leeren
        GtkWidget* baum_fs = NULL;
        GtkWidget* baum_auswertung = NULL;

        baum_fs = gtk_paned_get_child1( GTK_PANED(zond->hpaned) );
        gtk_widget_hide( baum_fs );

        baum_auswertung = gtk_paned_get_child2( GTK_PANED(gtk_paned_get_child2( GTK_PANED(zond->hpaned) )) );
        gtk_widget_show_all( baum_auswertung );

        sond_treeviewfm_set_root( SOND_TREEVIEWFM(zond->treeview[BAUM_FS]), NULL, NULL );
    }

    return;
}


void
init_headerbar ( Projekt* zond )
{
    //Menu erzeugen
    GtkWidget* menubar = init_menu( zond );

    //HeaderBar erzeugen
    GtkWidget* headerbar = gtk_header_bar_new();
    gtk_header_bar_set_has_subtitle(GTK_HEADER_BAR(headerbar), FALSE);
    gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(headerbar), ":minimize,maximize,close");
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);

    //Umschaltknopf erzeugen
    zond->fs_button = gtk_toggle_button_new_with_label( "FS" );
    g_signal_connect( zond->fs_button, "toggled", G_CALLBACK(cb_button_mode_toggled), zond );
    gtk_header_bar_pack_start( GTK_HEADER_BAR(headerbar), zond->fs_button );

    //alles in Headerbar packen
    gtk_header_bar_pack_start( GTK_HEADER_BAR(headerbar), menubar );

    //HeaderBar anzeigen
    gtk_window_set_titlebar( GTK_WINDOW(zond->app_window), headerbar );

    return;
}
