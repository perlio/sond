/*
 sond (sond_server_seafile.c) - Akten, Beweisstücke, Unterlagen
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

/*
 sond (sond_server_seafile.c) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_server_seafile.c
 * @brief Seafile Integration - REST Endpoints
 *
 * Endpoints:
 * - POST   /api/seafile/library    - Erstellt Library für Akte
 * - DELETE /api/seafile/library    - Löscht Library
 */

#include <glib.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>

#include "sond_server.h"

/* ========================================================================
 * Seafile Helper Functions (libsoup statt CURL)
 * ======================================================================== */

/**
 * seafile_request:
 *
 * Generische Funktion für Seafile API Requests.
 */
static GBytes* seafile_request(SondServer *server,
                                const gchar *method,
                                const gchar *path,
                                const gchar *body,
                                guint *status_code,
                                GError **error) {
    SoupSession *session = NULL;
    SoupMessage *msg = NULL;
    gchar *url = NULL;
    GBytes *response = NULL;

    url = g_strdup_printf("%s%s", server->seafile_url, path);
    msg = soup_message_new(method, url);
    g_free(url);

    /* Authorization Header */
    if (server->auth_token) {
        gchar *auth = g_strdup_printf("Token %s", server->auth_token);
        soup_message_headers_append(soup_message_get_request_headers(msg),
                                     "Authorization", auth);
        g_free(auth);
    }

    soup_message_headers_append(soup_message_get_request_headers(msg),
                                 "Accept", "application/json");

    /* Body für POST/PUT */
    if (body && (g_strcmp0(method, "POST") == 0 || g_strcmp0(method, "PUT") == 0)) {
        GBytes *body_bytes = g_bytes_new(body, strlen(body));
        soup_message_set_request_body_from_bytes(msg,
                                                  "application/x-www-form-urlencoded",
                                                  body_bytes);
        g_bytes_unref(body_bytes);
    }

    /* Send Request */
    session = soup_session_new();
    response = soup_session_send_and_read(session, msg, NULL, error);

    if (status_code) {
        *status_code = soup_message_get_status(msg);
    }

    g_object_unref(msg);
    g_object_unref(session);

    return response;
}

/**
 * seafile_search_library:
 *
 * Sucht eine Library anhand des Namens (Format: YYYY-NNN).
 * Returns: (transfer full): repo_id oder NULL
 */
static gchar* seafile_search_library(SondServer *server,
                                      gint reg_jahr,
                                      gint reg_nr,
                                      GError **error) {
    gchar *path = NULL;
    GBytes *response = NULL;
    guint status = 0;
    gchar *repo_id = NULL;

    path = g_strdup_printf("/api2/repos/?nameContains=%d-%d", reg_jahr, reg_nr);
    response = seafile_request(server, "GET", path, NULL, &status, error);
    g_free(path);

    if (!response || status != 200) {
        if (response) g_bytes_unref(response);
        return NULL;
    }

    /* Parse JSON Response */
    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_data(parser,
                                     g_bytes_get_data(response, NULL),
                                     -1, error)) {
        g_object_unref(parser);
        g_bytes_unref(response);
        return NULL;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (JSON_NODE_HOLDS_ARRAY(root)) {
        JsonArray *array = json_node_get_array(root);

        for (guint i = 0; i < json_array_get_length(array); i++) {
            JsonObject *obj = json_array_get_object_element(array, i);

            if (json_object_has_member(obj, "type") &&
                g_strcmp0(json_object_get_string_member(obj, "type"), "repo") == 0) {

                if (repo_id) {
                    /* Mehrere gefunden - Fehler */
                    g_set_error(error, SOND_SERVER_ERROR, 0,
                               "Multiple libraries found for %d-%d", reg_jahr, reg_nr);
                    g_free(repo_id);
                    repo_id = NULL;
                    break;
                }

                repo_id = g_strdup(json_object_get_string_member(obj, "id"));
            }
        }
    }

    g_object_unref(parser);
    g_bytes_unref(response);

    return repo_id;
}

/**
 * seafile_create_library:
 *
 * Erstellt eine neue Library.
 * Returns: (transfer full): repo_id oder NULL
 */
static gchar* seafile_create_library(SondServer *server,
                                      gint reg_jahr,
                                      gint reg_nr,
                                      GError **error) {
    gchar *body = NULL;
    GBytes *response = NULL;
    guint status = 0;
    gchar *repo_id = NULL;

    body = g_strdup_printf("name=%d-%d", reg_jahr, reg_nr);
    response = seafile_request(server, "POST", "/api2/repos/", body, &status, error);
    g_free(body);

    if (!response || status != 200) {
        if (response) g_bytes_unref(response);
        return NULL;
    }

    /* Parse Response */
    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_data(parser,
                                     g_bytes_get_data(response, NULL),
                                     -1, error)) {
        g_object_unref(parser);
        g_bytes_unref(response);
        return NULL;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (JSON_NODE_HOLDS_OBJECT(root)) {
        JsonObject *obj = json_node_get_object(root);
        if (json_object_has_member(obj, "repo_id")) {
            repo_id = g_strdup(json_object_get_string_member(obj, "repo_id"));
        }
    }

    g_object_unref(parser);
    g_bytes_unref(response);

    return repo_id;
}

/**
 * seafile_delete_library:
 *
 * Löscht eine Library.
 */
static gboolean seafile_delete_library(SondServer *server,
                                        const gchar *repo_id,
                                        GError **error) {
    gchar *path = NULL;
    GBytes *response = NULL;
    guint status = 0;
    gboolean success = FALSE;

    path = g_strdup_printf("/api2/repos/%s", repo_id);
    response = seafile_request(server, "DELETE", path, NULL, &status, error);
    g_free(path);

    success = (status == 200);

    if (response) g_bytes_unref(response);

    return success;
}

/**
 * seafile_create_directory:
 *
 * Erstellt ein Verzeichnis in einer Library.
 */
static gboolean seafile_create_directory(SondServer *server,
                                          const gchar *repo_id,
                                          const gchar *dirname,
                                          GError **error) {
    gchar *path = NULL;
    GBytes *response = NULL;
    guint status = 0;
    gboolean success = FALSE;

    path = g_strdup_printf("/api2/repos/%s/dir/?p=/%s", repo_id, dirname);
    response = seafile_request(server, "POST", path, "operation=mkdir", &status, error);
    g_free(path);

    success = (status == 201);

    if (response) g_bytes_unref(response);

    return success;
}

/**
 * seafile_share_with_group:
 *
 * Teilt eine Library mit einer Gruppe.
 */
static gboolean seafile_share_with_group(SondServer *server,
                                          const gchar *repo_id,
                                          gint group_id,
                                          GError **error) {
    gchar *path = NULL;
    gchar *body = NULL;
    GBytes *response = NULL;
    guint status = 0;
    gboolean success = FALSE;
    SoupMessage *msg = NULL;
    SoupSession *session = NULL;

    /* Custom Request mit PUT Method */
    path = g_strdup_printf("%s/api2/repos/%s/dir/shared_items/?p=/",
                          server->seafile_url, repo_id);
    msg = soup_message_new("PUT", path);
    g_free(path);

    /* Authorization */
    gchar *auth = g_strdup_printf("Token %s", server->auth_token);
    soup_message_headers_append(soup_message_get_request_headers(msg),
                                 "Authorization", auth);
    g_free(auth);

    /* Body */
    body = g_strdup_printf("share_type=group&group_id=%d&permission=rw", group_id);
    GBytes *body_bytes = g_bytes_new(body, strlen(body));
    soup_message_set_request_body_from_bytes(msg,
                                              "application/x-www-form-urlencoded",
                                              body_bytes);
    g_free(body);
    g_bytes_unref(body_bytes);

    /* Send */
    session = soup_session_new();
    response = soup_session_send_and_read(session, msg, NULL, error);
    status = soup_message_get_status(msg);

    success = (status == 200);

    if (response) g_bytes_unref(response);
    g_object_unref(msg);
    g_object_unref(session);

    return success;
}

/**
 * seafile_prepare_library:
 *
 * Bereitet Library vor: Verzeichnisse anlegen, teilen.
 */
static gboolean seafile_prepare_library(SondServer *server,
                                         const gchar *repo_id,
                                         GError **error) {
    /* Verzeichnisse anlegen */
    const gchar *dirs[] = {"docs", "eingang", "ausgang"};

    for (guint i = 0; i < G_N_ELEMENTS(dirs); i++) {
        if (!seafile_create_directory(server, repo_id, dirs[i], error)) {
            g_prefix_error(error, "Failed to create directory '%s': ", dirs[i]);
            return FALSE;
        }
    }

    /* Mit Gruppe teilen */
    if (server->seafile_group_id > 0) {
        if (!seafile_share_with_group(server, repo_id, server->seafile_group_id, error)) {
            g_prefix_error(error, "Failed to share with group: ");
            return FALSE;
        }
    }

    return TRUE;
}

/* ========================================================================
 * REST API Handlers
 * ======================================================================== */

/**
 * handle_seafile_library_create:
 *
 * POST /api/seafile/library
 * Body: {"reg_jahr": 2025, "reg_nr": 123}
 * Response: {"repo_id": "abc-123-def"}
 */
static void handle_seafile_library_create(SoupServer *soup_server,
                                           SoupServerMessage *msg,
                                           const char *path,
                                           GHashTable *query,
                                           gpointer user_data) {
    SondServer *server = SOND_SERVER(user_data);
    GError *error = NULL;
    SoupMessageBody *request_body = NULL;
    GBytes *body = NULL;
    JsonParser *parser = NULL;
    JsonObject *obj = NULL;
    gint reg_jahr = 0, reg_nr = 0;
    gchar *repo_id = NULL;
    gchar *response_json = NULL;

    /* Parse Request Body */
    request_body = soup_server_message_get_request_body(msg);
    if (!request_body || request_body->length == 0) {
        soup_server_message_set_status(msg, SOUP_STATUS_BAD_REQUEST, NULL);
        return;
    }

    body = soup_message_body_flatten(request_body);

    parser = json_parser_new();
    if (!json_parser_load_from_data(parser,
                                     g_bytes_get_data(body, NULL),
                                     g_bytes_get_size(body),
                                     &error)) {
        g_bytes_unref(body);
        g_object_unref(parser);
        soup_server_message_set_status(msg, SOUP_STATUS_BAD_REQUEST, NULL);
        return;
    }

    JsonNode *root = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(root)) {
        g_object_unref(parser);
        g_bytes_unref(body);
        soup_server_message_set_status(msg, SOUP_STATUS_BAD_REQUEST, NULL);
        return;
    }

    obj = json_node_get_object(root);
    reg_jahr = json_object_get_int_member(obj, "reg_jahr");
    reg_nr = json_object_get_int_member(obj, "reg_nr");

    g_object_unref(parser);
    g_bytes_unref(body);

    /* Create Library */
    repo_id = seafile_create_library(server, reg_jahr, reg_nr, &error);
    if (!repo_id) {
        soup_server_message_set_status(msg, SOUP_STATUS_INTERNAL_SERVER_ERROR, NULL);
        return;
    }

    /* Prepare Library */
    if (!seafile_prepare_library(server, repo_id, &error)) {
        /* Rollback: Delete Library */
        seafile_delete_library(server, repo_id, NULL);
        g_free(repo_id);
        soup_server_message_set_status(msg, SOUP_STATUS_INTERNAL_SERVER_ERROR, NULL);
        return;
    }

    /* Success Response */
    response_json = g_strdup_printf("{\"repo_id\": \"%s\"}", repo_id);
    g_free(repo_id);

    soup_server_message_set_status(msg, SOUP_STATUS_CREATED, NULL);
    soup_server_message_set_response(msg, "application/json",
                                      SOUP_MEMORY_TAKE, response_json,
                                      strlen(response_json));
}

/**
 * handle_seafile_library_delete:
 *
 * DELETE /api/seafile/library
 * Query: ?reg_jahr=2025&reg_nr=123
 * oder Body: {"repo_id": "abc-123-def"}
 */
static void handle_seafile_library_delete(SoupServer *soup_server,
                                           SoupServerMessage *msg,
                                           const char *path,
                                           GHashTable *query,
                                           gpointer user_data) {
    SondServer *server = SOND_SERVER(user_data);
    GError *error = NULL;
    gchar *repo_id = NULL;

    /* Option 1: Via Query Parameters */
    if (query) {
        const gchar *jahr_str = g_hash_table_lookup(query, "reg_jahr");
        const gchar *nr_str = g_hash_table_lookup(query, "reg_nr");

        if (jahr_str && nr_str) {
            gint reg_jahr = atoi(jahr_str);
            gint reg_nr = atoi(nr_str);

            repo_id = seafile_search_library(server, reg_jahr, reg_nr, &error);
        }
    }

    /* Option 2: Via Body (repo_id direkt) */
    if (!repo_id) {
    	SoupMessageBody *request_body = soup_server_message_get_request_body(msg);
    	if (request_body && request_body->length > 0) {
    	    GBytes *body = soup_message_body_flatten(request_body);
            if (body) {
    	    // ... parse JSON ...
                JsonParser *parser = json_parser_new();
                if (json_parser_load_from_data(parser,
                                               g_bytes_get_data(body, NULL),
                                               -1, NULL)) {
                    JsonNode *root = json_parser_get_root(parser);
                    if (JSON_NODE_HOLDS_OBJECT(root)) {
                        JsonObject *obj = json_node_get_object(root);
                        if (json_object_has_member(obj, "repo_id")) {
                            repo_id = g_strdup(json_object_get_string_member(obj, "repo_id"));
                        }
                    }
                }
                g_object_unref(parser);
            }

    	    g_bytes_unref(body);
    	}
    }

    if (!repo_id) {
        soup_server_message_set_status(msg, SOUP_STATUS_NOT_FOUND, NULL);
        return;
    }

    /* Delete Library */
    if (!seafile_delete_library(server, repo_id, &error)) {
        g_free(repo_id);
        soup_server_message_set_status(msg, SOUP_STATUS_INTERNAL_SERVER_ERROR, NULL);
        return;
    }

    g_free(repo_id);
    soup_server_message_set_status(msg, SOUP_STATUS_NO_CONTENT, NULL);
}

/* ========================================================================
 * Public API - Registration
 * ======================================================================== */

/**
 * sond_server_seafile_register_handlers:
 *
 * Registriert Seafile-Endpoints.
 */
void sond_server_seafile_register_handlers(SondServer *server) {
    soup_server_add_handler(server->soup_server,
                           "/api/seafile/library",
                           handle_seafile_library_create,
                           server, NULL);

    soup_server_add_handler(server->soup_server,
                           "/api/seafile/library",
                           handle_seafile_library_delete,
                           server, NULL);
}

/**
 * sond_server_seafile_get_auth_token:
 *
 * Holt einen Auth-Token von Seafile.
 */
gchar* sond_server_seafile_get_auth_token(SondServer *server,
                                           const gchar *user,
                                           const gchar *password,
                                           GError **error) {
    SoupSession *session = NULL;
    SoupMessage *msg = NULL;
    gchar *url = NULL;
    gchar *body_str = NULL;
    GBytes *body = NULL;
    GBytes *response = NULL;
    gchar *auth_token = NULL;

    url = g_strdup_printf("%s/api2/auth-token/", server->seafile_url);
    session = soup_session_new();
    msg = soup_message_new("POST", url);
    g_free(url);

    body_str = g_strdup_printf("username=%s&password=%s", user, password);
    body = g_bytes_new(body_str, strlen(body_str));
    g_free(body_str);

    soup_message_set_request_body_from_bytes(msg,
                                              "application/x-www-form-urlencoded",
                                              body);
    g_bytes_unref(body);

    response = soup_session_send_and_read(session, msg, NULL, error);

    if (!response) {
        g_object_unref(msg);
        g_object_unref(session);
        return NULL;
    }

    /* Parse Response */
    JsonParser *parser = json_parser_new();
    if (json_parser_load_from_data(parser,
                                    g_bytes_get_data(response, NULL),
                                    -1, error)) {
        JsonNode *root = json_parser_get_root(parser);
        if (JSON_NODE_HOLDS_OBJECT(root)) {
            JsonObject *obj = json_node_get_object(root);
            if (json_object_has_member(obj, "token")) {
                auth_token = g_strdup(json_object_get_string_member(obj, "token"));
            }
        }
    }

    g_object_unref(parser);
    g_bytes_unref(response);
    g_object_unref(msg);
    g_object_unref(session);

    return auth_token;
}
