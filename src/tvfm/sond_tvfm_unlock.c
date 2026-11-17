/*
 sond (sond_tvfm_unlock.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_tvfm_unlock.h"

#include <string.h>

#include "sond_tvfm_item_private.h"
#include "../sond_password.h"

typedef struct {
	SondFilePart *zip; //Ref
	GPtrArray *paths;  //Pfade der verschlüsselten Einträge im Archiv (g_free)
} ZipJob;

static void zip_job_free(gpointer p) {
	ZipJob *job = (ZipJob*) p;

	g_object_unref(job->zip);
	g_ptr_array_unref(job->paths);
	g_free(job);
}

void sond_tvfm_unlock_jobs_init(SondUnlockJobs *jobs) {
	jobs->zips = g_ptr_array_new_with_free_func(zip_job_free);
	jobs->pdfs = g_ptr_array_new_with_free_func(g_object_unref);
}

void sond_tvfm_unlock_jobs_clear(SondUnlockJobs *jobs) {
	g_ptr_array_unref(jobs->zips);
	g_ptr_array_unref(jobs->pdfs);
	jobs->zips = NULL;
	jobs->pdfs = NULL;
}

static ZipJob* zip_job_get(SondUnlockJobs *jobs, SondFilePart *zip) {
	ZipJob *job = NULL;

	for (guint i = 0; i < jobs->zips->len; i++) {
		job = g_ptr_array_index(jobs->zips, i);
		if (job->zip == zip)
			return job;
	}

	job = g_new0(ZipJob, 1);
	job->zip = g_object_ref(zip);
	job->paths = g_ptr_array_new_with_free_func(g_free);
	g_ptr_array_add(jobs->zips, job);

	return job;
}

static void zip_job_add_path(ZipJob *job, gchar const *path) {
	for (guint i = 0; i < job->paths->len; i++)
		if (!g_strcmp0(g_ptr_array_index(job->paths, i), path))
			return;

	g_ptr_array_add(job->paths, g_strdup(path));
}

//Pfade der verschlüsselten Einträge unter prefix (NULL = ganzes Archiv) dazu
static void zip_job_add_locked(SondUnlockJobs *jobs, SondFilePart *zip, gchar const *prefix) {
	GPtrArray *arr = sond_file_part_zip_locked_entries(zip, prefix);

	if (arr->len) {
		ZipJob *job = zip_job_get(jobs, zip);

		for (guint i = 0; i < arr->len; i++)
			zip_job_add_path(job, g_ptr_array_index(arr, i));
	}
	g_ptr_array_unref(arr);
}

static void pdf_job_add(SondUnlockJobs *jobs, SondFilePart *sfp) {
	for (guint i = 0; i < jobs->pdfs->len; i++)
		if (g_ptr_array_index(jobs->pdfs, i) == sfp)
			return;

	g_ptr_array_add(jobs->pdfs, g_object_ref(sfp));
}

void sond_tvfm_unlock_collect_item(SondUnlockJobs *jobs, SondTVFMItem *stvfm_item) {
	SondTVFMItemPrivate *item_priv = sond_tvfm_item_get_priv(stvfm_item);
	SondFilePart *sfp = item_priv->sond_file_part;

	if (!sfp)
		return; //Dateisystem-Verzeichnis, nichts zu entsperren

	if (!item_priv->path_or_section) {
		SondFilePart *parent = sond_file_part_get_parent(sfp);

		//selbst ein verschlüsselter Eintrag eines ZIP-Archivs
		if (parent && SOND_IS_FILE_PART_ZIP(parent) &&
				sond_file_part_zip_entry_is_encrypted(parent,
						sond_file_part_get_path(sfp)))
			zip_job_add_path(zip_job_get(jobs, parent), sond_file_part_get_path(sfp));

		//ein ZIP-Archiv als Ganzes: alle verschlüsselten Einträge
		if (SOND_IS_FILE_PART_ZIP(sfp))
			zip_job_add_locked(jobs, sfp, NULL);

		//eine PDF mit Passwort
		if (SOND_IS_FILE_PART_PDF(sfp) &&
				sond_file_part_get_lock_content(sfp) != SOND_LOCK_NONE)
			g_ptr_array_add(jobs->pdfs, g_object_ref(sfp));
	}
	else if (SOND_IS_FILE_PART_ZIP(sfp))
		zip_job_add_locked(jobs, sfp, item_priv->path_or_section);
}

SondUnlockPrepare sond_tvfm_unlock_prepare(SondUnlockJobs *jobs, SondUnlockHooks const *hooks,
		guint *n_total, gchar **message) {
	guint n = 0;

	for (guint i = 0; i < jobs->zips->len; i++)
		n += ((ZipJob*) g_ptr_array_index(jobs->zips, i))->paths->len;
	n += jobs->pdfs->len;
	if (n_total)
		*n_total = n;

	if (n == 0)
		return SOND_UNLOCK_NOTHING;

	//eine im Viewer geöffnete PDF nicht anfassen
	if (hooks && hooks->busy)
		for (guint i = 0; i < jobs->pdfs->len; i++) {
			SondFilePart *sfp = g_ptr_array_index(jobs->pdfs, i);

			if (hooks->busy(sfp, hooks->data)) {
				g_autofree gchar *fp = sond_file_part_get_filepart(sfp);

				if (message)
					*message = g_strdup_printf(
							"Die Datei '%s' ist geöffnet. Bitte zuerst schließen.", fp);

				return SOND_UNLOCK_BUSY;
			}
		}

	//lokal verfügbar?
	if (hooks && hooks->ensure_local) {
		for (guint i = 0; i < jobs->zips->len; i++)
			if (!hooks->ensure_local(((ZipJob*) g_ptr_array_index(jobs->zips, i))->zip,
					hooks->data))
				return SOND_UNLOCK_NOT_LOCAL;
		for (guint i = 0; i < jobs->pdfs->len; i++)
			if (!hooks->ensure_local(g_ptr_array_index(jobs->pdfs, i), hooks->data))
				return SOND_UNLOCK_NOT_LOCAL;
	}

	return SOND_UNLOCK_READY;
}

static void result_init(SondUnlockResult *res) {
	memset(res, 0, sizeof(SondUnlockResult));
	res->decrypted = g_ptr_array_new_with_free_func(g_free);
	res->skipped_fps = g_ptr_array_new_with_free_func(g_free);
}

void sond_tvfm_unlock_result_clear(SondUnlockResult *res) {
	if (res->decrypted)
		g_ptr_array_unref(res->decrypted);
	if (res->skipped_fps)
		g_ptr_array_unref(res->skipped_fps);
	res->decrypted = NULL;
	res->skipped_fps = NULL;
}

static void result_add(GPtrArray *arr, gchar const *fp) {
	for (guint i = 0; i < arr->len; i++)
		if (!g_strcmp0(g_ptr_array_index(arr, i), fp))
			return;

	g_ptr_array_add(arr, g_strdup(fp));
}

//Fehlschlag eines Entsperrversuchs: TRUE = weiter (übersprungen), FALSE = Aktion beenden
//(Abbruch oder Fehler)
static gboolean handle_failure(SondUnlockHooks const *hooks, GError *error, gchar const *fp,
		SondUnlockResult *res) {
	if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		if (sond_password_last_result() == SOND_PROMPT_CANCEL) {
			res->aborted = TRUE;

			return FALSE;
		}
		res->skipped++;
		if (fp)
			result_add(res->skipped_fps, fp);

		return TRUE; //übersprungen
	}

	if (hooks && hooks->report)
		hooks->report(error ? error->message : "Fehler", hooks->data);
	res->aborted = TRUE;

	return FALSE;
}

static void refresh(SondUnlockHooks const *hooks, SondFilePart *sfp) {
	if (hooks && hooks->refresh)
		hooks->refresh(sfp, hooks->data);
}

void sond_tvfm_unlock_run(SondUnlockJobs *jobs, SondUnlockHooks const *hooks,
		SondUnlockResult *res) {
	result_init(res);
	sond_password_action_begin(TRUE);

	for (guint i = 0; i < jobs->zips->len && !res->aborted; i++) {
		ZipJob *job = g_ptr_array_index(jobs->zips, i);
		GPtrArray *ok_paths = g_ptr_array_new_with_free_func(g_free);
		GPtrArray *new_sfps = NULL;
		g_autofree gchar *archive_fp = sond_file_part_get_filepart(job->zip);

		for (guint k = 0; k < job->paths->len && !res->aborted; k++) {
			gchar const *path = g_ptr_array_index(job->paths, k);
			g_autofree gchar *entry_fp = g_strconcat(archive_fp, "//", path, NULL);
			GError *error_probe = NULL;

			if (sond_file_part_zip_probe_entry(job->zip, path, &error_probe))
				g_ptr_array_add(ok_paths, g_strdup(path));
			else
				handle_failure(hooks, error_probe, entry_fp, res);
			g_clear_error(&error_probe);
		}

		if (!res->aborted && ok_paths->len) {
			GError *error_dec = NULL;

			if (sond_file_part_zip_decrypt_entries(job->zip, ok_paths, &new_sfps,
					&error_dec)) {
				handle_failure(hooks, error_dec, NULL, res);
				g_clear_error(&error_dec);
			}
			else {
				res->done += ok_paths->len;

				for (guint k = 0; k < ok_paths->len; k++) {
					g_autofree gchar *entry_fp = g_strconcat(archive_fp, "//",
							(gchar const*) g_ptr_array_index(ok_paths, k), NULL);

					result_add(res->decrypted, entry_fp);
				}

				for (guint k = 0; new_sfps && k < new_sfps->len; k++) {
					SondFilePart *sfp_new = g_ptr_array_index(new_sfps, k);

					refresh(hooks, sfp_new);
					if (SOND_IS_FILE_PART_PDF(sfp_new) &&
							sond_file_part_get_lock_content(sfp_new) != SOND_LOCK_NONE)
						pdf_job_add(jobs, sfp_new);
				}
			}
		}

		if (new_sfps)
			g_ptr_array_unref(new_sfps);
		g_ptr_array_unref(ok_paths);
	}

	for (guint i = 0; i < jobs->pdfs->len && !res->aborted; i++) {
		SondFilePart *sfp = g_ptr_array_index(jobs->pdfs, i);
		g_autofree gchar *fp = sond_file_part_get_filepart(sfp);
		GError *error_pdf = NULL;

		if (sond_file_part_pdf_decrypt(sfp, &error_pdf))
			handle_failure(hooks, error_pdf, fp, res);
		else {
			res->done++;
			result_add(res->decrypted, fp);

			//jetzt mit bekannten Anhängen: Baumelement (Blatt -> Ordner)
			refresh(hooks, sfp);
		}
		g_clear_error(&error_pdf);
	}

	sond_password_action_end();
}

void sond_tvfm_unlock_locked_items(GPtrArray *items, SondUnlockHooks const *hooks,
		SondUnlockResult *res) {
	SondUnlockJobs jobs;
	GPtrArray *unreachable = g_ptr_array_new_with_free_func(g_free);

	sond_tvfm_unlock_jobs_init(&jobs);

	for (guint i = 0; i < items->len; i++) {
		SondLockedItem *item = g_ptr_array_index(items, i);
		SondFilePart *sfp = NULL;
		GError *error = NULL;
		gboolean ok = FALSE;

		if (item->kind == SOND_LOCKED_ZIP_ENTRY) {
			gchar const *sep = g_strrstr(item->filepart, "//");

			if (sep) {
				g_autofree gchar *archive_fp = g_strndup(item->filepart,
						sep - item->filepart);

				sfp = sond_file_part_from_filepart(archive_fp, &error);
				if (sfp && SOND_IS_FILE_PART_ZIP(sfp)) {
					zip_job_add_path(zip_job_get(&jobs, sfp), sep + 2);
					ok = !(hooks && hooks->ensure_local) ||
							hooks->ensure_local(sfp, hooks->data);
				}
			}
		}
		else {
			sfp = sond_file_part_from_filepart(item->filepart, &error);
			if (sfp && SOND_IS_FILE_PART_PDF(sfp) &&
					!(hooks && hooks->busy && hooks->busy(sfp, hooks->data)) &&
					(!(hooks && hooks->ensure_local) ||
							hooks->ensure_local(sfp, hooks->data))) {
				pdf_job_add(&jobs, sfp);
				ok = TRUE;
			}
		}

		if (!ok)
			result_add(unreachable, item->filepart);
		if (sfp)
			g_object_unref(sfp);
		g_clear_error(&error); //Abbruch/Fehler: Datei bleibt gesperrt
	}

	sond_tvfm_unlock_run(&jobs, hooks, res);

	//was sich nicht bereitstellen ließ, zählt als übersprungen
	for (guint i = 0; i < unreachable->len; i++)
		result_add(res->skipped_fps, g_ptr_array_index(unreachable, i));

	g_ptr_array_unref(unreachable);
	sond_tvfm_unlock_jobs_clear(&jobs);
}
