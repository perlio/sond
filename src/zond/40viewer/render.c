/*
zond (render.c) - Akten, Beweisstücke, Unterlagen
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
#include <mupdf/pdf.h>

#include "../zond_pdf_document.h"
#include "../global_types.h"
#include "../error.h"

#include "../99conv/general.h"
#include "../99conv/pdf.h"

#include "viewer.h"
#include "document.h"
#include "viewer_pixbuf.h"
#include "viewer_page.h"



static gint
render_thumbnail( PdfViewer* pv, fz_context* ctx, gint num,
        PdfDocumentPage* pdf_document_page, gchar** errmsg )
{
    gint rc = 0;
    GtkTreeIter iter;
    fz_pixmap* pixmap = NULL;
    ViewerPixbuf* pixbuf = NULL;

    rc = viewer_get_iter_thumb( pv, num, &iter, errmsg );
    if ( rc == -1 )  ERROR_PAO( "viewer_get_iter_thumb" );

    gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(pv->tree_thumb) ),
            &iter, 0, &pixbuf, -1 );

    if ( pixbuf )
    {
        g_object_unref( pixbuf );
        return 1;
    }

    fz_matrix transform = fz_scale( 0.15, 0.15 );

    fz_rect rect = fz_transform_rect( viewer_page_get_crop( g_ptr_array_index( pv->arr_pages, num ) ), transform );
    fz_irect irect = fz_round_rect( rect );

//per draw-device to pixmap
    fz_try( ctx ) pixmap = fz_new_pixmap_with_bbox( ctx,
            fz_device_rgb( ctx ), irect, NULL, 0 );
    fz_catch( ctx ) ERROR_MUPDF( "fz_new_pixmap_with_bbox" )

    fz_try( ctx) fz_clear_pixmap_with_value( ctx, pixmap, 255 );
    fz_catch( ctx ) ERROR_MUPDF( "fz_clear_pixmap" )

    fz_device* draw_device = NULL;
    fz_try( ctx ) draw_device = fz_new_draw_device( ctx, fz_identity, pixmap );
    fz_catch( ctx ) ERROR_MUPDF( "fz_new_draw_device" )

    fz_try( ctx ) fz_run_display_list( ctx, pdf_document_page->display_list,
            draw_device, transform, rect, NULL );
    fz_always( ctx )
    {
        fz_close_device( ctx, draw_device );
        fz_drop_device( ctx, draw_device );
    }
    fz_catch( ctx )
    {
        fz_drop_pixmap( ctx, pixmap );
        ERROR_MUPDF( "fz_run_display_list" )
    }

    pixbuf = viewer_pixbuf_new_from_pixmap( zond_pdf_document_get_ctx( pdf_document_page->document ), pixmap );
    gtk_list_store_set( GTK_LIST_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(pv->tree_thumb) )), &iter, 0, pixbuf, -1 );
    g_object_unref( pixbuf );

    return 0;
}


static gint
render_pixmap( fz_context* ctx, ViewerPage* viewer_page, gdouble zoom,
        PdfDocumentPage* pdf_document_page, gchar** errmsg )
{
    fz_pixmap* pixmap = NULL;
    ViewerPixbuf* pixbuf = NULL;

    //pixmap schon gerendert?
    if ( gtk_image_get_storage_type( GTK_IMAGE(viewer_page) )
            != GTK_IMAGE_EMPTY ) return 1; //bereits gerendert

    fz_matrix transform = fz_scale( zoom / 100, zoom / 100);

    fz_rect rect = fz_transform_rect( viewer_page_get_crop( viewer_page ), transform );
    fz_irect irect = fz_round_rect( rect );

    //per draw-device to pixmap
    fz_try( ctx ) pixmap = fz_new_pixmap_with_bbox( ctx, fz_device_rgb( ctx ),
            irect, NULL, 0 );
    fz_catch( ctx ) ERROR_MUPDF( "fz_new_pixmap_with_bbox" )

    fz_try( ctx) fz_clear_pixmap_with_value( ctx, pixmap, 255 );
    fz_catch( ctx ) ERROR_MUPDF( "fz_clear_pixmap" )

    fz_device* draw_device = NULL;
    fz_try( ctx ) draw_device = fz_new_draw_device( ctx, fz_identity, pixmap );
    fz_catch( ctx )
    {
        fz_drop_pixmap( ctx, pixmap );
        ERROR_MUPDF( "fz_new_draw_device" )
    }

    fz_try( ctx ) fz_run_display_list( ctx, pdf_document_page->display_list,
            draw_device, transform, rect, NULL );
    fz_always( ctx )
    {
        fz_close_device( ctx, draw_device );
        fz_drop_device( ctx, draw_device );
    }
    fz_catch( ctx )
    {
        fz_drop_pixmap( ctx, pixmap );
        ERROR_MUPDF( "fz_run_display_list" )
    }

    pixbuf = viewer_pixbuf_new_from_pixmap( zond_pdf_document_get_ctx( pdf_document_page->document ), pixmap );
    gtk_image_set_from_pixbuf( GTK_IMAGE(viewer_page), pixbuf );
    g_object_unref( pixbuf );

    return 0;
}


gint
render_display_list_to_stext_page( fz_context* ctx, PdfDocumentPage* pdf_document_page,
        gchar** errmsg )
{
    fz_device* s_t_device = NULL;

    if ( pdf_document_page->stext_page ) return 0;

    fz_stext_options opts = { FZ_STEXT_DEHYPHENATE };

    fz_try( ctx ) pdf_document_page->stext_page = fz_new_stext_page( ctx, pdf_document_page->rect );
    fz_catch( ctx ) ERROR_MUPDF( "fz_new_stext_page" )

    //structured text-device
    fz_try( ctx ) s_t_device = fz_new_stext_device( ctx, pdf_document_page->stext_page, &opts );
    fz_catch( ctx ) ERROR_MUPDF( "fz_new_stext_device" )

    //und durchs stext-device laufen lassen
    fz_try( ctx ) fz_run_display_list( ctx, pdf_document_page->display_list, s_t_device,
            fz_identity, pdf_document_page->rect, NULL );
    fz_always( ctx )
    {
        fz_close_device( ctx, s_t_device );
        fz_drop_device( ctx, s_t_device );
    }
    fz_catch( ctx ) ERROR_MUPDF( "fz_run_display_list" )

    return 0;
}


static gint
render_display_list( fz_context* ctx, PdfDocumentPage* pdf_document_page,
        gchar** errmsg )
{
    fz_device* list_device = NULL;

    //Display_list immer noch leer?
    if ( !fz_display_list_is_empty( ctx, pdf_document_page->display_list ) ) return 1;

    //list_device für die Seite erzeugen
    fz_try( ctx ) list_device = fz_new_list_device( ctx, pdf_document_page->display_list );
    fz_catch( ctx )
    {
        if ( errmsg ) *errmsg = add_string( *errmsg,
                g_strconcat( "fz_new_list_device:\n", fz_caught_message( ctx ), NULL ) );

        return -1;
    }

    //page durchs list-device laufen lassen
    fz_try( ctx ) pdf_run_page( ctx, pdf_document_page->page, list_device, fz_identity, NULL );
    fz_always( ctx )
    {
        fz_close_device( ctx, list_device );
        fz_drop_device( ctx, list_device );
    }
    fz_catch( ctx )
    {
        if ( errmsg ) *errmsg = add_string( *errmsg,
                g_strconcat( "fz_run_page:\n", fz_caught_message( ctx ), NULL ) );

        return -1;
    }

    return 0;
}


void
render_page_thread( gpointer data, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    ViewerPage* viewer_page = NULL;
    PdfDocumentPage* pdf_document_page = NULL;
    fz_context* ctx = NULL;

    gint seite = GPOINTER_TO_INT(data) - 1;
    PdfViewer* pv =(PdfViewer*) user_data;

    viewer_page = g_ptr_array_index( pv->arr_pages, seite );
    pdf_document_page = viewer_page_get_document_page( viewer_page );

    ctx = fz_clone_context( zond_pdf_document_get_ctx( pdf_document_page->document ) );
    if ( !ctx ) ERROR_THREAD( "fz_clone_context" )

    zond_pdf_document_mutex_lock( pdf_document_page->document );
    rc = render_display_list( ctx, pdf_document_page, &errmsg );
    zond_pdf_document_mutex_unlock( pdf_document_page->document );
    if ( rc == -1 ) ERROR_THREAD( "render_display_list" )

    g_mutex_lock( &pdf_document_page->mutex_page );
    rc = render_display_list_to_stext_page( ctx, pdf_document_page, &errmsg );
    g_mutex_unlock( &pdf_document_page->mutex_page );
    if ( rc == -1 ) ERROR_THREAD( "render_diaplay_list_to_stext_page" )

    rc = render_pixmap( ctx, viewer_page, pv->zoom, pdf_document_page, &errmsg );
    if ( rc == -1 ) ERROR_THREAD( "render_pixmap" )

    rc = render_thumbnail( pv, ctx, seite, pdf_document_page, &errmsg );
    if ( rc == -1 ) ERROR_THREAD( "render_thumbnail" )

    fz_drop_context( ctx );

    return;
}


void
render_sichtbare_seiten( PdfViewer* pv )
{
    GtkTreePath* path = NULL;
    gint page_doc = 0;
    ViewerPage* viewer_page = NULL;
    PdfDocumentPage* pdf_document_page = NULL;
    gchar* path_doc = NULL;

    if ( pv->arr_pages->len == 0 ) return;

    gint erste = 0;
    gint letzte = 0;

    viewer_abfragen_sichtbare_seiten( pv, &erste, &letzte );

    viewer_page = g_ptr_array_index( pv->arr_pages, erste );
    pdf_document_page = viewer_page_get_document_page( viewer_page );

    //Seite von oberen - unterem Rand im entry anzeigen
    gchar* text = g_strdup_printf( "%i-%i", erste + 1, letzte + 1 );
    gtk_entry_set_text( GTK_ENTRY(pv->entry), text );
    g_free( text );

    //in Headerbar angezeigte Datei und Seite anzeigen
    path_doc = zond_pdf_document_get_path( pdf_document_page->document );
    gtk_header_bar_set_title( GTK_HEADER_BAR(pv->headerbar), path_doc );

    page_doc = zond_pdf_document_get_index( pdf_document_page );
    gchar* subtitle = g_strdup_printf( "Seite: %i", page_doc + 1);
    gtk_header_bar_set_subtitle( GTK_HEADER_BAR(pv->headerbar), subtitle );
    g_free( subtitle );

    if ( pv->thread_pool_page ) for ( gint i = letzte; i >= erste; i-- )
            g_thread_pool_move_to_front( pv->thread_pool_page, GINT_TO_POINTER(i + 1) );

    //thumb-Leiste anpassen
    path = gtk_tree_path_new_from_indices( erste, -1 );
    gtk_tree_view_scroll_to_cell( GTK_TREE_VIEW(pv->tree_thumb), path, NULL, FALSE, 0, 0 );
    gtk_tree_path_free( path );

    return;
}


void
render_sichtbare_thumbs( PdfViewer* pv )
{
    gint rc = 0;
    gint start = 0;
    gint end = 0;

    rc = viewer_get_visible_thumbs( pv, &start, &end );

    if ( rc == 0 )
            for ( gint i = start; i <= end; i++ )
            g_thread_pool_move_to_front( pv->thread_pool_page, GINT_TO_POINTER(i + 1) );

    return;
}


