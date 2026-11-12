/*
 sond (sond_server_archiv_job.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_server_archiv_job.h"
#include "sond_server_archiv.h"
#include "../sond_log_and_error.h"

#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <string.h>

extern SoupServer* sond_server_get_soup_server(SondServer *server);

/* Beendete Jobs bleiben so lange abfragbar */
#define JOB_RETENTION_SECONDS (7 * 24 * 3600)

typedef enum {
    JOB_RUNNING,
    JOB_OK,
    JOB_ERROR
} JobState;

typedef struct {
    gchar *library_name;
    gchar *archiv_name;
    gchar *user;
    gchar *message;
    gboolean restore;     /* TRUE: Wiederherstellen, sonst Archivieren */
    JobState state;
    gint64 start_time;    /* Unix-Sekunden */
    gint64 end_time;      /* 0 = läuft noch */
} ArchivJob;

typedef struct {
    SondServer *server;
    GMutex mutex;
    GHashTable *jobs;     /* library_name -> ArchivJob* */
} ArchivContext;

static void job_free(ArchivJob *job) {
    g_free(job->library_name);
    g_free(job->archiv_name);
    g_free(job->user);
    g_free(job->message);
    g_free(job);
}

/* ------------------------------------------------------------------------
 * Antworten
 * ---------------------------------------------------------------------- */

static void send_json(SoupServerMessage *msg, guint status, JsonBuilder *builder) {
    JsonGenerator *gen = json_generator_new();
    JsonNode *root = json_builder_get_root(builder);
    gchar *json = NULL;
    GBytes *bytes = NULL;

    json_generator_set_root(gen, root);
    json = json_generator_to_data(gen, NULL);
    soup_server_message_set_status(msg, status, NULL);
    soup_message_headers_set_content_type(soup_server_message_get_response_headers(msg),
            "application/json", NULL);
    bytes = g_bytes_new(json, strlen(json));
    soup_message_body_append_bytes(soup_server_message_get_response_body(msg), bytes);
    g_bytes_unref(bytes);
    g_free(json);
    json_node_free(root);
    g_object_unref(gen);
}

static void send_error(SoupServerMessage *msg, guint status, const gchar *text) {
    JsonBuilder *b = json_builder_new();

    json_builder_begin_object(b);
    json_builder_set_member_name(b, "success");
    json_builder_add_boolean_value(b, FALSE);
    json_builder_set_member_name(b, "error");
    json_builder_add_string_value(b, text);
    json_builder_end_object(b);
    send_json(msg, status, b);
    g_object_unref(b);
}

static const gchar* state_name(JobState state) {
    switch (state) {
    case JOB_RUNNING: return "running";
    case JOB_OK:      return "ok";
    default:          return "error";
    }
}

static void add_time(JsonBuilder *b, const gchar *member, gint64 unix_secs) {
    GDateTime *dt = NULL;
    gchar *iso = NULL;

    if (unix_secs <= 0)
        return;
    dt = g_date_time_new_from_unix_utc(unix_secs);
    iso = g_date_time_format_iso8601(dt);
    json_builder_set_member_name(b, member);
    json_builder_add_string_value(b, iso);
    g_free(iso);
    g_date_time_unref(dt);
}

/* Job als Objekt in den Builder schreiben (Mutex gehalten) */
static void add_job(JsonBuilder *b, const ArchivJob *job) {
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "library_name");
    json_builder_add_string_value(b, job->library_name);
    json_builder_set_member_name(b, "state");
    json_builder_add_string_value(b, state_name(job->state));
    json_builder_set_member_name(b, "art");
    json_builder_add_string_value(b, job->restore ? "wiederherstellen" : "archivieren");
    json_builder_set_member_name(b, "archiv_name");
    json_builder_add_string_value(b, job->archiv_name);
    json_builder_set_member_name(b, "user");
    json_builder_add_string_value(b, job->user);
    if (job->message) {
        json_builder_set_member_name(b, "message");
        json_builder_add_string_value(b, job->message);
    }
    add_time(b, "start_time", job->start_time);
    add_time(b, "end_time", job->end_time);
    json_builder_end_object(b);
}

/* ------------------------------------------------------------------------
 * Hintergrund-Thread
 * ---------------------------------------------------------------------- */

typedef struct {
    ArchivContext *ctx;
    gchar *library_name;
    gchar *archiv_name;
    gboolean restore;
} ThreadData;

static gpointer archiv_thread(gpointer data) {
    ThreadData *td = data;
    GError *error = NULL;
    gboolean ok = td->restore ?
            sond_server_archiv_restore(td->ctx->server, td->library_name,
                    td->archiv_name, &error) :
            sond_server_archiv_library(td->ctx->server, td->library_name,
                    td->archiv_name, TRUE, &error);
    ArchivJob *job = NULL;

    g_mutex_lock(&td->ctx->mutex);
    job = g_hash_table_lookup(td->ctx->jobs, td->library_name);
    if (job) {
        job->state = ok ? JOB_OK : JOB_ERROR;
        job->end_time = g_get_real_time() / G_USEC_PER_SEC;
        g_free(job->message);
        job->message = ok ? NULL : g_strdup(error ? error->message : "unbekannter Fehler");
    }
    g_mutex_unlock(&td->ctx->mutex);

    if (ok)
        LOG_INFO("%s von '%s' beendet\n",
                td->restore ? "Wiederherstellung" : "Archivierung", td->library_name);
    else
        LOG_ERROR("%s von '%s' fehlgeschlagen: %s\n",
                td->restore ? "Wiederherstellung" : "Archivierung", td->library_name,
                error ? error->message : "unbekannter Fehler");

    g_clear_error(&error);
    g_free(td->library_name);
    g_free(td->archiv_name);
    g_free(td);

    return NULL;
}

/* ------------------------------------------------------------------------
 * Handler
 * ---------------------------------------------------------------------- */

static void start_job(SoupServerMessage *msg, ArchivContext *ctx, gboolean restore) {
    const gchar *username = NULL;
    SoupMessageBody *body = NULL;
    JsonParser *parser = NULL;
    JsonObject *obj = NULL;
    const gchar *library_name = NULL;
    const gchar *archiv_name = NULL;
    gchar *busy = NULL;
    GHashTableIter iter;
    gpointer key = NULL;
    gpointer value = NULL;
    gint64 now = g_get_real_time() / G_USEC_PER_SEC;
    JsonBuilder *b = NULL;

    if (strcmp(soup_server_message_get_method(msg), "POST") != 0) {
        send_error(msg, SOUP_STATUS_METHOD_NOT_ALLOWED, "Only POST allowed");
        return;
    }
    if (!sond_server_check_authorization(ctx->server, msg, &username))
        return;

    body = soup_server_message_get_request_body(msg);
    if (!body->data) {
        send_error(msg, SOUP_STATUS_BAD_REQUEST, "Empty request body");
        return;
    }
    parser = json_parser_new();
    if (!json_parser_load_from_data(parser, body->data, -1, NULL) ||
            !JSON_NODE_HOLDS_OBJECT(json_parser_get_root(parser))) {
        send_error(msg, SOUP_STATUS_BAD_REQUEST, "Invalid JSON");
        g_object_unref(parser);
        return;
    }
    obj = json_node_get_object(json_parser_get_root(parser));
    if (json_object_has_member(obj, "library_name"))
        library_name = json_object_get_string_member(obj, "library_name");
    if (json_object_has_member(obj, "archiv_name"))
        archiv_name = json_object_get_string_member(obj, "archiv_name");

    if (!library_name || !library_name[0] || !archiv_name || !archiv_name[0] ||
            strpbrk(archiv_name, "/\\")) {
        send_error(msg, SOUP_STATUS_BAD_REQUEST,
                "'library_name' und 'archiv_name' (ohne Pfadtrenner) erforderlich");
        g_object_unref(parser);
        return;
    }

    g_mutex_lock(&ctx->mutex);

    //es läuft höchstens ein Job
    g_hash_table_iter_init(&iter, ctx->jobs);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        ArchivJob *j = value;

        if (j->state == JOB_RUNNING) {
            busy = g_strdup(j->library_name);
            break;
        }
    }
    if (busy) {
        gchar *text = g_strdup_printf("Es läuft bereits ein Archivierungs- oder Wiederherstellungsjob (Library '%s')", busy);

        g_mutex_unlock(&ctx->mutex);
        send_error(msg, SOUP_STATUS_CONFLICT, text);
        g_free(text);
        g_free(busy);
        g_object_unref(parser);
        return;
    }

    //alte, beendete Jobs entfernen
    g_hash_table_iter_init(&iter, ctx->jobs);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        ArchivJob *j = value;

        if (j->state != JOB_RUNNING && now - j->end_time > JOB_RETENTION_SECONDS)
            g_hash_table_iter_remove(&iter);
    }

    {
        ArchivJob *job = g_new0(ArchivJob, 1);
        ThreadData *td = g_new0(ThreadData, 1);
        GThread *thread = NULL;

        job->library_name = g_strdup(library_name);
        job->archiv_name = g_strdup(archiv_name);
        job->user = g_strdup(username);
        job->restore = restore;
        job->state = JOB_RUNNING;
        job->start_time = now;
        g_hash_table_replace(ctx->jobs, g_strdup(library_name), job);

        td->ctx = ctx;
        td->library_name = g_strdup(library_name);
        td->archiv_name = g_strdup(archiv_name);
        td->restore = restore;
        thread = g_thread_new("archiv-worker", archiv_thread, td);
        g_thread_unref(thread);
    }
    g_mutex_unlock(&ctx->mutex);

    LOG_INFO("%s von '%s' mit Archiv '%s' gestartet (%s)\n",
            restore ? "Wiederherstellung" : "Archivierung", library_name,
            archiv_name, username);

    b = json_builder_new();
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "success");
    json_builder_add_boolean_value(b, TRUE);
    json_builder_end_object(b);
    send_json(msg, SOUP_STATUS_OK, b);
    g_object_unref(b);
    g_object_unref(parser);
}

static void handle_archiv_start(SoupServer *soup_server, SoupServerMessage *msg,
                                const char *path, GHashTable *query, gpointer user_data) {
    (void) soup_server; (void) path; (void) query;
    start_job(msg, user_data, FALSE);
}

static void handle_archiv_restore(SoupServer *soup_server, SoupServerMessage *msg,
                                  const char *path, GHashTable *query, gpointer user_data) {
    (void) soup_server; (void) path; (void) query;
    start_job(msg, user_data, TRUE);
}

static void handle_archiv_status(SoupServer *soup_server, SoupServerMessage *msg,
                                 const char *path, GHashTable *query, gpointer user_data) {
    ArchivContext *ctx = user_data;
    const gchar *username = NULL;
    const gchar *library_name = query ? g_hash_table_lookup(query, "library_name") : NULL;
    JsonBuilder *b = NULL;

    (void) soup_server; (void) path;

    if (strcmp(soup_server_message_get_method(msg), "GET") != 0) {
        send_error(msg, SOUP_STATUS_METHOD_NOT_ALLOWED, "Only GET allowed");
        return;
    }
    if (!sond_server_check_authorization(ctx->server, msg, &username))
        return;

    b = json_builder_new();
    g_mutex_lock(&ctx->mutex);
    if (library_name && library_name[0]) {
        ArchivJob *job = g_hash_table_lookup(ctx->jobs, library_name);

        if (job) {
            add_job(b, job);
        } else {
            json_builder_begin_object(b);
            json_builder_set_member_name(b, "library_name");
            json_builder_add_string_value(b, library_name);
            json_builder_set_member_name(b, "state");
            json_builder_add_string_value(b, "none");
            json_builder_end_object(b);
        }
    } else {
        GHashTableIter iter;
        gpointer value = NULL;

        json_builder_begin_object(b);
        json_builder_set_member_name(b, "jobs");
        json_builder_begin_array(b);
        g_hash_table_iter_init(&iter, ctx->jobs);
        while (g_hash_table_iter_next(&iter, NULL, &value))
            add_job(b, value);
        json_builder_end_array(b);
        json_builder_end_object(b);
    }
    g_mutex_unlock(&ctx->mutex);

    send_json(msg, SOUP_STATUS_OK, b);
    g_object_unref(b);
}

void sond_server_archiv_job_register_handlers(SondServer *server) {
    ArchivContext *ctx = g_new0(ArchivContext, 1);
    SoupServer *soup_server = sond_server_get_soup_server(server);

    ctx->server = server;
    g_mutex_init(&ctx->mutex);
    ctx->jobs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
            (GDestroyNotify) job_free);

    soup_server_add_handler(soup_server, "/archiv/start", handle_archiv_start, ctx, NULL);
    soup_server_add_handler(soup_server, "/archiv/restore", handle_archiv_restore, ctx, NULL);
    soup_server_add_handler(soup_server, "/archiv/status", handle_archiv_status, ctx, NULL);

    LOG_INFO("Archiv handlers registered\n");
}
