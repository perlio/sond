#include "../global_types.h"
#include "../enums.h"
#include "../error.h"

#include "../99conv/db_read.h"
#include "../99conv/general.h"
#include "../99conv/baum.h"

#include <gtk/gtk.h>

#include "../../sond_treeview.h"
#include "../../misc.h"
#include "../zond_tree_store.h"


static gint
db_baum_insert_links( Projekt* zond, Baum baum, gchar** errmsg )
{
    return 0;
}

gint
db_baum_knoten( Projekt* zond, Baum baum, gint node_id, GtkTreeIter* iter,
        gboolean child, GtkTreeIter* iter_new, gchar** errmsg )
{
    //Inhalt des Datensatzes mit node_id == node_id abfragen
    gint rc = 0;
    gchar* icon_name = NULL;
    gchar* node_text = NULL;
    GtkTreeIter iter_inserted = { 0, };

    rc = db_get_icon_name_and_node_text( zond, baum, node_id, &icon_name,
            &node_text, errmsg );
    if ( rc == -1 ) ERROR_SOND( "db_get_icon_id_and_node_text" )
    else if ( rc == 1 )
    {
        if ( errmsg ) *errmsg = g_strdup( "node_id existiert nicht" );

        return -1;
    }

    //neuen Knoten einfügen
    zond_tree_store_insert( ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[baum]) )),
            iter, child, &iter_inserted );

    //Daten rein
    zond_tree_store_set( &iter_inserted, icon_name, node_text, node_id );

    g_free( icon_name );
    g_free( node_text );

    if ( iter_new ) *iter_new = iter_inserted;

    return 0;
}


//rekursive Funktion; gibt Zeiger auf 1. eingefügten Iter zurück (g_free)
gint
db_baum_knoten_mit_kindern( Projekt* zond, gboolean with_younger_siblings,
        Baum baum, gint node_id, GtkTreeIter* iter, gboolean child, GtkTreeIter* iter_new,
        gchar** errmsg )
{
    gint rc = 0;
    GtkTreeIter iter_inserted = { 0, };

    rc = db_baum_knoten( zond, baum, node_id, iter, child, &iter_inserted, errmsg );
    if ( rc ) ERROR_SOND( "db_baum_knoten" )

    //Prüfen, ob Kind- oder Geschwisterknoten vorhanden
    gint first_child_id = 0;
    gint younger_sibling_id = 0;

    first_child_id = db_get_first_child( zond, baum, node_id, errmsg );
    if ( first_child_id < 0 ) ERROR_SOND( "db_get_first_child" )

    if ( first_child_id > 0 )
    {
        gint rc = 0;
        rc = db_baum_knoten_mit_kindern( zond, TRUE, baum, first_child_id,
                &iter_inserted, TRUE, NULL, errmsg );
        if ( rc ) ERROR_SOND( "db_baum_knoten_mit_kindern" )
    }

    younger_sibling_id = db_get_younger_sibling( zond, baum, node_id, errmsg );
    if ( younger_sibling_id < 0 ) ERROR_SOND( "db_get_younger_sibling" )

    if ( younger_sibling_id > 0 && with_younger_siblings )
    {
        gint rc = 0;

        rc = db_baum_knoten_mit_kindern( zond, TRUE, baum,
                younger_sibling_id, &iter_inserted, FALSE, NULL, errmsg );
        if ( rc ) ERROR_SOND( "db_baum_knoten_mit_kindern" )
    }

    if ( iter_new ) *iter_new = iter_inserted;

    return 0;
}


//Prototype
void cb_cursor_changed( GtkTreeView*, gpointer );


static gint
db_baum_neu_laden( Projekt* zond, Baum baum, gchar** errmsg )
{
#ifndef VIEWER
    gint first_node_id = 0;

    zond_tree_store_clear( ZOND_TREE_STORE(gtk_tree_view_get_model(
            GTK_TREE_VIEW(zond->treeview[baum]) )) );

    first_node_id = db_get_first_child( zond, baum, 0, errmsg );
    if ( first_node_id < 0 ) ERROR_SOND( "db_get_first_child" )

    if ( first_node_id )
    {
        gint rc = 0;

        rc = db_baum_knoten_mit_kindern( zond, TRUE, baum, first_node_id, NULL,
                TRUE, NULL, errmsg );
        if ( rc ) ERROR_SOND( "db_baum_knoten_mit_kindern" )

/*        //kurz Signal verbinden, damit label und textview angezeigt werden
        gulong signal = g_signal_connect( zond->treeview[baum], "cursor-changed",
                G_CALLBACK(cb_cursor_changed), zond );
        sond_treeview_set_cursor( zond->treeview[baum], iter );
        g_signal_handler_disconnect( zond->treeview[baum], signal );
*/
    }
#endif // VIEWER
    return 0;
}


gint
db_baum_refresh( Projekt* zond, gchar** errmsg )
{
    gint rc = 0;

    rc = db_baum_neu_laden( zond, BAUM_INHALT, errmsg );
    if ( rc ) ERROR_SOND( "db_baum_neu_laden (BAUM_INHALT)" )

    rc = db_baum_neu_laden( zond, BAUM_AUSWERTUNG, errmsg );
    if ( rc ) ERROR_SOND( "db_baum_neu_laden (BAUM_AUSWERTUNG)" )

    rc = db_baum_insert_links( zond, BAUM_INHALT, errmsg );
    if ( rc ) ERROR_SOND( "db_baum_insert_links (BAUM_INHALT)" )

    rc = db_baum_insert_links( zond, BAUM_AUSWERTUNG, errmsg );
    if ( rc ) ERROR_SOND( "db_baum_insert_links (BAUM_AUSWERTUNG)" )

    gtk_tree_selection_unselect_all( zond->selection[BAUM_AUSWERTUNG] );

    g_object_set( sond_treeview_get_cell_renderer_text( zond->treeview[BAUM_AUSWERTUNG] ),
            "editable", FALSE, NULL);
    g_object_set( sond_treeview_get_cell_renderer_text( zond->treeview[BAUM_INHALT] ),
            "editable", TRUE, NULL);

    gtk_widget_grab_focus( GTK_WIDGET(zond->treeview[BAUM_INHALT]) );

    gtk_widget_set_sensitive( GTK_WIDGET(zond->textview), FALSE );

    return 0;
}

