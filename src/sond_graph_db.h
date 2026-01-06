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
 * @brief Graph-Datenbank API für MySQL
 *
 * Bietet Funktionen zum Speichern, Laden und Suchen von Graph-Nodes
 * mit komplexen Property- und Edge-Filtern.
 */

#ifndef SOND_GRAPH_DB_H
#define SOND_GRAPH_DB_H

#include <glib.h>
#include <mysql/mysql.h>
#include "sond_graph_node.h"
#include "sond_graph_edge.h"

G_BEGIN_DECLS

/* ========================================================================
 * Error Handling
 * ======================================================================== */

#define SOND_GRAPH_DB_ERROR (sond_graph_db_error_quark())

/**
 * SondGraphDbError:
 * @SOND_GRAPH_DB_ERROR_CONNECTION: Datenbankverbindungsfehler
 * @SOND_GRAPH_DB_ERROR_QUERY: SQL-Query-Fehler
 * @SOND_GRAPH_DB_ERROR_NOT_FOUND: Gesuchte Daten nicht gefunden
 *
 * Fehler-Codes für Graph-DB-Operationen.
 */
typedef enum {
    SOND_GRAPH_DB_ERROR_CONNECTION,
    SOND_GRAPH_DB_ERROR_QUERY,
    SOND_GRAPH_DB_ERROR_NOT_FOUND
} SondGraphDbError;

/**
 * sond_graph_db_error_quark:
 *
 * Returns: GQuark für Graph-DB-Errors
 */
GQuark sond_graph_db_error_quark(void);

/* ========================================================================
 * Setup Functions
 * ======================================================================== */

/**
 * sond_graph_db_setup:
 * @conn: MySQL-Verbindung
 * @error: (nullable): Fehler-Rückgabe
 *
 * Erstellt alle benötigten Tabellen und Stored Procedures.
 * Diese Funktion ist idempotent - kann mehrfach aufgerufen werden.
 *
 * Erstellt:
 * - nodes Tabelle
 * - edges Tabelle
 * - node_locks Tabelle
 * - Stored Procedures für Locking
 *
 * Returns: %TRUE bei Erfolg
 */
gboolean sond_graph_db_setup(MYSQL *conn, GError **error);

/* ========================================================================
 * Search Filter Types
 * ======================================================================== */

/**
 * SondPropertyFilter:
 *
 * Filter für Node- oder Edge-Properties.
 * Unterstützt einfache und verschachtelte Properties sowie Wildcards (* und ?).
 */
typedef struct _SondPropertyFilter SondPropertyFilter;

struct _SondPropertyFilter {
    gchar *key;       /* Property-Name */
    gchar *value;     /* Property-Wert (kann Wildcards enthalten) */
    gchar *path;      /* Pfad für verschachtelte Properties (optional) */
};

/**
 * sond_property_filter_new:
 * @key: Property-Name
 * @value: Property-Wert (kann * und ? als Wildcards enthalten)
 *
 * Erstellt einen Filter für eine Property.
 *
 * Wildcards:
 * - * : Beliebige Anzahl Zeichen
 * - ? : Genau ein Zeichen
 *
 * Returns: (transfer full): Neuer Filter
 */
SondPropertyFilter* sond_property_filter_new(const gchar *key, const gchar *value);

/**
 * sond_property_filter_new_with_path:
 * @key: Property-Name
 * @value: Property-Wert
 * @path: JSON-Pfad zur verschachtelten Property
 *
 * Erstellt einen Filter für verschachtelte Properties.
 *
 * Returns: (transfer full): Neuer Filter
 */
SondPropertyFilter* sond_property_filter_new_with_path(const gchar *key,
                                                        const gchar *value,
                                                        const gchar *path);

/**
 * sond_property_filter_free:
 * @filter: (nullable): Freizugebender Filter
 *
 * Gibt einen Property-Filter frei.
 */
void sond_property_filter_free(SondPropertyFilter *filter);

/**
 * SondEdgeFilter:
 *
 * Filter für Edges zwischen Nodes.
 * Kann Properties der Edge selbst sowie Kriterien für den Target-Node enthalten.
 */
typedef struct _SondEdgeFilter SondEdgeFilter;

struct _SondEdgeFilter {
    gchar *edge_label;                          /* Label der Edge */
    GPtrArray *property_filters;                /* Property-Filter der Edge */
    struct _SondGraphNodeSearchCriteria *target_criteria;  /* Kriterien für Target-Node */
};

/**
 * sond_edge_filter_new:
 * @edge_label: Label der Edge
 * @target_criteria: (nullable) (transfer full): Kriterien für Target-Node
 *
 * Erstellt einen Edge-Filter.
 *
 * Returns: (transfer full): Neuer Filter
 */
SondEdgeFilter* sond_edge_filter_new(const gchar *edge_label,
                                      struct _SondGraphNodeSearchCriteria *target_criteria);

/**
 * sond_edge_filter_free:
 * @filter: (nullable): Freizugebender Filter
 *
 * Gibt einen Edge-Filter frei.
 */
void sond_edge_filter_free(SondEdgeFilter *filter);

/**
 * sond_edge_filter_add_property:
 * @edge_filter: Edge-Filter
 * @key: Property-Name
 * @value: Property-Wert
 *
 * Fügt einen Property-Filter zur Edge hinzu.
 */
void sond_edge_filter_add_property(SondEdgeFilter *edge_filter,
                                    const gchar *key,
                                    const gchar *value);

/**
 * sond_edge_filter_add_nested_property:
 * @edge_filter: Edge-Filter
 * @key: Property-Name
 * @value: Property-Wert
 * @path: JSON-Pfad
 *
 * Fügt einen verschachtelten Property-Filter zur Edge hinzu.
 */
void sond_edge_filter_add_nested_property(SondEdgeFilter *edge_filter,
                                           const gchar *key,
                                           const gchar *value,
                                           const gchar *path);

/**
 * SondGraphNodeSearchCriteria:
 *
 * Such-Kriterien für Nodes.
 * Kombiniert Label-, Property- und Edge-Filter.
 */
typedef struct _SondGraphNodeSearchCriteria SondGraphNodeSearchCriteria;

struct _SondGraphNodeSearchCriteria {
    gchar *label;                    /* Node-Label (optional) */
    GPtrArray *property_filters;     /* Property-Filter */
    GPtrArray *edge_filters;         /* Edge-Filter (unterstützt Rekursion!) */
    guint limit;                     /* Maximale Anzahl Ergebnisse (0 = unbegrenzt) */
    guint offset;                    /* Offset für Pagination */
};

/**
 * sond_graph_node_search_criteria_new:
 *
 * Erstellt neue Such-Kriterien.
 *
 * Returns: (transfer full): Neue Kriterien
 */
SondGraphNodeSearchCriteria* sond_graph_node_search_criteria_new(void);

/**
 * sond_graph_node_search_criteria_free:
 * @criteria: (nullable): Freizugebende Kriterien
 *
 * Gibt Such-Kriterien frei.
 */
void sond_graph_node_search_criteria_free(SondGraphNodeSearchCriteria *criteria);

/**
 * sond_graph_node_search_criteria_add_property_filter:
 * @criteria: Such-Kriterien
 * @key: Property-Name
 * @value: Property-Wert
 *
 * Fügt einen Property-Filter hinzu.
 */
void sond_graph_node_search_criteria_add_property_filter(SondGraphNodeSearchCriteria *criteria,
                                                          const gchar *key,
                                                          const gchar *value);

/**
 * sond_graph_node_search_criteria_add_nested_property_filter:
 * @criteria: Such-Kriterien
 * @key: Property-Name
 * @value: Property-Wert
 * @path: JSON-Pfad
 *
 * Fügt einen verschachtelten Property-Filter hinzu.
 */
void sond_graph_node_search_criteria_add_nested_property_filter(SondGraphNodeSearchCriteria *criteria,
                                                                 const gchar *key,
                                                                 const gchar *value,
                                                                 const gchar *path);

/**
 * sond_graph_node_search_criteria_add_edge_filter:
 * @criteria: Such-Kriterien
 * @edge_label: Edge-Label
 * @target_criteria: (transfer full) (nullable): Kriterien für Target-Node
 *
 * Fügt einen Edge-Filter hinzu.
 * Ownership von @target_criteria wird übernommen.
 */
void sond_graph_node_search_criteria_add_edge_filter(SondGraphNodeSearchCriteria *criteria,
                                                      const gchar *edge_label,
                                                      SondGraphNodeSearchCriteria *target_criteria);

/**
 * sond_graph_node_search_criteria_add_simple_edge_filter:
 * @criteria: Such-Kriterien
 * @edge_label: Edge-Label
 *
 * Fügt einen einfachen Edge-Filter ohne Target-Kriterien hinzu.
 */
void sond_graph_node_search_criteria_add_simple_edge_filter(SondGraphNodeSearchCriteria *criteria,
                                                             const gchar *edge_label);

/* ========================================================================
 * Node Operations
 * ======================================================================== */

/**
 * sond_graph_db_load_node:
 * @conn: MySQL-Verbindung
 * @node_id: ID des zu ladenden Nodes
 * @error: (nullable): Fehler-Rückgabe
 *
 * Lädt einen kompletten Node inklusive Properties und ausgehenden Edges.
 *
 * Returns: (transfer full) (nullable): Geladener Node oder %NULL bei Fehler
 */
SondGraphNode* sond_graph_db_load_node(MYSQL *conn, gint64 node_id, GError **error);

/**
 * sond_graph_db_save_node:
 * @conn: MySQL-Verbindung
 * @node: Zu speichernder Node
 * @error: (nullable): Fehler-Rückgabe
 *
 * Speichert einen Node in der Datenbank.
 * - Wenn node->id == 0: INSERT (neue ID wird gesetzt)
 * - Wenn node->id > 0: UPDATE
 *
 * WICHTIG: Edges werden NICHT automatisch gespeichert!
 * Verwende sond_graph_db_save_node_with_edges() oder speichere Edges separat.
 *
 * Returns: %TRUE bei Erfolg
 */
gboolean sond_graph_db_save_node(MYSQL *conn, SondGraphNode *node, GError **error);

/**
 * sond_graph_db_save_node_with_edges:
 * @conn: MySQL-Verbindung
 * @node: Zu speichernder Node
 * @error: (nullable): Fehler-Rückgabe
 *
 * Speichert einen Node UND alle ausgehenden Edges in einer Transaktion.
 *
 * Ablauf:
 * 1. START TRANSACTION
 * 2. Node speichern
 * 3. Alle ausgehenden Edges speichern
 * 4. COMMIT (oder ROLLBACK bei Fehler)
 *
 * Alle Edges im Node müssen gültige source_id haben!
 * Bei neuen Nodes: zuerst wird Node gespeichert (erhält ID),
 * dann werden die Edge source_ids automatisch gesetzt.
 *
 * Returns: %TRUE bei Erfolg (alles committed), %FALSE bei Fehler (alles rollback)
 */
gboolean sond_graph_db_save_node_with_edges(MYSQL *conn, SondGraphNode *node, GError **error);

/**
 * sond_graph_db_delete_node:
 * @conn: MySQL-Verbindung
 * @node_id: ID des zu löschenden Nodes
 * @error: (nullable): Fehler-Rückgabe
 *
 * Löscht einen Node aus der Datenbank.
 * Edges werden automatisch durch CASCADE gelöscht.
 *
 * Returns: %TRUE bei Erfolg
 */
gboolean sond_graph_db_delete_node(MYSQL *conn, gint64 node_id, GError **error);

/**
 * sond_graph_db_search_nodes:
 * @conn: MySQL-Verbindung
 * @criteria: Such-Kriterien
 * @error: (nullable): Fehler-Rückgabe
 *
 * Sucht Nodes anhand komplexer Kriterien.
 * Unterstützt:
 * - Label-Filter
 * - Property-Filter (mit Wildcards)
 * - Verschachtelte Properties
 * - Edge-Filter (rekursiv!)
 * - Limit/Offset
 *
 * Beispiel:
 * ```c
 * // Finde alle "Person"-Nodes mit name="John*",
 * // die eine "KNOWS"-Edge zu einem "Person" mit city="Berlin" haben
 * SondGraphNodeSearchCriteria *criteria = sond_graph_node_search_criteria_new();
 * criteria->label = g_strdup("Person");
 * sond_graph_node_search_criteria_add_property_filter(criteria, "name", "John*");
 *
 * SondGraphNodeSearchCriteria *target = sond_graph_node_search_criteria_new();
 * target->label = g_strdup("Person");
 * sond_graph_node_search_criteria_add_property_filter(target, "city", "Berlin");
 *
 * sond_graph_node_search_criteria_add_edge_filter(criteria, "KNOWS", target);
 *
 * GPtrArray *results = sond_graph_db_search_nodes(conn, criteria, &error);
 * ```
 *
 * Returns: (transfer full) (element-type SondGraphNode) (nullable): Array mit Nodes oder %NULL bei Fehler
 */
GPtrArray* sond_graph_db_search_nodes(MYSQL *conn,
                                       const SondGraphNodeSearchCriteria *criteria,
                                       GError **error);

/* ========================================================================
 * Edge Operations
 * ======================================================================== */

/**
 * sond_graph_db_save_edge:
 * @conn: MySQL-Verbindung
 * @edge: Zu speichernde Edge
 * @error: (nullable): Fehler-Rückgabe
 *
 * Speichert eine Edge in der Datenbank.
 * - Wenn edge->id == 0: INSERT (neue ID wird gesetzt)
 * - Wenn edge->id > 0: UPDATE
 *
 * WICHTIG: Source- und Target-Nodes müssen bereits existieren!
 *
 * Returns: %TRUE bei Erfolg
 */
gboolean sond_graph_db_save_edge(MYSQL *conn, SondGraphEdge *edge, GError **error);

/**
 * sond_graph_db_delete_edge:
 * @conn: MySQL-Verbindung
 * @edge_id: ID der zu löschenden Edge
 * @error: (nullable): Fehler-Rückgabe
 *
 * Löscht eine Edge aus der Datenbank.
 *
 * Returns: %TRUE bei Erfolg
 */
gboolean sond_graph_db_delete_edge(MYSQL *conn, gint64 edge_id, GError **error);

/* ========================================================================
 * Node Locking
 * ======================================================================== */

/**
 * sond_graph_db_lock_node:
 * @conn: MySQL-Verbindung
 * @node_id: ID des zu lockenden Nodes
 * @user_id: Benutzer-ID (z.B. Username oder Session-ID)
 * @timeout_minutes: Timeout in Minuten (0 = kein Timeout)
 * @reason: (nullable): Grund für das Lock
 * @error: (nullable): Fehler-Rückgabe
 *
 * Lockt einen Node für exklusiven Zugriff.
 * - Wenn bereits vom selben User gelockt: Lock wird erneuert
 * - Wenn von anderem User gelockt: Fehler
 *
 * Returns: %TRUE bei Erfolg
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
 * @user_id: Benutzer-ID
 * @error: (nullable): Fehler-Rückgabe
 *
 * Entsperrt einen Node.
 * Nur der Lock-Inhaber kann entsperren.
 *
 * Returns: %TRUE bei Erfolg
 */
gboolean sond_graph_db_unlock_node(MYSQL *conn,
                                    gint64 node_id,
                                    const gchar *user_id,
                                    GError **error);

/**
 * sond_graph_db_force_unlock_node:
 * @conn: MySQL-Verbindung
 * @node_id: ID des zu entsperrenden Nodes
 * @error: (nullable): Fehler-Rückgabe
 *
 * Entsperrt einen Node OHNE User-Check (Admin-Funktion).
 *
 * Returns: %TRUE bei Erfolg
 */
gboolean sond_graph_db_force_unlock_node(MYSQL *conn,
                                          gint64 node_id,
                                          GError **error);

/**
 * sond_graph_db_check_lock:
 * @conn: MySQL-Verbindung
 * @node_id: ID des zu prüfenden Nodes
 * @locked_by: (out) (nullable) (transfer full): Benutzer der den Lock hält
 * @error: (nullable): Fehler-Rückgabe
 *
 * Prüft ob ein Node gelockt ist.
 *
 * Returns: %TRUE wenn gelockt, %FALSE wenn frei
 */
gboolean sond_graph_db_check_lock(MYSQL *conn,
                                   gint64 node_id,
                                   gchar **locked_by,
                                   GError **error);

/**
 * sond_graph_db_unlock_all_by_user:
 * @conn: MySQL-Verbindung
 * @user_id: Benutzer-ID
 * @error: (nullable): Fehler-Rückgabe
 *
 * Entsperrt alle Nodes eines Benutzers.
 * Nützlich beim Logout oder Session-Cleanup.
 *
 * Returns: Anzahl entsperrter Nodes, -1 bei Fehler
 */
gint sond_graph_db_unlock_all_by_user(MYSQL *conn,
                                       const gchar *user_id,
                                       GError **error);

/**
 * sond_graph_db_cleanup_expired_locks:
 * @conn: MySQL-Verbindung
 * @error: (nullable): Fehler-Rückgabe
 *
 * Entfernt alle abgelaufenen Locks.
 * Sollte regelmäßig aufgerufen werden (z.B. via Cron).
 *
 * Returns: Anzahl entfernter Locks, -1 bei Fehler
 */
gint sond_graph_db_cleanup_expired_locks(MYSQL *conn, GError **error);

G_END_DECLS

#endif /* SOND_GRAPH_DB_H */
