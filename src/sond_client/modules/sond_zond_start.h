/*
 sond (sond_zond_start.h) - Akten, Beweisstücke, Unterlagen
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

/* zond aus dem Client starten, mit der Projektdatei einer Akte. Frei von GTK. */

#ifndef SOND_ZOND_START_H
#define SOND_ZOND_START_H

#include <glib.h>
#include <gio/gio.h>

/**
 * sond_zond_project_file:
 * @library_dir: Verzeichnis der Library im SeaDrive
 * @library_name: Name der Library, z. B. "2026-23"
 *
 * Returns: (transfer full): Pfad der Projektdatei (<Library>/.<Name>.ZND)
 */
gchar* sond_zond_project_file(const gchar *library_dir, const gchar *library_name);

/**
 * sond_zond_find_exe:
 * @configured: (nullable): Pfad aus der Konfiguration ([zond] exe)
 * @error: (nullable)
 *
 * Sucht zond: erst der konfigurierte Pfad, dann zond(.exe) im Verzeichnis des
 * laufenden Programms, dann im Suchpfad.
 *
 * Returns: (transfer full) (nullable): Pfad oder NULL
 */
gchar* sond_zond_find_exe(const gchar *configured, GError **error);

/**
 * sond_zond_start:
 * @exe: Pfad zu zond
 * @project_file: Pfad der Projektdatei
 * @error: (nullable)
 *
 * Startet zond mit der Projektdatei als Argument, ohne auf das Ende zu warten.
 * Prüft vorher, dass die Projektdatei erreichbar ist.
 *
 * Returns: TRUE, wenn der Prozess gestartet wurde
 */
gboolean sond_zond_start(const gchar *exe, const gchar *project_file,
        GError **error);

#endif /* SOND_ZOND_START_H */
