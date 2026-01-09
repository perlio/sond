/*
 sond (sond_graph_edge.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2026  peloamerica

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
 * @brief Graph Edge - nutzt SondGraphProperty direkt
 */

#include "sond_graph_edge.h"
#include "sond_graph_property.h"
#include <json-glib/json-glib.h>
#include <string.h>

/* ========================================================================
 * SondGraphEdge - GObject Implementation
 * ======================================================================== */

struct _SondGraphEdge {
    GObject parent_instance;

    gint64 id;
    gint64 source_id;
    gint64 target_id;
    gchar *label;
    GPtrArray *properties;  /* GPtrArray von SondGraphProperty */
    GDateTime *created_at;
    GDateTime *updated_at;
};

G_DEFINE_TYPE(SondGraphEdge, sond_graph_edge, G_TYPE_OBJECT)

enum {
    PROP_0,
    PROP_ID,
    PROP_SOURCE_ID,
    PROP_TARGET_ID,
    PROP_LABEL,
    PROP_CREATED_AT,
    PROP_UPDATED_AT,
    N_PROPERTIES
};

static GParamSpec *edge_properties[N_PROPERTIES] = { NULL, };

static void sond_graph_edge_finalize(GObject *object) {
    SondGraphEdge *self = SOND_GRAPH_EDGE(object);

    g_free(self->label);

    if (self->properties) {
        g_ptr_array_unref(self->properties);
    }

    if (self->created_at) {
        g_date_time_unref(self->created_at);
    }

    if (self->updated_at) {
        g_date_time_unref(self->updated_at);
    }

    G_OBJECT_CLASS(sond_graph_edge_parent_class)->finalize(object);
}

static void sond_graph_edge_get_gobject_property(GObject *object,
                                          guint prop_id,
                                          GValue *value,
                                          GParamSpec *pspec) {
    SondGraphEdge *self = SOND_GRAPH_EDGE(object);

    switch (prop_id) {
        case PROP_ID:
            g_value_set_int64(value, self->id);
            break;
        case PROP_SOURCE_ID:
            g_value_set_int64(value, self->source_id);
            break;
        case PROP_TARGET_ID:
            g_value_set_int64(value, self->target_id);
            break;
        case PROP_LABEL:
            g_value_set_string(value, self->label);
            break;
        case PROP_CREATED_AT:
            g_value_set_boxed(value, self->created_at);
            break;
        case PROP_UPDATED_AT:
            g_value_set_boxed(value, self->updated_at);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void sond_graph_edge_set_gobject_property(GObject *object,
                                          guint prop_id,
                                          const GValue *value,
                                          GParamSpec *pspec) {
    SondGraphEdge *self = SOND_GRAPH_EDGE(object);

    switch (prop_id) {
        case PROP_ID:
            self->id = g_value_get_int64(value);
            break;
        case PROP_SOURCE_ID:
            self->source_id = g_value_get_int64(value);
            break;
        case PROP_TARGET_ID:
            self->target_id = g_value_get_int64(value);
            break;
        case PROP_LABEL:
            g_free(self->label);
            self->label = g_value_dup_string(value);
            break;
        case PROP_CREATED_AT:
            if (self->created_at) {
                g_date_time_unref(self->created_at);
            }
            self->created_at = g_value_dup_boxed(value);
            break;
        case PROP_UPDATED_AT:
            if (self->updated_at) {
                g_date_time_unref(self->updated_at);
            }
            self->updated_at = g_value_dup_boxed(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}

static void sond_graph_edge_class_init(SondGraphEdgeClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->finalize = sond_graph_edge_finalize;
    object_class->get_property = sond_graph_edge_get_gobject_property;
    object_class->set_property = sond_graph_edge_set_gobject_property;

    edge_properties[PROP_ID] = g_param_spec_int64(
        "id",
        "ID",
        "Edge ID",
        0, G_MAXINT64, 0,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    edge_properties[PROP_SOURCE_ID] = g_param_spec_int64(
        "source-id",
        "Source ID",
        "Source Node ID",
        0, G_MAXINT64, 0,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    edge_properties[PROP_TARGET_ID] = g_param_spec_int64(
        "target-id",
        "Target ID",
        "Target Node ID",
        0, G_MAXINT64, 0,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    edge_properties[PROP_LABEL] = g_param_spec_string(
        "label",
        "Label",
        "Edge label",
        NULL,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    edge_properties[PROP_CREATED_AT] = g_param_spec_boxed(
        "created-at",
        "Created At",
        "Creation timestamp",
        G_TYPE_DATE_TIME,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    edge_properties[PROP_UPDATED_AT] = g_param_spec_boxed(
        "updated-at",
        "Updated At",
        "Last update timestamp",
        G_TYPE_DATE_TIME,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties(object_class, N_PROPERTIES, edge_properties);
}

static void sond_graph_edge_init(SondGraphEdge *self) {
    self->id = 0;
    self->source_id = 0;
    self->target_id = 0;
    self->label = NULL;
    self->properties = g_ptr_array_new_with_free_func(
        (GDestroyNotify)sond_graph_property_free);
    self->created_at = NULL;
    self->updated_at = NULL;
}

/* ========================================================================
 * Constructor / Basic Getters/Setters
 * ======================================================================== */

SondGraphEdge* sond_graph_edge_new(void) {
    return g_object_new(SOND_TYPE_GRAPH_EDGE, NULL);
}

gint64 sond_graph_edge_get_id(SondGraphEdge *edge) {
    g_return_val_if_fail(SOND_IS_GRAPH_EDGE(edge), 0);
    return edge->id;
}

void sond_graph_edge_set_id(SondGraphEdge *edge, gint64 id) {
    g_return_if_fail(SOND_IS_GRAPH_EDGE(edge));

    if (edge->id != id) {
        edge->id = id;
        g_object_notify_by_pspec(G_OBJECT(edge), edge_properties[PROP_ID]);
    }
}

gint64 sond_graph_edge_get_source_id(SondGraphEdge *edge) {
    g_return_val_if_fail(SOND_IS_GRAPH_EDGE(edge), 0);
    return edge->source_id;
}

void sond_graph_edge_set_source_id(SondGraphEdge *edge, gint64 source_id) {
    g_return_if_fail(SOND_IS_GRAPH_EDGE(edge));

    if (edge->source_id != source_id) {
        edge->source_id = source_id;
        g_object_notify_by_pspec(G_OBJECT(edge), edge_properties[PROP_SOURCE_ID]);
    }
}

gint64 sond_graph_edge_get_target_id(SondGraphEdge *edge) {
    g_return_val_if_fail(SOND_IS_GRAPH_EDGE(edge), 0);
    return edge->target_id;
}

void sond_graph_edge_set_target_id(SondGraphEdge *edge, gint64 target_id) {
    g_return_if_fail(SOND_IS_GRAPH_EDGE(edge));

    if (edge->target_id != target_id) {
        edge->target_id = target_id;
        g_object_notify_by_pspec(G_OBJECT(edge), edge_properties[PROP_TARGET_ID]);
    }
}

const gchar* sond_graph_edge_get_label(SondGraphEdge *edge) {
    g_return_val_if_fail(SOND_IS_GRAPH_EDGE(edge), NULL);
    return edge->label;
}

void sond_graph_edge_set_label(SondGraphEdge *edge, const gchar *label) {
    g_return_if_fail(SOND_IS_GRAPH_EDGE(edge));

    if (g_strcmp0(edge->label, label) != 0) {
        g_free(edge->label);
        edge->label = g_strdup(label);
        g_object_notify_by_pspec(G_OBJECT(edge), edge_properties[PROP_LABEL]);
    }
}

GDateTime* sond_graph_edge_get_created_at(SondGraphEdge *edge) {
    g_return_val_if_fail(SOND_IS_GRAPH_EDGE(edge), NULL);
    return edge->created_at;
}

void sond_graph_edge_set_created_at(SondGraphEdge *edge, GDateTime *created_at) {
    g_return_if_fail(SOND_IS_GRAPH_EDGE(edge));

    if (edge->created_at) {
        g_date_time_unref(edge->created_at);
    }

    edge->created_at = created_at ? g_date_time_ref(created_at) : NULL;
    g_object_notify_by_pspec(G_OBJECT(edge), edge_properties[PROP_CREATED_AT]);
}

GDateTime* sond_graph_edge_get_updated_at(SondGraphEdge *edge) {
    g_return_val_if_fail(SOND_IS_GRAPH_EDGE(edge), NULL);
    return edge->updated_at;
}

void sond_graph_edge_set_updated_at(SondGraphEdge *edge, GDateTime *updated_at) {
    g_return_if_fail(SOND_IS_GRAPH_EDGE(edge));

    if (edge->updated_at) {
        g_date_time_unref(edge->updated_at);
    }

    edge->updated_at = updated_at ? g_date_time_ref(updated_at) : NULL;
    g_object_notify_by_pspec(G_OBJECT(edge), edge_properties[PROP_UPDATED_AT]);
}

/* ========================================================================
 * Properties Management - Konsistente API mit Node
 * ======================================================================== */

/**
 * sond_graph_edge_set_property:
 *
 * Setzt eine Property mit einem oder mehreren Werten.
 * Nutzt die sond_graph_property API direkt.
 */
void sond_graph_edge_set_property(SondGraphEdge *edge,
                                   const gchar *key,
                                   const gchar **values,
                                   guint n_values) {
    g_return_if_fail(SOND_IS_GRAPH_EDGE(edge));
    g_return_if_fail(key != NULL);
    g_return_if_fail(values != NULL);
    g_return_if_fail(n_values > 0);

    /* Nutze die Property-List API */
    sond_graph_property_list_set(edge->properties, key, values, n_values);
}

/**
 * sond_graph_edge_set_property_string:
 *
 * Convenience-Funktion für einzelne String-Werte.
 */
void sond_graph_edge_set_property_string(SondGraphEdge *edge,
                                          const gchar *key,
                                          const gchar *value) {
    g_return_if_fail(SOND_IS_GRAPH_EDGE(edge));
    g_return_if_fail(key != NULL);
    g_return_if_fail(value != NULL);

    sond_graph_property_list_set_string(edge->properties, key, value);
}

/**
 * sond_graph_edge_set_properties:
 *
 * Setzt mehrere Properties auf einmal aus einem Property-Array.
 */
void sond_graph_edge_set_properties(SondGraphEdge *edge, GPtrArray *properties) {
    g_return_if_fail(SOND_IS_GRAPH_EDGE(edge));

    if (!properties || properties->len == 0)
        return;

    /* Iteriere durch alle Properties und füge sie hinzu */
    for (guint i = 0; i < properties->len; i++) {
        SondGraphProperty *prop = g_ptr_array_index(properties, i);

        const gchar *key = sond_graph_property_get_key(prop);
        if (!key)
            continue;

        GPtrArray *values = sond_graph_property_get_values(prop);
        if (!values || values->len == 0) {
            if (values) g_ptr_array_unref(values);
            continue;
        }

        /* Setze Property */
        sond_graph_property_list_set(edge->properties, key,
                                      (const gchar **)values->pdata,
                                      values->len);

        g_ptr_array_unref(values);

        /* TODO: Sub-Properties kopieren wenn nötig */
        GPtrArray *sub_props = sond_graph_property_get_properties(prop);
        if (sub_props && sub_props->len > 0) {
            /* Sub-Properties kopieren - hier vereinfacht */
            /* Für vollständige Implementierung: Rekursiv kopieren */
        }
    }
}

/**
 * sond_graph_edge_get_property:
 *
 * Liest eine Property als Array von Strings.
 */
GPtrArray* sond_graph_edge_get_property(SondGraphEdge *edge,
                                         const gchar *key) {
    g_return_val_if_fail(SOND_IS_GRAPH_EDGE(edge), NULL);
    g_return_val_if_fail(key != NULL, NULL);

    return sond_graph_property_list_get(edge->properties, key);
}

/**
 * sond_graph_edge_get_property_string:
 *
 * Convenience-Funktion: Liest den ersten Wert einer Property.
 */
gchar* sond_graph_edge_get_property_string(SondGraphEdge *edge,
                                            const gchar *key) {
    g_return_val_if_fail(SOND_IS_GRAPH_EDGE(edge), NULL);
    g_return_val_if_fail(key != NULL, NULL);

    const gchar *value = sond_graph_property_list_get_string(edge->properties, key);
    return value ? g_strdup(value) : NULL;
}

/**
 * sond_graph_edge_get_property_count:
 *
 * Gibt die Anzahl der Values einer Property zurück.
 */
guint sond_graph_edge_get_property_count(SondGraphEdge *edge,
                                          const gchar *key) {
    g_return_val_if_fail(SOND_IS_GRAPH_EDGE(edge), 0);
    g_return_val_if_fail(key != NULL, 0);

    return sond_graph_property_list_get_count(edge->properties, key);
}

/**
 * sond_graph_edge_has_property:
 */
gboolean sond_graph_edge_has_property(SondGraphEdge *edge,
                                       const gchar *key) {
    g_return_val_if_fail(SOND_IS_GRAPH_EDGE(edge), FALSE);
    g_return_val_if_fail(key != NULL, FALSE);

    return sond_graph_property_list_has(edge->properties, key);
}

/**
 * sond_graph_edge_remove_property:
 */
void sond_graph_edge_remove_property(SondGraphEdge *edge,
                                      const gchar *key) {
    g_return_if_fail(SOND_IS_GRAPH_EDGE(edge));
    g_return_if_fail(key != NULL);

    sond_graph_property_list_remove(edge->properties, key);
}

/**
 * sond_graph_edge_get_property_keys:
 */
GPtrArray* sond_graph_edge_get_property_keys(SondGraphEdge *edge) {
    g_return_val_if_fail(SOND_IS_GRAPH_EDGE(edge), NULL);

    return sond_graph_property_list_get_keys(edge->properties);
}

/**
 * sond_graph_edge_get_all_properties:
 *
 * Gibt das interne Property-Array zurück (nicht-kopiert).
 */
GPtrArray* sond_graph_edge_get_properties(SondGraphEdge *edge) {
    g_return_val_if_fail(SOND_IS_GRAPH_EDGE(edge), NULL);
    return edge->properties;
}

/* ========================================================================
 * JSON Serialization
 * ======================================================================== */

/**
 * sond_graph_edge_to_json:
 *
 * Serialisiert die komplette Edge zu JSON.
 */
gchar* sond_graph_edge_to_json(SondGraphEdge *edge) {
    g_return_val_if_fail(SOND_IS_GRAPH_EDGE(edge), NULL);

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);

    /* ID */
    json_builder_set_member_name(builder, "id");
    json_builder_add_int_value(builder, edge->id);

    /* Source ID */
    json_builder_set_member_name(builder, "source_id");
    json_builder_add_int_value(builder, edge->source_id);

    /* Target ID */
    json_builder_set_member_name(builder, "target_id");
    json_builder_add_int_value(builder, edge->target_id);

    /* Label */
    if (edge->label) {
        json_builder_set_member_name(builder, "label");
        json_builder_add_string_value(builder, edge->label);
    }

    /* Properties */
    if (edge->properties && edge->properties->len > 0) {
        json_builder_set_member_name(builder, "properties");
        gchar *props_json = sond_graph_property_list_to_json(edge->properties);

        JsonParser *parser = json_parser_new();
        if (json_parser_load_from_data(parser, props_json, -1, NULL)) {
            JsonNode *props_node = json_parser_get_root(parser);
            json_builder_add_value(builder, json_node_copy(props_node));
        }
        g_object_unref(parser);
        g_free(props_json);
    }

    /* Timestamps */
    if (edge->created_at) {
        json_builder_set_member_name(builder, "created_at");
        gchar *iso8601 = g_date_time_format_iso8601(edge->created_at);
        json_builder_add_string_value(builder, iso8601);
        g_free(iso8601);
    }

    if (edge->updated_at) {
        json_builder_set_member_name(builder, "updated_at");
        gchar *iso8601 = g_date_time_format_iso8601(edge->updated_at);
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
 * sond_graph_edge_from_json:
 *
 * Erstellt eine Edge aus JSON.
 */
SondGraphEdge* sond_graph_edge_from_json(const gchar *json, GError **error) {
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
    SondGraphEdge *edge = sond_graph_edge_new();

    /* ID */
    if (json_object_has_member(obj, "id")) {
        edge->id = json_object_get_int_member(obj, "id");
    }

    /* Source ID */
    if (json_object_has_member(obj, "source_id")) {
        edge->source_id = json_object_get_int_member(obj, "source_id");
    }

    /* Target ID */
    if (json_object_has_member(obj, "target_id")) {
        edge->target_id = json_object_get_int_member(obj, "target_id");
    }

    /* Label */
    if (json_object_has_member(obj, "label")) {
        const gchar *label = json_object_get_string_member(obj, "label");
        if (label) {
            g_free(edge->label);
            edge->label = g_strdup(label);
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
            if (edge->properties) {
                g_ptr_array_unref(edge->properties);
            }
            edge->properties = props;
        }

        g_free(props_json);
        g_object_unref(gen);
    }

    /* Timestamps */
    if (json_object_has_member(obj, "created_at")) {
        const gchar *iso8601 = json_object_get_string_member(obj, "created_at");
        if (iso8601) {
            edge->created_at = g_date_time_new_from_iso8601(iso8601, NULL);
        }
    }

    if (json_object_has_member(obj, "updated_at")) {
        const gchar *iso8601 = json_object_get_string_member(obj, "updated_at");
        if (iso8601) {
            edge->updated_at = g_date_time_new_from_iso8601(iso8601, NULL);
        }
    }

    g_object_unref(parser);
    return edge;
}
