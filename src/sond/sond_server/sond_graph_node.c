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
 * @file sond_graph_node.c - FINALE VERSION
 * @brief Graph Node - nutzt SondGraphProperty direkt, speichert volle Edges
 */

#include "sond_graph_node.h"
#include "sond_graph_edge.h"
#include "sond_graph_property.h"
#include <json-glib/json-glib.h>
#include <string.h>

struct _SondGraphNode {
    GObject parent_instance;

    gint64 id;
    gchar *label;
    GPtrArray *properties;       /* GPtrArray von SondGraphProperty* */
    GPtrArray *outgoing_edges;   /* GPtrArray von SondGraphEdge* */
    GDateTime *created_at;
    GDateTime *updated_at;
};

G_DEFINE_TYPE(SondGraphNode, sond_graph_node, G_TYPE_OBJECT)

/* ========================================================================
 * SondGraphNode GObject Implementation
 * ======================================================================== */

static void sond_graph_node_finalize(GObject *object) {
    SondGraphNode *self = SOND_GRAPH_NODE(object);

    g_free(self->label);

    if (self->properties) {
        g_ptr_array_unref(self->properties);
    }

    if (self->outgoing_edges) {
        g_ptr_array_unref(self->outgoing_edges);
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
    self->properties = g_ptr_array_new_with_free_func(
        (GDestroyNotify)sond_graph_property_free);
    self->outgoing_edges = g_ptr_array_new_with_free_func(
        (GDestroyNotify)g_object_unref);
    self->created_at = NULL;
    self->updated_at = NULL;
}

SondGraphNode* sond_graph_node_new(void) {
    return g_object_new(SOND_TYPE_GRAPH_NODE, NULL);
}

/* ========================================================================
 * Getters/Setters
 * ======================================================================== */

gint64 sond_graph_node_get_id(SondGraphNode *node) {
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(node), 0);
    return node->id;
}

void sond_graph_node_set_id(SondGraphNode *node, gint64 id) {
    g_return_if_fail(SOND_IS_GRAPH_NODE(node));
    node->id = id;
}

const gchar* sond_graph_node_get_label(SondGraphNode *node) {
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(node), NULL);
    return node->label;
}

void sond_graph_node_set_label(SondGraphNode *node, const gchar *label) {
    g_return_if_fail(SOND_IS_GRAPH_NODE(node));
    g_free(node->label);
    node->label = g_strdup(label);
}

GDateTime* sond_graph_node_get_created_at(SondGraphNode *node) {
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(node), NULL);
    return node->created_at;
}

void sond_graph_node_set_created_at(SondGraphNode *node, GDateTime *created_at) {
    g_return_if_fail(SOND_IS_GRAPH_NODE(node));
    if (node->created_at) {
        g_date_time_unref(node->created_at);
    }
    node->created_at = created_at ? g_date_time_ref(created_at) : NULL;
}

GDateTime* sond_graph_node_get_updated_at(SondGraphNode *node) {
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(node), NULL);
    return node->updated_at;
}

void sond_graph_node_set_updated_at(SondGraphNode *node, GDateTime *updated_at) {
    g_return_if_fail(SOND_IS_GRAPH_NODE(node));
    if (node->updated_at) {
        g_date_time_unref(node->updated_at);
    }
    node->updated_at = updated_at ? g_date_time_ref(updated_at) : NULL;
}

/* ========================================================================
 * Properties Management - Delegiert an sond_graph_property_list_* API
 * ======================================================================== */

/**
 * sond_graph_node_set_property:
 *
 * Setzt eine Property mit einem Array von Values.
 */
void sond_graph_node_set_property(SondGraphNode *node,
                                   const gchar *key,
                                   const gchar **values,
                                   guint n_values) {
    g_return_if_fail(SOND_IS_GRAPH_NODE(node));
    g_return_if_fail(key != NULL);
    g_return_if_fail(values != NULL);
    g_return_if_fail(n_values > 0);

    sond_graph_property_list_set(node->properties, key, values, n_values);
}

/**
 * sond_graph_node_set_property_string:
 *
 * Convenience-Funktion für einzelne String-Werte.
 */
void sond_graph_node_set_property_string(SondGraphNode *node,
                                          const gchar *key,
                                          const gchar *value) {
    g_return_if_fail(SOND_IS_GRAPH_NODE(node));
    g_return_if_fail(key != NULL);
    g_return_if_fail(value != NULL);

    sond_graph_property_list_set_string(node->properties, key, value);
}

/**
 * sond_graph_node_set_properties:
 *
 * Setzt mehrere Properties auf einmal aus einem GPtrArray von SondGraphProperty.
 */
void sond_graph_node_set_properties(SondGraphNode *node, GPtrArray *props) {
    g_return_if_fail(SOND_IS_GRAPH_NODE(node));

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

        sond_graph_property_list_set(node->properties, key,
                                      (const gchar **)values->pdata,
                                      values->len);

        g_ptr_array_unref(values);

        /* TODO: Sub-Properties kopieren wenn nötig */
    }
}

/**
 * sond_graph_node_get_property:
 *
 * Liest eine Property als Array von Values.
 */
GPtrArray* sond_graph_node_get_property(SondGraphNode *node,
                                         const gchar *key) {
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(node), NULL);
    g_return_val_if_fail(key != NULL, NULL);

    return sond_graph_property_list_get(node->properties, key);
}

/**
 * sond_graph_node_get_property_string:
 *
 * Convenience-Funktion: Liest den ersten Wert einer Property.
 */
gchar* sond_graph_node_get_property_string(SondGraphNode *node,
                                            const gchar *key) {
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(node), NULL);
    g_return_val_if_fail(key != NULL, NULL);

    const gchar *value = sond_graph_property_list_get_string(node->properties, key);
    return value ? g_strdup(value) : NULL;
}

/**
 * sond_graph_node_get_property_count:
 *
 * Gibt die Anzahl der Values einer Property zurück.
 */
guint sond_graph_node_get_property_count(SondGraphNode *node,
                                          const gchar *key) {
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(node), 0);
    g_return_val_if_fail(key != NULL, 0);

    return sond_graph_property_list_get_count(node->properties, key);
}

/**
 * sond_graph_node_has_property:
 */
gboolean sond_graph_node_has_property(SondGraphNode *node,
                                       const gchar *key) {
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(node), FALSE);
    g_return_val_if_fail(key != NULL, FALSE);

    return sond_graph_property_list_has(node->properties, key);
}

/**
 * sond_graph_node_remove_property:
 */
void sond_graph_node_remove_property(SondGraphNode *node,
                                      const gchar *key) {
    g_return_if_fail(SOND_IS_GRAPH_NODE(node));
    g_return_if_fail(key != NULL);

    sond_graph_property_list_remove(node->properties, key);
}

/**
 * sond_graph_node_get_property_keys:
 */
GPtrArray* sond_graph_node_get_property_keys(SondGraphNode *node) {
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(node), NULL);

    return sond_graph_property_list_get_keys(node->properties);
}

/**
 * sond_graph_node_get_properties:
 *
 * Gibt das interne Property-Array zurück (nicht-kopiert).
 */
GPtrArray* sond_graph_node_get_properties(SondGraphNode *node) {
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(node), NULL);
    return node->properties;
}

/* ========================================================================
 * Nested Properties Management (Sub-Properties als Metadaten)
 * ======================================================================== */

/**
 * sond_graph_node_set_nested_property:
 *
 * Setzt eine Sub-Property (Metadaten) für eine existierende Property.
 */
void sond_graph_node_set_nested_property(SondGraphNode *node,
                                          const gchar *key,
                                          const gchar *nested_key,
                                          const gchar **values,
                                          guint n_values) {
    g_return_if_fail(SOND_IS_GRAPH_NODE(node));
    g_return_if_fail(key != NULL);
    g_return_if_fail(nested_key != NULL);
    g_return_if_fail(values != NULL);
    g_return_if_fail(n_values > 0);

    const gchar *path[] = {key, nested_key};
    sond_graph_property_list_set_at_path(node->properties, path, 2, values, n_values);
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
    g_return_if_fail(SOND_IS_GRAPH_NODE(node));
    g_return_if_fail(key != NULL);
    g_return_if_fail(nested_key != NULL);
    g_return_if_fail(value != NULL);

    const gchar *path[] = {key, nested_key};
    sond_graph_property_list_set_string_at_path(node->properties, path, 2, value);
}

/**
 * sond_graph_node_get_nested_property:
 *
 * Liest eine Sub-Property.
 */
GPtrArray* sond_graph_node_get_nested_property(SondGraphNode *node,
                                                const gchar *key,
                                                const gchar *nested_key) {
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(node), NULL);
    g_return_val_if_fail(key != NULL, NULL);
    g_return_val_if_fail(nested_key != NULL, NULL);

    const gchar *path[] = {key, nested_key};
    return sond_graph_property_list_get_at_path(node->properties, path, 2);
}

/**
 * sond_graph_node_get_nested_property_string:
 *
 * Convenience für einzelne Strings.
 */
gchar* sond_graph_node_get_nested_property_string(SondGraphNode *node,
                                                   const gchar *key,
                                                   const gchar *nested_key) {
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(node), NULL);
    g_return_val_if_fail(key != NULL, NULL);
    g_return_val_if_fail(nested_key != NULL, NULL);

    const gchar *path[] = {key, nested_key};
    return sond_graph_property_list_get_string_at_path(node->properties, path, 2);
}

/**
 * sond_graph_node_has_nested_property:
 */
gboolean sond_graph_node_has_nested_property(SondGraphNode *node,
                                              const gchar *key,
                                              const gchar *nested_key) {
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(node), FALSE);
    g_return_val_if_fail(key != NULL, FALSE);
    g_return_val_if_fail(nested_key != NULL, FALSE);

    const gchar *path[] = {key, nested_key};
    return sond_graph_property_list_has_at_path(node->properties, path, 2);
}

/* ========================================================================
 * Outgoing Edges Management - Speichert volle SondGraphEdge Objekte
 * ======================================================================== */

/**
 * sond_graph_node_add_outgoing_edge:
 *
 * Fügt eine ausgehende Edge hinzu.
 * Die Node übernimmt eine Referenz auf die Edge (ref count wird erhöht).
 */
void sond_graph_node_add_outgoing_edge(SondGraphNode *node,
                                        SondGraphEdge *edge) {
    g_return_if_fail(SOND_IS_GRAPH_NODE(node));
    g_return_if_fail(SOND_IS_GRAPH_EDGE(edge));

    g_object_ref(edge);
    g_ptr_array_add(node->outgoing_edges, edge);
}

/**
 * sond_graph_node_remove_outgoing_edge:
 *
 * Entfernt eine ausgehende Edge anhand ihrer ID.
 */
void sond_graph_node_remove_outgoing_edge(SondGraphNode *node,
                                           gint64 edge_id) {
    g_return_if_fail(SOND_IS_GRAPH_NODE(node));

    for (guint i = 0; i < node->outgoing_edges->len; i++) {
        SondGraphEdge *edge = g_ptr_array_index(node->outgoing_edges, i);
        if (sond_graph_edge_get_id(edge) == edge_id) {
            g_ptr_array_remove_index(node->outgoing_edges, i);
            return;
        }
    }
}

/**
 * sond_graph_node_get_outgoing_edges:
 *
 * Gibt das Array aller ausgehenden Edges zurück.
 */
GPtrArray* sond_graph_node_get_outgoing_edges(SondGraphNode *node) {
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(node), NULL);
    return node->outgoing_edges;
}

/**
 * sond_graph_node_find_edge:
 *
 * Findet eine Edge anhand ihrer ID.
 */
SondGraphEdge* sond_graph_node_find_edge(SondGraphNode *node,
                                          gint64 edge_id) {
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(node), NULL);

    for (guint i = 0; i < node->outgoing_edges->len; i++) {
        SondGraphEdge *edge = g_ptr_array_index(node->outgoing_edges, i);
        if (sond_graph_edge_get_id(edge) == edge_id) {
            return edge;
        }
    }
    return NULL;
}

/* ========================================================================
 * JSON Serialisierung - Delegiert an sond_graph_property API
 * ======================================================================== */

/**
 * sond_graph_node_to_json:
 *
 * Serialisiert die komplette Node zu JSON (ID, Label, Properties, Timestamps).
 * Edges werden NICHT serialisiert (zu groß/zirkulär).
 */
gchar* sond_graph_node_to_json(SondGraphNode *node) {
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(node), NULL);

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);

    /* ID */
    json_builder_set_member_name(builder, "id");
    json_builder_add_int_value(builder, node->id);

    /* Label */
    if (node->label) {
        json_builder_set_member_name(builder, "label");
        json_builder_add_string_value(builder, node->label);
    }

    /* Properties */
    if (node->properties && node->properties->len > 0) {
        json_builder_set_member_name(builder, "properties");
        gchar *props_json = sond_graph_property_list_to_json(node->properties);

        JsonParser *parser = json_parser_new();
        if (json_parser_load_from_data(parser, props_json, -1, NULL)) {
            JsonNode *props_node = json_parser_get_root(parser);
            json_builder_add_value(builder, json_node_copy(props_node));
        }
        g_object_unref(parser);
        g_free(props_json);
    }

    /* Timestamps */
    if (node->created_at) {
        json_builder_set_member_name(builder, "created_at");
        gchar *iso8601 = g_date_time_format_iso8601(node->created_at);
        json_builder_add_string_value(builder, iso8601);
        g_free(iso8601);
    }

    if (node->updated_at) {
        json_builder_set_member_name(builder, "updated_at");
        gchar *iso8601 = g_date_time_format_iso8601(node->updated_at);
        json_builder_add_string_value(builder, iso8601);
        g_free(iso8601);
    }

    json_builder_end_object(builder);

    JsonGenerator *generator = json_generator_new();
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(generator, root);

    gchar *json = json_generator_to_data(generator, NULL);

    json_node_free(root);
    g_object_unref(generator);
    g_object_unref(builder);

    return json;
}

/**
 * sond_graph_node_from_json:
 *
 * Erstellt eine Node aus JSON.
 */
SondGraphNode* sond_graph_node_from_json(const gchar *json, GError **error) {
    g_return_val_if_fail(json != NULL, NULL);

    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_data(parser, json, -1, error)) {
        g_object_unref(parser);
        return NULL;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(root)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Root node is not an object");
        g_object_unref(parser);
        return NULL;
    }

    JsonObject *obj = json_node_get_object(root);
    SondGraphNode *node = sond_graph_node_new();

    /* ID */
    if (json_object_has_member(obj, "id")) {
        node->id = json_object_get_int_member(obj, "id");
    }

    /* Label */
    if (json_object_has_member(obj, "label")) {
        const gchar *label = json_object_get_string_member(obj, "label");
        if (label) {
            node->label = g_strdup(label);
        }
    }

    /* Properties */
    if (json_object_has_member(obj, "properties")) {
        JsonNode *props_node = json_object_get_member(obj, "properties");
        JsonGenerator *gen = json_generator_new();
        json_generator_set_root(gen, props_node);
        gchar *props_json = json_generator_to_data(gen, NULL);

        GPtrArray *props = sond_graph_property_list_from_json(props_json, NULL);
        if (props) {
            if (node->properties) {
                g_ptr_array_unref(node->properties);
            }
            node->properties = props;
        }

        g_free(props_json);
        g_object_unref(gen);
    }

    /* Timestamps */
    if (json_object_has_member(obj, "created_at")) {
        const gchar *iso8601 = json_object_get_string_member(obj, "created_at");
        if (iso8601) {
            node->created_at = g_date_time_new_from_iso8601(iso8601, NULL);
        }
    }

    if (json_object_has_member(obj, "updated_at")) {
        const gchar *iso8601 = json_object_get_string_member(obj, "updated_at");
        if (iso8601) {
            node->updated_at = g_date_time_new_from_iso8601(iso8601, NULL);
        }
    }

    g_object_unref(parser);
    return node;
}
