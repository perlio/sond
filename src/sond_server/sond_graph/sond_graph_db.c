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
 * @brief Implementation der Graph-Datenbankfunktionen mit GPtrArray-Property-API
 */

#include "sond_graph_db.h"
#include "sond_graph_node.h"
#include "sond_graph_edge.h"
#include "sond_graph_property.h"

#include "../../sond_log_and_error.h"

#include <json-glib/json-glib.h>
#include <string.h>
#include <stdio.h>

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

    LOG_INFO("=== Graph Database Setup ===\n\n");

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

    LOG_INFO("\n✓ Graph database setup completed successfully!\n");

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
    gchar *properties = NULL;  /* Dynamisch allokiert */

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
    bind_params[0].buffer = (void*)&node_id;

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

    /* Ergebnis-Buffer - DYNAMISCH für Properties */
    gint64 id;
    gchar *label = g_malloc(256);  /* Größerer Buffer */
    unsigned long label_length;
    properties = g_malloc(1048576);  /* 1MB für Properties (statt 65KB) */
    unsigned long properties_length;
    gchar created_at[32] = {0};  /* Größerer Buffer für Timestamps */
    unsigned long created_length;
    my_bool created_is_null;
    gchar updated_at[32] = {0};
    unsigned long updated_length;
    my_bool updated_is_null;

    memset(bind_results, 0, sizeof(bind_results));

    bind_results[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_results[0].buffer = &id;

    bind_results[1].buffer_type = MYSQL_TYPE_STRING;
    bind_results[1].buffer = label;
    bind_results[1].buffer_length = 256;
    bind_results[1].length = &label_length;

    bind_results[2].buffer_type = MYSQL_TYPE_STRING;
    bind_results[2].buffer = properties;
    bind_results[2].buffer_length = 1048576;
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
        goto cleanup;
    }

    /* Fetch */
    int fetch_result = mysql_stmt_fetch(stmt);
    if (fetch_result == MYSQL_NO_DATA) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Node with ID %" G_GINT64_FORMAT " not found", node_id);
        g_prefix_error(error, "%s: ", __func__);
        goto cleanup;
    } else if (fetch_result != 0 && fetch_result != MYSQL_DATA_TRUNCATED) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "mysql_stmt_fetch failed: %s", mysql_stmt_error(stmt));
        g_prefix_error(error, "%s: ", __func__);
        goto cleanup;
    }

    /* Warnung bei Truncation */
    if (fetch_result == MYSQL_DATA_TRUNCATED) {
        g_warning("Data truncated for node %" G_GINT64_FORMAT, node_id);
    }

    /* Node erstellen */
    node = sond_graph_node_new();
    sond_graph_node_set_id(node, id);
    sond_graph_node_set_label(node, label);

    /* Properties laden - NEUE API mit GPtrArray */
    GError *json_error = NULL;
    GPtrArray *props = sond_graph_property_list_from_json(properties, &json_error);
    if (props == NULL && json_error != NULL) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Failed to parse properties JSON: %s",
                   json_error->message);
        g_prefix_error(error, "%s: ", __func__);
        g_clear_error(&json_error);
        g_object_unref(node);
        node = NULL;
        goto cleanup;
    }

    if (props != NULL) {
        sond_graph_node_set_properties(node, props);
        g_ptr_array_unref(props);
    }

    /* Timestamps */
    if (!created_is_null) {
        GDateTime *dt = parse_mysql_timestamp(created_at);
        if (dt) {
            sond_graph_node_set_created_at(node, dt);
            g_date_time_unref(dt);
        }
    }

    if (!updated_is_null) {
        GDateTime *dt = parse_mysql_timestamp(updated_at);
        if (dt) {
            sond_graph_node_set_updated_at(node, dt);
            g_date_time_unref(dt);
        }
    }

    mysql_stmt_close(stmt);
    stmt = NULL;

    /* Ausgehende Edges laden - MIT Properties! */
    const char *edges_query =
        "SELECT e.id, e.label, e.target_id, e.properties, e.created_at, e.updated_at "
        "FROM edges e WHERE e.source_id = ?";

    stmt = mysql_stmt_init(conn);
    if (mysql_stmt_prepare(stmt, edges_query, strlen(edges_query))) {
        /* Fehler beim Laden der Edges - Node trotzdem zurückgeben */
        g_warning("Failed to load edges: %s", mysql_stmt_error(stmt));
        goto cleanup;
    }

    memset(bind_params, 0, sizeof(bind_params));
    bind_params[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_params[0].buffer = (void*)&node_id;

    mysql_stmt_bind_param(stmt, bind_params);
    mysql_stmt_execute(stmt);

    /* Edge-Ergebnis mit Properties */
    gint64 edge_id, target_id;
    gchar *edge_label = g_malloc(256);
    unsigned long edge_label_length;
    gchar *edge_properties = g_malloc(1048576);  /* 1MB für Properties */
    unsigned long edge_properties_length;
    gchar edge_created_at[32] = {0};
    unsigned long edge_created_length;
    my_bool edge_created_is_null;
    gchar edge_updated_at[32] = {0};
    unsigned long edge_updated_length;
    my_bool edge_updated_is_null;

    MYSQL_BIND edge_bind[6];
    memset(edge_bind, 0, sizeof(edge_bind));

    edge_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    edge_bind[0].buffer = &edge_id;

    edge_bind[1].buffer_type = MYSQL_TYPE_STRING;
    edge_bind[1].buffer = edge_label;
    edge_bind[1].buffer_length = 256;
    edge_bind[1].length = &edge_label_length;

    edge_bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    edge_bind[2].buffer = &target_id;

    edge_bind[3].buffer_type = MYSQL_TYPE_STRING;
    edge_bind[3].buffer = edge_properties;
    edge_bind[3].buffer_length = 1048576;
    edge_bind[3].length = &edge_properties_length;

    edge_bind[4].buffer_type = MYSQL_TYPE_STRING;
    edge_bind[4].buffer = edge_created_at;
    edge_bind[4].buffer_length = sizeof(edge_created_at);
    edge_bind[4].length = &edge_created_length;
    edge_bind[4].is_null = &edge_created_is_null;

    edge_bind[5].buffer_type = MYSQL_TYPE_STRING;
    edge_bind[5].buffer = edge_updated_at;
    edge_bind[5].buffer_length = sizeof(edge_updated_at);
    edge_bind[5].length = &edge_updated_length;
    edge_bind[5].is_null = &edge_updated_is_null;

    mysql_stmt_bind_result(stmt, edge_bind);

    while (mysql_stmt_fetch(stmt) == 0) {
        /* Vollständige Edge mit Properties erstellen */
        SondGraphEdge *edge = sond_graph_edge_new();
        sond_graph_edge_set_id(edge, edge_id);
        sond_graph_edge_set_label(edge, edge_label);
        sond_graph_edge_set_source_id(edge, node_id);
        sond_graph_edge_set_target_id(edge, target_id);

        /* Edge-Properties laden */
        GError *edge_json_error = NULL;
        GPtrArray *edge_props = sond_graph_property_list_from_json(edge_properties, &edge_json_error);
        if (edge_props == NULL && edge_json_error != NULL) {
            g_warning("Failed to parse edge properties for edge %" G_GINT64_FORMAT " %s",
                     edge_id, edge_json_error->message);
            g_clear_error(&edge_json_error);
        }

        if (edge_props != NULL) {
            sond_graph_edge_set_properties(edge, edge_props);
            g_ptr_array_unref(edge_props);
        }

        /* Edge Timestamps */
        if (!edge_created_is_null) {
            GDateTime *dt = parse_mysql_timestamp(edge_created_at);
            if (dt) {
                sond_graph_edge_set_created_at(edge, dt);
                g_date_time_unref(dt);
            }
        }

        if (!edge_updated_is_null) {
            GDateTime *dt = parse_mysql_timestamp(edge_updated_at);
            if (dt) {
                sond_graph_edge_set_updated_at(edge, dt);
                g_date_time_unref(dt);
            }
        }

        sond_graph_node_add_outgoing_edge(node, edge);
        g_object_unref(edge);  // Ownership wurde an Node übergeben
    }

    g_free(edge_label);
    g_free(edge_properties);

cleanup:
    g_free(label);
    g_free(properties);

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
 * escape_sql_like_pattern:
 *
 * FIXED: Escaped zuerst SQL LIKE Zeichen, dann konvertiert Wildcards.
 */
static gchar* escape_sql_like_pattern(const gchar *value) {
    GString *result = g_string_new("");

    for (const gchar *p = value; *p != '\0'; p++) {
        if (*p == '%' || *p == '_') {
            /* Escape SQL LIKE Sonderzeichen */
            g_string_append_c(result, '\\');
            g_string_append_c(result, *p);
        } else if (*p == '*') {
            /* Wildcard * -> SQL % */
            g_string_append_c(result, '%');
        } else if (*p == '?') {
            /* Wildcard ? -> SQL _ */
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
        mysql_real_escape_string(conn, escaped_key, filter->key, strlen(filter->key));

        gboolean use_like = has_wildcards(filter->value);

        if (use_like) {
            /* FIXED: Proper escaping für LIKE Pattern */
            gchar *sql_pattern = escape_sql_like_pattern(filter->value);
            char *escaped_pattern = g_malloc(strlen(sql_pattern) * 2 + 1);
            mysql_real_escape_string(conn, escaped_pattern, sql_pattern, strlen(sql_pattern));

            if (filter->path != NULL) {
                char *escaped_path = g_malloc(strlen(filter->path) * 2 + 1);
                mysql_real_escape_string(conn, escaped_path, filter->path, strlen(filter->path));

                g_string_append_printf(query,
                    " AND EXISTS ("
                    "   SELECT 1 FROM JSON_TABLE(%s.properties, '$[*]' COLUMNS("
                    "     prop_key VARCHAR(255) PATH '$[0]',"
                    "     nested_val JSON PATH '$[1].%s'"
                    "   )) AS jt"
                    "   WHERE jt.prop_key = '%s'"
                    "   AND JSON_UNQUOTE(jt.nested_val) LIKE '%s' ESCAPE '\\\\'"
                    " )",
                    table_alias, escaped_path, escaped_key, escaped_pattern);

                g_free(escaped_path);
            } else {
                g_string_append_printf(query,
                    " AND EXISTS ("
                    "   SELECT 1 FROM JSON_TABLE(%s.properties, '$[*]' COLUMNS("
                    "     prop_key VARCHAR(255) PATH '$[0]',"
                    "     prop_value VARCHAR(1000) PATH '$[1][0]'"
                    "   )) AS jt"
                    "   WHERE jt.prop_key = '%s'"
                    "   AND jt.prop_value LIKE '%s' ESCAPE '\\\\'"
                    " )",
                    table_alias, escaped_key, escaped_pattern);
            }

            g_free(sql_pattern);
            g_free(escaped_pattern);
        } else {
            char *escaped_value = g_malloc(strlen(filter->value) * 2 + 1);
            mysql_real_escape_string(conn, escaped_value, filter->value, strlen(filter->value));

            if (filter->path != NULL) {
                char *escaped_path = g_malloc(strlen(filter->path) * 2 + 1);
                mysql_real_escape_string(conn, escaped_path, filter->path, strlen(filter->path));

                /* JSON Format angepasst an neue Property-API: ["key", ["val"], [...]] */
                g_string_append_printf(query,
                    " AND JSON_CONTAINS(%s.properties,"
                    "   JSON_ARRAY('%s', JSON_ARRAY('%s')),"
                    "   '$')",
                    table_alias, escaped_key, escaped_value);

                g_free(escaped_path);
            } else {
                g_string_append_printf(query,
                    " AND JSON_CONTAINS(%s.properties,"
                    "   JSON_ARRAY('%s', JSON_ARRAY('%s')),"
                    "   '$')",
                    table_alias, escaped_key, escaped_value);
            }

            g_free(escaped_value);
        }

        g_free(escaped_key);
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
            g_warning("Failed to load node %" G_GINT64_FORMAT " %s", node_id,
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
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(node), FALSE);

    MYSQL_STMT *stmt = NULL;
    gboolean success = FALSE;

    gint64 id = sond_graph_node_get_id(node);
    const gchar *label = sond_graph_node_get_label(node);

    /* Properties mit NEUER API serialisieren */
    GPtrArray *props = sond_graph_node_get_properties(node);
    gchar *properties_json = sond_graph_property_list_to_json(props);

    if (label == NULL) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Node label cannot be NULL");
        g_prefix_error(error, "%s: ", __func__);
        g_free(properties_json);
        return FALSE;
    }

    const char *query;

    if (id == 0) {
        /* INSERT - neuer Node */
        query = "INSERT INTO nodes (label, properties) VALUES (?, ?)";
    } else {
        /* UPDATE - bestehender Node */
        query = "UPDATE nodes SET label = ?, properties = ? WHERE id = ?";
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
        bind[2].buffer = (void*)&id;
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
        sond_graph_node_set_id(node, new_id);
    }

    success = TRUE;

    g_free(properties_json);
    mysql_stmt_close(stmt);

    return success;
}

/**
 * sond_graph_db_save_node_with_edges:
 */
gboolean sond_graph_db_save_node_with_edges(MYSQL *conn, SondGraphNode *node, GError **error) {
    g_return_val_if_fail(conn != NULL, FALSE);
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(node), FALSE);

    GError *tmp_error = NULL;

    /* Transaction starten */
    if (mysql_query(conn, "START TRANSACTION")) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Failed to start transaction: %s", mysql_error(conn));
        g_prefix_error(error, "%s: ", __func__);
        return FALSE;
    }

    /* Node speichern */
    if (!sond_graph_db_save_node(conn, node, &tmp_error)) {
        g_propagate_error(error, tmp_error);
        mysql_query(conn, "ROLLBACK");
        return FALSE;
    }

    /* Node-ID für neue Edges setzen */
    gint64 node_id = sond_graph_node_get_id(node);
    if (node_id == 0) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Node has no ID after save");
        g_prefix_error(error, "%s: ", __func__);
        mysql_query(conn, "ROLLBACK");
        return FALSE;
    }

    /* Alle ausgehenden Edges speichern */
    GPtrArray *edges = sond_graph_node_get_outgoing_edges(node);
    if (edges != NULL) {
        for (guint i = 0; i < edges->len; i++) {
            SondGraphEdge *edge = g_ptr_array_index(edges, i);

            /* Source-ID setzen falls noch nicht gesetzt */
            if (sond_graph_edge_get_source_id(edge) == 0) {
                sond_graph_edge_set_source_id(edge, node_id);
            }

            /* Edge speichern */
            if (!sond_graph_db_save_edge(conn, edge, &tmp_error)) {
                g_prefix_error(&tmp_error, "Failed to save edge %u/%u: ", i + 1, edges->len);
                g_propagate_error(error, tmp_error);
                mysql_query(conn, "ROLLBACK");
                return FALSE;
            }
        }
    }

    /* Transaction committen */
    if (mysql_query(conn, "COMMIT")) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Failed to commit transaction: %s", mysql_error(conn));
        g_prefix_error(error, "%s: ", __func__);
        mysql_query(conn, "ROLLBACK");
        return FALSE;
    }

    return TRUE;
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

    gchar *query = g_strdup_printf("DELETE FROM nodes WHERE id = %" G_GUINT64_FORMAT, node_id);

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
                   "Node with ID %" G_GINT64_FORMAT " not found", node_id);
        g_prefix_error(error, "%s: ", __func__);
        return FALSE;
    }

    return TRUE;
}

/* ========================================================================
 * Edge Operations
 * ======================================================================== */

/**
 * sond_graph_db_save_edge:
 */
gboolean sond_graph_db_save_edge(MYSQL *conn, SondGraphEdge *edge, GError **error) {
    g_return_val_if_fail(conn != NULL, FALSE);
    g_return_val_if_fail(SOND_IS_GRAPH_EDGE(edge), FALSE);

    MYSQL_STMT *stmt = NULL;
    gboolean success = FALSE;

    gint64 id = sond_graph_edge_get_id(edge);
    gint64 source_id = sond_graph_edge_get_source_id(edge);
    gint64 target_id = sond_graph_edge_get_target_id(edge);
    const gchar *label = sond_graph_edge_get_label(edge);

    /* Validierung */
    if (source_id == 0 || target_id == 0) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Edge source_id and target_id cannot be 0");
        g_prefix_error(error, "%s: ", __func__);
        return FALSE;
    }

    if (label == NULL) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Edge label cannot be NULL");
        g_prefix_error(error, "%s: ", __func__);
        return FALSE;
    }

    /* Properties serialisieren */
    GPtrArray *props = sond_graph_edge_get_properties(edge);
    gchar *properties_json = sond_graph_property_list_to_json(props);

    const char *query;

    if (id == 0) {
        /* INSERT */
        query = "INSERT INTO edges (source_id, target_id, label, properties) VALUES (?, ?, ?, ?)";
    } else {
        /* UPDATE */
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
    bind[0].buffer = (void*)&source_id;

    /* Parameter 2: target_id */
    bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[1].buffer = (void*)&target_id;

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
        bind[4].buffer = (void*)&id;
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
        sond_graph_edge_set_id(edge, new_id);
    }

    success = TRUE;

    g_free(properties_json);
    mysql_stmt_close(stmt);

    return success;
}

/**
 * sond_graph_db_delete_edge:
 */
gboolean sond_graph_db_delete_edge(MYSQL *conn, gint64 edge_id, GError **error) {
    g_return_val_if_fail(conn != NULL, FALSE);

    if (edge_id == 0) {
        g_set_error(error, SOND_GRAPH_DB_ERROR, SOND_GRAPH_DB_ERROR_QUERY,
                   "Cannot delete edge with ID 0");
        g_prefix_error(error, "%s: ", __func__);
        return FALSE;
    }

    gchar *query = g_strdup_printf("DELETE FROM edges WHERE id = %" G_GINT64_FORMAT, edge_id);

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
                   "Edge with ID %" G_GINT64_FORMAT " not found", edge_id);
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
    gchar *query = g_strdup_printf("DELETE FROM node_locks WHERE node_id = %" G_GINT64_FORMAT, node_id);

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
                   "Node %" G_GINT64_FORMAT " was not locked", node_id);
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
    char user_buffer[256] = {0};  /* Größerer Buffer */
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
 * @file sond_graph_db_search_json.c
 * @brief JSON-Serialisierung für SearchCriteria (zu sond_graph_db.c hinzufügen)
 */

/* ========================================================================
 * SearchCriteria → JSON
 * ======================================================================== */

/**
 * property_filter_to_json:
 */
static void property_filter_to_json(SondPropertyFilter *filter, JsonBuilder *builder) {
    json_builder_begin_object(builder);

    json_builder_set_member_name(builder, "key");
    json_builder_add_string_value(builder, filter->key);

    json_builder_set_member_name(builder, "value");
    json_builder_add_string_value(builder, filter->value);

    if (filter->path) {
        json_builder_set_member_name(builder, "path");
        json_builder_add_string_value(builder, filter->path);
    }

    json_builder_end_object(builder);
}

/**
 * edge_filter_to_json:
 */
static void edge_filter_to_json(SondEdgeFilter *filter, JsonBuilder *builder);

/**
 * search_criteria_to_json_internal:
 */
static void search_criteria_to_json_internal(SondGraphNodeSearchCriteria *criteria,
                                              JsonBuilder *builder) {
    json_builder_begin_object(builder);

    /* Label */
    if (criteria->label) {
        json_builder_set_member_name(builder, "label");
        json_builder_add_string_value(builder, criteria->label);
    }

    /* Property Filters */
    if (criteria->property_filters && criteria->property_filters->len > 0) {
        json_builder_set_member_name(builder, "property_filters");
        json_builder_begin_array(builder);

        for (guint i = 0; i < criteria->property_filters->len; i++) {
            SondPropertyFilter *filter = g_ptr_array_index(criteria->property_filters, i);
            property_filter_to_json(filter, builder);
        }

        json_builder_end_array(builder);
    }

    /* Edge Filters */
    if (criteria->edge_filters && criteria->edge_filters->len > 0) {
        json_builder_set_member_name(builder, "edge_filters");
        json_builder_begin_array(builder);

        for (guint i = 0; i < criteria->edge_filters->len; i++) {
            SondEdgeFilter *filter = g_ptr_array_index(criteria->edge_filters, i);
            edge_filter_to_json(filter, builder);
        }

        json_builder_end_array(builder);
    }

    /* Limit & Offset */
    if (criteria->limit > 0) {
        json_builder_set_member_name(builder, "limit");
        json_builder_add_int_value(builder, criteria->limit);
    }

    if (criteria->offset > 0) {
        json_builder_set_member_name(builder, "offset");
        json_builder_add_int_value(builder, criteria->offset);
    }

    json_builder_end_object(builder);
}

/**
 * edge_filter_to_json:
 */
static void edge_filter_to_json(SondEdgeFilter *filter, JsonBuilder *builder) {
    json_builder_begin_object(builder);

    json_builder_set_member_name(builder, "edge_label");
    json_builder_add_string_value(builder, filter->edge_label);

    /* Edge Property Filters */
    if (filter->property_filters && filter->property_filters->len > 0) {
        json_builder_set_member_name(builder, "property_filters");
        json_builder_begin_array(builder);

        for (guint i = 0; i < filter->property_filters->len; i++) {
            SondPropertyFilter *prop_filter = g_ptr_array_index(filter->property_filters, i);
            property_filter_to_json(prop_filter, builder);
        }

        json_builder_end_array(builder);
    }

    /* Target Criteria (rekursiv!) */
    if (filter->target_criteria) {
        json_builder_set_member_name(builder, "target_criteria");
        search_criteria_to_json_internal(filter->target_criteria, builder);
    }

    json_builder_end_object(builder);
}

/**
 * sond_graph_node_search_criteria_to_json:
 *
 * Serialisiert SearchCriteria zu JSON.
 */
gchar* sond_graph_node_search_criteria_to_json(SondGraphNodeSearchCriteria *criteria) {
    g_return_val_if_fail(criteria != NULL, NULL);

    JsonBuilder *builder = json_builder_new();
    search_criteria_to_json_internal(criteria, builder);

    JsonGenerator *generator = json_generator_new();
    json_generator_set_pretty(generator, TRUE);
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(generator, root);

    gchar *json = json_generator_to_data(generator, NULL);

    json_node_free(root);
    g_object_unref(generator);
    g_object_unref(builder);

    return json;
}

/* ========================================================================
 * JSON → SearchCriteria
 * ======================================================================== */

/**
 * property_filter_from_json:
 */
static SondPropertyFilter* property_filter_from_json(JsonObject *obj) {
    if (!json_object_has_member(obj, "key") || !json_object_has_member(obj, "value")) {
        return NULL;
    }

    const gchar *key = json_object_get_string_member(obj, "key");
    const gchar *value = json_object_get_string_member(obj, "value");

    if (json_object_has_member(obj, "path")) {
        const gchar *path = json_object_get_string_member(obj, "path");
        return sond_property_filter_new_with_path(key, value, path);
    } else {
        return sond_property_filter_new(key, value);
    }
}

/**
 * edge_filter_from_json:
 */
static SondEdgeFilter* edge_filter_from_json(JsonObject *obj);

/**
 * search_criteria_from_json_internal:
 */
static SondGraphNodeSearchCriteria* search_criteria_from_json_internal(JsonObject *obj) {
    SondGraphNodeSearchCriteria *criteria = sond_graph_node_search_criteria_new();

    /* Label */
    if (json_object_has_member(obj, "label")) {
        criteria->label = g_strdup(json_object_get_string_member(obj, "label"));
    }

    /* Property Filters */
    if (json_object_has_member(obj, "property_filters")) {
        JsonArray *filters = json_object_get_array_member(obj, "property_filters");
        guint n_filters = json_array_get_length(filters);

        for (guint i = 0; i < n_filters; i++) {
            JsonObject *filter_obj = json_array_get_object_element(filters, i);
            SondPropertyFilter *filter = property_filter_from_json(filter_obj);

            if (filter) {
                g_ptr_array_add(criteria->property_filters, filter);
            }
        }
    }

    /* Edge Filters */
    if (json_object_has_member(obj, "edge_filters")) {
        JsonArray *filters = json_object_get_array_member(obj, "edge_filters");
        guint n_filters = json_array_get_length(filters);

        for (guint i = 0; i < n_filters; i++) {
            JsonObject *filter_obj = json_array_get_object_element(filters, i);
            SondEdgeFilter *filter = edge_filter_from_json(filter_obj);

            if (filter) {
                g_ptr_array_add(criteria->edge_filters, filter);
            }
        }
    }

    /* Limit & Offset */
    if (json_object_has_member(obj, "limit")) {
        criteria->limit = json_object_get_int_member(obj, "limit");
    }

    if (json_object_has_member(obj, "offset")) {
        criteria->offset = json_object_get_int_member(obj, "offset");
    }

    return criteria;
}

/**
 * edge_filter_from_json:
 */
static SondEdgeFilter* edge_filter_from_json(JsonObject *obj) {
    if (!json_object_has_member(obj, "edge_label")) {
        return NULL;
    }

    const gchar *edge_label = json_object_get_string_member(obj, "edge_label");

    /* Target Criteria (kann NULL sein) */
    SondGraphNodeSearchCriteria *target_criteria = NULL;
    if (json_object_has_member(obj, "target_criteria")) {
        JsonObject *target_obj = json_object_get_object_member(obj, "target_criteria");
        target_criteria = search_criteria_from_json_internal(target_obj);
    }

    SondEdgeFilter *filter = sond_edge_filter_new(edge_label, target_criteria);

    /* Edge Property Filters */
    if (json_object_has_member(obj, "property_filters")) {
        JsonArray *prop_filters = json_object_get_array_member(obj, "property_filters");
        guint n_filters = json_array_get_length(prop_filters);

        for (guint i = 0; i < n_filters; i++) {
            JsonObject *prop_obj = json_array_get_object_element(prop_filters, i);
            SondPropertyFilter *prop_filter = property_filter_from_json(prop_obj);

            if (prop_filter) {
                g_ptr_array_add(filter->property_filters, prop_filter);
            }
        }
    }

    return filter;
}

/**
 * sond_graph_node_search_criteria_from_json:
 *
 * Deserialisiert SearchCriteria aus JSON.
 */
SondGraphNodeSearchCriteria* sond_graph_node_search_criteria_from_json(const gchar *json,
                                                                        GError **error) {
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
    SondGraphNodeSearchCriteria *criteria = search_criteria_from_json_internal(obj);

    g_object_unref(parser);

    return criteria;
}

