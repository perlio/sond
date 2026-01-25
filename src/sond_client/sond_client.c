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
#include "sond_offline_manager.h"
#include "../sond_log_and_error.h"
#include "../sond_graph/sond_graph_db.h"
#include "../sond_graph/sond_graph_node.h"

#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

struct _SondClient {
    GObject parent_instance;
    
    /* Konfiguration */
    gchar *server_url;
    guint server_port;
    
    /* Authentifizierung */
    gchar *session_token;  /* Session-Token vom Server */
    gchar *seafile_token; /* Seafile Auth-Token für user */
    gchar *username;       /* Seafile-Username */
    gchar *seafile_url;    /* Seafile-Server URL */
    
    /* Zuletzt verwendete RegNr (während Laufzeit) */
    guint last_regnr_lfd;
    guint last_regnr_year;
    gboolean has_last_regnr;
    
    /* Auth-Callback */
    void (*auth_failed_callback)(SondClient *client, gpointer user_data);
    gpointer auth_failed_user_data;
    
    /* Lazy-Login Callback */
    gboolean (*login_callback)(SondClient *client, gpointer user_data);
    gpointer login_callback_user_data;
    
    /* Offline Manager */
    SondOfflineManager *offline_manager;
    
    /* HTTP-Client */
    SoupSession *session;
};

G_DEFINE_TYPE(SondClient, sond_client, G_TYPE_OBJECT)

static void sond_client_finalize(GObject *object) {
    SondClient *self = SOND_CLIENT(object);
    
    g_free(self->server_url);
    g_free(self->session_token);
    g_free(self->seafile_token);
    g_free(self->username);
    g_free(self->seafile_url);
    
    if (self->offline_manager) {
        g_object_unref(self->offline_manager);
    }
    
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
    self->session_token = NULL;
    self->username = NULL;
    self->auth_failed_callback = NULL;
    self->auth_failed_user_data = NULL;
    self->login_callback = NULL;
    self->login_callback_user_data = NULL;
    self->offline_manager = NULL;
    
    /* Letzte RegNr initialisieren */
    self->last_regnr_lfd = 0;
    self->last_regnr_year = 0;
    self->has_last_regnr = FALSE;
}

static gboolean sond_client_load_config(SondClient *client,
                                        const gchar *config_file,
                                        GError **error) {
    GKeyFile *keyfile = g_key_file_new();
    
    if (!g_key_file_load_from_file(keyfile, config_file,
                                    G_KEY_FILE_NONE, error)) {
        g_key_file_free(keyfile);
        return FALSE;
    }
    
    gchar *host = g_key_file_get_string(keyfile, "server", "host", NULL);
    client->server_port = g_key_file_get_integer(keyfile, "server", "port", NULL);
    
    if (!host || client->server_port == 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Invalid server configuration");
        g_free(host);
        g_key_file_free(keyfile);
        return FALSE;
    }
    
    client->server_url = g_strdup_printf("http://%s:%u", host, client->server_port);
    g_free(host);
    
    /* Offline sync_directory laden (optional) */
    gchar *sync_directory = g_key_file_get_string(keyfile, "offline", "sync_directory", NULL);
    if (!sync_directory) {
        /* Default: ~/.local/share/sond/offline-akten */
    	//bzw. unter Windows: %APPDATA%\sond\offline-akten
        const gchar *data_dir = g_get_user_data_dir();
        sync_directory = g_build_filename(data_dir, "sond_client", "offline-akten", NULL);
        LOG_INFO("No sync_directory configured, using default: %s\n", sync_directory);
    }
    
    /* Offline Manager erstellen */
    client->offline_manager = sond_offline_manager_new(sync_directory);
    g_free(sync_directory);
    
    if (!client->offline_manager) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to create offline manager");
        g_key_file_free(keyfile);
        return FALSE;
    }
    
    g_key_file_free(keyfile);
    return TRUE;
}

SondClient* sond_client_new(const gchar *config_file, GError **error) {
    SondClient *client = g_object_new(SOND_TYPE_CLIENT, NULL);
    
    if (!sond_client_load_config(client, config_file, error)) {
        g_object_unref(client);
        return NULL;
    }
    
    client->session = soup_session_new();
    
    return client;
}

const gchar* sond_client_get_server_url(SondClient *client) {
    g_return_val_if_fail(SOND_IS_CLIENT(client), NULL);
    return client->server_url;
}

const gchar* sond_client_get_seafile_url(SondClient *client) {
    g_return_val_if_fail(SOND_IS_CLIENT(client), NULL);
    return client->seafile_url;
}

const gchar* sond_client_get_user_id(SondClient *client) {
    g_return_val_if_fail(SOND_IS_CLIENT(client), NULL);
    return client->username;
}

const gchar* sond_client_get_user_token(SondClient *client) {
    g_return_val_if_fail(SOND_IS_CLIENT(client), NULL);
    return client->seafile_token;
}

void sond_client_set_auth(SondClient *client,
                          const gchar *username,
                          const gchar *session_token,
						  const gchar *seafile_token,
						  const gchar *seafile_url) {
    g_return_if_fail(SOND_IS_CLIENT(client));
    
    g_free(client->username);
    g_free(client->session_token);
    g_free(client->seafile_token);
    g_free(client->seafile_url);
    
    client->username = g_strdup(username);
    client->session_token = g_strdup(session_token);
    client->seafile_token = g_strdup(seafile_token);
    client->seafile_url = g_strdup(seafile_url);
}

void sond_client_set_auth_failed_callback(SondClient *client,
                                          void (*callback)(SondClient*, gpointer),
                                          gpointer user_data) {
    g_return_if_fail(SOND_IS_CLIENT(client));
    
    client->auth_failed_callback = callback;
    client->auth_failed_user_data = user_data;
}

void sond_client_set_login_callback(SondClient *client,
                                    gboolean (*callback)(SondClient*, gpointer),
                                    gpointer user_data) {
    g_return_if_fail(SOND_IS_CLIENT(client));
    
    client->login_callback = callback;
    client->login_callback_user_data = user_data;
}

static void add_auth_header(SondClient *client, SoupMessage *msg) {
    if (client->session_token) {
        gchar *auth_value = g_strdup_printf("Bearer %s", client->session_token);
        soup_message_headers_append(soup_message_get_request_headers(msg),
                                    "Authorization", auth_value);
        g_free(auth_value);
    }
}

static gboolean ensure_authenticated(SondClient *client, GError **error) {
    if (client->session_token && client->username) {
        return TRUE;
    }
    
    if (!client->login_callback) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                   "Not authenticated and no login callback set");
        return FALSE;
    }
    
    if (!client->login_callback(client, client->login_callback_user_data)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                   "Login failed or cancelled");
        return FALSE;
    }
    
    if (!client->session_token || !client->username) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                   "Login callback succeeded but no token was set");
        return FALSE;
    }
    
    return TRUE;
}

static gboolean handle_auth_error(SondClient *client, GError **error) {
    LOG_ERROR("=== Authentication failed (401) - session expired or invalid ===\n");
    
    g_free(client->session_token);
    g_free(client->username);
    client->session_token = NULL;
    client->username = NULL;
    
    if (client->auth_failed_callback) {
        client->auth_failed_callback(client, client->auth_failed_user_data);
    }
    
    if (!client->login_callback) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                   "Authentication failed and no login callback set");
        return FALSE;
    }
    
    LOG_INFO("=== Attempting re-login... ===\n");
    
    if (!client->login_callback(client, client->login_callback_user_data)) {
        LOG_ERROR("=== Re-login callback returned FALSE ===\n");
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                   "Re-login failed or cancelled");
        return FALSE;
    }
    
    if (!client->session_token || !client->username) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                   "Re-login succeeded but no token was set");
        return FALSE;
    }
    
    LOG_INFO("=== Re-login successful - token set ===\n");
    return TRUE;
}

gboolean sond_client_create_and_lock_node(SondClient *client,
                                           gpointer node_ptr,
                                           const gchar *lock_reason,
                                           GError **error) {
    g_return_val_if_fail(SOND_IS_CLIENT(client), FALSE);
    g_return_val_if_fail(node_ptr != NULL, FALSE);
    
    if (!ensure_authenticated(client, error)) {
        return FALSE;
    }
    
    SondGraphNode *node = SOND_GRAPH_NODE(node_ptr);
    gchar *node_json = sond_graph_node_to_json(node);
    
    if (!node_json) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to serialize node");
        return FALSE;
    }
    
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    
    json_builder_set_member_name(builder, "node");
    JsonParser *node_parser = json_parser_new();
    if (json_parser_load_from_data(node_parser, node_json, -1, NULL)) {
        JsonNode *node_node = json_parser_get_root(node_parser);
        json_builder_add_value(builder, json_node_copy(node_node));
    }
    g_object_unref(node_parser);
    g_free(node_json);
    
    json_builder_set_member_name(builder, "user_id");
    json_builder_add_string_value(builder, client->username);
    
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
    
    gboolean retry = TRUE;
    gboolean success = FALSE;
    
    while (retry) {
        retry = FALSE;
        
        gchar *url = g_strdup_printf("%s/node/create_and_lock", client->server_url);
        SoupMessage *msg = soup_message_new("POST", url);
        g_free(url);
        
        add_auth_header(client, msg);
        
        soup_message_set_request_body_from_bytes(msg, "application/json",
            g_bytes_new(request_json, strlen(request_json)));
        
        GBytes *response = soup_session_send_and_read(client->session, msg, NULL, error);
        
        if (!response) {
            g_object_unref(msg);
            break;
        }
        
        guint status = soup_message_get_status(msg);
        
        if (status == 401) {
            g_bytes_unref(response);
            g_object_unref(msg);
            
            if (handle_auth_error(client, error)) {
                retry = TRUE;
                continue;
            }
            break;
        }
        
        if (status != 200) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Server returned status %u", status);
            g_bytes_unref(response);
            g_object_unref(msg);
            break;
        }
        
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
                
                SondGraphNode *updated_node = sond_graph_node_from_json(saved_json, NULL);
                if (updated_node) {
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
        success = TRUE;
    }
    
    g_free(request_json);
    return success;
}

gboolean sond_client_lock_node(SondClient *client,
                                gint64 node_id,
                                const gchar *lock_reason,
                                GError **error) {
    g_return_val_if_fail(SOND_IS_CLIENT(client), FALSE);
    g_return_val_if_fail(node_id > 0, FALSE);
    
    if (!ensure_authenticated(client, error)) {
        return FALSE;
    }
    
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    
    json_builder_set_member_name(builder, "user_id");
    json_builder_add_string_value(builder, client->username);
    
    json_builder_set_member_name(builder, "timeout_minutes");
    json_builder_add_int_value(builder, 0);
    
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
    
    gboolean retry = TRUE;
    gboolean success = FALSE;
    
    while (retry) {
        retry = FALSE;
        
        gchar *url = g_strdup_printf("%s/node/lock/%" G_GINT64_FORMAT, 
                                    client->server_url, node_id);
        SoupMessage *msg = soup_message_new("POST", url);
        g_free(url);
        
        add_auth_header(client, msg);
        
        soup_message_set_request_body_from_bytes(msg, "application/json",
            g_bytes_new(request_json, strlen(request_json)));
        
        GBytes *response = soup_session_send_and_read(client->session, msg, NULL, error);
        
        if (!response) {
            g_object_unref(msg);
            break;
        }
        
        guint status = soup_message_get_status(msg);
        
        if (status == 401) {
            g_bytes_unref(response);
            g_object_unref(msg);
            
            if (handle_auth_error(client, error)) {
                retry = TRUE;
                continue;
            }
            break;
        }
        
        success = (status == 200);
        
        if (!success) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Lock failed with status %u", status);
        }
        
        g_bytes_unref(response);
        g_object_unref(msg);
    }
    
    g_free(request_json);
    return success;
}

gboolean sond_client_unlock_node(SondClient *client,
                                  gint64 node_id,
                                  GError **error) {
    g_return_val_if_fail(SOND_IS_CLIENT(client), FALSE);
    g_return_val_if_fail(node_id > 0, FALSE);
    
    if (!ensure_authenticated(client, error)) {
        return FALSE;
    }
    
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    
    json_builder_set_member_name(builder, "user_id");
    json_builder_add_string_value(builder, client->username);
    
    json_builder_end_object(builder);
    
    JsonGenerator *generator = json_generator_new();
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(generator, root);
    gchar *request_json = json_generator_to_data(generator, NULL);
    
    json_node_free(root);
    g_object_unref(generator);
    g_object_unref(builder);
    
    gboolean retry = TRUE;
    gboolean success = FALSE;
    
    while (retry) {
        retry = FALSE;
        
        gchar *url = g_strdup_printf("%s/node/unlock/%" G_GINT64_FORMAT,
                                    client->server_url, node_id);
        SoupMessage *msg = soup_message_new("POST", url);
        g_free(url);
        
        add_auth_header(client, msg);
        
        soup_message_set_request_body_from_bytes(msg, "application/json",
            g_bytes_new(request_json, strlen(request_json)));
        
        GBytes *response = soup_session_send_and_read(client->session, msg, NULL, error);
        
        if (!response) {
            g_object_unref(msg);
            break;
        }
        
        guint status = soup_message_get_status(msg);
        
        if (status == 401) {
            g_bytes_unref(response);
            g_object_unref(msg);
            
            if (handle_auth_error(client, error)) {
                retry = TRUE;
                continue;
            }
            break;
        }
        
        success = (status == 200);
        
        if (!success) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Unlock failed with status %u: %s", status,
                       (gchar*) g_bytes_get_data(response, NULL));
        }
        
        g_bytes_unref(response);
        g_object_unref(msg);
    }
    
    g_free(request_json);
    return success;
}

GPtrArray* sond_client_search_nodes(SondClient *client,
                                     gpointer criteria_ptr,
                                     GError **error) {
    g_return_val_if_fail(SOND_IS_CLIENT(client), NULL);
    g_return_val_if_fail(criteria_ptr != NULL, NULL);
    
    if (!ensure_authenticated(client, error)) {
        return NULL;
    }
    
    SondGraphNodeSearchCriteria *criteria = (SondGraphNodeSearchCriteria*)criteria_ptr;
    gchar *criteria_json = sond_graph_node_search_criteria_to_json(criteria);
    
    if (!criteria_json) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to serialize search criteria");
        return NULL;
    }
    
    gboolean retry = TRUE;
    GPtrArray *result_nodes = NULL;
    
    while (retry) {
        retry = FALSE;
        
        gchar *url = g_strdup_printf("%s/node/search", client->server_url);
        SoupMessage *msg = soup_message_new("POST", url);
        g_free(url);
        
        add_auth_header(client, msg);
        
        soup_message_set_request_body_from_bytes(msg, "application/json",
            g_bytes_new(criteria_json, strlen(criteria_json)));
        
        GBytes *response = soup_session_send_and_read(client->session, msg, NULL, error);
        
        if (!response) {
            g_object_unref(msg);
            break;
        }
        
        guint status = soup_message_get_status(msg);
        
        if (status == 401) {
            g_bytes_unref(response);
            g_object_unref(msg);
            
            if (handle_auth_error(client, error)) {
                retry = TRUE;
                continue;
            }
            break;
        }
        
        if (status != 200) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Server returned status %u", status);
            g_bytes_unref(response);
            g_object_unref(msg);
            break;
        }
        
        /* Response parsen */
        gsize size;
        const gchar *data = g_bytes_get_data(response, &size);
        
        JsonParser *parser = json_parser_new();
        if (json_parser_load_from_data(parser, data, size, NULL)) {
            JsonNode *root = json_parser_get_root(parser);
            JsonObject *obj = json_node_get_object(root);
            
            if (json_object_has_member(obj, "data")) {
                JsonArray *nodes_array = json_object_get_array_member(obj, "data");
                
                result_nodes = g_ptr_array_new_with_free_func(g_object_unref);
                
                for (guint i = 0; i < json_array_get_length(nodes_array); i++) {
                    JsonNode *node_json = json_array_get_element(nodes_array, i);
                    
                    JsonGenerator *gen = json_generator_new();
                    json_generator_set_root(gen, node_json);
                    gchar *node_str = json_generator_to_data(gen, NULL);
                    
                    SondGraphNode *node = sond_graph_node_from_json(node_str, NULL);
                    if (node) {
                        g_ptr_array_add(result_nodes, node);
                    }
                    
                    g_free(node_str);
                    g_object_unref(gen);
                }
            }
        }
        
        g_object_unref(parser);
        g_bytes_unref(response);
        g_object_unref(msg);
    }
    
    g_free(criteria_json);
    return result_nodes;
}

gboolean sond_client_save_node(SondClient *client,
                                gpointer node_ptr,
                                GError **error) {
    g_return_val_if_fail(SOND_IS_CLIENT(client), FALSE);
    g_return_val_if_fail(node_ptr != NULL, FALSE);
    
    if (!ensure_authenticated(client, error)) {
        return FALSE;
    }
    
    SondGraphNode *node = SOND_GRAPH_NODE(node_ptr);
    gchar *node_json = sond_graph_node_to_json(node);
    
    if (!node_json) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to serialize node");
        return FALSE;
    }
    
    gboolean retry = TRUE;
    gboolean success = FALSE;
    
    while (retry) {
        retry = FALSE;
        
        gchar *url = g_strdup_printf("%s/node/save", client->server_url);
        SoupMessage *msg = soup_message_new("POST", url);
        g_free(url);
        
        add_auth_header(client, msg);
        
        soup_message_set_request_body_from_bytes(msg, "application/json",
            g_bytes_new(node_json, strlen(node_json)));
        
        GBytes *response = soup_session_send_and_read(client->session, msg, NULL, error);
        
        if (!response) {
            g_object_unref(msg);
            break;
        }
        
        guint status = soup_message_get_status(msg);
        
        if (status == 401) {
            g_bytes_unref(response);
            g_object_unref(msg);
            
            if (handle_auth_error(client, error)) {
                retry = TRUE;
                continue;
            }
            break;
        }
        
        if (status != 200) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Server returned status %u", status);
            g_bytes_unref(response);
            g_object_unref(msg);
            break;
        }
        
        /* Gespeicherten Node zurückladen */
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
                
                /* Node-ID aktualisieren */
                SondGraphNode *updated_node = sond_graph_node_from_json(saved_json, NULL);
                if (updated_node) {
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
        success = TRUE;
    }
    
    g_free(node_json);
    return success;
}

gboolean sond_client_delete_node(SondClient *client,
                                  gint64 node_id,
                                  GError **error) {
    g_return_val_if_fail(SOND_IS_CLIENT(client), FALSE);
    g_return_val_if_fail(node_id > 0, FALSE);
    
    if (!ensure_authenticated(client, error)) {
        return FALSE;
    }
    
    gboolean retry = TRUE;
    gboolean success = FALSE;
    
    while (retry) {
        retry = FALSE;
        
        gchar *url = g_strdup_printf("%s/node/delete/%" G_GINT64_FORMAT,
                                    client->server_url, node_id);
        SoupMessage *msg = soup_message_new("DELETE", url);
        g_free(url);
        
        add_auth_header(client, msg);
        
        GBytes *response = soup_session_send_and_read(client->session, msg, NULL, error);
        
        if (!response) {
            g_object_unref(msg);
            break;
        }
        
        guint status = soup_message_get_status(msg);
        
        if (status == 401) {
            g_bytes_unref(response);
            g_object_unref(msg);
            
            if (handle_auth_error(client, error)) {
                retry = TRUE;
                continue;
            }
            break;
        }
        
        success = (status == 200);
        
        if (!success) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Server returned status %u", status);
        }
        
        g_bytes_unref(response);
        g_object_unref(msg);
    }
    
    return success;
}

gchar* sond_client_check_lock(SondClient *client,
                               gint64 node_id,
                               GError **error) {
    g_return_val_if_fail(SOND_IS_CLIENT(client), NULL);
    g_return_val_if_fail(node_id > 0, NULL);
    
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
               "check_lock not yet implemented on server");
    return NULL;
}

gchar* sond_client_create_seafile_library(SondClient *client,
                                          const gchar *name,
                                          const gchar *description,
                                          GError **error) {
    g_return_val_if_fail(SOND_IS_CLIENT(client), NULL);
    g_return_val_if_fail(name != NULL, NULL);
    
    if (!ensure_authenticated(client, error)) {
        return NULL;
    }
    
    /* JSON Body erstellen */
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "name");
    json_builder_add_string_value(builder, name);
    
    if (description) {
        json_builder_set_member_name(builder, "desc");
        json_builder_add_string_value(builder, description);
    }
    
    json_builder_end_object(builder);
    
    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    gchar *request_json = json_generator_to_data(gen, NULL);
    g_object_unref(gen);
    g_object_unref(builder);
    
    gboolean retry = TRUE;
    gchar *library_id = NULL;
    
    while (retry) {
        retry = FALSE;
        
        gchar *url = g_strdup_printf("%s/seafile/library", client->server_url);
        SoupMessage *msg = soup_message_new("POST", url);
        g_free(url);
        
        add_auth_header(client, msg);
        
        soup_message_set_request_body_from_bytes(msg, "application/json",
            g_bytes_new(request_json, strlen(request_json)));
        
        GBytes *response = soup_session_send_and_read(client->session, msg, NULL, error);
        
        if (!response) {
            g_object_unref(msg);
            break;
        }
        
        guint status = soup_message_get_status(msg);
        
        if (status == 401) {
            g_bytes_unref(response);
            g_object_unref(msg);
            
            if (handle_auth_error(client, error)) {
                retry = TRUE;
                continue;
            }
            break;
        }
        
        if (status != 200) {
            gsize size;
            const gchar *body = g_bytes_get_data(response, &size);
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Server returned status %u: %.*s", status, (int)size, body);
            g_bytes_unref(response);
            g_object_unref(msg);
            break;
        }
        
        /* Response parsen */
        gsize size;
        const gchar *data = g_bytes_get_data(response, &size);
        
        JsonParser *parser = json_parser_new();
        if (json_parser_load_from_data(parser, data, size, NULL)) {
            JsonNode *root = json_parser_get_root(parser);
            JsonObject *obj = json_node_get_object(root);
            
            if (json_object_has_member(obj, "data")) {
                JsonObject *data_obj = json_object_get_object_member(obj, "data");
                if (json_object_has_member(data_obj, "library_id")) {
                    const gchar *lib_id = json_object_get_string_member(data_obj, "library_id");
                    library_id = g_strdup(lib_id);
                    LOG_INFO("Seafile Library '%s' created (ID: %s)\n", name, library_id);
                }
            }
        }
        
        g_object_unref(parser);
        g_bytes_unref(response);
        g_object_unref(msg);
        
        if (!library_id) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Failed to create Seafile library");
        }
    }
    
    g_free(request_json);
    return library_id;
}

gboolean sond_client_delete_seafile_library(SondClient *client,
                                            const gchar *library_id,
                                            GError **error) {
    g_return_val_if_fail(SOND_IS_CLIENT(client), FALSE);
    g_return_val_if_fail(library_id != NULL, FALSE);
    
    if (!ensure_authenticated(client, error)) {
        return FALSE;
    }
    
    gboolean retry = TRUE;
    gboolean success = FALSE;
    
    while (retry) {
        retry = FALSE;
        
        gchar *url = g_strdup_printf("%s/seafile/library/%s",
                                    client->server_url, library_id);
        SoupMessage *msg = soup_message_new("DELETE", url);
        g_free(url);
        
        add_auth_header(client, msg);
        
        GBytes *response = soup_session_send_and_read(client->session, msg, NULL, error);
        
        if (!response) {
            g_object_unref(msg);
            break;
        }
        
        guint status = soup_message_get_status(msg);
        
        if (status == 401) {
            g_bytes_unref(response);
            g_object_unref(msg);
            
            if (handle_auth_error(client, error)) {
                retry = TRUE;
                continue;
            }
            break;
        }
        
        success = (status == 200 || status == 204);
        
        if (!success) {
            gsize size;
            const gchar *body = g_bytes_get_data(response, &size);
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Server returned status %u: %.*s", status, (int)size, body);
        } else {
            LOG_INFO("Seafile Library deleted (ID: %s)\n", library_id);
        }
        
        g_bytes_unref(response);
        g_object_unref(msg);
    }
    
    return success;
}

gchar* sond_client_get_seafile_library_id(SondClient *client,
                                           const gchar *library_name,
                                           GError **error) {
    g_return_val_if_fail(client != NULL, NULL);
    g_return_val_if_fail(library_name != NULL, NULL);

    /* Sicherstellen dass wir eingeloggt sind */
    if (!ensure_authenticated(client, error)) {
        return NULL;
    }

    /* URL konstruieren */
    gchar *escaped_name = g_uri_escape_string(library_name, NULL, FALSE);
    gchar *path = g_strdup_printf("/seafile/library-id?name=%s", escaped_name);
    g_free(escaped_name);

    gchar *url = g_strdup_printf("%s%s", client->server_url, path);
    g_free(path);

    /* HTTP GET Request */
    SoupMessage *msg = soup_message_new("GET", url);
    g_free(url);

    /* Authorization Header */
    gchar *auth_header = g_strdup_printf("Bearer %s", client->session_token);
    soup_message_headers_append(soup_message_get_request_headers(msg),
                                "Authorization", auth_header);
    g_free(auth_header);

    /* Request senden */
    GInputStream *stream = soup_session_send(client->session, msg, NULL, error);

    if (!stream) {
        g_object_unref(msg);
        return NULL;
    }

    /* Status Code prüfen */
    guint status = soup_message_get_status(msg);

    if (status == 401) {
        /* Unauthorized - Re-Login versuchen */
        g_object_unref(stream);
        g_object_unref(msg);

        if (handle_auth_error(client, error)) {
            /* Retry nach Re-Login */
            return sond_client_get_seafile_library_id(client, library_name, error);
        } else {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                       "Nicht authentifiziert");
            return NULL;
        }
    }

    if (status != 200) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Server-Fehler: HTTP %u", status);
        g_object_unref(stream);
        g_object_unref(msg);
        return NULL;
    }

    /* Response lesen */
    GByteArray *bytes = g_byte_array_new();
    guint8 buffer[4096];
    gssize read_size;

    while ((read_size = g_input_stream_read(stream, buffer, sizeof(buffer), NULL, error)) > 0) {
        g_byte_array_append(bytes, buffer, read_size);
    }

    g_object_unref(stream);
    g_object_unref(msg);

    if (read_size < 0) {
        g_byte_array_free(bytes, TRUE);
        return NULL;
    }

    /* NULL-terminieren */
    g_byte_array_append(bytes, (guint8*)"\0", 1);
    gchar *response_body = (gchar*)g_byte_array_free(bytes, FALSE);

    /* JSON parsen */
    JsonParser *parser = json_parser_new();

    if (!json_parser_load_from_data(parser, response_body, -1, error)) {
        g_free(response_body);
        g_object_unref(parser);
        return NULL;
    }

    g_free(response_body);

    JsonNode *root = json_parser_get_root(parser);
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Ungültige JSON-Response");
        g_object_unref(parser);
        return NULL;
    }

    JsonObject *obj = json_node_get_object(root);
    const gchar *library_id_str = json_object_get_string_member(obj, "library_id");

    gchar *library_id = NULL;
    if (library_id_str && strlen(library_id_str) > 0) {
        library_id = g_strdup(library_id_str);
    } else {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "Library '%s' auf Server nicht gefunden", library_name);
    }

    g_object_unref(parser);

    return library_id;
}

gchar* sond_client_get_seafile_clone_token(SondClient *client,
                                            const gchar *repo_id,
                                            GError **error) {
    g_return_val_if_fail(SOND_IS_CLIENT(client), NULL);
    g_return_val_if_fail(repo_id != NULL, NULL);

    /* Sicherstellen dass wir eingeloggt sind */
    if (!ensure_authenticated(client, error)) {
        return NULL;
    }

    /* URL bauen: /api2/repos/{repo_id}/download-info/ */
    gchar *url = g_strdup_printf("%s/api2/repos/%s/download-info/",
                                 client->seafile_url, repo_id);

    /* HTTP GET Request */
    SoupSession *session = soup_session_new();
    SoupMessage *msg = soup_message_new("GET", url);
    g_free(url);

    if (!msg) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Konnte HTTP Request nicht erstellen");
        g_object_unref(session);
        return NULL;
    }

    /* Authorization Header */
    gchar *auth_header = g_strdup_printf("Token %s", client->seafile_token);
    soup_message_headers_append(soup_message_get_request_headers(msg),
                                "Authorization", auth_header);
    g_free(auth_header);

    /* Request senden */
    GBytes *response = soup_session_send_and_read(session, msg, NULL, error);
    guint status = soup_message_get_status(msg);

    g_object_unref(msg);
    g_object_unref(session);

    if (!response || status != 200) {
        if (response) g_bytes_unref(response);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Seafile returned status %u", status);
        return NULL;
    }
    /* JSON Response parsen */
    gsize size;
    const gchar *data = g_bytes_get_data(response, &size);

    JsonParser *parser = json_parser_new();
    gchar *clone_token = NULL;

    if (json_parser_load_from_data(parser, data, size, error)) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);

        if (json_object_has_member(obj, "token")) {
            const gchar *token = json_object_get_string_member(obj, "token");
            clone_token = g_strdup(token);
        } else {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Seafile response enthält kein 'token' Feld");
        }
    }

    g_object_unref(parser);
    g_bytes_unref(response);

    return clone_token;
}

gpointer sond_client_get_offline_manager(SondClient *client) {
    g_return_val_if_fail(SOND_IS_CLIENT(client), NULL);
    return client->offline_manager;
}

void sond_client_set_last_regnr(SondClient *client, guint lfd_nr, guint year) {
    g_return_if_fail(SOND_IS_CLIENT(client));
    
    client->last_regnr_lfd = lfd_nr;
    client->last_regnr_year = year;
    client->has_last_regnr = TRUE;
}

gboolean sond_client_get_last_regnr(SondClient *client, guint *lfd_nr, guint *year) {
    g_return_val_if_fail(SOND_IS_CLIENT(client), FALSE);
    g_return_val_if_fail(lfd_nr != NULL, FALSE);
    g_return_val_if_fail(year != NULL, FALSE);
    
    if (!client->has_last_regnr) {
        return FALSE;
    }
    
    *lfd_nr = client->last_regnr_lfd;
    *year = client->last_regnr_year;
    
    return TRUE;
}


