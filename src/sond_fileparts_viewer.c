/*
 sond (sond_fileparts_viewer.c) - Akten, Beweisstücke, Unterlagen
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
 * sond_file_part_open() - aus sond_fileparts.c ausgelagert, weil es die
 * GUI (sond_render() aus sond_renderer.c bzw. "Öffnen mit") braucht und
 * damit NUR von zond/viewer, nicht aber von sond_server benötigt wird.
 * Diese Datei wird deshalb im Makefile nur für die Targets zond und
 * viewer in SRCS aufgenommen, nicht für sond_server - kein #ifdef nötig.
 */

#include "sond_fileparts.h"
#include "sond_renderer.h"
#include "sond_file_helper.h"
#include "sond_tmpfiles.h"
#include "sond_log_and_error.h"

gint sond_file_part_open(SondFilePart* sfp, gboolean open_with,
		GError** error) {
	g_return_val_if_fail(sfp, -1);

	//hier alle Varianten, in denen eigener Viewer geöffnet wird
	if (!open_with &&
			((SOND_IS_FILE_PART_LEAF(sfp) &&
			((g_str_has_prefix(sond_file_part_leaf_get_mime_type(
					SOND_FILE_PART_LEAF(sfp)), "text/") &&
			 g_strcmp0(sond_file_part_leaf_get_mime_type(
					SOND_FILE_PART_LEAF(sfp)), "text/csv")) ||
			g_str_has_prefix(sond_file_part_leaf_get_mime_type(
					SOND_FILE_PART_LEAF(sfp)), "image/") ||
			!g_strcmp0("application/vnd.oasis.opendocument.text",
					sond_file_part_leaf_get_mime_type(
					SOND_FILE_PART_LEAF(sfp))) ||
			!g_strcmp0("application/vnd.openxmlformats-officedocument.wordprocessingml.document",
					sond_file_part_leaf_get_mime_type(
					SOND_FILE_PART_LEAF(sfp))))) ||
					SOND_IS_FILE_PART_GMESSAGE(sfp)))
	{
		GBytes* bytes = NULL;
		gint rc = 0;

		bytes = sond_file_part_get_bytes(sfp, error);
		if (!bytes)
			return -1;

		rc = sond_render(bytes, sfp, NULL, error);
		g_bytes_unref(bytes);
		if (rc)
			return -1;
	}/*
	else if (!open_with && SOND_IS_FILE_PART_PDF(sfp)) {

	}*/
	else {
		g_autofree gchar* path = NULL;

		if (!sond_file_part_get_parent(sfp)) //Datei im Filesystem
			path = g_strconcat(SOND_FILE_PART_CLASS(
					g_type_class_peek(SOND_TYPE_FILE_PART))->path_root,
					"/", sond_file_part_get_path(sfp), NULL);
		else { //Datei in zip/pdf/gmessage
			path = sond_file_part_write_to_tmp_file(sfp, TRUE, error);
			if (!path)
				return -1;
		}

		if (!sond_open(path, open_with, error)) {
			if (sond_file_part_get_parent(sfp)) { //tmp-Datei wurde erzeugt
				GError* error_rem = NULL;

				if (!sond_tmp_remove(path, &error_rem)) {
					LOG_WARN("Datei '%s' konnte nicht gelöscht werden:\n%s",
							path, error_rem ? error_rem->message : "(kein Fehler gesetzt)");
					g_clear_error(&error_rem);
				}
			}
			return -1;
		}

		//Kopie aus einem Container: löschen, sobald keine Anwendung sie mehr hält
		if (sond_file_part_get_parent(sfp))
			sond_tmp_register(path);
	}

	return 0;
}
