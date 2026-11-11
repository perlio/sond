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
    gchar *client_id;      /* Instanz-ID (UUID pro Prozess) - Lock-Identitaet */
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

    /* Node-IDs, die wir aktuell selbst gesperrt halten (Keys: heap-allokierte
     * gint64*, Values: ungenutzt). Dient dazu, beim Entsperren nur dann beim
     * Server nachzufragen, wenn wir den Lock wirklich halten (s.
     * sond_client_unlock_node). */
    GHashTable *held_locks;

    /* TRUE sobald einmal erfolgreich eingeloggt wurde (erster Login ODER
     * stiller Re-Login nach 401) - steuert, ob beim naechsten Login noch die
     * "gib alte Locks frei"-Anfrage geschickt wird (nur beim allerersten
     * Login dieses Prozesslaufs, s. sond_client_set_auth()). */
};

G_DEFINE_TYPE(SondClient, sond_client, G_TYPE_OBJECT)

/* ========================================================================
 * SondClientError - Fehlerdomaene mit Zusatzdaten (fehlende IDs, Lock-Inhaber)
 * ======================================================================== */

typedef struct {
    GArray *missing_ids;   /* gint64, nur bei GONE */
    gchar *locked_by;      /* nur bei LOCK_NOT_HELD */
    gchar *locked_at;      /* nur bei LOCK_NOT_HELD */
} SondClientErrorPrivate;

static void sond_client_error_private_init(SondClientErrorPrivate *priv) {
    priv->missing_ids = NULL;
    priv->locked_by = NULL;
    priv->locked_at = NULL;
}

static void sond_client_error_private_copy(const SondClientErrorPrivate *src,
                                           SondClientErrorPrivate *dest) {
    dest->missing_ids = src->missing_ids ? g_array_ref(src->missing_ids) : NULL;
    dest->locked_by = g_strdup(src->locked_by);
    dest->locked_at = g_strdup(src->locked_at);
}

static void sond_client_error_private_clear(SondClientErrorPrivate *priv) {
    if (priv->missing_ids) g_array_unref(priv->missing_ids);
    g_free(priv->locked_by);
    g_free(priv->locked_at);
    priv->missing_ids = NULL;
    priv->locked_by = NULL;
    priv->locked_at = NULL;
}

G_DEFINE_EXTENDED_ERROR(SondClientError, sond_client_error)

gboolean sond_client_error_is_conflict(const GError *error) {
    return error && error->domain == SOND_CLIENT_ERROR &&
           (error->code == SOND_CLIENT_ERROR_LOCK_NOT_HELD ||
            error->code == SOND_CLIENT_ERROR_STALE ||
            error->code == SOND_CLIENT_ERROR_GONE);
}

const GArray* sond_client_error_get_missing_ids(const GError *error) {
    g_return_val_if_fail(error && error->domain == SOND_CLIENT_ERROR, NULL);
    return sond_client_error_get_private(error)->missing_ids;
}

const gchar* sond_client_error_get_locked_by(const GError *error) {
    g_return_val_if_fail(error && error->domain == SOND_CLIENT_ERROR, NULL);
    return sond_client_error_get_private(error)->locked_by;
}

const gchar* sond_client_error_get_locked_at(const GError *error) {
    g_return_val_if_fail(error && error->domain == SOND_CLIENT_ERROR, NULL);
    return sond_client_error_get_private(error)->locked_at;
}

/**
 * set_write_error:
 *
 * Wertet eine Nicht-200-Antwort eines schreibenden Knoten-Endpunkts
 * (save/delete) aus. Enthaelt sie einen "code" (LOCK_NOT_HELD/STALE/GONE), wird ein
 * SOND_CLIENT_ERROR mit benutzerlesbarer Meldung und Zusatzdaten erzeugt,
 * sonst ein allgemeiner Fehler mit dem Servertext.
 *
 * Returns: der gesetzte #SondClientErrorCode
 */
static SondClientErrorCode set_write_error(guint status, GBytes *response, GError **error) {
    gsize len = 0;
    const gchar *data = response ? g_bytes_get_data(response, &len) : NULL;

    const gchar *code_str = NULL;
    const gchar *server_msg = NULL;
    const gchar *locked_by = NULL;
    const gchar *locked_at = NULL;
    JsonArray *missing = NULL;

    JsonParser *parser = json_parser_new();
    JsonObject *obj = NULL;
    if (data && json_parser_load_from_data(parser, data, len, NULL)) {
        JsonNode *root = json_parser_get_root(parser);
        if (root && JSON_NODE_HOLDS_OBJECT(root)) {
            obj = json_node_get_object(root);
        }
    }

    if (obj) {
        if (json_object_has_member(obj, "code"))
            code_str = json_object_get_string_member(obj, "code");
        if (json_object_has_member(obj, "error"))
            server_msg = json_object_get_string_member(obj, "error");
        if (json_object_has_member(obj, "locked_by"))
            locked_by = json_object_get_string_member(obj, "locked_by");
        if (json_object_has_member(obj, "locked_at"))
            locked_at = json_object_get_string_member(obj, "locked_at");
        if (json_object_has_member(obj, "missing_ids"))
            missing = json_object_get_array_member(obj, "missing_ids");
    }

    SondClientErrorCode code = SOND_CLIENT_ERROR_FAILED;
    if (status == 409 && code_str) {
        if (g_strcmp0(code_str, "LOCK_NOT_HELD") == 0) code = SOND_CLIENT_ERROR_LOCK_NOT_HELD;
        else if (g_strcmp0(code_str, "STALE") == 0) code = SOND_CLIENT_ERROR_STALE;
        else if (g_strcmp0(code_str, "GONE") == 0) code = SOND_CLIENT_ERROR_GONE;
    }

    if (code == SOND_CLIENT_ERROR_FAILED) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Server returned status %u%s%s", status,
                   server_msg ? ": " : "", server_msg ? server_msg : "");
        g_object_unref(parser);
        return code;
    }

    gchar *msg = NULL;
    switch (code) {
        case SOND_CLIENT_ERROR_LOCK_NOT_HELD:
            if (locked_by && *locked_by) {
                msg = g_strdup_printf(
                    "Der Datensatz ist inzwischen von '%s' gesperrt (z.B. weil Ihre "
                    "Sperre gebrochen wurde). Ihre Aenderungen wurden nicht gespeichert.",
                    locked_by);
            } else {
                msg = g_strdup("Sie halten die Sperre auf den Datensatz nicht (mehr). "
                               "Ihre Aenderungen wurden nicht gespeichert.");
            }
            break;
        case SOND_CLIENT_ERROR_STALE:
            msg = g_strdup("Der Datensatz wurde zwischenzeitlich von jemand anderem "
                           "geaendert. Ihre Aenderungen wurden nicht gespeichert.");
            break;
        default:
            msg = g_strdup("Der Datensatz (oder ein damit verknuepfter Eintrag) wurde "
                           "zwischenzeitlich geloescht.");
            break;
    }

    if (error) {
        GError *e = g_error_new_literal(SOND_CLIENT_ERROR, code, msg);
        SondClientErrorPrivate *priv = sond_client_error_get_private(e);
        if (code == SOND_CLIENT_ERROR_LOCK_NOT_HELD) {
            priv->locked_by = g_strdup(locked_by);
            priv->locked_at = g_strdup(locked_at);
        } else if (code == SOND_CLIENT_ERROR_GONE && missing) {
            priv->missing_ids = g_array_new(FALSE, FALSE, sizeof(gint64));
            for (guint i = 0; i < json_array_get_length(missing); i++) {
                gint64 id = json_array_get_int_element(missing, i);
                g_array_append_val(priv->missing_ids, id);
            }
        }
        g_propagate_error(error, e);
    }

    g_free(msg);
    g_object_unref(parser);
    return code;
}

static void sond_client_finalize(GObject *object) {
    SondClient *self = SOND_CLIENT(object);

    if (self->held_locks) {
        g_hash_table_destroy(self->held_locks);
    }

    g_free(self->server_url);
    g_free(self->session_token);
    g_free(self->seafile_token);
    g_free(self->username);
    g_free(self->client_id);
    g_free(self->seafile_url);

    if (self->offline_manager) {
        g_object_unref(self->offline_manager);
    }

    if (self->session) {
        g_object_unref(self->session);
    }

    G_OBJECT_CLASS(sond_client_parent_class)->finalize(object);
}

enum {
    SIGNAL_NODE_CONFLICT,
    N_SIGNALS
};
static guint client_signals[N_SIGNALS] = { 0 };

static void sond_client_class_init(SondClientClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = sond_client_finalize;

    /* "node-conflict": node_id, code (SondClientErrorCode), locked_by, locked_at */
    client_signals[SIGNAL_NODE_CONFLICT] =
        g_signal_new("node-conflict",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     0, NULL, NULL, NULL,
                     G_TYPE_NONE, 4,
                     G_TYPE_INT64, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);
}

static void sond_client_init(SondClient *self) {
    self->server_url = NULL;
    self->server_port = 0;
    self->session = NULL;
    self->session_token = NULL;
    self->username = NULL;
    /* Instanz-ID: bleibt ueber Re-Logins/Session-Timeouts stabil, unterscheidet
     * aber zwei Prozesse (auch desselben Users, auch auf verschiedenen Rechnern) */
    self->client_id = g_uuid_string_random();
    self->auth_failed_callback = NULL;
    self->auth_failed_user_data = NULL;
    self->login_callback = NULL;
    self->login_callback_user_data = NULL;
    self->offline_manager = NULL;

    /* Letzte RegNr initialisieren */
    self->last_regnr_lfd = 0;
    self->last_regnr_year = 0;
    self->has_last_regnr = FALSE;

    /* Eigene Locks */
    self->held_locks = g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free, NULL);
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

/**
 * notify_node_conflict:
 *
 * Wertet einen von set_write_error() erzeugten Fehler aus und meldet einen
 * Konflikt, der den Knoten selbst betrifft, per Signal "node-conflict"
 * (s. sond_client.h). GONE nur, wenn der Knoten selbst fehlt - fehlt nur ein
 * verknuepfter Knoten, bekommt das ausschliesslich der Aufrufer ueber den Fehler.
 * Wird der Lock dadurch hinfaellig (LOCK_NOT_HELD/GONE), wird er aus held_locks entfernt.
 */
static void notify_node_conflict(SondClient *client, gint64 node_id, const GError *werr) {
    if (!sond_client_error_is_conflict(werr)) {
        return;
    }

    if (werr->code == SOND_CLIENT_ERROR_GONE) {
        const GArray *missing = sond_client_error_get_missing_ids(werr);
        gboolean self_gone = FALSE;
        for (guint i = 0; missing && i < missing->len; i++) {
            if (g_array_index(missing, gint64, i) == node_id) {
                self_gone = TRUE;
            }
        }
        if (!self_gone) {
            return;
        }
    }

    if (werr->code == SOND_CLIENT_ERROR_LOCK_NOT_HELD || werr->code == SOND_CLIENT_ERROR_GONE) {
        g_hash_table_remove(client->held_locks, &node_id);
    }

    g_signal_emit(client, client_signals[SIGNAL_NODE_CONFLICT], 0,
                  node_id, (guint)werr->code,
                  werr->code == SOND_CLIENT_ERROR_LOCK_NOT_HELD ?
                      sond_client_error_get_locked_by(werr) : NULL,
                  werr->code == SOND_CLIENT_ERROR_LOCK_NOT_HELD ?
                      sond_client_error_get_locked_at(werr) : NULL);
}

static void add_auth_header(SondClient *client, SoupMessage *msg) {
    /* Instanz-ID bei JEDER Anfrage: der Server prueft damit vor schreibenden
     * Zugriffen generisch, ob ein fremder Lock besteht. */
    if (client->client_id) {
        soup_message_headers_append(soup_message_get_request_headers(msg),
                                    "X-Sond-Client-Id", client->client_id);
    }

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
    json_builder_set_member_name(builder, "client_id");
    json_builder_add_string_value(builder, client->client_id);
    
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
                    sond_graph_node_set_version(node, sond_graph_node_get_version(updated_node));
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

gboolean sond_client_lock_node_ex(SondClient *client,
                                   gint64 node_id,
                                   const gchar *lock_reason,
                                   gchar **out_locked_by,
                                   gchar **out_locked_at,
                                   GError **error) {
    g_return_val_if_fail(SOND_IS_CLIENT(client), FALSE);
    g_return_val_if_fail(node_id > 0, FALSE);

    if (out_locked_by) *out_locked_by = NULL;
    if (out_locked_at) *out_locked_at = NULL;

    if (!ensure_authenticated(client, error)) {
        return FALSE;
    }

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);

    json_builder_set_member_name(builder, "user_id");
    json_builder_add_string_value(builder, client->username);
    json_builder_set_member_name(builder, "client_id");
    json_builder_add_string_value(builder, client->client_id);

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
            /* Bei Konflikt (409) stehen locked_by/locked_at im Body - fuer
             * die Anzeige im Client ("Gesperrt von X, seit Y"). */
            if (status == 409 && (out_locked_by || out_locked_at)) {
                gsize resp_len = 0;
                const gchar *resp_data = g_bytes_get_data(response, &resp_len);
                JsonParser *resp_parser = json_parser_new();
                if (json_parser_load_from_data(resp_parser, resp_data, resp_len, NULL)) {
                    JsonObject *resp_obj = json_node_get_object(json_parser_get_root(resp_parser));
                    if (out_locked_by && json_object_has_member(resp_obj, "locked_by")) {
                        *out_locked_by = g_strdup(json_object_get_string_member(resp_obj, "locked_by"));
                    }
                    if (out_locked_at && json_object_has_member(resp_obj, "locked_at")) {
                        *out_locked_at = g_strdup(json_object_get_string_member(resp_obj, "locked_at"));
                    }
                }
                g_object_unref(resp_parser);
            }
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Lock failed with status %u", status);
        } else {
            /* Eigenen Lock-Bestand merken (s. held_locks). */
            gint64 *key = g_new(gint64, 1);
            *key = node_id;
            g_hash_table_replace(client->held_locks, key, GINT_TO_POINTER(1));
        }

        g_bytes_unref(response);
        g_object_unref(msg);
    }

    g_free(request_json);
    return success;
}

gboolean sond_client_lock_node(SondClient *client,
                                gint64 node_id,
                                const gchar *lock_reason,
                                GError **error) {
    return sond_client_lock_node_ex(client, node_id, lock_reason, NULL, NULL, error);
}

gboolean sond_client_unlock_node(SondClient *client,
                                  gint64 node_id,
                                  GError **error) {
    g_return_val_if_fail(SOND_IS_CLIENT(client), FALSE);
    g_return_val_if_fail(node_id > 0, FALSE);

    /* Generischer No-Op, wenn wir den Lock laut unserer eigenen
     * Buchhaltung (held_locks) gar nicht halten - z.B. beim Schliessen
     * einer nur lesend (READONLY, weil von jemand anderem gelockt)
     * angezeigten Akte. Das gehoert hier in die generische Client-Schicht
     * und nicht als Spezialfall in ein einzelnes Modul (sond_module_akte
     * o.ae.), denn jedes Modul, das Nodes locken kann, braucht dieselbe
     * Absicherung. Server wuerde sonst zu Recht mit 403 ablehnen
     * ("Node ist nicht gelockt" / "nur vom Lock-Inhaber") - kein
     * Server-Roundtrip noetig, wenn wir es lokal schon wissen. */
    if (!g_hash_table_contains(client->held_locks, &node_id)) {
        return TRUE;
    }

    if (!ensure_authenticated(client, error)) {
        return FALSE;
    }
    
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    
    json_builder_set_member_name(builder, "user_id");
    json_builder_add_string_value(builder, client->username);
    json_builder_set_member_name(builder, "client_id");
    json_builder_add_string_value(builder, client->client_id);
    
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
        } else {
            g_hash_table_remove(client->held_locks, &node_id);
        }

        g_bytes_unref(response);
        g_object_unref(msg);
    }

    g_free(request_json);
    return success;
}

gboolean sond_client_force_unlock_node(SondClient *client,
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
    json_builder_set_member_name(builder, "client_id");
    json_builder_add_string_value(builder, client->client_id);

    json_builder_set_member_name(builder, "force");
    json_builder_add_boolean_value(builder, TRUE);

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
                       "Force unlock failed with status %u: %s", status,
                       (gchar*) g_bytes_get_data(response, NULL));
        } else {
            /* Falls wir selbst (warum auch immer) in held_locks standen */
            g_hash_table_remove(client->held_locks, &node_id);
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
            GError *werr = NULL;
            set_write_error(status, response, &werr);

            /* Konflikt (gesperrt/geaendert/geloescht) per Signal melden; bei
             * LOCK_NOT_HELD/GONE wird der Lock dabei aus held_locks entfernt. */
            notify_node_conflict(client, sond_graph_node_get_id(node), werr);
            g_propagate_error(error, werr);

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
                    sond_graph_node_set_version(node, sond_graph_node_get_version(updated_node));
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
                                  gpointer node_ptr,
                                  GError **error) {
    g_return_val_if_fail(SOND_IS_CLIENT(client), FALSE);
    g_return_val_if_fail(SOND_IS_GRAPH_NODE(node_ptr), FALSE);

    SondGraphNode *node = SOND_GRAPH_NODE(node_ptr);
    gint64 node_id = sond_graph_node_get_id(node);
    gint64 version = sond_graph_node_get_version(node);
    g_return_val_if_fail(node_id > 0, FALSE);

    if (!ensure_authenticated(client, error)) {
        return FALSE;
    }

    gboolean retry = TRUE;
    gboolean success = FALSE;

    while (retry) {
        retry = FALSE;

        gchar *url = g_strdup_printf("%s/node/delete/%" G_GINT64_FORMAT "?version=%" G_GINT64_FORMAT,
                                    client->server_url, node_id, version);
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

        if (success) {
            /* Lock-Zeile verschwindet serverseitig per CASCADE mit dem Knoten */
            g_hash_table_remove(client->held_locks, &node_id);
        } else {
            GError *werr = NULL;
            set_write_error(status, response, &werr);
            notify_node_conflict(client, node_id, werr);
            g_propagate_error(error, werr);
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

/* =======================================================================
 * OCR API
 * ======================================================================= */

/**
 * sond_client_ocr_start_job:
 *
 * Startet OCR-Job auf dem Server
 */
gboolean sond_client_ocr_start_job(SondClient *client,
                                   const gchar *library_name,
                                   gboolean force_reprocess,
                                   GError **error) {
    g_return_val_if_fail(client != NULL, FALSE);
    g_return_val_if_fail(library_name != NULL, FALSE);

    /* Lazy-Auth falls nötig */
    if (!ensure_authenticated(client, error)) {
        return FALSE;
    }

    /* URL: POST /ocr/start?library_name=XXX&force_reprocess=false */
    gchar *escaped_name = g_uri_escape_string(library_name, NULL, FALSE);
    gchar *url = g_strdup_printf("%s/ocr/start?library_name=%s&force_reprocess=%s",
                                 client->server_url,
                                 escaped_name,
                                 force_reprocess ? "true" : "false");
    g_free(escaped_name);

    SoupMessage *msg = soup_message_new("POST", url);
    g_free(url);

    /* Session-Token im Header */
    add_auth_header(client, msg);

    GBytes *response = soup_session_send_and_read(client->session, msg, NULL, error);

    if (!response) {
        g_object_unref(msg);
        return FALSE;
    }

    guint status_code = soup_message_get_status(msg);

    /* 401 = Session abgelaufen -> Re-Login */
    if (status_code == 401) {
        g_bytes_unref(response);
        g_object_unref(msg);

        if (!handle_auth_error(client, error)) {
            return FALSE;
        }

        /* Retry */
        return sond_client_ocr_start_job(client, library_name, force_reprocess, error);
    }

    if (status_code != 200) {
        gsize size;
        const gchar *body = g_bytes_get_data(response, &size);

        /* Parse error message */
        JsonParser *parser = json_parser_new();
        if (json_parser_load_from_data(parser, body, size, NULL)) {
            JsonNode *root = json_parser_get_root(parser);
            JsonObject *obj = json_node_get_object(root);

            if (json_object_has_member(obj, "error")) {
                const gchar *err_msg = json_object_get_string_member(obj, "error");
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                           "OCR start failed: %s", err_msg);
            } else {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                           "OCR start failed (HTTP %u)", status_code);
            }
        } else {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "OCR start failed (HTTP %u)", status_code);
        }
        g_object_unref(parser);
        g_bytes_unref(response);
        g_object_unref(msg);
        return FALSE;
    }

    g_bytes_unref(response);
    g_object_unref(msg);

    LOG_INFO("OCR job started for library: %s", library_name);
    return TRUE;
}

/**
 * sond_client_ocr_get_status:
 *
 * Holt OCR-Status vom Server
 */
OcrStatus* sond_client_ocr_get_status(SondClient *client,
                                      const gchar *library_name,
                                      GError **error) {
    g_return_val_if_fail(client != NULL, NULL);
    g_return_val_if_fail(library_name != NULL, NULL);

    /* Lazy-Auth falls nötig */
    if (!ensure_authenticated(client, error)) {
        return NULL;
    }

    /* URL: GET /ocr/status?library_name=XXX */
    gchar *escaped_name = g_uri_escape_string(library_name, NULL, FALSE);
    gchar *url = g_strdup_printf("%s/ocr/status?library_name=%s",
                                 client->server_url,
                                 escaped_name);
    g_free(escaped_name);

    SoupMessage *msg = soup_message_new("GET", url);
    g_free(url);

    /* Session-Token im Header */
    add_auth_header(client, msg);

    GBytes *response = soup_session_send_and_read(client->session, msg, NULL, error);

    if (!response) {
        g_object_unref(msg);
        return NULL;
    }

    guint status_code = soup_message_get_status(msg);

    /* 401 = Session abgelaufen -> Re-Login */
    if (status_code == 401) {
        g_bytes_unref(response);
        g_object_unref(msg);

        if (!handle_auth_error(client, error)) {
            return NULL;
        }

        /* Retry */
        return sond_client_ocr_get_status(client, library_name, error);
    }

    if (status_code == 404) {
        /* Kein Job gefunden - das ist OK, nicht als Fehler behandeln */
        g_bytes_unref(response);
        g_object_unref(msg);
        return NULL;
    }

    if (status_code != 200) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "OCR status failed (HTTP %u)", status_code);
        g_bytes_unref(response);
        g_object_unref(msg);
        return NULL;
    }

    /* Parse Response */
    gsize size;
    const gchar *body = g_bytes_get_data(response, &size);

    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_data(parser, body, size, error)) {
        g_object_unref(parser);
        g_bytes_unref(response);
        g_object_unref(msg);
        return NULL;
    }

    JsonNode *root = json_parser_get_root(parser);
    JsonObject *obj = json_node_get_object(root);

    OcrStatus *status = g_new0(OcrStatus, 1);

    status->running = json_object_get_boolean_member(obj, "running");
    status->total_files = json_object_get_int_member(obj, "total_files");
    status->processed_files = json_object_get_int_member(obj, "processed_files");
    status->processed_pdfs = json_object_get_int_member(obj, "processed_pdfs");

    if (json_object_has_member(obj, "current_file")) {
        status->current_file = g_strdup(json_object_get_string_member(obj, "current_file"));
    }

    if (json_object_has_member(obj, "start_time")) {
        const gchar *start_str = json_object_get_string_member(obj, "start_time");
        status->start_time = g_date_time_new_from_iso8601(start_str, NULL);
    }

    if (json_object_has_member(obj, "end_time")) {
        const gchar *end_str = json_object_get_string_member(obj, "end_time");
        status->end_time = g_date_time_new_from_iso8601(end_str, NULL);
    }

    g_object_unref(parser);
    g_bytes_unref(response);
    g_object_unref(msg);

    return status;
}

/**
 * sond_client_ocr_status_free:
 *
 * Gibt OcrStatus frei
 */
void sond_client_ocr_status_free(OcrStatus *status) {
    if (!status) return;

    g_free(status->current_file);

    if (status->start_time) {
        g_date_time_unref(status->start_time);
    }
    if (status->end_time) {
        g_date_time_unref(status->end_time);
    }

    g_free(status);
}
