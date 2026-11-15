/*
 sond (gtk4/sond_treeview.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_treeview.h"

typedef struct {
	GtkWidget *scrolled;
	GtkWidget *column_view;
	GtkMultiSelection *selection;
	GListModel *roots; //oberste Ebene (eigene Referenz)
	GMenu *menu;
	GtkWidget *popover;
	GSimpleActionGroup *actions;
} SondTreeviewPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondTreeview, sond_treeview, GTK_TYPE_BOX)

static void sond_treeview_dispose(GObject *object) {
	SondTreeview *self = SOND_TREEVIEW(object);
	SondTreeviewPrivate *priv = sond_treeview_get_instance_private(self);

	if (priv->popover) {
		gtk_widget_unparent(priv->popover);
		priv->popover = NULL;
	}
	g_clear_object(&priv->selection);
	g_clear_object(&priv->roots);
	g_clear_object(&priv->menu);
	g_clear_object(&priv->actions);

	G_OBJECT_CLASS(sond_treeview_parent_class)->dispose(object);
}

static void sond_treeview_class_init(SondTreeviewClass *klass) {
	G_OBJECT_CLASS(klass)->dispose = sond_treeview_dispose;
	klass->create_children = NULL;
}

static void sond_treeview_init(SondTreeview *self) {
	SondTreeviewPrivate *priv = sond_treeview_get_instance_private(self);

	gtk_orientable_set_orientation(GTK_ORIENTABLE(self), GTK_ORIENTATION_VERTICAL);

	priv->column_view = gtk_column_view_new(NULL);
	gtk_column_view_set_show_column_separators(GTK_COLUMN_VIEW(priv->column_view),
			FALSE);
	gtk_column_view_set_reorderable(GTK_COLUMN_VIEW(priv->column_view), FALSE);

	priv->scrolled = gtk_scrolled_window_new();
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(priv->scrolled),
			priv->column_view);
	gtk_widget_set_vexpand(priv->scrolled, TRUE);
	gtk_widget_set_hexpand(priv->scrolled, TRUE);
	gtk_box_append(GTK_BOX(self), priv->scrolled);

	//Kontextmenü: Modell und Aktionen gehören der Basisklasse, gefüllt wird in der Unterklasse
	priv->actions = g_simple_action_group_new();
	gtk_widget_insert_action_group(GTK_WIDGET(self), "tv",
			G_ACTION_GROUP(priv->actions));

	priv->menu = g_menu_new();
	priv->popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(priv->menu));
	gtk_popover_set_has_arrow(GTK_POPOVER(priv->popover), FALSE);
	gtk_widget_set_halign(priv->popover, GTK_ALIGN_START);
	gtk_widget_set_parent(priv->popover, GTK_WIDGET(self));
}

/* Kinder beim Aufklappen: an die Unterklasse weiterreichen */
static GListModel* tree_create_func(gpointer item, gpointer user_data) {
	SondTreeview *self = SOND_TREEVIEW(user_data);
	SondTreeviewClass *klass = SOND_TREEVIEW_GET_CLASS(self);

	if (!klass->create_children)
		return NULL;

	return klass->create_children(self, item);
}

void sond_treeview_set_roots(SondTreeview *self, GListModel *roots) {
	SondTreeviewPrivate *priv = NULL;
	GtkTreeListModel *tree_model = NULL;
	GtkSorter *row_sorter = NULL;
	GtkSortListModel *sort_model = NULL;

	g_return_if_fail(SOND_IS_TREEVIEW(self));
	g_return_if_fail(G_IS_LIST_MODEL(roots));

	priv = sond_treeview_get_instance_private(self);

	g_clear_object(&priv->roots);
	priv->roots = g_object_ref(roots);

	//passthrough FALSE: die Zeilen sind GtkTreeListRow
	tree_model = gtk_tree_list_model_new(roots, FALSE, FALSE, tree_create_func,
			self, NULL);

	//die Spaltensortierung gilt je Ebene
	row_sorter = GTK_SORTER(gtk_tree_list_row_sorter_new(
			g_object_ref(gtk_column_view_get_sorter(GTK_COLUMN_VIEW(priv->column_view)))));
	sort_model = gtk_sort_list_model_new(G_LIST_MODEL(tree_model), row_sorter);

	g_clear_object(&priv->selection);
	priv->selection = gtk_multi_selection_new(G_LIST_MODEL(sort_model));
	gtk_column_view_set_model(GTK_COLUMN_VIEW(priv->column_view),
			GTK_SELECTION_MODEL(priv->selection));
}

GtkColumnView* sond_treeview_get_column_view(SondTreeview *self) {
	SondTreeviewPrivate *priv = NULL;

	g_return_val_if_fail(SOND_IS_TREEVIEW(self), NULL);
	priv = sond_treeview_get_instance_private(self);

	return GTK_COLUMN_VIEW(priv->column_view);
}

GtkColumnViewColumn* sond_treeview_add_column(SondTreeview *self,
		const gchar *title, GtkListItemFactory *factory, GtkSorter *sorter,
		gboolean expand) {
	SondTreeviewPrivate *priv = NULL;
	GtkColumnViewColumn *column = NULL;

	g_return_val_if_fail(SOND_IS_TREEVIEW(self), NULL);
	priv = sond_treeview_get_instance_private(self);

	column = gtk_column_view_column_new(title, factory); //übernimmt die Factory
	gtk_column_view_column_set_resizable(column, TRUE);
	gtk_column_view_column_set_expand(column, expand);
	if (sorter) {
		gtk_column_view_column_set_sorter(column, sorter); //behält eine eigene Referenz
		g_object_unref(sorter);
	}
	gtk_column_view_append_column(GTK_COLUMN_VIEW(priv->column_view), column);
	g_object_unref(column); //die Ansicht hält sie

	return column;
}

gpointer sond_treeview_listitem_get_item(GtkListItem *listitem) {
	GtkTreeListRow *row = NULL;

	g_return_val_if_fail(GTK_IS_LIST_ITEM(listitem), NULL);

	row = gtk_list_item_get_item(listitem);
	if (!row)
		return NULL;

	return gtk_tree_list_row_get_item(row);
}

GMenu* sond_treeview_get_menu(SondTreeview *self) {
	SondTreeviewPrivate *priv = NULL;

	g_return_val_if_fail(SOND_IS_TREEVIEW(self), NULL);
	priv = sond_treeview_get_instance_private(self);

	return priv->menu;
}

GActionMap* sond_treeview_get_action_map(SondTreeview *self) {
	SondTreeviewPrivate *priv = NULL;

	g_return_val_if_fail(SOND_IS_TREEVIEW(self), NULL);
	priv = sond_treeview_get_instance_private(self);

	return G_ACTION_MAP(priv->actions);
}

static void context_pressed(GtkGestureClick *gesture, gint n_press, gdouble x,
		gdouble y, gpointer user_data) {
	GtkListItem *listitem = user_data;
	GtkWidget *cell = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
	GtkWidget *view = gtk_widget_get_ancestor(cell, SOND_TYPE_TREEVIEW);
	SondTreeviewPrivate *priv = NULL;
	guint position = gtk_list_item_get_position(listitem);
	graphene_point_t in = GRAPHENE_POINT_INIT((float) x, (float) y);
	graphene_point_t out;
	GdkRectangle rect = { 0 };

	(void) n_press;

	if (!view || position == GTK_INVALID_LIST_POSITION)
		return;

	priv = sond_treeview_get_instance_private(SOND_TREEVIEW(view));
	if (!priv->selection)
		return;

	//ein Rechtsklick auf eine nicht markierte Zeile markiert nur sie
	if (!gtk_selection_model_is_selected(GTK_SELECTION_MODEL(priv->selection),
			position))
		gtk_selection_model_select_item(GTK_SELECTION_MODEL(priv->selection),
				position, TRUE);

	if (!gtk_widget_compute_point(cell, view, &in, &out))
		out = in;
	rect.x = (int) out.x;
	rect.y = (int) out.y;
	rect.width = 1;
	rect.height = 1;
	gtk_popover_set_pointing_to(GTK_POPOVER(priv->popover), &rect);
	gtk_popover_popup(GTK_POPOVER(priv->popover));

	gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}

void sond_treeview_attach_context_menu(GtkWidget *cell, GtkListItem *listitem) {
	GtkGesture *gesture = NULL;

	g_return_if_fail(GTK_IS_WIDGET(cell));
	g_return_if_fail(GTK_IS_LIST_ITEM(listitem));

	gesture = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_SECONDARY);
	g_signal_connect(gesture, "pressed", G_CALLBACK(context_pressed), listitem);
	gtk_widget_add_controller(cell, GTK_EVENT_CONTROLLER(gesture));
}

GPtrArray* sond_treeview_get_selected_items(SondTreeview *self) {
	SondTreeviewPrivate *priv = NULL;
	GPtrArray *items = g_ptr_array_new_with_free_func(g_object_unref);
	GtkBitset *selected = NULL;
	GtkBitsetIter iter;
	guint position = 0;

	g_return_val_if_fail(SOND_IS_TREEVIEW(self), items);
	priv = sond_treeview_get_instance_private(self);

	if (!priv->selection)
		return items;

	selected = gtk_selection_model_get_selection(GTK_SELECTION_MODEL(priv->selection));
	for (gboolean ok = gtk_bitset_iter_init_first(&iter, selected, &position); ok;
			ok = gtk_bitset_iter_next(&iter, &position)) {
		GtkTreeListRow *row = g_list_model_get_item(G_LIST_MODEL(priv->selection),
				position);

		if (row) {
			gpointer item = gtk_tree_list_row_get_item(row);

			if (item)
				g_ptr_array_add(items, item);
			g_object_unref(row);
		}
	}
	gtk_bitset_unref(selected);

	return items;
}

GPtrArray* sond_treeview_get_selected_rows(SondTreeview *self) {
	SondTreeviewPrivate *priv = NULL;
	GPtrArray *rows = g_ptr_array_new_with_free_func(g_object_unref);
	GtkBitset *selected = NULL;
	GtkBitsetIter iter;
	guint position = 0;

	g_return_val_if_fail(SOND_IS_TREEVIEW(self), rows);
	priv = sond_treeview_get_instance_private(self);

	if (!priv->selection)
		return rows;

	selected = gtk_selection_model_get_selection(GTK_SELECTION_MODEL(priv->selection));
	for (gboolean ok = gtk_bitset_iter_init_first(&iter, selected, &position); ok;
			ok = gtk_bitset_iter_next(&iter, &position)) {
		GtkTreeListRow *row = g_list_model_get_item(G_LIST_MODEL(priv->selection),
				position);

		if (row)
			g_ptr_array_add(rows, row);
	}
	gtk_bitset_unref(selected);

	return rows;
}

GListModel* sond_treeview_get_roots(SondTreeview *self) {
	SondTreeviewPrivate *priv = NULL;

	g_return_val_if_fail(SOND_IS_TREEVIEW(self), NULL);
	priv = sond_treeview_get_instance_private(self);

	return priv->roots;
}

GListModel* sond_treeview_row_get_container(SondTreeview *self, GtkTreeListRow *row) {
	GtkTreeListRow *parent = NULL;
	GListModel *container = NULL;

	g_return_val_if_fail(SOND_IS_TREEVIEW(self), NULL);
	g_return_val_if_fail(GTK_IS_TREE_LIST_ROW(row), NULL);

	parent = gtk_tree_list_row_get_parent(row);
	if (!parent)
		return sond_treeview_get_roots(self);

	container = gtk_tree_list_row_get_children(parent);
	g_object_unref(parent);

	return container;
}

GtkTreeListRow* sond_treeview_find_row(SondTreeview *self,
		SondTreeviewMatchFunc match, gpointer user_data, guint *position) {
	SondTreeviewPrivate *priv = NULL;
	guint n = 0;

	g_return_val_if_fail(SOND_IS_TREEVIEW(self), NULL);
	priv = sond_treeview_get_instance_private(self);

	if (!priv->selection)
		return NULL;

	n = g_list_model_get_n_items(G_LIST_MODEL(priv->selection));
	for (guint i = 0; i < n; i++) {
		GtkTreeListRow *row = g_list_model_get_item(G_LIST_MODEL(priv->selection), i);
		gpointer item = row ? gtk_tree_list_row_get_item(row) : NULL;
		gboolean found = item && match(item, user_data);

		if (item)
			g_object_unref(item);
		if (found) {
			if (position)
				*position = i;

			return row;
		}
		if (row)
			g_object_unref(row);
	}

	return NULL;
}

gboolean sond_treeview_select_matching(SondTreeview *self,
		SondTreeviewMatchFunc match, gpointer user_data) {
	SondTreeviewPrivate *priv = NULL;
	GtkTreeListRow *row = NULL;
	guint position = 0;

	g_return_val_if_fail(SOND_IS_TREEVIEW(self), FALSE);
	priv = sond_treeview_get_instance_private(self);

	row = sond_treeview_find_row(self, match, user_data, &position);
	if (!row)
		return FALSE;
	g_object_unref(row);

	gtk_selection_model_select_item(GTK_SELECTION_MODEL(priv->selection), position, TRUE);
	gtk_column_view_scroll_to(GTK_COLUMN_VIEW(priv->column_view), position, NULL,
			GTK_LIST_SCROLL_FOCUS, NULL);

	return TRUE;
}

static gboolean match_same_item(gpointer item, gpointer user_data) {
	return item == user_data;
}

gboolean sond_treeview_select_item(SondTreeview *self, gpointer item) {
	return sond_treeview_select_matching(self, match_same_item, item);
}
