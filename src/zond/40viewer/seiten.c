/*
zond (seiten.c) - Akten, Beweisstücke, Unterlagen
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

#include "../zond_pdf_document.h"

#include "../global_types.h"
#include "../zond_dbase.h"

#include "../99conv/general.h"
#include "../99conv/pdf.h"
#include "../pdf_ocr.h"

#include "../20allgemein/ziele.h"
#include "../20allgemein/project.h"

#include "viewer.h"
#include "render.h"
#include "document.h"

#include "../../misc.h"


static GPtrArray*
seiten_get_document_pages( PdfViewer* pv, GArray* arr_seiten_pv )
{
    GPtrArray* arr_document_page = NULL;

    if ( arr_seiten_pv )
    {
        arr_document_page = g_ptr_array_new( );

        for ( gint i = 0; i < arr_seiten_pv->len; i++ )
        {
            ViewerPageNew* viewer_page = g_ptr_array_index( pv->arr_pages,
                    g_array_index( arr_seiten_pv, gint, i ) );

            if ( !g_ptr_array_find( arr_document_page, viewer_page->pdf_document_page, NULL ) )
                    g_ptr_array_add( arr_document_page, (gpointer) viewer_page->pdf_document_page );
        }
    }

    return arr_document_page;
}


static GArray*
seiten_markierte_thumbs( PdfViewer* pv )
{
    GList* selected = NULL;
    GList* list = NULL;
    GArray* arr_page_pv = NULL;
    gint* index = NULL;

    selected = gtk_tree_selection_get_selected_rows(
            gtk_tree_view_get_selection( GTK_TREE_VIEW(pv->tree_thumb) ), NULL );

    if ( !selected ) return NULL;

    arr_page_pv = g_array_new( FALSE, FALSE, sizeof( gint ) );
    list = selected;
    do
    {
        index = gtk_tree_path_get_indices( list->data );
        g_array_append_val( arr_page_pv, index[0] );
    }
    while ( (list = list->next) );

    g_list_free_full( selected, (GDestroyNotify) gtk_tree_path_free );

    return arr_page_pv;
}


static gint
compare_gint ( gconstpointer a, gconstpointer b )
{
  const gint *_a = a;
  const gint *_b = b;

  return *_a - *_b;
}


static GArray*
seiten_parse_text( PdfViewer* pv, gint max, const gchar* text )
{
    gint start = 0;
    gint end = 0;
    const gchar* range = NULL;
    gint i = 0;
    gint page = 0;
    gint last_inserted = -1;

    if ( !fz_is_page_range( NULL, text ) ) return NULL;

    GArray* arr_tmp = g_array_new( FALSE, FALSE, sizeof( gint ) );
    GArray* arr_pages = g_array_new( FALSE, FALSE, sizeof( gint ) );

    range = text;
    while ((range = fz_parse_page_range( NULL, range, &start, &end, max )) )
    {
        if ( start < end )
        {
            for ( i = start; i <= end; i++ ) g_array_append_val( arr_tmp, i );
        }
        else if ( start > end )
        {
            for ( i = start; i >= end; i-- ) g_array_append_val( arr_tmp, i );
        }
        else g_array_append_val( arr_tmp, start );
    }

    g_array_sort( arr_tmp, (GCompareFunc) compare_gint );

    for ( i = 0; i < arr_tmp->len; i++ )
    {
        page = g_array_index( arr_tmp, gint, i ) - 1;
        if ( page != last_inserted )
        {
            g_array_append_val( arr_pages, page );
            last_inserted = page;
        }
    }

    g_array_free( arr_tmp, TRUE );

    return arr_pages;
}


static void
cb_seiten_drehen_entry( GtkEntry* entry, gpointer dialog )
{
    gtk_dialog_response( GTK_DIALOG(dialog), GTK_RESPONSE_OK );

    return;
}


static void
cb_radio_auswahl_toggled( GtkToggleButton* button, gpointer data )
{
    gtk_widget_set_sensitive( (GtkWidget*) data,
            gtk_toggle_button_get_active( button ) );

    return;
}


static GPtrArray*
seiten_abfrage_seiten( PdfViewer* pv, const gchar* title, gint* winkel )
{
    gint rc = 0;
    GtkWidget* radio_90_UZS = NULL;
    GtkWidget* radio_180 = NULL;
    GtkWidget* radio_90_gegen_UZS = NULL;
    gchar* text = NULL;
    GPtrArray* arr_document_pages = NULL;

    GtkWidget* dialog = gtk_dialog_new_with_buttons( title,
            GTK_WINDOW(pv->vf), GTK_DIALOG_MODAL,
            "Ok", GTK_RESPONSE_OK, "Abbrechen", GTK_RESPONSE_CANCEL, NULL );

    GtkWidget* content_area = gtk_dialog_get_content_area( GTK_DIALOG(dialog) );

    if ( winkel )
    {
        radio_90_UZS = gtk_radio_button_new_with_label( NULL, "90° im Uhrzeigersinn" );
        radio_180 = gtk_radio_button_new_with_label( NULL, "180°" );
        radio_90_gegen_UZS = gtk_radio_button_new_with_label( NULL, "90° gegen Uhrzeigersinn" );

        gtk_radio_button_join_group (GTK_RADIO_BUTTON(radio_180),
                GTK_RADIO_BUTTON(radio_90_UZS) );
        gtk_radio_button_join_group( GTK_RADIO_BUTTON(radio_90_gegen_UZS),
                GTK_RADIO_BUTTON(radio_180) );

        gtk_box_pack_start( GTK_BOX(content_area), radio_90_UZS, FALSE, FALSE, 0 );
        gtk_box_pack_start( GTK_BOX(content_area), radio_180, FALSE, FALSE, 0 );
        gtk_box_pack_start( GTK_BOX(content_area), radio_90_gegen_UZS, FALSE, FALSE, 0 );
    }

    GtkWidget* radio_alle = gtk_radio_button_new_with_label( NULL, "Alle" );
    GtkWidget* radio_mark = gtk_radio_button_new_with_label( NULL, "Markierte" );
    GtkWidget* radio_auswahl =
            gtk_radio_button_new_with_label( NULL, "Seiten auswählen" );

    gtk_radio_button_join_group (GTK_RADIO_BUTTON(radio_alle),
            GTK_RADIO_BUTTON(radio_auswahl) );
    gtk_radio_button_join_group( GTK_RADIO_BUTTON(radio_mark),
            GTK_RADIO_BUTTON(radio_alle) );

    GtkWidget* hbox = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 0 );
    gtk_box_pack_start( GTK_BOX(hbox), radio_auswahl, FALSE, FALSE, 0 );

    GtkWidget* entry = gtk_entry_new( );
    gtk_box_pack_start( GTK_BOX(hbox), entry, FALSE, FALSE, 0 );

    gtk_box_pack_start( GTK_BOX(content_area), radio_alle, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(content_area), radio_mark, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(content_area), hbox, FALSE, FALSE, 0 );

    g_signal_connect( radio_auswahl, "toggled",
            G_CALLBACK( cb_radio_auswahl_toggled), entry );

    if ( !gtk_tree_selection_count_selected_rows( gtk_tree_view_get_selection(
            GTK_TREE_VIEW(pv->tree_thumb) ) ) )
    {
        gtk_widget_set_sensitive( radio_mark, FALSE );
        gtk_widget_grab_focus( entry );
    }
    else gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(radio_mark), TRUE );

    g_signal_connect( entry, "activate", G_CALLBACK(cb_seiten_drehen_entry),
            (gpointer) dialog );

    gtk_widget_show_all( dialog );

    rc = gtk_dialog_run( GTK_DIALOG(dialog) );

    if ( rc == GTK_RESPONSE_OK )
    {
        GArray* arr_seiten_pv = NULL;

        if ( winkel )
        {
            if ( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(radio_90_UZS) ) )
                    *winkel = 90;
            else if ( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(radio_180) ) )
                    *winkel = 180;
            else *winkel = -90;
        }

        if ( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(radio_auswahl) ) )
        {
            text = g_strdup( gtk_entry_get_text( GTK_ENTRY(entry) ) );
            if ( text ) arr_seiten_pv =
                    seiten_parse_text( pv, pv->arr_pages->len, text );
            g_free( text );
        }
        else if ( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(radio_alle) ) )
        {
            arr_seiten_pv = g_array_new( FALSE, FALSE, sizeof( gint ) );
            for ( gint i = 0; i < pv->arr_pages->len; i++ )
                    g_array_append_val( arr_seiten_pv, i );
        }
        else if ( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(radio_mark) ) )
        {
            arr_seiten_pv = seiten_markierte_thumbs( pv );
        }

        if ( arr_seiten_pv && arr_seiten_pv->len )
        {
            arr_document_pages = seiten_get_document_pages( pv, arr_seiten_pv );
            g_array_unref( arr_seiten_pv );
        }
    }

    gtk_widget_destroy( dialog );

    return arr_document_pages;
}


/*
**  Seiten OCR
*/
void
cb_pv_seiten_ocr( GtkMenuItem* item, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    gchar* title = NULL;
    GPtrArray* arr_document_page = NULL;
    InfoWindow* info_window = NULL;

    PdfViewer* pv = (PdfViewer*) data;

    //zu OCRende Seiten holen
    title = g_strdup_printf( "Seiten OCR (1 - %i):", pv->arr_pages->len );
    arr_document_page = seiten_abfrage_seiten( pv, title, NULL );
    g_free( title );

    if ( !arr_document_page ) return;

    info_window = info_window_open( pv->vf, "OCR" );

    rc = pdf_ocr_pages( pv->zond, info_window, arr_document_page, &errmsg );
    info_window_close( info_window );

    if ( rc == -1 )
    {
        display_message( pv->vf, "Fehler - OCR\n\nBei Aufruf pdf_ocr_pages:\n", errmsg,
                NULL );
        g_free( errmsg );
        g_ptr_array_unref( arr_document_page );

        return;
    }

    for ( gint i = 0; i < arr_document_page->len; i++ )
    {
        PdfDocumentPage* pdf_document_page = g_ptr_array_index( arr_document_page, i );

        //fz_text_list droppen und auf NULL setzen
        while ( pdf_document_page->thread & 1 )
                viewer_transfer_rendered( pdf_document_page->thread_pv, TRUE );

        fz_drop_display_list( zond_pdf_document_get_ctx( pdf_document_page->document ),
                pdf_document_page->display_list );
        pdf_document_page->display_list = NULL;

        fz_drop_stext_page( zond_pdf_document_get_ctx( pdf_document_page->document ),
                pdf_document_page->stext_page );
        pdf_document_page->stext_page = NULL;

        pdf_document_page->thread &= 2;

        //Damit speichern angeht - gibt keinen Fehler zurück, wenn func == NULL
        viewer_foreach( pv, pdf_document_page, NULL, NULL, &errmsg );
    }

    //damit Text von Cursor "erkannt" wird
    g_signal_emit_by_name( pv->v_adj, "value-changed", NULL );

    g_ptr_array_unref( arr_document_page );

    return;
}


static void
seiten_refresh_layouts( GPtrArray* arr_pv )
{
    for ( gint i = 0; i < arr_pv->len; i++ )
    {
        PdfViewer* pv = g_ptr_array_index( arr_pv, i );

        if ( g_object_get_data( G_OBJECT(pv->layout), "dirty" ) )
        {
            viewer_refresh_layout( pv, 0 );
            g_object_set_data( G_OBJECT(pv->layout), "dirty", NULL );

            g_signal_emit_by_name( pv->v_adj, "value-changed", NULL );
        }
    }

    return;
}


/*
**      Seiten drehen
*/
static void
seiten_page_tilt( ViewerPageNew* viewer_page)
{
    float x1_tmp = 0.0;
    float y1_tmp = 0.0;

    x1_tmp = viewer_page->crop.x0 + (viewer_page->crop.y1 - viewer_page->crop.y0);
    y1_tmp = viewer_page->crop.y0 + (viewer_page->crop.x1 - viewer_page->crop.x0);

    viewer_page->crop.x1 = x1_tmp;
    viewer_page->crop.y1 = y1_tmp;

    return;
}


static gint
seiten_drehen_foreach( PdfViewer* pv, gint page_pv, gpointer data, gchar** errmsg )
{
    gint winkel = 0;
    gint rc = 0;
    GtkTreeIter iter = { 0 };
    winkel = GPOINTER_TO_INT(data);
    ViewerPageNew* viewer_page = NULL;

    viewer_close_thread_pool_and_transfer( pv );

    //damit in Seite gezeichnete Markierungen nach dem Drehen nicht an falscher Stelle sind
    pv->clicked_annot = NULL;
    pv->highlight.page[0] = -1;

    viewer_page = g_ptr_array_index( pv->arr_pages, page_pv );

    if ( viewer_page->image_page ) gtk_image_clear( GTK_IMAGE(viewer_page->image_page) );
    viewer_page->pixbuf_page = NULL;

    rc = viewer_get_iter_thumb( pv, page_pv, &iter );
    if ( rc ) ERROR_S_MESSAGE( "Bei Aufruf viewer_get_iter:\niter konnte "
            "nicht ermittelt werden" );

    gtk_list_store_set( GTK_LIST_STORE( gtk_tree_view_get_model(
            GTK_TREE_VIEW(pv->tree_thumb) ) ), &iter, 0, NULL, -1 );
    viewer_page->pixbuf_thumb = NULL;

    if ( winkel == 90 || winkel == -90 )
    {
        seiten_page_tilt( viewer_page );
        g_object_set_data( G_OBJECT(pv->layout), "dirty", GINT_TO_POINTER(1) );
    }

    viewer_page->thread = 0;
//    g_signal_emit_by_name( pv->v_adj, "value-changed", NULL );

    return 0;
}


static gint
seiten_drehen_pdf( PdfDocumentPage* pdf_document_page, gint winkel, gchar** errmsg )
{
    pdf_obj* page_obj = NULL;
    pdf_obj* rotate_obj = NULL;
    gint rotate = 0;

    fz_context* ctx = zond_pdf_document_get_ctx( pdf_document_page->document );

    page_obj = pdf_document_page->obj;

    fz_try( ctx ) rotate_obj = pdf_dict_get_inheritable( ctx, page_obj, PDF_NAME(Rotate) );
    fz_catch( ctx ) ERROR_MUPDF( "pdf_dict_get_inheritable" )

    if ( !rotate_obj )
    {
        rotate_obj = pdf_new_int( ctx, (int64_t) winkel );
        fz_try( ctx ) pdf_dict_put_drop( ctx, page_obj, PDF_NAME(Rotate), rotate_obj );
        fz_catch( ctx ) ERROR_MUPDF( "pdf_dict_put_drop" )
    }
    else
    {
        rotate = pdf_to_int( ctx, rotate_obj );
        rotate += winkel;
        if ( rotate < 0 ) rotate += 360;
        else if ( rotate > 360 ) rotate -= 360;
        else if ( rotate == 360 ) rotate = 0;

        pdf_set_int( ctx, rotate_obj, (int64_t) rotate );
    }

    pdf_document_page->rotate = rotate;

    return 0;
}


static gint
seiten_drehen( PdfViewer* pv, GPtrArray* arr_document_page, gint winkel, gchar** errmsg )
{
    for ( gint i = 0; i < arr_document_page->len; i++ )
    {
        gint rc = 0;

        PdfDocumentPage* pdf_document_page = g_ptr_array_index( arr_document_page, i );

        zond_pdf_document_mutex_lock( pdf_document_page->document );

        rc = seiten_drehen_pdf( pdf_document_page, winkel, errmsg );
        if ( rc == -1 )
        {
            zond_pdf_document_mutex_unlock( pdf_document_page->document );
            ERROR_S
        }

        zond_pdf_document_mutex_unlock( pdf_document_page->document );

        while ( pdf_document_page->thread & 1 )
                viewer_transfer_rendered( (PdfViewer*) pdf_document_page->thread_pv, TRUE );

        fz_drop_display_list( zond_pdf_document_get_ctx( pdf_document_page->document ),
                pdf_document_page->display_list );
        pdf_document_page->display_list = NULL;

        fz_drop_stext_page( zond_pdf_document_get_ctx( pdf_document_page->document ),
                pdf_document_page->stext_page );
        pdf_document_page->stext_page = NULL;

        pdf_document_page->thread &= 2;

        rc = viewer_foreach( pv, pdf_document_page,
                seiten_drehen_foreach, GINT_TO_POINTER(winkel), errmsg );
        if ( rc )
        {
            viewer_save_and_close( pv );

            ERROR_S
        }
    }

    return 0;
}


void
cb_pv_seiten_drehen( GtkMenuItem* item, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    gint winkel = 0;
    gchar* title = NULL;
    GPtrArray* arr_document_page = NULL;

    PdfViewer* pv = (PdfViewer*) data;

    //zu drehende Seiten holen
    title = g_strdup_printf( "Seiten drehen (1 - %i):", pv->arr_pages->len );
    arr_document_page = seiten_abfrage_seiten( pv, title, &winkel );
    g_free( title );

    if ( !arr_document_page ) return;

    rc = seiten_drehen( pv, arr_document_page, winkel, &errmsg );
    g_ptr_array_unref( arr_document_page );
    if ( rc )
    {
        display_message( pv->vf, "Fehler in Seiten drehen -\n\nBei Aufruf "
                "seiten_drehen\n", errmsg, NULL );
        g_free( errmsg );

        return;
    }

    seiten_refresh_layouts( pv->zond->arr_pv );

    return;
}


/*
**      Seiten löschen
*/
static gint
seiten_cb_loesche_seite( PdfViewer* pv, gint page_pv, gpointer data, gchar** errmsg )
{
    gint rc = 0;
    GtkTreeIter iter;
    ViewerPageNew* viewer_page = NULL;

    viewer_close_thread_pool_and_transfer( pv );

    //pv muß neues layout haben!
    g_object_set_data( G_OBJECT(pv->layout), "dirty", GINT_TO_POINTER(1) );

    viewer_page = g_ptr_array_index( pv->arr_pages, page_pv );
    if ( viewer_page->image_page ) gtk_widget_destroy( viewer_page->image_page );
    g_ptr_array_remove_index( pv->arr_pages, page_pv ); //viewer_page wird freed!

    rc = viewer_get_iter_thumb( pv, page_pv, &iter );
    if ( rc ) ERROR_S_MESSAGE( "Bei Aufruf viewer_get_iter_thumb:\n"
                "Iter konnte nicht ermittelt werden" );

    gtk_list_store_remove( GTK_LIST_STORE( gtk_tree_view_get_model(
            GTK_TREE_VIEW(pv->tree_thumb) ) ), &iter );

    return 0;
}


static gint
seiten_anbindung( PdfViewer* pv, GPtrArray* arr_document_page, gchar** errmsg )
{
    gint rc = 0;
    GPtrArray* arr_dests = NULL;

    arr_dests = g_ptr_array_new_with_free_func( (GDestroyNotify) g_free );

    //Alle NamedDests der zu löschenden Seiten sammeln
    for ( gint i = 0; i < arr_document_page->len; i++ )
    {
        PdfDocumentPage* pdf_document_page = g_ptr_array_index( arr_document_page, i );
        fz_context* ctx = zond_pdf_document_get_ctx( pdf_document_page->document );
        pdf_document* doc = zond_pdf_document_get_pdf_doc( pdf_document_page->document );

        rc = pdf_document_get_dest( ctx, doc, pdf_document_page->page_doc, (gpointer*) &arr_dests,
                FALSE, errmsg );
        if ( rc )
        {
            g_ptr_array_free( arr_dests, TRUE );

            ERROR_S
        }
    }
#ifdef VIEWER
    if ( arr_dests->len > 0 )
    {
        rc = abfrage_frage( pv->vf, "Zu löschende Seiten enthalten Ziele",
                "Trotzdem löschen?", NULL );
        if ( rc != GTK_RESPONSE_YES )
        {
            g_ptr_array_free( arr_dests, TRUE );
            return 1;
        }
    }
#else
    //Überprüfen, ob NamedDest in db als ziel
    for ( gint i = 0; i < arr_dests->len; i++ )
    {
//        rc = zond_dbase_check_id( pv->zond->dbase_zond->zond_dbase_work, g_ptr_array_index( arr_dests, i ), errmsg );
        if ( rc == -1 )
        {
            g_ptr_array_free( arr_dests, TRUE );
            ERROR_S
        }
        if ( rc == 1 )
        {
            g_ptr_array_free( arr_dests, TRUE );
            return 1;
        }
    }
#endif // VIEWER

    g_ptr_array_free( arr_dests, TRUE );

    return 0;
}


static gint
seiten_loeschen( PdfViewer* pv, GPtrArray* arr_document_page, gchar** errmsg )
{
    GPtrArray* arr_docs = NULL;

    arr_docs = g_ptr_array_new( );
    for ( gint i = 0; i < arr_document_page->len; i++ )
    {
        PdfDocumentPage* pdf_document_page = NULL;
        gint rc = 0;

        pdf_document_page = g_ptr_array_index( arr_document_page, i );

        if ( !g_ptr_array_find( arr_docs, pdf_document_page->document, NULL ) )
                g_ptr_array_add( arr_docs, pdf_document_page->document );

        //macht - sofern noch nicht geschehen - thread_pool des pv dicht, in dem Seite angezeigt wird
        //Dann wird Seite aus pv gelöscht
        rc = viewer_foreach( pv, pdf_document_page, seiten_cb_loesche_seite, NULL,
                errmsg );
        if ( rc )
        {
            g_ptr_array_unref( arr_docs );
            ERROR_S
        }

        //Seite aus document entfernen
        g_ptr_array_remove( zond_pdf_document_get_arr_pages( pdf_document_page->document ),
                pdf_document_page );
    }

    //jetzt jedes document durchgehen
    for ( gint i = 0; i < arr_docs->len; i++ )
    {
        ZondPdfDocument* zond_pdf_document = NULL;
        gint* pages = NULL;
        GPtrArray* arr_pages = NULL;
        fz_context* ctx = NULL;

        zond_pdf_document = g_ptr_array_index( arr_docs, i );
        arr_pages = zond_pdf_document_get_arr_pages( zond_pdf_document );
        ctx = zond_pdf_document_get_ctx( zond_pdf_document );

        pages = g_malloc( sizeof( gint ) * arr_pages->len );
        for ( gint u = 0; u < arr_pages->len; u++ )
        {
            PdfDocumentPage* pdf_document_page = NULL;

            pdf_document_page = g_ptr_array_index( arr_pages, u );
            pages[u] = pdf_document_page->page_doc;

            zond_pdf_document_unload_page( pdf_document_page );
            pdf_document_page->page_doc = u;
        }

        fz_try( ctx ) pdf_rearrange_pages( ctx,
                zond_pdf_document_get_pdf_doc( zond_pdf_document ), arr_pages->len, pages );
        fz_always( ctx ) g_free( pages );
        fz_catch( ctx )
        {
            g_ptr_array_unref( arr_docs );
            ERROR_MUPDF( "pdf_rearrange_pages" )
        }
    }

    g_ptr_array_unref( arr_docs );

    seiten_refresh_layouts( pv->zond->arr_pv );

    gtk_tree_selection_unselect_all(
            gtk_tree_view_get_selection( GTK_TREE_VIEW(pv->tree_thumb) ) );

    return 0;
}


void
cb_pv_seiten_loeschen( GtkMenuItem* item, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    gchar* title = NULL;
    GPtrArray* arr_document_page = NULL;
    gint count = 0;

    PdfViewer* pv = (PdfViewer*) data;

/*
    //Seiten löschen nur in "ganzen" Pdfs
    if ( pv->dd->next != NULL || pv->dd->anbindung != NULL )
    {
        display_message( pv->vf, "Seiten aus Auszug löschen nicht möglich" , NULL );
        return;
    }
*/
    count = pv->arr_pages->len;

    //zu löschende Seiten holen
    title = g_strdup_printf( "Seiten löschen (1 - %i):", count );
    arr_document_page = seiten_abfrage_seiten( pv, title, NULL );
    g_free( title );

    if ( !arr_document_page ) return;

    //Abfrage, ob Anbindung mit Seite verknüpft
    rc = seiten_anbindung( pv, arr_document_page, &errmsg );
    if ( rc )
    {
        if ( rc == -1 )
        {
            display_message( pv->vf, "Fehler Seiten löschen - \n",
                    errmsg, NULL );
            g_free( errmsg );
        }
        else if ( rc == 1 ) display_message( pv->vf, "Seiten enthalten Anbindungen - \n"
                "Löschen nicht zulässig", NULL );

        g_ptr_array_unref( arr_document_page );

        return;
    }

    rc = seiten_loeschen( pv, arr_document_page, &errmsg );
    g_ptr_array_unref( arr_document_page );
    if ( rc == -1 )
    {
        display_message( pv->vf, "Fehler in Seiten löschen -\n\nBei Aufruf "
                "seiten_loeschen:\n", errmsg, "\n\nViewer wird geschlossen", NULL );
        g_free( errmsg );

        viewer_save_and_close( pv );

        return;
    }
#ifndef VIEWER
    else if ( rc == 1 ) display_message( pv->vf, "Fehler in Seiten löschen -\n\n"
            "Zu löschende Seiten enthalten Anbindungen", NULL );
#endif // VIEWER

    return;
}


/*
**      Seiten einfügen
*/
typedef struct _InfoInsert
{
    guint pos;
    gint count;
} InfoInsert;

static gint
seiten_cb_einfuegen( PdfViewer* pv, gint page_pv, gpointer data, gchar** errmsg )
{
    DisplayedDocument* dd = NULL;
    InfoInsert* info_insert = NULL;

    info_insert = (InfoInsert*) data;

    if ( pv->dd->next || pv->dd->anbindung )
    {
        gint page_dd = 0;

        if ( info_insert->pos == 0 ) return 0;
        if ( page_pv == 0 ) return 0; //ist ja am Rand (Anfang) von Anbindung

        dd = document_get_dd( pv, page_pv, NULL, &page_dd, NULL );
        if ( page_dd == 0 ) return 0; //auch am Anfang von Anbindung!
        //auf am Ende von Anbindung muß nicht geprüft werden, da
            //- page_pv - außer wenn pos == 0, schon ausgeschlossen - nach einzufügenden Seiten liegt
            //- also wenn page_pv am Rand ist eins davor nicht am Rand
        //wenn in Anbindung, dann Anbindung aufbiegen
        if ( dd->anbindung ) dd->anbindung->bis.seite += info_insert->count;
    }
    else dd = pv->dd;

    for ( gint u = 0; u < info_insert->count; u++ )
    {
        ViewerPageNew* viewer_page = NULL;
        GtkTreeIter iter_tmp;

        viewer_page = viewer_new_page( pv, dd->zond_pdf_document, info_insert->pos + u );

        g_ptr_array_insert( pv->arr_pages, page_pv + u, viewer_page );

        gtk_list_store_insert( GTK_LIST_STORE(gtk_tree_view_get_model(
                GTK_TREE_VIEW(pv->tree_thumb) )), &iter_tmp, page_pv + u );
    }

    g_object_set_data( G_OBJECT(pv->layout), "dirty", GINT_TO_POINTER(1) );

    return 0;
}


static void
cb_pv_seiten_entry( GtkEntry* entry, gpointer button_datei )
{
    gtk_widget_grab_focus( (GtkWidget*) button_datei );

    return;
}


static gint
seiten_abfrage_seitenzahl( PdfViewer*pv, guint* num )
{
    gint res = 0;
    gint rc = 0;

    // Dialog erzeugen
    GtkWidget* dialog = gtk_dialog_new_with_buttons( "Seiten einfügen:",
            GTK_WINDOW(pv->vf), GTK_DIALOG_MODAL, "Datei", 1, "Clipboard", 2,
            "Abbrechen", GTK_RESPONSE_CANCEL, NULL );

    GtkWidget* content_area = gtk_dialog_get_content_area( GTK_DIALOG(dialog) );

    GtkWidget* frame = gtk_frame_new( "nach Seite:" );
    GtkWidget* entry = gtk_entry_new( );
    gtk_container_add( GTK_CONTAINER(frame), entry );
    gtk_box_pack_start( GTK_BOX(content_area), frame, TRUE, TRUE, 0 );

    GtkWidget* button_clipboard =
            gtk_dialog_get_widget_for_response( GTK_DIALOG(dialog), 2 );
    if ( !pv->zond->pv_clip ) gtk_widget_set_sensitive( button_clipboard, FALSE );

    GtkWidget* button_datei =
            gtk_dialog_get_widget_for_response( GTK_DIALOG(dialog), 1 );
    g_signal_connect( entry, "activate", G_CALLBACK(cb_pv_seiten_entry),
            button_datei );

    gtk_widget_show_all( dialog );
    gtk_widget_grab_focus( entry );

    res = gtk_dialog_run( GTK_DIALOG(dialog) );
    rc = string_to_guint( gtk_entry_get_text( GTK_ENTRY(entry) ), num );
    if ( rc ) res = -1;

    gtk_widget_destroy( dialog );

    if ( res != 1 && res != 2 ) return -1;

    return res;
}


void
cb_pv_seiten_einfuegen( GtkMenuItem* item, gpointer data )
{
    PdfViewer* pv = (PdfViewer*) data;
    DisplayedDocument* dd = NULL;
    gint page_doc = 0;
    gint ret = 0;
    gint rc = 0;
    guint pos = 0;
    pdf_document* doc_merge = NULL;
    gint count = 0;
    gchar* errmsg = NULL;
    InfoInsert info_insert = { 0 };

    ret = seiten_abfrage_seitenzahl( pv, &pos );
    if ( ret == -1 ) return;

    if ( pos > pv->arr_pages->len ) pos = pv->arr_pages->len;
    if ( pos < 0 ) pos = 0;

    //Falls Auszug: nicht zwischen "Grenzen" des dd einfügen - doppeldeutig
    if ( pv->dd->next != NULL || pv->dd->anbindung != NULL )
    {
        gint page_dd = 0;
        gint dd_len = 0;

        dd = document_get_dd( pv, pos - 1, NULL, &page_dd, &page_doc );
        dd_len = document_get_num_of_pages_of_dd( dd );

        if ( page_dd == 0 && page_dd == dd_len - 1 )
        {
            display_message( pv->vf, "In Auszug darf nicht an den Rändern der "
                    "Abschnitte eingefügt werden", NULL );

            return;
        }

        pos = page_doc + 1;
    }
    else dd = pv->dd;

    //komplette Datei wird eingefügt
    if ( ret == 1 )
    {
        gchar* path_merge = NULL;

        //Datei auswählen
        path_merge = filename_oeffnen( GTK_WINDOW(pv->vf) );
        if ( !is_pdf( path_merge ) )
        {
            display_message( pv->vf, "Keine PDF-Datei", NULL );
            g_free( path_merge );

            return;
        }

        fz_try( pv->zond->ctx ) doc_merge = pdf_open_document( pv->zond->ctx, path_merge );
        fz_always( pv->zond->ctx ) g_free( path_merge );
        fz_catch( pv->zond->ctx )
        {
            display_message( pv->vf, "Fehler Datei einfügen -\n\nBei Aufruf "
                    "pdf_open_document:\n", fz_caught_message( pv->zond->ctx ), NULL );

            return;
        }
    }
    else if ( ret == 2 ) doc_merge = pdf_keep_document( pv->zond->ctx, pv->zond->pv_clip ); //Clipboard

    count = pdf_count_pages( pv->zond->ctx, doc_merge );

    rc = zond_pdf_document_insert_pages( dd->zond_pdf_document, pos,
            pv->zond->ctx, doc_merge, &errmsg );
    pdf_drop_document(pv->zond->ctx, doc_merge );
    if ( rc )
    {
        display_message( pv->vf, "Fehler Einfügen\n\n", errmsg,
                "\n\nViewer wird geschlossen", NULL );
        g_free( errmsg );
        viewer_save_and_close( pv );

        return;
    }

    info_insert.pos = pos;
    info_insert.count = count;

    //betroffene viewer-seiten einfügen
    rc = viewer_foreach( pv, zond_pdf_document_get_pdf_document_page(
            pv->dd->zond_pdf_document, (pos) ? pos - 1 : pos + count ), seiten_cb_einfuegen,
            &info_insert, &errmsg );
    if ( rc )
    {
        display_message( pv->vf, "Fehler Einfügen -\n\n", errmsg,
                "\n\nViewer wird geschlossen", NULL );
        g_free( errmsg );
        viewer_save_and_close( pv );

        return;
    }

    seiten_refresh_layouts( pv->zond->arr_pv );

    return;
}


static pdf_document*
seiten_create_document( PdfViewer* pv, GArray* arr_page_pv, gchar** errmsg )
{
    pdf_document* doc_dest = NULL;
    gint rc = 0;

    fz_try( pv->zond->ctx ) doc_dest = pdf_create_document( pv->zond->ctx );
    fz_catch( pv->zond->ctx )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf pdf_create_document:\n",
                fz_caught_message( pv->zond->ctx ), NULL );

        return NULL;
    }

    for ( gint i = 0; i < arr_page_pv->len; i++ )
    {
        ViewerPageNew* viewer_page = NULL;
        PdfDocumentPage* pdf_document_page = NULL;

        gint page_pv = g_array_index( arr_page_pv, gint, i );
        viewer_page = g_ptr_array_index( pv->arr_pages, page_pv );
        pdf_document_page = viewer_page->pdf_document_page;

        zond_pdf_document_mutex_lock( pdf_document_page->document );
        rc = pdf_copy_page( pv->zond->ctx,
                zond_pdf_document_get_pdf_doc( pdf_document_page->document ),
                pdf_document_page->page_doc, pdf_document_page->page_doc, doc_dest, -1, errmsg );
        zond_pdf_document_mutex_unlock( pdf_document_page->document );
        if ( rc )
        {
            pdf_drop_document( pv->zond->ctx, doc_dest );

            ERROR_S_VAL( NULL )
        }
    }

    return doc_dest;
}


static gint
seiten_set_clipboard( PdfViewer* pv, GArray* arr_page_pv, gchar** errmsg )
{
    pdf_drop_document( pv->zond->ctx, pv->zond->pv_clip );
    pv->zond->pv_clip = NULL;

    pv->zond->pv_clip = seiten_create_document( pv, arr_page_pv, errmsg );
    if ( !pv->zond->pv_clip ) ERROR_S

    return 0;
}


void
cb_seiten_kopieren( GtkMenuItem* item, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    PdfViewer* pv = (PdfViewer*) data;

    GArray* arr_page_pv = seiten_markierte_thumbs( pv );
    if ( !arr_page_pv ) return;

    rc = seiten_set_clipboard( pv, arr_page_pv, &errmsg );
    if ( rc )
    {
        display_message( pv->vf, "Fehler Kopieren -\n\nBei Aufruf seiten_set_clipboard:\n",
                errmsg, NULL );
        g_free( errmsg );
    }

    g_array_unref( arr_page_pv );

    return;
}


void
cb_seiten_ausschneiden( GtkMenuItem* item, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    PdfViewer* pv = (PdfViewer*) data;

    //Nur aus ganzen PDFs ausschneiden
    if ( pv->dd->next != NULL || pv->dd->anbindung != NULL ) return;

    GArray* arr_page_pv = seiten_markierte_thumbs( pv );
    if ( !arr_page_pv ) return;

    rc = seiten_set_clipboard( pv, arr_page_pv, &errmsg );
    if ( rc )
    {
        display_message( pv->vf, "Fehler Kopieren -\n\nBei Aufruf seiten_set_clipboard:\n",
                errmsg, NULL );
        g_free( errmsg );
        g_array_unref( arr_page_pv );

        return;
    }

    GPtrArray* arr_document_page = seiten_get_document_pages( pv, arr_page_pv );
    g_array_unref( arr_page_pv );
    rc = seiten_loeschen( pv, arr_document_page, &errmsg );
    g_ptr_array_unref( arr_document_page );
    if ( rc == -1 )
    {
        display_message( pv->vf, "Fehler Ausschneiden -\n\nBei Aufruf seiten_loeschen:\n",
                errmsg, NULL );
        g_free( errmsg );
    }
    else if ( rc == 1 ) display_message( pv->vf, "Fehler Ausschneiden -\n\nSeiten enthalten Anbindungen\n",
            NULL );

    return;
}
