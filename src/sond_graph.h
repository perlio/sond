/*
 sond (sond_graph.h) - Akten, Beweisstücke, Unterlagen
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

/**
 * @file graph_db_setup.h
 * @brief Setup-Funktionen für Graph-Datenbank mit MariaDB
 *
 * Stellt Funktionen zum Einrichten einer Graph-Datenbank mit Nodes,
 * Edges und Lock-Mechanismus bereit.
 */

#ifndef GRAPH_DB_SETUP_H
#define GRAPH_DB_SETUP_H

#include <glib.h>

G_BEGIN_DECLS

/**
 * Error Domain für Graph-Datenbank-Operationen
 */
#define GRAPH_DB_ERROR (graph_db_error_quark())

/**
 * GraphDBError:
 * @GRAPH_DB_ERROR_INIT: MySQL-Initialisierung fehlgeschlagen
 * @GRAPH_DB_ERROR_CONNECTION: Datenbankverbindung fehlgeschlagen
 * @GRAPH_DB_ERROR_SQL: SQL-Statement-Ausführung fehlgeschlagen
 * @GRAPH_DB_ERROR_CONFIG: Ungültige Konfiguration
 *
 * Error-Codes für Graph-Datenbank-Operationen
 */
typedef enum {
    GRAPH_DB_ERROR_INIT,
    GRAPH_DB_ERROR_CONNECTION,
    GRAPH_DB_ERROR_SQL,
    GRAPH_DB_ERROR_CONFIG
} GraphDBError;

/**
 * DBConfig:
 * @host: Hostname oder IP-Adresse des Datenbankservers
 * @user: Datenbankbenutzername
 * @password: Datenbankpasswort
 * @database: Name der Datenbank
 * @port: Port-Nummer (Standard: 3306)
 *
 * Konfigurationsstruktur für Datenbankverbindung
 */
typedef struct {
    const char *host;
    const char *user;
    const char *password;
    const char *database;
    unsigned int port;
} DBConfig;

/**
 * graph_db_error_quark:
 *
 * Gibt die Error-Domain für Graph-Datenbank-Fehler zurück.
 *
 * Returns: Die Error-Domain als GQuark
 */
GQuark graph_db_error_quark(void);

/**
 * setup_graph_database:
 * @config: Zeiger auf DBConfig-Struktur mit Verbindungsparametern
 * @error: Rückgabe-Ort für einen GError, oder NULL
 *
 * Richtet die komplette Graph-Datenbank ein mit:
 * - nodes-Tabelle (id, label, properties als JSON)
 * - edges-Tabelle (source_id, target_id, label, properties)
 * - node_locks-Tabelle für Lock-Mechanismus
 * - lock_history-Tabelle für Audit-Trail
 * - Stored Procedures für Lock-Management
 *
 * Die Datenbank muss bereits existieren. Tabellen und Procedures werden
 * mit IF NOT EXISTS angelegt, sodass die Funktion idempotent ist.
 *
 * Returns: TRUE bei Erfolg, FALSE bei Fehler (error wird gesetzt)
 *
 * Beispiel:
 * |[<!-- language="C" -->
 * DBConfig config = {
 *     .host = "localhost",
 *     .user = "root",
 *     .password = "secret",
 *     .database = "graph_db",
 *     .port = 3306
 * };
 *
 * GError *error = NULL;
 * if (!setup_graph_database(&config, &error)) {
 *     g_printerr("Setup fehlgeschlagen: %s\n", error->message);
 *     g_error_free(error);
 *     return FALSE;
 * }
 * ]|
 */
gboolean setup_graph_database(const DBConfig *config, GError **error);

/**
 * test_database:
 * @config: Zeiger auf DBConfig-Struktur mit Verbindungsparametern
 * @error: Rückgabe-Ort für einen GError, oder NULL
 *
 * Testet die Datenbank durch Einfügen von Beispieldaten:
 * - 2 Person-Nodes (Alice, Bob)
 * - 1 Company-Node (Acme Corp)
 *
 * Gibt die Anzahl der eingefügten Nodes aus.
 *
 * Returns: TRUE bei Erfolg, FALSE bei Fehler (error wird gesetzt)
 *
 * Beispiel:
 * |[<!-- language="C" -->
 * GError *error = NULL;
 * if (!test_database(&config, &error)) {
 *     g_printerr("Test fehlgeschlagen: %s\n", error->message);
 *     g_error_free(error);
 * }
 * ]|
 */
gboolean test_database(const DBConfig *config, GError **error);

G_END_DECLS

#endif /* GRAPH_DB_SETUP_H */
