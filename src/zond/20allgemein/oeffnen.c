/*
zond (oeffnen.c) - Akten, Beweisstücke, Unterlagen
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

#include "../../misc.h"
#include "../../sond_treeview.h"

#include "../zond_pdf_document.h"
#include "../global_types.h"
#include "../zond_dbase.h"
#include "../zond_tree_store.h"

#include "../99conv/pdf.h"
#include "../99conv/general.h"

#include "ziele.h"
#include "project.h"

#include "../40viewer/document.h"
#include "../40viewer/viewer.h"


/*
static gboolean
oeffnen_dd_sind_gleich( DisplayedDocument* dd1, DisplayedDocument* dd2 )
{
    DisplayedDocument* dd1_ptr = dd1;
    DisplayedDocument* dd2_ptr = dd2;

    do
    {
        if ( dd1_ptr->zond_pdf_document != dd2_ptr->zond_pdf_document ) return FALSE;
        if ( (dd1_ptr->anbindung && !dd2_ptr->anbindung) ||
                (!dd1_ptr->anbindung && dd2_ptr->anbindung) ) return FALSE;
        if ( (dd1_ptr->anbindung && dd2_ptr->anbindung) &&
                !ziele_1_gleich_2( *dd1_ptr->anbindung, *dd2_ptr->anbindung) ) return FALSE;
        if ( (dd1_ptr->next && !dd2_ptr->next) || (!dd1_ptr->next && dd2_ptr->next) ) return FALSE;

        if ( !dd1_ptr->next && !dd2_ptr->next ) break;

        dd1_ptr = dd1_ptr->next;
        dd2_ptr = dd2_ptr->next;
    }
    while ( 1 );

    return TRUE;
}


static gint
oeffnen_auszug( Projekt* zond, gint node_id, gchar** errmsg )
{

    gint rc = 0;
    gint first_child = 0;
    gint younger_sibling = 0;
    DisplayedDocument* dd = NULL;
    DisplayedDocument* dd_next = NULL;
    GError* error = NULL;

    rc = zond_dbase_get_first_child( zond->dbase_zond->zond_dbase_work, node_id, &first_child, &error );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strdup( error->message );
        g_error_free( error );

        ERROR_S
    }
    else if ( first_child == 0 ) ERROR_S_MESSAGE( "Auszug anzeigen nicht möglich:\n"
                "Knoten hat keine Kinder" )

    younger_sibling = first_child;
    do
    {
        gchar* rel_path = NULL;
        Anbindung* anbindung = NULL;
        gint rc = 0;

        rc = zond_dbase_get_node( zond->dbase_zond->zond_dbase_work,
                younger_sibling, NULL, NULL, &rel_path, &anbindung, errmsg );
        if ( rc == -1 ) ERROR_S

        if ( rc < 2 && is_pdf( rel_path ) )//rel_path existiert
        {
            DisplayedDocument* dd_new = NULL;

            dd_new = document_new_displayed_document( rel_path, anbindung, errmsg );
            g_free( rel_path );
            g_free( anbindung );
            if ( !dd_new )
            {
                if ( *errmsg )
                {
                    document_free_displayed_documents( dd );
                    ERROR_S
                }
            }
            else
            {
                if ( !dd )
                {
                    dd = dd_new;
                    dd_next = dd_new;
                }
                else
                {
                    dd_next->next = dd_new;
                    dd_next = dd_next->next;
                }
            }
        }

        first_child = younger_sibling;

        rc = zond_dbase_get_younger_sibling( zond->dbase_zond->zond_dbase_work,
                first_child, &younger_sibling, &error);
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
            g_error_free( error );

            return -1;
        }
    }
    while ( younger_sibling > 0 );

    if ( !dd )
    {
        if ( errmsg ) *errmsg = add_string( *errmsg,
                g_strdup( "Keine Dateien/Anbindungen als Kinder" ) );

        return -1;
    }

    //auf schon geöffnet prüfen
    if ( !(zond->state & GDK_SHIFT_MASK) )
    {
        for ( gint i = 0; i < zond->arr_pv->len; i++ )
        {
            PdfViewer* pv_vergleich = g_ptr_array_index( zond->arr_pv, i );

            if ( oeffnen_dd_sind_gleich( pv_vergleich->dd, dd ) )
            {
                gtk_window_present( GTK_WINDOW(pv_vergleich->vf) );
                document_free_displayed_documents( dd );

                return 0;
            }
        }
    }

    PdfViewer* pv = viewer_start_pv( zond );
    viewer_display_document( pv, dd, 0, 0 );

    return 0;
}
*/


gint
oeffnen_internal_viewer( Projekt* zond, const gchar* rel_path, Anbindung* anbindung,
        const PdfPos* pos_pdf, gchar** errmsg )
{
    PdfPos pos_von = { 0 };

    //Neue Instanz oder bestehende?
    if ( !(zond->state & GDK_SHIFT_MASK) )
    {
        //Testen, ob pv mit rel_path schon geöffnet
        for ( gint i = 0; i < zond->arr_pv->len; i++ )
        {
            PdfViewer* pv = g_ptr_array_index( zond->arr_pv, i );
            if ( pv->dd->next == NULL &&
                    !g_strcmp0( rel_path, zond_pdf_document_get_path( pv->dd->zond_pdf_document ) ) )
            {
                if ( (!pv->dd->anbindung && !anbindung) ||
                        (pv->dd->anbindung && anbindung &&
                        ziele_1_gleich_2( *(pv->dd->anbindung), *anbindung )) )
                {
                    if ( pos_pdf ) pos_von = *pos_pdf;

                    gtk_window_present( GTK_WINDOW(pv->vf) );

                    if ( pos_von.seite > (pv->arr_pages->len - 1) )
                            pos_von.seite = pv->arr_pages->len - 1;

                    viewer_springen_zu_pos_pdf( pv, pos_von, 0.0 );

                    return 0;
                }
            }
        }
    }

    DisplayedDocument* dd = document_new_displayed_document( rel_path,
            anbindung, errmsg );
    if ( !dd && *errmsg ) ERROR_S
    else if ( !dd ) return 0;

    if ( pos_pdf ) pos_von = *pos_pdf;

    PdfViewer* pv = viewer_start_pv( zond, rel_path );
    viewer_display_document( pv, dd, pos_von.seite, pos_von.index );

    return 0;
}


gint
oeffnen_node( Projekt* zond, GtkTreeIter* iter, gboolean open_with, gchar** errmsg )
{
    gint rc = 0;
    gchar* rel_path = NULL;
    Anbindung anbindung = { 0 };
    PdfPos pos_pdf = { 0 };
    gint node_id = 0;
    GError* error = NULL;
    gint type = 0;
    gint link = 0;
    Anbindung* anbindung_int = NULL;

    gtk_tree_model_get( GTK_TREE_MODEL(zond_tree_store_get_tree_store( iter ) ), iter, 2, &node_id, -1 );

    rc = zond_dbase_get_type_and_link( zond->dbase_zond->zond_dbase_work,
            node_id, &type, &link, &error);
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
        g_error_free( error );

        return -1;
    }

    if ( type == ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_COPY ||
            type == ZOND_DBASE_TYPE_BAUM_INHALT_PDF_ABSCHNITT )
            node_id = link;

    rc = zond_dbase_get_node( zond->dbase_zond->zond_dbase_work,
            node_id, NULL, NULL, &rel_path, &anbindung.von.seite,
            &anbindung.von.index, &anbindung.bis.seite, &anbindung.bis.index,
            NULL, NULL, NULL, &error );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
        g_error_free( error );

        return -1;
    }

    if ( !rel_path ) return 0; //keine Datei angebunden

    //mit externem Programm öffnen
    if ( open_with || !is_pdf( rel_path ) ) //wenn kein pdf oder mit Programmauswahl zu öffnen:
    {
        gint rc = 0;

        rc = misc_datei_oeffnen( rel_path, open_with, errmsg );
        g_free( rel_path );
        if ( rc ) ERROR_S

        return 0;
    }

    if ( !(zond->state & GDK_CONTROL_MASK) )
    {
        if ( zond->state & GDK_MOD1_MASK )
        {
            pos_pdf.seite = (anbindung.bis.seite) ? anbindung.bis.seite : EOP;
            pos_pdf.index = (anbindung.bis.index) ? anbindung.bis.index : EOP;
        }
        else
        {
            pos_pdf.seite = anbindung.von.seite;
            pos_pdf.index = anbindung.von.index;
        }
    }
    else if ( zond->state & GDK_MOD1_MASK )
    {
        pos_pdf.seite = EOP;
        pos_pdf.index = EOP;
    }

    if ( (anbindung.bis.seite || anbindung.bis.index) &&
            (zond->state & GDK_CONTROL_MASK) ) anbindung_int = &anbindung;

    rc = oeffnen_internal_viewer( zond, rel_path, anbindung_int, &pos_pdf, errmsg );
    g_free( rel_path );
    if ( rc ) ERROR_S

    return 0;
}

