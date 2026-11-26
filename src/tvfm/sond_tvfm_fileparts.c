/*
 sond (sond_tvfm_fileparts.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_tvfm_fileparts.h"

#include <glib/gstdio.h>
#include <sys/stat.h>

#include "../sond_fileparts.h"
#include "../sond_file_helper.h"
#include "../sond_log_and_error.h"
#include "../sond_mime.h"
#include "sond_tvfm_load.h"

/* Reiner readdir-Scanner für Verzeichnisse des Dateisystems (nicht in Containern). Er nutzt nur
 * Verzeichnislisting und Metadaten (sond_stat), das löst bei SeaDrive-Platzhaltern keine Hydrierung
 * aus. Der Dateityp kommt allein aus der Endung (mime_from_extension) und wird ohne Dateizugriff
 * über sond_file_part_create_leaf() verpackt. In Container steigt er nicht hinein: eine Datei wie
 * x.zip ist ein einziger Filepart; Inhalte darin erreicht man nur über eine ausdrückliche Auswahl. */
#define READDIR_MAX_DEPTH 64

static gint readdir_rek(gchar const *rel_dir, GHashTable *ht, SondTVFMFilepartsOpts const *opts,
		guint depth, GError **error) {
	gchar *path_dir = NULL;
	SondDir *dir = NULL;
	gchar const *filename = NULL;

	//Schutz vor Verzeichnisschleifen (Junction/Symlink auf einen Vorfahren)
	if (depth > READDIR_MAX_DEPTH) {
		LOG_WARN("%s: Verzeichnistiefe > %d bei '%s' - übersprungen (Verzeichnisschleife?)",
				__func__, READDIR_MAX_DEPTH, rel_dir ? rel_dir : "");

		return 0;
	}

	/* Ein vollständig indizierter Ast wird nicht aufgeschlüsselt (erneutes Indizieren wäre ein
	 * No-Op). Die Wurzel (rel_dir == NULL) wird nie geprüft: Coverage wird nicht über die oberste
	 * Ebene hinaus zusammengefasst. Der Aufrufer setzt skip_fully_covered nicht, wenn erneut
	 * indiziert werden soll. */
	if (opts->skip_fully_covered && opts->index_ctx && rel_dir
			&& sond_index_ctx_get_dir_status(opts->index_ctx, rel_dir) == SOND_INDEX_STATUS_FULL)
		return 0;

	path_dir = rel_dir ? g_strconcat(opts->root, "/", rel_dir, NULL) : g_strdup(opts->root);
	dir = sond_dir_open(path_dir, error);
	g_free(path_dir);
	if (!dir)
		return -1;

	while ((filename = sond_dir_read_name(dir)) != NULL) {
		gchar *rel_path_child = rel_dir ? g_strconcat(rel_dir, "/", filename, NULL) : g_strdup(filename);
		gchar *abs_path_child = g_strconcat(opts->root, "/", rel_path_child, NULL);
		GStatBuf st = { 0 };
		GError *error_stat = NULL;
		gint rc_stat = sond_stat(abs_path_child, &st, &error_stat);

		g_free(abs_path_child);
		if (rc_stat) {
			LOG_WARN("%s: sond_stat('%s') gibt Fehler zurück: %s", __func__, rel_path_child,
					error_stat ? error_stat->message : "?");
			g_clear_error(&error_stat);
			g_free(rel_path_child);
			continue;
		}

		if (S_ISDIR(st.st_mode)) {
			gint rc = readdir_rek(rel_path_child, ht, opts, depth + 1, error);

			g_free(rel_path_child);
			if (rc) {
				sond_dir_close(dir);

				return -1;
			}
		} else if (sond_file_name_not_indexable(filename)) { //Bild, Video, ...
			g_free(rel_path_child);
		} else {
			SondFilePart *sfp_leaf = sond_file_part_create_leaf(rel_path_child, NULL,
					mime_from_extension(filename));

			g_free(rel_path_child);
			g_hash_table_insert(ht, sfp_leaf, NULL);
		}
	}

	sond_dir_close(dir);

	return 0;
}

gint sond_tvfm_fileparts_readdir(gchar const *rel_dir, GHashTable *ht,
		SondTVFMFilepartsOpts const *opts, GError **error) {
	return readdir_rek(rel_dir, ht, opts, 0, error);
}

gint sond_tvfm_item_get_fileparts(SondTVFMItem *item, GHashTable *ht,
		SondTVFMFilepartsOpts const *opts, GError **error) {
	SondTVFMItemType type = sond_tvfm_item_get_item_type(item);
	gchar const *path_or_section = sond_tvfm_item_get_path_or_section(item);
	SondFilePart *sfp = sond_tvfm_item_get_sond_file_part(item);
	SondPageRange *range = NULL;

	//Verzeichnis des Dateisystems (sfp bleibt auf dem ganzen Ast NULL): readdir-Scanner
	if (type == SOND_TVFM_ITEM_TYPE_DIR && !sfp)
		return sond_tvfm_fileparts_readdir(path_or_section, ht, opts, error);

	//Verzeichnis in einem Container: der Nutzer hat den Container beim Navigieren bereits geöffnet
	if (type == SOND_TVFM_ITEM_TYPE_DIR && path_or_section) {
		g_autoptr(GPtrArray) arr_children = NULL;

		if (sond_tvfm_item_load_children(item, &arr_children, NULL, error))
			return -1;

		for (guint i = 0; i < arr_children->len; i++) {
			if (sond_tvfm_item_get_fileparts(g_ptr_array_index(arr_children, i), ht, opts, error))
				return -1;
		}

		return 0;
	}

	if (type == SOND_TVFM_ITEM_TYPE_LEAF_SECTION && path_or_section) {
		if (!opts->section_range) {
			g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
					"Abschnitt, dessen Bedeutung dieser Baum nicht kennt");

			return -1;
		}

		if (opts->section_range(path_or_section, opts->section_data, &range, error))
			return -1;
	} else if (type == SOND_TVFM_ITEM_TYPE_LEAF && !path_or_section && SOND_IS_FILE_PART_GMESSAGE(sfp)) {
		//"Message"-Knoten einer E-Mail: nur der Header (sfp ist derselbe wie der der ganzen eml)
		range = sond_page_range_new_gmessage_header();
	} else if (type == SOND_TVFM_ITEM_TYPE_LEAF && SOND_IS_FILE_PART_PDF(sfp)
			&& sond_tvfm_item_is_content_root_marker(item)) {
		//"PageTree" einer PDF mit Einbettungen: nur die Seiten, nicht die Einbettungen
		range = sond_page_range_new_pdf_pagetree();
	}

	//eigene Referenz pro Schlüssel; mehrere Punkte derselben Datei werden vereinigt
	sond_page_range_merge(ht, g_object_ref(sfp), range);

	return 0;
}
