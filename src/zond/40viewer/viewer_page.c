/*
zond (pdf_documentpage.c) - Akten, Beweisstücke, Unterlagen
Copyright (C) 2021  pelo america

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

#include "viewer_page.h"

#include <gtk/gtk.h>
#include <mupdf/fitz.h>
#include "../global_types.h"

#include "../../misc.h"


typedef struct
{
    GArray* arr_text_found;
} ViewerPagePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(ViewerPage, viewer_page, GTK_TYPE_IMAGE)


static void
viewer_page_finalize( GObject* g_object )
{
    ViewerPagePrivate* viewer_page_priv = viewer_page_get_instance_private( VIEWER_PAGE(g_object) );

    g_array_unref( viewer_page_priv->arr_text_found );

    G_OBJECT_CLASS (viewer_page_parent_class)->finalize (g_object);

    return;
}


static void
viewer_page_class_init( ViewerPageClass* klass )
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->finalize = viewer_page_finalize;

    return;
}


static void
viewer_page_init( ViewerPage* self )
{
    ViewerPagePrivate* viewer_page_priv = viewer_page_get_instance_private( self );

    viewer_page_priv->arr_text_found = g_array_new( FALSE, FALSE, sizeof( fz_quad ) );

    return;
}


static void
viewer_page_mark_quad( cairo_t* cr, fz_quad quad, fz_matrix transform )
{
    fz_rect rect = fz_transform_rect( fz_rect_from_quad( quad ), transform );

    float x = rect.x0;
    float y = rect.y0;
    float width = rect.x1 - x;
    float heigth = rect.y1 - y;

    cairo_rectangle( cr, x, y, width, heigth );
    cairo_set_source_rgba (cr, 0, .1, .8, 0.5);
    cairo_fill(cr);
}


static gboolean
viewer_page_draw( GtkWidget* viewer_page, cairo_t* cr, gpointer data )
{
    PdfViewer* pdfv = (PdfViewer*) data;

    GArray* arr_text_found = viewer_page_get_arr_text_found( VIEWER_PAGE(viewer_page) );

    fz_matrix transform = fz_translate( 0.0, -pdfv->click_pdf_punkt.delta_y );
    transform = fz_post_scale( transform, pdfv->zoom / 100, pdfv->zoom / 100 );

    for ( gint i = 0; i < arr_text_found->len; i++ )
    {
        fz_quad quad = g_array_index( arr_text_found, fz_quad, i );

        viewer_page_mark_quad( cr, quad, transform );
    }

    if ( viewer_page != g_ptr_array_index( pdfv->arr_pages, pdfv->click_pdf_punkt.seite ) )
            return FALSE;

    //wenn annot angeclickt wurde
    if ( pdfv->clicked_annot )
    {
        PVQuad* pv_quad = pdfv->clicked_annot->first;

        do
        {
            fz_quad quad = fz_transform_quad( pv_quad->quad, transform );
            cairo_move_to( cr, quad.ul.x, quad.ul.y );
            cairo_line_to( cr, quad.ur.x, quad.ur.y );
            cairo_line_to( cr, quad.lr.x, quad.lr.y );
            cairo_line_to( cr, quad.ll.x, quad.ll.y );
            cairo_line_to( cr, quad.ul.x, quad.ul.y );
            cairo_set_source_rgb(cr, 0, 1, 0 );
            cairo_stroke( cr );

            pv_quad = pv_quad->next;
        }
        while ( pv_quad );
    }
    else //ansonsten etwaige highlights zeichnen
    {
        gint i = 0;
        while ( pdfv->highlight[i].ul.x >= 0 )
        {
            viewer_page_mark_quad( cr,pdfv->highlight[i], transform );

            i++;
        }
    }

    return FALSE;
}


ViewerPage*
viewer_page_new( PdfViewer* pdfv )
{
    ViewerPage* viewer_page = g_object_new( VIEWER_TYPE_PAGE, NULL );

    gtk_widget_show( GTK_WIDGET(viewer_page) );

    g_signal_connect_after( viewer_page, "draw", G_CALLBACK(viewer_page_draw), pdfv );
    gtk_layout_put( GTK_LAYOUT(pdfv->layout), GTK_WIDGET(viewer_page), 0, 0 );

    return viewer_page;
}


GArray*
viewer_page_get_arr_text_found( ViewerPage* self )
{
    ViewerPagePrivate* viewer_page_priv = viewer_page_get_instance_private( self );

    return viewer_page_priv->arr_text_found;
}


void
viewer_page_empty_arr_text_found( ViewerPage* self )
{
    GArray* arr_text_found = viewer_page_get_arr_text_found( self );

    g_array_remove_range( arr_text_found, 0, arr_text_found->len );

    return;
}
