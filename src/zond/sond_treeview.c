/*
sond (sond_treeview.c) - Akten, Beweisst�cke, Unterlagen
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

#include "sond_treeview.h"

#include "../misc.h"


typedef struct
{
    GtkCellRenderer* renderer_icon;
    GtkCellRenderer* renderer_text;
    gulong signal_key;
    gint id;
    GtkWidget* contextmenu;
} SondTreeviewPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondTreeview, sond_treeview, GTK_TYPE_TREE_VIEW)


static gboolean
sond_treeview_show_popupmenu( SondTreeview* stv, GdkEventButton* event,
        GtkMenu* contextmenu )
{
    GtkTreePath* path = NULL;
    gboolean ret = FALSE;

   //wenn was anderes als Rechtsklick:
    if ( ((event->button) != 3) || (event->type != GDK_BUTTON_PRESS) ) return FALSE;

    gtk_tree_view_get_path_at_pos( GTK_TREE_VIEW(stv), event->x, event->y,
            &path, NULL, NULL, NULL );
    if ( !path )
    {
        GtkTreeIter iter = { 0 };

        if ( gtk_tree_model_get_iter_first(
                gtk_tree_view_get_model( GTK_TREE_VIEW(stv) ), &iter ) ) return FALSE;
        else if ( !gtk_widget_has_focus( GTK_WIDGET(stv) ) )
                gtk_widget_grab_focus( GTK_WIDGET(stv) );
    }
    else
    {
        if ( !gtk_widget_has_focus( GTK_WIDGET(stv) ) )
        {
            //zun�chst cursor setzen, damit der bei Focus-Wechsel direkt markiet wird
            gtk_tree_view_set_cursor( GTK_TREE_VIEW(stv), path, NULL, FALSE );

           //focus auf neuen Baum...
            gtk_widget_grab_focus( GTK_WIDGET(stv) );
        }
        //angeklickte schon markiert: dann kein default-handler (selection soll bleiben)
        else if ( gtk_tree_selection_path_is_selected( gtk_tree_view_get_selection( GTK_TREE_VIEW(stv) ), path ) ) ret = TRUE;

        gtk_tree_path_free( path );
    }

    gtk_menu_popup_at_pointer( contextmenu, NULL );

    return ret;
}


static void
sond_treeview_class_init( SondTreeviewClass* klass )
{
    klass->clipboard = g_malloc0( sizeof( Clipboard ) );
    klass->clipboard->arr_ref = g_ptr_array_new_with_free_func( (GDestroyNotify) gtk_tree_row_reference_free );
    //class_finalize mu� nicht definiert werden -
    //statisch registrierte Klasse wird zur Laufzeit niemals finalisiert!

    klass->render_text_cell = NULL;
    klass->text_edited = NULL;
    klass->callback_key_press_event = NULL;
    klass->callback_key_press_event_func_data = NULL;

    return;
}


static void
renderer_text_editing_canceled( GtkCellRenderer* renderer,
                              gpointer data)
{
    SondTreeview* stv = (SondTreeview*) data;
    SondTreeviewClass* klass = SOND_TREEVIEW_GET_CLASS(stv);
    SondTreeviewPrivate* stv_priv = sond_treeview_get_instance_private( stv );

    if ( klass->callback_key_press_event )
            stv_priv->signal_key = g_signal_connect( stv, "key-press-event",
            G_CALLBACK(klass->callback_key_press_event),
            klass->callback_key_press_event_func_data );

    return;
}


static void
sond_treeview_text_edited( GtkCellRenderer* cell, gchar* path_string, gchar* new_text,
        gpointer data )
{
    GtkTreeIter iter = { 0 };

    SondTreeview* stv = (SondTreeview*) data;
    SondTreeviewClass* klass = SOND_TREEVIEW_GET_CLASS(stv);
    SondTreeviewPrivate* stv_priv = sond_treeview_get_instance_private( stv );

    gtk_tree_model_get_iter_from_string( gtk_tree_view_get_model( GTK_TREE_VIEW(stv) ), &iter, path_string );

    if ( klass->text_edited ) klass->text_edited( stv, &iter, new_text );

    if ( klass->callback_key_press_event )
            stv_priv->signal_key = g_signal_connect( stv, "key-press-event",
            G_CALLBACK(klass->callback_key_press_event),
            klass->callback_key_press_event_func_data );

    return;
}


static void
renderer_text_editing_started( GtkCellRenderer* renderer, GtkEditable* editable,
                             const gchar* path,
                             gpointer data )
{
    SondTreeview* stv = (SondTreeview*) data;
    SondTreeviewPrivate* stv_priv = sond_treeview_get_instance_private( stv );

    if ( stv_priv->signal_key )
    {
        g_signal_handler_disconnect( stv, stv_priv->signal_key );
        stv_priv->signal_key = 0;
    }

    return;
}


static void
sond_treeview_grey_cut_cell( SondTreeview* stv, GtkTreeIter* iter )
{
    SondTreeviewPrivate* stv_priv = sond_treeview_get_instance_private( stv );
    Clipboard* clipboard = SOND_TREEVIEW_GET_CLASS( stv )->clipboard;

    if ( clipboard->tree_view == stv && clipboard->ausschneiden )
    {
        gboolean enthalten = FALSE;
        GtkTreePath* path = NULL;
        GtkTreePath* path_sel = NULL;

        path = gtk_tree_model_get_path( gtk_tree_view_get_model( GTK_TREE_VIEW(stv) ), iter );
        for ( gint i = 0; i < clipboard->arr_ref->len; i++ )
        {
            path_sel = gtk_tree_row_reference_get_path( g_ptr_array_index( clipboard->arr_ref, i ) );
            if ( !path_sel ) continue;

            enthalten = !gtk_tree_path_compare( path, path_sel );
            gtk_tree_path_free( path_sel );
            if ( enthalten ) break;
        }
        gtk_tree_path_free( path );

        if ( enthalten ) g_object_set( G_OBJECT(stv_priv->renderer_text), "sensitive", FALSE,
                NULL );
        else g_object_set( G_OBJECT(stv_priv->renderer_text), "sensitive", TRUE, NULL );
    }
    else g_object_set( G_OBJECT(stv_priv->renderer_text), "sensitive", TRUE, NULL );

    return;
}


static void
sond_treeview_underline_cursor( SondTreeview* stv, GtkTreeIter* iter )
{
    SondTreeviewPrivate* stv_priv = sond_treeview_get_instance_private( stv );

    GtkTreePath* path_cursor = NULL;
    gtk_tree_view_get_cursor( GTK_TREE_VIEW(stv), &path_cursor, NULL );

    if ( path_cursor )
    {
        GtkTreePath* path = gtk_tree_model_get_path( gtk_tree_view_get_model(
                GTK_TREE_VIEW(stv) ), iter );
        if ( !gtk_tree_path_compare( path, path_cursor ) ) g_object_set(
                G_OBJECT(stv_priv->renderer_text), "underline-set", TRUE, NULL );
        else g_object_set( G_OBJECT(stv_priv->renderer_text), "underline-set", FALSE, NULL );
        gtk_tree_path_free( path );
    }
    else g_object_set( G_OBJECT(stv_priv->renderer_text), "underline-set", FALSE, NULL );

    gtk_tree_path_free( path_cursor );

    return;
}


static void
sond_treeview_render_text( GtkTreeViewColumn* column, GtkCellRenderer* renderer,
        GtkTreeModel* treemodel, GtkTreeIter* iter, gpointer data )
{
    SondTreeview* stv = SOND_TREEVIEW(data);

    sond_treeview_grey_cut_cell( stv, iter );
    sond_treeview_underline_cursor( stv, iter );

    if ( SOND_TREEVIEW_GET_CLASS(stv)->render_text_cell )
           SOND_TREEVIEW_GET_CLASS(stv)->render_text_cell( column, renderer, treemodel, iter, data );

    return;
}


static gboolean
sond_treeview_selection_select_func( GtkTreeSelection* selection, GtkTreeModel* model,
        GtkTreePath* path, gboolean selected, gpointer data )
{
    if ( selected ) return TRUE; //abschalten geht immer

    GList* list = gtk_tree_selection_get_selected_rows( selection, NULL );
    GList* l = list;
    while ( l )
    {
        if ( gtk_tree_path_is_ancestor( path, l->data ) ||
                gtk_tree_path_is_descendant( path, l->data ) )
        {
            g_list_free_full (list, (GDestroyNotify) gtk_tree_path_free);
            return FALSE;
        }

        l = l->next;
    }

    g_list_free_full (list, (GDestroyNotify) gtk_tree_path_free);

    return TRUE;
}


static void
sond_treeview_kopieren_activate( GtkMenuItem* item, gpointer user_data )
{
    SondTreeview* stv = (SondTreeview*) user_data;

    sond_treeview_copy_or_cut_selection( stv, FALSE );

    return;
}


static void
sond_treeview_ausschneiden_activate( GtkMenuItem* item, gpointer user_data )
{
    SondTreeview* stv = (SondTreeview*) user_data;

    sond_treeview_copy_or_cut_selection( stv, TRUE );

    return;
}


static void
sond_treeview_init( SondTreeview* stv )
{
    GtkTreeViewColumn* tvc = NULL;

    SondTreeviewPrivate* stv_private = sond_treeview_get_instance_private( stv );
    SondTreeviewClass* klass = SOND_TREEVIEW_GET_CLASS(stv);

    gtk_tree_view_set_fixed_height_mode( GTK_TREE_VIEW(stv), TRUE );
    gtk_tree_view_set_enable_tree_lines( GTK_TREE_VIEW(stv), TRUE );
    gtk_tree_view_set_enable_search( GTK_TREE_VIEW(stv), FALSE );

    //die Selection
    GtkTreeSelection* selection = gtk_tree_view_get_selection( GTK_TREE_VIEW(stv) );
    gtk_tree_selection_set_mode( selection, GTK_SELECTION_MULTIPLE );
    gtk_tree_selection_set_select_function( selection,
            (GtkTreeSelectionFunc) sond_treeview_selection_select_func, NULL, NULL );

    stv_private->renderer_icon = gtk_cell_renderer_pixbuf_new();
    stv_private->renderer_text = gtk_cell_renderer_text_new();

    g_object_set( stv_private->renderer_text, "editable", TRUE, NULL);
    g_object_set( stv_private->renderer_text, "underline", PANGO_UNDERLINE_SINGLE, NULL );

    GdkRGBA gdkrgba;
    gdkrgba.alpha = 1.0;
    gdkrgba.red = 0.95;
    gdkrgba.blue = 0.95;
    gdkrgba.green = 0.95;

    g_object_set( G_OBJECT(stv_private->renderer_text), "background-rgba", &gdkrgba,
            "background-set", FALSE, NULL );

    //die column
    tvc = gtk_tree_view_column_new();
    gtk_tree_view_column_set_resizable( tvc, TRUE);
    gtk_tree_view_column_set_sizing( tvc,
            GTK_TREE_VIEW_COLUMN_FIXED);

    gtk_tree_view_column_pack_start( tvc,
            stv_private->renderer_icon, FALSE );
    gtk_tree_view_column_pack_start( tvc,
            stv_private->renderer_text, TRUE );

    gtk_tree_view_append_column( GTK_TREE_VIEW(stv), tvc );

    gtk_tree_view_column_set_cell_data_func( tvc,
            stv_private->renderer_text, (GtkTreeCellDataFunc)
            sond_treeview_render_text, stv, NULL );

    gtk_tree_view_columns_autosize( GTK_TREE_VIEW(stv) );

    //Contextmenu
    //Rechtsklick - Kontextmenu
    //Kontextmenu erzeugen, welches bei Rechtsklick auf treeview angezeigt wird
    stv_private->contextmenu = gtk_menu_new();

    //Kopieren
    GtkWidget* item_kopieren = gtk_menu_item_new_with_label("Kopieren");
    g_object_set_data( G_OBJECT(stv_private->contextmenu),
            "item-kopieren", item_kopieren );
    g_signal_connect( G_OBJECT(item_kopieren), "activate",
            G_CALLBACK(sond_treeview_kopieren_activate), (gpointer) stv );
    gtk_menu_shell_append( GTK_MENU_SHELL(stv_private->contextmenu), item_kopieren );

    //Verschieben
    GtkWidget* item_ausschneiden = gtk_menu_item_new_with_label("Ausschneiden");
    g_object_set_data( G_OBJECT(stv_private->contextmenu),
            "item-ausschneiden", item_ausschneiden );
    g_signal_connect( G_OBJECT(item_ausschneiden), "activate",
            G_CALLBACK(sond_treeview_ausschneiden_activate), (gpointer) stv );
    gtk_menu_shell_append( GTK_MENU_SHELL(stv_private->contextmenu), item_ausschneiden );

    gtk_widget_show_all( stv_private->contextmenu );

    g_signal_connect( stv, "button-press-event",
            G_CALLBACK(sond_treeview_show_popupmenu), (gpointer) stv_private->contextmenu );

    //hiermit sollen die Momente abgefangen werden, in denen im treeview herumgetippt wird
    //dann soll key-press-event abgefangen werden und Callback gibt TRUE zur�ck
    //damit �bergeordnete Widgets nicht mehr reagieren
    g_signal_connect( stv_private->renderer_text, "editing-started",
            G_CALLBACK(renderer_text_editing_started), stv );
    g_signal_connect( stv_private->renderer_text, "editing-canceled",
            G_CALLBACK(renderer_text_editing_canceled), stv );
    g_signal_connect( stv_private->renderer_text, "edited",
            G_CALLBACK(sond_treeview_text_edited), stv );

    if ( klass->callback_key_press_event )
            stv_private->signal_key = g_signal_connect( stv, "key-press-event",
            G_CALLBACK(klass->callback_key_press_event), klass->callback_key_press_event_func_data );

    return;
}


void
sond_treeview_set_id( SondTreeview* stv, gint id )
{
    SondTreeviewPrivate* stv_priv = NULL;

    stv_priv = sond_treeview_get_instance_private( stv );
    stv_priv->id = id;

    return;
}


gint
sond_treeview_get_id( SondTreeview* stv )
{
    gint id = 0;
    SondTreeviewPrivate* stv_priv = NULL;

    stv_priv = sond_treeview_get_instance_private( stv );
    id = stv_priv->id;

    return id;
}


GtkCellRenderer*
sond_treeview_get_cell_renderer_icon( SondTreeview* stv )
{
    SondTreeviewPrivate* stv_priv = sond_treeview_get_instance_private( stv );

        return stv_priv->renderer_icon;
}


GtkCellRenderer*
sond_treeview_get_cell_renderer_text( SondTreeview* stv )
{
    SondTreeviewPrivate* stv_priv = sond_treeview_get_instance_private( stv );

    return stv_priv->renderer_text;
}


GtkWidget*
sond_treeview_get_contextmenu( SondTreeview* stv )
{
    SondTreeviewPrivate* stv_priv = sond_treeview_get_instance_private( stv );

    return stv_priv->contextmenu;
}


void
sond_treeview_expand_row( SondTreeview* stv, GtkTreeIter* iter )
{
    if ( !iter ) return;

    GtkTreePath* path = gtk_tree_model_get_path( gtk_tree_view_get_model(
            GTK_TREE_VIEW(stv) ), iter );
    gtk_tree_view_expand_to_path( GTK_TREE_VIEW(stv), path );
//    gtk_tree_view_expand_row( GTK_TREE_VIEW(stv), path, FALSE );
    gtk_tree_path_free( path );

    gtk_tree_view_columns_autosize( GTK_TREE_VIEW(stv) );

    return;
}


void
sond_treeview_expand_to_row( SondTreeview* stv, GtkTreeIter* iter )
{
    if ( !iter ) return;

    GtkTreePath* path = gtk_tree_model_get_path( gtk_tree_view_get_model(
            GTK_TREE_VIEW(stv) ), iter );
    if ( gtk_tree_path_up( path ) )
            gtk_tree_view_expand_to_path( GTK_TREE_VIEW(stv), path );
    gtk_tree_path_free( path );

    gtk_tree_view_columns_autosize( GTK_TREE_VIEW(stv) );

    return;
}


gboolean
sond_treeview_get_cursor( SondTreeview* stv, GtkTreeIter* iter )
{
    GtkTreePath* path = NULL;

    gtk_tree_view_get_cursor( GTK_TREE_VIEW(stv), &path, NULL );
    if ( !path ) return FALSE;

    if ( iter ) gtk_tree_model_get_iter( gtk_tree_view_get_model(
            GTK_TREE_VIEW(stv) ), iter, path );

    gtk_tree_path_free( path );

    return TRUE;
}


void
sond_treeview_set_cursor( SondTreeview* stv, GtkTreeIter* iter )
{
    if ( !iter ) return;

    GtkTreePath* path = gtk_tree_model_get_path( gtk_tree_view_get_model(
            GTK_TREE_VIEW(stv) ), iter );
    gtk_tree_view_set_cursor( GTK_TREE_VIEW(stv), path, NULL, FALSE );
    gtk_tree_path_free( path );

    gtk_widget_grab_focus( GTK_WIDGET(stv) );

    return;
}


void
sond_treeview_set_cursor_on_text_cell( SondTreeview* stv, GtkTreeIter* iter )
{
    if ( !iter ) return;

    SondTreeviewPrivate* stv_priv = sond_treeview_get_instance_private( stv );

    GtkTreePath* path = gtk_tree_model_get_path( gtk_tree_view_get_model(
            GTK_TREE_VIEW(stv) ), iter );
    gtk_tree_view_set_cursor_on_cell( GTK_TREE_VIEW(stv), path,
            gtk_tree_view_get_column( GTK_TREE_VIEW(stv), 0 ),
            stv_priv->renderer_text, FALSE );
    gtk_tree_path_free( path );

    return;
}


//�berpr�ft beim verschieben, ob auf zu verschiebenden Knoten oder dessen
//Nachfahren verschoben werden soll
gboolean
sond_treeview_test_cursor_descendant( SondTreeview* stv, gboolean child )
{
    Clipboard* clipboard = ((SondTreeviewClass*) g_type_class_peek( SOND_TYPE_TREEVIEW ))->clipboard;

    GtkTreePath* path = NULL;

    if ( clipboard->arr_ref->len == 0 ) return FALSE;

    gtk_tree_view_get_cursor( GTK_TREE_VIEW(stv), &path, NULL );
    if ( !path ) return FALSE;

    GtkTreePath* path_sel = NULL;
    for ( gint i = 0; i < clipboard->arr_ref->len; i++ )
    {
        gboolean res = FALSE;

        path_sel = gtk_tree_row_reference_get_path( g_ptr_array_index(
                clipboard->arr_ref, i ) );
        if ( !path_sel ) continue;

        if ( child && !gtk_tree_path_compare( path_sel, path ) ) res = TRUE;
        else if ( gtk_tree_path_is_descendant( path, path_sel ) ) res = TRUE;
        gtk_tree_path_free( path_sel );

        if ( res == TRUE )
        {
            gtk_tree_path_free( path );
            return TRUE;
        }
    }

    gtk_tree_path_free( path );

    return FALSE;
}


static GPtrArray*
sond_treeview_selection_get_refs( SondTreeview* stv )
{
    GList* selected = gtk_tree_selection_get_selected_rows(
            gtk_tree_view_get_selection( GTK_TREE_VIEW(stv) ), NULL );

    if ( !selected ) return NULL;

    GPtrArray* refs = g_ptr_array_new_with_free_func( (GDestroyNotify)
            gtk_tree_row_reference_free );

    GList* list = selected;
    do g_ptr_array_add( refs, gtk_tree_row_reference_new(
            gtk_tree_view_get_model( GTK_TREE_VIEW(stv) ), list->data ) );
    while ( (list = list->next) );

    g_list_free_full( selected, (GDestroyNotify) gtk_tree_path_free );

    return refs;
}


void
sond_treeview_copy_or_cut_selection( SondTreeview* stv, gboolean ausschneiden )
{
    Clipboard* clipboard = ((SondTreeviewClass*) g_type_class_peek( SOND_TYPE_TREEVIEW ))->clipboard;

    GPtrArray* refs = sond_treeview_selection_get_refs( stv );
    if ( !refs ) return;

    //wenn ausschneiden, alle rows ausgrauen
    if ( clipboard->ausschneiden )
            gtk_widget_queue_draw( GTK_WIDGET(clipboard->tree_view) );

    //Alte Auswahl l�schen, falls vorhanden
    g_ptr_array_unref( clipboard->arr_ref );

    //clipboard setzen
    clipboard->tree_view = stv;
    clipboard->ausschneiden = ausschneiden;
    clipboard->arr_ref = refs;

    if ( ausschneiden )
            gtk_widget_queue_draw( GTK_WIDGET(clipboard->tree_view) );

    return;
}


static gint
sond_treeview_refs_foreach( SondTreeview* stv_orig, GPtrArray* refs,
        gint (*foreach) ( SondTreeview*, GtkTreeIter*, gpointer, gchar** ),
        gpointer data, gchar** errmsg )
{
    if ( !refs ) return 0;

    for ( gint i = 0; i < refs->len; i++ )
    {
        GtkTreeIter iter_ref;
        gboolean success = FALSE;
        gint rc = 0;

        GtkTreeRowReference* ref = g_ptr_array_index( refs, i );
        GtkTreePath* path = gtk_tree_row_reference_get_path( ref );
        if ( !path ) continue;

        success = gtk_tree_model_get_iter( gtk_tree_view_get_model( GTK_TREE_VIEW(stv_orig) ),
                &iter_ref, path );
        gtk_tree_path_free( path );
        if ( !success )
        {
            if ( errmsg ) *errmsg = g_strdup( "Bei Aufruf gtk_tree_model_get_"
                    "iter:\nKonnte keinen g�ltigen Iter ermitteln" );
            return -1;
        }

        rc = foreach( stv_orig, &iter_ref, data, errmsg );
        if ( rc == -1 ) ERROR_S
        else if ( rc >= 1 ) return rc; //Abbruch gew�hlt
    }

    return 0;
}


gint
sond_treeview_clipboard_foreach( gint (*foreach)
        ( SondTreeview*, GtkTreeIter*, gpointer, gchar** ), gpointer data,
        gchar** errmsg )
{
    gint rc = 0;

    Clipboard* clipboard = ((SondTreeviewClass*) g_type_class_peek( SOND_TYPE_TREEVIEW ))->clipboard;

    rc = sond_treeview_refs_foreach( clipboard->tree_view, clipboard->arr_ref,
            foreach, data, errmsg );
    if ( rc == -1 ) ERROR_S
    else if ( rc >= 1 ) return rc;

    return 0;
}


gint
sond_treeview_selection_foreach( SondTreeview* stv, gint (*foreach)
        ( SondTreeview*, GtkTreeIter*, gpointer, gchar** ), gpointer data,
        gchar** errmsg )
{
    gint rc = 0;
    GPtrArray* refs = NULL;

    refs = sond_treeview_selection_get_refs( stv );
    if ( !refs ) return 0;

    rc = sond_treeview_refs_foreach( stv, refs, foreach, data, errmsg );
    g_ptr_array_unref( refs );
    if ( rc == -1 ) ERROR_S
    else if ( rc >= 1 ) return rc;

    return 0;
}


