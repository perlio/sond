/*
 sond (sond_client.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_client.h"
#include "../sond_log_and_error.h"
#include "../sond_server/sond_graph/sond_graph_node.h"

#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

struct _SondClient {
    GObject parent_instance;
    
    /* Konfiguration */
    gchar *server_url;
    guint server_port;
    
    /* Authentifizierung */
    gchar *session_token;  /* Session-Token vom Server */
    gchar *username;       /* Seafile-Username */
    
    /* Auth-Callback */
    void (*auth_failed_callback)(SondClient *client, gpointer user_data);
    gpointer auth_failed_user_data;
    
    /* HTTP-Client */
    SoupSession *session;
    gboolean connected;
};

G_DEFINE_TYPE(SondClient, sond_client, G_TYPE_OBJECT)

static void sond_client_finalize(GObject *object) {
    SondClient *self = SOND_CLIENT(object);
    
    g_free(self->server_url);
    g_free(self->session_token);
    g_free(self->username);
    
    if (self->session) {
        g_object_unref(self->session);
    }
    
    G_OBJECT_CLASS(sond_client_parent_class)->finalize(object);
}

static void sond_client_class_init(SondClientClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = sond_client_finalize;
}

static void sond_client_init(SondClient *self) {
    self->server_url = NULL;
    self->server_port = 0;
    self->session = NULL;
    self->connected = FALSE;
    self->session_token = NULL;
    self->username = NULL;
    self->auth_failed_callback = NULL;
    self->auth_failed_user_data = NULL;
}

/**
 * sond_client_load_config:
 */
static gboolean sond_client_load_config(SondClient *client,
                                        const gchar *config_file,
                                        GError **error) {
    GKeyFile *keyfile = g_key_file_new();
    
    if (!g_key_file_load_from_file(keyfile, config_file,
                                    G_KEY_FILE_NONE, error)) {
        g_key_file_free(keyfile);
        return FALSE;
    }
    
    /* Server Config */
    gchar *host = g_key_file_get_string(keyfile, "server", "host", NULL);
    client->server_port = g_key_file_get_integer(keyfile, "server", "port", NULL);
    
    if (!host || client->server_port == 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Invalid server configuration");
        g_free(host);
        g_key_file_free(keyfile);
        return FALSE;
    }
    
    /* Server-URL zusammenbauen */
    client->server_url = g_strdup_printf("http://%s:%u", host, client->server_port);
    g_free(host);
    
    g_key_file_free(keyfile);
    return TRUE;
}

SondClient* sond_client_new(const gchar *config_file, GError **error) {
    SondClient *client = g_object_new(SOND_TYPE_CLIENT, NULL);
    
    if (!sond_client_load_config(client, config_file, error)) {
        g_object_unref(client);
        return NULL;
    }
    
    /* HTTP-Session erstellen */
    client->session = soup_session_new();
    
    return client;
}

const gchar* sond_client_get_server_url(SondClient *client) {
    g_return_val_if_fail(SOND_IS_CLIENT(client), NULL);
    return client->server_url;
}

gboolean sond_client_is_connected(SondClient *client) {
    g_return_val_if_fail(SOND_IS_CLIENT(client), FALSE);
    return client->connected;
}

gboolean sond_client_connect(SondClient *client, GError **error) {
    g_return_val_if_fail(SOND_IS_CLIENT(client), FALSE);
    
    /* Einfacher Ping-Test - prüfe nur ob Server erreichbar ist */
    /* TODO: Besseren Health-Check-Endpunkt implementieren */
    
    client->connected = TRUE;
    LOG_INFO("Assuming connection to server at %s\n", client->server_url);
    
    return TRUE;
}

const gchar* sond_client_get_user_id(SondClient *client) {
    g_return_val_if_fail(SOND_IS_CLIENT(client), NULL);
    return client->username;  /* Username statt username@hostname */
}

void sond_client_set_auth(SondClient *client,
                          const gchar *username,
                          const gchar *session_token) {
    g_return_if_fail(SOND_IS_CLIENT(client));
    
    g_free(client->username);
    g_free(client->session_token);
    
    client->username = g_strdup(username);
    client->session_token = g_strdup(session_token);
    
    LOG_INFO("Auth set for user '%s'\n", username);
}

void sond_client_set_auth_failed_callback(SondClient *client,
                                          void (*callback)(SondClient*, gpointer),
                                          gpointer user_data) {
    g_return_if_fail(SOND_IS_CLIENT(client));
    
    client->auth_failed_callback = callback;
    client->auth_failed_user_data = user_data;
}

/**
 * add_auth_header:
 *
 * Fügt Authorization-Header zu Request hinzu.
 */
static void add_auth_header(SondClient *client, SoupMessage *msg) {
    if (client->session_token) {
        gchar *auth_value = g_strdup_printf("Bearer %s", client->session_token);
        soup_message_headers_append(soup_message_get_request_headers(msg),
                                    "Authorization", auth_value);
        g_free(auth_value);
    }
}

/**
 * check_auth_failed:
 *
 * Prüft ob Response 401 ist und ruft Callback auf.
 * 
 * Returns: TRUE wenn 401 (Auth failed), FALSE sonst
 */
static gboolean check_auth_failed(SondClient *client, guint status) {
    if (status == 401) {
        LOG_ERROR("Authentication failed (401) - session expired or invalid\n");
        
        /* Callback aufrufen falls gesetzt */
        if (client->auth_failed_callback) {
            client->auth_failed_callback(client, client->auth_failed_user_data);
        }
        
        return TRUE;
    }
    return FALSE;
}

gboolean sond_client_create_and_lock_node(SondClient *client,
                                           gpointer node_ptr,
                                           const gchar *lock_reason,
                                           GError **error) {
    g_return_val_if_fail(SOND_IS_CLIENT(client), FALSE);
    g_return_val_if_fail(node_ptr != NULL, FALSE);
    
    SondGraphNode *node = SOND_GRAPH_NODE(node_ptr);
    gchar *node_json = sond_graph_node_to_json(node);
    
    if (!node_json) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to serialize node");
        return FALSE;
    }
    
    /* Request-Body zusammenbauen */
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    
    /* Node als Sub-Object einfügen */
    json_builder_set_member_name(builder, "node");
    JsonParser *node_parser = json_parser_new();
    if (json_parser_load_from_data(node_parser, node_json, -1, NULL)) {
        JsonNode *node_node = json_parser_get_root(node_parser);
        json_builder_add_value(builder, json_node_copy(node_node));
    }
    g_object_unref(node_parser);
    g_free(node_json);
    
    json_builder_set_member_name(builder, "user_id");
    json_builder_add_string_value(builder, client->username);  /* Username statt user_id */
    
    if (lock_reason) {
        json_builder_set_member_name(builder, "lock_reason");
        json_builder_add_string_value(builder, lock_reason);
    }
    
    json_builder_end_object(builder);
    
    JsonGenerator *generator = json_generator_new();
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(generator, root);
    gchar *request_json = json_generator_to_data(generator, NULL);
    
    json_node_free(root);
    g_object_unref(generator);
    g_object_unref(builder);
    
    /* HTTP-Request */
    gchar *url = g_strdup_printf("%s/node/create_and_lock", client->server_url);
    SoupMessage *msg = soup_message_new("POST", url);
    g_free(url);
    
    /* Authorization-Header hinzufügen */
    add_auth_header(client, msg);
    
    soup_message_set_request_body_from_bytes(msg, "application/json",
        g_bytes_new_take(request_json, strlen(request_json)));
    
    GBytes *response = soup_session_send_and_read(client->session, msg, NULL, error);
    
    if (!response) {
        g_object_unref(msg);
        return FALSE;
    }
    
    guint status = soup_message_get_status(msg);
    
    /* 401-Check */
    if (check_auth_failed(client, status)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                   "Authentication failed - please login again");
        g_bytes_unref(response);
        g_object_unref(msg);
        return FALSE;
    }
    
    if (status != 200) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Server returned status %u", status);
        g_bytes_unref(response);
        g_object_unref(msg);
        return FALSE;
    }
    
    /* Response parsen und Node-ID aktualisieren */
    gsize size;
    const gchar *data = g_bytes_get_data(response, &size);
    
    JsonParser *parser = json_parser_new();
    if (json_parser_load_from_data(parser, data, size, NULL)) {
        JsonNode *root_node = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root_node);
        
        if (json_object_has_member(obj, "data")) {
            JsonNode *data_node = json_object_get_member(obj, "data");
            JsonGenerator *data_gen = json_generator_new();
            json_generator_set_root(data_gen, data_node);
            gchar *saved_json = json_generator_to_data(data_gen, NULL);
            
            /* Node aus Response aktualisieren (enthält jetzt ID) */
            SondGraphNode *updated_node = sond_graph_node_from_json(saved_json, NULL);
            if (updated_node) {
                /* ID übernehmen */
                gint64 node_id = sond_graph_node_get_id(updated_node);
                sond_graph_node_set_id(node, node_id);
                g_object_unref(updated_node);
            }
            
            g_free(saved_json);
            g_object_unref(data_gen);
        }
    }
    
    g_object_unref(parser);
    g_bytes_unref(response);
    g_object_unref(msg);
    
    return TRUE;
}

gboolean sond_client_lock_node(SondClient *client,
                                gint64 node_id,
                                const gchar *lock_reason,
                                GError **error) {
    g_return_val_if_fail(SOND_IS_CLIENT(client), FALSE);
    g_return_val_if_fail(node_id > 0, FALSE);
    
    /* Request-Body */
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    
    json_builder_set_member_name(builder, "user_id");
    json_builder_add_string_value(builder, client->username);  /* Username statt user_id */
    
    json_builder_set_member_name(builder, "timeout_minutes");
    json_builder_add_int_value(builder, 0);  /* Kein Timeout */
    
    if (lock_reason) {
        json_builder_set_member_name(builder, "reason");
        json_builder_add_string_value(builder, lock_reason);
    }
    
    json_builder_end_object(builder);
    
    JsonGenerator *generator = json_generator_new();
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(generator, root);
    gchar *request_json = json_generator_to_data(generator, NULL);
    
    json_node_free(root);
    g_object_unref(generator);
    g_object_unref(builder);
    
    /* HTTP-Request */
    gchar *url = g_strdup_printf("%s/node/lock/%" G_GINT64_FORMAT, 
                                client->server_url, node_id);
    SoupMessage *msg = soup_message_new("POST", url);
    g_free(url);
    
    /* Authorization-Header hinzufügen */
    add_auth_header(client, msg);
    
    soup_message_set_request_body_from_bytes(msg, "application/json",
        g_bytes_new_take(request_json, strlen(request_json)));
    
    GBytes *response = soup_session_send_and_read(client->session, msg, NULL, error);
    
    if (!response) {
        g_object_unref(msg);
        return FALSE;
    }
    
    guint status = soup_message_get_status(msg);
    
    /* 401-Check */
    if (check_auth_failed(client, status)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                   "Authentication failed - please login again");
        g_bytes_unref(response);
        g_object_unref(msg);
        return FALSE;
    }
    
    gboolean success = (status == 200);
    
    if (!success) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Lock failed with status %u", status);
    }
    
    g_bytes_unref(response);
    g_object_unref(msg);
    
    return success;
}

gboolean sond_client_unlock_node(SondClient *client,
                                  gint64 node_id,
                                  GError **error) {
    g_return_val_if_fail(SOND_IS_CLIENT(client), FALSE);
    g_return_val_if_fail(node_id > 0, FALSE);
    
    /* Request-Body */
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    
    json_builder_set_member_name(builder, "user_id");
    json_builder_add_string_value(builder, client->username);  /* Username statt user_id */
    
    json_builder_end_object(builder);
    
    JsonGenerator *generator = json_generator_new();
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(generator, root);
    gchar *request_json = json_generator_to_data(generator, NULL);
    
    json_node_free(root);
    g_object_unref(generator);
    g_object_unref(builder);
    
    /* HTTP-Request */
    gchar *url = g_strdup_printf("%s/node/unlock/%" G_GINT64_FORMAT,
                                client->server_url, node_id);
    SoupMessage *msg = soup_message_new("POST", url);
    g_free(url);
    
    /* Authorization-Header hinzufügen */
    add_auth_header(client, msg);
    
    soup_message_set_request_body_from_bytes(msg, "application/json",
        g_bytes_new_take(request_json, strlen(request_json)));
    
    GBytes *response = soup_session_send_and_read(client->session, msg, NULL, error);
    
    if (!response) {
        g_object_unref(msg);
        return FALSE;
    }
    
    guint status = soup_message_get_status(msg);
    
    /* 401-Check */
    if (check_auth_failed(client, status)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                   "Authentication failed - please login again");
        g_bytes_unref(response);
        g_object_unref(msg);
        return FALSE;
    }

    gboolean success = (status == 200);
    
    if (!success) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Unlock failed with status %u: %s", status,
				   g_bytes_get_data(response, NULL));
    }
    
    g_bytes_unref(response);
    g_object_unref(msg);
    
    return success;
}

gchar* sond_client_check_lock(SondClient *client,
                               gint64 node_id,
                               GError **error) {
    g_return_val_if_fail(SOND_IS_CLIENT(client), NULL);
    g_return_val_if_fail(node_id > 0, NULL);
    
    /* TODO: Server-Endpoint /node/lock/check/{id} implementieren */
    /* Vorerst NULL zurückgeben (nicht implementiert) */
    
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
               "check_lock not yet implemented on server");
    return NULL;
}
