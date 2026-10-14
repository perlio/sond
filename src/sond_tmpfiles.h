/*
 zond (sond_tmpfiles.h) - Akten, Beweisstücke, Unterlagen
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

#ifndef SRC_SOND_TMPFILES_H_
#define SRC_SOND_TMPFILES_H_

#include <glib.h>

/**
 * Temporäre Dateien liegen in <Nutzerdaten>/zond/tmp/<pid>-<n>/<Name>
 * (Windows: AppData/Local/zond/tmp): je Datei ein eigener Ordner, damit der
 * Name eindeutig bleibt und die Anwendung, die die Datei öffnet, den richtigen
 * Namen zeigt.
 *
 * Beim ersten Gebrauch räumt das Modul die Ordner beendeter Prozesse auf,
 * startet einen Timer, der vorgemerkte Dateien löscht, sobald keine Anwendung
 * sie mehr geöffnet hat, und räumt beim Programmende ein letztes Mal auf.
 */

/**
 * sond_tmp_new_path:
 * @name: Dateiname (mit Endung); wird bereinigt und gekürzt, NULL = "datei"
 *
 * Legt den Ordner an (die Datei nicht) und liefert den Pfad. Mit g_free()
 * freigeben.
 */
gchar* sond_tmp_new_path(gchar const *name, GError **error);

/**
 * sond_tmp_register:
 * @path: Datei, die eine andere Anwendung öffnet
 *
 * Merkt die Datei zum Löschen vor: der Timer versucht es regelmäßig (frühestens
 * 60 s nach dem Anlegen). Eine geöffnete Datei bleibt liegen und wird später
 * erneut versucht.
 */
void sond_tmp_register(gchar const *path);

/**
 * sond_tmp_remove:
 * @path: Datei aus sond_tmp_new_path()
 *
 * Löscht die Datei (auch schreibgeschützt) und ihren Ordner.
 */
gboolean sond_tmp_remove(gchar const *path, GError **error);

/**
 * sond_tmp_cleanup:
 *
 * Letzter Löschversuch aller vorgemerkten Dateien und Ordner dieses Prozesses;
 * läuft von selbst beim Programmende.
 */
void sond_tmp_cleanup(void);

#endif /* SRC_SOND_TMPFILES_H_ */
