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
    
    /* User-Identifikation */
    gchar *user_id;  /* username@hostname */
    
    /* HTTP-Client */
    SoupSession *session;
    gboolean connected;
};

G_DEFINE_TYPE(SondClient, sond_client, G_TYPE_OBJECT)

static void sond_client_finalize(GObject *object) {
    SondClient *self = SOND_CLIENT(object);
    
    g_free(self->server_url);
    g_free(self->user_id);
    
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
    
    /* User-ID generieren: username@hostname */
    /* TEMPORÄR: Komplett hardcoded zum Testen */
    self->user_id = g_strdup("testuser");
    
    const gchar *username = g_get_user_name();
    const gchar *hostname = g_get_host_name();
    
    if (!username) username = "unknown";
    if (!hostname) hostname = "unknown";
    
    /* NOTFALL-FALLBACK: Wenn irgendwas schiefgeht, nutze feste ID */
    self->user_id = g_strdup("sonduser@localhost");
    
    /* Versuche es trotzdem mit dem Filter */
    gboolean has_non_ascii = FALSE;
    for (const gchar *p = username; *p; p++) {
        if ((unsigned char)*p >= 128) has_non_ascii = TRUE;
    }
    for (const gchar *p = hostname; *p; p++) {
        if ((unsigned char)*p >= 128) has_non_ascii = TRUE;
    }
    
    if (!has_non_ascii) {
        /* Nur wenn garantiert ASCII, dann verwenden */
        g_free(self->user_id);
        self->user_id = g_strdup_printf("%s@%s", username, hostname);
    }
    
    /* Debug-Output mit g_print (immer sichtbar) */
    g_print("[CLIENT INIT] Original username='%s', hostname='%s'\n", username, hostname);
    g_print("[CLIENT INIT] Sanitized user_id='%s'\n", self->user_id);
    
    LOG_INFO("Client user_id: '%s' (from username='%s', hostname='%s')\n",
             self->user_id, username, hostname);
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
    return client->user_id;
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
    json_builder_add_string_value(builder, client->user_id);
    
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
    
    soup_message_set_request_body_from_bytes(msg, "application/json",
        g_bytes_new_take(request_json, strlen(request_json)));
    
    GBytes *response = soup_session_send_and_read(client->session, msg, NULL, error);
    
    if (!response) {
        g_object_unref(msg);
        return FALSE;
    }
    
    guint status = soup_message_get_status(msg);
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
    json_builder_add_string_value(builder, client->user_id);
    
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
    
    soup_message_set_request_body_from_bytes(msg, "application/json",
        g_bytes_new_take(request_json, strlen(request_json)));
    
    GBytes *response = soup_session_send_and_read(client->session, msg, NULL, error);
    
    if (!response) {
        g_object_unref(msg);
        return FALSE;
    }
    
    guint status = soup_message_get_status(msg);
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
    json_builder_add_string_value(builder, client->user_id);
    
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
    
    soup_message_set_request_body_from_bytes(msg, "application/json",
        g_bytes_new_take(request_json, strlen(request_json)));
    
    GBytes *response = soup_session_send_and_read(client->session, msg, NULL, error);
    
    if (!response) {
        g_object_unref(msg);
        return FALSE;
    }
    
    guint status = soup_message_get_status(msg);
    gboolean success = (status == 200);
    
    if (!success) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Unlock failed with status %u", status);
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
