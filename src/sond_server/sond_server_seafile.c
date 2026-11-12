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
#include <glib/gstdio.h>
#include <sqlite3.h>
#include <string.h>

#include "../sond_zond_schema.h"
#include "../sond_akte_name.h"

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
    
    /* Form-Data: username=xxx&password=yyy (korrekt kodiert - Werte koennen
     * '&'/'='/'%'/Leerzeichen enthalten) */
    gchar *form_data = soup_form_encode("username", user, "password", password, NULL);
    
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
 * sond_server_seafile_find_group_id_by_name:
 *
 * Seafile API:
 * GET https://seafile.example.com/api2/groups/
 * Authorization: Token xxx
 * Response: {"groups": [{"id": 1, "name": "xxx", ...}, ...], "replynum": N}
 */
gint sond_server_seafile_find_group_id_by_name(SondServer *server,
                                                const gchar *group_name,
                                                GError **error) {
    g_return_val_if_fail(server != NULL, -1);
    g_return_val_if_fail(group_name != NULL, -1);

    gchar const* seafile_url = sond_server_get_seafile_url(server);
    gchar const* auth_token = sond_server_get_seafile_token(server);
    if (!seafile_url || !auth_token) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Seafile URL/token not available");
        return -1;
    }

    gchar *url = g_strdup_printf("%s/api2/groups/", seafile_url);

    SoupSession *session = soup_session_new();
    SoupMessage *msg = soup_message_new("GET", url);
    g_free(url);

    gchar *auth_header = g_strdup_printf("Token %s", auth_token);
    soup_message_headers_append(soup_message_get_request_headers(msg),
                               "Authorization", auth_header);
    g_free(auth_header);

    GBytes *response = soup_session_send_and_read(session, msg, NULL, error);

    if (!response) {
        g_object_unref(msg);
        g_object_unref(session);
        return -1;
    }

    guint status = soup_message_get_status(msg);

    if (status != 200) {
        gsize size;
        const gchar *body = g_bytes_get_data(response, &size);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to list groups (HTTP %u): %.*s",
                   status, (int)size, body);

        g_bytes_unref(response);
        g_object_unref(msg);
        g_object_unref(session);
        return -1;
    }

    /* Response parsen: {"groups": [{"id": 1, "name": "xxx"}, ...]} */
    gsize size;
    const gchar *data = g_bytes_get_data(response, &size);

    JsonParser *parser = json_parser_new();
    gint found_id = 0;

    if (json_parser_load_from_data(parser, data, size, NULL)) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);

        if (json_object_has_member(obj, "groups")) {
            JsonArray *groups = json_object_get_array_member(obj, "groups");

            for (guint i = 0; i < json_array_get_length(groups); i++) {
                JsonObject *group = json_array_get_object_element(groups, i);

                if (json_object_has_member(group, "name")) {
                    const gchar *name = json_object_get_string_member(group, "name");
                    /* EXAKTER Match! */
                    if (name && g_strcmp0(name, group_name) == 0 &&
                        json_object_has_member(group, "id")) {
                        found_id = (gint)json_object_get_int_member(group, "id");
                        break;
                    }
                }
            }
        } else {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Seafile response missing 'groups' field");
            found_id = -1;
        }
    } else {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to parse Seafile response");
        found_id = -1;
    }

    g_object_unref(parser);
    g_bytes_unref(response);
    g_object_unref(msg);
    g_object_unref(session);

    return found_id;
}

/**
 * seafile_share_library_with_group:
 *
 * Teilt eine (gerade angelegte) Library mit der konfigurierten Seafile-
 * Gruppe (Schreibrecht "rw"). Ein Fehlschlag hierbei laesst die bereits
 * angelegte Library bestehen - es wird nur eine Warnung geloggt, kein
 * Abbruch von seafile_create_library().
 *
 * Seafile API:
 * PUT https://seafile.example.com/api2/repos/{repo_id}/dir/shared_items/?p=/
 * Authorization: Token xxx
 * Body: share_type=group&group_id=xxx&permission=rw
 */
static void seafile_share_library_with_group(SondServer *server,
                                              const gchar *library_id,
                                              guint group_id) {
    gchar const* seafile_url = sond_server_get_seafile_url(server);
    gchar const* auth_token = sond_server_get_seafile_token(server);
    if (!seafile_url || !auth_token) {
        LOG_WARN("Cannot share library '%s' with group %u: "
                 "Seafile URL/token not available\n", library_id, group_id);
        return;
    }

    gchar *url = g_strdup_printf("%s/api2/repos/%s/dir/shared_items/?p=/",
                                  seafile_url, library_id);

    SoupSession *session = soup_session_new();
    SoupMessage *msg = soup_message_new("PUT", url);
    g_free(url);

    gchar *auth_header = g_strdup_printf("Token %s", auth_token);
    soup_message_headers_append(soup_message_get_request_headers(msg),
                               "Authorization", auth_header);
    g_free(auth_header);

    soup_message_headers_set_content_type(soup_message_get_request_headers(msg),
                                          "application/x-www-form-urlencoded", NULL);

    gchar *form_data = g_strdup_printf("share_type=group&group_id=%u&permission=rw",
                                        group_id);
    soup_message_set_request_body_from_bytes(msg, "application/x-www-form-urlencoded",
        g_bytes_new_take(form_data, strlen(form_data)));

    GError *local_error = NULL;
    GBytes *response = soup_session_send_and_read(session, msg, NULL, &local_error);

    if (!response) {
        LOG_WARN("Failed to share library '%s' with group %u: %s\n",
                  library_id, group_id,
                  local_error ? local_error->message : "unknown error");
        g_clear_error(&local_error);
        g_object_unref(msg);
        g_object_unref(session);
        return;
    }

    guint status = soup_message_get_status(msg);
    if (status != 200) {
        gsize size;
        const gchar *body = g_bytes_get_data(response, &size);
        LOG_WARN("Failed to share library '%s' with group %u (HTTP %u): %.*s\n",
                  library_id, group_id, status, (int)size, body);
    } else {
        LOG_INFO("Library '%s' shared with group %u (rw)\n", library_id, group_id);
    }

    g_bytes_unref(response);
    g_object_unref(msg);
    g_object_unref(session);
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
    
    /* Form-Data: name=xxx&desc=yyy (korrekt kodiert - z.B. Aktennamen mit
     * '&' wuerden sonst das Feld zerschneiden) */
    gchar *form_data;
    if (desc && strlen(desc) > 0) {
        form_data = soup_form_encode("name", name, "desc", desc, NULL);
    } else {
        form_data = soup_form_encode("name", name, NULL);
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

    if (library_id && group_id > 0) {
        seafile_share_library_with_group(server, library_id, group_id);
    }

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

/**
 * seafile_upload_bytes:
 *
 * Lädt eine Datei in ein (vorhandenes) Verzeichnis einer Library hoch
 * (vorhandene gleichnamige Datei wird ersetzt). Anders als der Worker prüft
 * diese Funktion die HTTP-Status beider Schritte.
 *
 * Seafile API:
 * GET  https://seafile.example.com/api2/repos/{repo_id}/upload-link/?p={parent_dir}
 * POST {upload-link} (multipart: file, parent_dir, replace)
 */
static gboolean seafile_upload_bytes(SondServer *server,
                                      const gchar *library_id,
                                      const gchar *parent_dir,
                                      const gchar *filename,
                                      const guchar *data, gsize size,
                                      GError **error) {
    gchar const* seafile_url = sond_server_get_seafile_url(server);
    gchar const* auth_token = sond_server_get_seafile_token(server);
    if (!seafile_url || !auth_token) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Seafile URL/token not available");
        return FALSE;
    }

    SoupSession *session = soup_session_new();

    /* 1. Upload-Link holen */
    gchar *parent_esc = g_uri_escape_string(parent_dir, "/", FALSE);
    gchar *url = g_strdup_printf("%s/api2/repos/%s/upload-link/?p=%s",
                                  seafile_url, library_id, parent_esc);
    g_free(parent_esc);
    SoupMessage *msg = soup_message_new("GET", url);
    g_free(url);

    gchar *auth_header = g_strdup_printf("Token %s", auth_token);
    soup_message_headers_append(soup_message_get_request_headers(msg),
                               "Authorization", auth_header);
    g_free(auth_header);

    GBytes *link_bytes = soup_session_send_and_read(session, msg, NULL, error);
    if (!link_bytes) {
        g_object_unref(msg);
        g_object_unref(session);
        return FALSE;
    }

    guint status = soup_message_get_status(msg);
    gsize link_size = 0;
    gconstpointer link_data = g_bytes_get_data(link_bytes, &link_size);

    if (status != 200) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to get upload link (HTTP %u): %.*s",
                   status, (int)link_size, (const gchar*)link_data);
        g_bytes_unref(link_bytes);
        g_object_unref(msg);
        g_object_unref(session);
        return FALSE;
    }

    gchar *upload_url = g_strndup(link_data, link_size);
    g_strstrip(upload_url);
    if (upload_url[0] == '"') {
        gsize len = strlen(upload_url);
        gchar *tmp = g_strndup(upload_url + 1, len >= 2 ? len - 2 : 0);
        g_free(upload_url);
        upload_url = tmp;
    }
    g_bytes_unref(link_bytes);
    g_object_unref(msg);

    /* 2. Multipart-Body aufbauen */
    gchar *boundary = g_strdup_printf("----SondBoundary%016" G_GINT64_MODIFIER "x",
                                      g_get_monotonic_time());
    GString *body = g_string_new(NULL);

    g_string_append_printf(body, "--%s\r\n", boundary);
    g_string_append_printf(body,
        "Content-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
        "Content-Type: application/octet-stream\r\n\r\n", filename);
    g_string_append_len(body, (const gchar*)data, size);
    g_string_append(body, "\r\n");

    g_string_append_printf(body, "--%s\r\n", boundary);
    g_string_append_printf(body,
        "Content-Disposition: form-data; name=\"parent_dir\"\r\n\r\n%s\r\n",
        parent_dir);

    g_string_append_printf(body, "--%s\r\n", boundary);
    g_string_append(body,
        "Content-Disposition: form-data; name=\"replace\"\r\n\r\n1\r\n");

    g_string_append_printf(body, "--%s--\r\n", boundary);

    msg = soup_message_new("POST", upload_url);
    g_free(upload_url);

    gchar *content_type = g_strdup_printf("multipart/form-data; boundary=%s",
                                          boundary);
    g_free(boundary);

    gsize body_len = body->len;
    GBytes *body_bytes = g_bytes_new_take(g_string_free(body, FALSE), body_len);
    soup_message_set_request_body_from_bytes(msg, content_type, body_bytes);
    g_bytes_unref(body_bytes);
    g_free(content_type);

    GBytes *response = soup_session_send_and_read(session, msg, NULL, error);
    if (!response) {
        g_object_unref(msg);
        g_object_unref(session);
        return FALSE;
    }

    status = soup_message_get_status(msg);
    gboolean success = (status == 200 || status == 201);

    if (!success) {
        gsize resp_size = 0;
        const gchar *resp = g_bytes_get_data(response, &resp_size);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to upload '%s' (HTTP %u): %.*s",
                   filename, status, (int)resp_size, resp ? resp : "");
    }

    g_bytes_unref(response);
    g_object_unref(msg);
    g_object_unref(session);

    return success;
}

/**
 * seafile_create_project_file:
 *
 * Erzeugt eine leere zond-Projektdatei ".<library_name>.ZND" (Schema aus
 * sond_zond_schema_create()) und legt sie ins Wurzelverzeichnis der Library.
 * Der führende Punkt blendet sie im zond-Dateibaum aus.
 */
static gboolean seafile_create_project_file(SondServer *server,
                                             const gchar *library_id,
                                             const gchar *library_name,
                                             GError **error) {
    gchar *tmp_path = NULL;
    gchar *contents = NULL;
    gsize length = 0;
    sqlite3 *db = NULL;
    gboolean ok = FALSE;

    gint fd = g_file_open_tmp("sond_projekt_XXXXXX.znd", &tmp_path, error);
    if (fd < 0)
        return FALSE;
    g_close(fd, NULL);

    gint rc = sqlite3_open_v2(tmp_path, &db, SQLITE_OPEN_READWRITE, NULL);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Projektdatei konnte nicht angelegt werden: %s",
                   sqlite3_errstr(rc));
        sqlite3_close(db);
        goto out;
    }

    rc = sond_zond_schema_create(db, error);
    sqlite3_close(db);
    if (rc)
        goto out;

    if (!g_file_get_contents(tmp_path, &contents, &length, error))
        goto out;

    /* Pfadtrenner im Namen würden sonst den Dateinamen zerlegen */
    gchar *safe_name = g_strdup(library_name);
    g_strdelimit(safe_name, "/\\", '_');
    gchar *filename = g_strdup_printf(".%s.ZND", safe_name);
    g_free(safe_name);

    ok = seafile_upload_bytes(server, library_id, "/", filename,
                              (const guchar*)contents, length, error);
    g_free(filename);

out:
    g_free(contents);
    g_remove(tmp_path);
    g_free(tmp_path);

    return ok;
}

/**
 * seafile_api_call:
 *
 * Einfacher Aufruf der Seafile-API. @path_query ist alles hinter der Server-URL
 * (z. B. "/api2/repos/ID/dir/?p=%2F.sond"), @form_body ein optionaler
 * form-kodierter Body. Rückgabe TRUE, wenn eine Antwort eintraf (Status in
 * @status_out, Antworttext in @body_out); FALSE nur bei Verbindungsfehlern.
 */
static gboolean seafile_api_call(SondServer *server, const gchar *method,
                                 const gchar *path_query, const gchar *form_body,
                                 guint *status_out, gchar **body_out,
                                 GError **error) {
    gchar const *seafile_url = sond_server_get_seafile_url(server);
    gchar const *auth_token = sond_server_get_seafile_token(server);
    gchar *url = NULL;
    gchar *auth_header = NULL;
    SoupSession *session = NULL;
    SoupMessage *msg = NULL;
    GBytes *response = NULL;

    if (!seafile_url || !auth_token) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Seafile URL/token not available");
        return FALSE;
    }

    url = g_strdup_printf("%s%s", seafile_url, path_query);
    session = soup_session_new();
    msg = soup_message_new(method, url);
    g_free(url);

    auth_header = g_strdup_printf("Token %s", auth_token);
    soup_message_headers_append(soup_message_get_request_headers(msg),
                               "Authorization", auth_header);
    g_free(auth_header);

    if (form_body) {
        GBytes *bytes = g_bytes_new(form_body, strlen(form_body));

        soup_message_set_request_body_from_bytes(msg,
                "application/x-www-form-urlencoded", bytes);
        g_bytes_unref(bytes);
    }

    response = soup_session_send_and_read(session, msg, NULL, error);
    if (response) {
        gsize size = 0;
        const gchar *data = g_bytes_get_data(response, &size);

        if (status_out)
            *status_out = soup_message_get_status(msg);
        if (body_out)
            *body_out = g_strndup(data ? data : "", size);
        g_bytes_unref(response);
    }

    g_object_unref(msg);
    g_object_unref(session);

    return response != NULL;
}

/* Verzeichnis der Library anlegen, falls es fehlt (@path absolut, z. B. "/.sond") */
static gboolean seafile_ensure_dir(SondServer *server, const gchar *library_id,
                                   const gchar *path, GError **error) {
    gchar *esc = g_uri_escape_string(path, "/", FALSE);
    gchar *query = g_strdup_printf("/api2/repos/%s/dir/?p=%s", library_id, esc);
    guint status = 0;
    gchar *body = NULL;
    gboolean ok = FALSE;

    g_free(esc);

    //vorhanden?
    if (!seafile_api_call(server, "GET", query, NULL, &status, &body, error))
        goto out;
    g_clear_pointer(&body, g_free);
    if (status == 200) {
        ok = TRUE;
        goto out;
    }

    if (!seafile_api_call(server, "POST", query, "operation=mkdir", &status,
            &body, error))
        goto out;
    ok = (status == 200 || status == 201);
    if (!ok)
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Failed to create directory '%s' (HTTP %u): %s", path,
                   status, body ? body : "");

out:
    g_free(body);
    g_free(query);

    return ok;
}

/**
 * seafile_set_name_file:
 * @art: SOND_AKTE_NAME_BEZEICHNUNG oder SOND_AKTE_NAME_GEGENSTAND
 * @wert: Bezeichnung bzw. Gegenstand
 * @entfernen_wenn_leer: bei leerem Wert die vorhandene Datei dieser Art
 *   löschen (sonst bleibt alles unverändert)
 *
 * Legt den Wert als leere Datei .sond/akte/<Art>~<kodierter Wert> in der
 * Library ab (s. sond_akte_name.h). Eine andere Datei derselben Art (auch
 * eine ältere ohne Vorsilbe, die als Bezeichnung gilt) wird gelöscht, der Wert
 * ersetzt also den bisherigen; ist er schon so eingetragen, geschieht nichts.
 * Dateien der anderen Art bleiben unberührt.
 */
static gboolean seafile_set_name_file(SondServer *server,
                                      const gchar *library_id,
                                      gchar art,
                                      const gchar *wert,
                                      gboolean entfernen_wenn_leer,
                                      GError **error) {
    const gchar *dir = "/" SOND_AKTE_NAME_DIR;
    gchar *name = sond_akte_name_make(art, wert);
    gchar *dir_esc = NULL;
    gchar *query = NULL;
    gchar *body = NULL;
    guint status = 0;
    gboolean present = FALSE;
    gboolean ok = FALSE;
    GPtrArray *others = g_ptr_array_new_with_free_func(g_free);

    if (!name && !entfernen_wenn_leer) {
        g_ptr_array_unref(others);

        return TRUE;
    }

    //zum Anlegen müssen die Verzeichnisse da sein; zum bloßen Entfernen nicht
    if (name && (!seafile_ensure_dir(server, library_id, "/.sond", error) ||
            !seafile_ensure_dir(server, library_id, dir, error)))
        goto out;

    //vorhandene Einträge
    dir_esc = g_uri_escape_string(dir, "/", FALSE);
    query = g_strdup_printf("/api2/repos/%s/dir/?p=%s", library_id, dir_esc);
    if (!seafile_api_call(server, "GET", query, NULL, &status, &body, error))
        goto out;

    if (status == 200 && body) {
        JsonParser *parser = json_parser_new();

        if (json_parser_load_from_data(parser, body, -1, NULL)) {
            JsonNode *root = json_parser_get_root(parser);

            if (root && JSON_NODE_HOLDS_ARRAY(root)) {
                JsonArray *arr = json_node_get_array(root);

                for (guint i = 0; i < json_array_get_length(arr); i++) {
                    JsonObject *entry = json_array_get_object_element(arr, i);
                    const gchar *entry_name = NULL;

                    if (!entry || !json_object_has_member(entry, "name"))
                        continue;
                    entry_name = json_object_get_string_member(entry, "name");
                    if (!entry_name)
                        continue;

                    //nur Einträge derselben Art betreffen den neuen Wert
                    if (sond_akte_name_parse(entry_name, NULL, NULL) != art)
                        continue;

                    if (name && g_strcmp0(entry_name, name) == 0)
                        present = TRUE;
                    else
                        g_ptr_array_add(others, g_strdup(entry_name));
                }
            }
        }
        g_object_unref(parser);
    }

    if (name && !present && !seafile_upload_bytes(server, library_id, dir, name,
            (const guchar*) "", 0, error))
        goto out;

    //erst nach erfolgreichem Anlegen den bisherigen Wert entfernen
    for (guint i = 0; i < others->len; i++) {
        gchar *path = g_strconcat(dir, "/",
                (gchar*) g_ptr_array_index(others, i), NULL);
        gchar *path_esc = g_uri_escape_string(path, "/", FALSE);
        gchar *del_query = g_strdup_printf("/api2/repos/%s/file/?p=%s",
                library_id, path_esc);
        gchar *del_body = NULL;
        GError *del_error = NULL;

        if (!seafile_api_call(server, "DELETE", del_query, NULL, &status,
                &del_body, &del_error) || (status != 200 && status != 204))
            LOG_ERROR("Alter Eintrag '%s' konnte nicht entfernt werden: "
                      "%s\n", path, del_error ? del_error->message :
                      (del_body ? del_body : "unknown"));
        g_clear_error(&del_error);
        g_free(del_body);
        g_free(del_query);
        g_free(path_esc);
        g_free(path);
    }

    ok = TRUE;

out:
    g_ptr_array_unref(others);
    g_free(dir_esc);
    g_free(query);
    g_free(body);
    g_free(name);

    return ok;
}

/* Bezeichnung und Gegenstand (je nullable = unverändert; Gegenstand leer =
 * Eintrag entfernen, Bezeichnung leer = unverändert) */
static gboolean seafile_set_akte_names(SondServer *server,
                                       const gchar *library_id,
                                       const gchar *bezeichnung,
                                       const gchar *gegenstand,
                                       GError **error) {
    if (bezeichnung && !seafile_set_name_file(server, library_id,
            SOND_AKTE_NAME_BEZEICHNUNG, bezeichnung, FALSE, error))
        return FALSE;

    if (gegenstand && !seafile_set_name_file(server, library_id,
            SOND_AKTE_NAME_GEGENSTAND, gegenstand, TRUE, error))
        return FALSE;

    return TRUE;
}

/* ========================================================================
 * Request Handlers
 * ======================================================================== */

/**
 * sond_server_seafile_get_library_id_by_name:
 *
 * Sucht eine Library anhand des Namens und gibt die ID zurück.
 *
 * Seafile API:
 * GET https://seafile.example.com/api2/repos/?nameContains=XXX
 * Authorization: Token xxx
 * Response: [{"repo_id": "xxx", "repo_name": "yyy", ...}, ...]
 */
gchar* sond_server_seafile_get_library_id_by_name(SondServer *server,
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
 * Body: {"name": "2026-23", "desc": "optional", "bezeichnung": "optional",
 *        "gegenstand": "optional"}
 * (bezeichnung/gegenstand: werden als .sond/akte/B~<Name> bzw.
 *  .sond/akte/G~<Name> abgelegt)
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

    /* Auth-Check */
    const gchar *username = NULL;
    if (!sond_server_check_authorization(server, msg, &username)) {
        return;  /* 401 wurde bereits gesendet */
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

    /* Der Library-Name (Jahr-LfdNr) darf nie doppelt vorkommen; Seafile selbst
     * lässt gleiche Namen zu, deshalb hier prüfen. */
    gchar *existing_id = sond_server_seafile_get_library_id_by_name(server, name,
            &error);

    if (existing_id) {
        gchar *err_msg = g_strdup_printf("Library '%s' already exists", name);

        send_error_response(msg, SOUP_STATUS_CONFLICT, err_msg);
        g_free(err_msg);
        g_free(existing_id);
        g_object_unref(parser);

        return;
    }
    if (error && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
        gchar *err_msg = g_strdup_printf("Could not check for existing library: "
                "%s", error->message);

        send_error_response(msg, SOUP_STATUS_INTERNAL_SERVER_ERROR, err_msg);
        g_free(err_msg);
        g_clear_error(&error);
        g_object_unref(parser);

        return;
    }
    g_clear_error(&error);

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

    /* Leere Projektdatei anlegen. Scheitert das, wird die Library wieder
     * gelöscht, damit keine Akte mit unvollständigem Speicherplatz bleibt. */
    if (!seafile_create_project_file(server, library_id, name, &error)) {
        GError *del_error = NULL;

        LOG_ERROR("Projektdatei fuer Library '%s' fehlgeschlagen, loesche "
                  "Library (Rollback): %s\n", name,
                  error ? error->message : "unknown");

        if (!seafile_delete_library(server, library_id, &del_error)) {
            LOG_ERROR("Rollback fehlgeschlagen, Library '%s' (%s) bleibt "
                      "bestehen: %s\n", name, library_id,
                      del_error ? del_error->message : "unknown");
            g_clear_error(&del_error);
        }

        gchar *err_msg = g_strdup_printf("Failed to create project file: %s",
                                        error ? error->message : "unknown");
        send_error_response(msg, SOUP_STATUS_INTERNAL_SERVER_ERROR, err_msg);
        g_free(err_msg);
        g_clear_error(&error);
        g_free(library_id);
        g_object_unref(parser);

        return;
    }

    /* Aktenbezeichnung als Dateiname ablegen. Reine Zusatzangabe für die
     * Suche ohne Server: ein Fehlschlag lässt die Akte bestehen (der Client
     * kann die Bezeichnung über /seafile/library-bezeichnung nachtragen). */
    {
        const gchar *bezeichnung = json_object_has_member(obj, "bezeichnung") ?
                json_object_get_string_member(obj, "bezeichnung") : NULL;
        const gchar *gegenstand = json_object_has_member(obj, "gegenstand") ?
                json_object_get_string_member(obj, "gegenstand") : NULL;

        if ((bezeichnung || gegenstand) && !seafile_set_akte_names(server,
                library_id, bezeichnung, gegenstand, &error)) {
            LOG_ERROR("Aktenbezeichnung/Gegenstand fuer Library '%s' nicht "
                      "abgelegt: %s\n", name,
                      error ? error->message : "unknown");
            g_clear_error(&error);
        }
    }

    /* Response */
    JsonBuilder *response_builder = json_builder_new();
    json_builder_begin_object(response_builder);
    json_builder_set_member_name(response_builder, "library_id");
    json_builder_add_string_value(response_builder, library_id);
    json_builder_set_member_name(response_builder, "name");
    json_builder_add_string_value(response_builder, name);
    json_builder_end_object(response_builder);

    g_free(library_id);
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
 * handle_seafile_library_bezeichnung:
 * POST /seafile/library-bezeichnung
 *
 * Body: {"name": "2026-23", "bezeichnung": "Meier ./. Müller",
 *        "gegenstand": "Kündigungsschutz"}   (mindestens eines von beiden)
 * Setzt bzw. ersetzt die Aktenbezeichnung (.sond/akte) und/oder den Gegenstand
 * (jeweils in .sond/akte; Gegenstand leer = entfernen) der Library.
 * Response: {"success": true, "data": {"name": "2026-23"}}
 */
static void handle_seafile_library_bezeichnung(SoupServer *soup_server,
                                               SoupServerMessage *msg,
                                               const char *path,
                                               GHashTable *query,
                                               gpointer user_data) {
    SondServer *server = (SondServer*)user_data;
    const gchar *username = NULL;
    SoupMessageBody *body = NULL;
    GError *error = NULL;
    JsonParser *parser = NULL;
    JsonObject *obj = NULL;
    const gchar *name = NULL;
    const gchar *bezeichnung = NULL;
    const gchar *gegenstand = NULL;
    gchar *library_id = NULL;

    if (strcmp(soup_server_message_get_method(msg), "POST") != 0) {
        send_error_response(msg, SOUP_STATUS_METHOD_NOT_ALLOWED, "Only POST allowed");
        return;
    }

    if (!sond_server_check_authorization(server, msg, &username))
        return;  /* 401 wurde bereits gesendet */

    body = soup_server_message_get_request_body(msg);
    if (!body->data) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST, "Empty request body");
        return;
    }

    parser = json_parser_new();
    if (!json_parser_load_from_data(parser, body->data, -1, &error) ||
            !JSON_NODE_HOLDS_OBJECT(json_parser_get_root(parser))) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST, "Invalid JSON");
        g_clear_error(&error);
        g_object_unref(parser);
        return;
    }

    obj = json_node_get_object(json_parser_get_root(parser));
    name = json_object_has_member(obj, "name") ?
            json_object_get_string_member(obj, "name") : NULL;
    bezeichnung = json_object_has_member(obj, "bezeichnung") ?
            json_object_get_string_member(obj, "bezeichnung") : NULL;

    gegenstand = json_object_has_member(obj, "gegenstand") ?
            json_object_get_string_member(obj, "gegenstand") : NULL;

    if (!name || !*name || (!bezeichnung && !gegenstand)) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST,
                "Missing 'name' or 'bezeichnung'/'gegenstand' field");
        g_object_unref(parser);
        return;
    }

    library_id = sond_server_seafile_get_library_id_by_name(server, name, &error);
    if (!library_id) {
        gchar *err_msg = g_strdup_printf("Library '%s' not found: %s", name,
                error ? error->message : "unknown");

        send_error_response(msg, SOUP_STATUS_NOT_FOUND, err_msg);
        g_free(err_msg);
        g_clear_error(&error);
        g_object_unref(parser);
        return;
    }

    if (!seafile_set_akte_names(server, library_id, bezeichnung, gegenstand,
            &error)) {
        gchar *err_msg = g_strdup_printf("Failed to set Aktenbezeichnung: %s",
                error ? error->message : "unknown");

        send_error_response(msg, SOUP_STATUS_INTERNAL_SERVER_ERROR, err_msg);
        g_free(err_msg);
        g_clear_error(&error);
    } else {
        gchar *response_json = g_strdup("{\"ok\":true}");

        send_success_response(msg, response_json);
        g_free(response_json);
    }

    g_free(library_id);
    g_object_unref(parser);
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

    /* Auth-Check */
    const gchar *username = NULL;
    if (!sond_server_check_authorization(server, msg, &username)) {
        return;  /* 401 wurde bereits gesendet */
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
    gchar *library_id = sond_server_seafile_get_library_id_by_name(server, library_name, &error);

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
    
    soup_server_add_handler(soup_server, "/seafile/library-id",
                            handle_seafile_library_id_get, server, NULL);
    soup_server_add_handler(soup_server, "/seafile/library-bezeichnung",
                            handle_seafile_library_bezeichnung, server, NULL);
    soup_server_add_handler(soup_server, "/seafile/library",
                           handle_seafile_library_create, server, NULL);

    LOG_INFO("Seafile handlers registered\n");
}
