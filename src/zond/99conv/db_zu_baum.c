#include "../global_types.h"
#include "../enums.h"
#include "../error.h"

#include "../99conv/db_read.h"
#include "../99conv/general.h"
#include "../99conv/baum.h"

#include <gtk/gtk.h>


GtkTreeIter*
db_baum_knoten( Projekt* zond, Baum baum, gint node_id, GtkTreeIter* iter,
        gboolean child, gchar** errmsg )
{
    //Inhalt des Datensatzes mit node_id == node_id abfragen
    gint rc = 0;
    gchar* icon_name = NULL;
    gchar* node_text = NULL;

    rc = db_get_icon_name_and_node_text( zond, baum, node_id, &icon_name,
            &node_text, errmsg );
    if ( rc == -1 ) ERROR_PAO_R( "db_get_icon_id_and_node_text", NULL )
    else if ( rc == 1 )
    {
        if ( errmsg ) *errmsg = g_strdup( "node_id existiert nicht" );

        return NULL;
    }

    //neuen Knoten einfügen
    GtkTreeIter* new_iter = baum_einfuegen_knoten( zond->treeview[baum], iter,
            child );

    //Daten rein
    gtk_tree_store_set( GTK_TREE_STORE(gtk_tree_view_get_model( zond->treeview[baum] )),
            new_iter, 0, icon_name, 1, node_text, 2, node_id, -1);

    g_free( icon_name );
    g_free( node_text );

    return new_iter; //muß gtk_tree_iter_freed werden!!
}


//rekursive Funktion; gibt Zeiger auf 1. eingefügten Iter zurück (g_free)
GtkTreeIter*
db_baum_knoten_mit_kindern( Projekt* zond, gboolean with_younger_siblings,
        Baum baum, gint node_id, GtkTreeIter* iter, gboolean child,
        gchar** errmsg )
{
    GtkTreeIter* iter_new = NULL;
    iter_new = db_baum_knoten( zond, baum, node_id, iter, child, errmsg );
    if ( !iter_new ) ERROR_PAO_R( "db_baum_knoten", NULL )

    //Prüfen, ob Kind- oder Geschwisterknoten vorhanden
    gint first_child_id = 0;
    gint younger_sibling_id = 0;

    first_child_id = db_get_first_child( zond, baum, node_id, errmsg );
    if ( first_child_id < 0 ) ERROR_PAO_R( "db_get_first_child", NULL )

    if ( first_child_id > 0 )
    {
        GtkTreeIter* iter_child = NULL;
        iter_child = db_baum_knoten_mit_kindern( zond, TRUE, baum, first_child_id,
                iter_new, TRUE, errmsg );
        if ( !iter_child ) ERROR_PAO_R( "db_baum_knoten_mit_kindern", NULL )

        gtk_tree_iter_free( iter_child );
    }

    younger_sibling_id = db_get_younger_sibling( zond, baum, node_id, errmsg );
    if ( younger_sibling_id < 0 ) ERROR_PAO_R( "db_get_younger_sibling", NULL )

    if ( younger_sibling_id > 0 && with_younger_siblings )
    {
        GtkTreeIter* iter_sibling = NULL;
        iter_sibling = db_baum_knoten_mit_kindern( zond, TRUE, baum,
                younger_sibling_id, iter_new, FALSE, errmsg );
        if ( !iter_sibling ) ERROR_PAO_R( "db_baum_knoten_mit_kindern", NULL )

        gtk_tree_iter_free( iter_sibling);
    }
    return iter_new;
}


//Prototype
void cb_cursor_changed( GtkTreeView*, gpointer );


gint
db_baum_neu_laden( Projekt* zond, Baum baum, gchar** errmsg )
{
#ifndef VIEWER
    gint first_node_id = 0;

    gtk_tree_store_clear( GTK_TREE_STORE(gtk_tree_view_get_model(
            zond->treeview[baum] )) );

    first_node_id = db_get_first_child( zond, baum, 0, errmsg );
    if ( first_node_id < 0 ) ERROR_PAO( "db_get_first_child" )

    GtkTreeIter* iter = NULL;
    if ( first_node_id )
    {
        iter = db_baum_knoten_mit_kindern( zond, TRUE, baum, first_node_id, NULL,
                TRUE, errmsg );
        if ( !iter ) ERROR_PAO( "db_baum_knoten_mit_kindern" )

        //kurz Signal verbinden, damit label und textview angezeigt werden
        gulong signal = g_signal_connect( zond->treeview[baum], "cursor-changed",
                G_CALLBACK(cb_cursor_changed), zond );
        baum_setzen_cursor( zond, baum, iter );
        g_signal_handler_disconnect( zond->treeview[baum], signal );

        gtk_tree_iter_free( iter );
    }
#endif // VIEWER
    return 0;
}


gint
db_baum_refresh( Projekt* zond, gchar** errmsg )
{
    gchar* errmsg_ii = NULL;
    gint rc = 0;
    rc = db_baum_neu_laden( zond, BAUM_INHALT, &errmsg_ii );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf db_baum_neu_laden für "
                "BAUM_INHALT:\n", errmsg_ii, NULL );
        g_free( errmsg_ii );

        return -1;
    }

    rc = db_baum_neu_laden( zond, BAUM_AUSWERTUNG, &errmsg_ii );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf db_baum_neu_laden für "
                "BAUM_AUSWERTUNG:\n", errmsg_ii, NULL );
        g_free( errmsg_ii );

        return -1;
    }

    gtk_tree_selection_unselect_all( zond->selection[BAUM_AUSWERTUNG] );
    g_object_set(zond->renderer_text[BAUM_AUSWERTUNG], "editable", FALSE, NULL);
    g_object_set(zond->renderer_text[BAUM_INHALT], "editable", TRUE, NULL);

    gtk_widget_grab_focus( GTK_WIDGET(zond->treeview[BAUM_INHALT]) );

    gtk_widget_set_sensitive( GTK_WIDGET(zond->textview), FALSE );

    return 0;
}

