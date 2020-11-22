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

#include "../error.h"
#include "../global_types.h"

#include "../99conv/db_read.h"
#include "../99conv/general.h"
#include "../../misc.h"
#include "../99conv/baum.h"
#include "../99conv/mupdf.h"
#include "../99conv/pdf.h"

#include "../20allgemein/ziele.h"
#include "../20allgemein/oeffnen.h"

#include "../40viewer/document.h"
#include "../40viewer/render.h"

#include <gtk/gtk.h>
#include <mupdf/fitz.h>

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
    GtkListBox* list_box = g_object_get_data( G_OBJECT(window), "list-box" );
    GArray* arr_pdf_text_occ = g_object_get_data( G_OBJECT(list_box),
            "arr-pdf-text-occ" );
    g_array_free( arr_pdf_text_occ, TRUE );

    return FALSE;
}


static void
pdf_text_fuellen_fenster( Projekt* zond, GtkWidget* window, gint index )
{
    GtkListBox* list_box = g_object_get_data( G_OBJECT(window), "list-box" );
    GArray* arr_pdf_text_occ =
            g_object_get_data( G_OBJECT(list_box), "arr-pdf-text-occ" );

    PDFTextOcc pdf_text_occ = g_array_index( arr_pdf_text_occ, PDFTextOcc, index );

	//label erstellen
    gchar* label_text = g_strdup_printf( "%30s, S. %3i: %30s",
            pdf_text_occ.rel_path, pdf_text_occ.page + 1, pdf_text_occ.zeile );

    GtkWidget* label = gtk_label_new( label_text );
    g_object_set_data( G_OBJECT(label), "index", GINT_TO_POINTER(index) );
    gtk_label_set_xalign( GTK_LABEL(label), 0 );
    gtk_widget_show_all( label );

    gtk_list_box_insert( list_box, label, -1 );
    g_free( label_text );

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
    Projekt* zond = (Projekt*) data;

    gint node_id = 0;
    gchar* errmsg = NULL;

    GArray* arr_pdf_text_occ = g_object_get_data( G_OBJECT(box), "arr-pdf-text-occ" );
    GtkWidget* label = gtk_bin_get_child( GTK_BIN(row) );
    gint index = GPOINTER_TO_INT(g_object_get_data( G_OBJECT(label), "index" ));

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
    gtk_tree_view_expand_to_path( zond->treeview[BAUM_INHALT], path );
    gtk_tree_view_set_cursor( zond->treeview[BAUM_INHALT], path, NULL, FALSE );
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
    GtkWidget* label = gtk_bin_get_child( GTK_BIN(row) );
    index = GPOINTER_TO_INT(g_object_get_data( G_OBJECT(label), "index" ));

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

    g_object_set_data( G_OBJECT(window), "zond", zond );
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

    gtk_widget_show_all( window );

    g_signal_connect( list_box, "row-activated", G_CALLBACK(cb_textsuche_act),
            (gpointer) zond );
    g_signal_connect( list_box, "row-selected",
            G_CALLBACK(cb_textsuche_changed), (gpointer) zond );
    g_signal_connect( button_sql, "clicked", G_CALLBACK(cb_button_pdf_popover),
            (gpointer) popover );

    g_signal_connect( window, "delete-event",
            G_CALLBACK(cb_window_textsearch_delete), zond );

    return window;
}


gint
pdf_text_anzeigen_ergebnisse( Projekt* zond, gchar* search_text, GPtrArray* arr_rel_path,
        GArray* arr_pdf_text_occ, gchar** errmsg )
{
    GtkWidget* window = pdf_text_oeffnen_fenster( zond, arr_rel_path, arr_pdf_text_occ, search_text );

    for ( gint i = 0; i < arr_pdf_text_occ->len; i++ )
            pdf_text_fuellen_fenster( zond, window, i );

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
    DisplayedDocument* dd = NULL;

    dd = document_new_displayed_document( zond, rel_path, NULL, errmsg );
    if ( !dd ) ERROR_PAO( "pdf_textsuche_pdf" )

    gint rc = 0;
    fz_context* ctx = dd->document->ctx;

    for ( gint i = 0; i < dd->document->pages->len; i++ )
    {
        gint anzahl = 0;
        fz_quad quads[100] = { 0 };

        if ( DOCUMENT_PAGE(i)->stext_page->first_block == NULL )
        {
            if ( !fz_display_list_is_empty( ctx, DOCUMENT_PAGE(i)->display_list ) )
            {
                rc = render_display_list_to_stext_page( ctx, DOCUMENT_PAGE(i), errmsg );
                if ( rc )
                {
                    document_free_displayed_documents( zond, dd );
                    ERROR_PAO( "render_display_list_to_stext_page" )
                }
            }
            else //wenn display_list noch nicht erzeugt, dann direkt aus page erzeugen
            {
                g_mutex_lock( &dd->document->mutex_doc );
                rc = pdf_render_stext_page_direct( DOCUMENT_PAGE(i), errmsg );
                g_mutex_unlock( &dd->document->mutex_doc );
                if ( rc )
                {
                    document_free_displayed_documents( zond, dd );
                    ERROR_PAO( "pdf_render_stext_page_direct" )
                }
            }
        }

        anzahl = fz_search_stext_page( ctx,
                DOCUMENT_PAGE(i)->stext_page, search_text, quads, 99 );

        for ( gint u = 0; u < anzahl; u++ )
        {
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
            for ( block = DOCUMENT_PAGE(i)->stext_page->first_block; block; block = block->next )
            {
                if ( block->type != FZ_STEXT_BLOCK_TEXT ) continue;

                for (line = block->u.t.first_line; line; line = line->next )
                {
                    if ( fz_contains_rect( line->bbox,
                            fz_rect_from_quad( pdf_text_occ.quad ) ) )
                    {
                        found_line = TRUE;
                        //falls Zeile drüber exisitiert:
                        if ( line->prev )
                                for (ch = line->prev->first_char; ch; ch = ch->next)
                                fz_append_rune( ctx, buf, ch->c );
                        //die Zeile mit Fundort selbst
                        for (ch = line->first_char; ch; ch = ch->next)
                                fz_append_rune( ctx, buf, ch->c );
                        //falls Zeile drunter exisitiert:
                        if ( line->next )
                                for (ch = line->next->first_char; ch; ch = ch->next)
                                fz_append_rune( ctx, buf, ch->c );
                        break;
                    }
                    if ( found_line ) break;
                }
                if ( found_line ) break;
            }

            pdf_text_occ.zeile = g_strdup( fz_string_from_buffer( ctx, buf ) );
            fz_drop_buffer( ctx, buf );

            g_array_append_val( arr_pdf_text_occ, pdf_text_occ );
        }
    }

    document_free_displayed_documents( zond, dd );

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


