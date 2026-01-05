/*
 sond (sond_graph_edge.c) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_graph_edge.c
 * @brief Implementation des SondGraphEdge
 */

#include "sond_graph_edge.h"
#include <json-glib/json-glib.h>
#include <string.h>

struct _SondGraphEdge {
    GObject parent_instance;

    gint64 id;
    gint64 source_id;
    gint64 target_id;
    gchar *label;
    GHashTable *properties;  /* key -> value (beide gchar*) */
    GDateTime *created_at;
    GDateTime *updated_at;
};

G_DEFINE_TYPE(SondGraphEdge, sond_graph_edge, G_TYPE_OBJECT)

static void sond_graph_edge_finalize(GObject *object) {
    SondGraphEdge *self = SOND_GRAPH_EDGE(object);

    g_free(self->label);
    if (self->properties) {
        g_hash_table_unref(self->properties);
    }
    if (self->created_at) {
        g_date_time_unref(self->created_at);
    }
    if (self->updated_at) {
        g_date_time_unref(self->updated_at);
    }

    G_OBJECT_CLASS(sond_graph_edge_parent_class)->finalize(object);
}

static void sond_graph_edge_class_init(SondGraphEdgeClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = sond_graph_edge_finalize;
}

static void sond_graph_edge_init(SondGraphEdge *self) {
    self->id = 0;
    self->source_id = 0;
    self->target_id = 0;
    self->label = NULL;
    self->properties = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    self->created_at = NULL;
    self->updated_at = NULL;
}

/**
 * sond_graph_edge_new:
 */
SondGraphEdge* sond_graph_edge_new(void) {
    return g_object_new(SOND_GRAPH_TYPE_EDGE, NULL);
}

/**
 * sond_graph_edge_new_full:
 */
SondGraphEdge* sond_graph_edge_new_full(gint64 source_id,
                                         gint64 target_id,
                                         const gchar *label) {
    SondGraphEdge *edge = sond_graph_edge_new();
    edge->source_id = source_id;
    edge->target_id = target_id;
    edge->label = g_strdup(label);
    return edge;
}

/* ========================================================================
 * Getters/Setters
 * ======================================================================== */

gint64 sond_graph_edge_get_id(SondGraphEdge *edge) {
    g_return_val_if_fail(SOND_GRAPH_IS_EDGE(edge), 0);
    return edge->id;
}

void sond_graph_edge_set_id(SondGraphEdge *edge, gint64 id) {
    g_return_if_fail(SOND_GRAPH_IS_EDGE(edge));
    edge->id = id;
}

gint64 sond_graph_edge_get_source_id(SondGraphEdge *edge) {
    g_return_val_if_fail(SOND_GRAPH_IS_EDGE(edge), 0);
    return edge->source_id;
}

void sond_graph_edge_set_source_id(SondGraphEdge *edge, gint64 source_id) {
    g_return_if_fail(SOND_GRAPH_IS_EDGE(edge));
    edge->source_id = source_id;
}

gint64 sond_graph_edge_get_target_id(SondGraphEdge *edge) {
    g_return_val_if_fail(SOND_GRAPH_IS_EDGE(edge), 0);
    return edge->target_id;
}

void sond_graph_edge_set_target_id(SondGraphEdge *edge, gint64 target_id) {
    g_return_if_fail(SOND_GRAPH_IS_EDGE(edge));
    edge->target_id = target_id;
}

const gchar* sond_graph_edge_get_label(SondGraphEdge *edge) {
    g_return_val_if_fail(SOND_GRAPH_IS_EDGE(edge), NULL);
    return edge->label;
}

void sond_graph_edge_set_label(SondGraphEdge *edge, const gchar *label) {
    g_return_if_fail(SOND_GRAPH_IS_EDGE(edge));
    g_free(edge->label);
    edge->label = g_strdup(label);
}

GDateTime* sond_graph_edge_get_created_at(SondGraphEdge *edge) {
    g_return_val_if_fail(SOND_GRAPH_IS_EDGE(edge), NULL);
    return edge->created_at;
}

void sond_graph_edge_set_created_at(SondGraphEdge *edge, GDateTime *created_at) {
    g_return_if_fail(SOND_GRAPH_IS_EDGE(edge));
    if (edge->created_at) {
        g_date_time_unref(edge->created_at);
    }
    edge->created_at = created_at ? g_date_time_ref(created_at) : NULL;
}

GDateTime* sond_graph_edge_get_updated_at(SondGraphEdge *edge) {
    g_return_val_if_fail(SOND_GRAPH_IS_EDGE(edge), NULL);
    return edge->updated_at;
}

void sond_graph_edge_set_updated_at(SondGraphEdge *edge, GDateTime *updated_at) {
    g_return_if_fail(SOND_GRAPH_IS_EDGE(edge));
    if (edge->updated_at) {
        g_date_time_unref(edge->updated_at);
    }
    edge->updated_at = updated_at ? g_date_time_ref(updated_at) : NULL;
}

/* ========================================================================
 * Properties Management
 * ======================================================================== */

void sond_graph_edge_set_property(SondGraphEdge *edge,
                                   const gchar *key,
                                   const gchar *value) {
    g_return_if_fail(SOND_GRAPH_IS_EDGE(edge));
    g_return_if_fail(key != NULL);
    g_return_if_fail(value != NULL);

    g_hash_table_insert(edge->properties, g_strdup(key), g_strdup(value));
}

const gchar* sond_graph_edge_get_property(SondGraphEdge *edge,
                                            const gchar *key) {
    g_return_val_if_fail(SOND_GRAPH_IS_EDGE(edge), NULL);
    g_return_val_if_fail(key != NULL, NULL);

    return g_hash_table_lookup(edge->properties, key);
}

gboolean sond_graph_edge_has_property(SondGraphEdge *edge,
                                       const gchar *key) {
    g_return_val_if_fail(SOND_GRAPH_IS_EDGE(edge), FALSE);
    g_return_val_if_fail(key != NULL, FALSE);

    return g_hash_table_contains(edge->properties, key);
}

void sond_graph_edge_remove_property(SondGraphEdge *edge,
                                      const gchar *key) {
    g_return_if_fail(SOND_GRAPH_IS_EDGE(edge));
    g_return_if_fail(key != NULL);

    g_hash_table_remove(edge->properties, key);
}

GList* sond_graph_edge_get_property_keys(SondGraphEdge *edge) {
    g_return_val_if_fail(SOND_GRAPH_IS_EDGE(edge), NULL);

    return g_hash_table_get_keys(edge->properties);
}

/* ========================================================================
 * JSON Serialisierung (analog zu SondGraphNode)
 * ======================================================================== */

gchar* sond_graph_edge_properties_to_json(SondGraphEdge *edge) {
    g_return_val_if_fail(SOND_GRAPH_IS_EDGE(edge), NULL);

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_array(builder);

    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, edge->properties);

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

gboolean sond_graph_edge_properties_from_json(SondGraphEdge *edge,
                                                const gchar *json,
                                                GError **error) {
    g_return_val_if_fail(SOND_GRAPH_IS_EDGE(edge), FALSE);
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
    g_hash_table_remove_all(edge->properties);

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
            g_hash_table_insert(edge->properties,
                              g_strdup(key),
                              g_strdup(value));
        }
    }

    g_object_unref(parser);
    return TRUE;
}

/*
 * Kompilierung:
 * gcc -c sond_graph_edge.c $(pkg-config --cflags glib-2.0 gobject-2.0 json-glib-1.0)
 */
