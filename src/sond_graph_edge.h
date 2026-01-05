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
 * @brief SondGraphEdge - Graph-Edge mit Properties
 */

#ifndef SOND_GRAPH_EDGE_H
#define SOND_GRAPH_EDGE_H

#include <glib-object.h>

G_BEGIN_DECLS

#define SOND_GRAPH_TYPE_EDGE (sond_graph_edge_get_type())
G_DECLARE_FINAL_TYPE(SondGraphEdge, sond_graph_edge, SOND_GRAPH, EDGE, GObject)

/**
 * SondGraphEdge:
 *
 * Repräsentiert eine gerichtete Kante im Graph mit Properties.
 */

/* Konstruktor/Destruktor */
SondGraphEdge* sond_graph_edge_new(void);
SondGraphEdge* sond_graph_edge_new_full(gint64 source_id,
                                         gint64 target_id,
                                         const gchar *label);

/* Basis-Properties */
gint64 sond_graph_edge_get_id(SondGraphEdge *edge);
void sond_graph_edge_set_id(SondGraphEdge *edge, gint64 id);

gint64 sond_graph_edge_get_source_id(SondGraphEdge *edge);
void sond_graph_edge_set_source_id(SondGraphEdge *edge, gint64 source_id);

gint64 sond_graph_edge_get_target_id(SondGraphEdge *edge);
void sond_graph_edge_set_target_id(SondGraphEdge *edge, gint64 target_id);

const gchar* sond_graph_edge_get_label(SondGraphEdge *edge);
void sond_graph_edge_set_label(SondGraphEdge *edge, const gchar *label);

/* Timestamps */
GDateTime* sond_graph_edge_get_created_at(SondGraphEdge *edge);
void sond_graph_edge_set_created_at(SondGraphEdge *edge, GDateTime *created_at);

GDateTime* sond_graph_edge_get_updated_at(SondGraphEdge *edge);
void sond_graph_edge_set_updated_at(SondGraphEdge *edge, GDateTime *updated_at);

/* Properties Management */
void sond_graph_edge_set_property(SondGraphEdge *edge,
                                   const gchar *key,
                                   const gchar *value);

const gchar* sond_graph_edge_get_property(SondGraphEdge *edge,
                                            const gchar *key);

gboolean sond_graph_edge_has_property(SondGraphEdge *edge,
                                       const gchar *key);

void sond_graph_edge_remove_property(SondGraphEdge *edge,
                                      const gchar *key);

GList* sond_graph_edge_get_property_keys(SondGraphEdge *edge);

/* JSON Serialisierung */
gchar* sond_graph_edge_properties_to_json(SondGraphEdge *edge);
gboolean sond_graph_edge_properties_from_json(SondGraphEdge *edge,
                                                const gchar *json,
                                                GError **error);

G_END_DECLS

#endif /* SOND_GRAPH_EDGE_H */
