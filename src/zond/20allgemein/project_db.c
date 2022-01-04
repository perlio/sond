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

#define ERROR_SQL(x) { if ( errmsg ) *errmsg = add_string( g_strconcat( "Bei Aufruf " x ":\n", \
                       sqlite3_errmsg(dbase->db), NULL ), *errmsg ); \
                       return -1; }

#define ERROR_PAO_R(x,y) { if ( errmsg ) *errmsg = add_string( \
                       g_strdup( "Bei Aufruf " x ":\n" ), *errmsg ); \
                       return y; }

#include <stddef.h>
#include <sqlite3.h>
#include <glib.h>

#include "../global_types.h"

#include "../../misc.h"


gint
project_db_create_stmts( Database* dbase, gchar** errmsg )
{
    gint rc = 0;

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
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(ziele_einfuegen [0])" )

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
                "(db_get_icon_name_and_node_text [1])" )

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

    return 0;
}

