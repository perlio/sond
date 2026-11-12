/*
 sond (sond_server_seafile_stream.h) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_server_seafile_stream.h
 * @brief Datei-Download und -Upload gegen Seafile ohne Dateiinhalt im
 *        Arbeitsspeicher (für große Dateien, z. B. Archive)
 */

#ifndef SOND_SERVER_SEAFILE_STREAM_H
#define SOND_SERVER_SEAFILE_STREAM_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct _SondServer SondServer;

/**
 * sond_server_seafile_download_file:
 * @server: SondServer Instanz
 * @library_id: ID der Library
 * @path: Pfad der Datei in der Library (z. B. "/a/b.pdf")
 * @dest_path: lokale Zieldatei (wird ersetzt)
 * @error: (nullable): Error Rückgabe
 *
 * Lädt die Datei gestreamt in @dest_path. Bei einem Fehler oder
 * unvollständigem Empfang wird @dest_path wieder entfernt.
 *
 * Returns: TRUE bei Erfolg.
 */
gboolean sond_server_seafile_download_file(SondServer *server,
                                           const gchar *library_id,
                                           const gchar *path,
                                           const gchar *dest_path,
                                           GError **error);

/**
 * sond_server_seafile_upload_file:
 * @server: SondServer Instanz
 * @library_id: ID der Library
 * @parent_dir: vorhandenes Zielverzeichnis in der Library (z. B. "/")
 * @filename: Dateiname in der Library (gleichnamige Datei wird ersetzt)
 * @src_path: lokale Quelldatei
 * @error: (nullable): Error Rückgabe
 *
 * Lädt @src_path gestreamt hoch; der Inhalt wird nie vollständig in den
 * Arbeitsspeicher gelesen.
 *
 * Returns: TRUE bei Erfolg.
 */
gboolean sond_server_seafile_upload_file(SondServer *server,
                                         const gchar *library_id,
                                         const gchar *parent_dir,
                                         const gchar *filename,
                                         const gchar *src_path,
                                         GError **error);

G_END_DECLS

#endif /* SOND_SERVER_SEAFILE_STREAM_H */
