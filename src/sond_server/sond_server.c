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
#include "sond_server_seafile.h"
#include "sond_server_auth.h"
#include "../sond_graph/sond_graph_db.h"
#include "../sond_graph/sond_graph_node.h"
#include "../sond_graph/sond_graph_edge.h"

#include "../sond_log_and_error.h"

#include <json-glib/json-glib.h>
#include <string.h>
#ifdef __linux__
#include <glib-unix.h>
#endif // __linux__

GQuark sond_server_error_quark(void) {
    return g_quark_from_static_string("sond-server-error-quark");
}

struct _SondServer {
    GObject parent_instance;

    guint port;
    gboolean running;

    SoupServer *soup_server;
    MYSQL *db_conn;

    /* Seafile Config */
    gchar *seafile_url;           // z.B. "https://seafile.example.com"
    gchar *auth_token;            // Seafile Auth-Token
    gint seafile_group_id;        // Gruppe zum Teilen
    
    /* Auth/Session */
    guint session_lifetime_hours;
    guint session_inactivity_minutes;
    SessionManager *session_manager;  // Session-Verwaltung
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

/**
 * check_authorization:
 *
 * Prüft Authorization-Header und validiert Session-Token.
 * Sendet 401-Fehler bei ungültigem/fehlendem Token.
 *
 * Returns: TRUE wenn autorisiert, FALSE sonst (Response wurde bereits gesendet)
 */
static gboolean check_authorization(SondServer *server,
                                    SoupServerMessage *msg,
                                    const gchar **username) {
    SoupMessageHeaders *headers = soup_server_message_get_request_headers(msg);
    const gchar *auth_header = soup_message_headers_get_one(headers, "Authorization");

    if (!auth_header || !g_str_has_prefix(auth_header, "Bearer ")) {
        send_error_response(msg, SOUP_STATUS_UNAUTHORIZED,
                           "Missing or invalid Authorization header");
        return FALSE;
    }

    const gchar *token = auth_header + 7;  /* Skip "Bearer " */

    if (!session_manager_validate(server->session_manager, token, username)) {
        send_error_response(msg, SOUP_STATUS_UNAUTHORIZED,
                           "Invalid or expired session");
        return FALSE;
    }

    /* Activity updaten */
    session_manager_update_activity(server->session_manager, token);

    return TRUE;
}

/**
 * handle_auth_login:
 * POST /auth/login
 *
 * Body: { "username": "...", "password": "..." }
 * Response: { "success": true, "data": { "session_token": "...", "username": "..." } }
 */
static void handle_auth_login(SoupServer *soup_server,
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

    const gchar *username = json_object_has_member(obj, "username") ?
        json_object_get_string_member(obj, "username") : NULL;
    const gchar *password = json_object_has_member(obj, "password") ?
        json_object_get_string_member(obj, "password") : NULL;

    if (!username || !password) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST,
                           "Missing username or password");
        g_object_unref(parser);
        return;
    }

    /* Bei Seafile authentifizieren */
    gchar *seafile_token = sond_server_seafile_get_auth_token(server, username, password, &error);

    if (!seafile_token) {
        gchar *err_msg = g_strdup_printf("Authentication failed: %s",
                                        error ? error->message : "Invalid credentials");
        send_error_response(msg, SOUP_STATUS_UNAUTHORIZED, err_msg);
        g_free(err_msg);
        g_clear_error(&error);
        g_object_unref(parser);
        return;
    }

    /* Session erstellen */
    gchar *session_token = session_manager_create(server->session_manager,
                                                   username, seafile_token);
    g_free(seafile_token);

    /* Response erstellen */
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    
    json_builder_set_member_name(builder, "session_token");
    json_builder_add_string_value(builder, session_token);
    
    json_builder_set_member_name(builder, "username");
    json_builder_add_string_value(builder, username);
    
    json_builder_end_object(builder);

    JsonGenerator *generator = json_generator_new();
    JsonNode *response_node = json_builder_get_root(builder);
    json_generator_set_root(generator, response_node);
    gchar *response_json = json_generator_to_data(generator, NULL);

    send_success_response(msg, response_json);

    g_free(response_json);
    g_free(session_token);
    json_node_free(response_node);
    g_object_unref(generator);
    g_object_unref(builder);
    g_object_unref(parser);
}

/**
 * handle_auth_logout:
 * POST /auth/logout
 *
 * Header: Authorization: Bearer <token>
 * Response: { "success": true }
 */
static void handle_auth_logout(SoupServer *soup_server,
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

    /* Token aus Header holen */
    SoupMessageHeaders *headers = soup_server_message_get_request_headers(msg);
    const gchar *auth_header = soup_message_headers_get_one(headers, "Authorization");

    if (!auth_header || !g_str_has_prefix(auth_header, "Bearer ")) {
        send_error_response(msg, SOUP_STATUS_UNAUTHORIZED,
                           "Missing or invalid Authorization header");
        return;
    }

    const gchar *token = auth_header + 7;  /* Skip "Bearer " */

    /* Session invalidieren */
    session_manager_invalidate(server->session_manager, token);

    send_success_response(msg, NULL);
}

/**
 * handle_auth_validate:
 * GET /auth/validate
 *
 * Header: Authorization: Bearer <token>
 * Response: { "success": true, "data": { "username": "...", "valid": true } }
 */
static void handle_auth_validate(SoupServer *soup_server,
                                  SoupServerMessage *msg,
                                  const char *path,
                                  GHashTable *query,
                                  gpointer user_data) {
    SondServer *server = SOND_SERVER(user_data);

    if (strcmp(soup_server_message_get_method(msg), "GET") != 0) {
        send_error_response(msg, SOUP_STATUS_METHOD_NOT_ALLOWED,
                           "Only GET allowed");
        return;
    }

    /* Token aus Header holen */
    SoupMessageHeaders *headers = soup_server_message_get_request_headers(msg);
    const gchar *auth_header = soup_message_headers_get_one(headers, "Authorization");

    if (!auth_header || !g_str_has_prefix(auth_header, "Bearer ")) {
        send_error_response(msg, SOUP_STATUS_UNAUTHORIZED,
                           "Missing or invalid Authorization header");
        return;
    }

    const gchar *token = auth_header + 7;  /* Skip "Bearer " */
    const gchar *username = NULL;

    /* Session validieren */
    gboolean valid = session_manager_validate(server->session_manager, token, &username);

    if (!valid) {
        send_error_response(msg, SOUP_STATUS_UNAUTHORIZED,
                           "Invalid or expired session");
        return;
    }

    /* Activity updaten */
    session_manager_update_activity(server->session_manager, token);

    /* Response erstellen */
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    
    json_builder_set_member_name(builder, "valid");
    json_builder_add_boolean_value(builder, TRUE);
    
    json_builder_set_member_name(builder, "username");
    json_builder_add_string_value(builder, username);
    
    json_builder_end_object(builder);

    JsonGenerator *generator = json_generator_new();
    JsonNode *response_node = json_builder_get_root(builder);
    json_generator_set_root(generator, response_node);
    gchar *response_json = json_generator_to_data(generator, NULL);

    send_success_response(msg, response_json);

    g_free(response_json);
    json_node_free(response_node);
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

    /* Auth-Check */
    const gchar *username = NULL;
    if (!check_authorization(server, msg, &username)) {
        return;  /* 401 wurde bereits gesendet */
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

    /* Auth-Check */
    const gchar *username = NULL;
    if (!check_authorization(server, msg, &username)) {
        return;  /* 401 wurde bereits gesendet */
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

    /* Auth-Check */
    const gchar *username = NULL;
    if (!check_authorization(server, msg, &username)) {
        return;  /* 401 wurde bereits gesendet */
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

    /* Auth-Check */
    const gchar *username = NULL;
    if (!check_authorization(server, msg, &username)) {
        return;  /* 401 wurde bereits gesendet */
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

    /* Auth-Check */
    const gchar *username = NULL;
    if (!check_authorization(server, msg, &username)) {
        return;  /* 401 wurde bereits gesendet */
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

    /* Auth-Check */
    const gchar *username = NULL;
    if (!check_authorization(server, msg, &username)) {
        return;  /* 401 wurde bereits gesendet */
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
    
    /* WICHTIG: Array freigeben - die Nodes werden automatisch unref'd durch free_func */
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

    /* Auth-Check */
    const gchar *username = NULL;
    if (!check_authorization(server, msg, &username)) {
        return;  /* 401 wurde bereits gesendet */
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

    if (!user_id) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST,
                           "Missing user_id");
        g_object_unref(parser);
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
        g_object_unref(parser);
        return;
    }

    g_object_unref(parser);

    send_success_response(msg, NULL);
}

/**
 * handle_node_create_and_lock:
 * POST /node/create_and_lock
 *
 * Body: { "node": {...}, "user_id": "...", "lock_reason": "..." }
 * Response: { "success": true, "data": { Node mit ID } }
 *
 * Erstellt Node und setzt Lock atomar in einer Transaction.
 */
static void handle_node_create_and_lock(SoupServer *soup_server,
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

    /* Auth-Check */
    const gchar *username = NULL;
    if (!check_authorization(server, msg, &username)) {
        return;  /* 401 wurde bereits gesendet */
    }

    /* Body lesen */
    SoupMessageBody *body = soup_server_message_get_request_body(msg);
    if (!body->data) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST,
                           "Empty request body");
        return;
    }

    /* JSON parsen */
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

    /* Node-JSON extrahieren */
    if (!json_object_has_member(obj, "node")) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST, "Missing 'node' field");
        g_object_unref(parser);
        return;
    }

    JsonNode *node_node = json_object_get_member(obj, "node");
    JsonGenerator *node_gen = json_generator_new();
    json_generator_set_root(node_gen, node_node);
    gchar *node_json = json_generator_to_data(node_gen, NULL);
    g_object_unref(node_gen);

    /* user_id extrahieren */
    const gchar *user_id = json_object_has_member(obj, "user_id") ?
        json_object_get_string_member(obj, "user_id") : NULL;

    if (!user_id) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST, "Missing 'user_id' field");
        g_free(node_json);
        g_object_unref(parser);
        return;
    }

    /* lock_reason extrahieren (optional) */
    const gchar *lock_reason = json_object_has_member(obj, "lock_reason") ?
        json_object_get_string_member(obj, "lock_reason") : "Bearbeitung";

    /* Node aus JSON deserialisieren */
    SondGraphNode *node = sond_graph_node_from_json(node_json, &error);
    g_free(node_json);

    if (!node) {
        gchar *err_msg = g_strdup_printf("Invalid node JSON: %s",
                                        error ? error->message : "unknown");
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST, err_msg);
        g_free(err_msg);
        g_clear_error(&error);
        g_object_unref(parser);
        return;
    }

    /* Transaction starten */
    if (mysql_query(server->db_conn, "START TRANSACTION")) {
        gchar *err_msg = g_strdup_printf("Failed to start transaction: %s",
                                        mysql_error(server->db_conn));
        send_error_response(msg, SOUP_STATUS_INTERNAL_SERVER_ERROR, err_msg);
        g_free(err_msg);
        g_object_unref(node);
        g_object_unref(parser);
        return;
    }

    /* Node speichern */
    if (!sond_graph_db_save_node(server->db_conn, node, &error)) {
        mysql_query(server->db_conn, "ROLLBACK");
        gchar *err_msg = g_strdup_printf("Failed to save node: %s",
                                        error ? error->message : "unknown");
        send_error_response(msg, SOUP_STATUS_INTERNAL_SERVER_ERROR, err_msg);
        g_free(err_msg);
        g_clear_error(&error);
        g_object_unref(node);
        g_object_unref(parser);
        return;
    }

    gint64 node_id = sond_graph_node_get_id(node);

    /* Lock setzen (0 = kein Timeout) */
    if (!sond_graph_db_lock_node(server->db_conn, node_id, user_id, 0, lock_reason, &error)) {
        mysql_query(server->db_conn, "ROLLBACK");
        LOG_ERROR("Lock failed for node %" G_GINT64_FORMAT ": %s\n",
                 node_id, error ? error->message : "unknown");
        gchar *err_msg = g_strdup_printf("Failed to lock node: %s",
                                        error ? error->message : "unknown");
        send_error_response(msg, SOUP_STATUS_CONFLICT, err_msg);
        g_free(err_msg);
        g_clear_error(&error);
        g_object_unref(node);
        g_object_unref(parser);
        return;
    }

    g_object_unref(parser);

    /* Transaction committen */
    if (mysql_query(server->db_conn, "COMMIT")) {
        mysql_query(server->db_conn, "ROLLBACK");
        gchar *err_msg = g_strdup_printf("Failed to commit transaction: %s",
                                        mysql_error(server->db_conn));
        send_error_response(msg, SOUP_STATUS_INTERNAL_SERVER_ERROR, err_msg);
        g_free(err_msg);
        g_object_unref(node);
        return;
    }

    /* Erfolg - Node zurückgeben */
    gchar *response_json = sond_graph_node_to_json(node);
    send_success_response(msg, response_json);

    g_free(response_json);
    g_object_unref(node);
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

    /* Auth-Check */
    const gchar *username = NULL;
    if (!check_authorization(server, msg, &username)) {
        return;  /* 401 wurde bereits gesendet */
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
    if (!user_id) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST,
                           "Missing user_id");
        g_object_unref(parser);
        return;
    }

    /* Unlock */
    if (!sond_graph_db_unlock_node(server->db_conn, node_id, user_id, &error)) {
        gchar *err_msg = g_strdup_printf("Unlock failed: %s",
                                        error ? error->message : "unknown");
        send_error_response(msg, SOUP_STATUS_FORBIDDEN, err_msg);
        g_free(err_msg);
        g_clear_error(&error);
        g_object_unref(parser);
        return;
    }

    g_object_unref(parser);

    send_success_response(msg, NULL);
}

/* ========================================================================
 * GObject Implementation
 * ======================================================================== */

static void sond_server_stop(SondServer *server) {
    g_return_if_fail(SOND_IS_SERVER(server));

    if (!server->running) {
        return;
    }

    soup_server_disconnect(server->soup_server);
    server->running = FALSE;

    g_print("Server stopped\n");
}


static void sond_server_finalize(GObject *object) {
    SondServer *self = SOND_SERVER(object);

    if (self->running) {
        sond_server_stop(self);
    }

    if (self->soup_server) {
        g_object_unref(self->soup_server);
    }
    
    if (self->session_manager) {
        session_manager_free(self->session_manager);
    }

    if (self->db_conn) {
        mysql_close(self->db_conn);
    }

    g_free(self->seafile_url);
    g_free(self->auth_token);

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
    self->seafile_url = NULL;
    self->auth_token = NULL;
    self->session_lifetime_hours = 0;
    self->session_inactivity_minutes = 0;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

struct DataConfig {
	gchar *db_host;
	gint db_port;
	gchar *db_name;
	gchar *db_user;
	gchar *db_passfile;
	gchar *sf_user;
	gchar *sf_passfile;
};

static void data_config_free(struct DataConfig *data_config) {
	g_free(data_config->db_host);
	g_free(data_config->db_name);
	g_free(data_config->db_user);
	g_free(data_config->db_passfile);
	g_free(data_config->sf_user);
	g_free(data_config->sf_passfile);
}

static gboolean sond_server_load_config(SondServer *server,
                                  const gchar *config_file,
								  struct DataConfig *data_config,
                                  GError **error) {
	GError *error_tmp = NULL;

    GKeyFile *keyfile = g_key_file_new();

    if (!g_key_file_load_from_file(keyfile, config_file,
                                    G_KEY_FILE_NONE, error)) {
        g_key_file_free(keyfile);
        return FALSE;
    }

    /* === Server Config === */
    server->port = g_key_file_get_integer(keyfile, "server", "port", &error_tmp);
    if (!server->port) {
    	LOG_WARN("Couldn't read port in config (error: '%s'), "
				"using 35002\n", error_tmp->message);
		g_clear_error(&error_tmp);
		server->port = 35002;
    }
    
    /* === MariaDB Config === */
    data_config->db_host = g_key_file_get_string(keyfile, "mariadb", "host", &error_tmp);
    if (!data_config->db_host) {
    	LOG_WARN("Couldn't read host in config (error: '%s'), "
    			"using 'localhost'\n", error_tmp->message);
    	g_clear_error(&error_tmp);
		data_config->db_host = g_strdup("localhost");
	}

    data_config->db_port = g_key_file_get_integer(keyfile, "mariadb", "port", &error_tmp);
    if (!data_config->db_port) {
    	LOG_WARN("Couldn't read port in config (error: '%s'), "
    			"using 3306\n", error_tmp->message);
    	g_clear_error(&error_tmp);
		data_config->db_port = 3306;
	}

    data_config->db_name = g_key_file_get_string(keyfile, "mariadb", "database", error);
    if (!data_config->db_name) {
    	data_config_free(data_config);
    	return FALSE;
	}

    data_config->db_user = g_key_file_get_string(keyfile, "mariadb", "username", error);
    if (!data_config->db_user) {
    	data_config_free(data_config);
    	return FALSE;
	}

    data_config->db_passfile = g_key_file_get_string(keyfile, "mariadb", "password_file", error);
    if (!data_config->db_passfile) {
    	data_config_free(data_config);
    	return FALSE;
	}

    /* === Auth Config === */
    server->session_lifetime_hours = g_key_file_get_integer(keyfile, "auth", 
                                                             "session_lifetime_hours", NULL);
    server->session_inactivity_minutes = g_key_file_get_integer(keyfile, "auth",
                                                                 "session_inactivity_minutes", NULL);

    /* === Seafile Config === */
    server->seafile_url = g_key_file_get_string(keyfile, "seafile", "url", NULL);
    data_config->sf_user = g_key_file_get_string(keyfile, "seafile", "username", NULL);
    data_config->sf_passfile = g_key_file_get_string(keyfile, "seafile", "password_file", NULL);
    server->seafile_group_id = g_key_file_get_integer(keyfile, "seafile", "group_id", NULL);

    g_key_file_free(keyfile);
    return TRUE;
}

static gboolean sond_server_init_db_conn(SondServer *server,
								 struct DataConfig *data_config,
								 GError **error) {
    gchar *db_password = NULL;

    /* MySQL verbinden */
    server->db_conn = mysql_init(NULL);
    if (!server->db_conn) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "mysql_init failed");
        return FALSE;
    }

    //Passwort einlesen
    if (data_config->db_passfile) {
        if (g_file_get_contents(data_config->db_passfile, &db_password, NULL, error)) {
            g_strstrip(db_password);
        } else {
            g_prefix_error(error, "Failed to read MariaDB password file: ");
            mysql_close(server->db_conn);
            server->db_conn = NULL;
            return FALSE;
		}
    }

    gpointer suc = mysql_real_connect(server->db_conn, data_config->db_host,
    		data_config->db_user, db_password, data_config->db_name, 0, NULL, 0);

    // Passwort überschreiben
	memset(db_password, 0, strlen(db_password));
	g_free(db_password);
	db_password = NULL;  /* Prevent double-free in finalize() */

	if (!suc) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
			   "MySQL connection failed: %s", mysql_error(server->db_conn));
		mysql_close(server->db_conn);
		server->db_conn = NULL;
        return FALSE;
    }

    /* Graph-Datenbank Setup - Tabellen und Prozeduren erstellen */
    LOG_INFO("Initializing graph database schema...\n");
    if (!sond_graph_db_setup(server->db_conn, error)) {
        g_prefix_error(error, "Graph DB setup failed: ");
		mysql_close(server->db_conn);
		server->db_conn = NULL;
        return FALSE;
    }

    return TRUE;
}

static gboolean sond_server_init_seafile(SondServer *server,
							 struct DataConfig *data_config,
							 GError **error) {
    gchar *sf_password = NULL;

    if (!g_file_get_contents(data_config->sf_passfile, &sf_password, NULL, error)) {
        g_prefix_error(error, "Failed to read Seafile password file: ");
        return FALSE;
    }
	g_strstrip(sf_password);

	// Auth-Token holen
	server->auth_token = sond_server_seafile_get_auth_token(
		server, data_config->sf_user, sf_password, error);

	// Passwort überschreiben
	memset(sf_password, 0, strlen(sf_password));
	g_free(sf_password);
	if (!server->auth_token)
		return FALSE;

	return TRUE;
}

static gboolean sond_server_prepare(SondServer*server,
							 gchar const* config,
                             GError **error) {
	struct DataConfig data_config = { 0 };

    g_return_val_if_fail(config != NULL, FALSE);

    if (!sond_server_load_config(server, config, &data_config, error)) {
		return FALSE;
	}

    if (!sond_server_init_db_conn(server, &data_config, error)) {
    	data_config_free(&data_config);
		return FALSE;
    }

    if (!sond_server_init_seafile(server, &data_config, error)) {
		data_config_free(&data_config);
		return FALSE;
    }

    data_config_free(&data_config);

    /* SoupServer erstellen */
    server->soup_server = soup_server_new(NULL, NULL);
    
    /* SessionManager erstellen */
    server->session_manager = session_manager_new(server->session_lifetime_hours,
                                                   server->session_inactivity_minutes);

    /* Routes registrieren */
    
    /* Auth-Endpoints */
    soup_server_add_handler(server->soup_server, "/auth/login",
                           handle_auth_login, server, NULL);
    soup_server_add_handler(server->soup_server, "/auth/logout",
                           handle_auth_logout, server, NULL);
    soup_server_add_handler(server->soup_server, "/auth/validate",
                           handle_auth_validate, server, NULL);
    
    /* Node-Endpoints */
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
    soup_server_add_handler(server->soup_server, "/node/create_and_lock",
                           handle_node_create_and_lock, server, NULL);
    soup_server_add_handler(server->soup_server, "/node/lock",
                           handle_node_lock, server, NULL);
    soup_server_add_handler(server->soup_server, "/node/unlock",
                           handle_node_unlock, server, NULL);
    soup_server_add_handler(server->soup_server, "/edge/save",
                           handle_edge_save, server, NULL);

    /* Seafile Endpoints registrieren */
    sond_server_seafile_register_handlers(server);

    return TRUE;
}

/**
 * session_cleanup_timeout:
 *
 * Periodischer Callback für Session-Cleanup.
 */
static gboolean session_cleanup_timeout(gpointer user_data) {
    SondServer *server = SOND_SERVER(user_data);

    if (server->session_manager) {
        session_manager_cleanup_expired(server->session_manager);
    }

    return G_SOURCE_CONTINUE;  /* Weiter aufrufen */
}

static gboolean sond_server_start(SondServer *server, GError **error) {
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

    LOG_INFO("Server started on port %u\n", server->port);
    
    /* Periodisches Session-Cleanup alle 10 Minuten */
    g_timeout_add_seconds(600, (GSourceFunc)session_cleanup_timeout, server);

    return TRUE;
}

/* Globale Variablen für Signal-Handler */
static GMainLoop *main_loop = NULL;
static SondServer *server_instance = NULL;

/**
 * signal_handler:
 *
 * Behandelt SIGINT (Ctrl+C) und SIGTERM für sauberes Beenden.
 */
static gboolean signal_handler(gpointer user_data) {
    g_print("\n");
    LOG_ERROR("Received signal, shutting down...\n");

    /* Server stoppen */
    if (server_instance && server_instance->running) {
        sond_server_stop(server_instance);
    }

    /* Main loop beenden */
    if (main_loop && g_main_loop_is_running(main_loop)) {
        g_main_loop_quit(main_loop);
    }

    return G_SOURCE_REMOVE;
}

int main(int argc, char *argv[]) {
    GError *error = NULL;
    SondServer *server = NULL;
    GMainLoop *loop = NULL;
    gchar *config_file = "/etc/sond-server/sond-server.conf";

    logging_init("sond_server");

    /* Command Line Argument für Config-Pfad (optional) */
    if (argc > 1) {
        config_file = argv[1];
    }

    LOG_INFO("Starting SOND Server...\n");
    LOG_INFO("Config: %s\n", config_file);

    /* Server erstellen */
    server = g_object_new(SOND_TYPE_SERVER, NULL);
    server_instance = server;

    /* Config laden */
    if (!sond_server_prepare(server, config_file, &error)) {
        LOG_ERROR("Failed to load config: %s\n", error->message);
        g_error_free(error);
        g_object_unref(server);
        return 1;
    }

    /* Server starten */
    if (!sond_server_start(server, &error)) {
        LOG_ERROR("Failed to start server: %s\n", error->message);
        g_error_free(error);
        g_object_unref(server);
        return 1;
    }

    LOG_INFO("Server running on port %d\n", server->port);

    /* Main Loop */
    loop = g_main_loop_new(NULL, FALSE);

#ifdef __linux__
    /* Signal Handler für sauberes Shutdown */
    main_loop = loop;
    g_unix_signal_add(SIGINT, signal_handler, NULL);
    g_unix_signal_add(SIGTERM, signal_handler, NULL);
#endif // __linux__

    g_main_loop_run(loop);

    /* Cleanup */
    LOG_INFO("\nShutting down...\n");
    g_main_loop_unref(loop);
    g_object_unref(server);

    LOG_INFO("Server stopped\n");

    return 0;
}

/* ========================================================================
 * Public Accessors für Seafile-Integration
 * ======================================================================== */

gchar const* sond_server_get_seafile_url(SondServer *server) {
    g_return_val_if_fail(SOND_IS_SERVER(server), NULL);
    return g_strdup(server->seafile_url);
}

gchar* sond_server_get_seafile_token(SondServer *server) {
    g_return_val_if_fail(SOND_IS_SERVER(server), NULL);
    return g_strdup(server->auth_token);
}

guint sond_server_get_seafile_group_id(SondServer *server) {
    g_return_val_if_fail(SOND_IS_SERVER(server), 0);
    return server->seafile_group_id;
}

SoupServer* sond_server_get_soup_server(SondServer *server) {
    g_return_val_if_fail(SOND_IS_SERVER(server), NULL);
    return server->soup_server;
}
