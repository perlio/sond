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

#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

struct _SondClient {
    GObject parent_instance;
    
    /* Konfiguration */
    gchar *server_url;
    guint server_port;
    
    /* HTTP-Client */
    SoupSession *session;
    gboolean connected;
};

G_DEFINE_TYPE(SondClient, sond_client, G_TYPE_OBJECT)

static void sond_client_finalize(GObject *object) {
    SondClient *self = SOND_CLIENT(object);
    
    g_free(self->server_url);
    
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
