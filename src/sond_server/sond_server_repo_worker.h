/*
 sond (sond_server_repo_worker.h) - Akten, Beweisstücke, Unterlagen
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

#ifndef SOND_SERVER_REPO_WORKER_H
#define SOND_SERVER_REPO_WORKER_H

#include <glib.h>
#include "sond_server.h"
#include "sond_server_index.h"
#include "../sond_ocr.h"

G_BEGIN_DECLS

/**
 * SondWorkerCtx:
 *
 * Übergeordnete Struktur, die OCR-Pool und Index-Kontext zusammenfasst.
 * Wird als einziger Kontext-Parameter durch dispatch_buffer und alle
 * process_*-Funktionen durchgeschleift.
 * Jedes Feld kann NULL sein — dann wird der jeweilige Schritt übersprungen.
 */
typedef struct {
    SondOcrPool  *ocr_pool;   /* NULL → keine OCR         */
    SondIndexCtx *index_ctx;  /* NULL → keine Indizierung */
} SondWorkerCtx;

/**
 * OcrJobInfo:
 *
 * Information über einen laufenden oder abgeschlossenen Repo-Worker-Job.
 */
typedef struct {
    GMutex     mutex;
    gchar     *repo_id;
    gchar     *library_name;
    gboolean   running;
    gint       total_files;
    gint       processed_files;
    gint       processed_pdfs;
    gchar     *current_file;
    GDateTime *start_time;
    GDateTime *end_time;
    gboolean   force_reprocess;
} OcrJobInfo;

/**
 * OcrManager:
 *
 * Manager für Repo-Worker-Jobs (HTTP-Endpoint + Job-Verwaltung).
 */
typedef struct _OcrManager OcrManager;

struct _OcrManager {
    GHashTable *active_jobs;   /* repo_id → OcrJobInfo* */
    GMutex      jobs_mutex;
    guint       cleanup_timer_id;
    gint        retention_days;
    SondServer *server;
};

OcrManager* ocr_manager_new(SondServer *server);
void        ocr_manager_free(OcrManager *manager);

G_END_DECLS

#endif /* SOND_SERVER_REPO_WORKER_H */
