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
#include "zond_treeviewfm.h"

#include "global_types.h"
#include "../misc.h"
#include "../sond_treeviewfm.h"
#include "10init/app_window.h"
#include "10init/headerbar.h"

#include "20allgemein/oeffnen.h"
#include "20allgemein/project.h"

#include "99conv/general.h"



typedef struct
{
    Projekt* zond;
} ZondTreeviewPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(ZondTreeview, zond_treeview, SOND_TYPE_TREEVIEW)


void
zond_treeview_cursor_changed( ZondTreeview* treeview, gpointer user_data )
{
    gint rc = 0;
    gint node_id = 0;
    GtkTreeIter iter = { 0, };
    gint type = 0;
    gint link = 0;
    gchar* rel_path = NULL;
    Anbindung anbindung = { 0 };
    gchar* text_label = NULL;
    gchar* text = NULL;
    GError* error = NULL;

    Projekt* zond = (Projekt*) user_data;

    //wenn kein cursor gesetzt ist
    //also letzter Punkt gelöscht wird
    if ( !sond_treeview_get_cursor( SOND_TREEVIEW(treeview), &iter ) )
    {
        gtk_label_set_text( zond->label_status, "" ); //status-label leeren
        //textview deaktivieren - egal welcher baum
        gtk_widget_set_sensitive( GTK_WIDGET(zond->textview), FALSE );
        //textview leeren
        gtk_text_buffer_set_text( gtk_text_view_get_buffer( GTK_TEXT_VIEW(zond->textview) ),
                "", -1 );
        zond->node_id_act = 0;

        //falls extra-textview auf gelöschten Punkt
        if ( zond->node_id_extra )
        {
            gint root = 0;
            gint rc = 0;

            rc = zond_dbase_get_tree_root( zond->dbase_zond->zond_dbase_work,
                    zond->node_id_extra, &root, &error );
            if ( rc )
            {
                display_message( zond->app_window, "Fehler\n\n", error->message, NULL );
                g_error_free( error );

                return;
            }

            if ( root == sond_treeview_get_id( SOND_TREEVIEW(treeview) ) )
            {
                gboolean ret = FALSE;

                gtk_text_buffer_set_text( gtk_text_view_get_buffer( GTK_TEXT_VIEW(zond->textview_ii) ),
                        "", -1 );

                //Text-Fenster verstecken (falls nicht schn ist, Überprüfung aber überflüssig
                g_signal_emit_by_name( zond->textview_window, "delete-event", zond, &ret );

                //Vorsichtshalber auch Menüpunkt deaktivieren
                gtk_widget_set_sensitive( zond->menu.textview_extra, FALSE );

                zond->node_id_extra = 0;
            }
        }

        return;
    }

    gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(treeview) ), &iter, 2, &node_id, -1 );

    //Wenn gleicher Knoten: direkt zurück
    if ( node_id == zond->node_id_act ) return;

    //status_label setzen
    rc = zond_dbase_get_node( zond->dbase_zond->zond_dbase_work,
            node_id, &type, &link, &rel_path, &anbindung.von.seite, &anbindung.von.index,
            &anbindung.bis.seite, &anbindung.bis.index, NULL, NULL, NULL, &error );
    if ( rc )
    {
        display_message( zond->app_window, "Fehler\n\n", error->message, NULL );
        g_error_free( error );

        return;
    }

    if ( type == ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_COPY )
    { //dann link-node holen
        gint rc = 0;
        gint node_id_link = 0;

        g_free( rel_path );
        rel_path = NULL;
        node_id_link = link;

        rc = zond_dbase_get_node( zond->dbase_zond->zond_dbase_work,
                node_id_link, &type, &link, &rel_path, &anbindung.von.seite, &anbindung.von.index,
                &anbindung.bis.seite, &anbindung.bis.index, NULL, NULL, NULL, &error );
        if ( rc )
        {
            display_message( zond->app_window, "Fehler\n\n", error->message, NULL );
            g_error_free( error );

            return;
        }
    }

    if ( type == ZOND_DBASE_TYPE_BAUM_INHALT_FILE ||
            type == ZOND_DBASE_TYPE_BAUM_INHALT_VIRT_PDF ||
            type == ZOND_DBASE_TYPE_BAUM_INHALT_FILE_PART )
            text_label = g_strdup( rel_path );
    else
    {
        text_label = g_strdup_printf( "%s, S. %i, "
                "Index %i", rel_path,
                anbindung.von.seite + 1, anbindung.von.index );
        if ( anbindung.bis.seite || anbindung.bis.index )
                text_label = add_string( text_label,
                g_strdup_printf( " - S. %i, Index %i", anbindung.bis.seite,
                anbindung.bis.index ) );
    }

    g_free( rel_path );

    gtk_label_set_text( zond->label_status, text_label );
    g_free( text_label );

    //neuer Knoten == Extra-Fenster und vorheriger Knoten nicht
    if ( zond->node_id_extra && node_id == zond->node_id_extra &&
            zond->node_id_act != zond->node_id_extra )
            gtk_text_view_set_buffer( GTK_TEXT_VIEW(zond->textview),
            gtk_text_view_get_buffer( GTK_TEXT_VIEW(zond->textview_ii) ) );
    else //alle anderen Fälle:
            //1. Extra-Fenster geschlossen (!zond->node_id_extra)
            //2. vorher Extra-Fenster, jetzt nicht mehr
            //3. vorher nicht Extra-Fenster, jetzt auch nicht
    {
        GtkTextBuffer* buffer = NULL;

        //Falls vorher Extra-Fenster: neuen Buffer erzeugen
        //(Daß gleicher Knoten wurde oben schon ausgeschlossen)
        if ( zond->node_id_act == zond->node_id_extra )
        {
            //neuen Buffer erzeugen und ins "normale" Textview
            GtkTextIter text_iter = { 0 };

            buffer = gtk_text_buffer_new( NULL );
            gtk_text_buffer_get_end_iter( buffer, &text_iter );
            gtk_text_buffer_create_mark( buffer, "ende-text", &text_iter, FALSE );

            gtk_text_view_set_buffer( GTK_TEXT_VIEW(zond->textview), buffer );
        }
        //ansonsten: alten buffer nehmen
        else buffer = gtk_text_view_get_buffer( GTK_TEXT_VIEW(zond->textview) );

        //neuen text einfügen
        rc = zond_dbase_get_text( zond->dbase_zond->zond_dbase_work, node_id, &text, &error );
        if ( rc )
        {
            text_label = g_strconcat( "Fehler in ", __func__, ": Bei Aufruf "
                    "zond_dbase_get_text: ", error->message, NULL );
            g_error_free( error );
            gtk_label_set_text( zond->label_status, text_label );
            g_free( text_label );

            return;
        }

        if ( text )
        {
            gtk_text_buffer_set_text( buffer, text, -1 );
            gtk_text_view_scroll_to_mark( GTK_TEXT_VIEW(zond->textview),
                    gtk_text_buffer_get_mark( gtk_text_view_get_buffer(
                    GTK_TEXT_VIEW(zond->textview) ), "ende-text" ), 0.0,
                    FALSE, 0.0, 0.0 );
            g_free( text );
        }
        else gtk_text_buffer_set_text( buffer, "", -1 );
    }

    zond->node_id_act = node_id;

    return;
}


static void
zond_treeview_row_expanded( GtkTreeView* tree_view, GtkTreeIter* iter,
        GtkTreePath* path, gpointer data )
{
    //link hat Kind das ist noch dummy: link laden
    if ( zond_tree_store_link_is_unloaded( iter ) ) zond_tree_store_load_link( iter );

    return;
}


static gint
zond_treeview_open_path( Projekt* zond, GtkTreeView* tree_view, GtkTreePath* tree_path,
        gboolean open_with, gchar** errmsg )
{
    gint rc = 0;
    GtkTreeIter iter = { 0 };

    gtk_tree_model_get_iter( gtk_tree_view_get_model( tree_view ), &iter, tree_path );

    rc = oeffnen_node( zond, &iter, open_with, errmsg );
    if ( rc ) ERROR_S

    return 0;
}


static void
zond_treeview_row_activated( GtkWidget* ztv, GtkTreePath* tp, GtkTreeViewColumn* tvc,
        gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    Projekt* zond = (Projekt*) user_data;

    rc = zond_treeview_open_path( zond, GTK_TREE_VIEW(ztv), tp, FALSE, &errmsg );
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
    GError* error = NULL;
    gint node_id = 0;
    GtkTreeIter iter = { 0, };
    gint type = 0;
    gint link = 0;

    ZondTreeview* ztv = (ZondTreeview*) user_data;
    ZondTreeviewPrivate* ztv_priv = zond_treeview_get_instance_private( ztv );

    gtk_tree_model_get_iter_from_string( gtk_tree_view_get_model( GTK_TREE_VIEW(ztv) ), &iter, path_string );
    gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(ztv) ), &iter, 2, &node_id, -1 );

    //node_id holen, node_text in db ändern
    rc = zond_dbase_update_node_text( ztv_priv->zond->dbase_zond->zond_dbase_work, node_id, new_text, &error );
    if ( rc )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(ztv) ),
                "Knoten umbenennen nicht möglich\n\n", error->message, NULL );
        g_error_free( error );

        return;
    }

    rc = zond_dbase_get_type_and_link( ztv_priv->zond->dbase_zond->zond_dbase_work,
            node_id, &type, &link, &error );
    if ( rc )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(ztv) ),
                "Knoten umbenennen nicht möglich\n\n", error->message, NULL );
        g_error_free( error );

        return;
    }

    //BAUM_INHALT_PDF_ABSCHNITT und zugehöriger PDF_ABSCHNITT synchron halten...
    if ( type == ZOND_DBASE_TYPE_BAUM_INHALT_PDF_ABSCHNITT )
    {
        gint rc = 0;

        rc = zond_dbase_update_node_text( ztv_priv->zond->dbase_zond->zond_dbase_work,
                link, new_text, &error );
        if ( rc )
        {
            display_message( gtk_widget_get_toplevel( GTK_WIDGET(ztv) ),
                    "Knoten umbenennen nicht möglich\n\n", error->message, NULL );
            g_error_free( error );

            return;
        }
    }
    zond_tree_store_set( &iter, NULL, new_text, 0 );
    gtk_tree_view_columns_autosize( GTK_TREE_VIEW(ztv) );

//evtl. überflüssig, weil sond_treeview signal blockt
//    ztv_priv->zond->key_press_signal = g_signal_connect( ztv_priv->zond->app_window, "key-press-event",
//            G_CALLBACK(cb_key_press), ztv_priv->zond );

    return;
}


static void
zond_treeview_render_node_text( GtkTreeViewColumn* column, GtkCellRenderer* renderer,
        GtkTreeModel* model, GtkTreeIter* iter, gpointer data )
{
    gint rc = 0;
    gint node_id = 0;
    gchar* text = NULL;
    GError* error = NULL;

    ZondTreeview* ztv = (ZondTreeview*) data;
    ZondTreeviewPrivate* ztv_priv = zond_treeview_get_instance_private( ztv );

    if ( zond_tree_store_is_link( iter ) )
    {
        gchar *label = NULL;
        gchar *markuptxt = NULL;

        // Retrieve the current label
        gtk_tree_model_get( model, iter, 1, &label, -1);

        markuptxt = g_strdup_printf("<i>%s</i>", label);

        if ( zond_tree_store_get_link_head_nr( iter ) )
        {
            markuptxt = add_string( g_strdup( "<span weight=\"bold\">" ), markuptxt );
            markuptxt = add_string( markuptxt, g_strdup( "</span>" ) );
        }

        g_object_set( renderer, "markup", markuptxt, NULL);  // markup isn't showing and text field is blank due to "text" == NULL

        g_free( markuptxt );
    }

    //Hintergrund icon rot wenn Text in textview
    rc = zond_dbase_get_text( ztv_priv->zond->dbase_zond->zond_dbase_work,
            node_id, &text, &error );
    if ( rc )
    {
        gchar* text_label = NULL;
        text_label = g_strconcat( "Fehler in treeviews_render_node_text -\n\n"
                "Bei Aufruf zond_dbase_get_text:\n", error->message, NULL );
        g_error_free( error );
        gtk_label_set_text( ztv_priv->zond->label_status, text_label );
        g_free( text_label );
    }
    else if ( !text || !g_strcmp0( text, "" ) )
            g_object_set( renderer, "background-set", FALSE, NULL );
    else g_object_set( renderer, "background-set", TRUE, NULL );

    g_free( text );

    return;
}


static void
zond_treeview_constructed( GObject* self )
{
    //Die Signale
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
    ZondTreeStore* tree_store = g_object_new( ZOND_TYPE_TREE_STORE, NULL );

    gtk_tree_view_set_model( GTK_TREE_VIEW(ztv), GTK_TREE_MODEL(tree_store) );
    g_object_unref( tree_store );

    gtk_tree_view_set_headers_visible( GTK_TREE_VIEW(ztv), FALSE );

    gtk_tree_view_column_set_attributes(
            gtk_tree_view_get_column( GTK_TREE_VIEW(ztv), 0 ),
            sond_treeview_get_cell_renderer_icon( SOND_TREEVIEW(ztv) ),
            "icon-name", 0, NULL);
    gtk_tree_view_column_set_attributes(
            gtk_tree_view_get_column( GTK_TREE_VIEW(ztv), 0 ),
            sond_treeview_get_cell_renderer_text( SOND_TREEVIEW(ztv) ),
            "text", 1, NULL);

    return;
}


gboolean
zond_treeview_get_anchor( Projekt* zond, gboolean child, GtkTreeIter* iter_cursor,
        GtkTreeIter* iter_anchor, gint* anchor_id )
{
    GtkTreeIter iter_cursor_intern = { 0 };
    GtkTreeIter iter_anchor_intern = { 0 };
    gint head_nr = 0;

    if ( !sond_treeview_get_cursor( zond->treeview[zond->baum_active],
            &iter_cursor_intern ) )
    {
        //Trick, weil wir keinen gültigen iter übergeben können->
        //setzten stamp auf stamp des "richtigen" tree_stores und
        //user_data auf root_node
        ZondTreeStore* store = NULL;

        store = ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[zond->baum_active]) ));

        if ( iter_cursor )
        {
            iter_cursor->stamp = zond_tree_store_get_stamp( store );
            iter_cursor->user_data = zond_tree_store_get_root_node( store );
        }
        if ( iter_anchor )
        {
            iter_anchor->stamp = zond_tree_store_get_stamp( store );
            iter_anchor->user_data = zond_tree_store_get_root_node( store );
        }

        if ( anchor_id ) *anchor_id = zond_tree_store_get_root( store );

        return FALSE; //heißt: eigentlich kein cursor - fake-iter mit root gebildet
    }

    if ( child ) zond_tree_store_get_iter_target( &iter_cursor_intern, &iter_anchor_intern );
    else
    {
        if ( (head_nr = zond_tree_store_get_link_head_nr( &iter_cursor_intern )) <= 0 )
                zond_tree_store_get_iter_target( &iter_cursor_intern, &iter_anchor_intern );
        else iter_anchor_intern = iter_cursor_intern; //wenn iter_cursor head-link, dann ist link und nicht target anchor
    }

    if ( iter_cursor ) *iter_cursor = iter_cursor_intern;

    if ( anchor_id )
    {
        if ( head_nr <= 0 ) gtk_tree_model_get( GTK_TREE_MODEL(zond_tree_store_get_tree_store(
                &iter_anchor_intern )), &iter_anchor_intern, 2, anchor_id, -1 );
        else *anchor_id = head_nr;
    }

    if ( iter_anchor ) *iter_anchor = iter_anchor_intern;

    return TRUE;
}


static gint
zond_treeview_hat_vorfahre_datei( Projekt* zond, gint anchor_id, gboolean child, GError** error )
{
    gint rc = 0;
    gint type = 0;

    if ( anchor_id == 0 ) return 0;

    if ( !child )
    {
        gint parent_id = 0;

        rc = zond_dbase_get_parent( zond->dbase_zond->zond_dbase_work, anchor_id,
                &parent_id, error );
        if ( rc ) ERROR_Z

        anchor_id = parent_id;
    }

    rc = zond_dbase_get_type_and_link( zond->dbase_zond->zond_dbase_work,
            anchor_id, &type, NULL, error );
    if ( rc ) ERROR_Z

    if ( type != ZOND_DBASE_TYPE_BAUM_STRUKT &&
            type != ZOND_DBASE_TYPE_BAUM_ROOT ) return 1;

    return 0;
}


static gint
zond_treeview_insert_node( Projekt* zond, gboolean child, gchar** errmsg )
{
    gint anchor_id = 0;
    gint rc = 0;
    gint node_id_new = 0;
    GtkTreeIter iter_cursor = { 0 };
    GtkTreeIter iter_anchor = { 0 };
    GtkTreeIter iter_origin = { 0 };
    GtkTreeIter iter_new = { 0 };
    gboolean success = FALSE;
    ZondTreeStore* tree_store = NULL;
    GError* error = NULL;

    g_return_val_if_fail( zond->baum_active == BAUM_INHALT || zond->baum_active == BAUM_AUSWERTUNG, -1);

    if ( !(success = zond_treeview_get_anchor( zond, child, &iter_cursor,
            &iter_anchor, &anchor_id )) ) child = TRUE;

    if ( zond_tree_store_get_root( zond_tree_store_get_tree_store( &iter_anchor ) ) == BAUM_INHALT )
    {
        gint rc = 0;

        rc = zond_treeview_hat_vorfahre_datei( zond, anchor_id, child, &error );
        if ( rc == -1 )
        {
            if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__,
                    error->message );
            g_error_free( error );

            return -1;
        }
        else if ( rc == 1 ) return 1;
    }

    if ( success ) iter_origin = iter_cursor;

    rc = zond_dbase_begin( zond->dbase_zond->zond_dbase_work, &error );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
        g_error_free( error );

        return -1;
    }

    //Knoten in Datenbank einfügen
    node_id_new = zond_dbase_insert_node( zond->dbase_zond->zond_dbase_work,
            anchor_id, child, ZOND_DBASE_TYPE_BAUM_STRUKT, 0, NULL, 0, 0, 0, 0,
            zond->icon[ICON_NORMAL].icon_name, "Neuer Punkt", NULL, &error );
    if ( node_id_new == -1 )
    {
        if ( errmsg ) *errmsg = g_strdup( error->message );
        g_error_free( error );
        ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )
    }

    rc = zond_dbase_commit( zond->dbase_zond->zond_dbase_work, &error );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strdup( error->message );
        g_error_free( error );
        ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )
    }

    //Knoten in baum_inhalt einfuegen
    //success = sond_treeview_get_cursor( zond->treeview[baum], &iter ); - falsch!!!

    tree_store = zond_tree_store_get_tree_store( &iter_anchor );
    zond_tree_store_insert( tree_store, (success) ? &iter_anchor : NULL, child, &iter_new );

    //Standardinhalt setzen
    zond_tree_store_set( &iter_new, zond->icon[ICON_NORMAL].icon_name, "Neuer Punkt", node_id_new );

    if ( child && success )
            sond_treeview_expand_row( zond->treeview[zond->baum_active], &iter_origin );
    sond_treeview_set_cursor( zond->treeview[zond->baum_active], &iter_new );

    return 0;
}


static void
zond_treeview_punkt_einfuegen_activate( GtkMenuItem* item, gpointer user_data )
{
    gint rc = 0;
    gboolean child = FALSE;
    gchar* errmsg = NULL;

    Projekt* zond = (Projekt*) user_data;

    child = (gboolean) GPOINTER_TO_INT(g_object_get_data( G_OBJECT(item), "kind" ));

    rc = zond_treeview_insert_node( zond, child, &errmsg);
    if ( rc == -1 )
    {
        display_message( zond->app_window, "Punkt einfügen fehlgeschlagen\n\n",
                errmsg, NULL );
        g_free( errmsg );
    }
    else if ( rc == 1 ) display_message( zond->app_window, "Punkt darf nicht "
            "als Unterpunkt von Datei eingefügt weden", NULL );

    return;
}


static const gchar*
zond_treeview_get_icon_name( GFileInfo* info )
{
    const gchar* icon_name = NULL;
    const gchar* content_type = NULL;

    content_type = g_file_info_get_content_type( info );
    if ( !content_type ) return "dialog-error";

    if ( g_content_type_is_mime_type( content_type, "application/pdf" ) ) icon_name = "pdf";
    else if ( g_content_type_is_a( content_type, "audio" ) ) icon_name = "audio-x-generic";
    else icon_name = "dialog-error";

    return icon_name;
}


gint
zond_treeview_walk_tree( ZondTreeview* ztv, gboolean with_younger_siblings,
        gint node_id, GtkTreeIter* iter_anchor, gboolean child, GtkTreeIter* iter_inserted,
        gint anchor_id, gint* node_id_inserted,
        gint (*walk_tree) (ZondTreeview*, gint, GtkTreeIter*, gboolean,
        GtkTreeIter*, gint, gint*, GError**), GError** error )
{
    gint rc = 0;
    gint first_child = 0;
    gint node_id_new = 0;

    ZondTreeviewPrivate* ztv_priv = zond_treeview_get_instance_private( ztv );

    rc = walk_tree( ztv, node_id, iter_anchor, child, iter_inserted, anchor_id, &node_id_new, error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    rc = zond_dbase_get_first_child( ztv_priv->zond->dbase_zond->zond_dbase_work,
            node_id, &first_child, error );
    if ( rc ) ERROR_Z
    else if ( first_child > 0 )
    {
        gint rc = 0;

        rc = zond_treeview_walk_tree( ztv, TRUE, first_child, iter_inserted, TRUE,
            NULL, node_id_new, NULL, walk_tree, error );
        if ( rc ) ERROR_Z
    }

    if ( with_younger_siblings )
    {
        gint younger_sibling = 0;

        rc = zond_dbase_get_younger_sibling( ztv_priv->zond->dbase_zond->zond_dbase_work,
                node_id, &younger_sibling, error );
        if ( rc ) ERROR_Z
        else if ( younger_sibling > 0 )
        {
            rc = zond_treeview_walk_tree( ztv, TRUE, younger_sibling, iter_inserted, FALSE,
                    NULL, node_id_new, NULL, walk_tree, error );
            if ( rc ) ERROR_Z
        }
    }

    if ( node_id_inserted ) *node_id_inserted = node_id_new;

    return 0;
}


static gint
zond_treeview_insert_pdf_abschnitt( ZondTreeview* ztv, gint node_id,
        GtkTreeIter* iter, gboolean child, GtkTreeIter* iter_inserted,
        gint anchor_id, gint* node_id_inserted, GError** error )
{
    gint rc = 0;
    gchar* icon_name = NULL;
    gchar* node_text = NULL;
    GtkTreeIter iter_new = { 0 };

    ZondTreeviewPrivate* ztv_priv = zond_treeview_get_instance_private( ztv );

    rc = zond_dbase_get_node( ztv_priv->zond->dbase_zond->zond_dbase_work,
            node_id, 0, 0, NULL, 0, 0, 0, 0, &icon_name, &node_text, NULL, error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    zond_tree_store_insert( ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(ztv) )), iter,
            child, &iter_new);
    zond_tree_store_set( &iter_new, icon_name, node_text, node_id );

    g_free( icon_name);
    g_free( node_text );

    if ( iter_inserted ) *iter_inserted = iter_new;

    return 0;
}


static gint
zond_treeview_load_baum_inhalt_file( ZondTreeview* ztv,
        GtkTreeIter* iter_anchor, gboolean child, gint node_id,
        const gchar* icon_name, const gchar* rel_path,
        GtkTreeIter* iter_new, GError** error )
{
    gint rc = 0;
    gint pdf_root = 0;

    ZondTreeviewPrivate* ztv_priv = zond_treeview_get_instance_private( ztv );

    zond_tree_store_insert( ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(ztv) )),
            iter_anchor, child, iter_new );

    zond_tree_store_set( iter_new, icon_name, rel_path, node_id );

    rc = zond_dbase_get_pdf_root( ztv_priv->zond->dbase_zond->zond_dbase_work,
            rel_path, &pdf_root, error );
    if ( rc ) ERROR_Z

    if ( pdf_root )
    {
        gint rc = 0;
        gint pdf_abschnitt = 0;

        rc = zond_dbase_get_first_child( ztv_priv->zond->dbase_zond->zond_dbase_work,
                pdf_root, &pdf_abschnitt, error );
        if ( rc ) ERROR_Z

        if ( pdf_abschnitt )
        {
            gint rc = 0;

            rc = zond_treeview_walk_tree( ztv, TRUE, pdf_abschnitt, iter_new, TRUE,
                    NULL, 0, NULL, zond_treeview_insert_pdf_abschnitt, error );
            if ( rc ) ERROR_Z
        }
    }

    return 0;
}


/** Fehler: -1
    eingefügt: node_id
    nicht eingefügt, weil schon angebunden: 0 **/
static gint
zond_treeview_datei_anbinden( ZondTreeview* ztv, GtkTreeIter* anchor_iter,
        gint anchor_id, gboolean child, GFileInfo* info, gchar* rel_path,
        InfoWindow* info_window, gint* zaehler, gchar** errmsg )
{
    gint rc = 0;
    gint new_node_id = 0;
    const gchar* icon_name = NULL;
    GtkTreeIter iter_new = { 0 };
    GError* error = NULL;

    ZondTreeviewPrivate* ztv_priv = zond_treeview_get_instance_private( ztv );

    if ( info_window->cancel ) return -2;

    //Prüfen, ob PDF oder Abschnitt hiervon schon angebunden
    if ( rc > 0 )
    {
        gchar* text = add_string( rel_path, g_strdup( " ...bereits angebunden" ) );
        info_window_set_message( info_window, text );
        g_free( text );

        return 0; //Wenn angebunden: nix machen
    }

    info_window_set_message( info_window, rel_path );

    icon_name = zond_treeview_get_icon_name( info );

    new_node_id = zond_dbase_insert_node( ztv_priv->zond->dbase_zond->zond_dbase_work,
            anchor_id, child, ZOND_DBASE_TYPE_BAUM_INHALT_FILE, 0, rel_path, 0, 0, 0, 0,
            icon_name, rel_path, NULL, &error );
    if ( new_node_id == -1 )
    {
        if ( errmsg ) *errmsg = g_strdup( error->message );
        g_error_free( error );

        return -1;
    }

    rc = zond_treeview_load_baum_inhalt_file( ztv, (anchor_iter->user_data ==
            zond_tree_store_get_root_node( ZOND_TREE_STORE(gtk_tree_view_get_model(
            GTK_TREE_VIEW(ztv) )) )) ? NULL : anchor_iter, child, new_node_id, icon_name,
            rel_path, &iter_new, &error );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
        g_error_free( error );

        return -1;
    }

    *anchor_iter = iter_new;
    child = FALSE;
    (*zaehler)++;

    return new_node_id;
}


/*  Fehler: Rückgabe -1
**  ansonsten: Id des zunächst erzeugten Knotens  */
static gint
zond_treeview_ordner_anbinden_rekursiv( ZondTreeview* ztv, GtkTreeIter* anchor_iter,
        gint anchor_id, gboolean child, GFile* file, GFileInfo* info,
        InfoWindow* info_window, gint* zaehler, gchar** errmsg )
{
    gint new_node_id = 0;
    gchar* text = 0;
    const gchar* basename = NULL;
    ZondTreeStore* tree_store = NULL;
    GtkTreeIter iter_new = { 0 };
    GFileEnumerator* enumer = NULL;
    GError* error = NULL;
    gint anchor_id_loop = 0;
    GtkTreeIter iter_anchor_loop = { 0 };

    ZondTreeviewPrivate* ztv_priv = zond_treeview_get_instance_private( ztv );

    if ( info_window->cancel ) return -2;

    basename = g_file_info_get_name( info );

    new_node_id = zond_dbase_insert_node( ztv_priv->zond->dbase_zond->zond_dbase_work,
            anchor_id, child, ZOND_DBASE_TYPE_BAUM_STRUKT, 0, NULL, 0, 0, 0, 0,
            "folder", basename, NULL, &error );
    if ( new_node_id == -1 )
    {
        if( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
        g_error_free( error );

        return -1;
    }

    tree_store = ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(ztv) ));
    zond_tree_store_insert( tree_store, (anchor_iter->user_data ==
            zond_tree_store_get_root_node( tree_store )) ? NULL : anchor_iter, child, &iter_new );

    //Standardinhalt setzen
    zond_tree_store_set( &iter_new, "folder", basename, new_node_id );

    text = g_strconcat( "Verzeichnis eingefügt: ", basename, NULL );
    info_window_set_message( info_window, text );
    g_free( text );

    enumer = g_file_enumerate_children( file, "*", G_FILE_QUERY_INFO_NONE, NULL, &error );
    if ( !enumer )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerate_children:\n",
                error->message, NULL );
        g_error_free( error );

        return -1;
    }

    //new_anchor kopieren, da in der Schleife verändert wird
    //es soll aber der soeben erzeugte Punkt zurückgegegen werden
    child = TRUE;
    anchor_id_loop = new_node_id;
    iter_anchor_loop = iter_new;

    while ( 1 )
    {
        GFile* file_child = NULL;
        GFileInfo* info_child = NULL;
        gint new_node_id_loop = 0;

        if ( !g_file_enumerator_iterate( enumer, &info_child, &file_child, NULL, &error ) )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerator_iterate:\n",
                    error->message, NULL );
            g_error_free( error );
            g_object_unref( enumer );

            return -1;
        }

        if ( file_child ) //es gibt noch Datei in Verzeichnis
        {
            GFileType type = g_file_info_get_file_type( info_child );
            if ( type == G_FILE_TYPE_DIRECTORY )
            {
                new_node_id_loop = zond_treeview_ordner_anbinden_rekursiv( ztv,
                        &iter_anchor_loop, anchor_id_loop, child, file_child, info_child,
                        info_window, zaehler, errmsg );

                if ( new_node_id_loop == -1 )
                {
                    g_object_unref( enumer );

                    return -1;
                }
                else if ( new_node_id_loop == -2 ) break; //abgebrochen
            }
            else if ( type == G_FILE_TYPE_REGULAR )
            {
                gchar* rel_path = NULL;
                GFile* file_root = NULL;

                file_root = g_file_new_for_path( sond_treeviewfm_get_root( SOND_TREEVIEWFM(ztv) ) );
                rel_path = g_file_get_relative_path( file_root, file_child );
                g_object_unref( file_root );
                new_node_id_loop = zond_treeview_datei_anbinden( ztv,
                        &iter_anchor_loop, anchor_id_loop, child,
                        info_child, rel_path, info_window, zaehler, errmsg );
                g_free( rel_path );

                if ( new_node_id_loop == -1 )
                {
                    g_object_unref( enumer );

                    if ( errmsg ) *errmsg = add_string( g_strdup(
                            "Bei Aufruf datei_anbinden:\n" ), *errmsg );

                    return -1;
                }
                else if ( new_node_id_loop == -2 ) break; //abgebrochen
                else if ( new_node_id_loop == 0 ) continue;
            }

            anchor_id_loop = new_node_id_loop;
            child = FALSE;
        } //ende if ( child )
        else break;
    }

    g_object_unref( enumer );

    *anchor_iter = iter_new;

    return new_node_id;
}


typedef struct {
    ZondTreeview* ztv;
    gint anchor_id;
    GtkTreeIter anchor_iter;
    gboolean child;
    gboolean kind;
    gint zaehler;
    InfoWindow* info_window;
} SSelectionAnbinden;

static gint
zond_treeview_clipboard_anbinden_foreach( SondTreeview* stv, GtkTreeIter* iter,
        gpointer data, gchar** errmsg )
{
    GObject* object = NULL;
    gint new_node_id = 0;

    SSelectionAnbinden* s_selection = (SSelectionAnbinden*) data;

    gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(stv) ),
            iter, 0, &object, -1 );

    if ( G_IS_FILE_INFO(object) )
    {
        if ( g_file_info_get_file_type( G_FILE_INFO(object) ) == G_FILE_TYPE_DIRECTORY )
        {
            gchar* abs_path = NULL;
            GFile* file = NULL;

            abs_path = sond_treeviewfm_get_full_path( SOND_TREEVIEWFM(stv), iter );
            file = g_file_new_for_path( abs_path );
            g_free( abs_path );

            new_node_id = zond_treeview_ordner_anbinden_rekursiv( s_selection->ztv,
                    &s_selection->anchor_iter, s_selection->anchor_id, s_selection->child,
                    file, G_FILE_INFO(object), s_selection->info_window, &s_selection->zaehler,
                    errmsg );
            g_object_unref( file );
            g_object_unref( object );
            if ( new_node_id == -1 ) ERROR_S
        }
        else
        {
            gchar* rel_path = NULL;

            rel_path = sond_treeviewfm_get_rel_path( SOND_TREEVIEWFM(stv), iter );

            new_node_id = zond_treeview_datei_anbinden( s_selection->ztv,
                    &s_selection->anchor_iter, s_selection->anchor_id, s_selection->child,
                    G_FILE_INFO(object), rel_path, s_selection->info_window,
                    &s_selection->zaehler, errmsg );
            g_free( rel_path );
            g_object_unref( object );
            if ( new_node_id == -1 ) ERROR_S
            else if ( new_node_id == 0 ) return 0;
        }
    }
    else if ( ZOND_IS_PDF_ABSCHNITT(object) )
    {
        gint rc = 0;
        gint ID = 0;
        gchar const* rel_path = NULL;
        Anbindung anbindung = { 0 };
        gchar const* icon_name = NULL;
        gchar const* node_text = NULL;
        GError* error = NULL;
        gint first_child = 0;
        GtkTreeIter iter_inserted = { 0 };
        gint node_inserted = 0;

        ZondPdfAbschnitt* zpa = ZOND_PDF_ABSCHNITT(object);
        ZondTreeviewPrivate* ztv_priv = zond_treeview_get_instance_private( s_selection->ztv );

        zond_pdf_abschnitt_get( zpa, &ID, &rel_path, &anbindung, &icon_name, &node_text );

        node_inserted = zond_dbase_insert_node( ztv_priv->zond->dbase_zond->zond_dbase_work,
                s_selection->anchor_id, s_selection->kind, ZOND_DBASE_TYPE_BAUM_INHALT_PDF_ABSCHNITT,
                ID, NULL, 0, 0, 0, 0, NULL, NULL, NULL, &error );
        if ( node_inserted == -1 )
        {
            if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
            g_error_free( error );

            return -1;
        }

        zond_tree_store_insert( ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(s_selection->ztv) )),
                &s_selection->anchor_iter, s_selection->kind, &iter_inserted );
        zond_tree_store_set( &iter_inserted, icon_name, node_text, node_inserted );

        rc = zond_dbase_get_first_child( ztv_priv->zond->dbase_zond->zond_dbase_work,
                ID, &first_child, &error );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
            g_error_free( error );

            return -1;
        }

        if ( first_child )
        {
            rc = zond_treeview_walk_tree( s_selection->ztv, TRUE, first_child, &s_selection->anchor_iter, TRUE,
                    NULL, 0, NULL, zond_treeview_insert_pdf_abschnitt, &error );
            if ( rc )
            {
                if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
                g_error_free( error );

                return -1;
            }
        }
    }

    if ( new_node_id == -2 ) return 1; //abgebrochen!

    s_selection->kind = FALSE;
    s_selection->anchor_id = new_node_id;

    return 0;
}


static gint
zond_treeview_clipboard_anbinden( Projekt* zond, gint anchor_id, GtkTreeIter* anchor_iter,
        gboolean kind, InfoWindow* info_window, gchar** errmsg )
{
    gint rc = 0;
    gboolean success = FALSE;
    SSelectionAnbinden s_selection = { 0 };
    GError* error = NULL;

    s_selection.ztv = ZOND_TREEVIEW(zond->treeview[BAUM_INHALT]);
    s_selection.anchor_id = anchor_id;
    s_selection.anchor_iter = *anchor_iter;
    s_selection.kind = kind;
    s_selection.zaehler = 0;
    s_selection.info_window = info_window;

    rc = zond_dbase_begin( zond->dbase_zond->zond_dbase_work, &error );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
        g_error_free( error );

        return -1;
    }

    rc = sond_treeview_clipboard_foreach( zond_treeview_clipboard_anbinden_foreach, &s_selection, errmsg );
    if ( rc == -1 ) ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )

    rc = zond_dbase_commit( zond->dbase_zond->zond_dbase_work, &error );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
        g_error_free( error );
        ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )
    }

    if ( success ) sond_treeview_expand_row( zond->treeview[BAUM_INHALT], &s_selection.anchor_iter );
    sond_treeview_set_cursor( zond->treeview[BAUM_INHALT], &s_selection.anchor_iter );

    gtk_tree_view_columns_autosize( GTK_TREE_VIEW(((Projekt*) zond)->treeview[BAUM_INHALT]) );

    gchar* text = g_strdup_printf( "%i Datei(en) angebunden", s_selection.zaehler );
    info_window_set_message( info_window, text );
    g_free( text );

    return s_selection.zaehler;
}

typedef struct {
    Projekt* zond;
    GtkTreeIter* iter_anchor;
    gboolean child;
    gint anchor_id;
} SSelection;


static gint
zond_treeview_clipboard_verschieben_foreach( SondTreeview* tree_view, GtkTreeIter* iter_src,
        gpointer data, gchar** errmsg )
{
    gint node_id = 0;
    gint rc = 0;
    GtkTreeIter iter_new = { 0 };
    GError* error = NULL;

    SSelection* s_selection = (SSelection*) data;

    //soll link verschoben werden? Nur wenn head
    if ( zond_tree_store_is_link( iter_src ) )
    {
        //nur packen, wenn head
        if ( (node_id = zond_tree_store_get_link_head_nr( iter_src )) <= 0 ) return 0;
    }
    else gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(tree_view) ),
            iter_src, 2, &node_id, -1 );

    //soll Ziel verschoben werden? Nein!
    if ( zond_tree_store_get_root( zond_tree_store_get_tree_store( iter_src ) ) == BAUM_INHALT )
    {
        gint rc = 0;
        gint type = 0;

        rc = zond_dbase_get_type_and_link( s_selection->zond->dbase_zond->zond_dbase_work,
                node_id, &type, NULL, &error );
        if ( rc == -1 )
        {
            if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
            g_error_free( error );

            return -1;
        }

        if ( type == ZOND_DBASE_TYPE_PDF_ABSCHNITT ||
                type == ZOND_DBASE_TYPE_PDF_PUNKT ) return 0;
    }

    //Knoten verschieben verschieben
    rc = zond_dbase_verschieben_knoten( s_selection->zond->dbase_zond->zond_dbase_work,
            node_id, s_selection->anchor_id, s_selection->child, &error );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
        g_error_free( error );

        return -1;
    }

    zond_tree_store_move_node( iter_src, s_selection->iter_anchor, s_selection->child, &iter_new );

    s_selection->child = FALSE;
    *(s_selection->iter_anchor) = iter_new;
    s_selection->anchor_id = node_id;

    return 0;
}


static gint
zond_treeview_clipboard_verschieben( Projekt* zond, gboolean child, GtkTreeIter* iter_cursor,
        GtkTreeIter* iter_anchor, gint anchor_id, gchar** errmsg )
{
    gint rc = 0;
    Clipboard* clipboard = NULL;

    SSelection s_selection = { zond, iter_anchor, child, anchor_id };

    clipboard = ((SondTreeviewClass*) g_type_class_peek( SOND_TYPE_TREEVIEW ))->clipboard;

    if ( zond_tree_store_get_tree_store( iter_cursor ) !=
            ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(clipboard->tree_view) )) )
            return 0;

    rc = sond_treeview_clipboard_foreach( zond_treeview_clipboard_verschieben_foreach, &s_selection, errmsg );
    if ( rc == -1 ) ERROR_S

    //Alte Auswahl löschen
    if ( clipboard->arr_ref->len > 0 ) g_ptr_array_remove_range( clipboard->arr_ref,
            0, clipboard->arr_ref->len );

    gtk_widget_queue_draw( GTK_WIDGET(zond->treeview[zond->baum_active]) );

    if ( child && (iter_cursor->user_data !=
            zond_tree_store_get_root_node(
            zond_tree_store_get_tree_store( iter_cursor ) )) )
            sond_treeview_expand_row( zond->treeview[zond->baum_active], s_selection.iter_anchor );
    sond_treeview_set_cursor( zond->treeview[zond->baum_active], s_selection.iter_anchor );

    return 0;
}

static gint
zond_treeview_copy_pdf_abschnitt( ZondTreeview* ztv,
        gint node_id, GtkTreeIter* iter, gboolean child,
        GtkTreeIter* iter_inserted, gint anchor_id, gint* node_id_inserted, GError** error )
{
    gint type = 0;
    gint link = 0;
    gchar* icon_name = NULL;
    gchar* node_text = NULL;
    gchar* text = NULL;
    gint rc = 0;
    gint node_id_new = 0;

    ZondTreeviewPrivate* ztv_priv = zond_treeview_get_instance_private( ztv );

    rc = zond_dbase_get_node( ztv_priv->zond->dbase_zond->zond_dbase_work,
            node_id, &type, &link, NULL, NULL, NULL, NULL, NULL, &icon_name, &node_text,
            &text, error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    node_id_new = zond_dbase_insert_node( ztv_priv->zond->dbase_zond->zond_dbase_work,
            anchor_id, child, ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_COPY, node_id, NULL, 0, 0, 0, 0,
            icon_name, node_text, text, error );
    if ( node_id_new == -1 )
    {
        g_prefix_error( error, "%s\n", __func__ );
        g_free( icon_name );
        g_free( node_text );
        g_free( text );

        return -1;
    }

    zond_tree_store_insert( ZOND_TREE_STORE(gtk_tree_view_get_model(
            GTK_TREE_VIEW(ztv) )), iter, child, iter_inserted );
    zond_tree_store_set( iter_inserted, icon_name, node_text, node_id_new );

    g_free( icon_name );
    g_free( node_text );
    g_free( text );

    return 0;
}


gint
zond_treeview_copy_node_to_baum_auswertung( ZondTreeview* ztv,
        gint node_id, GtkTreeIter* iter, gboolean child,
        GtkTreeIter* iter_inserted, gint anchor_id, gint* node_id_inserted, GError** error )
{
    gint type = 0;
    gint link = 0;
    gchar* icon_name = NULL;
    gchar* node_text = NULL;
    gchar* text = NULL;
    gint rc = 0;
    gint node_id_new = 0;
    gint type_new = 0;
    gint link_new = 0;
    gchar* rel_path = NULL;

    ZondTreeviewPrivate* ztv_priv = zond_treeview_get_instance_private( ztv );

    rc = zond_dbase_get_node( ztv_priv->zond->dbase_zond->zond_dbase_work,
            node_id, &type, &link, &rel_path, NULL, NULL, NULL, NULL, &icon_name, &node_text,
            &text, error );
    if ( rc ) ERROR_Z

    if ( type != ZOND_DBASE_TYPE_BAUM_STRUKT )
    {
        if ( type == ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_COPY ) link_new = link;
        else link_new = node_id;

        type_new = ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_COPY;
    }
    else type_new = ZOND_DBASE_TYPE_BAUM_STRUKT;

    node_id_new = zond_dbase_insert_node( ztv_priv->zond->dbase_zond->zond_dbase_work,
            anchor_id, child, type_new, link_new, NULL, 0, 0, 0, 0,
            icon_name, node_text, text, error );
    if ( node_id_new == -1 )
    {
        g_prefix_error( error, "%s\n", __func__ );
        g_free( rel_path );
        g_free( icon_name );
        g_free( node_text );
        g_free( text );

        return -1;
    }

    if ( node_id_inserted ) *node_id_inserted = node_id_new;

    zond_tree_store_insert( ZOND_TREE_STORE(gtk_tree_view_get_model(
            GTK_TREE_VIEW(ztv) )), iter, child, iter_inserted );
    zond_tree_store_set( iter_inserted, icon_name, node_text, node_id_new );

    g_free( icon_name );
    g_free( node_text );
    g_free( text );

    if ( type == ZOND_DBASE_TYPE_BAUM_INHALT_FILE ||
            type == ZOND_DBASE_TYPE_BAUM_INHALT_PDF_ABSCHNITT ||
            type == ZOND_DBASE_TYPE_BAUM_INHALT_VIRT_PDF )
    {
        gint first_pdf_abschnitt = 0;

        //ersten PDF-Abschnitt
        if ( type == ZOND_DBASE_TYPE_BAUM_INHALT_PDF_ABSCHNITT )
        {
            gint rc = 0;

            g_free( rel_path );

            rc = zond_dbase_get_first_child(
                    ztv_priv->zond->dbase_zond->zond_dbase_work, link,
                    &first_pdf_abschnitt, error );
            if ( rc ) ERROR_Z
        }
        else
        {
            gint rc = 0;
            gint pdf_root = 0;

            rc = zond_dbase_get_pdf_root( ztv_priv->zond->dbase_zond->zond_dbase_work,
                    rel_path, &pdf_root, error );
            g_free( rel_path );
            if ( rc ) ERROR_Z

            if ( pdf_root == 0 ) return 0;

            rc = zond_dbase_get_first_child( ztv_priv->zond->dbase_zond->zond_dbase_work,
                    pdf_root, &first_pdf_abschnitt, error );
            if ( rc ) ERROR_Z
        }

        if ( first_pdf_abschnitt == 0 ) return 0;

        rc = zond_treeview_walk_tree( ztv, TRUE, first_pdf_abschnitt,
                iter_inserted, TRUE, NULL, node_id_new, NULL,
                zond_treeview_copy_pdf_abschnitt, error );
        if ( rc ) ERROR_Z
    }

    return 0;
}


static gint
zond_treeview_clipboard_kopieren_foreach( SondTreeview* tree_view, GtkTreeIter* iter, gpointer data, gchar** errmsg )
{
    gint rc = 0;
    gint node_id = 0;
    gint node_id_new = 0;
    GtkTreeIter iter_inserted = { 0 };
    GError* error = NULL;

    SSelection* s_selection = (SSelection*) data;

    rc = zond_dbase_begin( s_selection->zond->dbase_zond->zond_dbase_work, &error );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
        g_error_free( error );

        return -1;
    }

    //soll durch etwaige links "hindurchgucken"
    gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(tree_view) ), iter, 2, &node_id, -1 );

    rc = zond_treeview_walk_tree( ZOND_TREEVIEW(s_selection->zond->treeview[BAUM_AUSWERTUNG]),
            FALSE, node_id, s_selection->iter_anchor, s_selection->child,
            &iter_inserted, s_selection->anchor_id, &node_id_new,
            zond_treeview_copy_node_to_baum_auswertung, &error );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
        g_error_free( error );

        return -1;
    }

    s_selection->child = FALSE;
    *(s_selection->iter_anchor) = iter_inserted;
    s_selection->anchor_id = node_id_new;

    return 0;
}


static gint
zond_treeview_clipboard_kopieren( Projekt* zond, gboolean child,
        GtkTreeIter* iter_cursor, GtkTreeIter* iter_anchor,
        gint anchor_id, gchar** errmsg )
{
    Clipboard* clipboard = NULL;

    SSelection s_selection = { zond, iter_anchor, child, anchor_id };

    clipboard = ((SondTreeviewClass*) g_type_class_peek( SOND_TYPE_TREEVIEW ))->clipboard;

    if ( zond_tree_store_get_tree_store( iter_cursor ) ==
            ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[BAUM_AUSWERTUNG]) )) )
    {
        gint rc = 0;

        rc = sond_treeview_clipboard_foreach( zond_treeview_clipboard_kopieren_foreach,
            &s_selection, errmsg );
        if ( rc == -1 ) ERROR_S
    }
    /*
    else if ( clipboard->tree_view == zond->treeview[BAUM_INHALT] &&
            zond_tree_store_get_tree_store( iter_cursor ) ==
            gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[BAUM_INHALT]) ) )
    {
        //Erzeugen von virt-Pdfs
    }
    */
    else return 0;

    if ( child && (iter_cursor->user_data !=
            zond_tree_store_get_root_node(
            zond_tree_store_get_tree_store( iter_cursor ) )) )
            sond_treeview_expand_row( zond->treeview[zond->baum_active], s_selection.iter_anchor );
    sond_treeview_set_cursor( zond->treeview[zond->baum_active], s_selection.iter_anchor );

    return 0;
}


static gint
zond_treeview_paste_clipboard_as_link_foreach( SondTreeview* tree_view, GtkTreeIter* iter, gpointer data, gchar** errmsg )
{
    gint rc = 0;
    gint node_id_new = 0;
    gint node_id = 0;
    GtkTreeIter iter_target = { 0 };
    GtkTreeIter iter_new = { 0 };
    GError* error = NULL;

    SSelection* s_selection = (SSelection*) data;

    //soll durch etwaige links "hindurchgucken"
    gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(tree_view) ), iter, 2, &node_id, -1 );

    //node ID, auf den link zeigen soll
    rc = zond_dbase_begin( s_selection->zond->dbase_zond->zond_dbase_work, &error );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
        g_error_free( error );

        return -1;
    }

    node_id_new = zond_dbase_insert_node( s_selection->zond->dbase_zond->zond_dbase_work,
            s_selection->anchor_id, s_selection->child, ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_LINK,
            node_id, NULL, 0, 0, 0, 0, NULL, NULL, NULL, &error );
    if ( node_id_new < 0 )
    {
        if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
        g_error_free( error );

        ERROR_ROLLBACK( s_selection->zond->dbase_zond->zond_dbase_work )
    }

    rc = zond_dbase_commit( s_selection->zond->dbase_zond->zond_dbase_work, &error );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
        g_error_free( error );
        ERROR_ROLLBACK( s_selection->zond->dbase_zond->zond_dbase_work )
    }

    //falls link im clipboard: iter_target ermitteln, damit nicht link auf link zeigt
    zond_tree_store_get_iter_target( iter, &iter_target );
    zond_tree_store_insert_link( &iter_target, node_id_new, zond_tree_store_get_tree_store( s_selection->iter_anchor ),
            (s_selection->anchor_id) ? s_selection->iter_anchor: NULL, s_selection->child, &iter_new );

    s_selection->anchor_id = node_id_new;
    s_selection->child = FALSE;
    *(s_selection->iter_anchor) = iter_new;

    return 0;
}


//Ziel ist immer BAUM_AUSWERTUNG
static gint
zond_treeview_paste_clipboard_as_link( Projekt* zond, gboolean child,
        GtkTreeIter* iter_cursor, GtkTreeIter* iter_anchor, gint anchor_id,
        gchar** errmsg )
{
    gint rc = 0;
    GtkTreeIter iter_origin = { 0 };

    SSelection s_selection = { zond, iter_anchor, child, anchor_id };

    iter_origin = *iter_cursor;

    rc = sond_treeview_clipboard_foreach( zond_treeview_paste_clipboard_as_link_foreach, &s_selection, errmsg );
    if ( rc == -1 ) ERROR_S

    if ( child && (iter_origin.user_data !=
            zond_tree_store_get_root_node(
            zond_tree_store_get_tree_store( &iter_origin ) )) )
            sond_treeview_expand_row( zond->treeview[zond->baum_active], &iter_origin );
    sond_treeview_set_cursor( zond->treeview[zond->baum_active], s_selection.iter_anchor );

    return 0;
}


static gint
zond_treeview_paste_clipboard( Projekt* zond, gboolean child, gboolean link, gchar** errmsg )
{
    Clipboard* clipboard = NULL;
    GtkTreeIter iter_cursor = { 0, };
    GtkTreeIter iter_anchor = { 0 };
    gint anchor_id = 0;
    gboolean success = FALSE;

    if ( zond->baum_active == KEIN_BAUM || zond->baum_active == BAUM_FS ) return 0;

    clipboard = ((SondTreeviewClass*) g_type_class_peek( SOND_TYPE_TREEVIEW ))->clipboard;

    //Wenn clipboard leer - ganz am Anfang oder nach Einfügen von Ausschneiden
    if ( clipboard->arr_ref->len == 0 ) return 0;

    //ist der Baum, der markiert wurde, egal ob link zu anderem Baum oder nicht
    Baum baum_selection = (Baum) sond_treeview_get_id( clipboard->tree_view );

    //verhindern, daß in Zweig unterhalb eingefügt wird
    if ( zond->baum_active == baum_selection ) //wenn innerhalb des gleichen Baums
    {
        if ( sond_treeview_test_cursor_descendant( zond->treeview[zond->baum_active], child ) )
                ERROR_S_MESSAGE( "Unzulässiges Ziel: Abkömmling von zu verschiebendem "
                "Knoten" )
    }

    success = zond_treeview_get_anchor( zond, child, &iter_cursor, &iter_anchor, &anchor_id );
    if ( !success ) child = TRUE;
    //in link soll nix eingefügt werden - Konsequenzen kann man nicht überblicken
    else if ( !(iter_cursor.stamp == iter_anchor.stamp &&
            iter_cursor.user_data == iter_anchor.user_data) )
            ERROR_S_MESSAGE( "Unzulässiges Ziel: in Link darf nicht eingefügt werden" )

    if ( zond_tree_store_get_root( zond_tree_store_get_tree_store( &iter_anchor ) ) == BAUM_INHALT )
    {
        gint rc = 0;

        if ( anchor_id != zond_tree_store_get_root( zond_tree_store_get_tree_store( &iter_anchor ) ) )
        {
            gint rc = 0;
            GError* error = NULL;

            rc = zond_treeview_hat_vorfahre_datei( zond, anchor_id, child, &error );
            if ( rc == -1 )
            {
                if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__,
                        error->message );
                g_error_free( error );

                return -1;
            }
            else if ( rc == 1 ) return 1; //unzulässiges Ziel
        }

        if ( baum_selection == BAUM_FS )
        {
            InfoWindow* info_window = NULL;

            info_window = info_window_open( zond->app_window, "Dateien anbinden" );

            rc = zond_treeview_clipboard_anbinden( zond, anchor_id, &iter_anchor, child, info_window, errmsg );
            if ( rc == -1 )
            {
                info_window_set_message( info_window, *errmsg );
                g_clear_pointer( errmsg, g_free );
            }

            info_window_close( info_window );

            return 0;
        }
    }

    if ( clipboard->ausschneiden && !link)
    {
        gint rc = 0;

        rc = zond_treeview_clipboard_verschieben( zond, child, &iter_cursor,
                &iter_anchor, anchor_id, errmsg );
        if ( rc == -1 ) ERROR_S
    }
    else if ( !clipboard->ausschneiden && !link )
    {
        gint rc = 0;

        rc = zond_treeview_clipboard_kopieren( zond, child, &iter_cursor,
                &iter_anchor, anchor_id, errmsg );
        if ( rc == -1 ) ERROR_S
    }
    else if ( !clipboard->ausschneiden && link )
    {
        gint rc = 0;

        if ( zond->baum_active == BAUM_INHALT ) return 0; //nur in BAUM_AUSWERTUNG!

        rc = zond_treeview_paste_clipboard_as_link( zond, child, &iter_cursor,
                &iter_anchor, anchor_id, errmsg );
        if ( rc ) ERROR_S
    }
    //ausschneiden und link geht ja nicht...

    return 0;
}


static void
zond_treeview_paste_activate( GtkMenuItem* item, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    gboolean child = FALSE;

    Projekt* zond = (Projekt*) user_data;

    child = (gboolean) GPOINTER_TO_INT(g_object_get_data( G_OBJECT(item), "kind" ));

    rc = zond_treeview_paste_clipboard( zond, child, FALSE, &errmsg );
    if ( rc == -1 )
    {
        display_message( zond->app_window, "Fehler Einfügen Clipboard\n\n", errmsg,
                NULL );
        g_free( errmsg );
    }
    else if ( rc == 1 ) display_message( zond->app_window, "Einfügen als "
                "Unterpunkt einer Datei nicht zulässig", NULL );

    return;
}


static void
zond_treeview_paste_as_link_activate( GtkMenuItem* item, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    gboolean child = FALSE;

    Projekt* zond = (Projekt*) user_data;

    child = (gboolean) GPOINTER_TO_INT(g_object_get_data( G_OBJECT(item), "kind" ));

    rc = zond_treeview_paste_clipboard( zond, child, TRUE, &errmsg );
    if ( rc == -1 )
    {
        display_message( zond->app_window, "Fehler Einfügen Clipboard\n\n", errmsg,
                NULL );
        g_free( errmsg );
    }
    else if ( rc == 1 ) display_message( zond->app_window, "Einfügen als "
                "Unterpunkt einer Datei nicht zulässig", NULL );

    return;
}


static gint
zond_treeview_selection_loeschen_foreach( SondTreeview* tree_view, GtkTreeIter* iter,
        gpointer data, gchar** errmsg )
{
    gint rc = 0;
    gint node_id = 0;
    gboolean response = FALSE;
    GError* error = NULL;

    Projekt* zond = (Projekt*) data;

    //node_id herausfinden - wenn kein Link-Head->raus
    if ( zond_tree_store_is_link( iter ) &&
            !(node_id = zond_tree_store_get_link_head_nr( iter )) ) return 0;
    else gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(tree_view) ), iter, 2, &node_id, -1 );

    if ( node_id == zond->node_id_extra )
            g_signal_emit_by_name( zond->textview_window,
            "delete-event", zond, &response );

    rc = zond_dbase_begin( zond->dbase_zond->zond_dbase_work, &error );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
        g_error_free( error );

        return -1;
    }

    rc = zond_dbase_remove_node( zond->dbase_zond->zond_dbase_work,
            node_id, &error );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strdup( error->message );
        g_error_free( error );
        ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )
    }

    rc = zond_dbase_commit( zond->dbase_zond->zond_dbase_work, &error );
    if ( rc ) ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )

    zond_tree_store_remove( iter );

    return 0;
}


static void
zond_treeview_loeschen_activate( GtkMenuItem* item, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    Projekt* zond = (Projekt*) user_data;

    rc = sond_treeview_selection_foreach( zond->treeview[zond->baum_active],
            zond_treeview_selection_loeschen_foreach, zond, &errmsg );
    if ( rc == -1 )
    {
        display_message( zond->app_window, "Löschen fehlgeschlagen -\n\nBei Aufruf "
                "sond_treeviewfm_selection/treeviews_loeschen:\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


static gint
zond_treeview_knoten_verschieben( ZondTreeview* ztv, gint node_id, GtkTreeIter* iter_src,
        gint anchor_id, GtkTreeIter* iter_dest, gboolean child, gchar** errmsg )
{
    gint rc = 0;
    GError* error = NULL;

    ZondTreeviewPrivate* ztv_priv = zond_treeview_get_instance_private( ztv );

    //kind verschieben
    rc = zond_dbase_verschieben_knoten( ztv_priv->zond->dbase_zond->zond_dbase_work,
            node_id, anchor_id, child, &error );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strdup( error->message );
        g_error_free( error );

        ERROR_S
    }

    zond_tree_store_move_node( iter_src, iter_dest, child, NULL );

    return 0;
}


static gint
zond_treeview_selection_entfernen_anbindung_foreach( SondTreeview* stv,
        GtkTreeIter* iter, gpointer data, gchar** errmsg )
{
    gint rc = 0;
    gint older_sibling = 0;
    gint node_id = 0;
    gint anchor_id = 0;
    gint type = 0;
    GtkTreeIter iter_dest = { 0 };
    gboolean child = FALSE;
    GError* error = NULL;

    Projekt* zond = data;

    if ( sond_treeview_get_id( stv ) != BAUM_INHALT ) return 0;

    gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(stv) ), iter,
            2, &node_id, -1 );

    rc = zond_dbase_get_type_and_link( zond->dbase_zond->zond_dbase_work,
            node_id, &type, NULL, &error );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
        g_error_free( error );

        return -1;
    }

    if ( type != ZOND_DBASE_TYPE_PDF_ABSCHNITT ) return 0;

    iter_dest = *iter;

    //herausfinden, ob zu löschender Knoten älteres Geschwister hat
    rc = zond_dbase_get_older_sibling( zond->dbase_zond->zond_dbase_work,
            node_id, &older_sibling, &error);
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
        g_error_free( error );

        return -1;
    }

    if ( older_sibling )
    {
        if ( !gtk_tree_model_iter_previous( gtk_tree_view_get_model(
                GTK_TREE_VIEW(zond->treeview[BAUM_INHALT]) ), &iter_dest ) )
                ERROR_S_MESSAGE( "Kann älteres Geschwister nicht im tree finden" )

        child = FALSE;
        anchor_id = older_sibling;
    }
    else
    {
        gint parent = 0;

        if ( !gtk_tree_model_iter_parent( gtk_tree_view_get_model(
                GTK_TREE_VIEW(zond->treeview[BAUM_INHALT]) ), &iter_dest, iter ) )
                ERROR_S_MESSAGE( "Kann Elternknoten nicht finden" )

        //Elternknoten ermitteln
        rc = zond_dbase_get_parent( zond->dbase_zond->zond_dbase_work,
                node_id, &parent, &error );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
            g_error_free( error );

            return -1;
        }

        child = TRUE;
        anchor_id = parent;
    }

    rc = zond_dbase_begin( zond->dbase_zond->zond_dbase_work, &error );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
        g_error_free( error );

        return -1;
    }

    do
    {
        GtkTreeIter iter_src = { 0 };
        gint rc = 0;
        gint first_child_id = 0;

        rc = zond_dbase_get_first_child( zond->dbase_zond->zond_dbase_work, node_id,
                &first_child_id, &error );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strdup( error->message );
            g_error_free( error );
            ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )
        }

        if ( first_child_id == 0 ) break;

        if ( !gtk_tree_model_get_iter_first( gtk_tree_view_get_model(
                GTK_TREE_VIEW(zond->treeview[BAUM_INHALT]) ), &iter_src ) )
                ERROR_S_MESSAGE( "Kann iter Kindknoten nicht finden" )

        rc = zond_treeview_knoten_verschieben( ZOND_TREEVIEW(stv), first_child_id, &iter_src,
                anchor_id, &iter_dest, child, errmsg );
        if ( rc == -1 ) ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )

        anchor_id = first_child_id;
    } while ( 1 );

    rc = zond_dbase_remove_node( zond->dbase_zond->zond_dbase_work, node_id, &error );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strdup( error->message );
        g_error_free( error );
        ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )
    }

    zond_tree_store_remove( iter );

    rc = zond_dbase_commit( zond->dbase_zond->zond_dbase_work, &error );
    {
        if ( errmsg ) *errmsg = g_strdup( error->message );
        g_error_free( error );
        ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )
    }

    return 0;
}


//Funktioniert nur im BAUM_INHALT - Abfrage im cb schließt nur BAUM_FS aus
static void
zond_treeview_anbindung_entfernen_activate( GtkMenuItem* item, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    Projekt* zond = (Projekt*) user_data;

    if ( zond->baum_active != BAUM_INHALT ) return;

    rc = sond_treeview_selection_foreach( zond->treeview[BAUM_INHALT],
            zond_treeview_selection_entfernen_anbindung_foreach, zond, &errmsg );
    if ( rc )
    {
        display_message( zond->app_window, "Löschen von Anbindungen fehlgeschlagen\n\n",
                errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


static void
zond_treeview_jump_to_iter( Projekt* zond, GtkTreeIter* iter )
{
    Baum baum_target = KEIN_BAUM;

    baum_target = zond_tree_store_get_root( zond_tree_store_get_tree_store( iter ) );

    sond_treeview_expand_to_row( zond->treeview[baum_target], iter );
    sond_treeview_set_cursor( zond->treeview[baum_target], iter );

    return;
}


static gint
zond_treeview_jump_to_origin( ZondTreeview* ztv, GtkTreeIter* iter, GError** error )
{
    gint node_id = 0;
    gint type = 0;
    gint link = 0;
    gint rc = 0;

    ZondTreeviewPrivate* ztv_priv = zond_treeview_get_instance_private( ztv );

    gtk_tree_model_get( gtk_tree_view_get_model(
            GTK_TREE_VIEW(ztv_priv->zond->treeview[ztv_priv->zond->baum_active]) ),
            iter, 2, &node_id, -1 );

    rc = zond_dbase_get_type_and_link( ztv_priv->zond->dbase_zond->zond_dbase_work, node_id,
            &type, &link, error );
    if ( rc ) ERROR_Z
//ToDo
    if ( type == ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_COPY )
    {

    }
    else if ( type == ZOND_DBASE_TYPE_BAUM_INHALT_FILE )
    {
        /*
                //Sprung auf FILE/FILE_PART
            //wenn FS nicht angezeigt: erst einschalten, damit man was sieht
            if ( !gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(zond->fs_button) ) )
                    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(zond->fs_button), TRUE );

            rc = sond_treeviewfm_set_cursor_on_path( SOND_TREEVIEWFM(zond->treeview[BAUM_FS]), rel_path, &errmsg );
            if ( rc )
            {
                display_message( zond->app_window, "Fehler bei Springen zu Ursprung:\n\n",
                        errmsg, NULL );
                g_free( errmsg );

                return;
            }
        }
    */
    }
    else if ( type == ZOND_DBASE_TYPE_BAUM_INHALT_FILE_PART )
    {

    }
    else if ( type == ZOND_DBASE_TYPE_BAUM_INHALT_PDF_ABSCHNITT )
    {

    }
    else if ( type == ZOND_DBASE_TYPE_PDF_ABSCHNITT )
    {

    }
    else if ( type == ZOND_DBASE_TYPE_PDF_PUNKT )
    {

    }

    return 0;
}


static void
zond_treeview_jump_to_link_target( Projekt* zond, GtkTreeIter* iter )
{
    GtkTreeIter iter_target = { 0 };

    zond_tree_store_get_iter_target( iter, &iter_target );

    zond_treeview_jump_to_iter( zond, &iter_target );

    return;
}


static void
zond_treeview_jump_activate( GtkMenuItem* item, gpointer user_data )
{
    GtkTreeIter iter = { 0 };

    Projekt* zond = (Projekt*) user_data;

    if ( !sond_treeview_get_cursor( zond->treeview[zond->baum_active], &iter ) ) return;

    if ( zond_tree_store_is_link( &iter ) ) zond_treeview_jump_to_link_target( zond, &iter );
    else
    {
        gint rc = 0;
        GError* error = NULL;

        rc = zond_treeview_jump_to_origin( ZOND_TREEVIEW(zond->treeview[zond->baum_active]), &iter, &error );
        if ( rc )
        {
            display_message( zond->app_window, "Fehler Sprung zu Herkunft\n\n",
                    error->message, NULL );
            g_error_free( error );

            return;
        }
    }

    return;
}


static void
zond_treeview_datei_oeffnen_activate( GtkMenuItem* item, gpointer user_data )
{
    GtkTreePath* path = NULL;

    Projekt* zond = (Projekt*) user_data;

    gtk_tree_view_get_cursor( GTK_TREE_VIEW(zond->treeview[zond->baum_active]), &path, NULL );

    g_signal_emit_by_name( zond->treeview[zond->baum_active], "row-activated", path, NULL );

    gtk_tree_path_free( path );

    return;
}


static void
zond_treeview_datei_oeffnen_mit_activate( GtkMenuItem* item, gpointer user_data )
{
    GtkTreePath* path = NULL;
    gint rc = 0;
    gchar* errmsg = NULL;

    Projekt* zond = (Projekt*) user_data;

    gtk_tree_view_get_cursor( GTK_TREE_VIEW(zond->treeview[zond->baum_active]), &path, NULL );

    rc = zond_treeview_open_path( zond, GTK_TREE_VIEW(zond->treeview[zond->baum_active]), path, TRUE, &errmsg );
    gtk_tree_path_free( path );
    if ( rc )
    {
        display_message( zond->app_window, "Fehler beim Öffnen Knoten:\n\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}

typedef struct _SSelectionChangeIcon
{
    ZondDBase* zond_dbase;
    const gchar* icon_name;
} SSelectionChangeIcon;

static gint
zond_treeview_selection_change_icon_foreach( SondTreeview* tree_view, GtkTreeIter* iter,
        gpointer data, gchar** errmsg )
{
    gint rc = 0;
    gint node_id = 0;
    SSelectionChangeIcon* s_selection = NULL;
    GError* error = NULL;

    s_selection = data;

    gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(tree_view) ), iter, 2, &node_id, -1 );

    rc = zond_dbase_update_icon_name( s_selection->zond_dbase, node_id, s_selection->icon_name, &error );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
        g_error_free( error );

        return -1;
    }

    //neuen icon_name im tree speichern
    zond_tree_store_set( iter, s_selection->icon_name, NULL, 0 );

    return 0;
}


static void
zond_treeview_icon_activate( GtkMenuItem* item, gpointer user_data )
{
    gint rc = 0;
    gint icon_id = 0;
    gchar* errmsg = NULL;

    Projekt* zond = (Projekt*) user_data;

    icon_id = GPOINTER_TO_INT(g_object_get_data( G_OBJECT(item), "icon-id" ));

    SSelectionChangeIcon s_selection = { zond->dbase_zond->zond_dbase_work,
            zond->icon[icon_id].icon_name };

    rc = sond_treeview_selection_foreach( zond->treeview[zond->baum_active],
            zond_treeview_selection_change_icon_foreach, (gpointer) &s_selection, &errmsg );
    if ( rc == -1 )
    {
        display_message( zond->app_window, "Icon ändern fehlgeschlagen\n\n",
                errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


static void
zond_treeview_init_contextmenu( ZondTreeview* ztv )
{
    GtkWidget* contextmenu = NULL;

    ZondTreeviewPrivate* ztv_priv = zond_treeview_get_instance_private( ztv );
    Projekt* zond = ztv_priv->zond;

    contextmenu = sond_treeview_get_contextmenu( SOND_TREEVIEW(ztv) );

    //Trennblatt
    GtkWidget* item_separator_0 = gtk_separator_menu_item_new();
    gtk_menu_shell_prepend( GTK_MENU_SHELL(contextmenu), item_separator_0 );

    //Punkt einfügen
    GtkWidget* item_punkt_einfuegen = gtk_menu_item_new_with_label( "Punkt einfügen" );

    GtkWidget* menu_punkt_einfuegen = gtk_menu_new();

    GtkWidget* item_punkt_einfuegen_ge = gtk_menu_item_new_with_label(
            "Gleiche Ebene" );
    g_object_set_data( G_OBJECT(contextmenu), "item-punkt-einfuegen-ge",
            item_punkt_einfuegen_ge );
    g_signal_connect( G_OBJECT(item_punkt_einfuegen_ge), "activate",
            G_CALLBACK(zond_treeview_punkt_einfuegen_activate), (gpointer) zond );

    GtkWidget* item_punkt_einfuegen_up = gtk_menu_item_new_with_label(
            "Unterebene" );
    g_object_set_data( G_OBJECT(contextmenu), "item-punkt-einfuegen-up",
            item_punkt_einfuegen_up );
    g_object_set_data( G_OBJECT(item_punkt_einfuegen_up), "kind", GINT_TO_POINTER(1) );
    g_signal_connect( G_OBJECT(item_punkt_einfuegen_up), "activate",
            G_CALLBACK(zond_treeview_punkt_einfuegen_activate), (gpointer) zond );

    gtk_menu_shell_append( GTK_MENU_SHELL(menu_punkt_einfuegen),
            item_punkt_einfuegen_ge );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_punkt_einfuegen),
            item_punkt_einfuegen_up );

    gtk_menu_item_set_submenu( GTK_MENU_ITEM(item_punkt_einfuegen),
            menu_punkt_einfuegen );

    gtk_menu_shell_prepend( GTK_MENU_SHELL(contextmenu), item_punkt_einfuegen );

    //Einfügen
    GtkWidget* item_paste = gtk_menu_item_new_with_label("Einfügen");

    GtkWidget* menu_paste = gtk_menu_new();

    GtkWidget* item_paste_ge = gtk_menu_item_new_with_label( "Gleiche Ebene");
    g_object_set_data( G_OBJECT(contextmenu), "item-paste-ge", item_paste_ge );
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_paste), item_paste_ge );
    g_signal_connect( G_OBJECT(item_paste_ge), "activate",
            G_CALLBACK(zond_treeview_paste_activate), (gpointer) zond );

    GtkWidget* item_paste_up = gtk_menu_item_new_with_label( "Unterebene");
    g_object_set_data( G_OBJECT(item_paste_up), "kind",
            GINT_TO_POINTER(1) );
    g_object_set_data( G_OBJECT(contextmenu), "item-paste-up", item_paste_up );
    g_signal_connect( G_OBJECT(item_paste_up), "activate",
            G_CALLBACK(zond_treeview_paste_activate), (gpointer) zond );
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_paste), item_paste_up );

    gtk_menu_item_set_submenu( GTK_MENU_ITEM(item_paste), menu_paste );

    gtk_menu_shell_append( GTK_MENU_SHELL(contextmenu), item_paste );

    //Link Einfügen
    GtkWidget* item_paste_as_link = gtk_menu_item_new_with_label("Als Link einfügen");

    GtkWidget* menu_paste_as_link = gtk_menu_new();

    GtkWidget* item_paste_as_link_ge = gtk_menu_item_new_with_label(
            "Gleiche Ebene");
    g_object_set_data( G_OBJECT(contextmenu), "item-paste-as-link-ge",
            item_paste_as_link_ge );
    g_object_set_data( G_OBJECT(item_paste_as_link_ge), "link",
            GINT_TO_POINTER(1) );
    g_signal_connect( G_OBJECT(item_paste_as_link_ge), "activate",
            G_CALLBACK(zond_treeview_paste_as_link_activate), (gpointer) zond );
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_paste_as_link), item_paste_as_link_ge );

    GtkWidget* item_paste_as_link_up = gtk_menu_item_new_with_label(
            "Unterebene");
    g_object_set_data( G_OBJECT(contextmenu), "item-paste-as-link-up",
            item_paste_as_link_up );
    g_object_set_data( G_OBJECT(item_paste_as_link_up), "kind",
            GINT_TO_POINTER(1) );
    g_object_set_data( G_OBJECT(item_paste_as_link_up), "link",
            GINT_TO_POINTER(1) );
    g_signal_connect( G_OBJECT(item_paste_as_link_up), "activate",
            G_CALLBACK(zond_treeview_paste_as_link_activate), (gpointer) zond );
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_paste_as_link),
            item_paste_as_link_up);

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_paste_as_link), menu_paste_as_link );

    gtk_menu_shell_append( GTK_MENU_SHELL(contextmenu), item_paste_as_link );

    GtkWidget* item_separator_1 = gtk_separator_menu_item_new();
    gtk_menu_shell_append( GTK_MENU_SHELL(contextmenu), item_separator_1 );

    //Punkt(e) löschen
    GtkWidget* item_loeschen = gtk_menu_item_new_with_label("Löschen");
    g_object_set_data( G_OBJECT(contextmenu), "item-loeschen", item_loeschen );
    g_signal_connect( G_OBJECT(item_loeschen), "activate",
            G_CALLBACK(zond_treeview_loeschen_activate), (gpointer) zond );
    gtk_menu_shell_append( GTK_MENU_SHELL(contextmenu), item_loeschen );

    //Anbindung entfernen
    GtkWidget* item_anbindung_entfernen = gtk_menu_item_new_with_label(
            "Anbindung entfernen");
    g_object_set_data( G_OBJECT(contextmenu), "item-anbindung-entfernen",
            item_anbindung_entfernen );
    g_signal_connect( G_OBJECT(item_anbindung_entfernen), "activate",
            G_CALLBACK(zond_treeview_anbindung_entfernen_activate), zond );
    gtk_menu_shell_append( GTK_MENU_SHELL(contextmenu), item_anbindung_entfernen );

    GtkWidget* item_jump = gtk_menu_item_new_with_label( "Zu Ursprung springen" );
    g_object_set_data( G_OBJECT(contextmenu), "item-jump", item_jump );
    g_signal_connect( item_jump, "activate", G_CALLBACK(zond_treeview_jump_activate), zond );
    gtk_menu_shell_append( GTK_MENU_SHELL(contextmenu), item_jump );

    GtkWidget* item_separator_2 = gtk_separator_menu_item_new();
    gtk_menu_shell_append( GTK_MENU_SHELL(contextmenu), item_separator_2 );

    //Icons ändern
    GtkWidget* item_icon = gtk_menu_item_new_with_label( "Icon ändern" );

    GtkWidget* menu_icon = gtk_menu_new( );

    for ( gint i = 0; i < NUMBER_OF_ICONS; i++ )
    {
        gchar* key = NULL;

        GtkWidget *icon = gtk_image_new_from_icon_name( zond->icon[i].icon_name, GTK_ICON_SIZE_MENU );
        GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget *label = gtk_label_new ( zond->icon[i].display_name );
        GtkWidget *item_menu_icons = gtk_menu_item_new ( );
        gtk_container_add (GTK_CONTAINER (box), icon);
        gtk_container_add (GTK_CONTAINER (box), label);
        gtk_container_add (GTK_CONTAINER (item_menu_icons), box);

        key = g_strdup_printf( "item-menu-icons-%i", i );
        g_object_set_data( G_OBJECT(contextmenu), key, item_menu_icons );
        g_free( key );

        g_object_set_data( G_OBJECT(item_menu_icons), "icon-id",
                GINT_TO_POINTER(i) );
        g_signal_connect( item_menu_icons, "activate",
                G_CALLBACK(zond_treeview_icon_activate), (gpointer) zond );

        gtk_menu_shell_append( GTK_MENU_SHELL(menu_icon), item_menu_icons );
    }

    gtk_menu_item_set_submenu( GTK_MENU_ITEM(item_icon),
            menu_icon );

    gtk_menu_shell_append( GTK_MENU_SHELL(contextmenu), item_icon );

    //Datei Öffnen
    GtkWidget* item_datei_oeffnen = gtk_menu_item_new_with_label( "Öffnen" );
    g_object_set_data( G_OBJECT(contextmenu), "item-datei-oeffnen",
            item_datei_oeffnen );
    g_signal_connect( item_datei_oeffnen, "activate",
                G_CALLBACK(zond_treeview_datei_oeffnen_activate), (gpointer) zond );
    gtk_menu_shell_append( GTK_MENU_SHELL(contextmenu), item_datei_oeffnen );

    //Datei Öffnen
    GtkWidget* item_datei_oeffnen_mit = gtk_menu_item_new_with_label( "Öffnen mit" );
    g_object_set_data( G_OBJECT(contextmenu), "item-datei-oeffnen-mit",
            item_datei_oeffnen_mit );
    g_signal_connect( item_datei_oeffnen_mit, "activate",
                G_CALLBACK(zond_treeview_datei_oeffnen_mit_activate), (gpointer) zond );
    gtk_menu_shell_append( GTK_MENU_SHELL(contextmenu), item_datei_oeffnen_mit );

    gtk_widget_show_all( contextmenu );

    return;
}


ZondTreeview*
zond_treeview_new( Projekt* zond, gint root_node_id )
{
    ZondTreeview* ztv = NULL;
    ZondTreeviewPrivate* ztv_priv = NULL;

    ztv = g_object_new( ZOND_TYPE_TREEVIEW, NULL );

    ztv_priv = zond_treeview_get_instance_private( ztv );
    ztv_priv->zond = zond;
    sond_treeview_set_id( SOND_TREEVIEW(ztv), root_node_id );
    zond_tree_store_set_root( ZOND_TREE_STORE(gtk_tree_view_get_model(
            GTK_TREE_VIEW(ztv) )), root_node_id );

    zond_treeview_init_contextmenu( ztv );

    // Doppelklick = angebundene Datei anzeigen
    g_signal_connect( ztv, "row-activated",
            G_CALLBACK(zond_treeview_row_activated), (gpointer) ztv_priv->zond );
    //Zeile expandiert
    g_signal_connect( ztv, "row-expanded",
                G_CALLBACK(zond_treeview_row_expanded), zond );

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


static gint
zond_treeview_load_node( ZondTreeview* ztv, gint node_id, GtkTreeIter* iter_anchor,
        gboolean child, GtkTreeIter* iter_inserted, gint anchor_id,
        gint* node_id_inserted, GError** error )
{
    gint type = 0;
    gint link = 0;
    gchar* icon_name = NULL;
    gchar* node_text = NULL;
    gint rc = 0;
    GtkTreeIter iter_new = { 0 };

    ZondTreeviewPrivate* ztv_priv = zond_treeview_get_instance_private( ztv );

    rc = zond_dbase_get_node( ztv_priv->zond->dbase_zond->zond_dbase_work,
            node_id, &type, &link, NULL, NULL, NULL, NULL, NULL, &icon_name, &node_text,
            NULL, error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    if ( type == ZOND_DBASE_TYPE_BAUM_INHALT_FILE )
    {
        gint rc = 0;

        rc = zond_treeview_load_baum_inhalt_file( ztv, iter_anchor, child, node_id,
                icon_name, node_text, NULL, error );
        if ( rc ) ERROR_Z
    }
    else if ( type == ZOND_DBASE_TYPE_BAUM_INHALT_PDF_ABSCHNITT )
    {
        gint rc = 0;

        rc = zond_treeview_walk_tree( ztv, FALSE, link, iter_anchor, child, NULL,
                0, NULL, zond_treeview_insert_pdf_abschnitt, error );
        if ( rc )
        {
            g_prefix_error( error, "%s\n", __func__ );

            return -1;
        }
    }
    else
    {
        zond_tree_store_insert( ZOND_TREE_STORE(gtk_tree_view_get_model(
                GTK_TREE_VIEW(ztv) )), iter_anchor, child, &iter_new );
        if ( type == ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_LINK )
        {
            icon_name = g_strdup_printf( "%d", node_id ); //head_nr wird hier gespeichert
            node_id = link * -1;
        }

        zond_tree_store_set( &iter_new, icon_name, node_text, node_id );
    }

    g_free( icon_name );
    g_free( node_text );

    return 0;
}


static gboolean
zond_treeview_insert_links_foreach( GtkTreeModel* model, GtkTreePath* path,
        GtkTreeIter* iter, gpointer user_data )
{
    gchar* icon_name = NULL;
    gint node_id = 0;
    gint root = 0;
    GtkTreeIter iter_anchor = { 0 };
    gboolean child = FALSE;
    GtkTreeIter* iter_target = NULL;
    GError* error = NULL;
    gint rc = 0;

    Projekt* zond = (Projekt*) user_data;

    gtk_tree_model_get( model, iter, 0, &icon_name, 2, &node_id, -1 );

    if ( node_id > 0 ) return FALSE;

    node_id *= -1;

    rc = zond_dbase_get_tree_root( zond->dbase_zond->zond_dbase_work, node_id, &root, &error );
    if ( rc )
    {
        display_message( zond->app_window, error->message, NULL );
        g_error_free( error );

        return FALSE;
    }

    //iter_anchor basteln
    iter_anchor = *iter;
    if ( ((GNode*) (iter->user_data))->prev == NULL )
    {
        iter_anchor.user_data = ((GNode*) (iter->user_data))->parent;
        child = TRUE;
    }
    else
    {
        iter_anchor.user_data = ((GNode*) (iter->user_data))->prev;
        child = FALSE;
    }

    zond_tree_store_remove( iter );

    //iter_target ermitteln
    iter_target = zond_treeview_abfragen_iter( ZOND_TREEVIEW(zond->treeview[root]), node_id );
    if ( !iter_target )
    {
        display_message( zond->app_window, "zond_treeview_abfragen_iter (target) gibt NULL zurück", NULL );
        g_free( icon_name );

        return FALSE;
    }

    zond_tree_store_insert_link( iter_target, atoi( icon_name ),
            zond_tree_store_get_tree_store( &iter_anchor ),
            ((GNode*) (iter_anchor.user_data) == zond_tree_store_get_root_node( zond_tree_store_get_tree_store( &iter_anchor ) )) ? NULL :
             &iter_anchor, child, NULL );

    g_free( icon_name );
    gtk_tree_iter_free( iter_target );

    return FALSE;
}


gint
zond_treeview_load_baum( ZondTreeview* ztv, GError** error )
{
    gint first_child = 0;
    gint rc = 0;

    ZondTreeviewPrivate* ztv_priv = zond_treeview_get_instance_private( ztv );

    zond_tree_store_clear( ZOND_TREE_STORE(gtk_tree_view_get_model(
            GTK_TREE_VIEW(ztv) )) );

    rc = zond_dbase_get_first_child( ztv_priv->zond->dbase_zond->zond_dbase_work,
            zond_tree_store_get_root( ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(ztv) )) ),
            &first_child, error );
    if ( rc ) ERROR_Z
    else if ( first_child == 0 ) return 0; //Baum leer

    rc = zond_treeview_walk_tree( ztv, TRUE, first_child, NULL, TRUE, NULL,
            zond_tree_store_get_root( ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(ztv) )) ),
            NULL, zond_treeview_load_node, error );
    if ( rc ) ERROR_Z

    gtk_tree_model_foreach( gtk_tree_view_get_model( GTK_TREE_VIEW(ztv) ),
            zond_treeview_insert_links_foreach, ztv_priv->zond );

    return 0;
}



