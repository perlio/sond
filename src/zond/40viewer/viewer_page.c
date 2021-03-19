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

#include "viewer.h"

#include "../global_types.h"

#include "../../misc.h"


typedef enum
{
    PROP_PDFV = 1,
    PROP_DOCUMENTPAGE,
    PROP_CROP,
    N_PROPERTIES
} ViewerPageProperty;


typedef struct
{
    PdfViewer* pdfv;
    DocumentPage* document_page;
    fz_rect* crop;
} ViewerPagePrivate;


G_DEFINE_TYPE_WITH_PRIVATE(ViewerPage, viewer_page, GTK_TYPE_IMAGE)

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

static void
viewer_page_set_property (GObject      *object,
                          guint         property_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    ViewerPage* self = VIEWER_PAGE(object);
    ViewerPagePrivate* priv = viewer_page_get_instance_private( self );

    switch ((ViewerPageProperty) property_id)
    {
    case PROP_PDFV:
      priv->pdfv = g_value_get_pointer(value);
      break;

    case PROP_DOCUMENTPAGE:
      priv->document_page = g_value_get_pointer(value);
      break;

    case PROP_CROP:
      if ( priv->crop ) g_boxed_free( g_type_from_name( "fz_rect" ), priv->crop );
      priv->crop = g_value_get_boxed(value);
      break;

    default:
      /* We don't have any other property... */
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}


static void
viewer_page_get_property (GObject    *object,
                          guint       property_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    ViewerPage *self = VIEWER_PAGE(object);
    ViewerPagePrivate* priv = viewer_page_get_instance_private( self );

    switch ((ViewerPageProperty) property_id)
    {
        case PROP_PDFV:
                g_value_set_pointer( value, priv->pdfv );
                break;

        case PROP_DOCUMENTPAGE:
                g_value_set_pointer( value, priv->document_page );
                break;

        case PROP_CROP:
                g_value_set_boxed( value, priv->crop );
                break;

        default:
                /* We don't have any other property... */
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
                break;
    }
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

    fz_matrix transform = fz_translate( 0.0, -pdfv->click_pdf_punkt.delta_y );
    transform = fz_post_scale( transform, pdfv->zoom / 100, pdfv->zoom / 100 );

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


static void
viewer_page_constructed( GObject* self )
{
    ViewerPagePrivate* priv = viewer_page_get_instance_private( VIEWER_PAGE(self) );

    g_signal_connect_after( self, "draw", G_CALLBACK(viewer_page_draw), priv->pdfv );
    gtk_layout_put( GTK_LAYOUT(priv->pdfv->layout), GTK_WIDGET(self), 0, 0 );

    G_OBJECT_CLASS(viewer_page_parent_class)->constructed( self );

    return;
}


static gpointer
viewer_page_crop_copy( gpointer rect )
{
    return g_memdup( rect, sizeof( fz_rect ) );
}


static void
viewer_page_class_init( ViewerPageClass* klass )
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

//    if ( !g_type_from_name( "fz_rect" ) )
            g_boxed_type_register_static( "fz_rect", viewer_page_crop_copy, g_free );

    object_class->constructed = viewer_page_constructed;

    object_class->set_property = viewer_page_set_property;
    object_class->get_property = viewer_page_get_property;

    obj_properties[PROP_PDFV] =
            g_param_spec_pointer ("pdfv",
                                  "PdfViewer",
                                  "Kontext-Struktur.",
                                  G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

    obj_properties[PROP_DOCUMENTPAGE] =
            g_param_spec_pointer ("document-page",
                                  "DocumentPage",
                                  "Zeiger auf Seite des PDF-Documents.",
                                  G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

    obj_properties[PROP_CROP] =
            g_param_spec_boxed ("crop",
                                "fz_rect",
                                "cropped rect des PdfViewers.",
                                g_type_from_name( "fz_rect" ),
                                G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);


    g_object_class_install_properties(object_class,
                                      N_PROPERTIES,
                                      obj_properties);

    return;
}


static void
viewer_page_init( ViewerPage* self )
{
    gtk_widget_show( GTK_WIDGET(self) );

    return;
}


ViewerPage*
viewer_page_new_full( PdfViewer* pdfv, DocumentPage* document_page, fz_rect crop )
{
    ViewerPage* viewer_page = g_object_new( VIEWER_TYPE_PAGE, "pdfv", pdfv,
            "document-page", document_page, "crop", &crop, NULL );

    return viewer_page;
}


DocumentPage*
viewer_page_get_document_page( ViewerPage* self )
{
    ViewerPagePrivate* priv = viewer_page_get_instance_private( self );

    return priv->document_page;
}


fz_rect
viewer_page_get_crop( ViewerPage* self )
{
    ViewerPagePrivate* priv = viewer_page_get_instance_private( self );

    if ( priv->crop ) return *(priv->crop);

    return fz_empty_rect;
}
