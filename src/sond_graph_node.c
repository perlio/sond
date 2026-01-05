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
 * @file sond_graph_node.c
 * @brief Implementation des SondGraphNode
 */

#include "sond_graph_node.h"
#include <json-glib/json-glib.h>
#include <string.h>

struct _SondGraphNode {
    GObject parent_instance;

    gint64 id;
    gchar *label;
    GHashTable *properties;      /* key -> value (beide gchar*) */
    GList *outgoing_edges;       /* Liste von SondGraphEdgeRef* */
    GDateTime *created_at;
    GDateTime *updated_at;
};

G_DEFINE_TYPE(SondGraphNode, sond_graph_node, G_TYPE_OBJECT)

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
        g_hash_table_unref(self->properties);
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
    self->properties = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    self->outgoing_edges = NULL;
    self->created_at = NULL;
    self->updated_at = NULL;
}

/**
 * sond_graph_node_new:
 */
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
 * Properties Management
 * ======================================================================== */

void sond_graph_node_set_property(SondGraphNode *node,
                                   const gchar *key,
                                   const gchar *value) {
    g_return_if_fail(SOND_GRAPH_IS_NODE(node));
    g_return_if_fail(key != NULL);
    g_return_if_fail(value != NULL);

    g_hash_table_insert(node->properties, g_strdup(key), g_strdup(value));
}

const gchar* sond_graph_node_get_property(SondGraphNode *node,
                                            const gchar *key) {
    g_return_val_if_fail(SOND_GRAPH_IS_NODE(node), NULL);
    g_return_val_if_fail(key != NULL, NULL);

    return g_hash_table_lookup(node->properties, key);
}

gboolean sond_graph_node_has_property(SondGraphNode *node,
                                       const gchar *key) {
    g_return_val_if_fail(SOND_GRAPH_IS_NODE(node), FALSE);
    g_return_val_if_fail(key != NULL, FALSE);

    return g_hash_table_contains(node->properties, key);
}

void sond_graph_node_remove_property(SondGraphNode *node,
                                      const gchar *key) {
    g_return_if_fail(SOND_GRAPH_IS_NODE(node));
    g_return_if_fail(key != NULL);

    g_hash_table_remove(node->properties, key);
}

GList* sond_graph_node_get_property_keys(SondGraphNode *node) {
    g_return_val_if_fail(SOND_GRAPH_IS_NODE(node), NULL);

    return g_hash_table_get_keys(node->properties);
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
 * JSON Serialisierung
 * ======================================================================== */

/**
 * sond_graph_node_properties_to_json:
 *
 * Konvertiert Properties zu JSON-Array-Format:
 * [{"key": "name", "value": "Alice"}, {"key": "age", "value": "30"}]
 */
gchar* sond_graph_node_properties_to_json(SondGraphNode *node) {
    g_return_val_if_fail(SOND_GRAPH_IS_NODE(node), NULL);

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_array(builder);

    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, node->properties);

    while (g_hash_table_iter_next(&iter, &key, &value)) {
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "key");
        json_builder_add_string_value(builder, (const gchar*)key);
        json_builder_set_member_name(builder, "value");
        json_builder_add_string_value(builder, (const gchar*)value);
        json_builder_end_object(builder);
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

/**
 * sond_graph_node_properties_from_json:
 *
 * Lädt Properties aus JSON-Array-Format.
 */
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
    g_hash_table_remove_all(node->properties);

    for (guint i = 0; i < length; i++) {
        JsonNode *element = json_array_get_element(array, i);

        if (!JSON_NODE_HOLDS_OBJECT(element)) {
            continue;
        }

        JsonObject *obj = json_node_get_object(element);

        if (!json_object_has_member(obj, "key") ||
            !json_object_has_member(obj, "value")) {
            continue;
        }

        const gchar *key = json_object_get_string_member(obj, "key");
        const gchar *value = json_object_get_string_member(obj, "value");

        if (key && value) {
            g_hash_table_insert(node->properties,
                              g_strdup(key),
                              g_strdup(value));
        }
    }

    g_object_unref(parser);
    return TRUE;
}

/*
 * Kompilierung:
 * gcc -c sond_graph_node.c $(pkg-config --cflags glib-2.0 gobject-2.0 json-glib-1.0)
 */
