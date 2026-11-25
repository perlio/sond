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

#include "sond_server_repo_worker.h"
#include "sond_server_seafile.h"
#include "../sond_log_and_error.h"
#include "../sond_pdf_helper.h"
#include "../sond_ocr.h"
#include "../sond_process_file.h"
#include "../sond_index.h"
#include "../sond_fileparts.h"

#include <glib/gstdio.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <zip.h>
#include <gmime/gmime.h>


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
    GHashTable* files_new;
    GMutex mutex;
    GPtrArray* log_msg;
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
    info->start_time = g_date_time_new_now_local();
    info->end_time = NULL;
    info->force_reprocess = force_reprocess;
    info->ocr_mode = force_reprocess ? SOND_OCR_MODE_FORCE : SOND_OCR_MODE_CHECK;

    return info;
}

static void sond_locked_item_free_server(gpointer p) {
    SondLockedItem *item = p;

    g_free(item->filepart);
    g_free(item);
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
    if (info->sel_files)
        g_ptr_array_unref(info->sel_files);
    if (info->sel_dirs)
        g_ptr_array_unref(info->sel_dirs);
    g_free(info->job_token);
    g_free(info->error_message);
    if (info->locked)
        g_ptr_array_unref(info->locked);

    if (info->start_time) {
        g_date_time_unref(info->start_time);
    }
    if (info->end_time) {
        g_date_time_unref(info->end_time);
    }

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
 * ocr_job_try_start:
 *
 * Prueft unter EINEM gehaltenen jobs_mutex, ob fuer repo_id bereits ein Job
 * laeuft, und fuegt bei Erfolg new_job_info atomar in active_jobs ein -
 * kein Race-Fenster mehr zwischen Check und Insert (vorher: getrennte
 * ocr_job_get_by_repo_id()/ocr_job_add()-Aufrufe mit Luecke dazwischen,
 * in der zwei gleichzeitige Start-Requests fuer dieselbe Library beide den
 * "kein Job laeuft"-Check passieren und sich gegenseitig per
 * g_hash_table_insert()/GDestroyNotify den noch vom jeweils anderen
 * Worker-Thread benutzten OcrJobInfo unter den Fuessen wegziehen konnten).
 *
 * Rueckgabe: FALSE wenn bereits ein Job laeuft (new_job_info wurde NICHT
 * eingefuegt - der Aufrufer muss ihn selbst freigeben), sonst TRUE.
 */
static gboolean ocr_job_try_start(OcrManager *manager, const gchar *repo_id,
                                   OcrJobInfo *new_job_info) {
    g_mutex_lock(&manager->jobs_mutex);

    OcrJobInfo *existing = g_hash_table_lookup(manager->active_jobs, repo_id);
    if (existing) {
        g_mutex_lock(&existing->mutex);
        gboolean is_running = existing->running;
        g_mutex_unlock(&existing->mutex);

        if (is_running) {
            g_mutex_unlock(&manager->jobs_mutex);
            return FALSE;
        }
        /* Nicht (mehr) laufend - wird durch den Insert unten automatisch
         * ersetzt/freigegeben (GDestroyNotify auf dem alten Wert). */
    }

    g_hash_table_insert(manager->active_jobs, g_strdup(repo_id), new_job_info);
    g_mutex_unlock(&manager->jobs_mutex);
    return TRUE;
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
 * ocr_job_mark_complete:
 *
 * Markiert Job als abgeschlossen (Thread-sicher)
 */
static void ocr_job_mark_complete(OcrJobInfo *info) {
    g_mutex_lock(&info->mutex);
    info->running = FALSE;
    info->wctx_cancel = NULL;   //der Verarbeitungskontext des Threads endet mit dem Job
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

static gboolean seafile_file_detail(SeafileConfig *config,
                                    SoupSession *session,
                                    const gchar *file_path,
                                    gboolean *exists,
                                    GDateTime **mtime,
                                    GError **error);

static gpointer seafile_repo_worker_thread(gpointer user_data);

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
 * ocr_log_add_message:
 *
 * Fügt Fehler hinzu (Thread-sicher)
 */
static void ocr_log_add_message(OcrLogEntry* log_entry,
                              gchar const* format,
							  ...) {
	va_list args;
	gchar *message = NULL;

	va_start(args, format);
	message = g_strdup_vprintf(format, args);
	va_end(args);

	LOG_INFO("%s", message);

    g_mutex_lock(&log_entry->mutex);
    g_ptr_array_add(log_entry->log_msg, message);
    g_mutex_unlock(&log_entry->mutex);
}


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
    entry->files_new = g_hash_table_new_full(g_str_hash, g_str_equal,
                                         g_free, (GDestroyNotify)g_date_time_unref);
    g_mutex_init(&entry->mutex);
    entry->log_msg = g_ptr_array_new_with_free_func((GDestroyNotify) g_free);


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
    g_hash_table_iter_init(&iter, log_entry->files_new);

    while (g_hash_table_iter_next(&iter, &key, &value)) {
        const gchar *path = key;
        GDateTime *timestamp = value;

        gchar *iso8601 = g_date_time_format_iso8601(timestamp);
        json_builder_set_member_name(builder, path);
        json_builder_add_string_value(builder, iso8601);
        g_free(iso8601);
    }

    json_builder_end_object(builder); /* end "files" */

	/* log_msg */
	json_builder_set_member_name(builder, "log_msg");
	json_builder_begin_array(builder);

	if (log_entry->log_msg) {
		for (guint i = 0; i < log_entry->log_msg->len; i++) {
			const gchar *msg = g_ptr_array_index(log_entry->log_msg, i);
			json_builder_add_string_value(builder, msg ? msg : "");
		}
	}

	json_builder_end_array(builder);
	json_builder_end_object(builder); /* end root */

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

    if (entry->files_new) {
        g_hash_table_destroy(entry->files_new);
    }

    g_mutex_clear(&entry->mutex);
    g_ptr_array_unref(entry->log_msg);

    g_free(entry);
}


/* =======================================================================
 * HTTP Endpoint Handlers
 * ======================================================================= */

/* Pfad aus der Auswahl des Clients auf einen Seafile-Pfad abbilden: relativ zur
 * Bibliothek, mit führendem '/', ohne abschließendes '/'. NULL bei leerem oder
 * unzulässigem Pfad (".." oder "." als Bestandteil, Backslash). */
static gchar* ocr_normalize_selection_path(const gchar *in) {
    gchar *trimmed = NULL;
    gchar **parts = NULL;
    gchar *result = NULL;
    gboolean ok = TRUE;

    if (!in)
        return NULL;

    trimmed = g_strstrip(g_strdup(in));
    g_strdelimit(trimmed, "\\", '/');
    parts = g_strsplit(trimmed, "/", -1);
    for (gint i = 0; parts[i]; i++)
        if (!g_strcmp0(parts[i], "..") || !g_strcmp0(parts[i], "."))
            ok = FALSE;

    if (ok) {
        GString *path = g_string_new(NULL);

        for (gint i = 0; parts[i]; i++)
            if (parts[i][0])
                g_string_append_printf(path, "/%s", parts[i]);
        if (path->len > 0)
            result = g_string_free(path, FALSE);
        else
            g_string_free(path, TRUE);
    }

    g_strfreev(parts);
    g_free(trimmed);

    return result;
}

/* Wie ocr_normalize_selection_path(), aber ein Teil in einem Container bleibt erhalten:
 * "x.zip//a.pdf" ("//" trennt die Schichten) wird zu "/x.zip//a.pdf". Der Pfad vor dem "//" wird
 * normalisiert, der Teil dahinter bleibt wie er ist (Namen im Container, keine Dateipfade). */
static gchar* ocr_normalize_selection_filepart(const gchar *in) {
    gchar *trimmed = NULL;
    gchar const *sep = NULL;
    gchar *result = NULL;

    if (!in)
        return NULL;

    trimmed = g_strstrip(g_strdup(in));
    g_strdelimit(trimmed, "\\", '/');

    //ein "//" gleich am Anfang trennt nichts ab
    sep = (trimmed[0] && trimmed[1]) ? strstr(trimmed + 1, "//") : NULL;
    if (sep && sep[2]) {
        gchar *root_raw = g_strndup(trimmed, (gsize) (sep - trimmed));
        gchar *root = ocr_normalize_selection_path(root_raw);

        if (root)
            result = g_strconcat(root, "//", sep + 2, NULL);
        g_free(root_raw);
        g_free(root);
    } else {
        result = ocr_normalize_selection_path(trimmed);
    }
    g_free(trimmed);

    return result;
}

/* Liest "files" und "dirs" (Arrays von Strings) aus dem JSON-Body. Rückgabe: -1 bei
 * ungültigem Inhalt (error_text gesetzt), 0 ohne Auswahl, 1 mit Auswahl. */
static gint ocr_parse_selection(SoupServerMessage *msg, GPtrArray **sel_files,
        GPtrArray **sel_dirs, const gchar **error_text) {
    SoupMessageBody *body = soup_server_message_get_request_body(msg);
    JsonParser *parser = NULL;
    JsonObject *obj = NULL;
    const gchar *names[] = { "files", "dirs" };
    GPtrArray **targets[] = { sel_files, sel_dirs };

    *sel_files = NULL;
    *sel_dirs = NULL;

    if (!body || !body->data || body->length == 0)
        return 0;

    parser = json_parser_new();
    if (!json_parser_load_from_data(parser, body->data, (gssize) body->length, NULL) ||
            !JSON_NODE_HOLDS_OBJECT(json_parser_get_root(parser))) {
        *error_text = "Invalid JSON body";
        g_object_unref(parser);

        return -1;
    }
    obj = json_node_get_object(json_parser_get_root(parser));

    for (guint k = 0; k < G_N_ELEMENTS(names); k++) {
        JsonArray *arr = NULL;

        if (!json_object_has_member(obj, names[k]))
            continue;
        if (!JSON_NODE_HOLDS_ARRAY(json_object_get_member(obj, names[k]))) {
            *error_text = "'files' and 'dirs' must be arrays";
            goto fail;
        }

        arr = json_object_get_array_member(obj, names[k]);
        for (guint i = 0; i < json_array_get_length(arr); i++) {
            gchar *norm = NULL;

            if (!JSON_NODE_HOLDS_VALUE(json_array_get_element(arr, i)) ||
                    !(norm = (k == 0 ? ocr_normalize_selection_filepart :
                            ocr_normalize_selection_path)(
                            json_array_get_string_element(arr, i)))) {
                *error_text = "Invalid path in selection";
                goto fail;
            }
            if (!*targets[k])
                *targets[k] = g_ptr_array_new_with_free_func(g_free);
            g_ptr_array_add(*targets[k], norm);
        }
    }

    g_object_unref(parser);
    if (!*sel_files && !*sel_dirs) {
        *error_text = "Empty selection";

        return -1;
    }

    return 1;

fail:
    g_object_unref(parser);
    g_clear_pointer(sel_files, g_ptr_array_unref);
    g_clear_pointer(sel_dirs, g_ptr_array_unref);

    return -1;
}

/**
 * handle_ocr_start:
 *
 * POST /ocr/start?library_name=XXX&force_reprocess=false[&ocr_mode=none|check|force]
 *
 * ocr_mode: none = kein OCR (nur indizieren), check = nur Seiten ohne versteckten
 * Text, force = immer OCR (vorhandener Text wird ersetzt). Ohne Angabe gilt wie
 * bisher check, bei force_reprocess=true force.
 * Optionaler JSON-Body {"files":[...],"dirs":[...]} (Pfade relativ zur Bibliothek)
 * begrenzt den Job auf diese Dateien und Verzeichnisse (rekursiv).
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

    /* Auth-Check */
    const gchar *username = NULL;
    if (!sond_server_check_authorization(manager->server, msg, &username)) {
        return;  /* 401 wurde bereits gesendet */
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

    gint ocr_mode = force_reprocess ? SOND_OCR_MODE_FORCE : SOND_OCR_MODE_CHECK;
    const gchar *mode_str = g_hash_table_lookup(query, "ocr_mode");
    if (mode_str) {
        if (!g_strcmp0(mode_str, "none"))
            ocr_mode = SOND_OCR_MODE_NONE;
        else if (!g_strcmp0(mode_str, "check"))
            ocr_mode = SOND_OCR_MODE_CHECK;
        else if (!g_strcmp0(mode_str, "force"))
            ocr_mode = SOND_OCR_MODE_FORCE;
        else {
            send_error_response(msg, SOUP_STATUS_BAD_REQUEST,
                              "Invalid 'ocr_mode' (none, check, force)");
            return;
        }
    }

    //Kennung des Clients (undurchsichtig, nur zum Wiedererkennen in /ocr/status)
    const gchar *token_str = g_hash_table_lookup(query, "job_token");
    if (token_str) {
        gsize token_len = strlen(token_str);

        if (token_len == 0 || token_len > 64 ||
                strspn(token_str, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
                        "0123456789_-") != token_len) {
            send_error_response(msg, SOUP_STATUS_BAD_REQUEST, "Invalid 'job_token'");
            return;
        }
    }

    GPtrArray *sel_files = NULL;
    GPtrArray *sel_dirs = NULL;
    const gchar *sel_error = NULL;
    if (ocr_parse_selection(msg, &sel_files, &sel_dirs, &sel_error) < 0) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST, sel_error);
        return;
    }

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
        if (sel_files)
            g_ptr_array_unref(sel_files);
        if (sel_dirs)
            g_ptr_array_unref(sel_dirs);
        return;
    }

    /* Neuen Job erstellen und atomar (Check-und-Insert in einem Mutex-
     * kritischen Abschnitt) in die Verwaltung einhaengen - verhindert das
     * Race, bei dem zwei gleichzeitige Start-Requests fuer dieselbe Library
     * sich sonst gegenseitig den noch benutzten OcrJobInfo wegziehen konnten. */
    OcrJobInfo *job_info = ocr_job_info_new(repo_id, library_name, force_reprocess);
    job_info->ocr_mode = ocr_mode;
    job_info->job_token = g_strdup(token_str);
    job_info->sel_files = sel_files;  /* gehen in den Besitz des Jobs über */
    job_info->sel_dirs = sel_dirs;

    if (!ocr_job_try_start(manager, repo_id, job_info)) {
        gchar *err_msg = g_strdup_printf("OCR job already running for library '%s'",
                                       library_name);
        send_error_response(msg, SOUP_STATUS_CONFLICT, err_msg);
        g_free(err_msg);
        g_free(repo_id);
        ocr_job_info_free(job_info);  /* wurde nicht eingefuegt - selbst freigeben */
        return;
    }

    /* Worker-Thread starten */
    WorkerThreadData *thread_data = g_new0(WorkerThreadData, 1);
    thread_data->manager = manager;
    thread_data->job_info = job_info;

    GThread *thread = g_thread_new("ocr-worker", seafile_repo_worker_thread, thread_data);
    g_thread_unref(thread);

    LOG_INFO("Started OCR job for library '%s' (repo_id: %s, force: %s, ocr_mode: %d, "
           "selection: %u files, %u dirs)",
           library_name, repo_id, force_reprocess ? "true" : "false", ocr_mode,
           job_info->sel_files ? job_info->sel_files->len : 0,
           job_info->sel_dirs ? job_info->sel_dirs->len : 0);

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

    /* Auth-Check */
    const gchar *username = NULL;
    if (!sond_server_check_authorization(manager->server, msg, &username)) {
        return;  /* 401 wurde bereits gesendet */
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

    if (job_info->job_token) {
        json_builder_set_member_name(builder, "job_token");
        json_builder_add_string_value(builder, job_info->job_token);
    }

    json_builder_set_member_name(builder, "cancelled");
    json_builder_add_boolean_value(builder, g_atomic_int_get(&job_info->cancel) != 0);

    if (job_info->error_message) {
        json_builder_set_member_name(builder, "error");
        json_builder_add_string_value(builder, job_info->error_message);
    }

    /* Teile, die wegen eines Passwortschutzes übersprungen wurden: der Client entsperrt sie
     * (die Passwörter bleiben dort) und startet einen Nachlauf nur für diese Dateien */
    if (job_info->locked && job_info->locked->len > 0) {
        json_builder_set_member_name(builder, "locked");
        json_builder_begin_array(builder);
        for (guint i = 0; i < job_info->locked->len; i++) {
            SondLockedItem *item = g_ptr_array_index(job_info->locked, i);

            json_builder_begin_object(builder);
            json_builder_set_member_name(builder, "filepart");
            json_builder_add_string_value(builder, item->filepart);
            json_builder_set_member_name(builder, "kind");
            json_builder_add_string_value(builder,
                    item->kind == SOND_LOCKED_PDF ? "pdf" : "zip_entry");
            json_builder_end_object(builder);
        }
        json_builder_end_array(builder);
    }

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
 * handle_ocr_cancel:
 *
 * POST /ocr/cancel?library_name=XXX
 *
 * Fordert den Abbruch des laufenden Jobs an. Der Job bricht in der Datei ab (nach der Seite
 * bzw. dem Teil, die gerade bearbeitet werden), das bis dahin Erreichte bleibt erhalten.
 */
static void handle_ocr_cancel(SoupServer *soup_server,
                              SoupServerMessage *msg,
                              const char *path,
                              GHashTable *query,
                              gpointer user_data) {
    OcrManager *manager = (OcrManager*)user_data;
    const gchar *username = NULL;

    if (strcmp(soup_server_message_get_method(msg), "POST") != 0) {
        send_error_response(msg, SOUP_STATUS_METHOD_NOT_ALLOWED, "Only POST allowed");
        return;
    }

    if (!sond_server_check_authorization(manager->server, msg, &username))
        return;

    const gchar *library_name = query ? g_hash_table_lookup(query, "library_name") : NULL;
    if (!library_name || strlen(library_name) == 0) {
        send_error_response(msg, SOUP_STATUS_BAD_REQUEST,
                          "Missing 'library_name' query parameter");
        return;
    }

    GError *error = NULL;
    OcrJobInfo *job_info = ocr_manager_get_job_by_library_name(manager, library_name, &error);

    if (!job_info) {
        send_error_response(msg, SOUP_STATUS_NOT_FOUND, "No OCR job found");
        g_clear_error(&error);
        return;
    }

    g_mutex_lock(&job_info->mutex);
    gboolean running = job_info->running;
    g_mutex_unlock(&job_info->mutex);

    if (!running) {
        send_error_response(msg, SOUP_STATUS_CONFLICT, "OCR job is not running");
        return;
    }

    g_atomic_int_set(&job_info->cancel, 1);
    //auch die laufende Datei (Seiten, Container, OCR) bricht ab
    g_mutex_lock(&job_info->mutex);
    if (job_info->wctx_cancel)
        g_atomic_int_set(job_info->wctx_cancel, 1);
    g_mutex_unlock(&job_info->mutex);
    LOG_INFO("Cancel requested for OCR job of library '%s'", library_name);
    send_json_response(msg, SOUP_STATUS_OK, "{\"success\":true,\"message\":\"Cancel requested\"}");
}

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

    soup_server_add_handler(soup_server, "/ocr/cancel",
                           handle_ocr_cancel,
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
    if (!msg) {
    	g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
				   "Failed to create SoupMessage for URL: %s", url);
        g_free(url);
        return FALSE;
    }
    g_free(url);

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
    	g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "SoupMessage für Download '%s' "
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
    	g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "SoupMessage für Downloadlink '%s' "
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
 * seafile_file_detail:
 *
 * Fragt Dasein und Änderungszeit einer Datei ab (Seafile "file detail"). TRUE, wenn die
 * Abfrage geklappt hat; *exists sagt dann, ob es die Datei gibt. FALSE bei einem Fehler
 * (Netz, Seafile), dann ist nichts über die Datei bekannt. mtime (optional) bekommt eine
 * Referenz oder NULL, wenn Seafile keine Zeit liefert.
 */
static gboolean seafile_file_detail(SeafileConfig *config,
                                    SoupSession *session,
                                    const gchar *file_path,
                                    gboolean *exists,
                                    GDateTime **mtime,
                                    GError **error) {
    gchar *encoded_path = g_uri_escape_string(file_path, "/", FALSE);
    gchar *url = g_strdup_printf("%s/api2/repos/%s/file/detail/?p=%s",
                                 config->server_url, config->repo_id, encoded_path);
    SoupMessage *msg = NULL;
    GBytes *bytes = NULL;
    guint status = 0;
    gboolean ok = FALSE;

    g_free(encoded_path);
    *exists = FALSE;
    if (mtime)
        *mtime = NULL;

    msg = soup_message_new("GET", url);
    g_free(url);
    if (!msg) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "SoupMessage für file/detail '%s' konnte nicht erstellt werden", file_path);
        return FALSE;
    }

    gchar *auth_header = g_strdup_printf("Token %s", config->auth_token);
    soup_message_headers_append(soup_message_get_request_headers(msg),
                                "Authorization", auth_header);
    g_free(auth_header);

    bytes = soup_session_send_and_read(session, msg, NULL, error);
    if (!bytes) {
        g_object_unref(msg);
        return FALSE;
    }

    status = soup_message_get_status(msg);
    if (status == 404) {
        ok = TRUE; //es gibt die Datei nicht
    } else if (status == 200) {
        gsize size = 0;
        gconstpointer data = g_bytes_get_data(bytes, &size);
        JsonParser *parser = json_parser_new();

        if (json_parser_load_from_data(parser, data, (gssize) size, NULL) &&
                JSON_NODE_HOLDS_OBJECT(json_parser_get_root(parser))) {
            JsonObject *obj = json_node_get_object(json_parser_get_root(parser));

            *exists = TRUE;
            ok = TRUE;
            if (mtime && json_object_has_member(obj, "mtime")) {
                JsonNode *node = json_object_get_member(obj, "mtime");

                if (JSON_NODE_HOLDS_VALUE(node)) {
                    if (json_node_get_value_type(node) == G_TYPE_STRING)
                        *mtime = g_date_time_new_from_iso8601(
                                json_node_get_string(node), NULL);
                    else
                        *mtime = g_date_time_new_from_unix_local(json_node_get_int(node));
                }
            }
        } else {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "Ungültige Antwort von file/detail für '%s'", file_path);
        }
        g_object_unref(parser);
    } else {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "file/detail für '%s' lieferte HTTP %u", file_path, status);
    }

    g_bytes_unref(bytes);
    g_object_unref(msg);

    return ok;
}

/* Kontext für die Verzeichnisauflistung beim Zusammenfassen der Abdeckung */
typedef struct {
    SeafileConfig *config;
    SoupSession *session;
} ListDirCtx;

/**
 * seafile_list_dir_names:
 *
 * Namen der Einträge (Dateien und Verzeichnisse) eines Verzeichnisses (nicht
 * rekursiv), als Auflistung für sond_index_ctx_coverage_try_collapse_listing().
 * dir ist projektrelativ ohne führenden "/".
 */
static GPtrArray* seafile_list_dir_names(gchar const *dir, gpointer user_data,
                                         GError **error) {
    ListDirCtx *lctx = user_data;
    gchar *seafile_path = g_strdup_printf("/%s", dir);
    gchar *encoded_path = g_uri_escape_string(seafile_path, "/", FALSE);
    gchar *url = g_strdup_printf("%s/api2/repos/%s/dir/?p=%s", lctx->config->server_url,
                                 lctx->config->repo_id, encoded_path);
    SoupMessage *msg = soup_message_new("GET", url);
    GBytes *bytes = NULL;
    GPtrArray *names = NULL;

    g_free(seafile_path);
    g_free(encoded_path);
    g_free(url);
    if (!msg) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "SoupMessage für Auflistung '%s' konnte nicht erstellt werden", dir);
        return NULL;
    }

    gchar *auth_header = g_strdup_printf("Token %s", lctx->config->auth_token);
    soup_message_headers_append(soup_message_get_request_headers(msg),
                                "Authorization", auth_header);
    g_free(auth_header);

    bytes = soup_session_send_and_read(lctx->session, msg, NULL, error);
    if (bytes && soup_message_get_status(msg) == 200) {
        gsize size = 0;
        gconstpointer data = g_bytes_get_data(bytes, &size);
        JsonParser *parser = json_parser_new();

        if (json_parser_load_from_data(parser, data, (gssize) size, NULL) &&
                JSON_NODE_HOLDS_ARRAY(json_parser_get_root(parser))) {
            JsonArray *array = json_node_get_array(json_parser_get_root(parser));

            names = g_ptr_array_new_with_free_func(g_free);
            for (guint i = 0; i < json_array_get_length(array); i++) {
                JsonObject *obj = json_array_get_object_element(array, i);

                if (obj && json_object_has_member(obj, "name"))
                    g_ptr_array_add(names,
                            g_strdup(json_object_get_string_member(obj, "name")));
            }
        }
        g_object_unref(parser);
    } else if (bytes) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "Auflistung '%s' lieferte HTTP %u", dir, soup_message_get_status(msg));
    }

    if (bytes)
        g_bytes_unref(bytes);
    g_object_unref(msg);

    return names;
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
    	g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "SoupMessage für Uploadlink "
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
    const gchar *basename = strrchr(file_path, '/');
    const gchar *filename = basename ? basename + 1 : file_path;
    g_string_append_printf(body, "--%s\r\n", boundary);
    g_string_append(body, "Content-Disposition: form-data; name=\"file\"; filename=\"");
    g_string_append(body, filename);
    g_string_append(body, "\"\r\n");
    g_string_append(body, "Content-Type: application/octet-stream\r\n\r\n");
    g_string_append_len(body, (const gchar*)data, size);
    g_string_append(body, "\r\n");

    /* parent_dir part */
    const gchar *last_slash = strrchr(file_path, '/');
    gchar *parent_dir;
    if (last_slash && last_slash != file_path)
        parent_dir = g_strndup(file_path, last_slash - file_path);
    else
        parent_dir = g_strdup("/");
    g_string_append_printf(body, "--%s\r\n", boundary);
    g_string_append(body, "Content-Disposition: form-data; name=\"parent_dir\"\r\n\r\n");
    g_string_append(body, parent_dir);
    g_free(parent_dir);
    g_string_append(body, "\r\n");

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
    	g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "SoupMessage für Upload "
				"konnte nicht erstellt werden");
        g_free(boundary);
		return FALSE;
    }

    gchar *content_type = g_strdup_printf("multipart/form-data; boundary=%s", boundary);
    g_free(boundary);

    gsize body_len = body->len;
    gchar *body_str = g_string_free(body, FALSE);  /* Get string, free GString */
    GBytes *body_bytes = g_bytes_new_take(body_str, body_len);

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
#define INDEX_DB_FILENAME           "/.sond_index.db"

/* -----------------------------------------------------------------------
 * Teile in Containern (x.zip//a.pdf)
 * ----------------------------------------------------------------------- */

/* Die Dateiteile (SondFilePart) führen eine Klassen-weite Liste der geöffneten Dateien, die nicht
 * thread-sicher ist; der Abschnitt, der sie benutzt, läuft nur in einem Job zugleich. */
static GMutex fileparts_mutex;

/* Verzeichnis samt Inhalt löschen. Nur für das temporäre Verzeichnis eines Jobs. */
static void remove_tree(gchar const *path) {
    GDir *dir = g_dir_open(path, 0, NULL);
    gchar const *name = NULL;

    if (dir) {
        while ((name = g_dir_read_name(dir))) {
            gchar *child = g_build_filename(path, name, NULL);

            if (g_file_test(child, G_FILE_TEST_IS_DIR) && !g_file_test(child, G_FILE_TEST_IS_SYMLINK))
                remove_tree(child);
            else
                g_remove(child);
            g_free(child);
        }
        g_dir_close(dir);
    }
    g_rmdir(path);
}

/* Die Sperrliste des Verarbeitungskontexts (passwortgeschützte Teile) in den Job übernehmen und
 * leeren: sond_process_fileparts() beginnt jeden Lauf mit einer leeren Liste. */
static void job_take_locked(SondProcessFileCtx *wctx, OcrJobInfo *job_info) {
    if (!wctx->locked || wctx->locked->len == 0)
        return;

    g_mutex_lock(&job_info->mutex);
    if (!job_info->locked)
        job_info->locked = g_ptr_array_new_with_free_func(sond_locked_item_free_server);
    for (guint i = 0; i < wctx->locked->len; i++) {
        SondLockedItem *src = g_ptr_array_index(wctx->locked, i);
        SondLockedItem *copy = g_new0(SondLockedItem, 1);

        copy->filepart = g_strdup(src->filepart);
        copy->kind = src->kind;
        g_ptr_array_add(job_info->locked, copy);
    }
    g_mutex_unlock(&job_info->mutex);
    g_ptr_array_set_size(wctx->locked, 0);
}

/* Teile (parts: Fileparts ohne führendes '/', alle in der Wurzeldatei file) indizieren und OCRen:
 * die Wurzeldatei wird in das temporäre Verzeichnis des Jobs geladen (Pfade wie in der Bibliothek),
 * sond_process_fileparts() arbeitet auf ihr wie zond im Projektverzeichnis, und eine geänderte
 * Wurzeldatei (OCR in einem Teil schreibt den Container neu) wird wieder hochgeladen. Auch nach
 * einem Abbruch: das Teilergebnis passt zum Index. Die Wurzeldatei gilt nicht als verarbeitet (kein
 * Eintrag im OCR-Log), denn nur ein Teil wurde bearbeitet. */
static void process_root_parts(SeafileConfig *config, SoupSession *session,
        SondProcessFileCtx *wctx, OcrLogEntry *log_entry, OcrJobInfo *job_info,
        gchar **tmp_root, SeafileFile const *file, GPtrArray const *parts) {
    GError *error = NULL;
    gchar *local = NULL;
    gchar *dir = NULL;
    guchar *data = NULL;
    gsize size = 0;
    gchar *sum_before = NULL;
    GHashTable *ht = NULL;

    if (!*tmp_root) {
        *tmp_root = g_dir_make_tmp("sond-job-XXXXXX", &error);
        if (!*tmp_root) {
            ocr_log_add_message(log_entry, "Temporäres Verzeichnis nicht anlegbar: %s",
                    error ? error->message : "unknown");
            g_clear_error(&error);
            return;
        }
    }

    local = g_build_filename(*tmp_root, file->path + 1, NULL);
    dir = g_path_get_dirname(local);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);

    data = seafile_download_file(config, session, file->path, &size, &error);
    if (!data) {
        ocr_log_add_message(log_entry, "Download '%s' failed: %s", file->path,
                error ? error->message : "unknown");
        g_clear_error(&error);
        g_free(local);
        return;
    }
    sum_before = g_compute_checksum_for_data(G_CHECKSUM_SHA256, data, size);
    if (!g_file_set_contents(local, (gchar const*) data, (gssize) size, &error)) {
        ocr_log_add_message(log_entry, "'%s' nicht ablegbar: %s", file->path,
                error ? error->message : "unknown");
        g_clear_error(&error);
        g_free(data);
        g_free(sum_before);
        g_free(local);
        return;
    }
    g_free(data);

    ht = g_hash_table_new_full(NULL, NULL, g_object_unref, sond_page_range_free);

    g_mutex_lock(&fileparts_mutex);
    sond_file_part_set_thread_root(*tmp_root);

    for (guint i = 0; i < parts->len; i++) {
        gchar const *part = g_ptr_array_index((GPtrArray*) parts, i);
        SondFilePart *sfp = sond_file_part_from_filepart_leaf(part, &error);

        if (!sfp) {
            ocr_log_add_message(log_entry, "Teil '%s' nicht öffenbar: %s", part,
                    error ? error->message : "unknown");
            g_clear_error(&error);
            continue;
        }
        g_hash_table_insert(ht, sfp, NULL);   //die Tabelle übernimmt die Referenz
    }

    if (g_hash_table_size(ht) > 0) {
        job_take_locked(wctx, job_info);   //aus einem vorigen Lauf
        sond_process_fileparts(wctx, ht);
        job_take_locked(wctx, job_info);
    }

    g_hash_table_destroy(ht);   //gibt die Dateiteile frei, ehe das Verzeichnis verschwindet
    sond_file_part_set_thread_root(NULL);
    g_mutex_unlock(&fileparts_mutex);

    //geändert (OCR hat den Container neu geschrieben): die Wurzeldatei zurückladen
    if (g_file_get_contents(local, (gchar**) &data, &size, &error)) {
        gchar *sum_after = g_compute_checksum_for_data(G_CHECKSUM_SHA256, data, size);

        if (g_strcmp0(sum_before, sum_after)) {
            if (!seafile_upload_file(config, session, file->path, data, size, &error)) {
                ocr_log_add_message(log_entry, "Upload '%s' failed: %s", file->path,
                        error ? error->message : "unknown");
                g_clear_error(&error);
            }
        }
        g_free(sum_after);
        g_free(data);
    } else {
        ocr_log_add_message(log_entry, "'%s' nicht lesbar: %s", file->path,
                error ? error->message : "unknown");
        g_clear_error(&error);
    }

    g_remove(local);   //Platz freigeben; das Verzeichnis folgt am Ende des Jobs
    g_free(sum_before);
    g_free(local);
}

static gpointer seafile_repo_worker_thread(gpointer user_data) {
	GError *error = NULL;
	fz_context* ctx = NULL;
	gint global_progress = 0;

    WorkerThreadData *thread_data = (WorkerThreadData*)user_data;
    OcrManager *manager = thread_data->manager;
    OcrJobInfo *job_info = thread_data->job_info;

    g_free(thread_data);

    ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!ctx) {
    	LOG_ERROR("Failed to init context");

    	ocr_job_mark_complete(job_info);
    	return NULL;
    }

    /* Seafile Config erstellen */
    SeafileConfig config;
    config.server_url = g_strdup(sond_server_get_seafile_url(manager->server));
    config.auth_token = g_strdup(sond_server_get_seafile_token(manager->server));
    config.repo_id = g_strdup(job_info->repo_id);
    config.base_path = g_strdup("/");

    SoupSession *session = soup_session_new();

    /* OCR Log lesen */
    OcrLogEntry *log_entry = ocr_log_read(&config, session);

    SondOcrPool* ocr_pool = sond_ocr_pool_new("/usr/share/tesseract-ocr/5/tessdata/",
    		"deu", 8, &job_info->cancel, &global_progress, &error);
    if (!ocr_pool) {
		LOG_ERROR("Failed to create threadpool: %s", error->message);
		g_error_free(error);
		ocr_log_entry_free(log_entry);
		fz_drop_context(ctx);
		ocr_job_mark_complete(job_info);
		return NULL;
    }

    /* Index-Kontext: Arbeitsstand ist die Index-DB in Seafile. Sie wird zu Beginn
     * geladen, damit alles, was dort schon steht (auch aus zond), erhalten bleibt;
     * hochgeladen wird später der erweiterte Stand. Ohne vorhandene DB beginnt der Job
     * mit einer leeren. Ist die Abfrage selbst nicht möglich, bricht der Job ab: sonst
     * würde eine leere DB den vorhandenen Index überschreiben. */
#define INDEX_UPLOAD_INTERVAL 50  /* DB alle N Dateien hochladen */
    gchar *db_path = g_strdup_printf("%s/.sond_index_%s.db",
    		g_get_tmp_dir(), job_info->repo_id);
    gchar *db_journal = g_strconcat(db_path, "-journal", NULL);
    gboolean db_exists = FALSE;

    if (!seafile_file_detail(&config, session, INDEX_DB_FILENAME, &db_exists, NULL, &error)) {
        gchar *msg_text = g_strdup_printf("Index-DB in Seafile nicht abfragbar: %s",
                error ? error->message : "unknown");

        LOG_ERROR("%s", msg_text);
        g_mutex_lock(&job_info->mutex);
        job_info->error_message = msg_text;
        g_mutex_unlock(&job_info->mutex);
        g_clear_error(&error);
        g_free(db_path);
        g_free(db_journal);
        ocr_log_entry_free(log_entry);
        fz_drop_context(ctx);
        sond_ocr_pool_free(ocr_pool);
        g_object_unref(session);
        g_free(config.server_url);
        g_free(config.auth_token);
        g_free(config.repo_id);
        g_free(config.base_path);
        ocr_job_mark_complete(job_info);
        return NULL;
    }

    g_remove(db_journal);
    g_free(db_journal);
    g_remove(db_path); //eine Kopie aus einem früheren Job ist nicht maßgeblich
    if (db_exists) {
        gsize db_size = 0;
        guchar *db_data = seafile_download_file(&config, session, INDEX_DB_FILENAME,
                &db_size, &error);

        if (!db_data || !g_file_set_contents(db_path, (gchar const*) db_data,
                (gssize) db_size, &error)) {
            gchar *msg_text = g_strdup_printf("Index-DB konnte nicht geladen werden: %s",
                    error ? error->message : "unknown");

            LOG_ERROR("%s", msg_text);
            g_mutex_lock(&job_info->mutex);
            job_info->error_message = msg_text;
            g_mutex_unlock(&job_info->mutex);
            g_clear_error(&error);
            g_free(db_data);
            g_free(db_path);
            ocr_log_entry_free(log_entry);
            fz_drop_context(ctx);
            sond_ocr_pool_free(ocr_pool);
            g_object_unref(session);
            g_free(config.server_url);
            g_free(config.auth_token);
            g_free(config.repo_id);
            g_free(config.base_path);
            ocr_job_mark_complete(job_info);
            return NULL;
        }
        g_free(db_data);
    } else {
        ocr_log_add_message(log_entry, "Keine Index-DB in der Bibliothek, beginne mit leerer DB");
    }

    SondIndexCtx *index_ctx = sond_index_ctx_new(db_path, NULL /*kein Embedding vorerst*/,
    		0, 0, &error);
    if (!index_ctx) {
		LOG_WARN("Failed to create index context: %s - indexing disabled",
				error ? error->message : "unknown");
		g_clear_error(&error);
    }
    g_free(db_path);

    /* Designated initializers, da SondProcessFileCtx zusätzliche Felder
     * (cancel, progress, ocr_mode) hat, die hier zuvor stillschweigend
     * falsch belegt wurden (positionelle Initialisierung ohne diese Felder).
     * ocr_mode (vom Client gewählt): NONE = kein OCR, nur indizieren (ohne
     * Pool), CHECK = Seiten mit vorhandenem versteckten Text überspringen,
     * FORCE = vorhandenen Text entfernen und neu OCRen. */
    SondProcessFileCtx wctx = {
    		.ctx = ctx,
    		.cancel = 0,
    		.progress = 0,
    		.log_func = (void (*)(void*, gchar const*, ...)) ocr_log_add_message,
    		.log_func_data = (gpointer) log_entry,
    		.ocr_pool = (job_info->ocr_mode == SOND_OCR_MODE_NONE) ? NULL : ocr_pool,
    		.index_ctx = index_ctx,
    		.ocr_mode = job_info->ocr_mode,
    };

    /* Abbruch auch innerhalb einer Datei: /ocr/cancel setzt wctx.cancel über diesen Zeiger. Ein
     * Abbruch, der schon vor der Anmeldung kam, gilt sofort. */
    g_mutex_lock(&job_info->mutex);
    job_info->wctx_cancel = &wctx.cancel;
    if (g_atomic_int_get(&job_info->cancel))
        g_atomic_int_set(&wctx.cancel, 1);
    g_mutex_unlock(&job_info->mutex);

    /* Teile in Containern: Wurzeldatei -> Fileparts; whole: Dateien, die ganz gewählt sind (auch über
     * ein Verzeichnis), dort gilt die ganze Datei */
    GHashTable *parts_map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
            (GDestroyNotify) g_ptr_array_unref);
    GHashTable *whole = g_hash_table_new(g_str_hash, g_str_equal);
    gchar *tmp_root = NULL;   /* temporäres Verzeichnis für Teile in Containern */

    /* Liste alle Dateien rekursiv */
    GList *files = NULL;
    gboolean selection = (job_info->sel_files || job_info->sel_dirs);

    if (selection) {
        /* Nur die gewählten Verzeichnisse (rekursiv) und Dateien; eine Datei, die
         * in einem gewählten Verzeichnis liegt, kommt nur einmal vor. */
        GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);

        for (guint k = 0; job_info->sel_dirs && k < job_info->sel_dirs->len; k++) {
            GList *sub = NULL;
            gchar const *dir = g_ptr_array_index(job_info->sel_dirs, k);

            if (!seafile_list_files_recursive(&config, session, dir, &sub, &error)) {
                ocr_log_add_message(log_entry, "Verzeichnis '%s' nicht lesbar: %s",
                        dir, error ? error->message : "unknown");
                g_clear_error(&error);
                continue;
            }
            for (GList *l = sub; l; l = l->next) {
                SeafileFile *f = l->data;

                if (g_hash_table_add(seen, f->path)) {
                    g_hash_table_add(whole, f->path);
                    files = g_list_append(files, f);
                } else {
                    seafile_file_free(f);
                }
            }
            g_list_free(sub);
        }

        for (guint k = 0; job_info->sel_files && k < job_info->sel_files->len; k++) {
            gchar const *entry = g_ptr_array_index(job_info->sel_files, k);
            //ein Teil in einem Container ("/x.zip//a.pdf"): die Wurzeldatei wird geladen
            gchar const *sep = strstr(entry + 1, "//");
            g_autofree gchar *root_path = sep ? g_strndup(entry, (gsize) (sep - entry)) : NULL;
            gchar const *path = root_path ? root_path : entry;
            SeafileFile *f = NULL;

            gboolean exists = FALSE;
            GDateTime *mtime = NULL;

            if (sep) {
                GPtrArray *arr = g_hash_table_lookup(parts_map, path);

                if (!arr) {
                    arr = g_ptr_array_new_with_free_func(g_free);
                    g_hash_table_insert(parts_map, g_strdup(path), arr);
                }
                g_ptr_array_add(arr, g_strdup(entry + 1));   //ohne führendes '/'
            } else {
                g_hash_table_add(whole, (gpointer) path);
            }

            if (g_hash_table_contains(seen, path))
                continue;

            //Änderungszeit holen: gleiche Prüfung wie bei ganzen Verzeichnissen
            if (!seafile_file_detail(&config, session, path, &exists, &mtime, &error)) {
                ocr_log_add_message(log_entry, "Datei '%s' nicht abfragbar: %s",
                        path, error ? error->message : "unknown");
                g_clear_error(&error);
                continue;
            }
            if (!exists) {
                ocr_log_add_message(log_entry, "Datei '%s' nicht gefunden", path);
                continue;
            }

            f = g_new0(SeafileFile, 1);
            f->path = g_strdup(path);
            f->mtime = mtime;
            g_hash_table_add(seen, f->path);
            files = g_list_append(files, f);
        }
        g_hash_table_destroy(seen);
    } else if (!seafile_list_files_recursive(&config, session, config.base_path, &files, &error)) {
		LOG_ERROR("Failed to list files in repository '%s': %s",
				config.repo_id, error->message);
		g_error_free(error);

		ocr_log_entry_free(log_entry);
		fz_drop_context(ctx);
		g_object_unref(session);

		g_free(config.server_url);
		g_free(config.auth_token);
		g_free(config.repo_id);
		g_free(config.base_path);

		sond_ocr_pool_free(ocr_pool);

		ocr_job_mark_complete(job_info);
		return NULL;
    }

    guint file_count = g_list_length(files);

    /* Total files setzen */
    g_mutex_lock(&job_info->mutex);
    job_info->total_files = file_count;
    g_mutex_unlock(&job_info->mutex);

    /* Verzeichnis -> ein darin verarbeiteter Schlüssel, von dem aus zusammengefasst wird */
    GHashTable *collapse_groups = g_hash_table_new_full(g_str_hash, g_str_equal,
            g_free, g_free);

    /* Verarbeite jede Datei */
    guint i = 0;
    for (GList *l = files; l != NULL; l = l->next) {
        SeafileFile *file = (SeafileFile*)l->data;
        i++;

        if (g_atomic_int_get(&job_info->cancel)) {
            ocr_log_add_message(log_entry, "Job abgebrochen");
            break;
        }

        /* Soll Datei verarbeitet werden? Der alte Index einer Datei wird nur dann
         * verworfen, wenn sie sich seit der letzten Verarbeitung geändert hat oder
         * ausdrücklich alles neu verarbeitet werden soll. Sonst überspringt
         * sond_process_file() Seiten, die schon ausreichend indiziert sind, von selbst
         * (auch solche, die zond indiziert hat und die in keinem Log stehen). */
        gboolean should_process = job_info->force_reprocess;
        gboolean needs_clear = job_info->force_reprocess;

        if (!should_process && file->mtime) {
            GDateTime *last_processed = g_hash_table_lookup(log_entry->files, file->path);

            if (!last_processed) {
                /* Nie verarbeitet */
                should_process = TRUE;
            } else if (g_date_time_compare(file->mtime, last_processed) > 0) {
                /* Datei wurde geändert */
                should_process = TRUE;
                needs_clear = TRUE;
            }
        }

        ocr_job_update_progress(job_info, file->path);

        /* nur Teile dieser Datei gewählt (Container): sie werden eigens verarbeitet */
        {
            GPtrArray *parts = g_hash_table_lookup(parts_map, file->path);

            if (parts && !g_hash_table_contains(whole, file->path)) {
                process_root_parts(&config, session, &wctx, log_entry, job_info, &tmp_root,
                        file, parts);
                if (g_atomic_int_get(&job_info->cancel)) {
                    ocr_log_add_message(log_entry, "Job abgebrochen in '%s'", file->path);
                    break;
                }
                continue;
            }
        }

        if (!should_process)
            continue;

        /* Download */
        gsize data_size;
        guchar *data = seafile_download_file(&config, session, file->path, &data_size, &error);
        if (!data) {
            ocr_log_add_message(log_entry, "Download '%s' failed: %s",
            		file->path, error->message);
            g_clear_error(&error);
            continue;
        }

        /* Verarbeiten */
        ProcessedFile result = { 0 };

        /* sond_process_file() macht selbst kein pauschales clear_file mehr
         * (das würde die seitenweise "schon ausreichend indiziert"-Prüfung
         * für unveränderte Dateien unterlaufen). Hier ist aber bekannt,
         * dass die Datei laut mtime-Vergleich tatsächlich geändert wurde
         * (should_process==TRUE) - also explizit vorher den alten Index
         * für diese Datei verwerfen, damit veralteter Inhalt nicht stehen
         * bleibt bzw. fälschlich als "schon erledigt" übersprungen wird. */
        /* Der Index kennt Dateien unter ihrem projektrelativen Pfad ohne führenden
         * "/" (wie zond), Seafile liefert ihn mit. */
        gchar const *index_key = (file->path[0] == '/') ? file->path + 1 : file->path;

        if (wctx.index_ctx && needs_clear) {
            GError *clear_error = NULL;
            if (!sond_index_ctx_clear_file(wctx.index_ctx, index_key, &clear_error)) {
                ocr_log_add_message(log_entry, "clear_file '%s' failed: %s",
                        file->path, clear_error ? clear_error->message : "unknown");
                g_clear_error(&clear_error);
            }
        }

		sond_process_file(&wctx, data, data_size, index_key,
        				&result.data, &result.size, &result.pdf_count, NULL);
        /* Bei einem Abbruch in dieser Datei ist sie nur teilweise bearbeitet */
        gboolean abgebrochen = g_atomic_int_get(&job_info->cancel) != 0;

        /* Ordner zusammenfassen: einmal je Verzeichnis nach dem Lauf vormerken */
        if (index_ctx && sond_index_ctx_coverage_get(index_ctx, index_key) >= wctx.ocr_mode) {
            gchar const *slash = strrchr(index_key, '/');

            if (slash) {
                gchar *group = g_strndup(index_key, (gsize) (slash - index_key));

                if (!g_hash_table_contains(collapse_groups, group))
                    g_hash_table_insert(collapse_groups, group, g_strdup(index_key));
                else
                    g_free(group);
            }
        }
        g_free(data);

        /* PDF Count aktualisieren */
        g_mutex_lock(&job_info->mutex);
        job_info->processed_pdfs += result.pdf_count;
        g_mutex_unlock(&job_info->mutex);

        /* Upload verarbeitete Datei */
        if (result.data) {
            gboolean upload_success = seafile_upload_file(&config, session, file->path,
            		result.data, result.size, &error);
            g_free(result.data);
            if (!upload_success) {
                ocr_log_add_message(log_entry, "Upload '%s' failed: %s",
                		file->path, error->message);
                g_clear_error(&error);
                continue;
            }
        }

        /* Abgebrochen: das Teilergebnis ist hochgeladen (es passt zum Index, der die bearbeiteten
         * Seiten kennt), die Datei gilt aber nicht als verarbeitet und wird beim nächsten Lauf
         * fortgesetzt */
        if (abgebrochen) {
            ocr_log_add_message(log_entry, "Job abgebrochen in '%s'", file->path);
            break;
        }

        /* In Log eintragen */
		GDateTime *now = g_date_time_new_now_local();
		g_hash_table_insert(log_entry->files_new, g_strdup(file->path), now);

        /* Periodischer Index-Upload alle N Dateien */
        if (index_ctx && (i % INDEX_UPLOAD_INTERVAL == 0)) {
            gsize db_size = 0;
            guchar *db_data = NULL;
            GError *upload_err = NULL;

            if (g_file_get_contents(index_ctx->db_path,
            			(gchar**)&db_data, &db_size, &upload_err)) {
                seafile_upload_file(&config, session, INDEX_DB_FILENAME,
                		db_data, db_size, &upload_err);
                g_free(db_data);
            }
            g_clear_error(&upload_err);
        }
    }

    if (tmp_root) {
        remove_tree(tmp_root);
        g_free(tmp_root);
        tmp_root = NULL;
    }
    g_hash_table_destroy(parts_map);
    g_hash_table_destroy(whole);

    /* der Verarbeitungskontext gilt nur bis hier: /ocr/cancel darf nicht mehr darauf zeigen */
    g_mutex_lock(&job_info->mutex);
    job_info->wctx_cancel = NULL;
    g_mutex_unlock(&job_info->mutex);

    /* Übersprungene, passwortgeschützte Teile für den Client festhalten */
    if (wctx.locked && wctx.locked->len > 0) {
        g_mutex_lock(&job_info->mutex);
        if (!job_info->locked)
            job_info->locked = g_ptr_array_new_with_free_func(sond_locked_item_free_server);
        for (guint i = 0; i < wctx.locked->len; i++) {
            SondLockedItem *src = g_ptr_array_index(wctx.locked, i);
            SondLockedItem *copy = g_new0(SondLockedItem, 1);

            copy->filepart = g_strdup(src->filepart);
            copy->kind = src->kind;
            g_ptr_array_add(job_info->locked, copy);
        }
        g_mutex_unlock(&job_info->mutex);
        ocr_log_add_message(log_entry, "%u passwortgeschützte Teile übersprungen",
                wctx.locked->len);
    }

    /* Ordner zusammenfassen (Auflistung über Seafile statt Dateisystem) */
    if (index_ctx) {
        ListDirCtx list_ctx = { &config, session };
        GHashTableIter iter;
        gpointer key = NULL;
        gpointer value = NULL;

        g_hash_table_iter_init(&iter, collapse_groups);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            GError *collapse_error = NULL;

            if (!sond_index_ctx_coverage_try_collapse_listing(index_ctx,
                    (gchar const*) value, seafile_list_dir_names, &list_ctx,
                    &collapse_error)) {
                ocr_log_add_message(log_entry, "coverage_try_collapse '%s': %s",
                        (gchar const*) value,
                        collapse_error ? collapse_error->message : "unknown");
                g_clear_error(&collapse_error);
            }
        }
    }
    g_hash_table_destroy(collapse_groups);

    /* Log aktualisieren */
    if (log_entry->last_run) {
        g_date_time_unref(log_entry->last_run);
    }
    log_entry->last_run = g_date_time_new_now_local();

    if (!ocr_log_write(&config, session, log_entry)) {
        LOG_WARN("Failed to write OCR log file");
    }

    /* Abschließender Index-Upload */
    if (index_ctx) {
        gsize db_size = 0;
        guchar *db_data = NULL;
        GError *upload_err = NULL;

        if (g_file_get_contents(index_ctx->db_path,
        			(gchar**)&db_data, &db_size, &upload_err)) {
            if (!seafile_upload_file(&config, session, INDEX_DB_FILENAME,
            		db_data, db_size, &upload_err))
                LOG_WARN("Final index upload failed: %s",
                		upload_err ? upload_err->message : "unknown");
            g_free(db_data);
        }
        g_clear_error(&upload_err);
        sond_index_ctx_free(index_ctx);
    }

    /* Job als abgeschlossen markieren */
    ocr_job_mark_complete(job_info);
    fz_drop_context(ctx);
    sond_ocr_pool_free(ocr_pool);

    /* Cleanup */
    g_list_free_full(files, (GDestroyNotify)seafile_file_free);
    ocr_log_entry_free(log_entry);

    g_free(config.server_url);
    g_free(config.auth_token);
    g_free(config.repo_id);
    g_free(config.base_path);

    g_object_unref(session);

    return NULL;
}

