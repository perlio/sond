/*
zond (pdf_ocr.c) - Akten, Beweisstücke, Unterlagen
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

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <gtk/gtk.h>
#include <tesseract/capi.h>
#include <allheaders.h>
#include <glib/gstdio.h>

#include "../zond_pdf_document.h"

#include "../../misc.h"

#include "../error.h"

#include "../40viewer/document.h"

#include "../99conv/pdf.h"
#include "../99conv/general.h"

#ifdef _WIN32
#include <errhandlingapi.h>
#include <libloaderapi.h>
#elif defined( __linux__ )
#include <string.h>
#include <errno.h>
#endif // _WIN32


typedef struct _Tess_Recog
{
    TessBaseAPI* handle;
    ETEXT_DESC* monitor;
} TessRecog;


static gint
pdf_ocr_update_content_stream( fz_context* ctx, pdf_obj* page_ref,
        fz_buffer* buf, gchar** errmsg )
{
    pdf_document* doc = NULL;
    pdf_obj* contents_dict = NULL;
    pdf_obj* ind = NULL;

    doc = pdf_get_bound_document( ctx, page_ref );

    fz_var( contents_dict );
    fz_var( ind );
    fz_try( ctx )
    {
        pdf_dict_del( ctx, page_ref, PDF_NAME(Contents) );
        contents_dict = pdf_new_dict( ctx, doc, 2 );
        gint num = pdf_create_object( ctx, doc );
        pdf_update_object( ctx, doc, num, contents_dict );
        ind = pdf_new_indirect( ctx, doc, num, 0 );
        pdf_dict_put( ctx, page_ref, PDF_NAME(Contents), ind );
        pdf_update_stream( ctx, doc, ind, buf, 0 );
    }
    fz_always( ctx )
    {
        pdf_drop_obj( ctx, contents_dict );
        pdf_drop_obj( ctx, ind );
    }
    fz_catch( ctx ) ERROR_MUPDF( "update stream" )

    return 0;
}


static gboolean
pdf_ocr_text_showing_op( gchar* begin, gchar* end )
{
    gchar* ptr = NULL;

    ptr = begin;

    while ( ptr < end )
    {
        if ( *ptr == '"' || *ptr == 39 ) return TRUE;
        if ( *ptr == 'T' )
        {
            if ( *(ptr + 1) == 'J' || *(ptr + 1) == 'j' ) return TRUE;
        }

        ptr++;
    }

    return FALSE;
}


static gboolean
is_white( const char* s )
{
    if ( *s == 0 || *s == 9 || *s == 10 || *s == 12 || *s == 13 || *s == 32 )
            return TRUE;

    return FALSE;
}


static gint
pdf_ocr_find_next_Tr( gchar* buf, size_t size, gchar** end_Tr )
{
    gchar* ptr = NULL;
    gint64 num = 0;
    gchar* endptr = NULL;

    ptr = buf;

    while ( ptr < buf + size )
    {
        //1. Zahl
        num = g_ascii_strtoll( ptr, &endptr, 10 );
        if ( ptr != endptr ) //Zahl gefunden
        { //vorspulen
            ptr = endptr;
            while ( is_white( ptr) ) ptr++;
        }
        else
        {
            ptr++;
            continue;
        }

        //2. Tr
        if ( *ptr == 'T' && *(ptr + 1) == 'r' )
        {
            *end_Tr = ptr + 1;
            return (gint) num;
        }
    }

    //nix gefunden
    *end_Tr = buf + size;

    return -1;
}


static gboolean
pdf_ocr_text_object_uniform_vis( gchar* BT, gchar* ET, gboolean vis_BT, gboolean* vis_ET,
        gboolean* has_text )
{
    gboolean uniform = TRUE;
    gchar* ptr = NULL;
    gchar* end_ptr = NULL;
    gint Tr_act = -1;
    gint Tr = -1;

    *has_text = FALSE;
    *vis_ET = vis_BT;

    //Sichtbarkeit des ersten angezeigten Textes ermitteln
    ptr = BT;
    while ( ptr < ET )
    {
        Tr_act = pdf_ocr_find_next_Tr( ptr, ET - ptr, &end_ptr );

        if ( pdf_ocr_text_showing_op( ptr, end_ptr ) )
        {
            if ( *has_text && Tr_act != -1 )
            {
                if ( (Tr == 3) == (*vis_ET) ) uniform = FALSE;
            }

            *has_text = TRUE;
        }

        if ( Tr_act >= 0 ) Tr = Tr_act;

        if ( Tr >= 0 ) *vis_ET = (Tr == 3) ? FALSE : TRUE;

        ptr = end_ptr + 1;
    }

    return uniform;
}


static gchar*
pdf_ocr_find_next_ET( gchar* buf, size_t size )
{
    gchar* ptr = NULL;
    gboolean in_string = FALSE;

    ptr = buf;

    while ( ptr < buf + size - 1 )
    {
        if ( *ptr == '(' ) in_string = TRUE;
        if ( *ptr == ')' ) in_string = FALSE;

        if ( !in_string && *ptr == 'E' && *(ptr + 1) == 'T' ) return ptr + 1;

        ptr++;
    }

    //Nix gefunden darf eigentlich nicht sein
    return buf + size - 1;
}


static gchar*
pdf_ocr_find_next_BT( gchar* buf, size_t size, gchar** ET )
{
    gchar* ptr = NULL;
    gchar* BT = NULL;

    ptr = buf;

    while ( ptr < buf + size - 1 )
    {
        if ( *ptr == 'B' && *(ptr + 1) == 'T' )
        {
            BT = ptr;
            if ( ET ) *ET = pdf_ocr_find_next_ET( BT, buf + size - ptr );

            return BT;
        }
        ptr++;
    }

    //nix gefunden
    if ( ET ) *ET = buf + size - 1;

    return buf + size - 1;
}


static fz_buffer*
pdf_ocr_get_content_stream_as_buffer( fz_context* ctx, pdf_obj* page_ref,
        gchar** errmsg )
{
    pdf_obj* obj_contents = NULL;
    fz_stream* stream = NULL;
    fz_buffer* buf = NULL;

    //Stream doc_text
    obj_contents = pdf_dict_get( ctx, page_ref, PDF_NAME(Contents) );

    fz_try( ctx )
    {
        stream = pdf_open_contents_stream( ctx, pdf_get_bound_document( ctx, page_ref ), obj_contents );

        // Test
//        gint rc = pdf_print_token( ctx, stream, errmsg );
//        if ( rc ) ERROR_PAO_R( "pdf_print_token", NULL )

        fz_drop_stream( ctx, stream );

        stream = pdf_open_contents_stream( ctx, pdf_get_bound_document( ctx, page_ref ), obj_contents );
        buf = fz_read_all( ctx, stream, 1024 );
    }
    fz_always( ctx ) fz_drop_stream( ctx, stream );
    fz_catch( ctx ) ERROR_MUPDF_R( "open and read stream", NULL )

    return buf;
}


static gint
pdf_ocr_process_tess_tmp( fz_context* ctx, pdf_obj* page_ref,
        fz_matrix ctm, gchar** errmsg )
{
    gint rc = 0;
    fz_buffer* buf = NULL;
    fz_buffer* buf_new = NULL;
    size_t size = 0;
    gchar* data = NULL;
    gchar* cm = NULL;
    gchar* BT = NULL;

    cm = g_strdup_printf( "\n%g %g %g %g %g %g cm\nBT",
            ctm.a, ctm.b, ctm.c, ctm.d, ctm.e, ctm.f );

    //Komma durch Punkt ersetzen
    for ( gint i = 0; i < strlen( cm ); i++ ) if ( *(cm + i) == ',' )
            *(cm + i) = '.';

    buf = pdf_ocr_get_content_stream_as_buffer( ctx, page_ref, errmsg );
    if ( !buf )
    {
        g_free( cm );
        ERROR_PAO( "pdf_ocr_get_content_stream_as_buffer" )
    }

    size = fz_buffer_storage( ctx, buf, (guchar**) &data );

    BT = pdf_ocr_find_next_BT( data, size, NULL );

    fz_try( ctx ) buf_new = fz_new_buffer( ctx, size + strlen( cm ) + 10 );
    fz_catch( ctx )
    {
        g_free( cm );
        ERROR_MUPDF( "fz_new_buffer" );
    }

    fz_try( ctx )
    {
        fz_append_data( ctx, buf_new, cm, strlen( cm ) );
        fz_append_data( ctx, buf_new, BT + 2, size - (BT + 2 - data) );
    }
    fz_always( ctx )
    {
        g_free( cm );
        fz_drop_buffer( ctx, buf );
    }
    fz_catch( ctx )
    {
        fz_drop_buffer( ctx, buf_new );
        ERROR_MUPDF( "append buffer" )
    }

    rc = pdf_ocr_update_content_stream( ctx, page_ref, buf_new, errmsg );
    fz_drop_buffer( ctx, buf_new );
    if ( rc ) ERROR_PAO( "pdf_ocr_update_content_stream" )

    return 0;
}


static fz_matrix
pdf_ocr_create_matrix( fz_context* ctx, fz_rect rect, gfloat scale, gint rotate )
{
    gfloat shift_x = 0;
    gfloat shift_y = 0;
    gfloat width = 0;
    gfloat height = 0;

    width = rect.x1 - rect.x0;
    height = rect.y1 - rect.y0;

    fz_matrix ctm1 = fz_scale( scale, scale );
    fz_matrix ctm2 = fz_rotate( (float) rotate );

    if ( rotate == 90 ) shift_x = width;
    if ( rotate == 180 )
    {
        shift_x = height;
        shift_y = width;
    }
    if ( rotate == 270 ) shift_y = height;

    fz_matrix ctm = fz_concat( ctm1, ctm2 );

    ctm.e = shift_x;
    ctm.f = shift_y;

    return ctm;
}


static fz_rect
pdf_ocr_get_mediabox( fz_context* ctx, pdf_obj* page )
{
    fz_rect rect = { 0 };
    pdf_obj* mediabox = NULL;

    mediabox = pdf_dict_get( ctx, page, PDF_NAME(MediaBox) );
    rect.x0 = pdf_array_get_real( ctx, mediabox, 0 );
    rect.x1 = pdf_array_get_real( ctx, mediabox, 2 );
    rect.y0 = pdf_array_get_real( ctx, mediabox, 1 );
    rect.y1 = pdf_array_get_real( ctx, mediabox, 3 );

    return rect;
}


/** Flags:
*** 1<<0:   allg. Stream
*** 1<<1:   TextObjekte sichtbar
*** 1<<2:   TextObjekte unsichtbar  **/
static gint
pdf_ocr_filter_stream( fz_context* ctx, pdf_obj* page_ref, gint flags, gchar** errmsg )
{
    gint rc = 0;
    fz_buffer* buf = NULL;
    fz_buffer* buf_new = NULL;
    gchar* data = NULL;
    gchar* BT = NULL;
    gchar* pos = NULL;
    gchar* ET = NULL;
    gchar* end_Tr = NULL;
    gint last_Tr = -1;
    size_t size = 0;
    gboolean vis_TO = TRUE;

    if ( flags == 7 ) return 0;

    fz_try( ctx ) buf_new = fz_new_buffer( ctx, 1024 );
    fz_catch( ctx ) ERROR_MUPDF( "fz_new_buffer" )

    buf = pdf_ocr_get_content_stream_as_buffer( ctx, page_ref, errmsg );
    if ( !buf )
    {
        fz_drop_buffer( ctx, buf_new );
        ERROR_PAO( "pdf_ocr_get_content_stream_as_buffer" )
    }

    size = fz_buffer_storage( ctx, buf, (guchar**) &data );
    pos = data;

    while ( pos < data + size - 1 )
    {
        gboolean uniform = FALSE;
        gboolean has_text = FALSE;
        gchar* ptr = NULL;

        BT = pdf_ocr_find_next_BT( pos, size - (pos - data) , &ET );

        //wenn "normaler Stream" gewählt, dann alles außer Text-Objecten
        if ( flags & 1 )
        {
            fz_try( ctx ) fz_append_data( ctx, buf_new, pos, BT - pos );
            fz_catch( ctx )
            {
                fz_drop_buffer( ctx, buf );
                fz_drop_buffer( ctx, buf_new );
                ERROR_MUPDF( "fz_append_data" )
            }
        }

        //falls "normaler" stream nicht kopiert werden soll UND erstes TextObject des streams...
        if ( !(flags & 1) && pos == data && BT != ET ) //gucken, ob vorher (innerhalb q/Q-Rahmen) cm vorhanden
        {
            ptr = BT - 1;

            while ( *ptr != 'q' )
            {
                if ( *ptr == 'm' && *(ptr - 1) == 'c' ) //cm gefunden
                {
                    ptr = ptr - 2;

                    //sechs Leerstellen zurückspulen
                    gint zaehler = 6;
                    gdouble cm[6] = { 0.0 };
                    while ( zaehler >= 0 )
                    {
                        if ( is_white( ptr ) )
                        {
                            zaehler--;
                            while ( is_white( ptr ) ) ptr--;
                            while ( !is_white( ptr ) ) ptr--;
                        }
                        cm[zaehler] = g_ascii_strtod( ptr, NULL );
                    }

                    gchar* cm_string = g_strdup_printf( "\n%g %g %g %g %g %g cm\n",
                            cm[0], cm[1], cm[2], cm[3], cm[4], cm[5] );

                    //Komma durch Punkt ersetzen
                    for ( gint i = 0; i < strlen( cm_string ); i++ )
                            if ( *(cm_string + i) == ',' ) *(cm_string + i) = '.';

                    fz_try( ctx ) fz_append_data( ctx, buf_new, cm_string, strlen( cm_string ) );
                    fz_catch( ctx )
                    {
                        fz_drop_buffer( ctx, buf );
                        fz_drop_buffer( ctx, buf_new );
                        ERROR_MUPDF( "fz_append_data" )
                    }
                    break;
                }

                ptr--;
            }
        }

        if ( flags & 6 )
        {
            gboolean last_vis_TO = FALSE;

            //vor dem aktuellen TextObject suchen
            ptr = pos;
            while ( ptr < BT )
            {
                gint search_Tr = pdf_ocr_find_next_Tr( ptr, BT - ptr, &end_Tr );
                if ( search_Tr >= 0 ) last_Tr = search_Tr;
                ptr = end_Tr + 1;
            }

            if ( last_Tr == 3 ) vis_TO = FALSE;
            else if ( last_Tr >= 0 ) vis_TO = TRUE;

            uniform = pdf_ocr_text_object_uniform_vis( BT, ET, vis_TO,
                    &last_vis_TO, &has_text );

            vis_TO = last_vis_TO;
        }

       if ( (!uniform && (flags & 6)) || //TextObject ist gemischt-vorläufig
                                            //nur entfernen, wenn beide Sorten
                                            //Text entfernt werden sollen
                (uniform && (flags & 2) && has_text && vis_TO ) ||
                (uniform && (flags & 4) && has_text && (!vis_TO)) ) //nur sichtbarer Text soll
                                                        //entfernt werden und Text ist unsichtbar
        {
            fz_try( ctx )
            {
                fz_append_data( ctx, buf_new, BT, ET - BT + 1 );
                fz_append_data( ctx, buf_new, "\n", 1 );
            }
            fz_catch( ctx )
            {
                fz_drop_buffer( ctx, buf );
                fz_drop_buffer( ctx, buf_new );
                ERROR_MUPDF( "fz_append_data" )
            }
        }

        pos = ET + 1;
    }

    fz_drop_buffer( ctx, buf );

    rc = pdf_ocr_update_content_stream( ctx, page_ref, buf_new, errmsg );
    fz_drop_buffer( ctx, buf_new );
    if ( rc ) ERROR_PAO( "pdf_ocr_update_content_stream" )

    return 0;
}


static gint
pdf_ocr_sandwich_page( PdfDocumentPage* pdf_document_page,
        pdf_document* doc_text, gint page_text, gchar** errmsg )
{
    gint rc = 0;
    pdf_obj* page_ref_text = NULL;
    pdf_graft_map *graft_map = NULL;
    pdf_obj *obj = NULL;
    pdf_obj* contents_arr = NULL;
    gint zaehler = 0;

    pdf_obj* resources = NULL;
    pdf_obj* resources_text = NULL;
    pdf_obj* font_dict = NULL;
    pdf_obj* font_dict_text = NULL;
    pdf_obj* font_dict_key = NULL;
    pdf_obj* font_dict_val = NULL;

    fz_context* ctx = zond_pdf_document_get_ctx( pdf_document_page->document );

    fz_try ( ctx )
    {
        pdf_flatten_inheritable_page_items( ctx, pdf_document_page->page->obj );

        page_ref_text = pdf_lookup_page_obj( ctx, doc_text, page_text );
        pdf_flatten_inheritable_page_items( ctx, page_ref_text );
    }
    fz_catch( ctx ) ERROR_MUPDF_R( "pdf_lookup_page", -2 );

    rc = pdf_ocr_filter_stream( ctx, pdf_document_page->page->obj, 3, errmsg );
    if ( rc ) ERROR_PAO( "pdf_ocr_filter_stream" )

    fz_rect rect = pdf_ocr_get_mediabox( ctx, pdf_document_page->page->obj );
    float scale = 1./4./72.*70.;

    fz_matrix ctm = pdf_ocr_create_matrix( ctx, rect, scale, pdf_document_page->rotate );

    rc = pdf_ocr_process_tess_tmp( ctx, page_ref_text, ctm, errmsg );
    if ( rc ) ERROR_PAO_R( "pdf_ocr_process_tess_tmp", -2 )

    fz_try( ctx ) contents_arr = pdf_new_array( ctx, pdf_get_bound_document( ctx, pdf_document_page->page->obj ), 1 );
    fz_catch( ctx ) ERROR_MUPDF_R( "pdf_new_array", -2 )

    fz_try( ctx )
    {
        //Contents aus Ursrpungs-Pdf in neues Array umkopieren
        //graft nicht erforderlich, da selbes Dokument - Referenzen bleiben
        obj = pdf_dict_get( ctx, pdf_document_page->page->obj, PDF_NAME(Contents) ); //keine exception
        if ( pdf_is_array( ctx, obj ) )
        {
            for ( gint i = 0; i < pdf_array_len( ctx, obj ); i++ )
            {
                pdf_obj* content_stream = pdf_array_get( ctx, obj, i );
                if ( content_stream != NULL )
                {
                    pdf_array_put( ctx, contents_arr, zaehler, content_stream );
                    zaehler++;
                }
            }
        }
        else if ( pdf_is_stream( ctx, obj ) )
        {
            pdf_array_put( ctx, contents_arr, zaehler, obj );
            zaehler++;
        }

        //Jetzt aus Text-PDF - graf map erforderlich
        graft_map = pdf_new_graft_map( ctx, pdf_get_bound_document( ctx, pdf_document_page->page->obj ) ); //keine exception

        obj = pdf_dict_get( ctx, page_ref_text, PDF_NAME(Contents) );
        if ( pdf_is_array( ctx, obj ) )
        {
            for ( gint i = 0; i < pdf_array_len( ctx, obj ); i++ )
            {
                pdf_obj* content_stream = pdf_array_get( ctx, obj, i );
                if ( content_stream != NULL )
                {
                    pdf_array_put_drop( ctx, contents_arr, zaehler,
                            pdf_graft_mapped_object( ctx, graft_map,
                            content_stream ) );
                    zaehler++;
                }
            }
        }
        else if ( pdf_is_stream( ctx, obj ) )
        {
            pdf_array_put_drop( ctx, contents_arr, zaehler,
                    pdf_graft_mapped_object( ctx, graft_map, obj ) );
            zaehler++;
        }

        //alte Contents raus, neue rein
        pdf_dict_del( ctx, pdf_document_page->page->obj, PDF_NAME(Contents) );
        pdf_dict_put( ctx, pdf_document_page->page->obj, PDF_NAME(Contents), contents_arr );

        //Resources aus pdf_text hizukopieren
        resources = pdf_dict_get( ctx, pdf_document_page->page->obj, PDF_NAME(Resources) );
        //Zunächst testen, ob Page-Object Font enthält
        font_dict = pdf_dict_get( ctx, resources, PDF_NAME(Font) );
        if ( !font_dict )
        {
            font_dict = pdf_new_dict( ctx, pdf_get_bound_document( ctx, pdf_document_page->page->obj ), 1 );
            pdf_dict_put_drop( ctx, resources, PDF_NAME(Font), font_dict );
        }

        //Nun Text-Pdf
        resources_text = pdf_dict_get( ctx, page_ref_text, PDF_NAME(Resources) );

        font_dict_text = pdf_dict_get( ctx, resources_text, PDF_NAME(Font) );
        for ( gint i = 0; i < pdf_dict_len( ctx, font_dict_text ); i++ )
        {
            font_dict_key = pdf_dict_get_key( ctx, font_dict_text, i );
            font_dict_val = pdf_dict_get_val( ctx, font_dict_text, i );

            pdf_dict_put_drop( ctx, font_dict, font_dict_key,
                    pdf_graft_mapped_object( ctx, graft_map, font_dict_val ) );
        }
    }
    fz_always( ctx )
    {
        pdf_drop_obj( ctx, contents_arr );
        pdf_drop_graft_map( ctx, graft_map );
    }
    fz_catch( ctx ) ERROR_MUPDF( "fz_try (page_sandwich)" )

    return 0;
}


//thread-safe
static gint
pdf_ocr_sandwich_doc( GPtrArray* arr_document_pages, pdf_document* doc_text,
        InfoWindow* info_window, gchar** errmsg )
{
    if ( arr_document_pages->len == 0 ) return 0;

    gint rc = 0;
    gint zaehler = 0;
    gchar* message = NULL;

    for ( gint i = 0; i < arr_document_pages->len; i++ )
    {
        PdfDocumentPage* pdf_document_page =
                g_ptr_array_index( arr_document_pages, i );

        zond_pdf_document_mutex_lock( pdf_document_page->document );
        rc = pdf_ocr_sandwich_page( pdf_document_page, doc_text, zaehler, errmsg );
        zond_pdf_document_mutex_unlock( pdf_document_page->document );
        zaehler++;
        if ( rc == -1 ) ERROR_PAO( "pdf_ocr_sandwich" )
        else if ( rc == -2 )
        {
            message = g_strdup_printf( "Seite konnte nicht eingelesen werden -\n%s",
                    *errmsg );
            g_free( *errmsg );
            info_window_set_message( info_window, message );
            g_free( message );

            g_ptr_array_remove_index( arr_document_pages, i );
            i--;

            continue;
        }
    }

    return 0;
}


static gpointer
pdf_ocr_tess_recog( gpointer data )
{
    gint rc = 0;

    TessRecog* tess_recog = (TessRecog*) data;

    rc = TessBaseAPIRecognize( tess_recog->handle, tess_recog->monitor );

    if ( rc ) return GINT_TO_POINTER(1);

    return NULL;
}


static gboolean
pdf_ocr_cancel( void* cancel_this )
{
    volatile gboolean *cancelFlag = (volatile gboolean*) cancel_this;
    return *cancelFlag;
}


static gint
pdf_ocr_tess_page( InfoWindow* info_window, TessBaseAPI* handle,
        fz_pixmap* pixmap, gchar** errmsg )
{
    gint rc = 0;
    ETEXT_DESC* monitor = NULL;
    gint progress = 0;

    TessBaseAPISetImage( handle, pixmap->samples, pixmap->w, pixmap->h, pixmap->n, pixmap->stride );
    monitor = TessMonitorCreate( );
    TessMonitorSetCancelThis( monitor, &(info_window->cancel) );
    TessMonitorSetCancelFunc( monitor, (TessCancelFunc) pdf_ocr_cancel );

    TessRecog tess_recog = { handle, monitor };
    GThread* thread_recog = g_thread_new( "recog", pdf_ocr_tess_recog, &tess_recog );

    GtkWidget* bar_progress = gtk_progress_bar_new( );
    gtk_box_pack_start( GTK_BOX(info_window->content), bar_progress, FALSE, FALSE, 0 );

    gtk_widget_show_all( bar_progress );

    while ( gtk_events_pending( ) ) gtk_main_iteration( );
    info_window_scroll( info_window );

    while ( progress < 100 && !(info_window->cancel) )
    {
        progress = TessMonitorGetProgress( monitor );
        gtk_progress_bar_set_fraction( GTK_PROGRESS_BAR(bar_progress),
                ((gdouble) progress) / 100 );

        while ( gtk_events_pending( ) ) gtk_main_iteration( );
    }

    rc = GPOINTER_TO_INT(g_thread_join( thread_recog ));
    TessMonitorDelete( monitor );

    if ( rc && !(info_window->cancel) ) ERROR_PAO( "TessAPIPRecognize:\nFehler!" )

    return 0;
}


static fz_pixmap*
pdf_ocr_render_pixmap( fz_context* ctx, pdf_document* doc, gint num,
        float scale, gchar** errmsg )
{
    pdf_page* page = NULL;
    fz_pixmap* pixmap = NULL;

    page = pdf_load_page( ctx, doc, 0 );

    fz_rect rect = pdf_bound_page( ctx, page );
    fz_matrix ctm = pdf_ocr_create_matrix( ctx, rect, scale, 0 );

    rect = fz_transform_rect( rect, ctm );

//per draw-device to pixmap
    fz_try( ctx ) pixmap = fz_new_pixmap_with_bbox( ctx, fz_device_rgb( ctx ),
            fz_irect_from_rect( rect ), NULL, 0 );
    fz_catch( ctx )
    {
        fz_drop_page( ctx, &page->super );
        ERROR_MUPDF_R( "fz_new_pixmap_with_bbox", NULL )
    }

    fz_try( ctx) fz_clear_pixmap_with_value( ctx, pixmap, 255 );
    fz_catch( ctx )
    {
        fz_drop_page( ctx, &page->super );
        fz_drop_pixmap( ctx, pixmap );

        ERROR_MUPDF_R( "fz_clear_pixmap", NULL )
    }

    fz_device* draw_device = NULL;
    fz_try( ctx ) draw_device = fz_new_draw_device( ctx, ctm, pixmap );
    fz_catch( ctx )
    {
        fz_drop_page( ctx, &page->super );
        fz_drop_pixmap( ctx, pixmap );

        ERROR_MUPDF_R( "fz_new_draw_device", NULL )
    }

    fz_try( ctx ) pdf_run_page( ctx, page, draw_device, fz_identity, NULL );
    fz_always( ctx )
    {
        fz_close_device( ctx, draw_device );
        fz_drop_device( ctx, draw_device );
        fz_drop_page( ctx, &page->super );
    }
    fz_catch( ctx )
    {
        fz_drop_pixmap( ctx, pixmap );

        ERROR_MUPDF_R( "fz_new_draw_device", NULL )
    }

    return pixmap;
}


//thread-safe
static pdf_document*
pdf_ocr_create_doc_with_page( PdfDocumentPage* pdf_document_page, gint flag, gchar** errmsg )
{
    gint rc = 0;
    pdf_document* doc_new = NULL;
    gint page_doc = 0;
    pdf_obj* page_ref = NULL;

    fz_context* ctx = zond_pdf_document_get_ctx( pdf_document_page->document );
    pdf_document* doc = zond_pdf_document_get_pdf_doc( pdf_document_page->document );

    fz_try( ctx ) doc_new = pdf_create_document( ctx );
    fz_catch( ctx ) ERROR_MUPDF_R( "pdf_create_document", NULL )

    page_doc = zond_pdf_document_get_index( pdf_document_page );

    zond_pdf_document_mutex_lock( pdf_document_page->document );
    rc = pdf_copy_page( ctx, doc, page_doc, page_doc, doc_new, 0, errmsg );
    zond_pdf_document_mutex_unlock( pdf_document_page->document );
    if ( rc )
    {
        pdf_drop_document( ctx, doc_new );
        ERROR_PAO_R( "pdf_copy_page", NULL )
    }

    fz_try( ctx ) page_ref = pdf_lookup_page_obj( ctx, doc_new, 0 );
    fz_catch( ctx )
    {
        pdf_drop_document( ctx, doc_new );
        ERROR_MUPDF_R( "pdf_lookup_page_obj", NULL );
    }

    rc = pdf_ocr_filter_stream( ctx, page_ref, flag, errmsg );
    if ( rc )
    {
        pdf_drop_document( ctx, doc_new );
        ERROR_PAO_R( "pdf_ocr_filter_stream", NULL );
    }

    return doc_new;
}


//thread-safe
static gint
pdf_ocr_page( PdfDocumentPage* pdf_document_page, InfoWindow* info_window,
        TessBaseAPI* handle, TessResultRenderer* renderer, gchar** errmsg )
{
    gint rc = 0;
    fz_pixmap* pixmap = NULL;
    pdf_document* doc_new = NULL;

    doc_new = pdf_ocr_create_doc_with_page( pdf_document_page, 1, errmsg ); //thread-safe
    if ( !doc_new ) ERROR_PAO( "pdf_ocr_create_doc_with_page" )

    fz_context* ctx = fz_clone_context( zond_pdf_document_get_ctx( pdf_document_page->document ) );

    pixmap = pdf_ocr_render_pixmap( ctx, doc_new, 0, 4, errmsg );
    pdf_drop_document( ctx, doc_new );
    if ( !pixmap )
    {
        fz_drop_context( ctx );
        ERROR_PAO( "pdf_render_pixmap" )
    }

    rc = pdf_ocr_tess_page( info_window, handle, pixmap, errmsg );
    fz_drop_pixmap( ctx, pixmap );
    fz_drop_context( ctx );
    if ( rc ) ERROR_PAO( "pdf_ocr_tess_page" )

    return 0;
}


static gboolean
cb_dialog_delete( GtkWidget* window, GdkEvent* event, gpointer data )
{
    gtk_dialog_response( GTK_DIALOG(window), GTK_RESPONSE_CANCEL );

    return TRUE;
}


static GtkWidget*
pdf_ocr_create_dialog( InfoWindow* info_window, gint page )
{
    gchar* titel = g_strdup_printf( "Seite %i enthält bereits "
            "versteckten Text - Text löschen?", page );
    GtkWidget* dialog = gtk_dialog_new_with_buttons( titel,
            GTK_WINDOW(info_window->dialog), GTK_DIALOG_MODAL,
            "Ja", 1, "Ja für alle", 2, "Nein", 3, "Nein für alle", 4,
            "Anzeigen", 5,
            "Abbrechen", GTK_RESPONSE_CANCEL, NULL );
    g_free( titel );

    g_signal_connect( dialog, "delete-event", G_CALLBACK(cb_dialog_delete),
            NULL );

    return dialog;
}


//thread-safe
static fz_pixmap*
pdf_ocr_render_images( PdfDocumentPage* pdf_document_page, gchar** errmsg )
{
    pdf_document* doc_tmp_orig = NULL;
    fz_pixmap* pixmap = NULL;

    doc_tmp_orig = pdf_ocr_create_doc_with_page( pdf_document_page, 1, errmsg ); //thread-safe
    if ( !doc_tmp_orig ) ERROR_PAO_R( "pdf_create_doc_with_page", NULL )

    fz_context* ctx = fz_clone_context( zond_pdf_document_get_ctx( pdf_document_page->document ) );

    pixmap = pdf_ocr_render_pixmap( ctx, doc_tmp_orig,
            0, 1.2, errmsg );
    pdf_drop_document( ctx, doc_tmp_orig );
    fz_drop_context( ctx );
    if ( !pixmap ) ERROR_PAO_R( "pdf_ocr_render_pixmap", NULL )

    return pixmap;
}


static gchar*
pdf_ocr_get_text_from_stext_page( fz_context* ctx, fz_stext_page* stext_page,
        gchar** errmsg )
{
    gchar* text = "";
    guchar* text_tmp = NULL;
    fz_buffer* buf = NULL;
    fz_output* out = NULL;

    fz_try( ctx ) buf = fz_new_buffer( ctx, 1024 );
    fz_catch( ctx ) ERROR_MUPDF_R( "fz_new_buffer", NULL );

    fz_try( ctx ) out = fz_new_output_with_buffer( ctx, buf );
    fz_catch( ctx )
    {
        fz_drop_buffer( ctx, buf );
        ERROR_MUPDF_R( "fz_new_output_with_buffer", NULL );
    }

    fz_try( ctx ) fz_print_stext_page_as_text( ctx, out, stext_page );
    fz_always( ctx )
    {
        fz_close_output( ctx, out );
        fz_drop_output( ctx, out );
    }
    fz_catch( ctx )
    {
        fz_drop_buffer( ctx, buf );
        ERROR_MUPDF_R( "fz_print_stext_page_as_text", NULL )
    }

    fz_try( ctx ) fz_terminate_buffer( ctx, buf );
    fz_catch( ctx )
    {
        fz_drop_buffer( ctx, buf );
        ERROR_MUPDF_R( "fz_terminate_buffer", NULL );
    }

    fz_buffer_storage( ctx, buf, &text_tmp );
    text = g_strdup( (gchar*) text_tmp );
    fz_drop_buffer( ctx, buf );

    return text;
}


//thread-safe
static gchar*
pdf_ocr_get_hidden_text( PdfDocumentPage* pdf_document_page, gchar** errmsg )
{
    pdf_document* doc_tmp_alt = NULL;
    fz_page* page = NULL;
    fz_stext_page* stext_page = NULL;
    fz_device* s_t_device = NULL;
    gchar* text = NULL;

    //flag == 4: alles außer verstecktem Text herausfiltern
    doc_tmp_alt = pdf_ocr_create_doc_with_page( pdf_document_page, 4, errmsg ); //thread-safe
    if ( !doc_tmp_alt ) ERROR_PAO_R( "pdf_create_doc_with_page", NULL )

    fz_context* ctx = fz_clone_context( zond_pdf_document_get_ctx( pdf_document_page->document ) );

    fz_try( ctx ) page = fz_load_page( ctx, (fz_document*) doc_tmp_alt, 0 );
    fz_catch( ctx )
    {
        pdf_drop_document( ctx, doc_tmp_alt );
        ERR_MUPDF( "fz_load_page" );
        fz_drop_context( ctx );

        return NULL;
    }

    //structured text-device
    fz_try( ctx ) stext_page = fz_new_stext_page( ctx, fz_bound_page( ctx, page ) );
    fz_catch( ctx )
    {
        fz_drop_page( ctx, page );
        pdf_drop_document( ctx, doc_tmp_alt );
        ERR_MUPDF( "fz_new_stext_page" )
        fz_drop_context( ctx );

        return NULL;
    }

    fz_try( ctx ) s_t_device = fz_new_stext_device( ctx, stext_page, NULL );
    fz_catch( ctx )
    {
        fz_drop_stext_page( ctx, stext_page );
        fz_drop_page( ctx, page );
        pdf_drop_document( ctx, doc_tmp_alt );
        ERR_MUPDF( "fz_new_stext_device" )
        fz_drop_context( ctx );

        return NULL;
    }

//Seite durch's device laufen lassen
    fz_try( ctx ) fz_run_page( ctx, page, s_t_device, fz_identity, NULL );
    fz_always( ctx )
    {
        fz_close_device( ctx, s_t_device );
        fz_drop_device( ctx, s_t_device );
        fz_drop_page( ctx, page );
        pdf_drop_document( ctx, doc_tmp_alt );
    }
    fz_catch( ctx )
    {
        fz_drop_stext_page( ctx, stext_page );
        ERR_MUPDF( "fz_run_page" )
        fz_drop_context( ctx );

        return NULL;
    }

    //bisheriger versteckter Text
    text = pdf_ocr_get_text_from_stext_page( ctx, stext_page, errmsg );
    fz_drop_stext_page( ctx, stext_page );
    fz_drop_context( ctx );
    if ( !text ) ERROR_PAO_R( "pdf_get_text_from_stext_page", NULL )

    return text;
}


//thread-safe
static gint
pdf_ocr_show_text( InfoWindow* info_window, PdfDocumentPage* pdf_document_page,
        TessBaseAPI* handle, TessResultRenderer* renderer, gchar** errmsg )
{
    gint rc = 0;
    gchar* text_alt = NULL;
    fz_pixmap* pixmap_orig = NULL;
    gchar* text_neu = NULL;

    //Bisherigen versteckten Text
    text_alt = pdf_ocr_get_hidden_text( pdf_document_page, errmsg ); //thread-safe
    if ( !text_alt ) ERROR_PAO( "pdf_ocr_get_hidden_text" )

    //gerenderte Seite ohne sichtbaren Text
    pixmap_orig = pdf_ocr_render_images( pdf_document_page, errmsg ); //thread-safe
    if ( !pixmap_orig )
    {
        g_free( text_alt );
        ERROR_PAO( "pdf_ocr_render_images" )
    }

    //Eigene OCR
    //Wenn angezeigt werden soll, dann muß Seite erstmal OCRed werden
    //Um Vergleich zu haben
    rc = pdf_ocr_page( pdf_document_page, info_window, handle, renderer, errmsg ); //thread-safe
    if ( rc )
    {
        fz_context* ctx = fz_clone_context( zond_pdf_document_get_ctx( pdf_document_page->document ) );
        fz_drop_pixmap( ctx, pixmap_orig );
        fz_drop_context( ctx );
        g_free( text_alt );
        ERROR_PAO( "pdf_ocr_page" )
    }
    text_neu = TessBaseAPIGetUTF8Text( handle );

    //dialog erzeugen und erweitern
    GtkWidget* label_alt = gtk_label_new( "Gespeicherter Text" );
    GtkWidget* label_neu = gtk_label_new( "Neuer Text" );
    GtkWidget* hbox = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 0 );
    gtk_box_pack_start( GTK_BOX(hbox), label_alt, FALSE, FALSE, 0 );
    gtk_box_pack_end( GTK_BOX(hbox), label_neu, FALSE, FALSE, 0 );

    GtkWidget* text_view_alt = gtk_text_view_new( );
    gtk_text_view_set_editable( GTK_TEXT_VIEW(text_view_alt), FALSE );
    gtk_text_buffer_set_text( gtk_text_view_get_buffer(
            GTK_TEXT_VIEW(text_view_alt) ), text_alt, -1 );
    g_free( text_alt );

    GtkWidget* text_view_neu = gtk_text_view_new( );
    gtk_text_view_set_editable( GTK_TEXT_VIEW(text_view_neu), FALSE );
    gtk_text_buffer_set_text( gtk_text_view_get_buffer(
            GTK_TEXT_VIEW(text_view_neu) ), text_neu, -1 );
    TessDeleteText( text_neu );

    GtkWidget* swindow_alt = gtk_scrolled_window_new( NULL, NULL );

    GtkWidget* image_orig = gtk_image_new( );
    GdkPixbuf* pixbuf_orig =
            gdk_pixbuf_new_from_data( pixmap_orig->samples,
            GDK_COLORSPACE_RGB, FALSE, 8, pixmap_orig->w,
            pixmap_orig->h, pixmap_orig->stride, NULL, NULL );
    gtk_image_set_from_pixbuf( GTK_IMAGE(image_orig), pixbuf_orig );

    GtkWidget* swindow_neu = gtk_scrolled_window_new( NULL, NULL );

    gtk_container_add( GTK_CONTAINER(swindow_alt), text_view_alt );
    gtk_container_add( GTK_CONTAINER(swindow_neu), text_view_neu );

    GtkWidget* hbox2 = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 0 );
    gtk_box_pack_start( GTK_BOX(hbox2), swindow_alt, TRUE, TRUE, 0 );
    gtk_box_pack_start( GTK_BOX(hbox2), image_orig, TRUE, TRUE, 0 );
    gtk_box_pack_start( GTK_BOX(hbox2), swindow_neu, TRUE, TRUE, 0 );

    GtkWidget* swindow = gtk_scrolled_window_new( NULL, NULL );
    gtk_container_add( GTK_CONTAINER(swindow), hbox2 );

    GtkWidget* vbox = gtk_box_new( GTK_ORIENTATION_VERTICAL, 0 );
    gtk_box_pack_start( GTK_BOX(vbox), hbox, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(vbox), swindow, TRUE, TRUE, 0 );

    gint page = zond_pdf_document_get_index( pdf_document_page );
    GtkWidget* dialog = pdf_ocr_create_dialog( info_window, page + 1 );

    GtkWidget* content_area =
            gtk_dialog_get_content_area( GTK_DIALOG(dialog) );
    gtk_box_pack_start( GTK_BOX(content_area), vbox, TRUE, TRUE, 0 );

    gtk_dialog_set_response_sensitive( GTK_DIALOG(dialog), 5, FALSE );

    //anzeigen
    gtk_window_maximize( GTK_WINDOW(dialog) );
    gtk_widget_show_all( dialog );

    //neue Abfrage
    rc = gtk_dialog_run( GTK_DIALOG(dialog) );

    gtk_widget_destroy( dialog );
    fz_context* ctx = fz_clone_context( zond_pdf_document_get_ctx( pdf_document_page->document ) );
    fz_drop_pixmap( ctx, pixmap_orig );
    fz_drop_context( ctx );

    return rc;
}


/** thread-safe **/
static gint
pdf_ocr_page_has_hidden_text( PdfDocumentPage* pdf_document_page, gchar** errmsg )
{
    fz_buffer* buf = NULL;
    gchar* data = NULL;
    gchar* ptr = NULL;
    size_t size = 0;

    zond_pdf_document_mutex_lock( pdf_document_page->document );

    fz_context* ctx = zond_pdf_document_get_ctx( pdf_document_page->document );

    buf = pdf_ocr_get_content_stream_as_buffer( ctx, pdf_document_page->page->obj, errmsg );
    if ( !buf )
    {
        zond_pdf_document_mutex_unlock( pdf_document_page->document );
        ERROR_PAO( "pdf_ocr_get_content_stream_as_buffer" )
    }

    size = fz_buffer_storage( ctx, buf, (guchar**) &data );
    ptr = data;
    while ( ptr < data + size )
    {
        if ( *ptr == '3' )
        {
            ptr++;
            while ( is_white( ptr ) ) ptr++;

            if ( *ptr == 'T' && *(ptr + 1) == 'r' )
            {
                fz_drop_buffer( ctx, buf );
                zond_pdf_document_mutex_unlock( pdf_document_page->document );

                return 1;
            }
        }
        ptr ++;
    }
    fz_drop_buffer( ctx, buf );

    zond_pdf_document_mutex_unlock( pdf_document_page->document );

    return 0;
}


static gint
pdf_ocr_create_pdf_only_text( InfoWindow* info_window,
        GPtrArray* arr_document_pages, TessBaseAPI* handle,
        TessResultRenderer* renderer, gchar** errmsg )
{
    gint rc = 0;
    gint len = 0;
    gint zaehler = 0;
    gint i = 0;
    gint alle = 0;

    len = arr_document_pages->len;

    for ( i = 0; i < arr_document_pages->len; i++ )
    {
        gboolean rendered = FALSE;

        zaehler++;

        PdfDocumentPage* pdf_document_page = g_ptr_array_index( arr_document_pages, i );
        gint index = zond_pdf_document_get_index( pdf_document_page );

        gchar* info_text = g_strdup_printf( "(%i/%i) %s, Seite %i",
                zaehler, len, zond_pdf_document_get_path( pdf_document_page->document ),
                index + 1 );
        info_window_set_message( info_window, info_text );
        g_free( info_text );

        rc = pdf_ocr_page_has_hidden_text( pdf_document_page, errmsg ); //thread_safe
        if ( rc == -1 ) ERROR_PAO( "pdf_ocr_page_has_hidden_text" )

        if ( rc == 1 && alle == 0 )
        {
            GtkWidget* dialog = pdf_ocr_create_dialog( info_window, index + 1 );
            //braucht nicht thread_safe zu sein
            rc = 0;
            rc = gtk_dialog_run( GTK_DIALOG(dialog) );

            gtk_widget_destroy( dialog );

            //Wenn Anzeigen gewählt wird, dialog in Unterfunktion neu starten
            if ( rc == 5 )
            {
                rc = pdf_ocr_show_text( info_window, pdf_document_page, handle,
                        renderer, errmsg ); //thread-safe
                if ( rc == -1 ) ERROR_PAO( "pdf_ocr_show_text" )
                rendered = TRUE;
            }

            if ( rc == GTK_RESPONSE_CANCEL || rc == GTK_RESPONSE_DELETE_EVENT )
                    break;
            if ( rc == 1 ) rc = 0; //damit, falls bereits rendered, keine Fehlermeldung
            if ( rc == 2 ) alle = 1;
            if ( rc == 3 ) //Nein
            {
                //Seite an Stelle in Array "setzen"
                g_ptr_array_remove_index( arr_document_pages, i );
                i--;
                continue;
            }
            if ( rc == 4 ) break;
        }

        if ( !rendered ) rc = pdf_ocr_page( pdf_document_page, info_window, handle,
                renderer, errmsg ); //thread-safe
        if ( rc ) ERROR_PAO( "pdf_ocr_page" )

        if ( info_window->cancel ) break;

        //PDF rendern
        TessResultRendererAddImage( renderer, handle );
    }

    if ( i < arr_document_pages->len )
            g_ptr_array_remove_range( arr_document_pages, i, arr_document_pages->len - i );

    return 0;
}


static gint
init_tesseract( TessBaseAPI** handle, TessResultRenderer** renderer, gchar** errmsg )
{
    gint rc = 0;
    gchar* tessdata_dir = NULL;
    gchar* path_tmp = NULL;

    //TessBaseAPI
    *handle = TessBaseAPICreate( );
    if ( !(*handle) ) ERROR_PAO( "TessBaseApiCreate" )

    tessdata_dir = get_path_from_base( "share/tessdata", errmsg );
    if ( !tessdata_dir )
    {
        TessBaseAPIDelete( *handle );
        ERROR_SOND( "get_path_from_base" )
    }

    rc = TessBaseAPIInit3( *handle, tessdata_dir, "deu" );
    g_free( tessdata_dir );
    if ( rc )
    {
        TessBaseAPIEnd( *handle );
        TessBaseAPIDelete( *handle );
        ERROR_PAO( "TessBaseAPIInit3:\nFehler bei Initialisierung" )
    }

    path_tmp = get_path_from_base( "tmp/tess_tmp", errmsg );
    if ( !path_tmp )
    {
        TessBaseAPIEnd( *handle );
        TessBaseAPIDelete( *handle );
        ERROR_PAO( "get_path_from_base" );
    }

    //TessPdfRenderer
    *renderer = TessPDFRendererCreate( path_tmp, TessBaseAPIGetDatapath( *handle ), 1 );
    g_free( path_tmp );
    if ( !(*renderer) )
    {
        TessBaseAPIEnd( *handle );
        TessBaseAPIDelete( *handle );

        ERROR_PAO( "TessPDFRendererCreate" )
    }
    TessResultRendererBeginDocument( *renderer, NULL );

    return 0;
}


/** Rückgabewert:
*** Bei Fehler: -1; *errmsg wird gesetzt
*** Bei Abbruch: 0 **/
gint
pdf_ocr_pages( Projekt* zond, InfoWindow* info_window, GPtrArray* arr_document_pages, gchar** errmsg )
{
    gint rc = 0;
    TessBaseAPI* handle = NULL;
    TessResultRenderer* renderer = NULL;

    rc = init_tesseract( &handle, &renderer, errmsg );
    if ( rc ) ERROR_PAO( "init_tesseract" )

    rc = pdf_ocr_create_pdf_only_text( info_window, arr_document_pages, handle,
            renderer, errmsg );

    TessResultRendererEndDocument( renderer );
    TessDeleteResultRenderer( renderer );
    TessBaseAPIEnd( handle );
    TessBaseAPIDelete( handle );

    if ( rc ) ERROR_PAO( "pdf_ocr_create_pdf_only_text" )

    //erzeugtes PDF mit nur Text mit muPDF öffnen
    gchar* path_tmp = NULL;
    pdf_document* doc_text = NULL;

    path_tmp = get_path_from_base( "tmp/tess_tmp.pdf", errmsg );
    if ( !path_tmp ) ERROR_PAO_R( "get_path_from_base", -2 );

    //doc mit text öffnen
    fz_try( zond->ctx ) doc_text = pdf_open_document( zond->ctx, path_tmp );
    fz_always( zond->ctx ) { g_free( path_tmp ); }
    fz_catch( zond->ctx ) ERROR_MUPDF_CTX( "pdf_open_document", zond->ctx )

    //Text in PDF übertragen
    rc = pdf_ocr_sandwich_doc( arr_document_pages, doc_text, info_window, errmsg ); //thread-safe
    pdf_drop_document( zond->ctx, doc_text );
    if ( rc ) ERROR_PAO( "pdf_ocr_sandwich_doc" )

    return 0;
}


/*
Irgendwie sind die Bibliotheken für das Lesen von Pdf etc nicht mitkompiliert
D.h. wenn müßte das über command_line gehen
gint
ocr_convert_page( const gchar* path, gint page, gchar** errmsg )
{
    MagickBooleanType status;
	MagickWand *mw = NULL;

	MagickWandGenesis( );

	// Create a wand
	mw = NewMagickWand();

	gchar* image = g_strdup_printf( "%s[%i]", path, page );

	// Read the input image
	status = MagickReadImage( mw, "audio.png" ); //image );
	g_free( image );
	if ( status == MagickFalse )
    {
        char *description = NULL;
        ExceptionType severity;
        description = MagickGetException( mw, &severity );

        if ( errmsg ) *errmsg = add_string( *errmsg,
                g_strconcat( "Bei Aufruf MagickReadImage:\n",
                description, NULL ) );
        MagickRelinquishMemory( description );

        return -1;
    }

	// write it
	MagickWriteImage( mw, "out.tiff" );

	// Tidy up
	if(mw) mw = DestroyMagickWand(mw);

	MagickWandTerminus();

	return 0;
}
*/
/*
gint
pdf_ocr_to_tiff( const gchar* path, gint page, gchar** errmsg )
{
    gint exit = 0;
    GError* error = NULL;
    gchar* path_page = NULL;

    path_page = g_strdup_printf( "%s[%i]", path, page );

    gchar* argv[13] = { NULL };
    argv[0] = "C:/_msys64/mingw64/bin/convert";
    argv[1] = "-density";
    argv[2] = "300";
    argv[3] = path_page;
    argv[4] = "-depth";
    argv[5] = "8";
    argv[6] = "-strip";
    argv[7] = "-background";
    argv[8] = "white";
    argv[9] = "-alpha";
    argv[10] = "off";
    argv[11] = "output.tiff";
    argv[12] = NULL;


    gboolean rc = g_spawn_sync( NULL, argv, NULL, G_SPAWN_DEFAULT,
            NULL, NULL, NULL, NULL, &exit, &error );
    g_free( path_page );
    if ( !rc )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_spawn_async:\n",
                error->message, NULL );
        g_error_free( error );

        return -1;
    }

	return 0;
}
*/
