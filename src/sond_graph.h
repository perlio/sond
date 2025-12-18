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

#ifndef SOND_GRAPH_H
#define SOND_GRAPH_H

#include <glib.h>
#include <mysql/mysql.h>

G_BEGIN_DECLS

/**
 * SECTION:sond_graph
 * @title: SOND Graph Database
 * @short_description: Graph-Datenbank mit JSON-Properties
 *
 * Die SOND Graph Database speichert Nodes und Edges mit flexiblen Properties im JSON-Format.
 *
 * Property-Format:
 * [
 *   {
 *     "key": "name",
 *     "value": "Max",
 *     "properties": [...]  // optional
 *   }
 * ]
 */

/* Fehlercodes */
#define SOND_GRAPH_ERROR_DOMAIN g_quark_from_static_string("sond-graph-error")

/**
 * sond_graph_create:
 * @mysql: Aktive MySQL-Verbindung mit CREATE DATABASE Rechten
 * @db_name: Name der zu erstellenden Datenbank
 * @prompt_if_exists: TRUE = Benutzer bei existierender DB fragen, FALSE = automatisch überschreiben
 * @error: (nullable): Rückgabe für Fehlerinformationen
 *
 * Erstellt eine neue Graph-Datenbank mit nodes und edges Tabellen.
 *
 * Returns: 0 bei Erfolg, -1 bei Fehler
 */
gint sond_graph_create(MYSQL *mysql,
                       const gchar *db_name,
                       gboolean prompt_if_exists,
                       GError **error);

/**
 * sond_graph_insert_node:
 * @mysql: Aktive MySQL-Verbindung (Datenbank muss ausgewählt sein)
 * @label: Label des Nodes (z.B. "Person", "Company")
 * @properties_json: (nullable): Properties als JSON-String im Property-Format
 * @error: (nullable): Rückgabe für Fehlerinformationen
 *
 * Fügt einen neuen Node in die Datenbank ein.
 *
 * Beispiel für properties_json:
 * |[<!-- language="C" -->
 * "[{\"key\":\"name\",\"value\":\"Max\"},{\"key\":\"age\",\"value\":42}]"
 * ]|
 *
 * Returns: ID des eingefügten Nodes (>= 0) bei Erfolg, -1 bei Fehler
 */
gint sond_graph_insert_node(MYSQL *mysql,
                             const gchar *label,
                             const gchar *properties_json,
                             GError **error);

/**
 * sond_graph_insert_edge:
 * @mysql: Aktive MySQL-Verbindung (Datenbank muss ausgewählt sein)
 * @label: Label der Edge (z.B. "KNOWS", "WORKS_AT")
 * @from_node_id: ID des Start-Nodes
 * @to_node_id: ID des Ziel-Nodes
 * @properties_json: (nullable): Properties als JSON-String im Property-Format
 * @error: (nullable): Rückgabe für Fehlerinformationen
 *
 * Fügt eine neue Edge zwischen zwei Nodes ein.
 *
 * Beispiel für properties_json:
 * |[<!-- language="C" -->
 * "[{\"key\":\"since\",\"value\":\"2020-01-15\"}]"
 * ]|
 *
 * Returns: ID der eingefügten Edge (>= 0) bei Erfolg, -1 bei Fehler
 */
gint sond_graph_insert_edge(MYSQL *mysql,
                             const gchar *label,
                             gint from_node_id,
                             gint to_node_id,
                             const gchar *properties_json,
                             GError **error);

/**
 * sond_graph_update_node_properties:
 * @mysql: Aktive MySQL-Verbindung
 * @node_id: ID des Nodes
 * @properties_json: Neue Properties als JSON-String (überschreibt alle bisherigen)
 * @error: (nullable): Rückgabe für Fehlerinformationen
 *
 * Aktualisiert die Properties eines Nodes vollständig.
 *
 * Returns: 0 bei Erfolg, -1 bei Fehler
 */
gint sond_graph_update_node_properties(MYSQL *mysql,
                                        gint node_id,
                                        const gchar *properties_json,
                                        GError **error);

/**
 * sond_graph_update_edge_properties:
 * @mysql: Aktive MySQL-Verbindung
 * @edge_id: ID der Edge
 * @properties_json: Neue Properties als JSON-String (überschreibt alle bisherigen)
 * @error: (nullable): Rückgabe für Fehlerinformationen
 *
 * Aktualisiert die Properties einer Edge vollständig.
 *
 * Returns: 0 bei Erfolg, -1 bei Fehler
 */
gint sond_graph_update_edge_properties(MYSQL *mysql,
                                        gint edge_id,
                                        const gchar *properties_json,
                                        GError **error);

/**
 * sond_graph_delete_node:
 * @mysql: Aktive MySQL-Verbindung
 * @node_id: ID des zu löschenden Nodes
 * @error: (nullable): Rückgabe für Fehlerinformationen
 *
 * Löscht einen Node und alle zugehörigen Edges (CASCADE).
 *
 * Returns: 0 bei Erfolg, -1 bei Fehler
 */
gint sond_graph_delete_node(MYSQL *mysql,
                             gint node_id,
                             GError **error);

/**
 * sond_graph_delete_edge:
 * @mysql: Aktive MySQL-Verbindung
 * @edge_id: ID der zu löschenden Edge
 * @error: (nullable): Rückgabe für Fehlerinformationen
 *
 * Löscht eine Edge.
 *
 * Returns: 0 bei Erfolg, -1 bei Fehler
 */
gint sond_graph_delete_edge(MYSQL *mysql,
                             gint edge_id,
                             GError **error);

G_END_DECLS

#endif /* SOND_GRAPH_H */
