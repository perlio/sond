/*
zond (dbase_full.c) - Akten, Beweisstücke, Unterlagen
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

#include <glib.h>
#include <sqlite3.h>

#include "../global_types.h"
#include "../error.h"

#include "../../misc.h"
#include "../../dbase.h"

#include "dbase_full.h"


gint
dbase_full_insert_node( DBaseFull* dbase_full, Baum baum, gint node_id, gboolean child,
        const gchar* icon_name, const gchar* node_text, gchar** errmsg )
{
    gint rc = 0;
    gint new_node_id = 0;

    for ( gint i = 0; i < 5; i++ ) sqlite3_reset( dbase_full->insert_node[i] );

    rc = sqlite3_bind_int( dbase_full->insert_node[0 + (gint) baum], 1, child );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (child)" )

    rc = sqlite3_bind_int( dbase_full->insert_node[0 + (gint) baum], 2, node_id );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_bind_text( dbase_full->insert_node[0 + (gint) baum], 3,
            icon_name, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (icon_name)" )

    rc = sqlite3_bind_text( dbase_full->insert_node[0 + (gint) baum], 4, node_text,
            -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_text (node_text)" )

    rc = sqlite3_step( dbase_full->insert_node[0 + (gint) baum] );
    if ( rc != SQLITE_DONE ) ERROR_DBASE_FULL( "sqlite3_step [0/1]" )

    rc = sqlite3_step( dbase_full->insert_node[2 + (gint) baum] );
    if ( rc != SQLITE_DONE ) ERROR_DBASE_FULL( "sqlite3_step ([2/3])" )

    rc = sqlite3_step( dbase_full->insert_node[4] );
    if ( rc != SQLITE_ROW ) ERROR_DBASE_FULL( "sqlite3_step ([4])" )

    new_node_id = sqlite3_column_int( dbase_full->insert_node[4], 0 );

    return new_node_id;
}


/** Rückgabe 0 auch wenn Knoten nicht existiert **/
gint
dbase_full_set_node_text( DBaseFull* dbase_full, Baum baum, gint node_id,
        const gchar* node_text, gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( dbase_full->set_node_text[0] );
    sqlite3_reset( dbase_full->set_node_text[1] );

    rc = sqlite3_bind_text( dbase_full->set_node_text[0 + (gint) baum], 1,
            node_text, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_text (text)" )

    rc = sqlite3_bind_int( dbase_full->set_node_text[0 + (gint) baum], 2, node_id );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step(dbase_full->set_node_text[0 + (gint) baum] );
    if ( rc != SQLITE_DONE ) ERROR_DBASE_FULL( "sqlite3_step" )

    return 0;
}


/** gibt auch dann 0 zurück, wenn der Knoten gar nicht existiert   **/
gint
dbase_full_set_icon_id( DBaseFull* dbase_full, Baum baum, gint node_id, const gchar* icon_name,
        gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( dbase_full->set_icon_id[0] );
    sqlite3_reset( dbase_full->set_icon_id[1] );

    rc = sqlite3_bind_text( dbase_full->set_icon_id[0 + (gint) baum], 1,
            icon_name, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_text (icon_name)" )

    rc = sqlite3_bind_int( dbase_full->set_icon_id[0 + (gint) baum], 2, node_id );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( dbase_full->set_icon_id[0 + (gint) baum] );
    if ( rc != SQLITE_DONE ) ERROR_DBASE_FULL( "sqlite3_step" )

    return 0;
}


gint
dbase_full_insert_entity( DBaseFull* dbase_full, gint label, gchar** errmsg )
{
    gint rc = 0;
    gint new_node_id = 0;

    for ( gint i = 41; i < 42; i++ ) sqlite3_reset( dbase_full->stmts[i] );

    rc = sqlite3_bind_int( dbase_full->stmts[41], 1, label );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (label)" )

    rc = sqlite3_step( dbase_full->stmts[41] );
    if ( rc != SQLITE_DONE ) ERROR_DBASE_FULL( "sqlite3_step (insert)" )

    rc = sqlite3_step( dbase_full->stmts[42] );
    if ( rc != SQLITE_ROW ) ERROR_DBASE_FULL( "sqlite3_step (get last inserted rowid)" )

    new_node_id = sqlite3_column_int( dbase_full->stmts[42], 0 );

    return new_node_id;
}


gint
dbase_full_insert_property( DBaseFull* dbase_full, gint ID_entity, gint label,
        gchar* value, gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( dbase_full->stmts[43] );

    rc = sqlite3_bind_int( dbase_full->stmts[43], 1, ID_entity );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (ID_entity)" )

    rc = sqlite3_bind_int( dbase_full->stmts[43], 2, label );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (label)" )

    rc = sqlite3_bind_text( dbase_full->stmts[43], 3, value, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (value)" )

    rc = sqlite3_step( dbase_full->stmts[43] );
    if ( rc != SQLITE_DONE ) ERROR_DBASE_FULL( "sqlite3_step" )

    return 0;
}


gint
dbase_full_get_label_entity( DBaseFull* dbase_full, gint ID_entity, gchar** label, gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( dbase_full->stmts[44] );

    rc = sqlite3_bind_int( dbase_full->stmts[44], 1, ID_entity );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (ID_entity)" )

    rc = sqlite3_step( dbase_full->stmts[44] );
    if ( rc != SQLITE_ROW ) ERROR_DBASE_FULL( "sqlite3_step" )

    if ( label ) *label =
            g_strdup( (const gchar*) sqlite3_column_text( dbase_full->stmts[44], 0 ) );

    return 0;
}


static void
dbase_full_clear_property( gpointer data )
{
    Property* property = (Property*) data;

    g_free( property->label );
    g_free( property->value );

    return;
}


gint
dbase_full_get_properties( DBaseFull* dbase_full, gint ID_entity,
        GArray** arr_properties, gchar** errmsg )
{
    gint rc = 0;

    if ( !arr_properties ) return 0;

    *arr_properties = g_array_new( FALSE, FALSE, sizeof( Property ) );
    g_array_set_clear_func( *arr_properties, dbase_full_clear_property );

    sqlite3_reset( dbase_full->stmts[45] );

    rc = sqlite3_bind_int( dbase_full->stmts[45], 1, ID_entity );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (ID_entity)" )

    do
    {
        Property property = { 0 };

        rc = sqlite3_step( dbase_full->stmts[45] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
        {
            g_array_unref( *arr_properties );
            ERROR_DBASE_FULL( "sqlite3_step" )
        }
        else if ( rc == SQLITE_ROW )
        {
            property.ID = sqlite3_column_int( dbase_full->stmts[45], 0 );
            property.label = g_strdup( (const gchar*) sqlite3_column_text( dbase_full->stmts[45], 1 ) );
            property.value = g_strdup( (const gchar*) sqlite3_column_text( dbase_full->stmts[45], 2 ) );

            g_array_append_val( *arr_properties, property );
        }
    } while ( rc == SQLITE_ROW );

    return 0;
}


static void
dbase_full_clear_edge( gpointer data )
{
    Edge* edge = (Edge*) data;

    g_free( edge->label_edge );

    g_array_unref( edge->arr_properties );

    return;
}


gint
dbase_full_get_outgoing_edges( DBaseFull* dbase_full, gint ID_entity, GArray** arr_edges,
        gchar** errmsg )
{
    gint rc = 0;

    if ( !arr_edges ) return 0;

    *arr_edges = g_array_new( FALSE, FALSE, sizeof( Edge ) );
    g_array_set_clear_func( *arr_edges, dbase_full_clear_edge );

    sqlite3_reset( dbase_full->stmts[46] );

    rc = sqlite3_bind_int( dbase_full->stmts[46], 1, ID_entity );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (ID_entity)" )

    do
    {
        Edge edge = { 0 };

        rc = sqlite3_step( dbase_full->stmts[46] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
        {
            g_array_unref( *arr_edges );
            ERROR_DBASE_FULL( "sqlite3_step" )
        }
        else if ( rc == SQLITE_ROW )
        {
            edge.ID_subject = ID_entity;
            edge.ID_edge = sqlite3_column_int( dbase_full->stmts[46], 1 );
            edge.label_edge = g_strdup( (const gchar*) sqlite3_column_text( dbase_full->stmts[46], 2 ) );
            edge.ID_object = sqlite3_column_int( dbase_full->stmts[46], 3 );

            rc = dbase_full_get_properties( dbase_full, edge.ID_edge, &(edge.arr_properties), errmsg );
            if ( rc )
            {
                g_free( edge.label_edge );
                g_array_unref( edge.arr_properties );
                g_array_unref( *arr_edges );
                ERROR_SOND( "dbase_full_get_properties" )
            }

            g_array_append_val( *arr_edges, edge );
        }
    } while ( rc == SQLITE_ROW );

    return 0;
}


gint
dbase_full_get_label_text( DBaseFull* dbase_full, gint ID_label, gchar** label_text, gchar** errmsg )
{
    gint rc = 0;

    if ( !label_text ) return 0;

    sqlite3_reset( dbase_full->stmts[47] );

    rc = sqlite3_bind_int( dbase_full->stmts[47], 1, ID_label );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (ID_label)" )

    rc = sqlite3_step( dbase_full->stmts[47] );
    if ( rc != SQLITE_ROW ) ERROR_DBASE_FULL( "sqlite3_step" )

    *label_text = g_strdup( (const gchar*) sqlite3_column_text( dbase_full->stmts[47], 0 ) );

    return 0;
}


//Array von Kindern von label; nur ID (gint)
gint
dbase_full_get_array_children_label( DBaseFull* dbase_full, gint label,
        GArray** arr_children, gchar** errmsg )
{
    gint rc = 0;

    if ( !arr_children ) return 0;

    *arr_children = g_array_new( FALSE, FALSE, sizeof( gint ) );

    sqlite3_reset( dbase_full->stmts[48] );

    rc = sqlite3_bind_int( dbase_full->stmts[48], 1, label );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (label)" )

    do
    {
        gint child = 0;

        rc = sqlite3_step( dbase_full->stmts[48] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
        {
            g_array_unref( *arr_children );
            ERROR_DBASE_FULL( "sqlite3_step" )
        }
        else if ( rc == SQLITE_ROW )
        {
            child = sqlite3_column_int( dbase_full->stmts[48], 0 );
            g_array_append_val( *arr_children, child );
        }
    } while ( rc == SQLITE_ROW );

    return 0;
}


//Array von nodes mit dem label "nomen" oder eines Kindes von "nomen"; nur ID (gint)
gint
dbase_full_get_array_nodes( DBaseFull* dbase_full, gint nomen, GArray** arr_nodes,
        gchar** errmsg )
{
    gint rc = 0;

    if ( !arr_nodes ) return 0;

    *arr_nodes = g_array_new( FALSE, FALSE, sizeof( gint ) );

    sqlite3_reset( dbase_full->stmts[49] );

    rc = sqlite3_bind_int( dbase_full->stmts[49], 1, nomen );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (nomen)" )

    do
    {
        gint ID_entity = 0;

        rc = sqlite3_step( dbase_full->stmts[49] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
        {
            g_array_unref( *arr_nodes );
            ERROR_DBASE_FULL( "sqlite3_step" )
        }
        else if ( rc == SQLITE_ROW )
        {
            ID_entity = sqlite3_column_int( dbase_full->stmts[49], 0 );
            g_array_append_val( *arr_nodes, ID_entity );
        }
    } while ( rc == SQLITE_ROW );

    return 0;
}


gint
dbase_full_get_incoming_edges( DBaseFull* dbase_full, gint ID_entity, GArray** arr_edges,
        gchar** errmsg )
{
    gint rc = 0;

    if ( !arr_edges ) return 0;

    *arr_edges = g_array_new( FALSE, FALSE, sizeof( Edge ) );
    g_array_set_clear_func( *arr_edges, dbase_full_clear_edge );

    sqlite3_reset( dbase_full->stmts[50] );

    rc = sqlite3_bind_int( dbase_full->stmts[50], 1, ID_entity );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (ID_entity)" )

    do
    {
        Edge edge = { 0 };

        rc = sqlite3_step( dbase_full->stmts[50] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
        {
            g_array_unref( *arr_edges );
            ERROR_DBASE_FULL( "sqlite3_step" )
        }
        else if ( rc == SQLITE_ROW )
        {
            edge.ID_subject = sqlite3_column_int( dbase_full->stmts[50], 0 );
            edge.ID_edge = sqlite3_column_int( dbase_full->stmts[50], 1 );
            edge.label_edge = g_strdup( (const gchar*) sqlite3_column_text( dbase_full->stmts[50], 2 ) );
            edge.ID_object = ID_entity;

            rc = dbase_full_get_properties( dbase_full, edge.ID_edge, &(edge.arr_properties), errmsg );
            if ( rc )
            {
                g_free( edge.label_edge );
                g_array_unref( edge.arr_properties );
                g_array_unref( *arr_edges );
                ERROR_SOND( "dbase_full_get_properties" )
            }

            g_array_append_val( *arr_edges, edge );
        }
    } while ( rc == SQLITE_ROW );

    return 0;
}


gint
dbase_full_get_adm_entities( DBaseFull* dbase_full, gint ID_entity, gchar** adm_entity, gchar** errmsg )
{
    gint rc = 0;

    if ( !adm_entity ) return 0;

    sqlite3_reset( dbase_full->stmts[51] );

    rc = sqlite3_bind_int( dbase_full->stmts[51], 1, ID_entity );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (ID_entity)" )

    rc = sqlite3_step( dbase_full->stmts[51] );
    if ( rc != SQLITE_ROW ) ERROR_DBASE_FULL( "sqlite3_step" )

    *adm_entity = g_strdup( (const gchar*) sqlite3_column_text( dbase_full->stmts[51], 0 ) );

    return 0;

}


gint
dbase_full_prepare_stmts( DBaseFull* dbase_full, gchar** errmsg )
{
    gint rc = 0;
    gint zaehler = 0;
    sqlite3_stmt* stmt = NULL;

    rc = dbase_prepare_stmts( (DBase*) dbase_full, errmsg );
    if ( rc ) ERROR_SOND( "dbase_prepare_stmts" );

    gchar* sql[] = {

/*  insert_node (0) */
            "INSERT INTO baum_inhalt "
            "(parent_id, older_sibling_id, icon_name, node_text) "
            "VALUES ("
                "CASE ?1 " //child
                    "WHEN 0 THEN (SELECT parent_id FROM baum_inhalt WHERE node_id=?2) "
                    "WHEN 1 THEN ?2 " //node_id
                "END, "
                "CASE ?1 "
                    "WHEN 0 THEN ?2 "
                    "WHEN 1 THEN 0 "
                "END, "
                "?3, " //icon_name
                "?4); ", //node_text

            "INSERT INTO baum_auswertung "
            "(parent_id, older_sibling_id, icon_name, node_text) "
            "VALUES ("
                "CASE ?1 " //child
                    "WHEN 0 THEN (SELECT parent_id FROM baum_auswertung WHERE node_id=?2) "
                    "WHEN 1 THEN ?2 " //node_id
                "END, "
                "CASE ?1 "
                    "WHEN 0 THEN ?2 "
                    "WHEN 1 THEN 0 "
                "END, "
                "?3, " //icon_name
                "?4); ", //node_text

                "UPDATE baum_inhalt SET "
                    "older_sibling_id=last_insert_rowid() "
                "WHERE "
                    "parent_id=(SELECT parent_id FROM baum_inhalt WHERE node_id=last_insert_rowid()) "
                "AND "
                    "older_sibling_id=(SELECT older_sibling_id FROM baum_inhalt WHERE node_id=last_insert_rowid()) "
                "AND "
                    "node_id!=last_insert_rowid() "
                "AND "
                    "node_id!=0; ",

                "UPDATE baum_auswertung SET "
                    "older_sibling_id=last_insert_rowid() "
                "WHERE "
                    "parent_id=(SELECT parent_id FROM baum_auswertung WHERE node_id=last_insert_rowid()) "
                "AND "
                    "older_sibling_id=(SELECT older_sibling_id FROM baum_auswertung WHERE node_id=last_insert_rowid()) "
                "AND "
                    "node_id!=last_insert_rowid() "
                "AND "
                    "node_id!=0; ",

                "VALUES (last_insert_rowid()); ",

/*  db_set_node_text (5) */
            "UPDATE baum_inhalt SET node_text = ?1 WHERE node_id = ?2;",

            "UPDATE baum_auswertung SET node_text = ?1 WHERE node_id = ?2;",

/*  dbase_full_set_icon_id  */
            "UPDATE baum_inhalt SET icon_name = ?1 WHERE node_id = ?2;",

            "UPDATE baum_auswertung SET icon_name = ?1 WHERE node_id = ?2;",

/*  db_speichern_textview  */
            "UPDATE baum_auswertung SET text=? WHERE node_id=?;",

/*  db_set_datei (10) */
            "INSERT INTO dateien (rel_path, node_id) VALUES (?, ?); ",

/*  ziele_einfuegen  */
            "INSERT INTO ziele (ziel_id_von, index_von, "
            "ziel_id_bis, index_bis, rel_path, node_id) "
            "VALUES (?, ?, ?, ?,"
            "(SELECT dateien.rel_path FROM dateien "
            "LEFT JOIN ziele "
            "ON dateien.rel_path=ziele.rel_path "
            "JOIN baum_inhalt "
            "ON baum_inhalt.node_id=dateien.node_id OR "
            "baum_inhalt.node_id=ziele.node_id "
            "WHERE baum_inhalt.node_id=?), "
            "last_insert_rowid())",

/*  db_remove_node  */
            "UPDATE baum_inhalt SET older_sibling_id=(SELECT older_sibling_id FROM baum_inhalt "
            "WHERE node_id=?1) WHERE "
            "older_sibling_id=?1; ",

            "UPDATE baum_auswertung SET older_sibling_id=(SELECT older_sibling_id FROM baum_auswertung "
            "WHERE node_id=?1) WHERE "
            "older_sibling_id=?1; ",

            "DELETE FROM baum_inhalt WHERE node_id = ?;",

            "DELETE FROM baum_auswertung WHERE node_id = ?;",

/*  db_verschieben_knoten  */
            //knoten herauslösen = older_sibling von younger sibling verbiegen
            "UPDATE baum_inhalt SET older_sibling_id="
            "(SELECT older_sibling_id FROM baum_inhalt WHERE node_id=?1)" //node_id
            "WHERE older_sibling_id=?1; ",

            "UPDATE baum_auswertung SET older_sibling_id="
            "(SELECT older_sibling_id FROM baum_auswertung WHERE node_id=?1)"
            "WHERE older_sibling_id=?1; ",

            //older_sibling von neuem younger_sibling verbiegen
            "UPDATE baum_inhalt SET older_sibling_id=?1 WHERE node_id=" //node_id
            "(SELECT node_id FROM baum_inhalt WHERE parent_id=?2 AND older_sibling_id=?3); ", //new_parent_id/new_older_s_id

            "UPDATE baum_auswertung SET older_sibling_id=?1 WHERE node_id=" //node_id
            "(SELECT node_id FROM baum_auswertung WHERE parent_id=?2 AND older_sibling_id=?3); ", //new_parent_id/new_older_s_id

            "UPDATE baum_inhalt SET parent_id=?1, older_sibling_id=?2 WHERE node_id=?3; ",

            "UPDATE baum_auswertung SET parent_id=?1, older_sibling_id=?2 WHERE node_id=?3; ",

/*  db_knoten_nach_auswertung (22) */
            "INSERT INTO baum_auswertung "
            "(parent_id, older_sibling_id, icon_name, node_text, ref_id) "
            "VALUES ("
                "CASE ?1 " //child
                    "WHEN 0 THEN (SELECT parent_id FROM baum_auswertung WHERE node_id=?2) "
                    "WHEN 1 THEN ?2 " //node_id_nach
                "END, "
                "CASE ?1 " //older_sibling_id
                    "WHEN 0 THEN ?2 "
                    "WHEN 1 THEN 0 "
                "END, "
                "(SELECT icon_name FROM baum_inhalt WHERE node_id=?3), " //node_id_von
                "(SELECT node_text FROM baum_inhalt WHERE node_id=?3), "//node_text
                "(CASE "
                    "WHEN " //datei_id zu node_id_von?
                        "(SELECT dateien.rel_path FROM dateien LEFT JOIN ziele "
                        "ON dateien.rel_path=ziele.rel_path "
                        "JOIN baum_inhalt "
                        "ON baum_inhalt.node_id=dateien.node_id OR "
                        "baum_inhalt.node_id=ziele.node_id "
                        "WHERE baum_inhalt.node_id=?3)"
                            "IS NULL THEN 0 "
                    "ELSE ?3 "
                "END)); ",

            "INSERT INTO baum_auswertung "
            "(parent_id, older_sibling_id, icon_name, node_text, text, ref_id) "
            "VALUES ("
                "CASE ?1 " //child
                    "WHEN 0 THEN (SELECT parent_id FROM baum_auswertung WHERE node_id=?2) "
                    "WHEN 1 THEN ?2 " //node_id_nach
                "END, "
                "CASE ?1 " //older_sibling_id
                    "WHEN 0 THEN ?2 "
                    "WHEN 1 THEN 0 "
                "END, "
                "(SELECT icon_name FROM baum_auswertung WHERE node_id=?3), " //node_id_von
                "(SELECT node_text FROM baum_auswertung WHERE node_id=?3), "//node_text
                "(SELECT text FROM baum_auswertung WHERE node_id=?3), "//text
                "(CASE "
                    "WHEN " //datei_id zu node_id_von?
                        "(SELECT dateien.rel_path FROM dateien LEFT JOIN ziele "
                        "ON dateien.rel_path=ziele.rel_path "
                        "JOIN baum_inhalt "
                        "ON baum_inhalt.node_id=dateien.node_id OR "
                        "baum_inhalt.node_id=ziele.node_id "
                        "WHERE baum_inhalt.node_id="
                            "(SELECT ref_id FROM baum_auswertung WHERE node_id=?3)"
                            ") IS NULL THEN 0 "
                    "ELSE "
                        "(SELECT ref_id FROM baum_auswertung WHERE node_id=?3) "
                "END)); ",

            "UPDATE baum_auswertung SET "
                "older_sibling_id=last_insert_rowid() "
            "WHERE "
                "parent_id=(SELECT parent_id FROM baum_auswertung WHERE node_id=last_insert_rowid()) "
            "AND "
                "older_sibling_id=(SELECT older_sibling_id FROM baum_auswertung WHERE node_id=last_insert_rowid()) "
            "AND "
                "node_id!=last_insert_rowid() "
            "AND "
                "node_id!=0; ",

/*  db_get_icon_name_and_node_text  */
            "SELECT icon_name, node_text, node_id FROM baum_inhalt WHERE node_id=?;",

            "SELECT icon_name, node_text, node_id FROM baum_auswertung WHERE node_id=?;",

/*  db_get_parent  */
            "SELECT parent_id FROM baum_inhalt WHERE node_id = ?;",

            "SELECT parent_id FROM baum_auswertung WHERE node_id = ?;",

/*  db_get_older_sibling  */
            "SELECT older_sibling_id FROM baum_inhalt WHERE node_id = ?;",

            "SELECT older_sibling_id FROM baum_auswertung WHERE node_id = ?;",

/*  db_get_younger_sibling (31) */
            "SELECT inhalt1.node_id, inhalt2.node_id FROM baum_inhalt AS inhalt1 "
            "LEFT JOIN baum_inhalt AS inhalt2 "
            "ON inhalt1.node_id = inhalt2.older_sibling_id "
            "WHERE inhalt1.node_id > 0 AND inhalt1.node_id = ?;",

            "SELECT inhalt1.node_id, inhalt2.node_id FROM baum_auswertung AS inhalt1 "
            "LEFT JOIN baum_auswertung AS inhalt2 "
            "ON inhalt1.node_id = inhalt2.older_sibling_id "
            "WHERE inhalt1.node_id > 0 AND inhalt1.node_id = ?;",

/*  db_get_ref_id  */
            "SELECT ref_id FROM baum_auswertung WHERE node_id=?",

/*  db_get_ziel  */
            "SELECT ziel_id_von, index_von, ziel_id_bis, index_bis FROM ziele "
            "WHERE node_id=?;",

/*  db_get_text  */
            "SELECT node_id, text FROM baum_auswertung WHERE node_id = ?;",

/*  db_get_rel_path  */
            "SELECT baum_inhalt.node_id, rel_path FROM baum_inhalt "
            "LEFT JOIN "
            "(SELECT dateien.rel_path AS rel_path, dateien.node_id AS d_node_id, "
            "ziele.node_id AS z_node_id FROM dateien LEFT JOIN ziele "
            "ON dateien.rel_path=ziele.rel_path) "
            "ON baum_inhalt.node_id=d_node_id OR "
            "baum_inhalt.node_id=z_node_id "
            "WHERE baum_inhalt.node_id=?;",

/*  db_get_first_child  */
            "SELECT inhalt1.node_id, inhalt2.node_id FROM baum_inhalt AS inhalt1 "
            "LEFT JOIN baum_inhalt AS inhalt2 "
            "ON inhalt1.node_id = inhalt2.parent_id AND inhalt2.older_sibling_id = 0 "
                "AND inhalt2.node_id != 0 "
            "WHERE inhalt1.node_id = ?;",

            "SELECT inhalt1.node_id, inhalt2.node_id FROM baum_auswertung AS inhalt1 "
            "LEFT JOIN baum_auswertung AS inhalt2 "
            "ON inhalt1.node_id = inhalt2.parent_id AND inhalt2.older_sibling_id = 0 "
                "AND inhalt2.node_id != 0 "
            "WHERE inhalt1.node_id = ?;",

/*  db_get_node_id_from_rel_path  */
            "SELECT node_id FROM dateien WHERE rel_path=?;",

/*  db_check_id  */
            "SELECT ziel_id_von FROM ziele WHERE ziel_id_von=?1;"
            "UNION "
            "SELECT ziel_id_bis FROM ziele WHERE ziel_id_bis=?1;",

/*  insert_entity  (41) */
            "INSERT INTO entities (label) VALUES (?1);",

            "SELECT (last_insert_rowid()); ",

/*  insert_property (43)  */
            "INSERT INTO properties (entity,label,value) VALUES (?1, ?2, ?3);",

/*  get_label_entity (44)  */
            "SELECT labels.label FROM labels JOIN entities "
                "ON entities.label = labels.ID WHERE entities.ID = ?1;",

/*  get_properties (45)  */
            "SELECT ID_property, label_text, property_value FROM "
                "(SELECT properties.ID AS ID_property, properties.entity AS ID_entity, labels.label AS label_text, "
                    "properties.value AS property_value FROM labels JOIN properties ON labels.ID = properties.label "
                    "WHERE properties.entity = ?1) "
                    "JOIN "
                    "entities ON ID_entity = entities.ID;",

/*  get_outgoint_edges (46)  */
            "SELECT ID_subject, ID_edge, labels.label, ID_object "
                "FROM labels JOIN "
                "(SELECT edges.subject AS ID_subject, edges.edge AS ID_edge, entities.label AS ID_label_edge, edges.object AS ID_object "
                "FROM edges JOIN entities ON edges.edge = entities.ID WHERE edges.subject = ?1) "
                "ON ID_label_edge = labels.ID; ",

/*  get_label_text (47) */
            "SELECT labels.label FROM labels WHERE labels.ID = ?1; ",

/*  get_array_children (48) */
            "SELECT labels.ID FROM labels WHERE labels.parent = ?1; ",

/*  get_array_nodes (49)  */
            "SELECT entities.ID FROM entities JOIN "
            "(WITH RECURSIVE cte_labels (ID) AS ( "
                "VALUES (?1) "
                "UNION ALL "
                "SELECT labels.ID "
                    "FROM labels JOIN cte_labels WHERE "
                    "labels.parent = cte_labels.ID "
                ") SELECT ID AS ID_CTE FROM cte_labels) "
                "ON entities.label = ID_CTE; ",

/*  get_incoming_edges (50)  */
            "SELECT ID_subject, ID_edge, labels.label, ID_object "
                "FROM labels JOIN "
                "(SELECT edges.subject AS ID_subject, edges.edge AS ID_edge, entities.label AS ID_label_edge, edges.object AS ID_object "
                "FROM edges JOIN entities ON edges.edge = entities.ID WHERE edges.object = ?1) "
                "ON ID_label_edge = labels.ID; ",

/*  get_adm_entities (51)   */
            "SELECT labels.adm_entities FROM labels JOIN entities "
                "ON labels.ID = entities.label "
                "WHERE entities.ID = ?1; ",

            NULL };

    while ( sql[zaehler] != NULL )
    {
        stmt = dbase_prepare_stmt( dbase_full->dbase.db, sql[zaehler], errmsg );

        if ( !stmt ) ERROR_PAO( "dbase_prepare_stmt" )

        if ( zaehler == 0 ) dbase_full->insert_node[0] = stmt;
        else if ( zaehler == 1 ) dbase_full->insert_node[1] = stmt;
        else if ( zaehler == 2 ) dbase_full->insert_node[2] = stmt;
        else if ( zaehler == 3 ) dbase_full->insert_node[3] = stmt;
        else if ( zaehler == 4 ) dbase_full->insert_node[4] = stmt;

        else if ( zaehler == 5 ) dbase_full->set_node_text[0] = stmt;
        else if ( zaehler == 6 ) dbase_full->set_node_text[1] = stmt;

        else if ( zaehler == 7 ) dbase_full->set_icon_id[0] = stmt;
        else if ( zaehler == 8 ) dbase_full->set_icon_id[1] = stmt;

        else dbase_full->stmts[zaehler] = stmt;

        zaehler++;
    }

    return 0;
}


gint
dbase_full_create( const gchar* path, DBaseFull** dbase_full, gboolean create,
        gboolean overwrite, gchar** errmsg )
{
    gint rc = 0;

    *dbase_full = g_malloc0( sizeof( DBaseFull ) );

    rc = dbase_open( path, (DBase*) *dbase_full, create, overwrite, errmsg );
    if ( rc )
    {
        dbase_destroy( (DBase*) *dbase_full );
        *dbase_full = NULL;
        if ( rc == -1 ) ERROR_SOND( "dbase_full_open" )
        else return 1;
    }

    return 0;
}

