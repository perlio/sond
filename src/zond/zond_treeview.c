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

#include "40viewer/viewer.h"

#include "99conv/general.h"



typedef struct
{
    Projekt* zond;
} ZondTreeviewPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(ZondTreeview, zond_treeview, SOND_TYPE_TREEVIEW)


static gint
zond_treeview_get_root( ZondTreeview* ztv, gint node_id, gint* root, GError** error )
{
    gint rc = 0;
    gint type = 0;

    ZondTreeviewPrivate* ztv_priv = zond_treeview_get_instance_private( ztv );

    rc = zond_dbase_get_type_and_link( ztv_priv->zond->dbase_zond->zond_dbase_work,
            node_id, &type, NULL, error );
    if ( rc ) ERROR_Z

    if ( type == ZOND_DBASE_TYPE_FILE_PART )
    {
        gint rc = 0;
        gint baum_inhalt_file = 0;

        //prüfen, ob angebundener file_part
        rc = zond_dbase_find_baum_inhalt_file( ztv_priv->zond->dbase_zond->zond_dbase_work,
                node_id, &baum_inhalt_file, NULL, NULL, error );
        if ( rc ) ERROR_Z

        if ( baum_inhalt_file ) node_id = baum_inhalt_file;
    }

    rc = zond_dbase_get_tree_root( ztv_priv->zond->dbase_zond->zond_dbase_work,
            node_id, root, error );
    if ( rc ) ERROR_Z

    return 0;
}


void
zond_treeview_cursor_changed( ZondTreeview* treeview, gpointer user_data )
{
    gint rc = 0;
    gint node_id = 0;
    GtkTreeIter iter = { 0, };
    gint type = 0;
    gint link = 0;
    gchar* file_part = NULL;
    gchar* section = NULL;
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

            rc = zond_treeview_get_root( treeview, zond->node_id_extra, &root, &error );
            if ( rc )
            {
                text_label = g_strconcat( "Fehler in ", __func__, "\n",
                        error->message, NULL );
                g_error_free( error );
                gtk_label_set_text( zond->label_status, text_label );
                g_free( text_label );

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
            node_id, &type, &link, &file_part, &section, NULL, NULL, NULL, &error );
    if ( rc )
    {
        text_label = g_strconcat( "Fehler in ", __func__, ":\n",
                error->message, NULL );
        g_error_free( error );
        gtk_label_set_text( zond->label_status, text_label );
        g_free( text_label );

        return;
    }

    if ( type == ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_COPY )
    { //dann link-node holen
        gint rc = 0;
        gint node_id_link = 0;

        node_id_link = link;

        rc = zond_dbase_get_node( zond->dbase_zond->zond_dbase_work,
                node_id_link, &type, &link, &file_part, &section, NULL, NULL, NULL, &error );
        if ( rc )
        {
            text_label = g_strconcat( "Fehler in ", __func__, "\n",
                    error->message, NULL );
            g_error_free( error );
            gtk_label_set_text( zond->label_status, text_label );
            g_free( text_label );

            return;
        }
    }

    if ( type == ZOND_DBASE_TYPE_BAUM_STRUKT ) text_label = g_strdup( "" );
    else text_label = g_strdup_printf( "%s - %s", file_part, section );

    /*else if ( type == ZOND_DBASE_TYPE_BAUM_INHALT_FILE ||
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
                g_strdup_printf( " - S. %i, Index %i", anbindung.bis.seite + 1,
                anbindung.bis.index ) );
    }
*/
    g_free( file_part );
    g_free( section );

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


static void
zond_treeview_text_edited( SondTreeview* stv, GtkTreeIter* iter, gchar const* new_text )
{
    gint rc = 0;
    GError* error = NULL;
    gint node_id = 0;

    ZondTreeview* ztv = ZOND_TREEVIEW(stv);

    ZondTreeviewPrivate* ztv_priv = zond_treeview_get_instance_private( ztv );

    gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(ztv) ), iter, 2, &node_id, -1 );

    rc = zond_dbase_update_node_text( ztv_priv->zond->dbase_zond->zond_dbase_work, node_id, new_text, &error );
    if ( rc )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(ztv) ),
                "Knoten umbenennen nicht möglich\n\n", error->message, NULL );
        g_error_free( error );

        return;
    }

    zond_tree_store_set( iter, NULL, new_text, 0 );
    gtk_tree_view_columns_autosize( GTK_TREE_VIEW(ztv) );

    zond_treeviewfm_set_pdf_abschnitt( ZOND_TREEVIEWFM(ztv_priv->zond->treeview[BAUM_FS]),
            node_id, new_text );

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
            markuptxt = add_string( g_strdup( "<span foreground=\"purple\">" ), markuptxt );
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

    //chain-up
    G_OBJECT_CLASS(zond_treeview_parent_class)->constructed( self );

    return;
}


static void
zond_treeview_class_init( ZondTreeviewClass* klass )
{
    G_OBJECT_CLASS(klass)->constructed = zond_treeview_constructed;

    SOND_TREEVIEW_CLASS(klass)->render_text_cell = zond_treeview_render_node_text;
    SOND_TREEVIEW_CLASS(klass)->text_edited = zond_treeview_text_edited;

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
        if ( head_nr <= 0 )
        {
            gint rc = 0;
            gint anchor_id_tree = 0;
            gint baum_inhalt_file = 0;
            GError* error = NULL;

            gtk_tree_model_get( GTK_TREE_MODEL(zond_tree_store_get_tree_store(
                    &iter_anchor_intern )), &iter_anchor_intern, 2, &anchor_id_tree, -1 );

            rc = zond_dbase_get_baum_inhalt_file_from_file_part( zond->dbase_zond->zond_dbase_work,
                    anchor_id_tree, &baum_inhalt_file, &error );
            if ( rc ) //ToDo: richtige Error-Bearbeitung
            {
                display_message( zond->app_window, "Fehler Ermittlung anchor_id\n\n", error->message, NULL );
                g_error_free( error );

                return FALSE;
            }

            if ( baum_inhalt_file ) *anchor_id = baum_inhalt_file;
            else *anchor_id = anchor_id_tree;
        }
        else *anchor_id = head_nr;
    }

    if ( iter_anchor ) *iter_anchor = iter_anchor_intern;

    return TRUE;
}


static gint
zond_treeview_hat_vorfahre_datei( Projekt* zond, GtkTreeIter* iter_anchor,
        gint anchor_id, gboolean child, GError** error )
{
    gint rc = 0;
    gint type = 0;

    if ( anchor_id == 0 ) return 0;

    if ( !child )
    {
        GtkTreeIter iter_parent = { 0 };
        gint parent_id = 0;

        //Muß Eltern aus dem tree holen, nicht aus der DB, weil BAUM_INHALT_PDF_ABSCHNITT sonst nicht bemerkt wird
        if ( !gtk_tree_model_iter_parent( GTK_TREE_MODEL(zond_tree_store_get_tree_store( iter_anchor )),
                &iter_parent, iter_anchor ) ) return 0;

        gtk_tree_model_get( GTK_TREE_MODEL(zond_tree_store_get_tree_store( iter_anchor )),
                &iter_parent, 2, &parent_id, -1 );

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
    gint node_id_new = 0;
    GtkTreeIter iter_cursor = { 0 };
    GtkTreeIter iter_anchor = { 0 };
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

        rc = zond_treeview_hat_vorfahre_datei( zond, &iter_anchor, anchor_id, child, &error );
        if ( rc == -1 )
        {
            if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__,
                    error->message );
            g_error_free( error );

            return -1;
        }
        else if ( rc == 1 ) return 1;
    }

    //Knoten in Datenbank einfügen
    node_id_new = zond_dbase_insert_node( zond->dbase_zond->zond_dbase_work,
            anchor_id, child, ZOND_DBASE_TYPE_BAUM_STRUKT, 0, NULL, NULL,
            zond->icon[ICON_NORMAL].icon_name, "Neuer Punkt", NULL, &error );
    if ( node_id_new == -1 )
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
            sond_treeview_expand_row( zond->treeview[zond->baum_active], &iter_cursor );
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
    GtkTreeIter iter_new = { 0 };

    ZondTreeviewPrivate* ztv_priv = zond_treeview_get_instance_private( ztv );

    rc = walk_tree( ztv, node_id, iter_anchor, child, &iter_new, anchor_id, &node_id_new, error );
    if ( rc == -1 ) ERROR_Z
    else if ( rc == 0 ) //kein Abbruch gewählt, dann weiter in die Tiefe
    {
        rc = zond_dbase_get_first_child( ztv_priv->zond->dbase_zond->zond_dbase_work,
                node_id, &first_child, error );
        if ( rc ) ERROR_Z
        else if ( first_child > 0 )
        {
            gint rc = 0;

            rc = zond_treeview_walk_tree( ztv, TRUE, first_child, &iter_new, TRUE,
                NULL, node_id_new, NULL, walk_tree, error );
            if ( rc ) ERROR_Z
        }
    }

    if ( with_younger_siblings )
    {
        gint younger_sibling = 0;

        rc = zond_dbase_get_younger_sibling( ztv_priv->zond->dbase_zond->zond_dbase_work,
                node_id, &younger_sibling, error );
        if ( rc ) ERROR_Z
        else if ( younger_sibling > 0 )
        {
            rc = zond_treeview_walk_tree( ztv, TRUE, younger_sibling, &iter_new, FALSE,
                    NULL, node_id_new, NULL, walk_tree, error );
            if ( rc ) ERROR_Z
        }
    }

    if ( node_id_inserted ) *node_id_inserted = node_id_new;
    if ( iter_inserted ) *iter_inserted = iter_new;

    return 0;
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


static gint
zond_treeview_insert_file_parts( ZondTreeview* ztv, gint node_id,
        GtkTreeIter* iter, gboolean child, GtkTreeIter* iter_inserted,
        gint anchor_id, gint* node_id_inserted, GError** error )
{
    gint rc = 0;
    gchar* icon_name = NULL;
    gchar* node_text = NULL;
    GtkTreeIter iter_new = { 0 };
    GtkTreeIter* iter_pdf_abschnitt = NULL;

    ZondTreeviewPrivate* ztv_priv = zond_treeview_get_instance_private( ztv );

    //Wenn Zweig schon vorhanden ist, weil etwa BAUM_INHALT_PDF_ABSCHNITT bestanden hat
    iter_pdf_abschnitt = zond_treeview_abfragen_iter( ztv, node_id );
    if ( iter_pdf_abschnitt )
    {
        zond_tree_store_move_node( iter_pdf_abschnitt,
                ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(ztv_priv->zond->treeview[BAUM_INHALT]) )),
                iter, child, iter_inserted );

        gtk_tree_iter_free( iter_pdf_abschnitt );

        return 1;
    }

    //Ansonsten Einfügen
    rc = zond_dbase_get_node( ztv_priv->zond->dbase_zond->zond_dbase_work,
            node_id, NULL, NULL, NULL, NULL, &icon_name, &node_text, NULL, error );
    if ( rc ) ERROR_Z

    zond_tree_store_insert( ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(ztv) )), iter,
            child, &iter_new );
    zond_tree_store_set( &iter_new, icon_name, node_text, node_id );

    g_free( icon_name);
    g_free( node_text );

    if ( iter_inserted ) *iter_inserted = iter_new;

    return 0;
}


gint
zond_treeview_insert_file_in_db( Projekt* zond, gchar const* rel_path,
        gchar const* icon_name, gint* file_root, GError** error )
{
    gint rc = 0;
    gchar const* basename = NULL;
    gint file_root_int = 0;

    basename = g_strrstr( rel_path, "/" );
    if ( !basename ) basename = rel_path;

    rc = zond_dbase_create_file_root( zond->dbase_zond->zond_dbase_work,
            rel_path, icon_name, basename, NULL, &file_root_int, error );
    if ( rc ) ERROR_Z

    if ( file_root ) *file_root = file_root_int;

    //ToDo: MimeParts und zip einfügen

    return 0;
}


static gint
zond_treeview_remove_childish_anbindungen( ZondTreeview* ztv, InfoWindow* info_window,
        gint ID, gint* anchor_id, gboolean* child, Anbindung* anbindung, GError** error )
{
    gint resp = 0;

    ZondTreeviewPrivate* ztv_priv = zond_treeview_get_instance_private( ztv );

    do
    {
        gint rc = 0;
        gint baum_inhalt_file = 0;

        rc = zond_dbase_get_first_baum_inhalt_file_child( ztv_priv->zond->dbase_zond->zond_dbase_work,
                ID, &baum_inhalt_file, NULL, error );
        if ( rc ) ERROR_Z

        if ( !baum_inhalt_file ) break;

        //noch nicht gefrägt gehabt?!
        if ( !resp )
        {
            info_window_set_message( info_window, "...Abschnitt bereits angebunden" );

            resp = abfrage_frage( ztv_priv->zond->app_window,
                    "Mindestens ein Teil des PDF ist bereits angebunden",
                    "Abschnitt hinzuziehen?", NULL );
            if ( resp != GTK_RESPONSE_YES ) return 1;
        }

        //Prüfen, ob man sich die anchor_id löschen würde...
        if ( baum_inhalt_file == *anchor_id )
        {
            gint rc = 0;
            gint older_sibling = 0;

            //kann ja nicht child == TRUE sein, weil dann würde ja in Datei eingefügt, was sowieso verboten ist
            //also dann older sibling
            rc = zond_dbase_get_older_sibling( ztv_priv->zond->dbase_zond->zond_dbase_work,
                    baum_inhalt_file, &older_sibling, error );
            if ( rc ) ERROR_Z

            //sonst parent
            if ( !older_sibling )
            {
                gint rc = 0;
                gint parent = 0;

                rc = zond_dbase_get_parent( ztv_priv->zond->dbase_zond->zond_dbase_work,
                        baum_inhalt_file, &parent, error );
                if ( rc ) ERROR_Z

                *anchor_id = parent;
                *child = TRUE;
            }
            else *anchor_id = older_sibling;
        }

        //BAUM_INHALT_PDF_ABSCHNITT löschen
        rc = zond_dbase_remove_node( ztv_priv->zond->dbase_zond->zond_dbase_work,
                baum_inhalt_file, error );
        if ( rc ) ERROR_Z
    } while ( 1 );

    return 0;
}


/** Fehler: -1
    eingefügt: node_id
    nicht eingefügt, weil schon angebunden: 0 **/
static gint
zond_treeview_datei_anbinden( ZondTreeview* ztv, GtkTreeIter* anchor_iter,
        gint anchor_id, gboolean child, GFileInfo* info, gchar const* rel_path,
        InfoWindow* info_window, gint* zaehler, gchar** errmsg )
{
    gint new_node_id = 0;
    GtkTreeIter iter_new = { 0 };
    GError* error = NULL;
    gchar* file_part = NULL;
    gint file_part_root = 0;
    gint rc = 0;

    ZondTreeviewPrivate* ztv_priv = zond_treeview_get_instance_private( ztv );

    if ( info_window->cancel ) return -2;

    info_window_set_message( info_window, rel_path );

    file_part = g_strdup_printf( "/%s//", rel_path );
    rc = zond_dbase_get_file_part_root( ztv_priv->zond->dbase_zond->zond_dbase_work,
            file_part, &file_part_root, &error );
    g_free( file_part );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
        g_error_free( error );

        return -1;
    }

    if ( file_part_root ) //prüfen, ob Datei schon angebunden
    {
        gint rc = 0;
        gint baum_inhalt_file = 0;

        //wenn schon pdf_root existiert, dann herausfinden, ob aktuell an Baum angebunden
        rc = zond_dbase_get_baum_inhalt_file_from_file_part(ztv_priv->zond->dbase_zond->zond_dbase_work,
                file_part_root, &baum_inhalt_file, &error );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
            g_error_free( error );

            return -1;
        }

        if ( baum_inhalt_file )
        {
            info_window_set_message( info_window, "...bereits angebunden" );

            return 0; //Wenn angebunden: nix machen
        }

        //etwaige untergeordnete Anbindungen heranholen, falls gewünscht
        rc = zond_treeview_remove_childish_anbindungen( ztv, info_window, file_part_root, &anchor_id,
                &child, NULL, &error );
        if ( rc == -1 )
        {
            if ( errmsg ) *errmsg = g_strdup( error->message );
            g_error_free( error );

            return -1;
        }
        else if ( rc == 1 ) return 0; //will nicht...
    }
    else //Datei noch nicht in zond_dbase
    {
        gint rc = 0;

        rc = zond_treeview_insert_file_in_db( ztv_priv->zond, rel_path, zond_treeview_get_icon_name( info ), &file_part_root, &error );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strdup( error->message );
            g_error_free( error );

            return -1;
        }
    }

    new_node_id = zond_dbase_insert_node( ztv_priv->zond->dbase_zond->zond_dbase_work,
            anchor_id, child, ZOND_DBASE_TYPE_BAUM_INHALT_FILE, file_part_root, NULL, NULL,
            NULL, NULL, NULL, &error );
    if ( new_node_id == -1 )
    {
        if ( errmsg ) *errmsg = g_strdup( error->message );
        g_error_free( error );

        return -1;
    }

    rc = zond_treeview_walk_tree( ztv, FALSE, file_part_root,
        (zond_tree_store_get_root( ZOND_TREE_STORE(gtk_tree_view_get_model(
        GTK_TREE_VIEW(ztv) )) ) == anchor_id) ? NULL : anchor_iter, child, &iter_new, 0, NULL,
        zond_treeview_insert_file_parts, &error );
    if ( rc == -1 )
    {
        if ( errmsg ) *errmsg = g_strdup( error->message );
        g_error_free( error );

        return -1;
    }

    *anchor_iter = iter_new;
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
    gboolean child_loop = TRUE;

    ZondTreeviewPrivate* ztv_priv = zond_treeview_get_instance_private( ztv );

    if ( info_window->cancel ) return -2;

    basename = g_file_info_get_name( info );

    new_node_id = zond_dbase_insert_node( ztv_priv->zond->dbase_zond->zond_dbase_work,
            anchor_id, child, ZOND_DBASE_TYPE_BAUM_STRUKT, 0, NULL, NULL,
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
            GFileType type = G_FILE_TYPE_UNKNOWN;

            type = g_file_info_get_file_type( info_child );

            if ( type == G_FILE_TYPE_DIRECTORY )
            {
                new_node_id_loop = zond_treeview_ordner_anbinden_rekursiv( ztv,
                        &iter_anchor_loop, anchor_id_loop, child_loop, file_child, info_child,
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

                rel_path = get_rel_path_from_file( ztv_priv->zond->dbase_zond->project_dir, file_child );
                new_node_id_loop = zond_treeview_datei_anbinden( ztv,
                        &iter_anchor_loop, anchor_id_loop, child_loop,
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
            child_loop = FALSE;
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
    gint zaehler;
    InfoWindow* info_window;
} SSelectionAnbinden;

static gint
zond_treeview_clipboard_anbinden_foreach( SondTreeview* stv, GtkTreeIter* iter,
        gpointer data, gchar** errmsg )
{
    GObject* object = NULL;
    gint node_id_new = 0;

    SSelectionAnbinden* s_selection = (SSelectionAnbinden*) data;

    //Object im ZondTreeviewFM holen
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

            node_id_new = zond_treeview_ordner_anbinden_rekursiv( s_selection->ztv,
                    &s_selection->anchor_iter, s_selection->anchor_id, s_selection->child,
                    file, G_FILE_INFO(object), s_selection->info_window, &s_selection->zaehler,
                    errmsg );
            g_object_unref( file );
        }
        else
        {
            gchar* rel_path = NULL;

            rel_path = sond_treeviewfm_get_rel_path( SOND_TREEVIEWFM(stv), iter );

            node_id_new = zond_treeview_datei_anbinden( s_selection->ztv,
                    &s_selection->anchor_iter, s_selection->anchor_id, s_selection->child,
                    G_FILE_INFO(object), rel_path, s_selection->info_window,
                    &s_selection->zaehler, errmsg );
            g_free( rel_path );
        }

        g_object_unref( object );
        if ( node_id_new == -1 ) ERROR_S
        else if ( node_id_new == 0 ) return 0; //wenn schon angebunden ist
        else if ( node_id_new == -2 ) return 1; //sond_treeview_..._foreach bricht dann ab
    }
    else if ( ZOND_IS_PDF_ABSCHNITT(object) )
    {
        gint rc = 0;
        gint ID = 0;
        gchar const* rel_path = NULL;
        Anbindung anbindung = { 0 };
        GError* error = NULL;
        gint baum_inhalt_file = 0;
        gchar* text = NULL;

        ZondPdfAbschnitt* zpa = ZOND_PDF_ABSCHNITT(object);
        ZondTreeviewPrivate* ztv_priv = zond_treeview_get_instance_private( s_selection->ztv );

        zond_pdf_abschnitt_get( zpa, &ID, &rel_path, &anbindung, NULL, NULL );

        text = g_strdup_printf( "%s, S. %d, %d - S. %d, %d", rel_path, anbindung.von.seite,
                anbindung.von.index, anbindung.bis.seite, anbindung.bis.index );
        info_window_set_message( s_selection->info_window, text );
        g_free( text );

        //Test, ob Eltern-Abschnitt angebunden
        rc = zond_dbase_get_baum_inhalt_file_from_file_part( ztv_priv->zond->dbase_zond->zond_dbase_work,
                ID, &baum_inhalt_file, &error );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
            g_error_free( error );

            return -1;
        }

        if ( baum_inhalt_file )
        {
            info_window_set_message( s_selection->info_window, "...bereits angebunden" );

            return 0;
        }

        rc = zond_treeview_remove_childish_anbindungen( s_selection->ztv, s_selection->info_window,
                ID, &s_selection->anchor_id, &s_selection->child, &anbindung, &error );
        if ( rc == -1 )
        {
            if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
            g_error_free( error );

            return -1;
        }
        else if ( rc == 1 ) return 0;

        node_id_new = zond_dbase_insert_node( ztv_priv->zond->dbase_zond->zond_dbase_work,
                s_selection->anchor_id, s_selection->child, ZOND_DBASE_TYPE_BAUM_INHALT_FILE,
                ID, NULL, NULL, NULL, NULL, NULL, &error );
        if ( node_id_new == -1 )
        {
            if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
            g_error_free( error );

            return -1;
        }

        rc = zond_treeview_walk_tree( s_selection->ztv, FALSE, ID, &s_selection->anchor_iter, s_selection->child,
                NULL, 0, NULL, zond_treeview_insert_file_parts, &error );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
            g_error_free( error );

            return -1;
        }

        (s_selection->zaehler)++;
    }

    s_selection->child = FALSE;
    s_selection->anchor_id = node_id_new;

    return 0;
}


static gint
zond_treeview_clipboard_anbinden( Projekt* zond, gint anchor_id, GtkTreeIter* anchor_iter,
        gboolean child, InfoWindow* info_window, gchar** errmsg )
{
    gint rc = 0;
    SSelectionAnbinden s_selection = { 0 };

    s_selection.ztv = ZOND_TREEVIEW(zond->treeview[BAUM_INHALT]);
    s_selection.anchor_id = anchor_id;
    s_selection.anchor_iter = *anchor_iter;
    s_selection.child = child;
    s_selection.zaehler = 0;
    s_selection.info_window = info_window;

    rc = sond_treeview_clipboard_foreach( zond_treeview_clipboard_anbinden_foreach, &s_selection, errmsg );
    if ( rc == -1 ) ERROR_S

    if ( s_selection.zaehler ) sond_treeview_expand_row( zond->treeview[BAUM_INHALT], &s_selection.anchor_iter );
    sond_treeview_set_cursor( zond->treeview[BAUM_INHALT], &s_selection.anchor_iter );

    gtk_tree_view_columns_autosize( GTK_TREE_VIEW(((Projekt*) zond)->treeview[BAUM_INHALT]) );

    gchar* text = g_strdup_printf( "%i Anbindungen eingefügt", s_selection.zaehler );
    info_window_set_message( info_window, text );
    g_free( text );

    return 0;
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

        if ( type == ZOND_DBASE_TYPE_FILE_PART )
        { //Test, ob als BAUM_INHALT_PDF_ABSCHNITT angebunden - dann geht verschieben
            gint rc = 0;
            gint baum_inhalt_file = 0;

            rc = zond_dbase_get_baum_inhalt_file_from_file_part( s_selection->zond->dbase_zond->zond_dbase_work,
                    node_id, &baum_inhalt_file, &error );
            if ( rc == -1 )
            {
                if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
                g_error_free( error );

                return -1;
            }

            if ( !baum_inhalt_file ) return 0;

            node_id = baum_inhalt_file;
        }
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

    zond_tree_store_move_node( iter_src, zond_tree_store_get_tree_store( s_selection->iter_anchor ),
            s_selection->iter_anchor, s_selection->child, &iter_new );

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
    gchar* icon_name = NULL;
    gchar* node_text = NULL;
    gchar* text = NULL;
    gint rc = 0;

    ZondTreeviewPrivate* ztv_priv = zond_treeview_get_instance_private( ztv );

    rc = zond_dbase_get_node( ztv_priv->zond->dbase_zond->zond_dbase_work,
            node_id, NULL, NULL, NULL, NULL, &icon_name, &node_text,
            &text, error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    *node_id_inserted = zond_dbase_insert_node( ztv_priv->zond->dbase_zond->zond_dbase_work,
            anchor_id, child, ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_COPY, node_id, NULL, NULL,
            icon_name, node_text, text, error );
    if ( *node_id_inserted == -1 )
    {
        g_prefix_error( error, "%s\n", __func__ );
        g_free( icon_name );
        g_free( node_text );
        g_free( text );

        return -1;
    }

    zond_tree_store_insert( ZOND_TREE_STORE(gtk_tree_view_get_model(
            GTK_TREE_VIEW(ztv) )), iter, child, iter_inserted );
    zond_tree_store_set( iter_inserted, icon_name, node_text, *node_id_inserted );

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

    ZondTreeviewPrivate* ztv_priv = zond_treeview_get_instance_private( ztv );

    rc = zond_dbase_get_node( ztv_priv->zond->dbase_zond->zond_dbase_work,
            node_id, &type, &link, NULL, NULL, &icon_name, &node_text,
            &text, error );
    if ( rc ) ERROR_Z

    if ( type != ZOND_DBASE_TYPE_BAUM_STRUKT )
    {
        if ( type == ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_COPY ||
                type == ZOND_DBASE_TYPE_BAUM_INHALT_FILE ) link_new = link;
        else link_new = node_id;

        type_new = ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_COPY;
    }
    else type_new = ZOND_DBASE_TYPE_BAUM_STRUKT;

    node_id_new = zond_dbase_insert_node( ztv_priv->zond->dbase_zond->zond_dbase_work,
            anchor_id, child, type_new, link_new, NULL, NULL, icon_name, node_text, text, error );
    if ( node_id_new == -1 )
    {
        g_prefix_error( error, "%s\n", __func__ );
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

    if ( type == ZOND_DBASE_TYPE_BAUM_INHALT_FILE )
    {
        gint first_child = 0;

        rc = zond_dbase_get_first_child( ztv_priv->zond->dbase_zond->zond_dbase_work,
                link, &first_child, error );
        if ( rc ) ERROR_Z

        if ( first_child == 0 ) return 0;

        rc = zond_treeview_walk_tree( ztv, TRUE, first_child,
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
    GtkTreeIter iter_new = { 0 };
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
            &iter_new, s_selection->anchor_id, &node_id_new,
            zond_treeview_copy_node_to_baum_auswertung, &error );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
        g_error_free( error );

        ERROR_ROLLBACK( s_selection->zond->dbase_zond->zond_dbase_work )
    }

    rc = zond_dbase_commit( s_selection->zond->dbase_zond->zond_dbase_work, &error );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strdup( error->message );
        g_error_free( error );
        ERROR_ROLLBACK( s_selection->zond->dbase_zond->zond_dbase_work )
    }

    s_selection->child = FALSE;
    *(s_selection->iter_anchor) = iter_new;
    s_selection->anchor_id = node_id_new;

    return 0;
}


static gint
zond_treeview_clipboard_kopieren( Projekt* zond, gboolean child,
        GtkTreeIter* iter_cursor, GtkTreeIter* iter_anchor,
        gint anchor_id, gchar** errmsg )
{
    SSelection s_selection = { zond, iter_anchor, child, anchor_id };

    if ( zond_tree_store_get_tree_store( iter_cursor ) ==
            ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[BAUM_AUSWERTUNG]) )) )
    {
        gint rc = 0;

        rc = sond_treeview_clipboard_foreach( zond_treeview_clipboard_kopieren_foreach,
            &s_selection, errmsg );
        if ( rc == -1 ) ERROR_S
    }
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
    gint node_id_new = 0;
    gint node_id = 0;
    GtkTreeIter iter_target = { 0 };
    GtkTreeIter iter_new = { 0 };
    GError* error = NULL;

    SSelection* s_selection = (SSelection*) data;

    //soll durch etwaige links "hindurchgucken"
    gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(tree_view) ), iter, 2, &node_id, -1 );

    //node ID, auf den link zeigen soll
    node_id_new = zond_dbase_insert_node( s_selection->zond->dbase_zond->zond_dbase_work,
            s_selection->anchor_id, s_selection->child, ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_LINK,
            node_id, NULL, NULL, NULL, NULL, NULL, &error );
    if ( node_id_new < 0 )
    {
        if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
        g_error_free( error );

        ERROR_S
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

    SSelection s_selection = { zond, iter_anchor, child, anchor_id };


    rc = sond_treeview_clipboard_foreach( zond_treeview_paste_clipboard_as_link_foreach, &s_selection, errmsg );
    if ( rc == -1 ) ERROR_S

    if ( child && (iter_cursor->user_data !=
            zond_tree_store_get_root_node(
            zond_tree_store_get_tree_store( iter_cursor ) )) )
            sond_treeview_expand_row( zond->treeview[zond->baum_active], iter_cursor );
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
    gint root = 0;

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
                ERROR_S_MESSAGE( "Unzulässiges Ziel: Abkömmling von einzufügendem "
                "Knoten" )
    }

    success = zond_treeview_get_anchor( zond, child, &iter_cursor, &iter_anchor, &anchor_id );
    if ( !success ) child = TRUE;
    //in link soll nix eingefügt werden - Konsequenzen kann man nicht überblicken
    else if ( !(iter_cursor.stamp == iter_anchor.stamp &&
            iter_cursor.user_data == iter_anchor.user_data) )
            ERROR_S_MESSAGE( "Unzulässiges Ziel: Link" )

    if ( (root = zond_tree_store_get_root( zond_tree_store_get_tree_store( &iter_anchor ) )) == BAUM_INHALT )
    {
        gint rc = 0;

        if ( anchor_id != root )
        {
            gint rc = 0;
            GError* error = NULL;

            rc = zond_treeview_hat_vorfahre_datei( zond, &iter_anchor, anchor_id, child, &error );
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
    gint node_id = 0;
    GError* error = NULL;
    gint rc = 0;

    Projekt* zond = (Projekt*) data;

    //node_id herausfinden - wenn kein Link-Head->raus
    if ( zond_tree_store_is_link( iter ) )
    {
        gint head_nr = 0;

        head_nr = zond_tree_store_get_link_head_nr( iter );

        if ( head_nr == 0 ) return 0;
        else node_id = head_nr;
        //Prüfung auf Link von BAUM_AUSWERTUNG_COPY nicht erforderlich
        //nur Linkziel kann Ziel von Copy sein
        //Ebenso ob Link auf PDF-Abschnitt zeigt; nur Link wird entfernt
    }
    else
    {
        gint rc = 0;
        gboolean response = FALSE;
        gint baum_auswertung_copy = 0;
        gint baum_inhalt_file = 0;
        gint type = 0;
        gint link = 0;

        gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(tree_view) ), iter, 2, &node_id, -1 );

        rc = zond_dbase_get_type_and_link( zond->dbase_zond->zond_dbase_work,
                node_id, &type, &link, &error );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
            g_error_free( error );

            return -1;
        }

        rc = zond_dbase_get_baum_auswertung_copy( zond->dbase_zond->zond_dbase_work,
                node_id, &baum_auswertung_copy, &error );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
            g_error_free( error );

            return -1;
        }

        if ( baum_auswertung_copy ) return 0;

        //Prüfen, ob mitzulöschendes Kind von PDF_ABSCHNITT als BAUM_AUSWERTUNG_COPY angebunden ist
        if ( type == ZOND_DBASE_TYPE_FILE_PART )
        {
            gint rc = 0;
            gboolean copied = FALSE;

            rc = zond_dbase_is_file_part_copied( zond->dbase_zond->zond_dbase_work,
                    node_id, &copied, &error );
            if ( rc )
            {
                if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
                g_error_free( error );

                return -1;
            }

            if ( copied ) return 0;
        }

        if ( node_id == zond->node_id_extra )
                g_signal_emit_by_name( zond->textview_window,
                "delete-event", zond, &response );

        //Wenn node_id ein pdf_abschnitt ist, der über baum_inhalt_file angebunden ist,
        //dann soll letzterer gelöscht werden, also die Anbindung im Baum_inhalt
        rc = zond_dbase_get_baum_inhalt_file_from_file_part( zond->dbase_zond->zond_dbase_work,
                node_id, &baum_inhalt_file, &error );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
            g_error_free( error );

            return -1;
        }

        if ( baum_inhalt_file ) node_id = baum_inhalt_file;

        //ToDo: wenn Pdf-Abschnitt gelöscht wird - in ZondTreeviewFM umsetzen
        if ( !baum_inhalt_file )
        {

        }
    }

    rc = zond_dbase_remove_node( zond->dbase_zond->zond_dbase_work,
            node_id, &error );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strdup( error->message );
        g_error_free( error );

        return -1;
    }

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
zond_treeview_selection_entfernen_anbindung_foreach( SondTreeview* stv,
        GtkTreeIter* iter, gpointer data, gchar** errmsg )
{
    gint rc = 0;
    gint node_id = 0;
    gint type = 0;
    GError* error = NULL;
    gint id_anchor = 0;
    gint baum_inhalt_file = 0;
    gint id_parent = 0;
    gint (*fp_relative) (ZondDBase*, gint, gint*, GError**) = zond_dbase_get_first_child;
    gint relative = 0;
    gchar* file_part = NULL;
    gchar* section = NULL;
    GtkTreeIter iter_fm = { 0 };
    gboolean visible = FALSE;
    gboolean children = FALSE;
    gboolean opened = FALSE;

    Projekt* zond = data;
    ZondTreeviewFM* ztvfm = ZOND_TREEVIEWFM(zond->treeview[BAUM_FS]);

    if ( zond_tree_store_is_link( iter ) ) return 0;

    gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(stv) ), iter,
            2, &node_id, -1 );

    rc = zond_dbase_get_node( zond->dbase_zond->zond_dbase_work,
            node_id, &type, NULL, &file_part, &section, NULL, NULL, NULL, &error );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
        g_error_free( error );

        return -1;
    }

    if ( section ) //wenn nicht, muß baum_inhalt_file != 0 sein, d.h. es wird im fm nix gelöscht
    {
        gint rc = 0;

        rc = zond_treeviewfm_section_visible( ZOND_TREEVIEWFM(zond->treeview[BAUM_FS]),
                file_part, section, FALSE, &visible, &iter_fm, &children, &opened, &error );
        g_free( file_part );
        g_free( section );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
            g_error_free( error );

            return -1;
        }

        if ( !visible )
        {
            //parent brauchen wir ggf. um zu ermitteln, ob nach der Löschung parent noch Kinder hat
            rc = zond_dbase_get_parent( zond->dbase_zond->zond_dbase_work, node_id, &id_parent, &error );
            if ( rc )
            {
                if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
                g_error_free( error );

                return -1;
            }
        }
    }
    else g_free( file_part );

    if ( type != ZOND_DBASE_TYPE_BAUM_STRUKT )
    {
        gint rc = 0;
        gint baum_auswertung_copy = 0;

        //Test, ob als Copy angebunden
        rc = zond_dbase_get_baum_auswertung_copy( zond->dbase_zond->zond_dbase_work,
                node_id, &baum_auswertung_copy, &error );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
            g_error_free( error );

            return -1;
        }

        //dann nicht löschen
        if ( baum_auswertung_copy ) return 0;

        //Test, ob direkt angebunden
        rc = zond_dbase_get_baum_inhalt_file_from_file_part( zond->dbase_zond->zond_dbase_work,
                node_id, &baum_inhalt_file, &error );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
            g_error_free( error );

            return -1;
        }
    }

    rc = zond_dbase_begin( zond->dbase_zond->zond_dbase_work, &error );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
        g_error_free( error );

        return -1;
    }

    if ( baum_inhalt_file ) id_anchor = baum_inhalt_file;
    else id_anchor = node_id;

    do
    {
        gint rc = 0;

        rc = fp_relative( zond->dbase_zond->zond_dbase_work, node_id, &relative, &error );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strdup( error->message );
            g_error_free( error );

            ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )
        }

        if ( relative == 0 ) break;

        if ( baum_inhalt_file )
        {
            gint node_inserted = 0;

            node_inserted = zond_dbase_insert_node( zond->dbase_zond->zond_dbase_work, id_anchor, FALSE,
                    ZOND_DBASE_TYPE_BAUM_INHALT_FILE, relative, NULL, NULL, NULL, NULL, NULL, &error );
            if ( node_inserted == -1 )
            {
                if ( errmsg ) *errmsg = g_strdup( error->message );
                g_error_free( error );

                ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )
            }

            //wenn baum_inhalt_file, dann werden die Kinder von node_id nicht verschoben
            //dann müssen ab dem zweiten Durchgang die jüngeren Geschwister durchgegangen werden
            fp_relative = zond_dbase_get_younger_sibling;
            //und jüngeres Geschwister des ersten Kindes
            node_id = relative;

            id_anchor = node_inserted;
        }
        else
        {
            //kind verschieben
            rc = zond_dbase_verschieben_knoten( zond->dbase_zond->zond_dbase_work,
                    relative, id_anchor, FALSE, &error );
            if ( rc )
            {
                if ( errmsg ) *errmsg = g_strdup( error->message );
                g_error_free( error );

                ERROR_ROLLBACK( zond->dbase_zond->zond_dbase_work )
            }
            id_anchor = relative;
        }
    } while ( 1 );

    rc = zond_dbase_remove_node( zond->dbase_zond->zond_dbase_work,
            (baum_inhalt_file) ? baum_inhalt_file : node_id, &error );
    if ( rc )
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

    //Im Baum jedenfalls Punkt löschen und Kinder an die Stelle setzen
    zond_tree_store_kill_parent( iter );

    //In treeviewfm umsetzen
    if ( visible && !baum_inhalt_file ) //wenn baum_inhalt_file wird keine section gelöscht
    {
        gint rc = 0;
        gint first_child = 0;

        rc = zond_dbase_get_first_child( zond->dbase_zond->zond_dbase_work, id_parent, &first_child, &error );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
            g_error_free( error );

            return -1;
        }
        //1. einziges Kind seiner Eltern
            //1.1 selbst Kinder
                //1.1.1 selbst geöffnet: Kinder neue Kinder des Eltern-Knotens
                //1.1.2 selbst nicht expanded: Eltern-Knoten wird geschlossen und bekommt dummy als Kind
            //1.2 selbst keine Kinder: nix, nur Knoten löschen; Eltern werden automatisch geschlossen
        //2. Geschwister vorhanden
            //2.1 selbst Kinder
                //2.1.1 selbst geöffnet: Kinder neue Geschwister des nächstältesten Onkels
                //2.1.2 stlbst nicht expanded: s. 2.1.1
            //2.2 selbst keine Kinder: nix, nur Knoten löschen
        if ( first_child == 0 ) //in db gelöscht, also keins mehr übrig
        {
            if ( children )
            {
                if ( opened ) zond_treeviewfm_kill_parent( ztvfm, &iter_fm );
                else //Knoten schließen, dann wird er gelöscht und dummy eingefügt
                {
                    GtkTreePath* path = NULL;

                    path = gtk_tree_model_get_path( gtk_tree_view_get_model( GTK_TREE_VIEW(ztvfm) ), &iter_fm );
                    gtk_tree_view_collapse_row( GTK_TREE_VIEW(ztvfm), path );

                    gtk_tree_path_free( path );
                }
            }
            else gtk_tree_store_remove( GTK_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(ztvfm) )), &iter_fm );
            //mal gucken, ob nicht im callback dummy eingefügt wird...
            //der muß natürlich weg, denn es gibt ja keinen Abkömmling des Eltern-Knotens mehr
        }
        else //Knoten hat Geschwister
        {
            if ( children )
            {
                if ( opened ) zond_treeviewfm_kill_parent( ztvfm, &iter_fm );
                else //dann müssen die Kinder von Hand eingefügt werden, ggf. mit dummy
                {
                    gint rc = 0;
                    GtkTreeIter iter_parent = { 0 };
                    GtkTreeIter iter_child = { 0 };
                    GObject* object = NULL;
                    gint node_id_parent = 0;
                    gint node_id_dbase = 0;

                    //parent_iter ermitteln
                    if ( !gtk_tree_model_iter_parent( gtk_tree_view_get_model( GTK_TREE_VIEW(ztvfm) ), &iter_parent, &iter_fm ) )
                    {
                        if ( errmsg ) *errmsg = g_strdup_printf( "%s\niter hat keine Eltern", __func__ );

                        return -1;
                    }

                    if ( !gtk_tree_model_iter_children( gtk_tree_view_get_model( GTK_TREE_VIEW(ztvfm) ), &iter_child, &iter_parent ) )
                    {
                        if ( errmsg ) *errmsg = g_strdup_printf("%s\nparent-iter hat keine Kinder", __func__ );

                        return -1;
                    }

                    gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(ztvfm) ), &iter_parent,
                            0, &object, -1 );

                    if ( !object )
                    {
                        if ( errmsg ) *errmsg = g_strdup_printf( "%s\nKnoten enthält nichts", __func__ );

                        return -1;
                    }
                    else if ( !ZOND_IS_PDF_ABSCHNITT(object) )
                    {
                        if ( errmsg ) *errmsg = g_strdup_printf( "%s\nKnoten enthält keinen ZPA", __func__ );
                        g_object_unref( object );

                        return -1;
                    }

                    node_id_parent = zond_pdf_abschnitt_get_ID( ZOND_PDF_ABSCHNITT(object) );
                    g_object_unref( object );

                    rc = zond_dbase_get_first_child( zond->dbase_zond->zond_dbase_work, node_id_parent, &node_id_dbase, &error );
                    if ( rc )
                    {
                        if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
                        g_error_free( error );

                        return -1;
                    }

                    do
                    {
                        gint node_id_child = 0;
                        gint rc = 0;

                        rc = zond_treeviewfm_get_id_pda( ztvfm, &iter_child, &node_id_child, &error );
                        {
                            if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
                            g_error_free( error );

                            return -1;
                        }

                        if ( node_id_child != node_id_dbase ) //dann sind in dbase die Kinder von iter_fm!
                        {
                            gint stop = 0;
                            GtkTreeIter iter_stop = { 0 };

                            //nächsten iter, der Ende einfügen bestimmt
                            iter_stop = iter_child;
                            if ( gtk_tree_model_iter_next( gtk_tree_view_get_model( GTK_TREE_VIEW(ztvfm) ), &iter_stop ) )
                            {
                                gint rc = 0;

                                rc = zond_treeviewfm_get_id_pda( ztvfm, &iter_stop, &stop, &error );
                                if ( rc )
                                {
                                    if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
                                    g_error_free( error );

                                    return -1;
                                }
                            }
                            else stop = -1;

                            //muß neue Schleife, wo die eingefügt werden
                            do
                            {
                                gint rc = 0;
                                gint younger_sibling = 0;

                                //node_id_dbase einfügen, unter letztem iter_child
                                rc = zond_treeviewfm_insert_section( ztvfm, node_id_dbase, &iter_child,
                                        FALSE, NULL, &error );
                                if ( rc )
                                {
                                    if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__ , error->message );
                                    g_error_free( error );

                                    return -1;
                                }

                                rc = zond_dbase_get_younger_sibling( zond->dbase_zond->zond_dbase_work,
                                        node_id_dbase, &younger_sibling, &error );
                                if ( rc )
                                {
                                    if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__ , error->message );
                                    g_error_free( error );

                                    return -1;
                                }

                                node_id_dbase = younger_sibling;
                            } while ( node_id_dbase != 0 && node_id_dbase != stop );

                            //dann iter_fm löschen
                            gtk_tree_store_remove( GTK_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(ztvfm) )), &iter_fm );

                            //dann kann abgeborchen werden
                            break;
                        }

                        rc = zond_dbase_get_younger_sibling( zond->dbase_zond->zond_dbase_work, node_id_child, &node_id_dbase, &error );
                        if ( rc )
                        {
                            if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__ , error->message );
                            g_error_free( error );

                            return -1;
                        }
                    } while ( gtk_tree_model_iter_next( gtk_tree_view_get_model( GTK_TREE_VIEW(ztvfm) ), &iter_child ) );
                }
            }
            else gtk_tree_store_remove( GTK_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(ztvfm) )), &iter_fm );
        }
    }
    else if ( !baum_inhalt_file )
    {
        gint child = 0;
        gint rc = 0;

        //hat parent noch child?
        rc = zond_dbase_get_first_child( zond->dbase_zond->zond_dbase_work, id_parent, &child, &error );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
            g_error_free( error );

            return -1;
        }

        //falls nein: dummy löschen
        if ( child == 0 )
        {
            gint rc = 0;
            gboolean visible_parent = FALSE;
            GtkTreeIter iter_parent = { 0 };
            GtkTreeIter iter_child = { 0 };
            gint type = 0;
            gchar* file_part = NULL;
            gchar* section = NULL;

            rc = zond_dbase_get_node( zond->dbase_zond->zond_dbase_work, id_parent, &type, NULL, &file_part,
                    &section, NULL, NULL, NULL, &error );
            if ( rc )
            {
                if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
                g_error_free( error );

                return -1;
            }

            if ( type != ZOND_DBASE_TYPE_FILE_PART )
            {
                if ( errmsg ) *errmsg = g_strdup_printf( "%s\nUngültiger Knotentyp", __func__ );
                g_error_free( error );
                g_free( file_part );
                g_free( section );

                return -1;
            }

            rc = zond_treeviewfm_section_visible( ztvfm, file_part, section, FALSE,
                    &visible_parent, &iter_parent, NULL, NULL, &error );
            g_free( file_part );
            g_free( section );
            if ( rc )
            {
                if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
                g_error_free( error );

                return -1;
            }
            if ( visible_parent )
            {
                if ( !gtk_tree_model_iter_children( gtk_tree_view_get_model( GTK_TREE_VIEW(ztvfm) ), &iter_child, &iter_parent ) )
                {
                    if ( errmsg ) *errmsg = g_strdup_printf( "%s\nKnoten hat keinen Abkömmling, obwohl er müßte", __func__ );

                    return -1;
                }

                gtk_tree_store_remove( GTK_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(ztvfm) )), &iter_child );
            }
        }
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
zond_treeview_jump_to_node_id( Projekt* zond, ZondTreeview* ztv, gint node_id, GError** error )
{
    GtkTreeIter* iter = NULL;

    iter = zond_treeview_abfragen_iter( ztv, node_id );
    if ( !iter )
    {
        if ( error ) *error = g_error_new( ZOND_ERROR, 0, "%s\nzond_treeview_abfragen_iter gibt NULL zurück", __func__ );

        return -1;
    }

    zond_treeview_jump_to_iter( zond, iter );
    gtk_tree_iter_free( iter );

    return 0;
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

    if ( type == ZOND_DBASE_TYPE_BAUM_STRUKT ) return 0;
    else if ( type == ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_COPY )
    {
        gint rc = 0;

        rc = zond_treeview_jump_to_node_id( ztv_priv->zond, ztv, node_id, error );
        if ( rc ) ERROR_Z
    }
    else //FILE_PART
    {
        gint rc = 0;
        gchar* file_part = NULL;
        gchar* section = NULL;

        rc = zond_dbase_get_node( ztv_priv->zond->dbase_zond->zond_dbase_work,
                node_id, NULL, NULL, &file_part, &section, NULL, NULL, NULL, error );
        if ( rc ) ERROR_Z

        if ( g_str_has_prefix( file_part, "//" ) ) //Auszug; hat keinen origin im fs_tree!
        {
            g_free( file_part );
            g_free( section );

            return 0;
        }
        else
        {
            gint rc = 0;

            //wenn FS nicht angezeigt: erst einschalten, damit man was sieht
            if ( !gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(ztv_priv->zond->fs_button) ) )
                    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(ztv_priv->zond->fs_button), TRUE );

            rc = zond_treeviewfm_set_cursor_on_section( ZOND_TREEVIEWFM(ztv_priv->zond->treeview[BAUM_FS]),
                    file_part, section, error );
            g_free( file_part );
            g_free( section );
            if ( rc ) ERROR_Z
        }
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


static gint
zond_treeview_open_node( Projekt* zond, GtkTreeIter* iter, gboolean open_with, GError** error )
{
    gint rc = 0;
    gchar* file_part = NULL;
    gchar* section = NULL;
    Anbindung anbindung = { 0 };
    Anbindung anbindung_ges = { 0 };
    PdfPos pos_pdf = { 0 };
    gint node_id = 0;
    gint type = 0;
    gint link = 0;
    Anbindung* anbindung_int = NULL;
    gchar* errmsg = NULL;

    gtk_tree_model_get( GTK_TREE_MODEL(zond_tree_store_get_tree_store( iter ) ), iter, 2, &node_id, -1 );

    rc = zond_dbase_get_node( zond->dbase_zond->zond_dbase_work,
            node_id, &type, &link, &file_part, &section, NULL, NULL, NULL, error );
    if ( rc ) ERROR_Z

    if ( type == ZOND_DBASE_TYPE_BAUM_STRUKT ) return 0;
    else if ( type == ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_COPY )
    {
        gint rc = 0;

        //g_free( file_part ) überflüssig - wird in AUSWERTUNG_COPY nicht gespeichtert
        //Neu für link abfragen - da ist ja die Info
        node_id = link;
        rc = zond_dbase_get_node( zond->dbase_zond->zond_dbase_work,
                node_id, NULL, NULL, &file_part, &section, NULL, NULL, NULL, error );
        if ( rc ) ERROR_Z
    }

    //mit externem Programm öffnen
    if ( open_with || !is_pdf( file_part ) ) //wenn kein pdf oder mit Programmauswahl zu öffnen:
    {
        gint rc = 0;
        gchar* errmsg = NULL;

        g_free( section );

        rc = misc_datei_oeffnen( file_part, open_with, &errmsg );
        g_free( file_part );
        if ( rc )
        {
            if ( error ) *error = g_error_new( ZOND_ERROR, 0, "%s\n%s", __func__, errmsg );
            g_free( errmsg );

            return -1;
        }

        return 0;
    }

    g_free( file_part );

    //Dann: Pdf
    if ( section )
    {
        anbindung_parse_file_section( section, &anbindung );
        g_free( section );
    }

    if ( zond->state & GDK_CONTROL_MASK )
    {
        if ( (anbindung.bis.seite || anbindung.bis.index) )
                anbindung_int = &anbindung;
        else if ( anbindung.von.seite || anbindung.von.index ) //Pdf_punkt
        { //nächsthöheren Abschnitt ermitteln
            gint rc = 0;
            gint parent_id = 0;
            gchar* section_ges = NULL;

            rc = zond_dbase_get_parent( zond->dbase_zond->zond_dbase_work,
                    node_id, &parent_id, error );
            if ( rc ) ERROR_Z

            rc = zond_dbase_get_node( zond->dbase_zond->zond_dbase_work, parent_id,
                    NULL, NULL, NULL, &section_ges, NULL, NULL, NULL, error );
            if ( rc ) ERROR_Z

            if ( section ) //nicht root
            {
                anbindung_parse_file_section( section_ges, &anbindung_ges );
                g_free( section_ges );

                anbindung_int = &anbindung_ges;
            }
        }
    }
    else
    {
        //ermitteln, woran pdf_abschnitt angeknüpft ist
        //um Umfang des zu öffnenden PDF festzustellen
        gint rc = 0;
        gint file_part_id = 0;
        gchar* section_ges = NULL;

        rc = zond_dbase_find_baum_inhalt_file( zond->dbase_zond->zond_dbase_work,
                node_id, NULL, &file_part_id, NULL, error );
        if ( rc ) ERROR_Z

        rc = zond_dbase_get_node( zond->dbase_zond->zond_dbase_work,
                file_part_id, NULL, NULL, NULL, &section_ges, NULL, NULL, NULL, error );
        if ( rc ) ERROR_Z

        if ( section_ges )
        {
            anbindung_parse_file_section( section_ges, &anbindung_ges );
            g_free( section_ges );
            anbindung_int = &anbindung_ges;
        }
    }

    //jetzt Anfangspunkt
    if ( (anbindung.von.seite || anbindung.von.index) && // Pdf-Punkt
            !(anbindung.bis.seite || anbindung.bis.index) )
    {
        pos_pdf.seite = anbindung.von.seite - ((anbindung_int) ? anbindung_int->von.seite : 0);
        pos_pdf.index = anbindung.von.index;
    }
    else if ( zond->state & GDK_MOD1_MASK )
    {
        if ( anbindung.bis.seite || anbindung.bis.index ) //Abschnitt
        {
            pos_pdf.seite = anbindung.bis.seite - ((anbindung_int) ? anbindung_int->von.seite : 0);
            pos_pdf.index = anbindung.bis.index;
        }
        else //root
        {
            pos_pdf.seite = EOP;
            pos_pdf.index = EOP;
        }
    }
    else
    {
        if ( anbindung.von.seite || anbindung.von.index )
        {
            pos_pdf.seite = anbindung.von.seite - ((anbindung_int) ? anbindung_int->von.seite : 0);
            pos_pdf.index = anbindung.von.index;
        }
        //else: bleibt 0
    }

    //jetzt erneut file_part abfragen; sehr unschön, aber sonst müßte bei jedem Error g_free( file_part ) gemacht werden
    //ToDo: Unterfunktion erzeugen, der file_part übergeben wird
    rc = zond_dbase_get_node( zond->dbase_zond->zond_dbase_work,
            node_id, NULL, NULL, &file_part, NULL, NULL, NULL, NULL, error );
    if ( rc ) ERROR_Z

    rc = oeffnen_internal_viewer( zond, file_part, anbindung_int, &pos_pdf, &errmsg );
    g_free( file_part );
    if ( rc )
    {
        if ( error ) *error = g_error_new( ZOND_ERROR, 0, "%s\n%s", __func__, errmsg );
        g_free( errmsg );

        return -1;
    }

    return 0;
}


static gint
zond_treeview_open_path( Projekt* zond, GtkTreeView* tree_view, GtkTreePath* tree_path,
        gboolean open_with, GError** error )
{
    gint rc = 0;
    GtkTreeIter iter = { 0 };

    gtk_tree_model_get_iter( gtk_tree_view_get_model( tree_view ), &iter, tree_path );

    rc = zond_treeview_open_node( zond, &iter, open_with, error );
    if ( rc ) ERROR_Z

    return 0;
}


static void
zond_treeview_row_activated( GtkWidget* ztv, GtkTreePath* tp, GtkTreeViewColumn* tvc,
        gpointer user_data )
{
    gint rc = 0;
    GError* error = NULL;

    Projekt* zond = (Projekt*) user_data;

    rc = zond_treeview_open_path( zond, GTK_TREE_VIEW(ztv), tp, FALSE, &error );
    if ( rc )
    {
        display_message( zond->app_window, "Fehler - in ", __func__, "\n\n", error->message, NULL );
        g_error_free( error );
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
    GError* error = NULL;

    Projekt* zond = (Projekt*) user_data;

    gtk_tree_view_get_cursor( GTK_TREE_VIEW(zond->treeview[zond->baum_active]), &path, NULL );

    rc = zond_treeview_open_path( zond, GTK_TREE_VIEW(zond->treeview[zond->baum_active]), path, TRUE, &error);
    gtk_tree_path_free( path );
    if ( rc )
    {
        display_message( zond->app_window, "Fehler beim Öffnen Knoten:\n\n", error->message, NULL );
        g_error_free( error );
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

    //ToDo: wenn im treeviewfm angezeigt, auch ändern

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


static gint
zond_treeview_load_node( ZondTreeview* ztv, gint node_id, GtkTreeIter* iter_anchor,
        gboolean child, GtkTreeIter* iter_inserted, gint anchor_id,
        gint* node_id_inserted, GError** error )
{
    gint type = 0;
    gint link = 0;
    gint rc = 0;
    GtkTreeIter iter_new = { 0 };

    ZondTreeviewPrivate* ztv_priv = zond_treeview_get_instance_private( ztv );

    rc = zond_dbase_get_type_and_link( ztv_priv->zond->dbase_zond->zond_dbase_work,
            node_id, &type, &link, error );
    if ( rc ) ERROR_Z

    if ( type == ZOND_DBASE_TYPE_BAUM_INHALT_FILE )
    {
        gint rc = 0;

        rc = zond_treeview_walk_tree( ztv, FALSE, link, iter_anchor, child, &iter_new,
                node_id, NULL, zond_treeview_insert_file_parts, error );
        if ( rc ) ERROR_Z
    }
    else //eigentlich nur link, copy oder strukt...
    {
        gchar* icon_name = NULL;
        gchar* node_text = NULL;

        zond_tree_store_insert( ZOND_TREE_STORE(gtk_tree_view_get_model(
                GTK_TREE_VIEW(ztv) )), iter_anchor, child, &iter_new );

        if ( type == ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_LINK )
        {
            icon_name = g_strdup_printf( "%d", node_id ); //head_nr wird hier gespeichert
            node_id = link * -1;
        }
        else
        {
            gint rc = 0;

            rc = zond_dbase_get_node( ztv_priv->zond->dbase_zond->zond_dbase_work,
                node_id, NULL, NULL, NULL, NULL, &icon_name, &node_text, NULL, error );
            if ( rc ) ERROR_Z
        }

        zond_tree_store_set( &iter_new, icon_name, node_text, node_id );

        g_free( icon_name );
        g_free( node_text );
    }

    if ( iter_inserted ) *iter_inserted = iter_new;

    return 0;
}


static gint
zond_treeview_insert_links_foreach( ZondTreeview* ztv, GtkTreeIter* iter,
        GError** error )
{
    gchar* icon_name = NULL;
    gint node_id = 0;
    gint head_nr = 0;

    ZondTreeviewPrivate* ztv_priv = zond_treeview_get_instance_private( ztv );

    if ( iter ) gtk_tree_model_get( gtk_tree_view_get_model( GTK_TREE_VIEW(ztv) ),
            iter, 0, &icon_name, 2, &node_id, -1 );

    head_nr = atoi( icon_name );
    g_free( icon_name );

    if ( node_id >= 0 )
    {
        GtkTreeIter iter_child = { 0 };

        if ( gtk_tree_model_iter_children( gtk_tree_view_get_model( GTK_TREE_VIEW(ztv) ),
                &iter_child, iter ) )
        {
            gint rc = 0;

            rc = zond_treeview_insert_links_foreach( ztv, &iter_child, error );
            if ( rc ) ERROR_Z
        }
    }
    else
    {
        GtkTreeIter iter_anchor = { 0 };
        gboolean child = FALSE;
        GtkTreeIter* iter_target = NULL;
        gint root = 0;
        gint rc = 0;

        node_id *= -1;

        rc = zond_treeview_get_root( ztv, node_id, &root, error );
        if ( rc ) ERROR_Z

        //iter_anchor basteln
        iter_anchor = *iter; //Abfrage, ob iter != NULL überflüssig, da dann node_id niemals negativ ist
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
        iter_target = zond_treeview_abfragen_iter( ZOND_TREEVIEW(ztv_priv->zond->treeview[root]), node_id );
        if ( !iter_target )
        {
            if ( error ) *error = g_error_new( ZOND_ERROR, 0, "%s\nKein Iter ermittelt", __func__ );

            return -1;
        }

        zond_tree_store_insert_link( iter_target, head_nr,
                zond_tree_store_get_tree_store( &iter_anchor ),
                ((GNode*) (iter_anchor.user_data) == zond_tree_store_get_root_node( zond_tree_store_get_tree_store( &iter_anchor ) )) ? NULL :
                 &iter_anchor, child, iter ); //*iter existiert, ist aber bis hierhin nutzlos

        gtk_tree_iter_free( iter_target );
    }

    if ( iter && gtk_tree_model_iter_next( gtk_tree_view_get_model( GTK_TREE_VIEW(ztv) ),
            iter ) )
    {
        gint rc = 0;

        rc = zond_treeview_insert_links_foreach( ztv, iter, error );
        if ( rc ) ERROR_Z
    }

    return 0;
}


gint
zond_treeview_load_baum( ZondTreeview* ztv, GError** error )
{
    gint first_child = 0;
    gint rc = 0;
    gint root = 0;

    ZondTreeviewPrivate* ztv_priv = zond_treeview_get_instance_private( ztv );

    zond_tree_store_clear( ZOND_TREE_STORE(gtk_tree_view_get_model(
            GTK_TREE_VIEW(ztv) )) );

    root = zond_tree_store_get_root( ZOND_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(ztv) )) );
    rc = zond_dbase_get_first_child( ztv_priv->zond->dbase_zond->zond_dbase_work, root,
            &first_child, error );
    if ( rc ) ERROR_Z
    else if ( first_child == 0 ) return 0; //Baum leer

    rc = zond_treeview_walk_tree( ztv, TRUE, first_child, NULL, TRUE, NULL,
            root, NULL, zond_treeview_load_node, error );
    if ( rc ) ERROR_Z

    rc = zond_treeview_insert_links_foreach( ztv, NULL, error );
    if ( rc ) ERROR_Z

    return 0;
}

typedef struct
{
    gint ID;
    gchar const* text_new;
} Foreach;

static gboolean
zond_treeview_foreach_pdf_abschnitt( GtkTreeModel* model, GtkTreePath* path,
        GtkTreeIter* iter, gpointer data )
{
    gint node_id = 0;

    Foreach* foreach = (Foreach*) data;

    gtk_tree_model_get( model, iter, 2, &node_id, -1 );

    if ( node_id == foreach->ID )
    {
        zond_tree_store_set( iter, NULL, foreach->text_new, 0 );

        return TRUE;
    }

    return FALSE;
}


void
zond_treeview_set_text_pdf_abschnitt( ZondTreeview* ztv, gint ID_pdf_abschnitt, gchar const* text_new )
{
    Foreach foreach = { ID_pdf_abschnitt, text_new };

    gtk_tree_model_foreach( gtk_tree_view_get_model( GTK_TREE_VIEW(ztv) ), zond_treeview_foreach_pdf_abschnitt, &foreach );

    return;
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


