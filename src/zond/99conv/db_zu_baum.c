#include "../global_types.h"
#include "../enums.h"
#include "../error.h"
#include "../zond_dbase.h"

#include "../20allgemein/project.h"
#include "../99conv/general.h"
#include "../99conv/baum.h"

#include <gtk/gtk.h>

#include "../../sond_treeview.h"
#include "../../misc.h"
#include "../zond_tree_store.h"



static gint
db_baum_insert_links( Projekt* zond, gchar** errmsg )
{
    gint ID_start = 0;

    while ( 1 )
    {
        gint rc = 0;
        ZondTreeStore* tree_store = NULL;
        GtkTreeIter* iter_dest = NULL;
        Baum baum = KEIN_BAUM;
        gint node_id = 0;
        gchar* project = NULL;
        Baum baum_target = KEIN_BAUM;
        gint node_id_target = 0;
        GtkTreeIter* iter_target = NULL;
        gboolean child = FALSE;
        gint older_sibling = 0;

        rc = zond_dbase_get_link( zond->dbase_zond->zond_dbase_work, &ID_start, &baum, &node_id,
            &project, &baum_target, &node_id_target, errmsg );
        if ( rc == -1 ) ERROR_S
        else if ( rc == 1 ) break;

        ID_start++;

        //"leeren" iter, der link werden soll, rauslöschen
        //erst herausfinden
        iter_dest = baum_abfragen_iter( zond->treeview[baum], node_id );
        if ( !iter_dest )
        {
            g_free( project );
            ERROR_S_MESSAGE( "baum_abfragen_iter (link) gibt NULL zurück" )
        }

        tree_store = ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[baum]) ));

        //dann löschen
        zond_tree_store_remove( iter_dest );
        gtk_tree_iter_free( iter_dest );

        //iter_target ermitteln
        iter_target = baum_abfragen_iter( zond->treeview[baum_target], node_id_target );
        if ( !iter_target )
        {
            g_free( project );
            ERROR_S_MESSAGE( "baum_abfragen_iter (target) gibt NULL zurück" )
        }

        //iter wo link hinkommt ermitteln (iter_dest)
        older_sibling = zond_dbase_get_older_sibling( zond->dbase_zond->zond_dbase_work, baum, node_id, errmsg );
        if ( older_sibling < 0 )
        {
            g_free( project );
            gtk_tree_iter_free( iter_target );
            ERROR_S
        }
        else if ( older_sibling == 0 ) //erstes Kind
        {
            gint parent = 0;

            child = TRUE;

            parent = zond_dbase_get_parent( zond->dbase_zond->zond_dbase_work, baum, node_id, errmsg );
            if ( parent < 0 )
            {
                g_free( project );
                gtk_tree_iter_free( iter_target );
                ERROR_S
            }
            // else if ( parent == 0 ) -> überflüssig, da iter_dest dann = NULL
            else if ( parent > 0 )
            {
                iter_dest = baum_abfragen_iter( zond->treeview[baum], parent );
                if ( !iter_dest )
                {
                    g_free( project );
                    gtk_tree_iter_free( iter_target );
                    ERROR_S_MESSAGE( "baum_abfragen_iter gibt NULL zurück (I)" )
                }
            }
        }
        else if ( older_sibling > 0 )
        {
            iter_dest = baum_abfragen_iter( zond->treeview[baum], older_sibling );
            if ( !iter_dest )
            {
                g_free( project );
                gtk_tree_iter_free( iter_target );
                ERROR_S_MESSAGE( "baum_abfragen_iter gibt NULL zurück (II)" )
            }
        }

        g_free( project );

        zond_tree_store_insert_link( iter_target->user_data, node_id_target,
                tree_store, iter_dest, child, NULL );
    }

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

    //test auf link nicht erforderlich; Knoten wird dann nur mit node_id eingefügt
    rc = zond_dbase_get_icon_name_and_node_text( zond->dbase_zond->zond_dbase_work,
            baum, node_id, &icon_name, &node_text, errmsg );
    if ( rc == -1 ) ERROR_S
    else if ( rc == 1 ) ERROR_S_MESSAGE( "node_id existiert nicht" )

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
    if ( rc == 1 ) return 0;
    else if ( rc == -1 ) ERROR_S

    //Prüfen, ob Kind- oder Geschwisterknoten vorhanden
    gint first_child_id = 0;
    gint younger_sibling_id = 0;

    first_child_id = zond_dbase_get_first_child( zond->dbase_zond->zond_dbase_work, baum, node_id, errmsg );
    if ( first_child_id < 0 ) ERROR_S

    if ( first_child_id > 0 )
    {
        gint rc = 0;
        rc = db_baum_knoten_mit_kindern( zond, TRUE, baum, first_child_id,
                &iter_inserted, TRUE, NULL, errmsg );
        if ( rc ) ERROR_S
    }

    younger_sibling_id = zond_dbase_get_younger_sibling( zond->dbase_zond->zond_dbase_work, baum, node_id, errmsg );
    if ( younger_sibling_id < 0 ) ERROR_S

    if ( younger_sibling_id > 0 && with_younger_siblings )
    {
        gint rc = 0;

        rc = db_baum_knoten_mit_kindern( zond, TRUE, baum,
                younger_sibling_id, &iter_inserted, FALSE, NULL, errmsg );
        if ( rc ) ERROR_S
    }

    if ( iter_new ) *iter_new = iter_inserted;

    return 0;
}


static gint
db_baum_neu_laden( Projekt* zond, Baum baum, gchar** errmsg )
{
    gint first_node_id = 0;

    zond_tree_store_clear( ZOND_TREE_STORE(gtk_tree_view_get_model(
            GTK_TREE_VIEW(zond->treeview[baum]) )) );

    first_node_id = zond_dbase_get_first_child( zond->dbase_zond->zond_dbase_work, baum, 0, errmsg );
    if ( first_node_id < 0 ) ERROR_S

    if ( first_node_id )
    {
        gint rc = 0;

        rc = db_baum_knoten_mit_kindern( zond, TRUE, baum, first_node_id, NULL,
                TRUE, NULL, errmsg );
        if ( rc ) ERROR_S
    }

    return 0;
}


gint
db_baum_refresh( Projekt* zond, gchar** errmsg )
{
    gint rc = 0;

    rc = db_baum_neu_laden( zond, BAUM_INHALT, errmsg );
    if ( rc ) ERROR_S

    rc = db_baum_neu_laden( zond, BAUM_AUSWERTUNG, errmsg );
    if ( rc ) ERROR_S

    rc = db_baum_insert_links( zond, errmsg );
    if ( rc ) ERROR_S

    gtk_tree_selection_unselect_all( zond->selection[BAUM_AUSWERTUNG] );

    g_object_set( sond_treeview_get_cell_renderer_text( zond->treeview[BAUM_AUSWERTUNG] ),
            "editable", FALSE, NULL);
    g_object_set( sond_treeview_get_cell_renderer_text( zond->treeview[BAUM_INHALT] ),
            "editable", TRUE, NULL);

    gtk_widget_grab_focus( GTK_WIDGET(zond->treeview[BAUM_INHALT]) );

    gtk_widget_set_sensitive( GTK_WIDGET(zond->textview), FALSE );

    return 0;
}

