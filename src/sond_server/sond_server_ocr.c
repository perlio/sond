/*
 sond (sond_server_ocr.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_server_ocr.h"
#include "sond_server_seafile.h"
#include "../sond_log_and_error.h"
#include "../misc.h"

#include "../zond/99conv/pdf.h"

#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

/* =======================================================================
 * Forward Declarations - External
 * ======================================================================= */

extern const gchar* sond_server_get_seafile_url(SondServer *server);
extern const gchar* sond_server_get_seafile_token(SondServer *server);
extern SoupServer* sond_server_get_soup_server(SondServer *server);
static gboolean ocr_cleanup_old_jobs_callback(gpointer user_data);
static void ocr_manager_register_handlers(OcrManager *manager);

/* =======================================================================
 * Helper Structures
 * ======================================================================= */

/**
 * SeafileConfig:
 *
 * Konfiguration für Seafile-Zugriff
 */
typedef struct {
    gchar *server_url;
    gchar *auth_token;
    gchar *repo_id;
    gchar *base_path;
} SeafileConfig;

/**
 * ProcessedFile:
 *
 * Verarbeitete Datei
 */
typedef struct {
    guchar *data;
    gsize size;
    gint pdf_count;
} ProcessedFile;

/**
 * SeafileFile:
 *
 * Datei-Info aus Seafile
 */
typedef struct {
    gchar *path;
    gsize size;
    gchar *id;
    GDateTime *mtime;  /* Modification Time */
} SeafileFile;

/**
 * OcrLogEntry:
 *
 * Eintrag in .ocr_log.json
 */
typedef struct {
    GDateTime *last_run;
    GHashTable *files;  /* path -> GDateTime* (last processed time) */
} OcrLogEntry;

/**
 * WorkerThreadData:
 *
 * Daten die an Worker-Thread übergeben werden
 */
typedef struct {
    OcrManager *manager;
    OcrJobInfo *job_info;
} WorkerThreadData;

/* =======================================================================
 * Job Management
 * ======================================================================= */

/**
 * ocr_job_info_new:
 *
 * Erstellt neue OcrJobInfo
 */
static OcrJobInfo* ocr_job_info_new(const gchar *repo_id,
                                     const gchar *library_name,
                                     gboolean force_reprocess) {
    OcrJobInfo *info = g_new0(OcrJobInfo, 1);

    g_mutex_init(&info->mutex);
    info->repo_id = g_strdup(repo_id);
    info->library_name = g_strdup(library_name);
    info->running = TRUE;
    info->total_files = 0;
    info->processed_files = 0;
    info->processed_pdfs = 0;
    info->current_file = NULL;
    info->errors = NULL;
    info->start_time = g_date_time_new_now_local();
    info->end_time = NULL;
    info->force_reprocess = force_reprocess;

    return info;
}

/**
 * ocr_job_info_free:
 *
 * Gibt OcrJobInfo frei
 */
static void ocr_job_info_free(OcrJobInfo *info) {
    if (!info) return;

    g_mutex_clear(&info->mutex);
    g_free(info->repo_id);
    g_free(info->library_name);
    g_free(info->current_file);

    if (info->start_time) {
        g_date_time_unref(info->start_time);
    }
    if (info->end_time) {
        g_date_time_unref(info->end_time);
    }

    g_list_free_full(info->errors, (GDestroyNotify)g_free);

    g_free(info);
}

/**
 * ocr_job_get_by_repo_id:
 *
 * Holt Job-Info für Repository (Thread-sicher)
 */
static OcrJobInfo* ocr_job_get_by_repo_id(OcrManager *manager, const gchar *repo_id) {
    g_mutex_lock(&manager->jobs_mutex);
    OcrJobInfo *info = g_hash_table_lookup(manager->active_jobs, repo_id);
    g_mutex_unlock(&manager->jobs_mutex);

    return info;
}

/**
 * ocr_job_add:
 *
 * Fügt Job zur Verwaltung hinzu (Thread-sicher)
 */
static void ocr_job_add(OcrManager *manager, OcrJobInfo *info) {
    g_mutex_lock(&manager->jobs_mutex);
    g_hash_table_insert(manager->active_jobs, g_strdup(info->repo_id), info);
    g_mutex_unlock(&manager->jobs_mutex);
}

/**
 * ocr_job_update_progress:
 *
 * Aktualisiert Fortschritt (Thread-sicher)
 */
static void ocr_job_update_progress(OcrJobInfo *info,
                                    const gchar *current_file) {
    g_mutex_lock(&info->mutex);

    g_free(info->current_file);
    info->current_file = g_strdup(current_file);
    info->processed_files++;

    g_mutex_unlock(&info->mutex);
}

/**
 * ocr_job_add_error:
 *
 * Fügt Fehler hinzu (Thread-sicher)
 */
static void ocr_job_add_error(OcrJobInfo *info,
                              const gchar *filename,
                              const gchar *message) {
    OcrError *error = g_new0(OcrError, 1);
    error->filename = g_strdup(filename);
    error->message = g_strdup(message);
    error->timestamp = g_date_time_new_now_local();

    g_mutex_lock(&info->mutex);
    info->errors = g_list_append(info->errors, error);
    g_mutex_unlock(&info->mutex);
}

/**
 * ocr_job_mark_complete:
 *
 * Markiert Job als abgeschlossen (Thread-sicher)
 */
static void ocr_job_mark_complete(OcrJobInfo *info) {
    g_mutex_lock(&info->mutex);
    info->running = FALSE;
    info->end_time = g_date_time_new_now_local();
    g_mutex_unlock(&info->mutex);
}

/**
 * ocr_cleanup_old_jobs_callback:
 *
 * Täglicher Cleanup: Entfernt abgeschlossene Jobs älter als 7 Tage
 */
static gboolean ocr_cleanup_old_jobs_callback(gpointer user_data) {
    OcrManager *manager = (OcrManager*)user_data;

    LOG_INFO("Running daily OCR job cleanup...");

    g_mutex_lock(&manager->jobs_mutex);

    if (!manager->active_jobs) {
        g_mutex_unlock(&manager->jobs_mutex);
        return G_SOURCE_CONTINUE;
    }

    GDateTime *now = g_date_time_new_now_local();
    GDateTime *cutoff = g_date_time_add_days(now, -manager->retention_days);

    GHashTableIter iter;
    gpointer key, value;
    GList *to_remove = NULL;

    g_hash_table_iter_init(&iter, manager->active_jobs);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        OcrJobInfo *job = value;

        g_mutex_lock(&job->mutex);
        gboolean should_remove = !job->running &&
                                 job->end_time &&
                                 g_date_time_compare(job->end_time, cutoff) < 0;
        g_mutex_unlock(&job->mutex);

        if (should_remove) {
            to_remove = g_list_append(to_remove, g_strdup((gchar*)key));
        }
    }

    guint removed_count = g_list_length(to_remove);

    for (GList *l = to_remove; l != NULL; l = l->next) {
        LOG_INFO("Removing old OCR job: %s", (gchar*)l->data);
        g_hash_table_remove(manager->active_jobs, l->data);
    }

    g_list_free_full(to_remove, g_free);
    g_date_time_unref(cutoff);
    g_date_time_unref(now);

    g_mutex_unlock(&manager->jobs_mutex);

    if (removed_count > 0) {
        LOG_INFO("Daily OCR cleanup finished (removed %u old jobs)", removed_count);
    }

    return G_SOURCE_CONTINUE;
}

/* =======================================================================
 * HTTP Response Helpers
 * ======================================================================= */

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

/* =======================================================================
 * Forward Declarations - Seafile Interaction
 * ======================================================================= */
static gboolean seafile_list_files_recursive(SeafileConfig *config,
                                         SoupSession *session,
                                         const gchar *path,
                                         GList **files,
										 GError** error);

static guchar* seafile_download_file(SeafileConfig *config,
                                     SoupSession *session,
                                     const gchar *file_path,
                                     gsize *out_size,
									 GError** error);

static gboolean seafile_upload_file(SeafileConfig *config,
                                   SoupSession *session,
                                   const gchar *file_path,
                                   guchar *data,
                                   gsize size,
								   GError** error);

static ProcessedFile process_file_for_ocr(const gchar *filename,
                                          guchar *data,
                                          gsize size,
										  OcrJobInfo *job_info,
                                          GError **error);

static OcrLogEntry* ocr_log_read(SeafileConfig *config, SoupSession *session);
static gboolean ocr_log_write(SeafileConfig *config, SoupSession *session,
                              OcrLogEntry *log_entry);
static void ocr_log_entry_free(OcrLogEntry *entry);

static gpointer seafile_ocr_worker_thread(gpointer user_data);

static void send_json_response(SoupServerMessage *msg,
                                guint status_code,
                                const gchar *json_body);
static void send_error_response(SoupServerMessage *msg,
                                 guint status_code,
                                 const gchar *error_message);


/* =======================================================================
 * OCR Log Management (.ocr_log.json im Repo)
 * ======================================================================= */

#define OCR_LOG_FILENAME "/.ocr_log.json"

/**
 * ocr_log_read:
 *
 * Liest .ocr_log.json aus Seafile Repo
 */
static OcrLogEntry* ocr_log_read(SeafileConfig *config, SoupSession *session) {
    gsize data_size = 0;

    guchar *data = seafile_download_file(config, session, OCR_LOG_FILENAME, &data_size, NULL);

    OcrLogEntry *entry = g_new0(OcrLogEntry, 1);
    entry->files = g_hash_table_new_full(g_str_hash, g_str_equal,
                                         g_free, (GDestroyNotify)g_date_time_unref);

    if (!data) {
        /* Log-Datei existiert noch nicht - das ist OK */
        entry->last_run = NULL;
        return entry;
    }

    /* JSON parsen */
    JsonParser *parser = json_parser_new();
    GError *error = NULL;

    if (!json_parser_load_from_data(parser, (const gchar*)data, data_size, &error)) {
        LOG_WARN("Failed to parse OCR log: %s", error->message);
        g_error_free(error);
        g_free(data);
        g_object_unref(parser);
        return entry;
    }

    JsonNode *root = json_parser_get_root(parser);
    JsonObject *obj = json_node_get_object(root);

    /* last_run */
    if (json_object_has_member(obj, "last_run")) {
        const gchar *last_run_str = json_object_get_string_member(obj, "last_run");
        entry->last_run = g_date_time_new_from_iso8601(last_run_str, NULL);
    }

    /* files */
    if (json_object_has_member(obj, "files")) {
        JsonObject *files = json_object_get_object_member(obj, "files");
        GList *members = json_object_get_members(files);

        for (GList *l = members; l != NULL; l = l->next) {
            const gchar *path = l->data;
            const gchar *timestamp_str = json_object_get_string_member(files, path);
            GDateTime *timestamp = g_date_time_new_from_iso8601(timestamp_str, NULL);

            if (timestamp) {
                g_hash_table_insert(entry->files, g_strdup(path), timestamp);
            }
        }

        g_list_free(members);
    }

    g_object_unref(parser);
    g_free(data);

    return entry;
}

/**
 * ocr_log_write:
 *
 * Schreibt .ocr_log.json zurück nach Seafile
 */
static gboolean ocr_log_write(SeafileConfig *config, SoupSession *session,
                              OcrLogEntry *log_entry) {
	GError* error = NULL;

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);

    /* last_run */
    json_builder_set_member_name(builder, "last_run");
    if (log_entry->last_run) {
        gchar *iso8601 = g_date_time_format_iso8601(log_entry->last_run);
        json_builder_add_string_value(builder, iso8601);
        g_free(iso8601);
    } else {
        json_builder_add_null_value(builder);
    }

    /* files */
    json_builder_set_member_name(builder, "files");
    json_builder_begin_object(builder);

    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, log_entry->files);

    while (g_hash_table_iter_next(&iter, &key, &value)) {
        const gchar *path = key;
        GDateTime *timestamp = value;

        gchar *iso8601 = g_date_time_format_iso8601(timestamp);
        json_builder_set_member_name(builder, path);
        json_builder_add_string_value(builder, iso8601);
        g_free(iso8601);
    }

    json_builder_end_object(builder);
    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    json_generator_set_pretty(gen, TRUE);

    gchar *json_data = json_generator_to_data(gen, NULL);
    gsize json_size = strlen(json_data);

    gboolean success = seafile_upload_file(config, session, OCR_LOG_FILENAME,
                                          (guchar*)json_data, json_size, &error);

    g_free(json_data);
    g_object_unref(gen);
    g_object_unref(builder);

    return success;
}

/**
 * ocr_log_entry_free:
 *
 * Gibt OcrLogEntry frei
 */
static void ocr_log_entry_free(OcrLogEntry *entry) {
    if (!entry) return;

    if (entry->last_run) {
        g_date_time_unref(entry->last_run);
    }
    if (entry->files) {
        g_hash_table_destroy(entry->files);
    }

    g_free(entry);
}


/* =======================================================================
 * HTTP Endpoint Handlers
 * ======================================================================= */

/**
 * handle_ocr_start:
 *
 * POST /ocr/start?library_name=XXX&force_reprocess=false
 */
static void handle_ocr_start(SoupServer *soup_server,
                             SoupServerMessage *msg,
                             const char *path,
                             GHashTable *query,
                             gpointer user_data) {
    OcrManager *manager = (OcrManager*)user_data;

    if (strcmp(soup_server_message_get_method(msg), "POST") != 0) {
        send_error_response(msg, SOUP_STATUS_METHOD_NOT_ALLOWED, "Only POST allowed");
        return;
    }

    /* Query-Parameter holen */
    if (!query) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST, "Missing query parameters");
        return;
    }

    const gchar *library_name = g_hash_table_lookup(query, "library_name");
    if (!library_name || strlen(library_name) == 0) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST,
                          "Missing 'library_name' query parameter");
        return;
    }

    const gchar *force_str = g_hash_table_lookup(query, "force_reprocess");
    gboolean force_reprocess = (force_str && g_strcmp0(force_str, "true") == 0);

    /* Library-ID holen */
    GError *error = NULL;
    gchar *repo_id = sond_server_seafile_get_library_id_by_name(manager->server,
                                                                  library_name,
                                                                  &error);

    if (!repo_id) {
        gchar *err_msg = g_strdup_printf("Library not found: %s",
                                        error ? error->message : "unknown");
        send_error_response(msg, SOUP_STATUS_NOT_FOUND, err_msg);
        g_free(err_msg);
        g_clear_error(&error);
        return;
    }

    /* Prüfen ob Job bereits läuft */
    OcrJobInfo *existing_job = ocr_job_get_by_repo_id(manager, repo_id);

    if (existing_job) {
        g_mutex_lock(&existing_job->mutex);
        gboolean is_running = existing_job->running;
        g_mutex_unlock(&existing_job->mutex);

        if (is_running) {
            gchar *err_msg = g_strdup_printf("OCR job already running for library '%s'",
                                           library_name);
            send_error_response(msg, SOUP_STATUS_CONFLICT, err_msg);
            g_free(err_msg);
            g_free(repo_id);
            return;
        }

        /* Alter Job ist fertig - überschreiben */
        g_mutex_lock(&manager->jobs_mutex);
        g_hash_table_remove(manager->active_jobs, repo_id);
        g_mutex_unlock(&manager->jobs_mutex);
    }

    /* Neuen Job erstellen */
    OcrJobInfo *job_info = ocr_job_info_new(repo_id, library_name, force_reprocess);
    ocr_job_add(manager, job_info);

    /* Worker-Thread starten */
    WorkerThreadData *thread_data = g_new0(WorkerThreadData, 1);
    thread_data->manager = manager;
    thread_data->job_info = job_info;

    GThread *thread = g_thread_new("ocr-worker", seafile_ocr_worker_thread, thread_data);
    g_thread_unref(thread);

    LOG_INFO("Started OCR job for library '%s' (repo_id: %s, force: %s)",
           library_name, repo_id, force_reprocess ? "true" : "false");

    /* Response */
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "success");
    json_builder_add_boolean_value(builder, TRUE);
    json_builder_set_member_name(builder, "message");
    json_builder_add_string_value(builder, "OCR job started");
    json_builder_set_member_name(builder, "repo_id");
    json_builder_add_string_value(builder, repo_id);
    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(gen, root);
    gchar *response_json = json_generator_to_data(gen, NULL);

    send_json_response(msg, SOUP_STATUS_OK, response_json);

    g_free(response_json);
    json_node_free(root);
    g_object_unref(gen);
    g_object_unref(builder);
    g_free(repo_id);
}

/**
 * ocr_manager_get_job_by_library_name:
 *
 * Holt Job-Info für eine Library
 */
static OcrJobInfo* ocr_manager_get_job_by_library_name(OcrManager *manager,
                                                  const gchar *library_name,
                                                  GError **error) {
    g_return_val_if_fail(manager != NULL, NULL);
    g_return_val_if_fail(library_name != NULL, NULL);

    /* Library-ID holen */
    gchar *repo_id = sond_server_seafile_get_library_id_by_name(manager->server,
                                                                  library_name,
                                                                  error);
    if (!repo_id) {
        return NULL;
    }

    OcrJobInfo *job = ocr_job_get_by_repo_id(manager, repo_id);
    g_free(repo_id);

    if (!job) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "No OCR job found for library '%s'", library_name);
    }

    return job;
}

/**
 * handle_ocr_status:
 *
 * GET /ocr/status?library_name=XXX
 */
static void handle_ocr_status(SoupServer *soup_server,
                              SoupServerMessage *msg,
                              const char *path,
                              GHashTable *query,
                              gpointer user_data) {
    OcrManager *manager = (OcrManager*)user_data;

    if (strcmp(soup_server_message_get_method(msg), "GET") != 0) {
        send_error_response(msg, SOUP_STATUS_METHOD_NOT_ALLOWED, "Only GET allowed");
        return;
    }

    /* Query-Parameter holen */
    if (!query) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST, "Missing query parameters");
        return;
    }

    const gchar *library_name = g_hash_table_lookup(query, "library_name");
    if (!library_name || strlen(library_name) == 0) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST,
                          "Missing 'library_name' query parameter");
        return;
    }

    /* Job-Info holen */
    GError *error = NULL;
    OcrJobInfo *job_info = ocr_manager_get_job_by_library_name(manager, library_name, &error);

    if (!job_info) {
        gchar *err_msg = g_strdup_printf("No OCR job found: %s",
                                        error ? error->message : "unknown");
        send_error_response(msg, SOUP_STATUS_NOT_FOUND, err_msg);
        g_free(err_msg);
        g_clear_error(&error);
        return;
    }

    /* Status sammeln (Thread-sicher) */
    g_mutex_lock(&job_info->mutex);

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);

    json_builder_set_member_name(builder, "success");
    json_builder_add_boolean_value(builder, TRUE);

    json_builder_set_member_name(builder, "running");
    json_builder_add_boolean_value(builder, job_info->running);

    json_builder_set_member_name(builder, "total_files");
    json_builder_add_int_value(builder, job_info->total_files);

    json_builder_set_member_name(builder, "processed_files");
    json_builder_add_int_value(builder, job_info->processed_files);

    json_builder_set_member_name(builder, "processed_pdfs");
    json_builder_add_int_value(builder, job_info->processed_pdfs);

    if (job_info->current_file) {
        json_builder_set_member_name(builder, "current_file");
        json_builder_add_string_value(builder, job_info->current_file);
    }

    json_builder_set_member_name(builder, "error_count");
    json_builder_add_int_value(builder, g_list_length(job_info->errors));

    /* Start/End Times */
    if (job_info->start_time) {
        gchar *iso8601 = g_date_time_format_iso8601(job_info->start_time);
        json_builder_set_member_name(builder, "start_time");
        json_builder_add_string_value(builder, iso8601);
        g_free(iso8601);
    }

    if (job_info->end_time) {
        gchar *iso8601 = g_date_time_format_iso8601(job_info->end_time);
        json_builder_set_member_name(builder, "end_time");
        json_builder_add_string_value(builder, iso8601);
        g_free(iso8601);
    }

    /* Errors (erste 10) */
    if (job_info->errors) {
        json_builder_set_member_name(builder, "recent_errors");
        json_builder_begin_array(builder);

        guint count = 0;
        for (GList *l = job_info->errors; l != NULL && count < 10; l = l->next, count++) {
            OcrError *err = l->data;

            json_builder_begin_object(builder);
            json_builder_set_member_name(builder, "filename");
            json_builder_add_string_value(builder, err->filename);
            json_builder_set_member_name(builder, "message");
            json_builder_add_string_value(builder, err->message);

            if (err->timestamp) {
                gchar *iso8601 = g_date_time_format_iso8601(err->timestamp);
                json_builder_set_member_name(builder, "timestamp");
                json_builder_add_string_value(builder, iso8601);
                g_free(iso8601);
            }

            json_builder_end_object(builder);
        }

        json_builder_end_array(builder);
    }

    g_mutex_unlock(&job_info->mutex);

    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(gen, root);
    gchar *response_json = json_generator_to_data(gen, NULL);

    send_json_response(msg, SOUP_STATUS_OK, response_json);

    g_free(response_json);
    json_node_free(root);
    g_object_unref(gen);
    g_object_unref(builder);
}

/* =======================================================================
 * OcrManager - Constructor/Destructor
 * ======================================================================= */

/**
 * ocr_manager_register_handlers:
 *
 * Registriert OCR-Endpoints
 */
static void ocr_manager_register_handlers(OcrManager *manager) {
    SoupServer *soup_server = sond_server_get_soup_server(manager->server);

    soup_server_add_handler(soup_server, "/ocr/start",
                           handle_ocr_start,
                           manager, NULL);

    soup_server_add_handler(soup_server, "/ocr/status",
                           handle_ocr_status,
                           manager, NULL);

    LOG_INFO("Registered OCR endpoints");
}

/* =======================================================================
 * Forward Declarations - Internal
 * ======================================================================= */

/**
 * ocr_manager_new:
 *
 * Erstellt einen neuen OcrManager
 */
OcrManager* ocr_manager_new(SondServer *server) {
    OcrManager *manager = g_new0(OcrManager, 1);

    manager->server = server;
    manager->retention_days = 7;  // Standard: 7 Tage

    g_mutex_init(&manager->jobs_mutex);
    manager->active_jobs = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                   g_free,
                                                   (GDestroyNotify)ocr_job_info_free);

    /* Täglicher Cleanup-Timer (alle 24 Stunden) */
    manager->cleanup_timer_id = g_timeout_add_seconds(24 * 60 * 60,
                                                       ocr_cleanup_old_jobs_callback,
                                                       manager);

    /* Handler direkt registrieren */
    ocr_manager_register_handlers(manager);  // NEU HINZUFÜGEN

    LOG_INFO("OcrManager created (retention: %d days)", manager->retention_days);

    return manager;
}

/**
 * ocr_manager_free:
 *
 * Gibt OcrManager frei (wartet auf laufende Jobs)
 */
void ocr_manager_free(OcrManager *manager) {
    if (!manager) return;

    LOG_INFO("OcrManager cleanup started...");

    /* Timer stoppen */
    if (manager->cleanup_timer_id > 0) {
        g_source_remove(manager->cleanup_timer_id);
        manager->cleanup_timer_id = 0;
    }

    g_mutex_lock(&manager->jobs_mutex);

    if (!manager->active_jobs) {
        g_mutex_unlock(&manager->jobs_mutex);
        g_mutex_clear(&manager->jobs_mutex);
        g_free(manager);
        LOG_INFO("OcrManager cleanup: no jobs to clean");
        return;
    }

    /* Prüfe ob Jobs noch laufen */
    GHashTableIter iter;
    gpointer key, value;
    gboolean has_running_jobs = FALSE;

    g_hash_table_iter_init(&iter, manager->active_jobs);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        OcrJobInfo *job = value;
        g_mutex_lock(&job->mutex);
        if (job->running) {
            has_running_jobs = TRUE;
            LOG_WARN("OCR job still running for repo: %s", (gchar*)key);
        }
        g_mutex_unlock(&job->mutex);
    }

    if (has_running_jobs) {
        LOG_WARN("OcrManager cleanup: %d jobs still running - waiting...",
                   g_hash_table_size(manager->active_jobs));

        g_mutex_unlock(&manager->jobs_mutex);

        /* Warte bis alle Jobs fertig sind (mit Timeout) */
        gint timeout_seconds = 300;  // 5 Minuten
        gint waited = 0;

        while (has_running_jobs && waited < timeout_seconds) {
            g_usleep(G_USEC_PER_SEC);  // 1 Sekunde warten
            waited++;

            g_mutex_lock(&manager->jobs_mutex);
            has_running_jobs = FALSE;

            g_hash_table_iter_init(&iter, manager->active_jobs);
            while (g_hash_table_iter_next(&iter, &key, &value)) {
                OcrJobInfo *job = value;
                g_mutex_lock(&job->mutex);
                if (job->running) {
                    has_running_jobs = TRUE;
                }
                g_mutex_unlock(&job->mutex);
            }
            g_mutex_unlock(&manager->jobs_mutex);

            if (waited % 10 == 0 && has_running_jobs) {
                LOG_INFO("Still waiting for OCR jobs... (%d/%d seconds)",
                        waited, timeout_seconds);
            }
        }

        g_mutex_lock(&manager->jobs_mutex);

        if (has_running_jobs) {
            LOG_WARN("OcrManager cleanup timeout reached - forcing cleanup despite running jobs");
        }
    }

    /* Jetzt aufräumen */
    g_hash_table_destroy(manager->active_jobs);
    manager->active_jobs = NULL;

    g_mutex_unlock(&manager->jobs_mutex);
    g_mutex_clear(&manager->jobs_mutex);

    g_free(manager);

    LOG_INFO("OcrManager freed");
}

/* =======================================================================
 * Seafile API Funktionen
 * ======================================================================= */

/**
 * seafile_list_files_recursive:
 *
 * Listet rekursiv alle Dateien in einem Seafile-Pfad
 */
static gboolean seafile_list_files_recursive(SeafileConfig *config,
                                         SoupSession *session,
                                         const gchar *path,
                                         GList **files,
										 GError **error) {
    gchar *encoded_path = g_uri_escape_string(path, "/", FALSE);
    gchar *url = g_strdup_printf("%s/api2/repos/%s/dir/?p=%s",
                                 config->server_url,
                                 config->repo_id,
                                 encoded_path);
    g_free(encoded_path);

    SoupMessage *msg = soup_message_new("GET", url);
    g_free(url);
    if (!msg) {
    	g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
				   "Failed to create SoupMessage for URL: %s", url);
        return FALSE;
    }

    gchar *auth_header = g_strdup_printf("Token %s", config->auth_token);
    soup_message_headers_append(soup_message_get_request_headers(msg),
                               "Authorization", auth_header);
    g_free(auth_header);

    GBytes *response_bytes = soup_session_send_and_read(session, msg, NULL, error);

    if (!response_bytes) {
        g_object_unref(msg);
        return FALSE;
    }

    gsize response_size = 0;
    gconstpointer response_data = g_bytes_get_data(response_bytes, &response_size);

    JsonParser *parser = json_parser_new();
    
    if (!json_parser_load_from_data(parser, response_data, response_size, error)) {
        g_object_unref(parser);
        g_bytes_unref(response_bytes);
        g_object_unref(msg);
        return FALSE;
    }

    JsonNode *root = json_parser_get_root(parser);
    JsonArray *array = json_node_get_array(root);

    guint length = json_array_get_length(array);

    for (guint i = 0; i < length; i++) {
        JsonObject *obj = json_array_get_object_element(array, i);

        const gchar *type = json_object_get_string_member(obj, "type");
        const gchar *name = json_object_get_string_member(obj, "name");

        gchar *full_path;
        if (g_strcmp0(path, "/") == 0) {
            full_path = g_strdup_printf("/%s", name);
        } else {
            full_path = g_strdup_printf("%s/%s", path, name);
        }

        if (g_strcmp0(type, "file") == 0) {
            SeafileFile *file = g_new0(SeafileFile, 1);
            file->path = full_path;
            file->size = json_object_get_int_member(obj, "size");
            file->id = g_strdup(json_object_get_string_member(obj, "id"));

            /* mtime parsen */
            if (json_object_has_member(obj, "mtime")) {
                gint64 mtime_unix = json_object_get_int_member(obj, "mtime");
                file->mtime = g_date_time_new_from_unix_local(mtime_unix);
            } else {
                file->mtime = NULL;
            }

            *files = g_list_append(*files, file);

        } else if (g_strcmp0(type, "dir") == 0) {
        	if (!seafile_list_files_recursive(config, session, full_path, files, error)) {
        		g_free(full_path);
				g_object_unref(parser);
				g_bytes_unref(response_bytes);
				g_object_unref(msg);
				return FALSE;
        	}
            g_free(full_path);
        }
    }

    g_object_unref(parser);
    g_bytes_unref(response_bytes);
    g_object_unref(msg);

    return TRUE;
}

/**
 * seafile_download_file:
 *
 * Lädt eine Datei von Seafile herunter
 */
static guchar* seafile_download_file(SeafileConfig *config,
                                     SoupSession *session,
                                     const gchar *file_path,
                                     gsize *out_size,
									 GError** error) {
    /* 1. Get download link */
    gchar *encoded_path = g_uri_escape_string(file_path, "/", FALSE);
    gchar *url = g_strdup_printf("%s/api2/repos/%s/file/?p=%s",
                                 config->server_url,
                                 config->repo_id,
                                 encoded_path);
    g_free(encoded_path);

    SoupMessage *msg = soup_message_new("GET", url);
    g_free(url);
    if (!msg) {
    	g_set_error(error, G_IO_ERROR, G_IO_ERR, "SoupMessage für Download '%s' "
    			"konnte nicht erstellt werden", file_path);
        return NULL;
    }

    SoupMessageHeaders *headers = soup_message_get_request_headers(msg);
    
    gchar *auth_header = g_strdup_printf("Token %s", config->auth_token);
    soup_message_headers_append(headers, "Authorization", auth_header);
    g_free(auth_header);

    GBytes *link_bytes = soup_session_send_and_read(session, msg, NULL, error);

    if (!link_bytes) {
        g_object_unref(msg);
        return NULL;
    }

    gsize link_size;
    gconstpointer link_data = g_bytes_get_data(link_bytes, &link_size);

    gchar *download_url = g_strndup(link_data, link_size);
    g_strstrip(download_url);
    if (download_url[0] == '"') {
        gchar *tmp = g_strndup(download_url + 1, strlen(download_url) - 2);
        g_free(download_url);
        download_url = tmp;
    }

    g_bytes_unref(link_bytes);
    g_object_unref(msg);
    msg = NULL;

    /* 2. Download actual file */
    msg = soup_message_new("GET", download_url);
    if (!msg) {
    	g_set_error(error, G_IO_ERROR, G_IO_ERR, "SoupMessage für Downloadlink '%s' "
    			"konnte nicht erstellt werden", download_url);
        g_free(download_url);
        return NULL;
    }

    g_free(download_url);

    GBytes *file_bytes = soup_session_send_and_read(session, msg, NULL, error);

	if (!file_bytes) {
        g_object_unref(msg);
        return NULL;
    }

    *out_size = g_bytes_get_size(file_bytes);
    guchar *result = g_malloc(*out_size);
    memcpy(result, g_bytes_get_data(file_bytes, NULL), *out_size);

    g_bytes_unref(file_bytes);
    g_object_unref(msg);

    return result;
}

/**
 * seafile_upload_file:
 *
 * Lädt eine Datei zu Seafile hoch
 */
static gboolean seafile_upload_file(SeafileConfig *config,
                                   SoupSession *session,
                                   const gchar *file_path,
                                   guchar *data,
                                   gsize size,
								   GError** error) {
    /* 1. Get upload link */
    gchar *url = g_strdup_printf("%s/api2/repos/%s/upload-link/?p=/",
                                 config->server_url,
                                 config->repo_id);

    SoupMessage *msg = soup_message_new("GET", url);
    g_free(url);
    if (!msg) {
    	g_set_error(error, G_IO_ERROR, G_IO_ERR, "SoupMessage für Uploadlink "
				"konnte nicht erstellt werden");
		return FALSE;
    }

    gchar *auth_header = g_strdup_printf("Token %s", config->auth_token);
    soup_message_headers_append(soup_message_get_request_headers(msg),
                               "Authorization", auth_header);
    g_free(auth_header);

    GBytes *link_bytes = soup_session_send_and_read(session, msg, NULL, error);
    if (!link_bytes) {
        g_object_unref(msg);
        return FALSE;
    }

    gsize link_size = 0;
    gconstpointer link_data = g_bytes_get_data(link_bytes, &link_size);

    gchar *upload_url = g_strndup(link_data, link_size);
    g_strstrip(upload_url);
    if (upload_url[0] == '"') {
        gchar *tmp = g_strndup(upload_url + 1, strlen(upload_url) - 2);
        g_free(upload_url);
        upload_url = tmp;
    }

    g_bytes_unref(link_bytes);
    g_object_unref(msg);

    /* 2. Upload file - manuell Multipart erstellen */
    gchar *boundary = g_strdup_printf("----WebKitFormBoundary%016" G_GINT64_MODIFIER "x",
    		g_get_monotonic_time());
    GString *body = g_string_new(NULL);

    /* File part */
    g_string_append_printf(body, "--%s\r\n", boundary);
    g_string_append(body, "Content-Disposition: form-data; name=\"file\"; filename=\"");
    g_string_append(body, file_path);
    g_string_append(body, "\"\r\n");
    g_string_append(body, "Content-Type: application/octet-stream\r\n\r\n");
    g_string_append_len(body, (const gchar*)data, size);
    g_string_append(body, "\r\n");

    /* parent_dir part */
    g_string_append_printf(body, "--%s\r\n", boundary);
    g_string_append(body, "Content-Disposition: form-data; name=\"parent_dir\"\r\n\r\n");
    g_string_append(body, "/\r\n");

    /* replace part */
    g_string_append_printf(body, "--%s\r\n", boundary);
    g_string_append(body, "Content-Disposition: form-data; name=\"replace\"\r\n\r\n");
    g_string_append(body, "1\r\n");

    /* End boundary */
    g_string_append_printf(body, "--%s--\r\n", boundary);

    /* Create message */
    msg = soup_message_new("POST", upload_url);
    g_free(upload_url);
    if (!msg) {
    	g_set_error(error, G_IO_ERROR, G_IO_ERR, "SoupMessage für Upload "
				"konnte nicht erstellt werden");
        g_free(boundary);
		return FALSE;
    }

    gchar *content_type = g_strdup_printf("multipart/form-data; boundary=%s", boundary);
    g_free(boundary);

    gchar *body_str = g_string_free(body, FALSE);  /* Get string, free GString */
    GBytes *body_bytes = g_bytes_new_take(body_str, strlen(body_str));

    soup_message_set_request_body_from_bytes(msg, content_type, body_bytes);
    g_bytes_unref(body_bytes);
    g_free(content_type);

    GBytes *response = soup_session_send_and_read(session, msg, NULL, error);
    g_object_unref(msg);

    if (!response)
    	return FALSE;

    g_bytes_unref(response);

    return TRUE;
}

/**
 * seafile_file_free:
 *
 * Gibt SeafileFile frei
 */
static void seafile_file_free(SeafileFile *file) {
    if (!file) return;

    g_free(file->path);
    g_free(file->id);
    if (file->mtime) {
        g_date_time_unref(file->mtime);
    }
    g_free(file);
}

/* =======================================================================
 * OCR Worker Thread
 * ======================================================================= */

/**
 * seafile_ocr_worker_thread:
 *
 * Worker-Thread der die OCR-Verarbeitung durchführt
 */
static gpointer seafile_ocr_worker_thread(gpointer user_data) {
	GError *error = NULL;

    WorkerThreadData *thread_data = (WorkerThreadData*)user_data;
    OcrManager *manager = thread_data->manager;
    OcrJobInfo *job_info = thread_data->job_info;

    g_free(thread_data);

    /* Seafile Config erstellen */
    SeafileConfig config;
    config.server_url = g_strdup(sond_server_get_seafile_url(manager->server));
    config.auth_token = g_strdup(sond_server_get_seafile_token(manager->server));
    config.repo_id = g_strdup(job_info->repo_id);
    config.base_path = g_strdup("/");

    SoupSession *session = soup_session_new();

    /* OCR Log lesen */
    OcrLogEntry *log_entry = ocr_log_read(&config, session);

    /* Liste alle Dateien rekursiv */
    GList *files = NULL;
    if (!seafile_list_files_recursive(&config, session, config.base_path, &files, &error)) {
		gchar *err_msg = g_strdup_printf("Failed to list files in repository: %s",
				error ? error->message : "unknown");
		g_error_free(error);
		ocr_job_add_error(job_info, "/", err_msg);
		g_free(err_msg);

		ocr_log_entry_free(log_entry);
		g_object_unref(session);

		g_free(config.server_url);
		g_free(config.auth_token);
		g_free(config.repo_id);
		g_free(config.base_path);

		ocr_job_mark_complete(job_info);
		return NULL;
    }

    guint file_count = g_list_length(files);

    /* Total files setzen */
    g_mutex_lock(&job_info->mutex);
    job_info->total_files = file_count;
    g_mutex_unlock(&job_info->mutex);

    /* Verarbeite jede Datei */
    guint i = 0;
    for (GList *l = files; l != NULL; l = l->next) {
        SeafileFile *file = (SeafileFile*)l->data;
        i++;

        /* Soll Datei verarbeitet werden? */
        gboolean should_process = job_info->force_reprocess;

        if (!should_process && file->mtime) {
            GDateTime *last_processed = g_hash_table_lookup(log_entry->files, file->path);

            if (!last_processed) {
                /* Nie verarbeitet */
                should_process = TRUE;
            } else {
                /* Vergleiche Timestamps */
                if (g_date_time_compare(file->mtime, last_processed) > 0) {
                    should_process = TRUE;  /* Datei wurde geändert */
                }
            }
        }

        ocr_job_update_progress(job_info, file->path);

        if (!should_process)
            continue;

        /* Download */
        gsize data_size;
        guchar *data = seafile_download_file(&config, session, file->path, &data_size, &error);
        if (!data) {
            gchar *err_msg = g_strdup_printf("Download failed: %s", error->message);
            g_clear_error(&error);
            ocr_job_add_error(job_info, file->path, err_msg);
            g_free(err_msg);
            continue;
        }

        /* Verarbeiten */
        ProcessedFile processed = process_file_for_ocr(file->path, data, data_size,
        		job_info, &error);
        g_free(data);
        if (error) { //nur an error kann man erkennen, ob process_file_for_ocr fehlgeschlagen ist
            ocr_job_add_error(job_info, file->path, error->message);
            g_clear_error(&error);
            continue;
        }

        /* PDF Count aktualisieren */
        g_mutex_lock(&job_info->mutex);
        job_info->processed_pdfs += processed.pdf_count;
        g_mutex_unlock(&job_info->mutex);

        /* Upload */
        if (processed.data) {
            gboolean upload_success = seafile_upload_file(&config, session, file->path,
            		processed.data, processed.size, &error);
            g_free(processed.data);
            if (!upload_success) {
                gchar *err_msg = g_strdup_printf("Upload failed: %s",
                		error ? error->message : "unknown");
                g_clear_error(&error);
                ocr_job_add_error(job_info, file->path, err_msg);
                g_free(err_msg);
                continue;
            }
        }

        /* In Log eintragen, egal ob Leaf oder nicht */
		GDateTime *now = g_date_time_new_now_local();
		g_hash_table_insert(log_entry->files, g_strdup(file->path), now);
    }

    /* Log aktualisieren */
    if (log_entry->last_run) {
        g_date_time_unref(log_entry->last_run);
    }
    log_entry->last_run = g_date_time_new_now_local();

    if (!ocr_log_write(&config, session, log_entry)) {
        LOG_WARN("Failed to write OCR log file");
    }

    /* Cleanup */
    g_list_free_full(files, (GDestroyNotify)seafile_file_free);
    ocr_log_entry_free(log_entry);

    g_object_unref(session);

    g_free(config.server_url);
    g_free(config.auth_token);
    g_free(config.repo_id);
    g_free(config.base_path);

    /* Job als abgeschlossen markieren */
    ocr_job_mark_complete(job_info);

    LOG_INFO("OCR worker finished. Processed: %d/%d files, %d PDFs, %u errors",
           job_info->processed_files,
           job_info->total_files,
           job_info->processed_pdfs,
           g_list_length(job_info->errors));

    return NULL;
}

static void dispatch_buffer(fz_context* ctx, guchar* data, gsize size,
		gchar const* filename, OcrJobInfo* job_info,
		guchar** out_data, gsize* out_size, gint* out_pdf_count,
		GError** error);

static void process_zip_for_ocr(fz_context* ctx, guchar* data, gsize size,
		OcrJobInfo* job_info,
		guchar** out_data, gsize* out_size, gint* out_pdf_count,
		GError** error) {

	return;
}

static void process_gmessage_for_ocr(fz_context* ctx, guchar* data, gsize size,
		OcrJobInfo* job_info,
		guchar** out_data, gsize* out_size, gint* out_pdf_count,
		GError** error) {

	return;
}

typedef struct {
	pdf_document* doc;
	OcrJobInfo* job_info;
	gint* out_pdf_count;
} ProcessPdfData;

static gint process_emb_file(fz_context* ctx, pdf_obj* dict,
		pdf_obj* key, pdf_obj* val, gpointer data,
		GError** error) {
	pdf_obj* EF_F = NULL;
	fz_stream* stream = NULL;
	gchar const* path = NULL;
	fz_buffer* buf = NULL;

	EF_F = pdf_get_EF_F(ctx, val, &path, error);
	if (!EF_F && error && *error)
		return -1;
	else if (!EF_F) {
		g_set_error(error, G_IO_ERROR, G_IO_ERR,
				"No EF/F entry");
		return -1;
	}

	if (!path) {
		g_set_error(error, G_IO_ERROR, G_IO_ERR,
				"No path for embedded file");
		return -1;
	}

	fz_try(ctx)
		stream = pdf_open_stream(ctx, EF_F);
	fz_catch(ctx)
		ERROR_PDF

	fz_try(ctx)
		buf = fz_read_all(ctx, stream, 4096);
	fz_always(ctx)
		fz_drop_stream(ctx, stream);
	fz_catch(ctx)
		ERROR_PDF

	guchar* data_buf = NULL;
	gsize len = 0;
	len = fz_buffer_storage(ctx, buf, &data_buf);

	guchar* data_out = NULL;
	gsize size_out = 0;
	dispatch_buffer(ctx, data_buf, len,
			path, ((ProcessPdfData*)data)->job_info,
			&data_out, &size_out, ((ProcessPdfData*)data)->out_pdf_count,
			error);
	fz_drop_buffer(ctx, buf);

	fz_buffer* buf_new = NULL;
	fz_try(ctx)
		buf_new = fz_new_buffer_from_data(ctx, data_out, size_out);
	fz_catch(ctx) {
		g_free(data_out);
		ERROR_PDF
	}

	fz_try(ctx)
		pdf_update_stream(ctx, ((ProcessPdfData*)data)->doc, EF_F, buf_new, 0);
	fz_always(ctx) {
		fz_drop_buffer(ctx, buf_new);
		g_free(data_out);
	}
	fz_catch(ctx)
		ERROR_PDF

	return 0;
}

static void process_pdf_for_ocr(fz_context* ctx, guchar* data, gsize size,
		OcrJobInfo* job_info,
		guchar** out_data, gsize* out_size, gint* out_pdf_count,
		GError** error) {
	pdf_document* doc = NULL;
	fz_stream* file = NULL;
	fz_buffer* buf = NULL;
	gint rc = 0;

	ProcessPdfData process_data = {doc, job_info, out_pdf_count};

	fz_try(ctx)
		file = fz_open_memory(ctx, data, size);
	fz_catch(ctx) {
		g_set_error(error, g_quark_from_static_string("mupdf"), fz_caught(ctx),
				"Failed to open PDF memory stream: %s",
				fz_caught_message(ctx) ? fz_caught_message(ctx) : "unknown error");
		return;
	}

	doc = pdf_open_document_with_stream(ctx, file);

	//Alle embedded files durchgehen
	rc = pdf_walk_embedded_files(ctx, doc, process_emb_file, &process_data, error);
	if (rc) {
		pdf_drop_document(ctx, doc);
		return;
	}

	//pdf-page-tree OCRen

	*out_pdf_count += 1;

	//Rückgabe-buffer füllen
	buf = pdf_doc_to_buf(ctx, doc, error);
	pdf_drop_document(ctx, doc);
	fz_drop_stream(ctx, file);
	if (!buf)
		return;

	guchar* data_buf = NULL;
	gsize len = 0;
	len = fz_buffer_storage(ctx, buf, &data_buf);

	/* eigene Kopie anlegen */
	*out_data = fz_malloc(ctx, len);
	memcpy(*out_data, data_buf, len);
	*out_size = len;

	/* buffer und doc freigeben */
	fz_drop_buffer(ctx, buf);

	return;
}

static void dispatch_buffer(fz_context* ctx, guchar* data, gsize size,
		gchar const* filename, OcrJobInfo* job_info,
		guchar** out_data, gsize* out_size, gint* out_pdf_count,
		GError** error) {
	gchar* mime_type = NULL;

	mime_type = misc_guess_content_type(data, size, error);
	if (!mime_type)
		return; //Fehler!

	if (!g_strcmp0(mime_type, "application/pdf"))
		process_pdf_for_ocr(ctx, data, size, job_info,
				out_data, out_size, out_pdf_count, error);
	else if (!g_strcmp0(mime_type, "application/zip"))
		process_zip_for_ocr(ctx, data, size, job_info,
				out_data, out_size, out_pdf_count, error);
	else if (!g_strcmp0(mime_type, "message/rfc822"))
		process_gmessage_for_ocr(ctx, data, size, job_info,
				out_data, out_size, out_pdf_count, error);

	return;
}

static ProcessedFile process_file_for_ocr(const gchar *filename,
                                          guchar *data,
                                          gsize size,
										  OcrJobInfo* job_info,
                                          GError **error) {
	ProcessedFile result = { 0 };
	fz_context* ctx = NULL;

	ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
	if (!ctx) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
				"Failed to create MuPDF context");
		return result;
	}

	dispatch_buffer(ctx, data, size, filename, job_info,
			&result.data, &result.size, &result.pdf_count, error);
	fz_drop_context(ctx);

	return result;
}
