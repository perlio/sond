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

#include "../../misc.h"
#include "../zond_treeview.h"
#include "../zond_tree_store.h"


gboolean
ziele_1_gleich_2( const Anbindung anbindung1, const Anbindung anbindung2 )
{
    if ( (anbindung1.von.seite == anbindung2.von.seite) &&
            (anbindung1.bis.seite == anbindung2.bis.seite) &&
            (anbindung1.von.index == anbindung2.von.index) &&
            (anbindung1.bis.index == anbindung2.bis.index) ) return TRUE;
    else return FALSE;
}


static gint
ziele_vergleiche_pdf_pos( PdfPos pdf_pos1, PdfPos pdf_pos2 )
{
    if ( pdf_pos1.seite < pdf_pos2.seite ) return -1;
    else if ( pdf_pos1.seite > pdf_pos2.seite ) return 1;
    //wenn gleiche Seite:
    else if ( pdf_pos1.index < pdf_pos2.index ) return -1;
    else if ( pdf_pos1.index > pdf_pos2.index ) return 1;

    return 0;
}


static gboolean
ziele_anbindung_is_pdf_punkt( Anbindung anbindung )
{
    if ( (anbindung.von.seite || anbindung.von.index) &&
            !anbindung.bis.seite && !anbindung.bis.index ) return TRUE;

    return FALSE;
}


static gboolean
ziele_1_vor_2( Anbindung anbindung1, Anbindung anbindung2 )
{
    if ( ziele_1_gleich_2( anbindung1, anbindung2 ) ) return FALSE;

    if ( ziele_anbindung_is_pdf_punkt( anbindung1 ) )
    {
        if ( ziele_vergleiche_pdf_pos( anbindung1.von, anbindung2.von ) == -1 ) return TRUE;
        else return FALSE;
    }
    else if ( ziele_anbindung_is_pdf_punkt( anbindung2 ) )
    {
        if ( ziele_vergleiche_pdf_pos( anbindung1.bis, anbindung2.von ) == -1 ) return TRUE;
        else return FALSE;

    }
    else //beides komplette Anbindung
            if ( ziele_vergleiche_pdf_pos( anbindung1.bis, anbindung2.von ) == -1 ) return TRUE;

    return FALSE;
}




gboolean
ziele_1_eltern_von_2( Anbindung anbindung1, Anbindung anbindung2 )
{
    gint pos_anfang = 0;
    gint pos_ende = 0;

    //PdfPunkt kann niemals Eltern sein.
    if ( ziele_anbindung_is_pdf_punkt( anbindung1 ) ) return FALSE;

    //Gleiche können nicht Nachfolger sein
    if ( ziele_1_gleich_2( anbindung1, anbindung2 ) ) return FALSE;

    //wenn Anbindung1 Datei ist, dann ist sie Eltern
    if ( !anbindung1.von.seite && !anbindung1.von.index &&
            !anbindung1.bis.seite && !anbindung1.bis.index ) return FALSE;
    //auch wenn Anbindung2 Datei
    if ( !anbindung1.von.seite && !anbindung2.von.index &&
            !anbindung2.bis.seite && !anbindung2.bis.index ) return FALSE;

    pos_anfang = ziele_vergleiche_pdf_pos( anbindung1.von, anbindung2.von );
    pos_ende = ziele_vergleiche_pdf_pos( anbindung1.bis, anbindung2.bis );

    if ( pos_anfang > 0 ) return FALSE; //fängt schon später an...
    else //fängt entweder davor oder gleich an
    {
        if ( pos_anfang == 0 && pos_ende <= 0 ) return FALSE; //Fängt gleich an, hört nicht später auf...
        else if ( pos_anfang < 0 && pos_ende < 0 ) return FALSE; //Fängt vorher an, hört nicht mindestens gleich auf
    }

    return TRUE;
}


static gint
zond_anbindung_verschieben_kinder( Projekt* zond,
                          gint node_id,
                          GtkTreeIter* iter,
                          Anbindung anbindung,
                          GError** error )
{
    gboolean child = TRUE;
    gint anchor_id = node_id;

    do
    {
        gint rc = 0;
        gint younger_sibling = 0;
        Anbindung anbindung_y = { 0 };

        rc = zond_dbase_get_younger_sibling( zond->dbase_zond->zond_dbase_work,
                node_id, &younger_sibling, error );
        if ( rc ) ERROR_Z

        if ( younger_sibling == 0 ) break;

        rc = zond_dbase_get_node( zond->dbase_zond->zond_dbase_work,
                younger_sibling, NULL, NULL, NULL, &anbindung_y.von.seite, &anbindung_y.von.index,
                &anbindung_y.bis.seite, &anbindung_y.bis.index, NULL, NULL, NULL, error );
        if ( rc ) ERROR_Z

        if ( ziele_1_eltern_von_2( anbindung, anbindung_y ) )
        {
            gint rc = 0;
            GtkTreeIter iter_younger_sibling = { 0 };

            rc = zond_dbase_verschieben_knoten( zond->dbase_zond->zond_dbase_work,
                    younger_sibling, anchor_id, child, error );
            if ( rc ) ERROR_Z

            iter_younger_sibling = *iter;
            if ( !gtk_tree_model_iter_next( GTK_TREE_MODEL(zond_tree_store_get_tree_store( iter )),
                    &iter_younger_sibling ) )
            {
                if ( error ) *error = g_error_new( ZOND_ERROR, 0, "Kein iter für jüngeres Geschwister" );

                return -1;
            }

            zond_tree_store_move_node(
                    &iter_younger_sibling, ZOND_TREE_STORE(gtk_tree_view_get_model(
                    GTK_TREE_VIEW(zond->treeview[BAUM_INHALT]) )), iter, TRUE, NULL );

            anchor_id = younger_sibling;
            child = FALSE;
        }
        else break;
    } while ( 1 );

    return 0;
}


static gint
zond_anbindung_baum_inhalt( Projekt* zond, gint anchor_id_pdf_abschnitt, gboolean child,
        gint id_inserted, Anbindung anbindung, gchar const* rel_path, gchar const* node_text, GError** error )
{
    gint rc = 0;
    GtkTreeIter* iter = NULL;
    GtkTreeIter iter_inserted = { 0 };
    gint id_baum_inhalt = 0;
    gint anchor_id = 0;

    //Gucken, ob Anbindung im Baum-Inhalt aufscheint
    rc = zond_dbase_get_baum_inhalt_file_from_rel_path( zond->dbase_zond->zond_dbase_work,
            rel_path, &id_baum_inhalt, error );
    if ( rc ) ERROR_Z
    if ( !id_baum_inhalt )
    {
        gint rc = 0;

        rc = zond_dbase_get_baum_inhalt_pdf_abschnitt_from_anbindung( zond->dbase_zond->zond_dbase_work,
                rel_path, anbindung, &id_baum_inhalt, NULL, error );
        if ( rc ) ERROR_Z

        if ( !id_baum_inhalt ) return 0;
    }

    if ( !anchor_id_pdf_abschnitt ) anchor_id = id_baum_inhalt;
    else anchor_id = anchor_id_pdf_abschnitt;

    //eingefügtes ziel in Baum
    iter = zond_treeview_abfragen_iter( ZOND_TREEVIEW(zond->treeview[BAUM_INHALT]), anchor_id );
    if ( !iter )
    {
        if ( error ) *error = g_error_new( ZOND_ERROR, 0,
                "%s\nzond_treeview_abfragen_iter gibt NULL zurück", __func__ );

        return -1;
    }

    zond_tree_store_insert( zond_tree_store_get_tree_store( iter ), iter, child, &iter_inserted );
    gtk_tree_iter_free( iter );
    zond_tree_store_set( &iter_inserted, zond->icon[ICON_ANBINDUNG].icon_name, node_text, id_inserted );

    if ( child )
    {
        gint rc = 0;

        rc = zond_anbindung_verschieben_kinder( zond, id_inserted, &iter_inserted, anbindung, error );
        if ( rc ) ERROR_Z
    }

    if ( child ) sond_treeview_expand_row( zond->treeview[BAUM_INHALT], &iter_inserted );
    sond_treeview_set_cursor_on_text_cell( zond->treeview[BAUM_INHALT], &iter_inserted );
    gtk_widget_grab_focus( GTK_WIDGET(zond->treeview[BAUM_INHALT]) );

    return 0;
}


gint
ziele_abfragen_anker_rek( ZondDBase* zond_dbase, Anbindung anbindung,
        gint anchor_id, gint* anchor_id_new, gboolean* kind, GError** error )
{
    gint rc = 0;
    Anbindung anbindung_anchor = { 0 };

    rc = zond_dbase_get_node( zond_dbase, anchor_id, NULL, NULL, NULL,
            &anbindung_anchor.von.seite, &anbindung_anchor.von.index,
            &anbindung_anchor.bis.seite, &anbindung_anchor.bis.index,
            NULL, NULL, NULL, error );
    if ( rc ) ERROR_Z

    //ziele auf Identität prüfen
    if ( ziele_1_gleich_2( anbindung_anchor, anbindung ) )
    {
        if ( error ) *error = g_error_new( ZOND_ERROR, 0,
                "%s\nIdentischer Abschnitt wurde bereits angebunden", __func__ );

        return -1;
    }
    //Knoten kommt ist kind - anchor "weiter" oder root
    else if ( ziele_1_eltern_von_2( anbindung_anchor, anbindung ) ||
            (anbindung_anchor.von.seite == 0 && anbindung_anchor.von.index == 0 &&
             anbindung_anchor.bis.seite == 0 && anbindung_anchor.bis.index == 0) )
    {
        gint rc = 0;
        gint first_child_id = 0;

        *kind = TRUE;
        *anchor_id_new = anchor_id;

        rc = zond_dbase_get_first_child( zond_dbase, anchor_id, &first_child_id, error );
        if ( rc ) ERROR_Z

        if ( first_child_id > 0 ) //hat kind
        {
            gint rc = 0;

            rc = ziele_abfragen_anker_rek( zond_dbase, anbindung, first_child_id, anchor_id_new,
                    kind, error );
            if ( rc )
            {
                g_prefix_error( error, "%s\n", __func__ );

                return -1;
            }
        }
    }
    //Seiten oder Punkte vor den einzufügenden Punkt
    else if ( ziele_1_vor_2( anbindung_anchor, anbindung ) )
    {
        gint rc = 0;
        gint younger_sibling_id = 0;

        rc = zond_dbase_get_younger_sibling( zond_dbase, anchor_id, &younger_sibling_id,
                error );
        if ( rc ) ERROR_Z

        *kind = FALSE;
        *anchor_id_new = anchor_id;

        if ( younger_sibling_id > 0 )
        {
            gint rc = 0;

            rc = ziele_abfragen_anker_rek( zond_dbase, anbindung, younger_sibling_id,
                    anchor_id_new, kind, error );
            if ( rc ) ERROR_Z
        }
    }
    // wenn nicht danach, bleibt ja nur Überschneidung!!
    else if ( !ziele_1_vor_2( anbindung, anbindung_anchor ) &&
            !ziele_1_eltern_von_2( anbindung, anbindung_anchor ) )
    {
        if ( error ) *error = g_error_new( ZOND_ERROR, 0,
                "Eingegebenes Ziel überschneidet "
                "sich mit bereits bestehendem Ziel" );

        return -1;
    }

    return 0;
}


static gint
zond_anbindung_insert_pdf_abschnitt_in_dbase( Projekt* zond,
        const gchar* rel_path, Anbindung anbindung, gint* anchor_pdf_abschnitt,
        gboolean* child, gint* node_inserted, gchar** node_text, GError** error )
{
    gint pdf_root = 0;
    gint node_id_new = 0;
    gint rc = 0;
    gint anchor_id_dbase = 0;
    gboolean created = FALSE;

    rc = zond_dbase_get_or_create_pdf_root( zond->dbase_zond->zond_dbase_work,
            rel_path, &pdf_root, &created, error );
    if ( rc ) ERROR_Z

    anchor_id_dbase = pdf_root;

    if ( created )
    {
        gint rc = 0;

        rc = zond_dbase_update_icon_name( zond->dbase_zond->zond_dbase_work,
                pdf_root, "pdf", error );
        if ( rc ) ERROR_Z

        rc = zond_dbase_update_node_text( zond->dbase_zond->zond_dbase_work,
                pdf_root, g_strrstr( rel_path, "/" ) + 1, error );
        if ( rc ) ERROR_Z

        *child = TRUE; //erster Abschnitt
    }
    else
    {
        gint rc = 0;

        //ansonsten: vergleichen,
        rc = ziele_abfragen_anker_rek( zond->dbase_zond->zond_dbase_work, anbindung,
                pdf_root, &anchor_id_dbase, child, error );
        if ( rc ) ERROR_Z

        if ( anchor_id_dbase != pdf_root ) *anchor_pdf_abschnitt = anchor_id_dbase;
    }

    *node_text = g_strdup_printf( "S. %i", anbindung.von.seite + 1 );
    if ( anbindung.von.index ) *node_text = add_string( *node_text, g_strdup_printf( ", Index %d", anbindung.von.index ) );
    if ( anbindung.bis.seite || anbindung.bis.index )
            *node_text = add_string( *node_text, g_strdup_printf( " - S. %d", anbindung.bis.seite + 1 ) );
    if ( anbindung.bis.index != EOP ) *node_text = add_string( *node_text, g_strdup_printf( ", Index %d", anbindung.bis.index ) );

    node_id_new = zond_dbase_insert_node( zond->dbase_zond->zond_dbase_work, anchor_id_dbase, *child,
            ZOND_DBASE_TYPE_PDF_ABSCHNITT, 0, rel_path,
            anbindung.von.seite, anbindung.von.index,
            anbindung.bis.seite, anbindung.bis.index,
            zond->icon[ICON_ANBINDUNG].icon_name, *node_text, NULL, error );
    if ( node_id_new == -1 ) ERROR_Z

    if ( node_inserted ) *node_inserted = node_id_new;

    return 0;
}


static gint
zond_anbindung_fm( Projekt* zond, gchar const* rel_path, Anbindung anbindung,
        gchar const* node_text, GError** error )
{
    //ToDo:
    //ist rel_path sichtbar?
    //nein: return

    //hat rel_path Kinder?
    //nein: dummy einfügen
    //ja:
        //ist rel_path expanded?
        //nein: return
        //ja: Kinder durchgehen: Anbindung Eltern?

    return 0;
}


static gint
zond_anbindung_trees( Projekt* zond, gint anchor_pdf_abschnitt, gboolean child,
        gint node_inserted, Anbindung anbindung, gchar const* rel_path, gchar const* node_text, GError** error )
{
    gint rc = 0;

    rc = zond_anbindung_baum_inhalt( zond, anchor_pdf_abschnitt, child, node_inserted, anbindung,
            rel_path, node_text, error );
    if ( rc ) ERROR_Z

    rc = zond_anbindung_fm( zond, rel_path, anbindung, node_text, error );
    if ( rc ) ERROR_Z

    return 0;
}


gint
zond_anbindung_erzeugen( PdfViewer* pv, GError** error )
{
    gint rc = 0;
    gboolean child = FALSE;
    gint node_inserted = 0;
    gchar* node_text = NULL;
    gint anchor_pdf_abschnitt = 0;

    rc = zond_anbindung_insert_pdf_abschnitt_in_dbase( pv->zond,
            pv->rel_path, pv->anbindung, &anchor_pdf_abschnitt, &child,
            &node_inserted, &node_text, error );
    if ( rc ) ERROR_Z

    rc = zond_anbindung_trees( pv->zond, anchor_pdf_abschnitt, child, node_inserted, pv->anbindung,
            pv->rel_path, node_text, error );
    g_free( node_text );
    if ( rc ) ERROR_Z


    return 0;
}


