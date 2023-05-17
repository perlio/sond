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
    if ( anchor_id == 0 ) return 0;
    else if ( !child )
    {
        anchor_id = zond_dbase_get_parent( zond->dbase_zond->zond_dbase_work, baum, anchor_id, errmsg );
        if ( anchor_id < 0 ) ERROR_S
    }

    while ( anchor_id != 0 )
    {
        gint rc = 0;

        rc = zond_dbase_get_rel_path( zond->dbase_zond->zond_dbase_work, baum, anchor_id, NULL, errmsg );
        if ( rc == -1 ) ERROR_S
        else if ( rc == 0 ) return 1; //nicht mal datei!

        anchor_id = zond_dbase_get_parent( zond->dbase_zond->zond_dbase_work, baum, anchor_id, errmsg );
        if ( anchor_id < 0 ) ERROR_S
    }

    return 0;
}


gint
treeviews_get_baum_and_node_id( Projekt* zond, GtkTreeIter* iter, Baum* baum,
        gint* node_id )
{
    GtkTreeIter iter_target = { 0, };
    ZondTreeStore* tree_store = NULL;

    zond_tree_store_get_iter_target( iter, &iter_target );
    tree_store = zond_tree_store_get_tree_store( &iter_target );

    if ( tree_store == ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[BAUM_INHALT]) )) ) *baum = BAUM_INHALT;
    else if ( tree_store == ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[BAUM_AUSWERTUNG]) )) ) *baum = BAUM_AUSWERTUNG;
    else return 1;

    gtk_tree_model_get( GTK_TREE_MODEL(tree_store), &iter_target, 2, node_id, -1 );

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
        GList* list_links = NULL;
        gboolean response = FALSE;

        rc = treeviews_get_baum_and_node_id( s_selection->zond, iter, &baum, &node_id );
        if ( rc ) return 0;

        list_links = zond_tree_store_get_linked_nodes( iter );

        if ( list_links )
        {
            GList* ptr = NULL;

            ptr = list_links;
            do
            {
                GtkTreeIter iter_link = { 0 };

                iter_link.user_data = ptr->data; //stamp ist in der Funktion egal...
                head_nr = zond_tree_store_get_link_head_nr( &iter_link );
                if ( head_nr )
                {
                    Baum baum_link = KEIN_BAUM;
                    ZondTreeStore* tree_store = NULL;

                    tree_store = zond_tree_store_get_tree_store( &iter_link );
                    if ( tree_store == ZOND_TREE_STORE(gtk_tree_view_get_model(
                            GTK_TREE_VIEW(s_selection->zond->treeview[BAUM_INHALT]) )) )
                            baum_link = BAUM_INHALT;
                    else if ( tree_store == ZOND_TREE_STORE(gtk_tree_view_get_model(
                            GTK_TREE_VIEW(s_selection->zond->treeview[BAUM_AUSWERTUNG]) )) )
                            baum_link = BAUM_AUSWERTUNG;
                    else return 0; //???

                    rc = zond_dbase_begin( s_selection->zond->dbase_zond->zond_dbase_work, errmsg );
                    if ( rc ) ERROR_S

                    rc = zond_dbase_remove_node( s_selection->zond->dbase_zond->zond_dbase_work, baum_link, head_nr, errmsg );
                    if ( rc ) ERROR_ROLLBACK( s_selection->zond->dbase_zond->zond_dbase_work )

                    rc = zond_dbase_remove_link( s_selection->zond->dbase_zond->zond_dbase_work, baum_link, head_nr, errmsg );
                    if ( rc ) ERROR_ROLLBACK( s_selection->zond->dbase_zond->zond_dbase_work )

                    rc = zond_dbase_commit( s_selection->zond->dbase_zond->zond_dbase_work, errmsg );
                    if ( rc ) ERROR_ROLLBACK( s_selection->zond->dbase_zond->zond_dbase_work )
                }

            } while ( (ptr = ptr->next) );

//            g_list_free( list_links );
        }

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
        if ( rc ) ERROR_ROLLBACK( s_selection->zond->dbase_zond->zond_dbase_work  )

        zond_tree_store_remove_link( iter );
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
    Baum baum = KEIN_BAUM;
    gint node_id = 0;
    gint rc = 0;
    gint node_id_new = 0;
    GtkTreeIter iter = { 0 };
    GtkTreeIter new_iter = { 0 };
    gboolean success = FALSE;
    ZondTreeStore* tree_store = NULL;

    g_return_val_if_fail( baum_active == BAUM_INHALT || baum_active == BAUM_AUSWERTUNG, -1);

    //Knoten in baum_inhalt einfuegen
    success = sond_treeview_get_cursor( zond->treeview[baum_active], &iter );

    if ( !success )
    {
        baum = baum_active;
        child = TRUE;
    }
    else //cursor zeigt auf row
    {
        gint head_nr = 0;

        head_nr = zond_tree_store_get_link_head_nr( &iter );
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

        rc = treeviews_hat_vorfahre_datei( zond, baum, node_id, child, errmsg );
        if ( rc == -1 ) ERROR_S
        else if ( rc == 1 )
        {
            display_message( zond->app_window, "Einfügen als Unterpunkt von Datei nicht zulässig", NULL );

            return 1; //Abbruch ohne Fählermeldung
        }
    }

    rc = zond_dbase_begin( zond->dbase_zond->zond_dbase_work, errmsg );
    if ( rc ) ERROR_S

    //Knoten in Datenbank einfügen
    node_id_new = zond_dbase_insert_node( zond->dbase_zond->zond_dbase_work, baum,
            node_id, child, zond->icon[ICON_NORMAL].icon_name, "Neuer Punkt", errmsg );
    if ( node_id_new == -1 ) ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )

    rc = zond_dbase_commit( zond->dbase_zond->zond_dbase_work, errmsg );
    if ( rc ) ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )

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


static gint
treeviews_db_to_baum_links( Projekt* zond, gchar** errmsg )
{
    gint ID_start = 0;

    while ( 1 )
    {
        gint rc = 0;
        ZondTreeStore* tree_store = NULL;
        GtkTreeIter* iter_link = NULL;
        GtkTreeIter* iter_parent = NULL;
        Baum baum = KEIN_BAUM;
        gint node_id = 0;
        gchar* project = NULL;
        Baum baum_target = KEIN_BAUM;
        gint node_id_target = 0;
        GtkTreeIter* iter_target = NULL;
        gint older_sibling = 0;
        gint parent = 0;
        gint pos = 0;

        rc = zond_dbase_get_link( zond->dbase_zond->zond_dbase_work, &ID_start, &baum, &node_id,
            &project, &baum_target, &node_id_target, errmsg );
        g_free( project ); //einstweilen, bis wir project brauchen
        if ( rc == -1 ) ERROR_S
        else if ( rc == 1 ) break;

        ID_start++;

        //"leeren" iter, der link werden soll, rauslöschen
        //erst herausfinden
        iter_link = zond_treeview_abfragen_iter( ZOND_TREEVIEW(zond->treeview[baum]), node_id );
        if ( !iter_link ) ERROR_S_MESSAGE( "zond_treeview_abfragen_iter (link) gibt NULL zurück" )

        tree_store = ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[baum]) ));

        //dann löschen
        zond_tree_store_remove( iter_link );
        gtk_tree_iter_free( iter_link );

        //iter_target ermitteln
        iter_target = zond_treeview_abfragen_iter( ZOND_TREEVIEW(zond->treeview[baum_target]), node_id_target );
        if ( !iter_target ) ERROR_S_MESSAGE( "zond_treeview_abfragen_iter (target) gibt NULL zurück" )

        //iter wo link hinkommt ermitteln (iter_dest
        //zuerst iter_parent
        parent = zond_dbase_get_parent( zond->dbase_zond->zond_dbase_work, baum, node_id, errmsg );
        if ( parent < 0 )
        {
            gtk_tree_iter_free( iter_target );
            ERROR_S
        }
        // else if ( parent == 0 ) -> überflüssig, da iter_parent dann = NULL
        else if ( parent > 0 )
        {
            iter_parent = zond_treeview_abfragen_iter( ZOND_TREEVIEW(zond->treeview[baum]), parent );
            if ( !iter_parent )
            {
                gtk_tree_iter_free( iter_target );
                ERROR_S_MESSAGE( "zond_treeview_abfragen_iter gibt NULL zurück (I)" )
            }
        }

        //jetzt pos
        older_sibling = zond_dbase_get_older_sibling( zond->dbase_zond->zond_dbase_work, baum, node_id, errmsg );
        if ( older_sibling < 0 )
        {
            gtk_tree_iter_free( iter_target );
            if ( iter_parent ) gtk_tree_iter_free( iter_parent );
            ERROR_S
        }
        // else if ( older_sibling == 0 ) //erstes Kind -> pos = 0
        else if ( older_sibling > 0 )
        {
            gint noch_olderer_sibling = 0;

            do
            {

                pos++;
                noch_olderer_sibling = zond_dbase_get_older_sibling(
                        zond->dbase_zond->zond_dbase_work, baum, older_sibling, errmsg );
                if ( noch_olderer_sibling < 0 ) ERROR_S
            }
            while ( (older_sibling = noch_olderer_sibling) );
        }

        zond_tree_store_insert_link( iter_target->user_data, node_id, tree_store,
                iter_parent, (older_sibling) ? FALSE : TRUE, NULL );

        gtk_tree_iter_free( iter_target );
        if ( iter_parent ) gtk_tree_iter_free( iter_parent );

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
/*
    rc = zond_dbase_check_link( zond->dbase_zond->zond_dbase_work, baum, node_id,
            errmsg );
    if ( rc == -1 ) ERROR_S
    else if ( rc == 0 )
    {
        gint rc = 0; */
        gchar* icon_name = NULL;
        gchar* node_text = NULL;

        rc = zond_dbase_get_icon_name_and_node_text( zond->dbase_zond->zond_dbase_work,
                baum, node_id, &icon_name, &node_text, errmsg );
        if ( rc == -1 ) ERROR_S
        else if ( rc == 1 ) ERROR_S_MESSAGE( "node_id existiert nicht" )

        //neuen Knoten einfügen
        zond_tree_store_insert( ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[baum]) )),
                iter, child, &iter_inserted );

        //Daten rein
        zond_tree_store_set( &iter_inserted, icon_name, node_text, node_id );

        g_free( icon_name );
        g_free( node_text );
/*    }
    else
    {
       // zond_tree_store_insert_link( iter_target, node_id, )
    }
*/
    if ( iter_new ) *iter_new = iter_inserted;

    return 0;
}


//rekursive Funktion; gibt Zeiger auf 1. eingefügten Iter zurück (g_free)
gint
treeviews_db_to_baum_rec( Projekt* zond, gboolean with_younger_siblings,
        Baum baum, gint node_id, GtkTreeIter* iter, gboolean child, GtkTreeIter* iter_new,
        gchar** errmsg )
{
    gint rc = 0;
    GtkTreeIter iter_inserted = { 0, };
    gint first_child_id = 0;
    gint younger_sibling_id = 0;

    rc = treeviews_db_to_baum( zond, baum, node_id, iter, child, &iter_inserted, errmsg );
    if ( rc == 1 ) return 0;
    else if ( rc == -1 ) ERROR_S

    //Prüfen, ob Kind- oder Geschwisterknoten vorhanden
    first_child_id = zond_dbase_get_first_child( zond->dbase_zond->zond_dbase_work, baum, node_id, errmsg );
    if ( first_child_id < 0 ) ERROR_S

    if ( first_child_id > 0 )
    {
        gint rc = 0;
        rc = treeviews_db_to_baum_rec( zond, TRUE, baum, first_child_id,
                &iter_inserted, TRUE, NULL, errmsg );
        if ( rc ) ERROR_S
    }

    younger_sibling_id = zond_dbase_get_younger_sibling( zond->dbase_zond->zond_dbase_work, baum, node_id, errmsg );
    if ( younger_sibling_id < 0 ) ERROR_S

    if ( younger_sibling_id > 0 && with_younger_siblings )
    {
        gint rc = 0;

        rc = treeviews_db_to_baum_rec( zond, TRUE, baum,
                younger_sibling_id, &iter_inserted, FALSE, NULL, errmsg );
        if ( rc ) ERROR_S
    }

    if ( iter_new ) *iter_new = iter_inserted;

    return 0;
}


static gint
treeviews_reload_baum( Projekt* zond, Baum baum, gchar** errmsg )
{
    gint first_node_id = 0;

    zond_tree_store_clear( ZOND_TREE_STORE(gtk_tree_view_get_model(
            GTK_TREE_VIEW(zond->treeview[baum]) )) );

    first_node_id = zond_dbase_get_first_child( zond->dbase_zond->zond_dbase_work, baum, 0, errmsg );
    if ( first_node_id < 0 ) ERROR_S

    if ( first_node_id )
    {
        gint rc = 0;

        rc = treeviews_db_to_baum_rec( zond, TRUE, baum, first_node_id, NULL,
                TRUE, NULL, errmsg );
        if ( rc ) ERROR_S
    }

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
treeviews_knoten_verschieben( Projekt* zond, Baum baum, gint node_id, gint new_parent,
        gint new_older_sibling, gchar** errmsg )
{
    gint rc = 0;
    GtkTreeIter* iter = NULL;

    //kind verschieben
    rc = zond_dbase_verschieben_knoten( zond->dbase_zond->zond_dbase_work, baum, node_id, new_parent,
            new_older_sibling, errmsg );
    if ( rc ) ERROR_SOND (" zond_dbase_verschieben_knoten" )

    //Knoten im tree löschen
    //hierzu iter des verschobenen Kindknotens herausfinden
    iter = zond_treeview_abfragen_iter( ZOND_TREEVIEW(zond->treeview[baum]), node_id );

    zond_tree_store_remove( iter );
    gtk_tree_iter_free( iter );

    //jetzt neuen kindknoten aus db erzeugen
    //hierzu zunächst iter des Anker-Knotens ermitteln
    gboolean kind = FALSE;
    if ( new_older_sibling )
    {
        iter = zond_treeview_abfragen_iter( ZOND_TREEVIEW(zond->treeview[baum]), new_older_sibling );
        kind = FALSE;
    }
    else
    {
        iter = zond_treeview_abfragen_iter( ZOND_TREEVIEW(zond->treeview[baum]), new_parent );
        kind = TRUE;
    }

    //Knoten erzeugen
    rc = treeviews_db_to_baum_rec( zond, FALSE,
            baum, node_id, iter, kind, NULL, errmsg );
    gtk_tree_iter_free( iter );

    if ( rc ) ERROR_SOND( "db_baum_knoten_mit_kindern" )

    return 0;
}


//ermittelt node_id und iter des anchors, falls nicht in link eingefügt werden würde
gboolean
treeviews_get_anchor_id( Projekt* zond, gboolean* child, GtkTreeIter* iter, gint* anchor_id )
{
    GtkTreeIter iter_cursor = { 0, };

    if ( !sond_treeview_get_cursor( zond->treeview[zond->baum_active], &iter_cursor ) )
    {
        *child = TRUE;
        *anchor_id = 0;

        return TRUE;
    }

    if ( zond_tree_store_is_link( &iter_cursor ) )
    {
        gint head_nr = 0;

        head_nr = zond_tree_store_get_link_head_nr( &iter_cursor );

        if ( !head_nr || *child ) return FALSE;
        else *anchor_id = head_nr;
    }
    else gtk_tree_model_get( GTK_TREE_MODEL(zond_tree_store_get_tree_store(
            &iter_cursor )), &iter_cursor, 2, anchor_id, -1 );

    *iter = iter_cursor;

    return TRUE;
}


typedef struct {
    Projekt* zond;
    Baum baum_dest;
    gint anchor_id;
    GtkTreeIter* iter_dest;
    gboolean kind;
} SSelectionLink;


static gint
treeviews_paste_clipboard_as_link_foreach( SondTreeview* tree_view, GtkTreeIter* iter, gpointer data, gchar** errmsg )
{
    gint rc = 0;
    gint node_new = 0;
    Baum baum = KEIN_BAUM;
    gint node_id = 0;
    ZondTreeStore* tree_store_dest = NULL;
    GtkTreeIter iter_new = { 0, };

    SSelectionLink* s_selection = (SSelectionLink*) data;

    //anchor, im dest-baum
    //node ID, auf den link zeigen soll
    rc = treeviews_get_baum_and_node_id( s_selection->zond, iter, &baum, &node_id );
    if ( rc ) ERROR_S_MESSAGE( "Bei treeviews_get_baum_and_node_id:\nKein Baum gefunden" )

    rc = zond_dbase_begin( s_selection->zond->dbase_zond->zond_dbase_work, errmsg );
    if ( rc ) ERROR_S

    node_new = zond_dbase_insert_node( s_selection->zond->dbase_zond->zond_dbase_work,
            s_selection->baum_dest, s_selection->anchor_id, s_selection->kind, NULL,
            NULL, errmsg );
    if ( node_new < 0 ) ERROR_ROLLBACK( s_selection->zond->dbase_zond->zond_dbase_work )

    rc = zond_dbase_set_link( s_selection->zond->dbase_zond->zond_dbase_work,
            s_selection->baum_dest, node_new, NULL, sond_treeview_get_id( tree_view ),
            node_id, errmsg );
    if ( rc ) ERROR_ROLLBACK( s_selection->zond->dbase_zond->zond_dbase_work )

    rc = zond_dbase_commit( s_selection->zond->dbase_zond->zond_dbase_work, errmsg );
    if ( rc ) ERROR_ROLLBACK( s_selection->zond->dbase_zond->zond_dbase_work )

    tree_store_dest = ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(s_selection->zond->treeview[s_selection->baum_dest]) ));
    zond_tree_store_insert_link( iter, node_new, tree_store_dest,
            (s_selection->anchor_id) ? s_selection->iter_dest : NULL, s_selection->kind, &iter_new );

    *(s_selection->iter_dest) = iter_new;
    s_selection->anchor_id = node_new;
    s_selection->kind = FALSE;

    return 0;
}


//Ziel ist immer BAUM_AUSWERTUNG
gint
treeviews_paste_clipboard_as_link( Projekt* zond, Baum baum_dest, gint anchor_id,
        gboolean kind, GtkTreeIter* iter, gchar** errmsg )
{
    gint rc = 0;
    GtkTreeIter iter_anchor = { 0 };

    SSelectionLink s_selection = { zond, baum_dest, anchor_id, iter, kind };

    if ( iter ) iter_anchor = *iter;

    rc = sond_treeview_clipboard_foreach( zond->treeview[baum_dest],
            treeviews_paste_clipboard_as_link_foreach, &s_selection, errmsg );
    if ( rc == -1 ) ERROR_S

    if ( iter_anchor.stamp ) sond_treeview_expand_row( zond->treeview[baum_dest], &iter_anchor );
    sond_treeview_set_cursor( zond->treeview[baum_dest], s_selection.iter_dest );

    return 0;
}


typedef struct {
    Projekt* zond;
    Baum baum_dest;
    gint anchor_id;
    gboolean kind;
    GtkTreeIter* iter_dest;
} SSelectionKopieren;


static gint
treeviews_clipboard_kopieren_db( Projekt* zond, gboolean with_younger_siblings, Baum baum_von, gint node_von,
        gint node_nach, gboolean kind, gchar** errmsg )
{
    gint rc = 0;
    gint first_child_id = 0;
    gint new_node_id = 0;

    //wenn node_id_nach == 0 ist child egal, wird immer als kind eingesetzt
    new_node_id = zond_dbase_kopieren_nach_auswertung( zond->dbase_zond->zond_dbase_work, baum_von, node_von,
            node_nach, kind, errmsg );
    if ( new_node_id == -1 ) ERROR_SOND( "zond_dbase_kopieren_nach_auswertung" )

    //Prüfen, ob Kind- oder Geschwisterknoten vorhanden
    first_child_id = zond_dbase_get_first_child( zond->dbase_zond->zond_dbase_work, baum_von, node_von, errmsg );
    if ( first_child_id < 0 ) ERROR_SOND( "zond_dbase_get_first_child" )
    if ( first_child_id > 0 )
    {
        rc = treeviews_clipboard_kopieren_db( zond, TRUE, baum_von,
                first_child_id, new_node_id, TRUE, errmsg );
        if ( rc == -1  ) ERROR_SOND( "selection_copy_node_db" )
    }

    gint younger_sibling_id = 0;
    younger_sibling_id = zond_dbase_get_younger_sibling( zond->dbase_zond->zond_dbase_work, baum_von, node_von,
            errmsg );
    if ( younger_sibling_id < 0 ) ERROR_S
    if ( younger_sibling_id > 0 && with_younger_siblings )
    {
        rc = treeviews_clipboard_kopieren_db( zond, TRUE, baum_von,
                younger_sibling_id, new_node_id, FALSE, errmsg );
        if ( rc == -1 ) ERROR_S
    }

    return new_node_id;
}


static gint
treeviews_clipboard_kopieren_foreach( SondTreeview* tree_view, GtkTreeIter* iter, gpointer data, gchar** errmsg )
{
    gint rc = 0;
    GtkTreeIter iter_new = { 0, };
    gint node_id = 0;
    Baum baum = KEIN_BAUM;
    gint new_node_id = 0;

    SSelectionKopieren* s_selection = (SSelectionKopieren*) data;

    rc = treeviews_get_baum_and_node_id( s_selection->zond, iter, &baum, &node_id );
    if ( rc ) ERROR_S_MESSAGE( "Bei treeviews_get_baum_and_node_id:\nKein Baum gefunden" )

    rc = zond_dbase_begin( s_selection->zond->dbase_zond->zond_dbase_work, errmsg );
    if ( rc ) ERROR_S

    new_node_id = treeviews_clipboard_kopieren_db( s_selection->zond, FALSE, baum,
            node_id, s_selection->anchor_id, s_selection->kind, errmsg );
    if ( new_node_id == -1 ) ERROR_ROLLBACK( s_selection->zond->dbase_zond->zond_dbase_work )

    rc = treeviews_db_to_baum_rec( s_selection->zond, FALSE,
            BAUM_AUSWERTUNG, new_node_id, (s_selection->anchor_id) ? s_selection->iter_dest : NULL, s_selection->kind, &iter_new, errmsg );
    if ( rc ) ERROR_ROLLBACK( s_selection->zond->dbase_zond->zond_dbase_work )

    rc = zond_dbase_commit( s_selection->zond->dbase_zond->zond_dbase_work, errmsg );
    if ( rc ) ERROR_ROLLBACK( s_selection->zond->dbase_zond->zond_dbase_work )

    *(s_selection->iter_dest) = iter_new;
    s_selection->anchor_id = new_node_id;
    s_selection->kind = FALSE;

    return 0;
}


gint
treeviews_clipboard_kopieren( Projekt* zond, Baum baum_dest, gint anchor_id,
        gboolean kind, GtkTreeIter* iter, gchar** errmsg )
{
    gint rc = 0;
    GtkTreeIter iter_origin = { 0 };
    SSelectionKopieren s_selection = { zond, baum_dest, anchor_id, kind, iter };

    //falls in existierenden iter eigenfügt wird: diesen merken
    if ( kind && iter ) iter_origin = *iter;

    rc = sond_treeview_clipboard_foreach( zond->treeview[baum_dest],
            treeviews_clipboard_kopieren_foreach, &s_selection, errmsg );
    if ( rc == -1 ) ERROR_S

    if ( kind && iter_origin.stamp ) sond_treeview_expand_row(
            zond->treeview[baum_dest], &iter_origin );
    sond_treeview_set_cursor( zond->treeview[baum_dest], s_selection.iter_dest );

    return 0;
}


typedef struct {
    Projekt* zond;
    Baum baum;
    gint parent_id;
    gint older_sibling_id;
} SSelectionVerschieben;


static gint
treeeviews_clipboard_verschieben_foreach( SondTreeview* tree_view, GtkTreeIter* iter,
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
        if ( rc == -1 ) ERROR_S
        else if ( rc == 0 ) return 0;
    }

    rc = treeviews_knoten_verschieben( s_selection->zond, s_selection->baum, node_id, s_selection->parent_id,
            s_selection->older_sibling_id, errmsg );
    if ( rc ) ERROR_S

    s_selection->older_sibling_id = node_id;

    return 0;
}


//Ist immer verschieben innerhalb des Baums
gint
treeviews_clipboard_verschieben( Projekt* zond, Baum baum, gint anchor_id, gboolean kind,
        gchar** errmsg )
{
    gint rc = 0;
    Clipboard* clipboard = NULL;
    SSelectionVerschieben s_selection = { zond, baum, 0, 0 };

    if ( kind ) s_selection.parent_id = anchor_id;
    else
    {
        s_selection.parent_id = zond_dbase_get_parent( zond->dbase_zond->zond_dbase_work, baum, anchor_id, errmsg );
        if ( s_selection.parent_id < 0 ) ERROR_S

        s_selection.older_sibling_id = anchor_id;
    }

    rc = sond_treeview_clipboard_foreach( zond->treeview[baum],
            treeeviews_clipboard_verschieben_foreach, &s_selection, errmsg );
    if ( rc == -1 ) ERROR_S

    //Alte Auswahl löschen
    clipboard = sond_treeview_get_clipboard( zond->treeview[baum] );
    if ( clipboard->arr_ref->len > 0 ) g_ptr_array_remove_range( clipboard->arr_ref,
            0, clipboard->arr_ref->len );

    gtk_widget_queue_draw( GTK_WIDGET(zond->treeview[baum]) );

    GtkTreeIter* iter = zond_treeview_abfragen_iter(
            ZOND_TREEVIEW(zond->treeview[baum]), anchor_id );
    if ( !iter ) ERROR_S_MESSAGE( "zond_treeview_abfrageb_iter findet node_id nicht" )

    if ( kind && anchor_id ) sond_treeview_expand_row( zond->treeview[baum], iter );
    sond_treeview_set_cursor( zond->treeview[baum], iter );

    gtk_tree_iter_free( iter );

    return 0;
}


