/*
zond (zond_database.c) - Akten, Beweisstücke, Unterlagen
Copyright (C) 2021  pelo america

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


#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <gtk/gtk.h>
#include <sqlite3.h>

#include "../misc.h"

#include "error.h"
#include "global_types.h"

#include "20allgemein/project.h"
#include "20allgemein/dbase_full.h"

#include "zond_database.h"


static GtkWidget*
zond_database_get_info_entity( DBaseFull* dbase_full, gint ID_entity, gchar** errmsg )
{
    gint rc = 0;
    GtkWidget* info = NULL;
    gchar* label_entity = NULL;
    GArray* arr_properties = NULL;
    gchar* markup = NULL;

    rc = dbase_full_get_label_entity( dbase_full, ID_entity, &label_entity, errmsg );
    if ( rc ) ERROR_PAO_R( "dbase_full_get_label_entity", NULL )

    rc = dbase_full_get_properties( dbase_full, ID_entity, &arr_properties, errmsg );
    if ( rc )
    {
        g_free( label_entity );
        ERROR_PAO_R( "dbase_full_get_properties", NULL )
    }

    markup = g_strdup_printf( "<big><b>%i %s</b></big>\n", ID_entity, label_entity );
    g_free( label_entity );

    if ( arr_properties->len > 0 )
    {
        markup = add_string( markup, g_strdup( "   Properties:\n" ) );

        for ( gint i = 0; i < arr_properties->len; i++ )
        {
            Property property = g_array_index( arr_properties, Property, i );
            markup = add_string( markup, g_strdup_printf( "     <small>%i  %s  %s\n</small>", property.ID, property.label, property.value ) );
        }
    }

    g_array_unref( arr_properties );

    info = gtk_label_new( NULL );
    gtk_label_set_markup( GTK_LABEL(info), markup );
    g_free( markup );

    return info;
}


static gint
zond_database_fill_listbox_with_existing_nodes( DBaseFull* dbase_full,
        GtkWidget* listbox, gint nomen, gchar** errmsg )
{
    gint rc = 0;
    GArray* arr_nodes = NULL;

    rc = dbase_full_get_array_nodes( dbase_full, nomen, &arr_nodes, errmsg );
    if ( rc ) ERROR_PAO( "dbase_full_get_array_nodes" )

    for ( gint i = 0; i < arr_nodes->len; i++ )
    {
        GtkWidget* label_node = NULL;

        label_node = zond_database_get_info_entity( dbase_full, g_array_index( arr_nodes, gint, i ), errmsg );
        if ( !label_node )
        {
            g_array_unref( arr_nodes );
            ERROR_PAO( "zond_database_get_info_entity" )
        }

        gtk_list_box_insert( GTK_LIST_BOX(listbox), label_node, -1 );
    }

    return 0;
}


static gint
zond_database_fill_listbox_with_outgoing_edges( DBaseFull* dbase_full,
        GtkWidget* listbox, gint ID_entity, gchar** errmsg)
{
    gint rc = 0;
    GArray* arr_edges = NULL;
    GtkWidget* button_new = NULL;

    rc = dbase_full_get_outgoing_edges( dbase_full, ID_entity, &arr_edges, errmsg );
    if ( rc ) ERROR_PAO( "dbase_full_get_outgoing_edges" )

    button_new = gtk_button_new_with_label( "Neue ausgehende\nVerbindung" );
    gtk_list_box_insert( GTK_LIST_BOX(listbox), button_new, -1 );

    for ( gint i = 0; i < arr_edges->len; i++ )
    {
        Edge edge = { 0 };
        GtkWidget* hbox = NULL;
        GtkWidget* label_edge = NULL;
        GtkWidget* label_object = NULL;
        gchar* text_edge = NULL;

        edge = g_array_index( arr_edges, Edge, i );

        hbox = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 0 );

        text_edge = g_strdup_printf( "ID: %i  Label: %s", edge.ID_edge, edge.label_edge );
        for (gint u = 0; u < edge.arr_properties->len; u++ )
        {
            Property property = g_array_index( edge.arr_properties, Property, u );

            text_edge = add_string( text_edge, g_strdup_printf( "\n%i  %s  %s",
                    property.ID, property.label, property.value ) );
        }
        label_edge = gtk_label_new( (const gchar*) text_edge );
        g_free( text_edge );
        gtk_box_pack_start( GTK_BOX(hbox), label_edge, FALSE, FALSE, 0 );

        label_object = zond_database_get_info_entity( dbase_full, edge.ID_object, errmsg );
        if ( !label_object )
        {
            gtk_widget_destroy( label_edge );
            g_array_unref( arr_edges );
            ERROR_PAO( "zond_database_get_info_entity" )
        }

        gtk_box_pack_start( GTK_BOX(hbox), label_object, FALSE, FALSE, 0 );

        gtk_list_box_insert( GTK_LIST_BOX(listbox), hbox, -1 );
    }

    g_array_unref( arr_edges );

    return 0;
}


static gint
zond_database_fill_listbox_with_incoming_edges( DBaseFull* dbase_full,
        GtkWidget* listbox, gint ID_entity, gchar** errmsg)
{
    gint rc = 0;
    GArray* arr_edges = NULL;

    rc = dbase_full_get_incoming_edges( dbase_full, ID_entity, &arr_edges, errmsg );
    if ( rc ) ERROR_PAO( "dbase_full_get_incoming_edges" )

    for ( gint i = 0; i < arr_edges->len; i++ )
    {
        Edge edge = { 0 };
        GtkWidget* hbox = NULL;
        GtkWidget* label_edge = NULL;
        GtkWidget* label_subject = NULL;
        gchar* text_edge = NULL;

        edge = g_array_index( arr_edges, Edge, i );

        hbox = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 0 );

        label_subject = zond_database_get_info_entity( dbase_full, edge.ID_subject, errmsg );
        if ( !label_subject )
        {
            gtk_widget_destroy( label_edge );
            g_array_unref( arr_edges );
            ERROR_PAO( "zond_database_get_info_entity" )
        }

        gtk_box_pack_start( GTK_BOX(hbox), label_subject, FALSE, FALSE, 0 );

        text_edge = g_strdup_printf( "ID: %i  Label: %s", edge.ID_edge, edge.label_edge );
        for (gint u = 0; u < edge.arr_properties->len; u++ )
        {
            Property property = g_array_index( edge.arr_properties, Property, u );

            text_edge = add_string( text_edge, g_strdup_printf( "\n%i  %s  %s",
                    property.ID, property.label, property.value ) );
        }
        label_edge = gtk_label_new( (const gchar*) text_edge );
        g_free( text_edge );
        gtk_box_pack_start( GTK_BOX(hbox), label_edge, FALSE, FALSE, 0 );

        gtk_list_box_insert( GTK_LIST_BOX(listbox), hbox, -1 );
    }

    g_array_unref( arr_edges );

    return 0;
}


static gint
zond_database_get_tree_labels( DBaseFull* dbase_full, gint label,
        GtkTreeIter* iter, GtkTreeStore* tree_store, gchar** errmsg )
{
    gint rc = 0;
    gchar* label_text = NULL;
    GtkTreeIter iter_new;
    GArray* arr_children;

    rc = dbase_full_get_label_text( dbase_full, label, &label_text, errmsg );
    if ( rc ) ERROR_PAO( "dbase_full_get_label_text" )

    //Füge label in tree_store
    gtk_tree_store_insert( tree_store, &iter_new, iter, -1 );
    gtk_tree_store_set( tree_store, &iter_new, 0, label, 1, label_text, -1 );

    //Ermittle array von Kindern von label
    rc = dbase_full_get_array_children_label( dbase_full, label, &arr_children, errmsg );
    if ( rc ) ERROR_PAO( "dbase_full_get_array_children_label" )

    //Schleife über alle Kinder:
    for ( gint i = 0; i < arr_children->len; i++ )
    {
        rc = zond_database_get_tree_labels( dbase_full,
                g_array_index( arr_children, gint, i ), &iter_new, tree_store, errmsg );
        if ( rc )
        {
            g_array_unref( arr_children );
            ERROR_PAO( "zond_database_get_tree_labels" )
        }
    }

    g_array_unref( arr_children );

    return 0;
}


static void
zond_database_listbox_foreach( GtkWidget* row, gpointer data )
{
    gtk_container_remove( (GtkContainer*) data, row );

    return;
}


static void
zond_database_cb_button_clicked( GtkButton* button, gpointer data )
{
    DBaseFull* dbase_full = (DBaseFull*) data;


    return;
}


static void
zond_database_cb_combo_changed( GtkWidget* combo_labels, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    GtkTreeIter iter = { 0 };
    GtkTreeModel* model = NULL;
    gint label = 0;

    GtkWidget* listbox = (GtkWidget*) data;
    DBaseFull* dbase_full = g_object_get_data( G_OBJECT(listbox), "dbase-full" );

    //neues label abfragen
    if ( !gtk_combo_box_get_active_iter( GTK_COMBO_BOX(combo_labels), &iter ) )
    {
        display_message( gtk_widget_get_toplevel( listbox ), "Fehler bei "
                "label-Auswahl -\n\ngtk_combo_box_get_active_iter:\n"
                "Konnte iter nicht setzen", NULL );
        return;
    }

    model = gtk_combo_box_get_model( GTK_COMBO_BOX(combo_labels) );
    gtk_tree_model_get( model, &iter, 0, &label, -1 );

    //listbox leeren
    gtk_container_foreach( GTK_CONTAINER(listbox), zond_database_listbox_foreach, listbox );

    //listbox mit neuem label füllen
    rc = zond_database_fill_listbox_with_existing_nodes( dbase_full, listbox,
            label, &errmsg );
    if ( rc )
    {
        display_message( gtk_widget_get_toplevel( listbox ), "Fehler bei "
                "label-Auswahl -\n\nzond_database_fill_listbox_with_existing_nodes:\n",
                errmsg, NULL );
        g_free( errmsg );
    }
    else gtk_widget_show_all( listbox );

    return;
}


static gint
zond_database_add_link( DBaseFull* dbase_full, GtkWidget* dialog,
        gint ID_entity, gint praedikat, gint nomen, gchar** errmsg )
{
    gint rc = 0;
    GtkWidget* content = NULL;
    GtkWidget* label_subject = NULL;
    GtkWidget* combo_labels = NULL;
    GtkWidget* listbox_incoming = NULL;
    GtkWidget* swindow_incoming = NULL;
    GtkWidget* listbox_linked = NULL;
    GtkWidget* swindow_linked = NULL;
    GtkWidget* listbox_existing = NULL;
    GtkWidget* swindow_existing = NULL;
    GtkWidget* button_new = NULL;
    GtkWidget* grid = NULL;
    GtkCellRenderer* column = NULL;
    gint ret = 0;
    GtkTreeStore* tree_store = NULL;

    content = gtk_dialog_get_content_area( GTK_DIALOG(dialog) );

    grid = gtk_grid_new( );
    gtk_box_pack_start( GTK_BOX(content), grid, TRUE, TRUE, 0 );

    swindow_incoming = gtk_scrolled_window_new( NULL, NULL );
    gtk_grid_attach( GTK_GRID(grid), swindow_incoming, 0, 0, 1, 3 );
    listbox_incoming = gtk_list_box_new( );
    rc = zond_database_fill_listbox_with_incoming_edges( dbase_full,
            listbox_incoming, ID_entity, errmsg );
    if ( rc )
    {
        gtk_widget_destroy( listbox_incoming );
        gtk_widget_destroy( grid );
        ERROR_PAO( "zond_database_fill_listbox_with_incoming_edges" )
    }

    //Knoten, der verbunden werden soll
    label_subject = zond_database_get_info_entity( dbase_full, ID_entity, errmsg );
    if ( !label_subject )
    {
        gtk_widget_destroy( grid );
        ERROR_PAO( "zond_database_get_info_entity" )
    }
    gtk_grid_attach( GTK_GRID(grid), label_subject, 1, 0, 1, 2 );

    //Listbox mit verbundenen Knoten, egal welches Prädikat
    //Scrolled Window
    swindow_linked = gtk_scrolled_window_new( NULL, NULL );
    gtk_grid_attach( GTK_GRID(grid), swindow_linked, 2, 0, 1, 4 );

    //hierein die ListBox
    listbox_linked = gtk_list_box_new( );
    rc = zond_database_fill_listbox_with_outgoing_edges( dbase_full, listbox_linked,
            ID_entity, errmsg );
    if ( rc )
    {
        gtk_widget_destroy( listbox_linked );
        gtk_widget_destroy( grid );
        ERROR_PAO( "zond_database_fill_listbox_with_linked_nodes" )
    }

    gtk_container_add( GTK_CONTAINER(swindow_linked), listbox_linked );

    //ComboBox mit dem bei Funktionsaufruf übergebenen Nomen einschließlich Kindern
    //Erst TreeStore mit root == nomen
    tree_store = gtk_tree_store_new( 2, G_TYPE_INT, G_TYPE_STRING );
    rc = zond_database_get_tree_labels( dbase_full, nomen, NULL,
            tree_store, errmsg );
    if ( rc )
    {
        gtk_widget_destroy( grid );
        g_object_unref( tree_store );
        ERROR_PAO( "zond_database_get_tree_labels" )
    }
    //ComboBox
    combo_labels = gtk_combo_box_new_with_model( GTK_TREE_MODEL(tree_store) );
    g_object_unref( tree_store );
    column = gtk_cell_renderer_text_new( );
    gtk_cell_layout_pack_start( GTK_CELL_LAYOUT(combo_labels), column, TRUE );
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo_labels), column,
                                   "text", 1,
                                   NULL);
    gtk_combo_box_set_active( GTK_COMBO_BOX(combo_labels), 0 );
    gtk_grid_attach( GTK_GRID(grid), combo_labels, 3, 0, 1, 1 );

    //Listbox mit allen Knoten mit label == nomen oder label == kind(nomen)
    swindow_existing = gtk_scrolled_window_new( NULL, NULL );
    gtk_grid_attach( GTK_GRID(grid), swindow_existing, 3, 1, 1, 2 );

    listbox_existing = gtk_list_box_new( );
    rc = zond_database_fill_listbox_with_existing_nodes( dbase_full, listbox_existing,
            nomen, errmsg );
    if ( rc )
    {
        gtk_widget_destroy( listbox_existing );
        gtk_widget_destroy( grid );
        ERROR_PAO( "zond_database_fill_listbox_with_existing_nodes" )
    }
    gtk_container_add( GTK_CONTAINER(swindow_existing), listbox_existing );

    //Button für neuen node
    button_new = gtk_button_new_with_label( "Neuer Node" );
    gtk_grid_attach( GTK_GRID(grid), button_new, 3, 3, 1, 1 );

    gtk_widget_show_all( content );

    g_object_set_data( G_OBJECT(listbox_existing), "dbase-full", dbase_full );
    g_signal_connect( combo_labels, "changed", (GCallback) zond_database_cb_combo_changed, listbox_existing );

    g_signal_connect( button_new, "clicked", (GCallback) zond_database_cb_button_clicked, dbase_full );

    ret = gtk_dialog_run( GTK_DIALOG(dialog) );

    return ret;
}


gint
zond_database_edit_node( Projekt* zond, gint ID_entity, gchar** errmsg )
{
    gint rc = 0;
    gint ret = 0;
    GtkWidget* dialog = NULL;
    GtkWidget* content = NULL;
    GtkWidget* hbox = NULL;
    GtkWidget* listbox_in = NULL;
    GtkWidget* listbox_out = NULL;
    GtkWidget* swindow_in = NULL;
    GtkWidget* swindow_out = NULL;
    GtkWidget* label_node = NULL;

    //Fenster öffnen
    dialog = gtk_dialog_new_with_buttons( "Datenbank editieren",
            GTK_WINDOW(zond->app_window), GTK_DIALOG_MODAL, "Speichern",
            GTK_RESPONSE_APPLY, "Ok", GTK_RESPONSE_OK,
            "Abbrechen", GTK_RESPONSE_CANCEL, NULL );

    content = gtk_dialog_get_content_area( GTK_DIALOG(dialog) );

    hbox = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 1 );
    gtk_box_pack_start( GTK_BOX(content), hbox, TRUE, TRUE, 2 );

    //eingehende edges
    swindow_in = gtk_scrolled_window_new( NULL, NULL );
    gtk_box_pack_start( GTK_BOX(hbox), swindow_in, TRUE, TRUE, 2 );

    listbox_in = gtk_list_box_new( );
    rc = zond_database_fill_listbox_with_incoming_edges( zond->dbase_zond->dbase_work, listbox_in, ID_entity, errmsg );
    if ( rc )
    {
        gtk_widget_destroy( listbox_in );
        gtk_widget_destroy( dialog );
        ERROR_PAO( "zond_database_fill_listbox_with_incoming_edges" )
    }
    gtk_container_add( GTK_CONTAINER(swindow_in), listbox_in );

    //node
    label_node = zond_database_get_info_entity( zond->dbase_zond->dbase_work, ID_entity, errmsg );
    if ( !label_node )
    {
        gtk_widget_destroy( dialog );
        ERROR_PAO( "zond_database_get_info_entity" )
    }
    gtk_box_pack_start( GTK_BOX(hbox), label_node, TRUE, TRUE, 2 );

    //ausgehende edges
    swindow_out = gtk_scrolled_window_new( NULL, NULL );
    gtk_box_pack_start( GTK_BOX(hbox), swindow_out, TRUE, TRUE, 2 );

    listbox_out = gtk_list_box_new( );
    rc = zond_database_fill_listbox_with_outgoing_edges( zond->dbase_zond->dbase_work, listbox_out, ID_entity, errmsg );
    if ( rc )
    {
        gtk_widget_destroy( listbox_out );
        gtk_widget_destroy( dialog );
        ERROR_PAO( "zond_database_fill_listbox_with_outgoing_edges" )
    }
    gtk_container_add( GTK_CONTAINER(swindow_out), listbox_out );

    gtk_widget_show_all( content );

    do ret = gtk_dialog_run( GTK_DIALOG(dialog) );
    while ( ret == GTK_RESPONSE_APPLY );

    gtk_widget_destroy( dialog );

    return 0;
}


static gint
zond_database_create_anbindung( DBaseFull* dbase_full, gint node_id, gchar** errmsg )
{
    gint rc = 0;
    gint ID_entity = 0;
    gchar* value = 0;

    ID_entity = dbase_full_insert_entity( dbase_full, 100, errmsg );
    if ( ID_entity == -1 ) ERROR_PAO( "dbase_full_insert_entity" )

    value = g_strdup_printf( "%i", node_id );
    rc = dbase_full_insert_property( dbase_full, ID_entity, 10010, value, errmsg );
    g_free( value );
    if ( rc ) ERROR_PAO( "dbase_full_insert_property" )

    return ID_entity;
}


gint
zond_database_insert_anbindung( Projekt* zond, gint node_id, gchar** errmsg )
{
    gint ID_entity = 0;
    gint rc = 0;

    rc = dbase_begin( (DBase*) zond->dbase_zond->dbase_work, errmsg );
    if ( rc ) ERROR_PAO( "zond_database_create_savepoint" )

    //Neue entity mit Label Anbindung erzeugen
    ID_entity = zond_database_create_anbindung( zond->dbase_zond->dbase_work, node_id, errmsg );
    if ( ID_entity == -1 ) ERROR_ROLLBACK( (DBase*) zond->dbase_zond->dbase_work,
            "zond_database_create_anbindung" )

    rc = dbase_commit( (DBase*) zond->dbase_zond->dbase_work, errmsg );
    if ( rc ) ERROR_ROLLBACK( (DBase*) zond->dbase_zond->dbase_work,
            "dbase_commit" )

    //Erzeugte Anbindung verlinken
    rc = zond_database_edit_node( zond, ID_entity, errmsg );
    if ( rc ) ERROR_PAO( "zond_database_edit_node" )

    return 0;
}


