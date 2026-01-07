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
 * @brief Implementation der Graph-Edge Strukturen
 */

#include "sond_graph_edge.h"
#include "sond_graph_property.h"
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
    GPtrArray *properties;
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
    PROP_PROPERTIES,
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

static void sond_graph_edge_get_property(GObject *object,
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
        case PROP_PROPERTIES:
            g_value_set_pointer(value, self->properties);
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

static void sond_graph_edge_set_property(GObject *object,
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
        case PROP_PROPERTIES:
            if (self->properties) {
                g_ptr_array_unref(self->properties);
            }
            self->properties = g_value_get_pointer(value);
            if (self->properties) {
                g_ptr_array_ref(self->properties);
            }
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
    object_class->get_property = sond_graph_edge_get_property;
    object_class->set_property = sond_graph_edge_set_property;

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

    edge_properties[PROP_PROPERTIES] = g_param_spec_pointer(
        "properties",
        "Properties",
        "Edge properties",
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
 * Public API
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

GPtrArray* sond_graph_edge_get_properties(SondGraphEdge *edge) {
    g_return_val_if_fail(SOND_IS_GRAPH_EDGE(edge), NULL);
    return edge->properties;
}

void sond_graph_edge_set_properties(SondGraphEdge *edge, GPtrArray *properties) {
    g_return_if_fail(SOND_IS_GRAPH_EDGE(edge));

    if (edge->properties) {
        g_ptr_array_unref(edge->properties);
    }

    /* Deep copy der Properties */
    if (properties) {
        edge->properties = g_ptr_array_new_with_free_func(
            (GDestroyNotify)sond_graph_property_free);

        for (guint i = 0; i < properties->len; i++) {
            SondGraphProperty *prop = g_ptr_array_index(properties, i);
            const gchar *key = sond_graph_property_get_key(prop);
            GPtrArray *values = sond_graph_property_get_values(prop);

            if (values && values->len > 0) {
                /* Konvertiere zu const gchar** für new() */
                const gchar **values_array = g_new(const gchar*, values->len);
                for (guint j = 0; j < values->len; j++) {
                    values_array[j] = g_ptr_array_index(values, j);
                }

                SondGraphProperty *new_prop = sond_graph_property_new(
                    key, values_array, values->len);
                g_ptr_array_add(edge->properties, new_prop);

                g_free(values_array);

                /* Sub-Properties kopieren */
                GPtrArray *sub_props = sond_graph_property_get_properties(prop);
                if (sub_props && sub_props->len > 0) {
                    for (guint j = 0; j < sub_props->len; j++) {
                        SondGraphProperty *sub = g_ptr_array_index(sub_props, j);
                        /* Rekursion würde hier nötig sein für volle Deep Copy */
                        /* Vereinfacht: nur erste Ebene kopieren */
                        sond_graph_property_add_subproperty(new_prop, sub);
                    }
                }
            }
        }
    } else {
        edge->properties = g_ptr_array_new_with_free_func(
            (GDestroyNotify)sond_graph_property_free);
    }

    g_object_notify_by_pspec(G_OBJECT(edge), edge_properties[PROP_PROPERTIES]);
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
 * SondGraphEdgeRef - Leichtgewichtige Referenz
 * ======================================================================== */

SondGraphEdgeRef* sond_graph_edge_ref_new(gint64 id,
                                           const gchar *label,
                                           gint64 target_id) {
    SondGraphEdgeRef *ref = g_new0(SondGraphEdgeRef, 1);
    ref->id = id;
    ref->label = g_strdup(label);
    ref->target_id = target_id;
    return ref;
}

SondGraphEdgeRef* sond_graph_edge_ref_copy(const SondGraphEdgeRef *ref) {
    if (ref == NULL)
        return NULL;

    return sond_graph_edge_ref_new(ref->id, ref->label, ref->target_id);
}

void sond_graph_edge_ref_free(SondGraphEdgeRef *ref) {
    if (ref == NULL)
        return;

    g_free(ref->label);
    g_free(ref);
}

/*
 * Kompilierung:
 * gcc -c sond_graph_edge.c $(pkg-config --cflags glib-2.0 gobject-2.0)
 */
