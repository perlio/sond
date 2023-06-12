/*
zond (ziele.c) - Akten, Beweisstücke, Unterlagen
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

#include <glib/gstdio.h>
#include <sqlite3.h>
#include <gtk/gtk.h>

#include "../zond_pdf_document.h"

#include "../global_types.h"
#include "../zond_dbase.h"

#include "../99conv/general.h"
#include "../99conv/pdf.h"

#include "../40viewer/viewer.h"
#include "../40viewer/document.h"

#include "zieleplus.h"
#include "treeviews.h"

#include "../../misc.h"
#include "../zond_treeview.h"



gboolean
ziele_1_gleich_2( const Anbindung anbindung1, const Anbindung anbindung2 )
{
    if ( (anbindung1.von.seite == anbindung2.von.seite) &&
            (anbindung1.bis.seite == anbindung2.bis.seite) &&
            (anbindung1.von.index == anbindung2.von.index) &&
            (anbindung1.bis.index == anbindung2.bis.index) ) return TRUE;
    else return FALSE;
}


static gboolean
ziele_1_vor_2( Anbindung anbindung1, Anbindung anbindung2 )
{
    if ( (anbindung1.bis.seite < anbindung2.von.seite) ||
            ((anbindung1.bis.seite == anbindung2.von.seite) &&
            (anbindung1.bis.index < anbindung2.von.index)) ) return TRUE;
    else return FALSE;
}


static gboolean
ziele_1_eltern_von_2( Anbindung anbindung1, Anbindung anbindung2 )
{
    if ( ((anbindung1.von.seite < anbindung2.von.seite) ||
            ((anbindung1.von.seite == anbindung2.von.seite) &&
            (anbindung1.von.index <= anbindung2.von.index)))
            &&
            //hinten:
            ((anbindung1.bis.seite > anbindung2.bis.seite) ||
            ((anbindung1.bis.seite == anbindung2.bis.seite) &&
            (anbindung1.bis.index >= anbindung2.bis.index))) ) return TRUE;
    else return FALSE;
}



static gint
ziele_verschieben_kinder( Projekt* zond,
                          gint node_id,
                          Anbindung anbindung,
                          gchar** errmsg )
{
    gint rc = 0;

/*  kinder an node_id anhängen  */
    //younger_sibling ermittelm
    gint younger_sibling = 0;
    gint older_sibling = 0;

    while ( (younger_sibling = zond_dbase_get_younger_sibling( zond->dbase_zond->zond_dbase_work, BAUM_INHALT,
            node_id, errmsg )) )
    {
        if ( younger_sibling < 0 ) ERROR_S

        Anbindung* anbindung_younger_sibling = NULL;
        rc = treeviews_get_rel_path_and_anbindung( zond, BAUM_INHALT, younger_sibling,
                NULL, &anbindung_younger_sibling, errmsg );
        if ( rc == -1 ) ERROR_S
        else if ( rc ) ERROR_S_MESSAGE( "Keine Anbindung gespeichert" )
        else
        {
            gboolean kind = ziele_1_eltern_von_2( anbindung,
                    *anbindung_younger_sibling );
            g_free( anbindung_younger_sibling );
            if ( !kind ) break;
        }

        rc = treeviews_knoten_verschieben( zond, BAUM_INHALT, younger_sibling, node_id,
                older_sibling, errmsg );
        if ( rc ) ERROR_S
        older_sibling = younger_sibling;
    }

    return 0;
}


static gint
ziele_einfuegen_db( Projekt* zond, gint anchor_id, gboolean kind, Ziel* ziel,
        const gchar* node_text, gchar** errmsg )
{
    gint rc = 0;
    gint new_node = 0;

    //zuerst in baum_inhalt-Tabelle (weil FK in Tabelle "ziele" darauf Bezug nimmt)
    new_node = zond_dbase_insert_node( zond->dbase_zond->zond_dbase_work, BAUM_INHALT, anchor_id, kind,
            zond->icon[ICON_ANBINDUNG].icon_name, node_text, errmsg );
    if ( new_node == -1 ) ERROR_S

    rc = zond_dbase_set_ziel( zond->dbase_zond->zond_dbase_work, ziel, anchor_id, errmsg );
    if ( rc ) ERROR_S

    return new_node;
}


static gint
ziele_einfuegen_anbindung( Projekt* zond, const gchar* rel_path, gint anchor_id,
        gboolean kind, Anbindung anbindung, Ziel* ziel, gchar** errmsg )
{
    gint rc = 0;
    gint new_node = 0;
    GtkTreeIter iter_new = { 0, };
    GtkTreeIter iter_origin = { 0 };

    gchar* node_text = NULL;
    if ( anbindung.bis.seite > anbindung.von.seite) node_text =
            g_strdup_printf( "S. %i - %i, %s", anbindung.von.seite + 1,
            anbindung.bis.seite + 1, rel_path );
    else node_text = g_strdup_printf( "S. %i, %s", anbindung.von.seite + 1,
            rel_path );

    rc = zond_dbase_begin( zond->dbase_zond->zond_dbase_work, errmsg );
    if ( rc )
    {
        g_free( node_text );

        ERROR_S
    }

    new_node = ziele_einfuegen_db( zond, anchor_id, kind, ziel, node_text, errmsg );
    g_free( node_text );
    if ( new_node == -1) ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )

    //eingefügtes ziel in Baum
    GtkTreeIter* iter = zond_treeview_abfragen_iter( ZOND_TREEVIEW(zond->treeview[BAUM_INHALT]), anchor_id );
    if ( !iter ) ERROR_S_MESSAGE( "node_id nicht gefunden" )

    iter_origin = *iter;
    gtk_tree_iter_free( iter );

    rc = treeviews_db_to_baum( zond, BAUM_INHALT, new_node, &iter_origin, kind, &iter_new,
            errmsg );
    if ( rc ) ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )

    rc = ziele_verschieben_kinder( zond, new_node, anbindung, errmsg );
    if ( rc ) ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )

    rc = zond_dbase_commit( zond->dbase_zond->zond_dbase_work, errmsg );
    if ( rc ) ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )

    if ( kind ) sond_treeview_expand_row( zond->treeview[BAUM_INHALT], &iter_origin);
    sond_treeview_set_cursor_on_text_cell( zond->treeview[BAUM_INHALT], &iter_new );
    gtk_widget_grab_focus( GTK_WIDGET(zond->treeview[BAUM_INHALT]) );

    return new_node;
}


static gint
ziele_erzeugen_ziel( GtkWidget* window, const DisplayedDocument* dd,
        Anbindung anbindung, Ziel* ziel, gchar** errmsg )
{
    gint rc = 0;
    fz_context* ctx = NULL;
    pdf_document* doc = NULL;

    ctx = zond_pdf_document_get_ctx( dd->zond_pdf_document );
    doc = zond_pdf_document_get_pdf_doc( dd->zond_pdf_document );

    zond_pdf_document_mutex_lock( dd->zond_pdf_document );

    //schon nameddest zur Seite?
    rc = pdf_document_get_dest( ctx, doc, anbindung.von.seite,
            (gpointer*) &ziel->ziel_id_von, TRUE, errmsg );
    if ( rc )
    {
        zond_pdf_document_mutex_unlock( dd->zond_pdf_document );

        ERROR_S
    }

    //nameddest herausfinden bzw. einfügen
    rc = pdf_document_get_dest( ctx, doc, anbindung.bis.seite,
            (gpointer*) &ziel->ziel_id_bis, TRUE, errmsg );
    if ( rc )
    {
        zond_pdf_document_mutex_unlock( dd->zond_pdf_document );

        ERROR_S
    }

    gint page_number1 = -1;
    gint page_number2 = -1;

    if ( !ziel->ziel_id_von ) page_number1 = anbindung.von.seite;
    if ( !ziel->ziel_id_bis ) page_number2 = anbindung.bis.seite;

    if ( page_number1 >= 0 || page_number2 >= 0 )
    {
        if ( zond_pdf_document_is_dirty( dd->zond_pdf_document ) )
        {
            rc = abfrage_frage( window, "Änderungen müssen vor Einfügen von "
                    "Anbindungen gespeichert werden", "Änderungen speichern?", NULL );
            if ( rc == GTK_RESPONSE_YES )
            {
                rc = zond_pdf_document_save( dd->zond_pdf_document, errmsg );
                if ( rc )
                {
                    zond_pdf_document_mutex_unlock( dd->zond_pdf_document );
                    ERROR_S_VAL( -2 )
                }
            }
            else
            {
                zond_pdf_document_mutex_unlock( dd->zond_pdf_document );

                return 1;
            }
        }
        else zond_pdf_document_close_doc_and_pages( dd->zond_pdf_document );

        //namedDest einfügen
        rc = SetDestPage( dd, page_number1, page_number2,
                &ziel->ziel_id_von, &ziel->ziel_id_bis, errmsg );
        if ( rc )
        {
            display_message( window, "Anbindung konnte nicht erzeugt "
                    "werden\n\nBei Aufruf SetDestPage:\n", *errmsg, NULL );
            g_free( *errmsg );

            rc = zond_pdf_document_reopen_doc_and_pages( dd->zond_pdf_document, errmsg );
            zond_pdf_document_mutex_unlock( dd->zond_pdf_document );
            if ( rc ) ERROR_SOND_VAL( "zond_pdf_document_reopen_doc_and_pages", -2 )

            return 1;
        }

        rc = zond_pdf_document_reopen_doc_and_pages( dd->zond_pdf_document, errmsg );
        if ( rc )
        {
            zond_pdf_document_mutex_unlock( dd->zond_pdf_document );
            ERROR_SOND_VAL( "zond_pdf_document_reopen_doc_and_pages", -2 )
        }
    }

    zond_pdf_document_mutex_unlock( dd->zond_pdf_document );

    ziel->index_von = anbindung.von.index;
    ziel->index_bis = anbindung.bis.index;

    return 0;
}


gint
ziele_abfragen_anker_rek( Projekt* zond, gint node_id, Anbindung anbindung,
        gboolean* kind, gchar** errmsg )
{
    gint rc = 0;
    gint new_node_id = 0;
    gint first_child_id = 0;

    if ( node_id == 0 ) return 0;

    Anbindung* anbindung_node_id = NULL;
    rc = treeviews_get_rel_path_and_anbindung( zond, BAUM_INHALT, node_id, NULL,
            &anbindung_node_id, errmsg );
    if ( rc == -1 ) ERROR_SOND( "abfragen_rel_path_and_anbindung" )
    else if ( rc ) //Datei, hat ja kein ziel gespeichert
    {
        *kind = TRUE;
        new_node_id = node_id;

        first_child_id = zond_dbase_get_first_child( zond->dbase_zond->zond_dbase_work, BAUM_INHALT,
                node_id, errmsg );
        if ( first_child_id < 0 ) ERROR_SOND( "zond_dbase_get_first_child" )
        else if ( first_child_id > 0 ) //hat kind
        {
            gint res = ziele_abfragen_anker_rek( zond, first_child_id, anbindung,
                    kind, errmsg );
            if ( res == -1 ) return -1; //bei rekursivem Aufruf Fehlermeldung nicht "aufblasen"

            if ( res > 0 ) new_node_id = res;
        }
    }
    else //anbindung zurück
    {
        //ziele auf Identität prüfen
        if ( ziele_1_gleich_2( *anbindung_node_id, anbindung ) )
        {
            g_free( anbindung_node_id );

            ERROR_SOND( "Einzufügendes Ziel mit bestehendem Ziel identisch" )
        }

        //Knoten kommt als parent in Beracht
        if ( ziele_1_eltern_von_2( *anbindung_node_id, anbindung ) )
        {
            *kind = TRUE;
            new_node_id = node_id;

            first_child_id = zond_dbase_get_first_child( zond->dbase_zond->zond_dbase_work, BAUM_INHALT,
                    node_id, errmsg );
            if ( first_child_id < 0 )
            {
                g_free( anbindung_node_id );

                ERROR_SOND( "zond_dbase_get_first_child" )
            }
            else if ( first_child_id > 0 ) //hat kind
            {
                gint res = ziele_abfragen_anker_rek( zond, first_child_id, anbindung,
                        kind, errmsg );
                if ( res == -1 )
                {
                    g_free( anbindung_node_id );

                    return -1;
                }

                if ( res > 0 ) new_node_id = res;
            }
        }
        //Seiten oder Punkte vor den einzufügenden Punkt
        else if ( ziele_1_vor_2( *anbindung_node_id, anbindung ) )
        {
            *kind = FALSE;
            new_node_id = node_id;

            gint younger_sibling_id = zond_dbase_get_younger_sibling( zond->dbase_zond->zond_dbase_work, BAUM_INHALT,
                    node_id, errmsg );
            if ( younger_sibling_id < 0 )
            {
                g_free( anbindung_node_id );

                ERROR_SOND( "zond_dbase_get_younger_sibling" )
            }

            if ( younger_sibling_id != 0 )
            {
                gint res = ziele_abfragen_anker_rek( zond, younger_sibling_id, anbindung,
                        kind, errmsg );
                if ( res == -1 )
                {
                    g_free( anbindung_node_id );

                    return -1;
                }

                if ( res > 0 ) new_node_id = res;
            }
        }
        // wenn nicht danach, bleibt ja nur Überschneidung!!
        else if ( !ziele_1_vor_2( anbindung, *anbindung_node_id ) &&
                !ziele_1_eltern_von_2( anbindung, *anbindung_node_id ) )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Eingegebenes Ziel überschneidet "
                    "sich mit bereits bestehendem Ziel", NULL );
            g_free( anbindung_node_id );

            return -1;
        }
    }

    return new_node_id;
}


static void
ziele_free( Ziel* ziel )
{
    if ( !ziel ) return;

    g_free( ziel->ziel_id_von );
    g_free( ziel->ziel_id_bis );

    g_free( ziel );

    return;
}


gint
ziele_erzeugen_anbindung( PdfViewer* pv, gint* ptr_new_node, gchar** errmsg )
{
    gint rc = 0;
    gint node_id = 0;
    gint anchor_id = 0;
    gboolean kind = FALSE;
    Ziel* ziel = NULL;
    DisplayedDocument* dd_von = NULL;
    gint page_doc_von = 0;
    DisplayedDocument* dd_bis = NULL;
    gint page_doc_bis = 0;
    Anbindung anbindung = { 0 };

    dd_von = document_get_dd( pv, pv->anbindung.von.seite, NULL, NULL, &page_doc_von );
    dd_bis = document_get_dd( pv, pv->anbindung.bis.seite, NULL, NULL, &page_doc_bis );

    if ( dd_von != dd_bis ) return 2;

    anbindung.von.seite = page_doc_von;
    anbindung.von.index = pv->anbindung.von.index;
    anbindung.bis.seite = page_doc_bis;
    anbindung.bis.index = pv->anbindung.bis.index;

    node_id = zond_dbase_get_node_id_from_rel_path( pv->zond->dbase_zond->zond_dbase_work,
            zond_pdf_document_get_path( dd_von->zond_pdf_document ), errmsg );
    if ( node_id == -1 ) ERROR_S
    else if ( node_id == 0 ) ERROR_S_MESSAGE( "Datei nicht vorhanden" )

    //Kinder von Knoten mit DateiID=datei_id durchgehen
    anchor_id = ziele_abfragen_anker_rek( pv->zond, node_id, anbindung, &kind, errmsg );
    if ( anchor_id == -1 ) ERROR_S

    ziel = g_malloc0( sizeof( Ziel ) );

    rc = ziele_erzeugen_ziel( pv->vf, dd_von, anbindung, ziel, errmsg );
    if ( rc ) ziele_free( ziel );
    if ( rc == 1 ) return 1;
    else if ( rc == -1 ) ERROR_SOND( "ziele_erzeugen_ziel" )
    else if ( rc == -2 ) ERROR_SOND_VAL( "ziele_erzeugen_ziel", -2 )

    rc = ziele_einfuegen_anbindung( pv->zond,
            zond_pdf_document_get_path( dd_von->zond_pdf_document ), anchor_id, kind,
            anbindung, ziel, errmsg );
    ziele_free( ziel );
    if ( rc == -1 ) ERROR_SOND( "ziele_einfuegen_anbindung" )

    if ( ptr_new_node ) *ptr_new_node = rc;

    return 0;
}


