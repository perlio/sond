#include "zond/global_types.h"

#include "misc.h"

#include <gtk/gtk.h>



void
treeview_zelle_ausgrauen( GtkTreeView* tree_view, GtkTreePath* path,
        GtkCellRenderer* renderer, Clipboard* clipboard )
{
    if ( clipboard->tree_view == tree_view && clipboard->ausschneiden )
    {
        gboolean enthalten = FALSE;
        GtkTreePath* path_sel = NULL;
        for ( gint i = 0; i < clipboard->arr_ref->len; i++ )
        {
            path_sel = gtk_tree_row_reference_get_path( g_ptr_array_index( clipboard->arr_ref, i ) );
            enthalten = !gtk_tree_path_compare( path, path_sel );
            gtk_tree_path_free( path_sel );
            if ( enthalten ) break;
        }

        if ( enthalten ) g_object_set( G_OBJECT(renderer), "sensitive", FALSE,
                NULL );
        else g_object_set( G_OBJECT(renderer), "sensitive", TRUE, NULL );
    }
    else g_object_set( G_OBJECT(renderer), "sensitive", TRUE, NULL );

    return;
}


void
treeview_underline_cursor( GtkTreeView* tree_view, GtkTreePath* path,
        GtkCellRenderer* renderer )
{
    GtkTreePath* path_cursor = NULL;
    gtk_tree_view_get_cursor( tree_view, &path_cursor, NULL );

    if ( path_cursor )
    {
        if ( !gtk_tree_path_compare( path, path_cursor ) ) g_object_set(
                G_OBJECT(renderer), "underline-set", TRUE, NULL );
        else g_object_set( G_OBJECT(renderer), "underline-set", FALSE, NULL );
    }
    else g_object_set( G_OBJECT(renderer), "underline-set", FALSE, NULL );

    gtk_tree_path_free( path_cursor );

    return;
}


gboolean
treeview_selection_select_func( GtkTreeSelection* selection, GtkTreeModel* model,
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


gint
treeview_selection_foreach( GtkTreeView* tree_view, GPtrArray* refs,
        gint (*foreach) ( GtkTreeView*, GtkTreeIter*, gint, gpointer, gchar** ),
        gpointer data, gchar** errmsg )
{
    if ( !refs ) return 0;

    for ( gint i = 0; i < refs->len; i++ )
    {
        GtkTreeIter iter_ref;
        gboolean success = FALSE;
        gint rc = 0;
        gint node_id = 0;

        GtkTreeRowReference* ref = g_ptr_array_index( refs, i );
        GtkTreePath* path = gtk_tree_row_reference_get_path( ref );
        if ( !path ) continue;

        success = gtk_tree_model_get_iter( gtk_tree_view_get_model( tree_view ),
                &iter_ref, path );
        gtk_tree_path_free( path );
        if ( !success )
        {
            if ( errmsg ) *errmsg = g_strdup( "Bei Aufruf gtk_tree_model_get_"
                    "iter:\nKonnte keinen gültigen Iter ermitteln" );
            return -1;
        }

        rc = foreach( tree_view, &iter_ref, node_id, data, errmsg );
        if ( rc == -1)
        {
            if ( errmsg ) *errmsg = add_string( "Bei Aufruf foreach:\n", *errmsg );
        }
        else if ( rc == 1 ) break; //Abbruch gewählt
    }

    return 0;
}


gint
treeview_selection_testen_cursor_ist_abkoemmling( GtkTreeView* tree_view, GPtrArray* arr_refs )
{
    GtkTreePath* path = NULL;

    if ( arr_refs->len == 0 ) return 0;

    gtk_tree_view_get_cursor( tree_view, &path, NULL );
    if ( !path ) return 2; //kein Cursor

    GtkTreePath* path_sel = NULL;
    gboolean descend = FALSE;
    for ( gint i = 0; i < arr_refs->len; i++ )
    {
        path_sel = gtk_tree_row_reference_get_path( g_ptr_array_index( arr_refs, i ) );
        if ( !path_sel ) continue;
        descend = gtk_tree_path_is_descendant( path, path_sel );
        gtk_tree_path_free( path_sel );
        if ( descend )
        {
            gtk_tree_path_free( path );

            return 1;
        }
    }

    gtk_tree_path_free( path );

    return 0;
}


GPtrArray*
treeview_selection_get_refs( GtkTreeView* tree_view )
{
    GList* selected = gtk_tree_selection_get_selected_rows(
            gtk_tree_view_get_selection( tree_view ), NULL );

    if ( !selected ) return NULL;

    GPtrArray* refs = g_ptr_array_new_with_free_func( (GDestroyNotify)
            gtk_tree_row_reference_free );

    GList* list = selected;
    do g_ptr_array_add( refs, gtk_tree_row_reference_new(
            gtk_tree_view_get_model( tree_view ), list->data ) );
    while ( (list = list->next) );

    g_list_free_full( selected, (GDestroyNotify) gtk_tree_path_free );

    return refs;
}


