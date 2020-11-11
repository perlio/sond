/*
zond (project.c) - Akten, Beweisstücke, Unterlagen
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

#include "../global_types.h"
#include "../error.h"

#include "../99conv/general.h"
#include "../99conv/db_zu_baum.h"

#include "../../misc.h"


#include "../20allgemein/fs_tree.h"

#include <sqlite3.h>
#include <gtk/gtk.h>
#include <glib/gstdio.h>


static gint
db_backup( sqlite3* db_orig, sqlite3* db_dest, gchar** errmsg )
{
    gint rc = 0;

    //Datenbank öffnen
    sqlite3_backup* backup = NULL;
    backup = sqlite3_backup_init( db_dest, "main", db_orig, "main" );

    if ( !backup )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_backup_init\nresult code: ",
                sqlite3_errstr( sqlite3_errcode( db_dest ) ), "\n",
                sqlite3_errmsg( db_dest ), NULL );

        return -1;
    }
    rc = sqlite3_backup_step( backup, -1 );
    sqlite3_backup_finish( backup );
    if ( rc != SQLITE_DONE )
    {
        if ( errmsg && rc == SQLITE_NOTADB ) *errmsg = g_strdup( "Datei ist "
                "keine SQLITE-Datenbank" );
        else if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_backup_step:\nresult code: ",
                sqlite3_errstr( rc ), "\n", sqlite3_errmsg( db_dest ), NULL );

        return -1;
    }


    return 0;
}


gboolean
db_create( sqlite3* db, gchar** errmsg )
{
    gchar* errmsg_ii = NULL;
    gchar* sql = NULL;
    gint rc = 0;

    //Tabellenstruktur erstellen
    sql = //Haupttabelle
            "DROP TABLE IF EXISTS baum_auswertung; "
            "DROP TABLE IF EXISTS dateien;"
            "DROP TABLE IF EXISTS ziele;"
            "DROP TABLE IF EXISTS baum_inhalt;"

            "CREATE TABLE IF NOT EXISTS Entities ( "
                "ID INTEGER NOT NULL, "
                "Bezeichnung TEXT, "
                "PRIMARY KEY(ID) "
            "); "
            "CREATE TABLE IF NOT EXISTS Properties ( "
                "ID INTEGER NOT NULL, "
                "Bezeichnung TEXT, "
                "adm_entity TEXT, "
                "adm_value TEXT, "
                "PRIMARY KEY(ID) "
            "); "
            "CREATE TABLE IF NOT EXISTS Statements ( "
                "ID INTEGER NOT NULL, "
                "entity INTEGER NOT NULL, "
                "property INTEGER NOT NULL, "
                "value TEXT, "
                "FOREIGN KEY (property) REFERENCES Properties (ID), "
                "PRIMARY KEY(ID) "
            "); "

            "CREATE TABLE baum_inhalt ("
            "node_id INTEGER PRIMARY KEY,"
            "parent_id INTEGER NOT NULL,"
            "older_sibling_id INTEGER NOT NULL,"
            "icon_name VARCHAR(50),"
            "node_text VARCHAR(200), "
            "FOREIGN KEY (parent_id) REFERENCES baum_inhalt (node_id) "
            "ON DELETE CASCADE ON UPDATE CASCADE, "
            "FOREIGN KEY (older_sibling_id) REFERENCES baum_inhalt (node_id) "
            "ON DELETE CASCADE ON UPDATE CASCADE ); "

            "INSERT INTO baum_inhalt (node_id, parent_id, older_sibling_id, "
            "node_text ) VALUES (0, 0, 0, 'zondv1');"

            //Hilfstabelle "dateien"
            //hier werden angebundene Dateien erfaßt
            "CREATE TABLE dateien ("
            "rel_path VARCHAR(200) PRIMARY KEY,"
            "node_id INTEGER NOT NULL, "
            "FOREIGN KEY (node_id) REFERENCES baum_inhalt (node_id) "
            "ON DELETE CASCADE ON UPDATE CASCADE);"

            //Hilfstabelle "ziele"
            //hier werden Anbindungen an Dateien mit Zusatzinfo abgelegt
            "CREATE TABLE ziele ("
            "ziel_id_von VARCHAR(50), "
            "index_von INTEGER, "
            "ziel_id_bis VARCHAR(50), "
            "index_bis INTEGER, "
            "rel_path VARCHAR(200) NOT NULL, "
            "node_id INTEGER NOT NULL, "
            "PRIMARY KEY (ziel_id_von, index_von, ziel_id_bis, index_bis), "
            "FOREIGN KEY (rel_path) REFERENCES dateien (rel_path) "
            "ON DELETE CASCADE ON UPDATE CASCADE,"
            "FOREIGN KEY (node_id) REFERENCES baum_inhalt (node_id) "
            "ON DELETE CASCADE ON UPDATE CASCADE );"

            //Auswertungs-Baum
            "CREATE TABLE baum_auswertung ( "
            "node_id INTEGER PRIMARY KEY,"
            "parent_id INTEGER NOT NULL,"
            "older_sibling_id INTEGER NOT NULL,"
            "icon_name VARCHAR(50),"
            "node_text VARCHAR(200),"
            "text VARCHAR, "
            "ref_id INTEGER NULL DEFAULT NULL,"
            "FOREIGN KEY (parent_id) REFERENCES baum_auswertung (node_id) "
            "ON DELETE CASCADE ON UPDATE CASCADE, "
            "FOREIGN KEY (older_sibling_id) REFERENCES baum_auswertung (node_id) "
            "ON DELETE CASCADE ON UPDATE CASCADE, "
            "FOREIGN KEY (ref_id) REFERENCES baum_inhalt (node_id) "
            "ON DELETE RESTRICT ON UPDATE RESTRICT );"

            "INSERT INTO baum_auswertung (node_id, parent_id, older_sibling_id, "
            "node_text) VALUES (0, 0, 0, 'root')";

    rc = sqlite3_exec( db, sql, NULL, NULL, &errmsg_ii );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_exec\nsql: ",
                sql, "\nresult code: ", sqlite3_errstr( rc ), "\nerrmsg: ",
                errmsg_ii, NULL );
        sqlite3_free( errmsg_ii );

        return FALSE;
    }

    return TRUE;
}


void
project_set_changed( gpointer user_data )
{
    Projekt* zond = (Projekt*) user_data;

    zond->changed = TRUE;
    gtk_widget_set_sensitive( zond->menu.speichernitem, TRUE );
    g_settings_set_boolean( zond->settings, "speichern", TRUE );

    return;
}


void
reset_project_changed( Projekt* zond )
{
    zond->changed = FALSE;
    gtk_widget_set_sensitive( zond->menu.speichernitem, FALSE );
    g_settings_set_boolean( zond->settings, "speichern", FALSE );

    return;
}


static void
project_destroy_stmts( Projekt* zond )
{
    sqlite3_finalize( zond->stmts.db_speichern_textview[0] );
    sqlite3_finalize( zond->stmts.db_set_datei[0] );
    sqlite3_finalize( zond->stmts.transaction[0] );
    sqlite3_finalize( zond->stmts.transaction[1] );
    sqlite3_finalize( zond->stmts.transaction[2] );
    sqlite3_finalize( zond->stmts.transaction_store[0] );
    sqlite3_finalize( zond->stmts.transaction_store[1] );
    sqlite3_finalize( zond->stmts.transaction_store[2] );
    sqlite3_finalize( zond->stmts.db_insert_node[0] );
    sqlite3_finalize( zond->stmts.db_insert_node[1] );
    sqlite3_finalize( zond->stmts.db_insert_node[2] );
    sqlite3_finalize( zond->stmts.db_insert_node[3] );
    sqlite3_finalize( zond->stmts.db_insert_node[4] );
    sqlite3_finalize( zond->stmts.ziele_einfuegen[0] );
    sqlite3_finalize( zond->stmts.db_kopieren_nach_auswertung[0] );
    sqlite3_finalize( zond->stmts.db_kopieren_nach_auswertung[1] );
    sqlite3_finalize( zond->stmts.db_kopieren_nach_auswertung[2] );
    sqlite3_finalize( zond->stmts.db_get_icon_name_and_node_text[0] );
    sqlite3_finalize( zond->stmts.db_get_icon_name_and_node_text[1] );
    sqlite3_finalize( zond->stmts.db_remove_node[0] );
    sqlite3_finalize( zond->stmts.db_remove_node[1] );
    sqlite3_finalize( zond->stmts.db_remove_node[2] );
    sqlite3_finalize( zond->stmts.db_remove_node[3] );
    sqlite3_finalize( zond->stmts.db_verschieben_knoten[0] );
    sqlite3_finalize( zond->stmts.db_verschieben_knoten[1] );
    sqlite3_finalize( zond->stmts.db_verschieben_knoten[2] );
    sqlite3_finalize( zond->stmts.db_verschieben_knoten[3] );
    sqlite3_finalize( zond->stmts.db_verschieben_knoten[4] );
    sqlite3_finalize( zond->stmts.db_verschieben_knoten[5] );
    sqlite3_finalize( zond->stmts.db_get_parent[0] );
    sqlite3_finalize( zond->stmts.db_get_parent[1] );
    sqlite3_finalize( zond->stmts.db_get_older_sibling[0] );
    sqlite3_finalize( zond->stmts.db_get_older_sibling[1] );
    sqlite3_finalize( zond->stmts.db_get_younger_sibling[0] );
    sqlite3_finalize( zond->stmts.db_get_younger_sibling[1] );
    sqlite3_finalize( zond->stmts.db_get_ref_id[0] );
    sqlite3_finalize( zond->stmts.db_get_ziel[0] );
    sqlite3_finalize( zond->stmts.db_get_text[0] );
    sqlite3_finalize( zond->stmts.db_get_rel_path[0] );
    sqlite3_finalize( zond->stmts.db_set_node_text[0] );
    sqlite3_finalize( zond->stmts.db_set_node_text[1] );
    sqlite3_finalize( zond->stmts.db_set_icon_id[0] );
    sqlite3_finalize( zond->stmts.db_set_icon_id[1] );
    sqlite3_finalize( zond->stmts.db_get_first_child[0] );
    sqlite3_finalize( zond->stmts.db_get_first_child[1] );
    sqlite3_finalize( zond->stmts.db_get_node_id_from_rel_path[0] );
    sqlite3_finalize( zond->stmts.db_check_id[0] );
    sqlite3_finalize( zond->stmts.db_update_path[0] );
    sqlite3_finalize( zond->stmts.db_update_path[1] );

    return;
}


static gint
project_create_stmts( Projekt* zond, gchar** errmsg )
{
    gint rc = 0;

/*  db_speichern_textview  */
    rc = sqlite3_prepare_v2( zond->db, "UPDATE baum_auswertung SET text=? WHERE "
            "node_id=?;", -1, &(zond->stmts.db_speichern_textview[0]), NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2 "
                "(db_speichern_textview):\n", sqlite3_errmsg( zond->db ), NULL );

        return -1;
    }

/*  db_set_datei */
    rc = sqlite3_prepare_v2( zond->db,
            "INSERT INTO dateien (rel_path, node_id) "
            "VALUES (?, ?); ", //datei_id, rel_path, last_insert_rowid
            -1, &(zond->stmts.db_set_datei[0]), NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2 "
                "(db_set_datei [0]):\n", sqlite3_errmsg( zond->db ), NULL );

        return -1;
    }

/*  transactions  */
    rc = sqlite3_prepare_v2( zond->db, "BEGIN;", -1, &(zond->stmts.transaction[0]), NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2 "
                "(BEGIN):\n",
                sqlite3_errmsg( zond->db ), NULL );

        return -1;
    }

    rc = sqlite3_prepare_v2( zond->db, "COMMIT;", -1, &(zond->stmts.transaction[1]), NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2 "
                "(COMMIT):\n",
                sqlite3_errmsg( zond->db ), NULL );

        return -1;
    }

    rc = sqlite3_prepare_v2( zond->db, "ROLLBACK;", -1, &(zond->stmts.transaction[2]), NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2 "
                "(ROLLBACK):\n",
                sqlite3_errmsg( zond->db ), NULL );

        return -1;
    }

/*  transactions_store  */
    rc = sqlite3_prepare_v2( zond->db_store, "BEGIN;", -1, &(zond->stmts.transaction_store[0]), NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2 "
                "(BEGIN db_store):\n",
                sqlite3_errmsg( zond->db_store ), NULL );

        return -1;
    }

    rc = sqlite3_prepare_v2( zond->db_store, "COMMIT;", -1, &(zond->stmts.transaction_store[1]), NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2 "
                "(COMMIT db_store):\n",
                sqlite3_errmsg( zond->db_store ), NULL );

        return -1;
    }

    rc = sqlite3_prepare_v2( zond->db_store, "ROLLBACK;", -1, &(zond->stmts.transaction_store[2]), NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2 "
                "(ROLLBACK db_Store):\n",
                sqlite3_errmsg( zond->db_store ), NULL );

        return -1;
    }

/*  db_insert_node  */
    rc = sqlite3_prepare_v2( zond->db,
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
            -1, &(zond->stmts.db_insert_node[0]), NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2 "
                "(db_insert_node[0]):\n", sqlite3_errmsg( zond->db ), NULL );

        return -1;
    }

    rc = sqlite3_prepare_v2( zond->db,
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
            -1, &(zond->stmts.db_insert_node[1]), NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2 "
                "(db_insert_node[1]):\n", sqlite3_errmsg( zond->db ), NULL );

        return -1;
    }

    rc = sqlite3_prepare_v2( zond->db,
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
            -1, &(zond->stmts.db_insert_node[2]), NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2 "
                "(db_insert_node [2]):\n", sqlite3_errmsg( zond->db ), NULL );

        return -1;
    }

    rc = sqlite3_prepare_v2( zond->db,
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
            -1, &(zond->stmts.db_insert_node[3]), NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2 "
                "(db_insert_node [3]):\n", sqlite3_errmsg( zond->db ), NULL );

        return -1;
    }

    rc = sqlite3_prepare_v2( zond->db, "VALUES (last_insert_rowid()); ",
            -1, &(zond->stmts.db_insert_node[4]), NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2 "
                "(db_insert_node [4]):\n", sqlite3_errmsg( zond->db ), NULL );

        return -1;
    }

/*  ziele_einfuegen  */
    rc = sqlite3_prepare_v2( zond->db, "INSERT INTO ziele (ziel_id_von, index_von, "
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
            -1, &(zond->stmts.ziele_einfuegen[0]), NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2 "
                "(ziele_einfuegen [0]):\n", sqlite3_errmsg( zond->db ), NULL );

        return -1;
    }

/*  db_knoten_nach_auswertung  */
    rc = sqlite3_prepare_v2( zond->db,
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
            -1, &(zond->stmts.db_kopieren_nach_auswertung[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "Bei Aufruf sqlite3_prepare_v2 "
                "(db_kopieren_nach_auswertung [0]):\n" )

    rc = sqlite3_prepare_v2( zond->db,
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
            -1, &(zond->stmts.db_kopieren_nach_auswertung[1]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "Bei Aufruf sqlite3_prepare_v2 "
                "(db_kopieren_nach_auswertung [1]):\n" )

    rc = sqlite3_prepare_v2( zond->db,
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
            -1, &(zond->stmts.db_kopieren_nach_auswertung[2]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "Bei Aufruf sqlite3_prepare_v2 "
                "(db_kopieren_nach_auswertung [2]):\n" )

/*  db_get_icon_name_and_node_text  */
    rc = sqlite3_prepare_v2( zond->db,
            "SELECT icon_name, node_text, node_id FROM baum_inhalt WHERE node_id=?;",
            -1, &(zond->stmts.db_get_icon_name_and_node_text[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_get_icon_name_and_node_text [0]):\n" )

    rc = sqlite3_prepare_v2( zond->db,
            "SELECT icon_name, node_text, node_id FROM baum_auswertung WHERE node_id=?;",
            -1, &(zond->stmts.db_get_icon_name_and_node_text[1]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_get_icon_name_and_node_text [1]):\n" )

/*  db_remove_node  */
    rc = sqlite3_prepare_v2( zond->db,
            "UPDATE baum_inhalt SET older_sibling_id=(SELECT older_sibling_id FROM baum_inhalt "
            "WHERE node_id=?1) WHERE "
            "older_sibling_id=?1; ",
            -1, &(zond->stmts.db_remove_node[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_remove_node[0]):\n" )

    rc = sqlite3_prepare_v2( zond->db,
            "UPDATE baum_auswertung SET older_sibling_id=(SELECT older_sibling_id FROM baum_auswertung "
            "WHERE node_id=?1) WHERE "
            "older_sibling_id=?1; ",
            -1, &(zond->stmts.db_remove_node[1]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_remove_node[1]):\n" )

    rc = sqlite3_prepare_v2( zond->db,
            "DELETE FROM baum_inhalt WHERE node_id = ?;",
            -1, &(zond->stmts.db_remove_node[2]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_remove_node[2]):\n" )

    rc = sqlite3_prepare_v2( zond->db,
            "DELETE FROM baum_auswertung WHERE node_id = ?;",
            -1, &(zond->stmts.db_remove_node[3]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_remove_node[3]):\n" )

/*  db_verschieben_knoten  */
    //knoten herauslösen = older_sibling von younger sibling verbiegen
    rc = sqlite3_prepare_v2( zond->db,
            "UPDATE baum_inhalt SET older_sibling_id="
            "(SELECT older_sibling_id FROM baum_inhalt WHERE node_id=?1)" //node_id
            "WHERE older_sibling_id=?1; ",
            -1, &(zond->stmts.db_verschieben_knoten[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_verschieben_knoten[0])" )

    rc = sqlite3_prepare_v2( zond->db,
            "UPDATE baum_auswertung SET older_sibling_id="
            "(SELECT older_sibling_id FROM baum_auswertung WHERE node_id=?1)"
            "WHERE older_sibling_id=?1; ",
            -1, &(zond->stmts.db_verschieben_knoten[1]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_verschieben_knoten[1])" )

    //older_sibling von neuem younger_sibling verbiegen
    rc = sqlite3_prepare_v2( zond->db,
            "UPDATE baum_inhalt SET older_sibling_id=?1 WHERE node_id=" //node_id
                "(SELECT node_id FROM baum_inhalt WHERE parent_id=?2 AND older_sibling_id=?3); ", //new_parent_id/new_older_s_id
            -1, &(zond->stmts.db_verschieben_knoten[2]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_verschieben_knoten[2])" )

    rc = sqlite3_prepare_v2( zond->db,
            "UPDATE baum_auswertung SET older_sibling_id=?1 WHERE node_id=" //node_id
                "(SELECT node_id FROM baum_auswertung WHERE parent_id=?2 AND older_sibling_id=?3); ", //new_parent_id/new_older_s_id
            -1, &(zond->stmts.db_verschieben_knoten[3]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_verschieben_knoten[3])" )

    rc = sqlite3_prepare_v2( zond->db,
            "UPDATE baum_inhalt SET parent_id=?1, older_sibling_id=?2 WHERE node_id=?3; ",
            -1, &(zond->stmts.db_verschieben_knoten[4]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_verschieben_knoten[4])" )

    rc = sqlite3_prepare_v2( zond->db,
            "UPDATE baum_auswertung SET parent_id=?1, older_sibling_id=?2 WHERE node_id=?3; ",
            -1, &(zond->stmts.db_verschieben_knoten[5]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_verschieben_knoten[5])" )

/*  db_get_parent  */
    rc = sqlite3_prepare_v2( zond->db,
            "SELECT parent_id FROM baum_inhalt WHERE node_id = ?;",
            -1, &(zond->stmts.db_get_parent[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_get_parent[0])" )

    rc = sqlite3_prepare_v2( zond->db,
            "SELECT parent_id FROM baum_auswertung WHERE node_id = ?;",
            -1, &(zond->stmts.db_get_parent[1]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_get_parent[1])" )

/*  db_get_older_sibling  */
    rc = sqlite3_prepare_v2( zond->db,
            "SELECT older_sibling_id FROM baum_inhalt WHERE node_id = ?;",
            -1, &(zond->stmts.db_get_older_sibling[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_get_older_sibling[0])" )

    rc = sqlite3_prepare_v2( zond->db,
            "SELECT older_sibling_id FROM baum_auswertung WHERE node_id = ?;",
            -1, &(zond->stmts.db_get_older_sibling[1]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_get_older_sibling[1])" )

/*  db_get_younger_sibling  */
    rc = sqlite3_prepare_v2( zond->db,
            "SELECT inhalt1.node_id, inhalt2.node_id FROM baum_inhalt AS inhalt1 "
                "LEFT JOIN baum_inhalt AS inhalt2 "
                "ON inhalt1.node_id = inhalt2.older_sibling_id "
                "WHERE inhalt1.node_id > 0 AND inhalt1.node_id = ?;",
            -1, &(zond->stmts.db_get_younger_sibling[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_get_younger_sibling[0])" )

    rc = sqlite3_prepare_v2( zond->db,
            "SELECT inhalt1.node_id, inhalt2.node_id FROM baum_auswertung AS inhalt1 "
                "LEFT JOIN baum_auswertung AS inhalt2 "
                "ON inhalt1.node_id = inhalt2.older_sibling_id "
                "WHERE inhalt1.node_id > 0 AND inhalt1.node_id = ?;",
            -1, &(zond->stmts.db_get_younger_sibling[1]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_get_younger_sibling[1])" )

/*  db_get_ref_id  */
    rc = sqlite3_prepare_v2( zond->db,
            "SELECT ref_id FROM baum_auswertung WHERE node_id=?",
            -1, &(zond->stmts.db_get_ref_id[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_get_ref_id[0])" )

/*  db_get_ziel  */
    rc = sqlite3_prepare_v2( zond->db,
            "SELECT ziel_id_von, index_von, ziel_id_bis, index_bis FROM ziele "
            "WHERE node_id=?;",
            -1, &(zond->stmts.db_get_ziel[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_get_ziel[0])" )

/*  db_get_text  */
    rc = sqlite3_prepare_v2( zond->db,
            "SELECT node_id, text FROM baum_auswertung WHERE node_id = ?;",
            -1, &(zond->stmts.db_get_text[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_get_text[0])" )

/*  db_get_rel_path  */
    rc = sqlite3_prepare_v2( zond->db,
            "SELECT baum_inhalt.node_id, rel_path FROM baum_inhalt "
            "LEFT JOIN "
            "(SELECT dateien.rel_path AS rel_path, dateien.node_id AS d_node_id, "
            "ziele.node_id AS z_node_id FROM dateien LEFT JOIN ziele "
            "ON dateien.rel_path=ziele.rel_path) "
            "ON baum_inhalt.node_id=d_node_id OR "
            "baum_inhalt.node_id=z_node_id "
            "WHERE baum_inhalt.node_id=?;",
            -1, &(zond->stmts.db_get_rel_path[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_get_rel_path[0])" )

/*  db_set_node_text  */
    rc = sqlite3_prepare_v2( zond->db,
            "UPDATE baum_inhalt SET node_text = ?1 WHERE node_id = ?2;",
            -1, &(zond->stmts.db_set_node_text[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_set_node_text[0])" )

    rc = sqlite3_prepare_v2( zond->db,
            "UPDATE baum_auswertung SET node_text = ?1 WHERE node_id = ?2;",
            -1, &(zond->stmts.db_set_node_text[1]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_set_node_text[1])" )

/*  db_set_icon_name  */
    rc = sqlite3_prepare_v2( zond->db,
            "UPDATE baum_inhalt SET icon_name = ?1 WHERE node_id = ?2;",
            -1, &(zond->stmts.db_set_icon_id[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_set_icon_id[0])" )

    rc = sqlite3_prepare_v2( zond->db,
            "UPDATE baum_auswertung SET icon_name = ?1 WHERE node_id = ?2;",
            -1, &(zond->stmts.db_set_icon_id[1]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_set_icon_id[1])" )

/*  db_get_first_child  */
    rc = sqlite3_prepare_v2( zond->db,
            "SELECT inhalt1.node_id, inhalt2.node_id FROM baum_inhalt AS inhalt1 "
                "LEFT JOIN baum_inhalt AS inhalt2 "
                "ON inhalt1.node_id = inhalt2.parent_id AND inhalt2.older_sibling_id = 0 "
                    "AND inhalt2.node_id != 0 "
                "WHERE inhalt1.node_id = ?;",
            -1, &(zond->stmts.db_get_first_child[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_get_first_child[0])" )

    rc = sqlite3_prepare_v2( zond->db,
            "SELECT inhalt1.node_id, inhalt2.node_id FROM baum_auswertung AS inhalt1 "
                "LEFT JOIN baum_auswertung AS inhalt2 "
                "ON inhalt1.node_id = inhalt2.parent_id AND inhalt2.older_sibling_id = 0 "
                    "AND inhalt2.node_id != 0 "
                "WHERE inhalt1.node_id = ?;",
            -1, &(zond->stmts.db_get_first_child[1]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_get_first_child[1])" )

/*  db_get_node_id_from_rel_path  */
    rc = sqlite3_prepare_v2( zond->db,
        "SELECT node_id FROM dateien WHERE rel_path=?;",
            -1, &(zond->stmts.db_get_node_id_from_rel_path[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_get_node_id_from_rel_path[0])" )

/*  db_check_id  */
    rc = sqlite3_prepare_v2( zond->db,
        "SELECT ziel_id_von FROM ziele WHERE ziel_id_von=?1;"
        "UNION "
        "SELECT ziel_id_bis FROM ziele WHERE ziel_id_bis=?1;",
            -1, &(zond->stmts.db_check_id[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_check_id[0])" )

/*  db_update_path  */
    rc = sqlite3_prepare_v2( zond->db,
            "UPDATE dateien SET rel_path = REPLACE( SUBSTR( rel_path, 1, LENGTH( ?1 ) ), ?1, ?2 ) || SUBSTR( rel_path, LENGTH( ?1 ) + 1 );",
            -1, &(zond->stmts.db_update_path[0]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_update_path[0])" )

    rc = sqlite3_prepare_v2( zond->db_store,
            "UPDATE dateien SET rel_path = REPLACE( SUBSTR( rel_path, 1, LENGTH( ?1 ) ), ?1, ?2 ) || SUBSTR( rel_path, LENGTH( ?1 ) + 1 );",
            -1, &(zond->stmts.db_update_path[1]), NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_prepare_v2 "
                "(db_update_path[1])" )

    return 0;
}


void
projekt_set_widgets_sensitiv( Projekt* zond, gboolean active )
{
    gtk_widget_set_sensitive( GTK_WIDGET(zond->menu.schliessenitem), active );
    gtk_widget_set_sensitive( GTK_WIDGET(zond->menu.exportitem), active );
    gtk_widget_set_sensitive( GTK_WIDGET(zond->menu.pdf), active );
    gtk_widget_set_sensitive( GTK_WIDGET(zond->menu.struktur), active );
    gtk_widget_set_sensitive( GTK_WIDGET(zond->menu.suchen), active );
    gtk_widget_set_sensitive( GTK_WIDGET(zond->menu.ansicht), active );
    gtk_widget_set_sensitive( GTK_WIDGET(zond->fs_button), active );
//    gtk_widget_set_sensitive( GTK_WIDGET(zond->menu.extras), TRUE );

    return;
}


static gint
projekt_init( Projekt* zond, gboolean disc, gchar** errmsg )
{
    gint rc = 0;

    gchar* origin = g_strconcat( zond->project_dir, "/", zond->project_name, NULL );
    rc = sqlite3_open( origin, &(zond->db_store) ); //gibt auch dann ok zurück, wenn keine sqlite-Datei!
    g_free( origin );

    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strdup( "Datei ist "
                "keine Sqlite-Datenbank" );

        return -1;
    }

    if ( disc ) if ( !db_create( zond->db_store, errmsg ) )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf db_create:\n",
                sqlite3_errmsg( zond->db_store ), NULL );

        return -1;
    }

    //Arbeitskopie
    gchar* working_copy = g_strconcat( zond->project_dir, "/", zond->project_name,
            ".tmp", NULL );
    rc = sqlite3_open( working_copy, &(zond->db) );
    g_free( working_copy );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite_open(working_copy):\n",
                sqlite3_errmsg( zond->db ), NULL );

        return -1;
    }

    //in Arbeitskopie kopieren
    rc = db_backup( zond->db_store, zond->db, errmsg );
    if ( rc ) ERROR_PAO( "db_backup" )

    sqlite3_update_hook( zond->db, (void*) project_set_changed, (gpointer) zond );

    //Damit foreign keys funktionieren
    gchar* errmsg_ii = NULL;
    gchar* sql = "PRAGMA foreign_keys = ON; PRAGMA case_sensitive_like = ON";
    rc = sqlite3_exec( zond->db, sql, NULL, NULL, &errmsg_ii );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_exec\nsql: ",
                sql, "\nresult code: ", sqlite3_errstr( rc ), "\n", errmsg_ii, NULL );
        sqlite3_free( errmsg_ii );

        return -1;
    }

    rc = project_create_stmts( zond, errmsg );
    if ( rc ) ERROR_PAO( "project_create_stmts" )

    rc = fs_tree_load_dir( zond, NULL, errmsg );
    if ( rc ) ERROR_PAO( "fs_tree_load_dir" )

    return 0;
}


gint
projekt_aktivieren( Projekt* zond, gchar* project, gboolean disc, gchar** errmsg )
{
    gint rc = 0;

    //Pfad aus filename entfernen
    gchar* mark = strrchr( project, '/' );
    zond->project_name = g_strdup( mark  + 1 );

    //project_dir
    zond->project_dir = g_strndup( project, strlen( project ) - strlen(
            mark ) );

    //zum Arbeitsverzeichnis machen
    g_chdir( zond->project_dir );

//Datenbankdateien öffnen
    //Ursprungsdatei
    rc = projekt_init( zond, disc, errmsg );

    if ( rc )
    {
        sqlite3_close( zond->db_store );
        sqlite3_close( zond->db );

        zond->db_store = NULL;
        zond->db = NULL;

        g_free( zond->project_name );
        g_free( zond->project_dir );

        zond->project_name = NULL;
        zond->project_dir = NULL;

        ERROR_PAO( "project_init" )
    }

    projekt_set_widgets_sensitiv( zond, TRUE );

    //project_name als Titel Headerbar
    gtk_header_bar_set_title(
            GTK_HEADER_BAR(gtk_window_get_titlebar(
            GTK_WINDOW(zond->app_window) )), zond->project_name );

    //project_name in settings schreiben
    gchar* set = g_strconcat( zond->project_dir, "/", zond->project_name, NULL );
    g_settings_set_string( zond->settings, "project", set );
    g_free( set );

    //project_dir in fs_treeview
    g_object_set_data( G_OBJECT(zond->treeview[BAUM_FS]), "root", zond->project_dir );

    reset_project_changed( zond );

    return 0;
}


void projekt_schliessen( Projekt* zond )
{
    if ( !zond->project_name ) return;

    //Menus aktivieren/ausgrauen
    projekt_set_widgets_sensitiv( zond, FALSE );

    //damit focus aus text_view rauskommt und changed-signal abgeschaltet wird
    gtk_widget_grab_focus( GTK_WIDGET(zond->treeview[BAUM_INHALT]) );

    //textview leeren
    GtkTextBuffer* buffer = gtk_text_view_get_buffer( zond->textview );
    gtk_text_buffer_set_text( buffer, "", -1 );

    g_object_set_data( G_OBJECT(buffer), "changed", NULL );
    g_object_set_data( G_OBJECT(zond->textview), "node-id", NULL );

    reset_project_changed( zond );

    //Vor leeren der treeviews: focus-in-callback blocken
    //darin wird cursor-changed-callback angeschaltet --> Absturz
    g_signal_handler_block( zond->treeview[BAUM_INHALT],
            zond->treeview_focus_in_signal[BAUM_INHALT] );
    g_signal_handler_block( zond->treeview[BAUM_AUSWERTUNG],
            zond->treeview_focus_in_signal[BAUM_AUSWERTUNG] );

    //treeviews leeren
    gtk_tree_store_clear( GTK_TREE_STORE(gtk_tree_view_get_model(
            zond->treeview[BAUM_INHALT] )) );
    gtk_tree_store_clear( GTK_TREE_STORE(gtk_tree_view_get_model(
            zond->treeview[BAUM_AUSWERTUNG] )) );

    //Wieder anschalten
    g_signal_handler_unblock( zond->treeview[BAUM_INHALT],
            zond->treeview_focus_in_signal[BAUM_INHALT] );
    g_signal_handler_unblock( zond->treeview[BAUM_AUSWERTUNG],
            zond->treeview_focus_in_signal[BAUM_AUSWERTUNG] );

    //muß vor project_destroy..., weil callback ausgelöst wird, der db_get_node_id... aufruft
    gtk_tree_store_clear( GTK_TREE_STORE(gtk_tree_view_get_model(
            zond->treeview[BAUM_FS] )) );

    //prepared statements zerstören
    project_destroy_stmts( zond );

    //Datenbanken schließen
    gint rc1 = sqlite3_close( zond->db_store );
    gint rc2 = sqlite3_close( zond->db );

    //Abfrage, welche stmts noch offen sind (falls ich irgendwo noch welche
    //vergessen habe
    if ( rc1 == SQLITE_BUSY || rc2 == SQLITE_BUSY )
    {
        sqlite3_stmt* stmt = NULL;
        sqlite3_stmt* next = NULL;
        gchar* sql = g_strdup( "" );
        do
        {
            stmt = sqlite3_next_stmt( zond->db, next );
            sql = add_string( sql, g_strdup( sqlite3_sql( stmt ) ) );
            sql = add_string( sql, g_strdup( "\n" ) );
            if ( stmt ) next = stmt;
        } while ( stmt );
        meldung( zond->app_window, "Datenbanken können nicht geschlossen "
                "werden:\n\n", sql, NULL );
        g_free( sql );
    }

    gchar* working_copy = g_strconcat( zond->project_dir, "/",
            zond->project_name, ".tmp", NULL );
    gint res = g_remove( working_copy );
    if ( res == -1) meldung( zond->app_window, "Fehler beim Löschen der "
            "temporären Datenbank:\n", strerror( errno ), NULL );
    g_free( working_copy );

    //project_dir in fs_treeview auf NULL setzen
    g_object_set_data( G_OBJECT(zond->treeview[BAUM_FS]), "root", NULL );

    //project-Namen zurücksetzen
    g_free( zond->project_name );
    g_free( zond->project_dir );
    zond->project_name = NULL;
    zond->project_dir = NULL;

    gtk_header_bar_set_title(
            GTK_HEADER_BAR(gtk_window_get_titlebar(
            GTK_WINDOW(zond->app_window) )), "" );

    //project in settings auf leeren String setzen
    g_settings_set_string( zond->settings, "project", "" );

    return;
}


void
cb_menu_datei_speichern_activate( GtkMenuItem* item, gpointer user_data )
{
    Projekt* zond = (Projekt*) user_data;

    if ( !(zond->changed) ) return;

    gint rc = 0;
    gchar* errmsg = NULL;

    rc = db_backup( zond->db, zond->db_store, &errmsg );
    if ( rc )
    {
        meldung( zond->app_window, "Fehler beim Speichern:\n Bei Aufruf "
                "db_backup\n", errmsg, NULL );
        g_free( errmsg );
    }
    else reset_project_changed( zond );

    return;
}


void
cb_menu_datei_schliessen_activate( GtkMenuItem* item, gpointer user_data )
{
    Projekt* zond = (Projekt*) user_data;

    if ( zond->changed )
    {
        gint rc = 0;
        rc = abfrage_frage( zond->app_window, "Datei schließen", "Änderungen "
                "aktuelles Projekt speichern?", NULL );

        if ( rc == GTK_RESPONSE_YES ) cb_menu_datei_speichern_activate( NULL, zond );
        else if ( rc != GTK_RESPONSE_NO) return;
    }

    projekt_schliessen( (Projekt*) zond );

    return;
}


void
cb_menu_datei_oeffnen_activate( GtkMenuItem* item, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    Projekt* zond = (Projekt*) user_data;

    //nachfragen, ob aktuelles Projekt gespeichert werden soll
    if ( zond->project_name != NULL )
    {
        rc = abfrage_frage( zond->app_window, "Datei öffnen", "Projekt wechseln?", NULL );
        if( (rc != GTK_RESPONSE_YES) ) return; //Abbrechen -> nicht öffnen
    }

    if ( zond->changed )
    {
        rc = abfrage_frage( zond->app_window, "Datei öffnen", "Änderungen "
                "aktuelles Projekt speichern?", NULL );

        if ( rc == GTK_RESPONSE_YES ) cb_menu_datei_speichern_activate( NULL, zond );
        else if ( rc != GTK_RESPONSE_NO) return;
    }

    gchar* abs_path = filename_oeffnen( GTK_WINDOW(zond->app_window) );
    if ( !abs_path ) return;

    projekt_schliessen( zond );
    rc = projekt_aktivieren( zond, abs_path, FALSE, &errmsg );
    g_free( abs_path );
    if ( rc )
    {
        meldung( zond->app_window, "Bei Aufruf projekt_aktivieren:\n",
                errmsg, NULL );
        g_free( errmsg );

        return;
    }

    rc = db_baum_refresh( zond, &errmsg );
    if ( rc == -1 )
    {
        meldung( zond->app_window, "Fehler beim Öffnen des Projekts:\nBei "
                "Aufruf db_baum_refresh:\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


void
cb_menu_datei_neu_activate( GtkMenuItem* item, gpointer user_data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    Projekt* zond = (Projekt*) user_data;

    //nachfragen, ob aktuelles Projekt gespeichert werden soll
    if ( zond->project_name != NULL )
    {
        rc = abfrage_frage( zond->app_window, "Neues Projekt", "Projekt wechseln?", NULL );
        if( rc != GTK_RESPONSE_YES ) return; //kein neues Projekt anlegen
    }

    if ( zond->changed )
    {
        rc = abfrage_frage( zond->app_window, "Neues Projekt", "Änderungen "
                "aktuelles Projekt speichern?", NULL );

        if ( rc == GTK_RESPONSE_YES ) cb_menu_datei_speichern_activate( NULL, zond );
        else if ( rc != GTK_RESPONSE_NO) return;

        reset_project_changed( zond );
    }

    gchar* abs_path = filename_speichern( GTK_WINDOW(zond->app_window),
            "Projekt anlegen" );
    if ( !abs_path ) return;

    projekt_schliessen( zond );
    rc = projekt_aktivieren( zond, abs_path, TRUE, &errmsg );
    g_free( abs_path );
    if ( rc )
    {
        meldung( zond->app_window, "Bei Aufruf projekt_aktivieren:\n",
                errmsg, NULL );
        g_free( errmsg );

        return;
    }

    reset_project_changed( zond );

    return;
}


