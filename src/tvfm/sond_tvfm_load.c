/*
 sond (sond_tvfm_load.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_tvfm_load.h"

#include "sond_tvfm_item_private.h"
#include "../sond_log_and_error.h"
#include "../sond_fileparts.h"
#include "../sond_file_helper.h"
#include "../seadrive/sond_seadrive_core.h"

#ifdef _WIN32

//Schutz vor Verzeichnisschleifen (Junction/Symlink auf einen Vorfahren)
#define LOAD_MAX_DEPTH 64

gchar* sond_tvfm_load_root_file_path(SondTVFMOwner *owner, SondFilePart *sfp) {
	SondFilePart *sfp_root = sfp;
	SondFilePart *sfp_parent = NULL;
	gchar const *root = NULL;
	gchar const *sfp_path = NULL;

	if (!sfp || !sond_tvfm_owner_is_seadrive_path(owner))
		return NULL;

	while ((sfp_parent = sond_file_part_get_parent(sfp_root)))
		sfp_root = sfp_parent;

	root = sond_tvfm_owner_get_root(owner);
	sfp_path = sond_file_part_get_path(sfp_root);
	if (!root || !sfp_path)
		return NULL;

	return g_strconcat(root, "/", sfp_path, NULL);
}

gboolean sond_tvfm_load_not_loaded(SondTVFMOwner *owner, SondFilePart *sfp) {
	gchar *full_path = sond_tvfm_load_root_file_path(owner, sfp);
	gboolean not_loaded = FALSE;

	if (!full_path)
		return FALSE;

	not_loaded = sond_seadrive_needs_hydration(full_path);
	g_free(full_path);

	return not_loaded;
}

//nicht geladene Dateien unterhalb von abs_dir sammeln
static void scan_dir(GPtrArray *arr, gchar const *abs_dir, guint depth) {
	SondDir *dir = NULL;
	gchar const *name = NULL;

	if (depth > LOAD_MAX_DEPTH) {
		LOG_WARN("Verzeichnistiefe > %d bei '%s' - übersprungen "
				"(Verzeichnisschleife?)", LOAD_MAX_DEPTH, abs_dir);

		return;
	}

	dir = sond_dir_open(abs_dir, NULL);
	if (!dir)
		return;

	while ((name = sond_dir_read_name(dir)) != NULL) {
		gchar *path = g_strconcat(abs_dir, "/", name, NULL);
		GStatBuf st = { 0 };

		if (sond_stat(path, &st, NULL))
			g_free(path);
		else if (S_ISDIR(st.st_mode)) {
			scan_dir(arr, path, depth + 1);
			g_free(path);
		} else if (sond_seadrive_needs_hydration(path))
			g_ptr_array_add(arr, path);
		else
			g_free(path);
	}
	sond_dir_close(dir);
}

void sond_tvfm_load_collect(GPtrArray *arr, SondTVFMOwner *owner, SondTVFMItem *item) {
	SondTVFMItemPrivate *priv = sond_tvfm_item_get_priv(item);

	if (!sond_tvfm_owner_is_seadrive_path(owner))
		return;

	if (priv->sond_file_part) {
		gchar *full_path = sond_tvfm_load_root_file_path(owner, priv->sond_file_part);

		if (full_path && sond_seadrive_needs_hydration(full_path))
			g_ptr_array_add(arr, full_path);
		else
			g_free(full_path);
	} else { //Dateisystem-Verzeichnis
		gchar const *root = sond_tvfm_owner_get_root(owner);
		gchar *dir = NULL;

		if (!root)
			return;

		dir = priv->path_or_section ? g_strconcat(root, "/", priv->path_or_section, NULL) :
				g_strdup(root);
		scan_dir(arr, dir, 0);
		g_free(dir);
	}
}

#else /* !_WIN32 */

gchar* sond_tvfm_load_root_file_path(SondTVFMOwner *owner, SondFilePart *sfp) {
	(void) owner;
	(void) sfp;

	return NULL;
}

gboolean sond_tvfm_load_not_loaded(SondTVFMOwner *owner, SondFilePart *sfp) {
	(void) owner;
	(void) sfp;

	return FALSE;
}

void sond_tvfm_load_collect(GPtrArray *arr, SondTVFMOwner *owner, SondTVFMItem *item) {
	(void) arr;
	(void) owner;
	(void) item;
}

#endif
