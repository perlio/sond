/*
 sond (sond_akte.c) - Akten, Beweisstücke, Unterlagen
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

/**
 * @file sond_akte.c
 * @brief Implementation der Akte-API
 */

#include "sond_akte.h"
#include "sond_graph_property.h"
#include "sond_graph_db.h"
#include <string.h>

#define AKTE_LABEL "Akte"

/* ========================================================================
 * Konstruktoren
 * ======================================================================== */

SondAkte* sond_akte_new(const gchar *jahr, const gchar *lfdnr) {
    g_return_val_if_fail(jahr != NULL, NULL);
    g_return_val_if_fail(lfdnr != NULL, NULL);

    SondAkte *akte = sond_graph_node_new();
    sond_graph_node_set_label(akte, AKTE_LABEL);

    sond_akte_set_regnr(akte, jahr, lfdnr);

    return akte;
}

SondAkte* sond_akte_new_full(const gchar *jahr,
                              const gchar *lfdnr,
                              const gchar *name,
                              const gchar *langbezeichnung) {
    g_return_val_if_fail(jahr != NULL, NULL);
    g_return_val_if_fail(lfdnr != NULL, NULL);
    g_return_val_if_fail(name != NULL, NULL);
    g_return_val_if_fail(langbezeichnung != NULL, NULL);

    SondAkte *akte = sond_akte_new(jahr, lfdnr);
    sond_akte_set_name(akte, name);
    sond_akte_set_langbezeichnung(akte, langbezeichnung);

    return akte;
}

/* ========================================================================
 * Registriernummer
 * ======================================================================== */

gchar* sond_akte_get_jahr(SondAkte *akte) {
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(akte), NULL);

    GPtrArray *props = sond_graph_node_get_properties(akte);
    GPtrArray *regnr = sond_graph_property_list_get(props, "regnr");

    if (!regnr || regnr->len < 1) {
        if (regnr) g_ptr_array_unref(regnr);
        return NULL;
    }

    gchar *jahr = g_strdup(g_ptr_array_index(regnr, 0));
    g_ptr_array_unref(regnr);

    return jahr;
}

gchar* sond_akte_get_lfdnr(SondAkte *akte) {
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(akte), NULL);

    GPtrArray *props = sond_graph_node_get_properties(akte);
    GPtrArray *regnr = sond_graph_property_list_get(props, "regnr");

    if (!regnr || regnr->len < 2) {
        if (regnr) g_ptr_array_unref(regnr);
        return NULL;
    }

    gchar *lfdnr = g_strdup(g_ptr_array_index(regnr, 1));
    g_ptr_array_unref(regnr);

    return lfdnr;
}

gboolean sond_akte_get_regnr(SondAkte *akte, gchar **jahr, gchar **lfdnr) {
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(akte), FALSE);

    GPtrArray *props = sond_graph_node_get_properties(akte);
    GPtrArray *regnr = sond_graph_property_list_get(props, "regnr");

    if (!regnr || regnr->len < 2) {
        if (regnr) g_ptr_array_unref(regnr);
        if (jahr) *jahr = NULL;
        if (lfdnr) *lfdnr = NULL;
        return FALSE;
    }

    if (jahr) {
        *jahr = g_strdup(g_ptr_array_index(regnr, 0));
    }

    if (lfdnr) {
        *lfdnr = g_strdup(g_ptr_array_index(regnr, 1));
    }

    g_ptr_array_unref(regnr);
    return TRUE;
}

void sond_akte_set_regnr(SondAkte *akte, const gchar *jahr, const gchar *lfdnr) {
    g_return_if_fail(SOND_IS_GRAPH_NODE(akte));
    g_return_if_fail(jahr != NULL);
    g_return_if_fail(lfdnr != NULL);

    GPtrArray *props = sond_graph_node_get_properties(akte);

    const gchar *values[] = { jahr, lfdnr };
    sond_graph_property_list_set(props, "regnr", values, 2);
}

gchar* sond_akte_get_regnr_string(SondAkte *akte) {
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(akte), NULL);

    gchar *jahr = NULL;
    gchar *lfdnr = NULL;

    if (!sond_akte_get_regnr(akte, &jahr, &lfdnr)) {
        return NULL;
    }

    gchar *result = g_strdup_printf("%s/%s", jahr, lfdnr);

    g_free(jahr);
    g_free(lfdnr);

    return result;
}

/* ========================================================================
 * Name und Bezeichnung
 * ======================================================================== */

gchar* sond_akte_get_name(SondAkte *akte) {
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(akte), NULL);

    GPtrArray *props = sond_graph_node_get_properties(akte);
    const gchar *name = sond_graph_property_list_get_string(props, "name");

    return name ? g_strdup(name) : NULL;
}

void sond_akte_set_name(SondAkte *akte, const gchar *name) {
    g_return_if_fail(SOND_IS_GRAPH_NODE(akte));
    g_return_if_fail(name != NULL);

    GPtrArray *props = sond_graph_node_get_properties(akte);
    sond_graph_property_list_set_string(props, "name", name);
}

gchar* sond_akte_get_langbezeichnung(SondAkte *akte) {
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(akte), NULL);

    GPtrArray *props = sond_graph_node_get_properties(akte);
    const gchar *lang = sond_graph_property_list_get_string(props, "langbezeichnung");

    return lang ? g_strdup(lang) : NULL;
}

void sond_akte_set_langbezeichnung(SondAkte *akte, const gchar *langbezeichnung) {
    g_return_if_fail(SOND_IS_GRAPH_NODE(akte));
    g_return_if_fail(langbezeichnung != NULL);

    GPtrArray *props = sond_graph_node_get_properties(akte);
    sond_graph_property_list_set_string(props, "langbezeichnung", langbezeichnung);
}

/* ========================================================================
 * Datum
 * ======================================================================== */

gchar* sond_akte_get_angelegt_am(SondAkte *akte) {
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(akte), NULL);

    GPtrArray *props = sond_graph_node_get_properties(akte);
    const gchar *datum = sond_graph_property_list_get_string(props, "angelegt_am");

    return datum ? g_strdup(datum) : NULL;
}

void sond_akte_set_angelegt_am(SondAkte *akte, const gchar *datum) {
    g_return_if_fail(SOND_IS_GRAPH_NODE(akte));
    g_return_if_fail(datum != NULL);

    GPtrArray *props = sond_graph_node_get_properties(akte);
    sond_graph_property_list_set_string(props, "angelegt_am", datum);
}

GDateTime* sond_akte_get_angelegt_am_datetime(SondAkte *akte) {
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(akte), NULL);

    gchar *datum = sond_akte_get_angelegt_am(akte);
    if (!datum) {
        return NULL;
    }

    /* Parse ISO 8601: YYYY-MM-DD */
    int year, month, day;
    if (sscanf(datum, "%d-%d-%d", &year, &month, &day) != 3) {
        g_free(datum);
        return NULL;
    }

    g_free(datum);

    return g_date_time_new_local(year, month, day, 0, 0, 0.0);
}

void sond_akte_set_angelegt_am_datetime(SondAkte *akte, GDateTime *datetime) {
    g_return_if_fail(SOND_IS_GRAPH_NODE(akte));
    g_return_if_fail(datetime != NULL);

    gchar *datum = g_date_time_format(datetime, "%Y-%m-%d");
    sond_akte_set_angelegt_am(akte, datum);
    g_free(datum);
}

/* ========================================================================
 * Ablage
 * ======================================================================== */

gboolean sond_akte_is_abgelegt(SondAkte *akte) {
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(akte), FALSE);

    GPtrArray *props = sond_graph_node_get_properties(akte);
    return sond_graph_property_list_has(props, "abgelegt");
}

gboolean sond_akte_get_ablage(SondAkte *akte, gchar **datum, gchar **nummer) {
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(akte), FALSE);

    GPtrArray *props = sond_graph_node_get_properties(akte);
    GPtrArray *abgelegt = sond_graph_property_list_get(props, "abgelegt");

    if (!abgelegt || abgelegt->len < 2) {
        if (abgelegt) g_ptr_array_unref(abgelegt);
        if (datum) *datum = NULL;
        if (nummer) *nummer = NULL;
        return FALSE;
    }

    if (datum) {
        *datum = g_strdup(g_ptr_array_index(abgelegt, 0));
    }

    if (nummer) {
        *nummer = g_strdup(g_ptr_array_index(abgelegt, 1));
    }

    g_ptr_array_unref(abgelegt);
    return TRUE;
}

void sond_akte_set_ablage(SondAkte *akte, const gchar *datum, const gchar *nummer) {
    g_return_if_fail(SOND_IS_GRAPH_NODE(akte));
    g_return_if_fail(datum != NULL);
    g_return_if_fail(nummer != NULL);

    GPtrArray *props = sond_graph_node_get_properties(akte);

    const gchar *values[] = { datum, nummer };
    sond_graph_property_list_set(props, "abgelegt", values, 2);
}

void sond_akte_clear_ablage(SondAkte *akte) {
    g_return_if_fail(SOND_IS_GRAPH_NODE(akte));

    GPtrArray *props = sond_graph_node_get_properties(akte);
    sond_graph_property_list_remove(props, "abgelegt");
}

/* ========================================================================
 * Validierung
 * ======================================================================== */

gboolean sond_akte_is_valid(SondAkte *akte) {
    GError *error = NULL;
    gboolean valid = sond_akte_validate(akte, &error);

    if (error) {
        g_error_free(error);
    }

    return valid;
}

gboolean sond_akte_validate(SondAkte *akte, GError **error) {
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(akte), FALSE);

    /* Label prüfen */
    const gchar *label = sond_graph_node_get_label(akte);
    if (g_strcmp0(label, AKTE_LABEL) != 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Node label must be '%s', got '%s'", AKTE_LABEL, label);
        return FALSE;
    }

    /* Registriernummer prüfen */
    gchar *jahr = NULL;
    gchar *lfdnr = NULL;
    if (!sond_akte_get_regnr(akte, &jahr, &lfdnr)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Missing or invalid registriernummer");
        return FALSE;
    }
    g_free(jahr);
    g_free(lfdnr);

    /* Name prüfen */
    gchar *name = sond_akte_get_name(akte);
    if (!name || strlen(name) == 0) {
        g_free(name);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Missing name");
        return FALSE;
    }
    g_free(name);

    /* Langbezeichnung prüfen */
    gchar *lang = sond_akte_get_langbezeichnung(akte);
    if (!lang || strlen(lang) == 0) {
        g_free(lang);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Missing langbezeichnung");
        return FALSE;
    }
    g_free(lang);

    return TRUE;
}

/* ========================================================================
 * Datenbank-Operationen
 * ======================================================================== */

gboolean sond_akte_save(MYSQL *conn, SondAkte *akte, GError **error) {
    g_return_val_if_fail(conn != NULL, FALSE);
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(akte), FALSE);

    /* Validieren vor dem Speichern */
    if (!sond_akte_validate(akte, error)) {
        g_prefix_error(error, "Akte validation failed: ");
        return FALSE;
    }

    return sond_graph_db_save_node(conn, akte, error);
}

SondAkte* sond_akte_load(MYSQL *conn, gint64 akte_id, GError **error) {
    g_return_val_if_fail(conn != NULL, NULL);

    SondAkte *akte = sond_graph_db_load_node(conn, akte_id, error);
    if (!akte) {
        return NULL;
    }

    /* Prüfen ob es wirklich eine Akte ist */
    const gchar *label = sond_graph_node_get_label(akte);
    if (g_strcmp0(label, AKTE_LABEL) != 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Node %ld is not an Akte (label: %s)", akte_id, label);
        g_object_unref(akte);
        return NULL;
    }

    return akte;
}

gboolean sond_akte_delete(MYSQL *conn, gint64 akte_id, GError **error) {
    g_return_val_if_fail(conn != NULL, FALSE);

    return sond_graph_db_delete_node(conn, akte_id, error);
}

/* ========================================================================
 * Such-Funktionen
 * ======================================================================== */

SondAkte* sond_akte_search_by_regnr(MYSQL *conn,
                                     const gchar *jahr,
                                     const gchar *lfdnr,
                                     GError **error) {
    g_return_val_if_fail(conn != NULL, NULL);
    g_return_val_if_fail(jahr != NULL, NULL);
    g_return_val_if_fail(lfdnr != NULL, NULL);

    SondGraphNodeSearchCriteria *criteria = sond_graph_node_search_criteria_new();
    criteria->label = g_strdup(AKTE_LABEL);
    criteria->limit = 1;

    /* Suche nach Jahr und lfdnr - beide müssen matchen */
    sond_graph_node_search_criteria_add_property_filter(criteria, "regnr", jahr);
    sond_graph_node_search_criteria_add_property_filter(criteria, "regnr", lfdnr);

    GPtrArray *results = sond_graph_db_search_nodes(conn, criteria, error);
    sond_graph_node_search_criteria_free(criteria);

    if (!results) {
        return NULL;
    }

    SondAkte *akte = NULL;
    if (results->len > 0) {
        akte = g_ptr_array_index(results, 0);
        g_object_ref(akte);  /* Referenz erhöhen, da Array freigegeben wird */
    }

    g_ptr_array_unref(results);

    return akte;
}

GPtrArray* sond_akte_search_by_year(MYSQL *conn,
                                     const gchar *jahr,
                                     GError **error) {
    g_return_val_if_fail(conn != NULL, NULL);
    g_return_val_if_fail(jahr != NULL, NULL);

    SondGraphNodeSearchCriteria *criteria = sond_graph_node_search_criteria_new();
    criteria->label = g_strdup(AKTE_LABEL);

    sond_graph_node_search_criteria_add_property_filter(criteria, "regnr", jahr);

    GPtrArray *results = sond_graph_db_search_nodes(conn, criteria, error);
    sond_graph_node_search_criteria_free(criteria);

    return results;
}

GPtrArray* sond_akte_search_by_name(MYSQL *conn,
                                     const gchar *name,
                                     GError **error) {
    g_return_val_if_fail(conn != NULL, NULL);
    g_return_val_if_fail(name != NULL, NULL);

    SondGraphNodeSearchCriteria *criteria = sond_graph_node_search_criteria_new();
    criteria->label = g_strdup(AKTE_LABEL);

    sond_graph_node_search_criteria_add_property_filter(criteria, "name", name);

    GPtrArray *results = sond_graph_db_search_nodes(conn, criteria, error);
    sond_graph_node_search_criteria_free(criteria);

    return results;
}

GPtrArray* sond_akte_get_all_abgelegt(MYSQL *conn, GError **error) {
    g_return_val_if_fail(conn != NULL, NULL);

    /* Suche nach Akten die "abgelegt" Property haben */
    SondGraphNodeSearchCriteria *criteria = sond_graph_node_search_criteria_new();
    criteria->label = g_strdup(AKTE_LABEL);

    /* Wildcard * matcht jeden Wert */
    sond_graph_node_search_criteria_add_property_filter(criteria, "abgelegt", "*");

    GPtrArray *results = sond_graph_db_search_nodes(conn, criteria, error);
    sond_graph_node_search_criteria_free(criteria);

    return results;
}

GPtrArray* sond_akte_get_all_active(MYSQL *conn, GError **error) {
    g_return_val_if_fail(conn != NULL, NULL);

    /* Problem: SQL kann nicht direkt nach "Property existiert NICHT" suchen
     * Workaround: Alle Akten laden und filtern */

    SondGraphNodeSearchCriteria *criteria = sond_graph_node_search_criteria_new();
    criteria->label = g_strdup(AKTE_LABEL);

    GPtrArray *all_akten = sond_graph_db_search_nodes(conn, criteria, error);
    sond_graph_node_search_criteria_free(criteria);

    if (!all_akten) {
        return NULL;
    }

    /* Filtern: nur nicht-abgelegte */
    GPtrArray *active = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);

    for (guint i = 0; i < all_akten->len; i++) {
        SondAkte *akte = g_ptr_array_index(all_akten, i);

        if (!sond_akte_is_abgelegt(akte)) {
            g_ptr_array_add(active, g_object_ref(akte));
        }
    }

    g_ptr_array_unref(all_akten);

    return active;
}

/*
 * Kompilierung:
 * gcc -c sond_akte.c $(mysql_config --cflags) $(pkg-config --cflags glib-2.0 gobject-2.0)
 */
