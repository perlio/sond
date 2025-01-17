/* zond (zond_tree_store.h)
 * Copyright (C) 2021 peloamerica
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ZOND_TREE_STORE_H_INCLUDED
#define ZOND_TREE_STORE_H_INCLUDED

#include <gtk/gtk.h>
#include <stdarg.h>

G_BEGIN_DECLS

#define ZOND_TYPE_TREE_STORE                        (zond_tree_store_get_type ())
#define ZOND_TREE_STORE(obj)                        (G_TYPE_CHECK_INSTANCE_CAST ((obj), ZOND_TYPE_TREE_STORE, ZondTreeStore))
#define ZOND_TREE_STORE_CLASS(klass)                (G_TYPE_CHECK_CLASS_CAST ((klass), ZOND_TYPE_TREE_STORE, ZondTreeStoreClass))
#define ZOND_IS_TREE_STORE(obj)                        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), ZOND_TYPE_TREE_STORE))
#define ZOND_IS_TREE_STORE_CLASS(klass)                (G_TYPE_CHECK_CLASS_TYPE ((klass), ZOND_TYPE_TREE_STORE))
#define ZOND_TREE_STORE_GET_CLASS(obj)                (G_TYPE_INSTANCE_GET_CLASS ((obj), ZOND_TYPE_TREE_STORE, ZondTreeStoreClass))
typedef struct _ZondTreeStore ZondTreeStore;
typedef struct _ZondTreeStoreClass ZondTreeStoreClass;
typedef struct _ZondTreeStorePrivate ZondTreeStorePrivate;
struct _ZondTreeStore {
	GObject parent;
	ZondTreeStorePrivate *priv;
};
struct _ZondTreeStoreClass {
	GObjectClass parent_class;
	/* Padding for future expansion */
	void (*_gtk_reserved1)(void);
	void (*_gtk_reserved2)(void);
	void (*_gtk_reserved3)(void);
	void (*_gtk_reserved4)(void);
};
GDK_AVAILABLE_IN_ALL
GType zond_tree_store_get_type(void)
G_GNUC_CONST;

GDK_AVAILABLE_IN_ALL
void zond_tree_store_set(GtkTreeIter *iter, const gchar *icon_name,
		const gchar *node_text, const gint node_id);

GDK_AVAILABLE_IN_ALL
void zond_tree_store_remove(GtkTreeIter *iter);

GDK_AVAILABLE_IN_ALL
void zond_tree_store_insert(ZondTreeStore *tree_store, GtkTreeIter *iter,
		gboolean child, GtkTreeIter *iter_new);

GDK_AVAILABLE_IN_ALL
void zond_tree_store_insert_link(GtkTreeIter*, gint, ZondTreeStore*,
		GtkTreeIter*, gboolean, GtkTreeIter*);

gint
zond_tree_store_get_link_head_nr(GtkTreeIter*);

GDK_AVAILABLE_IN_ALL
void zond_tree_store_clear(ZondTreeStore *tree_store);

void zond_tree_store_copy_node(GtkTreeIter*, ZondTreeStore*, GtkTreeIter*,
		gboolean, GtkTreeIter*);

void zond_tree_store_move_node(GtkTreeIter*, ZondTreeStore*, GtkTreeIter*,
		gboolean, GtkTreeIter*);

gboolean
zond_tree_store_is_link(GtkTreeIter*);

void zond_tree_store_get_iter_target(GtkTreeIter*, GtkTreeIter*);

ZondTreeStore* zond_tree_store_get_tree_store(GtkTreeIter*);

void zond_tree_store_set_root(ZondTreeStore*, gint);

gint zond_tree_store_get_root(ZondTreeStore*);

GNode* zond_tree_store_get_root_node(ZondTreeStore*);

gint zond_tree_store_get_stamp(ZondTreeStore*);

GList* zond_tree_store_get_linked_nodes(GtkTreeIter*);

gboolean zond_tree_store_link_is_unloaded(GtkTreeIter*);

void zond_tree_store_load_link(GtkTreeIter*);

gint zond_tree_store_get_node_id(GtkTreeIter*);

void zond_tree_store_kill_parent(GtkTreeIter*);

G_END_DECLS
#endif /* ZOND_TREE_STORE_H_INCLUDED */
