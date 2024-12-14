/*
zond (viewer.c) - Akten, Beweisstücke, Unterlagen
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
#include <glib/gstdio.h>

#include "../zond_pdf_document.h"

#include "../global_types.h"

#include "../99conv/general.h"
#include "../99conv/pdf.h"

#include "../20allgemein/ziele.h"

#include "document.h"
#include "render.h"
#include "stand_alone.h"
#include "seiten.h"
#include "viewer.h"

#include "../../misc.h"



void
viewer_springen_zu_pos_pdf( PdfViewer* pv, PdfPos pdf_pos, gdouble delta )
{
    gdouble value = 0.0;
    ViewerPageNew* viewer_page = NULL;

    if ( pdf_pos.seite > pv->arr_pages->len ) pdf_pos.seite = pv->arr_pages->len - 1;

    viewer_page = g_ptr_array_index( pv->arr_pages, pdf_pos.seite );

    //Länge aktueller Seite ermitteln
    gdouble page = viewer_page->crop.y1 - viewer_page->crop.y0;
    if ( pdf_pos.index <= page ) value = viewer_page->y_pos + pdf_pos.index * pv->zoom / 100;
    else value = viewer_page->y_pos + page * pv->zoom / 100;

//    value = value * pv->zoom / 100;
#ifndef VIEWER
    if ( pv->zond->state & GDK_MOD1_MASK )
    {
        gdouble page_size = gtk_adjustment_get_page_size( pv->v_adj );
        value -= page_size;
    }
#endif // VIEWER
    gtk_adjustment_set_value( pv->v_adj, (value > delta) ? value - delta : 0 );

    return;
}


static void
viewer_abfragen_sichtbare_seiten( PdfViewer* pv, gint* von, gint* bis )
{
    gdouble value = gtk_adjustment_get_value( pv->v_adj );
    gdouble size = gtk_adjustment_get_page_size( pv->v_adj );

    *von = -1;
    *bis = -1;

    gdouble v_oben = 0.0;
    gdouble v_unten = -10.0;

    while ( ((value + size) > v_oben) && ((*bis) < ((gint) pv->arr_pages->len - 1)) )
    {
        ViewerPageNew* viewer_page = NULL;

        (*bis)++;
        viewer_page = g_ptr_array_index( pv->arr_pages, *bis );

        v_oben += ((viewer_page->crop.y1 -viewer_page->crop.y0) * pv->zoom / 100) + 10;

        if ( value > v_unten ) (*von)++;
        v_unten += ((viewer_page->crop.y1 - viewer_page->crop.y0) * pv->zoom / 100) + 10;
    }

    return;
}


gint
viewer_get_iter_thumb( PdfViewer* pv, gint page_pv, GtkTreeIter* iter )
{
    GtkTreeModel* model = NULL;
    GtkTreePath* path = NULL;
    gboolean exists = FALSE;

    path = gtk_tree_path_new_from_indices( page_pv, -1 );
    model = gtk_tree_view_get_model( GTK_TREE_VIEW(pv->tree_thumb) );
    exists = gtk_tree_model_get_iter( model, iter, path );
    gtk_tree_path_free( path );
    if ( !exists ) return -1;

    return 0;
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
viewer_draw_image_page( GtkWidget* image, cairo_t* cr, gpointer data )
{
    fz_matrix transform = { 0, };

    ViewerPageNew* viewer_page = (ViewerPageNew*) data;

    transform = fz_translate( 0.0, -viewer_page->crop.y0 );
    transform = fz_post_scale( transform, viewer_page->pdfv->zoom / 100, viewer_page->pdfv->zoom / 100 );

    //wenn annot angeclickt wurde
    if ( viewer_page->pdfv->clicked_annot &&
            viewer_page->pdfv->clicked_annot->pdf_document_page == viewer_page->pdf_document_page)
    {
        if ( viewer_page->pdfv->clicked_annot->type == PDF_ANNOT_HIGHLIGHT ||
                viewer_page->pdfv->clicked_annot->type == PDF_ANNOT_UNDERLINE )
        {
            GArray* arr_quads = viewer_page->pdfv->clicked_annot->annot_text_markup.arr_quads;

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
        else if ( viewer_page->pdfv->clicked_annot->type == PDF_ANNOT_TEXT )
        {
            fz_rect rect = { 0, };

            rect = fz_transform_rect( viewer_page->pdfv->clicked_annot->annot_text.rect, transform );

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
        while ( viewer_page->pdfv->highlight.page[i] >= 0 )
        {
            if ( viewer_page == g_ptr_array_index( viewer_page->pdfv->arr_pages,
                    viewer_page->pdfv->highlight.page[i] ) )
                    viewer_page_mark_quad( cr, viewer_page->pdfv->highlight.quad[i], transform );

            i++;
        }
    }

    return FALSE;
}


void
viewer_transfer_rendered( PdfViewer* pdfv, gboolean protect )
{
    gint idx = 0;

    if ( protect ) g_mutex_lock( &pdfv->mutex_arr_rendered );

    if ( (idx = pdfv->arr_rendered->len - 1) < 0 )
    {
        if ( protect ) g_mutex_unlock( &pdfv->mutex_arr_rendered );

        return;
    }

    while( idx >= 0 )
    {
        RenderResponse render_response = { 0 };
        ViewerPageNew* viewer_page = NULL;

        render_response = g_array_index( pdfv->arr_rendered, RenderResponse, idx );

        viewer_page = g_ptr_array_index( pdfv->arr_pages, render_response.page );

        //thread von viewer_page und pdf_document_page fertig: bit 1 löschen
        viewer_page->thread &= 6;
        viewer_page->pdf_document_page->thread &= 14;
        viewer_page->pdf_document_page->thread_pv = NULL;

        //Beim rendern ein Fehler aufgetreten
        if ( render_response.error ) display_message( pdfv->vf,
                "Fehler beim Rendern\n\n", render_response.error_message, NULL );

        if ( render_response.error == 0 || render_response.error > 2 )
                viewer_page->pdf_document_page->thread |= 2;

        if ( render_response.error == 0 || render_response.error > 3 )
                viewer_page->pdf_document_page->thread |= 4;

        if ( render_response.error == 0 || render_response.error > 4 )
                viewer_page->pdf_document_page->thread |= 8;

        if ( !(viewer_page->thread & 2) && (render_response.error == 0 || render_response.error > 5) )
        {
            gint x_pos = 0;
            gint width = 0;

            //Kein mutex erforderlich, weil nur Zugriff, wenn schon gerendert
            //Dann ist aber ausgeschlossen, daß ein thread auf viewer_page->pixbuf_page zugreift
            viewer_page->image_page = gtk_image_new_from_pixbuf( viewer_page->pixbuf_page );
            gtk_widget_show( viewer_page->image_page );
            g_signal_connect_after( viewer_page->image_page, "draw", G_CALLBACK(viewer_draw_image_page), viewer_page );

            gtk_widget_get_size_request( pdfv->layout, &width, NULL );
            x_pos = (width - (viewer_page->crop.x1 - viewer_page->crop.x0) * pdfv->zoom / 100) / 2;

            gtk_layout_put( GTK_LAYOUT(pdfv->layout), GTK_WIDGET(viewer_page->image_page), x_pos, viewer_page->y_pos );
            g_object_unref( viewer_page->pixbuf_page );

            viewer_page->thread |= 2; //bit 2: image gerendert
        }

        if ( !(viewer_page->thread & 4) && render_response.error == 0 )
        {
            //tree_thumb
            GtkTreeIter iter;
            gint rc = viewer_get_iter_thumb( pdfv, render_response.page, &iter );
            if ( rc == 0 ) gtk_list_store_set( GTK_LIST_STORE( gtk_tree_view_get_model(
                    GTK_TREE_VIEW(pdfv->tree_thumb) ) ), &iter, 0, viewer_page->pixbuf_thumb, -1 );
            g_object_unref( viewer_page->pixbuf_thumb );

            viewer_page->thread |= 4; //bit 2: thumb gerendert
        }

        g_array_remove_index_fast( pdfv->arr_rendered, idx );
        idx--;

        pdfv->count_active_thread--;
    }

    if ( protect ) g_mutex_unlock( &pdfv->mutex_arr_rendered );

    return;
}


static gboolean
viewer_check_rendering( gpointer data )
{
    gboolean protect = FALSE;

    PdfViewer* pv = (PdfViewer*) data;

    if ( pv->thread_pool_page && g_thread_pool_unprocessed( pv->thread_pool_page ) != 0 )
            protect = TRUE;
    viewer_transfer_rendered( pv, protect );

    if ( pv->count_active_thread == 0 )
    {
        pv->idle_source = 0;
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}


static void
viewer_thread_render( PdfViewer* pv, gint page )
{
    ViewerPageNew* viewer_page = NULL;
    gint thread_data = 0;
    GError* error = NULL;

    viewer_page = g_ptr_array_index( pv->arr_pages, page );

    if ( viewer_page->thread == 6 && viewer_page->pdf_document_page->thread == 14 )
            return; //alles fertig, nix zu tun

    if ( viewer_page->thread & 1 ) return; //Seite läuft schon

    if ( viewer_page->pdf_document_page->thread & 1 ) //pdf_document_page wird gerade in anderem viewer gerendert
    {
        if ( viewer_page->thread == 6 ) return; //im viewer alles gerendert, pdf_document_page läuft-> nix zu tun
        else while ( viewer_page->pdf_document_page->thread & 1 )
                viewer_transfer_rendered( (PdfViewer*) viewer_page->pdf_document_page->thread_pv, TRUE );
    }

    if ( !pv->idle_source ) pv->idle_source = g_idle_add( G_SOURCE_FUNC(viewer_check_rendering), pv );

    if ( !pv->thread_pool_page )
    {
        GError* error = NULL;

        if ( !(pv->thread_pool_page =
                g_thread_pool_new( (GFunc) render_page_thread, pv, 3, FALSE, &error )) )
        {
            display_message( pv->vf, "Thread-Pool kann nicht erzeugt werden\n\n"
                    "Bei Aufruf g_thread_pool_new:\n", error->message, NULL );
            g_error_free( error );

            return;
        }
    }

    if ( !(viewer_page->pdf_document_page->thread & 2) ) thread_data += 1;
    if ( !(viewer_page->pdf_document_page->thread & 4) ) thread_data += 2;
    if ( !(viewer_page->pdf_document_page->thread & 8) ) thread_data += 4;
    if ( !(viewer_page->thread & 2) ) thread_data += 8;
    if ( !(viewer_page->thread & 4) ) thread_data += 16;

    thread_data += page << 5;

    if ( !g_thread_pool_push( pv->thread_pool_page, GINT_TO_POINTER(thread_data), &error ) )
    {
        display_message( pv->vf, "Fehler Start Thread\n\n"
                "Bei Aufruf g_thread_pool_push:\n", error->message, NULL );
        g_error_free( error );

        return;
    }
    g_thread_pool_move_to_front( pv->thread_pool_page, GINT_TO_POINTER(thread_data) );
    pv->count_active_thread++;

    viewer_page->thread |= 1; //bit 1: thread gestartet
    viewer_page->pdf_document_page->thread |= 1;
    viewer_page->pdf_document_page->thread_pv = (gpointer) pv;

    return;
}


static void
viewer_render_sichtbare_seiten( PdfViewer* pv )
{
    GtkTreePath* path = NULL;
    ViewerPageNew* viewer_page = NULL;
    const gchar* path_doc = NULL;
    const gchar* file = NULL;
    gchar* dir = NULL;
    gint erste = 0;
    gint letzte = 0;
    gchar* title = NULL;

    if ( pv->arr_pages->len == 0 ) return;

    viewer_abfragen_sichtbare_seiten( pv, &erste, &letzte );

    viewer_page = g_ptr_array_index( pv->arr_pages, erste );

    //in Headerbar angezeigte Datei und Seite anzeigen
    path_doc = zond_pdf_document_get_file_part( viewer_page->pdf_document_page->document );
    file = g_strrstr( path_doc, "/" );
    if ( file ) file++; // "/" entfernen
    else file = path_doc;
    dir = g_strndup( path_doc, strlen( path_doc ) - strlen( file ) );

    title = g_strdup_printf( "%s [Seite %i]", file,
            viewer_page->pdf_document_page->page_doc + 1 );
    gtk_header_bar_set_title( GTK_HEADER_BAR(pv->headerbar), title );
    g_free( title );

    gtk_header_bar_set_subtitle( GTK_HEADER_BAR(pv->headerbar), dir );
    g_free( dir );

    //Seite von oberen - unterem Rand im entry anzeigen
    gchar* text = g_strdup_printf( "%i-%i", erste + 1, letzte + 1 );
    gtk_entry_set_text( GTK_ENTRY(pv->entry), text );
    g_free( text );

    //rendern in Auftrag
    for ( gint i = letzte; i >= erste; i-- ) viewer_thread_render( pv, i );

    //thumb-Leiste anpassen
    path = gtk_tree_path_new_from_indices( erste, -1 );
    gtk_tree_view_scroll_to_cell( GTK_TREE_VIEW(pv->tree_thumb), path, NULL, FALSE, 0, 0 );
    gtk_tree_path_free( path );

    return;
}


void
viewer_refresh_layout( PdfViewer* pv, gint pos )
{
    ViewerPageNew* viewer_page = NULL;
    gdouble y_pos = 0;
    gdouble x_max = 0;

    if ( pv->arr_pages->len == 0 ) return;

    if ( pos > 0 )
    {
        viewer_page = g_ptr_array_index( pv->arr_pages, pos - 1 );

        y_pos = viewer_page->y_pos + (viewer_page->crop.y1 - viewer_page->crop.y0) * pv->zoom / 100 + PAGE_SPACE;
    }

    for ( gint u = pos; u < pv->arr_pages->len; u++ )
    {
        viewer_page = g_ptr_array_index( pv->arr_pages, u );
        if ( (viewer_page->crop.x1 - viewer_page->crop.x0) > x_max )
                x_max = viewer_page->crop.x1 - viewer_page->crop.x0;

        viewer_page->y_pos = (gint) (y_pos + .5);

        y_pos += (viewer_page->crop.y1 - viewer_page->crop.y0) *
                pv->zoom / 100 + PAGE_SPACE;
    }

    x_max = x_max * pv->zoom / 100;

    y_pos -= PAGE_SPACE;

    gtk_layout_set_size( GTK_LAYOUT(pv->layout), (gint) (x_max + .5), (gint) (y_pos + .5) );

    //label mit Gesamtseitenzahl erzeugen
    gchar* text = g_strdup_printf( "/ %i ", pv->arr_pages->len );
    gtk_label_set_text( GTK_LABEL(pv->label_anzahl), text );
    g_free( text );

    gtk_widget_set_size_request( pv->layout, (gint) (x_max+ .5), (gint) (y_pos + .5) );

    gtk_adjustment_set_value( pv->h_adj, (x_max - VIEWER_WIDTH) / 2 );

    for ( gint u = pos; u < pv->arr_pages->len; u++ )
    {
        ViewerPageNew* viewer_page = NULL;

        viewer_page = g_ptr_array_index( pv->arr_pages, u );
        if ( viewer_page->image_page )
                gtk_layout_move( GTK_LAYOUT(pv->layout), viewer_page->image_page,
                (gint) (x_max - (viewer_page->crop.x1 - viewer_page->crop.x0) * pv->zoom / 100) / 2, viewer_page->y_pos );
    }

    return;
}


ViewerPageNew*
viewer_new_page( PdfViewer* pdfv, ZondPdfDocument* zond_pdf_document, gint page_doc )
{
    ViewerPageNew* viewer_page = g_malloc0( sizeof( ViewerPageNew ) );

    viewer_page->pdfv = pdfv;
    viewer_page->pdf_document_page =
            zond_pdf_document_get_pdf_document_page( zond_pdf_document, page_doc );

    viewer_page->crop = viewer_page->pdf_document_page->rect;

    return viewer_page;
}


static void
viewer_create_layout( PdfViewer* pv )
{
    DisplayedDocument* dd = pv->dd;
    gdouble x_max = 0;
    gdouble y_pos = 0;

    do
    {
        gint von = 0;
        gint bis = 0;

        if ( dd->anbindung )
        {
            von = dd->anbindung->von.seite;
            bis = dd->anbindung->bis.seite;
        }
        else bis = zond_pdf_document_get_number_of_pages( dd->zond_pdf_document ) - 1;

        g_ptr_array_set_size( pv->arr_pages, bis - von + 1 + pv->arr_pages->len );

        for ( gint i = von; i <= bis; i++ )
        {
            ViewerPageNew* viewer_page = NULL;
            GtkTreeIter iter_tmp;

            viewer_page = viewer_new_page( pv, dd->zond_pdf_document, i );

            viewer_page->y_pos = (gint) (y_pos + .5);
            if ( dd->anbindung )
            {
                viewer_page->crop.y0 = (i == dd->anbindung->von.seite) ?
                        (gfloat) dd->anbindung->von.index : viewer_page->crop.y0;
                viewer_page->crop.y1 = ((i == dd->anbindung->bis.seite) &&
                        (dd->anbindung->bis.index < EOP)) ?
                        (gfloat) dd->anbindung->bis.index : viewer_page->crop.y1;
            }

            ((pv->arr_pages)->pdata)[pv->arr_pages->len - 1 - (bis - i)] = viewer_page;

            gtk_list_store_insert( GTK_LIST_STORE(gtk_tree_view_get_model(
                    GTK_TREE_VIEW(pv->tree_thumb) )), &iter_tmp, -1 );

            y_pos += ((viewer_page->crop.y1 - viewer_page->crop.y0) * pv->zoom / 100) + PAGE_SPACE;
            if ( (viewer_page->crop.x1 - viewer_page->crop.x0) > x_max )
                    x_max = viewer_page->crop.x1 - viewer_page->crop.x0;
        }

    } while ( (dd = dd->next) );

    x_max = x_max * pv->zoom / 100;

    y_pos -= PAGE_SPACE;

    gtk_layout_set_size( GTK_LAYOUT(pv->layout), (gint) (x_max + .5), (gint) (y_pos + .5 ) );

    //label mit Gesamtseitenzahl erzeugen
    gchar* text = g_strdup_printf( "/ %i ", pv->arr_pages->len );
    gtk_label_set_text( GTK_LABEL(pv->label_anzahl), text );
    g_free( text );

    gtk_widget_set_size_request( pv->layout, (gint) (x_max + 0.5), (gint) (y_pos + 0.5) );

    gtk_adjustment_set_value( pv->h_adj, (x_max - VIEWER_WIDTH) / 2);

    return;
}


void
viewer_display_document( PdfViewer* pv, DisplayedDocument* dd, gint page, gint index )
{
    PdfPos pdf_pos = { page, index };

    pv->dd = dd;

    viewer_create_layout( pv );

    if ( page || index ) viewer_springen_zu_pos_pdf( pv, pdf_pos, 0.0 );
    else g_signal_emit_by_name( pv->v_adj, "value-changed", NULL ); // falls pos == 0

    gtk_widget_grab_focus( pv->layout );

    return;
}


void
viewer_close_thread_pool_and_transfer( PdfViewer* pdfv )
{
    if ( pdfv->thread_pool_page )
    {
        g_thread_pool_free( pdfv->thread_pool_page, TRUE, TRUE );
        pdfv->thread_pool_page = NULL;
    }

    viewer_transfer_rendered( pdfv, FALSE );

    return;
}

static void
viewer_free_render_response( gpointer data )
{
    RenderResponse* render_response = (RenderResponse*) data;

    g_free( render_response->error_message );

    return;
}

static void
viewer_schliessen( PdfViewer* pv )
{
    viewer_close_thread_pool_and_transfer( pv ); //..._and_transfer, damit etwaig noch gerenderte GdkPixbufs verarztet werden
    g_idle_remove_by_data( pv );

    g_array_unref( pv->arr_rendered );
    g_mutex_clear( &pv->mutex_arr_rendered );

    g_ptr_array_unref( pv->arr_pages ); //vor gtk_widget_destroy(vf), weil freeFunc gesetzt Nein! stimmt nicht! Keine free-func
    g_array_unref( pv->text_occ.arr_quad );

    gtk_widget_destroy( pv->vf );

    document_free_displayed_documents( pv->dd );

    //pv aus Liste der geöffneten pvs entfernen
    g_ptr_array_remove_fast( pv->zond->arr_pv, pv );

    g_free( pv );

    return;
}


static gboolean
viewer_entry_in_anbindung( JournalEntry entry, Anbindung* anbindung )
{
    gboolean ret = TRUE;

    if ( !anbindung ) return ret; //ganzes Dokument

    if ( entry.type == JOURNAL_TYPE_PAGES_INSERTED )
    {
        if ( entry.PagesInserted.pos < anbindung->von.seite ||
                entry.PagesInserted.pos > anbindung->von.seite ) ret = FALSE;
        else if ( entry.PagesInserted.pos == anbindung->von.seite ||
                entry.PagesInserted.pos == anbindung->bis.seite )
        {
            if ( !entry.PagesInserted.anbindung ) ret = FALSE;
            else if ( )
        }

    }
    else if ( entry.tyoe == JOURNAL_TYPE_PAGES_DELETED )
    {

    }
}


static gint
viewer_save_dirty_docs( PdfViewer* pdfv, gboolean ask )
{
    DisplayedDocument* dd = NULL;
    GError* error = NULL;

    dd = pdfv->dd;
    if ( !dd ) return 0;

    //Alle schmutzigen Dds speichern
    do
    {
        gint rc = 0;
        GArray* arr_journal = NULL;
        GArray* arr_anb_work = NULL;
        GArray* arr_anb_store = NULL;
        GArray* arr_redo = NULL;

        arr_journal = zond_pdf_document_get_arr_journal( dd->zond_pdf_document );

        if ( arr_journal->len == 0 ) continue; //nix geändert



        for ( gint i = arr_journal->len -1; i >= 0; i-- )
        {
            JournalEntry entry = { 0 };

            entry = g_array_index( arr_journal, JournalEntry, i );

            if ( !viewer_entry_in_anbindung( entry, dd->anbindung )
            {
                /*
                g_array_append_val( arr_redo, entry_actus_contrarius )
                undo( entry )
                */
            }
            else
            {
                if ( (entry ist TYPE_PAGES_INSERTED || _DELETED) && entry betrifft dd->anbindung)
                {
                    //lese, falls noch nicht geschehen, alle Kinder von root(file_part) ein, wenn sie nicht > dd->anbindung sind

                    //passe diese in Datenbank an
                }

                g_array_index_remove( arr_journal, i );
            }
            */
        }

        rc = document_save_dd( dd, ask, &error );
        if ( rc )
        {
            gchar* error_text = NULL;

            error_text = g_strdup_printf( "Fehler bei Speichern der Datei '%s'",
                    zond_pdf_document_get_file_part( dd->zond_pdf_document ) );
            display_error( pdfv->vf, error_text, error->message );
            g_free( error_text );
            g_error_free( error );

            continue;
        }
    } while ( (dd = dd->next) );

    //Bei allen sauberen pvs Speichern insensitiv
    for ( gint i = 0; i < pdfv->zond->arr_pv->len; i++ )
    {
        PdfViewer* pdfv_test = NULL;
        DisplayedDocument* dd_test = NULL;
        gboolean dirty = FALSE;

        pdfv_test = g_ptr_array_index( pdfv->zond->arr_pv, i );

        dd_test = pdfv_test->dd;
        do
        {
            ZondPdfDocument* zpdf = NULL;
            GArray* arr_journal = NULL;

            zpdf = dd_test->zond_pdf_document;
            arr_journal = zond_pdf_document_get_arr_journal( zpdf );
            if ( arr_journal->len > 0 ) dirty = TRUE;
            //ToDo: Prüfen, ob entries im journal auch dieses dd betreffen
        }
        while ( (dd_test = dd_test->next) );

        if ( dirty == FALSE ) gtk_widget_set_sensitive( pdfv_test->button_speichern, FALSE );
    }

    return 0;
}


void
viewer_save_and_close( PdfViewer* pdfv )
{
    gint rc = 0;

    gtk_popover_popdown( GTK_POPOVER(pdfv->annot_pop_edit) );

    rc = viewer_save_dirty_docs( pdfv, TRUE );
    if ( rc ) return;

    viewer_schliessen( pdfv );

    return;
}


static void
cb_thumb_sel_changed( GtkTreeSelection* sel, gpointer data )
{
    gboolean active = FALSE;

    PdfViewer* pv = (PdfViewer*) data;

    active = (gtk_tree_selection_count_selected_rows( sel ) == 0 ) ? FALSE : TRUE;

    gtk_widget_set_sensitive( pv->item_kopieren, active );
    gtk_widget_set_sensitive( pv->item_ausschneiden, active );

    return;
}


static void
cb_thumb_activated(GtkTreeView* tv, GtkTreePath* path, GtkTreeViewColumn* column,
        gpointer data )
{
    gint* indices = NULL;
    PdfPos pos = { 0 };

    PdfViewer* pv = (PdfViewer*) data;

    indices = gtk_tree_path_get_indices( path );

    pos.seite = indices[0];
    pos.index = 0;
    viewer_springen_zu_pos_pdf( pv, pos, 0.0 );

    return;
}


#ifndef VIEWER
static void
cb_viewer_loeschen_anbindung_button_clicked( GtkButton* button, gpointer data )
{
   PdfViewer* pv = (PdfViewer*) data;

    //anbindung.von "löschen"
    pv->anbindung.von.index = -1;

    //Anzeige Beginn rückgängig machen
    gtk_widget_set_tooltip_text( pv->button_anbindung, "Anbindung Anfang löschen" );
    gtk_widget_set_sensitive( pv->button_anbindung, FALSE );

    return;
}
#endif // VIEWER


static void
cb_viewer_auswahlwerkzeug( GtkButton* button, gpointer data )
{
    gint button_ID = 0;

    PdfViewer* pv = (PdfViewer*) data;

    button_ID = GPOINTER_TO_INT(g_object_get_data( G_OBJECT(button), "ID" ));

    pv->state = button_ID;

    return;
}


static void
cb_pv_speichern( GtkButton* button, gpointer data )
{
    PdfViewer* pv = (PdfViewer*) data;

    viewer_save_dirty_docs( pv, FALSE );

    return;
}


static gint
viewer_get_visible_thumbs( PdfViewer* pv, gint* start, gint* end )
{
    GtkTreePath* path_start = NULL;
    GtkTreePath* path_end = NULL;
    gint* index_start = NULL;
    gint* index_end = NULL;

    if ( pv->arr_pages->len == 0 ) return 1;

    if ( !gtk_tree_view_get_visible_range( GTK_TREE_VIEW(pv->tree_thumb),
            &path_start, &path_end ) ) return 1;

    index_start = gtk_tree_path_get_indices( path_start );
    index_end = gtk_tree_path_get_indices( path_end );

    *start = index_start[0];
    *end = index_end[0];

    gtk_tree_path_free( path_start );
    gtk_tree_path_free( path_end );

    return 0;
}


static void
viewer_render_sichtbare_thumbs( PdfViewer* pv )
{
    gint rc = 0;
    gint start = 0;
    gint end = 0;

    if ( !gtk_widget_get_visible( pv->swindow_tree ) ) return;

    rc = viewer_get_visible_thumbs( pv, &start, &end );
    if ( rc == 0 ) for ( gint i = start; i <= end; i++ ) viewer_thread_render( pv, i );

    return;
}


static void
cb_tree_thumb( GtkToggleButton* button, gpointer data )
{
    PdfViewer* pdfv= (PdfViewer*) data;

    if ( gtk_toggle_button_get_active( button ) )
    {
        gtk_widget_show( pdfv->swindow_tree );
        viewer_render_sichtbare_thumbs( pdfv );
    }
    else
    {
        gtk_widget_hide( pdfv->swindow_tree );
        GtkTreeSelection* sel = gtk_tree_view_get_selection(
                GTK_TREE_VIEW(pdfv->tree_thumb) );
        gtk_tree_selection_unselect_all( sel );
    }

    return;
}


static void
cb_viewer_text_search_entry_buffer_changed( gpointer data )
{
    PdfViewer* pv = (PdfViewer*) data;

//    pv->highlight.page[0] = -1;

    g_array_remove_range( pv->text_occ.arr_quad, 0, pv->text_occ.arr_quad->len );
    pv->text_occ.not_found = FALSE;
    pv->text_occ.index_act = -1;
    pv->text_occ.page_act = -1;

    return;
}


/*  punkt:      Koordinate im Layout (ScrolledWindow)
    pdf_punkt:  hier wird Ergebnis abgelegt
    gint:       0 wenn Punkt auf Seite liegt; -1 wenn außerhalb

    Wenn Punkt im Zwischenraum zwischen zwei Seiten oder unterhalb der letzten
    Seite liegt, wird pdf_punkt.seite die davorliegende Seite und
    pdf_punkt.punkt.y = EOP.
    Wenn punkt links oder rechts daneben liegt, ist pdf_punkt.punkt.x negativ
    oder größer als Seitenbreite
*/
static gint
viewer_abfragen_pdf_punkt( PdfViewer* pv, fz_point punkt, PdfPunkt* pdf_punkt )
{
    gint ret = 0;
    gdouble v_oben = 0.0;
    gdouble v_unten = 0.0;
    ViewerPageNew* viewer_page = NULL;
    gint width = 0;
    gint x = 0;

    gint i = 0;

    if ( punkt.y < 0 )
    {
        pdf_punkt->seite = 0;
        pdf_punkt->punkt.y = 0;
        ret = -1;

        viewer_page = g_ptr_array_index( pv->arr_pages, 0 );
    }
    else
    {
        for ( i = 0; i < pv->arr_pages->len; i++ )
        {
            viewer_page = g_ptr_array_index( pv->arr_pages, i );

            pdf_punkt->delta_y = viewer_page->crop.y0;

            v_unten = v_oben +
                    (viewer_page->crop.y1 - viewer_page->crop.y0) * pv->zoom / 100;

            if ( punkt.y >= v_oben && punkt.y <= v_unten )
            {
                pdf_punkt->seite = i;
                pdf_punkt->punkt.y = (punkt.y - v_oben) / pv->zoom * 100 + viewer_page->crop.y0;

                break;
            }
            else if ( punkt.y < v_unten )
            {
                pdf_punkt->seite = i - 1;
                pdf_punkt->punkt.y = EOP;
                ret = -1;

                break;
            }

            v_oben = v_unten + PAGE_SPACE;
        }

        if ( i == pv->arr_pages->len )
        {
            pdf_punkt->seite = i - 1;
            pdf_punkt->punkt.y = EOP;
            ret = -1;
        }
    }

    gtk_widget_get_size_request( pv->layout, &width, NULL );
    x = (gint) (width - (viewer_page->crop.x1 - viewer_page->crop.x0) * pv->zoom / 100) / 2;

    if ( punkt.x < x ) ret = -1;

    if ( punkt.x > (((viewer_page->crop.x1 - viewer_page->crop.x0) * pv->zoom / 100) + x) ) ret = -1;

    pdf_punkt->punkt.x = (punkt.x - x) / pv->zoom * 100;

    return ret;
}


static void
viewer_anzeigen_text_occ( PdfViewer* pv )
{
    PdfPos pdf_pos = { 0 };
    fz_quad quad = { 0 };

    quad = g_array_index( pv->text_occ.arr_quad, fz_quad, pv->text_occ.index_act );

    pv->highlight.page[0] = pv->text_occ.page_act;
    pv->highlight.quad[0] = quad;
    pv->highlight.page[1] = -1;

    pdf_pos.seite = pv->text_occ.page_act;
    pdf_pos.index = (gint) quad.ul.y;

    viewer_springen_zu_pos_pdf( pv, pdf_pos, 40 );
    gtk_widget_queue_draw( pv->layout ); //für den Fall, daß auf gleicher Höhe - dann zeichnet viewer_spring... nicht neu

    return;
}


static gint
viewer_text_occ_search_next( PdfViewer* pv, gint index, gint dir )
{
    gint idx = (dir == 1) ? -1 : pv->text_occ.arr_quad->len;

    do
    {
        fz_quad quad = { 0 };

        idx += dir;

        quad = g_array_index( pv->text_occ.arr_quad, fz_quad, idx );

        if ( dir == 1 && (gint) quad.ul.y < index ) continue;
        else if ( dir == -1 && (gint) quad.ul.y > index ) continue;

        return idx;

    } while ( (dir == 1 && idx < pv->text_occ.arr_quad->len - 1) || (dir == -1 && idx > 0) );

    return -1;
}


static gint
viewer_render_stext_page_from_page( PdfDocumentPage* pdf_document_page, gchar** errmsg )
{
    fz_device* s_t_device = NULL;
    fz_stext_page* stext_page = NULL;

    fz_stext_options opts = { FZ_STEXT_DEHYPHENATE };

    fz_context* ctx = zond_pdf_document_get_ctx( pdf_document_page->document );

    fz_try( ctx ) stext_page =
            fz_new_stext_page( ctx, pdf_document_page->rect );
    fz_catch( ctx ) ERROR_MUPDF( "fz_new_stext_page" )

    //structured text-device
    fz_try( ctx ) s_t_device = fz_new_stext_device( ctx, stext_page, &opts );
    fz_catch( ctx )
    {
        fz_drop_stext_page( ctx, stext_page );
        ERROR_MUPDF( "fz_new_stext_device" )
    }

    //doc-lock muß gesetzt werden, da _load_page auf document zugreift
    zond_pdf_document_mutex_lock( pdf_document_page->document );

    //page durchs list-device laufen lassen
    fz_try( ctx ) pdf_run_page( ctx, pdf_document_page->page, s_t_device, fz_identity, NULL );
    fz_always( ctx )
    {
        zond_pdf_document_mutex_unlock( pdf_document_page->document );
        fz_close_device( ctx, s_t_device );
        fz_drop_device( ctx, s_t_device );
    }
    fz_catch( ctx ) ERROR_MUPDF( "pdf_run_page" )

    pdf_document_page->stext_page = stext_page;

    return 0;
}


gint
viewer_render_stext_page_fast( fz_context* ctx, PdfDocumentPage* pdf_document_page, gchar** errmsg )
{
    //thread für Seite gestartet?
    while ( pdf_document_page->thread & 1 )
            viewer_transfer_rendered( pdf_document_page->thread_pv, TRUE );

    if ( pdf_document_page->thread & 8 ) return 0;

    //page oder display_list nicht geladen
    if ( !(pdf_document_page->thread & 4) )
    {
        gint rc = 0;

        if ( !(pdf_document_page->thread & 2) )
        {
            gint rc = 0;

            zond_pdf_document_mutex_lock( pdf_document_page->document );
            rc = zond_pdf_document_load_page( pdf_document_page, errmsg );
            zond_pdf_document_mutex_unlock( pdf_document_page->document );
            if ( rc ) ERROR_S

            pdf_document_page->thread |= 2;
        }

        rc = viewer_render_stext_page_from_page( pdf_document_page, errmsg );
        if ( rc ) ERROR_S

    }
    else //display_list fertig
    {
        gint rc = 0;

        rc = render_stext_page_from_display_list( ctx, pdf_document_page, errmsg );
        if ( rc ) ERROR_S
    }

    pdf_document_page->thread |= 8;

    return 0;
}



static void
cb_viewer_text_search( GtkWidget* widget, gpointer data )
{
    gint dir = 0;
    PdfPos pdf_pos = { 0 };
    PdfPunkt pdf_punkt = { 0 };
    const gchar* search_text = NULL;

    PdfViewer* pv = (PdfViewer*) data;

    //dokument durchsucht und kein Fund: return
    if ( pv->text_occ.not_found == TRUE )
    {
        display_message( gtk_widget_get_toplevel( widget ), "Kein Treffer", NULL );
        return;
    }

    dir = (widget == pv->button_vorher) ? -1 : 1;

    // Fund angezeigt?
    if ( pv->text_occ.index_act >= 0 )
    {
        //nicht erste oder letzte Fundstelle auf dieser Seite
        if ( !((dir == 1 && pv->text_occ.index_act + dir == pv->text_occ.arr_quad->len) ||
                (dir == -1 && pv->text_occ.index_act == 0 )) )
        {
            pv->text_occ.index_act += dir;
            viewer_anzeigen_text_occ( pv );

            return;
        }
        else //doch
        {
            pdf_pos.seite = pv->text_occ.page_act + dir;

            //Überlauf??
            if ( dir == 1 )
            {
                pdf_pos.index = 0;
                if ( pv->text_occ.page_act == pv->arr_pages->len - 1 ) pdf_pos.seite = 0;
            }
            else if ( dir == -1 )
            {
                pdf_pos.index = EOP;
                if ( pv->text_occ.page_act == 0 ) pdf_pos.seite = pv->arr_pages->len - 1;
            }

            //damit festgestellt werden kann, ob einmal durch...
            //pdf_punkt sonst 0
            pdf_punkt.seite = pdf_pos.seite;
            pdf_punkt.punkt.y = (float) pdf_pos.index;
        }
    }
    else //kein Fund angezeigt: pdf_pos ermitteln
    {
        fz_point point = { 0.0, 0.0 };

        point.y = gtk_adjustment_get_value( pv->v_adj );
        viewer_abfragen_pdf_punkt( pv, point, &pdf_punkt );

        pdf_pos.seite = pdf_punkt.seite;
        pdf_pos.index = (gint) pdf_punkt.punkt.y;
    }

    //kein Fund angezeigt oder erster/letzter Fund auf durchsuchter Seite:
    //nächste/vorherige Seiten müssen so lange durchsucht werden, bis Erfolg oder wieder am Anfang
    search_text = gtk_entry_get_text( GTK_ENTRY(pv->entry_search) );

    //wenn entry leer: nichts machen
    if ( !g_strcmp0( search_text, "" ) ) return;

    do
    {
        //Seite ist aktuell nicht durchsucht - durchsuchen
        if ( pdf_pos.seite != pv->text_occ.page_act )
        {
            gint rc = 0;
            gint anzahl = 0;
            fz_quad quads[100] = { 0 };
            fz_context* ctx = NULL;
            gchar* errmsg = NULL;

            //array leeren
            g_array_remove_range( pv->text_occ.arr_quad, 0, pv->text_occ.arr_quad->len );

            //page_act durchsuchen
            ViewerPageNew* viewer_page = g_ptr_array_index( pv->arr_pages,
                    pdf_pos.seite );

            ctx = zond_pdf_document_get_ctx( viewer_page->pdf_document_page->document );

            rc = viewer_render_stext_page_fast( ctx, viewer_page->pdf_document_page, &errmsg );
            if ( rc )
            {
                display_message( pv->vf, "Fehler Textsuche -\n\n",
                        errmsg, NULL );
                g_free( errmsg );

                return;
            }

            fz_try( ctx ) anzahl = fz_search_stext_page( ctx,
                    viewer_page->pdf_document_page->stext_page, search_text, NULL,
                    quads, 99 );
            fz_catch( ctx )
            {
                pv->text_occ.not_found = TRUE;
                display_message( pv->vf, "Fehler Textsuche -\n\nBei Aufruf fz_search_"
                        "stext_page:\n", fz_caught_message( ctx ), NULL );
                return;
            }

            for ( gint u = 0; u < anzahl; u++ )
            {
                fz_rect text_rect = fz_rect_from_quad( quads[u] );
                fz_rect cropped_text_rect = fz_intersect_rect( viewer_page->crop, text_rect );

                if ( !fz_is_empty_rect( cropped_text_rect ) )
                {
                    fz_quad quad = { 0 };

                    cropped_text_rect = fz_translate_rect( cropped_text_rect,
                            -viewer_page->crop.x0, -viewer_page->crop.y0 );

                    quad = fz_quad_from_rect( cropped_text_rect );

                    g_array_append_val( pv->text_occ.arr_quad, quad );
                }
            }
        }

        //Treffer in soeben oder schon früher durchsuchter Seite?
        if ( pv->text_occ.arr_quad->len ) //dann gucken, ob im sichtbaren Teil der Seite
        {
            pv->text_occ.index_act = viewer_text_occ_search_next( pv, pdf_pos.index, dir );

            if ( pv->text_occ.index_act > -1 )
            {
                pv->text_occ.page_act = pdf_pos.seite;
                viewer_anzeigen_text_occ( pv );

                return;
            }
        }

        //auf der durchsuchten Seite paßt nix: weiterspulen
        pdf_pos.seite += dir;
        if ( dir == 1 ) pdf_pos.index = 0;
        else pdf_pos.index = EOP;

        //Überlauf
        if ( dir == 1 && pdf_pos.seite == pv->arr_pages->len ) pdf_pos.seite = 0;
        else if ( dir == -1 && pdf_pos.seite == -1 ) pdf_pos.seite = pv->arr_pages->len - 1;

        if ( pdf_pos.seite == pdf_punkt.seite ) //Ausgangsseite wieder erreicht?
        {
            //vielleicht Treffer vor/nach index?
            if ( (dir == 1 && pdf_pos.index <= (gint) pdf_punkt.punkt.y) ||
                    (dir == -1 && pdf_pos.index >= (gint) pdf_punkt.punkt.y) )
            {
                pv->text_occ.not_found = TRUE;
                display_message( pv->vf, "Kein Treffer", NULL );

                return;
            }
            else //ansonsten referenz-index ( pdf_punkt) verstellen, damit das nur einmal durchlaufen wird
            {
                if ( dir == 1 ) pdf_punkt.punkt.y = 0;
                else pdf_punkt.punkt.y = (float) EOP;
            }
        }
    }
    while ( 1 );

    return;
}


static void
cb_viewer_spinbutton_value_changed( GtkSpinButton* spin_button, gpointer user_data )
{
    PdfViewer* pv = (PdfViewer*) user_data;

    pv->zoom = gtk_spin_button_get_value( spin_button );

    viewer_close_thread_pool_and_transfer( pv );

    for ( gint i = 0; i < pv->arr_pages->len; i++ )
    {
        ViewerPageNew* viewer_page = NULL;

        viewer_page = g_ptr_array_index( pv->arr_pages, i );
        if ( viewer_page->image_page ) gtk_image_clear( GTK_IMAGE(viewer_page->image_page) );
        viewer_page->pixbuf_page = NULL;
        viewer_page->thread &= 4; //thumb bleibt
    }

    //Alte Position merken
    gdouble v_pos = gtk_adjustment_get_value( pv->v_adj ) /
            gtk_adjustment_get_upper( pv->v_adj );
    gdouble h_pos =  gtk_adjustment_get_value( pv->h_adj )/
            gtk_adjustment_get_upper( pv->h_adj );

    viewer_refresh_layout( pv, 0 );

    gtk_adjustment_set_value( pv->v_adj, gtk_adjustment_get_upper( pv->v_adj ) *
            v_pos );
    gtk_adjustment_set_value( pv->h_adj, gtk_adjustment_get_upper( pv->h_adj ) *
            h_pos );

    g_signal_emit_by_name( pv->v_adj, "value-changed", NULL );

    gtk_widget_grab_focus( pv->layout );

    return;
}


static void
cb_viewer_page_entry_activated( GtkEntry* entry, gpointer user_data )
{
    PdfViewer* pv = (PdfViewer*) user_data;

    const gchar* text_entry = gtk_entry_get_text( entry );

    guint page_num = 0;
    gint rc = 0;
    gint erste = 0;
    gint letzte = 0;

    rc = string_to_guint( text_entry, &page_num );
    if ( rc || (page_num < 1) || (page_num > pv->arr_pages->len) )
    {
        viewer_abfragen_sichtbare_seiten( pv, &erste, &letzte );
        gchar* text = NULL;
        text = g_strdup_printf( "%i-%i", erste + 1, letzte + 1 );
        gtk_entry_set_text( entry, (const gchar*) text );
        g_free( text );
    }
    else
    {
        ViewerPageNew* viewer_page = NULL;

        viewer_page = g_ptr_array_index( pv->arr_pages, page_num - 1 );
        gtk_adjustment_set_value( pv->v_adj, viewer_page->y_pos );
    }

    gtk_widget_grab_focus( pv->layout );

    return;
}


static void
cb_pv_copy_text( GtkMenuItem* item, gpointer data )
{
    gchar* text = NULL;
    gint i = 0;
    gint page_prev = -1;
    gint start = 0;
    gint end = 0;

    PdfViewer* pv = (PdfViewer*) data;


    do
    {
        if ( pv->highlight.page[i] != page_prev )
        {
            if ( page_prev != -1 )
            {
                gchar* add = NULL;
                ViewerPageNew* viewer_page = NULL;
                fz_context* ctx = NULL;

                end = i - 1;
                //text sammeln von page_prev von start - end
                viewer_page = g_ptr_array_index( pv->arr_pages, page_prev );
                while ( viewer_page->pdf_document_page->thread & 1 )
                        viewer_transfer_rendered( viewer_page->pdf_document_page->thread_pv, TRUE );

                ctx = zond_pdf_document_get_ctx( viewer_page->pdf_document_page->document );

                if ( viewer_page->pdf_document_page->thread & 8 )
                {
                    add = fz_copy_selection( ctx, viewer_page->pdf_document_page->stext_page,
                        pv->highlight.quad[start].ul, pv->highlight.quad[end].lr,
                        FALSE );

                    text = add_string( text, add );
                }
            }

            page_prev = pv->highlight.page[i];
            start = i;
        }
    } while ( pv->highlight.page[i++] != -1 );

    GtkClipboard* clipboard = gtk_clipboard_get( GDK_SELECTION_CLIPBOARD );
    gtk_clipboard_set_text( clipboard, text, -1 );

    g_free( text );

    return;
}



static gint
viewer_on_text( PdfViewer* pv, ViewerPageNew* viewer_page, fz_point punkt )
{
	for ( fz_stext_block* block = viewer_page->pdf_document_page->stext_page->first_block; block;
            block = block->next)
	{
		if (block->type != FZ_STEXT_BLOCK_TEXT) continue;

		for ( fz_stext_line* line = block->u.t.first_line; line; line = line->next)
		{
			fz_rect box = line->bbox;
			if ( punkt.x >= box.x0 && punkt.x <= box.x1 &&
                    punkt.y >= box.y0 && punkt.y <= box.y1 )
            {
                gboolean quer = FALSE;
                gint rotate = 0;

                rotate = viewer_page->pdf_document_page->rotate;

                if ( rotate == 90 || rotate == 180 ) quer = TRUE;

                if ( line->wmode == 0 && !quer ) return 1;
                else if ( line->wmode == 1 && quer ) return 1;
                else if ( line->wmode == 0 && quer ) return 2;
                if ( line->wmode == 1 && !quer ) return 2;
            }
		}
	}

	return 0;
}


static gboolean
inside_quad( fz_quad quad, fz_point punkt )
{
    fz_rect rect = fz_rect_from_quad( quad );

    return fz_is_point_inside_rect( punkt, rect );
}


static PdfDocumentPageAnnot*
viewer_on_annot( PdfViewer* pv, ViewerPageNew* viewer_page, fz_point point )
{
    for ( gint i = 0; i < viewer_page->pdf_document_page->arr_annots->len; i++ )
    {
        PdfDocumentPageAnnot* pdf_document_page_annot = NULL;
        pdf_document_page_annot = g_ptr_array_index( viewer_page->pdf_document_page->arr_annots, i );

        if ( pdf_document_page_annot->type == PDF_ANNOT_HIGHLIGHT ||
                pdf_document_page_annot->type == PDF_ANNOT_UNDERLINE ||
                pdf_document_page_annot->type == PDF_ANNOT_STRIKE_OUT ||
                pdf_document_page_annot->type == PDF_ANNOT_SQUIGGLY )
        {
            for ( gint u = 0; u < pdf_document_page_annot->annot_text_markup.arr_quads->len; u++ )
            {
                fz_quad quad = g_array_index( pdf_document_page_annot->annot_text_markup.arr_quads, fz_quad, u );
                if ( inside_quad( quad, point ) ) return pdf_document_page_annot;
            }
        }
        else if ( pdf_document_page_annot->type == PDF_ANNOT_TEXT )
                {if ( fz_is_point_inside_rect( point,
                pdf_document_page_annot->annot_text.rect ) ) return pdf_document_page_annot;}
    }

    return NULL;
}


static void
viewer_set_cursor( PdfViewer* pv, gint rc, ViewerPageNew* viewer_page,
        PdfDocumentPageAnnot* pdf_document_page_annot, PdfPunkt pdf_punkt )
{
    gint on_text = 0;

    if ( rc ) gdk_window_set_cursor( pv->gdk_window, pv->cursor_default );
    else if ( pdf_document_page_annot ) gdk_window_set_cursor( pv->gdk_window, pv->cursor_annot );
    else if ( (viewer_page->pdf_document_page->thread & 8) && (on_text = viewer_on_text( pv, viewer_page, pdf_punkt.punkt )) )
    {
        if ( on_text == 1 ) gdk_window_set_cursor( pv->gdk_window, pv->cursor_text );
        else if ( on_text == 2 ) gdk_window_set_cursor( pv->gdk_window, pv->cursor_vtext );
    }
    else gdk_window_set_cursor( pv->gdk_window, pv->cursor_default );

    return;
}


static void
viewer_thumblist_render_textcell( GtkTreeViewColumn* column, GtkCellRenderer* cell,
        GtkTreeModel* model, GtkTreeIter* iter, gpointer data )
{
    GtkTreePath* path = NULL;
    gint* indices = NULL;
    gchar* text = NULL;

    path = gtk_tree_model_get_path( model, iter );
    indices = gtk_tree_path_get_indices( path );

    text = g_strdup_printf( "%i", indices[0] + 1 );
    gtk_tree_path_free( path );
    g_object_set( G_OBJECT(cell), "text", text, NULL );
    g_free( text );

    return;
}


static gint
viewer_cb_change_annot( PdfViewer* pv, gint page_pv, DisplayedDocument* dd,
        gpointer data, gchar** errmsg )
{
    ViewerPageNew* viewer_page = g_ptr_array_index( pv->arr_pages, page_pv );

    while ( viewer_page->thread & 1 )
            viewer_transfer_rendered( pv, TRUE );

    if ( viewer_page->thread & 2 )
    {
        gtk_image_clear( GTK_IMAGE(viewer_page->image_page) );
        viewer_page->pixbuf_page = NULL;
    }

    if ( viewer_page->thread & 4 )
    {
        GtkTreeIter iter = { 0 };
        gint rc = 0;

        rc = viewer_get_iter_thumb( pv, page_pv, &iter );
        if ( rc ) ERROR_S_MESSAGE( "Iter für thumbnail konnte nicht ermittelt werden" )

        //thumb löschen
        gtk_list_store_set( GTK_LIST_STORE( gtk_tree_view_get_model(
                GTK_TREE_VIEW(pv->tree_thumb) ) ), &iter, 0, NULL, -1 );
        viewer_page->pixbuf_thumb = NULL;
    }

    viewer_page->thread = 0;

    viewer_thread_render( pv, page_pv );

    return 0;
}


gint
viewer_foreach( PdfViewer* pdfv, PdfDocumentPage* pdf_document_page,
        gint (*cb_foreach_pv) (PdfViewer*, gint, DisplayedDocument*,
        gpointer, gchar**), gpointer data, gchar** errmsg )
{
    for ( gint p = 0; p < pdfv->zond->arr_pv->len; p++ )
    {
        gint zaehler = 0;
        gboolean dirty = FALSE;

        PdfViewer* pv_vergleich = g_ptr_array_index( pdfv->zond->arr_pv, p );
        DisplayedDocument* dd_vergleich = pv_vergleich->dd;

        do
        {
            gint von = 0;
            gint bis = 0;

            if ( dd_vergleich->anbindung )
            {
                von = dd_vergleich->anbindung->von.seite;
                bis = dd_vergleich->anbindung->bis.seite;
            }
            else bis = zond_pdf_document_get_number_of_pages( dd_vergleich->zond_pdf_document ) - 1;

            if ( pdf_document_page->document == dd_vergleich->zond_pdf_document )
            {
                GPtrArray* arr_pages = zond_pdf_document_get_arr_pages( pdf_document_page->document );

                for ( gint i = von; i <= bis; i++ )
                {
                    PdfDocumentPage* pdf_document_page_vergleich = g_ptr_array_index( arr_pages, i );
                    if ( pdf_document_page_vergleich == pdf_document_page )
                    {
                        gint rc = 0;

                        if ( cb_foreach_pv ) rc = cb_foreach_pv( pv_vergleich, zaehler + i,
                                dd_vergleich, data, errmsg );
                        if ( rc ) ERROR_S
                        dirty = TRUE;

                        break;
                    }
                }
            }
            else zaehler += (bis - von);
        } while ( (dd_vergleich = dd_vergleich->next) );

        if ( dirty ) gtk_widget_set_sensitive( pv_vergleich->button_speichern, TRUE );
    }

    return 0;
}


static gint
viewer_annot_delete( PdfDocumentPage* pdf_document_page, PdfDocumentPageAnnot* pdf_document_page_annot, gchar** errmsg )
{
    gint index = 0;
    JournalEntry entry = { JOURNAL_TYPE_ANNOT_DELETED, };
    GArray* arr_journal = NULL;

    fz_context* ctx = zond_pdf_document_get_ctx( pdf_document_page->document );

    fz_try( ctx )
    {
        pdf_annot* annot = NULL;

        annot = pdf_first_annot( ctx, pdf_document_page->page );

        do
        {
            if ( annot == pdf_document_page_annot->annot ) break;
            index++;
        }
        while ( (annot = pdf_next_annot( ctx, annot )) );
    }
    fz_catch( ctx ) ERROR_MUPDF( "annot-Index ermitteln" )

    fz_try( ctx ) pdf_delete_annot( ctx, pdf_document_page->page, pdf_document_page_annot->annot );
    fz_catch( ctx ) ERROR_MUPDF( "pdf_delete_annot" )

    arr_journal = zond_pdf_document_get_arr_journal( pdf_document_page->document );

    entry.AnnotDeleted.index = index;
    g_array_append_val( arr_journal, entry );

    return 0;
}


static gboolean
cb_viewer_swindow_key_press( GtkWidget* swindow, GdkEvent* event, gpointer user_data )
{
    PdfViewer* pv = (PdfViewer*) user_data;

    if ( !(pv->clicked_annot) ) return FALSE;

    if ( event->key.keyval == GDK_KEY_Delete )
    {
        gint rc = 0;
        gchar* errmsg = NULL;

        ViewerPageNew* viewer_page = g_ptr_array_index( pv->arr_pages, pv->click_pdf_punkt.seite );

        while ( viewer_page->pdf_document_page->thread & 1 )
                viewer_transfer_rendered( viewer_page->pdf_document_page->thread_pv, TRUE );

        gtk_popover_popdown( GTK_POPOVER(pv->annot_pop_edit) );

        zond_pdf_document_mutex_lock( viewer_page->pdf_document_page->document );
        rc = viewer_annot_delete( viewer_page->pdf_document_page, pv->clicked_annot, &errmsg );
        zond_pdf_document_mutex_unlock( viewer_page->pdf_document_page->document );
        if ( rc )
        {
            display_message( pv->vf, "Fehler - Annotation löschen\n\n"
                    "Bei Aufruf annot_delete", errmsg, NULL );
            g_free( errmsg );

            return FALSE;
        }

        fz_drop_display_list( zond_pdf_document_get_ctx( viewer_page->pdf_document_page->document ),
                viewer_page->pdf_document_page->display_list );
        viewer_page->pdf_document_page->display_list = NULL;
        viewer_page->pdf_document_page->thread &= 10; //4 löschen


        g_ptr_array_remove( viewer_page->pdf_document_page->arr_annots, pv->clicked_annot );
        pv->clicked_annot = NULL;

        rc = viewer_foreach( pv, viewer_page->pdf_document_page,
                viewer_cb_change_annot, NULL, &errmsg );
        if ( rc )
        {
            display_message( pv->vf, "Fehler -\n\n",
                    "Bei Aufruf viewer_refresh_changed_page:\n", errmsg, NULL );
            g_free( errmsg );

            return FALSE;
        }
    }

    return FALSE;
}


static fz_rect
viewer_clamp_icon_rect( ViewerPageNew* viewer_page, fz_rect rect )
{
    fz_rect rect_cropped = { 0, };

    //clamp
    rect_cropped = rect;

    if ( rect_cropped.x0 < viewer_page->crop.x0 ) rect_cropped.x0 = viewer_page->crop.x0;
    if ( rect_cropped.x0 + ANNOT_ICON_WIDTH > viewer_page->crop.x1 ) rect_cropped.x0 = viewer_page->crop.x1 - ANNOT_ICON_WIDTH;

    if ( rect_cropped.y0 < viewer_page->crop.y0 ) rect_cropped.y0 = viewer_page->crop.y0;
    if ( rect_cropped.y0 + ANNOT_ICON_HEIGHT > viewer_page->crop.y1 ) rect_cropped.y0 = viewer_page->crop.y1 - ANNOT_ICON_HEIGHT;

    rect_cropped.x1 = rect_cropped.x0 + ANNOT_ICON_WIDTH;
    rect_cropped.y1 = rect_cropped.y0 + ANNOT_ICON_HEIGHT;

    return rect_cropped;
}


static fz_rect
viewer_rotate_rect( ViewerPageNew* viewer_page, fz_rect rect )
{
    if ( viewer_page->pdf_document_page->rotate == 90 )
    {
        rect.x0 -= 20;
        rect.x1 -= 20;
    }
    else if ( viewer_page->pdf_document_page->rotate == 180 )
    {
        rect.x0 -=20;
        rect.x1 -=20;
        rect.y0 -=20;
        rect.y1 -=20;
    }
    else if ( viewer_page->pdf_document_page->rotate == 270 )
    {
        rect.y0 -= 20;
        rect.y1 -= 20;
    }

    return rect;
}


static gint
viewer_annot_create( ViewerPageNew* viewer_page, PdfViewer* pdfv,
        gchar** errmsg )
{
    pdf_annot* annot = NULL;
    enum pdf_annot_type art = 0;
    fz_rect rect = fz_empty_rect;
    PdfDocumentPageAnnot* pdf_document_page_annot = NULL;

    fz_context* ctx = zond_pdf_document_get_ctx( viewer_page->pdf_document_page->document );

    if ( pdfv->state == 1 ) art = PDF_ANNOT_HIGHLIGHT;
    else if ( pdfv->state == 2 ) art = PDF_ANNOT_UNDERLINE;
    else if ( pdfv->state == 3 ) art = PDF_ANNOT_TEXT;

    zond_pdf_document_mutex_lock( viewer_page->pdf_document_page->document );

    fz_try( ctx ) annot = pdf_create_annot( ctx, viewer_page->pdf_document_page->page, art );
    fz_catch( ctx )
    {
        zond_pdf_document_mutex_unlock( viewer_page->pdf_document_page->document );
        ERROR_MUPDF( "pdf_create_annot/pdf_set_annot_color" )
    }

    if ( pdfv->state == 1 || pdfv->state == 2 )
    {
        gint i = 0;

        if ( art == PDF_ANNOT_UNDERLINE )
        {
            const gfloat color[3] = { 0.1, .85, 0 };
            pdf_set_annot_color( ctx, annot, 3, color );
        }

        while ( pdfv->highlight.page[i] != -1 )
        {
            if ( viewer_page->pdf_document_page->page_doc == pdfv->highlight.page[i] )
            {
                fz_try( ctx )
                    pdf_add_annot_quad_point( ctx, annot, pdfv->highlight.quad[i] );
                fz_catch( ctx )
                {
                    pdf_drop_annot( ctx, annot );
                    zond_pdf_document_mutex_unlock( viewer_page->pdf_document_page->document );
                    ERROR_MUPDF( "pdf_annot_quad_point" )
                }

                fz_rect temp = fz_rect_from_quad( pdfv->highlight.quad[i] );
                rect = fz_union_rect( rect, temp );
            }
            i++;
        }
    }
    else if ( pdfv->state == 3 )
    {
        rect.x0 = pdfv->click_pdf_punkt.punkt.x;
        rect.y0 = pdfv->click_pdf_punkt.punkt.y;
        rect.x1 = rect.x0 + 20;
        rect.y1 = rect.y0 + 20;

        rect = viewer_clamp_icon_rect( viewer_page, rect );

        fz_try( ctx ) pdf_set_annot_icon_name( ctx, annot, "Comment" );
        fz_catch( ctx )
        {
            pdf_drop_annot( ctx, annot );
            zond_pdf_document_mutex_unlock( viewer_page->pdf_document_page->document );
            ERROR_MUPDF( "pdf_set_annot_icon_name" )
        }

        fz_try( ctx ) pdf_set_annot_rect( ctx, annot, viewer_rotate_rect( viewer_page, rect ) );
        fz_catch( ctx )
        {
            pdf_drop_annot( ctx, annot );
            zond_pdf_document_mutex_unlock( viewer_page->pdf_document_page->document );
            ERROR_MUPDF( "pdf_set_annot_rect" )
        }
    }

    pdf_drop_annot( ctx, annot );

    pdf_update_page( ctx, viewer_page->pdf_document_page->page );

    zond_pdf_document_mutex_unlock( viewer_page->pdf_document_page->document );

    pdf_document_page_annot = g_malloc0( sizeof( PdfDocumentPageAnnot ) );

    pdf_document_page_annot->annot = annot;
    pdf_document_page_annot->type = art;
    pdf_document_page_annot->flags = 28; //scheint so'n default zu sein...
    pdf_document_page_annot->pdf_document_page = viewer_page->pdf_document_page;

    //Text-Markup-annots
    if ( art == PDF_ANNOT_HIGHLIGHT || art == PDF_ANNOT_UNDERLINE )
    {
        zond_pdf_document_mutex_lock( viewer_page->pdf_document_page->document );

        fz_try( ctx ) pdf_document_page_annot->annot_text_markup.n_quad =
                pdf_annot_quad_point_count( ctx, annot );
        fz_catch( ctx )
        {
            g_free( pdf_document_page_annot );
            zond_pdf_document_mutex_unlock( viewer_page->pdf_document_page->document );

            ERROR_MUPDF( "pdf_annot_quad_point_count" )
        }

        pdf_document_page_annot->annot_text_markup.arr_quads =
                g_array_new( FALSE, FALSE, sizeof( fz_quad ) );

        for ( gint i = 0; i < pdf_document_page_annot->annot_text_markup.n_quad; i++ )
        {
            fz_quad quad = pdf_annot_quad_point( ctx, annot, i );
            g_array_append_val( pdf_document_page_annot->annot_text_markup.arr_quads, quad );
        }
        zond_pdf_document_mutex_unlock( viewer_page->pdf_document_page->document );
    }
    else if ( pdf_document_page_annot->type == PDF_ANNOT_TEXT )
    {
        pdf_document_page_annot->annot_text.rect = rect;
        pdf_document_page_annot->annot_text.open = FALSE;
        pdf_document_page_annot->annot_text.name = "Comment";
    }

    g_ptr_array_add( viewer_page->pdf_document_page->arr_annots, pdf_document_page_annot );

    return 0;
}


static void
viewer_annot_edit_closed( GtkWidget* popover, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    gchar* text = NULL;
    ViewerPageNew* viewer_page = NULL;
    fz_context* ctx = NULL;
    PdfDocumentPageAnnot* pdf_document_page_annot = NULL;
    GtkTextIter start = { 0, };
    GtkTextIter end = { 0, };
    GtkTextBuffer* text_buffer = NULL;

    PdfViewer* pdfv = (PdfViewer*) data;

    viewer_page = g_object_get_data( G_OBJECT(popover), "viewer-page" );

    ctx = zond_pdf_document_get_ctx( viewer_page->pdf_document_page->document );
    pdf_document_page_annot = g_object_get_data( G_OBJECT(popover), "pdf-document-page-annot" );

    text_buffer = gtk_text_view_get_buffer( GTK_TEXT_VIEW(pdfv->annot_textview) );
    gtk_text_buffer_get_bounds( text_buffer, &start, &end );

    text = gtk_text_buffer_get_text( text_buffer, &start, &end, TRUE );

    zond_pdf_document_mutex_lock( viewer_page->pdf_document_page->document );

    fz_try( ctx )
    {
        pdf_set_annot_contents( ctx, pdf_document_page_annot->annot, text );
        pdf_document_page_annot->content =
                pdf_annot_contents( ctx, pdf_document_page_annot->annot );
    }
    fz_catch( ctx )
    {
        display_message( pdfv->vf, "Fehler speichern TextAnnot -\n\nBei Aufruf "
                "pdf_set_annot_contents/pdf_annot_contents:\n", fz_caught_message( ctx ), NULL );
        zond_pdf_document_mutex_unlock( viewer_page->pdf_document_page->document );

        return;
    }

    zond_pdf_document_mutex_unlock( viewer_page->pdf_document_page->document );

    gtk_text_buffer_set_text( text_buffer, "", -1 );
    //wenn foreach_fn == NULL kann kein Fehler zurückkgegeben werden
    viewer_foreach( pdfv, viewer_page->pdf_document_page,
            NULL, NULL, &errmsg );

    return;
}


static gboolean
cb_viewer_layout_release_button( GtkWidget* layout, GdkEvent* event, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    ViewerPageNew* viewer_page = NULL;
    PdfPunkt pdf_punkt = { 0 };
    gint von = 0;
    gint bis = 0;

    PdfViewer* pv = (PdfViewer*) data;

    if ( event->button.button != GDK_BUTTON_PRIMARY ) return FALSE;
    if ( !(pv->dd) ) return TRUE;

    rc = viewer_abfragen_pdf_punkt( pv,
            fz_make_point( event->motion.x, event->motion.y ), &pdf_punkt );
    viewer_page = g_ptr_array_index( pv->arr_pages, pdf_punkt.seite );
    while ( viewer_page->pdf_document_page->thread & 1 )
            viewer_transfer_rendered( viewer_page->pdf_document_page->thread_pv, TRUE );

    pv->click_on_text = FALSE;

    viewer_set_cursor( pv, rc, viewer_page, pv->clicked_annot, pdf_punkt );

    //Text ist markiert
    if ( pv->highlight.page[0] != -1 )
    {
        //Annot ist gewählt
        if ( (pv->state == 1 || pv->state == 2) )
        {
            if ( pv->click_pdf_punkt.seite < pdf_punkt.seite )
            {
                von = pv->click_pdf_punkt.seite;
                bis = pdf_punkt.seite;
            }
            else if ( pv->click_pdf_punkt.seite > pdf_punkt.seite )
            {
                von = pdf_punkt.seite;
                bis = pv->click_pdf_punkt.seite;
            }
            else //gleiche Seite
            {
                von = pdf_punkt.seite;
                bis = pdf_punkt.seite;
            }

            for ( gint page = von; page <= bis; page ++ )
            {
                ViewerPageNew* viewer_page_loop = NULL;

                if ( page == pdf_punkt.seite ) viewer_page_loop = viewer_page;
                else
                {
                    viewer_page_loop = g_ptr_array_index( pv->arr_pages, page );
                    while ( (viewer_page_loop->pdf_document_page->thread & 1 ) )
                            viewer_transfer_rendered( viewer_page_loop->pdf_document_page->thread_pv, TRUE );
                }

                if ( !(viewer_page_loop->pdf_document_page->thread & 2) ) return TRUE;

                rc = viewer_annot_create( viewer_page_loop, pv, &errmsg );
                if ( rc )
                {
                    display_message( pv->vf, "Fehler - Annotation einfügen:\n\nBei Aufruf "
                            "annot_create:\n", errmsg, NULL );
                    g_free( errmsg );

                    return TRUE;
                }

                fz_drop_display_list( zond_pdf_document_get_ctx( viewer_page_loop->pdf_document_page->document ),
                        viewer_page_loop->pdf_document_page->display_list );
                viewer_page_loop->pdf_document_page->display_list = NULL;
                viewer_page_loop->pdf_document_page->thread &= 10;

                rc = viewer_foreach( pv, viewer_page_loop->pdf_document_page,
                        viewer_cb_change_annot, NULL, &errmsg );
                if ( rc )
                {
                    display_message( pv->vf, "Fehler -\n\n",
                            "Bei Aufruf viewer_refresh_changed_page:\n", errmsg, NULL );
                    g_free( errmsg );

                    return TRUE;
                }
            }
            pv->highlight.page[0] = -1;
        }
        else gtk_widget_set_sensitive( pv->item_copy, TRUE );

        return TRUE;
    }

    //Button wird losgelassen, nachdem auf Text-Annot geklickt wurde
    if ( pv->clicked_annot && pv->clicked_annot->type == PDF_ANNOT_TEXT )
    {
        //verschoben?
        if ( !(pv->click_pdf_punkt.seite == pdf_punkt.seite &&
                pv->click_pdf_punkt.punkt.x == pdf_punkt.punkt.x &&
                pv->click_pdf_punkt.punkt.y == pdf_punkt.punkt.y) )
        {
            fz_context* ctx = NULL;

            if ( pv->click_pdf_punkt.seite != pdf_punkt.seite )
            {
                viewer_page = g_ptr_array_index( pv->arr_pages, pv->click_pdf_punkt.seite );
                while ( (viewer_page->pdf_document_page->thread & 1) )
                        viewer_transfer_rendered( viewer_page->pdf_document_page->thread_pv, TRUE );

                if ( !(viewer_page->pdf_document_page->thread & 2) ) return TRUE;
            }

            ctx = zond_pdf_document_get_ctx( viewer_page->pdf_document_page->document );

            //neues rect speichert
            zond_pdf_document_mutex_lock( viewer_page->pdf_document_page->document );
            fz_try( ctx )
            {
                pv->clicked_annot->annot_text.rect =
                        viewer_clamp_icon_rect( viewer_page,
                        pv->clicked_annot->annot_text.rect );

                pdf_set_annot_rect( ctx, pv->clicked_annot->annot,
                        viewer_rotate_rect( viewer_page, pv->clicked_annot->annot_text.rect ) );
            }
            fz_always( ctx ) zond_pdf_document_mutex_unlock( viewer_page->pdf_document_page->document );
            fz_catch( ctx )
            {
                display_message( pv->vf, "Fehler -Änderung Annot kann nicht "
                        "gespeichert werden\n\nBeiAufruf pdf_set_annot_rect:\n",
                        fz_caught_message( ctx ), NULL );

                return TRUE;
            }

            fz_drop_display_list( ctx, viewer_page->pdf_document_page->display_list );
            viewer_page->pdf_document_page->display_list = NULL;
            viewer_page->pdf_document_page->thread &= 10;

            rc = viewer_foreach( pv, viewer_page->pdf_document_page,
                    viewer_cb_change_annot, NULL, &errmsg );
            if ( rc )
            {
                display_message( pv->vf, "Fehler -\n\n",
                        "Bei Aufruf viewer_refresh_changed_page:\n", errmsg, NULL );
                g_free( errmsg );

                return TRUE;
            }
        }
        else if ( pv->clicked_annot->annot_text.open )  //nicht verschoben, edit-popup geöffnet
        {
            //angeklickt -> textview öffnen
            GdkRectangle gdk_rectangle = { 0, };
            gint x = 0, y = 0, width = 0, height = 0;

            gtk_container_child_get( GTK_CONTAINER(pv->layout), GTK_WIDGET(viewer_page->image_page), "y", &y, NULL );
            y += (gint) (pv->clicked_annot->annot_text.rect.y0 * pv->zoom / 100 );
            y -= gtk_adjustment_get_value( pv->v_adj );

            gtk_container_child_get( GTK_CONTAINER(pv->layout), GTK_WIDGET(viewer_page->image_page), "x", &x, NULL );
            x += (gint) (pv->clicked_annot->annot_text.rect.x0 * pv->zoom / 100 );
            x -= gtk_adjustment_get_value( pv->h_adj );

            height = (gint) ((pv->clicked_annot->annot_text.rect.y1 - pv->clicked_annot->annot_text.rect.y0) * pv->zoom / 100);
            width = (gint) ((pv->clicked_annot->annot_text.rect.x1 - pv->clicked_annot->annot_text.rect.x0) * pv->zoom / 100);

            gdk_rectangle.x = x;
            gdk_rectangle.y = y;
            gdk_rectangle.width = width;
            gdk_rectangle.height = height;

            gtk_popover_popdown( GTK_POPOVER(pv->annot_pop) );

            g_object_set_data( G_OBJECT(pv->annot_pop_edit), "viewer-page", viewer_page );
            g_object_set_data( G_OBJECT(pv->annot_pop_edit), "pdf-document-page-annot", pv->clicked_annot );
            gtk_popover_set_pointing_to( GTK_POPOVER(pv->annot_pop_edit), &gdk_rectangle );
            if ( pv->clicked_annot->content )
                    gtk_text_buffer_set_text( gtk_text_view_get_buffer(
                    GTK_TEXT_VIEW(pv->annot_textview) ),
                    pv->clicked_annot->content, -1 );
            gtk_popover_popup( GTK_POPOVER(pv->annot_pop_edit) );
            gtk_widget_grab_focus( pv->annot_pop_edit );
        }
    }

    return TRUE;
}


static gboolean
cb_viewer_motion_notify( GtkWidget* window, GdkEvent* event, gpointer data )
{
    PdfViewer* pv = (PdfViewer*) data;

    //Signal wird nur durchgelassen, wenn layout keines erhält,
    //also wenn Maus außerhalb layout
    //Ausnahme: Button wurde in layout gedrückt und wird gehalten
    //Vielleicht Fehler in GDK? Oder extra?
    gdk_window_set_cursor( pv->gdk_window, pv->cursor_default );

    return FALSE;
}


static gboolean
cb_viewer_layout_motion_notify( GtkWidget* layout, GdkEvent* event, gpointer data )
{
    gint rc = 0;
    PdfPunkt pdf_punkt = { 0 };
    ViewerPageNew* viewer_page = NULL;

    PdfViewer* pv = (PdfViewer*) data;

    if ( !(pv->dd) ) return TRUE;

    rc = viewer_abfragen_pdf_punkt( pv, fz_make_point( event->motion.x,
            event->motion.y ), &pdf_punkt );

    viewer_page = g_ptr_array_index( pv->arr_pages, pdf_punkt.seite );
    while ( (viewer_page->pdf_document_page->thread & 1 ) )
            viewer_transfer_rendered( viewer_page->pdf_document_page->thread_pv, TRUE );

    //Text erfassen, wenn linker button gehalten
    if ( event->motion.state == GDK_BUTTON1_MASK )
    {
        if ( pv->click_on_text && !pv->clicked_annot )
        {
            PdfPunkt von = { 0 };
            PdfPunkt bis = { 0 };
            gint zaehler = 0;
            fz_context* ctx = NULL;
            gint n = 0;
            fz_point point_start = { 0, };
            fz_point point_end = { 0, };

            if ( pv->click_pdf_punkt.seite < pdf_punkt.seite )
            {
                von = pv->click_pdf_punkt;
                bis = pdf_punkt;
            }
            else if ( pv->click_pdf_punkt.seite > pdf_punkt.seite )
            {
                von = pdf_punkt;
                bis = pv->click_pdf_punkt;
            }
            else //gleiche Seite
            {
                if ( pv->click_pdf_punkt.punkt.y < pdf_punkt.punkt.y )
                {
                    von = pv->click_pdf_punkt;
                    bis = pdf_punkt;
                }
                else if ( pv->click_pdf_punkt.punkt.y > pdf_punkt.punkt.y )
                {
                    von = pdf_punkt;
                    bis = pv->click_pdf_punkt;
                }
                else //gleiche Höhe
                {
                    if ( pv->click_pdf_punkt.punkt.x < pdf_punkt.punkt.x )
                    {
                        von = pv->click_pdf_punkt;
                        bis = pdf_punkt;
                    }
                    else if ( pv->click_pdf_punkt.punkt.x >= pdf_punkt.punkt.x )
                    {
                        von = pdf_punkt;
                        bis = pv->click_pdf_punkt;
                    }
                }
            }

            for ( gint page = von.seite; page <= bis.seite; page ++ )
            {
                ViewerPageNew* viewer_page_loop = NULL;

                if ( page == pdf_punkt.seite ) viewer_page_loop = viewer_page;
                else
                {
                    viewer_page_loop = g_ptr_array_index( pv->arr_pages, page );
                    while ( (viewer_page_loop->pdf_document_page->thread & 1 ) )
                            viewer_transfer_rendered( viewer_page_loop->pdf_document_page->thread_pv, TRUE );
                }

                //when stext_page nicht gerendert, dann weiter
                if ( !(viewer_page_loop->pdf_document_page->thread & 8) ) continue;

                if ( page == von.seite )
                {
                    point_start = von.punkt;
                    if ( page == bis.seite ) point_end = bis.punkt;
                    else point_end = fz_make_point( viewer_page_loop->crop.x1, viewer_page_loop->crop.y1 );
                }
                else if ( page == bis.seite )
                { //Der Fall, daß page == von.seite, ist schon abgefrühstückt
                    point_start = fz_make_point( 0, 0 );
                    point_end = bis.punkt;
                }
                else //dazwischen
                {
                    point_start = fz_make_point( 0, 0 );
                    point_end = fz_make_point( viewer_page_loop->crop.x1, viewer_page_loop->crop.y1 );
                }

                ctx = zond_pdf_document_get_ctx( viewer_page_loop->pdf_document_page->document );

                n = fz_highlight_selection( ctx, viewer_page_loop->pdf_document_page->stext_page,
                        point_start, point_end, &pv->highlight.quad[zaehler],
                        999 - zaehler );

                for ( gint u = 0; u < n; u++ ) pv->highlight.page[u + zaehler] = page;

                zaehler += n;

                pv->highlight.page[zaehler] = -1;

                gtk_widget_queue_draw( viewer_page_loop->image_page );
            }

            //Wenn Maus ruckartig über Seitengrenzen bewegt wird
            // werden alt Markierungen manchmal nicht mitgenommen
            for ( gint range_old = pv->von_alt; range_old <= pv->bis_alt; range_old++ )
            {
                if ( range_old < von.seite || range_old > bis.seite )
                {
                    ViewerPageNew* viewer_page_old_range = NULL;

                    viewer_page_old_range = g_ptr_array_index( pv->arr_pages, range_old );
                    while ( (viewer_page_old_range->pdf_document_page->thread & 1 ) )
                            viewer_transfer_rendered( viewer_page_old_range->pdf_document_page->thread_pv, TRUE );

                    if ( !(viewer_page->thread & 2 ) ) continue;

                    gtk_widget_queue_draw( viewer_page_old_range->image_page );
                }
            }
            pv->von_alt = von.seite;
            pv->bis_alt = bis.seite;
        }
        else if ( pv->clicked_annot && pv->clicked_annot->type == PDF_ANNOT_TEXT )
        {
//            if ( rc || pdf_punkt.seite != pv->click_pdf_punkt.seite ) return TRUE;

            if ( !(viewer_page->thread & 2) ) return TRUE;

            gtk_popover_popdown( GTK_POPOVER(pv->annot_pop) );

            pv->clicked_annot->annot_text.rect.x0 -= (pv->x - event->motion.x_root) / pv->zoom * 100;
            pv->clicked_annot->annot_text.rect.x1 -= (pv->x - event->motion.x_root) / pv->zoom * 100;
            pv->clicked_annot->annot_text.rect.y0 -= (pv->y - event->motion.y_root) / pv->zoom * 100;
            pv->clicked_annot->annot_text.rect.y1 -= (pv->y - event->motion.y_root) / pv->zoom * 100;

            gtk_widget_queue_draw( viewer_page->image_page );
        }
        else //nicht auf Text und nicht auf Text-annot
        { //layout wird mit Mauszeiger geschoben
            gdouble y = gtk_adjustment_get_value( pv->v_adj );
            gdouble x = gtk_adjustment_get_value(pv->h_adj );
            gtk_adjustment_set_value( pv->v_adj, y + pv->y - event->motion.y_root );
            gtk_adjustment_set_value( pv->h_adj, x+ pv->x - event->motion.x_root );
        }
        pv->y = event->motion.y_root;
        pv->x = event->motion.x_root;
    }
    //kein Button, Mauszeiger wird über annot bewegt
    else
    {
        PdfDocumentPageAnnot* pdf_document_page_annot = NULL;

        if ( (viewer_page->pdf_document_page->thread & 2) &&
                (pdf_document_page_annot = viewer_on_annot( pv, viewer_page, pdf_punkt.punkt )) )
        {
            //Popover anzeigen, falls /Contents text enthält
            if ( pdf_document_page_annot->type == PDF_ANNOT_TEXT &&
                    pdf_document_page_annot->content && //Inhalt?
                    g_strcmp0( pdf_document_page_annot->content, "" ) && //gefüllt?
                    !( pdf_document_page_annot == pv->clicked_annot && //nicht angeklickt und...
                    gtk_widget_is_visible( pv->annot_pop_edit )) )//...geöffnet
            {
                GdkRectangle gdk_rectangle = { 0, };
                gint x = 0, y = 0, width = 0, height = 0;

                gtk_container_child_get( GTK_CONTAINER(pv->layout),
                        viewer_page->image_page, "y", &y, NULL );
                y += (gint) (pdf_document_page_annot->annot_text.rect.y0 * pv->zoom / 100 );
                y -= gtk_adjustment_get_value( pv->v_adj );

                gtk_container_child_get( GTK_CONTAINER(pv->layout),
                        viewer_page->image_page, "x", &x, NULL );
                x += (gint) (pdf_document_page_annot->annot_text.rect.x0 * pv->zoom / 100 );
                x -= gtk_adjustment_get_value( pv->h_adj );

                height = (gint) ((pdf_document_page_annot->annot_text.rect.y1 - pdf_document_page_annot->annot_text.rect.y0) * pv->zoom / 100);
                width = (gint) ((pdf_document_page_annot->annot_text.rect.x1 - pdf_document_page_annot->annot_text.rect.x0) * pv->zoom / 100);

                gdk_rectangle.x = x;
                gdk_rectangle.y = y;
                gdk_rectangle.width = width;
                gdk_rectangle.height = height;

                gtk_popover_set_pointing_to( GTK_POPOVER(pv->annot_pop), &gdk_rectangle );
                gtk_label_set_text( GTK_LABEL(pv->annot_label), pdf_document_page_annot->content );
                gtk_popover_popup( GTK_POPOVER(pv->annot_pop) );
            }
        }
        else gtk_popover_popdown( GTK_POPOVER(pv->annot_pop) );

        viewer_set_cursor( pv, rc, viewer_page, pdf_document_page_annot, pdf_punkt ); //Kein Knopf gedrückt
    }

    return TRUE;
}


static gboolean
cb_viewer_layout_press_button( GtkWidget* layout, GdkEvent* event, gpointer
        user_data )
{
    gint rc = 0;
    PdfPunkt pdf_punkt = { 0 };

    PdfViewer* pv = (PdfViewer*) user_data;

    if ( !(pv->dd) ) return TRUE;

    gtk_widget_grab_focus( pv->layout );

    rc = viewer_abfragen_pdf_punkt( pv, fz_make_point( event->button.x,
            event->button.y ), &pdf_punkt );

//Einzelklick
    if ( event->button.type == GDK_BUTTON_PRESS &&
            event->button.button == 1 )
    {
        ViewerPageNew* viewer_page = NULL;

        pv->click_pdf_punkt = pdf_punkt;
        pv->highlight.page[0] = -1;
        pv->text_occ.index_act = -1;

        pv->y = event->button.y_root;
        pv->x = event->button.x_root;

        pv->von_alt = pdf_punkt.seite;
        pv->von_alt = pdf_punkt.seite;

        gtk_widget_set_sensitive( pv->item_copy, FALSE );

        viewer_page = g_ptr_array_index( pv->arr_pages, pdf_punkt.seite );
        while ( (viewer_page->pdf_document_page->thread & 1) )
                viewer_transfer_rendered( viewer_page->pdf_document_page->thread_pv, TRUE );

        PdfDocumentPageAnnot* pdf_document_page_annot = NULL;

        if ( (viewer_page->pdf_document_page->thread & 2) &&
                (pdf_document_page_annot = viewer_on_annot( pv, viewer_page, pdf_punkt.punkt )) )
        {
            if ( pv->clicked_annot && pv->clicked_annot->type == PDF_ANNOT_TEXT )
            {
                if ( pdf_document_page_annot != pv->clicked_annot )
                        pv->clicked_annot->annot_text.open = FALSE;
                else pv->clicked_annot->annot_text.open = TRUE;
            }

            pv->clicked_annot = pdf_document_page_annot;
        }
        else //nicht auf annot geklickt, z.B. weil neben layout geclickt
        {
            //wird weiter unten geprüft, ob click_on_text wieder angeschaltet werden soll
            pv->click_on_text = FALSE;

            if ( pv->clicked_annot && pv->clicked_annot->type == PDF_ANNOT_TEXT )
                    pv->clicked_annot->annot_text.open = FALSE;
            pv->clicked_annot = NULL;

            if ( (viewer_page->pdf_document_page->thread & 2) && pv->state == 3 ) //Neue AnnotText einfügen
            {
                gint rc = 0;
                gchar* errmsg = NULL;

                rc = viewer_annot_create( viewer_page, pv, &errmsg );
                if ( rc )
                {
                    display_message( pv->vf, "Fehler - Annotation einfügen:\n\n",
                            errmsg, NULL );
                    g_free( errmsg );

                    return TRUE;
                }

                fz_drop_display_list( zond_pdf_document_get_ctx(
                        viewer_page->pdf_document_page->document ),
                        viewer_page->pdf_document_page->display_list );
                viewer_page->pdf_document_page->display_list = NULL;

                viewer_page->pdf_document_page->thread &= 10;

                rc = viewer_foreach( pv, viewer_page->pdf_document_page,
                        viewer_cb_change_annot, NULL, &errmsg );
                if ( rc )
                {
                    display_message( pv->vf, "Fehler - in ", __func__, "\n\n",
                            errmsg, NULL );
                    g_free( errmsg );

                    return TRUE;
                }

                //neu erzeugte Text-Annot soll markiert sein!
                pv->clicked_annot = g_ptr_array_index(
                        viewer_page->pdf_document_page->arr_annots,
                        viewer_page->pdf_document_page->arr_annots->len - 1 );
                pv->clicked_annot->annot_text.open = TRUE;

                //Nach Einfügen von annot-text: auf Zeiger zurückflitschen
                gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(pv->button_zeiger), TRUE );
            }
            else if ( (viewer_page->pdf_document_page->thread & 4) &&
                    viewer_on_text( pv, viewer_page, pdf_punkt.punkt ) ) pv->click_on_text = TRUE;
            else gdk_window_set_cursor( pv->gdk_window, pv->cursor_grab );
        }

        gtk_widget_queue_draw( pv->layout ); //um ggf. Markierung der annot zu löschen
    }
#ifndef VIEWER
//Doppelklick - nur für Anbindung interessant
    else if ( event->button.type == GDK_2BUTTON_PRESS &&
            event->button.button == 1 )
    {
        gboolean punktgenau = FALSE;
        if ( event->button.state == GDK_SHIFT_MASK ) punktgenau = TRUE;
        if ( !rc && pv->anbindung.von.index == -1 )
        {
            pv->anbindung.von.seite = pdf_punkt.seite;
            if ( punktgenau ) pv->anbindung.von.index = pdf_punkt.punkt.y;
            else pv->anbindung.von.index = 0;

            //Wahl des Beginns irgendwie anzeigen
            gchar* button_label_text =
                    g_strdup_printf( "Anbindung Anfang löschen\nSeite: %i, Index: %i",
                    pv->anbindung.von.seite, pv->anbindung.von.index );
            gtk_widget_set_tooltip_text( pv->button_anbindung, button_label_text );
            gtk_widget_set_sensitive( pv->button_anbindung, TRUE );

            g_free( button_label_text );

            return FALSE;
        }

        //Wenn nicht zurückliegende Seite oder - wenn punktgenau - gleiche
        //Seite und zurückliegender Index
        if ( !rc )
        {
            GError* error = NULL;

            //"richtige" Reihenfolge
            if  ( (pdf_punkt.seite >= pv->anbindung.von.seite) ||
                    ((punktgenau) &&
                    (pdf_punkt.seite == pv->anbindung.von.seite) &&
                    (pdf_punkt.punkt.y >= pv->anbindung.von.index)) )
            {
                pv->anbindung.bis.seite = pdf_punkt.seite;
                if ( punktgenau ) pv->anbindung.bis.index = pdf_punkt.punkt.y;
                else pv->anbindung.bis.index = EOP;
            }
            else //umdrehen
            {
                pv->anbindung.bis.seite = pv->anbindung.von.seite;
                if ( pv->anbindung.von.index == 0 ) pv->anbindung.bis.index = EOP;
                else pv->anbindung.bis.index = pv->anbindung.von.index;

                pv->anbindung.von.seite = pdf_punkt.seite;
                if ( punktgenau ) pv->anbindung.von.index = pdf_punkt.punkt.y;
                else pv->anbindung.von.index = 0;
            }

            rc = zond_anbindung_erzeugen( pv, &error );
            if ( rc == -1 )
            {
                display_message( pv->vf, "Fehler - Anbinden per Doppelklick\n\n",
                        error->message, NULL );
                g_error_free( error );
            }
            else if ( rc == 0 ) gtk_window_present( GTK_WINDOW(pv->zond->app_window) );
        }

        //anbindung.von "löschen"
        pv->anbindung.von.index = -1;

        //Anzeige Beginn rückgängig machen
        gtk_widget_set_tooltip_text( pv->button_anbindung, "Anbindung Anfang löschen" );
        gtk_widget_set_sensitive( pv->button_anbindung, FALSE );
    }
#endif

    return TRUE;
}


static void
viewer_cb_draw_page_for_printing( GtkPrintOperation* op, GtkPrintContext* context, gint page_nr, gpointer user_data )
{
    PdfViewer* pdfv = NULL;
    ViewerPageNew* viewer_page = NULL;
    fz_context* ctx = NULL;
    gdouble width = 0;
    gdouble height = 0;
    gdouble zoom_x = 0;
    gdouble zoom_y = 0;
    gdouble zoom = 0;

    pdfv = (PdfViewer*) user_data;

    //page_act durchsuchen
    viewer_page = g_ptr_array_index( pdfv->arr_pages, page_nr );

    viewer_thread_render( pdfv, page_nr );
    while ( viewer_page->pdf_document_page->thread & 1 )
            viewer_transfer_rendered( (PdfViewer*) viewer_page->pdf_document_page->thread_pv, TRUE );

    width = gtk_print_context_get_width( context );
    height = gtk_print_context_get_height( context );

    zoom_x = width / (viewer_page->crop.x1 - viewer_page->crop.x0);
    zoom_y = height / (viewer_page->crop.y1 - viewer_page->crop.y0);

    zoom = (zoom_x <= zoom_y) ? zoom_x : zoom_y;

    ctx = fz_clone_context( zond_pdf_document_get_ctx( viewer_page->pdf_document_page->document ) );
    if ( !ctx )
    {
        gchar* errmsg = NULL;

        errmsg = g_strdup_printf( "Seite Nr. %i", page_nr );
        display_message( pdfv->vf, "Fehler Drucken ", errmsg, " -\n\nfz_context "
                "konnte nicht geklont werden", NULL );
        g_free( errmsg );

        return;
    }

    //Pixmap rendern
    fz_pixmap* pixmap = NULL;

    fz_matrix transform = fz_scale( zoom, zoom );

    fz_rect rect = fz_transform_rect( viewer_page->crop, transform );
    fz_irect irect = fz_round_rect( rect );

    //per draw-device to pixmap
    fz_try( ctx ) pixmap = fz_new_pixmap_with_bbox( ctx, fz_device_rgb( ctx ),
            irect, NULL, 0 );
    fz_catch( ctx )
    {
        gchar* errmsg = NULL;

        errmsg = g_strdup_printf( "Seite Nr. %i", page_nr );
        display_message( pdfv->vf, "Fehler Drucken ", errmsg, " -\n\nBei "
                "Aufruf fz_new_pixmap_with_bbox:\n", fz_caught_message( ctx ), NULL );
        g_free( errmsg );
        fz_drop_context( ctx );

        return;
    }

    fz_try( ctx) fz_clear_pixmap_with_value( ctx, pixmap, 255 );
    fz_catch( ctx )
    {
        gchar* errmsg = NULL;

        errmsg = g_strdup_printf( "Seite Nr. %i", page_nr );
        display_message( pdfv->vf, "Fehler Drucken ", errmsg, " -\n\nBei "
                "Aufruf fz_pixmap_with_value:\n", fz_caught_message( ctx ), NULL );
        g_free( errmsg );
        fz_drop_context( ctx );

        return;
    }

    fz_device* draw_device = NULL;
    fz_try( ctx ) draw_device = fz_new_draw_device( ctx, fz_identity, pixmap );
    fz_catch( ctx )
    {
        gchar* errmsg = NULL;

        fz_drop_pixmap( ctx, pixmap );

        errmsg = g_strdup_printf( "Seite Nr. %i", page_nr );
        display_message( pdfv->vf, "Fehler Drucken ", errmsg, " -\n\nBei "
                "Aufruf fz_new_draw_device:\n", fz_caught_message( ctx ), NULL );
        g_free( errmsg );
        fz_drop_context( ctx );

        return;
    }

    fz_try( ctx ) fz_run_display_list( ctx, viewer_page->pdf_document_page->display_list,
            draw_device, transform, rect, NULL );
    fz_always( ctx )
    {
        fz_close_device( ctx, draw_device );
        fz_drop_device( ctx, draw_device );
    }
    fz_catch( ctx )
    {
        gchar* errmsg = NULL;

        fz_drop_pixmap( ctx, pixmap );

        errmsg = g_strdup_printf( "Seite Nr. %i", page_nr );
        display_message( pdfv->vf, "Fehler Drucken ", errmsg, " -\n\nBei "
                "Aufruf fz_run_display_list:\n", fz_caught_message( ctx ), NULL );
        g_free( errmsg );
        fz_drop_context( ctx );

        return;
    }
    GdkPixbuf* pixbuf = NULL;

    pixbuf = gdk_pixbuf_new_from_data( pixmap->samples,
            GDK_COLORSPACE_RGB, FALSE, 8, pixmap->w, pixmap->h,
            pixmap->stride, NULL, NULL );

    cairo_t* cr = gtk_print_context_get_cairo_context( context );
    gdk_cairo_set_source_pixbuf( cr, pixbuf, 0, 0 );
    g_object_unref( pixbuf );
    fz_drop_pixmap( ctx, pixmap );
    fz_drop_context( ctx );

    cairo_paint( cr );
    cairo_fill( cr );

    return;
}


static void
viewer_cb_print( GtkButton* button, gpointer data )
{
    GtkPrintOperation* print = NULL;
    GtkPrintOperationResult res;
    GtkPageSetup* page_setup = NULL;
    GError* error = NULL;

    PdfViewer* pdfv = (PdfViewer*) data;

    print = gtk_print_operation_new( );

    gtk_print_operation_set_n_pages( print, pdfv->arr_pages->len );

    page_setup = gtk_page_setup_new( );
    gtk_page_setup_set_top_margin( page_setup, 0, GTK_UNIT_POINTS );
    gtk_page_setup_set_bottom_margin( page_setup, 0, GTK_UNIT_POINTS );
    gtk_page_setup_set_left_margin( page_setup, 0, GTK_UNIT_POINTS );
    gtk_page_setup_set_right_margin( page_setup, 0, GTK_UNIT_POINTS );
    gtk_print_operation_set_default_page_setup( print, page_setup );
    g_object_unref( page_setup );

    g_signal_connect (print, "draw_page",
            G_CALLBACK (viewer_cb_draw_page_for_printing), pdfv );

    res = gtk_print_operation_run( print, GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,
                                 GTK_WINDOW(pdfv->vf), &error );
    g_object_unref( print );
    if ( res == GTK_PRINT_OPERATION_RESULT_ERROR )
    {
        display_message( pdfv->vf, "Fehler Ausdruck -\n\nBei Aufruf gtk_print_operation_run:\n",
                error->message, NULL );
        g_clear_error( &error );
    }

    return;
}


static void
viewer_einrichten_fenster( PdfViewer* pv )
{
    pv->vf = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_default_size( GTK_WINDOW(pv->vf), VIEWER_WIDTH, VIEWER_HEIGHT );

    GtkAccelGroup* accel_group = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(pv->vf), accel_group);

//  Menu
#ifdef VIEWER
    GtkWidget* item_oeffnen = gtk_menu_item_new_with_label( "Datei öffnen" );
    pv->item_schliessen = gtk_menu_item_new_with_label( "Datei schließen" );
    GtkWidget* item_beenden = gtk_menu_item_new_with_label( "Beenden" );
    GtkWidget* item_sep1 = gtk_separator_menu_item_new( );
#endif // VIEWER
    pv->item_kopieren = gtk_menu_item_new_with_label( "Seiten kopieren" );
    gtk_widget_add_accelerator( pv->item_kopieren, "activate", accel_group,
            GDK_KEY_c, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    pv->item_ausschneiden = gtk_menu_item_new_with_label( "Seiten ausschneiden" );
    gtk_widget_add_accelerator( pv->item_ausschneiden, "activate", accel_group,
            GDK_KEY_x, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    //Drehen
    pv->item_drehen = gtk_menu_item_new_with_label( "Seiten drehen" );
    //Einfügen
    pv->item_einfuegen = gtk_menu_item_new_with_label( "Seiten einfügen" );
    //Löschen
    pv->item_loeschen = gtk_menu_item_new_with_label( "Seiten löschen" );
    //Löschen
    pv->item_entnehmen = gtk_menu_item_new_with_label( "Entnehmen" );
    //Löschen
    pv->item_ocr = gtk_menu_item_new_with_label( "OCR" );

    GtkWidget* sep0 = gtk_separator_menu_item_new( );

    pv->item_copy = gtk_menu_item_new_with_label( "Text kopieren" );

    gtk_widget_set_sensitive( pv->item_kopieren, FALSE );
    gtk_widget_set_sensitive( pv->item_ausschneiden, FALSE );
    gtk_widget_set_sensitive( pv->item_copy, FALSE );

    //Menu erzeugen
    GtkWidget* menu_viewer = gtk_menu_new( );

    //Füllen
#ifdef VIEWER
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_viewer), item_oeffnen );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_viewer), pv->item_schliessen );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_viewer), item_beenden );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_viewer), item_sep1);
#endif // VIEWER
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_viewer), pv->item_kopieren );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_viewer), pv->item_ausschneiden );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_viewer), pv->item_einfuegen );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_viewer), pv->item_drehen );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_viewer), pv->item_loeschen );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_viewer), pv->item_entnehmen );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_viewer), pv->item_ocr );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_viewer), sep0 );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_viewer), pv->item_copy );

    //menu sichtbar machen
    gtk_widget_show_all( menu_viewer );

    //Menu Button
    GtkWidget* button_menu_viewer = gtk_menu_button_new( );

    //einfügen
    gtk_menu_button_set_popup( GTK_MENU_BUTTON(button_menu_viewer), menu_viewer );

//  Headerbar
    pv->headerbar = gtk_header_bar_new( );
    gtk_header_bar_set_show_close_button( GTK_HEADER_BAR(pv->headerbar),
            TRUE );
    gtk_header_bar_set_has_subtitle(GTK_HEADER_BAR(pv->headerbar), TRUE );
    gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(pv->headerbar),
            ":minimize,maximize,close" );
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(pv->headerbar),
            TRUE);
    gtk_window_set_titlebar( GTK_WINDOW(pv->vf), pv->headerbar );

//Toolbar
    //ToggleButton zum "Ausfahren der thumbnail-Leiste
    GtkWidget* button_thumb = gtk_toggle_button_new( );
    GtkWidget* image_thumb = gtk_image_new_from_icon_name( "go-next",
            GTK_ICON_SIZE_BUTTON );
    gtk_button_set_image( GTK_BUTTON(button_thumb), image_thumb );

    //  Werkzeug Zeiger
    pv->button_speichern = gtk_button_new_from_icon_name( "document-save", GTK_ICON_SIZE_BUTTON );
    gtk_widget_set_sensitive( pv->button_speichern, FALSE );
    GtkWidget* button_print = gtk_button_new_from_icon_name( "document-print", GTK_ICON_SIZE_BUTTON );
    pv->button_zeiger = gtk_radio_button_new( NULL );
    GtkWidget* button_highlight = gtk_radio_button_new_from_widget( GTK_RADIO_BUTTON(pv->button_zeiger) );
    GtkWidget* button_underline = gtk_radio_button_new_from_widget( GTK_RADIO_BUTTON(button_highlight) );
    GtkWidget* button_paint = gtk_radio_button_new_from_widget( GTK_RADIO_BUTTON(button_underline) );

    GtkWidget* image_zeiger = gtk_image_new_from_icon_name( "accessories-text-editor",
            GTK_ICON_SIZE_BUTTON );
    gtk_button_set_image( GTK_BUTTON(pv->button_zeiger), image_zeiger );
    GtkWidget* image_highlight = gtk_image_new_from_icon_name( "edit-select-all",
            GTK_ICON_SIZE_BUTTON );
    gtk_button_set_image( GTK_BUTTON(button_highlight), image_highlight );
    GtkWidget* image_underline = gtk_image_new_from_icon_name( "format-text-underline",
            GTK_ICON_SIZE_BUTTON );
    gtk_button_set_image( GTK_BUTTON(button_underline), image_underline );
    GtkWidget* image_paint = gtk_image_new_from_icon_name( "edit-paste",
            GTK_ICON_SIZE_BUTTON );
    gtk_button_set_image( GTK_BUTTON(button_paint), image_paint );

    //SpinButton für Zoom
    GtkWidget* spin_button = gtk_spin_button_new_with_range( ZOOM_MIN,
            ZOOM_MAX, 5.0 );
    gtk_orientable_set_orientation( GTK_ORIENTABLE(spin_button), GTK_ORIENTATION_VERTICAL );
    gtk_spin_button_set_value( GTK_SPIN_BUTTON(spin_button), (gdouble) pv->zoom );

    //frame
    GtkWidget* frame_spin = gtk_frame_new( "Zoom" );
    gtk_container_add( GTK_CONTAINER(frame_spin), spin_button );

    //signale des SpinButton
    g_signal_connect( spin_button, "value-changed",
            G_CALLBACK(cb_viewer_spinbutton_value_changed), (gpointer) pv );

    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(pv->button_zeiger), TRUE );

    g_object_set_data( G_OBJECT(button_highlight), "ID", GINT_TO_POINTER(1) );
    g_object_set_data( G_OBJECT(button_underline), "ID", GINT_TO_POINTER(2) );
    g_object_set_data( G_OBJECT(button_paint), "ID", GINT_TO_POINTER(3) );

#ifndef VIEWER
    //button löschenAnbindung Anfangsposition
    pv->button_anbindung = gtk_button_new_from_icon_name( "edit-delete", GTK_ICON_SIZE_BUTTON );
    gtk_widget_set_sensitive( pv->button_anbindung, FALSE );
    gtk_widget_set_tooltip_text( pv->button_anbindung, "Anbindung Anfang löschen" );
#endif // VIEWERDisplayedDocument*

//vbox Tools
    GtkWidget* vbox_tools = gtk_box_new( GTK_ORIENTATION_VERTICAL, 0 );

    //einfügen
    gtk_box_pack_start( GTK_BOX(vbox_tools), button_menu_viewer, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(vbox_tools), pv->button_speichern, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(vbox_tools), button_print, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(vbox_tools), button_thumb, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(vbox_tools), pv->button_zeiger, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(vbox_tools), button_highlight, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(vbox_tools), button_underline, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(vbox_tools), button_paint, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(vbox_tools), frame_spin, FALSE, FALSE, 0 );
#ifndef VIEWER
    gtk_box_pack_start( GTK_BOX(vbox_tools), pv->button_anbindung, FALSE, FALSE, 0 );
#endif // VIEWER

//Box mit Eingabemöglichkeiten
    //Eingagabefeld für Seitenzahlen erzeugen
    pv->entry = gtk_entry_new();
    gtk_entry_set_input_purpose( GTK_ENTRY(pv->entry), GTK_INPUT_PURPOSE_DIGITS );
    gtk_entry_set_width_chars( GTK_ENTRY(pv->entry), 9 );

    //label mit Gesamtseitenzahl erzeugen
    pv->label_anzahl = gtk_label_new( "" );

    //in box und dann in frame
    GtkWidget* box_seiten = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 0 );
    gtk_box_pack_start( GTK_BOX(box_seiten), pv->entry, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(box_seiten), pv->label_anzahl, FALSE, FALSE, 0 );
    GtkWidget* frame_seiten = gtk_frame_new( "Seiten" );
    gtk_container_add( GTK_CONTAINER(frame_seiten), box_seiten );

    //Textsuche im geöffneten Dokument
    pv->button_vorher = gtk_button_new_from_icon_name( "go-previous",
            GTK_ICON_SIZE_SMALL_TOOLBAR );
    pv->entry_search = gtk_entry_new( );
    gtk_entry_set_width_chars( GTK_ENTRY(pv->entry_search), 15 );
    pv->button_nachher = gtk_button_new_from_icon_name( "go-next",
            GTK_ICON_SIZE_SMALL_TOOLBAR );

    GtkWidget* box_text = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 0 );
    gtk_box_pack_start( GTK_BOX(box_text), pv->button_vorher, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(box_text), pv->entry_search, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(box_text), pv->button_nachher, FALSE, FALSE, 0 );
    GtkWidget* frame_search = gtk_frame_new( "Text suchen" );
    gtk_container_add( GTK_CONTAINER(frame_search), box_text );

    gtk_header_bar_pack_start( GTK_HEADER_BAR(pv->headerbar), frame_seiten );
    gtk_header_bar_pack_end( GTK_HEADER_BAR(pv->headerbar), frame_search );

//layout
    pv->layout = gtk_layout_new( NULL, NULL );
    gtk_widget_set_can_focus( pv->layout, TRUE );

    gtk_widget_add_events( pv->layout, GDK_POINTER_MOTION_MASK );
    gtk_widget_add_events( pv->layout, GDK_BUTTON_PRESS_MASK );
    gtk_widget_add_events( pv->layout, GDK_BUTTON_RELEASE_MASK );

//Scrolled window
    GtkWidget* swindow = gtk_scrolled_window_new( NULL, NULL );
    //Adjustments
    pv->v_adj = gtk_scrolled_window_get_vadjustment(
            GTK_SCROLLED_WINDOW(swindow) );
    pv->h_adj = gtk_scrolled_window_get_hadjustment(
            GTK_SCROLLED_WINDOW(swindow) );

    gtk_container_add( GTK_CONTAINER(swindow), pv->layout );
    gtk_widget_set_halign( pv->layout, GTK_ALIGN_CENTER );

//Scrolled window für thumbnail_tree
    pv->tree_thumb = gtk_tree_view_new( );

    gtk_tree_view_set_headers_visible( GTK_TREE_VIEW(pv->tree_thumb), FALSE );
    GtkTreeSelection* sel = gtk_tree_view_get_selection( GTK_TREE_VIEW(pv->tree_thumb) );
    gtk_tree_selection_set_mode( sel, GTK_SELECTION_MULTIPLE );

    GtkTreeViewColumn* column = gtk_tree_view_column_new( );
    gtk_tree_view_column_set_resizable(column, FALSE);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);

    GtkCellRenderer* renderer_text = gtk_cell_renderer_text_new( );
    gtk_tree_view_column_pack_start( column, renderer_text, FALSE );
    gtk_tree_view_column_set_cell_data_func( column, renderer_text,
            viewer_thumblist_render_textcell, NULL, NULL );

    GtkCellRenderer* renderer_pixbuf = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start( column, renderer_pixbuf, TRUE );

    gtk_tree_view_column_set_attributes( column, renderer_pixbuf, "pixbuf", 0, NULL );
    gtk_tree_view_append_column( GTK_TREE_VIEW(pv->tree_thumb), column);

    GtkListStore* store_thumbs = gtk_list_store_new( 1, GDK_TYPE_PIXBUF );
    gtk_tree_view_set_model( GTK_TREE_VIEW(pv->tree_thumb), GTK_TREE_MODEL(store_thumbs) );
    g_object_unref( store_thumbs );

    pv->swindow_tree = gtk_scrolled_window_new( NULL, NULL );
    gtk_container_add( GTK_CONTAINER(pv->swindow_tree), pv->tree_thumb );
    GtkAdjustment* vadj_thumb =
            gtk_scrolled_window_get_vadjustment( GTK_SCROLLED_WINDOW(pv->swindow_tree) );

    GtkWidget* paned = gtk_paned_new( GTK_ORIENTATION_HORIZONTAL );
    gtk_paned_pack1( GTK_PANED(paned), swindow, TRUE, TRUE );
    gtk_paned_pack2( GTK_PANED(paned), pv->swindow_tree, FALSE, FALSE );
    gtk_paned_set_position( GTK_PANED(paned), 760 );

//hbox erstellen
    GtkWidget* hbox = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 0 );

    gtk_box_pack_start( GTK_BOX(hbox), vbox_tools, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(hbox), paned, TRUE, TRUE, 0 );

    gtk_container_add( GTK_CONTAINER(pv->vf), hbox );

    //Popover für Annot-Inhalte
    pv->annot_pop = gtk_popover_new( pv->layout );
    pv->annot_label = gtk_label_new( NULL );
    gtk_widget_show( pv->annot_label );
    gtk_container_add( GTK_CONTAINER(pv->annot_pop), pv->annot_label );
    gtk_popover_set_modal( GTK_POPOVER(pv->annot_pop), FALSE );

    //popover mit textview
    pv->annot_pop_edit = gtk_popover_new( pv->layout );
    pv->annot_textview = gtk_text_view_new( );
    gtk_widget_show( pv->annot_textview );
    gtk_container_add( GTK_CONTAINER(pv->annot_pop_edit), pv->annot_textview );
    gtk_popover_set_modal( GTK_POPOVER(pv->annot_pop_edit), TRUE );
    g_signal_connect( pv->annot_pop_edit, "closed", G_CALLBACK(viewer_annot_edit_closed), pv );

    gtk_widget_show_all( pv->vf );
    gtk_widget_hide( pv->swindow_tree );

    pv->gdk_window = gtk_widget_get_window( pv->vf );
    GdkDisplay* display = gdk_window_get_display( pv->gdk_window );
    pv->cursor_default = gdk_cursor_new_from_name( display, "default" );
    pv->cursor_text = gdk_cursor_new_from_name( display, "text" );
    pv->cursor_vtext = gdk_cursor_new_from_name( display, "vertical-text" );
    pv->cursor_grab = gdk_cursor_new_from_name( display, "grab" );
    pv->cursor_annot = gdk_cursor_new_from_name( display, "pointer" );

//Signale Menu
#ifdef VIEWER
    //öffnen
    g_signal_connect( item_oeffnen, "activate", G_CALLBACK(cb_datei_oeffnen), pv );
    //öffnen
    g_signal_connect( pv->item_schliessen, "activate", G_CALLBACK(cb_datei_schliessen), pv );
    //beenden
    g_signal_connect_swapped( item_beenden, "activate", G_CALLBACK(viewer_save_and_close), pv );
#endif // VIEWER
    //Seiten kopieren
    g_signal_connect( pv->item_kopieren, "activate", G_CALLBACK(cb_seiten_kopieren),
            pv );
    //Seiten ausschneiden
    g_signal_connect( pv->item_ausschneiden, "activate",
            G_CALLBACK(cb_seiten_ausschneiden), pv );
    //löschen
    g_signal_connect( pv->item_loeschen, "activate", G_CALLBACK(cb_pv_seiten_loeschen), pv );
    //einfügen
    g_signal_connect( pv->item_einfuegen, "activate", G_CALLBACK(cb_pv_seiten_einfuegen), pv );
    //drehen
    g_signal_connect( pv->item_drehen, "activate", G_CALLBACK(cb_pv_seiten_drehen), pv );
    //OCR
    g_signal_connect( pv->item_ocr, "activate", G_CALLBACK(cb_pv_seiten_ocr), pv );
    //Text kopieren
    g_signal_connect( pv->item_copy, "activate", G_CALLBACK(cb_pv_copy_text), pv );

    //signale des entry
    g_signal_connect( pv->entry, "activate",
            G_CALLBACK(cb_viewer_page_entry_activated), (gpointer) pv );

    //Textsuche-entry
    g_signal_connect( pv->entry_search, "activate",
            G_CALLBACK(cb_viewer_text_search), pv );
    g_signal_connect( pv->button_nachher, "clicked",
            G_CALLBACK(cb_viewer_text_search), pv );
    g_signal_connect( pv->button_vorher, "clicked",
            G_CALLBACK(cb_viewer_text_search), pv );
    g_signal_connect_swapped( gtk_entry_get_buffer( GTK_ENTRY(pv->entry_search) ),
            "deleted-text", G_CALLBACK(cb_viewer_text_search_entry_buffer_changed), pv );
    g_signal_connect_swapped( gtk_entry_get_buffer( GTK_ENTRY(pv->entry_search) ),
            "inserted-text", G_CALLBACK(cb_viewer_text_search_entry_buffer_changed), pv );

// Signale Toolbox
    g_signal_connect( pv->button_speichern, "clicked", G_CALLBACK(cb_pv_speichern),
            pv );
    g_signal_connect( button_print, "clicked", G_CALLBACK(viewer_cb_print), pv );
    g_signal_connect( button_thumb, "toggled", G_CALLBACK(cb_tree_thumb),
            pv );
    g_signal_connect( pv->button_zeiger, "toggled",
            G_CALLBACK(cb_viewer_auswahlwerkzeug), (gpointer) pv );
    g_signal_connect( button_highlight, "toggled",
            G_CALLBACK(cb_viewer_auswahlwerkzeug), (gpointer) pv );
    g_signal_connect( button_underline, "toggled",
            G_CALLBACK(cb_viewer_auswahlwerkzeug), (gpointer) pv );
    g_signal_connect( button_paint, "toggled",
            G_CALLBACK(cb_viewer_auswahlwerkzeug), (gpointer) pv );
#ifndef VIEWER
    //Anbindung löschen
    g_signal_connect( pv->button_anbindung, "clicked",
            G_CALLBACK(cb_viewer_loeschen_anbindung_button_clicked), pv );
#endif // VIEWER
    //und des vadjustments
    g_signal_connect_swapped( pv->v_adj, "value-changed",
            G_CALLBACK(viewer_render_sichtbare_seiten), pv );

    //vadjustment von thumbnail-leiste
    g_signal_connect_swapped( vadj_thumb, "value-changed",
            G_CALLBACK(viewer_render_sichtbare_thumbs), pv );

    //thumb-tree
    g_signal_connect( pv->tree_thumb, "row-activated",
            G_CALLBACK(cb_thumb_activated), pv );
    g_signal_connect( gtk_tree_view_get_selection( GTK_TREE_VIEW(pv->tree_thumb) ),
            "changed", G_CALLBACK(cb_thumb_sel_changed), pv );

    //Jetzt die Signale des layout verbinden
    g_signal_connect( pv->layout, "button-press-event",
            G_CALLBACK(cb_viewer_layout_press_button), (gpointer) pv );
    g_signal_connect( pv->layout, "button-release-event",
            G_CALLBACK(cb_viewer_layout_release_button), (gpointer) pv );
    g_signal_connect( pv->layout, "motion-notify-event",
            G_CALLBACK(cb_viewer_layout_motion_notify), (gpointer) pv );
    g_signal_connect( pv->layout, "key-press-event",
            G_CALLBACK(cb_viewer_swindow_key_press), (gpointer) pv );

    g_signal_connect( pv->vf, "motion-notify-event",
            G_CALLBACK(cb_viewer_motion_notify), (gpointer) pv );

    g_signal_connect_swapped( pv->vf, "delete-event",
            G_CALLBACK(viewer_save_and_close), (gpointer) pv );

    return;
}


PdfViewer*
viewer_start_pv( Projekt* zond )
{
    PdfViewer* pv = g_malloc0( sizeof( PdfViewer ) );

    pv->zond = zond;
    pv->zoom = g_settings_get_double( zond->settings, "zoom" );

    g_ptr_array_add( zond->arr_pv, pv );

    pv->arr_pages = g_ptr_array_new_with_free_func( g_free );

    //highlight Sentinel an den Anfang setzen
    pv->highlight.page[0] = -1;
    pv->anbindung.von.index = -1;
    pv->anbindung.bis.index = EOP + 1;

    pv->text_occ.arr_quad = g_array_new( FALSE, FALSE, sizeof( fz_quad ) );

    pv->arr_rendered = g_array_new( FALSE, FALSE, sizeof( RenderResponse ) );
    g_array_set_clear_func( pv->arr_rendered, (GDestroyNotify) viewer_free_render_response );

    g_mutex_init( &pv->mutex_arr_rendered );

    //  Fenster erzeugen und anzeigen
    viewer_einrichten_fenster( pv );

    return pv;
}


