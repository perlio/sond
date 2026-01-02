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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mysql/mysql.h>
#include <glib.h>

// Error Domain
#define GRAPH_DB_ERROR graph_db_error_quark()
GQuark graph_db_error_quark(void) {
    return g_quark_from_static_string("graph-db-error-quark");
}

// Error Codes
typedef enum {
    GRAPH_DB_ERROR_INIT,
    GRAPH_DB_ERROR_CONNECTION,
    GRAPH_DB_ERROR_SQL,
    GRAPH_DB_ERROR_CONFIG
} GraphDBError;

// Verbindungsparameter
typedef struct {
    const char *host;
    const char *user;
    const char *password;
    const char *database;
    unsigned int port;
} DBConfig;

// SQL-Statements für das Setup
const char *sql_statements[] = {
    // Nodes-Tabelle
    "CREATE TABLE IF NOT EXISTS nodes ("
    "  id BIGINT PRIMARY KEY AUTO_INCREMENT,"
    "  label VARCHAR(50) NOT NULL,"
    "  properties JSON NOT NULL DEFAULT ('[]'),"
    "  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
    "  updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
    "  INDEX idx_nodes_label (label)"
    ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci",

    // Edges-Tabelle
    "CREATE TABLE IF NOT EXISTS edges ("
    "  id BIGINT PRIMARY KEY AUTO_INCREMENT,"
    "  source_id BIGINT NOT NULL,"
    "  target_id BIGINT NOT NULL,"
    "  label VARCHAR(50) NOT NULL,"
    "  properties JSON NOT NULL DEFAULT ('[]'),"
    "  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
    "  updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
    "  FOREIGN KEY (source_id) REFERENCES nodes(id) ON DELETE CASCADE,"
    "  FOREIGN KEY (target_id) REFERENCES nodes(id) ON DELETE CASCADE,"
    "  INDEX idx_edges_source (source_id),"
    "  INDEX idx_edges_target (target_id),"
    "  INDEX idx_edges_label (label),"
    "  INDEX idx_edges_source_target (source_id, target_id)"
    ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci",

    // Node-Locks-Tabelle
    "CREATE TABLE IF NOT EXISTS node_locks ("
    "  node_id BIGINT PRIMARY KEY,"
    "  locked_by VARCHAR(100) NOT NULL,"
    "  locked_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
    "  expires_at TIMESTAMP NULL,"
    "  lock_reason VARCHAR(255) NULL,"
    "  FOREIGN KEY (node_id) REFERENCES nodes(id) ON DELETE CASCADE,"
    "  INDEX idx_locks_user (locked_by),"
    "  INDEX idx_locks_expires (expires_at)"
    ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci",

    // Lock-Historie
    "CREATE TABLE IF NOT EXISTS lock_history ("
    "  id BIGINT PRIMARY KEY AUTO_INCREMENT,"
    "  node_id BIGINT NOT NULL,"
    "  locked_by VARCHAR(100) NOT NULL,"
    "  locked_at TIMESTAMP NOT NULL,"
    "  unlocked_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
    "  duration_seconds INT GENERATED ALWAYS AS (TIMESTAMPDIFF(SECOND, locked_at, unlocked_at)) STORED,"
    "  lock_reason VARCHAR(255) NULL,"
    "  INDEX idx_history_node (node_id),"
    "  INDEX idx_history_user (locked_by)"
    ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci",

    // Prozeduren löschen falls vorhanden
    "DROP PROCEDURE IF EXISTS lock_node",
    "DROP PROCEDURE IF EXISTS unlock_node",
    "DROP PROCEDURE IF EXISTS unlock_all_by_user",
    "DROP PROCEDURE IF EXISTS cleanup_expired_locks",
    "DROP PROCEDURE IF EXISTS check_lock_status",

    NULL  // Terminator
};

// Stored Procedures
const char *procedures[] = {
    // lock_node
    "CREATE PROCEDURE lock_node("
    "  IN p_node_id BIGINT,"
    "  IN p_user_id VARCHAR(100),"
    "  IN p_timeout_minutes INT,"
    "  IN p_reason VARCHAR(255)"
    ")"
    "BEGIN "
    "  DECLARE v_current_lock_user VARCHAR(100);"
    "  DECLARE v_expires TIMESTAMP;"
    "  SELECT locked_by INTO v_current_lock_user FROM node_locks WHERE node_id = p_node_id;"
    "  IF v_current_lock_user IS NOT NULL AND v_current_lock_user != p_user_id THEN "
    "    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Node ist bereits von einem anderen Bearbeiter gelockt';"
    "  END IF;"
    "  IF p_timeout_minutes IS NOT NULL THEN "
    "    SET v_expires = DATE_ADD(NOW(), INTERVAL p_timeout_minutes MINUTE);"
    "  END IF;"
    "  INSERT INTO node_locks (node_id, locked_by, locked_at, expires_at, lock_reason) "
    "  VALUES (p_node_id, p_user_id, CURRENT_TIMESTAMP, v_expires, p_reason) "
    "  ON DUPLICATE KEY UPDATE locked_by = p_user_id, locked_at = CURRENT_TIMESTAMP, "
    "    expires_at = v_expires, lock_reason = p_reason;"
    "  SELECT 'success' as status, node_id, locked_by, locked_at, expires_at "
    "  FROM node_locks WHERE node_id = p_node_id;"
    "END",

    // unlock_node
    "CREATE PROCEDURE unlock_node("
    "  IN p_node_id BIGINT,"
    "  IN p_user_id VARCHAR(100)"
    ")"
    "BEGIN "
    "  DECLARE v_current_lock_user VARCHAR(100);"
    "  DECLARE v_locked_at TIMESTAMP;"
    "  DECLARE v_reason VARCHAR(255);"
    "  SELECT locked_by, locked_at, lock_reason INTO v_current_lock_user, v_locked_at, v_reason "
    "  FROM node_locks WHERE node_id = p_node_id;"
    "  IF v_current_lock_user IS NULL THEN "
    "    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Node ist nicht gelockt';"
    "  ELSEIF v_current_lock_user != p_user_id THEN "
    "    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Node kann nur vom Lock-Inhaber entsperrt werden';"
    "  END IF;"
    "  INSERT INTO lock_history (node_id, locked_by, locked_at, lock_reason) "
    "  VALUES (p_node_id, v_current_lock_user, v_locked_at, v_reason);"
    "  DELETE FROM node_locks WHERE node_id = p_node_id;"
    "  SELECT 'success' as status;"
    "END",

    // unlock_all_by_user
    "CREATE PROCEDURE unlock_all_by_user("
    "  IN p_user_id VARCHAR(100)"
    ")"
    "BEGIN "
    "  INSERT INTO lock_history (node_id, locked_by, locked_at, lock_reason) "
    "  SELECT node_id, locked_by, locked_at, lock_reason FROM node_locks WHERE locked_by = p_user_id;"
    "  DELETE FROM node_locks WHERE locked_by = p_user_id;"
    "  SELECT ROW_COUNT() as unlocked_count;"
    "END",

    // cleanup_expired_locks
    "CREATE PROCEDURE cleanup_expired_locks() "
    "BEGIN "
    "  INSERT INTO lock_history (node_id, locked_by, locked_at, lock_reason) "
    "  SELECT node_id, locked_by, locked_at, lock_reason FROM node_locks "
    "  WHERE expires_at IS NOT NULL AND expires_at < NOW();"
    "  DELETE FROM node_locks WHERE expires_at IS NOT NULL AND expires_at < NOW();"
    "  SELECT ROW_COUNT() as cleaned_locks;"
    "END",

    // check_lock_status
    "CREATE PROCEDURE check_lock_status("
    "  IN p_node_id BIGINT"
    ")"
    "BEGIN "
    "  SELECT node_id, locked_by, locked_at, expires_at, lock_reason, "
    "    CASE WHEN expires_at IS NOT NULL AND expires_at < NOW() THEN 'expired' ELSE 'active' END as lock_status "
    "  FROM node_locks WHERE node_id = p_node_id;"
    "END",

    NULL  // Terminator
};

/**
 * Führt ein einzelnes SQL-Statement aus
 */
static gboolean execute_sql(MYSQL *conn, const char *sql, GError **error) {
    if (mysql_query(conn, sql)) {
        g_set_error(error, GRAPH_DB_ERROR, GRAPH_DB_ERROR_SQL,
                   "SQL-Fehler: %s (Statement: %.100s...)",
                   mysql_error(conn), sql);
        g_prefix_error(error, "%s: ", __func__);
        return FALSE;
    }
    return TRUE;
}

/**
 * Erstellt Datenbankverbindung
 */
static MYSQL* connect_database(const DBConfig *config, GError **error) {
    MYSQL *conn = mysql_init(NULL);

    if (conn == NULL) {
        g_set_error(error, GRAPH_DB_ERROR, GRAPH_DB_ERROR_INIT,
                   "mysql_init() fehlgeschlagen");
        g_prefix_error(error, "%s: ", __func__);
        return NULL;
    }

    if (mysql_real_connect(conn, config->host, config->user, config->password,
                          config->database, config->port, NULL, 0) == NULL) {
        g_set_error(error, GRAPH_DB_ERROR, GRAPH_DB_ERROR_CONNECTION,
                   "Verbindung zu %s:%u fehlgeschlagen: %s",
                   config->host, config->port, mysql_error(conn));
        mysql_close(conn);
        g_prefix_error(error, "%s: ", __func__);
        return NULL;
    }

    return conn;
}

/**
 * Aktiviert Multi-Statement-Modus
 */
static gboolean enable_multi_statements(MYSQL *conn, GError **error) {
    if (mysql_set_server_option(conn, MYSQL_OPTION_MULTI_STATEMENTS_ON)) {
        g_set_error(error, GRAPH_DB_ERROR, GRAPH_DB_ERROR_SQL,
                   "Multi-Statements aktivieren fehlgeschlagen: %s",
                   mysql_error(conn));
        g_prefix_error(error, "%s: ", __func__);
        return FALSE;
    }
    return TRUE;
}

/**
 * Erstellt alle Tabellen
 */
static gboolean create_tables(MYSQL *conn, GError **error) {
    GError *tmp_error = NULL;

    g_print("\nErstelle Tabellen...\n");

    for (int i = 0; sql_statements[i] != NULL; i++) {
        g_print("  Statement %d...", i + 1);

        if (!execute_sql(conn, sql_statements[i], &tmp_error)) {
            g_prefix_error(&tmp_error, "%s: ", __func__);
            g_propagate_error(error, tmp_error);
            return FALSE;
        }

        g_print(" OK\n");
    }

    return TRUE;
}

/**
 * Erstellt alle Stored Procedures
 */
static gboolean create_procedures(MYSQL *conn, GError **error) {
    GError *tmp_error = NULL;

    g_print("\nErstelle Stored Procedures...\n");

    for (int i = 0; procedures[i] != NULL; i++) {
        g_print("  Procedure %d...", i + 1);

        if (!execute_sql(conn, procedures[i], &tmp_error)) {
            g_prefix_error(&tmp_error, "%s: ", __func__);
            g_propagate_error(error, tmp_error);
            return FALSE;
        }

        g_print(" OK\n");
    }

    return TRUE;
}

/**
 * Richtet die Graph-Datenbank ein
 */
gboolean setup_graph_database(const DBConfig *config, GError **error) {
    MYSQL *conn = NULL;
    GError *tmp_error = NULL;
    gboolean success = FALSE;

    // Validierung
    if (config == NULL) {
        g_set_error(error, GRAPH_DB_ERROR, GRAPH_DB_ERROR_CONFIG,
                   "Konfiguration ist NULL");
        g_prefix_error(error, "%s: ", __func__);
        return FALSE;
    }

    // Verbinden
    g_print("Verbinde zu %s:%u als %s...\n",
            config->host, config->port, config->user);

    conn = connect_database(config, &tmp_error);
    if (conn == NULL) {
        g_prefix_error(&tmp_error, "%s: ", __func__);
        g_propagate_error(error, tmp_error);
        return FALSE;
    }

    g_print("Verbindung erfolgreich!\n");

    // Multi-Statement-Modus aktivieren
    if (!enable_multi_statements(conn, &tmp_error)) {
        g_prefix_error(&tmp_error, "%s: ", __func__);
        g_propagate_error(error, tmp_error);
        goto cleanup;
    }

    // Tabellen erstellen
    if (!create_tables(conn, &tmp_error)) {
        g_prefix_error(&tmp_error, "%s: ", __func__);
        g_propagate_error(error, tmp_error);
        goto cleanup;
    }

    // Stored Procedures erstellen
    if (!create_procedures(conn, &tmp_error)) {
        g_prefix_error(&tmp_error, "%s: ", __func__);
        g_propagate_error(error, tmp_error);
        goto cleanup;
    }

    g_print("\n✓ Graph-Datenbank erfolgreich eingerichtet!\n");
    success = TRUE;

cleanup:
    if (conn) {
        mysql_close(conn);
    }

    return success;
}

/**
 * Testet die Datenbank durch Einfügen von Beispieldaten
 */
gboolean test_database(const DBConfig *config, GError **error) {
    MYSQL *conn = NULL;
    GError *tmp_error = NULL;
    gboolean success = FALSE;

    conn = connect_database(config, &tmp_error);
    if (conn == NULL) {
        g_prefix_error(&tmp_error, "%s: ", __func__);
        g_propagate_error(error, tmp_error);
        return FALSE;
    }

    g_print("\nFüge Testdaten ein...\n");

    const char *test_sql =
        "INSERT INTO nodes (label, properties) VALUES "
        "('Person', '[{\"key\":\"name\",\"value\":\"Alice\"},{\"key\":\"age\",\"value\":\"30\"}]'),"
        "('Person', '[{\"key\":\"name\",\"value\":\"Bob\"},{\"key\":\"age\",\"value\":\"25\"}]'),"
        "('Company', '[{\"key\":\"name\",\"value\":\"Acme Corp\"}]')";

    if (!execute_sql(conn, test_sql, &tmp_error)) {
        g_prefix_error(&tmp_error, "%s: ", __func__);
        g_propagate_error(error, tmp_error);
        goto cleanup;
    }

    g_print("✓ Testdaten eingefügt\n");

    // Test-Abfrage
    if (mysql_query(conn, "SELECT COUNT(*) as count FROM nodes") == 0) {
        MYSQL_RES *result = mysql_store_result(conn);
        if (result) {
            MYSQL_ROW row = mysql_fetch_row(result);
            g_print("✓ Anzahl Nodes: %s\n", row[0]);
            mysql_free_result(result);
        }
    }

    success = TRUE;

cleanup:
    if (conn) {
        mysql_close(conn);
    }

    return success;
}

/**
 * Main-Funktion mit Beispiel
 */
int main(int argc, char *argv[]) {
    GError *error = NULL;

    // Konfiguration
    DBConfig config = {
        .host = "localhost",
        .user = "root",
        .password = "password",  // ÄNDERN!
        .database = "graph_db",
        .port = 3306
    };

    // Optional: Parameter aus Kommandozeile
    if (argc >= 2) config.host = argv[1];
    if (argc >= 3) config.user = argv[2];
    if (argc >= 4) config.password = argv[3];
    if (argc >= 5) config.database = argv[4];

    g_print("=== Graph Database Setup ===\n");
    g_print("Host: %s\n", config.host);
    g_print("User: %s\n", config.user);
    g_print("Database: %s\n\n", config.database);

    // Setup durchführen
    if (!setup_graph_database(&config, &error)) {
        g_printerr("\n✗ Setup fehlgeschlagen!\n");
        g_printerr("Fehler: %s\n", error->message);
        g_error_free(error);
        return EXIT_FAILURE;
    }

    // Optional: Test durchführen
    if (!test_database(&config, &error)) {
        g_printerr("\n✗ Test fehlgeschlagen!\n");
        g_printerr("Fehler: %s\n", error->message);
        g_error_free(error);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/*
 * Kompilierung:
 * gcc -o setup_graph_db setup_graph_db.c $(mysql_config --cflags --libs) $(pkg-config --cflags --libs glib-2.0)
 *
 * Alternative:
 * gcc -o setup_graph_db setup_graph_db.c -I/usr/include/mysql -L/usr/lib/mysql -lmariadb \
 *     $(pkg-config --cflags --libs glib-2.0)
 *
 * Ausführung:
 * ./setup_graph_db [host] [user] [password] [database]
 */
