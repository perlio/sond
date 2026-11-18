/*
 sond (gtk4/sond_treeviewfm_watch.c) - Akten, Beweisstücke, Unterlagen
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

/* Root-Monitor und SeaDrive-Watcher (Teil von SondTreeviewFM, s. sond_treeviewfm_private.h) */

#include "sond_treeviewfm_private.h"

#include <string.h>
#ifdef G_OS_WIN32
#include <windows.h>
#endif

#include "../sond_log_and_error.h"
#include "../sond_fileparts.h"
#include "../sond_file_helper.h"
#include "../tvfm/sond_tvfm_item.h"
#include "../tvfm/sond_tvfm_item_private.h"
#include "../tvfm/sond_tvfm_owner.h"
#include "../seadrive/sond_seadrive_core.h"
#include "../sond_icon_util.h"
#include "../tvfm/sond_tvfm_status.h"
#include "../sond_mime.h"
#ifdef G_OS_WIN32
#include "../seadrive/sond_seadrive_watcher.h"
#endif

/* --- Zugänge von außen auf der obersten Ebene --- */

/* Fügt Einträge, die auf der obersten Ebene von außen hinzugekommen sind, in den Baum
 * ein. Idempotent: vorhandene Zeilen bleiben unberührt. Löschen oder Umbenennen von
 * außen wird bewusst nicht nachgeführt. */
static void root_add_new(SondTreeviewFM *self) {
	SondTreeviewFMPrivate *priv = fm_get_priv(self);
	SondLazyChildren *lazy = fm_lazy_from_model(sond_treeview_get_roots(SOND_TREEVIEW(self)));
	SondTVFMItem *item_root = NULL;
	GPtrArray *children = NULL;
	GHashTable *known = NULL;
	GError *error = NULL;

	if (!priv->root || !lazy)
		return;

	item_root = sond_tvfm_item_create(self, NULL, NULL);
	if (sond_tvfm_item_load_children(item_root, &children, NULL, &error)) {
		LOG_WARN("Wurzelverzeichnis konnte nicht neu gelesen werden: %s",
				error ? error->message : "?");
		g_clear_error(&error);
		g_object_unref(item_root);

		return;
	}

	known = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	for (guint i = 0; lazy->children && i < lazy->children->len; i++) {
		gchar const *name = sond_tvfm_item_get_basename(g_ptr_array_index(lazy->children, i));

		if (name)
			g_hash_table_add(known, g_strdup(name));
	}

	for (guint i = 0; i < children->len; i++) {
		SondTVFMItem *child = g_ptr_array_index(children, i);
		gchar const *name = sond_tvfm_item_get_basename(child);

		if (name && !g_hash_table_contains(known, name))
			fm_lazy_insert(lazy, child);
	}

	g_hash_table_unref(known);
	g_ptr_array_unref(children);
	g_object_unref(item_root);
}

static gboolean root_monitor_timeout(gpointer data) {
	SondTreeviewFM *self = SOND_TREEVIEWFM(data);
	SondTreeviewFMPrivate *priv = fm_get_priv(self);

	priv->root_monitor_timer = 0;
	root_add_new(self);

	return G_SOURCE_REMOVE;
}

static void root_monitor_changed(GFileMonitor *monitor, GFile *file, GFile *other,
		GFileMonitorEvent event, gpointer data) {
	SondTreeviewFM *self = SOND_TREEVIEWFM(data);
	SondTreeviewFMPrivate *priv = fm_get_priv(self);

	(void) monitor;
	(void) file;
	(void) other;

	switch (event) {
	case G_FILE_MONITOR_EVENT_CREATED:
	case G_FILE_MONITOR_EVENT_MOVED_IN:
	case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
		break;
	case G_FILE_MONITOR_EVENT_CHANGED:
		//Datei wird noch geschrieben: nur weiter warten, wenn etwas ansteht
		if (!priv->root_monitor_timer)
			return;
		break;
	default:
		return;
	}

	if (priv->root_monitor_timer)
		g_source_remove(priv->root_monitor_timer);
	priv->root_monitor_timer = g_timeout_add(500, root_monitor_timeout, self);
}

void fm_root_monitor_stop(SondTreeviewFMPrivate *priv) {
	if (priv->root_monitor_timer) {
		g_source_remove(priv->root_monitor_timer);
		priv->root_monitor_timer = 0;
	}

	if (priv->root_monitor) {
		g_file_monitor_cancel(priv->root_monitor);
		g_object_unref(priv->root_monitor);
		priv->root_monitor = NULL;
	}
}

void fm_root_monitor_start(SondTreeviewFM *self) {
	SondTreeviewFMPrivate *priv = fm_get_priv(self);
	GFile *file_root = NULL;
	GError *error = NULL;

	file_root = g_file_new_for_path(priv->root);
	priv->root_monitor = g_file_monitor_directory(file_root, G_FILE_MONITOR_WATCH_MOVES,
			NULL, &error);
	g_object_unref(file_root);

	if (!priv->root_monitor) {
		LOG_WARN("Wurzelverzeichnis kann nicht überwacht werden: %s",
				error ? error->message : "?");
		g_clear_error(&error);

		return;
	}

	g_signal_connect(priv->root_monitor, "changed", G_CALLBACK(root_monitor_changed), self);
}

static gboolean match_fs_rel(gpointer item, gpointer user_data) {
	gchar *rel = NULL;
	gboolean match = FALSE;

	if (sond_tvfm_item_fs_path(SOND_TVFM_ITEM(item), &rel, NULL))
		match = !g_strcmp0(rel, (gchar const*) user_data);
	g_free(rel);

	return match;
}

//Ersetzt die Zeile des Eintrags bei rel durch new_item; die Zeile muss sichtbar sein
static void replace_row_item(SondTreeviewFM *self, SondTVFMItem *old_item,
		SondTVFMItem *new_item, gchar const *rel) {
	SondTreeview *base = SOND_TREEVIEW(self);
	GtkTreeListRow *row = sond_treeview_find_row(base, match_fs_rel, (gpointer) rel,
			NULL);
	SondLazyChildren *lazy = NULL;

	if (!row)
		return;

	lazy = fm_lazy_from_model(sond_treeview_row_get_container(base, row));
	if (lazy)
		fm_lazy_replace(lazy, old_item, new_item);
	g_object_unref(row);
}

/* Ersetzt die sichtbare Zeile von old_item (Datei im Dateisystem oder Eintrag in einem
 * Container) durch new_item, z.B. nach dem Entsperren eines Platzhalters. Ist die Zeile nicht
 * mehr zu finden, geschieht nichts. */
void fm_replace_item(SondTreeviewFM *self, SondTVFMItem *old_item, SondTVFMItem *new_item) {
	SondTreeview *base = SOND_TREEVIEW(self);
	GtkTreeListRow *row = sond_treeview_find_row(base, fm_match_same, old_item, NULL);
	SondLazyChildren *lazy = NULL;

	if (!row)
		return;

	lazy = fm_lazy_from_model(sond_treeview_row_get_container(base, row));
	if (lazy)
		fm_lazy_replace(lazy, old_item, new_item);
	g_object_unref(row);
}

/* Der Eintrag einer Datei des Dateisystems hat sich geändert (z.B. Passwortschutz entfernt,
 * jetzt mit bekanntem Typ und Anhängen): die sichtbare Zeile durch einen neuen Eintrag aus
 * sfp ersetzen. Unsichtbare Zeilen werden beim Aufklappen richtig geladen. */
void sond_treeviewfm_refresh_file_part(SondTreeviewFM *self, SondFilePart *sfp) {
	SondTreeview *base = NULL;
	gchar const *rel = NULL;
	GtkTreeListRow *row = NULL;
	SondTVFMItem *old_item = NULL;
	SondTVFMItem *new_item = NULL;

	g_return_if_fail(SOND_IS_TREEVIEWFM(self));
	base = SOND_TREEVIEW(self);

	rel = sond_file_part_get_path(sfp);
	if (!rel || sond_file_part_get_parent(sfp))
		return; //nur Dateien im Dateisystem

	row = sond_treeview_find_row(base, match_fs_rel, (gpointer) rel, NULL);
	if (!row)
		return;

	old_item = gtk_tree_list_row_get_item(row);
	g_object_unref(row);
	if (!old_item)
		return;

	new_item = sond_tvfm_item_create(self, sfp, NULL);
	replace_row_item(self, old_item, new_item, rel);
	g_object_unref(new_item);
	g_object_unref(old_item);
}

#ifdef G_OS_WIN32
/* --- Anbindung des Watchers (sond_seadrive_watcher.c) --- */

static gboolean watcher_refresh_idle(gpointer data) {
	SondTreeviewFM *self = SOND_TREEVIEWFM(data);
	SondTreeviewFMPrivate *priv = fm_get_priv(self);

	priv->refresh_idle = 0;
	g_signal_emit(self, fm_signals[SIGNAL_PINS_CHANGED], 0);

	return G_SOURCE_REMOVE;
}

//Bei vielen Ereignissen hintereinander die Symbole nur einmal auffrischen
static void watcher_changed_cb(SondSeadriveWatcher *w, gpointer data) {
	SondTreeviewFM *self = SOND_TREEVIEWFM(data);
	SondTreeviewFMPrivate *priv = fm_get_priv(self);

	(void) w;

	if (!priv->refresh_idle)
		priv->refresh_idle = g_idle_add_full(G_PRIORITY_LOW, watcher_refresh_idle,
				g_object_ref(self), g_object_unref);
}

static void watcher_status_cb(SondSeadriveWatcher *w, guint pending_down,
		guint pending_up, gpointer data) {
	(void) w;

	g_signal_emit(SOND_TREEVIEWFM(data), fm_signals[SIGNAL_SEADRIVE_STATUS], 0,
			pending_down, pending_up);
}

//Pfad relativ zur Wurzel (Zeiger in full_path) oder NULL
static gchar const* rel_path_of(SondTreeviewFMPrivate *priv, gchar const *full_path) {
	gsize len = priv->root ? strlen(priv->root) : 0;

	if (!len || !g_str_has_prefix(full_path, priv->root) || full_path[len] != '/' ||
			!full_path[len + 1])
		return NULL;

	return full_path + len + 1;
}

/* Eine Datei wurde geladen: war sie beim Einlesen ein einfaches Blatt (offline, nicht
 * auf Kinder geprüft), jetzt durch den richtigen Typ (PDF, ZIP, E-Mail) ersetzen */
static void watcher_hydrated_cb(SondSeadriveWatcher *w, gchar const *path, gpointer data) {
	SondTreeviewFM *self = SOND_TREEVIEWFM(data);
	SondTreeviewFMPrivate *priv = fm_get_priv(self);
	SondTreeview *base = SOND_TREEVIEW(self);
	gchar const *rel = rel_path_of(priv, path);
	GtkTreeListRow *row = NULL;
	SondTVFMItem *item = NULL;
	SondTVFMItem *item_new = NULL;
	SondFilePart *sfp_old = NULL;
	SondFilePart *sfp_new = NULL;
	GPtrArray *arr = NULL;
	GError *error = NULL;

	(void) w;

	if (!rel)
		return;

	row = sond_treeview_find_row(base, match_fs_rel, (gpointer) rel, NULL);
	if (!row)
		return; //nicht sichtbar: beim Aufklappen wird richtig geladen

	item = gtk_tree_list_row_get_item(row);
	g_object_unref(row);
	if (!item)
		return;

	sfp_old = sond_tvfm_item_get_sond_file_part(item);
	if (sond_tvfm_item_get_item_type(item) != SOND_TVFM_ITEM_TYPE_LEAF ||
			!SOND_IS_FILE_PART_LEAF(sfp_old)) {
		g_object_unref(item);

		return;
	}

	//das alte Blatt aus der Liste geöffneter Dateien nehmen, sonst gibt create() es zurück
	arr = sond_file_part_get_arr_opened_files(sond_file_part_get_parent(sfp_old));
	if (arr)
		g_ptr_array_remove_fast(arr, sfp_old);

	sfp_new = sond_file_part_create(sond_file_part_get_parent(sfp_old), rel, &error);
	if (!sfp_new) {
		LOG_WARN("SondFilePart kann nicht geöffnet werden:\n%s",
				error ? error->message : "?");
		g_clear_error(&error);
		g_object_unref(item);

		return;
	}

	item_new = sond_tvfm_item_create(self, sfp_new, NULL);
	g_object_unref(sfp_new);
	replace_row_item(self, item, item_new, rel);
	g_object_unref(item_new);
	g_object_unref(item);
}

/* Eine Datei wurde aus dem lokalen Speicher genommen: eine PDF, die als Container
 * (mit Seiten und Anhängen) angezeigt wird, wieder durch ein Blatt ersetzen. ZIP und
 * E-Mail bleiben Container, das Aufklappen lädt sie bei Bedarf neu. */
static void watcher_dehydrated_cb(SondSeadriveWatcher *w, gchar const *path,
		gpointer data) {
	SondTreeviewFM *self = SOND_TREEVIEWFM(data);
	SondTreeviewFMPrivate *priv = fm_get_priv(self);
	SondTreeview *base = SOND_TREEVIEW(self);
	gchar const *rel = rel_path_of(priv, path);
	GtkTreeListRow *row = NULL;
	SondTVFMItem *item = NULL;
	SondTVFMItem *item_new = NULL;
	SondFilePart *sfp_old = NULL;
	SondFilePart *sfp_new = NULL;
	GPtrArray *arr = NULL;
	gchar const *mime = NULL;

	(void) w;

	if (!rel)
		return;

	row = sond_treeview_find_row(base, match_fs_rel, (gpointer) rel, NULL);
	if (!row)
		return;

	item = gtk_tree_list_row_get_item(row);
	g_object_unref(row);
	if (!item)
		return;

	sfp_old = sond_tvfm_item_get_sond_file_part(item);
	if (sond_tvfm_item_get_item_type(item) != SOND_TVFM_ITEM_TYPE_DIR || !sfp_old ||
			sond_tvfm_item_get_path_or_section(item) ||
			!SOND_IS_FILE_PART_PDF(sfp_old)) {
		g_object_unref(item);

		return;
	}

	arr = sond_file_part_get_arr_opened_files(sond_file_part_get_parent(sfp_old));
	if (arr)
		g_ptr_array_remove_fast(arr, sfp_old);

	mime = mime_from_extension(rel);
	sfp_new = sond_file_part_create_leaf(rel, sond_file_part_get_parent(sfp_old),
			mime ? mime : "application/octet-stream");
	if (!sfp_new) {
		g_object_unref(item);

		return;
	}

	item_new = sond_tvfm_item_create(self, sfp_new, NULL);
	g_object_unref(sfp_new);
	replace_row_item(self, item, item_new, rel);
	g_object_unref(item_new);
	g_object_unref(item);
}

void fm_watcher_start(SondTreeviewFM *self) {
	SondTreeviewFMPrivate *priv = fm_get_priv(self);
	SondSeadriveWatcher *w = NULL;

	if (priv->watcher || !priv->root || !priv->is_seadrive)
		return;

	w = sond_seadrive_watcher_new(priv->root);
	g_signal_connect(w, "changed", G_CALLBACK(watcher_changed_cb), self);
	g_signal_connect(w, "status-changed", G_CALLBACK(watcher_status_cb), self);
	g_signal_connect(w, "hydrated", G_CALLBACK(watcher_hydrated_cb), self);
	g_signal_connect(w, "dehydrated", G_CALLBACK(watcher_dehydrated_cb), self);
	priv->watcher = w;
	sond_seadrive_watcher_start(w);
}

/* Löst den Watcher vom Widget; danach kommen keine Signale mehr. Das Warten auf den
 * Thread (bis ca. 20 s) übernimmt der Reaper des Watchers; er berührt das Widget nicht. */
void fm_watcher_stop(SondTreeviewFM *self) {
	SondTreeviewFMPrivate *priv = fm_get_priv(self);
	SondSeadriveWatcher *w = priv->watcher;

	if (!w)
		return;

	priv->watcher = NULL;
	g_signal_handlers_disconnect_by_data(w, self);
	sond_seadrive_watcher_stop_async(w);
	g_object_unref(w);
	g_signal_emit(self, fm_signals[SIGNAL_SEADRIVE_STATUS], 0, (guint) 0, (guint) 0);
}
#endif

gboolean sond_treeviewfm_get_pin_summary(SondTreeviewFM *self, guint *total, guint *pinned) {
#ifdef G_OS_WIN32
	SondTreeviewFMPrivate *priv = NULL;

	g_return_val_if_fail(SOND_IS_TREEVIEWFM(self), FALSE);
	priv = fm_get_priv(self);

	return priv->watcher && sond_seadrive_watcher_get_pin_summary(priv->watcher, total, pinned);
#else
	(void) self;
	(void) total;
	(void) pinned;

	return FALSE;
#endif
}
