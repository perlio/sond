/*
sojus (sojus_adressen.c) - softkanzlei
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


#include <stdlib.h>
#include <gtk/gtk.h>

#include "sojus_init.h"
#include "sojus_database.h"

#include "../misc.h"


typedef struct _Property
{
    gint property_ID;
    GtkWidget* entry_value;
    gboolean dirty;
} Property;


typedef struct _History
{
    Property bemerkung;
    Property von;
    Property bis;
} History;

typedef struct _Property_With_History
{
    Property property;
    History history;
    GtkWidget* button_delete;
} PropertyWithHistory;

typedef struct _Telefonnummer
{
    gint ID_rel;
    gint ID_telefonnummer;
    gint label_telefonnummer;
    gchar* teilnehmernr;
    gint ID_teilnetzvorwahl;
    gchar* teilnetzvorwahl;
    gint ID_laendervorwahl;
    gchar* laendervorwahl;
    History history;
} Telefonnr;


typedef struct _Sitz
{
    gint ID_sitz;
    gint label_sitz;
    gint ID_land;
    gchar* land;
    gint ID_stadt;
    gchar* stadt;
    gint ID_PLZ;
    gchar* PLZ;
    gint ID_strasse;
    gchar* strasse;
    gint ID_hausnr;
    gchar* hausnr;
    gint ID_adresszusatz;
    gchar* adresszusatz;
    GArray* telefonnummern; //Array von Telefonnrn
    GArray* durchwahlen;
    History history;
} Sitz;

typedef struct _Arbeitsplatz
{
    Sitz sitz;
    GArray* Durchwahl; //Properties
    History history;
} Arbeitsplatz;


typedef struct _Property_Box
{
    GtkWidget* list_box;
    GtkWidget* button_neu;
    GArray* arr_properties;
} PropertyBox;


typedef struct _Adresse
{
    Sojus* sojus;

    GtkWidget* adressen_window; //dialog

    GtkWidget* box; //vBox in scrolled_window in content-Teil

    gint ID_subject;
    GtkWidget* combo_subject;
    gboolean dirty;

    PropertyBox namen;

} Adresse;


static void
sojus_adressen_adresse_free( Adresse* adresse )
{
    g_array_unref( adresse->namen.arr_properties );

    g_free( adresse );

    return;
}


static gboolean
sojus_adressen_cb_close_request( GtkWindow* self, GdkEvent* event, gpointer data )
{
    Adresse* adresse = (Adresse*) data;

    sojus_adressen_adresse_free( adresse );

    gtk_widget_destroy( GTK_WIDGET(self) );

    return TRUE;
}


static void
sojus_adressen_cb_entry_adressnr(GtkEntry* entry, gpointer data )
{
    gchar* name = NULL;
    gboolean isnum = TRUE;
    gchar* ptr = NULL;
    gchar* sql = NULL;
    gint rc = 0;
    MYSQL_RES* mysql_res = NULL;
    MYSQL_ROW row = NULL;
    GArray* arr_IDs = NULL;

    Adresse* adresse = (Adresse*) data;

    name = (gchar*) gtk_entry_get_text( entry );

    if ( !g_strcmp0( "", name ) ) return;

    //Test, ob Nummer
    ptr = name;
    for ( gint i = 0; i < strlen( name ); i++ )
    {
        if ( *ptr < '0' || *ptr > '9' )
        {
            isnum = FALSE;
            break;
        }
        ptr++;
    }

    if ( isnum )
    {
        gint ID = 0;

        //Test, ob ID für Subjekt existiert
        //Model mit Liste aller Namen von Subjekten erstellen
        sql = g_strdup_printf(
                "WITH RECURSIVE cte_labels (ID) AS ( "
                    "VALUES (100) "
                    "UNION ALL "
                    "SELECT labels.ID FROM labels JOIN cte_labels "
                        "ON labels.parent = cte_labels.ID "
                ") "
                "SELECT COUNT(entities.ID) FROM "
                    "cte_labels JOIN "
                    "entities "
                    "WHERE "
                        "cte_labels.ID = entities.label AND "
                        "entities.ID = %i "
                    ";", atoi( name ) );
        rc = mysql_query( adresse->sojus->con, sql );
        g_free( sql );
        if ( rc )
        {
            display_message( adresse->sojus->app_window, "Fehler sojus_adressen_cb_entry_adressnr -\n\n",
                    "mysql_query:\n", mysql_error( adresse->sojus->con ), NULL );
            return;
        }

        mysql_res = mysql_store_result( adresse->sojus->con );
        if ( !mysql_res )
        {
            display_message( adresse->sojus->app_window, "Fehler sojus_adressen_cb_entry_adressnr -\n\n",
                    "mysql_store_result:\n", mysql_error( adresse->sojus->con ), NULL );
            return;
        }

        row = mysql_fetch_row( mysql_res );
        if ( row && atoi( row[0] ) == 1 ) ID = atoi( name );
        mysql_free_result( mysql_res );

        if ( ID )
        {
            adresse->sojus->adressnr_akt = ID;
            goto ADRESSEN_LADEN;
        }
    }

    //subjects suchen, die eingegebenen Namen haben
    sql = g_strdup_printf(
            "SELECT e1.ID FROM "
                "rels JOIN "
                "entities AS e1 JOIN "
                "entities AS e2 JOIN "
                "properties "
                "WHERE "
                    "properties.value = '%s' AND "
                    "properties.entity = rels.object AND "
                    "rels.entity = e2.ID AND "
                    "e2.label = 10100 AND "
                    "rels.subject = e1.ID "
                ";", name );

    rc = mysql_query( adresse->sojus->con, sql );
    g_free( sql );
    if ( rc )
    {
        display_message( adresse->sojus->app_window, "Fehler in sojus_adressen_cb_entry_adressnr -\n\n",
                "mysql_query:\n", mysql_error( adresse->sojus->con ), NULL );
        return;
    }

    mysql_res = mysql_store_result( adresse->sojus->con );
    if ( !mysql_res )
    {
        display_message( adresse->sojus->app_window, "Fehler in sojus_adressen_cb_entry_adressnr -\n\n",
                "mysql_store_result:\n", mysql_error( adresse->sojus->con ), NULL );
        return;
    }

    arr_IDs = g_array_new( FALSE, FALSE, sizeof( gint ) );
    while ( (row = mysql_fetch_row( mysql_res )) )
    {
        gint ID = 0;

        ID = atoi( row[0] );
        g_array_append_val( arr_IDs, ID );
    }

    mysql_free_result( mysql_res );

    if ( arr_IDs->len > 1 ) //Fenster, in dem Auswahl der passenden Datensätze ermöglicht wird
    {

    }
    else if ( arr_IDs->len == 0 )
    {
        g_array_unref( arr_IDs );

        return;
    }
    else adresse->sojus->adressnr_akt = g_array_index( arr_IDs, gint, 0 );
    g_array_unref( arr_IDs );

ADRESSEN_LADEN:

    //Person-Datensatz füllen


    return;
}


static gboolean
sojus_adressen_completion_match( GtkEntryCompletion* completion, const gchar* key,
        GtkTreeIter* iter, gpointer data )
{
    GtkTreeModel *model = NULL;
    gchar *item = NULL;
    gchar* item_down = NULL;
    gboolean ans = FALSE;

    model = gtk_entry_completion_get_model( completion );
    gtk_tree_model_get( model, iter, 0, &item, -1 );
    item_down = g_ascii_strdown( item, -1 );
    g_free( item );
    if ( g_strstr_len( item_down, -1, key ) ) ans = TRUE;
    g_free( item_down );

    return ans;
}


static void
sojus_adressen_create_entry_completion( Sojus* sojus, GtkWidget* entry )
{
    GtkEntryCompletion* completion = NULL;
    GtkListStore* list_store = NULL;
    gint rc = 0;
    MYSQL_RES* mysql_res = NULL;
    MYSQL_ROW row = NULL;

    //Model mit Liste aller Namen von Subjekten erstellen
    rc = mysql_query( sojus->con,
            "WITH RECURSIVE cte_labels (ID) AS ( "
                "VALUES (100) "
                "UNION ALL "
                "SELECT labels.ID FROM labels JOIN cte_labels "
                    "ON labels.parent = cte_labels.ID "
            ") "
            "SELECT properties.value FROM "
                "cte_labels JOIN "
                "rels JOIN "
                "entities AS e1 JOIN "
                "entities AS e2 JOIN "
                "properties "
                "WHERE "
                    "cte_labels.ID = e1.label AND "
                    "e1.ID = rels.subject AND "
                    "rels.object = e2.ID AND "
                    "e2.label = 10100 AND "
                    "rels.object = properties.entity "
                    "ORDER BY properties.value "
                ";" );
    if ( rc )
    {
        display_message( sojus->app_window, "Fehler bei Einlesen der Namen -\n\n",
                "mysql_query:\n", mysql_error( sojus->con ), NULL );
        return;
    }

    mysql_res = mysql_store_result( sojus->con );
    if ( !mysql_res )
    {
        display_message( sojus->app_window, "Fehler bei Einlesen der Namen -\n\n",
                "mysql_store_result:\n", mysql_error( sojus->con ), NULL );
        return;
    }

    list_store = gtk_list_store_new( 1, G_TYPE_STRING );

    while ( (row = mysql_fetch_row( mysql_res )) )
            gtk_list_store_insert_with_values( list_store, NULL, -1, 0, row[0], -1 );

    mysql_free_result( mysql_res );

    completion = gtk_entry_completion_new( );
    gtk_entry_completion_set_model( completion, GTK_TREE_MODEL(list_store) );

    gtk_entry_set_completion( GTK_ENTRY(entry), completion );
    gtk_entry_completion_set_text_column( completion, 0 );
    gtk_entry_completion_set_minimum_key_length( completion, 3 );
    gtk_entry_completion_set_match_func( completion, sojus_adressen_completion_match, NULL, NULL );

    return;
}


static void
sojus_adressen_cb_entry_changed( GtkEntryBuffer* entry_buffer, gpointer data )
{
    GtkWidget* button_speichern = NULL;

    PropertyBox* property_box = (PropertyBox*) data;

    button_speichern = gtk_dialog_get_widget_for_response(
            GTK_DIALOG(gtk_widget_get_toplevel( property_box->list_box )),
            GTK_RESPONSE_APPLY );
    gtk_widget_set_sensitive( button_speichern, TRUE );

    return;
}


static void
sojus_adressen_add_property_row( PropertyBox* property_box )
{
    GtkWidget* box = NULL;
    GtkWidget* button_delete = NULL;
    PropertyWithHistory propertywh = { 0, };

    box = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 1 );
    gtk_list_box_insert( GTK_LIST_BOX(property_box->list_box), box, -1 );

    button_delete = gtk_button_new_from_icon_name( "edit-delete", GTK_ICON_SIZE_BUTTON );
    gtk_box_pack_start( GTK_BOX(box), button_delete, FALSE, FALSE, 1 );

    g_object_set_data( G_OBJECT(button_delete), "property-box", property_box );

    propertywh.property.entry_value = gtk_entry_new( );
    propertywh.history.von.entry_value = gtk_entry_new( );
    propertywh.history.bis.entry_value = gtk_entry_new( );
    propertywh.history.bemerkung.entry_value = gtk_entry_new( );

    gtk_box_pack_start( GTK_BOX(box), propertywh.history.von.entry_value, FALSE, TRUE, 1 );
    gtk_box_pack_start( GTK_BOX(box), propertywh.history.bis.entry_value, FALSE, TRUE, 1 );
    gtk_box_pack_start( GTK_BOX(box), propertywh.property.entry_value, TRUE, TRUE, 1 );
    gtk_box_pack_start( GTK_BOX(box), propertywh.history.bemerkung.entry_value, FALSE, TRUE, 1 );

    g_signal_connect( propertywh.property.entry_value, "changed", G_CALLBACK(sojus_adressen_cb_entry_changed), property_box );
    g_signal_connect( propertywh.history.von.entry_value, "changed", G_CALLBACK(sojus_adressen_cb_entry_changed), property_box );
    g_signal_connect( propertywh.history.bis.entry_value, "changed", G_CALLBACK(sojus_adressen_cb_entry_changed), property_box );
    g_signal_connect( propertywh.history.bemerkung.entry_value, "changed", G_CALLBACK(sojus_adressen_cb_entry_changed), property_box );

    gtk_widget_show_all( box );

    g_array_append_val( property_box->arr_properties, propertywh );

    return;
}


static void
sojus_adressen_cb_new_row( GtkWidget* button, gpointer data )
{
    GtkWidget* button_speichern = NULL;

    PropertyBox* property_box = (PropertyBox*) data;

    sojus_adressen_add_property_row( property_box );

    button_speichern = gtk_dialog_get_widget_for_response( GTK_DIALOG(gtk_widget_get_toplevel( button )), GTK_RESPONSE_APPLY );
    gtk_widget_set_sensitive( button_speichern, TRUE );

    return;
}


static void
sojus_adressen_add_list_box( GtkWidget* box, PropertyBox* property_box, const gchar* title )
{
    GtkWidget* frame = NULL;
    GtkWidget* swindow = NULL;

    property_box->arr_properties = g_array_new( FALSE, FALSE, sizeof( PropertyWithHistory ) );

    property_box->list_box = gtk_list_box_new( );
    swindow = gtk_scrolled_window_new( NULL, NULL );
    gtk_container_add( GTK_CONTAINER(swindow), property_box->list_box );
    frame = gtk_frame_new( title );
    gtk_container_add( GTK_CONTAINER(frame), swindow );

    property_box->button_neu = gtk_button_new_with_label( "Neue Zeile" );
    gtk_list_box_insert( GTK_LIST_BOX(property_box->list_box), property_box->button_neu, -1 );
    gtk_widget_set_halign( property_box->button_neu, GTK_ALIGN_START );

    g_signal_connect( property_box->button_neu, "clicked", G_CALLBACK(sojus_adressen_cb_new_row), property_box );
    gtk_box_pack_start( GTK_BOX(box), frame, TRUE, TRUE, 1 );

    gtk_widget_show_all( frame );

    return;
}


static void
sojus_adressen_neue_adresse( Adresse* adresse )
{
    GtkWidget* headerbar = NULL;
    GList* children = NULL;

    headerbar = gtk_window_get_titlebar( GTK_WINDOW(adresse->adressen_window) );
    children = gtk_container_get_children( GTK_CONTAINER(headerbar) );
    gtk_entry_set_text( GTK_ENTRY(gtk_bin_get_child(children->data)), "-neu-" );
    gtk_widget_set_sensitive( GTK_WIDGET(children->data), FALSE );

    gtk_widget_set_sensitive( GTK_WIDGET(children->next->data), FALSE );

    g_list_free( children );

    gtk_widget_set_sensitive( adresse->box, TRUE );

    gtk_widget_set_sensitive( gtk_dialog_get_widget_for_response( GTK_DIALOG(adresse->adressen_window), GTK_RESPONSE_REJECT ), TRUE );
    gtk_widget_set_sensitive( gtk_dialog_get_widget_for_response( GTK_DIALOG(adresse->adressen_window), GTK_RESPONSE_APPLY ), FALSE );

    return;
}


static void
sojus_adressen_cb_neue_adresse( GtkWidget* button, gpointer data )
{
    sojus_adressen_neue_adresse( (Adresse*) data );

    return;
}


static gint
sojus_adressen_get_number_actual_properties( PropertyBox* property_box, gchar** errmsg )
{


    return 1;
}


static gint
sojus_adressen_speichern_property( MYSQL* con, gint ID_subject,
        Property* property, gint label_property, gint label_value, gchar** errmsg )
{
    gchar* value = NULL;
    value = (gchar*) gtk_entry_get_text( GTK_ENTRY(property->entry_value) );

    if ( !property->dirty )
    {
        if ( property->property_ID ) //property ist schon gespeichert
        {
            if ( !g_strcmp0( value, "" ) ) //ist leer
            {
                gint rc = 0;

                rc = sojus_database_delete_property( con, property->property_ID, errmsg );
                if ( rc ) ERROR_SOND( "sojus_database_delete_property" )

                return 1; //history-qualifier werden mitgelöscht
            }
            else //steht was drin
            {
                gint rc = 0;

                rc = sojus_database_update_property( con, property->property_ID, value, errmsg );
                if ( rc ) ERROR_SOND( "sojus_database_update_property" )
            }
        }
        else if ( g_strcmp0( value, "" ) )
        {
            gint rc = 0;

            rc = sojus_database_insert_property( con, ID_subject, label_property, label_value, value, errmsg );
            if ( rc ) ERROR_SOND( "sojus_database_insert_property" )
        }
    }

    return 0;
}


static gint
sojus_adressen_speichern_property_box( MYSQL* con, gint ID_subject, PropertyBox* property_box, gchar** errmsg )
{
    for ( gint i = 0; i < property_box->arr_properties->len; i++ )
    {
        gint rc = 0;
        PropertyWithHistory propertywh = { 0, };

        propertywh = g_array_index( property_box->arr_properties, PropertyWithHistory, i );

        rc = sojus_adressen_speichern_property( con, ID_subject,
                &propertywh.property, 10100, 100001, errmsg );
        if ( rc == -1 ) ERROR_SOND( "sojus_adressen_speichern_property" )
        else if ( rc == 1 ) continue; //gelöscht - mit qualifiern

        rc = sojus_adressen_speichern_property( con,
                propertywh.property.property_ID, &propertywh.history.von, 10030,
                100004, errmsg );
        if ( rc ) ERROR_SOND( "sojus_adressen_speichern_property" )

        rc = sojus_adressen_speichern_property( con,
                propertywh.property.property_ID, &propertywh.history.von, 10040,
                100004, errmsg );
        if ( rc ) ERROR_SOND( "sojus_adressen_speichern_property" )

        rc = sojus_adressen_speichern_property( con,
                propertywh.property.property_ID, &propertywh.history.bemerkung,
                10050, 100001, errmsg );
        if ( rc ) ERROR_SOND( "sojus_adressen_speichern_property" )
    }

    return 0;
}


static gint
sojus_adressen_speichern( Adresse* adresse, gchar** errmsg )
{
    gint ret = 0;

    //neues subject anlegen?
    if ( !adresse->ID_subject )
    {
        GtkTreeIter iter = { 0 };
        GtkTreeModel* model = NULL;
        gint label_subject = 0;
        gchar* entry_text = NULL;
        GtkWidget* headerbar = NULL;
        GList* children = NULL;


        //iter abfragen
        if ( !gtk_combo_box_get_active_iter( GTK_COMBO_BOX(adresse->combo_subject), &iter ) )
        {
            if ( errmsg ) *errmsg = g_strdup( "Bei Aufruf gtk_combo_box_get_active_iter:\n"
                    "iter konnte nicht gesetzt werden" );
            return -1;
        }

        model = gtk_combo_box_get_model( GTK_COMBO_BOX(adresse->combo_subject) );
        gtk_tree_model_get( model, &iter, 0, &label_subject, -1 );

        ret = sojus_adressen_insert_node( adresse->sojus->con, label_subject, errmsg );
        if ( ret == -1 ) ERROR_SOND( "sojus_adressen_insert_noder" )
        else adresse->ID_subject = ret;

        entry_text = g_strdup_printf( "%i", adresse->ID_subject );
        headerbar = gtk_window_get_titlebar( GTK_WINDOW(adresse->adressen_window) );
        children = gtk_container_get_children( GTK_CONTAINER(headerbar) );
        gtk_entry_set_text( GTK_ENTRY(gtk_bin_get_child(children->data)), entry_text );
        g_list_free( children );
        g_free( entry_text );
    }

    ret = sojus_adressen_get_number_actual_properties( &adresse->namen, errmsg );
    if ( ret == -1 ) ERROR_SOND( "sojus_adressen_get_number_actual_properties" )
    else if ( ret != 1 ) return 1;

    ret = sojus_adressen_speichern_property_box( adresse->sojus->con,
            adresse->ID_subject, &adresse->namen, errmsg );
    if ( ret ) ERROR_SOND( "sojus_adressen_speichern_property_box" )


    return 0;
}


static gint
sojus_adressen_get_label_children( Adresse* adresse, gint label,
        GtkTreeIter* iter, GtkTreeStore* tree_store, gchar** errmsg )
{
    gint rc = 0;
    GtkTreeIter iter_new;
    GArray* arr_children;
    MYSQL_RES* mysql_res = NULL;
    MYSQL_ROW row = NULL;
    gchar* sql = NULL;

    sql = g_strdup_printf( "SELECT labels.label FROM labels WHERE labels.ID = %i;", label );
    rc = mysql_query( adresse->sojus->con, sql );
    g_free( sql );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf mysql_query:\n",
                mysql_error( adresse->sojus->con ), NULL );

        return -1;
    }

    mysql_res = mysql_store_result( adresse->sojus->con );
    if ( !mysql_res )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf mysql_store_result:\n",
                mysql_error( adresse->sojus->con ), NULL );

        return -1;
    }

    while ( (row = mysql_fetch_row( mysql_res )) )
    {
        gtk_tree_store_insert( tree_store, &iter_new, iter, -1 );
        gtk_tree_store_set( tree_store, &iter_new, 0, label, 1, row[0], -1 );
    }

    mysql_free_result( mysql_res );
    mysql_res = NULL;

    //Kinder von label
    arr_children = g_array_new( FALSE, FALSE, sizeof( gint ) );

    sql = g_strdup_printf( "SELECT labels.ID FROM labels WHERE labels.parent = %i;", label );
    rc = mysql_query( adresse->sojus->con, sql );
    g_free( sql );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf mysql_query:\n",
                mysql_error( adresse->sojus->con ), NULL );
        g_array_unref( arr_children );

        return -1;
    }

    mysql_res = mysql_store_result( adresse->sojus->con );
    if ( !mysql_res )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf mysql_store_result:\n",
                mysql_error( adresse->sojus->con ), NULL );
        g_array_unref( arr_children );

        return -1;
    }

    while ( (row = mysql_fetch_row( mysql_res )) )
    {
        gint label_child = 0;

        label_child = atoi( row[0] );
        g_array_append_val( arr_children, label_child );
    }

    mysql_free_result( mysql_res );

    //Schleife über alle Kinder:
    for ( gint i = 0; i < arr_children->len; i++ )
    {
        rc = sojus_adressen_get_label_children( adresse,
                g_array_index( arr_children, gint, i ), &iter_new, tree_store, errmsg );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sojus_adressen_get_label:\n",
                    errmsg, NULL );
            g_free( errmsg );
            g_array_unref( arr_children );

            return -1;
        }
    }

    g_array_unref( arr_children );

    return 0;
}


static GtkWidget*
sojus_adressen_create_combo_subjects( Adresse* adresse, gchar** errmsg )
{
    gint rc = 0;
    GtkTreeStore* tree_store = NULL;
    GtkWidget* combo = NULL;
    GtkCellRenderer* column = NULL;

    tree_store = gtk_tree_store_new( 2, G_TYPE_INT, G_TYPE_STRING );
    rc = sojus_adressen_get_label_children( adresse, 100, NULL, tree_store, errmsg );
    if ( rc )
    {
        if ( errmsg ) *errmsg = add_string( g_strdup( "Bei Aufruf sojus_adressen_get_label_children:\n" ),
                *errmsg );

        return NULL;
    }

    combo = gtk_combo_box_new_with_model( GTK_TREE_MODEL(tree_store) );
    g_object_unref( tree_store );
    column = gtk_cell_renderer_text_new( );
    gtk_cell_layout_pack_start( GTK_CELL_LAYOUT(combo), column, TRUE );
    gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo), column, "text", 1,
                                   NULL);
    gtk_combo_box_set_active( GTK_COMBO_BOX(combo), 0 );

    return combo;
}


static gint
sojus_adressen_create_box( Adresse* adresse, gchar** errmsg )
{
    GtkWidget* content = NULL;
    GtkWidget* swindow = NULL;

    //combobox für subject-Art
    adresse->combo_subject =  sojus_adressen_create_combo_subjects( adresse, errmsg );
    if ( !adresse->combo_subject )
    {
        if ( errmsg ) *errmsg = add_string( g_strdup( "Bei Aufruf "
                "sojus_adressen_create_combo_subjects:\n" ), *errmsg );

        return -1;
    }

    content = gtk_dialog_get_content_area( GTK_DIALOG(adresse->adressen_window) );
    swindow = gtk_scrolled_window_new( NULL, NULL );
    gtk_box_pack_start( GTK_BOX(content), swindow, TRUE, TRUE, 1 );

    adresse->box = gtk_box_new( GTK_ORIENTATION_VERTICAL, 0 );
    gtk_container_add( GTK_CONTAINER(swindow), adresse->box );

    gtk_box_pack_start( GTK_BOX(adresse->box), adresse->combo_subject, FALSE, FALSE, 1 );
    gtk_widget_set_halign( adresse->combo_subject, GTK_ALIGN_START );

    gtk_widget_show_all( adresse->adressen_window );
    gtk_widget_set_sensitive( adresse->box, FALSE );

    sojus_adressen_add_list_box( adresse->box,&adresse->namen, "Name" );
    sojus_adressen_add_property_row( &adresse->namen );

    return 0;
}


static void
sojus_adressen_cb_bu_neu( GtkWidget* button, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    GtkWidget* button_speichern = NULL;
    GtkWidget* headerbar = NULL;
    GList* children = NULL;

    Adresse* adresse = (Adresse*) data;

    button_speichern = gtk_dialog_get_widget_for_response( GTK_DIALOG(adresse->adressen_window), GTK_RESPONSE_APPLY );
    if ( gtk_widget_get_sensitive( button_speichern ) )
    {
        gint ret = 0;

        ret = abfrage_frage( adresse->adressen_window, "Geändert", "Änderungen "
                "speichern?", NULL );
        if ( ret == GTK_RESPONSE_CANCEL ) return;
        else if ( ret == GTK_RESPONSE_YES )
        {
            gint rc = 0;
            gchar* errmsg = NULL;

            rc = sojus_adressen_speichern( adresse, &errmsg );
            if ( rc == 1 )
            {
                display_message( adresse->adressen_window, "Genau ein Name muß "
                        "aktuell sein\nBitte korrigieren", NULL );
                return;
            }
            if ( rc == -1 )
            {
                gint ret = 0;
                gchar* message = NULL;

                message = g_strconcat( "Fehler beim Speichern -\n\nBei Aufruf sojus_adressen_speichern:\n", errmsg, NULL );
                g_free( errmsg );
                ret = abfrage_frage( adresse->adressen_window, message, "Trotzdem neue Adresse?", NULL );
                g_free( message );
                if ( ret == GTK_RESPONSE_CANCEL || ret == GTK_RESPONSE_NO ) return;
            }
        }
    }

    gtk_widget_destroy( gtk_widget_get_parent( gtk_widget_get_parent( adresse->box ) ) );
    rc = sojus_adressen_create_box( adresse, &errmsg );
    if ( rc == 1 )
    {
        display_message( adresse->adressen_window, "Genau ein Name muß "
                "aktuell sein\nBitte korrigieren", NULL );

        return;
    }
    if ( rc == -1 )
    {
        gboolean ret = FALSE;

        display_message( adresse->adressen_window, "Fehler -\n\nBei Aufruf "
                "sojus_adressen_create_box:\n", errmsg, NULL );
        g_free( errmsg );

        g_signal_emit_by_name( adresse->adressen_window, "delete-event", adresse, &ret );

        return;
    }

    headerbar = gtk_window_get_titlebar( GTK_WINDOW(adresse->adressen_window) );
    children = gtk_container_get_children( GTK_CONTAINER(headerbar) );
    gtk_entry_set_text( GTK_ENTRY(gtk_bin_get_child(children->data)), "" );
    gtk_widget_set_sensitive( GTK_WIDGET(children->data), TRUE );

    gtk_widget_set_sensitive( GTK_WIDGET(children->next->data), TRUE );

    g_list_free( children );

    return;
}


static void
sojus_adressen_cb_bu_speichern( GtkWidget* button, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    Adresse* adresse = (Adresse*) data;

    rc = sojus_adressen_speichern( adresse, &errmsg );
    if ( rc == 1 )
    {
        display_message( adresse->adressen_window, "Genau ein Name muß "
                "aktuell sein\nBitte korrigieren", NULL );

        return;
    }
    if ( rc == -1 )
    {
        display_message( adresse->adressen_window, "Fehler beim Speichern -\n\n"
                "Bei Aufruf sojus_adressen_speichern:\n", errmsg, NULL );
        g_free( errmsg );

        return;
    }

    gtk_widget_set_sensitive( gtk_dialog_get_widget_for_response( GTK_DIALOG(adresse->adressen_window), GTK_RESPONSE_APPLY ), FALSE );

    return;
}


static void
sojus_adressen_cb_bu_speichern_verlassen( GtkWidget* button, gpointer data )
{
    GtkWidget* button_speichern = NULL;
    gboolean ret = FALSE;

    Adresse* adresse = (Adresse*) data;

    button_speichern = gtk_dialog_get_widget_for_response( GTK_DIALOG(adresse->adressen_window), GTK_RESPONSE_APPLY );
    if ( gtk_widget_get_sensitive( button_speichern ) )
    {
        gint rc = 0;
        gchar* errmsg = NULL;

        rc = sojus_adressen_speichern( adresse, &errmsg );
        if ( rc )
        {
            gint ret = 0;
            gchar* message = NULL;

            message = g_strconcat( "Fehler beim Speichern -\n\nBei Aufruf sojus_adressen_speichern:\n", errmsg, NULL );
            g_free( errmsg );
            ret = abfrage_frage( adresse->adressen_window, message, "Trotzdem neue Adresse?", NULL );
            g_free( message );
            if ( ret == GTK_RESPONSE_CANCEL || ret == GTK_RESPONSE_NO ) return;
        }
    }

    g_signal_emit_by_name( adresse->adressen_window, "delete-event", NULL, &ret );

    return;
}


static void
sojus_adressen_cb_bu_abbrechen( GtkWidget* button, gpointer data )
{
    gboolean ret = FALSE;

    Adresse* adresse = (Adresse*) data;

    g_signal_emit_by_name( adresse->adressen_window, "delete-event", adresse, &ret );

    return;
}

void
sojus_adressen_cb_fenster( GtkButton* button, gpointer data )
{
    gint rc = 0;
    Adresse* adresse = NULL;
    GtkWidget* button_apply = NULL;
    GtkWidget* button_change_adress = NULL;
    gchar* errmsg = NULL;

    adresse = g_malloc0( sizeof( Adresse ) );

    adresse->sojus = (Sojus*) data;

    adresse->adressen_window = gtk_dialog_new_with_buttons( "Adressen",
            GTK_WINDOW(adresse->sojus->app_window), GTK_DIALOG_DESTROY_WITH_PARENT,
            "Andere Adresse", GTK_RESPONSE_REJECT, "Speichern",
            GTK_RESPONSE_APPLY, "Ok", GTK_RESPONSE_OK, "Abbrechen", GTK_RESPONSE_CANCEL, NULL );
    gtk_window_set_default_size( GTK_WINDOW(adresse->adressen_window), 1200, 700 );

    GtkWidget* adressen_headerbar = gtk_header_bar_new( );
    gtk_header_bar_set_show_close_button( GTK_HEADER_BAR(adressen_headerbar), TRUE );
    gtk_header_bar_set_title( GTK_HEADER_BAR(adressen_headerbar), "Adressen" );
    gtk_window_set_titlebar( GTK_WINDOW(adresse->adressen_window), adressen_headerbar );

    button_apply = gtk_dialog_get_widget_for_response( GTK_DIALOG( adresse->adressen_window ),
            GTK_RESPONSE_APPLY );
    gtk_widget_set_sensitive( button_apply, FALSE );
    button_change_adress = gtk_dialog_get_widget_for_response( GTK_DIALOG( adresse->adressen_window ),
            GTK_RESPONSE_REJECT );
    gtk_widget_set_sensitive( button_change_adress, FALSE );

    //Adressnr
    GtkWidget* frame_adressnr = gtk_frame_new( "Adressnr." );
    GtkWidget* entry_adressnr = gtk_entry_new( );
    gtk_container_add( GTK_CONTAINER(frame_adressnr), entry_adressnr );
    gtk_header_bar_pack_start( GTK_HEADER_BAR(adressen_headerbar), frame_adressnr );

    //Button Neue Adresse
    GtkWidget* button_neue_adresse = gtk_button_new_with_label( "Neue Adresse" );
    gtk_header_bar_pack_start( GTK_HEADER_BAR(adressen_headerbar), button_neue_adresse );

    if ( adresse->sojus->adressnr_akt )
    {
        gchar* text_adressnr = g_strdup_printf( "%i", adresse->sojus->adressnr_akt );
        gtk_entry_set_text( GTK_ENTRY(entry_adressnr), text_adressnr );
        g_free( text_adressnr );
    }

    //entry-completion fettig machen
    sojus_adressen_create_entry_completion( adresse->sojus, entry_adressnr );

    g_signal_connect( G_OBJECT(entry_adressnr), "activate",
            G_CALLBACK(sojus_adressen_cb_entry_adressnr), adresse );
    g_signal_connect( button_neue_adresse, "clicked", G_CALLBACK(sojus_adressen_cb_neue_adresse), adresse );

    rc = sojus_adressen_create_box( adresse, &errmsg );
    if ( rc )
    {
        gboolean ret = FALSE;

        display_message( adresse->adressen_window, "Fehler -\n\nBei Aufruf "
                "sojus_adressen_create_box:\n", errmsg, NULL );
        g_free( errmsg );

        g_signal_emit_by_name( adresse->adressen_window, "delete-event", adresse, &ret );

        return;
    }

    //Signale
    g_signal_connect( gtk_dialog_get_widget_for_response(
            GTK_DIALOG(adresse->adressen_window), GTK_RESPONSE_REJECT),
            "clicked", G_CALLBACK(sojus_adressen_cb_bu_neu), adresse );
    g_signal_connect( gtk_dialog_get_widget_for_response(
            GTK_DIALOG(adresse->adressen_window), GTK_RESPONSE_APPLY),
            "clicked", G_CALLBACK(sojus_adressen_cb_bu_speichern), adresse );
    g_signal_connect( gtk_dialog_get_widget_for_response(
            GTK_DIALOG(adresse->adressen_window), GTK_RESPONSE_OK),
            "clicked", G_CALLBACK(sojus_adressen_cb_bu_speichern_verlassen), adresse );
    g_signal_connect( gtk_dialog_get_widget_for_response(
            GTK_DIALOG(adresse->adressen_window), GTK_RESPONSE_CANCEL),
            "clicked", G_CALLBACK(sojus_adressen_cb_bu_abbrechen), adresse );

    g_signal_connect( G_OBJECT(adresse->adressen_window), "delete-event",
            G_CALLBACK(sojus_adressen_cb_close_request), adresse );

    return;
}
