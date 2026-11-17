/*
 sond (sond_tvfm_index_sync.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_tvfm_index_sync.h"

gboolean sond_tvfm_index_sync_prepare_insert(SondIndexCtx *ctx, gchar const *new_rel,
		gchar const *root_dir, GError **error) {
	gboolean batch = FALSE;
	gboolean ok = FALSE;

	if (!ctx || !new_rel)
		return TRUE;

	batch = sond_index_ctx_batch_begin(ctx, error);
	if (!batch)
		return FALSE;

	ok = sond_index_ctx_coverage_invalidate(ctx, new_rel, root_dir, error);
	sond_index_ctx_batch_end(ctx, ok);

	return ok;
}

gboolean sond_tvfm_index_sync_moved(SondIndexCtx *ctx, gchar const *old_rel,
		gchar const *new_rel, GError **error) {
	gboolean ok = FALSE;

	if (!ctx || !old_rel || !new_rel)
		return TRUE;

	if (!sond_index_ctx_batch_begin(ctx, error))
		return FALSE;

	ok = sond_index_ctx_rename_file(ctx, old_rel, new_rel, error);
	sond_index_ctx_batch_end(ctx, ok);

	return ok;
}

gboolean sond_tvfm_index_sync_deleted(SondIndexCtx *ctx, gchar const *rel, GError **error) {
	gboolean ok = FALSE;

	if (!ctx || !rel)
		return TRUE;

	if (!sond_index_ctx_batch_begin(ctx, error))
		return FALSE;

	/* Der Pfad selbst existiert nicht mehr; ein abdeckender Vorfahre bleibt gültig (Löschen
	 * kann seine Aussage nicht verletzen), nur Einträge für diesen Pfad räumen wir weg, damit
	 * nicht später ein ganz anderer Pfad gleichen Namens als "schon abgedeckt" gilt. */
	ok = sond_index_ctx_clear_file(ctx, rel, error) &&
			sond_index_ctx_coverage_clear(ctx, rel, error);
	sond_index_ctx_batch_end(ctx, ok);

	return ok;
}
