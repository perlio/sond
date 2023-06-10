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
#include "../../dbase.h"


/** Wenn Fehler: -1
    Wenn Vorfahre Datei ist: 1
    ansonsten: 0 **/
gint
treeviews_hat_vorfahre_datei( Projekt* zond, Baum baum, gint anchor_id, gboolean child, gchar** errmsg )
{
    gint rc = 0;

    if ( anchor_id == 0 ) return 0;

    if ( !child )
    {
        anchor_id = zond_dbase_get_parent( zond->dbase_zond->zond_dbase_work, baum, anchor_id, errmsg );
        if ( anchor_id < 0 ) ERROR_S
    }

    rc = zond_dbase_get_rel_path( zond->dbase_zond->zond_dbase_work, baum, anchor_id, NULL, errmsg );
    if ( rc == -1 ) ERROR_S
    else if ( rc == 0 ) return 1; //Datei!

    return 0;
}


static Baum
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
        ERROR_SOND_VAL( "general_get_page_num_from_dest", NULL )
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

        ERROR_SOND_VAL( "general_get_page_num_from_dest", NULL )
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
    if ( rc == -1 ) ERROR_SOND( "zond_dbase_get_rel_path" )
    else if ( rc == 1 ) return 2;

    rc = zond_dbase_get_ziel( zond->dbase_zond->zond_dbase_work, baum, node_id, &ziel, errmsg );
    if ( rc == -1 )
    {
        g_free( rel_path_intern );
        ERROR_SOND( "zond_dbase_get_ziel" )
    }
    else if ( rc == 1 )
    {
        if ( rel_path ) *rel_path = rel_path_intern;
        else g_free( rel_path_intern );

        return 1;
    }

    const ZondPdfDocument* zond_pdf_document = zond_pdf_document_is_open( rel_path_intern );
    if ( zond_pdf_document ) zond_pdf_document_mutex_lock( zond_pdf_document );

    anbindung_intern = treeviews_ziel_zu_anbindung( zond->ctx, rel_path_intern, &ziel, errmsg );

    if ( zond_pdf_document ) zond_pdf_document_mutex_unlock( zond_pdf_document );

    if ( !anbindung_intern )
    {
        g_free( rel_path_intern );
        ERROR_SOND( "ziel_zu_anbindung" )
    }

    if ( rel_path ) *rel_path = rel_path_intern;
    else g_free( rel_path_intern );

    if ( anbindung ) *anbindung = anbindung_intern;
    else g_free( anbindung_intern );

    return 0;
}


static gint
treeviews_selection_entfernen_anbindung_foreach( SondTreeview* stv,
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
    if ( rc == -1 ) ERROR_S
    if ( rc == 1 ) return 0; //Knoten ist keine Anbindung

    //herausfinden, ob zu löschender Knoten älteres Geschwister hat
    older_sibling = zond_dbase_get_older_sibling( zond->dbase_zond->zond_dbase_work, BAUM_INHALT, node_id, errmsg );
    if ( older_sibling < 0 ) ERROR_S

    //Elternknoten ermitteln
    parent = zond_dbase_get_parent( zond->dbase_zond->zond_dbase_work, BAUM_INHALT, node_id, errmsg );
    if ( parent < 0 ) ERROR_S

    rc = zond_dbase_begin( zond->dbase_zond->zond_dbase_work, errmsg );
    if ( rc ) ERROR_S

    child = 0;
    while ( (child = zond_dbase_get_first_child( zond->dbase_zond->zond_dbase_work, BAUM_INHALT, node_id,
            errmsg )) )
    {
        if ( child < 0 ) ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )

        rc = treeviews_knoten_verschieben( zond, BAUM_INHALT, child, parent,
                older_sibling, errmsg );
        if ( rc == -1 ) ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )

        older_sibling = child;
    }

    rc = zond_dbase_remove_node( zond->dbase_zond->zond_dbase_work,BAUM_INHALT, node_id, errmsg );
    if ( rc ) ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )

    zond_tree_store_remove( iter );

    rc = zond_dbase_commit( zond->dbase_zond->zond_dbase_work, errmsg );
    if ( rc ) ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )

    return 0;
}


//Funktioniert nur im BAUM_INHALT - Abfrage im cb schließt nur BAUM_FS aus
gint
treeviews_selection_entfernen_anbindung( Projekt* zond, Baum baum_active, gchar** errmsg )
{
    gint rc = 0;

    if ( baum_active != BAUM_INHALT ) return 0;

    rc = sond_treeview_selection_foreach( zond->treeview[BAUM_INHALT],
            treeviews_selection_entfernen_anbindung_foreach, zond, errmsg );
    if ( rc == -1 ) ERROR_S

    return 0;
}


typedef struct _S_Selection_Loeschen
{
    Projekt* zond;
    Baum baum_active;
} SSelectionLoeschen;


static gint
treeviews_selection_loeschen_foreach( SondTreeview* tree_view, GtkTreeIter* iter,
        gpointer data, gchar** errmsg )
{
    gint node_id = 0;
    gint head_nr = 0;
    Baum baum = KEIN_BAUM;

    SSelectionLoeschen* s_selection = data;

    //Nur "nomale" Knoten oder ...
    if ( !zond_tree_store_is_link( iter ) )
    {
        gint rc = 0;
        gboolean response = FALSE;

        rc = treeviews_get_baum_and_node_id( s_selection->zond, iter, &baum, &node_id );
        if ( rc ) return 0;

        if ( node_id == s_selection->zond->node_id_extra )
                g_signal_emit_by_name( s_selection->zond->textview_window,
                "delete-event", s_selection->zond, &response );

        rc = zond_dbase_remove_node( s_selection->zond->dbase_zond->zond_dbase_work, baum, node_id, errmsg );
        if ( rc ) ERROR_S

        zond_tree_store_remove( iter );
    }//... Gesamt-Links
    else if ( (head_nr = zond_tree_store_get_link_head_nr( iter )) )
    {
        gint rc = 0;

        rc = zond_dbase_begin( s_selection->zond->dbase_zond->zond_dbase_work, errmsg );
        if ( rc ) ERROR_S

        rc = zond_dbase_remove_link( s_selection->zond->dbase_zond->zond_dbase_work,
                sond_treeview_get_id( tree_view ), head_nr, errmsg );
        if ( rc ) ERROR_S

        rc = zond_dbase_remove_node( s_selection->zond->dbase_zond->zond_dbase_work,
                sond_treeview_get_id( tree_view ), head_nr, errmsg );
        if ( rc ) ERROR_ROLLBACK( s_selection->zond->dbase_zond->zond_dbase_work )

        rc = zond_dbase_commit( s_selection->zond->dbase_zond->zond_dbase_work, errmsg );
        if ( rc ) ERROR_ROLLBACK( s_selection->zond->dbase_zond->zond_dbase_work )

        zond_tree_store_remove( iter );
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
            treeviews_selection_loeschen_foreach, &s_selection, errmsg );
    if ( rc == -1 ) ERROR_S

    return 0;
}


typedef struct _S_Selection_Change_Icon
{
    Projekt* zond;
    const gchar* icon_name;
} SSelectionChangeIcon;

static gint
treeviews_selection_change_icon_foreach( SondTreeview* tree_view, GtkTreeIter* iter,
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
    if ( rc ) ERROR_S

    //neuen icon_name im tree speichern
    zond_tree_store_set( iter, s_selection->icon_name, NULL, 0 );

    return 0;
}


gint
treeviews_selection_change_icon( Projekt* zond, Baum baum_active, const gchar* icon_name, gchar** errmsg )
{
    gint rc = 0;
    SSelectionChangeIcon s_selection = { zond, icon_name };

    rc = sond_treeview_selection_foreach( zond->treeview[baum_active],
            treeviews_selection_change_icon_foreach, (gpointer) &s_selection, errmsg );
    if ( rc == -1 ) ERROR_S

    return 0;
}


static gint
treeviews_selection_set_node_text_foreach( SondTreeview* stv, GtkTreeIter* iter,
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

    rc = treeviews_get_rel_path_and_anbindung( zond, baum, node_id, &rel_path,
            &anbindung, errmsg );
    if ( rc == -1 ) ERROR_S
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
        ERROR_S
    }

    //neuen text im tree speichern
    zond_tree_store_set( iter, NULL, node_text, 0 );

    g_free( node_text );

    return 0;
}

gint
treeviews_selection_set_node_text( Projekt* zond, Baum baum_active, gchar** errmsg )
{
    gint rc = 0;

    g_return_val_if_fail( baum_active == BAUM_INHALT || baum_active == BAUM_AUSWERTUNG, -1 );

    rc = sond_treeview_selection_foreach( zond->treeview[baum_active],
            treeviews_selection_set_node_text_foreach, zond, errmsg );
    if ( rc ) ERROR_S

    return 0;
}


gint
treeviews_insert_node( Projekt* zond, Baum baum_active, gboolean child, gchar** errmsg )
{
    Baum baum_anchor = KEIN_BAUM;
    gint anchor_id = 0;
    gint rc = 0;
    gint node_id_new = 0;
    GtkTreeIter iter_cursor = { 0 };
    GtkTreeIter iter_anchor = { 0 };
    GtkTreeIter iter_origin = { 0 };
    GtkTreeIter iter_new = { 0 };
    gboolean success = FALSE;
    ZondTreeStore* tree_store = NULL;

    g_return_val_if_fail( baum_active == BAUM_INHALT || baum_active == BAUM_AUSWERTUNG, -1);

    if ( !(success = treeviews_get_anchor( zond, child, &iter_cursor,
            &iter_anchor, &baum_anchor, &anchor_id )) ) child = TRUE;

    if ( baum_anchor == BAUM_INHALT )
    {
        gint rc = 0;

        rc = treeviews_hat_vorfahre_datei( zond, baum_anchor, anchor_id, child, errmsg );
        if ( rc == -1 ) ERROR_S
        else if ( rc == 1 ) return 1;
    }

    if ( success ) iter_origin = iter_cursor;

    rc = zond_dbase_begin( zond->dbase_zond->zond_dbase_work, errmsg );
    if ( rc ) ERROR_S

    //Knoten in Datenbank einfügen
    node_id_new = zond_dbase_insert_node( zond->dbase_zond->zond_dbase_work, baum_anchor,
            anchor_id, child, zond->icon[ICON_NORMAL].icon_name, "Neuer Punkt", errmsg );
    if ( node_id_new == -1 ) ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )

    rc = zond_dbase_commit( zond->dbase_zond->zond_dbase_work, errmsg );
    if ( rc ) ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )

    //Knoten in baum_inhalt einfuegen
    //success = sond_treeview_get_cursor( zond->treeview[baum], &iter ); - falsch!!!

    tree_store = ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[baum_anchor]) ));
    zond_tree_store_insert( tree_store, (success) ? &iter_anchor : NULL, child, &iter_new );

    //Standardinhalt setzen
    zond_tree_store_set( &iter_new, zond->icon[ICON_NORMAL].icon_name, "Neuer Punkt", node_id_new );

    if ( child && success )
            sond_treeview_expand_row( zond->treeview[zond->baum_active], &iter_origin );
    sond_treeview_set_cursor( zond->treeview[baum_anchor], &iter_new );

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


gint
treeviews_db_to_baum( Projekt* zond, Baum baum, gint node_id, GtkTreeIter* iter,
        gboolean child, GtkTreeIter* iter_new, gchar** errmsg )
{
    //Inhalt des Datensatzes mit node_id == node_id abfragen
    gint rc = 0;
    GtkTreeIter iter_inserted = { 0, };
    gchar* icon_name = NULL;
    gchar* node_text = NULL;

    rc = zond_dbase_check_link( zond->dbase_zond->zond_dbase_work, baum, node_id,
            errmsg );
    if ( rc == -1 ) ERROR_S
    else if ( rc == 0 )
    {
        gint rc = 0;

        rc = zond_dbase_get_icon_name_and_node_text( zond->dbase_zond->zond_dbase_work,
                baum, node_id, &icon_name, &node_text, errmsg );
        if ( rc == -1 ) ERROR_S
        else if ( rc == 1 ) ERROR_S_MESSAGE( "node_id existiert nicht" )
    }

    //neuen Knoten einfügen
    zond_tree_store_insert( ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[baum]) )),
            iter, child, &iter_inserted );

    //Daten rein
    zond_tree_store_set( &iter_inserted, icon_name, node_text, node_id );

    g_free( icon_name );
    g_free( node_text );

    if ( iter_new ) *iter_new = iter_inserted;

    return 0;
}


//rekursive Funktion; gibt Zeiger auf 1. eingefügten Iter zurück (g_free)
static gint
treeviews_db_to_baum_rec( Projekt* zond, gboolean with_younger_siblings,
        Baum baum, gint node_id, GtkTreeIter* iter, gboolean child, GtkTreeIter* iter_new,
        gchar** errmsg )
{
    gint rc = 0;
    GtkTreeIter iter_inserted = { 0, };
    gint first_child_id = 0;
    gint younger_sibling_id = 0;

    if ( node_id )
    {
        rc = treeviews_db_to_baum( zond, baum, node_id, iter, child, &iter_inserted, errmsg );
        if ( rc == 1 ) return 0;
        else if ( rc == -1 ) ERROR_S
    }

    //Prüfen, ob Kind- oder Geschwisterknoten vorhanden
    first_child_id = zond_dbase_get_first_child( zond->dbase_zond->zond_dbase_work, baum, node_id, errmsg );
    if ( first_child_id < 0 ) ERROR_S
    else if ( first_child_id > 0 )
    {
        gint rc = 0;
        rc = treeviews_db_to_baum_rec( zond, TRUE, baum, first_child_id,
                (iter_inserted.stamp) ? &iter_inserted : NULL, TRUE, NULL, errmsg );
        if ( rc ) ERROR_S
    }

    if ( with_younger_siblings )
    {
        younger_sibling_id = zond_dbase_get_younger_sibling( zond->dbase_zond->zond_dbase_work, baum, node_id, errmsg );
        if ( younger_sibling_id < 0 ) ERROR_S
        else if ( younger_sibling_id > 0 )
        {
            gint rc = 0;

            rc = treeviews_db_to_baum_rec( zond, TRUE, baum,
                    younger_sibling_id, (iter_inserted.stamp) ? &iter_inserted : NULL, FALSE, NULL, errmsg );
            if ( rc ) ERROR_S
        }
    }

    if ( iter_new ) *iter_new = iter_inserted;

    return 0;
}


gint
treeviews_load_node( Projekt* zond, gboolean with_younger_siblings,
        Baum baum, gint node_id, GtkTreeIter* iter_anchor, gboolean child,
        GtkTreeIter* iter_new, gchar** errmsg )
{
    gint rc = 0;
    GtkTreeIter iter_node = { 0 };
    GtkTreeIter iter_link = { 0 };

    rc = treeviews_db_to_baum_rec( zond, with_younger_siblings, baum, node_id,
            iter_anchor, child, &iter_node, errmsg );
    if ( rc ) ERROR_S

    if ( iter_new ) *iter_new = iter_node;

    rc = treeviews_db_to_baum_rec_links( zond, with_younger_siblings, baum,
            node_id, &iter_link, errmsg );
    if ( rc ) ERROR_S

    if ( iter_link.stamp && iter_new ) *iter_new = iter_link;

    return 0;
}


static gint
treeviews_reload_baum( Projekt* zond, Baum baum, gchar** errmsg )
{
    gint rc = 0;

    zond_tree_store_clear( ZOND_TREE_STORE(gtk_tree_view_get_model(
            GTK_TREE_VIEW(zond->treeview[baum]) )) );

    rc = treeviews_load_node( zond, FALSE, baum, 0, NULL, TRUE, NULL, errmsg );
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


gint
treeviews_knoten_verschieben( Projekt* zond, Baum baum, gint node_id, gint parent_id,
        gint older_sibling_id, gchar** errmsg )
{
    gint rc = 0;
    gboolean child = FALSE;
    GtkTreeIter* iter_src = NULL;
    GtkTreeIter* iter_dest = NULL;

    //kind verschieben
    rc = zond_dbase_verschieben_knoten( zond->dbase_zond->zond_dbase_work, baum, node_id, parent_id,
            older_sibling_id, errmsg );
    if ( rc ) ERROR_S

    //Knoten im tree löschen
    //hierzu iter des verschobenen Kindknotens herausfinden
    iter_src = zond_treeview_abfragen_iter( ZOND_TREEVIEW(zond->treeview[baum]), node_id );

    //zunächst iter des Anker-Knotens ermitteln
    if ( !older_sibling_id ) child = TRUE;

    //zunächst iter des Anker-Knotens ermitteln
    if ( child ) iter_dest =
            zond_treeview_abfragen_iter( ZOND_TREEVIEW(zond->treeview[baum]),
            parent_id );
    else iter_dest =
            zond_treeview_abfragen_iter( ZOND_TREEVIEW(zond->treeview[baum]),
            older_sibling_id );

    zond_tree_store_move_node( iter_src, iter_dest, child, NULL );

    gtk_tree_iter_free( iter_dest );
    gtk_tree_iter_free( iter_src );

    return 0;
}


//ermittelt node_id und iter des anchors, falls nicht in link eingefügt werden würde
gboolean
treeviews_get_anchor( Projekt* zond, gboolean child, GtkTreeIter* iter_cursor,
        GtkTreeIter* iter_anchor, Baum* baum_anchor, gint* anchor_id )
{
    GtkTreeIter iter_anchor_intern = { 0 };

    if ( !sond_treeview_get_cursor( zond->treeview[zond->baum_active],
            iter_cursor ) )
    {
        if ( baum_anchor ) *baum_anchor = zond->baum_active;

        //Trick, weil wir keinen gültigen iter übergeben können->
        //setzten stamp auf stamp des "richtigen" tree_stores und
        //user_data auf root_node
        GtkTreeModel* model = NULL;

        model = GTK_TREE_MODEL(gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[zond->baum_active]) ));

        if ( iter_cursor )
        {
            iter_cursor->stamp = zond_tree_store_get_stamp( ZOND_TREE_STORE(model) );
            iter_cursor->user_data = zond_tree_store_get_root_node( ZOND_TREE_STORE(model) );
        }
        if ( iter_anchor )
        {
            iter_anchor->stamp = zond_tree_store_get_stamp( ZOND_TREE_STORE(model) );
            iter_anchor->user_data = zond_tree_store_get_root_node( ZOND_TREE_STORE(model) );
        }

        return FALSE; //heißt: eigentlich kein cursor - fake-iter mit root gebildet
    }

    if ( child ) zond_tree_store_get_iter_target( iter_cursor, &iter_anchor_intern );
    else
    {
        if ( zond_tree_store_get_link_head_nr( iter_cursor ) <= 0 )
                zond_tree_store_get_iter_target( iter_cursor, &iter_anchor_intern );
        else iter_anchor_intern = *iter_cursor; //wenn iter_cursor head-link, dann ist link und nicht target anchor
    }

    if ( baum_anchor ) *baum_anchor = treeviews_get_baum_iter( zond, &iter_anchor_intern );
    if ( anchor_id )  gtk_tree_model_get( GTK_TREE_MODEL(zond_tree_store_get_tree_store(
            &iter_anchor_intern )), &iter_anchor_intern, 2, anchor_id, -1 );

    if ( iter_anchor ) *iter_anchor = iter_anchor_intern;

    return TRUE;
}


static gboolean
treeviews_move_iter( GtkTreeIter* iter, gboolean child )
{
    if ( child )
    {
        if ( !(iter->user_data = ((GNode*) (iter->user_data))->children) ) return FALSE;
    }
    else
    {
        if ( !(iter->user_data = ((GNode*) (iter->user_data))->next) ) return FALSE;
    }

    return TRUE;
}


static gint
treeviews_move_cursor_and_anchor( GtkTreeIter* iter_cursor,
        GtkTreeIter* iter_anchor, gboolean child, gchar** errmsg )
{
    if ( !treeviews_move_iter( iter_cursor, child ) )
            ERROR_S_MESSAGE( "Eingefügter iter_cursor nicht zu ermitten" )

    if ( !treeviews_move_iter( iter_anchor, child ) )
            ERROR_S_MESSAGE( "Eingefügter iter_anchor nicht zu ermitten" )

    return 0;
}


typedef struct {
    Projekt* zond;
    gboolean child;
    GtkTreeIter* iter_cursor;
    GtkTreeIter* iter_anchor;
    Baum baum_anchor;
    gint anchor_id;
} SSelection;


static gint
treeviews_paste_clipboard_as_link_foreach( SondTreeview* tree_view, GtkTreeIter* iter, gpointer data, gchar** errmsg )
{
    gint rc = 0;
    gint node_id_new = 0;
    Baum baum = KEIN_BAUM;
    gint node_id = 0;
    GtkTreeIter iter_target = { 0 };

    SSelection* s_selection = (SSelection*) data;

    //node ID, auf den link zeigen soll
    //falls link im clipboard, baum und node_id des target ermitteln
    rc = treeviews_get_baum_and_node_id( s_selection->zond, iter, &baum, &node_id );
    if ( rc ) ERROR_S_MESSAGE( "Bei treeviews_get_baum_and_node_id:\nKein Baum gefunden" )

    rc = zond_dbase_begin( s_selection->zond->dbase_zond->zond_dbase_work, errmsg );
    if ( rc ) ERROR_S

    node_id_new = zond_dbase_insert_node( s_selection->zond->dbase_zond->zond_dbase_work,
            s_selection->baum_anchor, s_selection->anchor_id, s_selection->child, NULL,
            NULL, errmsg );
    if ( node_id_new < 0 ) ERROR_ROLLBACK( s_selection->zond->dbase_zond->zond_dbase_work )

    rc = zond_dbase_set_link( s_selection->zond->dbase_zond->zond_dbase_work,
            s_selection->baum_anchor, node_id_new, NULL, baum, node_id, errmsg );
    if ( rc ) ERROR_ROLLBACK( s_selection->zond->dbase_zond->zond_dbase_work )

    rc = zond_dbase_commit( s_selection->zond->dbase_zond->zond_dbase_work, errmsg );
    if ( rc ) ERROR_ROLLBACK( s_selection->zond->dbase_zond->zond_dbase_work )

    //falls link im clipboard: iter_target ermitteln, damit nicht link auf link zeigt
    zond_tree_store_get_iter_target( iter, &iter_target );
    zond_tree_store_insert_link( &iter_target, node_id_new, zond_tree_store_get_tree_store( s_selection->iter_anchor ),
            (s_selection->anchor_id) ? s_selection->iter_anchor: NULL, s_selection->child, NULL );

    rc = treeviews_move_cursor_and_anchor( s_selection->iter_cursor, s_selection->iter_anchor, s_selection->child, errmsg );
    if ( rc ) ERROR_S

    s_selection->anchor_id = node_id_new;
    s_selection->child = FALSE;

    return 0;
}


//Ziel ist immer BAUM_AUSWERTUNG
gint
treeviews_paste_clipboard_as_link( Projekt* zond, gboolean child, GtkTreeIter* iter_cursor,
        GtkTreeIter* iter_anchor, Baum baum_anchor, gint anchor_id, gchar** errmsg )
{
    gint rc = 0;
    GtkTreeIter iter_origin = { 0 };

    SSelection s_selection = { zond, child, iter_cursor, iter_anchor, baum_anchor,
            anchor_id };

    iter_origin = *iter_cursor;

    rc = sond_treeview_clipboard_foreach( treeviews_paste_clipboard_as_link_foreach, &s_selection, errmsg );
    if ( rc == -1 ) ERROR_S

    if ( child && (iter_origin.user_data !=
            zond_tree_store_get_root_node(
            zond_tree_store_get_tree_store( &iter_origin ) )) )
            sond_treeview_expand_row( zond->treeview[zond->baum_active], &iter_origin );
    sond_treeview_set_cursor( zond->treeview[baum_anchor], s_selection.iter_cursor );

    return 0;
}


//node_nach ist immer in BAUM_AUSWERTUNG
static gint
treeviews_clipboard_kopieren_db( Projekt* zond, gboolean with_younger_siblings,
        Baum baum_von, gint node_von, gint node_nach, gboolean kind, gchar** errmsg )
{
    gint rc = 0;
    gint first_child_id = 0;
    gint node_id_new = 0;
    gint ID_link = 0;

    //wenn node_id_nach == 0 ist child egal, wird immer als kind eingesetzt
    node_id_new = zond_dbase_kopieren_nach_auswertung( zond->dbase_zond->zond_dbase_work, baum_von, node_von,
            node_nach, kind, errmsg );
    if ( node_id_new == -1 ) ERROR_S

    //prüfen ob link...
    ID_link = zond_dbase_check_link( zond->dbase_zond->zond_dbase_work, baum_von,
            node_von, errmsg );
    if ( ID_link == -1 ) ERROR_S
    else if ( ID_link > 0 )
    {
        gint rc = 0;
        Baum baum_link = KEIN_BAUM;
        gint node_link = 0;
        Baum baum_target = KEIN_BAUM;
        gint node_target = 0;

        rc = zond_dbase_get_link( zond->dbase_zond->zond_dbase_work, ID_link,
                &baum_link, &node_link, NULL, &baum_target, &node_target, errmsg );
        if ( rc == -1 ) ERROR_S
        else if ( rc == 1 ) ERROR_S_MESSAGE( "Kein link zu ID gefunden - db korrupt" )

        rc = zond_dbase_set_link( zond->dbase_zond->zond_dbase_work, BAUM_AUSWERTUNG,
                node_id_new, NULL, baum_target, node_target, errmsg );
        if ( rc ) ERROR_S
    }

    //Prüfen, ob Kind- oder Geschwisterknoten vorhanden
    first_child_id = zond_dbase_get_first_child( zond->dbase_zond->zond_dbase_work, baum_von, node_von, errmsg );
    if ( first_child_id < 0 ) ERROR_S
    else if ( first_child_id > 0 )
    {
        rc = treeviews_clipboard_kopieren_db( zond, TRUE, baum_von,
                first_child_id, node_id_new, TRUE, errmsg );
        if ( rc == -1  ) ERROR_SOND( "selection_copy_node_db" )
    }

    if ( with_younger_siblings )
    {
        gint younger_sibling_id = 0;

        younger_sibling_id = zond_dbase_get_younger_sibling( zond->dbase_zond->zond_dbase_work, baum_von, node_von,
                errmsg );
        if ( younger_sibling_id < 0 ) ERROR_S
        else if ( younger_sibling_id > 0 )
        {
            rc = treeviews_clipboard_kopieren_db( zond, TRUE, baum_von,
                    younger_sibling_id, node_id_new, FALSE, errmsg );
            if ( rc == -1 ) ERROR_S
        }
    }

    return node_id_new;
}


static gint
treeviews_clipboard_kopieren_foreach( SondTreeview* tree_view, GtkTreeIter* iter, gpointer data, gchar** errmsg )
{
    gint rc = 0;
    gint node_id = 0;
    Baum baum = KEIN_BAUM;
    gint node_id_new = 0;

    SSelection* s_selection = (SSelection*) data;

    //Kopieren nur nach BAUM_AUSWERTUNG!
    if ( s_selection->baum_anchor != BAUM_AUSWERTUNG ) return 0;

    rc = treeviews_get_baum_and_node_id( s_selection->zond, iter, &baum, &node_id );
    if ( rc ) ERROR_S_MESSAGE( "Bei Aufruf treeviews_get_baum_and_node_id:\nKein Baum gefunden" )

    rc = zond_dbase_begin( s_selection->zond->dbase_zond->zond_dbase_work, errmsg );
    if ( rc ) ERROR_S

    node_id_new = treeviews_clipboard_kopieren_db( s_selection->zond, FALSE, baum,
            node_id, s_selection->anchor_id, s_selection->child, errmsg );
    if ( node_id_new == -1 ) ERROR_ROLLBACK( s_selection->zond->dbase_zond->zond_dbase_work )

    //ToDO: iter_cursor richtig ermitteln
    rc = treeviews_load_node( s_selection->zond, FALSE, BAUM_AUSWERTUNG,
            node_id_new, (s_selection->anchor_id) ? s_selection->iter_cursor : NULL,
            s_selection->child, NULL, errmsg );
    if ( rc ) ERROR_ROLLBACK( s_selection->zond->dbase_zond->zond_dbase_work )

    rc = treeviews_move_cursor_and_anchor( s_selection->iter_cursor,
            s_selection->iter_anchor, s_selection->child, errmsg );
    if ( rc ) ERROR_ROLLBACK( s_selection->zond->dbase_zond->zond_dbase_work )

    rc = zond_dbase_commit( s_selection->zond->dbase_zond->zond_dbase_work, errmsg );
    if ( rc ) ERROR_ROLLBACK( s_selection->zond->dbase_zond->zond_dbase_work )

    s_selection->anchor_id = node_id_new;
    s_selection->child = FALSE;

    return 0;
}


gint
treeviews_clipboard_kopieren( Projekt* zond, gboolean child,
        GtkTreeIter* iter_cursor, GtkTreeIter* iter_anchor, Baum baum_anchor,
        gint anchor_id, gchar** errmsg )
{
    gint rc = 0;
    GtkTreeIter iter_origin = { 0 };
    SSelection s_selection = { zond, child, iter_cursor, iter_anchor, baum_anchor, anchor_id };

    //falls in existierenden iter_cursor eigenfügt wird: diesen merken
    iter_origin = *iter_cursor;

    rc = sond_treeview_clipboard_foreach( treeviews_clipboard_kopieren_foreach,
            &s_selection, errmsg );
    if ( rc == -1 ) ERROR_S

    if ( child && (iter_origin.user_data !=
            zond_tree_store_get_root_node(
            zond_tree_store_get_tree_store( &iter_origin ) )) )
            sond_treeview_expand_row( zond->treeview[zond->baum_active], &iter_origin );
    sond_treeview_set_cursor( zond->treeview[zond->baum_active], s_selection.iter_cursor );

    return 0;
}


static gint
treeviews_clipboard_verschieben_foreach( SondTreeview* tree_view, GtkTreeIter* iter_src,
        gpointer data, gchar** errmsg )
{
    gint node_id = 0;
    Baum baum = KEIN_BAUM;
    gint id_parent = 0;
    gint id_older_sibling = 0;
    gint rc = 0;

    SSelection* s_selection = (SSelection*) data;

    rc = treeviews_get_baum_and_node_id( s_selection->zond, iter_src, &baum, &node_id );
    if ( rc == 1 ) ERROR_S_MESSAGE( "Bei Aufruf treeviews_get_baum_and_node_id:\n"
            "Kein Baum gefunden" )

    //soll Ziel verschoben werden? Nein!
    if ( baum == BAUM_INHALT )
    {
        gint rc = 0;

        rc = zond_dbase_get_ziel( s_selection->zond->dbase_zond->zond_dbase_work, BAUM_INHALT,
                node_id, NULL, errmsg );
        if ( rc == -1 ) ERROR_S
        else if ( rc == 0 ) return 0;
    }

    //soll link verschoben werden? Nur wenn head
    if ( zond_tree_store_is_link( iter_src ) )
    {
        //nur packen, wenn head
        if ( (node_id = zond_tree_store_get_link_head_nr( iter_src )) <= 0 ) return 0;
    }

    //parent und older sibling des neu einzufügenden Knotens bestimmen
    if ( s_selection->child ) id_parent = s_selection->anchor_id;
    else
    {
        id_older_sibling = s_selection->anchor_id;
        id_parent = zond_dbase_get_parent( s_selection->zond->dbase_zond->zond_dbase_work, s_selection->baum_anchor, id_older_sibling, errmsg );
        if ( id_parent < 0 ) ERROR_S
    }

    //Knoten verschieben verschieben
    rc = zond_dbase_verschieben_knoten( s_selection->zond->dbase_zond->zond_dbase_work, s_selection->baum_anchor,
            node_id, id_parent, id_older_sibling, errmsg );
    if ( rc ) ERROR_S

    zond_tree_store_move_node( iter_src, s_selection->iter_anchor, s_selection->child, NULL );

    rc = treeviews_move_cursor_and_anchor( s_selection->iter_cursor, s_selection->iter_anchor, s_selection->child, errmsg );
    if ( rc ) ERROR_S

    s_selection->child = FALSE;
    s_selection->anchor_id = node_id;

    return 0;
}


//Ist immer verschieben innerhalb des Baums
gint
treeviews_clipboard_verschieben( Projekt* zond, gboolean child, GtkTreeIter* iter_cursor,
        GtkTreeIter* iter_anchor, Baum baum_anchor, gint anchor_id, gchar** errmsg )
{
    gint rc = 0;
    Clipboard* clipboard = NULL;
    GtkTreeIter iter_origin = { 0 };

    SSelection s_selection = { zond, child, iter_cursor, iter_anchor, baum_anchor, anchor_id };

    iter_origin = *iter_cursor;

    rc = sond_treeview_clipboard_foreach( treeviews_clipboard_verschieben_foreach, &s_selection, errmsg );
    if ( rc == -1 ) ERROR_S

    //Alte Auswahl löschen
    clipboard = ((SondTreeviewClass*) g_type_class_peek( SOND_TYPE_TREEVIEW ))->clipboard;
    if ( clipboard->arr_ref->len > 0 ) g_ptr_array_remove_range( clipboard->arr_ref,
            0, clipboard->arr_ref->len );

    gtk_widget_queue_draw( GTK_WIDGET(zond->treeview[zond->baum_active]) );

    if ( child && (iter_origin.user_data !=
            zond_tree_store_get_root_node(
            zond_tree_store_get_tree_store( &iter_origin ) )) )
            sond_treeview_expand_row( zond->treeview[zond->baum_active], &iter_origin );
    sond_treeview_set_cursor( zond->treeview[zond->baum_active], s_selection.iter_cursor );

    return 0;
}


static void
treeviews_jump_to_iter( Projekt* zond, GtkTreeIter* iter )
{
    Baum baum_target = KEIN_BAUM;

    baum_target = treeviews_get_baum_iter( zond, iter );

    gtk_widget_grab_focus( GTK_WIDGET(zond->treeview[baum_target]) );

    sond_treeview_expand_to_row( zond->treeview[baum_target], iter );
    sond_treeview_set_cursor( zond->treeview[baum_target], iter );

    return;
}


void
treeviews_jump_to_link_target( Projekt* zond )
{
    GtkTreeIter iter = { 0 };
    GtkTreeIter iter_target = { 0 };

    if ( sond_treeview_get_cursor( zond->treeview[zond->baum_active], &iter ) )
    {
        zond_tree_store_get_iter_target( &iter, &iter_target );

        treeviews_jump_to_iter( zond, &iter_target );
    }

    return;
}

