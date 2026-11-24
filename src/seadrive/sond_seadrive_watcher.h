/*
 sond (sond_seadrive_watcher.h) - Akten, Beweisstücke, Unterlagen
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

/*
 * Beobachtet ein Verzeichnis im SeaDrive-Laufwerk (Windows: ReadDirectoryChangesW
 * in einem Thread) und führt den Zustand der Dateien (Badge, Ordner-Statistik,
 * Zähler "wird heruntergeladen" und "nicht synchron"). GTK-frei und ohne Bezug
 * zu einem Baum-Widget: GTK3-Zond und GTK4-Client binden es über die Signale an
 * ihre Anzeige. Alle Signale kommen im Hauptthread (GLib-Hauptschleife) an.
 *
 * Signale:
 *   changed()                    irgendein Zustand hat sich geändert
 *   status-changed(down, up)     die Zähler haben sich geändert
 *   file-changed(path)           der Badge einer Datei hat sich geändert
 *   hydrated(path)               eine Datei wurde geladen
 *   dehydrated(path)             eine Datei wurde aus dem lokalen Speicher genommen
 * path ist der volle Pfad. Nach stop()/stop_async() kommen keine Signale mehr.
 *
 * Nur unter Windows wirksam; sonst gibt es das Objekt nicht (die Aufrufer
 * prüfen _WIN32).
 */

#ifndef SOND_SEADRIVE_WATCHER_H_INCLUDED
#define SOND_SEADRIVE_WATCHER_H_INCLUDED

#include <glib-object.h>

#include "sond_seadrive_core.h" /* SondSeadriveBadge, SondSeadriveDirStatus */

G_BEGIN_DECLS

#ifdef _WIN32

/* Rekursive Ordner-Statistik, jeweils im ganzen Teilbaum unter dem Ordner. */
typedef struct {
	guint not_hydrated;    /* Dateien, die NICHT lokal vorhanden sind (unabhängig vom Pin) */
	guint hydrated_pinned; /* Dateien, die lokal vorhanden UND gepinnt sind */
	guint total;           /* Dateien insgesamt */
} SondSeadriveDirCounts;

#define SOND_TYPE_SEADRIVE_WATCHER sond_seadrive_watcher_get_type( )
G_DECLARE_FINAL_TYPE(SondSeadriveWatcher, sond_seadrive_watcher, SOND, SEADRIVE_WATCHER, GObject)

SondSeadriveWatcher* sond_seadrive_watcher_new(const gchar *root);

/* Startet den Thread (Initialscan, dann Ereignisse). Ein zweiter Aufruf ist wirkungslos. */
void sond_seadrive_watcher_start(SondSeadriveWatcher*);

/* Beendet den Thread und wartet darauf (Dauer bis ca. 20 s, s. Implementierung) */
void sond_seadrive_watcher_stop(SondSeadriveWatcher*);

/* Beendet den Thread, ohne zu warten. Das Objekt bleibt bis zum Ende des Threads am
 * Leben, der Aufrufer darf seine Referenz sofort freigeben. */
void sond_seadrive_watcher_stop_async(SondSeadriveWatcher*);

/* Datei-Badge bzw. Ordner-Status (NONE, wenn nichts bekannt ist); O(1) */
SondSeadriveBadge sond_seadrive_watcher_get_file_badge(SondSeadriveWatcher*,
		const gchar *file_full_path);
SondSeadriveDirStatus sond_seadrive_watcher_get_dir_status(SondSeadriveWatcher*,
		const gchar *dir_path);

/* Wie viele der Dateien im ganzen Projekt (Wurzel des Watchers) sind angeheftet (lokal
 * und gepinnt, oder gepinnt und noch im Laden)? FALSE, solange der erste Scan nicht fertig
 * ist. */
gboolean sond_seadrive_watcher_get_pin_summary(SondSeadriveWatcher*, guint *total,
		guint *pinned);

guint sond_seadrive_watcher_get_pending_down(SondSeadriveWatcher*);
guint sond_seadrive_watcher_get_pending_up(SondSeadriveWatcher*);

#endif /* _WIN32 */

G_END_DECLS

#endif /* SOND_SEADRIVE_WATCHER_H_INCLUDED */
