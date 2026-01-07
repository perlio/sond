/*
 sond (sond_graph_node.c) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_graph_node.c - KORRIGIERTE VERSION v2
 * @brief Konsistente API: Values sind IMMER Arrays!
 */

/* ========================================================================
 * WICHTIG: Properties Format (KONSISTENT mit sond_graph_property.c):
 *
 * Eine Property ist ein Array:
 * - Element 0: key (String)
 * - Element 1: values (IMMER Array von Strings, auch bei 1 Element!)
 * - Element 2 (optional): properties (Array von Sub-Properties = Metadaten)
 *
 * Beispiele:
 * ["name", ["Alice"]]
 * ["age", ["30"]]
 * ["address", ["Berlin", "Hauptstr 1", "10115", "Germany"], [
 *   ["type", ["Hauptwohnsitz"]],
 *   ["valid_from", ["2020-01-01"]]
 * ]]
 * ======================================================================== */

#include "sond_graph_node.h"
#include "sond_graph_property.h"
#include <json-glib/json-glib.h>
#include <string.h>

/* Internal structure - VEREINFACHT! */
typedef struct {
    gchar *key;
    GPtrArray *values;         /* IMMER Array von Strings (1-n Elemente) */
    GList *properties;         /* Metadaten (Liste von SondGraphNodeProperty*) */
} SondGraphNodeProperty;

struct _SondGraphNode {
    GObject parent_instance;

    gint64 id;
    gchar *label;
    GList *properties;           /* Liste von SondGraphNodeProperty* */
    GList *outgoing_edges;       /* Liste von SondGraphEdgeRef* */
    GDateTime *created_at;
    GDateTime *updated_at;
};

G_DEFINE_TYPE(SondGraphNode, sond_graph_node, G_TYPE_OBJECT)

/* ========================================================================
 * SondGraphNodeProperty Internal Functions
 * ======================================================================== */

static SondGraphNodeProperty* property_new(const gchar *key,
                                            const gchar **values,
                                            guint n_values) {
    g_return_val_if_fail(key != NULL, NULL);
    g_return_val_if_fail(values != NULL, NULL);
    g_return_val_if_fail(n_values > 0, NULL);

    SondGraphNodeProperty *prop = g_new0(SondGraphNodeProperty, 1);
    prop->key = g_strdup(key);

    /* Deep copy aller Values */
    prop->values = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; i < n_values; i++) {
        g_ptr_array_add(prop->values, g_strdup(values[i]));
    }

    prop->properties = NULL;
    return prop;
}

/* Convenience für einzelne Werte */
static SondGraphNodeProperty* property_new_string(const gchar *key, const gchar *value) {
    const gchar *values[] = { value };
    return property_new(key, values, 1);
}

static void property_free(SondGraphNodeProperty *prop) {
    if (prop == NULL)
        return;

    g_free(prop->key);

    if (prop->values) {
        g_ptr_array_unref(prop->values);
    }

    if (prop->properties) {
        g_list_free_full(prop->properties, (GDestroyNotify)property_free);
    }

    g_free(prop);
}

static SondGraphNodeProperty* property_find(GList *properties, const gchar *key) {
    for (GList *l = properties; l != NULL; l = l->next) {
        SondGraphNodeProperty *prop = l->data;
        if (g_strcmp0(prop->key, key) == 0) {
            return prop;
        }
    }
    return NULL;
}

/* ========================================================================
 * SondGraphEdgeRef Implementation
 * ======================================================================== */

SondGraphEdgeRef* sond_graph_edge_ref_new(gint64 edge_id, const gchar *label, gint64 target_id) {
    SondGraphEdgeRef *ref = g_new0(SondGraphEdgeRef, 1);
    ref->edge_id = edge_id;
    ref->label = g_strdup(label);
    ref->target_id = target_id;
    return ref;
}

void sond_graph_edge_ref_free(SondGraphEdgeRef *ref) {
    if (ref == NULL)
        return;

    g_free(ref->label);
    g_free(ref);
}

/* ========================================================================
 * SondGraphNode GObject Implementation
 * ======================================================================== */

static void sond_graph_node_finalize(GObject *object) {
    SondGraphNode *self = SOND_GRAPH_NODE(object);

    g_free(self->label);

    if (self->properties) {
        g_list_free_full(self->properties, (GDestroyNotify)property_free);
    }

    if (self->outgoing_edges) {
        g_list_free_full(self->outgoing_edges, (GDestroyNotify)sond_graph_edge_ref_free);
    }

    if (self->created_at) {
        g_date_time_unref(self->created_at);
    }

    if (self->updated_at) {
        g_date_time_unref(self->updated_at);
    }

    G_OBJECT_CLASS(sond_graph_node_parent_class)->finalize(object);
}

static void sond_graph_node_class_init(SondGraphNodeClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = sond_graph_node_finalize;
}

static void sond_graph_node_init(SondGraphNode *self) {
    self->id = 0;
    self->label = NULL;
    self->properties = NULL;
    self->outgoing_edges = NULL;
    self->created_at = NULL;
    self->updated_at = NULL;
}

SondGraphNode* sond_graph_node_new(void) {
    return g_object_new(SOND_GRAPH_TYPE_NODE, NULL);
}

/* ========================================================================
 * Getters/Setters
 * ======================================================================== */

gint64 sond_graph_node_get_id(SondGraphNode *node) {
    g_return_val_if_fail(SOND_GRAPH_IS_NODE(node), 0);
    return node->id;
}

void sond_graph_node_set_id(SondGraphNode *node, gint64 id) {
    g_return_if_fail(SOND_GRAPH_IS_NODE(node));
    node->id = id;
}

const gchar* sond_graph_node_get_label(SondGraphNode *node) {
    g_return_val_if_fail(SOND_GRAPH_IS_NODE(node), NULL);
    return node->label;
}

void sond_graph_node_set_label(SondGraphNode *node, const gchar *label) {
    g_return_if_fail(SOND_GRAPH_IS_NODE(node));
    g_free(node->label);
    node->label = g_strdup(label);
}

GDateTime* sond_graph_node_get_created_at(SondGraphNode *node) {
    g_return_val_if_fail(SOND_GRAPH_IS_NODE(node), NULL);
    return node->created_at;
}

void sond_graph_node_set_created_at(SondGraphNode *node, GDateTime *created_at) {
    g_return_if_fail(SOND_GRAPH_IS_NODE(node));
    if (node->created_at) {
        g_date_time_unref(node->created_at);
    }
    node->created_at = created_at ? g_date_time_ref(created_at) : NULL;
}

GDateTime* sond_graph_node_get_updated_at(SondGraphNode *node) {
    g_return_val_if_fail(SOND_GRAPH_IS_NODE(node), NULL);
    return node->updated_at;
}

void sond_graph_node_set_updated_at(SondGraphNode *node, GDateTime *updated_at) {
    g_return_if_fail(SOND_GRAPH_IS_NODE(node));
    if (node->updated_at) {
        g_date_time_unref(node->updated_at);
    }
    node->updated_at = updated_at ? g_date_time_ref(updated_at) : NULL;
}

/* ========================================================================
 * Properties Management - KONSISTENTE API (Values immer Array!)
 * ======================================================================== */

/**
 * sond_graph_node_set_property:
 *
 * Setzt eine Property mit einem Array von Values.
 * Dies ist die einzige "set"-Funktion - konsistent mit sond_graph_property!
 */
void sond_graph_node_set_property(SondGraphNode *node,
                                   const gchar *key,
                                   const gchar **values,
                                   guint n_values) {
    g_return_if_fail(SOND_GRAPH_IS_NODE(node));
    g_return_if_fail(key != NULL);
    g_return_if_fail(values != NULL);
    g_return_if_fail(n_values > 0);

    SondGraphNodeProperty *prop = property_find(node->properties, key);

    if (prop) {
        /* Update: Ersetze values */
        if (prop->values) {
            g_ptr_array_unref(prop->values);
        }

        prop->values = g_ptr_array_new_with_free_func(g_free);
        for (guint i = 0; i < n_values; i++) {
            g_ptr_array_add(prop->values, g_strdup(values[i]));
        }
    } else {
        /* Neu anlegen */
        prop = property_new(key, values, n_values);
        node->properties = g_list_append(node->properties, prop);
    }
}

/**
 * sond_graph_node_set_property_string:
 *
 * Convenience-Funktion für einzelne String-Werte.
 */
void sond_graph_node_set_property_string(SondGraphNode *node,
                                          const gchar *key,
                                          const gchar *value) {
    const gchar *values[] = { value };
    sond_graph_node_set_property(node, key, values, 1);
}

/**
 * sond_graph_node_set_properties:
 *
 * Setzt mehrere Properties auf einmal aus einem GPtrArray von SondGraphProperty.
 */
void sond_graph_node_set_properties(SondGraphNode *node, GPtrArray *props) {
    g_return_if_fail(SOND_GRAPH_IS_NODE(node));

    if (!props || props->len == 0)
        return;

    for (guint i = 0; i < props->len; i++) {
        SondGraphProperty *prop = g_ptr_array_index(props, i);

        const gchar *key = sond_graph_property_get_key(prop);
        if (!key)
            continue;

        GPtrArray *values = sond_graph_property_get_values(prop);
        if (!values || values->len == 0) {
            if (values) g_ptr_array_unref(values);
            continue;
        }

        /* Setze Property (values ist immer ein Array) */
        sond_graph_node_set_property(node, key,
                                      (const gchar **)values->pdata,
                                      values->len);

        g_ptr_array_unref(values);
    }
}

/**
 * sond_graph_node_get_property:
 *
 * Liest eine Property als Array von Values.
 *
 * Returns: (transfer full): GPtrArray von Strings (caller muss unref)
 */
GPtrArray* sond_graph_node_get_property(SondGraphNode *node,
                                         const gchar *key) {
    g_return_val_if_fail(SOND_GRAPH_IS_NODE(node), NULL);
    g_return_val_if_fail(key != NULL, NULL);

    SondGraphNodeProperty *prop = property_find(node->properties, key);
    if (!prop || !prop->values)
        return NULL;

    /* Deep copy */
    GPtrArray *result = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; i < prop->values->len; i++) {
        const gchar *val = g_ptr_array_index(prop->values, i);
        g_ptr_array_add(result, g_strdup(val));
    }

    return result;
}

/**
 * sond_graph_node_get_property_string:
 *
 * Convenience-Funktion: Liest den ersten Wert einer Property.
 *
 * Returns: (transfer full): String oder NULL
 */
gchar* sond_graph_node_get_property_string(SondGraphNode *node,
                                            const gchar *key) {
    g_return_val_if_fail(SOND_GRAPH_IS_NODE(node), NULL);
    g_return_val_if_fail(key != NULL, NULL);

    SondGraphNodeProperty *prop = property_find(node->properties, key);
    if (!prop || !prop->values || prop->values->len == 0)
        return NULL;

    const gchar *first = g_ptr_array_index(prop->values, 0);
    return g_strdup(first);
}

/**
 * sond_graph_node_get_property_count:
 *
 * Gibt die Anzahl der Values einer Property zurück.
 */
guint sond_graph_node_get_property_count(SondGraphNode *node,
                                          const gchar *key) {
    g_return_val_if_fail(SOND_GRAPH_IS_NODE(node), 0);
    g_return_val_if_fail(key != NULL, 0);

    SondGraphNodeProperty *prop = property_find(node->properties, key);
    if (!prop || !prop->values)
        return 0;

    return prop->values->len;
}

/**
 * sond_graph_node_has_property:
 */
gboolean sond_graph_node_has_property(SondGraphNode *node,
                                       const gchar *key) {
    g_return_val_if_fail(SOND_GRAPH_IS_NODE(node), FALSE);
    g_return_val_if_fail(key != NULL, FALSE);

    return property_find(node->properties, key) != NULL;
}

/**
 * sond_graph_node_remove_property:
 */
void sond_graph_node_remove_property(SondGraphNode *node,
                                      const gchar *key) {
    g_return_if_fail(SOND_GRAPH_IS_NODE(node));
    g_return_if_fail(key != NULL);

    SondGraphNodeProperty *prop = property_find(node->properties, key);
    if (prop) {
        node->properties = g_list_remove(node->properties, prop);
        property_free(prop);
    }
}

/**
 * sond_graph_node_get_property_keys:
 */
GList* sond_graph_node_get_property_keys(SondGraphNode *node) {
    g_return_val_if_fail(SOND_GRAPH_IS_NODE(node), NULL);

    GList *keys = NULL;
    for (GList *l = node->properties; l != NULL; l = l->next) {
        SondGraphNodeProperty *prop = l->data;
        keys = g_list_append(keys, g_strdup(prop->key));
    }
    return keys;
}

/* ========================================================================
 * Nested Properties Management (Sub-Properties als Metadaten)
 * ======================================================================== */

/**
 * sond_graph_node_set_nested_property:
 *
 * Setzt eine Sub-Property (Metadaten) für eine existierende Property.
 *
 * Beispiel:
 *   sond_graph_node_set_property_string(node, "address", "Berlin");
 *   sond_graph_node_set_nested_property_string(node, "address", "type", "Hauptwohnsitz");
 */
void sond_graph_node_set_nested_property(SondGraphNode *node,
                                          const gchar *key,
                                          const gchar *nested_key,
                                          const gchar **values,
                                          guint n_values) {
    g_return_if_fail(SOND_GRAPH_IS_NODE(node));
    g_return_if_fail(key != NULL);
    g_return_if_fail(nested_key != NULL);
    g_return_if_fail(values != NULL);
    g_return_if_fail(n_values > 0);

    SondGraphNodeProperty *prop = property_find(node->properties, key);
    if (!prop) {
        g_warning("sond_graph_node_set_nested_property: Parent property '%s' does not exist", key);
        return;
    }

    /* Finde oder erstelle nested property */
    SondGraphNodeProperty *nested = property_find(prop->properties, nested_key);

    if (nested) {
        /* Update */
        if (nested->values) {
            g_ptr_array_unref(nested->values);
        }
        nested->values = g_ptr_array_new_with_free_func(g_free);
        for (guint i = 0; i < n_values; i++) {
            g_ptr_array_add(nested->values, g_strdup(values[i]));
        }
    } else {
        /* Neu anlegen */
        nested = property_new(nested_key, values, n_values);
        prop->properties = g_list_append(prop->properties, nested);
    }
}

/**
 * sond_graph_node_set_nested_property_string:
 *
 * Convenience für einzelne String-Werte.
 */
void sond_graph_node_set_nested_property_string(SondGraphNode *node,
                                                 const gchar *key,
                                                 const gchar *nested_key,
                                                 const gchar *value) {
    const gchar *values[] = { value };
    sond_graph_node_set_nested_property(node, key, nested_key, values, 1);
}

/**
 * sond_graph_node_get_nested_property:
 *
 * Liest eine Sub-Property.
 *
 * Returns: (transfer full): GPtrArray oder NULL
 */
GPtrArray* sond_graph_node_get_nested_property(SondGraphNode *node,
                                                const gchar *key,
                                                const gchar *nested_key) {
    g_return_val_if_fail(SOND_GRAPH_IS_NODE(node), NULL);
    g_return_val_if_fail(key != NULL, NULL);
    g_return_val_if_fail(nested_key != NULL, NULL);

    SondGraphNodeProperty *prop = property_find(node->properties, key);
    if (!prop)
        return NULL;

    SondGraphNodeProperty *nested = property_find(prop->properties, nested_key);
    if (!nested || !nested->values)
        return NULL;

    /* Deep copy */
    GPtrArray *result = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; i < nested->values->len; i++) {
        const gchar *val = g_ptr_array_index(nested->values, i);
        g_ptr_array_add(result, g_strdup(val));
    }

    return result;
}

/**
 * sond_graph_node_get_nested_property_string:
 *
 * Convenience für einzelne Strings.
 */
gchar* sond_graph_node_get_nested_property_string(SondGraphNode *node,
                                                   const gchar *key,
                                                   const gchar *nested_key) {
    GPtrArray *values = sond_graph_node_get_nested_property(node, key, nested_key);
    if (!values || values->len == 0) {
        if (values) g_ptr_array_unref(values);
        return NULL;
    }

    gchar *result = g_strdup(g_ptr_array_index(values, 0));
    g_ptr_array_unref(values);
    return result;
}

/**
 * sond_graph_node_has_nested_property:
 */
gboolean sond_graph_node_has_nested_property(SondGraphNode *node,
                                              const gchar *key,
                                              const gchar *nested_key) {
    g_return_val_if_fail(SOND_GRAPH_IS_NODE(node), FALSE);
    g_return_val_if_fail(key != NULL, FALSE);
    g_return_val_if_fail(nested_key != NULL, FALSE);

    SondGraphNodeProperty *prop = property_find(node->properties, key);
    if (!prop)
        return FALSE;

    return property_find(prop->properties, nested_key) != NULL;
}

/* ========================================================================
 * Outgoing Edges Management
 * ======================================================================== */

void sond_graph_node_add_outgoing_edge(SondGraphNode *node,
                                        SondGraphEdgeRef *edge_ref) {
    g_return_if_fail(SOND_GRAPH_IS_NODE(node));
    g_return_if_fail(edge_ref != NULL);

    node->outgoing_edges = g_list_append(node->outgoing_edges, edge_ref);
}

void sond_graph_node_remove_outgoing_edge(SondGraphNode *node,
                                           gint64 edge_id) {
    g_return_if_fail(SOND_GRAPH_IS_NODE(node));

    for (GList *l = node->outgoing_edges; l != NULL; l = l->next) {
        SondGraphEdgeRef *ref = l->data;
        if (ref->edge_id == edge_id) {
            node->outgoing_edges = g_list_remove(node->outgoing_edges, ref);
            sond_graph_edge_ref_free(ref);
            return;
        }
    }
}

GList* sond_graph_node_get_outgoing_edges(SondGraphNode *node) {
    g_return_val_if_fail(SOND_GRAPH_IS_NODE(node), NULL);
    return node->outgoing_edges;
}

SondGraphEdgeRef* sond_graph_node_find_edge(SondGraphNode *node,
                                              gint64 edge_id) {
    g_return_val_if_fail(SOND_GRAPH_IS_NODE(node), NULL);

    for (GList *l = node->outgoing_edges; l != NULL; l = l->next) {
        SondGraphEdgeRef *ref = l->data;
        if (ref->edge_id == edge_id) {
            return ref;
        }
    }
    return NULL;
}

/* ========================================================================
 * JSON Serialisierung (Rekursiv - konsistent mit sond_graph_property!)
 * ======================================================================== */

static void property_to_json_builder(SondGraphNodeProperty *prop, JsonBuilder *builder) {
    json_builder_begin_array(builder);

    /* Element 0: key */
    json_builder_add_string_value(builder, prop->key);

    /* Element 1: values (IMMER Array) */
    json_builder_begin_array(builder);
    for (guint i = 0; i < prop->values->len; i++) {
        const gchar *val = g_ptr_array_index(prop->values, i);
        json_builder_add_string_value(builder, val);
    }
    json_builder_end_array(builder);

    /* Element 2 (optional): properties (Metadaten) */
    if (prop->properties) {
        json_builder_begin_array(builder);

        for (GList *l = prop->properties; l != NULL; l = l->next) {
            SondGraphNodeProperty *sub = l->data;
            property_to_json_builder(sub, builder);  /* REKURSION */
        }

        json_builder_end_array(builder);
    }

    json_builder_end_array(builder);
}

gchar* sond_graph_node_properties_to_json(SondGraphNode *node) {
    g_return_val_if_fail(SOND_GRAPH_IS_NODE(node), NULL);

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_array(builder);

    for (GList *l = node->properties; l != NULL; l = l->next) {
        SondGraphNodeProperty *prop = l->data;
        property_to_json_builder(prop, builder);
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

static SondGraphNodeProperty* property_from_json_array(JsonArray *array) {
    guint length = json_array_get_length(array);

    if (length < 2) {
        return NULL;
    }

    /* Element 0: key */
    JsonNode *key_node = json_array_get_element(array, 0);
    if (!JSON_NODE_HOLDS_VALUE(key_node)) {
        return NULL;
    }

    const gchar *key = json_node_get_string(key_node);
    if (!key) {
        return NULL;
    }

    /* Element 1: values (MUSS ein Array sein) */
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
    SondGraphNodeProperty *prop = property_new(key,
                                                (const gchar**)values->pdata,
                                                values->len);
    g_ptr_array_free(values, TRUE);

    /* Element 2 (optional): properties (Metadaten) */
    if (length >= 3) {
        JsonNode *props_node = json_array_get_element(array, 2);

        if (JSON_NODE_HOLDS_ARRAY(props_node)) {
            JsonArray *props_array = json_node_get_array(props_node);
            guint props_length = json_array_get_length(props_array);

            for (guint i = 0; i < props_length; i++) {
                JsonNode *element = json_array_get_element(props_array, i);

                if (JSON_NODE_HOLDS_ARRAY(element)) {
                    SondGraphNodeProperty *sub = property_from_json_array(
                        json_node_get_array(element));  /* REKURSION */

                    if (sub) {
                        prop->properties = g_list_append(prop->properties, sub);
                    }
                }
            }
        }
    }

    return prop;
}

gboolean sond_graph_node_properties_from_json(SondGraphNode *node,
                                                const gchar *json,
                                                GError **error) {
    g_return_val_if_fail(SOND_GRAPH_IS_NODE(node), FALSE);
    g_return_val_if_fail(json != NULL, FALSE);

    JsonParser *parser = json_parser_new();

    if (!json_parser_load_from_data(parser, json, -1, error)) {
        g_object_unref(parser);
        return FALSE;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_ARRAY(root)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Root node is not an array");
        g_object_unref(parser);
        return FALSE;
    }

    JsonArray *array = json_node_get_array(root);
    guint length = json_array_get_length(array);

    /* Properties clearen */
    if (node->properties) {
        g_list_free_full(node->properties, (GDestroyNotify)property_free);
        node->properties = NULL;
    }

    for (guint i = 0; i < length; i++) {
        JsonNode *element = json_array_get_element(array, i);

        if (!JSON_NODE_HOLDS_ARRAY(element)) {
            continue;
        }

        SondGraphNodeProperty *prop = property_from_json_array(
            json_node_get_array(element));

        if (prop) {
            node->properties = g_list_append(node->properties, prop);
        }
    }

    g_object_unref(parser);
    return TRUE;
}
