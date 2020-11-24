/*
zond (project_db.c) - Akten, Beweisstücke, Unterlagen
Copyright (C) 2020  pelo america

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

#include <sqlite3.h>

#include "../error.h"

typedef int gint;

typedef struct _Prepared_Statements
{
    sqlite3_stmt* db_speichern_textview[1];
    sqlite3_stmt* db_set_datei[1];
    sqlite3_stmt* transaction[3];
    sqlite3_stmt* transaction_store[3];
    sqlite3_stmt* db_insert_node[5];
    sqlite3_stmt* ziele_einfuegen[1];
    sqlite3_stmt* db_kopieren_nach_auswertung[3];
    sqlite3_stmt* db_get_icon_name_and_node_text[2];
    sqlite3_stmt* db_remove_node[4];
    sqlite3_stmt* db_verschieben_knoten[6];
    sqlite3_stmt* db_get_parent[2];
    sqlite3_stmt* db_get_older_sibling[2];
    sqlite3_stmt* db_get_younger_sibling[2];
    sqlite3_stmt* db_get_ref_id[1];
    sqlite3_stmt* db_get_ziel[1];
    sqlite3_stmt* db_get_text[1];
    sqlite3_stmt* db_get_rel_path[1];
    sqlite3_stmt* db_set_node_text[2];
    sqlite3_stmt* db_set_icon_id[2];
    sqlite3_stmt* db_get_first_child[2];
    sqlite3_stmt* db_get_node_id_from_rel_path[1];
    sqlite3_stmt* db_check_id[1];
    sqlite3_stmt* db_update_path[2];
} PreparedStatements;

typedef struct _Database
{
    sqlite3* db;
    sqlite3* db_store;
    PreparedStatements* stmts;
} Database;

/*
static gint
project_db_create_stmts( Database* dbase, gchar** errmsg )
{
    gint rc = 0;

/*  db_speichern_textview  */
    rc = sqlite3_prepare_v2( dbase->db, "UPDATE baum_auswertung SET text=? WHERE "
            "node_id=?;", -1, &(dbase->stmts.db_speichern_textview[0]), NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2 "
                "(db_speichern_textview):\n", sqlite3_errmsg( dbase->db ), NULL );

        return -1;
    }

/*  db_set_datei */
    rc = sqlite3_prepare_v2( dbase->db,
            "INSERT INTO dateien (rel_path, node_id) "
            "VALUES (?, ?); ", //datei_id, rel_path, last_insert_rowid
            -1, &(dbase->stmts.db_set_datei[0]), NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2 "
                "(db_set_datei [0]):\n", sqlite3_errmsg( dbase->db ), NULL );

        return -1;
    }

/*  transactions  */
    rc = sqlite3_prepare_v2( dbase->db, "BEGIN;", -1, &(dbase->stmts.transaction[0]), NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2 "
                "(BEGIN):\n",
                sqlite3_errmsg( dbase->db ), NULL );

        return -1;
    }

    rc = sqlite3_prepare_v2( dbase->db, "COMMIT;", -1, &(dbase->stmts.transaction[1]), NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2 "
                "(COMMIT):\n",
                sqlite3_errmsg( dbase->db ), NULL );

        return -1;
    }

    rc = sqlite3_prepare_v2( dbase->db, "ROLLBACK;", -1, &(dbase->stmts.transaction[2]), NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2 "
                "(ROLLBACK):\n",
                sqlite3_errmsg( dbase->db ), NULL );

        return -1;
    }

/*  transactions_store  */
    rc = sqlite3_prepare_v2( dbase->db_store, "BEGIN;", -1, &(dbase->stmts.transaction_store[0]), NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2 "
                "(BEGIN db_store):\n",
                sqlite3_errmsg( dbase->db_store ), NULL );

        return -1;
    }

    rc = sqlite3_prepare_v2( dbase->db_store, "COMMIT;", -1, &(dbase->stmts.transaction_store[1]), NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2 "
                "(COMMIT db_store):\n",
                sqlite3_errmsg( dbase->db_store ), NULL );

        return -1;
    }

    rc = sqlite3_prepare_v2( dbase->db_store, "ROLLBACK;", -1, &(dbase->stmts.transaction_store[2]), NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2 "
                "(ROLLBACK db_Store):\n",
                sqlite3_errmsg( dbase->db_store ), NULL );

        return -1;
    }

/*  db_insert_node  */
    rc = sqlite3_prepare_v2( dbase->db,
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
            -1, &(dbase->stmts.db_insert_node[0]), NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2 "
                "(db_insert_node[0]):\n", sqlite3_errmsg( dbase->db ), NULL );

        return -1;
    }

    rc = sqlite3_prepare_v2( dbase->db,
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
            -1, &(dbase->stmts.db_insert_node[1]), NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2 "
                "(db_insert_node[1]):\n", sqlite3_errmsg( dbase->db ), NULL );

        return -1;
    }

    rc = sqlite3_prepare_v2( dbase->db,
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
            -1, &(dbase->stmts.db_insert_node[2]), NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2 "
                "(db_insert_node [2]):\n", sqlite3_errmsg( dbase->db ), NULL );

        return -1;
    }

    rc = sqlite3_prepare_v2( dbase->db,
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
            -1, &(dbase->stmts.db_insert_node[3]), NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2 "
                "(db_insert_node [3]):\n", sqlite3_errmsg( dbase->db ), NULL );

        return -1;
    }

    rc = sqlite3_prepare_v2( dbase->db, "VALUES (last_insert_rowid()); ",
            -1, &(dbase->stmts.db_insert_node[4]), NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2 "
                "(db_insert_node [4]):\n", sqlite3_errmsg( dbase->db ), NULL );

        return -1;
    }

/*  ziele_einfuegen  */
    rc = sqlite3_prepare_v2( dbase->db, "INSERT INTO ziele (ziel_id_von, index_von, "
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
            -1, &(dbase->stmts.ziele_einfuegen[0]), NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2 "
                "(ziele_einfuegen [0]):\n", sqlite3_errmsg( dbase->db ), NULL );

        return -1;
    }

/*  db_knoten_nach_auswertung  */
    rc = sqlite3_prepare_v2( dbase->db,
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
            -1, &(dbase->stmts.db_kopieren_nach_auswertung[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "Bei Aufruf sqlite3_prepare_v2 "
                "(db_kopieren_nach_auswertung [0]):\n" )

    rc = sqlite3_prepare_v2( dbase->db,
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
            -1, &(dbase->stmts.db_kopieren_nach_auswertung[1]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "Bei Aufruf sqlite3_prepare_v2 "
                "(db_kopieren_nach_auswertung [1]):\n" )

    rc = sqlite3_prepare_v2( dbase->db,
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
            -1, &(dbase->stmts.db_kopieren_nach_auswertung[2]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "Bei Aufruf sqlite3_prepare_v2 "
                "(db_kopieren_nach_auswertung [2]):\n" )

/*  db_get_icon_name_and_node_text  */
    rc = sqlite3_prepare_v2( dbase->db,
            "SELECT icon_name, node_text, node_id FROM baum_inhalt WHERE node_id=?;",
            -1, &(dbase->stmts.db_get_icon_name_and_node_text[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_get_icon_name_and_node_text [0]):\n" )

    rc = sqlite3_prepare_v2( dbase->db,
            "SELECT icon_name, node_text, node_id FROM baum_auswertung WHERE node_id=?;",
            -1, &(dbase->stmts.db_get_icon_name_and_node_text[1]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_get_icon_name_and_node_text [1]):\n" )

/*  db_remove_node  */
    rc = sqlite3_prepare_v2( dbase->db,
            "UPDATE baum_inhalt SET older_sibling_id=(SELECT older_sibling_id FROM baum_inhalt "
            "WHERE node_id=?1) WHERE "
            "older_sibling_id=?1; ",
            -1, &(dbase->stmts.db_remove_node[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_remove_node[0]):\n" )

    rc = sqlite3_prepare_v2( dbase->db,
            "UPDATE baum_auswertung SET older_sibling_id=(SELECT older_sibling_id FROM baum_auswertung "
            "WHERE node_id=?1) WHERE "
            "older_sibling_id=?1; ",
            -1, &(dbase->stmts.db_remove_node[1]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_remove_node[1]):\n" )

    rc = sqlite3_prepare_v2( dbase->db,
            "DELETE FROM baum_inhalt WHERE node_id = ?;",
            -1, &(dbase->stmts.db_remove_node[2]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_remove_node[2]):\n" )

    rc = sqlite3_prepare_v2( dbase->db,
            "DELETE FROM baum_auswertung WHERE node_id = ?;",
            -1, &(dbase->stmts.db_remove_node[3]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_remove_node[3]):\n" )

/*  db_verschieben_knoten  */
    //knoten herauslösen = older_sibling von younger sibling verbiegen
    rc = sqlite3_prepare_v2( dbase->db,
            "UPDATE baum_inhalt SET older_sibling_id="
            "(SELECT older_sibling_id FROM baum_inhalt WHERE node_id=?1)" //node_id
            "WHERE older_sibling_id=?1; ",
            -1, &(dbase->stmts.db_verschieben_knoten[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_verschieben_knoten[0])" )

    rc = sqlite3_prepare_v2( dbase->db,
            "UPDATE baum_auswertung SET older_sibling_id="
            "(SELECT older_sibling_id FROM baum_auswertung WHERE node_id=?1)"
            "WHERE older_sibling_id=?1; ",
            -1, &(dbase->stmts.db_verschieben_knoten[1]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_verschieben_knoten[1])" )

    //older_sibling von neuem younger_sibling verbiegen
    rc = sqlite3_prepare_v2( dbase->db,
            "UPDATE baum_inhalt SET older_sibling_id=?1 WHERE node_id=" //node_id
                "(SELECT node_id FROM baum_inhalt WHERE parent_id=?2 AND older_sibling_id=?3); ", //new_parent_id/new_older_s_id
            -1, &(dbase->stmts.db_verschieben_knoten[2]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_verschieben_knoten[2])" )

    rc = sqlite3_prepare_v2( dbase->db,
            "UPDATE baum_auswertung SET older_sibling_id=?1 WHERE node_id=" //node_id
                "(SELECT node_id FROM baum_auswertung WHERE parent_id=?2 AND older_sibling_id=?3); ", //new_parent_id/new_older_s_id
            -1, &(dbase->stmts.db_verschieben_knoten[3]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_verschieben_knoten[3])" )

    rc = sqlite3_prepare_v2( dbase->db,
            "UPDATE baum_inhalt SET parent_id=?1, older_sibling_id=?2 WHERE node_id=?3; ",
            -1, &(dbase->stmts.db_verschieben_knoten[4]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_verschieben_knoten[4])" )

    rc = sqlite3_prepare_v2( dbase->db,
            "UPDATE baum_auswertung SET parent_id=?1, older_sibling_id=?2 WHERE node_id=?3; ",
            -1, &(dbase->stmts.db_verschieben_knoten[5]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_verschieben_knoten[5])" )

/*  db_get_parent  */
    rc = sqlite3_prepare_v2( dbase->db,
            "SELECT parent_id FROM baum_inhalt WHERE node_id = ?;",
            -1, &(dbase->stmts.db_get_parent[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_get_parent[0])" )

    rc = sqlite3_prepare_v2( dbase->db,
            "SELECT parent_id FROM baum_auswertung WHERE node_id = ?;",
            -1, &(dbase->stmts.db_get_parent[1]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_get_parent[1])" )

/*  db_get_older_sibling  */
    rc = sqlite3_prepare_v2( dbase->db,
            "SELECT older_sibling_id FROM baum_inhalt WHERE node_id = ?;",
            -1, &(dbase->stmts.db_get_older_sibling[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_get_older_sibling[0])" )

    rc = sqlite3_prepare_v2( dbase->db,
            "SELECT older_sibling_id FROM baum_auswertung WHERE node_id = ?;",
            -1, &(dbase->stmts.db_get_older_sibling[1]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_get_older_sibling[1])" )

/*  db_get_younger_sibling  */
    rc = sqlite3_prepare_v2( dbase->db,
            "SELECT inhalt1.node_id, inhalt2.node_id FROM baum_inhalt AS inhalt1 "
                "LEFT JOIN baum_inhalt AS inhalt2 "
                "ON inhalt1.node_id = inhalt2.older_sibling_id "
                "WHERE inhalt1.node_id > 0 AND inhalt1.node_id = ?;",
            -1, &(dbase->stmts.db_get_younger_sibling[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_get_younger_sibling[0])" )

    rc = sqlite3_prepare_v2( dbase->db,
            "SELECT inhalt1.node_id, inhalt2.node_id FROM baum_auswertung AS inhalt1 "
                "LEFT JOIN baum_auswertung AS inhalt2 "
                "ON inhalt1.node_id = inhalt2.older_sibling_id "
                "WHERE inhalt1.node_id > 0 AND inhalt1.node_id = ?;",
            -1, &(dbase->stmts.db_get_younger_sibling[1]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_get_younger_sibling[1])" )

/*  db_get_ref_id  */
    rc = sqlite3_prepare_v2( dbase->db,
            "SELECT ref_id FROM baum_auswertung WHERE node_id=?",
            -1, &(dbase->stmts.db_get_ref_id[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_get_ref_id[0])" )

/*  db_get_ziel  */
    rc = sqlite3_prepare_v2( dbase->db,
            "SELECT ziel_id_von, index_von, ziel_id_bis, index_bis FROM ziele "
            "WHERE node_id=?;",
            -1, &(dbase->stmts.db_get_ziel[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_get_ziel[0])" )

/*  db_get_text  */
    rc = sqlite3_prepare_v2( dbase->db,
            "SELECT node_id, text FROM baum_auswertung WHERE node_id = ?;",
            -1, &(dbase->stmts.db_get_text[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_get_text[0])" )

/*  db_get_rel_path  */
    rc = sqlite3_prepare_v2( dbase->db,
            "SELECT baum_inhalt.node_id, rel_path FROM baum_inhalt "
            "LEFT JOIN "
            "(SELECT dateien.rel_path AS rel_path, dateien.node_id AS d_node_id, "
            "ziele.node_id AS z_node_id FROM dateien LEFT JOIN ziele "
            "ON dateien.rel_path=ziele.rel_path) "
            "ON baum_inhalt.node_id=d_node_id OR "
            "baum_inhalt.node_id=z_node_id "
            "WHERE baum_inhalt.node_id=?;",
            -1, &(dbase->stmts.db_get_rel_path[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_get_rel_path[0])" )

/*  db_set_node_text  */
    rc = sqlite3_prepare_v2( dbase->db,
            "UPDATE baum_inhalt SET node_text = ?1 WHERE node_id = ?2;",
            -1, &(dbase->stmts.db_set_node_text[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_set_node_text[0])" )

    rc = sqlite3_prepare_v2( dbase->db,
            "UPDATE baum_auswertung SET node_text = ?1 WHERE node_id = ?2;",
            -1, &(dbase->stmts.db_set_node_text[1]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_set_node_text[1])" )

/*  db_set_icon_name  */
    rc = sqlite3_prepare_v2( dbase->db,
            "UPDATE baum_inhalt SET icon_name = ?1 WHERE node_id = ?2;",
            -1, &(dbase->stmts.db_set_icon_id[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_set_icon_id[0])" )

    rc = sqlite3_prepare_v2( dbase->db,
            "UPDATE baum_auswertung SET icon_name = ?1 WHERE node_id = ?2;",
            -1, &(dbase->stmts.db_set_icon_id[1]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_set_icon_id[1])" )

/*  db_get_first_child  */
    rc = sqlite3_prepare_v2( dbase->db,
            "SELECT inhalt1.node_id, inhalt2.node_id FROM baum_inhalt AS inhalt1 "
                "LEFT JOIN baum_inhalt AS inhalt2 "
                "ON inhalt1.node_id = inhalt2.parent_id AND inhalt2.older_sibling_id = 0 "
                    "AND inhalt2.node_id != 0 "
                "WHERE inhalt1.node_id = ?;",
            -1, &(dbase->stmts.db_get_first_child[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_get_first_child[0])" )

    rc = sqlite3_prepare_v2( dbase->db,
            "SELECT inhalt1.node_id, inhalt2.node_id FROM baum_auswertung AS inhalt1 "
                "LEFT JOIN baum_auswertung AS inhalt2 "
                "ON inhalt1.node_id = inhalt2.parent_id AND inhalt2.older_sibling_id = 0 "
                    "AND inhalt2.node_id != 0 "
                "WHERE inhalt1.node_id = ?;",
            -1, &(dbase->stmts.db_get_first_child[1]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_get_first_child[1])" )

/*  db_get_node_id_from_rel_path  */
    rc = sqlite3_prepare_v2( dbase->db,
        "SELECT node_id FROM dateien WHERE rel_path=?;",
            -1, &(dbase->stmts.db_get_node_id_from_rel_path[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_get_node_id_from_rel_path[0])" )

/*  db_check_id  */
    rc = sqlite3_prepare_v2( dbase->db,
        "SELECT ziel_id_von FROM ziele WHERE ziel_id_von=?1;"
        "UNION "
        "SELECT ziel_id_bis FROM ziele WHERE ziel_id_bis=?1;",
            -1, &(dbase->stmts.db_check_id[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_check_id[0])" )

/*  db_update_path  */
    rc = sqlite3_prepare_v2( dbase->db,
            "UPDATE dateien SET rel_path = REPLACE( SUBSTR( rel_path, 1, LENGTH( ?1 ) ), ?1, ?2 ) || SUBSTR( rel_path, LENGTH( ?1 ) + 1 );",
            -1, &(dbase->stmts.db_update_path[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_update_path[0])" )

    rc = sqlite3_prepare_v2( dbase->db_store,
            "UPDATE dateien SET rel_path = REPLACE( SUBSTR( rel_path, 1, LENGTH( ?1 ) ), ?1, ?2 ) || SUBSTR( rel_path, LENGTH( ?1 ) + 1 );",
            -1, &(dbase->stmts.db_update_path[1]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_update_path[1])" )

    return 0;
}


