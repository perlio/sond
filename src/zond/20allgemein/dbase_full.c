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


static gint
dbase_full_prepare_stmts( DBaseFull* dbase_full, gchar** errmsg )
{
    gint zaehler = 0;
    sqlite3_stmt* stmt = NULL;
    gchar* sql[] = {

/*  insert_node  */
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

/*  db_set_node_text  */
            "UPDATE baum_inhalt SET node_text = ?1 WHERE node_id = ?2;",

            "UPDATE baum_auswertung SET node_text = ?1 WHERE node_id = ?2;",

            NULL,

/*  db_set_icon_name  */
            "UPDATE baum_inhalt SET icon_name = ?1 WHERE node_id = ?2;",

            "UPDATE baum_auswertung SET icon_name = ?1 WHERE node_id = ?2;",

/*  db_speichern_textview  */
            "UPDATE baum_auswertung SET text=? WHERE node_id=?;",

/*  db_set_datei */
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

/*  db_knoten_nach_auswertung  */
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

/*  db_get_younger_sibling  */
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

            NULL };

    while ( sql[zaehler] != NULL )
    {
        stmt = dbase_prepare_stmt( dbase_full->dbase.db, sql[zaehler], errmsg );

        if ( !stmt )
        {
            if ( errmsg ) *errmsg = add_string( g_strdup( "Bei Aufruf dbase_prepare_stmt:\n" ),
                    *errmsg );

            return -1;
        }

        if ( zaehler == 0 ) dbase_full->insert_node[0] = stmt;
        else if ( zaehler == 1 ) dbase_full->insert_node[1] = stmt;
        else if ( zaehler == 2 ) dbase_full->insert_node[2] = stmt;
        else if ( zaehler == 3 ) dbase_full->insert_node[3] = stmt;
        else if ( zaehler == 4 ) dbase_full->insert_node[4] = stmt;

        else if ( zaehler == 5 ) dbase_full->set_node_text[0] = stmt;
        else if ( zaehler == 6 ) dbase_full->set_node_text[1] = stmt;

        zaehler++;
    }

    return 0;
}


static DBaseFull*
dbase_full_new( void )
{
    return g_malloc0( sizeof( DBaseFull ) );
}


gint
dbase_full_open( const gchar* path, DBaseFull* dbase_full, gboolean create,
        gboolean overwrite, gchar** errmsg )
{
    gint rc = 0;

    rc = dbase_open( path, (DBase*) dbase_full, create, overwrite, errmsg );
    if ( rc == -1 ) ERROR( "dbase_open" )
    else if ( rc == 1 ) return 1;

    rc = dbase_full_prepare_stmts( dbase_full, errmsg );
    if ( rc ) ERROR( "dbase_full_prepare_stmts" )

    return 0;
}


gint
dbase_full_create( const gchar* path, DBaseFull** dbase_full, gboolean create,
        gboolean overwrite, gchar** errmsg )
{
    gint rc = 0;

    *dbase_full = dbase_full_new( );

    rc = dbase_full_open( path, *dbase_full, create, overwrite,errmsg );
    if ( rc )
    {
        dbase_destroy( (DBase*) *dbase_full );
        if ( rc == -1 ) ERROR( "dbase_full_open" )
        else return 1;
    }

    return 0;
}

