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


typedef enum _Direction
{
    INCOMING,
    OUTGOING
} Direction;

typedef struct _Node
{
    Entity* node;
    GArray* arr_incoming_segs;
    GArray* arr_outgoing_segs;
} Node;

typedef struct _Linked_Node
{
    gint ID;
    Node* node;
} LinkedNode;

typedef union _Segment
{
    Direction dir;
    struct
    {
        Node* subject;
        Entity* edge;
    } In;
    struct
    {
        Entity* edge;
        Node* object;
    } Out;
} Segment;

void
zond_database_free_segment( gpointer data )
{
    Segment* segment = (Segment*) data;

    if ( segment->dir == INCOMING )
    {
        g_free( segment->In.subject->node->label );
        g_array_unref( segment->In.subject->node->arr_properties );
        g_free( segment->In.edge->label );
        g_array_unref( segment->In.edge->arr_properties );
    }
    else
    {
        g_free( segment->Out.object->node->label );
        g_array_unref( segment->Out.object->node->arr_properties );
        g_free( segment->Out.edge->label );
        g_array_unref( segment->Out.edge->arr_properties );
    }

    return;
}

static GtkWidget*
zond_database_get_info_entity( DBaseFull* dbase_full, gint ID_entity, gchar** errmsg )
{
    gint rc = 0;
    GtkWidget* info = NULL;
    gchar* label_entity = NULL;
    GArray* arr_properties = NULL;
    gchar* markup = NULL;

    rc = dbase_full_get_label_text_for_entity( dbase_full, ID_entity, &label_entity, errmsg );
    if ( rc ) ERROR_PAO_R( "dbase_full_get_label_entity", NULL )

    rc = dbase_full_get_properties( dbase_full, ID_entity, &arr_properties, errmsg );
    if ( rc )
    {
        g_free( label_entity );
        ERROR_PAO_R( "dbase_full_get_properties", NULL )
    }

    markup = g_strdup_printf( "<big><b>%s</b></big>\n", label_entity );
    g_free( label_entity );

    if ( arr_properties->len > 0 )
    {
        markup = add_string( markup, g_strdup( "   Properties:\n" ) );

        for ( gint i = 0; i < arr_properties->len; i++ )
        {
            Property property = g_array_index( arr_properties, Property, i );
            markup = add_string( markup,
                    g_strdup_printf( "     <small>%s:  %s\n</small>",
                    property.label, property.value ) );
            for ( gint u = 0; u < property.arr_properties->len; u++ )
            {
                Property property_of_property = g_array_index( property.arr_properties, Property, u );
                markup = add_string( markup,
                        g_strdup_printf( "       <small>%s:  %s\n</small>",
                        property_of_property.label, property_of_property.value ) );
            }
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
zond_database_combo_get_label( DBaseFull* dbase_full, GtkWidget* combo, gchar** errmsg )
{
    GtkTreeIter iter = { 0 };
    GtkTreeModel* model = NULL;
    gint label = 0;

    //iter abfragen
    if ( !gtk_combo_box_get_active_iter( GTK_COMBO_BOX(combo), &iter ) )
    {
        if ( errmsg ) *errmsg = g_strdup( "Bei Aufruf gtk_combo_box_get_active_iter:\n"
                "iter konnte nicht gesetzt werden" );
        return -1;
    }

    model = gtk_combo_box_get_model( GTK_COMBO_BOX(combo) );
    gtk_tree_model_get( model, &iter, 0, &label, -1 );

    return label;
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


static gint
zond_database_get_adm_entities( DBaseFull* dbase_full, gint label,
        GtkTreeStore* tree_store, gchar** errmsg )
{
    GArray* arr_adm_entities = NULL;
    gint rc = 0;

    rc = dbase_full_get_adm_entities( dbase_full, label, &arr_adm_entities, errmsg );
    if ( rc ) ERROR_PAO( "dbase_full_get_adm_entities" )

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


typedef struct _Data_CB_New_Edge
{
    DBaseFull* dbase_full;
    gint ID_entity;
    GtkWidget* listbox_in;
    GtkWidget* listbox_out;
} DataCBNewEdge;

typedef struct _Data_CB_New_Node
{
    DataCBNewEdge data_cb_new_edge;
    gint label_edge;
    gint label_node;
    GtkWidget* listbox_node;
} DataCBNewNode;


static void
zond_database_cb_button_new_node( GtkButton* button, gpointer data )
{
    DataCBNewNode* data_cb_new_node = NULL;
    gint ID_node = 0;
    gint ID_edge = 0;
    gint rc = 0;
    gchar* errmsg = NULL;
    GtkWidget* label_node = NULL;

    data_cb_new_node = (DataCBNewNode*) data;

    //neue entity mit label = combo-wahl erzeugen
    ID_node = dbase_full_insert_entity( data_cb_new_node->data_cb_new_edge.dbase_full, data_cb_new_node->label_node, &errmsg );
    if ( ID_node == -1 )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(button) ), "Fehler bei "
                "Erzeugung Knoten -\n\nBei Aufruf dbase_full_insert_entity (node):\n",
                errmsg, NULL );
        gtk_dialog_response( GTK_DIALOG(gtk_widget_get_toplevel( GTK_WIDGET(button) )),
                GTK_RESPONSE_CANCEL );
        g_free( errmsg );

        return;
    }

    //neue entity mit label = label:edge erzeugen
    ID_edge = dbase_full_insert_entity( data_cb_new_node->data_cb_new_edge.dbase_full,
            data_cb_new_node->label_edge, &errmsg );
    if ( ID_edge == -1 )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(button) ), "Fehler bei "
                "Erzeugung Knoten -\n\nBei Aufruf dbase_full_insert_entity (edge):\n",
                errmsg, NULL );
        gtk_dialog_response( GTK_DIALOG(gtk_widget_get_toplevel( GTK_WIDGET(button) )),
                GTK_RESPONSE_CANCEL );
        g_free( errmsg );

        return;
    }

    //neue edge
    rc = dbase_full_insert_edge( data_cb_new_node->data_cb_new_edge.dbase_full,
            ID_edge, data_cb_new_node->data_cb_new_edge.ID_entity, ID_node,
            &errmsg );
    if ( rc )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(button) ), "Fehler bei "
                "Erzeugung Knoten -\n\nBei Aufruf dbase_full_insert_edge:\n",
                errmsg, NULL );
        gtk_dialog_response( GTK_DIALOG(gtk_widget_get_toplevel( GTK_WIDGET(button) )),
                GTK_RESPONSE_CANCEL );
        g_free( errmsg );

        return;
    }

    //in listbox anzeigen
    label_node = zond_database_get_info_entity( data_cb_new_node->data_cb_new_edge.dbase_full, ID_node, &errmsg );
    if ( !label_node )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(button) ), "Fehler bei "
                "Erzeugung Knoten -\n\nBei Aufruf zond_database_get_info_entity:\n",
                errmsg, NULL );
        gtk_dialog_response( GTK_DIALOG(gtk_widget_get_toplevel( GTK_WIDGET(button) )),
                GTK_RESPONSE_CANCEL );
        g_free( errmsg );

        return;
    }

    gtk_widget_show( label_node );
    gtk_list_box_insert( GTK_LIST_BOX(data_cb_new_node->listbox_node), label_node, -1 );

    return;
}


static void
zond_database_cb_combo_edge_row_changed( GtkComboBox* self, gpointer data )
{
    DataCBNewNode* data_cb_new_node = NULL;
    gint label = 0;
    gchar* errmsg = NULL;

    data_cb_new_node = (DataCBNewNode*) data;

    label = zond_database_combo_get_label( data_cb_new_node->data_cb_new_edge.dbase_full, GTK_WIDGET(self), &errmsg );
    if ( label == -1 )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(self) ), "Fehler bei "
                "Zeilenwechsel in ComboBox -\n\nBei Aufruf zond_database_combo_get_label:\n",
                errmsg, NULL );
        gtk_dialog_response( GTK_DIALOG(gtk_widget_get_toplevel( GTK_WIDGET(self) )),
                GTK_RESPONSE_CANCEL );
        g_free( errmsg );

        return;
    }

    data_cb_new_node->label_edge = label;

    return;
}


static void
zond_database_cb_combo_node_row_changed( GtkComboBox* self, gpointer data )
{
    DataCBNewNode* data_cb_new_node = NULL;
    gint label = 0;
    gchar* errmsg = NULL;

    data_cb_new_node = (DataCBNewNode*) data;

    label = zond_database_combo_get_label( data_cb_new_node->data_cb_new_edge.dbase_full, GTK_WIDGET(self), &errmsg );
    if ( label == -1 )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(self) ), "Fehler bei "
                "Zeilenwechsel in ComboBox -\n\nBei Aufruf zond_database_combo_get_label:\n",
                errmsg, NULL );
        gtk_dialog_response( GTK_DIALOG(gtk_widget_get_toplevel( GTK_WIDGET(self) )),
                GTK_RESPONSE_CANCEL );
        g_free( errmsg );

        return;
    }

    data_cb_new_node->label_node = label;

    return;
}


static void
zond_database_cb_button_new_edge( GtkButton* button, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    DataCBNewEdge* data_cb_new_edge = NULL;
    DataCBNewNode* data_cb_new_node = NULL;
    GtkWidget* dialog = NULL;
    GtkWidget* content = NULL;
    GtkWidget* hbox = NULL;
    GtkTreeStore* treestore_edge = NULL;
    GtkWidget* combo_edge = NULL;
    GtkCellRenderer* column_edge = NULL;
    gint label_edge = 0;
    GtkWidget* vbox_node = NULL;
    GtkTreeStore* treestore_node = NULL;
    GtkWidget* combo_node = NULL;
    GtkCellRenderer* column_node = NULL;
    GtkWidget* swindow_node = NULL;
    GtkWidget* listbox_node = NULL;
    GtkWidget* button_new_node = NULL;
    gint label_entity = 0;
    gint label_node = 0;
    gint ret = 0;

    data_cb_new_edge = (DataCBNewEdge*) data;

    //Fenster öffnen
    dialog = gtk_dialog_new_with_buttons( "Verbindung erstellen",
            GTK_WINDOW(gtk_widget_get_toplevel( GTK_WIDGET(button) )), GTK_DIALOG_MODAL, "Ok", GTK_RESPONSE_OK,
            "Abbrechen", GTK_RESPONSE_CANCEL, NULL );

    content = gtk_dialog_get_content_area( GTK_DIALOG(dialog) );

    hbox = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 1 );
    gtk_box_pack_start( GTK_BOX(content), hbox, TRUE, TRUE, 2 );

    label_entity = dbase_full_get_label_for_entity( data_cb_new_edge->dbase_full, data_cb_new_edge->ID_entity, &errmsg );
    if ( label_entity == -1 )
    {
        display_message( gtk_widget_get_toplevel( dialog ), "Fehler bei Verbindung erstellen -\n\n"
                "Bei Aufruf dbase_full_get_label_for_entity:\n", errmsg, NULL );
        gtk_widget_destroy( dialog );
        g_free( errmsg );

        return;
    }

    treestore_edge = gtk_tree_store_new( 2, G_TYPE_INT, G_TYPE_STRING );
    rc = zond_database_get_adm_entities( data_cb_new_edge->dbase_full, label_entity, treestore_edge, &errmsg );
    if ( rc )
    {
        g_object_unref( treestore_edge );
        display_message( gtk_widget_get_toplevel( dialog ), "Fehler bei Verbindung erstellen -\n\n"
                "Bei Aufruf zond_database_get_adm_entities:\n", errmsg, NULL );
        gtk_widget_destroy( dialog );
        g_free( errmsg );

        return;
    }
    combo_edge = gtk_combo_box_new_with_model( GTK_TREE_MODEL(treestore_edge) );
    g_object_unref( treestore_edge );
    column_edge = gtk_cell_renderer_text_new( );
    gtk_cell_layout_pack_start( GTK_CELL_LAYOUT(combo_edge), column_edge, TRUE );
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo_edge), column_edge,
                                   "text", 1,
                                   NULL);
    gtk_combo_box_set_active( GTK_COMBO_BOX(combo_edge), 0 );
    gtk_box_pack_start( GTK_BOX(hbox), combo_edge, FALSE, FALSE, 2 );

    //Knoten
    //vbox
    vbox_node = gtk_box_new( GTK_ORIENTATION_VERTICAL, 2 );
    gtk_box_pack_start( GTK_BOX(hbox), vbox_node, TRUE, TRUE, 2 );

    label_edge = zond_database_combo_get_label( data_cb_new_edge->dbase_full, combo_edge, &errmsg );
    if ( label_edge == -1 )
    {
        gtk_widget_destroy( dialog );
        display_message( gtk_widget_get_toplevel( dialog ), "Fehler bei Verbindung erstellen -\n\n"
                "Bei Aufruf zond_database_combo_get_label:\n", errmsg, NULL );
        g_free( errmsg );

        return;
    }
    treestore_node = gtk_tree_store_new( 2, G_TYPE_INT, G_TYPE_STRING );
    rc = zond_database_get_adm_entities( data_cb_new_edge->dbase_full, label_edge, treestore_node, &errmsg );
    if ( rc )
    {
        display_message( gtk_widget_get_toplevel( dialog ), "Fehler bei Verbindung erstellen -\n\n"
                "Bei Aufruf zond_database_get_adm_entities:\n", errmsg, NULL );
        gtk_widget_destroy( dialog );
        g_object_unref( treestore_node);
        g_free( errmsg );

        return;
    }
    combo_node = gtk_combo_box_new_with_model( GTK_TREE_MODEL(treestore_node) );
    g_object_unref( treestore_node );
    column_node = gtk_cell_renderer_text_new( );
    gtk_cell_layout_pack_start( GTK_CELL_LAYOUT(combo_node), column_node, TRUE );
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo_node), column_node,
                                   "text", 1,
                                   NULL);
    gtk_combo_box_set_active( GTK_COMBO_BOX(combo_node), 0 );
    gtk_box_pack_start( GTK_BOX(vbox_node), combo_node, FALSE, FALSE, 2 );

    label_node = zond_database_combo_get_label( data_cb_new_edge->dbase_full, combo_node, &errmsg );
    if ( label_node == -1 )
    {
        display_message( gtk_widget_get_toplevel( dialog ), "Fehler bei Verbindung erstellen -\n\n"
                "Bei Aufruf zond_database_combo_get_label:\n", errmsg, NULL );
        gtk_widget_destroy( dialog );
        g_free( errmsg );

        return;
    }
    swindow_node = gtk_scrolled_window_new( NULL, NULL );
    gtk_box_pack_start( GTK_BOX(vbox_node), swindow_node, TRUE, TRUE, 2 );

    listbox_node = gtk_list_box_new( );
    gtk_container_add( GTK_CONTAINER(swindow_node), listbox_node );

    rc = zond_database_fill_listbox_with_existing_nodes( data_cb_new_edge->dbase_full, listbox_node, label_node, &errmsg );
    if ( rc )
    {
        display_message( gtk_widget_get_toplevel( dialog ), "Fehler bei Verbindung erstellen -\n\n"
                "Bei Aufruf zond_database_fill_listbox_with_existing_nodes:\n", errmsg, NULL );
        gtk_widget_destroy( dialog );
        g_free( errmsg );

        return;
    }

    button_new_node = gtk_button_new_with_label( "Neuer Knoten" );
    gtk_box_pack_start( GTK_BOX(vbox_node), button_new_node, FALSE, FALSE, 2 );

    data_cb_new_node = g_malloc0( sizeof( DataCBNewNode ) );
    data_cb_new_node->data_cb_new_edge = *data_cb_new_edge;
    data_cb_new_node->label_edge = label_edge;
    data_cb_new_node->label_node = label_node;
    data_cb_new_node->listbox_node = listbox_node;

    g_signal_connect( combo_node, "changed", (GCallback) zond_database_cb_combo_node_row_changed, data_cb_new_node );
    g_signal_connect( combo_node, "changed", (GCallback) zond_database_cb_combo_edge_row_changed, data_cb_new_node );
    g_signal_connect( button_new_node, "clicked",
            (GCallback) zond_database_cb_button_new_node, data_cb_new_node );

    gtk_widget_show_all( content );
    ret = gtk_dialog_run( GTK_DIALOG(dialog) );

    g_free( data_cb_new_node );

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
        {/*
            label_subject = zond_database_get_info_entity( dbase_full, edge.ID_subject, errmsg );
            if ( !label_subject )
            {
                gtk_widget_destroy( label_edge );
                g_array_unref( arr_edges );
                ERROR_PAO( "zond_database_get_info_entity" )
            }
*/
            gtk_box_pack_start( GTK_BOX(hbox), label_subject, FALSE, FALSE, 0 );
        }
/*
        text_edge = g_strdup_printf( "ID: %i  Label: %s", edge.ID_edge, edge.label_edge );
        for (gint u = 0; u < edge.arr_properties->len; u++ )
        {
            Property property = g_array_index( edge.arr_properties, Property, u );

            text_edge = add_string( text_edge, g_strdup_printf( "\n%i  %s  %s",
                    property.ID, property.label, property.value ) );
        }
*/        label_edge = gtk_label_new( (const gchar*) text_edge );
        g_free( text_edge );
        gtk_box_pack_start( GTK_BOX(hbox), label_edge, FALSE, FALSE, 0 );

        if ( dir == OUTGOING )
        {/*
            label_object = zond_database_get_info_entity( dbase_full, edge.ID_object, errmsg );
            if ( !label_object )
            {
                gtk_widget_destroy( label_edge );
                g_array_unref( arr_edges );
                ERROR_PAO( "zond_database_get_info_entity" )
            }
            gtk_box_pack_start( GTK_BOX(hbox), label_object, FALSE, FALSE, 0 );
      */  }

        listbox_row = gtk_list_box_row_new( );
//        g_object_set_data( G_OBJECT(listbox_row), "ID-edge", GINT_TO_POINTER(edge.ID_edge) );
        gtk_container_add( GTK_CONTAINER(listbox_row), hbox );

        gtk_list_box_insert( GTK_LIST_BOX(listbox), listbox_row, -1 );
    }

    g_array_unref( arr_edges );

    return 0;
}


/*
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
    GtkWidget* button_in = NULL;
    GtkWidget* button_out = NULL;
    GtkWidget* vbox_out = NULL;
    GtkWidget* label_node = NULL;
    Entity node = { 0 };

    Direction dir = INCOMING;
    DataCBNewEdge* data_cb_new_edge = NULL;

    rc = dbase_full_get_entity( zond->dbase_zond->dbase_work, ID_entity, &node, errmsg );
    if ( rc ) ERROR_PAO( "dbase_full_get_entity" )

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

    data_cb_new_edge = g_malloc0( sizeof( DataCBNewEdge ) );
    data_cb_new_edge->dbase_full = zond->dbase_zond->dbase_work;
    data_cb_new_edge->ID_entity = ID_entity;
    data_cb_new_edge->listbox_in = listbox_in;
    data_cb_new_edge->listbox_out = listbox_out;

//    g_signal_connect( listbox_in, "row-activated", zond_database_cb_listbox_row, ptr_dialog );
//    g_signal_connect( listbox_out, "row-activated", zond_database_cb_listbox_row, ptr_dialog );
    g_signal_connect( button_in, "clicked", (GCallback) zond_database_cb_button_new_edge, data_cb_new_edge );
    g_signal_connect( button_out, "clicked", (GCallback) zond_database_cb_button_new_edge, data_cb_new_edge );

    do ret = gtk_dialog_run( GTK_DIALOG(dialog) );
    while ( ret == GTK_RESPONSE_APPLY );

    g_free( data_cb_new_edge );

    gtk_widget_destroy( dialog );

    return 0;
}
*/

static gint
zond_database_cmpint (gconstpointer a, gconstpointer b)
{
  const gint *_a = a;
  const gint *_b = b;

  return *_a - *_b;
}


static Node*
zond_database_get_node( DBaseFull*, gint, GArray*, gint, gchar** );

static gint
zond_database_get_outgoing_segments( DBaseFull* dbase_full, Entity* entity,
        GArray* arr_linked_nodes, GArray** arr_outgoing_segments, gchar** errmsg )
{
    gint rc = 0;
    GArray* arr_edges = NULL;

    if ( !arr_outgoing_segments ) return 0; //Sicherheit!

    rc = dbase_full_get_outgoing_edges( dbase_full, entity->ID, &arr_edges, errmsg );
    if ( rc ) ERROR_PAO( "dbase_full_get_outgoing_edges" )

    *arr_outgoing_segments = g_array_new( FALSE, FALSE, sizeof( Segment ) );
    g_array_set_clear_func( *arr_outgoing_segments, zond_database_free_segment );

    for ( gint i = 0; i < arr_edges->len; i++ ) //sonst bleibt *arr_outgo... == NULL!
    {
        Segment segment = { 0 };
        Edge edge = { 0 };

        edge = g_array_index( arr_edges, Edge, i );

        segment.dir = OUTGOING;

        rc = dbase_full_get_entity( dbase_full, edge.ID, &segment.Out.edge, errmsg );
        if ( rc )
        {
            g_array_unref( *arr_outgoing_segments );
            *arr_outgoing_segments = NULL;
            g_array_unref( arr_edges );
            ERROR_PAO( "dbase_full_get_entity" )
        }

        guint match_index = 0;
        if ( g_array_binary_search( arr_linked_nodes, &(edge.object),
                zond_database_cmpint, &match_index ) )
        {
            //wenn object noch nicht erfaßt, dann rekursiv das gleiche...
            segment.Out.object = zond_database_get_node( dbase_full, edge.object,
                    arr_linked_nodes, 1, errmsg );
            if ( !(segment.Out.object ) )
            {
                g_array_unref( *arr_outgoing_segments );
                *arr_outgoing_segments = NULL;
                g_array_unref( arr_edges );
                zond_database_free_segment( &segment );
                ERROR_PAO( "zond_database_get_node" )
            }

            g_array_append_val( arr_linked_nodes, edge.object );
        }
        else
        {
            LinkedNode linked_node = g_array_index( arr_linked_nodes, LinkedNode, match_index );
            segment.Out.object = linked_node.node;
            if ( !(segment.Out.object->arr_outgoing_segs) ) //ist in der anderen Richtung gewesen
            {
                rc = zond_database_get_outgoing_segments( dbase_full,
                        linked_node.node->node, arr_linked_nodes,
                        arr_outgoing_segments, errmsg );
                if ( rc )
                {
                    g_array_unref( *arr_outgoing_segments );
                    *arr_outgoing_segments = NULL;
                    g_array_unref( arr_edges );
                    zond_database_free_segment( &segment );
                    ERROR_PAO( "zond_database_get_outgoing_segments" )
                }
            }
        }

        g_array_append_val( *arr_outgoing_segments, segment );
    }
    g_array_unref( arr_edges );

    return 0;
}


static gint
zond_database_get_incoming_segments( DBaseFull* dbase_full, Entity* entity,
        GArray* arr_linked_nodes, GArray** arr_incoming_segments, gchar** errmsg )
{
    gint rc = 0;
    GArray* arr_edges = NULL;

    if ( !arr_incoming_segments ) return 0; //Sicherheit!

    rc = dbase_full_get_incoming_edges( dbase_full, entity->ID, &arr_edges, errmsg );
    if ( rc ) ERROR_PAO( "dbase_full_get_incoming_edges" )

    *arr_incoming_segments = g_array_new( FALSE, FALSE, sizeof( Segment ) );
    g_array_set_clear_func( *arr_incoming_segments, zond_database_free_segment );

    for ( gint i = 0; i < arr_edges->len; i++ ) //sonst bleibt *arr_outgo... == NULL!
    {
        Segment segment = { 0 };
        Edge edge = { 0 };

        edge = g_array_index( arr_edges, Edge, i );

        segment.dir = INCOMING;

        rc = dbase_full_get_entity( dbase_full, edge.ID, &segment.In.edge, errmsg );
        if ( rc )
        {
            g_array_unref( *arr_incoming_segments );
            *arr_incoming_segments = NULL;
            g_array_unref( arr_edges );
            ERROR_PAO( "dbase_full_get_entity" )
        }

        guint match_index = 0;
        if ( g_array_binary_search( arr_linked_nodes, &(edge.subject),
                zond_database_cmpint, &match_index ) )
        {
            //wenn object noch nicht erfaßt, dann rekursiv das gleiche...
            segment.In.subject = zond_database_get_node( dbase_full, edge.subject,
                    arr_linked_nodes, 1, errmsg );
            if ( !(segment.In.subject ) )
            {
                g_array_unref( *arr_incoming_segments );
                *arr_incoming_segments = NULL;
                g_array_unref( arr_edges );
                zond_database_free_segment( &segment );
                ERROR_PAO( "zond_database_get_node" )
            }

            g_array_append_val( arr_linked_nodes, edge.subject );
        }
        else
        {
            LinkedNode linked_node = g_array_index( arr_linked_nodes, LinkedNode, match_index );
            segment.In.subject = linked_node.node;
            if ( !(segment.In.subject->arr_outgoing_segs) ) //ist in der anderen Richtung gewesen
            {
                rc = zond_database_get_outgoing_segments( dbase_full,
                        linked_node.node->node, arr_linked_nodes,
                        arr_incoming_segments, errmsg );
                if ( rc )
                {
                    g_array_unref( *arr_incoming_segments );
                    *arr_incoming_segments = NULL;
                    g_array_unref( arr_edges );
                    zond_database_free_segment( &segment );
                    ERROR_PAO( "zond_database_get_outgoing_segments" )
                }
            }
        }

        g_array_append_val( *arr_incoming_segments, segment );
    }
    g_array_unref( arr_edges );

    return 0;
}


static void
zond_database_free_node( Node* node )
{
    g_array_unref( node->arr_incoming_segs );
    g_array_unref( node->arr_outgoing_segs );

    g_free( node->node->label );
    g_array_unref( node->node->arr_properties );
    g_free( node->node );

    g_free( node );

    return;
}


/** flags: 1-mit outgoing; 2-mit incoming 3-alles**/
Node*
zond_database_get_node( DBaseFull* dbase_full, gint ID_entity, GArray* arr_linked_nodes, gint flags, gchar** errmsg )
{
    gint rc = 0;
    Node* node = NULL;

    node = g_malloc0( sizeof( Node ) );

    rc = dbase_full_get_entity( dbase_full, ID_entity, &node->node, errmsg );
    if ( rc )
    {
        zond_database_free_node( node );
        ERROR_PAO_R( "dbase_full_get_entity", NULL )
    }

    LinkedNode linked_node = { ID_entity, node };
    g_array_append_val( arr_linked_nodes, linked_node );

    if ( flags & 1 )
    {
        rc = zond_database_get_outgoing_segments( dbase_full, node->node,
                arr_linked_nodes, &node->arr_outgoing_segs, errmsg );
        if ( rc ) ERROR_PAO_R( "zond_database_get_outgoing_segments", NULL )
    }

    if ( flags & 2 )
    {
        rc = zond_database_get_incoming_segments( dbase_full, node->node,
                arr_linked_nodes, &node->arr_incoming_segs, errmsg );
        if ( rc ) ERROR_PAO_R( "zond_database_get_incoming_segments", NULL )
    }

}


gint
zond_database_edit_node( Projekt* zond, gint ID_entity, gchar** errmsg )
{

}

gint
zond_database_insert_anbindung( Projekt* zond, gint node_id, gchar** errmsg )
{
    gint ID_entity = 0;
    gchar* value = 0;
    gint rc = 0;

    rc = dbase_begin( (DBase*) zond->dbase_zond->dbase_work, errmsg );
    if ( rc ) ERROR_PAO( "zond_database_create_savepoint" )

    ID_entity = dbase_full_insert_entity( zond->dbase_zond->dbase_work, 100, errmsg );
    if ( ID_entity == -1 ) ERROR_PAO( "dbase_full_insert_entity" )

    value = g_strdup_printf( "%i", node_id );
    rc = dbase_full_insert_property( zond->dbase_zond->dbase_work, ID_entity, 10010, value, errmsg );
    g_free( value );
    if ( rc ) ERROR_PAO( "dbase_full_insert_property" )


    rc = dbase_commit( (DBase*) zond->dbase_zond->dbase_work, errmsg );
    if ( rc ) ERROR_ROLLBACK( (DBase*) zond->dbase_zond->dbase_work,
            "dbase_commit" )

    //Erzeugte Anbindung verlinken
    rc = zond_database_edit_node( zond, ID_entity, errmsg );
    if ( rc ) ERROR_PAO( "zond_database_edit_node" )

    return 0;
}


