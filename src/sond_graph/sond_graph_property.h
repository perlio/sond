/*
 sond (sond_graph_property.h) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_graph_property.h
 * @brief Vereinfachte Property-API für verschachtelte String-Arrays
 *
 * Alle Property-Werte sind String-Arrays mit 1-n Elementen.
 * Properties können beliebig verschachtelt werden (Sub-Properties).
 */

#ifndef SOND_GRAPH_PROPERTY_H
#define SOND_GRAPH_PROPERTY_H

#include <glib.h>

G_BEGIN_DECLS

/* ========================================================================
 * Typen
 * ======================================================================== */

/**
 * SondGraphProperty:
 *
 * Eine Property bestehend aus:
 * - key: Name der Property
 * - values: Array von 1-n Strings
 * - properties: Optionales Array von Sub-Properties
 */
typedef struct _SondGraphProperty SondGraphProperty;

/* ========================================================================
 * Konstruktoren / Destruktoren
 * ======================================================================== */

/**
 * sond_graph_property_new:
 * @key: Property-Name
 * @values: Array von Strings
 * @n_values: Anzahl der Strings (muss > 0 sein)
 *
 * Erstellt eine neue Property mit mehreren Werten.
 *
 * Returns: (transfer full): Neue Property oder %NULL bei Fehler
 */
SondGraphProperty* sond_graph_property_new(const gchar *key,
                                            const gchar **values,
                                            guint n_values);

/**
 * sond_graph_property_new_string:
 * @key: Property-Name
 * @value: Einzelner String-Wert
 *
 * Convenience-Funktion für Property mit genau einem Wert.
 *
 * Returns: (transfer full): Neue Property oder %NULL bei Fehler
 */
SondGraphProperty* sond_graph_property_new_string(const gchar *key,
                                                   const gchar *value);

/**
 * sond_graph_property_free:
 * @prop: (nullable): Property zum Freigeben
 *
 * Gibt eine Property und alle Sub-Properties rekursiv frei.
 */
void sond_graph_property_free(SondGraphProperty *prop);

/* ========================================================================
 * Basis-Funktionen
 * ======================================================================== */

/**
 * sond_graph_property_find:
 * @properties: (element-type SondGraphProperty): Property-Array
 * @key: Gesuchter Property-Name
 *
 * Sucht eine Property nach Namen.
 *
 * Returns: (transfer none) (nullable): Property oder %NULL wenn nicht gefunden
 */
SondGraphProperty* sond_graph_property_find(GPtrArray *properties,
                                             const gchar *key);

/**
 * sond_graph_property_get_key:
 * @prop: Property
 *
 * Returns: (transfer none) (nullable): Property-Name
 */
const gchar* sond_graph_property_get_key(SondGraphProperty *prop);

/**
 * sond_graph_property_get_values:
 * @prop: Property
 *
 * Gibt direkte Referenz auf das Values-Array zurück (keine Kopie).
 * NICHT unreffen - gehört der Property!
 *
 * Returns: (transfer none) (nullable) (element-type utf8): Values-Array
 */
GPtrArray* sond_graph_property_get_values(SondGraphProperty *prop);

/**
 * sond_graph_property_get_first_value:
 * @prop: Property
 *
 * Convenience-Funktion für ersten Wert.
 *
 * Returns: (transfer none) (nullable): Erster Wert oder %NULL
 */
const gchar* sond_graph_property_get_first_value(SondGraphProperty *prop);

/**
 * sond_graph_property_get_value_count:
 * @prop: Property
 *
 * Returns: Anzahl der Werte (0 wenn Property ungültig)
 */
guint sond_graph_property_get_value_count(SondGraphProperty *prop);

/**
 * sond_graph_property_get_properties:
 * @prop: Property
 *
 * Gibt direkte Referenz auf Sub-Properties zurück (keine Kopie).
 *
 * Returns: (transfer none) (nullable) (element-type SondGraphProperty): Sub-Properties
 */
GPtrArray* sond_graph_property_get_properties(SondGraphProperty *prop);

/**
 * sond_graph_property_set_values:
 * @prop: Property
 * @values: Neue Werte
 * @n_values: Anzahl der Werte (muss > 0 sein)
 *
 * Setzt neue Werte für die Property.
 */
void sond_graph_property_set_values(SondGraphProperty *prop,
                                     const gchar **values,
                                     guint n_values);

/**
 * sond_graph_property_set_string:
 * @prop: Property
 * @value: Neuer einzelner Wert
 *
 * Convenience-Funktion zum Setzen eines einzelnen Werts.
 */
void sond_graph_property_set_string(SondGraphProperty *prop,
                                     const gchar *value);

/**
 * sond_graph_property_add_subproperty:
 * @prop: Eltern-Property
 * @subprop: (transfer full): Hinzuzufügende Sub-Property
 *
 * Fügt eine Sub-Property hinzu. Ownership wird übernommen.
 */
void sond_graph_property_add_subproperty(SondGraphProperty *prop,
                                          SondGraphProperty *subprop);

/* ========================================================================
 * High-Level API für Property-Listen
 * ======================================================================== */

/**
 * sond_graph_property_list_set:
 * @properties: (element-type SondGraphProperty): Property-Array
 * @key: Property-Name
 * @values: Werte
 * @n_values: Anzahl der Werte
 *
 * Setzt oder erstellt eine Property mit mehreren Werten.
 */
void sond_graph_property_list_set(GPtrArray *properties,
                                   const gchar *key,
                                   const gchar **values,
                                   guint n_values);

/**
 * sond_graph_property_list_set_string:
 * @properties: (element-type SondGraphProperty): Property-Array
 * @key: Property-Name
 * @value: Einzelner Wert
 *
 * Convenience für Property mit einem Wert.
 */
void sond_graph_property_list_set_string(GPtrArray *properties,
                                          const gchar *key,
                                          const gchar *value);

/**
 * sond_graph_property_list_get:
 * @properties: (element-type SondGraphProperty): Property-Array
 * @key: Property-Name
 *
 * Gibt Values-Array zurück. Caller muss g_ptr_array_unref() aufrufen!
 *
 * Returns: (transfer full) (nullable) (element-type utf8): Values oder %NULL
 */
GPtrArray* sond_graph_property_list_get(GPtrArray *properties,
                                         const gchar *key);

/**
 * sond_graph_property_list_get_string:
 * @properties: (element-type SondGraphProperty): Property-Array
 * @key: Property-Name
 *
 * Convenience für ersten Wert.
 *
 * Returns: (transfer none) (nullable): Erster Wert oder %NULL
 */
const gchar* sond_graph_property_list_get_string(GPtrArray *properties,
                                                  const gchar *key);

/**
 * sond_graph_property_list_get_count:
 * @properties: (element-type SondGraphProperty): Property-Array
 * @key: Property-Name
 *
 * Returns: Anzahl der Werte (0 wenn Property nicht existiert)
 */
guint sond_graph_property_list_get_count(GPtrArray *properties,
                                          const gchar *key);

/**
 * sond_graph_property_list_has:
 * @properties: (element-type SondGraphProperty): Property-Array
 * @key: Property-Name
 *
 * Returns: %TRUE wenn Property existiert
 */
gboolean sond_graph_property_list_has(GPtrArray *properties,
                                       const gchar *key);

/**
 * sond_graph_property_list_remove:
 * @properties: (element-type SondGraphProperty): Property-Array
 * @key: Property-Name
 *
 * Entfernt eine Property.
 */
void sond_graph_property_list_remove(GPtrArray *properties,
                                      const gchar *key);

/**
 * sond_graph_property_list_get_keys:
 * @properties: (element-type SondGraphProperty): Property-Array
 *
 * Gibt alle Property-Namen zurück.
 *
 * Returns: (transfer full) (element-type utf8): Array mit Namen
 */
GPtrArray* sond_graph_property_list_get_keys(GPtrArray *properties);

/* ========================================================================
 * Path-basierte API für verschachtelte Properties
 * ======================================================================== */

/**
 * sond_graph_property_list_set_at_path:
 * @properties: (element-type SondGraphProperty): Root-Properties
 * @path: (array length=path_length): Pfad zur Property
 * @path_length: Länge des Pfads
 * @values: Werte
 * @n_values: Anzahl der Werte
 *
 * Setzt eine verschachtelte Property über einen Pfad.
 * Fehlende Zwischenproperties werden automatisch erstellt.
 *
 * Beispiel: path = {"metadata", "author", "name"}, path_length = 3
 */
void sond_graph_property_list_set_at_path(GPtrArray *properties,
                                           const gchar **path,
                                           guint path_length,
                                           const gchar **values,
                                           guint n_values);

/**
 * sond_graph_property_list_set_string_at_path:
 * @properties: (element-type SondGraphProperty): Root-Properties
 * @path: (array length=path_length): Pfad zur Property
 * @path_length: Länge des Pfads
 * @value: Einzelner Wert
 *
 * Convenience für einzelnen Wert über Pfad.
 */
void sond_graph_property_list_set_string_at_path(GPtrArray *properties,
                                                   const gchar **path,
                                                   guint path_length,
                                                   const gchar *value);

/**
 * sond_graph_property_list_get_at_path:
 * @properties: (element-type SondGraphProperty): Root-Properties
 * @path: (array length=path_length): Pfad zur Property
 * @path_length: Länge des Pfads
 *
 * Gibt Values einer verschachtelten Property zurück.
 * Caller muss g_ptr_array_unref() aufrufen!
 *
 * Returns: (transfer full) (nullable) (element-type utf8): Values oder %NULL
 */
GPtrArray* sond_graph_property_list_get_at_path(GPtrArray *properties,
                                                 const gchar **path,
                                                 guint path_length);

/**
 * sond_graph_property_list_get_string_at_path:
 * @properties: (element-type SondGraphProperty): Root-Properties
 * @path: (array length=path_length): Pfad zur Property
 * @path_length: Länge des Pfads
 *
 * Convenience für ersten Wert über Pfad.
 *
 * Returns: (transfer full) (nullable): Erster Wert oder %NULL (muss mit g_free() freigegeben werden)
 */
gchar* sond_graph_property_list_get_string_at_path(GPtrArray *properties,
                                                    const gchar **path,
                                                    guint path_length);

/**
 * sond_graph_property_list_has_at_path:
 * @properties: (element-type SondGraphProperty): Root-Properties
 * @path: (array length=path_length): Pfad zur Property
 * @path_length: Länge des Pfads
 *
 * Returns: %TRUE wenn Property auf dem Pfad existiert
 */
gboolean sond_graph_property_list_has_at_path(GPtrArray *properties,
                                               const gchar **path,
                                               guint path_length);

/* ========================================================================
 * JSON Serialisierung
 * ======================================================================== */

/**
 * sond_graph_property_list_to_json:
 * @properties: (element-type SondGraphProperty): Properties
 *
 * Konvertiert Properties zu JSON.
 * Format: [["key", ["val1", "val2"], [sub-properties]], ...]
 *
 * Returns: (transfer full): JSON-String (muss mit g_free() freigegeben werden)
 */
gchar* sond_graph_property_list_to_json(GPtrArray *properties);

/**
 * sond_graph_property_list_from_json:
 * @json: JSON-String
 * @error: (nullable): Fehler-Rückgabe
 *
 * Parst Properties aus JSON.
 *
 * Returns: (transfer full) (element-type SondGraphProperty) (nullable):
 *          Property-Array oder %NULL bei Fehler
 */
GPtrArray* sond_graph_property_list_from_json(const gchar *json,
                                               GError **error);

G_END_DECLS

#endif /* SOND_GRAPH_PROPERTY_H */
