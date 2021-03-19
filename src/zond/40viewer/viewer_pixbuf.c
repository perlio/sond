/*
zond (viewer_pixbuf.c) - Akten, Beweisstücke, Unterlagen
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


//Fake-Subklasse von GdkPixbuf -
//"Richtige" subclass mit GObject funktioniert nicht, da Def GdkPixbuf nicht in header...


#include <mupdf/fitz.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

typedef struct
{
    fz_context* ctx;
    fz_pixmap* pixmap;
} ViewerPixbufPrivate;


static void
viewer_pixbuf_finalize( guchar *pixels, gpointer data )
{
    ViewerPixbufPrivate* viewer_pixbuf_priv = (ViewerPixbufPrivate*) data;

    fz_drop_pixmap( viewer_pixbuf_priv->ctx, viewer_pixbuf_priv->pixmap );
    fz_drop_context( viewer_pixbuf_priv->ctx );

    g_free( viewer_pixbuf_priv );

    return;
}


GdkPixbuf*
viewer_pixbuf_new_from_pixmap( fz_context* ctx, fz_pixmap* pixmap )
{
    GdkPixbuf* pixbuf = NULL;

    ViewerPixbufPrivate* viewer_pixbuf_priv = g_malloc0( sizeof( ViewerPixbufPrivate ) );
    viewer_pixbuf_priv->ctx = fz_clone_context( ctx );
    viewer_pixbuf_priv->pixmap = pixmap;

    pixbuf = gdk_pixbuf_new_from_data( pixmap->samples,
            GDK_COLORSPACE_RGB, FALSE, 8, pixmap->w, pixmap->h,
            pixmap->stride, viewer_pixbuf_finalize, viewer_pixbuf_priv );


    return pixbuf;
}
