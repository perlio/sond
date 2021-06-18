/*
sond (sond_treeview.c) - Akten, Beweisstücke, Unterlagen
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

#include "misc.h"


typedef struct
{
    GtkTreeViewColumn* first_column;
    GtkCellRenderer* renderer_icon;
    GtkCellRenderer* renderer_text;
    void (* render_text_cell) ( SondTreeview*, GtkTreeIter*, gpointer );
    gpointer render_text_cell_data;
    Clipboard* clipboard;
} SondTreeviewPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondTreeview, sond_treeview, GTK_TYPE_TREE_VIEW)


static void
sond_treeview_class_init( SondTreeviewClass* klass )
{
    return;
}


static void
sond_treeview_grey_cut_cell( SondTreeview* stv, GtkTreeIter* iter )
{
    SondTreeviewPrivate* stv_priv = sond_treeview_get_instance_private( stv );

    if ( !(stv_priv->clipboard) ) return;

    if ( stv_priv->clipboard->tree_view == stv && stv_priv->clipboard->ausschneiden )
    {
        gboolean enthalten = FALSE;
        GtkTreePath* path = NULL;
        GtkTreePath* path_sel = NULL;

        path = gtk_tree_model_get_path( gtk_tree_view_get_model( GTK_TREE_VIEW(stv) ), iter );
        for ( gint i = 0; i < stv_priv->clipboard->arr_ref->len; i++ )
        {
            path_sel = gtk_tree_row_reference_get_path( g_ptr_array_index( stv_priv->clipboard->arr_ref, i ) );
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
    SondTreeview* stv = SOND_TREEVIEW(gtk_tree_view_column_get_tree_view( column ));
    SondTreeviewPrivate* stv_priv = sond_treeview_get_instance_private( stv );

    sond_treeview_grey_cut_cell( stv, iter );
    sond_treeview_underline_cursor( stv, iter );

    stv_priv->render_text_cell( stv, iter, stv_priv->render_text_cell_data );

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
sond_treeview_init( SondTreeview* stv )
{
    SondTreeviewPrivate* stv_private = sond_treeview_get_instance_private( stv );

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
    stv_private->first_column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_resizable( stv_private->first_column, TRUE);
    gtk_tree_view_column_set_sizing( stv_private->first_column,
            GTK_TREE_VIEW_COLUMN_FIXED);

    gtk_tree_view_column_pack_start( stv_private->first_column,
            stv_private->renderer_icon, FALSE );
    gtk_tree_view_column_pack_start( stv_private->first_column,
            stv_private->renderer_text, TRUE );

    gtk_tree_view_append_column( GTK_TREE_VIEW(stv), stv_private->first_column );

    gtk_tree_view_column_set_cell_data_func( stv_private->first_column,
            stv_private->renderer_text, (GtkTreeCellDataFunc)
            sond_treeview_render_text, NULL, NULL );

    gtk_tree_view_columns_autosize( GTK_TREE_VIEW(stv) );

    return;
}


SondTreeview*
sond_treeview_new( )
{
    return g_object_new( SOND_TYPE_TREEVIEW, NULL );
}


void
sond_treeview_set_clipboard( SondTreeview* stv, Clipboard* clipboard )
{
    SondTreeviewPrivate* stv_priv = sond_treeview_get_instance_private( stv );

    stv_priv->clipboard = clipboard;

    return;
}


Clipboard*
sond_treeview_get_clipboard( SondTreeview* stv )
{
    SondTreeviewPrivate* stv_priv = sond_treeview_get_instance_private( stv );

    return stv_priv->clipboard;
}

void
sond_treeview_set_render_text_cell_func( SondTreeview* stv, void (* render_text_cell)
        ( SondTreeview*, GtkTreeIter*, gpointer ), gpointer func_data )
{
    SondTreeviewPrivate* stv_priv = sond_treeview_get_instance_private( stv );

    stv_priv->render_text_cell = render_text_cell;
    stv_priv->render_text_cell_data = func_data;

    return;
}


GtkTreeViewColumn*
sond_treeview_get_column( SondTreeview* stv )
{
    SondTreeviewPrivate* stv_priv = sond_treeview_get_instance_private( stv );

        return stv_priv->first_column;
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


void
sond_treeview_expand_row( SondTreeview* stv, GtkTreeIter* iter )
{
    if ( !iter ) return;

    GtkTreePath* path = gtk_tree_model_get_path( gtk_tree_view_get_model(
            GTK_TREE_VIEW(stv) ), iter );
    gtk_tree_view_expand_to_path( GTK_TREE_VIEW(stv), path );
    gtk_tree_view_expand_row( GTK_TREE_VIEW(stv), path, TRUE );
    gtk_tree_path_free( path );

    gtk_tree_view_columns_autosize( GTK_TREE_VIEW(stv) );

    return;
}


GtkTreeIter*
sond_treeview_insert_node( SondTreeview* stv, GtkTreeIter* iter, gboolean child )
{
    GtkTreeIter new_iter;
    GtkTreeStore* treestore = GTK_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(stv) ));

    //Hauptknoten erzeugen
    if ( !child ) gtk_tree_store_insert_after( treestore, &new_iter, NULL, iter );
    //Unterknoten erzeugen
    else gtk_tree_store_insert_after( treestore, &new_iter, iter, NULL );

    GtkTreeIter* ret_iter = gtk_tree_iter_copy( &new_iter );

    return ret_iter; //muß nach Gebrauch gtk_tree_iter_freed werden!!!
}


GtkTreeIter*
sond_treeview_get_cursor( SondTreeview* stv )
{
    GtkTreePath* path;

    gtk_tree_view_get_cursor( GTK_TREE_VIEW(stv), &path, NULL );
    if ( !path ) return NULL;

    GtkTreeIter iter;
    gtk_tree_model_get_iter( gtk_tree_view_get_model( GTK_TREE_VIEW(stv) ), &iter, path );

    gtk_tree_path_free( path );

    GtkTreeIter* ret_iter = gtk_tree_iter_copy( &iter );

    return ret_iter; //muß gtk_tree_iter_freed werden!
}


void
sond_treeview_set_cursor( SondTreeview* stv, GtkTreeIter* iter )
{
    if ( !iter ) return;

    GtkTreePath* path = gtk_tree_model_get_path( gtk_tree_view_get_model(
            GTK_TREE_VIEW(stv) ), iter );
    gtk_tree_view_set_cursor( GTK_TREE_VIEW(stv), path, NULL, FALSE );
    gtk_tree_path_free( path );

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
            stv_priv->first_column, stv_priv->renderer_text, FALSE );
    gtk_tree_path_free( path );

    return;
}


//überprüft beim verschieben, ob auf zu verschiebenden Knoten oder dessen
//Nachfahren verschoben werden soll
gboolean
sond_treeview_test_cursor_descendant( SondTreeview* stv )
{
    SondTreeviewPrivate* stv_priv = sond_treeview_get_instance_private( stv );

    GtkTreePath* path = NULL;

    if ( stv_priv->clipboard->arr_ref->len == 0 ) return FALSE;

    gtk_tree_view_get_cursor( GTK_TREE_VIEW(stv), &path, NULL );
    if ( !path ) return FALSE;

    GtkTreePath* path_sel = NULL;
    gboolean descend = FALSE;
    for ( gint i = 0; i < stv_priv->clipboard->arr_ref->len; i++ )
    {
        path_sel = gtk_tree_row_reference_get_path( g_ptr_array_index(
                stv_priv->clipboard->arr_ref, i ) );
        if ( !path_sel ) continue;
        descend = gtk_tree_path_is_descendant( path, path_sel );
        gtk_tree_path_free( path_sel );
        if ( descend )
        {
            gtk_tree_path_free( path );

            return TRUE;
        }
    }

    gtk_tree_path_free( path );

    return FALSE;
}


GPtrArray*
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
    SondTreeviewPrivate* stv_priv = sond_treeview_get_instance_private( stv );

    if ( !(stv_priv->clipboard) ) return;

    GPtrArray* refs = sond_treeview_selection_get_refs( stv );
    if ( !refs ) return;

    //wenn ausgeschnitten war, alle rows wieder normal zeichnen
    if ( stv_priv->clipboard->ausschneiden )
            gtk_widget_queue_draw( GTK_WIDGET(stv_priv->clipboard->tree_view) );

    //Alte Auswahl löschen, falls vorhanden
    g_ptr_array_unref( stv_priv->clipboard->arr_ref );

    //clipboard setzen
    stv_priv->clipboard->tree_view = stv;
    stv_priv->clipboard->ausschneiden = ausschneiden;
    stv_priv->clipboard->arr_ref = refs;

    if ( ausschneiden )
            gtk_widget_queue_draw( GTK_WIDGET(stv_priv->clipboard->tree_view) );

    return;
}


static gint
sond_treeview_refs_foreach( SondTreeview* stv, GPtrArray* refs,
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

        success = gtk_tree_model_get_iter( gtk_tree_view_get_model( GTK_TREE_VIEW(stv) ),
                &iter_ref, path );
        gtk_tree_path_free( path );
        if ( !success )
        {
            if ( errmsg ) *errmsg = g_strdup( "Bei Aufruf gtk_tree_model_get_"
                    "iter:\nKonnte keinen gültigen Iter ermitteln" );
            return -1;
        }

        rc = foreach( stv, &iter_ref, data, errmsg );
        if ( rc == -1 ) ERROR_SOND( "foreach" )
        else if ( rc > 1 ) return rc; //Abbruch gewählt
    }

    return 0;
}


gint
sond_treeview_clipboard_foreach( SondTreeview* stv, gint (*foreach)
        ( SondTreeview*, GtkTreeIter*, gpointer, gchar** ), gpointer data,
        gchar** errmsg )
{
    gint rc = 0;

    SondTreeviewPrivate* stv_priv = sond_treeview_get_instance_private( stv );

    rc = sond_treeview_refs_foreach( stv, stv_priv->clipboard->arr_ref, foreach,
            data, errmsg );
    if ( rc == -1 ) ERROR_SOND( "sond_treeview_refs_foreach" )
    else if ( rc > 1 ) return rc;

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
    if ( rc == -1 ) ERROR_SOND( "sond_treeview_refs_foreach" )
    else if ( rc > 1 ) return rc;

    return 0;
}


