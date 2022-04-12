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

#include "../zond_pdf_document.h"
#include "../global_types.h"

#include "../../misc.h"


typedef enum
{
    PROP_PDFV = 1,
    PROP_DOCUMENTPAGE,
    PROP_CROP_X0,
    PROP_CROP_X1,
    PROP_CROP_Y0,
    PROP_CROP_Y1,
    N_PROPERTIES
} ViewerPageProperty;


typedef struct
{
    PdfViewer* pdfv;
    PdfDocumentPage* pdf_document_page;
    fz_rect crop;
    ViewerPixbuf* pixbuf_page;
    ViewerPixbuf* pixbuf_thumb;
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
      priv->pdf_document_page = g_value_get_pointer(value);
      break;

    case PROP_CROP_X0:
      priv->crop.x0 = g_value_get_float(value);
      break;

    case PROP_CROP_X1:
      priv->crop.x1 = g_value_get_float(value);
      break;

    case PROP_CROP_Y0:
      priv->crop.y0 = g_value_get_float(value);
      break;

    case PROP_CROP_Y1:
      priv->crop.y1 = g_value_get_float(value);
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
                g_value_set_pointer( value, priv->pdf_document_page );
                break;

        case PROP_CROP_X0:
                g_value_set_float( value, priv->crop.x0 );
                break;

        case PROP_CROP_X1:
                g_value_set_float( value, priv->crop.x1 );
                break;

        case PROP_CROP_Y0:
                g_value_set_float( value, priv->crop.y0 );
                break;

        case PROP_CROP_Y1:
                g_value_set_float( value, priv->crop.y1 );
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
    fz_matrix transform = { 0, };
    fz_rect crop = { 0, };

    PdfViewer* pdfv = (PdfViewer*) data;

    crop = viewer_page_get_crop( VIEWER_PAGE(viewer_page) );
    transform = fz_translate( 0.0, -crop.y0 );
    transform = fz_post_scale( transform, pdfv->zoom / 100, pdfv->zoom / 100 );

    //wenn annot angeclickt wurde
    if ( pdfv->clicked_annot )
    {
        if ( pdfv->clicked_annot->type == PDF_ANNOT_HIGHLIGHT ||
                pdfv->clicked_annot->type == PDF_ANNOT_UNDERLINE )
        {
            //Test auf type nicht erforderlich, da clicked_annot nur gesetzt wird, wenn HIGHLIGH oder UNDERLINE
            GArray* arr_quads = pdfv->clicked_annot->annot_text_markup.arr_quads;

            for ( gint i = 0; i < arr_quads->len; i++ )
            {
                fz_quad quad = g_array_index( arr_quads, fz_quad, i );
                quad = fz_transform_quad( quad, transform );
                cairo_move_to( cr, quad.ul.x, quad.ul.y );
                cairo_line_to( cr, quad.ur.x, quad.ur.y );
                cairo_line_to( cr, quad.lr.x, quad.lr.y );
                cairo_line_to( cr, quad.ll.x, quad.ll.y );
                cairo_line_to( cr, quad.ul.x, quad.ul.y );
                cairo_set_source_rgb(cr, 0, 1, 0 );
                cairo_stroke( cr );
            }
        }
        else if ( pdfv->clicked_annot->type == PDF_ANNOT_TEXT &&
                pdfv->clicked_annot->pdf_document_page ==
                viewer_page_get_document_page( VIEWER_PAGE(viewer_page) ) )
        {
            fz_rect rect = { 0, };

            rect = fz_transform_rect( pdfv->clicked_annot->rect, transform );

            cairo_move_to( cr, rect.x0, rect.y0 );
            cairo_line_to( cr, rect.x1, rect.y0 );
            cairo_line_to( cr, rect.x1, rect.y1 );
            cairo_line_to( cr, rect.x0, rect.y1 );
            cairo_line_to( cr, rect.x0, rect.y0 );
            cairo_set_source_rgb(cr, 0, 1, 0 );
            cairo_stroke( cr );
        }
    }
    else //ansonsten etwaige highlights zeichnen
    {
        gint i = 0;
        while ( pdfv->highlight.page[i] >= 0 )
        {
            if ( viewer_page == g_ptr_array_index( pdfv->arr_pages,
                    pdfv->highlight.page[i] ) )
                    viewer_page_mark_quad( cr, pdfv->highlight.quad[i], transform );

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

    G_OBJECT_CLASS(viewer_page_parent_class)->constructed( self );

    return;
}


static void
viewer_page_class_init( ViewerPageClass* klass )
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

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

    obj_properties[PROP_CROP_X0] =
            g_param_spec_float ("crop-x0",
                                "fz_rect.x0",
                                "x0-Koordinate des cropped rect des PdfViewers.",
                                0.0, 100000.0, 0,
                                G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

    obj_properties[PROP_CROP_X1] =
            g_param_spec_float ("crop-x1",
                                "fz_rect.x1",
                                "x1-Koordinate des cropped rect des PdfViewers.",
                                0.0, 100000.0, 0,
                                G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

    obj_properties[PROP_CROP_Y0] =
            g_param_spec_float ("crop-y0",
                                "fz_rect.y0",
                                "y0-Koordinate des cropped rect des PdfViewers.",
                                0.0, 100000.0, 0,
                                G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

    obj_properties[PROP_CROP_Y1] =
            g_param_spec_float ("crop-y1",
                                "fz_rect.y1",
                                "y1-Koordinate des cropped rect des PdfViewers.",
                                0.0, 100000.0, 0,
                                G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);


    g_object_class_install_properties(object_class,
                                      N_PROPERTIES,
                                      obj_properties);

    return;
}


static void
viewer_page_init( ViewerPage* self )
{
    ViewerPagePrivate* priv = viewer_page_get_instance_private( self );

    priv->pixbuf_page = NULL;
    priv->pixbuf_thumb = NULL;

    gtk_widget_show( GTK_WIDGET(self) );

    return;
}


ViewerPage*
viewer_page_new_full( PdfViewer* pdfv, PdfDocumentPage* document_page, fz_rect crop )
{
    ViewerPage* viewer_page = g_object_new( VIEWER_TYPE_PAGE, "pdfv", pdfv,
            "document-page", document_page, "crop-x0", crop.x0,
            "crop-x1", crop.x1, "crop-y0", crop.y0, "crop-y1", crop.y1, NULL );

    return viewer_page;
}


PdfDocumentPage*
viewer_page_get_document_page( ViewerPage* self )
{
    ViewerPagePrivate* priv = viewer_page_get_instance_private( self );

    return priv->pdf_document_page;
}


fz_rect
viewer_page_get_crop( ViewerPage* self )
{
    ViewerPagePrivate* priv = viewer_page_get_instance_private( self );

    return priv->crop;
}


void
viewer_page_tilt( ViewerPage* self )
{
    ViewerPagePrivate* priv = viewer_page_get_instance_private( self );

    float x1_tmp = 0.0;
    float y1_tmp = 0.0;

    x1_tmp = priv->crop.x0 + (priv->crop.y1 - priv->crop.y0);
    y1_tmp = priv->crop.y0 + (priv->crop.x1 - priv->crop.x0);

    priv->crop.x1 = x1_tmp;
    priv->crop.y1 = y1_tmp;

    return;
}


void
viewer_page_set_pixbuf_page( ViewerPage* self, ViewerPixbuf* pix )
{
    ViewerPagePrivate* priv = viewer_page_get_instance_private( self );

    priv->pixbuf_page = pix;

    return;
}


ViewerPixbuf*
viewer_page_get_pixbuf_page( ViewerPage* self )
{
    ViewerPagePrivate* priv = viewer_page_get_instance_private( self );

    return priv->pixbuf_page;
}


void
viewer_page_set_pixbuf_thumb( ViewerPage* self, ViewerPixbuf* pix )
{
    ViewerPagePrivate* priv = viewer_page_get_instance_private( self );

    priv->pixbuf_thumb = pix;

    return;
}


ViewerPixbuf*
viewer_page_get_pixbuf_thumb( ViewerPage* self )
{
    ViewerPixbuf* pix = NULL;

    ViewerPagePrivate* priv = viewer_page_get_instance_private( self );

    pix = priv->pixbuf_thumb;

    return pix;
}


