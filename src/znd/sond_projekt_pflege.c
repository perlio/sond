/*
 zond (sond_projekt_pflege.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_projekt_pflege.h"

#include "../sond_log_and_error.h"
#include "../sond_process_file.h"

struct _SondProjektPflege {
	SondProjektKontext kontext;

	/* Index-DB innerhalb der Dual-Write-Transaktion: als Schema idx an die store-Connection
	 * gehängt, index_tx ist eine Kopie des Index-Kontexts, die über diese Connection arbeitet.
	 * So gehören die Schreibzugriffe auf die Index-DB zum selben Commit wie store und work. */
	SondIndexCtx index_tx;
	gboolean index_tx_active;

	//PDFs und Mails, deren Struktur sich durch die laufende Aktion ändert (eigene Refs)
	GPtrArray *pending_structure;

	//Adressänderungen der übrigen Anhänge einer PDF (s. emb_sibling_changes)
	SondFilePart *pending_sibling_pdf;
	GPtrArray *pending_sibling_old;
	GPtrArray *pending_sibling_new;
};

SondProjektPflege* sond_projekt_pflege_new(SondProjektKontext const *kontext) {
	SondProjektPflege *pflege = NULL;

	g_return_val_if_fail(kontext && kontext->dbase, NULL);

	pflege = g_new0(SondProjektPflege, 1);
	pflege->kontext = *kontext;

	return pflege;
}

void sond_projekt_pflege_free(SondProjektPflege *pflege) {
	if (!pflege)
		return;

	sond_projekt_pflege_emb_sibling_clear(pflege);
	sond_projekt_pflege_structure_clear(pflege);
	g_free(pflege);
}

static DBaseZond* pflege_dbase(SondProjektPflege *pflege) {
	return pflege->kontext.dbase(pflege->kontext.data);
}

static SondIndexCtx* pflege_index_ctx(SondProjektPflege *pflege) {
	return pflege->kontext.index_ctx ? pflege->kontext.index_ctx(pflege->kontext.data) : NULL;
}

/* ------------------------------------------------------------------------
 * Prüfen
 * --------------------------------------------------------------------- */

gint sond_projekt_pflege_check_delete(SondProjektPflege *pflege, gchar const *path,
		gboolean is_dir, gboolean container_root, gchar const *section, DBaseZondRef *ref,
		GError **error) {
	if (is_dir) {
		g_autofree gchar *path_prefix = g_strconcat(path, container_root ? "//" : "/", NULL);

		return dbase_zond_test_path(pflege_dbase(pflege), path_prefix, ref, error);
	}

	return dbase_zond_test_path_section(pflege_dbase(pflege), path, section, FALSE, ref, error);
}

/* ------------------------------------------------------------------------
 * Index in der Transaktion
 * --------------------------------------------------------------------- */

gint sond_projekt_pflege_index_tx_begin(SondProjektPflege *pflege, GError **error) {
	SondIndexCtx *index_ctx = pflege_index_ctx(pflege);
	DBaseZond *dbase_zond = pflege_dbase(pflege);

	if (!index_ctx || pflege->index_tx_active)
		return 0;

	if (dbase_zond_attach_index(dbase_zond, index_ctx->db_path, error))
		return -1;

	dbase_zond_index_ctx(dbase_zond, &pflege->index_tx, index_ctx);
	pflege->index_tx_active = TRUE;

	return 0;
}

void sond_projekt_pflege_index_tx_end(SondProjektPflege *pflege) {
	if (!pflege->index_tx_active)
		return;

	dbase_zond_detach_index(pflege_dbase(pflege));
	pflege->index_tx_active = FALSE;
}

SondIndexCtx* sond_projekt_pflege_index_tx(SondProjektPflege *pflege) {
	return pflege->index_tx_active ? &pflege->index_tx : NULL;
}

void sond_projekt_pflege_tx_abort(SondProjektPflege *pflege) {
	dbase_zond_rollback(pflege_dbase(pflege), NULL);
	sond_projekt_pflege_index_tx_end(pflege);
}

gint sond_projekt_pflege_gmessage_index_renumber(SondProjektPflege *pflege, gchar const *prefix,
		gint index, gboolean into, GError **error) {
	SondIndexCtx *index_ctx = NULL;
	gchar const *boundary = NULL;
	g_autofree gchar *mail = NULL;
	GError *idx_err = NULL;

	index_ctx = sond_projekt_pflege_index_tx(pflege);
	if (!index_ctx)
		return 0;

	if (!sond_index_ctx_update_gmessage_index(index_ctx, prefix, index, into, &idx_err))
		goto err;

	//Mail = alles vor dem letzten "//"
	boundary = g_strrstr(prefix, "//");
	mail = boundary ? g_strndup(prefix, boundary - prefix) : g_strdup(prefix);
	if (!sond_index_ctx_clear_gmessage_structure(index_ctx, mail, &idx_err))
		goto err;

	return 0;

err:
	if (error)
		*error = g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED, "%s: %s", __func__,
				idx_err ? idx_err->message : "?");
	g_clear_error(&idx_err);

	return -1;
}

/* ------------------------------------------------------------------------
 * Anhänge einer PDF
 * --------------------------------------------------------------------- */

void sond_projekt_pflege_emb_sibling_clear(SondProjektPflege *pflege) {
	g_clear_object(&pflege->pending_sibling_pdf);
	g_clear_pointer(&pflege->pending_sibling_old, g_ptr_array_unref);
	g_clear_pointer(&pflege->pending_sibling_new, g_ptr_array_unref);
}

gint sond_projekt_pflege_emb_sibling_changes(SondProjektPflege *pflege, SondFilePart *sfp,
		gchar const *filename_new, GError **error) {
	SondFilePart *sfp_pdf = NULL;
	GPtrArray *old = NULL;
	GPtrArray *new = NULL;

	sond_projekt_pflege_emb_sibling_clear(pflege);

	if (!sfp)
		return 0;

	sfp_pdf = sond_file_part_get_parent(sfp);
	if (!sfp_pdf || !SOND_IS_FILE_PART_PDF(sfp_pdf))
		return 0;

	if (sond_file_part_pdf_emb_address_changes(SOND_FILE_PART_PDF(sfp_pdf),
			sond_file_part_get_path(sfp), filename_new, &old, &new, error))
		return -1;

	if (old->len == 0) {
		g_ptr_array_unref(old);
		g_ptr_array_unref(new);

		return 0;
	}

	pflege->pending_sibling_pdf = g_object_ref(sfp_pdf);
	pflege->pending_sibling_old = old;
	pflege->pending_sibling_new = new;

	return 0;
}

gint sond_projekt_pflege_emb_sibling_apply(SondProjektPflege *pflege, GError **error) {
	g_autofree gchar *filepart_pdf = NULL;

	if (!pflege->pending_sibling_pdf)
		return 0;

	filepart_pdf = sond_file_part_get_filepart(pflege->pending_sibling_pdf);

	for (guint i = 0; i < pflege->pending_sibling_old->len; i++) {
		g_autofree gchar *path_old = g_strconcat(filepart_pdf, "//",
				g_ptr_array_index(pflege->pending_sibling_old, i), NULL);
		g_autofree gchar *path_new = g_strconcat(filepart_pdf, "//",
				g_ptr_array_index(pflege->pending_sibling_new, i), NULL);
		GError *idx_err = NULL;

		if (dbase_zond_update_path(pflege_dbase(pflege), path_old, path_new, error))
			return -1;

		if (sond_projekt_pflege_index_tx(pflege) &&
				!sond_index_ctx_rename_file(sond_projekt_pflege_index_tx(pflege), path_old,
						path_new, &idx_err)) {
			if (error)
				*error = g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED,
						"%s: sond_index_ctx_rename_file: %s", __func__,
						idx_err ? idx_err->message : "?");
			g_clear_error(&idx_err);

			return -1;
		}
	}

	return 0;
}

void sond_projekt_pflege_emb_sibling_adjust_opened(SondProjektPflege *pflege) {
	GPtrArray *opened = NULL;

	if (!pflege->pending_sibling_pdf)
		return;

	opened = sond_file_part_get_arr_opened_files(pflege->pending_sibling_pdf);
	for (guint i = 0; opened && i < opened->len; i++) {
		SondFilePart *child = g_ptr_array_index(opened, i);

		for (guint j = 0; j < pflege->pending_sibling_old->len; j++)
			if (!g_strcmp0(sond_file_part_get_path(child),
					g_ptr_array_index(pflege->pending_sibling_old, j))) {
				sond_file_part_set_path(child, g_ptr_array_index(pflege->pending_sibling_new, j));
				break;
			}
	}
}

/* ------------------------------------------------------------------------
 * Struktur von PDF und Mail im Index
 * --------------------------------------------------------------------- */

void sond_projekt_pflege_structure_add(SondProjektPflege *pflege, SondFilePart *container) {
	if (!container)
		return;

	if (!pflege->pending_structure)
		pflege->pending_structure = g_ptr_array_new_with_free_func(g_object_unref);
	else if (g_ptr_array_find(pflege->pending_structure, container, NULL))
		return;

	g_ptr_array_add(pflege->pending_structure, g_object_ref(container));
}

void sond_projekt_pflege_structure_clear(SondProjektPflege *pflege) {
	g_clear_pointer(&pflege->pending_structure, g_ptr_array_unref);
}

void sond_projekt_pflege_structure_refresh(SondProjektPflege *pflege) {
	SondIndexCtx *index_ctx = pflege_index_ctx(pflege);

	if (!pflege->pending_structure || !index_ctx)
		return;

	for (guint i = 0; i < pflege->pending_structure->len; i++) {
		GError *error = NULL;

		if (sond_process_file_record_structure(index_ctx,
				g_ptr_array_index(pflege->pending_structure, i), &error)) {
			LOG_WARN("%s: %s", __func__, error ? error->message : "?");
			g_clear_error(&error);
		}
	}
}
