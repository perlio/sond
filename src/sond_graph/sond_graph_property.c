/*
 sond (sond_graph_property.c) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_graph_property.c
 * @brief Vereinfachte Property-Implementierung - alle Werte sind Arrays
 */

#include "sond_graph_property.h"
#include <json-glib/json-glib.h>
#include <string.h>

/* Interne Struktur - RADIKAL VEREINFACHT */
struct _SondGraphProperty {
    gchar *key;
    GPtrArray *values;      /* IMMER Array von Strings (1-n Elemente) */
    GPtrArray *properties;  /* Sub-Properties (optional) */
};

/* ========================================================================
 * Konstruktoren / Destruktoren
 * ======================================================================== */

SondGraphProperty* sond_graph_property_new(const gchar *key,
                                            const gchar **values,
                                            guint n_values) {
    g_return_val_if_fail(key != NULL, NULL);
    g_return_val_if_fail(values != NULL, NULL);
    g_return_val_if_fail(n_values > 0, NULL);

    SondGraphProperty *prop = g_new0(SondGraphProperty, 1);
    prop->key = g_strdup(key);

    /* Deep copy aller Values */
    prop->values = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; i < n_values; i++) {
        g_ptr_array_add(prop->values, g_strdup(values[i]));
    }

    prop->properties = NULL;
    return prop;
}

/* Convenience-Functor für einzelne Strings */
SondGraphProperty* sond_graph_property_new_string(const gchar *key,
                                                   const gchar *value) {
    const gchar *values[] = { value };
    return sond_graph_property_new(key, values, 1);
}

void sond_graph_property_free(SondGraphProperty *prop) {
    if (prop == NULL)
        return;

    g_free(prop->key);

    if (prop->values) {
        g_ptr_array_unref(prop->values);
    }

    if (prop->properties) {
        g_ptr_array_unref(prop->properties);
    }

    g_free(prop);
}

/* ========================================================================
 * Basis-Funktionen
 * ======================================================================== */

SondGraphProperty* sond_graph_property_find(GPtrArray *properties, const gchar *key) {
    if (!properties)
        return NULL;

    for (guint i = 0; i < properties->len; i++) {
        SondGraphProperty *prop = g_ptr_array_index(properties, i);
        if (g_strcmp0(prop->key, key) == 0) {
            return prop;
        }
    }
    return NULL;
}

const gchar* sond_graph_property_get_key(SondGraphProperty *prop) {
    return prop ? prop->key : NULL;
}

GPtrArray* sond_graph_property_get_values(SondGraphProperty *prop) {
    return prop ? prop->values : NULL;
}

/* Convenience für einzelne Werte */
const gchar* sond_graph_property_get_first_value(SondGraphProperty *prop) {
    if (!prop || !prop->values || prop->values->len == 0)
        return NULL;

    return g_ptr_array_index(prop->values, 0);
}

guint sond_graph_property_get_value_count(SondGraphProperty *prop) {
    if (!prop || !prop->values)
        return 0;

    return prop->values->len;
}

GPtrArray* sond_graph_property_get_properties(SondGraphProperty *prop) {
    return prop ? prop->properties : NULL;
}

void sond_graph_property_set_values(SondGraphProperty *prop,
                                     const gchar **values,
                                     guint n_values) {
    g_return_if_fail(prop != NULL);
    g_return_if_fail(values != NULL);
    g_return_if_fail(n_values > 0);

    if (prop->values) {
        g_ptr_array_unref(prop->values);
    }

    /* Deep copy */
    prop->values = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; i < n_values; i++) {
        g_ptr_array_add(prop->values, g_strdup(values[i]));
    }
}

/* Convenience für einzelne Werte */
void sond_graph_property_set_string(SondGraphProperty *prop, const gchar *value) {
    const gchar *values[] = { value };
    sond_graph_property_set_values(prop, values, 1);
}

void sond_graph_property_add_subproperty(SondGraphProperty *prop,
                                          SondGraphProperty *subprop) {
    g_return_if_fail(prop != NULL);
    g_return_if_fail(subprop != NULL);

    if (!prop->properties) {
        prop->properties = g_ptr_array_new_with_free_func(
            (GDestroyNotify)sond_graph_property_free);
    }

    g_ptr_array_add(prop->properties, subprop);
}

/* ========================================================================
 * High-Level API für Property-Listen
 * ======================================================================== */

void sond_graph_property_list_set(GPtrArray *properties,
                                   const gchar *key,
                                   const gchar **values,
                                   guint n_values) {
    g_return_if_fail(properties != NULL);
    g_return_if_fail(key != NULL);
    g_return_if_fail(values != NULL);
    g_return_if_fail(n_values > 0);

    SondGraphProperty *prop = sond_graph_property_find(properties, key);

    if (prop) {
        sond_graph_property_set_values(prop, values, n_values);
    } else {
        prop = sond_graph_property_new(key, values, n_values);
        g_ptr_array_add(properties, prop);
    }
}

/* Convenience für einzelne Strings */
void sond_graph_property_list_set_string(GPtrArray *properties,
                                          const gchar *key,
                                          const gchar *value) {
    const gchar *values[] = { value };
    sond_graph_property_list_set(properties, key, values, 1);
}

GPtrArray* sond_graph_property_list_get(GPtrArray *properties,
                                         const gchar *key) {
    g_return_val_if_fail(key != NULL, NULL);

    SondGraphProperty *prop = sond_graph_property_find(properties, key);
    if (!prop || !prop->values)
        return NULL;

    /* Reference erhöhen - Caller muss unref */
    return g_ptr_array_ref(prop->values);
}

/* Convenience für einzelne Werte */
const gchar* sond_graph_property_list_get_string(GPtrArray *properties,
                                                  const gchar *key) {
    g_return_val_if_fail(key != NULL, NULL);

    SondGraphProperty *prop = sond_graph_property_find(properties, key);
    return sond_graph_property_get_first_value(prop);
}

guint sond_graph_property_list_get_count(GPtrArray *properties,
                                          const gchar *key) {
    g_return_val_if_fail(key != NULL, 0);

    SondGraphProperty *prop = sond_graph_property_find(properties, key);
    return sond_graph_property_get_value_count(prop);
}

gboolean sond_graph_property_list_has(GPtrArray *properties,
                                       const gchar *key) {
    g_return_val_if_fail(key != NULL, FALSE);

    return sond_graph_property_find(properties, key) != NULL;
}

void sond_graph_property_list_remove(GPtrArray *properties,
                                      const gchar *key) {
    g_return_if_fail(properties != NULL);
    g_return_if_fail(key != NULL);

    for (guint i = 0; i < properties->len; i++) {
        SondGraphProperty *prop = g_ptr_array_index(properties, i);
        if (g_strcmp0(prop->key, key) == 0) {
            g_ptr_array_remove_index(properties, i);
            return;
        }
    }
}

GPtrArray* sond_graph_property_list_get_keys(GPtrArray *properties) {
    GPtrArray *keys = g_ptr_array_new_with_free_func(g_free);

    if (properties) {
        for (guint i = 0; i < properties->len; i++) {
            SondGraphProperty *prop = g_ptr_array_index(properties, i);
            g_ptr_array_add(keys, g_strdup(prop->key));
        }
    }

    return keys;
}

/* ========================================================================
 * Path-basierte API
 * ======================================================================== */

void sond_graph_property_list_set_at_path(GPtrArray *properties,
                                           const gchar **path,
                                           guint path_length,
                                           const gchar **values,
                                           guint n_values) {
    g_return_if_fail(properties != NULL);
    g_return_if_fail(path != NULL);
    g_return_if_fail(path_length > 0);
    g_return_if_fail(values != NULL);
    g_return_if_fail(n_values > 0);

    if (path_length == 1) {
        sond_graph_property_list_set(properties, path[0], values, n_values);
        return;
    }

    /* Navigiere durch Pfad */
    GPtrArray *current_array = properties;
    SondGraphProperty *current_prop = NULL;

    for (guint i = 0; i < path_length - 1; i++) {
        current_prop = sond_graph_property_find(current_array, path[i]);

        if (!current_prop) {
            /* Erstelle fehlende Zwischenproperties mit leerem Wert */
            const gchar *empty[] = { "" };
            current_prop = sond_graph_property_new(path[i], empty, 1);
            current_prop->properties = g_ptr_array_new_with_free_func(
                (GDestroyNotify)sond_graph_property_free);
            g_ptr_array_add(current_array, current_prop);
        } else if (!current_prop->properties) {
            current_prop->properties = g_ptr_array_new_with_free_func(
                (GDestroyNotify)sond_graph_property_free);
        }

        current_array = current_prop->properties;
    }

    /* Letzte Ebene */
    sond_graph_property_list_set(current_prop->properties,
                                  path[path_length - 1],
                                  values,
                                  n_values);
}

/* Convenience für einzelne Strings in Pfaden */
void sond_graph_property_list_set_string_at_path(GPtrArray *properties,
                                                   const gchar **path,
                                                   guint path_length,
                                                   const gchar *value) {
    const gchar *values[] = { value };
    sond_graph_property_list_set_at_path(properties, path, path_length, values, 1);
}

GPtrArray* sond_graph_property_list_get_at_path(GPtrArray *properties,
                                                 const gchar **path,
                                                 guint path_length) {
    g_return_val_if_fail(path != NULL, NULL);
    g_return_val_if_fail(path_length > 0, NULL);

    if (path_length == 1) {
        return sond_graph_property_list_get(properties, path[0]);
    }

    GPtrArray *current_array = properties;
    SondGraphProperty *current_prop = NULL;

    for (guint i = 0; i < path_length; i++) {
        current_prop = sond_graph_property_find(current_array, path[i]);

        if (!current_prop) {
            return NULL;
        }

        if (i == path_length - 1) {
            return current_prop->values ? g_ptr_array_ref(current_prop->values) : NULL;
        }

        if (!current_prop->properties) {
            return NULL;
        }

        current_array = current_prop->properties;
    }

    return NULL;
}

/* Convenience für einzelne Strings */
gchar* sond_graph_property_list_get_string_at_path(GPtrArray *properties,
                                                    const gchar **path,
                                                    guint path_length) {
    GPtrArray *values = sond_graph_property_list_get_at_path(properties, path, path_length);

    if (!values || values->len == 0) {
        if (values) g_ptr_array_unref(values);
        return NULL;
    }

    const gchar *first = g_ptr_array_index(values, 0);
    gchar *result = g_strdup(first);
    g_ptr_array_unref(values);

    return result;
}

gboolean sond_graph_property_list_has_at_path(GPtrArray *properties,
                                               const gchar **path,
                                               guint path_length) {
    GPtrArray *values = sond_graph_property_list_get_at_path(properties, path, path_length);
    if (values) {
        g_ptr_array_unref(values);
        return TRUE;
    }
    return FALSE;
}

/* ========================================================================
 * JSON Serialisierung - VIEL EINFACHER!
 * ======================================================================== */

static void property_to_json_builder(SondGraphProperty *prop, JsonBuilder *builder) {
    json_builder_begin_array(builder);

    /* Key */
    json_builder_add_string_value(builder, prop->key);

    /* Values - IMMER ein Array */
    json_builder_begin_array(builder);
    for (guint i = 0; i < prop->values->len; i++) {
        const gchar *val = g_ptr_array_index(prop->values, i);
        json_builder_add_string_value(builder, val);
    }
    json_builder_end_array(builder);

    /* Sub-Properties (optional) */
    if (prop->properties && prop->properties->len > 0) {
        json_builder_begin_array(builder);
        for (guint i = 0; i < prop->properties->len; i++) {
            SondGraphProperty *sub = g_ptr_array_index(prop->properties, i);
            property_to_json_builder(sub, builder);
        }
        json_builder_end_array(builder);
    }

    json_builder_end_array(builder);
}

gchar* sond_graph_property_list_to_json(GPtrArray *properties) {
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_array(builder);

    if (properties) {
        for (guint i = 0; i < properties->len; i++) {
            SondGraphProperty *prop = g_ptr_array_index(properties, i);
            property_to_json_builder(prop, builder);
        }
    }

    json_builder_end_array(builder);

    JsonGenerator *generator = json_generator_new();
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(generator, root);

    gchar *json = json_generator_to_data(generator, NULL);

    json_node_free(root);
    g_object_unref(generator);
    g_object_unref(builder);

    return json;
}

static SondGraphProperty* property_from_json_array(JsonArray *array) {
    guint length = json_array_get_length(array);

    if (length < 2) {
        return NULL;
    }

    /* Key */
    JsonNode *key_node = json_array_get_element(array, 0);
    if (!JSON_NODE_HOLDS_VALUE(key_node)) {
        return NULL;
    }

    const gchar *key = json_node_get_string(key_node);
    if (!key) {
        return NULL;
    }

    /* Values - MUSS ein Array sein */
    JsonNode *values_node = json_array_get_element(array, 1);
    if (!JSON_NODE_HOLDS_ARRAY(values_node)) {
        return NULL;
    }

    JsonArray *values_array = json_node_get_array(values_node);
    guint n_values = json_array_get_length(values_array);

    if (n_values == 0) {
        return NULL;
    }

    /* Sammle alle Values */
    GPtrArray *values = g_ptr_array_new();
    for (guint i = 0; i < n_values; i++) {
        JsonNode *val = json_array_get_element(values_array, i);
        if (JSON_NODE_HOLDS_VALUE(val)) {
            const gchar *val_str = json_node_get_string(val);
            if (val_str) {
                g_ptr_array_add(values, (gpointer)val_str);
            }
        }
    }

    if (values->len == 0) {
        g_ptr_array_free(values, TRUE);
        return NULL;
    }

    /* Erstelle Property */
    SondGraphProperty *prop = sond_graph_property_new(key,
                                                       (const gchar**)values->pdata,
                                                       values->len);
    g_ptr_array_free(values, TRUE);

    /* Sub-Properties (optional) */
    if (length >= 3) {
        JsonNode *props_node = json_array_get_element(array, 2);

        if (JSON_NODE_HOLDS_ARRAY(props_node)) {
            JsonArray *props_array = json_node_get_array(props_node);
            guint props_length = json_array_get_length(props_array);

            if (props_length > 0) {
                prop->properties = g_ptr_array_new_with_free_func(
                    (GDestroyNotify)sond_graph_property_free);

                for (guint i = 0; i < props_length; i++) {
                    JsonNode *element = json_array_get_element(props_array, i);

                    if (JSON_NODE_HOLDS_ARRAY(element)) {
                        SondGraphProperty *sub = property_from_json_array(
                            json_node_get_array(element));

                        if (sub) {
                            g_ptr_array_add(prop->properties, sub);
                        }
                    }
                }
            }
        }
    }

    return prop;
}

GPtrArray* sond_graph_property_list_from_json(const gchar *json, GError **error) {
    g_return_val_if_fail(json != NULL, NULL);

    JsonParser *parser = json_parser_new();

    if (!json_parser_load_from_data(parser, json, -1, error)) {
        g_object_unref(parser);
        return NULL;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_ARRAY(root)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Root node is not an array");
        g_object_unref(parser);
        return NULL;
    }

    JsonArray *array = json_node_get_array(root);
    guint length = json_array_get_length(array);

    GPtrArray *properties = g_ptr_array_new_with_free_func(
        (GDestroyNotify)sond_graph_property_free);

    for (guint i = 0; i < length; i++) {
        JsonNode *element = json_array_get_element(array, i);

        if (!JSON_NODE_HOLDS_ARRAY(element)) {
            continue;
        }

        SondGraphProperty *prop = property_from_json_array(
            json_node_get_array(element));

        if (prop) {
            g_ptr_array_add(properties, prop);
        }
    }

    g_object_unref(parser);
    return properties;
}

/*
 * Kompilierung:
 * gcc -c sond_graph_property.c $(pkg-config --cflags glib-2.0 json-glib-1.0)
 */
