/*
 * sond (sond_seafile_sync.c) - Seafile RPC Integration
 * Copyright (C) 2026 pelo america
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "sond_seafile_sync.h"
#include "../sond_log_and_error.h"
#include "sond_client.h"

#include <gio/gio.h>
#include <libsoup/soup.h>
#include "libsearpc/searpc-client.h"
#include "libsearpc/searpc-named-pipe-transport.h"

/* ========================================================================
 * Helper Functions
 * ======================================================================== */

/**
 * get_seafile_pipe_path:
 *
 * Ermittelt den Named Pipe Pfad zum Seafile-Client.
 * Format: \\.\pipe\seafile_{BASE64(username)}
 */
static gchar* get_seafile_pipe_path(void) {
    const gchar *username = g_get_user_name();
    gchar *username_b64 = g_base64_encode((const guchar*)username, strlen(username));

#ifdef G_OS_WIN32
    gchar *pipe_path = g_strdup_printf("\\\\.\\pipe\\seafile_%s", username_b64);
#else
    /* Linux: Unix Domain Socket */
    const gchar *home = g_get_home_dir();
    gchar *pipe_path = g_strdup_printf("%s/.seafile/seafile.sock", home);
#endif

    g_free(username_b64);
    return pipe_path;
}

/**
 * connect_to_seafile:
 *
 * Verbindet zum Seafile-Client via Named Pipe (Windows) oder Socket (Linux).
 *
 * Returns: (transfer full) (nullable): SearpcClient oder NULL bei Fehler
 */
static SearpcClient* connect_to_seafile(GError **error) {
    gchar *pipe_path = get_seafile_pipe_path();
    SearpcNamedPipeClient *pipe_client = searpc_create_named_pipe_client(pipe_path);
    if (!pipe_client) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Konnte Named Pipe Client nicht erstellen: %s", pipe_path);
        g_free(pipe_path);
        return NULL;
    }

    if (searpc_named_pipe_client_connect(pipe_client) < 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Seafile-Client nicht erreichbar: %s\n"
                   "Stellen Sie sicher dass der Seafile-Client läuft!", pipe_path);
        g_free(pipe_path);
        g_free(pipe_client);
        return NULL;
    }

    g_free(pipe_path);

    SearpcClient *client = searpc_client_with_named_pipe_transport(
        pipe_client,
        "seafile-rpcserver"
    );

    if (!client) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Konnte RPC-Transport nicht erstellen");
        g_free(pipe_client);
        return NULL;
    }

    /* WICHTIG: pipe_client gehört jetzt dem client - nicht freigeben! */

    LOG_INFO("Seafile-Client Verbindung hergestellt\n");

    return client;
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

gchar* sond_seafile_find_library_by_name(const gchar *library_name,
                                          GError **error) {
    g_return_val_if_fail(library_name != NULL, NULL);

    SearpcClient *client = connect_to_seafile(error);
    if (!client) {
        return NULL;
    }

    LOG_INFO("Suche Library mit Name: %s\n", library_name);

    /* RPC-Call: seafile_get_repo_list */
    json_t *result = NULL;
    GError *rpc_error = NULL;

    searpc_client_call(
        client,
        "seafile_get_repo_list",
        "json",
        0,
        &result,
        &rpc_error,
        2,
        "int", (void*)-1,  /* offset = -1 (alle) */
        "int", (void*)-1   /* limit = -1 (alle) */
    );

    if (rpc_error) {
        g_propagate_error(error, rpc_error);
        searpc_free_client_with_pipe_transport(client);
        return NULL;
    }

    gchar *library_id = NULL;

    if (result && json_is_array(result)) {
        size_t count = json_array_size(result);

        for (size_t i = 0; i < count; i++) {
            json_t *repo = json_array_get(result, i);

            const char *name = json_string_value(json_object_get(repo, "name"));

            if (name && g_strcmp0(name, library_name) == 0) {
                const char *id = json_string_value(json_object_get(repo, "id"));
                if (id) {
                    library_id = g_strdup(id);
                    LOG_INFO("Library gefunden: %s -> %s\n", library_name, library_id);
                    break;
                }
            }
        }

        json_decref(result);
    }

    if (!library_id) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "Library '%s' nicht gefunden", library_name);
    }

    searpc_free_client_with_pipe_transport(client);

    return library_id;
}

gboolean sond_seafile_sync_library(SondClient* client, const gchar *library_id,
                                    const gchar* library_name, const gchar *local_path,
                                    GError **error) {
    g_return_val_if_fail(library_id != NULL, FALSE);
    g_return_val_if_fail(local_path != NULL, FALSE);

    SearpcClient *rpc_client = connect_to_seafile(error);
    if (!rpc_client) {
        return FALSE;
    }

    //Verzeichnis erstellen falls nicht vorhanden
    if (!g_file_test(local_path, G_FILE_TEST_EXISTS)) {
        if (g_mkdir_with_parents(local_path, 0755) != 0) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Konnte lokales Verzeichnis nicht erstellen: %s", local_path);
            searpc_free_client_with_pipe_transport(rpc_client);
            return FALSE;
        }
    }

    /* Clone-Token vom Seafile-Server holen */
    gchar *clone_token = sond_client_get_seafile_clone_token(client, library_id, error);
    if (!clone_token) {
        searpc_free_client_with_pipe_transport(rpc_client);
        return FALSE;
    }

    /* RPC-Call: seafile_clone
     * Parameter laut seafile-rpc.h:
     * - repo_id
     * - repo_version (meistens 0)
     * - repo_name
     * - worktree (lokaler Pfad)
     * - token
     * - passwd
     * - magic
     * - email
     * - random_key
     * - enc_version
     * - more_info
     */
    GError *rpc_error = NULL;
    gchar *task_id = NULL;
    gchar *more_info = NULL;

    //username ist in more_info nicht notwendig
    more_info = g_strdup_printf("{\"server_url\":\"%s\"}",
    		sond_client_get_seafile_url(client));

    searpc_client_call(
    	rpc_client,
        "seafile_clone",
        "string",
        0,
        &task_id,
        &rpc_error,
        11,
        "string", library_id,       // repo_id
        "int", (void*)1,             // repo_version (0 für normale Repos)
        "string", library_name,        // repo_name (nutzen library_id als Name)
        "string", local_path,        // worktree
        "string", clone_token,  // token
        "string", NULL,                // passwd (leer wenn nicht verschlüsselt)
        "string", "",                // magic (leer)
        "string", "", 			// email - kann leer bleiben, wird nicht geprüft
        "string", "",                // random_key (leer)
        "int", (void*)0,             // enc_version (0 = nicht verschlüsselt)
        "string", more_info		// more_info (nicht leer, wegen server_url)
    );

    g_free(more_info);
    g_free(clone_token);
    searpc_free_client_with_pipe_transport(rpc_client);

    if (rpc_error) {
        g_propagate_error(error, rpc_error);
        return FALSE;
    }

    if (!task_id) { //zwar kein error, aber clonen hat nicht geklappt
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
				   "Konnte Library nicht synchronisieren (keine Task-ID erhalten)");
		return FALSE;
	}

    g_free(task_id);

    LOG_INFO("Library synced: %s -> %s\n", library_id, local_path);

    return TRUE;
}

gboolean sond_seafile_unsync_library(const gchar *library_id,
                                      GError **error) {
    g_return_val_if_fail(library_id != NULL, FALSE);

    SearpcClient *client = connect_to_seafile(error);
    if (!client) {
        return FALSE;
    }

    /* RPC-Call: seafile_unsync */
    GError *rpc_error = NULL;
    int result = 0;

    searpc_client_call(
        client,
        "seafile_destroy_repo",
        "int",
        0,
        &result,
        &rpc_error,
        1,
        "string", library_id
    );

    searpc_free_client_with_pipe_transport(client);

    if (rpc_error) {
        g_propagate_error(error, rpc_error);
        return FALSE;
    }

    if (result != 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "seafile_unsync gab Fehlercode zurück: %d", result);
        return FALSE;
    }

    LOG_INFO("Library unsynced: %s\n", library_id);

    return TRUE;
}

gchar* sond_seafile_get_sync_status(const gchar *library_id,
                                     GError **error) {
    g_return_val_if_fail(library_id != NULL, NULL);

    SearpcClient *client = connect_to_seafile(error);
    if (!client) {
        return NULL;
    }

    /* RPC-Call: seafile_get_repo_sync_task (gibt JSON zurück) */
    GError *rpc_error = NULL;
    json_t *result = NULL;

    searpc_client_call(
        client,
        "seafile_get_repo_sync_task",
        "json",
        0,
        &result,
        &rpc_error,
        1,
        "string", library_id
    );

    if (rpc_error) {
        /* Kein Fehler wenn Library einfach nicht synchronisiert wird */
        g_error_free(rpc_error);
        searpc_free_client_with_pipe_transport(client);
        return NULL;
    }

    gchar *state = NULL;

    if (result && json_is_object(result)) {
        const char *state_str = json_string_value(json_object_get(result, "state"));
        if (state_str) {
            state = g_strdup(state_str);
        }
        json_decref(result);
    }

    searpc_free_client_with_pipe_transport(client);

    return state;
}

gboolean sond_seafile_test_connection(GError **error) {
    SearpcClient *client = connect_to_seafile(error);
    if (!client) {
        return FALSE;
    }

    /* Einfacher Ping-Test: Hole Repo-Liste */
    GError *rpc_error = NULL;
    json_t *result = NULL;

    searpc_client_call(
        client,
        "seafile_get_repo_list",
        "json",
        0,
        &result,
        &rpc_error,
        2,
        "int", (void*)0,   /* offset = 0 */
        "int", (void*)1    /* limit = 1 (nur eine Library holen) */
    );

    if (rpc_error) {
        g_propagate_error(error, rpc_error);
        searpc_free_client_with_pipe_transport(client);
        return FALSE;
    }

    if (result) {
        json_decref(result);
    }

    searpc_free_client_with_pipe_transport(client);

    LOG_INFO("Seafile-Client Verbindungstest erfolgreich\n");

    return TRUE;
}

gboolean sond_seafile_is_repo_in_sync(const gchar *library_id,
                                       GError **error) {
    g_return_val_if_fail(library_id != NULL, FALSE);

    SearpcClient *client = connect_to_seafile(error);
    if (!client) {
        return FALSE;
    }

    /* RPC-Call: seafile_get_repo_sync_task */
    json_t *sync_task = NULL;
    GError *rpc_error = NULL;

    searpc_client_call(
        client,
        "seafile_get_repo_sync_task",
        "json",
        0,
        &sync_task,
        &rpc_error,
        1,
        "string", library_id
    );

    if (rpc_error) {
        /* Kein Sync-Task = nicht synchronisiert */
        g_propagate_error(error, rpc_error);
        searpc_free_client_with_pipe_transport(client);
        return FALSE;
    }

    if (!sync_task || !json_is_object(sync_task)) {
        /* Kein Task-Objekt = nicht synchronisiert */
        if (sync_task) json_decref(sync_task);
        searpc_free_client_with_pipe_transport(client);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "Kein Sync-Task für Repository %s", library_id);
        return FALSE;
    }

    /* Prüfe Status */
    const char *state = json_string_value(json_object_get(sync_task, "state"));
    gboolean is_synced = FALSE;

    if (state) {
        if (g_strcmp0(state, "done") == 0 || 
            g_strcmp0(state, "synchronized") == 0) {
            is_synced = TRUE;
            LOG_INFO("Repository %s ist synchronisiert\n", library_id);
        } else {
            LOG_INFO("Repository %s ist nicht synchronisiert (state=%s)\n", 
                     library_id, state);
        }
    }

    json_decref(sync_task);
    searpc_free_client_with_pipe_transport(client);

    return is_synced;
}
