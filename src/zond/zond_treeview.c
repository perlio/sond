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
#include "10init/headerbar.h"

#include "20allgemein/oeffnen.h"
#include "20allgemein/project.h"
#include "20allgemein/treeviews.h"
#include "20allgemein/selection.h"


typedef struct
{
    Projekt* zond;
} ZondTreeviewPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(ZondTreeview, zond_treeview, SOND_TYPE_TREEVIEW)


void
zond_treeview_cursor_changed( ZondTreeview* treeview, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    gint node_id = 0;
    GtkTreeIter iter = { 0, };
    Baum baum_target = KEIN_BAUM;
    gchar* rel_path = NULL;
    Anbindung* anbindung = NULL;
    gchar* text_label = NULL;
    gchar* text = NULL;

    Projekt* zond = (Projekt*) user_data;

    //wenn kein cursor gesetzt ist
    //also letzter Punkt gelöscht wird
    if ( !sond_treeview_get_cursor( SOND_TREEVIEW(treeview), &iter ) )
    {
        gtk_label_set_text( zond->label_status, "" ); //status-label leeren
        //textview deaktivieren - egal welcher baum
        gtk_widget_set_sensitive( GTK_WIDGET(zond->textview), FALSE );
        //wenn letzter Punkt in baum_auswertung gelöscht: textview leeren
        if ( SOND_TREEVIEW(treeview) == zond->treeview[BAUM_AUSWERTUNG] )
        {
            gboolean ret = FALSE;

            gtk_text_buffer_set_text( gtk_text_view_get_buffer( GTK_TEXT_VIEW(zond->textview) ),
                    "", -1 );
            gtk_text_buffer_set_text( gtk_text_view_get_buffer( GTK_TEXT_VIEW(zond->textview_ii) ),
                    "", -1 );

            //Text-Fenster verstecken (falls nicht schn ist, Überprüfung aber überflüssig
            g_signal_emit_by_name( zond->textview_window, "delete-event", zond, &ret );

            //Vorsichtshalber auch Menüpunkt deaktivieren
            gtk_widget_set_sensitive( zond->menu.textview_extra, FALSE );

            zond->node_id_act = 0;
            zond->node_id_extra = 0;
        }

        return;
    }
    else treeviews_get_baum_and_node_id( zond, &iter, &baum_target, &node_id );

    //status_label setzen
    rc = treeviews_get_rel_path_and_anbindung( zond, baum_target, node_id, &rel_path,
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

    if ( baum_target == BAUM_INHALT || rc == -1 )
    {
        gtk_widget_set_sensitive( zond->textview, FALSE );
        gtk_widget_set_sensitive( zond->menu.textview_extra, FALSE );

        //marker setzen, wenn Knoten in baum_inhalt aktiviert wird
        //node_id_act negativ
        if ( zond->node_id_act > 0 ) zond->node_id_act *= -1;

        return;
    }
    //else if ( baum_target == BAUM_AUSWERTUNG ) - BAUM_FS löst diesen cb nicht aus

    //wenn von baum_inhalt in baum_auswertung gesprungen wird:
    if ( zond->node_id_act <= 0 )
    {
        //textview aktivieren je nach baum
        gtk_widget_set_sensitive( zond->textview, TRUE );

        //Wenn gesondertes Textfenster nicht geöffnet ist: Menüpunkt aktivieren
        if ( !(zond->node_id_extra) )
                gtk_widget_set_sensitive( zond->menu.textview_extra, TRUE );

        zond->node_id_act *= -1;
    }

    //Wenn gleicher Knoten: direkt zurück
    if ( node_id == zond->node_id_act ) return;

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
zond_treeview_row_activated( GtkWidget* ztv, GtkTreePath* tp, GtkTreeViewColumn* tvc,
        gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    GtkTreeIter iter = { 0, };

    Projekt* zond = (Projekt*) user_data;

    if ( !gtk_tree_model_get_iter( gtk_tree_view_get_model( GTK_TREE_VIEW(ztv) ),
            &iter, tp ) ) return;

    rc = oeffnen_node( zond, &iter, &errmsg );
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

        if ( zond_tree_store_get_link_head_nr( iter ) )
        {
            markuptxt = add_string( g_strdup( "<span weight=\"bold\">" ), markuptxt );
            markuptxt = add_string( markuptxt, g_strdup( "</span>" ) );
        }

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
    ZondTreeStore* tree_store = zond_tree_store_new( 0 );

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


void
zond_treeview_punkt_einfuegen_activate( GtkMenuItem* item, gpointer user_data )
{
    gint rc = 0;
    gboolean child = FALSE;
    gchar* errmsg = NULL;

    Projekt* zond = (Projekt*) user_data;

    child = (gboolean) GPOINTER_TO_INT(g_object_get_data( G_OBJECT(item), "kind" ));

    rc = treeviews_insert_node( zond, zond->baum_active, child, &errmsg );
    if ( rc == -1 )
    {
        display_message( zond->app_window, "Punkt einfügen fehlgeschlagen\n\n"
                "Bei Aufruf treeviews_insert_node:\n", errmsg, NULL );
        g_free( errmsg );
    }
    else if ( rc == 1 ) display_message( zond->app_window, "Punkt darf nicht "
            "in Bestandsverzeichnis eingefügt weden", NULL );

    return;
}


static void
zond_treeview_paste_activate( GtkMenuItem* item, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    gboolean child = FALSE;

    Projekt* zond = (Projekt*) user_data;

    child = (gboolean) GPOINTER_TO_INT(g_object_get_data( G_OBJECT(item), "kind" ));

    rc = three_treeviews_paste_clipboard( zond, child, FALSE, &errmsg );
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

    rc = three_treeviews_paste_clipboard( zond, child, TRUE, &errmsg );
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


void
item_loeschen_activate( GtkMenuItem* item, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    Projekt* zond = (Projekt*) user_data;

}


void
item_anbindung_entfernen_activate( GtkMenuItem* item, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    Projekt* zond = (Projekt*) user_data;

}


void
item_jump_activate( GtkMenuItem* item, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    Projekt* zond = (Projekt*) user_data;

}


static void
zond_treeview_datei_oeffnen_activate( GtkMenuItem* item, gpointer user_data )
{
    GtkTreePath* path = NULL;

    Projekt* zond = (Projekt*) user_data;

    gtk_tree_view_get_cursor( GTK_TREE_VIEW(zond->treeview[zond->baum_active]), &path, NULL );

    g_signal_emit_by_name( zond->treeview[zond->baum_active], "row-activated", path, NULL, zond );

    gtk_tree_path_free( path );

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
    g_object_set_data( G_OBJECT(sond_treeview_get_contextmenu( SOND_TREEVIEW(ztv) )),
            "item-paste-ge", item_paste_ge );
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_paste), item_paste_ge );
    g_signal_connect( G_OBJECT(item_paste_ge), "activate",
            G_CALLBACK(zond_treeview_paste_activate), (gpointer) zond );

    GtkWidget* item_paste_up = gtk_menu_item_new_with_label( "Unterebene");
    g_object_set_data( G_OBJECT(item_paste_up), "kind",
            GINT_TO_POINTER(1) );
    g_object_set_data( G_OBJECT(sond_treeview_get_contextmenu( SOND_TREEVIEW(ztv) )),
            "item-paste-up", item_paste_up );
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
    g_object_set_data( G_OBJECT(item_paste_as_link_ge), "link",
            GINT_TO_POINTER(1) );
    g_signal_connect( G_OBJECT(item_paste_as_link_ge), "activate",
            G_CALLBACK(zond_treeview_paste_as_link_activate), (gpointer) zond );
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_paste_as_link), item_paste_as_link_ge );

    GtkWidget* item_paste_as_link_up = gtk_menu_item_new_with_label(
            "Unterebene");
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
    GtkWidget* item_loeschen = gtk_menu_item_new_with_label("Punkte löschen");
    g_signal_connect( G_OBJECT(item_loeschen), "activate",
            G_CALLBACK(item_loeschen_activate), (gpointer) zond );
    gtk_menu_shell_append( GTK_MENU_SHELL(contextmenu), item_loeschen );

    //Speichern als Projektdatei
    GtkWidget* item_anbindung_entfernen = gtk_menu_item_new_with_label(
            "Anbindung entfernen");
    g_signal_connect( G_OBJECT(item_anbindung_entfernen), "activate",
            G_CALLBACK(item_anbindung_entfernen_activate), zond );
    gtk_menu_shell_append( GTK_MENU_SHELL(contextmenu), item_anbindung_entfernen );

    GtkWidget* item_jump = gtk_menu_item_new_with_label( "Zu Linkziel springen" );
    g_signal_connect( item_jump, "activate", G_CALLBACK(item_jump_activate), zond );
    gtk_menu_shell_append( GTK_MENU_SHELL(contextmenu), item_jump );

    GtkWidget* item_separator_2 = gtk_separator_menu_item_new();
    gtk_menu_shell_append( GTK_MENU_SHELL(contextmenu), item_separator_2 );

    //Datei Öffnen
    GtkWidget* item_datei_oeffnen = gtk_menu_item_new_with_label( "Öffnen" );
    g_object_set_data( G_OBJECT(sond_treeview_get_contextmenu( SOND_TREEVIEW(ztv) )),
            "item-datei-oeffnen", item_datei_oeffnen );
    g_signal_connect( item_datei_oeffnen, "activate",
                G_CALLBACK(zond_treeview_datei_oeffnen_activate), (gpointer) zond );
    gtk_menu_shell_append( GTK_MENU_SHELL(contextmenu), item_datei_oeffnen );

    gtk_widget_show_all( contextmenu );

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



