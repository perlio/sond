/*
 sond (sond_server.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2026 pelo america

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
 * @file sond_server.c
 * @brief Implementation des REST Servers
 */

#include "sond_server.h"
#include "sond_graph_db.h"
#include "sond_graph_node.h"
#include "sond_graph_edge.h"
#include <json-glib/json-glib.h>
#include <string.h>

struct _SondServer {
    GObject parent_instance;

    SoupServer *soup_server;
    MYSQL *db_conn;
    guint port;
    gboolean running;

    /* DB Config */
    gchar *db_host;
    gchar *db_user;
    gchar *db_password;
    gchar *db_name;
};

G_DEFINE_TYPE(SondServer, sond_server, G_TYPE_OBJECT)

/* ========================================================================
 * Helper Functions
 * ======================================================================== */

/**
 * send_json_response:
 *
 * Sendet eine JSON-Antwort mit Status-Code.
 */
static void send_json_response(SoupServerMessage *msg,
                                guint status_code,
                                const gchar *json_body) {
    soup_server_message_set_status(msg, status_code, NULL);
    soup_message_headers_set_content_type(soup_server_message_get_response_headers(msg),
                                          "application/json", NULL);

    GBytes *bytes = g_bytes_new(json_body, strlen(json_body));
    soup_message_body_append_bytes(soup_server_message_get_response_body(msg), bytes);
    g_bytes_unref(bytes);
}

/**
 * send_error_response:
 *
 * Sendet eine Fehler-Antwort im JSON-Format.
 */
static void send_error_response(SoupServerMessage *msg,
                                 guint status_code,
                                 const gchar *error_message) {
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);

    json_builder_set_member_name(builder, "error");
    json_builder_add_string_value(builder, error_message);

    json_builder_end_object(builder);

    JsonGenerator *generator = json_generator_new();
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(generator, root);

    gchar *json = json_generator_to_data(generator, NULL);
    send_json_response(msg, status_code, json);

    g_free(json);
    json_node_free(root);
    g_object_unref(generator);
    g_object_unref(builder);
}

/**
 * send_success_response:
 *
 * Sendet eine Erfolgs-Antwort.
 */
static void send_success_response(SoupServerMessage *msg,
                                   const gchar *data_json) {
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);

    json_builder_set_member_name(builder, "success");
    json_builder_add_boolean_value(builder, TRUE);

    if (data_json) {
        json_builder_set_member_name(builder, "data");

        /* Parse data JSON und einfügen */
        JsonParser *parser = json_parser_new();
        if (json_parser_load_from_data(parser, data_json, -1, NULL)) {
            JsonNode *data_node = json_parser_get_root(parser);
            json_builder_add_value(builder, json_node_copy(data_node));
        } else {
            json_builder_add_null_value(builder);
        }
        g_object_unref(parser);
    }

    json_builder_end_object(builder);

    JsonGenerator *generator = json_generator_new();
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(generator, root);

    gchar *json = json_generator_to_data(generator, NULL);
    send_json_response(msg, SOUP_STATUS_OK, json);

    g_free(json);
    json_node_free(root);
    g_object_unref(generator);
    g_object_unref(builder);
}

/* ========================================================================
 * Request Handlers
 * ======================================================================== */

/**
 * handle_node_save:
 * POST /node/save
 *
 * Body: Node als JSON
 * Response: { "success": true, "data": { Node mit ID } }
 */
static void handle_node_save(SoupServer *soup_server,
                              SoupServerMessage *msg,
                              const char *path,
                              GHashTable *query,
                              gpointer user_data) {
    SondServer *server = SOND_SERVER(user_data);

    /* Nur POST erlaubt */
    if (strcmp(soup_server_message_get_method(msg), "POST") != 0) {
        send_error_response(msg, SOUP_STATUS_METHOD_NOT_ALLOWED,
                           "Only POST allowed");
        return;
    }

    /* Body lesen */
    SoupMessageBody *body = soup_server_message_get_request_body(msg);
    if (!body->data) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST,
                           "Empty request body");
        return;
    }

    /* Node aus JSON deserialisieren */
    GError *error = NULL;
    SondGraphNode *node = sond_graph_node_from_json(body->data, &error);

    if (!node) {
        gchar *err_msg = g_strdup_printf("Invalid JSON: %s",
                                        error ? error->message : "unknown");
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST, err_msg);
        g_free(err_msg);
        g_clear_error(&error);
        return;
    }

    /* Node speichern */
    if (!sond_graph_db_save_node(server->db_conn, node, &error)) {
        gchar *err_msg = g_strdup_printf("Failed to save node: %s",
                                        error ? error->message : "unknown");
        send_error_response(msg, SOUP_STATUS_INTERNAL_SERVER_ERROR, err_msg);
        g_free(err_msg);
        g_clear_error(&error);
        g_object_unref(node);
        return;
    }

    /* Gespeicherten Node zurück serialisieren */
    gchar *response_json = sond_graph_node_to_json(node);
    send_success_response(msg, response_json);

    g_free(response_json);
    g_object_unref(node);
}

/**
 * handle_node_load:
 * GET /node/load/{id}
 *
 * Response: { "success": true, "data": { Node } }
 */
static void handle_node_load(SoupServer *soup_server,
                              SoupServerMessage *msg,
                              const char *path,
                              GHashTable *query,
                              gpointer user_data) {
    SondServer *server = SOND_SERVER(user_data);

    /* Nur GET erlaubt */
    if (strcmp(soup_server_message_get_method(msg), "GET") != 0) {
        send_error_response(msg, SOUP_STATUS_METHOD_NOT_ALLOWED,
                           "Only GET allowed");
        return;
    }

    /* ID aus Path extrahieren: /node/load/123 */
    const char *id_str = strrchr(path, '/');
    if (!id_str || strlen(id_str) <= 1) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST,
                           "Missing node ID in path");
        return;
    }
    id_str++; /* Skip '/' */

    gint64 node_id = g_ascii_strtoll(id_str, NULL, 10);
    if (node_id == 0) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST,
                           "Invalid node ID");
        return;
    }

    /* Node laden */
    GError *error = NULL;
    SondGraphNode *node = sond_graph_db_load_node(server->db_conn, node_id, &error);

    if (!node) {
        gchar *err_msg = g_strdup_printf("Failed to load node: %s",
                                        error ? error->message : "unknown");
        send_error_response(msg, SOUP_STATUS_NOT_FOUND, err_msg);
        g_free(err_msg);
        g_clear_error(&error);
        return;
    }

    /* Node serialisieren */
    gchar *response_json = sond_graph_node_to_json(node);
    send_success_response(msg, response_json);

    g_free(response_json);
    g_object_unref(node);
}

/**
 * handle_node_delete:
 * DELETE /node/delete/{id}
 *
 * Response: { "success": true }
 */
static void handle_node_delete(SoupServer *soup_server,
                                SoupServerMessage *msg,
                                const char *path,
                                GHashTable *query,
                                gpointer user_data) {
    SondServer *server = SOND_SERVER(user_data);

    /* Nur DELETE erlaubt */
    if (strcmp(soup_server_message_get_method(msg), "DELETE") != 0) {
        send_error_response(msg, SOUP_STATUS_METHOD_NOT_ALLOWED,
                           "Only DELETE allowed");
        return;
    }

    /* ID aus Path extrahieren */
    const char *id_str = strrchr(path, '/');
    if (!id_str || strlen(id_str) <= 1) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST,
                           "Missing node ID in path");
        return;
    }
    id_str++;

    gint64 node_id = g_ascii_strtoll(id_str, NULL, 10);
    if (node_id == 0) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST,
                           "Invalid node ID");
        return;
    }

    /* Node löschen */
    GError *error = NULL;
    if (!sond_graph_db_delete_node(server->db_conn, node_id, &error)) {
        gchar *err_msg = g_strdup_printf("Failed to delete node: %s",
                                        error ? error->message : "unknown");
        send_error_response(msg, SOUP_STATUS_INTERNAL_SERVER_ERROR, err_msg);
        g_free(err_msg);
        g_clear_error(&error);
        return;
    }

    send_success_response(msg, NULL);
}

/**
 * handle_node_save_with_edges:
 * POST /node/save-with-edges
 *
 * Body: Node als JSON (mit Edges)
 * Response: { "success": true, "data": { Node mit ID } }
 */
static void handle_node_save_with_edges(SoupServer *soup_server,
                                         SoupServerMessage *msg,
                                         const char *path,
                                         GHashTable *query,
                                         gpointer user_data) {
    SondServer *server = SOND_SERVER(user_data);

    if (strcmp(soup_server_message_get_method(msg), "POST") != 0) {
        send_error_response(msg, SOUP_STATUS_METHOD_NOT_ALLOWED,
                           "Only POST allowed");
        return;
    }

    SoupMessageBody *body = soup_server_message_get_request_body(msg);
    if (!body->data) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST,
                           "Empty request body");
        return;
    }

    GError *error = NULL;
    SondGraphNode *node = sond_graph_node_from_json(body->data, &error);

    if (!node) {
        gchar *err_msg = g_strdup_printf("Invalid JSON: %s",
                                        error ? error->message : "unknown");
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST, err_msg);
        g_free(err_msg);
        g_clear_error(&error);
        return;
    }

    /* Node mit Edges speichern */
    if (!sond_graph_db_save_node_with_edges(server->db_conn, node, &error)) {
        gchar *err_msg = g_strdup_printf("Failed to save node: %s",
                                        error ? error->message : "unknown");
        send_error_response(msg, SOUP_STATUS_INTERNAL_SERVER_ERROR, err_msg);
        g_free(err_msg);
        g_clear_error(&error);
        g_object_unref(node);
        return;
    }

    gchar *response_json = sond_graph_node_to_json(node);
    send_success_response(msg, response_json);

    g_free(response_json);
    g_object_unref(node);
}

/**
 * handle_edge_save:
 * POST /edge/save
 */
static void handle_edge_save(SoupServer *soup_server,
                              SoupServerMessage *msg,
                              const char *path,
                              GHashTable *query,
                              gpointer user_data) {
    SondServer *server = SOND_SERVER(user_data);

    if (strcmp(soup_server_message_get_method(msg), "POST") != 0) {
        send_error_response(msg, SOUP_STATUS_METHOD_NOT_ALLOWED,
                           "Only POST allowed");
        return;
    }

    SoupMessageBody *body = soup_server_message_get_request_body(msg);
    if (!body->data) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST,
                           "Empty request body");
        return;
    }

    /* Edge aus JSON deserialisieren - komplette Node laden, dann Edge extrahieren */
    /* Vereinfacht: erwarten wir ein Edge-JSON-Format */
    send_error_response(msg, SOUP_STATUS_NOT_IMPLEMENTED,
                       "Edge save not yet implemented - use node/save-with-edges");
}

/**
 * handle_node_search:
 * POST /node/search
 *
 * Body: SearchCriteria als JSON
 * Response: { "success": true, "data": [Node1, Node2, ...] }
 */
static void handle_node_search(SoupServer *soup_server,
                                SoupServerMessage *msg,
                                const char *path,
                                GHashTable *query,
                                gpointer user_data) {
    SondServer *server = SOND_SERVER(user_data);

    if (strcmp(soup_server_message_get_method(msg), "POST") != 0) {
        send_error_response(msg, SOUP_STATUS_METHOD_NOT_ALLOWED,
                           "Only POST allowed");
        return;
    }

    SoupMessageBody *body = soup_server_message_get_request_body(msg);
    if (!body->data) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST,
                           "Empty request body");
        return;
    }

    /* SearchCriteria aus JSON deserialisieren */
    GError *error = NULL;
    SondGraphNodeSearchCriteria *criteria =
        sond_graph_node_search_criteria_from_json(body->data, &error);

    if (!criteria) {
        gchar *err_msg = g_strdup_printf("Invalid search criteria: %s",
                                        error ? error->message : "unknown");
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST, err_msg);
        g_free(err_msg);
        g_clear_error(&error);
        return;
    }

    /* Suche ausführen */
    GPtrArray *nodes = sond_graph_db_search_nodes(server->db_conn, criteria, &error);
    sond_graph_node_search_criteria_free(criteria);

    if (!nodes) {
        gchar *err_msg = g_strdup_printf("Search failed: %s",
                                        error ? error->message : "unknown");
        send_error_response(msg, SOUP_STATUS_INTERNAL_SERVER_ERROR, err_msg);
        g_free(err_msg);
        g_clear_error(&error);
        return;
    }

    /* Nodes zu JSON Array serialisieren */
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_array(builder);

    for (guint i = 0; i < nodes->len; i++) {
        SondGraphNode *node = g_ptr_array_index(nodes, i);
        gchar *node_json = sond_graph_node_to_json(node);

        /* Parse und einfügen */
        JsonParser *parser = json_parser_new();
        if (json_parser_load_from_data(parser, node_json, -1, NULL)) {
            JsonNode *node_node = json_parser_get_root(parser);
            json_builder_add_value(builder, json_node_copy(node_node));
        }
        g_object_unref(parser);
        g_free(node_json);
    }

    json_builder_end_array(builder);

    JsonGenerator *generator = json_generator_new();
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(generator, root);

    gchar *response_json = json_generator_to_data(generator, NULL);
    send_success_response(msg, response_json);

    g_free(response_json);
    json_node_free(root);
    g_object_unref(generator);
    g_object_unref(builder);
    g_ptr_array_unref(nodes);
}

/**
 * handle_node_lock:
 * POST /node/lock/{id}
 *
 * Body: { "user_id": "...", "timeout_minutes": 30, "reason": "..." }
 * Response: { "success": true }
 */
static void handle_node_lock(SoupServer *soup_server,
                              SoupServerMessage *msg,
                              const char *path,
                              GHashTable *query,
                              gpointer user_data) {
    SondServer *server = SOND_SERVER(user_data);

    if (strcmp(soup_server_message_get_method(msg), "POST") != 0) {
        send_error_response(msg, SOUP_STATUS_METHOD_NOT_ALLOWED,
                           "Only POST allowed");
        return;
    }

    /* ID aus Path extrahieren */
    const char *id_str = strrchr(path, '/');
    if (!id_str || strlen(id_str) <= 1) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST,
                           "Missing node ID in path");
        return;
    }
    id_str++;

    gint64 node_id = g_ascii_strtoll(id_str, NULL, 10);
    if (node_id == 0) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST,
                           "Invalid node ID");
        return;
    }

    /* Body parsen */
    SoupMessageBody *body = soup_server_message_get_request_body(msg);
    if (!body->data) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST,
                           "Empty request body");
        return;
    }

    GError *error = NULL;
    JsonParser *parser = json_parser_new();

    if (!json_parser_load_from_data(parser, body->data, -1, &error)) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST, "Invalid JSON");
        g_clear_error(&error);
        g_object_unref(parser);
        return;
    }

    JsonNode *root = json_parser_get_root(parser);
    JsonObject *obj = json_node_get_object(root);

    const gchar *user_id = json_object_get_string_member(obj, "user_id");
    guint timeout_minutes = json_object_has_member(obj, "timeout_minutes") ?
        json_object_get_int_member(obj, "timeout_minutes") : 0;
    const gchar *reason = json_object_has_member(obj, "reason") ?
        json_object_get_string_member(obj, "reason") : NULL;

    g_object_unref(parser);

    if (!user_id) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST,
                           "Missing user_id");
        return;
    }

    /* Lock setzen */
    if (!sond_graph_db_lock_node(server->db_conn, node_id, user_id,
                                 timeout_minutes, reason, &error)) {
        gchar *err_msg = g_strdup_printf("Lock failed: %s",
                                        error ? error->message : "unknown");
        send_error_response(msg, SOUP_STATUS_CONFLICT, err_msg);
        g_free(err_msg);
        g_clear_error(&error);
        return;
    }

    send_success_response(msg, NULL);
}

/**
 * handle_node_unlock:
 * POST /node/unlock/{id}
 *
 * Body: { "user_id": "..." }
 * Response: { "success": true }
 */
static void handle_node_unlock(SoupServer *soup_server,
                                SoupServerMessage *msg,
                                const char *path,
                                GHashTable *query,
                                gpointer user_data) {
    SondServer *server = SOND_SERVER(user_data);

    if (strcmp(soup_server_message_get_method(msg), "POST") != 0) {
        send_error_response(msg, SOUP_STATUS_METHOD_NOT_ALLOWED,
                           "Only POST allowed");
        return;
    }

    /* ID aus Path */
    const char *id_str = strrchr(path, '/');
    if (!id_str || strlen(id_str) <= 1) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST,
                           "Missing node ID in path");
        return;
    }
    id_str++;

    gint64 node_id = g_ascii_strtoll(id_str, NULL, 10);
    if (node_id == 0) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST,
                           "Invalid node ID");
        return;
    }

    /* Body parsen */
    SoupMessageBody *body = soup_server_message_get_request_body(msg);
    if (!body->data) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST,
                           "Empty request body");
        return;
    }

    GError *error = NULL;
    JsonParser *parser = json_parser_new();

    if (!json_parser_load_from_data(parser, body->data, -1, &error)) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST, "Invalid JSON");
        g_clear_error(&error);
        g_object_unref(parser);
        return;
    }

    JsonNode *root = json_parser_get_root(parser);
    JsonObject *obj = json_node_get_object(root);

    const gchar *user_id = json_object_get_string_member(obj, "user_id");
    g_object_unref(parser);

    if (!user_id) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST,
                           "Missing user_id");
        return;
    }

    /* Unlock */
    if (!sond_graph_db_unlock_node(server->db_conn, node_id, user_id, &error)) {
        gchar *err_msg = g_strdup_printf("Unlock failed: %s",
                                        error ? error->message : "unknown");
        send_error_response(msg, SOUP_STATUS_FORBIDDEN, err_msg);
        g_free(err_msg);
        g_clear_error(&error);
        return;
    }

    send_success_response(msg, NULL);
}

/* ========================================================================
 * GObject Implementation
 * ======================================================================== */

static void sond_server_finalize(GObject *object) {
    SondServer *self = SOND_SERVER(object);

    if (self->running) {
        sond_server_stop(self);
    }

    if (self->soup_server) {
        g_object_unref(self->soup_server);
    }

    if (self->db_conn) {
        mysql_close(self->db_conn);
    }

    g_free(self->db_host);
    g_free(self->db_user);
    g_free(self->db_password);
    g_free(self->db_name);

    G_OBJECT_CLASS(sond_server_parent_class)->finalize(object);
}

static void sond_server_class_init(SondServerClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = sond_server_finalize;
}

static void sond_server_init(SondServer *self) {
    self->soup_server = NULL;
    self->db_conn = NULL;
    self->port = 0;
    self->running = FALSE;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

SondServer* sond_server_new(const gchar *db_host,
                             const gchar *db_user,
                             const gchar *db_password,
                             const gchar *db_name,
                             guint port,
                             GError **error) {
    g_return_val_if_fail(db_host != NULL, NULL);
    g_return_val_if_fail(db_user != NULL, NULL);
    g_return_val_if_fail(db_name != NULL, NULL);

    SondServer *server = g_object_new(SOND_TYPE_SERVER, NULL);

    server->db_host = g_strdup(db_host);
    server->db_user = g_strdup(db_user);
    server->db_password = g_strdup(db_password);
    server->db_name = g_strdup(db_name);
    server->port = port;

    /* MySQL verbinden */
    server->db_conn = mysql_init(NULL);
    if (!server->db_conn) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "mysql_init failed");
        g_object_unref(server);
        return NULL;
    }

    if (!mysql_real_connect(server->db_conn, db_host, db_user, db_password,
                           db_name, 0, NULL, 0)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "MySQL connection failed: %s", mysql_error(server->db_conn));
        g_object_unref(server);
        return NULL;
    }

    /* SoupServer erstellen */
    server->soup_server = soup_server_new(NULL, NULL);

    /* Routes registrieren */
    soup_server_add_handler(server->soup_server, "/node/save",
                           handle_node_save, server, NULL);
    soup_server_add_handler(server->soup_server, "/node/load",
                           handle_node_load, server, NULL);
    soup_server_add_handler(server->soup_server, "/node/delete",
                           handle_node_delete, server, NULL);
    soup_server_add_handler(server->soup_server, "/node/save-with-edges",
                           handle_node_save_with_edges, server, NULL);
    soup_server_add_handler(server->soup_server, "/node/search",
                           handle_node_search, server, NULL);
    soup_server_add_handler(server->soup_server, "/node/lock",
                           handle_node_lock, server, NULL);
    soup_server_add_handler(server->soup_server, "/node/unlock",
                           handle_node_unlock, server, NULL);
    soup_server_add_handler(server->soup_server, "/edge/save",
                           handle_edge_save, server, NULL);

    return server;
}

gboolean sond_server_start(SondServer *server, GError **error) {
    g_return_val_if_fail(SOND_IS_SERVER(server), FALSE);

    if (server->running) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Server already running");
        return FALSE;
    }

    /* Server auf Port binden und starten */
    if (!soup_server_listen_all(server->soup_server, server->port, 0, error)) {
        return FALSE;
    }

    server->running = TRUE;

    g_print("Server started on port %u\n", server->port);

    return TRUE;
}

void sond_server_stop(SondServer *server) {
    g_return_if_fail(SOND_IS_SERVER(server));

    if (!server->running) {
        return;
    }

    soup_server_disconnect(server->soup_server);
    server->running = FALSE;

    g_print("Server stopped\n");
}

gboolean sond_server_is_running(SondServer *server) {
    g_return_val_if_fail(SOND_IS_SERVER(server), FALSE);
    return server->running;
}

guint sond_server_get_port(SondServer *server) {
    g_return_val_if_fail(SOND_IS_SERVER(server), 0);
    return server->port;
}

gchar* sond_server_get_url(SondServer *server) {
    g_return_val_if_fail(SOND_IS_SERVER(server), NULL);
    return g_strdup_printf("http://localhost:%u", server->port);
}

/*
 * Kompilierung:
 * gcc -c sond_server.c $(pkg-config --cflags glib-2.0 gobject-2.0 libsoup-3.0 json-glib-1.0) $(mysql_config --cflags)
 */
