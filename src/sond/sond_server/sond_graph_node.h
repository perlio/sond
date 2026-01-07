/*
 sond (sond_graph_node.h) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_graph_node.h
 * @brief Graph Node Implementation - Nutzt SondGraphProperty direkt
 *
 * Die Node nutzt SondGraphProperty für alle Property-Operationen.
 * Dies stellt Konsistenz mit Edge und dem Property-System sicher.
 *
 * Properties Format:
 * - Property = [key, [values...], [sub-properties...]]
 * - Values sind IMMER ein Array von Strings (auch bei nur 1 Element)
 * - Sub-Properties sind optionale Metadaten über den Wert
 *
 * Beispiele:
 *   ["name", ["Alice"]]
 *   ["age", ["30"]]
 *   ["address", ["Berlin", "Hauptstr 1", "10115"], [
 *     ["type", ["Hauptwohnsitz"]],
 *     ["valid_from", ["2020-01-01"]]
 *   ]]
 */

#ifndef SOND_GRAPH_NODE_H
#define SOND_GRAPH_NODE_H

#include <glib-object.h>
#include <glib.h>

G_BEGIN_DECLS

/* ========================================================================
 * Type Definitions
 * ======================================================================== */

#define SOND_TYPE_GRAPH_NODE (sond_graph_node_get_type())
G_DECLARE_FINAL_TYPE(SondGraphNode, sond_graph_node, SOND, GRAPH_NODE, GObject)

/* Forward declaration */
typedef struct _SondGraphEdge SondGraphEdge;

/* ========================================================================
 * Constructor / Destructor
 * ======================================================================== */

/**
 * sond_graph_node_new:
 *
 * Erstellt einen neuen Graph-Knoten.
 *
 * Returns: (transfer full): Neuer SondGraphNode
 */
SondGraphNode* sond_graph_node_new(void);

/* ========================================================================
 * Basic Properties (ID, Label, Timestamps)
 * ======================================================================== */

/**
 * sond_graph_node_get_id:
 * @node: Ein SondGraphNode
 *
 * Gibt die ID des Knotens zurück.
 *
 * Returns: Node ID
 */
gint64 sond_graph_node_get_id(SondGraphNode *node);

/**
 * sond_graph_node_set_id:
 * @node: Ein SondGraphNode
 * @id: Neue ID
 *
 * Setzt die ID des Knotens.
 */
void sond_graph_node_set_id(SondGraphNode *node, gint64 id);

/**
 * sond_graph_node_get_label:
 * @node: Ein SondGraphNode
 *
 * Gibt das Label des Knotens zurück (z.B. "Person", "Company").
 *
 * Returns: (nullable): Label oder NULL
 */
const gchar* sond_graph_node_get_label(SondGraphNode *node);

/**
 * sond_graph_node_set_label:
 * @node: Ein SondGraphNode
 * @label: Neues Label
 *
 * Setzt das Label des Knotens.
 */
void sond_graph_node_set_label(SondGraphNode *node, const gchar *label);

/**
 * sond_graph_node_get_created_at:
 * @node: Ein SondGraphNode
 *
 * Gibt den Erstellungszeitpunkt zurück.
 *
 * Returns: (nullable) (transfer none): GDateTime oder NULL
 */
GDateTime* sond_graph_node_get_created_at(SondGraphNode *node);

/**
 * sond_graph_node_set_created_at:
 * @node: Ein SondGraphNode
 * @created_at: (nullable): Erstellungszeitpunkt
 *
 * Setzt den Erstellungszeitpunkt.
 */
void sond_graph_node_set_created_at(SondGraphNode *node, GDateTime *created_at);

/**
 * sond_graph_node_get_updated_at:
 * @node: Ein SondGraphNode
 *
 * Gibt den letzten Änderungszeitpunkt zurück.
 *
 * Returns: (nullable) (transfer none): GDateTime oder NULL
 */
GDateTime* sond_graph_node_get_updated_at(SondGraphNode *node);

/**
 * sond_graph_node_set_updated_at:
 * @node: Ein SondGraphNode
 * @updated_at: (nullable): Änderungszeitpunkt
 *
 * Setzt den Änderungszeitpunkt.
 */
void sond_graph_node_set_updated_at(SondGraphNode *node, GDateTime *updated_at);

/* ========================================================================
 * Properties Management
 * ======================================================================== */

/**
 * sond_graph_node_set_property:
 * @node: Ein SondGraphNode
 * @key: Property-Name
 * @values: Array von Strings
 * @n_values: Anzahl der Werte
 *
 * Setzt eine Property mit einem oder mehreren Werten.
 * Dies ist die Haupt-Funktion zum Setzen von Properties.
 *
 * Beispiel:
 * |[<!-- language="C" -->
 * const gchar *address[] = {"Berlin", "Hauptstr 1", "10115"};
 * sond_graph_node_set_property(node, "address", address, 3);
 * ]|
 */
void sond_graph_node_set_property(SondGraphNode *node,
                                   const gchar *key,
                                   const gchar **values,
                                   guint n_values);

/**
 * sond_graph_node_set_property_string:
 * @node: Ein SondGraphNode
 * @key: Property-Name
 * @value: String-Wert
 *
 * Convenience-Funktion zum Setzen einer Property mit einem einzelnen Wert.
 *
 * Beispiel:
 * |[<!-- language="C" -->
 * sond_graph_node_set_property_string(node, "name", "Alice");
 * ]|
 */
void sond_graph_node_set_property_string(SondGraphNode *node,
                                          const gchar *key,
                                          const gchar *value);

/**
 * sond_graph_node_set_properties:
 * @node: Ein SondGraphNode
 * @props: (element-type SondGraphProperty): GPtrArray von SondGraphProperty
 *
 * Setzt mehrere Properties auf einmal aus einem Property-Array.
 * Nützlich beim Laden von Properties aus der Datenbank.
 *
 * Beispiel:
 * |[<!-- language="C" -->
 * GPtrArray *props = fetch_properties_from_db(node_id);
 * sond_graph_node_set_properties(node, props);
 * ]|
 */
void sond_graph_node_set_properties(SondGraphNode *node, GPtrArray *props);

/**
 * sond_graph_node_get_property:
 * @node: Ein SondGraphNode
 * @key: Property-Name
 *
 * Liest eine Property als Array von Strings.
 *
 * Returns: (transfer full) (element-type utf8) (nullable): GPtrArray von Strings oder NULL.
 *          Caller muss g_ptr_array_unref() aufrufen.
 *
 * Beispiel:
 * |[<!-- language="C" -->
 * GPtrArray *address = sond_graph_node_get_property(node, "address");
 * if (address) {
 *     for (guint i = 0; i < address->len; i++) {
 *         g_print("%s\n", (gchar*)g_ptr_array_index(address, i));
 *     }
 *     g_ptr_array_unref(address);
 * }
 * ]|
 */
GPtrArray* sond_graph_node_get_property(SondGraphNode *node,
                                         const gchar *key);

/**
 * sond_graph_node_get_property_string:
 * @node: Ein SondGraphNode
 * @key: Property-Name
 *
 * Convenience-Funktion zum Lesen des ersten Werts einer Property.
 *
 * Returns: (transfer full) (nullable): String oder NULL. Caller muss g_free() aufrufen.
 *
 * Beispiel:
 * |[<!-- language="C" -->
 * gchar *name = sond_graph_node_get_property_string(node, "name");
 * g_print("Name: %s\n", name);
 * g_free(name);
 * ]|
 */
gchar* sond_graph_node_get_property_string(SondGraphNode *node,
                                            const gchar *key);

/**
 * sond_graph_node_get_property_count:
 * @node: Ein SondGraphNode
 * @key: Property-Name
 *
 * Gibt die Anzahl der Werte einer Property zurück.
 *
 * Returns: Anzahl der Werte oder 0
 */
guint sond_graph_node_get_property_count(SondGraphNode *node,
                                          const gchar *key);

/**
 * sond_graph_node_has_property:
 * @node: Ein SondGraphNode
 * @key: Property-Name
 *
 * Prüft ob eine Property existiert.
 *
 * Returns: TRUE wenn die Property existiert
 */
gboolean sond_graph_node_has_property(SondGraphNode *node,
                                       const gchar *key);

/**
 * sond_graph_node_remove_property:
 * @node: Ein SondGraphNode
 * @key: Property-Name
 *
 * Entfernt eine Property.
 */
void sond_graph_node_remove_property(SondGraphNode *node,
                                      const gchar *key);

/**
 * sond_graph_node_get_property_keys:
 * @node: Ein SondGraphNode
 *
 * Gibt eine Liste aller Property-Namen zurück.
 *
 * Returns: (transfer full) (element-type utf8): GPtrArray von Strings.
 *          Caller muss g_ptr_array_unref() aufrufen.
 */
GPtrArray* sond_graph_node_get_property_keys(SondGraphNode *node);

/**
 * sond_graph_node_get_properties:
 * @node: Ein SondGraphNode
 *
 * Gibt das interne Property-Array zurück (nicht-kopiert).
 * Das Array gehört der Node und darf nicht modifiziert oder freigegeben werden.
 *
 * Hinweis: Dies ist eine Low-Level-Funktion. Für die meisten Anwendungsfälle
 * sollten die einzelnen Property-Getter verwendet werden.
 *
 * Returns: (transfer none) (element-type SondGraphProperty): GPtrArray von SondGraphProperty
 */
GPtrArray* sond_graph_node_get_properties(SondGraphNode *node);

/* ========================================================================
 * Nested Properties (Sub-Properties als Metadaten)
 * ======================================================================== */

/**
 * sond_graph_node_set_nested_property:
 * @node: Ein SondGraphNode
 * @key: Property-Name
 * @nested_key: Sub-Property-Name
 * @values: Array von Strings
 * @n_values: Anzahl der Werte
 *
 * Setzt eine Sub-Property (Metadaten) für eine existierende Property.
 * Die Parent-Property muss bereits existieren.
 *
 * Beispiel:
 * |[<!-- language="C" -->
 * // Erst Hauptproperty setzen
 * sond_graph_node_set_property_string(node, "address", "Berlin");
 *
 * // Dann Metadaten hinzufügen
 * sond_graph_node_set_nested_property_string(node, "address", "type", "Hauptwohnsitz");
 * sond_graph_node_set_nested_property_string(node, "address", "valid_from", "2020-01-01");
 * ]|
 */
void sond_graph_node_set_nested_property(SondGraphNode *node,
                                          const gchar *key,
                                          const gchar *nested_key,
                                          const gchar **values,
                                          guint n_values);

/**
 * sond_graph_node_set_nested_property_string:
 * @node: Ein SondGraphNode
 * @key: Property-Name
 * @nested_key: Sub-Property-Name
 * @value: String-Wert
 *
 * Convenience-Funktion zum Setzen einer Sub-Property mit einem einzelnen Wert.
 */
void sond_graph_node_set_nested_property_string(SondGraphNode *node,
                                                 const gchar *key,
                                                 const gchar *nested_key,
                                                 const gchar *value);

/**
 * sond_graph_node_get_nested_property:
 * @node: Ein SondGraphNode
 * @key: Property-Name
 * @nested_key: Sub-Property-Name
 *
 * Liest eine Sub-Property.
 *
 * Returns: (transfer full) (element-type utf8) (nullable): GPtrArray von Strings oder NULL.
 *          Caller muss g_ptr_array_unref() aufrufen.
 */
GPtrArray* sond_graph_node_get_nested_property(SondGraphNode *node,
                                                const gchar *key,
                                                const gchar *nested_key);

/**
 * sond_graph_node_get_nested_property_string:
 * @node: Ein SondGraphNode
 * @key: Property-Name
 * @nested_key: Sub-Property-Name
 *
 * Convenience-Funktion zum Lesen des ersten Werts einer Sub-Property.
 *
 * Returns: (transfer full) (nullable): String oder NULL. Caller muss g_free() aufrufen.
 */
gchar* sond_graph_node_get_nested_property_string(SondGraphNode *node,
                                                   const gchar *key,
                                                   const gchar *nested_key);

/**
 * sond_graph_node_has_nested_property:
 * @node: Ein SondGraphNode
 * @key: Property-Name
 * @nested_key: Sub-Property-Name
 *
 * Prüft ob eine Sub-Property existiert.
 *
 * Returns: TRUE wenn die Sub-Property existiert
 */
gboolean sond_graph_node_has_nested_property(SondGraphNode *node,
                                              const gchar *key,
                                              const gchar *nested_key);

/* ========================================================================
 * Edge Management
 * ======================================================================== */

/**
 * sond_graph_node_add_outgoing_edge:
 * @node: Ein SondGraphNode
 * @edge: (transfer none): Edge-Objekt
 *
 * Fügt eine ausgehende Edge hinzu.
 * Die Node erhöht die Referenz-Zählung der Edge (g_object_ref).
 */
void sond_graph_node_add_outgoing_edge(SondGraphNode *node,
                                        SondGraphEdge *edge);

/**
 * sond_graph_node_remove_outgoing_edge:
 * @node: Ein SondGraphNode
 * @edge_id: Edge ID
 *
 * Entfernt eine ausgehende Edge anhand ihrer ID.
 */
void sond_graph_node_remove_outgoing_edge(SondGraphNode *node,
                                           gint64 edge_id);

/**
 * sond_graph_node_get_outgoing_edges:
 * @node: Ein SondGraphNode
 *
 * Gibt das Array aller ausgehenden Edges zurück.
 *
 * Returns: (transfer none) (element-type SondGraphEdge): GPtrArray von SondGraphEdge.
 *          Das Array gehört dem Node und darf nicht modifiziert werden.
 */
GPtrArray* sond_graph_node_get_outgoing_edges(SondGraphNode *node);

/**
 * sond_graph_node_find_edge:
 * @node: Ein SondGraphNode
 * @edge_id: Edge ID
 *
 * Findet eine Edge anhand ihrer ID.
 *
 * Returns: (transfer none) (nullable): SondGraphEdge oder NULL
 */
SondGraphEdge* sond_graph_node_find_edge(SondGraphNode *node,
                                          gint64 edge_id);

/* ========================================================================
 * JSON Serialization
 * ======================================================================== */

/**
 * sond_graph_node_to_json:
 * @node: Ein SondGraphNode
 *
 * Serialisiert die komplette Node zu JSON (ID, Label, Properties, Timestamps).
 * Edges werden nicht serialisiert (zu groß/zirkulär).
 *
 * Format:
 * {
 *   "id": 123,
 *   "label": "Person",
 *   "properties": [...],
 *   "created_at": "2025-01-01T12:00:00Z",
 *   "updated_at": "2025-01-02T12:00:00Z"
 * }
 *
 * Returns: (transfer full): JSON-String. Caller muss g_free() aufrufen.
 */
gchar* sond_graph_node_to_json(SondGraphNode *node);

/**
 * sond_graph_node_from_json:
 * @json: JSON-String
 * @error: (nullable): Error-Rückgabe
 *
 * Erstellt eine Node aus JSON.
 *
 * Returns: (transfer full) (nullable): Neue SondGraphNode oder NULL bei Fehler.
 *          Caller muss g_object_unref() aufrufen.
 */
SondGraphNode* sond_graph_node_from_json(const gchar *json, GError **error);

G_END_DECLS

#endif /* SOND_GRAPH_NODE_H */
