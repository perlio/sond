/*
 zond (export_zip.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2020  pelo america

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

#include <gtk/gtk.h>
#include <zip.h>

#include "../zond_init.h"

#include "export_zip.h"

static void export_zip_error(GError **error, zip_error_t *ze, const gchar *was) {
	g_set_error(error, ZOND_ERROR, ZOND_ERROR_ZIP, "%s\n%s", was,
			zip_error_strerror(ze));
}

gint export_zip_schreiben(const gchar *filename,
		const ExportZipDatei *dateien, guint anzahl, GError **error) {
	zip_error_t ze;
	zip_source_t *src = NULL;
	zip_t *za = NULL;
	zip_stat_t st;
	guint8 *buf = NULL;
	gboolean ok = FALSE;

	zip_error_init(&ze);

	src = zip_source_buffer_create(NULL, 0, 0, &ze);
	if (!src) {
		export_zip_error(error, &ze, "zip_source_buffer_create");
		zip_error_fini(&ze);

		return -1;
	}

	za = zip_open_from_source(src, ZIP_TRUNCATE, &ze);
	if (!za) {
		export_zip_error(error, &ze, "zip_open_from_source");
		zip_source_free(src);
		zip_error_fini(&ze);

		return -1;
	}

	//zip_close() gibt die Quelle frei - hier behalten, um das Ergebnis zu lesen
	zip_source_keep(src);

	for (guint i = 0; i < anzahl; i++) {
		zip_source_t *s = NULL;
		zip_int64_t idx = 0;

		s = zip_source_buffer(za, dateien[i].data, dateien[i].len, 0);
		if (!s)
			goto fehler_za;

		idx = zip_file_add(za, dateien[i].name, s, ZIP_FL_ENC_UTF_8);
		if (idx < 0) {
			zip_source_free(s);

			goto fehler_za;
		}

		if (dateien[i].store
				&& zip_set_file_compression(za, idx, ZIP_CM_STORE, 0))
			goto fehler_za;
	}

	if (zip_close(za)) {
		export_zip_error(error, zip_get_error(za), "zip_close");
		zip_discard(za);
		zip_source_free(src);
		zip_error_fini(&ze);

		return -1;
	}

	if (zip_source_open(src) < 0 || zip_source_stat(src, &st) < 0
			|| !(st.valid & ZIP_STAT_SIZE)) {
		export_zip_error(error, zip_source_error(src), "zip_source_open");
		zip_source_free(src);
		zip_error_fini(&ze);

		return -1;
	}

	buf = g_malloc(st.size);
	{
		zip_uint64_t gelesen = 0;

		while (gelesen < st.size) {
			zip_int64_t n = zip_source_read(src, buf + gelesen,
					st.size - gelesen);

			if (n <= 0)
				break;
			gelesen += n;
		}
		ok = (gelesen == st.size);
	}
	zip_source_close(src);
	zip_source_free(src);
	zip_error_fini(&ze);

	if (!ok) {
		g_free(buf);
		g_set_error(error, ZOND_ERROR, ZOND_ERROR_ZIP,
				"Archiv konnte nicht aus dem Speicher gelesen werden");

		return -1;
	}

	ok = g_file_set_contents(filename, (const gchar*) buf, st.size, error);
	g_free(buf);

	return ok ? 0 : -1;

	fehler_za:
	export_zip_error(error, zip_get_error(za), "zip_file_add");
	zip_discard(za);
	zip_source_free(src);
	zip_error_fini(&ze);

	return -1;
}
