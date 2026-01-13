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

/* Forward declaration - wir brauchen Zugriff auf Server-Internals */
extern const gchar* sond_server_get_seafile_url(SondServer *server);

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
    
    LOG_INFO("Authenticating user '%s' with Seafile...\n", user);
    
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
            LOG_INFO("Seafile authentication successful for '%s'\n", user);
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

void sond_server_seafile_register_handlers(SondServer *server) {
    /* Placeholder für Seafile-Endpoints */
    LOG_INFO("Seafile handlers registered (placeholder)\n");
}
