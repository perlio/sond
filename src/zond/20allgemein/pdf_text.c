/*
zond (pdf_text.c) - Akten, Beweisstücke, Unterlagen
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

#include "../zond_pdf_document.h"

#include "../error.h"
#include "../global_types.h"

#include "../99conv/db_read.h"
#include "../99conv/general.h"
#include "../../misc.h"
#include "../99conv/baum.h"
#include "../99conv/pdf.h"

#include "../20allgemein/ziele.h"
#include "../20allgemein/oeffnen.h"

#include "../40viewer/document.h"
#include "../40viewer/render.h"


typedef struct _Pdf_Viewer PdfViewer;

typedef struct _PDF_Text_Occ
{
    gchar* rel_path;
    gint page;
    fz_quad quad;
    gchar* zeile;
} PDFTextOcc;


static gboolean
cb_window_textsearch_delete( GtkWidget* window, GdkEvent* event, gpointer data )
{
    g_array_unref( (GArray*) data );

    return FALSE;
}


static void
pdf_text_fuellen_fenster( Projekt* zond, GtkListBox* list_box, GArray* arr_pdf_text_occ )
{
    for ( gint i = 0; i < arr_pdf_text_occ->len; i++ )
    {
        GtkTextIter text_iter = { 0 };
        PDFTextOcc pdf_text_occ = { 0 };
        gchar* label_text = NULL;
        GtkWidget* text_view = NULL;

        pdf_text_occ = g_array_index( arr_pdf_text_occ, PDFTextOcc, i );

        label_text = g_strdup_printf( "<markup><small><u>%s, S. %i</u></small>"
                "</markup>\n", pdf_text_occ.rel_path, pdf_text_occ.page + 1 );

        //text_view erzeugen und einfügen
        text_view = gtk_text_view_new( );
        gtk_text_view_set_wrap_mode( GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD );
        gtk_text_view_set_editable( GTK_TEXT_VIEW(text_view), FALSE );
        GtkTextBuffer* buf = gtk_text_view_get_buffer( GTK_TEXT_VIEW(text_view) );
        gtk_text_buffer_get_start_iter( buf, &text_iter );
        gtk_text_buffer_insert_markup( buf, &text_iter, label_text, -1 );
        g_free( label_text );
        gtk_text_buffer_insert_markup( buf, &text_iter, pdf_text_occ.zeile, -1 );
        gtk_text_buffer_insert( buf, &text_iter, "\n", -1 );

        gtk_list_box_insert( list_box, text_view, -1 );
    }

    return;
}


static void
cb_button_pdf_popover( GtkButton* button, gpointer data )
{
    gtk_popover_popup( GTK_POPOVER((GtkWidget*) data) );

    return;
}


static void
cb_textsuche_changed( GtkListBox* box, GtkListBoxRow* row, gpointer data )
{
    gint index = 0;

    Projekt* zond = (Projekt*) data;

    gint node_id = 0;
    gchar* errmsg = NULL;

    GArray* arr_pdf_text_occ = g_object_get_data( G_OBJECT(box), "arr-pdf-text-occ" );

    index = gtk_list_box_row_get_index( row );

    PDFTextOcc pdf_text_occ = g_array_index( arr_pdf_text_occ, PDFTextOcc, index );

    //herausfinden, welche Anbuindung am besten paßt

    node_id = db_get_node_id_from_rel_path( zond, pdf_text_occ.rel_path, &errmsg );
    if ( node_id == 0 )
    {
        meldung( zond->app_window, "Fehler -\n\n"
                "Datei ", pdf_text_occ.rel_path, " nicht vorhanden", NULL );
        return;
    }
    else if ( node_id < 0 )
    {
        meldung( zond->app_window, "Fehler - \n\n"
                "Bei Aufruf db_get_node_id_from_rel_path:\n", errmsg, NULL );
        g_free( errmsg );

        return;
    }

    Anbindung anbindung = { 0 };
    anbindung.von.seite = pdf_text_occ.page;
    anbindung.bis.seite = pdf_text_occ.page;
    anbindung.von.index = pdf_text_occ.quad.ul.y;
    anbindung.bis.index = pdf_text_occ.quad.ll.y;

    gboolean kind = FALSE;

    node_id = ziele_abfragen_anker_rek( zond, node_id, anbindung, &kind, &errmsg );
    if ( node_id == -1 )
    {
        meldung( zond->app_window, "Fehler -\n\nBei Aufruf ziele_abfragen_anker_"
                "rek:\n", errmsg, NULL );
        g_free( errmsg );

        return;
    }

    //cursor dorthin setzen
    if ( !kind )
    {
        node_id = db_get_parent( zond, BAUM_INHALT, node_id, &errmsg );
        if ( node_id < 0 )
        {
            meldung( zond->app_window, "Fehler - \n\nBei Aufruf db_get_parent:\n",
                    errmsg, NULL );
            g_free( errmsg );

            return;
        }
    }

    GtkTreePath* path = baum_abfragen_path( zond->treeview[BAUM_INHALT], node_id );
    gtk_tree_view_expand_to_path( GTK_TREE_VIEW(zond->treeview[BAUM_INHALT]), path );
    gtk_tree_view_set_cursor( GTK_TREE_VIEW(zond->treeview[BAUM_INHALT]), path, NULL, FALSE );
    gtk_tree_path_free( path );

    return;
}


static void
cb_textsuche_act( GtkListBox* box, GtkListBoxRow* row, gpointer data )
{
    Projekt* zond = (Projekt*) data;

    gint rc = 0;
    gchar* errmsg = NULL;
    GArray* arr_pdf_text_occ = NULL;
    gint index = 0;
    PDFTextOcc pdf_text_occ = { 0 };
    PdfPos pos_pdf = { 0 };

    arr_pdf_text_occ = g_object_get_data( G_OBJECT(box), "arr-pdf-text-occ" );
    index = gtk_list_box_row_get_index( row );

    pdf_text_occ = g_array_index( arr_pdf_text_occ, PDFTextOcc, index );

    pos_pdf.seite = pdf_text_occ.page;
    pos_pdf.index = (gint) (pdf_text_occ.quad.ul.y);

    rc = oeffnen_datei( zond, pdf_text_occ.rel_path, NULL, &pos_pdf, &errmsg );
    if ( rc )
    {
        meldung( zond->app_window, "Fehler in Textsuche -\n\n"
                "Bei Aufruf oeffnen_datei:\n", errmsg, NULL );
        g_free( errmsg );

        return;
    }

    //gefundene Stelle markieren, wenn internalviewer
    //zuletzt geöffneter pv
    if ( g_settings_get_boolean( zond->settings, "internalviewer" ) )
    {
        PdfViewer* pv = g_ptr_array_index( zond->arr_pv, (zond->arr_pv->len) - 1 );

        pv->highlight[0] = pdf_text_occ.quad;
        //Sentinel!
        pv->highlight[1].ul.x = -1;

        //Trick: click_pdf_punkt mißbraucht, damit highlight richtiger Seite
        //zugeordnet wird - in echt nix geclickt!
        pv->click_pdf_punkt.seite = pos_pdf.seite;

        //draw-Befehl nicht notwendig, da main-loop noch nicht idle war und widgets
        //daher noch nicht dargestelt wurde
    }

    return;
}


static GtkWidget*
pdf_text_oeffnen_fenster( Projekt* zond, GPtrArray* arr_rel_path,
        GArray* arr_pdf_text_occ, gchar* search_text )
{
    //Fenster erzeugen
    GtkWidget* window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_default_size( GTK_WINDOW(window), 1000, 400 );
//    gtk_window_set_transient_for( GTK_WINDOW(window), GTK_WINDOW(zond->app_window) );

    GtkWidget* swindow = gtk_scrolled_window_new( NULL, NULL );
    GtkWidget* list_box = gtk_list_box_new( );
    gtk_list_box_set_selection_mode( GTK_LIST_BOX(list_box),
            GTK_SELECTION_MULTIPLE );
    gtk_list_box_set_activate_on_single_click( GTK_LIST_BOX(list_box), FALSE );

    g_object_set_data( G_OBJECT(window), "list-box", list_box );
    g_object_set_data( G_OBJECT(list_box), "arr-pdf-text-occ", arr_pdf_text_occ );

    gtk_container_add( GTK_CONTAINER(swindow), list_box );
    gtk_container_add( GTK_CONTAINER(window), swindow );

    //Headerbar erzeugen
    GtkWidget* headerbar = gtk_header_bar_new( );
    gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(headerbar), ":minimize,close");
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
    gchar* title = g_strconcat( "Suchtext: ", search_text, NULL );
    gtk_header_bar_set_title( GTK_HEADER_BAR(headerbar), title );
    g_free( title );

    gtk_window_set_titlebar( GTK_WINDOW(window), headerbar );

    //popover
    GtkWidget* button_sql = gtk_button_new_with_label( "Duchsuchte PDFs" );
    GtkWidget* popover = gtk_popover_new( button_sql );

    gchar* rel_path = NULL;
    gchar* text = g_strdup( "" );
    for ( gint i = 0; i < arr_rel_path->len; i++ )
    {
        rel_path = g_ptr_array_index( arr_rel_path, i );

        text = add_string( text, g_strdup( rel_path ) );
        if ( i < arr_rel_path->len - 1 )
                text = add_string( text, g_strdup( "\n" ) );
    }

    GtkWidget* label = gtk_label_new( text );
    g_free( text );
    gtk_widget_show_all( label );
    gtk_header_bar_pack_end( GTK_HEADER_BAR(headerbar), button_sql );

    gtk_container_add( GTK_CONTAINER(popover), label );

    g_signal_connect( list_box, "row-activated", G_CALLBACK(cb_textsuche_act),
            (gpointer) zond );
    g_signal_connect( list_box, "row-selected",
            G_CALLBACK(cb_textsuche_changed), (gpointer) zond );
    g_signal_connect( button_sql, "clicked", G_CALLBACK(cb_button_pdf_popover),
            (gpointer) popover );

    g_signal_connect( window, "delete-event",
            G_CALLBACK(cb_window_textsearch_delete), arr_pdf_text_occ );

    return window;
}


gint
pdf_text_anzeigen_ergebnisse( Projekt* zond, gchar* search_text, GPtrArray* arr_rel_path,
        GArray* arr_pdf_text_occ, gchar** errmsg )
{
    GtkWidget* window = pdf_text_oeffnen_fenster( zond, arr_rel_path, arr_pdf_text_occ, search_text );

    GtkListBox* list_box = g_object_get_data( G_OBJECT(window), "list-box" );

    pdf_text_fuellen_fenster( zond, list_box, arr_pdf_text_occ );

    gtk_widget_show_all( window );

    return 0;
}


static void
pdf_text_occ_free( gpointer data )
{
    PDFTextOcc* pdf_text_occ = (PDFTextOcc*) data;

    g_free( pdf_text_occ->rel_path );
    g_free( pdf_text_occ->zeile );

    return;
}


static gint
pdf_textsuche_pdf( Projekt* zond, const gchar* rel_path, const gchar* search_text,
        GArray* arr_pdf_text_occ, InfoWindow* info_window, gchar** errmsg )
{
    gint rc = 0;
    DisplayedDocument* dd = NULL;
    GPtrArray* arr_pdf_document_pages = NULL;
    fz_context* ctx = NULL;

    dd = document_new_displayed_document( rel_path, NULL, errmsg );
    if ( !dd ) ERROR_PAO( "pdf_textsuche_pdf" )

    ctx = zond_pdf_document_get_ctx( dd->zond_pdf_document );
    arr_pdf_document_pages = zond_pdf_document_get_arr_pages( dd->zond_pdf_document );

    for ( gint i = 0; i < arr_pdf_document_pages->len; i++ )
    {
        gint anzahl = 0;
        fz_quad quads[100] = { 0 };

        PdfDocumentPage* pdf_document_page = g_ptr_array_index( arr_pdf_document_pages, i );

        if ( pdf_document_page->stext_page->first_block == NULL )
        {
            zond_pdf_document_mutex_lock( dd->zond_pdf_document );

            if ( !fz_display_list_is_empty( ctx, pdf_document_page->display_list ) )
            {
                rc = render_display_list_to_stext_page( ctx, pdf_document_page, errmsg );
                zond_pdf_document_mutex_unlock( dd->zond_pdf_document );
                if ( rc )
                {
                    document_free_displayed_documents( dd );
                    ERROR_PAO( "render_display_list_to_stext_page" )
                }
            }
            else //wenn display_list noch nicht erzeugt, dann direkt aus page erzeugen
            {
                rc = pdf_render_stext_page_direct( pdf_document_page, errmsg );
                zond_pdf_document_mutex_unlock( dd->zond_pdf_document );
                if ( rc )
                {
                    document_free_displayed_documents( dd );
                    ERROR_PAO( "pdf_render_stext_page_direct" )
                }
            }
        }

        anzahl = fz_search_stext_page( ctx,
                pdf_document_page->stext_page, search_text, quads, 99 );

        for ( gint u = 0; u < anzahl; u++ )
        {
            gchar* text_tmp = NULL;
            PDFTextOcc pdf_text_occ = { 0 };

            pdf_text_occ.rel_path = g_strdup( rel_path );
            pdf_text_occ.page = i;
            pdf_text_occ.quad = quads[u];

            //Text der gesamten line
            /*  ToDo: fz_try!!! */
            //line herausfinden, in der sich rect befindet
            fz_buffer* buf = NULL;
            buf = fz_new_buffer( ctx, 128 );

            gboolean found_line = FALSE;

            fz_stext_block* block = NULL;
            fz_stext_line* line = NULL;
            fz_stext_char* ch = NULL;
            for ( block = pdf_document_page->stext_page->first_block; block; block = block->next )
            {
                if ( block->type != FZ_STEXT_BLOCK_TEXT ) continue;

                for (line = block->u.t.first_line; line; line = line->next )
                {
                    if ( fz_contains_rect( line->bbox,
                            fz_rect_from_quad( pdf_text_occ.quad ) ) )
                    {
                        gboolean marked_up = FALSE;

                        found_line = TRUE;
                        //falls Zeile drüber exisitiert:
                        if ( line->prev )
                        {
                            for (ch = line->prev->first_char; ch; ch = ch->next)
                                    fz_append_rune( ctx, buf, ch->c );
                            fz_append_byte( ctx, buf, 10 );
                        }
                        //die Zeile mit Fundort selbst
                        for (ch = line->first_char; ch; ch = ch->next)
                        {
                            gboolean inside = fz_contains_rect(
                                    fz_rect_from_quad( pdf_text_occ.quad ),
                                    fz_rect_from_quad( ch->quad ) );

                            if ( inside && !marked_up )
                            {
                                fz_append_string( ctx, buf, "<markup><i><b>" );
                                marked_up = TRUE;
                            }
                            else if ( !inside && marked_up )
                            {
                                fz_append_string( ctx, buf, "</b></i></markup>" );
                                marked_up = FALSE;
                            }

                            fz_append_rune( ctx, buf, ch->c );
                        }

                        //Falls Zeile mit gesuchtem Wort aufhört:
                        if ( marked_up ) fz_append_string( ctx, buf, "</b></i></markup>" );

                        //falls Zeile drunter exisitiert:
                        if ( line->next )
                        {
                            fz_append_byte( ctx, buf, 10 );
                            for (ch = line->next->first_char; ch; ch = ch->next)
                                    fz_append_rune( ctx, buf, ch->c );
                        }
                        break;
                    }
                    if ( found_line ) break;
                }
                if ( found_line ) break;
            }

            text_tmp = g_strdup( fz_string_from_buffer( ctx, buf ) );
            fz_drop_buffer( ctx, buf );

            g_strstrip( text_tmp );
            if ( *text_tmp == '\10' ) pdf_text_occ.zeile = g_strdup( text_tmp + 1 );
            else pdf_text_occ.zeile = g_strdup( text_tmp );
            g_free( text_tmp );

            if ( *(pdf_text_occ.zeile + strlen( pdf_text_occ.zeile )) == '\10' )
                    *(pdf_text_occ.zeile + strlen( pdf_text_occ.zeile ) - 1) = '\0';

            g_array_append_val( arr_pdf_text_occ, pdf_text_occ );
        }
    }

    document_free_displayed_documents( dd );

    return 0;
}


gint
pdf_textsuche( Projekt* zond, InfoWindow* info_window, GPtrArray* array_rel_path,
        const gchar* search_text, GArray** arr_pdf_text_occ, gchar** errmsg )
{
    gint rc = 0;
    gchar* rel_path = NULL;
    gchar* message = NULL;

    *arr_pdf_text_occ = g_array_new( FALSE, FALSE, sizeof( PDFTextOcc ) );
    g_array_set_clear_func( *arr_pdf_text_occ, (GDestroyNotify) pdf_text_occ_free );

    for ( gint i = 0; i < array_rel_path->len; i++ )
    {
        rel_path = g_ptr_array_index( array_rel_path, i );

        message = g_strconcat( "Suche in ", rel_path, NULL );
        info_window_set_message( info_window, message );
        g_free( message );
        while ( gtk_events_pending( ) ) gtk_main_iteration( );

        rc = pdf_textsuche_pdf( zond, rel_path, search_text, *arr_pdf_text_occ,
                info_window, errmsg );
        if ( rc )
        {
            g_array_free( *arr_pdf_text_occ, TRUE );

            if ( rc == -1 ) ERROR_PAO( "pdf_textsuche_pdf" )
            if ( rc == -2 ) return -2;
        }
    }

    return 0;
}


