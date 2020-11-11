/*
zond (annot.c) - Akten, Beweisstücke, Unterlagen
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

#include "../99conv/general.h"
#include "../99conv/mupdf.h"

#include "document.h"
#include "render.h"
#include "viewer.h"

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <mupdf/pdf.h>



static void
annot_free_pv_annot( PVAnnot* pv_annot )
{
    if ( !pv_annot ) return;

    PVQuad* next = NULL;

    PVQuad* pv_quad = pv_annot->first;

    if ( pv_quad ) do
    {
        next = pv_quad->next;
        g_free( pv_quad );
        pv_quad = next;
    }
    while ( pv_quad );

    g_free( pv_annot );

    return;
}


void
annot_free_pv_annot_page( PVAnnotPage* pv_annot_page )
{
    if ( !pv_annot_page ) return;

    PVAnnot* next = NULL;

    PVAnnot* pv_annot = pv_annot_page->first;

    if ( pv_annot ) do
    {
        next = pv_annot->next;
        annot_free_pv_annot( pv_annot );
        pv_annot = next;
    }
    while ( pv_annot );

    g_free( pv_annot_page );

    return;
}


static void
annot_load_quads( fz_context* ctx, PVAnnot* pv_annot, pdf_annot* annot, gint idx )
{
    PVQuad* pv_quad = NULL;

    if ( !(pv_annot->first) )
    {
        pv_annot->first = g_malloc0( sizeof( PVQuad ) );
        pv_quad = pv_annot->first;
    }
    else
    {
        pv_quad = pv_annot->first;

        while( pv_quad->next ) pv_quad = pv_quad->next;

        pv_quad->next = g_malloc0( sizeof( PVQuad ) );
        pv_quad = pv_quad->next;
    }

    pv_quad->quad = pdf_annot_quad_point( ctx, annot, idx );

    return;
}


static void
annot_load_pv_annot( DocumentPage* document_page, pdf_annot* annot, gint idx )
{
    PVAnnot* pv_annot = NULL;
    fz_context* ctx = document_page->document->ctx;

    if ( !(document_page->pv_annot_page->first) )
    {
        document_page->pv_annot_page->first = g_malloc0( sizeof( PVAnnot ) );
        pv_annot = document_page->pv_annot_page->first;
    }
    else
    {
        pv_annot = document_page->pv_annot_page->first;

        while ( pv_annot->next ) pv_annot = pv_annot->next;

        pv_annot->next = g_malloc0( sizeof( PVAnnot ) );
        PVAnnot* prev = pv_annot;
        pv_annot = pv_annot->next;
        pv_annot->prev = prev;
    }

    pv_annot->idx = idx;
    pv_annot->type = pdf_annot_type( ctx, annot );
    pv_annot->annot_rect = pdf_annot_rect( ctx, annot );
    pv_annot->n_quad = pdf_annot_quad_point_count( ctx, annot );

    for ( gint i = 0; i < pv_annot->n_quad; i++ )
            annot_load_quads( ctx, pv_annot, annot, i );

    document_page->pv_annot_page->last = pv_annot;

    return;
}


void
annot_load_pv_annot_page( DocumentPage* document_page )
{
    document_page->pv_annot_page = g_malloc0( sizeof( PVAnnotPage ) );

    pdf_annot* annot = pdf_first_annot( document_page->document->ctx,
            pdf_page_from_fz_page( document_page->document->ctx,
            document_page->page ) );

    if ( !annot ) return;

    gint idx = 0;
    do
    {
        annot_load_pv_annot( document_page, annot, idx );
        idx++;
    }
    while ( (annot = pdf_next_annot( document_page->document->ctx, annot )) );

    document_page->pv_annot_page->last->next = NULL;

    return;
}


gint
annot_create( DocumentPage* document_page, fz_quad* highlight, gint state,
        gchar** errmsg )
{
    pdf_annot* annot = NULL;
    enum pdf_annot_type art = 0;
    fz_context* ctx = document_page->document->ctx;

    if ( state == 1 ) art = PDF_ANNOT_HIGHLIGHT;
    else if ( state == 2 ) art = PDF_ANNOT_UNDERLINE;

    fz_try( ctx ) annot = pdf_create_annot( ctx, pdf_page_from_fz_page( ctx,
            document_page->page ), art );
    fz_catch( ctx ) ERROR_MUPDF( "pdf_create_annot/pdf_set_annot_color" )

    if ( art == PDF_ANNOT_UNDERLINE )
    {
        const gfloat color[3] = { 0.1, .85, 0 };
        pdf_set_annot_color( ctx, annot, 3, color );
    }

    gint i = 0;
    fz_rect rect = { 0 };
    while ( highlight[i].ul.x != -1 )
    {
        fz_try( ctx )
            pdf_add_annot_quad_point( ctx, annot, highlight[i] );
        fz_catch( ctx )
        {
            pdf_drop_annot( ctx, annot );

            ERROR_MUPDF( "pdf_annot_quad_point" )
        }

        fz_rect temp = fz_rect_from_quad( highlight[i] );
        rect = fz_union_rect( rect, temp );
        i++;
    }

    fz_try( ctx ) pdf_set_annot_rect( ctx, annot, rect );
    fz_always( ctx ) pdf_drop_annot( ctx, annot );
    fz_catch( ctx ) ERROR_MUPDF( "pdf_set_annot_rect" )

    return 0;
}


gint
annot_delete( DisplayedDocument* dd, gint page_doc, PVAnnot* pv_annot, gchar** errmsg )
{
    pdf_page* page = NULL;
    fz_context* ctx = dd->document->ctx;

    fz_try( ctx ) page = pdf_load_page( ctx, pdf_specifics( ctx,
            dd->document->doc ), page_doc );
    fz_catch( ctx ) ERROR_MUPDF( "pdf_load_page" )

    pdf_annot* annot = NULL;
    annot = pdf_first_annot( ctx, page );

    for ( gint i = 0; i < pv_annot->idx; i++ ) annot = pdf_next_annot( ctx, annot );

    fz_try( ctx ) pdf_delete_annot( ctx, page, annot );
    fz_always( ctx ) fz_drop_page( ctx, (fz_page*) page );
    fz_catch( ctx ) ERROR_MUPDF( "pdf_delete_annot" )

    return 0;
}


