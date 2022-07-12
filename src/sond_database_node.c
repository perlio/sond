/*
sond (sond_database_node.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_database_node.h"

#include "sond_database_entity.h"
#include "sond_database.h"
#include "misc.h"


typedef struct
{
    GtkWidget* box_incoming;
    GtkWidget* sde;
    GtkWidget* box_outgoing;

} SondDatabaseNodePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondDatabaseNode, sond_database_node, GTK_TYPE_BOX )


static void
sond_database_node_class_init( SondDatabaseNodeClass* klass )
{

    return;
}


static void
sond_database_node_init( SondDatabaseNode* self )
{
    SondDatabaseNodePrivate* priv = sond_database_node_get_instance_private( self );

    priv->box_incoming = gtk_box_new( GTK_ORIENTATION_VERTICAL, 0 );
    priv->sde = sond_database_entity_new( );
    priv->box_outgoing = gtk_box_new( GTK_ORIENTATION_VERTICAL, 0 );

    gtk_box_pack_start( GTK_BOX(self), priv->box_incoming, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(self), priv->sde, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(self), priv->box_outgoing, FALSE, FALSE, 0 );

    return;
}


GtkWidget*
sond_database_node_new( void )
{
    SondDatabaseNode* sdn = NULL;

    sdn = g_object_new( SOND_TYPE_DATABASE_NODE, NULL, NULL );
    gtk_orientable_set_orientation( GTK_ORIENTABLE(sdn), GTK_ORIENTATION_HORIZONTAL );
    gtk_box_set_spacing( GTK_BOX(sdn), 15 );

    return GTK_WIDGET(sdn);
}


gint
sond_database_node_load( SondDatabaseNode* sdn, gpointer database, gint ID_entity, gchar** errmsg )
{
    SondDatabaseNodePrivate* priv = NULL;
    gint rc = 0;
    GArray* arr_i_rels = NULL;
    GArray* arr_o_rels = NULL;

    priv = sond_database_node_get_instance_private( sdn );

    rc = sond_database_entity_load( SOND_DATABASE_ENTITY(priv->sde), database, ID_entity, errmsg );
    if ( rc ) ERROR_S

    rc = sond_database_get_incoming_rels( database, ID_entity, &arr_i_rels, errmsg );
    if ( rc ) ERROR_S

    for ( gint i = 0; i < ((arr_i_rels->len <= 10) ? arr_i_rels->len : 10); i++ )
    {
        gint ID_entity_rel = 0;
        gint ID_entity_subject = 0;
        gint rc = 0;
        GtkWidget* sde_rel = NULL;
        GtkWidget* sde_object = NULL;
        GtkWidget* box_segment = NULL;

        box_segment = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 10 );
        gtk_box_pack_start( GTK_BOX(priv->box_incoming), box_segment, FALSE, FALSE, 0 );

        ID_entity_rel = g_array_index( arr_i_rels, gint, i );

        rc = sond_database_get_subject_from_rel( database, ID_entity_rel, errmsg );
        if ( rc == -1 )
        {
            g_array_unref( arr_i_rels );
            ERROR_S
        }
        else ID_entity_subject = rc;

        sde_object = sond_database_entity_load_new( database, ID_entity_subject, errmsg );
        if ( !sde_object )
        {
            g_array_unref( arr_i_rels );
            ERROR_S
        }

        gtk_box_pack_start( GTK_BOX(box_segment), sde_object, FALSE, FALSE, 0 );

        sde_rel = sond_database_entity_load_new( database, ID_entity_rel, errmsg );
        if ( !sde_rel )
        {
            g_array_unref( arr_i_rels );
            ERROR_S
        }

        gtk_box_pack_start( GTK_BOX(box_segment), sde_rel, FALSE, FALSE, 0 );
    }
    g_array_unref( arr_i_rels );

    rc = sond_database_get_outgoing_rels( database, ID_entity, &arr_o_rels, errmsg );
    if ( rc ) ERROR_S

    for ( gint i = 0; i < ((arr_o_rels->len <= 10) ? arr_o_rels->len : 10); i++ )
    {
        gint ID_entity_rel = 0;
        gint ID_entity_object = 0;
        gint rc = 0;
        GtkWidget* sde_rel = NULL;
        GtkWidget* sde_object = NULL;
        GtkWidget* box_segment = NULL;

        box_segment = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 10 );
        gtk_box_pack_start( GTK_BOX(priv->box_outgoing), box_segment, FALSE, FALSE, 0 );

        ID_entity_rel = g_array_index( arr_o_rels, gint, i );

        rc = sond_database_get_object_from_rel( database, ID_entity_rel, errmsg );
        if ( rc == -1 )
        {
            g_array_unref( arr_o_rels );
            ERROR_S
        }
        else ID_entity_object = rc;

        sde_rel = sond_database_entity_load_new( database, ID_entity_rel, errmsg );
        if ( !sde_rel )
        {
            g_array_unref( arr_o_rels );
            ERROR_S
        }

        gtk_box_pack_start( GTK_BOX(box_segment), sde_rel, FALSE, FALSE, 0 );

        sde_object = sond_database_entity_load_new( database, ID_entity_object, errmsg );
        if ( !sde_object )
        {
            g_array_unref( arr_o_rels );
            ERROR_S
        }

        gtk_box_pack_start( GTK_BOX(box_segment), sde_object, FALSE, FALSE, 0 );
    }
    g_array_unref( arr_o_rels );

    //incoming rels
    return 0;
}


GtkWidget*
sond_database_node_load_new( gpointer database, gint ID_entity, gchar** errmsg )
{
    GtkWidget* sdn = NULL;
    gint rc = 0;

    sdn = sond_database_node_new( );

    rc = sond_database_node_load( SOND_DATABASE_NODE(sdn), database, ID_entity, errmsg );
    if ( rc ) ERROR_S_VAL( NULL )

    return sdn;
}

