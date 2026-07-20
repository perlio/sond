/*
 sond (sond_treeview.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2021  pelo america

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
#include "sond_log_and_error.h"

#include "misc.h"

typedef struct {
	GtkCellRenderer *renderer_icon;
	GtkCellRenderer *renderer_text;
	gint id;
	GtkWidget *contextmenu;       /* GtkPopoverMenu (GTK3 + GTK4) */
	GSimpleActionGroup *action_group; /* instanzspezifische Aktionsgruppe */
	GtkEventController *key_controller;
} SondTreeviewPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondTreeview, sond_treeview, GTK_TYPE_TREE_VIEW)

/* --------------------------------------------------------------------------
 * Popup-Menu
 * -------------------------------------------------------------------------- */
static void sond_treeview_show_popupmenu(SondTreeview *stv, gdouble x,
		gdouble y) {
	SondTreeviewPrivate *stv_priv = sond_treeview_get_instance_private(stv);

	if (!stv_priv->contextmenu)
		return;

	GdkRectangle rect = { (gint)x, (gint)y, 1, 1 };
	gtk_popover_set_pointing_to(GTK_POPOVER(stv_priv->contextmenu), &rect);
	gtk_popover_popup(GTK_POPOVER(stv_priv->contextmenu));
}

static void sond_treeview_gesture_pressed(GtkGestureMultiPress *gesture,
		gint n_press, gdouble x, gdouble y, gpointer data) {
	SondTreeview *stv = SOND_TREEVIEW(data);

	if (gtk_gesture_single_get_current_button(
			GTK_GESTURE_SINGLE(gesture)) != GDK_BUTTON_SECONDARY)
		return;

	gtk_gesture_set_sequence_state(GTK_GESTURE(gesture),
			gtk_gesture_single_get_current_sequence(GTK_GESTURE_SINGLE(gesture)),
			GTK_EVENT_SEQUENCE_CLAIMED);

	// Gesture-Koordinaten sind Widget-Koordinaten - in Bin-Window-Koordinaten
	// umrechnen, damit Header-Hoehe (bei ZondTreeviewFM) korrekt beruecksichtigt wird
	gint bx = 0, by = 0;
	gtk_tree_view_convert_widget_to_bin_window_coords(
			GTK_TREE_VIEW(stv), (gint)x, (gint)y, &bx, &by);

	GtkTreePath *path = NULL;
	gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(stv), bx, by,
			&path, NULL, NULL, NULL);

	if (!path) {
		GtkTreeIter iter = { 0 };
		if (gtk_tree_model_get_iter_first(
				gtk_tree_view_get_model(GTK_TREE_VIEW(stv)), &iter))
			return;
		if (!gtk_widget_has_focus(GTK_WIDGET(stv)))
			gtk_widget_grab_focus(GTK_WIDGET(stv));
	} else {
		GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(stv));

		if (!gtk_widget_has_focus(GTK_WIDGET(stv)))
			gtk_widget_grab_focus(GTK_WIDGET(stv));

		if (!gtk_tree_selection_path_is_selected(selection, path))
			gtk_tree_view_set_cursor(GTK_TREE_VIEW(stv), path, NULL, FALSE);

		gtk_tree_path_free(path);
	}

	sond_treeview_show_popupmenu(stv, x, y);
}

/* --------------------------------------------------------------------------
 * finalize
 * -------------------------------------------------------------------------- */
static void sond_treeview_finalize(GObject *object) {
	SondTreeviewPrivate *stv_priv = sond_treeview_get_instance_private(
			SOND_TREEVIEW(object));

	if (stv_priv->contextmenu) {
		gtk_widget_destroy(stv_priv->contextmenu);
		stv_priv->contextmenu = NULL;
	}

	g_clear_object(&stv_priv->action_group);

	G_OBJECT_CLASS(sond_treeview_parent_class)->finalize(object);
}

/* --------------------------------------------------------------------------
 * Oeffentliche Hilfsfunktion: Basis-Section (Kopieren/Ausschneiden) anhaengen.
 * Wird von abgeleiteten Klassen in deren class_init aufgerufen,
 * nachdem sie ein eigenes GMenu angelegt haben.
 * -------------------------------------------------------------------------- */
void sond_treeview_add_base_menu(GMenu *gmenu) {
	GMenu *sec_base = g_menu_new();
	g_menu_append(sec_base, "Kopieren",     "stv.kopieren");
	g_menu_append(sec_base, "Ausschneiden", "stv.ausschneiden");
	g_menu_append_section(gmenu, NULL, G_MENU_MODEL(sec_base));
	g_object_unref(sec_base);
}

/* --------------------------------------------------------------------------
 * class_init / constructed
 * -------------------------------------------------------------------------- */
static void sond_treeview_constructed(GObject *object) {
	SondTreeview *stv = SOND_TREEVIEW(object);
	SondTreeviewPrivate *stv_priv = sond_treeview_get_instance_private(stv);
	GMenu *gmenu = SOND_TREEVIEW_GET_CLASS(stv)->gmenu;

	/* Aktionsgruppe mit Präfix "stv" am Widget registrieren */
	gtk_widget_insert_action_group(GTK_WIDGET(stv), "stv",
			G_ACTION_GROUP(stv_priv->action_group));

	stv_priv->contextmenu = gtk_popover_menu_new();
	gtk_popover_set_relative_to(GTK_POPOVER(stv_priv->contextmenu),
			GTK_WIDGET(stv));
	gtk_popover_bind_model(GTK_POPOVER(stv_priv->contextmenu),
			G_MENU_MODEL(gmenu), NULL);

	GtkGesture *gesture = gtk_gesture_multi_press_new(GTK_WIDGET(stv));
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture),
			GDK_BUTTON_SECONDARY);
	/* CAPTURE-Phase: muss vor dem internen GtkTreeView-Button-Handling
	 * feuern, sonst hat GtkTreeView die Selektion bereits auf die
	 * angeklickte Zeile reduziert, bevor wir reagieren koennen. */
	gtk_event_controller_set_propagation_phase(
			GTK_EVENT_CONTROLLER(gesture), GTK_PHASE_CAPTURE);
	g_signal_connect(gesture, "pressed",
			G_CALLBACK(sond_treeview_gesture_pressed), stv);

	G_OBJECT_CLASS(sond_treeview_parent_class)->constructed(object);

	return;
}

static void sond_treeview_class_init(SondTreeviewClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	klass->clipboard = g_malloc0(sizeof(Clipboard));
	klass->clipboard->arr_ref = g_ptr_array_new_with_free_func(
			(GDestroyNotify) gtk_tree_row_reference_free);

	/* Eigenes GMenu fuer SondTreeview-Instanzen (Fallback) */
	klass->gmenu = g_menu_new();
	sond_treeview_add_base_menu(klass->gmenu);

	klass->render_text_cell = NULL;
	klass->text_edited = NULL;

	object_class->finalize = sond_treeview_finalize;
	object_class->constructed = sond_treeview_constructed;

	return;
}

/* --------------------------------------------------------------------------
 * CellRenderer-Callbacks
 * -------------------------------------------------------------------------- */
static void renderer_text_editing_canceled(GtkCellRenderer *renderer,
		gpointer data) {
	SondTreeview *stv = (SondTreeview*) data;
	SondTreeviewPrivate *stv_priv = sond_treeview_get_instance_private(stv);

	gtk_event_controller_set_propagation_phase(
			stv_priv->key_controller, GTK_PHASE_BUBBLE);

	return;
}

static void sond_treeview_text_edited(GtkCellRenderer *cell,
		gchar *path_string, gchar *new_text, gpointer data) {
	GtkTreeIter iter = { 0 };

	SondTreeview *stv = (SondTreeview*) data;
	SondTreeviewPrivate *stv_priv = sond_treeview_get_instance_private(stv);
	SondTreeviewClass *klass = SOND_TREEVIEW_GET_CLASS(stv);

	gtk_tree_model_get_iter_from_string(
			gtk_tree_view_get_model(GTK_TREE_VIEW(stv)), &iter, path_string);

	if (klass->text_edited)
		klass->text_edited(stv, &iter, new_text);

	gtk_event_controller_set_propagation_phase(
			stv_priv->key_controller, GTK_PHASE_BUBBLE);

	return;
}

static void renderer_text_editing_started(GtkCellRenderer *renderer,
		GtkEditable *editable, const gchar *path, gpointer data) {
	SondTreeview *stv = (SondTreeview*) data;
	SondTreeviewPrivate *stv_priv = sond_treeview_get_instance_private(stv);

	gtk_event_controller_set_propagation_phase(
			stv_priv->key_controller, GTK_PHASE_NONE);

	return;
}

/* --------------------------------------------------------------------------
 * Render-Callbacks
 * -------------------------------------------------------------------------- */
static void sond_treeview_grey_cut_cell(SondTreeview *stv, GtkTreeIter *iter) {
	SondTreeviewPrivate *stv_priv = sond_treeview_get_instance_private(stv);
	Clipboard *clipboard = SOND_TREEVIEW_GET_CLASS(stv)->clipboard;

	if (clipboard->tree_view == stv && clipboard->ausschneiden) {
		gboolean enthalten = FALSE;
		GtkTreePath *path = NULL;
		GtkTreePath *path_sel = NULL;

		path = gtk_tree_model_get_path(
				gtk_tree_view_get_model(GTK_TREE_VIEW(stv)), iter);
		for (gint i = 0; i < clipboard->arr_ref->len; i++) {
			path_sel = gtk_tree_row_reference_get_path(
					g_ptr_array_index(clipboard->arr_ref, i));
			if (!path_sel)
				continue;

			enthalten = !gtk_tree_path_compare(path, path_sel);
			gtk_tree_path_free(path_sel);
			if (enthalten)
				break;
		}
		gtk_tree_path_free(path);

		if (enthalten)
			g_object_set(G_OBJECT(stv_priv->renderer_text), "sensitive",
					FALSE, NULL);
		else
			g_object_set(G_OBJECT(stv_priv->renderer_text), "sensitive",
					TRUE, NULL);
	} else
		g_object_set(G_OBJECT(stv_priv->renderer_text), "sensitive",
				TRUE, NULL);

	return;
}

static void sond_treeview_underline_cursor(SondTreeview *stv,
		GtkTreeIter *iter) {
	SondTreeviewPrivate *stv_priv = sond_treeview_get_instance_private(stv);

	GtkTreePath *path_cursor = NULL;
	gtk_tree_view_get_cursor(GTK_TREE_VIEW(stv), &path_cursor, NULL);

	if (path_cursor) {
		GtkTreePath *path = gtk_tree_model_get_path(
				gtk_tree_view_get_model(GTK_TREE_VIEW(stv)), iter);
		if (!gtk_tree_path_compare(path, path_cursor))
			g_object_set(G_OBJECT(stv_priv->renderer_text),
					"underline-set", TRUE, NULL);
		else
			g_object_set(G_OBJECT(stv_priv->renderer_text),
					"underline-set", FALSE, NULL);
		gtk_tree_path_free(path);
	} else
		g_object_set(G_OBJECT(stv_priv->renderer_text),
				"underline-set", FALSE, NULL);

	gtk_tree_path_free(path_cursor);

	return;
}

static void sond_treeview_render_text(GtkTreeViewColumn *column,
		GtkCellRenderer *renderer, GtkTreeModel *treemodel, GtkTreeIter *iter,
		gpointer data) {
	SondTreeview *stv = SOND_TREEVIEW(data);

	sond_treeview_grey_cut_cell(stv, iter);
	sond_treeview_underline_cursor(stv, iter);

	if (SOND_TREEVIEW_GET_CLASS(stv)->render_text_cell)
		SOND_TREEVIEW_GET_CLASS(stv)->render_text_cell(column, renderer,
				treemodel, iter, data);

	return;
}

static gboolean sond_treeview_selection_select_func(
		GtkTreeSelection *selection, GtkTreeModel *model, GtkTreePath *path,
		gboolean selected, gpointer data) {
	if (selected)
		return TRUE;

	GList *list = gtk_tree_selection_get_selected_rows(selection, NULL);
	GList *l = list;
	while (l) {
		if (gtk_tree_path_is_ancestor(path, l->data)
				|| gtk_tree_path_is_descendant(path, l->data)) {
			g_list_free_full(list, (GDestroyNotify) gtk_tree_path_free);
			return FALSE;
		}
		l = l->next;
	}

	g_list_free_full(list, (GDestroyNotify) gtk_tree_path_free);

	return TRUE;
}

/* --------------------------------------------------------------------------
 * Aktions-Callbacks (GSimpleAction, GTK3 und GTK4)
 * -------------------------------------------------------------------------- */
static void sond_treeview_action_kopieren(GSimpleAction *action,
		GVariant *parameter, gpointer user_data) {
	sond_treeview_copy_or_cut_selection(SOND_TREEVIEW(user_data), FALSE);
}

static void sond_treeview_action_ausschneiden(GSimpleAction *action,
		GVariant *parameter, gpointer user_data) {
	sond_treeview_copy_or_cut_selection(SOND_TREEVIEW(user_data), TRUE);
}

static void sond_treeview_init(SondTreeview *stv) {
	GtkTreeViewColumn *tvc = NULL;

	SondTreeviewPrivate *stv_private = sond_treeview_get_instance_private(stv);

	gtk_tree_view_set_fixed_height_mode(GTK_TREE_VIEW(stv), TRUE);
	gtk_tree_view_set_enable_tree_lines(GTK_TREE_VIEW(stv), TRUE);
	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(stv), FALSE);

	GtkTreeSelection *selection = gtk_tree_view_get_selection(
			GTK_TREE_VIEW(stv));
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_MULTIPLE);
	gtk_tree_selection_set_select_function(selection,
			(GtkTreeSelectionFunc) sond_treeview_selection_select_func,
			NULL, NULL);

	stv_private->renderer_icon = gtk_cell_renderer_pixbuf_new();
	stv_private->renderer_text = gtk_cell_renderer_text_new();

	g_object_set(stv_private->renderer_text, "editable", TRUE, NULL);
	g_object_set(stv_private->renderer_text, "underline",
			PANGO_UNDERLINE_SINGLE, NULL);

	GdkRGBA gdkrgba;
	gdkrgba.alpha = 1.0;
	gdkrgba.red   = 0.95;
	gdkrgba.blue  = 0.95;
	gdkrgba.green = 0.95;

	g_object_set(G_OBJECT(stv_private->renderer_text), "background-rgba",
			&gdkrgba, "background-set", FALSE, NULL);

	tvc = gtk_tree_view_column_new();
	gtk_tree_view_column_set_sizing(tvc, GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_column_set_resizable(tvc, TRUE);

	gtk_tree_view_column_pack_start(tvc, stv_private->renderer_icon, FALSE);
	gtk_tree_view_column_pack_start(tvc, stv_private->renderer_text, FALSE);

	gtk_tree_view_append_column(GTK_TREE_VIEW(stv), tvc);

	gtk_tree_view_column_set_cell_data_func(tvc, stv_private->renderer_text,
			(GtkTreeCellDataFunc) sond_treeview_render_text, stv, NULL);

	/* Instanzspezifische ActionGroup */
	stv_private->action_group = g_simple_action_group_new();

	GSimpleAction *act_kop = g_simple_action_new("kopieren", NULL);
	g_signal_connect(act_kop, "activate",
			G_CALLBACK(sond_treeview_action_kopieren), stv);
	g_action_map_add_action(G_ACTION_MAP(stv_private->action_group),
			G_ACTION(act_kop));
	g_object_unref(act_kop);

	GSimpleAction *act_aus = g_simple_action_new("ausschneiden", NULL);
	g_signal_connect(act_aus, "activate",
			G_CALLBACK(sond_treeview_action_ausschneiden), stv);
	g_action_map_add_action(G_ACTION_MAP(stv_private->action_group),
			G_ACTION(act_aus));
	g_object_unref(act_aus);

	/* CellRenderer-Signale */
	g_signal_connect(stv_private->renderer_text, "editing-started",
			G_CALLBACK(renderer_text_editing_started), stv);
	g_signal_connect(stv_private->renderer_text, "editing-canceled",
			G_CALLBACK(renderer_text_editing_canceled), stv);
	g_signal_connect(stv_private->renderer_text, "edited",
			G_CALLBACK(sond_treeview_text_edited), stv);

	/* KeyController — wird beim Editieren deaktiviert */
	stv_private->key_controller = gtk_event_controller_key_new(
			GTK_WIDGET(stv));
	gtk_event_controller_set_propagation_phase(
			stv_private->key_controller, GTK_PHASE_BUBBLE);

	return;
}

/* --------------------------------------------------------------------------
 * Öffentliche API
 * -------------------------------------------------------------------------- */
void sond_treeview_set_id(SondTreeview *stv, gint id) {
	SondTreeviewPrivate* stv_priv =
			sond_treeview_get_instance_private(stv);

	stv_priv->id = id;
	return;
}

gint sond_treeview_get_id(SondTreeview *stv) {
	SondTreeviewPrivate* stv_priv =
			sond_treeview_get_instance_private(stv);

	return stv_priv->id;
}

GMenu* sond_treeview_get_gmenu(SondTreeview *stv) {
	return SOND_TREEVIEW_GET_CLASS(stv)->gmenu;
}

GSimpleActionGroup* sond_treeview_get_action_group(SondTreeview *stv) {
	SondTreeviewPrivate* stv_priv =
			sond_treeview_get_instance_private(stv);

	return stv_priv->action_group;
}

GtkCellRenderer* sond_treeview_get_cell_renderer_icon(SondTreeview *stv) {
	SondTreeviewPrivate* stv_priv =
			sond_treeview_get_instance_private(stv);

	return stv_priv->renderer_icon;
}

GtkCellRenderer* sond_treeview_get_cell_renderer_text(SondTreeview *stv) {
	SondTreeviewPrivate* stv_priv =
			sond_treeview_get_instance_private(stv);

	return stv_priv->renderer_text;
}

GtkWidget* sond_treeview_get_contextmenu(SondTreeview *stv) {
	SondTreeviewPrivate* stv_priv =
			sond_treeview_get_instance_private(stv);

	return stv_priv->contextmenu;
}

void sond_treeview_expand_row(SondTreeview *stv, GtkTreeIter *iter) {
	if (!iter)
		return;

	GtkTreePath *path = gtk_tree_model_get_path(
			gtk_tree_view_get_model(GTK_TREE_VIEW(stv)), iter);
	gtk_tree_view_expand_to_path(GTK_TREE_VIEW(stv), path);
	gtk_tree_path_free(path);

	return;
}

void sond_treeview_expand_to_row(SondTreeview *stv, GtkTreeIter *iter) {
	if (!iter)
		return;

	GtkTreePath *path = gtk_tree_model_get_path(
			gtk_tree_view_get_model(GTK_TREE_VIEW(stv)), iter);
	if (gtk_tree_path_up(path))
		gtk_tree_view_expand_to_path(GTK_TREE_VIEW(stv), path);
	gtk_tree_path_free(path);

	return;
}

gboolean sond_treeview_row_expanded(SondTreeview *stv, GtkTreeIter *iter) {
	GtkTreePath *path = gtk_tree_model_get_path(
			gtk_tree_view_get_model(GTK_TREE_VIEW(stv)), iter);
	gboolean expanded = gtk_tree_view_row_expanded(GTK_TREE_VIEW(stv), path);
	gtk_tree_path_free(path);

	return expanded;
}

gboolean sond_treeview_get_cursor(SondTreeview *stv, GtkTreeIter *iter) {
	GtkTreePath *path = NULL;

	gtk_tree_view_get_cursor(GTK_TREE_VIEW(stv), &path, NULL);
	if (!path)
		return FALSE;

	if (iter)
		gtk_tree_model_get_iter(gtk_tree_view_get_model(GTK_TREE_VIEW(stv)),
				iter, path);

	gtk_tree_path_free(path);

	return TRUE;
}

void sond_treeview_set_cursor(SondTreeview *stv, GtkTreeIter *iter) {
	if (!iter)
		return;

	GtkTreePath *path = gtk_tree_model_get_path(
			gtk_tree_view_get_model(GTK_TREE_VIEW(stv)), iter);
	gtk_tree_view_set_cursor(GTK_TREE_VIEW(stv), path, NULL, FALSE);
	gtk_tree_path_free(path);

	gtk_widget_grab_focus(GTK_WIDGET(stv));

	return;
}

void sond_treeview_set_cursor_on_text_cell(SondTreeview *stv,
		GtkTreeIter *iter) {
	if (!iter)
		return;

	SondTreeviewPrivate *stv_priv = sond_treeview_get_instance_private(stv);

	GtkTreePath *path = gtk_tree_model_get_path(
			gtk_tree_view_get_model(GTK_TREE_VIEW(stv)), iter);
	gtk_tree_view_set_cursor_on_cell(GTK_TREE_VIEW(stv), path,
			gtk_tree_view_get_column(GTK_TREE_VIEW(stv), 0),
			stv_priv->renderer_text, FALSE);
	gtk_tree_path_free(path);

	return;
}

gboolean sond_treeview_test_cursor_descendant(SondTreeview *stv,
		gboolean child) {
	Clipboard *clipboard = ((SondTreeviewClass*) g_type_class_peek(
			SOND_TYPE_TREEVIEW))->clipboard;

	GtkTreePath *path = NULL;

	if (clipboard->arr_ref->len == 0)
		return FALSE;

	gtk_tree_view_get_cursor(GTK_TREE_VIEW(stv), &path, NULL);
	if (!path)
		return FALSE;

	GtkTreePath *path_sel = NULL;
	for (gint i = 0; i < clipboard->arr_ref->len; i++) {
		gboolean res = FALSE;

		path_sel = gtk_tree_row_reference_get_path(
				g_ptr_array_index(clipboard->arr_ref, i));
		if (!path_sel)
			continue;

		if (child && !gtk_tree_path_compare(path_sel, path))
			res = TRUE;
		else if (gtk_tree_path_is_descendant(path, path_sel))
			res = TRUE;
		gtk_tree_path_free(path_sel);

		if (res == TRUE) {
			gtk_tree_path_free(path);
			return TRUE;
		}
	}

	gtk_tree_path_free(path);

	return FALSE;
}

static GPtrArray* sond_treeview_selection_get_refs(SondTreeview *stv) {
	GList *selected = gtk_tree_selection_get_selected_rows(
			gtk_tree_view_get_selection(GTK_TREE_VIEW(stv)), NULL);

	if (!selected)
		return NULL;

	GPtrArray *refs = g_ptr_array_new_with_free_func(
			(GDestroyNotify) gtk_tree_row_reference_free);

	GList *list = selected;
	do
		g_ptr_array_add(refs,
				gtk_tree_row_reference_new(
						gtk_tree_view_get_model(GTK_TREE_VIEW(stv)),
						list->data));
	while ((list = list->next));

	g_list_free_full(selected, (GDestroyNotify) gtk_tree_path_free);

	return refs;
}

void sond_treeview_copy_or_cut_selection(SondTreeview *stv,
		gboolean ausschneiden) {
	Clipboard *clipboard = ((SondTreeviewClass*) g_type_class_peek(
			SOND_TYPE_TREEVIEW))->clipboard;

	GPtrArray *refs = sond_treeview_selection_get_refs(stv);
	if (!refs)
		return;

	if (clipboard->ausschneiden)
		gtk_widget_queue_draw(GTK_WIDGET(clipboard->tree_view));

	g_ptr_array_unref(clipboard->arr_ref);

	clipboard->tree_view = stv;
	clipboard->ausschneiden = ausschneiden;
	clipboard->arr_ref = refs;

	if (ausschneiden)
		gtk_widget_queue_draw(GTK_WIDGET(clipboard->tree_view));

	return;
}

static gint sond_treeview_refs_foreach(SondTreeview *stv_orig, GPtrArray *refs,
		gint (*foreach)(SondTreeview*, GtkTreeIter*, gpointer, GError**),
		gpointer data, GError **error) {
	if (!refs)
		return 0;

	for (gint i = 0; i < refs->len; i++) {
		GtkTreeIter iter_ref;
		gboolean success = FALSE;
		gint rc = 0;

		GtkTreeRowReference *ref = g_ptr_array_index(refs, i);
		GtkTreePath *path = gtk_tree_row_reference_get_path(ref);
		if (!path)
			continue;

		success = gtk_tree_model_get_iter(
				gtk_tree_view_get_model(GTK_TREE_VIEW(stv_orig)), &iter_ref,
				path);
		gtk_tree_path_free(path);
		if (!success) {
			LOG_WARN("%s\ngtk_tree_model_iter erfolglos", __func__);
			continue;
		}

		rc = foreach(stv_orig, &iter_ref, data, error);
		if (rc == -1)
			return -1;
		else if (rc >= 1)
			return rc;
	}

	return 0;
}

gint sond_treeview_clipboard_foreach(
		gint (*foreach)(SondTreeview*, GtkTreeIter*, gpointer, GError**),
		gpointer data, GError **error) {
	gint rc = 0;

	Clipboard *clipboard = ((SondTreeviewClass*) g_type_class_peek(
			SOND_TYPE_TREEVIEW))->clipboard;

	rc = sond_treeview_refs_foreach(clipboard->tree_view, clipboard->arr_ref,
			foreach, data, error);
	if (rc == -1)
		return -1;
	else if (rc >= 1)
		return rc;

	return 0;
}

gint sond_treeview_selection_foreach(SondTreeview *stv,
		gint (*foreach)(SondTreeview*, GtkTreeIter*, gpointer, GError**),
		gpointer data, GError **error) {
	gint rc = 0;
	GPtrArray *refs = NULL;

	refs = sond_treeview_selection_get_refs(stv);
	if (!refs)
		return 0;

	rc = sond_treeview_refs_foreach(stv, refs, foreach, data, error);
	g_ptr_array_unref(refs);
	if (rc == -1)
		return -1;
	else if (rc >= 1)
		return rc;

	return 0;
}
