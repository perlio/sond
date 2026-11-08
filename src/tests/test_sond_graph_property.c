/*
 sond (test_sond_graph_property.c) - Akten, Beweisstücke, Unterlagen
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
 * Erster Baustein der neuen Test-Infrastruktur fuer sond_server
 * (GLib Testing, g_test_init/g_test_add_func - kein DB-/Netzwerk-Zugriff).
 *
 * sond_graph_property.c wurde als erstes Ziel gewaehlt, weil es reine,
 * deterministische Datenstruktur-Logik ist (nur glib + json-glib), ohne
 * GTK/DB/Netzwerk - also genau der Fall, fuer den GLib Testing gedacht ist.
 */

#include <glib.h>
#include "../sond_graph/sond_graph_property.h"

static void
test_new_single_value(void)
{
    SondGraphProperty *prop = sond_graph_property_new_string("name", "Max");

    g_assert_nonnull(prop);
    g_assert_cmpstr(sond_graph_property_get_key(prop), ==, "name");
    g_assert_cmpuint(sond_graph_property_get_value_count(prop), ==, 1);
    g_assert_cmpstr(sond_graph_property_get_first_value(prop), ==, "Max");

    sond_graph_property_free(prop);
}

static void
test_new_multi_value(void)
{
    const gchar *values[] = { "a", "b", "c" };
    SondGraphProperty *prop = sond_graph_property_new("tags", values, 3);

    g_assert_nonnull(prop);
    g_assert_cmpuint(sond_graph_property_get_value_count(prop), ==, 3);

    GPtrArray *got = sond_graph_property_get_values(prop);
    g_assert_nonnull(got);
    g_assert_cmpuint(got->len, ==, 3);
    g_assert_cmpstr(g_ptr_array_index(got, 0), ==, "a");
    g_assert_cmpstr(g_ptr_array_index(got, 1), ==, "b");
    g_assert_cmpstr(g_ptr_array_index(got, 2), ==, "c");

    sond_graph_property_free(prop);
}

static void
test_set_values_replaces(void)
{
    SondGraphProperty *prop = sond_graph_property_new_string("k", "old");
    const gchar *new_values[] = { "new1", "new2" };

    sond_graph_property_set_values(prop, new_values, 2);
    g_assert_cmpuint(sond_graph_property_get_value_count(prop), ==, 2);
    g_assert_cmpstr(sond_graph_property_get_first_value(prop), ==, "new1");

    sond_graph_property_set_string(prop, "single");
    g_assert_cmpuint(sond_graph_property_get_value_count(prop), ==, 1);
    g_assert_cmpstr(sond_graph_property_get_first_value(prop), ==, "single");

    sond_graph_property_free(prop);
}

static void
test_subproperties(void)
{
    SondGraphProperty *parent = sond_graph_property_new_string("parent", "p");
    SondGraphProperty *child = sond_graph_property_new_string("child", "c");

    sond_graph_property_add_subproperty(parent, child);

    GPtrArray *subs = sond_graph_property_get_properties(parent);
    g_assert_nonnull(subs);
    g_assert_cmpuint(subs->len, ==, 1);
    g_assert_true(g_ptr_array_index(subs, 0) == child);

    /* child gehoert jetzt parent - KEIN eigenes free. */
    sond_graph_property_free(parent);
}

static void
test_find(void)
{
    SondGraphProperty *a = sond_graph_property_new_string("a", "1");
    SondGraphProperty *b = sond_graph_property_new_string("b", "2");
    GPtrArray *arr = g_ptr_array_new();

    g_ptr_array_add(arr, a);
    g_ptr_array_add(arr, b);

    g_assert_true(sond_graph_property_find(arr, "b") == b);
    g_assert_null(sond_graph_property_find(arr, "nope"));

    g_ptr_array_set_free_func(arr, (GDestroyNotify) sond_graph_property_free);
    g_ptr_array_free(arr, TRUE);
}

static void
test_list_set_get_roundtrip(void)
{
    GPtrArray *props = g_ptr_array_new_with_free_func(
            (GDestroyNotify) sond_graph_property_free);

    g_assert_false(sond_graph_property_list_has(props, "status"));

    sond_graph_property_list_set_string(props, "status", "open");
    g_assert_true(sond_graph_property_list_has(props, "status"));
    g_assert_cmpstr(sond_graph_property_list_get_string(props, "status"),
            ==, "open");
    g_assert_cmpuint(sond_graph_property_list_get_count(props, "status"), ==, 1);

    /* Erneutes list_set_string auf denselben key ersetzt, statt zu
     * duplizieren. */
    sond_graph_property_list_set_string(props, "status", "closed");
    g_assert_cmpuint(props->len, ==, 1);
    g_assert_cmpstr(sond_graph_property_list_get_string(props, "status"),
            ==, "closed");

    const gchar *multi[] = { "x", "y" };
    sond_graph_property_list_set(props, "multi", multi, 2);
    GPtrArray *vals = sond_graph_property_list_get(props, "multi");
    g_assert_nonnull(vals);
    g_assert_cmpuint(vals->len, ==, 2);
    g_ptr_array_unref(vals);

    sond_graph_property_list_remove(props, "status");
    g_assert_false(sond_graph_property_list_has(props, "status"));
    g_assert_null(sond_graph_property_list_get_string(props, "status"));

    g_ptr_array_unref(props);
}

static void
test_list_get_keys(void)
{
    GPtrArray *props = g_ptr_array_new_with_free_func(
            (GDestroyNotify) sond_graph_property_free);

    sond_graph_property_list_set_string(props, "k1", "v1");
    sond_graph_property_list_set_string(props, "k2", "v2");

    GPtrArray *keys = sond_graph_property_list_get_keys(props);
    g_assert_nonnull(keys);
    g_assert_cmpuint(keys->len, ==, 2);

    gboolean has_k1 = FALSE, has_k2 = FALSE;
    for (guint i = 0; i < keys->len; i++) {
        const gchar *k = g_ptr_array_index(keys, i);
        if (g_strcmp0(k, "k1") == 0) has_k1 = TRUE;
        if (g_strcmp0(k, "k2") == 0) has_k2 = TRUE;
    }
    g_assert_true(has_k1);
    g_assert_true(has_k2);

    g_ptr_array_unref(keys);
    g_ptr_array_unref(props);
}

static void
test_path_api_creates_intermediate(void)
{
    GPtrArray *props = g_ptr_array_new_with_free_func(
            (GDestroyNotify) sond_graph_property_free);
    const gchar *path[] = { "metadata", "author", "name" };

    g_assert_false(sond_graph_property_list_has_at_path(props, path, 3));

    sond_graph_property_list_set_string_at_path(props, path, 3, "Heinz");

    g_assert_true(sond_graph_property_list_has_at_path(props, path, 3));
    gchar *val = sond_graph_property_list_get_string_at_path(props, path, 3);
    g_assert_cmpstr(val, ==, "Heinz");
    g_free(val);

    /* Zwischenebenen muessen automatisch angelegt worden sein. */
    const gchar *path_metadata[] = { "metadata" };
    g_assert_true(sond_graph_property_list_has_at_path(props, path_metadata, 1));

    const gchar *path_author[] = { "metadata", "author" };
    g_assert_true(sond_graph_property_list_has_at_path(props, path_author, 2));

    /* Ein anderer Blattpfad unter derselben Zwischenebene darf nicht da sein. */
    const gchar *path_other[] = { "metadata", "author", "email" };
    g_assert_false(sond_graph_property_list_has_at_path(props, path_other, 3));

    g_ptr_array_unref(props);
}

static void
test_path_api_multi_value(void)
{
    GPtrArray *props = g_ptr_array_new_with_free_func(
            (GDestroyNotify) sond_graph_property_free);
    const gchar *path[] = { "a", "b" };
    const gchar *values[] = { "1", "2", "3" };

    sond_graph_property_list_set_at_path(props, path, 2, values, 3);

    GPtrArray *got = sond_graph_property_list_get_at_path(props, path, 2);
    g_assert_nonnull(got);
    g_assert_cmpuint(got->len, ==, 3);
    g_assert_cmpstr(g_ptr_array_index(got, 0), ==, "1");
    g_assert_cmpstr(g_ptr_array_index(got, 2), ==, "3");
    g_ptr_array_unref(got);

    g_ptr_array_unref(props);
}

static void
test_json_roundtrip(void)
{
    GPtrArray *props = g_ptr_array_new_with_free_func(
            (GDestroyNotify) sond_graph_property_free);
    const gchar *path[] = { "metadata", "author" };

    sond_graph_property_list_set_string(props, "status", "open");
    sond_graph_property_list_set_string_at_path(props, path, 2, "Heinz");

    gchar *json = sond_graph_property_list_to_json(props);
    g_assert_nonnull(json);

    GError *error = NULL;
    GPtrArray *parsed = sond_graph_property_list_from_json(json, &error);
    g_assert_no_error(error);
    g_assert_nonnull(parsed);

    g_assert_cmpstr(sond_graph_property_list_get_string(parsed, "status"),
            ==, "open");

    gchar *val = sond_graph_property_list_get_string_at_path(parsed, path, 2);
    g_assert_cmpstr(val, ==, "Heinz");
    g_free(val);

    g_free(json);
    g_ptr_array_unref(parsed);
    g_ptr_array_unref(props);
}

static void
test_json_from_invalid_json_fails(void)
{
    GError *error = NULL;
    GPtrArray *parsed = sond_graph_property_list_from_json("{not valid json", &error);

    g_assert_null(parsed);
    g_assert_nonnull(error);
    g_clear_error(&error);
}

/* Kein eigenes main() mehr - Registrierung erfolgt zentral in test_main.c,
 * damit mehrere Test-Dateien zu EINEM Testbinary zusammengelinkt werden
 * koennen (sonst mehrfach definiertes main()). */
void
test_sond_graph_property_register(void)
{
    g_test_add_func("/sond_graph_property/new_single_value", test_new_single_value);
    g_test_add_func("/sond_graph_property/new_multi_value", test_new_multi_value);
    g_test_add_func("/sond_graph_property/set_values_replaces", test_set_values_replaces);
    g_test_add_func("/sond_graph_property/subproperties", test_subproperties);
    g_test_add_func("/sond_graph_property/find", test_find);
    g_test_add_func("/sond_graph_property/list_set_get_roundtrip", test_list_set_get_roundtrip);
    g_test_add_func("/sond_graph_property/list_get_keys", test_list_get_keys);
    g_test_add_func("/sond_graph_property/path_api_creates_intermediate", test_path_api_creates_intermediate);
    g_test_add_func("/sond_graph_property/path_api_multi_value", test_path_api_multi_value);
    g_test_add_func("/sond_graph_property/json_roundtrip", test_json_roundtrip);
    g_test_add_func("/sond_graph_property/json_from_invalid_json_fails", test_json_from_invalid_json_fails);
}
