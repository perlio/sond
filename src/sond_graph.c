/*
 sond (sond_graph.c) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_graph_db.c
 * @brief Implementation der Graph-Datenbankfunktionen
 */

#include "sond_graph_db.h"
#include "sond_graph_node.h"
#include "sond_graph_edge.h"
#include <string.h>

GQuark sond_graph_db_error_quark(void) {
    return g_quark_from_static_string("sond-graph-db-error-quark");
}

/* SQL-Statements für Tabellen */
static const char *table_statements[] = {
    /* Nodes-Tabelle */
    "CREATE TABLE IF NOT EXISTS nodes ("
    "  id BIGINT PRIMARY KEY AUTO_INCREMENT,"
    "  label VARCHAR(50) NOT NULL,"
    "  properties JSON NOT NULL DEFAULT ('[]'),"
    "  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
    "  updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
    "  INDEX idx_nodes_label (label)"
    ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci",

    /* Edges-Tabelle */
    "CREATE TABLE IF NOT EXISTS edges ("
    "  id BIGINT PRIMARY KEY AUTO_INCREMENT,"
    "  source_id BIGINT NOT NULL,"
    "  target_id BIGINT NOT NULL,"
    "  label VARCHAR(50) NOT NULL,"
    "  properties JSON NOT NULL DEFAULT ('[]'),"
    "  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
    "  updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
    "  FOREIGN KEY (source_id) REFERENCES nodes(id) ON DELETE CASCADE,"
    "  FOREIGN KEY (target_id) REFERENCES nodes(id) ON DELETE CASCADE,"
    "  INDEX idx_edges_source (source_id),"
    "  INDEX idx_edges_target (target_id),"
    "  INDEX idx_edges_label (label),"
    "  INDEX idx_edges_source_target (source_id, target_id)"
    ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci",

    /* Node-Locks-Tabelle */
    "CREATE TABLE IF NOT EXISTS node_locks ("
    "  node_id BIGINT PRIMARY KEY,"
    "  locked_by VARCHAR(100) NOT NULL,"
    "  locked_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
    "  expires_at TIMESTAMP NULL,"
    "  lock_reason VARCHAR(255) NULL,"
    "  FOREIGN KEY (node_id) REFERENCES nodes(id) ON DELETE CASCADE,"
    "  INDEX idx_locks_user (locked_by),"
    "  INDEX idx_locks_expires (expires_at)"
    ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci",

    NULL  /* Terminator */
};

/* SQL-Statements zum Löschen alter Procedures */
static const char *drop_procedure_statements[] = {
    "DROP PROCEDURE IF EXISTS lock_node",
    "DROP PROCEDURE IF EXISTS unlock_node",
    "DROP PROCEDURE IF EXISTS unlock_all_by_user",
    "DROP PROCEDURE IF EXISTS cleanup_expired_locks",
    "DROP PROCEDURE IF EXISTS check_lock_status",
    NULL
};

/* Stored Procedures */
static const char *procedure_statements[] = {
    /* lock_node */
    "CREATE PROCEDURE lock_node("
    "  IN p_node_id BIGINT,"
    "  IN p_user_id VARCHAR(100),"
    "  IN p_timeout_minutes INT,"
    "  IN p_reason VARCHAR(255)"
    ")"
    "BEGIN "
    "  DECLARE v_current_lock_user VARCHAR(100);"
    "  DECLARE v_expires TIMESTAMP;"
    "  SELECT locked_by INTO v_current_lock_user FROM node_locks WHERE node_id = p_node_id;"
    "  IF v_current_lock_user IS NOT NULL AND v_current_lock_user != p_user_id THEN "
    "    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Node ist bereits von einem anderen Bearbeiter gelockt';"
    "  END IF;"
    "  IF p_timeout_minutes IS NOT NULL THEN "
    "    SET v_expires = DATE_ADD(NOW(), INTERVAL p_timeout_minutes MINUTE);"
    "  END IF;"
    "  INSERT INTO node_locks (node_id, locked_by, locked_at, expires_at, lock_reason) "
    "  VALUES (p_node_id, p_user_id, CURRENT_TIMESTAMP, v_expires, p_reason) "
    "  ON DUPLICATE KEY UPDATE locked_by = p_user_id, locked_at = CURRENT_TIMESTAMP, "
    "    expires_at = v_expires, lock_reason = p_reason;"
    "  SELECT 'success' as status, node_id, locked_by, locked_at, expires_at "
    "  FROM node_locks WHERE node_id = p_node_id;"
    "END",

    /* unlock_node */
    "CREATE PROCEDURE unlock_node("
    "  IN p_node_id BIGINT,"
    "  IN p_user_id VARCHAR(100)"
    ")"
    "BEGIN "
    "  DECLARE v_current_lock_user VARCHAR(100);"
    "  SELECT locked_by INTO v_current_lock_user FROM node_locks WHERE node_id = p_node_id;"
    "  IF v_current_lock_user IS NULL THEN "
    "    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Node ist nicht gelockt';"
    "  ELSEIF v_current_lock_user != p_user_id THEN "
    "    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'Node kann nur vom Lock-Inhaber entsperrt werden';"
    "  END IF;"
    "  DELETE FROM node_locks WHERE node_id = p_node_id;"
    "  SELECT 'success' as status;"
    "END",

    /* unlock_all_by_user */
    "CREATE PROCEDURE unlock_all_by_user("
    "  IN p_user_id VARCHAR(100)"
    ")"
    "BEGIN "
    "  DELETE FROM node_locks WHERE locked_by = p_user_id;"
    "  SELECT ROW_COUNT() as unlocked_count;"
    "END",

    /* cleanup_expired_locks */
    "CREATE PROCEDURE cleanup_expired_locks() "
    "BEGIN "
    "  DELETE FROM node_locks WHERE expires_at IS NOT NULL AND expires_at < NOW();"
    "  SELECT ROW_COUNT() as cleaned_locks;"
    "END",

    /* check_lock_status */
    "CREATE PROCEDURE check_lock_status("
    "  IN p_node_id BIGINT"
    ")"
    "BEGIN "
    "  SELECT node_id, locked_by, locked_at, expires_at, lock_reason, "
    "    CASE WHEN expires_at IS NOT NULL AND expires_at < NOW() THEN 'expired' ELSE 'active' END as lock_status "
    "  FROM node_locks WHERE node_id = p_node_id;"
    "END",

    NULL  /* Terminator */
};

/**
 * execute_sql:
 *
 * Führt ein SQL-Statement aus.
 */
static gboolean execute_sql(MYSQL *conn, const char *sql, GError **error) {
    if (mysql_query(conn, sql)) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "SQL execution failed: %s (Statement: %.100s...)",
                   mysql_error(conn), sql);
        g_prefix_error(error, "%s: ", __func__);
        return FALSE;
    }
    return TRUE;
}

/**
 * create_tables:
 *
 * Erstellt alle Tabellen.
 */
static gboolean create_tables(MYSQL *conn, GError **error) {
    GError *tmp_error = NULL;

    g_print("Creating tables...\n");

    for (int i = 0; table_statements[i] != NULL; i++) {
        g_print("  Table %d...", i + 1);

        if (!execute_sql(conn, table_statements[i], &tmp_error)) {
            g_prefix_error(&tmp_error, "%s: ", __func__);
            g_propagate_error(error, tmp_error);
            return FALSE;
        }

        g_print(" OK\n");
    }

    return TRUE;
}

/**
 * drop_procedures:
 *
 * Löscht alte Procedures falls vorhanden.
 */
static gboolean drop_procedures(MYSQL *conn, GError **error) {
    GError *tmp_error = NULL;

    g_print("Dropping old procedures...\n");

    for (int i = 0; drop_procedure_statements[i] != NULL; i++) {
        if (!execute_sql(conn, drop_procedure_statements[i], &tmp_error)) {
            g_prefix_error(&tmp_error, "%s: ", __func__);
            g_propagate_error(error, tmp_error);
            return FALSE;
        }
    }

    g_print("  OK\n");

    return TRUE;
}

/**
 * create_procedures:
 *
 * Erstellt alle Stored Procedures.
 */
static gboolean create_procedures(MYSQL *conn, GError **error) {
    GError *tmp_error = NULL;

    g_print("Creating stored procedures...\n");

    for (int i = 0; procedure_statements[i] != NULL; i++) {
        g_print("  Procedure %d...", i + 1);

        if (!execute_sql(conn, procedure_statements[i], &tmp_error)) {
            g_prefix_error(&tmp_error, "%s: ", __func__);
            g_propagate_error(error, tmp_error);
            return FALSE;
        }

        g_print(" OK\n");
    }

    return TRUE;
}

/**
 * sond_graph_db_setup:
 */
gboolean sond_graph_db_setup(MYSQL *conn, GError **error) {
    GError *tmp_error = NULL;

    /* Validierung */
    if (conn == NULL) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_CONNECTION,
                   "MySQL connection is NULL");
        g_prefix_error(error, "%s: ", __func__);
        return FALSE;
    }

    /* Verbindung testen */
    if (mysql_ping(conn) != 0) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_CONNECTION,
                   "MySQL connection is not active: %s", mysql_error(conn));
        g_prefix_error(error, "%s: ", __func__);
        return FALSE;
    }

    g_print("=== Graph Database Setup ===\n\n");

    /* Tabellen erstellen */
    if (!create_tables(conn, &tmp_error)) {
        g_prefix_error(&tmp_error, "%s: ", __func__);
        g_propagate_error(error, tmp_error);
        return FALSE;
    }

    /* Alte Procedures löschen */
    if (!drop_procedures(conn, &tmp_error)) {
        g_prefix_error(&tmp_error, "%s: ", __func__);
        g_propagate_error(error, tmp_error);
        return FALSE;
    }

    /* Procedures erstellen */
    if (!create_procedures(conn, &tmp_error)) {
        g_prefix_error(&tmp_error, "%s: ", __func__);
        g_propagate_error(error, tmp_error);
        return FALSE;
    }

    g_print("\n✓ Graph database setup completed successfully!\n");

    return TRUE;
}

/* ========================================================================
 * Node Load/Search Functions
 * ======================================================================== */

/**
 * parse_mysql_timestamp:
 *
 * Konvertiert MySQL TIMESTAMP zu GDateTime.
 */
static GDateTime* parse_mysql_timestamp(const char *timestamp) {
    if (timestamp == NULL)
        return NULL;

    /* Format: YYYY-MM-DD HH:MM:SS */
    int year, month, day, hour, min, sec;
    if (sscanf(timestamp, "%d-%d-%d %d:%d:%d",
               &year, &month, &day, &hour, &min, &sec) == 6) {
        return g_date_time_new_local(year, month, day, hour, min, (gdouble)sec);
    }

    return NULL;
}

/**
 * sond_graph_db_load_node:
 */
SondGraphNode* sond_graph_db_load_node(MYSQL *conn, gint64 node_id, GError **error) {
    g_return_val_if_fail(conn != NULL, NULL);

    SondGraphNode *node = NULL;
    MYSQL_STMT *stmt = NULL;
    MYSQL_BIND bind_params[1];
    MYSQL_BIND bind_results[5];

    /* Query vorbereiten */
    const char *query =
        "SELECT id, label, properties, created_at, updated_at "
        "FROM nodes WHERE id = ?";

    stmt = mysql_stmt_init(conn);
    if (stmt == NULL) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_init failed: %s", mysql_error(conn));
        g_prefix_error(error, "%s: ", __func__);
        goto cleanup;
    }

    if (mysql_stmt_prepare(stmt, query, strlen(query))) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_prepare failed: %s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        goto cleanup;
    }

    /* Parameter binden */
    memset(bind_params, 0, sizeof(bind_params));
    bind_params[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_params[0].buffer = &node_id;

    if (mysql_stmt_bind_param(stmt, bind_params)) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_bind_param failed: %s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        goto cleanup;
    }

    /* Ausführen */
    if (mysql_stmt_execute(stmt)) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_execute failed: %s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        goto cleanup;
    }

    /* Ergebnis-Buffer */
    gint64 id;
    char label[51] = {0};
    unsigned long label_length;
    char *properties = g_malloc(65536);  /* Max JSON size */
    unsigned long properties_length;
    char created_at[20] = {0};
    unsigned long created_length;
    my_bool created_is_null;
    char updated_at[20] = {0};
    unsigned long updated_length;
    my_bool updated_is_null;

    memset(bind_results, 0, sizeof(bind_results));

    bind_results[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_results[0].buffer = &id;

    bind_results[1].buffer_type = MYSQL_TYPE_STRING;
    bind_results[1].buffer = label;
    bind_results[1].buffer_length = sizeof(label);
    bind_results[1].length = &label_length;

    bind_results[2].buffer_type = MYSQL_TYPE_STRING;
    bind_results[2].buffer = properties;
    bind_results[2].buffer_length = 65536;
    bind_results[2].length = &properties_length;

    bind_results[3].buffer_type = MYSQL_TYPE_STRING;
    bind_results[3].buffer = created_at;
    bind_results[3].buffer_length = sizeof(created_at);
    bind_results[3].length = &created_length;
    bind_results[3].is_null = &created_is_null;

    bind_results[4].buffer_type = MYSQL_TYPE_STRING;
    bind_results[4].buffer = updated_at;
    bind_results[4].buffer_length = sizeof(updated_at);
    bind_results[4].length = &updated_length;
    bind_results[4].is_null = &updated_is_null;

    if (mysql_stmt_bind_result(stmt, bind_results)) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_bind_result failed: %s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        g_free(properties);
        goto cleanup;
    }

    /* Fetch */
    int fetch_result = mysql_stmt_fetch(stmt);
    if (fetch_result == MYSQL_NO_DATA) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Node with ID %ld not found", node_id);
        g_prefix_error(error, "%s: ", __func__);
        g_free(properties);
        goto cleanup;
    } else if (fetch_result != 0) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_fetch failed: %s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        g_free(properties);
        goto cleanup;
    }

    /* Node erstellen */
    node = graph_node_new();
    graph_node_set_id(node, id);
    graph_node_set_label(node, label);

    /* Properties laden */
    GError *json_error = NULL;
    if (!graph_node_properties_from_json(node, properties, &json_error)) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Failed to parse properties JSON: %s",
                   json_error ? json_error->message : "unknown");
        g_prefix_error(error, "%s: ", __func__);
        g_clear_error(&json_error);
        g_object_unref(node);
        node = NULL;
        g_free(properties);
        goto cleanup;
    }
    g_free(properties);

    /* Timestamps */
    if (!created_is_null) {
        GDateTime *dt = parse_mysql_timestamp(created_at);
        if (dt) {
            graph_node_set_created_at(node, dt);
            g_date_time_unref(dt);
        }
    }

    if (!updated_is_null) {
        GDateTime *dt = parse_mysql_timestamp(updated_at);
        if (dt) {
            graph_node_set_updated_at(node, dt);
            g_date_time_unref(dt);
        }
    }

    mysql_stmt_close(stmt);
    stmt = NULL;

    /* Ausgehende Edges laden */
    const char *edges_query =
        "SELECT e.id, e.label, e.target_id "
        "FROM edges e WHERE e.source_id = ?";

    stmt = mysql_stmt_init(conn);
    if (mysql_stmt_prepare(stmt, edges_query, strlen(edges_query))) {
        /* Fehler beim Laden der Edges - Node trotzdem zurückgeben */
        g_warning("Failed to load edges: %s", mysql_stmt_error(stmt));
        goto cleanup;
    }

    memset(bind_params, 0, sizeof(bind_params));
    bind_params[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_params[0].buffer = &node_id;

    mysql_stmt_bind_param(stmt, bind_params);
    mysql_stmt_execute(stmt);

    /* Edge-Ergebnis */
    gint64 edge_id, target_id;
    char edge_label[51] = {0};
    unsigned long edge_label_length;

    MYSQL_BIND edge_bind[3];
    memset(edge_bind, 0, sizeof(edge_bind));

    edge_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    edge_bind[0].buffer = &edge_id;

    edge_bind[1].buffer_type = MYSQL_TYPE_STRING;
    edge_bind[1].buffer = edge_label;
    edge_bind[1].buffer_length = sizeof(edge_label);
    edge_bind[1].length = &edge_label_length;

    edge_bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    edge_bind[2].buffer = &target_id;

    mysql_stmt_bind_result(stmt, edge_bind);

    while (mysql_stmt_fetch(stmt) == 0) {
        SondGraphEdgeRef *ref = graph_edge_ref_new(edge_id, edge_label, target_id);
        graph_node_add_outgoing_edge(node, ref);
    }

cleanup:
    if (stmt) {
        mysql_stmt_close(stmt);
    }

    return node;
}

/**
 * SondPropertyFilter:
 */
SondPropertyFilter* sond_property_filter_new(const gchar *key, const gchar *value) {
    SondPropertyFilter *filter = g_new0(SondPropertyFilter, 1);
    filter->key = g_strdup(key);
    filter->value = g_strdup(value);
    filter->path = NULL;
    return filter;
}

SondPropertyFilter* sond_property_filter_new_with_path(const gchar *key,
                                                        const gchar *value,
                                                        const gchar *path) {
    SondPropertyFilter *filter = g_new0(SondPropertyFilter, 1);
    filter->key = g_strdup(key);
    filter->value = g_strdup(value);
    filter->path = g_strdup(path);
    return filter;
}

void sond_property_filter_free(SondPropertyFilter *filter) {
    if (filter == NULL)
        return;

    g_free(filter->key);
    g_free(filter->value);
    g_free(filter->path);
    g_free(filter);
}

/**
 * SondEdgeFilter:
 */
SondEdgeFilter* sond_edge_filter_new(const gchar *edge_label,
                                      SondGraphNodeSearchCriteria *target_criteria) {
    SondEdgeFilter *filter = g_new0(SondEdgeFilter, 1);
    filter->edge_label = g_strdup(edge_label);
    filter->property_filters = g_ptr_array_new_with_free_func((GDestroyNotify)sond_property_filter_free);
    filter->target_criteria = target_criteria;
    return filter;
}

void sond_edge_filter_free(SondEdgeFilter *filter) {
    if (filter == NULL)
        return;

    g_free(filter->edge_label);
    if (filter->property_filters != NULL) {
        g_ptr_array_unref(filter->property_filters);
    }
    if (filter->target_criteria != NULL) {
        sond_graph_node_search_criteria_free(filter->target_criteria);
    }
    g_free(filter);
}

/**
 * SondGraphNodeSearchCriteria:
 */
SondGraphNodeSearchCriteria* sond_graph_node_search_criteria_new(void) {
    SondGraphNodeSearchCriteria *criteria = g_new0(SondGraphNodeSearchCriteria, 1);
    criteria->property_filters = g_ptr_array_new_with_free_func((GDestroyNotify)sond_property_filter_free);
    criteria->edge_filters = g_ptr_array_new_with_free_func((GDestroyNotify)sond_edge_filter_free);
    return criteria;
}

void sond_graph_node_search_criteria_free(SondGraphNodeSearchCriteria *criteria) {
    if (criteria == NULL)
        return;

    g_free(criteria->label);
    if (criteria->property_filters != NULL) {
        g_ptr_array_unref(criteria->property_filters);
    }
    if (criteria->edge_filters != NULL) {
        g_ptr_array_unref(criteria->edge_filters);
    }
    g_free(criteria);
}

void sond_graph_node_search_criteria_add_property_filter(SondGraphNodeSearchCriteria *criteria,
                                                          const gchar *key,
                                                          const gchar *value) {
    g_return_if_fail(criteria != NULL);
    g_return_if_fail(key != NULL);
    g_return_if_fail(value != NULL);

    SondPropertyFilter *filter = sond_property_filter_new(key, value);
    g_ptr_array_add(criteria->property_filters, filter);
}

void sond_graph_node_search_criteria_add_nested_property_filter(SondGraphNodeSearchCriteria *criteria,
                                                                 const gchar *key,
                                                                 const gchar *value,
                                                                 const gchar *path) {
    g_return_if_fail(criteria != NULL);
    g_return_if_fail(key != NULL);
    g_return_if_fail(value != NULL);
    g_return_if_fail(path != NULL);

    SondPropertyFilter *filter = sond_property_filter_new_with_path(key, value, path);
    g_ptr_array_add(criteria->property_filters, filter);
}

void sond_graph_node_search_criteria_add_edge_filter(SondGraphNodeSearchCriteria *criteria,
                                                      const gchar *edge_label,
                                                      SondGraphNodeSearchCriteria *target_criteria) {
    g_return_if_fail(criteria != NULL);
    g_return_if_fail(edge_label != NULL);

    SondEdgeFilter *filter = sond_edge_filter_new(edge_label, target_criteria);
    g_ptr_array_add(criteria->edge_filters, filter);
}

void sond_graph_node_search_criteria_add_simple_edge_filter(SondGraphNodeSearchCriteria *criteria,
                                                             const gchar *edge_label) {
    g_return_if_fail(criteria != NULL);
    g_return_if_fail(edge_label != NULL);

    SondEdgeFilter *filter = sond_edge_filter_new(edge_label, NULL);
    g_ptr_array_add(criteria->edge_filters, filter);
}

void sond_edge_filter_add_property(SondEdgeFilter *edge_filter,
                                    const gchar *key,
                                    const gchar *value) {
    g_return_if_fail(edge_filter != NULL);
    g_return_if_fail(key != NULL);
    g_return_if_fail(value != NULL);

    SondPropertyFilter *filter = sond_property_filter_new(key, value);
    g_ptr_array_add(edge_filter->property_filters, filter);
}

void sond_edge_filter_add_nested_property(SondEdgeFilter *edge_filter,
                                           const gchar *key,
                                           const gchar *value,
                                           const gchar *path) {
    g_return_if_fail(edge_filter != NULL);
    g_return_if_fail(key != NULL);
    g_return_if_fail(value != NULL);
    g_return_if_fail(path != NULL);

    SondPropertyFilter *filter = sond_property_filter_new_with_path(key, value, path);
    g_ptr_array_add(edge_filter->property_filters, filter);
}

/**
 * has_wildcards:
 *
 * Prüft ob ein String Wildcards enthält (* oder ?).
 */
static gboolean has_wildcards(const gchar *str) {
    return (strchr(str, '*') != NULL || strchr(str, '?') != NULL);
}

/**
 * convert_wildcards_to_sql:
 *
 * Konvertiert Wildcards (* und ?) zu SQL LIKE Pattern (% und _).
 */
static gchar* convert_wildcards_to_sql(const gchar *value) {
    GString *result = g_string_new("");

    for (const gchar *p = value; *p != '\0'; p++) {
        if (*p == '*') {
            g_string_append_c(result, '%');
        } else if (*p == '?') {
            g_string_append_c(result, '_');
        } else {
            g_string_append_c(result, *p);
        }
    }

    return g_string_free(result, FALSE);
}

/**
 * build_property_filters_sql:
 *
 * Hilfsfunktion: Generiert SQL für Property-Filter.
 */
static void build_property_filters_sql(MYSQL *conn,
                                        const gchar *table_alias,
                                        GPtrArray *filters,
                                        GString *query) {
    if (filters == NULL || filters->len == 0)
        return;

    for (guint i = 0; i < filters->len; i++) {
        SondPropertyFilter *filter = g_ptr_array_index(filters, i);

        char *escaped_key = g_malloc(strlen(filter->key) * 2 + 1);
        char *escaped_value = g_malloc(strlen(filter->value) * 2 + 1);
        mysql_real_escape_string(conn, escaped_key, filter->key, strlen(filter->key));
        mysql_real_escape_string(conn, escaped_value, filter->value, strlen(filter->value));

        gboolean use_like = has_wildcards(filter->value);

        if (use_like) {
            gchar *sql_pattern = convert_wildcards_to_sql(filter->value);
            char *escaped_pattern = g_malloc(strlen(sql_pattern) * 2 + 1);
            mysql_real_escape_string(conn, escaped_pattern, sql_pattern, strlen(sql_pattern));

            if (filter->path != NULL) {
                char *escaped_path = g_malloc(strlen(filter->path) * 2 + 1);
                mysql_real_escape_string(conn, escaped_path, filter->path, strlen(filter->path));

                g_string_append_printf(query,
                    " AND EXISTS ("
                    "   SELECT 1 FROM JSON_TABLE(%s.properties, '$[*]' COLUMNS("
                    "     prop_key VARCHAR(255) PATH '$.key',"
                    "     nested_val JSON PATH '$.value.%s'"
                    "   )) AS jt"
                    "   WHERE jt.prop_key = '%s'"
                    "   AND JSON_UNQUOTE(jt.nested_val) LIKE '%s'"
                    " )",
                    table_alias, escaped_path, escaped_key, escaped_pattern);

                g_free(escaped_path);
            } else {
                g_string_append_printf(query,
                    " AND EXISTS ("
                    "   SELECT 1 FROM JSON_TABLE(%s.properties, '$[*]' COLUMNS("
                    "     prop_key VARCHAR(255) PATH '$.key',"
                    "     prop_value VARCHAR(1000) PATH '$.value'"
                    "   )) AS jt"
                    "   WHERE jt.prop_key = '%s'"
                    "   AND jt.prop_value LIKE '%s'"
                    " )",
                    table_alias, escaped_key, escaped_pattern);
            }

            g_free(sql_pattern);
            g_free(escaped_pattern);
        } else {
            if (filter->path != NULL) {
                char *escaped_path = g_malloc(strlen(filter->path) * 2 + 1);
                mysql_real_escape_string(conn, escaped_path, filter->path, strlen(filter->path));

                g_string_append_printf(query,
                    " AND JSON_CONTAINS(%s.properties,"
                    "   JSON_OBJECT('key', '%s', 'value', JSON_OBJECT('%s', '%s')),"
                    "   '$')",
                    table_alias, escaped_key, escaped_path, escaped_value);

                g_free(escaped_path);
            } else {
                g_string_append_printf(query,
                    " AND JSON_CONTAINS(%s.properties,"
                    "   JSON_OBJECT('key', '%s', 'value', '%s'),"
                    "   '$')",
                    table_alias, escaped_key, escaped_value);
            }
        }

        g_free(escaped_key);
        g_free(escaped_value);
    }
}

/**
 * build_edge_filters_sql:
 *
 * Rekursive Hilfsfunktion: Generiert SQL für Edge-Filter.
 */
static void build_edge_filters_sql(MYSQL *conn,
                                    GPtrArray *edge_filters,
                                    GString *query,
                                    const gchar *source_alias,
                                    guint depth) {
    if (edge_filters == NULL || edge_filters->len == 0)
        return;

    for (guint i = 0; i < edge_filters->len; i++) {
        SondEdgeFilter *edge_filter = g_ptr_array_index(edge_filters, i);

        /* Unique aliases für diese Tiefe */
        gchar *edge_alias = g_strdup_printf("e%u_%u", depth, i);
        gchar *target_alias = g_strdup_printf("t%u_%u", depth, i);

        char *escaped_edge_label = g_malloc(strlen(edge_filter->edge_label) * 2 + 1);
        mysql_real_escape_string(conn, escaped_edge_label,
                               edge_filter->edge_label,
                               strlen(edge_filter->edge_label));

        /* Edge EXISTS öffnen */
        g_string_append_printf(query,
            " AND EXISTS (SELECT 1 FROM edges %s WHERE %s.source_id = %s.id",
            edge_alias, edge_alias, source_alias);
        g_string_append_printf(query, " AND %s.label = '%s'", edge_alias, escaped_edge_label);

        /* Edge-Properties filtern */
        build_property_filters_sql(conn, edge_alias, edge_filter->property_filters, query);

        /* Target-Node-Kriterien (rekursiv!) */
        if (edge_filter->target_criteria != NULL) {
            SondGraphNodeSearchCriteria *tc = edge_filter->target_criteria;

            g_string_append_printf(query,
                " AND EXISTS (SELECT 1 FROM nodes %s WHERE %s.id = %s.target_id",
                target_alias, target_alias, edge_alias);

            /* Target Label */
            if (tc->label != NULL) {
                char *escaped_target_label = g_malloc(strlen(tc->label) * 2 + 1);
                mysql_real_escape_string(conn, escaped_target_label, tc->label, strlen(tc->label));
                g_string_append_printf(query, " AND %s.label = '%s'", target_alias, escaped_target_label);
                g_free(escaped_target_label);
            }

            /* Target Properties */
            build_property_filters_sql(conn, target_alias, tc->property_filters, query);

            /* REKURSION: Target hat auch Edges! */
            if (tc->edge_filters != NULL && tc->edge_filters->len > 0) {
                build_edge_filters_sql(conn, tc->edge_filters, query, target_alias, depth + 1);
            }

            g_string_append(query, ")");  /* Close target EXISTS */
        }

        g_string_append(query, ")");  /* Close edge EXISTS */

        g_free(edge_alias);
        g_free(target_alias);
        g_free(escaped_edge_label);
    }
}

/**
 * sond_graph_db_search_nodes:
 */
GPtrArray* sond_graph_db_search_nodes(MYSQL *conn,
                                       const SondGraphNodeSearchCriteria *criteria,
                                       GError **error) {
    g_return_val_if_fail(conn != NULL, NULL);
    g_return_val_if_fail(criteria != NULL, NULL);

    GString *query = g_string_new("SELECT DISTINCT n.id FROM nodes n WHERE 1=1");

    /* Label-Filter */
    if (criteria->label != NULL) {
        char *escaped_label = g_malloc(strlen(criteria->label) * 2 + 1);
        mysql_real_escape_string(conn, escaped_label, criteria->label, strlen(criteria->label));
        g_string_append_printf(query, " AND n.label = '%s'", escaped_label);
        g_free(escaped_label);
    }

    /* Node-Properties */
    build_property_filters_sql(conn, "n", criteria->property_filters, query);

    /* Edge-Filter (rekursiv!) */
    build_edge_filters_sql(conn, criteria->edge_filters, query, "n", 0);

    /* Limit und Offset */
    if (criteria->limit > 0) {
        g_string_append_printf(query, " LIMIT %u", criteria->limit);
    }
    if (criteria->offset > 0) {
        g_string_append_printf(query, " OFFSET %u", criteria->offset);
    }

    /* Debug */
    g_debug("Search query: %s", query->str);

    /* Query ausführen */
    if (mysql_query(conn, query->str)) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Search query failed: %s", mysql_error(conn));
        g_prefix_error(error, "%s: ", __func__);
        g_string_free(query, TRUE);
        return NULL;
    }

    g_string_free(query, TRUE);

    MYSQL_RES *result = mysql_store_result(conn);
    if (result == NULL) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Failed to get result: %s", mysql_error(conn));
        g_prefix_error(error, "%s: ", __func__);
        return NULL;
    }

    /* Nodes laden */
    GPtrArray *nodes = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result))) {
        gint64 node_id = g_ascii_strtoll(row[0], NULL, 10);

        GError *load_error = NULL;
        SondGraphNode *node = sond_graph_db_load_node(conn, node_id, &load_error);
        if (node != NULL) {
            g_ptr_array_add(nodes, node);
        } else {
            g_warning("Failed to load node %ld: %s", node_id,
                     load_error ? load_error->message : "unknown");
            g_clear_error(&load_error);
        }
    }

    mysql_free_result(result);

    return nodes;
}

/**
 * sond_graph_db_save_node:
 */
gboolean sond_graph_db_save_node(MYSQL *conn, SondGraphNode *node, GError **error) {
    g_return_val_if_fail(conn != NULL, FALSE);
    g_return_val_if_fail(GRAPH_IS_NODE(node), FALSE);

    GError *tmp_error = NULL;
    MYSQL_STMT *stmt = NULL;
    gboolean success = FALSE;

    gint64 id = graph_node_get_id(node);
    const gchar *label = graph_node_get_label(node);
    gchar *properties_json = graph_node_properties_to_json(node);

    if (label == NULL) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Node label cannot be NULL");
        g_prefix_error(error, "%s: ", __func__);
        g_free(properties_json);
        return FALSE;
    }

    const char *query;
    int param_count;

    if (id == 0) {
        /* INSERT - neuer Node */
        query = "INSERT INTO nodes (label, properties) VALUES (?, ?)";
        param_count = 2;
    } else {
        /* UPDATE - bestehender Node */
        query = "UPDATE nodes SET label = ?, properties = ? WHERE id = ?";
        param_count = 3;
    }

    stmt = mysql_stmt_init(conn);
    if (stmt == NULL) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_init failed: %s", mysql_error(conn));
        g_prefix_error(error, "%s: ", __func__);
        g_free(properties_json);
        return FALSE;
    }

    if (mysql_stmt_prepare(stmt, query, strlen(query))) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_prepare failed: %s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        g_free(properties_json);
        mysql_stmt_close(stmt);
        return FALSE;
    }

    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));

    unsigned long label_length = strlen(label);
    unsigned long props_length = strlen(properties_json);

    /* Parameter 1: label */
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)label;
    bind[0].buffer_length = label_length;
    bind[0].length = &label_length;

    /* Parameter 2: properties */
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = properties_json;
    bind[1].buffer_length = props_length;
    bind[1].length = &props_length;

    /* Parameter 3: id (nur bei UPDATE) */
    if (id > 0) {
        bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[2].buffer = &id;
    }

    if (mysql_stmt_bind_param(stmt, bind)) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_bind_param failed: %s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        g_free(properties_json);
        mysql_stmt_close(stmt);
        return FALSE;
    }

    if (mysql_stmt_execute(stmt)) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_execute failed: %s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        g_free(properties_json);
        mysql_stmt_close(stmt);
        return FALSE;
    }

    /* Bei INSERT: Neue ID setzen */
    if (id == 0) {
        gint64 new_id = mysql_stmt_insert_id(stmt);
        graph_node_set_id(node, new_id);
    }

    success = TRUE;

    g_free(properties_json);
    mysql_stmt_close(stmt);

    return success;
}

/**
 * sond_graph_db_delete_node:
 */
gboolean sond_graph_db_delete_node(MYSQL *conn, gint64 node_id, GError **error) {
    g_return_val_if_fail(conn != NULL, FALSE);

    if (node_id == 0) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Cannot delete node with ID 0");
        g_prefix_error(error, "%s: ", __func__);
        return FALSE;
    }

    gchar *query = g_strdup_printf("DELETE FROM nodes WHERE id = %ld", node_id);

    if (mysql_query(conn, query)) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Delete failed: %s", mysql_error(conn));
        g_prefix_error(error, "%s: ", __func__);
        g_free(query);
        return FALSE;
    }

    g_free(query);

    /* Prüfen ob wirklich gelöscht wurde */
    my_ulonglong affected = mysql_affected_rows(conn);
    if (affected == 0) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Node with ID %ld not found", node_id);
        g_prefix_error(error, "%s: ", __func__);
        return FALSE;
    }

    return TRUE;
}

/* ========================================================================
 * Node Lock Functions
 * ======================================================================== */

/**
 * sond_graph_db_lock_node:
 */
gboolean sond_graph_db_lock_node(MYSQL *conn,
                                  gint64 node_id,
                                  const gchar *user_id,
                                  guint timeout_minutes,
                                  const gchar *reason,
                                  GError **error) {
    g_return_val_if_fail(conn != NULL, FALSE);
    g_return_val_if_fail(user_id != NULL, FALSE);

    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (stmt == NULL) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_init failed: %s", mysql_error(conn));
        g_prefix_error(error, "%s: ", __func__);
        return FALSE;
    }

    /* Stored Procedure aufrufen */
    const char *query = "CALL lock_node(?, ?, ?, ?)";

    if (mysql_stmt_prepare(stmt, query, strlen(query))) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_prepare failed: %s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        mysql_stmt_close(stmt);
        return FALSE;
    }

    MYSQL_BIND bind[4];
    memset(bind, 0, sizeof(bind));

    unsigned long user_id_length = strlen(user_id);
    unsigned long reason_length = reason ? strlen(reason) : 0;
    my_bool reason_is_null = (reason == NULL);
    my_bool timeout_is_null = (timeout_minutes == 0);

    /* Parameter 1: node_id */
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (void*)&node_id;

    /* Parameter 2: user_id */
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)user_id;
    bind[1].buffer_length = user_id_length;
    bind[1].length = &user_id_length;

    /* Parameter 3: timeout_minutes (kann NULL sein) */
    bind[2].buffer_type = MYSQL_TYPE_LONG;
    bind[2].buffer = (void*)&timeout_minutes;
    bind[2].is_null = &timeout_is_null;

    /* Parameter 4: reason (kann NULL sein) */
    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)reason;
    bind[3].buffer_length = reason_length;
    bind[3].length = &reason_length;
    bind[3].is_null = &reason_is_null;

    if (mysql_stmt_bind_param(stmt, bind)) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_bind_param failed: %s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        mysql_stmt_close(stmt);
        return FALSE;
    }

    if (mysql_stmt_execute(stmt)) {
        /* Fehler könnte "bereits gelockt" sein */
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "%s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        mysql_stmt_close(stmt);
        return FALSE;
    }

    mysql_stmt_close(stmt);
    return TRUE;
}

/**
 * sond_graph_db_unlock_node:
 */
gboolean sond_graph_db_unlock_node(MYSQL *conn,
                                    gint64 node_id,
                                    const gchar *user_id,
                                    GError **error) {
    g_return_val_if_fail(conn != NULL, FALSE);
    g_return_val_if_fail(user_id != NULL, FALSE);

    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (stmt == NULL) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_init failed: %s", mysql_error(conn));
        g_prefix_error(error, "%s: ", __func__);
        return FALSE;
    }

    const char *query = "CALL unlock_node(?, ?)";

    if (mysql_stmt_prepare(stmt, query, strlen(query))) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_prepare failed: %s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        mysql_stmt_close(stmt);
        return FALSE;
    }

    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));

    unsigned long user_id_length = strlen(user_id);

    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = (void*)&node_id;

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)user_id;
    bind[1].buffer_length = user_id_length;
    bind[1].length = &user_id_length;

    if (mysql_stmt_bind_param(stmt, bind)) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_bind_param failed: %s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        mysql_stmt_close(stmt);
        return FALSE;
    }

    if (mysql_stmt_execute(stmt)) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "%s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        mysql_stmt_close(stmt);
        return FALSE;
    }

    mysql_stmt_close(stmt);
    return TRUE;
}

/**
 * sond_graph_db_force_unlock_node:
 */
gboolean sond_graph_db_force_unlock_node(MYSQL *conn,
                                          gint64 node_id,
                                          GError **error) {
    g_return_val_if_fail(conn != NULL, FALSE);

    if (node_id == 0) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Cannot unlock node with ID 0");
        g_prefix_error(error, "%s: ", __func__);
        return FALSE;
    }

    /* Direkt aus Tabelle löschen - ohne User-Check */
    gchar *query = g_strdup_printf("DELETE FROM node_locks WHERE node_id = %ld", node_id);

    if (mysql_query(conn, query)) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Force unlock failed: %s", mysql_error(conn));
        g_prefix_error(error, "%s: ", __func__);
        g_free(query);
        return FALSE;
    }

    g_free(query);

    /* Prüfen ob ein Lock entfernt wurde */
    my_ulonglong affected = mysql_affected_rows(conn);
    if (affected == 0) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Node %ld was not locked", node_id);
        g_prefix_error(error, "%s: ", __func__);
        return FALSE;
    }

    return TRUE;
}

/**
 * sond_graph_db_check_lock:
 */
gboolean sond_graph_db_check_lock(MYSQL *conn,
                                   gint64 node_id,
                                   gchar **locked_by,
                                   GError **error) {
    g_return_val_if_fail(conn != NULL, FALSE);

    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (stmt == NULL) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_init failed: %s", mysql_error(conn));
        g_prefix_error(error, "%s: ", __func__);
        return FALSE;
    }

    const char *query = "SELECT locked_by FROM node_locks WHERE node_id = ?";

    if (mysql_stmt_prepare(stmt, query, strlen(query))) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_prepare failed: %s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        mysql_stmt_close(stmt);
        return FALSE;
    }

    MYSQL_BIND bind_param[1];
    memset(bind_param, 0, sizeof(bind_param));

    bind_param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_param[0].buffer = (void*)&node_id;

    if (mysql_stmt_bind_param(stmt, bind_param)) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_bind_param failed: %s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        mysql_stmt_close(stmt);
        return FALSE;
    }

    if (mysql_stmt_execute(stmt)) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_execute failed: %s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        mysql_stmt_close(stmt);
        return FALSE;
    }

    /* Ergebnis holen */
    char user_buffer[101] = {0};
    unsigned long user_length;

    MYSQL_BIND bind_result[1];
    memset(bind_result, 0, sizeof(bind_result));

    bind_result[0].buffer_type = MYSQL_TYPE_STRING;
    bind_result[0].buffer = user_buffer;
    bind_result[0].buffer_length = sizeof(user_buffer);
    bind_result[0].length = &user_length;

    if (mysql_stmt_bind_result(stmt, bind_result)) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_bind_result failed: %s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        mysql_stmt_close(stmt);
        return FALSE;
    }

    int fetch_result = mysql_stmt_fetch(stmt);

    gboolean is_locked = FALSE;

    if (fetch_result == 0) {
        /* Lock gefunden */
        is_locked = TRUE;
        if (locked_by != NULL) {
            *locked_by = g_strdup(user_buffer);
        }
    } else {
        /* Kein Lock */
        if (locked_by != NULL) {
            *locked_by = NULL;
        }
    }

    mysql_stmt_close(stmt);

    return is_locked;
}

/**
 * sond_graph_db_unlock_all_by_user:
 */
gint sond_graph_db_unlock_all_by_user(MYSQL *conn,
                                       const gchar *user_id,
                                       GError **error) {
    g_return_val_if_fail(conn != NULL, -1);
    g_return_val_if_fail(user_id != NULL, -1);

    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (stmt == NULL) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_init failed: %s", mysql_error(conn));
        g_prefix_error(error, "%s: ", __func__);
        return -1;
    }

    const char *query = "CALL unlock_all_by_user(?)";

    if (mysql_stmt_prepare(stmt, query, strlen(query))) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_prepare failed: %s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        mysql_stmt_close(stmt);
        return -1;
    }

    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));

    unsigned long user_id_length = strlen(user_id);

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char*)user_id;
    bind[0].buffer_length = user_id_length;
    bind[0].length = &user_id_length;

    if (mysql_stmt_bind_param(stmt, bind)) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_bind_param failed: %s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        mysql_stmt_close(stmt);
        return -1;
    }

    if (mysql_stmt_execute(stmt)) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_execute failed: %s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        mysql_stmt_close(stmt);
        return -1;
    }

    /* Ergebnis holen (unlocked_count) */
    guint unlocked_count = 0;

    MYSQL_BIND bind_result[1];
    memset(bind_result, 0, sizeof(bind_result));

    bind_result[0].buffer_type = MYSQL_TYPE_LONG;
    bind_result[0].buffer = &unlocked_count;

    if (mysql_stmt_bind_result(stmt, bind_result)) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_bind_result failed: %s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        mysql_stmt_close(stmt);
        return -1;
    }

    mysql_stmt_fetch(stmt);
    mysql_stmt_close(stmt);

    return (gint)unlocked_count;
}

/**
 * sond_graph_db_cleanup_expired_locks:
 */
gint sond_graph_db_cleanup_expired_locks(MYSQL *conn, GError **error) {
    g_return_val_if_fail(conn != NULL, -1);

    if (mysql_query(conn, "CALL cleanup_expired_locks()")) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Failed to cleanup locks: %s", mysql_error(conn));
        g_prefix_error(error, "%s: ", __func__);
        return -1;
    }

    MYSQL_RES *result = mysql_store_result(conn);
    if (result == NULL) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Failed to get result: %s", mysql_error(conn));
        g_prefix_error(error, "%s: ", __func__);
        return -1;
    }

    gint cleaned_count = 0;
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row && row[0]) {
        cleaned_count = atoi(row[0]);
    }

    mysql_free_result(result);

    return cleaned_count;
}

/**
 * Edge-Datenbankfunktionen
 *
 * Diese Funktionen in sond_graph_db.c einfügen (nach den Node-Funktionen)
 */

/* ========================================================================
 * Edge Load/Save/Delete Functions
 * ======================================================================== */

/**
 * sond_graph_db_load_edge:
 * @conn: MySQL connection
 * @edge_id: ID der zu ladenden Edge
 * @error: Error location
 *
 * Lädt eine Edge aus der Datenbank.
 *
 * Returns: (transfer full): GraphEdge oder NULL bei Fehler
 */
GraphEdge* sond_graph_db_load_edge(MYSQL *conn, gint64 edge_id, GError **error) {
    g_return_val_if_fail(conn != NULL, NULL);

    GraphEdge *edge = NULL;
    MYSQL_STMT *stmt = NULL;
    MYSQL_BIND bind_params[1];
    MYSQL_BIND bind_results[7];

    /* Query vorbereiten */
    const char *query =
        "SELECT id, source_id, target_id, label, properties, created_at, updated_at "
        "FROM edges WHERE id = ?";

    stmt = mysql_stmt_init(conn);
    if (stmt == NULL) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_init failed: %s", mysql_error(conn));
        g_prefix_error(error, "%s: ", __func__);
        goto cleanup;
    }

    if (mysql_stmt_prepare(stmt, query, strlen(query))) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_prepare failed: %s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        goto cleanup;
    }

    /* Parameter binden */
    memset(bind_params, 0, sizeof(bind_params));
    bind_params[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_params[0].buffer = &edge_id;

    if (mysql_stmt_bind_param(stmt, bind_params)) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_bind_param failed: %s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        goto cleanup;
    }

    /* Ausführen */
    if (mysql_stmt_execute(stmt)) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_execute failed: %s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        goto cleanup;
    }

    /* Ergebnis-Buffer */
    gint64 id, source_id, target_id;
    char label[51] = {0};
    unsigned long label_length;
    char *properties = g_malloc(65536);
    unsigned long properties_length;
    char created_at[20] = {0};
    unsigned long created_length;
    my_bool created_is_null;
    char updated_at[20] = {0};
    unsigned long updated_length;
    my_bool updated_is_null;

    memset(bind_results, 0, sizeof(bind_results));

    bind_results[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_results[0].buffer = &id;

    bind_results[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_results[1].buffer = &source_id;

    bind_results[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_results[2].buffer = &target_id;

    bind_results[3].buffer_type = MYSQL_TYPE_STRING;
    bind_results[3].buffer = label;
    bind_results[3].buffer_length = sizeof(label);
    bind_results[3].length = &label_length;

    bind_results[4].buffer_type = MYSQL_TYPE_STRING;
    bind_results[4].buffer = properties;
    bind_results[4].buffer_length = 65536;
    bind_results[4].length = &properties_length;

    bind_results[5].buffer_type = MYSQL_TYPE_STRING;
    bind_results[5].buffer = created_at;
    bind_results[5].buffer_length = sizeof(created_at);
    bind_results[5].length = &created_length;
    bind_results[5].is_null = &created_is_null;

    bind_results[6].buffer_type = MYSQL_TYPE_STRING;
    bind_results[6].buffer = updated_at;
    bind_results[6].buffer_length = sizeof(updated_at);
    bind_results[6].length = &updated_length;
    bind_results[6].is_null = &updated_is_null;

    if (mysql_stmt_bind_result(stmt, bind_results)) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_bind_result failed: %s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        g_free(properties);
        goto cleanup;
    }

    /* Fetch */
    int fetch_result = mysql_stmt_fetch(stmt);
    if (fetch_result == MYSQL_NO_DATA) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Edge with ID %ld not found", edge_id);
        g_prefix_error(error, "%s: ", __func__);
        g_free(properties);
        goto cleanup;
    } else if (fetch_result != 0) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_fetch failed: %s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        g_free(properties);
        goto cleanup;
    }

    /* Edge erstellen */
    edge = graph_edge_new();
    graph_edge_set_id(edge, id);
    graph_edge_set_source_id(edge, source_id);
    graph_edge_set_target_id(edge, target_id);
    graph_edge_set_label(edge, label);

    /* Properties laden */
    GError *json_error = NULL;
    if (!graph_edge_properties_from_json(edge, properties, &json_error)) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Failed to parse properties JSON: %s",
                   json_error ? json_error->message : "unknown");
        g_prefix_error(error, "%s: ", __func__);
        g_clear_error(&json_error);
        g_object_unref(edge);
        edge = NULL;
        g_free(properties);
        goto cleanup;
    }
    g_free(properties);

    /* Timestamps */
    if (!created_is_null) {
        GDateTime *dt = parse_mysql_timestamp(created_at);
        if (dt) {
            graph_edge_set_created_at(edge, dt);
            g_date_time_unref(dt);
        }
    }

    if (!updated_is_null) {
        GDateTime *dt = parse_mysql_timestamp(updated_at);
        if (dt) {
            graph_edge_set_updated_at(edge, dt);
            g_date_time_unref(dt);
        }
    }

cleanup:
    if (stmt) {
        mysql_stmt_close(stmt);
    }

    return edge;
}

/**
 * sond_graph_db_save_edge:
 * @conn: MySQL connection
 * @edge: Edge zum Speichern
 * @error: Error location
 *
 * Speichert eine Edge in der Datenbank.
 * Wenn edge->id == 0, wird INSERT ausgeführt und die neue ID gesetzt.
 * Sonst wird UPDATE ausgeführt.
 *
 * Returns: TRUE bei Erfolg
 */
gboolean sond_graph_db_save_edge(MYSQL *conn, GraphEdge *edge, GError **error) {
    g_return_val_if_fail(conn != NULL, FALSE);
    g_return_val_if_fail(GRAPH_IS_EDGE(edge), FALSE);

    MYSQL_STMT *stmt = NULL;
    gboolean success = FALSE;

    gint64 id = graph_edge_get_id(edge);
    gint64 source_id = graph_edge_get_source_id(edge);
    gint64 target_id = graph_edge_get_target_id(edge);
    const gchar *label = graph_edge_get_label(edge);
    gchar *properties_json = graph_edge_properties_to_json(edge);

    /* Validierung */
    if (label == NULL) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Edge label cannot be NULL");
        g_prefix_error(error, "%s: ", __func__);
        g_free(properties_json);
        return FALSE;
    }

    if (source_id == 0 || target_id == 0) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Edge source_id and target_id must be set");
        g_prefix_error(error, "%s: ", __func__);
        g_free(properties_json);
        return FALSE;
    }

    const char *query;

    if (id == 0) {
        /* INSERT - neue Edge */
        query = "INSERT INTO edges (source_id, target_id, label, properties) VALUES (?, ?, ?, ?)";
    } else {
        /* UPDATE - bestehende Edge */
        query = "UPDATE edges SET source_id = ?, target_id = ?, label = ?, properties = ? WHERE id = ?";
    }

    stmt = mysql_stmt_init(conn);
    if (stmt == NULL) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_init failed: %s", mysql_error(conn));
        g_prefix_error(error, "%s: ", __func__);
        g_free(properties_json);
        return FALSE;
    }

    if (mysql_stmt_prepare(stmt, query, strlen(query))) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_prepare failed: %s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        g_free(properties_json);
        mysql_stmt_close(stmt);
        return FALSE;
    }

    MYSQL_BIND bind[5];
    memset(bind, 0, sizeof(bind));

    unsigned long label_length = strlen(label);
    unsigned long props_length = strlen(properties_json);

    /* Parameter 1: source_id */
    bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[0].buffer = &source_id;

    /* Parameter 2: target_id */
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = &target_id;

    /* Parameter 3: label */
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)label;
    bind[2].buffer_length = label_length;
    bind[2].length = &label_length;

    /* Parameter 4: properties */
    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = properties_json;
    bind[3].buffer_length = props_length;
    bind[3].length = &props_length;

    /* Parameter 5: id (nur bei UPDATE) */
    if (id > 0) {
        bind[4].buffer_type = MYSQL_TYPE_LONGLONG;
        bind[4].buffer = &id;
    }

    if (mysql_stmt_bind_param(stmt, bind)) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_bind_param failed: %s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        g_free(properties_json);
        mysql_stmt_close(stmt);
        return FALSE;
    }

    if (mysql_stmt_execute(stmt)) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_execute failed: %s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        g_free(properties_json);
        mysql_stmt_close(stmt);
        return FALSE;
    }

    /* Bei INSERT: Neue ID setzen */
    if (id == 0) {
        gint64 new_id = mysql_stmt_insert_id(stmt);
        graph_edge_set_id(edge, new_id);
    }

    success = TRUE;

    g_free(properties_json);
    mysql_stmt_close(stmt);

    return success;
}

/**
 * sond_graph_db_delete_edge:
 * @conn: MySQL connection
 * @edge_id: ID der zu löschenden Edge
 * @error: Error location
 *
 * Löscht eine Edge aus der Datenbank.
 *
 * Returns: TRUE bei Erfolg
 */
gboolean sond_graph_db_delete_edge(MYSQL *conn, gint64 edge_id, GError **error) {
    g_return_val_if_fail(conn != NULL, FALSE);

    if (edge_id == 0) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Cannot delete edge with ID 0");
        g_prefix_error(error, "%s: ", __func__);
        return FALSE;
    }

    gchar *query = g_strdup_printf("DELETE FROM edges WHERE id = %ld", edge_id);

    if (mysql_query(conn, query)) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Delete failed: %s", mysql_error(conn));
        g_prefix_error(error, "%s: ", __func__);
        g_free(query);
        return FALSE;
    }

    g_free(query);

    /* Prüfen ob wirklich gelöscht wurde */
    my_ulonglong affected = mysql_affected_rows(conn);
    if (affected == 0) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Edge with ID %ld not found", edge_id);
        g_prefix_error(error, "%s: ", __func__);
        return FALSE;
    }

    return TRUE;
}

/**
 * sond_graph_db_delete_edges_between:
 * @conn: MySQL connection
 * @source_id: Source Node ID
 * @target_id: Target Node ID
 * @label: Optional: nur Edges mit diesem Label löschen (NULL = alle)
 * @error: Error location
 *
 * Löscht alle Edges zwischen zwei Nodes (optional gefiltert nach Label).
 *
 * Returns: Anzahl der gelöschten Edges, oder -1 bei Fehler
 */
gint sond_graph_db_delete_edges_between(MYSQL *conn,
                                         gint64 source_id,
                                         gint64 target_id,
                                         const gchar *label,
                                         GError **error) {
    g_return_val_if_fail(conn != NULL, -1);

    gchar *query;
    if (label != NULL) {
        char *escaped_label = g_malloc(strlen(label) * 2 + 1);
        mysql_real_escape_string(conn, escaped_label, label, strlen(label));

        query = g_strdup_printf(
            "DELETE FROM edges WHERE source_id = %ld AND target_id = %ld AND label = '%s'",
            source_id, target_id, escaped_label);

        g_free(escaped_label);
    } else {
        query = g_strdup_printf(
            "DELETE FROM edges WHERE source_id = %ld AND target_id = %ld",
            source_id, target_id);
    }

    if (mysql_query(conn, query)) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Delete failed: %s", mysql_error(conn));
        g_prefix_error(error, "%s: ", __func__);
        g_free(query);
        return -1;
    }

    g_free(query);

    return (gint)mysql_affected_rows(conn);
}

/**
 * sond_graph_db_get_edges_from_node:
 * @conn: MySQL connection
 * @source_id: Source Node ID
 * @label: Optional: nur Edges mit diesem Label (NULL = alle)
 * @error: Error location
 *
 * Lädt alle ausgehenden Edges eines Nodes.
 *
 * Returns: (transfer full): Array von GraphEdge, oder NULL bei Fehler
 */
GPtrArray* sond_graph_db_get_edges_from_node(MYSQL *conn,
                                               gint64 source_id,
                                               const gchar *label,
                                               GError **error) {
    g_return_val_if_fail(conn != NULL, NULL);

    gchar *query;
    if (label != NULL) {
        char *escaped_label = g_malloc(strlen(label) * 2 + 1);
        mysql_real_escape_string(conn, escaped_label, label, strlen(label));

        query = g_strdup_printf(
            "SELECT id FROM edges WHERE source_id = %ld AND label = '%s'",
            source_id, escaped_label);

        g_free(escaped_label);
    } else {
        query = g_strdup_printf(
            "SELECT id FROM edges WHERE source_id = %ld",
            source_id);
    }

    if (mysql_query(conn, query)) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Query failed: %s", mysql_error(conn));
        g_prefix_error(error, "%s: ", __func__);
        g_free(query);
        return NULL;
    }

    g_free(query);

    MYSQL_RES *result = mysql_store_result(conn);
    if (result == NULL) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Failed to get result: %s", mysql_error(conn));
        g_prefix_error(error, "%s: ", __func__);
        return NULL;
    }

    GPtrArray *edges = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result))) {
        gint64 edge_id = g_ascii_strtoll(row[0], NULL, 10);

        GError *load_error = NULL;
        GraphEdge *edge = sond_graph_db_load_edge(conn, edge_id, &load_error);
        if (edge != NULL) {
            g_ptr_array_add(edges, edge);
        } else {
            g_warning("Failed to load edge %ld: %s", edge_id,
                     load_error ? load_error->message : "unknown");
            g_clear_error(&load_error);
        }
    }

    mysql_free_result(result);

    return edges;
}

/*
 * Kompilierung:
 * gcc -c sond_graph_db.c $(mysql_config --cflags) $(pkg-config --cflags glib-2.0 gobject-2.0 json-glib-1.0)
 */
