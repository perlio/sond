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


typedef struct _Dialog
{
    DBaseFull* dbase_full;
    GtkWidget* dialog;
    GtkWidget* listbox_in;
    GtkWidget* listbox_out;
    GtkWidget* button_in;
    GtkWidget* button_out;
    gint ID_entity;
} Dialog;

typedef enum _Direction
{
    INCOMING,
    OUTGOING
} Direction;


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
zond_database_parse_label_string( const gchar* adm_entities, GArray** arr_adm_entites,
        gchar** errmsg )
{
    gchar** strsplit = NULL;
    gchar* ptr = NULL;
    gint i = 0;

    if ( !arr_adm_entites ) return;

    *arr_adm_entites = g_array_new( FALSE, FALSE, sizeof( gint ) );

    strsplit = g_strsplit( adm_entities, ",", 0 );

    while ( (ptr = strsplit[i]) )
    {
        gint ID_entity = atoi( ptr );
        g_array_append_val( *arr_adm_entites, ID_entity );
        i++;
    }

    g_strfreev( strsplit );

    return;
}


static gint
zond_database_get_adm_entities_for_subject( DBaseFull* dbase_full, gint ID_entity,
        GtkTreeStore* tree_store, gchar** errmsg )
{
    GArray* arr_adm_entities = NULL;
    gchar* adm_entities = NULL;
    gint rc = 0;

    rc = dbase_full_get_adm_entities( dbase_full, ID_entity, &adm_entities, errmsg );
    if ( rc ) ERROR_PAO( "dbase_full_get_adm_entities" )

    if ( !adm_entities ) return 0;

    zond_database_parse_label_string( adm_entities, &arr_adm_entities, errmsg );
    g_free( adm_entities );

    for ( gint i = 0; i < arr_adm_entities->len; i++ )
    {
        rc = zond_database_get_tree_labels( dbase_full, g_array_index( arr_adm_entities, gint, i ), NULL, tree_store, errmsg );
        if ( rc )
        {
            g_array_unref( arr_adm_entities );
            ERROR_PAO( "zond_database_get_tree_labels" );
        }
    }

    g_array_unref( arr_adm_entities );

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
zond_database_fill_listbox_with_edges( DBaseFull* dbase_full,
        Direction dir, GtkWidget* listbox, gint ID_entity, gchar** errmsg)
{
    gint rc = 0;
    GArray* arr_edges = NULL;

    if ( dir == INCOMING ) rc = dbase_full_get_incoming_edges( dbase_full, ID_entity, &arr_edges, errmsg );
    else rc = dbase_full_get_outgoing_edges( dbase_full, ID_entity, &arr_edges, errmsg );
    if ( rc ) ERROR_PAO( "dbase_full_get_incoming/outgoing_edges" )

    for ( gint i = 0; i < arr_edges->len; i++ )
    {
        Edge edge = { 0 };
        GtkWidget* hbox = NULL;
        GtkWidget* label_edge = NULL;
        GtkWidget* label_subject = NULL;
        GtkWidget* listbox_row = NULL;
        gchar* text_edge = NULL;
        GtkWidget* label_object = NULL;

        edge = g_array_index( arr_edges, Edge, i );

        hbox = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 0 );

        if ( dir == INCOMING )
        {
            label_subject = zond_database_get_info_entity( dbase_full, edge.ID_subject, errmsg );
            if ( !label_subject )
            {
                gtk_widget_destroy( label_edge );
                g_array_unref( arr_edges );
                ERROR_PAO( "zond_database_get_info_entity" )
            }

            gtk_box_pack_start( GTK_BOX(hbox), label_subject, FALSE, FALSE, 0 );
        }

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

        if ( dir == OUTGOING )
        {
            label_object = zond_database_get_info_entity( dbase_full, edge.ID_object, errmsg );
            if ( !label_object )
            {
                gtk_widget_destroy( label_edge );
                g_array_unref( arr_edges );
                ERROR_PAO( "zond_database_get_info_entity" )
            }
            gtk_box_pack_start( GTK_BOX(hbox), label_object, FALSE, FALSE, 0 );
        }

        listbox_row = gtk_list_box_row_new( );
        g_object_set_data( G_OBJECT(listbox_row), "ID-edge", GINT_TO_POINTER(edge.ID_edge) );
        gtk_container_add( GTK_CONTAINER(listbox_row), hbox );

        gtk_list_box_insert( GTK_LIST_BOX(listbox), listbox_row, -1 );
    }

    g_array_unref( arr_edges );

    return 0;
}


static void
zond_database_cb_button_new( GtkButton* button, gpointer data )
{
    Dialog* ptr_dialog = NULL;
    GtkWidget* dialog = NULL;
    GtkWidget* content = NULL;
    GtkWidget* hbox = NULL;
    gint ret = 0;
    DBaseFull* dbase_full = NULL;

    dbase_full = (DBaseFull*) data;

    //Fenster öffnen
    dialog = gtk_dialog_new_with_buttons( "Verbindung erstellen",
            GTK_WINDOW(gtk_widget_get_toplevel( GTK_WIDGET(button) )), GTK_DIALOG_MODAL, "Ok", GTK_RESPONSE_OK,
            "Abbrechen", GTK_RESPONSE_CANCEL, NULL );

    content = gtk_dialog_get_content_area( GTK_DIALOG(dialog) );

    hbox = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 1 );
    gtk_box_pack_start( GTK_BOX(content), hbox, TRUE, TRUE, 2 );


    ret = gtk_dialog_run( GTK_DIALOG(dialog) );

    return;
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
    GtkWidget* vbox_in = NULL;
    GtkWidget* combo_out = NULL;
    GtkCellRendererText* column_out = NULL;
    GtkTreeStore* treestore_out = NULL;
    GtkWidget* button_in = NULL;
    GtkWidget* button_out = NULL;
    GtkWidget* vbox_out = NULL;
    GtkWidget* label_node = NULL;

    Direction dir = INCOMING;
    Dialog* ptr_dialog = NULL;

    //Fenster öffnen
    dialog = gtk_dialog_new_with_buttons( "Datenbank editieren",
            GTK_WINDOW(zond->app_window), GTK_DIALOG_MODAL, "Speichern",
            GTK_RESPONSE_APPLY, "Ok", GTK_RESPONSE_OK,
            "Abbrechen", GTK_RESPONSE_CANCEL, NULL );

    content = gtk_dialog_get_content_area( GTK_DIALOG(dialog) );

    hbox = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 1 );
    gtk_box_pack_start( GTK_BOX(content), hbox, TRUE, TRUE, 2 );

    //eingehende edges
    vbox_in = gtk_box_new( GTK_ORIENTATION_VERTICAL, 1 );
    gtk_box_pack_start( GTK_BOX(hbox), vbox_in, TRUE, TRUE, 2 );

    //button new
    button_in = gtk_button_new_with_label( "Neue eingehene\nVerbindung" );
    gtk_box_pack_start( GTK_BOX(vbox_in), button_in, TRUE, TRUE, 2 );

    swindow_in = gtk_scrolled_window_new( NULL, NULL );
    gtk_box_pack_start( GTK_BOX(vbox_in), swindow_in, TRUE, TRUE, 2 );

    listbox_in = gtk_list_box_new( );
    rc = zond_database_fill_listbox_with_edges( zond->dbase_zond->dbase_work, dir, listbox_in, ID_entity, errmsg );
    if ( rc )
    {
        gtk_widget_destroy( listbox_in );
        gtk_widget_destroy( dialog );
        ERROR_PAO( "zond_database_fill_listbox_with_edges (incoming)" )
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
    dir = OUTGOING;
    vbox_out = gtk_box_new( GTK_ORIENTATION_VERTICAL, 1 );
    gtk_box_pack_start( GTK_BOX(hbox), vbox_out, TRUE, TRUE, 2 );

    treestore_out = gtk_tree_store_new( 2, G_TYPE_INT, G_TYPE_STRING );
    rc = zond_database_get_adm_entities_for_subject( zond->dbase_zond->dbase_work, ID_entity, treestore_out, errmsg );
    if ( rc )
    {
        gtk_widget_destroy( dialog );
        ERROR_PAO( "zond_database_get_tree_adm_entities" )
    }
    combo_out = gtk_combo_box_new_with_model( GTK_TREE_MODEL(treestore_out) );
    g_object_unref( treestore_out );
    column_out = gtk_cell_renderer_text_new( );
    gtk_cell_layout_pack_start( GTK_CELL_LAYOUT(combo_out), column_out, TRUE );
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo_out), column_out,
                                   "text", 1,
                                   NULL);
    gtk_combo_box_set_active( GTK_COMBO_BOX(combo_out), 0 );
    gtk_box_pack_start( GTK_BOX(vbox_out), combo_out, TRUE, TRUE, 2 );

    //button new
    button_out = gtk_button_new_with_label( "Neue ausgehene\nVerbindung" );
    gtk_box_pack_start( GTK_BOX(vbox_out), button_out, TRUE, TRUE, 2 );

    swindow_out = gtk_scrolled_window_new( NULL, NULL );
    gtk_box_pack_start( GTK_BOX(vbox_out), swindow_out, TRUE, TRUE, 2 );

    listbox_out = gtk_list_box_new( );
    rc = zond_database_fill_listbox_with_edges( zond->dbase_zond->dbase_work,
            dir, listbox_out, ID_entity, errmsg );
    if ( rc )
    {
        gtk_widget_destroy( listbox_out );
        gtk_widget_destroy( dialog );
        ERROR_PAO( "zond_database_fill_listbox_with_edges (outgoing)" )
    }
    gtk_container_add( GTK_CONTAINER(swindow_out), listbox_out );

    gtk_widget_show_all( content );

    ptr_dialog = g_malloc0( sizeof( Dialog ) );
    ptr_dialog->dbase_full = zond->dbase_zond->dbase_work;
    ptr_dialog->dialog = dialog;
    ptr_dialog->listbox_in = listbox_in;
    ptr_dialog->listbox_out = listbox_out;
    ptr_dialog->button_in = button_in;
    ptr_dialog->button_out = button_out;
    ptr_dialog->ID_entity = ID_entity;

//    g_signal_connect( listbox_in, "row-activated", zond_database_cb_listbox_row, ptr_dialog );
//    g_signal_connect( listbox_out, "row-activated", zond_database_cb_listbox_row, ptr_dialog );
    g_signal_connect( button_in, "clicked", (GCallback) zond_database_cb_button_new, ptr_dialog );
    g_signal_connect( button_out, "clicked", (GCallback) zond_database_cb_button_new, ptr_dialog );

    do ret = gtk_dialog_run( GTK_DIALOG(dialog) );
    while ( ret == GTK_RESPONSE_APPLY );

    g_free( ptr_dialog );

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


