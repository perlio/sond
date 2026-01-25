/*
 sond (sond_server_seafile.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_server_seafile.h"
#include "../sond_log_and_error.h"

#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <string.h>

/* Forward declarations */
typedef struct _SondServer SondServer;
extern gchar const* sond_server_get_seafile_url(SondServer *server);
extern gchar const* sond_server_get_seafile_token(SondServer *server);
extern guint sond_server_get_seafile_group_id(SondServer *server);

/**
 * sond_server_seafile_get_auth_token:
 *
 * Authentifiziert bei Seafile und holt Auth-Token.
 * 
 * Seafile API:
 * POST https://seafile.example.com/api2/auth-token/
 * Body: username=xxx&password=yyy (form-encoded)
 * Response: {"token": "abc123..."}
 */
gchar* sond_server_seafile_get_auth_token(SondServer *server,
                                           const gchar *user,
                                           const gchar *password,
                                           GError **error) {
    g_return_val_if_fail(server != NULL, NULL);
    g_return_val_if_fail(user != NULL, NULL);
    g_return_val_if_fail(password != NULL, NULL);
    
    const gchar *seafile_url = sond_server_get_seafile_url(server);
    if (!seafile_url) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Seafile URL not configured");
        return NULL;
    }
    
    /* URL: https://seafile.example.com/api2/auth-token/ */
    gchar *url = g_strdup_printf("%s/api2/auth-token/", seafile_url);
    
    /* Form-Data: username=xxx&password=yyy */
    gchar *form_data = g_strdup_printf("username=%s&password=%s", user, password);
    
    SoupSession *session = soup_session_new();
    SoupMessage *msg = soup_message_new("POST", url);
    g_free(url);
    
    /* Content-Type: application/x-www-form-urlencoded */
    soup_message_headers_set_content_type(soup_message_get_request_headers(msg),
                                          "application/x-www-form-urlencoded", NULL);
    
    soup_message_set_request_body_from_bytes(msg, "application/x-www-form-urlencoded",
        g_bytes_new_take(form_data, strlen(form_data)));
    
    GBytes *response = soup_session_send_and_read(session, msg, NULL, error);
    
    if (!response) {
        g_object_unref(msg);
        g_object_unref(session);
        return NULL;
    }
    
    guint status = soup_message_get_status(msg);
    
    if (status != 200) {
        gsize size;
        const gchar *body = g_bytes_get_data(response, &size);
        
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Seafile authentication failed (HTTP %u): %.*s",
                   status, (int)size, body);
        
        g_bytes_unref(response);
        g_object_unref(msg);
        g_object_unref(session);
        return NULL;
    }
    
    /* Response parsen: {"token": "abc123..."} */
    gsize size;
    const gchar *data = g_bytes_get_data(response, &size);
    
    JsonParser *parser = json_parser_new();
    gchar *token = NULL;
    
    if (json_parser_load_from_data(parser, data, size, NULL)) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);
        
        if (json_object_has_member(obj, "token")) {
            const gchar *token_str = json_object_get_string_member(obj, "token");
            token = g_strdup(token_str);
        } else {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Seafile response missing 'token' field");
        }
    } else {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to parse Seafile response");
    }
    
    g_object_unref(parser);
    g_bytes_unref(response);
    g_object_unref(msg);
    g_object_unref(session);
    
    return token;
}

/**
 * seafile_create_library:
 * 
 * Erstellt eine neue Seafile Library.
 * 
 * Seafile API:
 * POST https://seafile.example.com/api2/repos/
 * Authorization: Token xxx
 * Body: name=LibraryName&desc=Description
 */
static gchar* seafile_create_library(SondServer *server,
                                      const gchar *name,
                                      const gchar *desc,
                                      GError **error) {
    g_return_val_if_fail(server != NULL, NULL);
    g_return_val_if_fail(name != NULL, NULL);
    
    gchar const* seafile_url = sond_server_get_seafile_url(server);
    if (!seafile_url) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Seafile URL not configured");
        return NULL;
    }
    
    gchar const* auth_token = sond_server_get_seafile_token(server);
    if (!auth_token) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Seafile auth token not available");
        return NULL;
    }
    
    guint group_id = sond_server_get_seafile_group_id(server);
    
    /* URL: https://seafile.example.com/api2/repos/ */
    gchar *url = g_strdup_printf("%s/api2/repos/", seafile_url);
    
    /* Form-Data: name=xxx&desc=yyy */
    gchar *form_data;
    if (desc && strlen(desc) > 0) {
        form_data = g_strdup_printf("name=%s&desc=%s", name, desc);
    } else {
        form_data = g_strdup_printf("name=%s", name);
    }
    
    SoupSession *session = soup_session_new();
    SoupMessage *msg = soup_message_new("POST", url);
    g_free(url);
    
    /* Authorization Header */
    gchar *auth_header = g_strdup_printf("Token %s", auth_token);
    soup_message_headers_append(soup_message_get_request_headers(msg),
                               "Authorization", auth_header);
    g_free(auth_header);
    
    /* Content-Type */
    soup_message_headers_set_content_type(soup_message_get_request_headers(msg),
                                          "application/x-www-form-urlencoded", NULL);
    
    soup_message_set_request_body_from_bytes(msg, "application/x-www-form-urlencoded",
        g_bytes_new_take(form_data, strlen(form_data)));
    
    GBytes *response = soup_session_send_and_read(session, msg, NULL, error);
    
    if (!response) {
        g_object_unref(msg);
        g_object_unref(session);
        return NULL;
    }
    
    guint status = soup_message_get_status(msg);
    
    if (status != 200 && status != 201) {
        gsize size;
        const gchar *body = g_bytes_get_data(response, &size);
        
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to create library (HTTP %u): %.*s",
                   status, (int)size, body);
        
        g_bytes_unref(response);
        g_object_unref(msg);
        g_object_unref(session);
        return NULL;
    }
    
    /* Response parsen: {"repo_id": "xxx", "repo_name": "yyy", ...} */
    gsize size;
    const gchar *data = g_bytes_get_data(response, &size);
    
    JsonParser *parser = json_parser_new();
    gchar *library_id = NULL;
    
    if (json_parser_load_from_data(parser, data, size, NULL)) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);
        
        if (json_object_has_member(obj, "repo_id")) {
            const gchar *repo_id = json_object_get_string_member(obj, "repo_id");
            library_id = g_strdup(repo_id);
        } else {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Seafile response missing 'repo_id' field");
        }
    } else {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to parse Seafile response");
    }
    
    g_object_unref(parser);
    g_bytes_unref(response);
    g_object_unref(msg);
    g_object_unref(session);
    
    return library_id;
}

/**
 * seafile_delete_library:
 * 
 * Löscht eine Seafile Library.
 * 
 * Seafile API:
 * DELETE https://seafile.example.com/api2/repos/{repo_id}/
 * Authorization: Token xxx
 */
static gboolean seafile_delete_library(SondServer *server,
                                        const gchar *library_id,
                                        GError **error) {
    g_return_val_if_fail(server != NULL, FALSE);
    g_return_val_if_fail(library_id != NULL, FALSE);
    
    gchar const* seafile_url = sond_server_get_seafile_url(server);
    if (!seafile_url) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Seafile URL not configured");
        return FALSE;
    }
    
    gchar const* auth_token = sond_server_get_seafile_token(server);
    if (!auth_token) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Seafile auth token not available");
        return FALSE;
    }
    
    LOG_INFO("Deleting Seafile library with ID '%s'...\n", library_id);
    
    /* URL: https://seafile.example.com/api2/repos/{id}/ */
    gchar *url = g_strdup_printf("%s/api2/repos/%s/", seafile_url, library_id);
    
    SoupSession *session = soup_session_new();
    SoupMessage *msg = soup_message_new("DELETE", url);
    g_free(url);
    
    /* Authorization Header */
    gchar *auth_header = g_strdup_printf("Token %s", auth_token);
    soup_message_headers_append(soup_message_get_request_headers(msg),
                               "Authorization", auth_header);
    g_free(auth_header);
    
    GBytes *response = soup_session_send_and_read(session, msg, NULL, error);
    
    if (!response) {
        g_object_unref(msg);
        g_object_unref(session);
        return FALSE;
    }
    
    guint status = soup_message_get_status(msg);
    gboolean success = (status == 200 || status == 204);
    
    if (!success) {
        gsize size;
        const gchar *body = g_bytes_get_data(response, &size);
        
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to delete library (HTTP %u): %.*s",
                   status, (int)size, body);
    } else {
        LOG_INFO("Seafile library '%s' deleted\n", library_id);
    }
    
    g_bytes_unref(response);
    g_object_unref(msg);
    g_object_unref(session);
    
    return success;
}

/* ========================================================================
 * Request Handlers
 * ======================================================================== */

/**
 * seafile_get_library_id_by_name:
 *
 * Sucht eine Library anhand des Namens und gibt die ID zurück.
 *
 * Seafile API:
 * GET https://seafile.example.com/api2/repos/?nameContains=XXX
 * Authorization: Token xxx
 * Response: [{"repo_id": "xxx", "repo_name": "yyy", ...}, ...]
 */
static gchar* seafile_get_library_id_by_name(SondServer *server,
                                               const gchar *library_name,
                                               GError **error) {
    g_return_val_if_fail(server != NULL, NULL);
    g_return_val_if_fail(library_name != NULL, NULL);

    gchar const* seafile_url = sond_server_get_seafile_url(server);
    if (!seafile_url) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Seafile URL not configured");
        return NULL;
    }

    gchar const* auth_token = sond_server_get_seafile_token(server);
    if (!auth_token) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Seafile auth token not available");
        return NULL;
    }

    /* URL mit nameContains Filter: https://seafile.example.com/api2/repos/?nameContains=XXX */
    gchar *url = g_strdup_printf("%s/api2/repos/?nameContains=%s", seafile_url, library_name);

    SoupSession *session = soup_session_new();
    SoupMessage *msg = soup_message_new("GET", url);
    g_free(url);

    /* Authorization Header */
    gchar *auth_header = g_strdup_printf("Token %s", auth_token);
    soup_message_headers_append(soup_message_get_request_headers(msg),
                               "Authorization", auth_header);
    g_free(auth_header);

    GBytes *response = soup_session_send_and_read(session, msg, NULL, error);

    if (!response) {
        g_object_unref(msg);
        g_object_unref(session);
        return NULL;
    }

    guint status = soup_message_get_status(msg);

    if (status != 200) {
        gsize size;
        const gchar *body = g_bytes_get_data(response, &size);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to search libraries (HTTP %u): %.*s",
                   status, (int)size, body);

        g_bytes_unref(response);
        g_object_unref(msg);
        g_object_unref(session);
        return NULL;
    }

    /* Response parsen: [{"repo_id": "xxx", "repo_name": "yyy"}, ...] */
    gsize size;
    const gchar *data = g_bytes_get_data(response, &size);

    JsonParser *parser = json_parser_new();
    gchar *library_id = NULL;

    if (json_parser_load_from_data(parser, data, size, NULL)) {
        JsonNode *root = json_parser_get_root(parser);
        JsonArray *repos = json_node_get_array(root);

        /* Exakte Übereinstimmung suchen (nameContains kann auch Teilstrings finden) */
        for (guint i = 0; i < json_array_get_length(repos); i++) {
            JsonObject *repo = json_array_get_object_element(repos, i);

            if (json_object_has_member(repo, "name")) {
                const gchar *name = json_object_get_string_member(repo, "name");
                /* EXAKTER Match! */
                if (name && g_strcmp0(name, library_name) == 0) {
                    if (json_object_has_member(repo, "id")) {
                        const gchar *id = json_object_get_string_member(repo, "id");
                        library_id = g_strdup(id);
                        break;
                    }
                }
            }
        }

        if (!library_id) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                       "Library '%s' not found", library_name);
        }
    } else {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to parse Seafile response");
    }

    g_object_unref(parser);
    g_bytes_unref(response);
    g_object_unref(msg);
    g_object_unref(session);

    return library_id;
}

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

static void send_error_response(SoupServerMessage *msg,
                                 guint status_code,
                                 const gchar *error_message) {
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "success");
    json_builder_add_boolean_value(builder, FALSE);
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

static void send_success_response(SoupServerMessage *msg,
                                   const gchar *data_json) {
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "success");
    json_builder_add_boolean_value(builder, TRUE);

    if (data_json) {
        json_builder_set_member_name(builder, "data");
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
 * handle_seafile_library_create:
 * POST /seafile/library
 * 
 * Body: {"name": "2026-23", "desc": "optional"}
 * Response: {"success": true, "data": {"library_id": "xxx", "name": "2026-23"}}
 */
static void handle_seafile_library_create(SoupServer *soup_server,
                                          SoupServerMessage *msg,
                                          const char *path,
                                          GHashTable *query,
                                          gpointer user_data) {
    SondServer *server = (SondServer*)user_data;

    if (strcmp(soup_server_message_get_method(msg), "POST") != 0) {
        send_error_response(msg, SOUP_STATUS_METHOD_NOT_ALLOWED, "Only POST allowed");
        return;
    }

    SoupMessageBody *body = soup_server_message_get_request_body(msg);
    if (!body->data) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST, "Empty request body");
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

    const gchar *name = json_object_get_string_member(obj, "name");
    const gchar *desc = json_object_has_member(obj, "desc") ?
        json_object_get_string_member(obj, "desc") : NULL;

    if (!name || strlen(name) == 0) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST, "Missing 'name' field");
        g_object_unref(parser);

        return;
    }

    /* Library erstellen */
    gchar *library_id = seafile_create_library(server, name, desc, &error);

    if (!library_id) {
        gchar *err_msg = g_strdup_printf("Failed to create library: %s",
                                        error ? error->message : "unknown");
        send_error_response(msg, SOUP_STATUS_INTERNAL_SERVER_ERROR, err_msg);
        g_free(err_msg);
        g_clear_error(&error);
        g_object_unref(parser);

        return;
    }

    /* Response */
    JsonBuilder *response_builder = json_builder_new();
    json_builder_begin_object(response_builder);
    json_builder_set_member_name(response_builder, "library_id");
    json_builder_add_string_value(response_builder, library_id);
    json_builder_set_member_name(response_builder, "name");
    json_builder_add_string_value(response_builder, name);
    json_builder_end_object(response_builder);

    g_object_unref(parser);

    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(response_builder));
    gchar *response_json = json_generator_to_data(gen, NULL);

    send_success_response(msg, response_json);

    g_free(response_json);
    g_object_unref(gen);
    g_object_unref(response_builder);
}

/**
 * handle_seafile_library_delete:
 * DELETE /seafile/library/{library_id}
 * 
 * Response: {"success": true}
 */
static void handle_seafile_library_delete(SoupServer *soup_server,
                                          SoupServerMessage *msg,
                                          const char *path,
                                          GHashTable *query,
                                          gpointer user_data) {
    SondServer *server = (SondServer*)user_data;

    if (strcmp(soup_server_message_get_method(msg), "DELETE") != 0) {
        send_error_response(msg, SOUP_STATUS_METHOD_NOT_ALLOWED, "Only DELETE allowed");
        return;
    }

    /* Library-ID aus Path: /seafile/library/{id} */
    const char *id_str = strrchr(path, '/');
    if (!id_str || strlen(id_str) <= 1) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST, "Missing library ID in path");
        return;
    }
    id_str++;

    /* Library löschen */
    GError *error = NULL;
    if (!seafile_delete_library(server, id_str, &error)) {
        gchar *err_msg = g_strdup_printf("Failed to delete library: %s",
                                        error ? error->message : "unknown");
        send_error_response(msg, SOUP_STATUS_INTERNAL_SERVER_ERROR, err_msg);
        g_free(err_msg);
        g_clear_error(&error);
        return;
    }

    send_success_response(msg, NULL);
}

/**
 * handle_seafile_library_id_get:
 * GET /seafile/library-id?name=XXX
 *
 * Response: {"library_id": "xxx"}
 */
static void handle_seafile_library_id_get(SoupServer *soup_server,
                                          SoupServerMessage *msg,
                                          const char *path,
                                          GHashTable *query,
                                          gpointer user_data) {
    SondServer *server = (SondServer*)user_data;

    if (strcmp(soup_server_message_get_method(msg), "GET") != 0) {
        send_error_response(msg, SOUP_STATUS_METHOD_NOT_ALLOWED, "Only GET allowed");
        return;
    }

    /* Query-Parameter 'name' holen */
    if (!query) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST, "Missing query parameters");
        return;
    }

    const gchar *library_name = g_hash_table_lookup(query, "name");
    if (!library_name || strlen(library_name) == 0) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST, "Missing 'name' query parameter");
        return;
    }

    /* Library-ID suchen */
    GError *error = NULL;
    gchar *library_id = seafile_get_library_id_by_name(server, library_name, &error);

    if (!library_id) {
        guint status_code = SOUP_STATUS_INTERNAL_SERVER_ERROR;

        /* 404 wenn nicht gefunden */
        if (error && error->code == G_IO_ERROR_NOT_FOUND) {
            status_code = SOUP_STATUS_NOT_FOUND;
        }

        gchar *err_msg = g_strdup_printf("Failed to find library: %s",
                                        error ? error->message : "unknown");
        send_error_response(msg, status_code, err_msg);
        g_free(err_msg);
        g_clear_error(&error);
        return;
    }

    /* Response: {"library_id": "xxx"} */
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "library_id");
    json_builder_add_string_value(builder, library_id);
    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(gen, root);
    gchar *response_json = json_generator_to_data(gen, NULL);

    send_json_response(msg, SOUP_STATUS_OK, response_json);

    g_free(library_id);
    g_free(response_json);
    json_node_free(root);
    g_object_unref(gen);
    g_object_unref(builder);
}

 /**
 * seafile_get_library_id_by_name:
 *
 * Sucht eine Library anhand des Namens und gibt die ID zurück.
 *
 * Seafile API:
 * GET https://seafile.example.com/api2/repos/
 * Authorization: Token xxx
 * Response: [{"repo_id": "xxx", "repo_name": "yyy", ...}, ...]
 */
void sond_server_seafile_register_handlers(SondServer *server) {
    extern SoupServer* sond_server_get_soup_server(SondServer *server);
    
    SoupServer *soup_server = sond_server_get_soup_server(server);
    
    soup_server_add_handler(soup_server, "/seafile/library",
                           handle_seafile_library_create, server, NULL);
    soup_server_add_handler(soup_server, "/seafile/library/",
                           handle_seafile_library_delete, server, NULL);
    soup_server_add_handler(soup_server, "/seafile/library-id",
                            handle_seafile_library_id_get, server, NULL);

    LOG_INFO("Seafile handlers registered\n");
}
