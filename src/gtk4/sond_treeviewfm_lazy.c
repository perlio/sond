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
static void lazy_ensure_loaded(SondLazyChildren *self) {
	GError *error = NULL;

	if (self->loaded)
		return;
	self->loaded = TRUE;

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

