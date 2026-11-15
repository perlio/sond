/*
 sond (sond_seadrive_sync.h) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_seadrive_sync.h
 * @brief Prüfung, ob lokale Änderungen im SeaDrive-Laufwerk schon hochgeladen sind
 *
 * Frei von GTK (nutzbar im Client). Nutzt unter Windows die Cloud-Files-API
 * (cldapi.dll), dieselbe wie der Watcher in sond_seadrive.c.
 */

#ifndef SOND_SEADRIVE_SYNC_H
#define SOND_SEADRIVE_SYNC_H

#include <glib.h>

G_BEGIN_DECLS

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

#endif /* SOND_SEADRIVE_SYNC_H */
