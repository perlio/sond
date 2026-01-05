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
 * @file sond_graph_db.h
 * @brief Graph-Datenbankfunktionen
 */

#ifndef SOND_GRAPH_DB_H
#define SOND_GRAPH_DB_H

#include <glib.h>
#include <mysql/mysql.h>

G_BEGIN_DECLS

/**
 * SOND_GRAPH_DB_ERROR:
 *
 * Error domain für Graph-Datenbank-Operationen
 */
#define SOND_GRAPH_DB_ERROR (sond_graph_db_error_quark())

/**
 * SondGraphDBError:
 * @SOND_GRAPH_DB_ERROR_CONNECTION: Verbindungsfehler
 * @SOND_GRAPH_DB_ERROR_QUERY: SQL-Query fehlgeschlagen
 * @SOND_GRAPH_DB_ERROR_SETUP: Setup fehlgeschlagen
 *
 * Error-Codes für Graph-Datenbank-Operationen
 */
typedef enum {
    SOND_GRAPH_DB_ERROR_CONNECTION,
    SOND_GRAPH_DB_ERROR_QUERY,
    SOND_GRAPH_DB_ERROR_SETUP
} SondGraphDBError;

/**
 * sond_graph_db_error_quark:
 *
 * Gibt die Error-Domain für Graph-Datenbank-Fehler zurück.
 *
 * Returns: Die Error-Domain als GQuark
 */
GQuark sond_graph_db_error_quark(void);

/**
 * sond_graph_db_setup:
 * @conn: Aktive MySQL-Verbindung
 * @error: Rückgabe-Ort für GError
 *
 * Richtet die Graph-Datenbank ein. Erstellt:
 * - nodes-Tabelle (id, label, properties, timestamps)
 * - edges-Tabelle (id, source_id, target_id, label, properties, timestamps)
 * - node_locks-Tabelle (node_id, locked_by, locked_at, expires_at, lock_reason)
 * - Stored Procedures für Lock-Management
 *
 * Die Funktion ist idempotent - kann mehrfach aufgerufen werden.
 * Tabellen werden mit IF NOT EXISTS erstellt.
 *
 * Returns: TRUE bei Erfolg, FALSE bei Fehler
 *
 * Beispiel:
 * |[<!-- language="C" -->
 * MYSQL *conn = mysql_init(NULL);
 * mysql_real_connect(conn, "localhost", "user", "pass", "db", 0, NULL, 0);
 *
 * GError *error = NULL;
 * if (!sond_graph_db_setup(conn, &error)) {
 *     g_printerr("Setup failed: %s\n", error->message);
 *     g_error_free(error);
 *     return FALSE;
 * }
 * ]|
 */
gboolean sond_graph_db_setup(MYSQL *conn, GError **error);

/**
 * sond_graph_db_load_node:
 * @conn: MySQL-Verbindung
 * @node_id: ID des zu ladenden Nodes
 * @error: Rückgabe-Ort für GError
 *
 * Lädt einen kompletten Node aus der Datenbank inklusive:
 * - Alle Properties (als JSON-Array)
 * - Ausgehende Edge-Referenzen (ID + Label der Ziel-Nodes)
 * - Timestamps
 *
 * Returns: (transfer full) (nullable): Geladener GraphNode oder NULL bei Fehler
 *
 * Beispiel:
 * |[<!-- language="C" -->
 * GError *error = NULL;
 * GraphNode *node = sond_graph_db_load_node(conn, 42, &error);
 * if (node == NULL) {
 *     g_printerr("Load failed: %s\n", error->message);
 *     g_error_free(error);
 *     return;
 * }
 *
 * // Node verwenden...
 * g_print("Loaded: %s\n", graph_node_get_label(node));
 *
 * g_object_unref(node);
 * ]|
 */
GraphNode* sond_graph_db_load_node(MYSQL *conn, gint64 node_id, GError **error);

/**
 * SondPropertyFilter:
 * @key: Property-Key
 * @value: Property-Value
 * @path: (nullable): Pfad für verschachtelte Properties (z.B. "address.city")
 *
 * Ein einzelnes Property-Filter-Kriterium.
 *
 * - Ohne path: Sucht nur auf Hauptebene
 * - Mit path: Sucht verschachtelt (z.B. "address.city" findet city unter address)
 */
typedef struct {
    gchar *key;
    gchar *value;
    gchar *path;  /* z.B. "address" oder NULL für Hauptebene */
} SondPropertyFilter;

/**
 * sond_property_filter_new:
 * @key: Property-Key
 * @value: Property-Value
 *
 * Erstellt einen neuen Property-Filter (nur Hauptebene).
 *
 * Returns: (transfer full): Neuer SondPropertyFilter
 */
SondPropertyFilter* sond_property_filter_new(const gchar *key, const gchar *value);

/**
 * sond_property_filter_new_with_path:
 * @key: Property-Key
 * @value: Property-Value
 * @path: Pfad zum Parent-Property (z.B. "address")
 *
 * Erstellt einen Property-Filter für verschachtelte Properties.
 *
 * Beispiel:
 * |[<!-- language="C" -->
 * // Sucht: properties[?].key="address" -> properties[?].key="city", value="München"
 * SondPropertyFilter *filter = sond_property_filter_new_with_path("city", "München", "address");
 * ]|
 *
 * Returns: (transfer full): Neuer SondPropertyFilter
 */
SondPropertyFilter* sond_property_filter_new_with_path(const gchar *key,
                                                        const gchar *value,
                                                        const gchar *path);

/**
 * sond_property_filter_free:
 * @filter: Zu befreiender Filter
 *
 * Gibt einen Property-Filter frei.
 */
void sond_property_filter_free(SondPropertyFilter *filter);

/**
 * SondEdgeFilter:
 * @edge_label: Edge-Label (z.B. "WORKS_FOR", "KNOWS")
 * @target_criteria: Suchkriterien für den Ziel-Node
 *
 * Filter für ausgehende Edges.
 * Ermöglicht Suche nach Nodes basierend auf ihren Verbindungen.
 */
typedef struct {
    gchar *edge_label;
    SondGraphNodeSearchCriteria *target_criteria;
} SondEdgeFilter;

/**
 * sond_edge_filter_new:
 * @edge_label: Edge-Label
 * @target_criteria: (transfer full): Kriterien für Ziel-Node
 *
 * Erstellt einen neuen Edge-Filter.
 *
 * Returns: (transfer full): Neuer SondEdgeFilter
 */
SondEdgeFilter* sond_edge_filter_new(const gchar *edge_label,
                                      SondGraphNodeSearchCriteria *target_criteria);

/**
 * sond_edge_filter_free:
 * @filter: Zu befreiender Edge-Filter
 *
 * Gibt einen Edge-Filter frei.
 */
void sond_edge_filter_free(SondEdgeFilter *filter);

/**
 * SondGraphNodeSearchCriteria:
 * @label: (nullable): Node-Label zum Filtern (NULL = alle Labels)
 * @property_filters: (element-type SondPropertyFilter): Array von Property-Filtern (alle müssen matchen = AND)
 * @edge_filters: (element-type SondEdgeFilter): Array von Edge-Filtern (alle müssen matchen = AND)
 * @limit: Maximale Anzahl Ergebnisse (0 = unbegrenzt)
 * @offset: Offset für Pagination (0 = von Anfang)
 *
 * Suchkriterien für Nodes.
 * Alle Property-Filter und Edge-Filter werden mit AND verknüpft.
 */
typedef struct {
    gchar *label;
    GPtrArray *property_filters;  /* Array of SondPropertyFilter* */
    GPtrArray *edge_filters;      /* Array of SondEdgeFilter* */
    guint limit;
    guint offset;
} SondGraphNodeSearchCriteria;

/**
 * sond_graph_node_search_criteria_new:
 *
 * Erstellt neue Suchkriterien (alle Felder NULL/0).
 *
 * Returns: (transfer full): Neue SondGraphNodeSearchCriteria
 */
SondGraphNodeSearchCriteria* sond_graph_node_search_criteria_new(void);

/**
 * sond_graph_node_search_criteria_free:
 * @criteria: Zu befreiende Suchkriterien
 *
 * Gibt Suchkriterien frei.
 */
void sond_graph_node_search_criteria_free(SondGraphNodeSearchCriteria *criteria);

/**
 * sond_graph_node_search_criteria_add_property_filter:
 * @criteria: Suchkriterien
 * @key: Property-Key
 * @value: Property-Value (unterstützt Wildcards: * für beliebig viele Zeichen, ? für ein Zeichen)
 *
 * Fügt einen Property-Filter zu den Suchkriterien hinzu (nur Hauptebene).
 * Alle Filter werden mit AND verknüpft.
 *
 * Wildcards:
 * - * = beliebig viele Zeichen (z.B. "Münch*" findet "München", "Münchhausen")
 * - ? = genau ein Zeichen (z.B. "Berl?n" findet "Berlin")
 *
 * Beispiel:
 * |[<!-- language="C" -->
 * // Findet alle Namen die mit "Al" beginnen
 * sond_graph_node_search_criteria_add_property_filter(criteria, "name", "Al*");
 *
 * // Findet alle Städte die auf "berg" enden
 * sond_graph_node_search_criteria_add_property_filter(criteria, "city", "*berg");
 * ]|
 */
void sond_graph_node_search_criteria_add_property_filter(SondGraphNodeSearchCriteria *criteria,
                                                          const gchar *key,
                                                          const gchar *value);

/**
 * sond_graph_node_search_criteria_add_nested_property_filter:
 * @criteria: Suchkriterien
 * @key: Property-Key
 * @value: Property-Value (unterstützt Wildcards: * und ?)
 * @path: Pfad zum Parent-Property
 *
 * Fügt einen verschachtelten Property-Filter hinzu.
 * Unterstützt Wildcards wie add_property_filter.
 *
 * Beispiel:
 * |[<!-- language="C" -->
 * // Person seit 2013 in München
 * SondGraphNodeSearchCriteria *criteria = sond_graph_node_search_criteria_new();
 * criteria->label = g_strdup("Person");
 * sond_graph_node_search_criteria_add_nested_property_filter(criteria, "city", "München", "address");
 * sond_graph_node_search_criteria_add_nested_property_filter(criteria, "since", "2013", "address");
 * ]|
 */
void sond_graph_node_search_criteria_add_nested_property_filter(SondGraphNodeSearchCriteria *criteria,
                                                                 const gchar *key,
                                                                 const gchar *value,
                                                                 const gchar *path);

/**
 * sond_graph_node_search_criteria_add_edge_filter:
 * @criteria: Suchkriterien
 * @edge_label: Edge-Label (z.B. "WORKS_FOR")
 * @target_criteria: (transfer full): Kriterien für Ziel-Node
 *
 * Fügt einen Edge-Filter hinzu.
 * Findet Nodes die eine Edge mit dem Label zum Ziel-Node haben,
 * wobei der Ziel-Node die target_criteria erfüllen muss.
 *
 * Beispiel:
 * |[<!-- language="C" -->
 * // Alle Personen in München die bei Siemens arbeiten
 * SondGraphNodeSearchCriteria *criteria = sond_graph_node_search_criteria_new();
 * criteria->label = g_strdup("Person");
 * sond_graph_node_search_criteria_add_nested_property_filter(criteria, "city", "München", "address");
 *
 * // Target: Company mit name="Siemens"
 * SondGraphNodeSearchCriteria *target = sond_graph_node_search_criteria_new();
 * target->label = g_strdup("Company");
 * sond_graph_node_search_criteria_add_property_filter(target, "name", "Siemens");
 *
 * sond_graph_node_search_criteria_add_edge_filter(criteria, "WORKS_FOR", target);
 * ]|
 */
void sond_graph_node_search_criteria_add_edge_filter(SondGraphNodeSearchCriteria *criteria,
                                                      const gchar *edge_label,
                                                      SondGraphNodeSearchCriteria *target_criteria);

/**
 * sond_graph_db_search_nodes:
 * @conn: MySQL-Verbindung
 * @criteria: Suchkriterien
 * @error: Rückgabe-Ort für GError
 *
 * Sucht Nodes nach Kriterien und lädt sie direkt. Beispiele:
 *
 * - Alle Nodes mit Label "Person":
 *   criteria->label = "Person"
 *
 * - Alle Nodes mit property name="Alice":
 *   sond_graph_node_search_criteria_add_property_filter(criteria, "name", "Alice")
 *
 * - Alle Persons mit name="Alice" UND age="30":
 *   criteria->label = "Person"
 *   sond_graph_node_search_criteria_add_property_filter(criteria, "name", "Alice")
 *   sond_graph_node_search_criteria_add_property_filter(criteria, "age", "30")
 *
 * - Erste 10 Person-Nodes:
 *   criteria->label = "Person"
 *   criteria->limit = 10
 *
 * Returns: (transfer full) (element-type GraphNode): Array von gefundenen Nodes
 *
 * Beispiel:
 * |[<!-- language="C" -->
 * SondGraphNodeSearchCriteria *criteria = sond_graph_node_search_criteria_new();
 * criteria->label = g_strdup("Person");
 * criteria->limit = 10;
 *
 * GError *error = NULL;
 * GPtrArray *nodes = sond_graph_db_search_nodes(conn, criteria, &error);
 *
 * if (nodes != NULL) {
 *     for (guint i = 0; i < nodes->len; i++) {
 *         GraphNode *node = g_ptr_array_index(nodes, i);
 *         g_print("Found: %s\n", graph_node_get_label(node));
 *     }
 *     g_ptr_array_unref(nodes);
 * }
 *
 * sond_graph_node_search_criteria_free(criteria);
 * ]|
 */
GPtrArray* sond_graph_db_search_nodes(MYSQL *conn,
                                       const SondGraphNodeSearchCriteria *criteria,
                                       GError **error);

/**
 * sond_graph_db_save_node:
 * @conn: MySQL-Verbindung
 * @node: Zu speichernder Node
 * @error: Rückgabe-Ort für GError
 *
 * Speichert einen Node in der Datenbank.
 *
 * - Wenn ID = 0: INSERT (neue ID wird im Node gesetzt)
 * - Wenn ID > 0: UPDATE (bestehender Node wird aktualisiert)
 *
 * Properties werden als JSON gespeichert.
 * Edges werden NICHT automatisch gespeichert - diese müssen separat
 * mit sond_graph_db_save_edge() gespeichert werden.
 *
 * Returns: TRUE bei Erfolg, FALSE bei Fehler
 *
 * Beispiel:
 * |[<!-- language="C" -->
 * // Neuen Node erstellen und speichern
 * GraphNode *node = graph_node_new_with_label("Person");
 * graph_node_set_property(node, "name", "Alice");
 * graph_node_set_property(node, "age", "30");
 *
 * GError *error = NULL;
 * if (sond_graph_db_save_node(conn, node, &error)) {
 *     g_print("Saved with ID: %ld\n", graph_node_get_id(node));
 * } else {
 *     g_printerr("Save failed: %s\n", error->message);
 *     g_error_free(error);
 * }
 *
 * // Node ändern und aktualisieren
 * graph_node_set_property(node, "age", "31");
 * if (sond_graph_db_save_node(conn, node, &error)) {
 *     g_print("Updated!\n");
 * }
 *
 * g_object_unref(node);
 * ]|
 */
gboolean sond_graph_db_save_node(MYSQL *conn, GraphNode *node, GError **error);

/**
 * sond_graph_db_delete_node:
 * @conn: MySQL-Verbindung
 * @node_id: ID des zu löschenden Nodes
 * @error: Rückgabe-Ort für GError
 *
 * Löscht einen Node aus der Datenbank.
 *
 * Alle ausgehenden und eingehenden Edges werden durch CASCADE
 * automatisch mitgelöscht.
 *
 * Returns: TRUE bei Erfolg, FALSE bei Fehler
 */
gboolean sond_graph_db_delete_node(MYSQL *conn, gint64 node_id, GError **error);

/* ========================================================================
 * Node Lock Functions
 * ======================================================================== */

/**
 * sond_graph_db_lock_node:
 * @conn: MySQL-Verbindung
 * @node_id: ID des zu lockenden Nodes
 * @user_id: Benutzer-ID (z.B. Username, Session-ID)
 * @timeout_minutes: Timeout in Minuten (0 = kein Timeout)
 * @reason: (nullable): Grund für den Lock (optional)
 * @error: Rückgabe-Ort für GError
 *
 * Lockt einen Node für einen Benutzer.
 *
 * - Wenn Node bereits von anderem User gelockt: Fehler
 * - Wenn Node von gleichem User gelockt: Lock wird erneuert
 * - Mit Timeout: Lock läuft automatisch ab
 *
 * Returns: TRUE bei Erfolg, FALSE bei Fehler
 *
 * Beispiel:
 * |[<!-- language="C" -->
 * // Node für 30 Minuten locken
 * if (sond_graph_db_lock_node(conn, 42, "alice", 30, "Editing address", &error)) {
 *     // Node laden und bearbeiten
 *     GraphNode *node = sond_graph_db_load_node(conn, 42, &error);
 *     graph_node_set_property(node, "city", "Berlin");
 *     sond_graph_db_save_node(conn, node, &error);
 *     g_object_unref(node);
 *
 *     // Lock freigeben
 *     sond_graph_db_unlock_node(conn, 42, "alice", &error);
 * } else {
 *     g_printerr("Lock failed: %s\n", error->message);
 * }
 * ]|
 */
gboolean sond_graph_db_lock_node(MYSQL *conn,
                                  gint64 node_id,
                                  const gchar *user_id,
                                  guint timeout_minutes,
                                  const gchar *reason,
                                  GError **error);

/**
 * sond_graph_db_unlock_node:
 * @conn: MySQL-Verbindung
 * @node_id: ID des zu entsperrenden Nodes
 * @user_id: Benutzer-ID (muss Lock-Inhaber sein)
 * @error: Rückgabe-Ort für GError
 *
 * Entsperrt einen gelockten Node.
 *
 * Nur der Lock-Inhaber kann entsperren.
 *
 * Returns: TRUE bei Erfolg, FALSE bei Fehler
 */
gboolean sond_graph_db_unlock_node(MYSQL *conn,
                                    gint64 node_id,
                                    const gchar *user_id,
                                    GError **error);

/**
 * sond_graph_db_force_unlock_node:
 * @conn: MySQL-Verbindung
 * @node_id: ID des zu entsperrenden Nodes
 * @error: Rückgabe-Ort für GError
 *
 * Entsperrt einen Node gewaltsam - unabhängig vom Lock-Inhaber.
 * Für Admin-Funktionen oder wenn ein User abstürzt.
 *
 * ⚠️  VORSICHT: Umgeht die normale Lock-Überprüfung!
 *
 * Returns: TRUE bei Erfolg, FALSE bei Fehler
 *
 * Beispiel:
 * |[<!-- language="C" -->
 * // Admin entfernt verwaisten Lock
 * if (sond_graph_db_force_unlock_node(conn, 42, &error)) {
 *     g_print("Lock forcibly removed\n");
 * }
 * ]|
 */
gboolean sond_graph_db_force_unlock_node(MYSQL *conn,
                                          gint64 node_id,
                                          GError **error);

/**
 * sond_graph_db_check_lock:
 * @conn: MySQL-Verbindung
 * @node_id: ID des Nodes
 * @locked_by: (out) (nullable): Wer hat den Lock (NULL wenn nicht gelockt)
 * @error: Rückgabe-Ort für GError
 *
 * Prüft ob ein Node gelockt ist.
 *
 * Returns: TRUE wenn gelockt, FALSE wenn frei
 *
 * Beispiel:
 * |[<!-- language="C" -->
 * gchar *locked_by = NULL;
 * if (sond_graph_db_check_lock(conn, 42, &locked_by, &error)) {
 *     g_print("Node is locked by: %s\n", locked_by);
 *     g_free(locked_by);
 * } else {
 *     g_print("Node is free\n");
 * }
 * ]|
 */
gboolean sond_graph_db_check_lock(MYSQL *conn,
                                   gint64 node_id,
                                   gchar **locked_by,
                                   GError **error);

/**
 * sond_graph_db_unlock_all_by_user:
 * @conn: MySQL-Verbindung
 * @user_id: Benutzer-ID
 * @error: Rückgabe-Ort für GError
 *
 * Entsperrt alle Nodes eines Benutzers.
 * Nützlich bei Session-Ende / Logout.
 *
 * Returns: Anzahl entsperrter Nodes, -1 bei Fehler
 */
gint sond_graph_db_unlock_all_by_user(MYSQL *conn,
                                       const gchar *user_id,
                                       GError **error);

/**
 * sond_graph_db_cleanup_expired_locks:
 * @conn: MySQL-Verbindung
 * @error: Rückgabe-Ort für GError
 *
 * Entfernt alle abgelaufenen Locks.
 * Sollte regelmäßig aufgerufen werden (z.B. per Cronjob).
 *
 * Returns: Anzahl entfernter Locks, -1 bei Fehler
 */
gint sond_graph_db_cleanup_expired_locks(MYSQL *conn, GError **error);

/**
 * Helper functions for SondGraphNodeSearchCriteria
 */
void sond_graph_node_search_criteria_add_property_filter(
    SondGraphNodeSearchCriteria *criteria,
    const gchar *key,
    const gchar *value);

void sond_graph_node_search_criteria_add_nested_property_filter(
    SondGraphNodeSearchCriteria *criteria,
    const gchar *key,
    const gchar *value,
    const gchar *path);

void sond_graph_node_search_criteria_add_edge_filter(
    SondGraphNodeSearchCriteria *criteria,
    const gchar *edge_label,
    SondGraphNodeSearchCriteria *target_criteria);

void sond_graph_node_search_criteria_add_simple_edge_filter(
    SondGraphNodeSearchCriteria *criteria,
    const gchar *edge_label);

G_END_DECLS

#endif /* SOND_GRAPH_DB_H */
