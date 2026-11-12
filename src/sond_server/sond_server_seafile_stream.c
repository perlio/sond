/*
 sond (sond_server_seafile_stream.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_server_seafile_stream.h"

#include <libsoup/soup.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>

extern gchar const* sond_server_get_seafile_url(SondServer *server);
extern gchar const* sond_server_get_seafile_token(SondServer *server);

/* ------------------------------------------------------------------------
 * Multipart-Körper als Stream: Kopf + Datei + Ende
 * ---------------------------------------------------------------------- */

#define SOND_TYPE_MULTIPART_STREAM (sond_multipart_stream_get_type())
G_DECLARE_FINAL_TYPE(SondMultipartStream, sond_multipart_stream, SOND, MULTIPART_STREAM, GInputStream)

struct _SondMultipartStream {
    GInputStream parent_instance;
    GBytes *head;
    GBytes *tail;
    GInputStream *file;
    gint phase;      /* 0 = Kopf, 1 = Datei, 2 = Ende, 3 = fertig */
    gsize offset;    /* Position im aktuellen Bytes-Abschnitt */
};

G_DEFINE_TYPE(SondMultipartStream, sond_multipart_stream, G_TYPE_INPUT_STREAM)

static gssize multipart_read(GInputStream *stream, void *buffer, gsize count,
                             GCancellable *cancellable, GError **error) {
    SondMultipartStream *self = SOND_MULTIPART_STREAM(stream);

    while (self->phase < 3) {
        if (self->phase == 1) {
            gssize n = g_input_stream_read(self->file, buffer, count, cancellable, error);

            if (n != 0)
                return n;   /* Daten oder Fehler (-1) */
            self->phase = 2;
            self->offset = 0;
            continue;
        }

        GBytes *seg = (self->phase == 0) ? self->head : self->tail;
        gsize size = 0;
        const guchar *data = g_bytes_get_data(seg, &size);

        if (self->offset < size) {
            gsize n = MIN(count, size - self->offset);

            memcpy(buffer, data + self->offset, n);
            self->offset += n;

            return (gssize) n;
        }

        self->phase++;
        self->offset = 0;
    }

    return 0;
}

static void multipart_finalize(GObject *object) {
    SondMultipartStream *self = SOND_MULTIPART_STREAM(object);

    g_clear_pointer(&self->head, g_bytes_unref);
    g_clear_pointer(&self->tail, g_bytes_unref);
    g_clear_object(&self->file);

    G_OBJECT_CLASS(sond_multipart_stream_parent_class)->finalize(object);
}

static void sond_multipart_stream_class_init(SondMultipartStreamClass *klass) {
    G_OBJECT_CLASS(klass)->finalize = multipart_finalize;
    G_INPUT_STREAM_CLASS(klass)->read_fn = multipart_read;
}

static void sond_multipart_stream_init(SondMultipartStream *self) {
    (void) self;
}

/* ------------------------------------------------------------------------
 * Hilfsfunktionen
 * ---------------------------------------------------------------------- */

static void set_auth(SoupMessage *msg, const gchar *token) {
    gchar *value = g_strdup_printf("Token %s", token);

    soup_message_headers_append(soup_message_get_request_headers(msg),
            "Authorization", value);
    g_free(value);
}

/* Holt den Download- bzw. Upload-Link (Antwort ist ein JSON-String). */
static gchar* get_link(SoupSession *session, const gchar *token, const gchar *url,
                       GError **error) {
    SoupMessage *msg = soup_message_new("GET", url);
    GBytes *bytes = NULL;
    gchar *link = NULL;
    gsize size = 0;
    const gchar *data = NULL;
    guint status = 0;

    if (!msg) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "Ungültige URL: %s", url);
        return NULL;
    }
    set_auth(msg, token);

    bytes = soup_session_send_and_read(session, msg, NULL, error);
    if (!bytes) {
        g_object_unref(msg);
        return NULL;
    }

    status = soup_message_get_status(msg);
    data = g_bytes_get_data(bytes, &size);
    if (status != 200) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "Seafile-Link nicht erhalten (HTTP %u): %.*s",
                status, (int) size, data ? data : "");
    } else {
        link = g_strndup(data, size);
        g_strstrip(link);
        if (link[0] == '"') {
            gsize len = strlen(link);
            gchar *tmp = g_strndup(link + 1, len >= 2 ? len - 2 : 0);

            g_free(link);
            link = tmp;
        }
    }

    g_bytes_unref(bytes);
    g_object_unref(msg);

    return link;
}

/* Zeichen, die den Kopf des Multipart-Teils zerstören würden, ersetzen. */
static gchar* safe_filename(const gchar *name) {
    gchar *s = g_strdup(name);

    for (gchar *p = s; *p; p++)
        if (*p == '"' || *p == '\\' || *p == '\r' || *p == '\n')
            *p = '_';

    return s;
}

/* ------------------------------------------------------------------------
 * Download
 * ---------------------------------------------------------------------- */

gboolean sond_server_seafile_download_file(SondServer *server,
        const gchar *library_id, const gchar *path, const gchar *dest_path,
        GError **error) {
    g_return_val_if_fail(server && library_id && path && dest_path, FALSE);

    const gchar *seafile_url = sond_server_get_seafile_url(server);
    const gchar *token = sond_server_get_seafile_token(server);
    SoupSession *session = NULL;
    SoupMessage *msg = NULL;
    GInputStream *in = NULL;
    GFile *dest = NULL;
    GFileOutputStream *out = NULL;
    gchar *esc = NULL;
    gchar *url = NULL;
    gchar *link = NULL;
    gssize copied = -1;
    gint64 expected = -1;
    gboolean ok = FALSE;

    if (!seafile_url || !token) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "Seafile URL/token not available");
        return FALSE;
    }

    session = soup_session_new();
    esc = g_uri_escape_string(path, "/", FALSE);
    url = g_strdup_printf("%s/api2/repos/%s/file/?p=%s", seafile_url, library_id, esc);
    g_free(esc);

    link = get_link(session, token, url, error);
    g_free(url);
    if (!link)
        goto out;

    msg = soup_message_new("GET", link);
    if (!msg) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "Ungültiger Downloadlink für '%s'", path);
        goto out;
    }

    in = soup_session_send(session, msg, NULL, error);
    if (!in)
        goto out;

    if (soup_message_get_status(msg) != 200) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "Download von '%s' fehlgeschlagen (HTTP %u)", path,
                soup_message_get_status(msg));
        goto out;
    }
    expected = soup_message_headers_get_content_length(
            soup_message_get_response_headers(msg));

    dest = g_file_new_for_path(dest_path);
    out = g_file_replace(dest, NULL, FALSE, G_FILE_CREATE_REPLACE_DESTINATION, NULL, error);
    if (!out)
        goto out;

    copied = g_output_stream_splice(G_OUTPUT_STREAM(out), in,
            G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE | G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET,
            NULL, error);
    if (copied < 0)
        goto out;

    if (expected >= 0 && copied != expected) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "Download von '%s' unvollständig (%" G_GSSIZE_FORMAT " von %"
                G_GINT64_FORMAT " Bytes)", path, copied, expected);
        goto out;
    }

    ok = TRUE;

out:
    if (!ok && dest_path)
        g_remove(dest_path);
    g_clear_object(&out);
    g_clear_object(&in);
    g_clear_object(&dest);
    g_clear_object(&msg);
    g_clear_object(&session);
    g_free(link);

    return ok;
}

/* ------------------------------------------------------------------------
 * Upload
 * ---------------------------------------------------------------------- */

gboolean sond_server_seafile_upload_file(SondServer *server,
        const gchar *library_id, const gchar *parent_dir, const gchar *filename,
        const gchar *src_path, GError **error) {
    g_return_val_if_fail(server && library_id && parent_dir && filename && src_path, FALSE);

    const gchar *seafile_url = sond_server_get_seafile_url(server);
    const gchar *token = sond_server_get_seafile_token(server);
    SoupSession *session = NULL;
    SoupMessage *msg = NULL;
    GFile *src = NULL;
    GFileInfo *info = NULL;
    GFileInputStream *file_in = NULL;
    SondMultipartStream *body = NULL;
    GBytes *response = NULL;
    GString *head = NULL;
    GString *tail = NULL;
    gchar *esc = NULL;
    gchar *url = NULL;
    gchar *link = NULL;
    gchar *boundary = NULL;
    gchar *name = NULL;
    gchar *content_type = NULL;
    gint64 file_size = 0;
    guint status = 0;
    gboolean ok = FALSE;

    if (!seafile_url || !token) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "Seafile URL/token not available");
        return FALSE;
    }

    src = g_file_new_for_path(src_path);
    info = g_file_query_info(src, G_FILE_ATTRIBUTE_STANDARD_SIZE,
            G_FILE_QUERY_INFO_NONE, NULL, error);
    if (!info)
        goto out;
    file_size = g_file_info_get_size(info);

    file_in = g_file_read(src, NULL, error);
    if (!file_in)
        goto out;

    session = soup_session_new();
    esc = g_uri_escape_string(parent_dir, "/", FALSE);
    url = g_strdup_printf("%s/api2/repos/%s/upload-link/?p=%s", seafile_url, library_id, esc);
    g_free(esc);

    link = get_link(session, token, url, error);
    g_free(url);
    if (!link)
        goto out;

    boundary = g_strdup_printf("----SondBoundary%08x%08x%016" G_GINT64_MODIFIER "x",
            g_random_int(), g_random_int(), (guint64) g_get_monotonic_time());
    name = safe_filename(filename);

    head = g_string_new(NULL);
    g_string_append_printf(head, "--%s\r\n"
            "Content-Disposition: form-data; name=\"parent_dir\"\r\n\r\n%s\r\n"
            "--%s\r\n"
            "Content-Disposition: form-data; name=\"replace\"\r\n\r\n1\r\n"
            "--%s\r\n"
            "Content-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
            "Content-Type: application/octet-stream\r\n\r\n",
            boundary, parent_dir, boundary, boundary, name);
    tail = g_string_new(NULL);
    g_string_append_printf(tail, "\r\n--%s--\r\n", boundary);

    body = g_object_new(SOND_TYPE_MULTIPART_STREAM, NULL);
    body->file = G_INPUT_STREAM(g_object_ref(file_in));
    body->head = g_bytes_new(head->str, head->len);
    body->tail = g_bytes_new(tail->str, tail->len);

    msg = soup_message_new("POST", link);
    if (!msg) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Ungültiger Uploadlink");
        goto out;
    }
    set_auth(msg, token);

    content_type = g_strdup_printf("multipart/form-data; boundary=%s", boundary);
    soup_message_set_request_body(msg, content_type, G_INPUT_STREAM(body),
            (gssize) (head->len + file_size + tail->len));

    response = soup_session_send_and_read(session, msg, NULL, error);
    if (!response)
        goto out;

    status = soup_message_get_status(msg);
    if (status != 200 && status != 201) {
        gsize size = 0;
        const gchar *text = g_bytes_get_data(response, &size);

        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "Upload von '%s' fehlgeschlagen (HTTP %u): %.*s",
                filename, status, (int) size, text ? text : "");
        goto out;
    }

    ok = TRUE;

out:
    if (head)
        g_string_free(head, TRUE);
    if (tail)
        g_string_free(tail, TRUE);
    g_clear_pointer(&response, g_bytes_unref);
    g_clear_object(&msg);
    g_clear_object(&body);
    g_clear_object(&file_in);
    g_clear_object(&info);
    g_clear_object(&src);
    g_clear_object(&session);
    g_free(link);
    g_free(boundary);
    g_free(name);
    g_free(content_type);

    return ok;
}
