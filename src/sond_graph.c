/*
 sond (sond_graph.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2025  peloamerica

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

#include <mysql.h>
#include <glib.h>

/**
 * Richtet die Datenbank ein (Tabellen nodes, edges, node_locks)
 * Erstellt nur fehlende Tabellen, vorhandene werden nicht überschrieben
 *
 * @param conn Aktive MySQL-Verbindung
 * @return TRUE bei Erfolg, FALSE bei Fehler
 */
gboolean setup_database(MYSQL *conn) {
    g_return_val_if_fail(conn != NULL, FALSE);

    const gchar *queries[] = {
        // 1. Nodes Tabelle
        "CREATE TABLE IF NOT EXISTS nodes ("
        "  node_id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,"
        "  label VARCHAR(100) NOT NULL,"
        "  properties JSON DEFAULT NULL,"
        "  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "  updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
        "  INDEX idx_label (label)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci",

        // 2. Edges Tabelle
        "CREATE TABLE IF NOT EXISTS edges ("
        "  edge_id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,"
        "  source_node_id BIGINT UNSIGNED NOT NULL,"
        "  target_node_id BIGINT UNSIGNED NOT NULL,"
        "  label VARCHAR(100) NOT NULL,"
        "  properties JSON DEFAULT NULL,"
        "  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "  FOREIGN KEY (source_node_id) REFERENCES nodes(node_id) ON DELETE CASCADE,"
        "  FOREIGN KEY (target_node_id) REFERENCES nodes(node_id) ON DELETE CASCADE,"
        "  INDEX idx_source (source_node_id),"
        "  INDEX idx_target (target_node_id),"
        "  INDEX idx_label (label),"
        "  INDEX idx_source_label (source_node_id, label)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci",

        // 3. Node Locks Tabelle
        "CREATE TABLE IF NOT EXISTS node_locks ("
        "  node_id BIGINT UNSIGNED PRIMARY KEY,"
        "  locked_by VARCHAR(100) NOT NULL,"
        "  locked_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "  expires_at TIMESTAMP NOT NULL,"
        "  client_info VARCHAR(255),"
        "  FOREIGN KEY (node_id) REFERENCES nodes(node_id) ON DELETE CASCADE,"
        "  INDEX idx_expires (expires_at),"
        "  INDEX idx_locked_by (locked_by)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci",

        NULL  // Terminator
    };

    // Transaktion starten
    if (mysql_query(conn, "START TRANSACTION")) {
        g_warning("Konnte Transaktion nicht starten: %s", mysql_error(conn));
        return FALSE;
    }

    // Alle Queries ausführen
    for (gint i = 0; queries[i] != NULL; i++) {
        g_debug("Führe aus: %.50s...", queries[i]);

        if (mysql_query(conn, queries[i])) {
            g_warning("Fehler beim Erstellen von Tabelle %d: %s",
                     i + 1, mysql_error(conn));
            mysql_query(conn, "ROLLBACK");
            return FALSE;
        }

        g_info("Tabelle %d erfolgreich erstellt/überprüft", i + 1);
    }

    // Commit
    if (mysql_query(conn, "COMMIT")) {
        g_warning("Fehler beim Commit: %s", mysql_error(conn));
        mysql_query(conn, "ROLLBACK");
        return FALSE;
    }

    g_info("Datenbank erfolgreich eingerichtet!");
    return TRUE;
}

/**
 * Prüft ob alle notwendigen Tabellen existieren
 *
 * @param conn Aktive MySQL-Verbindung
 * @return TRUE wenn alle Tabellen existieren, sonst FALSE
 */
gboolean check_database_schema(MYSQL *conn) {
    g_return_val_if_fail(conn != NULL, FALSE);

    const gchar *tables[] = {"nodes", "edges", "node_locks", NULL};

    for (gint i = 0; tables[i] != NULL; i++) {
        gchar *query = g_strdup_printf(
            "SHOW TABLES LIKE '%s'", tables[i]
        );

        if (mysql_query(conn, query)) {
            g_free(query);
            return FALSE;
        }

        MYSQL_RES *result = mysql_store_result(conn);
        gint num_rows = mysql_num_rows(result);
        mysql_free_result(result);
        g_free(query);

        if (num_rows == 0) {
            g_info("Tabelle '%s' fehlt", tables[i]);
            return FALSE;
        }
    }

    g_info("Alle Tabellen vorhanden");
    return TRUE;
}

/**
 * Initialisiert die Datenbank-Verbindung und richtet Schema ein
 *
 * @param host Hostname (z.B. "localhost")
 * @param user Benutzername
 * @param password Passwort
 * @param database Datenbankname
 * @return MySQL-Verbindung oder NULL bei Fehler
 */
MYSQL* init_database(const gchar *host,
                     const gchar *user,
                     const gchar *password,
                     const gchar *database) {
    g_return_val_if_fail(host != NULL, NULL);
    g_return_val_if_fail(user != NULL, NULL);
    g_return_val_if_fail(database != NULL, NULL);

    // Verbindung initialisieren
    MYSQL *conn = mysql_init(NULL);
    if (!conn) {
        g_error("mysql_init() fehlgeschlagen");
        return NULL;
    }

    // Verbinden
    if (!mysql_real_connect(conn, host, user, password, database,
                           0, NULL, 0)) {
        g_error("Verbindung fehlgeschlagen: %s", mysql_error(conn));
        mysql_close(conn);
        return NULL;
    }

    g_info("Verbunden mit Datenbank '%s' auf '%s'", database, host);

    // Schema prüfen
    if (!check_database_schema(conn)) {
        g_info("Schema unvollständig, richte Datenbank ein...");

        if (!setup_database(conn)) {
            g_error("Konnte Datenbank nicht einrichten");
            mysql_close(conn);
            return NULL;
        }
    }

    return conn;
}

/**
 * Löscht alle Tabellen (VORSICHT!)
 * Nützlich für Tests oder komplettes Reset
 */
gboolean drop_all_tables(MYSQL *conn) {
    g_return_val_if_fail(conn != NULL, FALSE);

    g_warning("ACHTUNG: Lösche alle Tabellen!");

    const gchar *queries[] = {
        "SET FOREIGN_KEY_CHECKS = 0",
        "DROP TABLE IF EXISTS node_locks",
        "DROP TABLE IF EXISTS edges",
        "DROP TABLE IF EXISTS nodes",
        "SET FOREIGN_KEY_CHECKS = 1",
        NULL
    };

    for (gint i = 0; queries[i] != NULL; i++) {
        if (mysql_query(conn, queries[i])) {
            g_warning("Fehler beim Löschen: %s", mysql_error(conn));
            return FALSE;
        }
    }

    g_info("Alle Tabellen gelöscht");
    return TRUE;
}

