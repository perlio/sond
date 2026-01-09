/*
 sond (sond_graph_edge.h) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_graph_edge.h
 * @brief Graph Edge Implementation - Nutzt SondGraphProperty direkt
 *
 * Die Edge nutzt SondGraphProperty für alle Property-Operationen.
 * Dies stellt Konsistenz mit Node und dem Property-System sicher.
 *
 * Properties Format (identisch mit Node):
 * - Property = [key, [values...], [sub-properties...]]
 * - Values sind IMMER ein Array von Strings (auch bei nur 1 Element)
 * - Sub-Properties sind optionale Metadaten über den Wert
 */

#ifndef SOND_GRAPH_EDGE_H
#define SOND_GRAPH_EDGE_H

#include <glib-object.h>
#include <glib.h>

G_BEGIN_DECLS

/* ========================================================================
 * Type Definitions
 * ======================================================================== */

#define SOND_TYPE_GRAPH_EDGE (sond_graph_edge_get_type())
G_DECLARE_FINAL_TYPE(SondGraphEdge, sond_graph_edge, SOND, GRAPH_EDGE, GObject)

/* ========================================================================
 * Constructor / Destructor
 * ======================================================================== */

/**
 * sond_graph_edge_new:
 *
 * Erstellt eine neue Graph-Edge.
 *
 * Returns: (transfer full): Neue SondGraphEdge
 */
SondGraphEdge* sond_graph_edge_new(void);

/* ========================================================================
 * Basic Properties (ID, Label, Source/Target, Timestamps)
 * ======================================================================== */

/**
 * sond_graph_edge_get_id:
 * @edge: Eine SondGraphEdge
 *
 * Gibt die ID der Edge zurück.
 *
 * Returns: Edge ID
 */
gint64 sond_graph_edge_get_id(SondGraphEdge *edge);

/**
 * sond_graph_edge_set_id:
 * @edge: Eine SondGraphEdge
 * @id: Neue ID
 *
 * Setzt die ID der Edge.
 */
void sond_graph_edge_set_id(SondGraphEdge *edge, gint64 id);

/**
 * sond_graph_edge_get_source_id:
 * @edge: Eine SondGraphEdge
 *
 * Gibt die ID des Quellknotens zurück.
 *
 * Returns: Source Node ID
 */
gint64 sond_graph_edge_get_source_id(SondGraphEdge *edge);

/**
 * sond_graph_edge_set_source_id:
 * @edge: Eine SondGraphEdge
 * @source_id: Source Node ID
 *
 * Setzt die ID des Quellknotens.
 */
void sond_graph_edge_set_source_id(SondGraphEdge *edge, gint64 source_id);

/**
 * sond_graph_edge_get_target_id:
 * @edge: Eine SondGraphEdge
 *
 * Gibt die ID des Zielknotens zurück.
 *
 * Returns: Target Node ID
 */
gint64 sond_graph_edge_get_target_id(SondGraphEdge *edge);

/**
 * sond_graph_edge_set_target_id:
 * @edge: Eine SondGraphEdge
 * @target_id: Target Node ID
 *
 * Setzt die ID des Zielknotens.
 */
void sond_graph_edge_set_target_id(SondGraphEdge *edge, gint64 target_id);

/**
 * sond_graph_edge_get_label:
 * @edge: Eine SondGraphEdge
 *
 * Gibt das Label der Edge zurück (z.B. "KNOWS", "WORKS_AT").
 *
 * Returns: (nullable): Label oder NULL
 */
const gchar* sond_graph_edge_get_label(SondGraphEdge *edge);

/**
 * sond_graph_edge_set_label:
 * @edge: Eine SondGraphEdge
 * @label: Neues Label
 *
 * Setzt das Label der Edge.
 */
void sond_graph_edge_set_label(SondGraphEdge *edge, const gchar *label);

/**
 * sond_graph_edge_get_created_at:
 * @edge: Eine SondGraphEdge
 *
 * Gibt den Erstellungszeitpunkt zurück.
 *
 * Returns: (nullable) (transfer none): GDateTime oder NULL
 */
GDateTime* sond_graph_edge_get_created_at(SondGraphEdge *edge);

/**
 * sond_graph_edge_set_created_at:
 * @edge: Eine SondGraphEdge
 * @created_at: (nullable): Erstellungszeitpunkt
 *
 * Setzt den Erstellungszeitpunkt.
 */
void sond_graph_edge_set_created_at(SondGraphEdge *edge, GDateTime *created_at);

/**
 * sond_graph_edge_get_updated_at:
 * @edge: Eine SondGraphEdge
 *
 * Gibt den letzten Änderungszeitpunkt zurück.
 *
 * Returns: (nullable) (transfer none): GDateTime oder NULL
 */
GDateTime* sond_graph_edge_get_updated_at(SondGraphEdge *edge);

/**
 * sond_graph_edge_set_updated_at:
 * @edge: Eine SondGraphEdge
 * @updated_at: (nullable): Änderungszeitpunkt
 *
 * Setzt den Änderungszeitpunkt.
 */
void sond_graph_edge_set_updated_at(SondGraphEdge *edge, GDateTime *updated_at);

/* ========================================================================
 * Properties Management
 * ======================================================================== */

/**
 * sond_graph_edge_set_property:
 * @edge: Eine SondGraphEdge
 * @key: Property-Name
 * @values: Array von Strings
 * @n_values: Anzahl der Werte
 *
 * Setzt eine Property mit einem oder mehreren Werten.
 *
 * Beispiel:
 * |[<!-- language="C" -->
 * const gchar *since[] = {"2020", "01", "15"};
 * sond_graph_edge_set_property(edge, "since", since, 3);
 * ]|
 */
void sond_graph_edge_set_property(SondGraphEdge *edge,
                                   const gchar *key,
                                   const gchar **values,
                                   guint n_values);

/**
 * sond_graph_edge_set_property_string:
 * @edge: Eine SondGraphEdge
 * @key: Property-Name
 * @value: String-Wert
 *
 * Convenience-Funktion zum Setzen einer Property mit einem einzelnen Wert.
 *
 * Beispiel:
 * |[<!-- language="C" -->
 * sond_graph_edge_set_property_string(edge, "weight", "5.0");
 * ]|
 */
void sond_graph_edge_set_property_string(SondGraphEdge *edge,
                                          const gchar *key,
                                          const gchar *value);

/**
 * sond_graph_edge_set_properties:
 * @edge: Eine SondGraphEdge
 * @properties: (element-type SondGraphProperty): GPtrArray von SondGraphProperty
 *
 * Setzt mehrere Properties auf einmal aus einem Property-Array.
 *
 * Beispiel:
 * |[<!-- language="C" -->
 * GPtrArray *props = fetch_edge_properties_from_db(edge_id);
 * sond_graph_edge_set_properties(edge, props);
 * ]|
 */
void sond_graph_edge_set_properties(SondGraphEdge *edge, GPtrArray *properties);

/**
 * sond_graph_edge_get_property:
 * @edge: Eine SondGraphEdge
 * @key: Property-Name
 *
 * Liest eine Property als Array von Strings.
 *
 * Returns: (transfer full) (element-type utf8) (nullable): GPtrArray von Strings oder NULL.
 *          Caller muss g_ptr_array_unref() aufrufen.
 *
 * Beispiel:
 * |[<!-- language="C" -->
 * GPtrArray *since = sond_graph_edge_get_property(edge, "since");
 * if (since) {
 *     g_print("Since: %s-%s-%s\n",
 *             (gchar*)g_ptr_array_index(since, 0),
 *             (gchar*)g_ptr_array_index(since, 1),
 *             (gchar*)g_ptr_array_index(since, 2));
 *     g_ptr_array_unref(since);
 * }
 * ]|
 */
GPtrArray* sond_graph_edge_get_property(SondGraphEdge *edge,
                                         const gchar *key);

/**
 * sond_graph_edge_get_property_string:
 * @edge: Eine SondGraphEdge
 * @key: Property-Name
 *
 * Convenience-Funktion zum Lesen des ersten Werts einer Property.
 *
 * Returns: (transfer full) (nullable): String oder NULL. Caller muss g_free() aufrufen.
 *
 * Beispiel:
 * |[<!-- language="C" -->
 * gchar *weight = sond_graph_edge_get_property_string(edge, "weight");
 * g_print("Weight: %s\n", weight);
 * g_free(weight);
 * ]|
 */
gchar* sond_graph_edge_get_property_string(SondGraphEdge *edge,
                                            const gchar *key);

/**
 * sond_graph_edge_get_property_count:
 * @edge: Eine SondGraphEdge
 * @key: Property-Name
 *
 * Gibt die Anzahl der Werte einer Property zurück.
 *
 * Returns: Anzahl der Werte oder 0
 */
guint sond_graph_edge_get_property_count(SondGraphEdge *edge,
                                          const gchar *key);

/**
 * sond_graph_edge_has_property:
 * @edge: Eine SondGraphEdge
 * @key: Property-Name
 *
 * Prüft ob eine Property existiert.
 *
 * Returns: TRUE wenn die Property existiert
 */
gboolean sond_graph_edge_has_property(SondGraphEdge *edge,
                                       const gchar *key);

/**
 * sond_graph_edge_remove_property:
 * @edge: Eine SondGraphEdge
 * @key: Property-Name
 *
 * Entfernt eine Property.
 */
void sond_graph_edge_remove_property(SondGraphEdge *edge,
                                      const gchar *key);

/**
 * sond_graph_edge_get_property_keys:
 * @edge: Eine SondGraphEdge
 *
 * Gibt eine Liste aller Property-Namen zurück.
 *
 * Returns: (transfer full) (element-type utf8): GPtrArray von Strings.
 *          Caller muss g_ptr_array_unref() aufrufen.
 */
GPtrArray* sond_graph_edge_get_property_keys(SondGraphEdge *edge);

/**
 * sond_graph_edge_get_properties:
 * @edge: Eine SondGraphEdge
 *
 * Gibt das interne Property-Array zurück (nicht-kopiert).
 * Das Array gehört der Edge und darf nicht modifiziert oder freigegeben werden.
 *
 * Returns: (transfer none) (element-type SondGraphProperty): GPtrArray von SondGraphProperty
 */
GPtrArray* sond_graph_edge_get_properties(SondGraphEdge *edge);

/* ========================================================================
 * JSON Serialization
 * ======================================================================== */

/**
 * sond_graph_edge_to_json:
 * @edge: Eine SondGraphEdge
 *
 * Serialisiert die komplette Edge zu JSON.
 *
 * Format:
 * {
 *   "id": 456,
 *   "source_id": 123,
 *   "target_id": 789,
 *   "label": "KNOWS",
 *   "properties": [...],
 *   "created_at": "2025-01-01T12:00:00Z",
 *   "updated_at": "2025-01-02T12:00:00Z"
 * }
 *
 * Returns: (transfer full): JSON-String. Caller muss g_free() aufrufen.
 */
gchar* sond_graph_edge_to_json(SondGraphEdge *edge);

/**
 * sond_graph_edge_from_json:
 * @json: JSON-String
 * @error: (nullable): Error-Rückgabe
 *
 * Erstellt eine Edge aus JSON.
 *
 * Returns: (transfer full) (nullable): Neue SondGraphEdge oder NULL bei Fehler.
 *          Caller muss g_object_unref() aufrufen.
 */
SondGraphEdge* sond_graph_edge_from_json(const gchar *json, GError **error);

G_END_DECLS

#endif /* SOND_GRAPH_EDGE_H */
