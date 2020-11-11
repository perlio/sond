#include "../global_types.h"

#include <gtk/gtk.h>


GtkTreeIter*
baum_einfuegen_knoten( GtkTreeView* treeview, GtkTreeIter* iter,
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


gint
baum_abfragen_parent_id( Projekt* zond, Baum baum, GtkTreeIter* iter )
{
    gint parent_id = 0;
    GtkTreeIter iter_parent;
    GtkTreeModel* model = gtk_tree_view_get_model( zond->treeview[baum] );

    if ( gtk_tree_model_iter_parent( model, &iter_parent, iter) )
            gtk_tree_model_get( model, &iter_parent, 2, &parent_id, -1 );

    return parent_id;
}


gint
baum_abfragen_older_sibling_id( Projekt* zond, Baum baum, GtkTreeIter* iter )
{
    gint older_sibling_id = 0;

    GtkTreeIter* iter_older_sibling = gtk_tree_iter_copy( iter );

    GtkTreeModel* model = gtk_tree_view_get_model( zond->treeview[baum] );

    if ( gtk_tree_model_iter_previous( model, iter_older_sibling ) )
            gtk_tree_model_get( model, iter_older_sibling, 2,
            &older_sibling_id, -1 );

    gtk_tree_iter_free( iter_older_sibling );

    return older_sibling_id;
}


GtkTreeIter*
baum_abfragen_aktuellen_cursor( GtkTreeView* treeview )
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


gint
baum_abfragen_aktuelle_node_id( GtkTreeView* treeview )
{
    gint node_id = 0;
    GtkTreeIter* iter = baum_abfragen_aktuellen_cursor( treeview );
    if ( !iter ) return 0;

    gtk_tree_model_get( gtk_tree_view_get_model( treeview ), iter, 2, &node_id, -1 );
    gtk_tree_iter_free( iter );

    return node_id;
}


Baum
baum_abfragen_aktiver_treeview( Projekt* zond )
{
    Baum baum = KEIN_BAUM;

    if ( gtk_widget_is_focus( GTK_WIDGET(zond->treeview[BAUM_FS]) ) ) baum =
            BAUM_FS;
    if ( gtk_widget_is_focus( GTK_WIDGET(zond->treeview[BAUM_INHALT]) ) ) baum =
            BAUM_INHALT;
    if ( gtk_widget_is_focus( GTK_WIDGET(zond->treeview[BAUM_AUSWERTUNG]) ) ) baum =
            BAUM_AUSWERTUNG;

    return baum;
}


void baum_setzen_cursor( Projekt* zond, Baum baum, GtkTreeIter* iter )
{
    if ( !iter ) return;

    GtkTreePath* path = gtk_tree_model_get_path( gtk_tree_view_get_model(
            zond->treeview[baum] ), iter );
    gtk_tree_view_set_cursor( zond->treeview[baum], path, NULL, FALSE );
    gtk_tree_path_free( path );

    return;
}


void
expand_row( Projekt* zond, Baum baum, GtkTreeIter* iter )
{
    if ( !iter ) return;

    GtkTreePath* path = gtk_tree_model_get_path( gtk_tree_view_get_model(
            zond->treeview[baum] ), iter );
    gtk_tree_view_expand_to_path( zond->treeview[baum], path );
    gtk_tree_view_expand_row( zond->treeview[baum], path, TRUE );
    gtk_tree_path_free( path );

    return;
}


void
expand_to_row( Projekt* zond, Baum baum, GtkTreeIter* iter )
{
    if ( !iter ) return;

    GtkTreePath* path = gtk_tree_model_get_path( gtk_tree_view_get_model(
            zond->treeview[baum] ), iter );
    gtk_tree_view_expand_to_path( zond->treeview[baum], path );
    gtk_tree_view_set_cursor( zond->treeview[baum], path, NULL, FALSE );
    gtk_tree_path_free( path );

    return;
}


gint
baum_abfragen_node_id( GtkTreeView* treeview, GtkTreePath* path, gchar** errmsg )
{
    gint node_id = 0;
    GtkTreeIter iter;

    //node_id_orig im baum_inhalt abfragen
    if ( !gtk_tree_model_get_iter( gtk_tree_view_get_model( treeview ),
            &iter, path ) )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf gtk_tree_model_get_iter:\n"
                "Rückgabewert FALSE - path nicht gültig", NULL );

        return -1;
    }
    gtk_tree_model_get( gtk_tree_view_get_model( treeview ),
            &iter, 2, &node_id, -1 );

    return node_id;
}


static gboolean
baum_path_foreach_node_id( GtkTreeModel* model, GtkTreePath* path, GtkTreeIter* iter,
        gpointer user_data )
{
    GtkTreePath** new_path = (GtkTreePath**) user_data;
    gint node_id = GPOINTER_TO_INT(g_object_get_data( G_OBJECT(model),
            "node_id" ));

    gint node_id_tree = 0;
    gtk_tree_model_get( model, iter, 2, &node_id_tree, -1 );

    if ( node_id == node_id_tree )
    {
        *new_path = gtk_tree_path_copy( path );
        return TRUE;
    }
    else return FALSE;
}


GtkTreePath*
baum_abfragen_path( GtkTreeView* treeview, gint node_id )
{
    GtkTreePath* path = NULL;
    GtkTreeModel* model = gtk_tree_view_get_model( treeview );

    g_object_set_data( G_OBJECT(model), "node_id", GINT_TO_POINTER(node_id) );
    gtk_tree_model_foreach( model, (GtkTreeModelForeachFunc)
            baum_path_foreach_node_id, &path );

    return path;
}


gboolean
baum_iter_foreach_node_id( GtkTreeModel* model, GtkTreePath* path,
        GtkTreeIter* iter, gpointer user_data )
{
    GtkTreeIter** new_iter = (GtkTreeIter**) user_data;
    gint node_id = GPOINTER_TO_INT(g_object_get_data( G_OBJECT(model),
            "node_id" ));

    gint node_id_tree = 0;
    gtk_tree_model_get( model, iter, 2, &node_id_tree, -1 );

    if ( node_id == node_id_tree )
    {
        *new_iter = gtk_tree_iter_copy( iter );
        return TRUE;
    }
    else return FALSE;
}


GtkTreeIter*
baum_abfragen_iter( GtkTreeView* treeview, gint node_id )
{
    GtkTreeIter* iter = NULL;
    GtkTreeModel* model = gtk_tree_view_get_model( treeview );

    g_object_set_data( G_OBJECT(model), "node_id", GINT_TO_POINTER(node_id) );
    gtk_tree_model_foreach( model, (GtkTreeModelForeachFunc)
            baum_iter_foreach_node_id, &iter );

    return iter;
}


Baum
baum_get_baum_from_treeview( Projekt* zond, GtkWidget* tree_view )
{
    if ( tree_view == GTK_WIDGET(zond->treeview[BAUM_FS]) ) return BAUM_FS;
    if ( tree_view == GTK_WIDGET(zond->treeview[BAUM_INHALT]) ) return BAUM_INHALT;
    if ( tree_view == GTK_WIDGET(zond->treeview[BAUM_AUSWERTUNG]) ) return BAUM_AUSWERTUNG;

    return KEIN_BAUM;
}
