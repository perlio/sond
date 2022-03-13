/*
zond (app_window.c) - Akten, Beweisstücke, Unterlagen
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

#include "../../misc.h"
#include "../global_types.h"

#include "../zond_dbase.h"
#include "../zond_treeview.h"
#include "../zond_treeviewfm.h"

#include "../20allgemein/project.h"
#include "../20allgemein/suchen.h"



static gboolean
cb_delete_event( GtkWidget* app_window, GdkEvent* event, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    Projekt* zond = (Projekt*) user_data;

    rc = projekt_schliessen( zond, &errmsg );
    if ( rc == -1 )
    {
        display_message( zond->app_window, "Fehler bei Schließen des Projekts -\n\n"
                "Bei Aufruf projekt_schliessen:\n", errmsg, NULL );
        g_free( errmsg );

        return TRUE;
    }
    else if ( rc == 1 ) return TRUE;

    gtk_widget_destroy( zond->app_window );

    pdf_drop_document( zond->ctx, zond->pv_clip );

    fz_drop_context( zond->ctx );

    g_ptr_array_unref( zond->arr_pv );

    g_free( zond );

    return TRUE;
}


static gboolean
cb_pao_button_event( GtkWidget* app_window, GdkEvent* event, gpointer data )
{
    ((Projekt*)data)->state = event->button.state;

    return FALSE;
}


static void
cb_text_buffer_changed( GtkTextBuffer* buffer, gpointer zond )
{
    g_object_set_data( G_OBJECT(gtk_text_view_get_buffer(
            ((Projekt*) zond)->textview )), "changed", GINT_TO_POINTER(1) );

    return;
}


static gboolean
cb_textview_focus_in( GtkWidget* textview, GdkEvent* event, gpointer user_data )
{
    Projekt* zond = (Projekt*) user_data;

    g_signal_handler_disconnect( zond->app_window, zond->key_press_signal );

    zond->text_buffer_changed_signal =
            g_signal_connect( gtk_text_view_get_buffer( GTK_TEXT_VIEW(textview) ),
            "changed", G_CALLBACK(cb_text_buffer_changed), (gpointer) zond );

    return FALSE;
}



gboolean
cb_key_press( GtkWidget* treeview, GdkEventKey* event, gpointer data )
{
    Projekt* zond = (Projekt*) data;

    if ( event->is_modifier || (event->state & GDK_CONTROL_MASK) ||
            (event->keyval < 0x21) ||
            (event->keyval > 0x7e) )
            return FALSE;

    gtk_popover_popup( GTK_POPOVER(zond->popover) );

    return FALSE;
}


static gboolean
cb_textview_focus_out( GtkWidget* textview, GdkEvent* event, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    Projekt* zond = (Projekt*) user_data;

    //key_press-event-signal einschalten
    zond->key_press_signal = g_signal_connect( zond->app_window,
            "key-press-event", G_CALLBACK(cb_key_press), zond );

    g_signal_handler_disconnect( gtk_text_view_get_buffer( GTK_TEXT_VIEW(textview) ),
            zond->text_buffer_changed_signal );
    zond->text_buffer_changed_signal = 0;

    GtkTextBuffer* buffer = gtk_text_view_get_buffer( GTK_TEXT_VIEW(textview) );

    if ( g_object_get_data( G_OBJECT(buffer), "changed" ) )
    {
        //inhalt textview abspeichern
        GtkTextIter start;
        GtkTextIter end;

        gtk_text_buffer_get_start_iter( buffer, &start );
        gtk_text_buffer_get_end_iter( buffer, &end );

        gchar* text = gtk_text_buffer_get_text( buffer, &start, &end, FALSE );

        gint node_id = GPOINTER_TO_INT(g_object_get_data( G_OBJECT(zond->textview),
                "node-id" ) );

        rc = zond_dbase_set_text( zond->dbase_zond->zond_dbase_work, node_id, text, &errmsg );
        g_free( text );
        if ( rc )
        {
            display_message( zond->app_window, "Fehler in cb_textview_focus_out:\n\n"
                    "Bei Aufruf zond_dbase_set_text:\n", errmsg, NULL );
            g_free( errmsg );

            return FALSE;
        }
    }

    gtk_widget_queue_draw( GTK_WIDGET(zond->treeview[BAUM_AUSWERTUNG]) );

    return FALSE;
}


static void
cb_entry_search( GtkWidget* entry, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    const gchar* text = NULL;
    gchar* search_text = NULL;

    Projekt* zond = (Projekt*) data;

    text = gtk_entry_get_text( GTK_ENTRY(entry) );

    if ( !text || strlen( text ) < 3 ) return;

    search_text = g_strconcat( "%", text, "%", NULL );
    rc = suchen_treeviews( zond, search_text, &errmsg );
    g_free( search_text );
    if ( rc )
    {
        display_message( zond->app_window, "Fehler Suche -\n\nBei Aufruf "
                "suchen_treeviews:\n", errmsg, NULL );
        g_free( errmsg );
    }

    gtk_popover_popdown( GTK_POPOVER(zond->popover) );

    return;
}


static gboolean
cb_focus_out( GtkWidget* treeview, GdkEvent* event, gpointer user_data )
{
    Projekt* zond = (Projekt*) user_data;

    zond->baum_active = KEIN_BAUM;

    Baum baum = (Baum) sond_treeview_get_id( SOND_TREEVIEW(treeview) );

    //cursor-changed-signal ausschalten
    if ( baum != BAUM_FS && zond->cursor_changed_signal )
    {
        g_signal_handler_disconnect( treeview, zond->cursor_changed_signal );
        zond->cursor_changed_signal = 0;
    }

    g_object_set( sond_treeview_get_cell_renderer_text( zond->treeview[baum] ),
            "editable", FALSE, NULL);

    zond->baum_prev = baum;

    return FALSE;
}


static gboolean
cb_focus_in( GtkWidget* treeview, GdkEvent* event, gpointer user_data )
{
    Projekt* zond = (Projekt*) user_data;

    zond->baum_active = (Baum) sond_treeview_get_id( SOND_TREEVIEW(treeview) );

    //cursor-changed-signal für den aktivierten treeview anschalten
    if ( zond->baum_active != BAUM_FS )
    {
        zond->cursor_changed_signal = g_signal_connect( treeview, "cursor-changed",
            G_CALLBACK(zond_treeview_cursor_changed), zond );

        g_signal_emit_by_name( treeview, "cursor-changed", user_data, NULL );
    }

    if ( zond->baum_active != zond->baum_prev )
    {
        //selection in "altem" treeview löschen
        gtk_tree_selection_unselect_all( zond->selection[zond->baum_prev] );
/* wennüberhaupt, dann in cb row_collapse oder _expand verschieben, aber eher besser so!!!
        //Cursor gewählter treeview selektieren
        GtkTreePath* path = NULL;
        GtkTreeViewColumn* focus_column = NULL;
        gtk_tree_view_get_cursor( GTK_TREE_VIEW(treeview), &path, &focus_column );
        if ( path )
        {
            gtk_tree_view_set_cursor( GTK_TREE_VIEW(treeview), path,
                    focus_column, FALSE );
            gtk_tree_path_free( path );
        }
*/
    }

    g_object_set( sond_treeview_get_cell_renderer_text( zond->treeview[zond->baum_active] ),
            "editable", TRUE, NULL);

    if ( zond->baum_active != BAUM_AUSWERTUNG )
            gtk_widget_set_sensitive( GTK_WIDGET(zond->textview), FALSE );

    return FALSE;
}


static void
treeviews_cb_editing_canceled( GtkCellRenderer* renderer,
                              gpointer data)
{
    Projekt* zond = (Projekt*) data;

    zond->key_press_signal = g_signal_connect( zond->app_window, "key-press-event",
            G_CALLBACK(cb_key_press), zond );

    return;
}


static void
treeviews_cb_editing_started( GtkCellRenderer* renderer, GtkEditable* editable,
                             const gchar* path,
                             gpointer data )
{
    Projekt* zond = (Projekt*) data;

    g_signal_handler_disconnect( zond->app_window, zond->key_press_signal );
    zond->key_press_signal = 0;

    return;
}


static void
init_treeviews( Projekt* zond )
{
    //der treeview
    zond->treeview[BAUM_FS] = SOND_TREEVIEW(zond_treeviewfm_new( zond ));
    zond->treeview[BAUM_INHALT] = SOND_TREEVIEW(zond_treeview_new( zond, (gint) BAUM_INHALT));
    zond->treeview[BAUM_AUSWERTUNG] = SOND_TREEVIEW(zond_treeview_new( zond, (gint) BAUM_AUSWERTUNG ));

    for ( Baum baum = BAUM_FS; baum < NUM_BAUM; baum++ )
    {
        //die Selection
        zond->selection[baum] = gtk_tree_view_get_selection(
                GTK_TREE_VIEW(zond->treeview[baum]) );

        //Text-Spalte wird editiert
        //Beginn
        g_signal_connect( sond_treeview_get_cell_renderer_text( zond->treeview[baum] ),
                "editing-started", G_CALLBACK(treeviews_cb_editing_started), zond );
        g_signal_connect( sond_treeview_get_cell_renderer_text( zond->treeview[baum] ),
                "editing-canceled", G_CALLBACK(treeviews_cb_editing_canceled), zond );

        //focus-in
        zond->treeview_focus_in_signal[baum] = g_signal_connect( zond->treeview[baum],
                "focus-in-event", G_CALLBACK(cb_focus_in), (gpointer) zond );
        g_signal_connect( zond->treeview[baum], "focus-out-event",
                G_CALLBACK(cb_focus_out), (gpointer) zond );
    }

    return;
}


void
init_app_window( Projekt* zond )
{
    GtkWidget* entry_search = NULL;

    //ApplicationWindow erzeugen
    zond->app_window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_default_size( GTK_WINDOW(zond->app_window), 800, 1000 );

    //vbox für Statuszeile im unteren Bereich
    GtkWidget* vbox = gtk_box_new( GTK_ORIENTATION_VERTICAL, 0 );
    gtk_container_add( GTK_CONTAINER(zond->app_window), vbox );

/*
**  oberer Teil der VBox  */
    //zuerst HBox
    zond->hpaned = gtk_paned_new( GTK_ORIENTATION_HORIZONTAL );
    gtk_paned_set_position( GTK_PANED(zond->hpaned), 400 );

    //jetzt in oberen Teil der vbox einfügen
    gtk_box_pack_start( GTK_BOX(vbox), zond->hpaned, TRUE, TRUE, 0 );

    //TreeView erzeugen und in das scrolled window
    init_treeviews( zond );

    //BAUM_FS
    GtkWidget* swindow_baum_fs = gtk_scrolled_window_new( NULL, NULL );
    gtk_container_add( GTK_CONTAINER(swindow_baum_fs), GTK_WIDGET(zond->treeview[BAUM_FS]) );
    gtk_paned_add1( GTK_PANED(zond->hpaned), swindow_baum_fs );

    GtkWidget* hpaned_inner = gtk_paned_new( GTK_ORIENTATION_HORIZONTAL );
    gtk_paned_add2( GTK_PANED(zond->hpaned), hpaned_inner );

    //BAUM_INHALT
    GtkWidget* swindow_baum_inhalt = gtk_scrolled_window_new( NULL, NULL );
    gtk_container_add( GTK_CONTAINER(swindow_baum_inhalt), GTK_WIDGET(zond->treeview[BAUM_INHALT]) );

    //BAUM_AUSWERTUNG
    //VPaned erzeugen
    GtkWidget* paned_baum_auswertung = gtk_paned_new( GTK_ORIENTATION_VERTICAL );
    gtk_paned_set_position( GTK_PANED(paned_baum_auswertung), 500);

    GtkWidget* swindow_treeview_auswertung = gtk_scrolled_window_new( NULL, NULL );
    gtk_container_add( GTK_CONTAINER(swindow_treeview_auswertung), GTK_WIDGET(zond->treeview[BAUM_AUSWERTUNG]) );

    gtk_paned_pack1( GTK_PANED(paned_baum_auswertung), swindow_treeview_auswertung, TRUE, TRUE);

    //in die untere Hälfte des vpaned kommt text_view in swindow
    //Scrolled Window zum Anzeigen erstellen
    GtkWidget* swindow_textview = gtk_scrolled_window_new( NULL, NULL );
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(swindow_textview),
            GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_paned_pack2( GTK_PANED(paned_baum_auswertung), swindow_textview, TRUE, TRUE );

    //text_view erzeugen
    zond->textview = GTK_TEXT_VIEW(gtk_text_view_new( ));
    gtk_text_view_set_wrap_mode( zond->textview, GTK_WRAP_WORD );
    gtk_text_view_set_accepts_tab( zond->textview, FALSE );
    gtk_widget_set_sensitive( GTK_WIDGET(zond->textview), FALSE );

    //Und dann in untere Hälfte des übergebenen vpaned reinpacken
    gtk_container_add( GTK_CONTAINER(swindow_textview),
            GTK_WIDGET(zond->textview) );

    //Zum Start: links BAUM_INHALT, rechts BAUM_AUSWERTUNG
    gtk_paned_pack1( GTK_PANED(hpaned_inner), swindow_baum_inhalt, TRUE, TRUE );
    gtk_paned_pack2( GTK_PANED(hpaned_inner), paned_baum_auswertung, TRUE, TRUE );

    //Hört die Signale
    g_signal_connect( zond->textview, "focus-in-event",
            G_CALLBACK(cb_textview_focus_in), (gpointer) zond );
    g_signal_connect( zond->textview, "focus-out-event",
            G_CALLBACK(cb_textview_focus_out), (gpointer) zond );

    g_signal_connect( zond->app_window, "button-press-event",
            G_CALLBACK(cb_pao_button_event), zond );
    g_signal_connect( zond->app_window, "button-release-event",
            G_CALLBACK(cb_pao_button_event), zond );

    zond->popover = gtk_popover_new( GTK_WIDGET(zond->treeview[BAUM_INHALT]) );
    entry_search = gtk_entry_new( );
    gtk_widget_show( entry_search );
    gtk_container_add( GTK_CONTAINER(zond->popover), entry_search );

    g_signal_connect( entry_search, "activate", G_CALLBACK(cb_entry_search), zond );

/*
**  untere Hälfte vbox: Status-Zeile  */
    //Label erzeugen
    zond->label_status = GTK_LABEL(gtk_label_new( "" ));
    gtk_label_set_xalign( zond->label_status, 0.02 );
    gtk_box_pack_end( GTK_BOX(vbox), GTK_WIDGET(zond->label_status), FALSE, FALSE, 0 );

    //Signal für App-Fenster schließen
    g_signal_connect( zond->app_window, "delete-event",
            G_CALLBACK(cb_delete_event), zond );

    return;
}


