#include <gtk/gtk.h>

#include "treeview.h" //wg. Deklaration von Clipboard

#include "misc.h"


Clipboard*
treeview_init_clipboard( void )
{
    Clipboard* clipboard = g_malloc0( sizeof( Clipboard ) );

    clipboard->arr_ref = g_ptr_array_new_with_free_func( (GDestroyNotify) gtk_tree_row_reference_free );

    return clipboard;
}


void
treeview_expand_row( GtkTreeView* tree_view, GtkTreeIter* iter )
{
    if ( !iter ) return;

    GtkTreePath* path = gtk_tree_model_get_path( gtk_tree_view_get_model(
            tree_view ), iter );
    gtk_tree_view_expand_to_path( tree_view, path );
    gtk_tree_view_expand_row( tree_view, path, TRUE );
    gtk_tree_path_free( path );

    return;
}


GtkTreeIter*
treeview_insert_node( GtkTreeView* treeview, GtkTreeIter* iter,
        gboolean child )
{
    GtkTreeIter new_iter;
    GtkTreeStore* treestore = GTK_TREE_STORE(gtk_tree_view_get_model( treeview ));

    //Hauptknoten erzeugen
    if ( !child ) gtk_tree_store_insert_after( treestore, &new_iter, NULL, iter );
    //Unterknoten erzeugen
    else gtk_tree_store_insert_after( treestore, &new_iter, iter, NULL );

    GtkTreeIter* ret_iter = gtk_tree_iter_copy( &new_iter );

    return ret_iter; //muß nach Gebrauch gtk_tree_iter_freed werden!!!
}


GtkTreeIter*
treeview_get_cursor( GtkTreeView* treeview )
{
    GtkTreePath* path;

    gtk_tree_view_get_cursor( treeview, &path, NULL );
    if ( !path ) return NULL;

    GtkTreeIter iter;
    gtk_tree_model_get_iter( gtk_tree_view_get_model( treeview ), &iter, path );

    gtk_tree_path_free( path );

    GtkTreeIter* ret_iter = gtk_tree_iter_copy( &iter );

    return ret_iter; //muß gtk_tree_iter_freed werden!
}


void
treeview_set_cursor( GtkTreeView* tree_view, GtkTreeIter* iter )
{
    if ( !iter ) return;

    GtkTreePath* path = gtk_tree_model_get_path( gtk_tree_view_get_model(
            tree_view ), iter );
    gtk_tree_view_set_cursor( tree_view, path, NULL, FALSE );
    gtk_tree_path_free( path );

    return;
}


void
treeview_zelle_ausgrauen( GtkTreeView* tree_view, GtkTreePath* path,
        GtkCellRenderer* renderer, Clipboard* clipboard )
{
    if ( !clipboard ) return;

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


//überprüft beim verschieben, ob auf zu verschiebenden Knoten oder dessen
//Nachfahren verschoben werden soll
//Falls ja: Rückgabe 1
gint
treeview_selection_testen_cursor_ist_abkoemmling( GtkTreeView* tree_view, GPtrArray* refs )
{
    GtkTreePath* path = NULL;

    if ( refs->len == 0 ) return 0;

    gtk_tree_view_get_cursor( tree_view, &path, NULL );
    if ( !path ) return 2;

    GtkTreePath* path_sel = NULL;
    gboolean descend = FALSE;
    for ( gint i = 0; i < refs->len; i++ )
    {
        path_sel = gtk_tree_row_reference_get_path( g_ptr_array_index( refs, i ) );
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


gint
treeview_selection_foreach( GtkTreeView* tree_view, GPtrArray* refs,
        gint (*foreach) ( GtkTreeView*, GtkTreeIter*, gpointer, gchar** ),
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

        success = gtk_tree_model_get_iter( gtk_tree_view_get_model( tree_view ),
                &iter_ref, path );
        gtk_tree_path_free( path );
        if ( !success )
        {
            if ( errmsg ) *errmsg = g_strdup( "Bei Aufruf gtk_tree_model_get_"
                    "iter:\nKonnte keinen gültigen Iter ermitteln" );
            return -1;
        }

        rc = foreach( tree_view, &iter_ref, data, errmsg );
        if ( rc == -1)
        {
            if ( errmsg ) *errmsg = add_string( g_strdup( "Bei Aufruf foreach:\n" ),
                    *errmsg );

            return -1;
        }
        else if ( rc > 1 ) return rc; //Abbruch gewählt
    }

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


void
treeview_copy_or_cut_selection( GtkTreeView* tree_view, Clipboard* clipboard,
        gboolean ausschneiden )
{
    GPtrArray* refs = treeview_selection_get_refs( tree_view );
    if ( !refs ) return;

    //wenn ausgeschnitten war, alle rows wieder normal zeichnen
    if ( clipboard->ausschneiden )
            gtk_widget_queue_draw( GTK_WIDGET(clipboard->tree_view) );

    //Alte Auswahl löschen, falls vorhanden
    g_ptr_array_unref( clipboard->arr_ref );

    //clipboard setzen
    clipboard->tree_view = tree_view;
    clipboard->ausschneiden = ausschneiden;
    clipboard->arr_ref = refs;

    if ( ausschneiden )
            gtk_widget_queue_draw( GTK_WIDGET(clipboard->tree_view) );

    return;
}

