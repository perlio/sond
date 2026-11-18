/*
 sond (sond_tvfm_search.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_tvfm_search.h"

#include <string.h>

#include "sond_tvfm_item_private.h"
#include "../sond_log_and_error.h"
#include "../sond_fileparts.h"
#include "sond_tvfm_load.h"

//Schutz vor Verzeichnisschleifen (Junction/Symlink auf einen Vorfahren)
#define SEARCH_MAX_DEPTH 64

struct _SondTVFMSearch {
	SondTVFMOwner *owner;
	SondTVFMSearchOpts opts;
	gchar *text; //Kopie von opts.text
	gchar *needle; //bei !case_sensitive bereits gefaltet
	GPatternSpec *pspec; //nur bei SOND_TVFM_SEARCH_WILDCARD
	GPtrArray *arr_hits;
	GHashTable *ht_seen; //Trefferstrings (gehören arr_hits), gegen Doppelte
	guint n_skipped;
	guint n_not_loaded; //nicht geladene SeaDrive-Container, nicht durchsucht
	gint cancelled;
};

SondTVFMSearch* sond_tvfm_search_new(SondTVFMOwner *owner,
		SondTVFMSearchOpts const *opts) {
	SondTVFMSearch *search = NULL;

	g_return_val_if_fail(SOND_IS_TVFM_OWNER(owner), NULL);
	g_return_val_if_fail(opts && opts->text && *opts->text, NULL);

	search = g_new0(SondTVFMSearch, 1);
	search->owner = owner;
	search->opts = *opts;
	search->text = g_strdup(opts->text);
	search->opts.text = search->text;
	search->needle = opts->case_sensitive ? g_strdup(opts->text) :
			g_utf8_casefold(opts->text, -1);
	if (opts->mode == SOND_TVFM_SEARCH_WILDCARD)
		search->pspec = g_pattern_spec_new(search->needle);
	search->arr_hits = g_ptr_array_new_with_free_func(g_free);
	search->ht_seen = g_hash_table_new(g_str_hash, g_str_equal);

	return search;
}

void sond_tvfm_search_free(SondTVFMSearch *search) {
	if (!search)
		return;

	g_hash_table_destroy(search->ht_seen);
	if (search->arr_hits)
		g_ptr_array_unref(search->arr_hits);
	if (search->pspec)
		g_pattern_spec_free(search->pspec);
	g_free(search->needle);
	g_free(search->text);
	g_free(search);

	return;
}

void sond_tvfm_search_cancel(SondTVFMSearch *search) {
	g_return_if_fail(search);

	g_atomic_int_set(&search->cancelled, 1);

	return;
}

GPtrArray* sond_tvfm_search_take_hits(SondTVFMSearch *search) {
	GPtrArray *arr = NULL;

	g_return_val_if_fail(search, NULL);

	arr = search->arr_hits;
	search->arr_hits = NULL;
	g_hash_table_remove_all(search->ht_seen);

	return arr;
}

guint sond_tvfm_search_get_n_skipped(SondTVFMSearch *search) {
	g_return_val_if_fail(search, 0);

	return search->n_skipped;
}

guint sond_tvfm_search_get_n_not_loaded(SondTVFMSearch *search) {
	g_return_val_if_fail(search, 0);

	return search->n_not_loaded;
}

static gboolean search_name_matches(SondTVFMSearch *search, gchar const *name) {
	gboolean found = FALSE;
	gchar *folded = NULL;
	gchar const *cmp = name;

	if (!search->opts.case_sensitive) {
		folded = g_utf8_casefold(name, -1);
		cmp = folded;
	}

	switch (search->opts.mode) {
	case SOND_TVFM_SEARCH_WHOLE_NAME:
		found = !g_strcmp0(cmp, search->needle);
		break;
	case SOND_TVFM_SEARCH_WILDCARD:
		found = g_pattern_spec_match_string(search->pspec, cmp);
		break;
	default:
		found = (strstr(cmp, search->needle) != NULL);
	}

	g_free(folded);

	return found;
}

static void search_add_hit(SondTVFMSearch *search, SondTVFMItem *item) {
	gchar *filepart = sond_tvfm_item_get_filepart(item);

	if (!filepart)
		return;

	if (g_hash_table_contains(search->ht_seen, filepart)) {
		g_free(filepart);

		return;
	}

	g_hash_table_add(search->ht_seen, filepart);
	g_ptr_array_add(search->arr_hits, filepart);

	return;
}

/* Prüft das Item und durchsucht (bei Verzeichnissen und, wenn erlaubt,
 * Containern) rekursiv seine Kinder. Nur für das oberste Item (top) wird ein
 * Lesefehler als Fehler gemeldet, tiefer liegende nicht lesbare Verzeichnisse
 * werden übersprungen und gezählt. */
static gint search_item(SondTVFMSearch *search, SondTVFMItem *item,
		gboolean top, guint depth, GError **error) {
	GPtrArray *arr_children = NULL;
	GError *error_load = NULL;
	gboolean container_root = FALSE;
	gboolean check = FALSE;

	SondTVFMItemPrivate *priv = sond_tvfm_item_get_priv(item);

	if (g_atomic_int_get(&search->cancelled))
		return 0;

	if (depth > SEARCH_MAX_DEPTH) {
		LOG_WARN("Dateisuche: Verschachtelungstiefe > %d - übersprungen "
				"(Verzeichnisschleife?)", SEARCH_MAX_DEPTH);
		search->n_skipped++;

		return 0;
	}

	if (priv->type != SOND_TVFM_ITEM_TYPE_DIR
			&& priv->type != SOND_TVFM_ITEM_TYPE_LEAF)
		return 0;

	//Pseudo-Kinder PageTree/Message stehen für den Container selbst
	if (priv->is_content_root_marker)
		return 0;

	//Container (ZIP, PDF mit Anhängen, E-Mail mit Teilen) zählt als Datei
	container_root = (priv->type == SOND_TVFM_ITEM_TYPE_DIR
			&& priv->sond_file_part && !priv->path_or_section);
	check = (priv->type == SOND_TVFM_ITEM_TYPE_LEAF || container_root) ?
			search->opts.match_files : search->opts.match_dirs;

	if (check) {
		gchar const *name = sond_tvfm_item_get_display_name(item);

		if (!name)
			name = sond_tvfm_item_get_basename(item);
		if (name && search_name_matches(search, name))
			search_add_hit(search, item);
	}

	if (priv->type != SOND_TVFM_ITEM_TYPE_DIR || !priv->has_children)
		return 0;
	if (container_root && !search->opts.in_containers)
		return 0;

	//nicht geladener Container: nicht lesen, das löste sonst einen Download aus
	if (container_root && sond_tvfm_load_not_loaded(search->owner,
			priv->sond_file_part)) {
		search->n_not_loaded++;

		return 0;
	}

	if (sond_tvfm_item_load_children(item, &arr_children, NULL, &error_load)) {
		if (top) {
			g_propagate_error(error, error_load);

			return -1;
		}

		LOG_WARN("Dateisuche: Verzeichnis übersprungen: %s",
				error_load ? error_load->message : "?");
		g_clear_error(&error_load);
		search->n_skipped++;

		return 0;
	}

	for (guint i = 0; i < arr_children->len; i++)
		search_item(search, (SondTVFMItem*) g_ptr_array_index(arr_children, i),
				FALSE, depth + 1, NULL);

	g_ptr_array_unref(arr_children);

	return 0;
}

gint sond_tvfm_search_run(SondTVFMSearch *search, GPtrArray *start,
		GError **error) {
	g_return_val_if_fail(search && start, -1);

	for (guint i = 0; i < start->len; i++) {
		if (search_item(search, g_ptr_array_index(start, i), TRUE, 0, error))
			return -1;
	}

	return 0;
}
