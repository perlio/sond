/*
 sond (sond_server_ocr.h) - Akten, Beweisstücke, Unterlagen
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

/**
 * @file sond_server_ocr.h
 * @brief OCR Endpoint für Seafile Repository Verarbeitung
 */

#ifndef SOND_SERVER_OCR_H
#define SOND_SERVER_OCR_H

#include <glib.h>
#include "sond_server.h"

G_BEGIN_DECLS

/**
 * OcrError:
 * @filename: Dateiname wo Fehler auftrat
 * @message: Fehlermeldung
 * @timestamp: Zeitpunkt des Fehlers
 *
 * Einzelner OCR-Fehler während Verarbeitung
 */
typedef struct {
    gchar *filename;
    gchar *message;
    GDateTime *timestamp;
} OcrError;

/**
 * OcrJobInfo:
 * @mutex: Mutex für Thread-sichere Updates
 * @repo_id: Repository ID
 * @library_name: Library Name
 * @running: Läuft der Job gerade?
 * @total_files: Gesamtzahl Dateien im Repo
 * @processed_files: Anzahl verarbeiteter Dateien
 * @processed_pdfs: Anzahl verarbeiteter PDF-Seiten
 * @current_file: Aktuell verarbeitete Datei
 * @errors: Liste von OcrError*
 * @start_time: Startzeitpunkt
 * @end_time: Endzeitpunkt (NULL wenn running)
 * @force_reprocess: Alle Dateien neu verarbeiten?
 *
 * Information über einen laufenden oder abgeschlossenen OCR-Job
 */
typedef struct {
    GMutex mutex;
    gchar *repo_id;
    gchar *library_name;
    gboolean running;
    gint total_files;
    gint processed_files;
    gint processed_pdfs;
    gchar *current_file;
    GList *errors;  // Liste von OcrError*
    GDateTime *start_time;
    GDateTime *end_time;
    gboolean force_reprocess;
} OcrJobInfo;

/**
 * OcrManager:
 *
 * Manager für OCR-Jobs
 */
typedef struct _OcrManager OcrManager;

struct _OcrManager {
    GHashTable *active_jobs;    /* repo_id (string) -> OcrJobInfo* */
    GMutex jobs_mutex;
    guint cleanup_timer_id;
    gint retention_days;        /* Wie lange Jobs behalten (Tage) */
    SondServer *server;         /* Referenz zum Server */
};

/**
 * ocr_manager_new:
 * @server: SondServer Instanz
 *
 * Erstellt einen neuen OcrManager.
 *
 * Returns: (transfer full): Neuer OcrManager. Muss mit ocr_manager_free() freigegeben werden.
 */
OcrManager* ocr_manager_new(SondServer *server);

/**
 * ocr_manager_free:
 * @manager: OcrManager Instanz
 *
 * Gibt OcrManager frei und wartet auf laufende Jobs.
 */
void ocr_manager_free(OcrManager *manager);


G_END_DECLS

#endif /* SOND_SERVER_OCR_H */
