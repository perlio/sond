/*
 sond (test_sond_graph_db_filter.c) - Akten, Beweisstücke, Unterlagen
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
 * Dritter Baustein der Test-Infrastruktur (GLib Testing). Testet die
 * DB-FREIEN Teile von sond_graph_db.c: Property-/Edge-Filter-Konstruktion,
 * die Wildcard-Erkennung/-Escaping (vormals static, fuer diesen Test
 * exportiert - s. sond_graph_db.h) und die JSON-Hin-/Rueckserialisierung
 * von SondGraphNodeSearchCriteria (inkl. rekursiver Edge-Filter).
 *
 * Bewusst NICHT getestet: alles, was eine echte MYSQL-Verbindung braucht
 * (sond_graph_db_load_node, build_property_filters_sql, ...) - das ist
 * Gegenstand der spaeter geplanten Docker/MariaDB-Integrationstests.
 */

#include <glib.h>
#include "../sond_graph/sond_graph_db.h"

static void
test_has_wildcards(void)
{
    g_assert_true(sond_graph_db_has_wildcards("a*b"));
    g_assert_true(sond_graph_db_has_wildcards("a?b"));
    g_assert_true(sond_graph_db_has_wildcards("*"));
    g_assert_true(sond_graph_db_has_wildcards("?"));
    g_assert_false(sond_graph_db_has_wildcards("abc"));
    g_assert_false(sond_graph_db_has_wildcards(""));
}

static void
test_escape_sql_like_pattern(void)
{
    gchar *r;

    r = sond_graph_db_escape_sql_like_pattern("foo");
    g_assert_cmpstr(r, ==, "foo");
    g_free(r);

    r = sond_graph_db_escape_sql_like_pattern("a*b");
    g_assert_cmpstr(r, ==, "a%b");
    g_free(r);

    r = sond_graph_db_escape_sql_like_pattern("a?b");
    g_assert_cmpstr(r, ==, "a_b");
    g_free(r);

    /* Literale %/_ im Originalwert muessen escaped werden, BEVOR */
    /* */
    r = sond_graph_db_escape_sql_like_pattern("100%");
    g_assert_cmpstr(r, ==, "100\\%");
    g_free(r);

    r = sond_graph_db_escape_sql_like_pattern("a_b");
    g_assert_cmpstr(r, ==, "a\\_b");
    g_free(r);

    /* Kombination: literales % bleibt escaped, * wird zu echtem SQL-%. */
    r = sond_graph_db_escape_sql_like_pattern("50%*done");
    g_assert_cmpstr(r, ==, "50\\%%done");
    g_free(r);
}

static void
test_property_filter_constructors(void)
{
    SondPropertyFilter *f1 = sond_property_filter_new("key1", "val1");
    g_assert_cmpstr(f1->key, ==, "key1");
    g_assert_cmpstr(f1->value, ==, "val1");
    g_assert_cmpint(f1->array_index, ==, -1);
    g_assert_null(f1->path);
    sond_property_filter_free(f1);

    SondPropertyFilter *f2 = sond_property_filter_new_with_path("key2", "val2", "sub.path");
    g_assert_cmpstr(f2->path, ==, "sub.path");
    g_assert_cmpint(f2->array_index, ==, -1);
    sond_property_filter_free(f2);

    SondPropertyFilter *f3 = sond_property_filter_new_with_index("key3", "val3", 1);
    g_assert_cmpint(f3->array_index, ==, 1);
    g_assert_null(f3->path);
    sond_property_filter_free(f3);
}

static void
test_search_criteria_basic(void)
{
    SondGraphNodeSearchCriteria *criteria = sond_graph_node_search_criteria_new();

    g_assert_nonnull(criteria);
    g_assert_null(criteria->label);
    g_assert_cmpuint(criteria->property_filters->len, ==, 0);
    g_assert_cmpuint(criteria->edge_filters->len, ==, 0);

    criteria->label = g_strdup("Person");
    sond_graph_node_search_criteria_add_property_filter(criteria, "name", "Alice");
    sond_graph_node_search_criteria_add_nested_property_filter(criteria, "address", "Berlin*", "meta.city");
    sond_graph_node_search_criteria_add_simple_edge_filter(criteria, "knows");

    g_assert_cmpuint(criteria->property_filters->len, ==, 2);
    g_assert_cmpuint(criteria->edge_filters->len, ==, 1);

    sond_graph_node_search_criteria_free(criteria);
}

static void
test_json_roundtrip_simple(void)
{
    SondGraphNodeSearchCriteria *criteria = sond_graph_node_search_criteria_new();
    criteria->label = g_strdup("Person");
    criteria->limit = 10;
    criteria->offset = 5;
    sond_graph_node_search_criteria_add_property_filter(criteria, "name", "Alice");

    gchar *json = sond_graph_node_search_criteria_to_json(criteria);
    g_assert_nonnull(json);

    GError *error = NULL;
    SondGraphNodeSearchCriteria *parsed = sond_graph_node_search_criteria_from_json(json, &error);
    g_assert_no_error(error);
    g_assert_nonnull(parsed);

    g_assert_cmpstr(parsed->label, ==, "Person");
    g_assert_cmpuint(parsed->limit, ==, 10);
    g_assert_cmpuint(parsed->offset, ==, 5);
    g_assert_cmpuint(parsed->property_filters->len, ==, 1);

    SondPropertyFilter *pf = g_ptr_array_index(parsed->property_filters, 0);
    g_assert_cmpstr(pf->key, ==, "name");
    g_assert_cmpstr(pf->value, ==, "Alice");
    g_assert_cmpint(pf->array_index, ==, -1);
    g_assert_null(pf->path);

    g_free(json);
    sond_graph_node_search_criteria_free(parsed);
    sond_graph_node_search_criteria_free(criteria);
}

static void
test_json_roundtrip_nested_path_filter(void)
{
    SondGraphNodeSearchCriteria *criteria = sond_graph_node_search_criteria_new();
    sond_graph_node_search_criteria_add_nested_property_filter(criteria, "address", "Berlin", "meta.city");

    gchar *json = sond_graph_node_search_criteria_to_json(criteria);
    SondGraphNodeSearchCriteria *parsed = sond_graph_node_search_criteria_from_json(json, NULL);
    g_assert_nonnull(parsed);

    g_assert_cmpuint(parsed->property_filters->len, ==, 1);
    SondPropertyFilter *pf = g_ptr_array_index(parsed->property_filters, 0);
    g_assert_cmpstr(pf->key, ==, "address");
    g_assert_cmpstr(pf->value, ==, "Berlin");
    g_assert_cmpstr(pf->path, ==, "meta.city");

    g_free(json);
    sond_graph_node_search_criteria_free(parsed);
    sond_graph_node_search_criteria_free(criteria);
}

static void
test_json_roundtrip_indexed_filter(void)
{
    SondGraphNodeSearchCriteria *criteria = sond_graph_node_search_criteria_new();
    SondPropertyFilter *indexed = sond_property_filter_new_with_index("regnr", "2026", 0);
    g_ptr_array_add(criteria->property_filters, indexed);

    gchar *json = sond_graph_node_search_criteria_to_json(criteria);
    SondGraphNodeSearchCriteria *parsed = sond_graph_node_search_criteria_from_json(json, NULL);
    g_assert_nonnull(parsed);

    g_assert_cmpuint(parsed->property_filters->len, ==, 1);
    SondPropertyFilter *pf = g_ptr_array_index(parsed->property_filters, 0);
    g_assert_cmpstr(pf->key, ==, "regnr");
    g_assert_cmpint(pf->array_index, ==, 0);
    g_assert_null(pf->path);

    g_free(json);
    sond_graph_node_search_criteria_free(parsed);
    sond_graph_node_search_criteria_free(criteria);
}

static void
test_json_roundtrip_recursive_edge_filter(void)
{
    SondGraphNodeSearchCriteria *target = sond_graph_node_search_criteria_new();
    target->label = g_strdup("Company");
    sond_graph_node_search_criteria_add_property_filter(target, "name", "Acme*");

    SondGraphNodeSearchCriteria *criteria = sond_graph_node_search_criteria_new();
    criteria->label = g_strdup("Person");
    /* Ownership von target geht an criteria ueber (transfer full). */
    sond_graph_node_search_criteria_add_edge_filter(criteria, "works_for", target);

    /* Property-Filter auf der Edge selbst nachtraeglich ergaenzen. */
    SondEdgeFilter *edge_filter = g_ptr_array_index(criteria->edge_filters, 0);
    sond_edge_filter_add_property(edge_filter, "since", "2020");

    gchar *json = sond_graph_node_search_criteria_to_json(criteria);
    g_assert_nonnull(json);

    GError *error = NULL;
    SondGraphNodeSearchCriteria *parsed = sond_graph_node_search_criteria_from_json(json, &error);
    g_assert_no_error(error);
    g_assert_nonnull(parsed);

    g_assert_cmpstr(parsed->label, ==, "Person");
    g_assert_cmpuint(parsed->edge_filters->len, ==, 1);

    SondEdgeFilter *parsed_edge = g_ptr_array_index(parsed->edge_filters, 0);
    g_assert_cmpstr(parsed_edge->edge_label, ==, "works_for");
    g_assert_cmpuint(parsed_edge->property_filters->len, ==, 1);

    SondPropertyFilter *edge_pf = g_ptr_array_index(parsed_edge->property_filters, 0);
    g_assert_cmpstr(edge_pf->key, ==, "since");
    g_assert_cmpstr(edge_pf->value, ==, "2020");

    g_assert_nonnull(parsed_edge->target_criteria);
    g_assert_cmpstr(parsed_edge->target_criteria->label, ==, "Company");
    g_assert_cmpuint(parsed_edge->target_criteria->property_filters->len, ==, 1);

    SondPropertyFilter *target_pf = g_ptr_array_index(parsed_edge->target_criteria->property_filters, 0);
    g_assert_cmpstr(target_pf->key, ==, "name");
    g_assert_cmpstr(target_pf->value, ==, "Acme*");

    g_free(json);
    sond_graph_node_search_criteria_free(parsed);
    sond_graph_node_search_criteria_free(criteria);
}

static void
test_json_from_invalid_json_fails(void)
{
    GError *error = NULL;
    SondGraphNodeSearchCriteria *parsed = sond_graph_node_search_criteria_from_json("{not valid json", &error);

    g_assert_null(parsed);
    g_assert_nonnull(error);
    g_clear_error(&error);
}

void
test_sond_graph_db_filter_register(void)
{
    g_test_add_func("/sond_graph_db_filter/has_wildcards", test_has_wildcards);
    g_test_add_func("/sond_graph_db_filter/escape_sql_like_pattern", test_escape_sql_like_pattern);
    g_test_add_func("/sond_graph_db_filter/property_filter_constructors", test_property_filter_constructors);
    g_test_add_func("/sond_graph_db_filter/search_criteria_basic", test_search_criteria_basic);
    g_test_add_func("/sond_graph_db_filter/json_roundtrip_simple", test_json_roundtrip_simple);
    g_test_add_func("/sond_graph_db_filter/json_roundtrip_nested_path_filter", test_json_roundtrip_nested_path_filter);
    g_test_add_func("/sond_graph_db_filter/json_roundtrip_indexed_filter", test_json_roundtrip_indexed_filter);
    g_test_add_func("/sond_graph_db_filter/json_roundtrip_recursive_edge_filter", test_json_roundtrip_recursive_edge_filter);
    g_test_add_func("/sond_graph_db_filter/json_from_invalid_json_fails", test_json_from_invalid_json_fails);
}
