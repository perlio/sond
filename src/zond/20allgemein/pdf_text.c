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

#include "project.h"

#include "../zond_pdf_document.h"
#include "../global_types.h"
#include "../zond_dbase.h"
#include "../zond_treeview.h"

#include "../20allgemein/ziele.h"
#include "../20allgemein/oeffnen.h"

#include "../40viewer/render.h"
#include "../40viewer/viewer.h"

#include "../99conv/general.h"
#include "../../misc.h"
#include "../99conv/pdf.h"



typedef struct _Pdf_Viewer PdfViewer;

typedef struct _PDF_Text_Occ
{
    gchar* file_part;
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
                "</markup>\n", pdf_text_occ.file_part, pdf_text_occ.page + 1 );

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
    gint node_id = 0;
    GError* error = NULL;
    gint rc = 0;
    gint pdf_root = 0;
    gboolean child = FALSE;
    Anbindung anbindung = { 0 };
    gint anchor_id = 0;
    gint baum_inhalt_file = 0;

    Projekt* zond = (Projekt*) data;

    GArray* arr_pdf_text_occ = g_object_get_data( G_OBJECT(box), "arr-pdf-text-occ" );

    index = gtk_list_box_row_get_index( row );

    PDFTextOcc pdf_text_occ = g_array_index( arr_pdf_text_occ, PDFTextOcc, index );

    //herausfinden, welche Anbindung am besten paßt
    //erst pdf_root herausfinden. dann anker_rek
    anbindung.von.seite = pdf_text_occ.page;
    anbindung.bis.seite = pdf_text_occ.page;
    anbindung.von.index = pdf_text_occ.quad.ul.y;
    anbindung.bis.index = pdf_text_occ.quad.ll.y;

    rc = zond_dbase_get_file_part_root( zond->dbase_zond->zond_dbase_work,
            pdf_text_occ.file_part, &pdf_root, &error );
    if ( rc )
    {
        display_message( zond->app_window, "Fehler Ermittlung root-node\n\n",
                error->message, NULL );
        g_error_free( error );

        return;
    }

    rc = ziele_abfragen_anker_rek( zond->dbase_zond->zond_dbase_work,
            anbindung, pdf_root, &anchor_id, &child, &error);
    if ( rc )
    {
        display_message( zond->app_window, "Fehler Abfrage\n\n",
                error->message, NULL );
        g_error_free( error );

        return;
    }

    if ( !child ) //heißt auch, daß anchor_id != pdf_root ist
    {
        gint rc = 0;
        gint parent_id = 0;

        rc = zond_dbase_get_parent( zond->dbase_zond->zond_dbase_work, anchor_id, &parent_id, &error );
        if ( rc )
        {
            display_message( zond->app_window, "Fehler - \n\nBei Aufruf zond_dbase_get_parent:\n",
                    error->message, NULL );
            g_error_free( error );

            return;
        }

        node_id = parent_id;
    }
    else node_id = anchor_id;

    //prüfen, ob Abschnitt angebunden ist (BAUM_INHALT_PDF_ABSCHNITT (link->)
    rc = zond_dbase_find_baum_inhalt_file( zond->dbase_zond->zond_dbase_work,
            node_id, &baum_inhalt_file, NULL, NULL, &error );
    if ( rc )
    {
        display_message( zond->app_window, "Fehler Abfrage\n\n",
                error->message, NULL );
        g_error_free( error );

        return;
    }

    if ( !baum_inhalt_file ) return;
    else node_id = baum_inhalt_file; //weil ja nur hierüber angebunden, nicht als Baum_inhalt_pdf_abschnitt

    //cursor dorthin setzen
    GtkTreePath* path = zond_treeview_get_path( zond->treeview[BAUM_INHALT], node_id );
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

    rc = zond_treeview_oeffnen_internal_viewer( zond, pdf_text_occ.file_part, NULL, &pos_pdf, &errmsg );
    if ( rc )
    {
        display_message( zond->app_window, "Fehler in Textsuche -\n\n",
                errmsg, NULL );
        g_free( errmsg );

        return;
    }

    PdfViewer* pv = g_ptr_array_index( zond->arr_pv, (zond->arr_pv->len) - 1 );

    pv->highlight.page[0] = pdf_text_occ.page;
    pv->highlight.quad[0] = pdf_text_occ.quad;
    //Sentinel!
    pv->highlight.page[1] = -1;

    //draw-Befehl nicht notwendig, da main-loop noch nicht idle war und widgets
    //daher noch nicht dargestelt wurde

    return;
}


static GtkWidget*
pdf_text_oeffnen_fenster( Projekt* zond, GPtrArray* arr_file_part,
        GArray* arr_pdf_text_occ, gchar* search_text )
{
    gchar* title = NULL;
    GtkWidget* window = NULL;
    GtkWidget* listbox = NULL;
    GtkWidget* headerbar = NULL;

    title = g_strconcat( "Suchtext: ", search_text, NULL );
    window = result_listbox_new( NULL, title, GTK_SELECTION_MULTIPLE );
    g_free( title );

    listbox = (GtkWidget*) g_object_get_data( G_OBJECT(window), "listbox" );
    g_object_set_data( G_OBJECT(listbox), "arr-pdf-text-occ", arr_pdf_text_occ );

    //popover
    GtkWidget* button_sql = gtk_button_new_with_label( "Duchsuchte PDFs" );
    GtkWidget* popover = gtk_popover_new( button_sql );

    gchar* file_part = NULL;
    gchar* text = g_strdup( "" );
    for ( gint i = 0; i < arr_file_part->len; i++ )
    {
        file_part = g_ptr_array_index( arr_file_part, i );

        text = add_string( text, g_strdup( file_part ) );
        if ( i < arr_file_part->len - 1 )
                text = add_string( text, g_strdup( "\n" ) );
    }

    GtkWidget* label = gtk_label_new( text );
    g_free( text );

    headerbar = (GtkWidget*) g_object_get_data( G_OBJECT(window), "headerbar" );
    gtk_header_bar_pack_end( GTK_HEADER_BAR(headerbar), button_sql );

    gtk_container_add( GTK_CONTAINER(popover), label );

    gtk_widget_show_all( window );

    g_signal_connect( listbox, "row-activated", G_CALLBACK(cb_textsuche_act),
            (gpointer) zond );
    g_signal_connect( listbox, "row-selected",
            G_CALLBACK(cb_textsuche_changed), (gpointer) zond );
    g_signal_connect( button_sql, "clicked", G_CALLBACK(cb_button_pdf_popover),
            (gpointer) popover );

    g_signal_connect( window, "delete-event",
            G_CALLBACK(cb_window_textsearch_delete), arr_pdf_text_occ );

    return window;
}


gint
pdf_text_anzeigen_ergebnisse( Projekt* zond, gchar* search_text, GPtrArray* arr_file_part,
        GArray* arr_pdf_text_occ, gchar** errmsg )
{
    GtkWidget* window = pdf_text_oeffnen_fenster( zond, arr_file_part, arr_pdf_text_occ, search_text );

    GtkListBox* list_box = g_object_get_data( G_OBJECT(window), "listbox" );

    pdf_text_fuellen_fenster( zond, list_box, arr_pdf_text_occ );

    gtk_widget_show_all( window );

    return 0;
}


static void
pdf_text_occ_free( gpointer data )
{
    PDFTextOcc* pdf_text_occ = (PDFTextOcc*) data;

    g_free( pdf_text_occ->file_part );
    g_free( pdf_text_occ->zeile );

    return;
}


static gint
pdf_textsuche_pdf( Projekt* zond, const gchar* file_part, const gchar* search_text,
        GArray* arr_pdf_text_occ, InfoWindow* info_window, gchar** errmsg )
{
    gint rc = 0;
    ZondPdfDocument* zond_pdf_document = NULL;
    GPtrArray* arr_pdf_document_pages = NULL;
    fz_context* ctx = NULL;

    zond_pdf_document = zond_pdf_document_open( file_part, 0, -1, errmsg );
    if ( !zond_pdf_document ) ERROR_S

    ctx = zond_pdf_document_get_ctx( zond_pdf_document );
    arr_pdf_document_pages = zond_pdf_document_get_arr_pages( zond_pdf_document );

    for ( gint i = 0; i < arr_pdf_document_pages->len; i++ )
    {
        gint anzahl = 0;
        fz_quad quads[100] = { 0 };

        PdfDocumentPage* pdf_document_page = g_ptr_array_index( arr_pdf_document_pages, i );

        rc = viewer_render_stext_page_fast( ctx, pdf_document_page, errmsg );
        if ( rc ) ERROR_S

        anzahl = fz_search_stext_page( ctx,
                pdf_document_page->stext_page, search_text, NULL, quads, 99 );

        for ( gint u = 0; u < anzahl; u++ )
        {
            gchar* text_tmp = NULL;
            PDFTextOcc pdf_text_occ = { 0 };

            pdf_text_occ.file_part = g_strdup( file_part );
            pdf_text_occ.page = i;
            pdf_text_occ.quad = quads[u];

            //Text der gesamten line
            //line herausfinden, in der sich rect befindet
            fz_buffer* buf = NULL;
            fz_try( ctx ) buf = fz_new_buffer( ctx, 128 );
            fz_catch( ctx )
            {
                g_free( pdf_text_occ.file_part );
                ERROR_MUPDF( "fz_new_buffer" )
            }

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
                                    fz_append_rune( ctx, buf, ch->c ); //keine exception, die nur bei shared buffer
                            fz_append_byte( ctx, buf, 10 ); //dto.
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

    zond_pdf_document_close( zond_pdf_document );

    return 0;
}


gint
pdf_textsuche( Projekt* zond, InfoWindow* info_window, GPtrArray* arr_file_part,
        const gchar* search_text, GArray** arr_pdf_text_occ, gchar** errmsg )
{
    *arr_pdf_text_occ = g_array_new( FALSE, FALSE, sizeof( PDFTextOcc ) );
    g_array_set_clear_func( *arr_pdf_text_occ, (GDestroyNotify) pdf_text_occ_free );

    for ( gint i = 0; i < arr_file_part->len; i++ )
    {
        gchar* file_part = NULL;
        gint rc = 0;

        file_part = g_ptr_array_index( arr_file_part, i );

        info_window_set_message( info_window, file_part );

        //prüfen, ob in Viewer geöffnet
        if ( zond_pdf_document_is_open( file_part ) )
        {
            info_window_set_message( info_window, "... in Viewer geöffnet - übersprungen" );
            continue;
        }

        rc = pdf_textsuche_pdf( zond, file_part, search_text, *arr_pdf_text_occ,
                info_window, errmsg );
        if ( rc )
        {
            g_array_unref( *arr_pdf_text_occ );
            ERROR_S
        }
    }

    return 0;
}


