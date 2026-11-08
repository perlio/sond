/*
 sond (test_sond_graph_node.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2026  pelo america

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

/*
 * Zweiter Baustein der Test-Infrastruktur (GLib Testing). sond_graph_node.c
 * ist - wie sond_graph_property.c - reine, deterministische Logik (GObject
 * + json-glib), ohne DB/Netzwerk-Zugriff.
 */

#include <glib.h>
#include "../sond_graph/sond_graph_node.h"
#include "../sond_graph/sond_graph_edge.h"
#include "../sond_graph/sond_graph_property.h"

static void
test_new_defaults(void)
{
    SondGraphNode *node = sond_graph_node_new();

    g_assert_nonnull(node);
    g_assert_cmpint(sond_graph_node_get_id(node), ==, 0);
    g_assert_null(sond_graph_node_get_label(node));
    g_assert_null(sond_graph_node_get_created_at(node));
    g_assert_null(sond_graph_node_get_updated_at(node));

    GPtrArray *keys = sond_graph_node_get_property_keys(node);
    g_assert_nonnull(keys);
    g_assert_cmpuint(keys->len, ==, 0);
    g_ptr_array_unref(keys);

    GPtrArray *edges = sond_graph_node_get_outgoing_edges(node);
    g_assert_nonnull(edges);
    g_assert_cmpuint(edges->len, ==, 0);

    g_object_unref(node);
}

static void
test_id_and_label(void)
{
    SondGraphNode *node = sond_graph_node_new();

    sond_graph_node_set_id(node, 42);
    g_assert_cmpint(sond_graph_node_get_id(node), ==, 42);

    sond_graph_node_set_label(node, "Person");
    g_assert_cmpstr(sond_graph_node_get_label(node), ==, "Person");

    g_object_unref(node);
}

static void
test_timestamps(void)
{
    SondGraphNode *node = sond_graph_node_new();
    GDateTime *created = g_date_time_new_from_iso8601("2025-01-01T12:00:00Z", NULL);
    GDateTime *updated = g_date_time_new_from_iso8601("2025-06-15T08:30:00Z", NULL);

    g_assert_nonnull(created);
    g_assert_nonnull(updated);

    sond_graph_node_set_created_at(node, created);
    sond_graph_node_set_updated_at(node, updated);

    g_assert_true(g_date_time_equal(sond_graph_node_get_created_at(node), created));
    g_assert_true(g_date_time_equal(sond_graph_node_get_updated_at(node), updated));

    /* Setter nimmt eine eigene Referenz (g_date_time_ref) - unsere lokalen
     * Referenzen bleiben uns, muessen separat freigegeben werden. */
    g_date_time_unref(created);
    g_date_time_unref(updated);
    g_object_unref(node);
}

static void
test_property_single_value(void)
{
    SondGraphNode *node = sond_graph_node_new();

    g_assert_false(sond_graph_node_has_property(node, "name"));

    sond_graph_node_set_property_string(node, "name", "Alice");
    g_assert_true(sond_graph_node_has_property(node, "name"));
    g_assert_cmpuint(sond_graph_node_get_property_count(node, "name"), ==, 1);

    gchar *val = sond_graph_node_get_property_string(node, "name");
    g_assert_cmpstr(val, ==, "Alice");
    g_free(val);

    sond_graph_node_remove_property(node, "name");
    g_assert_false(sond_graph_node_has_property(node, "name"));

    g_object_unref(node);
}

static void
test_property_multi_value(void)
{
    SondGraphNode *node = sond_graph_node_new();
    const gchar *address[] = { "Berlin", "Hauptstr 1", "10115" };

    sond_graph_node_set_property(node, "address", address, 3);
    g_assert_cmpuint(sond_graph_node_get_property_count(node, "address"), ==, 3);

    GPtrArray *got = sond_graph_node_get_property(node, "address");
    g_assert_nonnull(got);
    g_assert_cmpuint(got->len, ==, 3);
    g_assert_cmpstr(g_ptr_array_index(got, 0), ==, "Berlin");
    g_assert_cmpstr(g_ptr_array_index(got, 2), ==, "10115");
    g_ptr_array_unref(got);

    g_object_unref(node);
}

static void
test_property_keys(void)
{
    SondGraphNode *node = sond_graph_node_new();

    sond_graph_node_set_property_string(node, "name", "Alice");
    sond_graph_node_set_property_string(node, "age", "30");

    GPtrArray *keys = sond_graph_node_get_property_keys(node);
    g_assert_cmpuint(keys->len, ==, 2);

    gboolean has_name = FALSE, has_age = FALSE;
    for (guint i = 0; i < keys->len; i++) {
        const gchar *k = g_ptr_array_index(keys, i);
        if (g_strcmp0(k, "name") == 0) has_name = TRUE;
        if (g_strcmp0(k, "age") == 0) has_age = TRUE;
    }
    g_assert_true(has_name);
    g_assert_true(has_age);

    g_ptr_array_unref(keys);
    g_object_unref(node);
}

static void
test_nested_property(void)
{
    SondGraphNode *node = sond_graph_node_new();

    sond_graph_node_set_property_string(node, "address", "Berlin");

    g_assert_false(sond_graph_node_has_nested_property(node, "address", "type"));
    sond_graph_node_set_nested_property_string(node, "address", "type", "Hauptwohnsitz");
    g_assert_true(sond_graph_node_has_nested_property(node, "address", "type"));

    gchar *val = sond_graph_node_get_nested_property_string(node, "address", "type");
    g_assert_cmpstr(val, ==, "Hauptwohnsitz");
    g_free(val);

    g_object_unref(node);
}

static void
test_set_properties_bulk(void)
{
    SondGraphNode *node = sond_graph_node_new();
    GPtrArray *props = g_ptr_array_new_with_free_func(
            (GDestroyNotify) sond_graph_property_free);

    g_ptr_array_add(props, sond_graph_property_new_string("name", "Bob"));
    g_ptr_array_add(props, sond_graph_property_new_string("age", "25"));

    sond_graph_node_set_properties(node, props);

    gchar *name = sond_graph_node_get_property_string(node, "name");
    g_assert_cmpstr(name, ==, "Bob");
    g_free(name);

    gchar *age = sond_graph_node_get_property_string(node, "age");
    g_assert_cmpstr(age, ==, "25");
    g_free(age);

    /* props gehoert weiterhin uns (set_properties kopiert die Werte) -
     * muss selbst freigegeben werden. */
    g_ptr_array_unref(props);
    g_object_unref(node);
}

static void
test_set_properties_bulk_ignores_subproperties(void)
{
    /* Dokumentiert den bekannten TODO in sond_graph_node_set_properties()
     * ("Sub-Properties kopieren wenn noetig"): Sub-Properties werden beim
     * Bulk-Setzen aktuell NICHT uebernommen. Wird das irgendwann behoben,
     * schlaegt dieser Test an und muss angepasst werden. */
    SondGraphNode *node = sond_graph_node_new();
    GPtrArray *props = g_ptr_array_new_with_free_func(
            (GDestroyNotify) sond_graph_property_free);

    SondGraphProperty *address = sond_graph_property_new_string("address", "Berlin");
    sond_graph_property_add_subproperty(address,
            sond_graph_property_new_string("type", "Hauptwohnsitz"));
    g_ptr_array_add(props, address);

    sond_graph_node_set_properties(node, props);

    g_assert_false(sond_graph_node_has_nested_property(node, "address", "type"));

    g_ptr_array_unref(props);
    g_object_unref(node);
}

static void
test_edges(void)
{
    SondGraphNode *node = sond_graph_node_new();
    SondGraphEdge *edge1 = sond_graph_edge_new();
    SondGraphEdge *edge2 = sond_graph_edge_new();

    sond_graph_edge_set_id(edge1, 1);
    sond_graph_edge_set_id(edge2, 2);

    sond_graph_node_add_outgoing_edge(node, edge1);
    sond_graph_node_add_outgoing_edge(node, edge2);

    GPtrArray *edges = sond_graph_node_get_outgoing_edges(node);
    g_assert_cmpuint(edges->len, ==, 2);

    g_assert_true(sond_graph_node_find_edge(node, 1) == edge1);
    g_assert_true(sond_graph_node_find_edge(node, 2) == edge2);
    g_assert_null(sond_graph_node_find_edge(node, 999));

    sond_graph_node_remove_outgoing_edge(node, 1);
    g_assert_cmpuint(sond_graph_node_get_outgoing_edges(node)->len, ==, 1);
    g_assert_null(sond_graph_node_find_edge(node, 1));
    g_assert_true(sond_graph_node_find_edge(node, 2) == edge2);

    /* add_outgoing_edge nimmt eine eigene Referenz (g_object_ref) - unsere
     * lokalen Referenzen muessen wir selbst wieder loslassen. */
    g_object_unref(edge1);
    g_object_unref(edge2);
    g_object_unref(node);
}

static void
test_json_roundtrip(void)
{
    SondGraphNode *node = sond_graph_node_new();
    GDateTime *created = g_date_time_new_from_iso8601("2025-01-01T12:00:00Z", NULL);

    sond_graph_node_set_id(node, 7);
    sond_graph_node_set_label(node, "Person");
    sond_graph_node_set_property_string(node, "name", "Alice");
    sond_graph_node_set_created_at(node, created);
    g_date_time_unref(created);

    /* Edges werden laut Doku absichtlich NICHT serialisiert. */
    SondGraphEdge *edge = sond_graph_edge_new();
    sond_graph_edge_set_id(edge, 1);
    sond_graph_node_add_outgoing_edge(node, edge);
    g_object_unref(edge);

    gchar *json = sond_graph_node_to_json(node);
    g_assert_nonnull(json);

    GError *error = NULL;
    SondGraphNode *parsed = sond_graph_node_from_json(json, &error);
    g_assert_no_error(error);
    g_assert_nonnull(parsed);

    g_assert_cmpint(sond_graph_node_get_id(parsed), ==, 7);
    g_assert_cmpstr(sond_graph_node_get_label(parsed), ==, "Person");

    gchar *name = sond_graph_node_get_property_string(parsed, "name");
    g_assert_cmpstr(name, ==, "Alice");
    g_free(name);

    g_assert_nonnull(sond_graph_node_get_created_at(parsed));

    /* Edges absichtlich nicht im JSON enthalten. */
    g_assert_cmpuint(sond_graph_node_get_outgoing_edges(parsed)->len, ==, 0);

    g_free(json);
    g_object_unref(parsed);
    g_object_unref(node);
}

static void
test_json_from_invalid_json_fails(void)
{
    GError *error = NULL;
    SondGraphNode *parsed = sond_graph_node_from_json("{not valid json", &error);

    g_assert_null(parsed);
    g_assert_nonnull(error);
    g_clear_error(&error);
}

/* Kein eigenes main() - siehe test_main.c. */
void
test_sond_graph_node_register(void)
{
    g_test_add_func("/sond_graph_node/new_defaults", test_new_defaults);
    g_test_add_func("/sond_graph_node/id_and_label", test_id_and_label);
    g_test_add_func("/sond_graph_node/timestamps", test_timestamps);
    g_test_add_func("/sond_graph_node/property_single_value", test_property_single_value);
    g_test_add_func("/sond_graph_node/property_multi_value", test_property_multi_value);
    g_test_add_func("/sond_graph_node/property_keys", test_property_keys);
    g_test_add_func("/sond_graph_node/nested_property", test_nested_property);
    g_test_add_func("/sond_graph_node/set_properties_bulk", test_set_properties_bulk);
    g_test_add_func("/sond_graph_node/set_properties_bulk_ignores_subproperties", test_set_properties_bulk_ignores_subproperties);
    g_test_add_func("/sond_graph_node/edges", test_edges);
    g_test_add_func("/sond_graph_node/json_roundtrip", test_json_roundtrip);
    g_test_add_func("/sond_graph_node/json_from_invalid_json_fails", test_json_from_invalid_json_fails);
}
