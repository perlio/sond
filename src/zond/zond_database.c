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

#include "error.h"
#include "global_types.h"

#include "20allgemein/project.h"
#include "20allgemein/dbase_full.h"

#include "zond_database.h"


static void
zond_database_cb_combo_changed( GtkWidget* combo_labels, gpointer data )
{
    GtkWidget* listbox = (GtkWidget*) data;

    //listbox leeren

    //listbox mit neuem label füllen

    return;
}


static GtkWidget*
zond_database_get_info_entity( DBaseFull* dbase_full, gint ID_entity, gchar** errmsg )
{
    gint rc = 0;
    GtkWidget* info = NULL;
    gchar* label_entity = NULL;
    GArray* arr_properties = NULL;
    GArray* arr_edges = NULL;
    gchar* markup = NULL;

    rc = dbase_full_get_label_entity( dbase_full, ID_entity, &label_entity, errmsg );
    if ( rc ) ERROR_PAO_R( "dbase_full_get_label_identity", NULL )

    rc = dbase_full_get_properties( dbase_full, ID_entity, &arr_properties, errmsg );
    if ( rc )
    {
        g_free( label_entity );
        ERROR_PAO_R( "dbase_full_get_properties", NULL )
    }

    rc = dbase_full_get_edges( dbase_full, ID_entity, &arr_edges, errmsg );
    if ( rc )
    {
        g_free( label_entity );
        g_array_unref( arr_properties );
        ERROR_PAO_R( "dbase_full_get_properties", NULL )
    }

    markup = g_strdup_printf( "<big>%i  <b>%s</b></big>\n", ID_entity, label_entity );
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

    if ( arr_edges->len > 0 )
    {
        markup = add_string( markup, g_strdup( "   Edges:\n" ) );

        for ( gint i = 0; i < arr_properties->len; i++ )
        {
            Edge edge = g_array_index( arr_edges, Edge, i );
            markup = add_string( markup, g_strdup_printf( "     <small>%i  %s  %s\n</small>",
                    edge.ID_edge, edge.label_edge, edge.label_object ) );

            if ( edge.arr_properties->len > 0 )
            {
                markup = add_string( markup, g_strdup( "       Qualifier:" ) );

                for ( gint u = 0; u < edge.arr_properties->len; u++ )
                {
                    Property property = g_array_index( edge.arr_properties, Property, u );
                    markup = add_string( markup, g_strdup_printf(
                            "          <small>%i  %s  %s\n</small>",
                            property.ID, property.label, property.value ) );
                }
            }
        }
    }

    g_array_unref( arr_edges );

    info = gtk_label_new( NULL );
    gtk_label_set_markup( GTK_LABEL(info), markup );
    g_free( markup );

    return info;
}


static gint
zond_database_fill_listbox_with_linked_nodes( DBaseFull* dbase_full,
        GtkWidget* listbox, gint ID_entity, gchar** errmsg)
{
    gint rc = 0;
    GArray* arr_edges = NULL;

    rc = dbase_full_get_edges( dbase_full, ID_entity, &arr_edges, errmsg );
    if ( rc ) ERROR_PAO( "dbase_full_get_edges" )

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

        label_object = zond_database_get_info_entity( dbase_full, ID_entity, errmsg );
        if ( !label_object )
        {
            gtk_widget_destroy( label_edge );
            g_array_unref( arr_edges );
            ERROR_PAO( "zond_database_get_info_entity" )
        }

        gtk_box_pack_start( GTK_BOX(hbox), label_object, FALSE, FALSE, 0 );
    }

    g_array_unref( arr_edges );

    return 0;
}


static GtkTreeStore*
zond_database_get_tree_labels( DBaseFull* dbase_full, gint label, gchar** errmsg )
{
    GtkTreeStore* tree_store = NULL;
    gchar* sql = NULL;

    sql = g_strdup_printf( "SELECT label FROM labels WHERE ");

    g_free( sql );

    return tree_store;
}


static gint
zond_database_link_nodes( DBaseFull* dbase_full, GtkWidget* dialog,
        gint ID_entity, gint praedikat, gint nomen, gchar** errmsg )
{
    gint rc = 0;
    GtkWidget* content = NULL;
    GtkTreeStore* tree_labels = NULL;
    GtkWidget* combo_labels = NULL;
    GtkWidget* listbox_linked = NULL;
    GtkWidget* swindow_linked = NULL;
    GtkWidget* listbox_existing = NULL;
    GtkWidget* swindow_existing = NULL;
    GtkWidget* grid = NULL;
    gint ret = 0;

    content = gtk_dialog_get_content_area( GTK_DIALOG(dialog) );

    grid = gtk_grid_new( );
    gtk_box_pack_start( GTK_BOX(content), grid, FALSE, FALSE, 0 );

    swindow_linked = gtk_scrolled_window_new( NULL, NULL );
    gtk_grid_attach( GTK_GRID(grid), swindow_linked, 0, 0, 1, 1 );

    listbox_linked = gtk_list_box_new( );
    rc = zond_database_fill_listbox_with_linked_nodes( dbase_full, listbox_linked,
            ID_entity, errmsg );
    if ( rc )
    {
        gtk_widget_destroy( grid );
        ERROR_PAO( "zond_database_fill_listbox_with_linked_nodes" )
    }

    gtk_container_add( GTK_CONTAINER(swindow_linked), listbox_linked );

    tree_labels = zond_database_get_tree_labels( dbase_full, nomen, errmsg );
    if ( !tree_labels ) ERROR_PAO( "zond_database_link_to_konvolut" )
    combo_labels = gtk_combo_box_new_with_model( GTK_TREE_MODEL(tree_labels) );
    g_object_unref( tree_labels );
    gtk_grid_attach( GTK_GRID(grid), combo_labels, 1, 0, 1, 1 );

    swindow_existing = gtk_scrolled_window_new( NULL, NULL );
    gtk_grid_attach( GTK_GRID(grid), swindow_linked, 2, 0, 1, 1 );

    listbox_existing = gtk_list_box_new( );
//    rc = zond_database_fill_listbox_with_existing_nodes( dbase_full,
//            nomen, errmsg );
    gtk_container_add( GTK_CONTAINER(swindow_existing), listbox_existing );

    g_signal_connect( combo_labels, "changed", (GCallback) zond_database_cb_combo_changed, listbox_existing );

    ret = gtk_dialog_run( GTK_DIALOG(dialog) );

    return ret;
}


static gint
zond_database_link_anbindung( Projekt* zond, gint ID_entity, gchar** errmsg )
{
    gint ret = 0;
    GtkWidget* dialog = NULL;

    //Fenster öffnen
    dialog = gtk_dialog_new_with_buttons( "Anbindung verknüpfen",
            GTK_WINDOW(zond->app_window), GTK_DIALOG_MODAL, "Weiter",
            GTK_RESPONSE_CLOSE, "Ok", GTK_RESPONSE_OK,
            "Abbrechen", GTK_RESPONSE_CANCEL, NULL );

    //Fenster ausgestalten mit Link zu Konvolut
    ret = zond_database_link_nodes( zond->dbase_zond->dbase_work, dialog,
            ID_entity, 10290, 600, errmsg ); //600 ist Konvolut, 10290 Anbindung --> Konvolut
    if ( ret == GTK_RESPONSE_CANCEL ) return 1;
    else if ( ret == GTK_RESPONSE_CLOSE ) return 0;

    //else if ret == GTK_RESPONSE_APPLY: link to inhalt

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
    rc = dbase_full_insert_property( dbase_full, ID_entity, 10075, value, errmsg );
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

    //Erzeugte Anbindung verlinken
    rc = zond_database_link_anbindung( zond, ID_entity, errmsg );
    if ( rc == 1 )
    {
        ROLLBACK( (DBase*) zond->dbase_zond->dbase_work )
        return 0;
    }
    else if ( rc == -1 ) ERROR_ROLLBACK( (DBase*) zond->dbase_zond->dbase_work,
            "zond_database_link_anbindung" );

    rc = dbase_commit( (DBase*) zond->dbase_zond->dbase_work, errmsg );
    if ( rc ) ERROR_ROLLBACK( (DBase*) zond->dbase_zond->dbase_work,
            "dbase_commit" )

    return 0;
}
