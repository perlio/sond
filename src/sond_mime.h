/*
 sond (sond_mime.h) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2026  peloamerica

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

#ifndef SRC_SOND_MIME_H_
#define SRC_SOND_MIME_H_

typedef char gchar;
typedef size_t gsize;
typedef struct _GError GError;
typedef unsigned char guchar;
typedef int gboolean;

const gchar* mime_to_extension(const gchar*);

const gchar* mime_to_extension_ci(const gchar*);

const gchar* mime_to_extension_with_params(const gchar*);

const gchar* mime_from_extension(const gchar* filename);

gchar* mime_guess_content_type(const guchar* buffer, gsize size,
		const gchar* path, GError** error);

/* Einmalig aufrufen, solange garantiert nur der Hauptthread läuft (z.B. in
 * project_open(), vor sond_process_file_create_wctx()) - s. Doc-Kommentar
 * in sond_mime.c. Lädt das magic_t-Handle, das mime_guess_content_type()
 * danach für die gesamte Programmlaufzeit wiederverwendet, statt es bei
 * jedem Aufruf neu zu öffnen/laden. */
gboolean mime_guess_content_type_init(GError** error);

#endif /* SRC_SOND_MIME_H_ */
