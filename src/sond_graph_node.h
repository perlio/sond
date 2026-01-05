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
 * @brief SondGraphNode - Graph-Node mit Properties
 */

#ifndef SOND_GRAPH_NODE_H
#define SOND_GRAPH_NODE_H

#include <glib-object.h>

G_BEGIN_DECLS

#define SOND_GRAPH_TYPE_NODE (sond_graph_node_get_type())
G_DECLARE_FINAL_TYPE(SondGraphNode, sond_graph_node, SOND_GRAPH, NODE, GObject)

/**
 * SondGraphEdgeRef:
 *
 * Leichtgewichtige Referenz auf eine ausgehende Edge.
 * Speichert nur ID, Label und Target-ID (nicht die kompletten Edge-Properties).
 */
typedef struct _SondGraphEdgeRef {
    gint64 edge_id;
    gchar *label;
    gint64 target_id;
} SondGraphEdgeRef;

/* EdgeRef Konstruktor/Destruktor */
SondGraphEdgeRef* sond_graph_edge_ref_new(gint64 edge_id, const gchar *label, gint64 target_id);
void sond_graph_edge_ref_free(SondGraphEdgeRef *ref);

/**
 * SondGraphNode:
 *
 * Repräsentiert einen Node im Graph mit Properties und ausgehenden Edges.
 */

/* Konstruktor/Destruktor */
SondGraphNode* sond_graph_node_new(void);

/* Basis-Properties */
gint64 sond_graph_node_get_id(SondGraphNode *node);
void sond_graph_node_set_id(SondGraphNode *node, gint64 id);

const gchar* sond_graph_node_get_label(SondGraphNode *node);
void sond_graph_node_set_label(SondGraphNode *node, const gchar *label);

/* Timestamps */
GDateTime* sond_graph_node_get_created_at(SondGraphNode *node);
void sond_graph_node_set_created_at(SondGraphNode *node, GDateTime *created_at);

GDateTime* sond_graph_node_get_updated_at(SondGraphNode *node);
void sond_graph_node_set_updated_at(SondGraphNode *node, GDateTime *updated_at);

/* Properties Management */
void sond_graph_node_set_property(SondGraphNode *node,
                                   const gchar *key,
                                   const gchar *value);

const gchar* sond_graph_node_get_property(SondGraphNode *node,
                                            const gchar *key);

gboolean sond_graph_node_has_property(SondGraphNode *node,
                                       const gchar *key);

void sond_graph_node_remove_property(SondGraphNode *node,
                                      const gchar *key);

GList* sond_graph_node_get_property_keys(SondGraphNode *node);

/* Outgoing Edges Management */
void sond_graph_node_add_outgoing_edge(SondGraphNode *node,
                                        SondGraphEdgeRef *edge_ref);

void sond_graph_node_remove_outgoing_edge(SondGraphNode *node,
                                           gint64 edge_id);

GList* sond_graph_node_get_outgoing_edges(SondGraphNode *node);

SondGraphEdgeRef* sond_graph_node_find_edge(SondGraphNode *node,
                                              gint64 edge_id);

/* JSON Serialisierung */
gchar* sond_graph_node_properties_to_json(SondGraphNode *node);
gboolean sond_graph_node_properties_from_json(SondGraphNode *node,
                                                const gchar *json,
                                                GError **error);

G_END_DECLS

#endif /* SOND_GRAPH_NODE_H */
