/*
sond (sond_database_entity.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_database_entity.h"

#include "sond_database_property.h"
#include "sond_database.h"
#include "misc.h"


typedef struct
{
    GtkWidget* box_entity;
    GtkWidget* label_ID;
    GtkWidget* label_label;
    GtkWidget* label_ID_label;
    GtkWidget* box_properties;
} SondDatabaseEntityPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondDatabaseEntity, sond_database_entity, GTK_TYPE_BOX)


static void
sond_database_entity_finalize( GObject* self )
{
    SondDatabaseEntityPrivate* priv = sond_database_entity_get_instance_private( SOND_DATABASE_ENTITY(self) );

    // ...

    G_OBJECT_CLASS(sond_database_entity_parent_class)->finalize( self );

    return;
}


static void
sond_database_entity_class_init( SondDatabaseEntityClass* klass )
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

//    object_class->finalize = zond_dbase_finalize;

    return;
}


static void
sond_database_entity_init( SondDatabaseEntity* self )
{
    SondDatabaseEntityPrivate* priv = NULL;
    GtkWidget* frame_ID = NULL;
    GtkWidget* frame_label = NULL;
    GtkWidget* frame_ID_label = NULL;
    GtkWidget* box_padding = NULL;
    GtkWidget* label_padding = NULL;

    frame_ID = gtk_frame_new( "ID" );
    frame_label = gtk_frame_new( "Label" );
    frame_ID_label = gtk_frame_new( "ID Label" );

    priv = sond_database_entity_get_instance_private( self );

    priv->label_ID = gtk_label_new( "" );
    gtk_container_add( GTK_CONTAINER(frame_ID), priv->label_ID );

    priv->label_label = gtk_label_new( "" );
    gtk_container_add( GTK_CONTAINER(frame_label), priv->label_label );

    priv->label_ID_label = gtk_label_new( "" );
    gtk_container_add( GTK_CONTAINER(frame_ID_label), priv->label_ID_label );

    priv->box_entity = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 2 );
    gtk_box_pack_start( GTK_BOX(priv->box_entity), frame_ID, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(priv->box_entity), frame_label, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(priv->box_entity), frame_ID_label, FALSE, FALSE, 0 );

    gtk_box_pack_start( GTK_BOX(self), priv->box_entity, FALSE, FALSE, 0 );

    label_padding = gtk_label_new( NULL );
    gtk_widget_set_size_request( label_padding, 20, -1 );

    priv->box_properties = gtk_box_new( GTK_ORIENTATION_VERTICAL, 0 );

    box_padding = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 0 );
    gtk_box_pack_start( GTK_BOX(box_padding), label_padding, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(box_padding), priv->box_properties, FALSE, FALSE, 0 );

    gtk_box_pack_start( GTK_BOX(self), box_padding, FALSE, FALSE, 0 );

    return;
}


GtkWidget*
sond_database_entity_new( void )
{
    SondDatabaseEntity* sde = NULL;

    sde = g_object_new( SOND_TYPE_DATABASE_ENTITY, NULL, NULL );
    gtk_orientable_set_orientation( GTK_ORIENTABLE(sde), GTK_ORIENTATION_VERTICAL );

    return GTK_WIDGET(sde);
}


gint
sond_database_entity_load( SondDatabaseEntity* sde, gpointer database, gint ID_entity, gchar** errmsg )
{
    gint rc = 0;
    gint ID_label = 0;
    gchar* label = NULL;
    SondDatabaseEntityPrivate* priv = NULL;
    gchar* label_text = NULL;
    GArray* arr_properties = NULL;

    priv = sond_database_entity_get_instance_private( sde );

    ID_label = sond_database_get_ID_label_for_entity( database, ID_entity, errmsg );
    if ( ID_label == -1 ) ERROR_S

    rc = sond_database_get_label_for_ID_label( database, ID_label, &label, errmsg );
    if ( rc ) ERROR_S

    label_text = g_strdup_printf( "%i", ID_entity );
    gtk_label_set_text( GTK_LABEL(priv->label_ID), label_text );
    g_free( label_text );

    label_text = g_strdup_printf( "%i", ID_label );
    gtk_label_set_text( GTK_LABEL(priv->label_ID_label), label_text );
    g_free( label_text );

    gtk_label_set_text( GTK_LABEL(priv->label_label), label );
    g_free( label );

    rc = sond_database_get_properties( database, ID_entity, &arr_properties, errmsg );
    if ( !arr_properties ) ERROR_S

    for ( gint i = 0; i < arr_properties->len; i++ )
    {
        gint ID_property = 0;
        SondDatabaseProperty* sdp = NULL;

        ID_property = g_array_index( arr_properties, gint, i );
        sdp = sond_database_property_load_new( database, ID_property, errmsg );

        //in sde einfügen
        gtk_box_pack_start( GTK_BOX(priv->box_properties), GTK_WIDGET(sdp), FALSE, FALSE, 0 );
    }

    //width

    g_array_unref( arr_properties );

    return 0;
}


GtkWidget*
sond_database_entity_load_new( gpointer database, gint ID, gchar** errmsg )
{
    GtkWidget* sde = NULL;
    gint rc = 0;

    sde = sond_database_entity_new( );

    rc = sond_database_entity_load( SOND_DATABASE_ENTITY(sde), database, ID, errmsg );
    if ( rc )
    {
        g_object_unref( sde );
        ERROR_S_VAL( NULL )
    }

    return sde;
}


GtkWidget*
sond_database_entity_get_prop_box( SondDatabaseEntity* sde )
{
    SondDatabaseEntityPrivate* priv = sond_database_entity_get_instance_private( sde );

    return priv->box_properties;
}
