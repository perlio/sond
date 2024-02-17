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

#include "treeviews.h"
#include "project.h"

#include "../global_types.h"
#include "../zond_tree_store.h"
#include "../zond_dbase.h"
#include "../zond_treeview.h"
#include "../zond_pdf_document.h"

#include "../99conv/pdf.h"

#include "../../misc.h"


Baum
treeviews_get_baum_iter( Projekt* zond, GtkTreeIter* iter )
{
    ZondTreeStore* tree_store = NULL;

    tree_store = zond_tree_store_get_tree_store( iter );

    if ( tree_store == ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[BAUM_INHALT]) )) ) return BAUM_INHALT;
    else if ( tree_store == ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[BAUM_AUSWERTUNG]) )) ) return BAUM_AUSWERTUNG;

    return KEIN_BAUM;
}


gint
treeviews_get_baum_and_node_id( Projekt* zond, GtkTreeIter* iter, Baum* baum,
        gint* node_id )
{
    Baum baum_target = KEIN_BAUM;
    GtkTreeIter iter_target = { 0, };

    zond_tree_store_get_iter_target( iter, &iter_target );
    baum_target = treeviews_get_baum_iter( zond, &iter_target );

    if ( baum_target == KEIN_BAUM ) return 1;
    else if ( baum ) *baum = baum_target;

    gtk_tree_model_get( GTK_TREE_MODEL(zond_tree_store_get_tree_store( &iter_target )),
            &iter_target, 2, node_id, -1 );

    return 0;
}


static gint
treeviews_get_page_num_from_dest_doc( fz_context* ctx, pdf_document* doc, const gchar* dest, gchar** errmsg )
{
    pdf_obj* obj_dest_string = NULL;
    pdf_obj* obj_dest = NULL;
    pdf_obj* pageobj = NULL;
    gint page_num = 0;

    obj_dest_string = pdf_new_string( ctx, dest, strlen( dest ) );
    fz_try( ctx ) obj_dest = pdf_lookup_dest( ctx, doc, obj_dest_string);
    fz_always( ctx ) pdf_drop_obj( ctx, obj_dest_string );
    fz_catch( ctx ) ERROR_MUPDF( "pdf_lookup_dest" )

	pageobj = pdf_array_get( ctx, obj_dest, 0 );

	if ( pdf_is_int( ctx, pageobj ) ) page_num = pdf_to_int( ctx, pageobj );
	else
	{
		fz_try( ctx ) page_num = pdf_lookup_page_number( ctx, doc, pageobj );
		fz_catch( ctx ) ERROR_MUPDF( "pdf_lookup_page_number" )
	}

    return page_num;
}


static gint
treeviews_get_page_num_from_dest( fz_context* ctx, const gchar* rel_path,
        const gchar* dest, gchar** errmsg )
{
    pdf_document* doc = NULL;
    gint page_num = 0;

    fz_try( ctx ) doc = pdf_open_document( ctx, rel_path );
    fz_catch( ctx ) ERROR_MUPDF( "fz_open_document" )

    page_num = treeviews_get_page_num_from_dest_doc( ctx, doc, dest, errmsg );
	pdf_drop_document( ctx, doc );
    if ( page_num < 0 ) ERROR_S

    return page_num;
}


/** Gibt nur bei Fehler NULL zurück, sonst immer Zeiger auf Anbindung **/
static Anbindung*
treeviews_ziel_zu_anbindung( fz_context* ctx, const gchar* rel_path, Ziel* ziel, gchar** errmsg )
{
    gint page_num = 0;

    Anbindung* anbindung = g_malloc0( sizeof( Anbindung ) );

    page_num = treeviews_get_page_num_from_dest( ctx, rel_path, ziel->ziel_id_von, errmsg );
    if ( page_num == -1 )
    {
        g_free( anbindung );
        ERROR_S_VAL( NULL )
    }
    else if ( page_num == -2 )
    {
        if ( errmsg ) *errmsg = g_strdup( "NamedDest nicht in Dokument vohanden" );
        g_free( anbindung );

        return NULL;
    }
    else anbindung->von.seite = page_num;

    page_num = treeviews_get_page_num_from_dest( ctx, rel_path, ziel->ziel_id_bis,
            errmsg );
    if ( page_num == -1 )
    {
        g_free( anbindung );

        ERROR_S_VAL( NULL )
    }
    else if ( page_num == -2 )
    {
        if ( errmsg ) *errmsg = g_strdup( "NamedDest nicht in Dokument vohanden" );
        g_free( anbindung );

        return NULL;
    }
    else anbindung->bis.seite = page_num;

    anbindung->von.index = ziel->index_von;
    anbindung->bis.index = ziel->index_bis;

    return anbindung;
}


/** Keine Datei mit node_id verknüpft: 2
    Kein ziel mit node_id verknüpft: 1
    Datei und ziel verknüpft: 0
    Fehler (inkl. node_id existiert nicht): -1

    Funktion sollte thread-safe sein! **/
gint
treeviews_get_rel_path_and_anbindung( Projekt* zond, Baum baum, gint node_id,
        gchar** rel_path, Anbindung** anbindung, gchar** errmsg )
{
    gint rc = 0;
    Ziel ziel = { 0, };
    gchar* rel_path_intern = NULL;
    Anbindung* anbindung_intern = NULL;

    rc = zond_dbase_get_rel_path( zond->dbase_zond->zond_dbase_work, baum, node_id, &rel_path_intern, errmsg );
    if ( rc == -1 ) ERROR_S
    else if ( rc == 1 ) return 2;

    rc = zond_dbase_get_ziel( zond->dbase_zond->zond_dbase_work, baum, node_id, &ziel, errmsg );
    if ( rc == -1 )
    {
        g_free( rel_path_intern );
        ERROR_S
    }
    else if ( rc == 1 )
    {
        if ( rel_path ) *rel_path = rel_path_intern;
        else g_free( rel_path_intern );

        return 1;
    }
/*  Muß nicht mit mutex geschützt werden
        ->Zugriff auf gleiche Datei aus mehreren threads m.E. zulässig, wenn unterschiedliches pdf_document

    const ZondPdfDocument* zond_pdf_document = zond_pdf_document_is_open( rel_path_intern );
    if ( zond_pdf_document ) zond_pdf_document_mutex_lock( zond_pdf_document );
*/
    anbindung_intern = treeviews_ziel_zu_anbindung( zond->ctx, rel_path_intern, &ziel, errmsg );

//    if ( zond_pdf_document ) zond_pdf_document_mutex_unlock( zond_pdf_document );

    if ( !anbindung_intern )
    {
        g_free( rel_path_intern );
        ERROR_S
    }

    if ( rel_path ) *rel_path = rel_path_intern;
    else g_free( rel_path_intern );

    if ( anbindung ) *anbindung = anbindung_intern;
    else g_free( anbindung_intern );

    return 0;
}



static gint
treeviews_db_to_baum_link( Projekt* zond, Baum baum, gint node_id,
        GtkTreeIter* iter_new, gchar** errmsg )
{
    gint rc = 0;
    gint ID_link = 0;
    Baum baum_target = KEIN_BAUM;
    gint node_id_target = 0;
    GtkTreeIter* iter_link = NULL;
    GtkTreeIter* iter_target = NULL;
    GtkTreeIter iter_anchor = { 0 };
    gboolean child = FALSE;
    GtkTreeIter iter_new_intern = { 0 };

    ID_link = zond_dbase_check_link( zond->dbase_zond->zond_dbase_work, baum, node_id, errmsg );
    if ( ID_link == -1 ) ERROR_S
    else if ( ID_link == 0 ) return 1; //halt kein link

    //wenn link:
    rc = zond_dbase_get_link( zond->dbase_zond->zond_dbase_work, ID_link, NULL, NULL,
        NULL, &baum_target, &node_id_target, errmsg );
    if ( rc == -1 ) ERROR_S
    else if ( rc == 1 ) ERROR_S_MESSAGE( "Kein link in DB gefunden" )

    //"leeren" iter, der link werden soll, rauslöschen
    //erst herausfinden
    iter_link = zond_treeview_abfragen_iter( ZOND_TREEVIEW(zond->treeview[baum]), node_id );
    if ( !iter_link ) ERROR_S_MESSAGE( "zond_treeview_abfragen_iter (link) gibt NULL zurück" )

    //iter_anchor basteln
    iter_anchor = *iter_link;
    if ( ((GNode*) (iter_link->user_data))->prev == NULL )
    {
        iter_anchor.user_data = ((GNode*) (iter_link->user_data))->parent;
        child = TRUE;
    }
    else
    {
        iter_anchor.user_data = ((GNode*) (iter_link->user_data))->prev;
        child = FALSE;
    }

    //dann löschen
    zond_tree_store_remove( iter_link );
    gtk_tree_iter_free( iter_link );

    //iter_target ermitteln
    iter_target = zond_treeview_abfragen_iter( ZOND_TREEVIEW(zond->treeview[baum_target]), node_id_target );
    if ( !iter_target ) ERROR_S_MESSAGE( "zond_treeview_abfragen_iter (target) gibt NULL zurück" )

    zond_tree_store_insert_link( iter_target, node_id,
            zond_tree_store_get_tree_store( &iter_anchor ),
            ((GNode*) (iter_anchor.user_data) == zond_tree_store_get_root_node( zond_tree_store_get_tree_store( &iter_anchor ) )) ? NULL :
             &iter_anchor, child, &iter_new_intern );

    gtk_tree_iter_free( iter_target );

    if ( iter_new ) *iter_new = iter_new_intern;

    return 0;
}


static gint
treeviews_db_to_baum_rec_links( Projekt* zond, gboolean with_younger_siblings,
        Baum baum, gint node_id, GtkTreeIter* iter_new, gchar** errmsg )
{
    gint first_child_id = 0;

    if ( node_id )
    {
        gint rc = 0;

        rc = treeviews_db_to_baum_link( zond, baum, node_id, iter_new, errmsg );
        if ( rc == -1 ) ERROR_S
        //else if ( rc == 1 ) -> nix machen, weiter laufen lassen
    }

    //Prüfen, ob Kind- oder Geschwisterknoten vorhanden
    first_child_id = zond_dbase_get_first_child( zond->dbase_zond->zond_dbase_work, baum, node_id, errmsg );
    if ( first_child_id < 0 ) ERROR_S
    else if ( first_child_id > 0 )
    {
        gint rc = 0;

        rc = treeviews_db_to_baum_rec_links( zond, TRUE, baum, first_child_id,
                NULL, errmsg );
        if ( rc ) ERROR_S
    }

    if ( with_younger_siblings )
    {
        gint younger_sibling_id = 0;

        younger_sibling_id = zond_dbase_get_younger_sibling( zond->dbase_zond->zond_dbase_work, baum, node_id, errmsg );
        if ( younger_sibling_id < 0 ) ERROR_S
        else if ( younger_sibling_id > 0 )
        {
            gint rc = 0;

            rc = treeviews_db_to_baum_rec_links( zond, TRUE, baum,
                    younger_sibling_id, NULL, errmsg );
            if ( rc ) ERROR_S
        }
    }

    return 0;
}


//rekursive Funktion; gibt Zeiger auf 1. eingefügten Iter zurück (g_free)
static gint
treeviews_reload_baum( Projekt* zond, gint root, gchar** errmsg )
{
    gint rc = 0;

    zond_tree_store_clear( ZOND_TREE_STORE(gtk_tree_view_get_model(
            GTK_TREE_VIEW(zond->treeview[baum]) )) );

    rc = treeviews_load_node( zond, FALSE, root, NULL, TRUE, NULL, errmsg );
    if ( rc ) ERROR_S

    return 0;
}


gint
treeviews_reload_baeume( Projekt* zond, gchar** errmsg )
{
    gint rc = 0;
    GtkTreeIter iter = { 0 };

    rc = treeviews_reload_baum( zond, BAUM_INHALT, errmsg );
    if ( rc ) ERROR_S

    rc = treeviews_reload_baum( zond, BAUM_AUSWERTUNG, errmsg );
    if ( rc ) ERROR_S

    g_object_set( sond_treeview_get_cell_renderer_text( zond->treeview[BAUM_AUSWERTUNG] ),
            "editable", FALSE, NULL);
    g_object_set( sond_treeview_get_cell_renderer_text( zond->treeview[BAUM_INHALT] ),
            "editable", TRUE, NULL);

    gtk_widget_grab_focus( GTK_WIDGET(zond->treeview[BAUM_INHALT]) );

    if ( gtk_tree_model_get_iter_first( gtk_tree_view_get_model(
            GTK_TREE_VIEW(zond->treeview[BAUM_AUSWERTUNG]) ), &iter ) )
    {
        sond_treeview_set_cursor( zond->treeview[BAUM_AUSWERTUNG], &iter );
        gtk_tree_selection_unselect_all( zond->selection[BAUM_AUSWERTUNG] );
    }

    return 0;
}


