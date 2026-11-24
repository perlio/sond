/*
 sond (sond_seadrive_core.h) - Akten, Beweisstücke, Unterlagen
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

/**
 * @file sond_seadrive_core.h
 * @brief GTK-freier Kern der SeaDrive-Anbindung (Cloud-Files-API)
 *
 * Pin-Zustand lesen/setzen, Prüfung auf SeaDrive-Pfade, Hydrierung (synchron).
 * Nutzbar von GTK3- und GTK4-Programmen. Die Anbindung an den Dateibaum
 * (Watcher, Badges, Fortschrittsdialoge, Menüs) bleibt in sond_seadrive.c.
 *
 * Nur unter Windows (und nicht im Viewer) mit Funktion; sond_seadrive.h
 * stellt für alle anderen Fälle Stubs bereit.
 */

#ifndef SOND_SEADRIVE_CORE_H_INCLUDED
#define SOND_SEADRIVE_CORE_H_INCLUDED

#include <glib.h>

G_BEGIN_DECLS

/* Pin state values (mirror CF_PIN_STATE_* from cfapi.h) */
#define STVFM_PIN_STATE_UNSPECIFIED  0   /* "Always available" deactivated  */
#define STVFM_PIN_STATE_PINNED       1   /* "Always available" activated    */
#define STVFM_PIN_STATE_UNPINNED     2   /* clear cache                     */
#define STVFM_PIN_STATE_EXCLUDED     3
#define STVFM_PIN_STATE_INHERIT      4

/* Anzeige-Zustände (für Badges im Dateibaum); GTK-frei, auf allen Systemen verfügbar */

/* SeaDrive-Cloud-Status, per Cairo als einfacher, voll gefüllter Farbkreis
 * gezeichnet (wie SondIndexStatus) statt über Icon-Theme-Namen
 * ("view-refresh", "process-stop", "emblem-default") oder feinere Formen
 * wie Haken/Ring - beides war bei den winzigen Overlay-Größen kaum
 * unterscheidbar/erkennbar. Der Normalfall (lokal vorhanden, nicht
 * gepinnt) bleibt bewusst ohne Icon, um nicht unnötig visuelles Rauschen
 * zu erzeugen: Violett = nicht lokal und (aktuell) auch nicht angefordert,
 * Orange = gepinnt, aber noch nicht heruntergeladen ("wird geladen, sobald
 * online"), Grün = lokal vorhanden UND dauerhaft gehalten (gepinnt).
 *
 * Prioritätsreihenfolge (s. sond_treeviewfm_render_file_icon()):
 * 1. gepinnt UND nicht hydriert -> PENDING (Orange) - explizit angefordert,
 *    Download folgt.
 * 2. nicht hydriert, nicht gepinnt -> OFFLINE (Violett) - einfach nicht
 *    lokal, keine Anforderung.
 * 3. hydriert UND gepinnt -> PINNED (Grün).
 * 4. sonst (hydriert, nicht gepinnt) -> NONE - der unmarkierte
 *    Normalzustand, keine Garantie, aber auch keine Aktion nötig.
 */
typedef enum {
	SOND_SEADRIVE_BADGE_NONE = 0,
	SOND_SEADRIVE_BADGE_OFFLINE,  /* nicht lokal, nicht angefordert */
	SOND_SEADRIVE_BADGE_PENDING,  /* gepinnt, noch nicht heruntergeladen */
	SOND_SEADRIVE_BADGE_PINNED    /* lokal vorhanden UND dauerhaft gehalten */
} SondSeadriveBadge;

/* Rekursiver SeaDrive-Hydrierungsstatus für einen ganzen Verzeichnis-
 * Teilbaum (analog SondIndexStatus, aber für den Cloud-Status statt für
 * Indizierung) - s. sond_treeviewfm_seadrive_get_dir_status(). Ersetzt für
 * Verzeichnis-Einträge das simple Attribut-Badge (das nur den Ordner
 * selbst betrifft, was bei SeaDrive praktisch nie gesetzt ist) durch eine
 * Aussage über den gesamten Teilbaum. Bewusst dieselbe Grün/Violett/kein-
 * Badge-Bedeutung wie beim Datei-Badge (SondSeadriveBadge), damit der
 * Nutzer nicht zwei verschiedene Farbschemata lernen muss - Grau ist der
 * einzige zusätzliche, ordner-spezifische Zustand (kann bei einer
 * einzelnen Datei nicht auftreten):
 * - NONE: Teilbaum leer/nicht gescannt, ODER alle Dateien hydriert, aber
 *   nicht alle gepinnt (= dieselbe Bedeutung wie beim Datei-Badge: kein
 *   Grund für ein Icon).
 * - FULL_OFFLINE (Violett): alle Dateien im Teilbaum nicht hydriert.
 * - FULL_HYDRATED_PINNED (Grün): alle Dateien hydriert UND alle gepinnt.
 * - MIXED (Grau): weder komplett hydriert noch komplett offline -
 *   uneinheitlicher Teilbaum, hat also KEINEN gemeinsamen Nenner mit
 *   einer der anderen drei Bedeutungen.
 */
typedef enum {
	SOND_SEADRIVE_DIR_STATUS_NONE = 0,
	SOND_SEADRIVE_DIR_STATUS_FULL_OFFLINE,
	SOND_SEADRIVE_DIR_STATUS_FULL_HYDRATED_PINNED,
	SOND_SEADRIVE_DIR_STATUS_MIXED
} SondSeadriveDirStatus;

#if defined(_WIN32)

/* TRUE, wenn full_path in einer Cloud-Sync-Wurzel (SeaDrive, OneDrive o. ä.) liegt */
gboolean sond_seadrive_is_seadrive_path(const gchar *full_path);

/* Aktueller Pin-Zustand (STVFM_PIN_STATE_*); UNSPECIFIED, wenn cldapi.dll fehlt
 * oder die Datei keine Cloud-Datei ist */
guint sond_seadrive_get_pin_state(const gchar *full_path);

/* Pin-Zustand einer Datei oder eines Verzeichnisses setzen; mit recurse auch
 * für alles darunter (CF_SET_PIN_FLAG_RECURSE). */
gboolean sond_seadrive_set_pin_state(const gchar *full_path, guint pin_state,
        gboolean recurse, GError **error);

/* Sammelmeldung für gescheiterte set_pin_state()-Aufrufe: merkt einen Fehlschlag
 * in failed (GPtrArray mit g_free). Die Anzeige (Dialog) ist Sache der Oberfläche. */
void sond_seadrive_pin_note_failure(GPtrArray *failed, const gchar *full_path,
        GError *error);

/* Schneller Vorab-Check (nur Dateiattribute): ist full_path ein noch nicht
 * lokal vorhandener Platzhalter? */
gboolean sond_seadrive_needs_hydration(const gchar *full_path);

/* Stößt den Download eines Platzhalters an (synchron, kann bei großen Dateien
 * lange blockieren); TRUE auch, wenn die Datei schon lokal ist. */
gboolean sond_seadrive_hydrate(const gchar *full_path, GError **error);

/* Hydrierung im Hintergrund. Reiht den Download von full_path in einem eigenen
 * Thread (use_pool: in einem Pool mit wenigen Arbeitern, für viele Dateien) ein und
 * kehrt sofort zurück. TRUE, wenn neu eingereiht; FALSE, wenn für die Datei schon ein
 * Download läuft oder das Einreihen scheiterte. Bei einem Fehlschlag (nicht bei Abbruch
 * durch den Nutzer) ruft der Hauptthread failed(full_path, Meldung, user_data) auf
 * (darf NULL sein). destroy(user_data) (darf NULL sein) wird in jedem Fall genau einmal
 * aufgerufen: im Hauptthread nach dem Download, bei FALSE sofort. */
typedef void (*SondSeadriveHydrateFailedFunc)(const gchar *full_path,
        const gchar *message, gpointer user_data);

gboolean sond_seadrive_hydrate_start(const gchar *full_path, gboolean use_pool,
        SondSeadriveHydrateFailedFunc failed, gpointer user_data,
        GDestroyNotify destroy);

/* Läuft für full_path gerade ein Hintergrund-Download? */
gboolean sond_seadrive_is_hydrating(const gchar *full_path);

/* Versucht (bestmöglich, ohne Erfolgsgarantie), einen laufenden Download abzubrechen. */
void sond_seadrive_hydrate_cancel(const gchar *full_path);

/* Lade-Auftrag: lädt alle nicht geladenen Dateien aus full_paths (Pool ab acht Dateien)
 * und hält den Zustand für eine Fortschrittsanzeige. NULL, wenn nichts zu laden ist.
 * sond_seadrive_load_job_poll() aktualisiert den Zustand (z.B. alle 300 ms im Timer);
 * die Einträge sind nur lesbar. Ab mehr als 20 Dateien (is_aggregate) fragt poll nur
 * wenige laufende Dateien nach dem Fortschritt, die Anzeige sollte dann eine Sammelzeile
 * zeigen. cancel bricht alle noch laufenden Downloads ab; abgebrochene Dateien gelten
 * als nicht geladen. take_failed liefert die Namen aller nicht geladenen Dateien
 * (GPtrArray mit g_free). */
typedef struct _SondSeadriveLoadJob SondSeadriveLoadJob;

typedef struct {
    gchar   *full_path;
    guint64  file_size;
    guint64  on_disk;  /* zuletzt ermittelte, lokal vorhandene Bytes */
    gboolean done;     /* Download beendet (Erfolg oder nicht) */
    gboolean ok;       /* Datei ist danach vollständig lokal */
} SondSeadriveLoadEntry;

SondSeadriveLoadJob* sond_seadrive_load_job_new(GPtrArray *full_paths);
void sond_seadrive_load_job_poll(SondSeadriveLoadJob *job);
gboolean sond_seadrive_load_job_all_done(SondSeadriveLoadJob *job);
guint sond_seadrive_load_job_n_entries(SondSeadriveLoadJob *job);
SondSeadriveLoadEntry const* sond_seadrive_load_job_entry(SondSeadriveLoadJob *job,
        guint index);
guint sond_seadrive_load_job_n_done(SondSeadriveLoadJob *job);
guint64 sond_seadrive_load_job_total_bytes(SondSeadriveLoadJob *job);
guint64 sond_seadrive_load_job_loaded_bytes(SondSeadriveLoadJob *job);
gboolean sond_seadrive_load_job_is_aggregate(SondSeadriveLoadJob *job);
void sond_seadrive_load_job_cancel(SondSeadriveLoadJob *job);
gboolean sond_seadrive_load_job_cancelled(SondSeadriveLoadJob *job);
GPtrArray* sond_seadrive_load_job_take_failed(SondSeadriveLoadJob *job);
void sond_seadrive_load_job_free(SondSeadriveLoadJob *job);

/* Zeiger auf CfGetPlaceholderInfo (cldapi.dll, zur Laufzeit geladen) oder NULL;
 * für Code, der Platzhalter-Informationen selbst abfragt (Sync-Zustand). */
gpointer sond_seadrive_core_placeholder_info(void);

/* Gilt die Datei als zum Server synchron (hochgeladen)? Im Zweifel nicht. */
gboolean sond_seadrive_file_in_sync(const gchar *path);

#endif /* _WIN32 */

/**
 * sond_seadrive_count_unsynced:
 * @dir: Verzeichnis (UTF-8), rekursiv durchsucht, auch Punkt-Dateien
 * @since_unix: nur Dateien, die seit diesem Zeitpunkt (Unix-Sekunden, mit einer
 *   Minute Toleranz) geändert wurden, kommen in Betracht
 *
 * Zählt die Dateien unter @dir, die seit @since_unix geändert wurden und noch
 * nicht mit dem Server synchron sind. Im Zweifel gilt eine Datei als nicht
 * synchron (ein fälschliches "alles hochgeladen" wäre gefährlicher).
 *
 * Returns: Anzahl (0 = alles hochgeladen) oder -1, wenn das nicht ermittelt
 *   werden kann (kein Windows, Cloud-API fehlt, Verzeichnis nicht lesbar,
 *   unverhältnismäßig viele Einträge).
 */
gint sond_seadrive_count_unsynced(const gchar *dir, gint64 since_unix);

G_END_DECLS

#endif /* SOND_SEADRIVE_CORE_H_INCLUDED */
