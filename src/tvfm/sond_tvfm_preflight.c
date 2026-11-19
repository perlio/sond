/*
 sond (sond_tvfm_preflight.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_tvfm_preflight.h"

#include "sond_tvfm_item_private.h"
#include "../sond_fileparts.h"
#include "../sond_password.h"

//Ein Eintrag prüfen; FALSE = Aktion abbrechen
static gboolean preflight_entry(SondTVFMPreflightResult *res, gboolean move,
		SondFilePart *sfp_zip, gchar const *path) {
	GError *error = NULL;

	if (sond_file_part_zip_probe_entry(sfp_zip, path, &error))
		return TRUE;

	if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		g_clear_error(&error);

		if (sond_password_last_result() == SOND_PROMPT_CANCEL || move) {
			res->aborted = TRUE;

			return FALSE;
		}

		//übersprungen: beim Kopieren auslassen
		{
			g_autofree gchar *archive_fp = sond_file_part_get_filepart(sfp_zip);
			g_autofree gchar *entry_fp = g_strconcat(archive_fp, "//", path, NULL);

			sond_password_action_skip_add(entry_fp);
		}

		return TRUE;
	}

	//nicht gefragt werden konnte oder anderer Fehler
	g_free(res->failure);
	res->failure = g_strdup(error ? error->message : "Eintrag nicht lesbar");
	g_clear_error(&error);

	return FALSE;
}

//Eine Quelle prüfen; FALSE = Aktion abbrechen
static gboolean preflight_item(SondTVFMPreflightResult *res, gboolean move,
		SondTVFMItem *item, SondFilePart *sfp_target) {
	SondTVFMItemPrivate *item_priv = sond_tvfm_item_get_priv(item);
	SondFilePart *sfp = item_priv->sond_file_part;
	gboolean ok = TRUE;

	if (!sfp)
		return TRUE; //Dateisystem: nichts zu entsperren

	if (!item_priv->path_or_section) {
		//einzelne Datei: gesperrter Eintrag in einem ZIP?
		SondFilePart *sfp_zip = sond_file_part_get_parent(sfp);

		if (!sfp_zip || !SOND_IS_FILE_PART_ZIP(sfp_zip) ||
				!(sond_file_part_is_placeholder(sfp) ||
				sond_file_part_get_lock_entry(sfp) != SOND_LOCK_NONE))
			return TRUE;

		//Verschieben im selben Archiv ist ein Umbenennen, ohne Lesen
		if (move && sfp_target == sfp_zip)
			return TRUE;

		return preflight_entry(res, move, sfp_zip, sond_file_part_get_path(sfp));
	}

	if (SOND_IS_FILE_PART_ZIP(sfp)) {
		//Verzeichnis im ZIP: alle gesperrten Einträge darunter
		GPtrArray *arr = NULL;

		if (move && sfp_target == sfp)
			return TRUE;

		arr = sond_file_part_zip_locked_entries(sfp, item_priv->path_or_section);
		for (guint i = 0; i < arr->len && ok; i++)
			ok = preflight_entry(res, move, sfp, g_ptr_array_index(arr, i));
		g_ptr_array_unref(arr);
	}

	return ok;
}

gboolean sond_tvfm_preflight(GPtrArray *items, SondTVFMItem *parent, gboolean move,
		SondTVFMPreflightResult *res) {
	SondFilePart *sfp_target = parent ? sond_tvfm_item_get_priv(parent)->sond_file_part : NULL;

	g_return_val_if_fail(items && res, FALSE);

	for (guint i = 0; i < items->len; i++)
		if (!preflight_item(res, move, g_ptr_array_index(items, i), sfp_target))
			return FALSE;

	return TRUE;
}
