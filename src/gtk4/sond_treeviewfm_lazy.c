/*
 sond (gtk4/sond_treeviewfm_lazy.c) - Akten, Beweisstücke, Unterlagen
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

/* Träges Kindermodell (Teil von SondTreeviewFM, s. sond_treeviewfm_private.h) */

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
#include "sond_seadrive_load_dialog.h"
#include "../tvfm/sond_tvfm_load.h"
#ifdef G_OS_WIN32
#include "../seadrive/sond_seadrive_watcher.h"
#endif

/* ------------------------------------------------------------------------
 * Kinder laden (träge)
 * --------------------------------------------------------------------- */

/* GtkTreeListModel ruft create_func schon, um zu prüfen, ob eine Zeile aufklappbar
 * ist, und wirft das Ergebnis dann weg. Die Kinder dürfen deshalb erst gelesen
 * werden, wenn jemand sie wirklich braucht (Aufklappen): diese Liste lädt beim
 * ersten Zugriff auf ihre Einträge. */
#ifdef G_OS_WIN32
typedef struct {
	SondLazyChildren *lazy;
	GWeakRef tree;
} LoadPending;

static void load_pending_free(gpointer data) {
	LoadPending *pending = (LoadPending*) data;

	g_weak_ref_clear(&pending->tree);
	g_object_unref(pending->lazy);
	g_free(pending);
}

/* Der Container (ZIP, E-Mail, PDF) ist ein nicht geladener SeaDrive-Platzhalter: ihn zu lesen
 * löste sonst den Download-Dialog des Betriebssystems aus. Stattdessen mit unserem
 * Fortschrittsfenster laden (mit Abbrechen) und erst danach die Kinder einlesen. Das geschieht
 * nicht mitten im Zugriff auf das Listenmodell (eine verschachtelte Hauptschleife dort wäre
 * unsicher), sondern danach: bis dahin ist die Liste leer. Bei Abbruch klappt die Zeile wieder zu. */
static gboolean lazy_load_idle(gpointer data) {
	LoadPending *pending = (LoadPending*) data;
	SondLazyChildren *self = pending->lazy;
	SondTreeviewFM *tree = g_weak_ref_get(&pending->tree);
	SondTVFMItemPrivate *priv = sond_tvfm_item_get_priv(self->item);
	gboolean ok = FALSE;

	if (!tree)
		return G_SOURCE_REMOVE;

	{
		gchar *path = sond_tvfm_load_root_file_path(SOND_TVFM_OWNER(tree),
				priv->sond_file_part);

		ok = sond_seadrive_load_file_sync(fm_parent_window(tree), path);
		g_free(path);
	}

	if (ok) {
		GPtrArray *children = NULL;
		GError *error = NULL;

		if (sond_tvfm_item_load_children(self->item, &children, NULL, &error)) {
			LOG_WARN("Kinder von '%s' nicht geladen: %s",
					sond_tvfm_item_get_display_name(self->item) ?
							sond_tvfm_item_get_display_name(self->item) : "?",
					error ? error->message : "?");
			g_clear_error(&error);
		} else {
			g_ptr_array_unref(self->children);
			self->children = children;
			g_list_model_items_changed(G_LIST_MODEL(self), 0, 0, children->len);
		}
	} else {
		GtkTreeListRow *row = sond_treeview_find_row(SOND_TREEVIEW(tree), fm_match_same,
				self->item, NULL);

		if (row) {
			gtk_tree_list_row_set_expanded(row, FALSE);
			g_object_unref(row);
		}
	}

	g_object_unref(tree);

	return G_SOURCE_REMOVE;
}

//TRUE, wenn das Laden der Kinder zurückgestellt wurde (s. lazy_load_idle)
static gboolean lazy_defer_load(SondLazyChildren *self) {
	SondTVFMItemPrivate *priv = sond_tvfm_item_get_priv(self->item);
	LoadPending *pending = NULL;

	if (!priv->sond_file_part || priv->path_or_section || !priv->owner ||
			!sond_tvfm_load_not_loaded(priv->owner, priv->sond_file_part))
		return FALSE;

	pending = g_new0(LoadPending, 1);
	pending->lazy = g_object_ref(self);
	g_weak_ref_init(&pending->tree, priv->owner);
	self->children = g_ptr_array_new_with_free_func(g_object_unref);
	g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, lazy_load_idle, pending, load_pending_free);

	return TRUE;
}
#endif

static void lazy_ensure_loaded(SondLazyChildren *self) {
	GError *error = NULL;

	if (self->loaded)
		return;
	self->loaded = TRUE;

#ifdef G_OS_WIN32
	if (lazy_defer_load(self))
		return;
#endif

	if (sond_tvfm_item_load_children(self->item, &self->children, NULL, &error)) {
		LOG_WARN("Kinder von '%s' nicht geladen: %s\n",
				sond_tvfm_item_get_display_name(self->item) ?
						sond_tvfm_item_get_display_name(self->item) : "?",
				error ? error->message : "?");
		g_clear_error(&error);
		self->children = g_ptr_array_new_with_free_func(g_object_unref);
	}
}

static GType lazy_get_item_type(GListModel *list) {
	(void) list;

	return SOND_TYPE_TVFM_ITEM;
}

static guint lazy_get_n_items(GListModel *list) {
	SondLazyChildren *self = SOND_LAZY_CHILDREN(list);

	lazy_ensure_loaded(self);

	return self->children ? self->children->len : 0;
}

static gpointer lazy_get_item(GListModel *list, guint position) {
	SondLazyChildren *self = SOND_LAZY_CHILDREN(list);

	lazy_ensure_loaded(self);
	if (!self->children || position >= self->children->len)
		return NULL;

	return g_object_ref(g_ptr_array_index(self->children, position));
}

static void lazy_list_model_init(GListModelInterface *iface) {
	iface->get_item_type = lazy_get_item_type;
	iface->get_n_items = lazy_get_n_items;
	iface->get_item = lazy_get_item;
}

G_DEFINE_TYPE_WITH_CODE(SondLazyChildren, sond_lazy_children, G_TYPE_OBJECT,
		G_IMPLEMENT_INTERFACE(G_TYPE_LIST_MODEL, lazy_list_model_init))

static void lazy_finalize(GObject *object) {
	SondLazyChildren *self = SOND_LAZY_CHILDREN(object);

	g_clear_object(&self->item);
	g_clear_pointer(&self->children, g_ptr_array_unref);

	G_OBJECT_CLASS(sond_lazy_children_parent_class)->finalize(object);
}

static void sond_lazy_children_class_init(SondLazyChildrenClass *klass) {
	G_OBJECT_CLASS(klass)->finalize = lazy_finalize;
}

static void sond_lazy_children_init(SondLazyChildren *self) {
	(void) self;
}


/* Änderungen an einer Kinderliste (nach Anlegen, Umbenennen, Löschen); sie melden sich
 * über items-changed, der Baum zeigt sie an */
SondLazyChildren* fm_lazy_from_model(GListModel *model) {
	return (model && SOND_IS_LAZY_CHILDREN(model)) ? SOND_LAZY_CHILDREN(model) : NULL;
}

//fertig geladene Liste (Wurzel); Eigentum des Arrays geht an die Liste
SondLazyChildren* fm_lazy_new_loaded(GPtrArray *items) {
	SondLazyChildren *lazy = g_object_new(SOND_TYPE_LAZY_CHILDREN, NULL);

	lazy->children = items;
	lazy->loaded = TRUE;

	return lazy;
}

static gint lazy_index_of(SondLazyChildren *self, SondTVFMItem *item) {
	if (!self->children)
		return -1;

	for (guint i = 0; i < self->children->len; i++)
		if (g_ptr_array_index(self->children, i) == item)
			return (gint) i;

	return -1;
}

void fm_lazy_insert(SondLazyChildren *self, SondTVFMItem *item) {
	lazy_ensure_loaded(self);
	if (!self->children)
		self->children = g_ptr_array_new_with_free_func(g_object_unref);

	g_ptr_array_add(self->children, g_object_ref(item));
	g_list_model_items_changed(G_LIST_MODEL(self), self->children->len - 1, 0, 1);
}

//Position des Eintrags in der Liste (lädt sie ggf.); -1, wenn er nicht darin steht
gint fm_lazy_index_of(SondLazyChildren *self, SondTVFMItem *item) {
	lazy_ensure_loaded(self);

	return lazy_index_of(self, item);
}

//Fügt den Eintrag an der Position index ein (negativ oder zu groß: am Ende)
void fm_lazy_insert_at(SondLazyChildren *self, gint index, SondTVFMItem *item) {
	lazy_ensure_loaded(self);
	if (!self->children)
		self->children = g_ptr_array_new_with_free_func(g_object_unref);

	if (index < 0 || (guint) index > self->children->len)
		index = (gint) self->children->len;

	g_ptr_array_insert(self->children, index, g_object_ref(item));
	g_list_model_items_changed(G_LIST_MODEL(self), (guint) index, 0, 1);
}

void fm_lazy_remove(SondLazyChildren *self, SondTVFMItem *item) {
	gint index = lazy_index_of(self, item);

	if (index < 0)
		return;

	g_ptr_array_remove_index(self->children, (guint) index);
	g_list_model_items_changed(G_LIST_MODEL(self), (guint) index, 1, 0);
}

//ersetzt den Eintrag durch einen anderen (z.B. Platzhalter durch Container)
void fm_lazy_replace(SondLazyChildren *self, SondTVFMItem *old_item,
		SondTVFMItem *new_item) {
	gint index = lazy_index_of(self, old_item);
	gpointer old_ref = NULL;

	if (index < 0)
		return;

	old_ref = g_ptr_array_index(self->children, index);
	g_ptr_array_index(self->children, index) = g_object_ref(new_item);
	g_list_model_items_changed(G_LIST_MODEL(self), (guint) index, 1, 1);
	g_object_unref(old_ref);
}

//Eintrag hat sich geändert (Name, Größe, ...): Zeile neu aufbauen und neu einsortieren
void fm_lazy_touch(SondLazyChildren *self, SondTVFMItem *item) {
	gint index = lazy_index_of(self, item);

	if (index >= 0)
		g_list_model_items_changed(G_LIST_MODEL(self), (guint) index, 1, 1);
}

GListModel* fm_create_children(SondTreeview *base, gpointer object) {
	SondTVFMItem *item = SOND_TVFM_ITEM(object);
	SondTVFMItemPrivate *item_priv = sond_tvfm_item_get_priv(item);
	SondLazyChildren *lazy = NULL;

	(void) base;

	if (item_priv->type != SOND_TVFM_ITEM_TYPE_DIR || !item_priv->has_children)
		return NULL;

	//keine Ein-/Ausgabe hier, die Kinder werden erst beim Aufklappen gelesen
	lazy = g_object_new(SOND_TYPE_LAZY_CHILDREN, NULL);
	lazy->item = g_object_ref(item);

	return G_LIST_MODEL(lazy);
}

