/*
sond (zond_treeview.c) - Akten, Beweisstücke, Unterlagen
Copyright (C) 2022  pelo america

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

#include "zond_treeview.h"

#include "zond_dbase.h"
#include "zond_tree_store.h"

#include "global_types.h"
#include "../misc.h"
#include "10init/app_window.h"
#include "20allgemein/oeffnen.h"
#include "20allgemein/project.h"
#include "20allgemein/treeviews.h"


typedef struct
{
    Projekt* zond;
} ZondTreeviewPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(ZondTreeview, zond_treeview, SOND_TYPE_TREEVIEW)


static gboolean
zond_treeview_show_popupmenu( GtkTreeView* treeview, GdkEventButton* event,
        GtkMenu* contextmenu_tv )
{
    //Rechtsklick
    if ( ((event->button) == 3) && (event->type == GDK_BUTTON_PRESS) )
    {
        GtkTreePath* path;
        gtk_tree_view_get_cursor( treeview, &path, NULL );
        if ( !path ) return FALSE;
        gtk_tree_path_free( path );

        gtk_menu_popup_at_pointer( contextmenu_tv, NULL );

        return TRUE;
    }

    return FALSE;
}


void
zond_treeview_cursor_changed( ZondTreeview* treeview, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    gint node_id = 0;
    GtkTreeIter iter = { 0, };
    Baum baum = KEIN_BAUM;
    gchar* rel_path = NULL;
    Anbindung* anbindung = NULL;
    gchar* text_label = NULL;
    gchar* text = NULL;

    Projekt* zond = (Projekt*) user_data;

    //wenn kein cursor gesetzt ist
    if ( !sond_treeview_get_cursor( SOND_TREEVIEW(treeview), &iter ) ||
            treeviews_get_baum_and_node_id( zond, &iter, &baum, &node_id ) )
    {
        gtk_label_set_text( zond->label_status, "" ); //statur-label leeren
        gtk_text_buffer_set_text( gtk_text_view_get_buffer( zond->textview ), "", -1 );
        gtk_widget_set_sensitive( GTK_WIDGET(zond->textview), FALSE );

        return;
    }
    else if ( baum == BAUM_AUSWERTUNG ) gtk_widget_set_sensitive( GTK_WIDGET(zond->textview), TRUE );
    else gtk_widget_set_sensitive( GTK_WIDGET(zond->textview), FALSE );

    //status_label setzen
    rc = treeviews_get_rel_path_and_anbindung( zond, baum, node_id, &rel_path,
            &anbindung, &errmsg );
    if ( rc == -1 )
    {
        text_label = g_strconcat( "Fehler in ", __func__, ":\n\n Bei Aufruf "
                "abfragen_rel_path_and_anbindung:", errmsg, NULL );
        g_free( errmsg );
    }

    if ( rc == 2 ) text_label = g_strdup( "Keine Anbindung" );
    else if ( rc == 1 ) text_label = g_strdup( rel_path );
    else if ( rc == 0 )
    {
        text_label = g_strdup_printf( "%s, von Seite %i, "
                "Index %i, bis Seite %i, index %i", rel_path,
                anbindung->von.seite + 1, anbindung->von.index, anbindung->bis.seite + 1,
                anbindung->bis.index );
        g_free( anbindung );
    }

    gtk_label_set_text( zond->label_status, text_label );
    g_free( text_label );
    g_free( rel_path );

    if ( baum == BAUM_INHALT || rc == -1 ) return;

    //TextBuffer laden
    GtkTextBuffer* buffer = gtk_text_view_get_buffer( zond->textview );

    //neuen text einfügen
    rc = zond_dbase_get_text( zond->dbase_zond->zond_dbase_work, node_id, &text, &errmsg );
    if ( rc )
    {
        text_label = g_strconcat( "Fehler in ", __func__, ": Bei Aufruf "
                "zond_dbase_get_text: ", errmsg, NULL );
        g_free( errmsg );
        gtk_label_set_text( zond->label_status, text_label );
        g_free( text_label );

        return;
    }

    if ( text )
    {
        gtk_text_buffer_set_text( buffer, text, -1 );
        g_free( text );
    }
    else gtk_text_buffer_set_text( buffer, "", -1 );

    g_object_set_data( G_OBJECT(gtk_text_view_get_buffer( zond->textview )),
            "changed", NULL );
    g_object_set_data( G_OBJECT(zond->textview),
            "node-id", GINT_TO_POINTER(node_id) );

    return;
}


static void
zond_treeview_row_activated( GtkWidget* ztv, GtkTreePath* tp, GtkTreeViewColumn* tvc,
        gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    gint node_id = 0;
    Baum baum = KEIN_BAUM;
    GtkTreeIter iter = { 0, };

    Projekt* zond = (Projekt*) user_data;

    if ( !gtk_tree_model_get_iter( gtk_tree_view_get_model( GTK_TREE_VIEW(ztv) ),
            &iter, tp ) ) return;

    rc = treeviews_get_baum_and_node_id( zond, &iter, &baum, &node_id );
    if ( rc ) return;

    rc = oeffnen_node( zond, baum, node_id, &errmsg );
    if ( rc )
    {
        display_message( zond->app_window, "Fehler beim Öffnen Knoten:\n\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


static void
zond_treeview_cell_edited( GtkCellRenderer* cell, gchar* path_string, gchar* new_text,
        gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    Baum baum = KEIN_BAUM;
    gint node_id = 0;
    GtkTreeIter iter = { 0, };

    ZondTreeview* ztv = (ZondTreeview*) user_data;
    ZondTreeviewPrivate* ztv_priv = zond_treeview_get_instance_private( ztv );

    gtk_tree_model_get_iter_from_string( gtk_tree_view_get_model( GTK_TREE_VIEW(ztv) ), &iter, path_string );

    rc = treeviews_get_baum_and_node_id( ztv_priv->zond, &iter, &baum, &node_id );
    if ( rc ) return;

    //node_id holen, node_text in db ändern
    rc = zond_dbase_set_node_text( ztv_priv->zond->dbase_zond->zond_dbase_work, baum, node_id, new_text, &errmsg );
    if ( rc )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(ztv) ), "Knoten umbenennen nicht möglich\n\n"
                "Bei Aufruf zond_dbase_set_node_text:\n", errmsg, NULL );
        g_free( errmsg );
    }
    else
    {
        zond_tree_store_set( &iter, NULL, new_text, 0 );
        gtk_tree_view_columns_autosize( GTK_TREE_VIEW(ztv) );
    }

    ztv_priv->zond->key_press_signal = g_signal_connect( ztv_priv->zond->app_window, "key-press-event",
            G_CALLBACK(cb_key_press), ztv_priv->zond );

    return;
}


static void
zond_treeview_render_node_text( GtkTreeViewColumn* column, GtkCellRenderer* renderer,
        GtkTreeModel* model, GtkTreeIter* iter, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    Baum baum = KEIN_BAUM;
    gint node_id = 0;

    ZondTreeview* ztv = (ZondTreeview*) data;
    ZondTreeviewPrivate* ztv_priv = zond_treeview_get_instance_private( ztv );

    if ( zond_tree_store_is_link( iter ) )
    {
        gchar *label = NULL;
        gchar *markuptxt = NULL;

        // Retrieve the current label
        gtk_tree_model_get( model, iter, 1, &label, -1);

        markuptxt = g_strdup_printf("<i>%s</i>", label);

        g_object_set( renderer, "markup", markuptxt, NULL);  // markup isn't showing and text field is blank due to "text" == NULL

        g_free( markuptxt );
    }

    rc = treeviews_get_baum_and_node_id( ztv_priv->zond, iter, &baum, &node_id );
    if ( rc ) return;

    if ( baum == BAUM_AUSWERTUNG )
    {
        gchar* text = NULL;

        //Hintergrund icon rot wenn Text in textview
        rc = zond_dbase_get_text( ztv_priv->zond->dbase_zond->zond_dbase_work,
                node_id, &text, &errmsg );
        if ( rc )
        {
            gchar* text_label = NULL;
            text_label = g_strconcat( "Fehler in treeviews_render_node_text -\n\n"
                    "Bei Aufruf zond_dbase_get_text:\n", errmsg, NULL );
            g_free( errmsg );
            gtk_label_set_text( ztv_priv->zond->label_status, text_label );
            g_free( text_label );
        }
        else if ( !text || !g_strcmp0( text, "" ) )
                g_object_set( renderer, "background-set", FALSE, NULL );
        else g_object_set( renderer, "background-set", TRUE, NULL );

        g_free( text );
    }

    return;
}


static void
zond_treeview_constructed( GObject* self )
{
    //Die Signale
    //Rechtsklick - Kontextmenu
    //Kontextmenu erzeugen, welches bei Rechtsklick auf treeview angezeigt wird
    GtkWidget* contextmenu_tv = gtk_menu_new();

    GtkWidget* datei_oeffnen_item = gtk_menu_item_new_with_label( "Öffnen" );
    gtk_menu_shell_append( GTK_MENU_SHELL(contextmenu_tv), datei_oeffnen_item );
    gtk_widget_show( datei_oeffnen_item );

    g_signal_connect( self, "button-press-event",
            G_CALLBACK(zond_treeview_show_popupmenu), (gpointer) contextmenu_tv );

//        g_signal_connect( datei_oeffnen_item, "activate",
//                G_CALLBACK(cb_datei_oeffnen), (gpointer) zond );

    //Zeile expandiert oder kollabiert
    g_signal_connect( self, "row-expanded",
            G_CALLBACK(gtk_tree_view_columns_autosize), NULL );
    g_signal_connect( self, "row-collapsed",
            G_CALLBACK(gtk_tree_view_columns_autosize), NULL );

    g_signal_connect( sond_treeview_get_cell_renderer_text( SOND_TREEVIEW(self) ),
            "edited", G_CALLBACK(zond_treeview_cell_edited), (gpointer) self ); //Klick in textzelle = Inhalt editieren

    //chain-up
    G_OBJECT_CLASS(zond_treeview_parent_class)->constructed( self );

    return;
}


static void
zond_treeview_class_init( ZondTreeviewClass* klass )
{
    G_OBJECT_CLASS(klass)->constructed = zond_treeview_constructed;

    SOND_TREEVIEW_CLASS(klass)->render_text_cell = zond_treeview_render_node_text;

    return;
}


static void
zond_treeview_init( ZondTreeview* ztv )
{
    //Tree-Model erzeugen und verbinden
    ZondTreeStore* tree_store = zond_tree_store_new( );

    gtk_tree_view_set_model( GTK_TREE_VIEW(ztv), GTK_TREE_MODEL(tree_store) );
    g_object_unref( tree_store );

    gtk_tree_view_set_headers_visible( GTK_TREE_VIEW(ztv), FALSE );

    gtk_tree_view_column_set_attributes(
            sond_treeview_get_column( SOND_TREEVIEW(ztv) ),
            sond_treeview_get_cell_renderer_icon( SOND_TREEVIEW(ztv) ),
            "icon-name", 0, NULL);
    gtk_tree_view_column_set_attributes(
            sond_treeview_get_column( SOND_TREEVIEW(ztv) ),
            sond_treeview_get_cell_renderer_text( SOND_TREEVIEW(ztv) ),
            "text", 1, NULL);

    return;
}


ZondTreeview*
zond_treeview_new( Projekt* zond, gint id )
{
    ZondTreeview* ztv = NULL;
    ZondTreeviewPrivate* ztv_priv = NULL;

    ztv = g_object_new( ZOND_TYPE_TREEVIEW, NULL );

    ztv_priv = zond_treeview_get_instance_private( ztv );
    ztv_priv->zond = zond;
    sond_treeview_set_id( SOND_TREEVIEW(ztv), id );

    // Doppelklick = angebundene Datei anzeigen
    g_signal_connect( ztv, "row-activated",
            G_CALLBACK(zond_treeview_row_activated), (gpointer) ztv_priv->zond );


    return ztv;
}


static gboolean
zond_treeview_iter_foreach_node_id( GtkTreeModel* model, GtkTreePath* path,
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
zond_treeview_abfragen_iter( ZondTreeview* treeview, gint node_id )
{
    GtkTreeIter* iter = NULL;
    GtkTreeModel* model = gtk_tree_view_get_model( GTK_TREE_VIEW(treeview) );

    g_object_set_data( G_OBJECT(model), "node_id", GINT_TO_POINTER(node_id) );
    gtk_tree_model_foreach( model, (GtkTreeModelForeachFunc)
            zond_treeview_iter_foreach_node_id, &iter );

    return iter;
}


static gboolean
zond_treeview_foreach_path( GtkTreeModel* model, GtkTreePath* path, GtkTreeIter* iter,
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

//Ggf. ausschleichen oder vereinheitlichen mit ...abfragen_iter
GtkTreePath*
zond_treeview_get_path( SondTreeview* treeview, gint node_id )
{
    GtkTreePath* path = NULL;
    GtkTreeModel* model = gtk_tree_view_get_model( GTK_TREE_VIEW(treeview) );

    g_object_set_data( G_OBJECT(model), "node_id", GINT_TO_POINTER(node_id) );
    gtk_tree_model_foreach( model, (GtkTreeModelForeachFunc)
            zond_treeview_foreach_path, &path );

    return path;
}



