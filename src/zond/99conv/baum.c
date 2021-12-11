#include "../global_types.h"

#include <gtk/gtk.h>

#include "../../sond_treeview.h"


gint
baum_abfragen_aktuelle_node_id( SondTreeview* treeview )
{
    gint node_id = 0;
    GtkTreeIter iter = { 0 };

    if ( !sond_treeview_get_cursor( treeview, &iter ) ) return 0;

    gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(treeview) ), &iter, 2, &node_id, -1 );

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


gint
baum_abfragen_node_id( SondTreeview* treeview, GtkTreePath* path, gchar** errmsg )
{
    gint node_id = 0;
    GtkTreeIter iter;

    //node_id_orig im baum_inhalt abfragen
    if ( !gtk_tree_model_get_iter( gtk_tree_view_get_model( GTK_TREE_VIEW(treeview) ),
            &iter, path ) )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf gtk_tree_model_get_iter:\n"
                "Rückgabewert FALSE - path nicht gültig", NULL );

        return -1;
    }
    gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(treeview) ),
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
baum_abfragen_path( SondTreeview* treeview, gint node_id )
{
    GtkTreePath* path = NULL;
    GtkTreeModel* model = gtk_tree_view_get_model( GTK_TREE_VIEW(treeview) );

    g_object_set_data( G_OBJECT(model), "node_id", GINT_TO_POINTER(node_id) );
    gtk_tree_model_foreach( model, (GtkTreeModelForeachFunc)
            baum_path_foreach_node_id, &path );

    return path;
}


static gboolean
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
baum_abfragen_iter( SondTreeview* treeview, gint node_id )
{
    GtkTreeIter* iter = NULL;
    GtkTreeModel* model = gtk_tree_view_get_model( GTK_TREE_VIEW(treeview) );

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
