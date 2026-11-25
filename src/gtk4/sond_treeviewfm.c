/*
 sond (gtk4/sond_treeviewfm.c) - Akten, Beweisstücke, Unterlagen
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

static void sond_treeviewfm_owner_init(SondTVFMOwnerInterface *iface);

G_DEFINE_TYPE_WITH_CODE(SondTreeviewFM, sond_treeviewfm, SOND_TYPE_TREEVIEW,
		G_ADD_PRIVATE(SondTreeviewFM)
		G_IMPLEMENT_INTERFACE(SOND_TYPE_TVFM_OWNER, sond_treeviewfm_owner_init))

SondTreeviewFMPrivate* fm_get_priv(SondTreeviewFM *self) {
	return sond_treeviewfm_get_instance_private(self);
}



guint fm_signals[N_SIGNALS] = { 0 };

/* ------------------------------------------------------------------------
 * Eigentümer-Schnittstelle (sond_tvfm_owner.h)
 * --------------------------------------------------------------------- */

static gchar const* owner_get_root(SondTVFMOwner *owner) {
	return sond_treeviewfm_get_root(SOND_TREEVIEWFM(owner));
}

static gboolean owner_is_seadrive_path(SondTVFMOwner *owner) {
	return sond_treeviewfm_is_seadrive_path(SOND_TREEVIEWFM(owner));
}

static gboolean owner_supports_sections(SondTVFMOwner *owner) {
	return SOND_TREEVIEWFM_GET_CLASS(owner)->has_sections != NULL;
}

static gboolean owner_has_sections(SondTVFMOwner *owner, SondTVFMItem *item) {
	return SOND_TREEVIEWFM_GET_CLASS(owner)->has_sections ?
			SOND_TREEVIEWFM_GET_CLASS(owner)->has_sections(item) : FALSE;
}

static gboolean owner_supports_load_sections(SondTVFMOwner *owner) {
	return SOND_TREEVIEWFM_GET_CLASS(owner)->load_sections != NULL;
}

static gint owner_load_sections(SondTVFMOwner *owner, SondTVFMItem *item,
		GPtrArray **arr_children, GError **error) {
	return SOND_TREEVIEWFM_GET_CLASS(owner)->load_sections ?
			SOND_TREEVIEWFM_GET_CLASS(owner)->load_sections(item, arr_children,
					error) : -1;
}

static gboolean owner_supports_delete_section(SondTVFMOwner *owner) {
	return SOND_TREEVIEWFM_GET_CLASS(owner)->delete_section != NULL;
}

static gint owner_delete_section(SondTVFMOwner *owner, SondTVFMItem *item,
		GError **error) {
	return SOND_TREEVIEWFM_GET_CLASS(owner)->delete_section ?
			SOND_TREEVIEWFM_GET_CLASS(owner)->delete_section(item, error) : 0;
}

static gint owner_before_move(SondTVFMOwner *owner, SondTVFMItem *item,
		SondTVFMItem *parent, gchar const *base, gint index_to, GError **error,
		gpointer *ctx) {
	gint res = 0;

	g_signal_emit(owner, SOND_TREEVIEWFM_GET_CLASS(owner)->signal_before_move, 0,
			item, parent, base, index_to, error, ctx, &res);

	return res;
}

static gint owner_before_delete(SondTVFMOwner *owner, SondTVFMItem *item,
		GError **error, gpointer *ctx) {
	gint res = 0;

	g_signal_emit(owner, SOND_TREEVIEWFM_GET_CLASS(owner)->signal_before_delete, 0,
			item, error, ctx, &res);

	return res;
}

static gint owner_before_insert(SondTVFMOwner *owner, SondTVFMItem *item,
		SondTVFMItem *parent, gchar const *base, gint index_to, GError **error) {
	gint res = 0;

	g_signal_emit(owner, SOND_TREEVIEWFM_GET_CLASS(owner)->signal_before_insert, 0,
			item, parent, base, index_to, error, &res);

	return res;
}

static void owner_after(SondTVFMOwner *owner, gboolean success, gpointer ctx) {
	g_signal_emit(owner, SOND_TREEVIEWFM_GET_CLASS(owner)->signal_after, 0,
			success, ctx);
}

static void sond_treeviewfm_owner_init(SondTVFMOwnerInterface *iface) {
	iface->get_root = owner_get_root;
	iface->is_seadrive_path = owner_is_seadrive_path;
	iface->supports_sections = owner_supports_sections;
	iface->has_sections = owner_has_sections;
	iface->supports_load_sections = owner_supports_load_sections;
	iface->load_sections = owner_load_sections;
	iface->supports_delete_section = owner_supports_delete_section;
	iface->delete_section = owner_delete_section;
	iface->before_move = owner_before_move;
	iface->before_delete = owner_before_delete;
	iface->before_insert = owner_before_insert;
	iface->after = owner_after;
}

/* ------------------------------------------------------------------------
 * Hilfen
 * --------------------------------------------------------------------- */

/* Voller Pfad eines Dateisystem-Eintrags (g_free()); NULL für Einträge in Containern */
gchar* fm_item_full_path(SondTreeviewFM *self, SondTVFMItem *item,
		gboolean *is_dir) {
	SondTreeviewFMPrivate *priv = sond_treeviewfm_get_instance_private(self);
	gchar *rel = NULL;
	gchar *full = NULL;

	if (!priv->root || !sond_tvfm_item_fs_path(item, &rel, is_dir))
		return NULL;

	full = g_strconcat(priv->root, "/", rel, NULL);
	g_free(rel);

	return full;
}

gboolean fm_item_is_dir(SondTVFMItem *item) {
	gboolean dir = FALSE;

	if (sond_tvfm_item_fs_stat(item, NULL, NULL, &dir))
		return dir;

	return sond_tvfm_item_get_item_type(item) == SOND_TVFM_ITEM_TYPE_DIR;
}

/* ------------------------------------------------------------------------
 * Wurzel, Auswahl, Anheften
 * --------------------------------------------------------------------- */

const gchar* sond_treeviewfm_get_root(SondTreeviewFM *self) {
	SondTreeviewFMPrivate *priv = NULL;

	g_return_val_if_fail(SOND_IS_TREEVIEWFM(self), NULL);
	priv = sond_treeviewfm_get_instance_private(self);

	return priv->root;
}

gboolean sond_treeviewfm_is_seadrive_path(SondTreeviewFM *self) {
	SondTreeviewFMPrivate *priv = NULL;

	g_return_val_if_fail(SOND_IS_TREEVIEWFM(self), FALSE);
	priv = sond_treeviewfm_get_instance_private(self);

	return priv->is_seadrive;
}

static void update_pin_actions(SondTreeviewFM *self) {
	SondTreeviewFMPrivate *priv = sond_treeviewfm_get_instance_private(self);
	GActionMap *map = sond_treeview_get_action_map(SOND_TREEVIEW(self));
	static const gchar *names[] = { "pin", "unpin", "free" };

	for (guint i = 0; i < G_N_ELEMENTS(names); i++) {
		GAction *action = g_action_map_lookup_action(map, names[i]);

		if (action)
			g_simple_action_set_enabled(G_SIMPLE_ACTION(action), priv->is_seadrive);
	}
}

void sond_treeviewfm_set_read_only(SondTreeviewFM *self, gchar const *reason) {
	SondTreeviewFMPrivate *priv = NULL;
	GActionMap *map = NULL;
	static const gchar *names[] = { "cut", "paste", "paste-same", "paste-child", "mkdir-same",
			"mkdir-child", "rename", "delete", "unlock" };

	g_return_if_fail(SOND_IS_TREEVIEWFM(self));
	priv = sond_treeviewfm_get_instance_private(self);

	if (!g_strcmp0(priv->read_only, reason))
		return;

	g_free(priv->read_only);
	priv->read_only = g_strdup(reason);

	map = sond_treeview_get_action_map(SOND_TREEVIEW(self));
	for (guint i = 0; i < G_N_ELEMENTS(names); i++) {
		GAction *action = g_action_map_lookup_action(map, names[i]);

		if (action)
			g_simple_action_set_enabled(G_SIMPLE_ACTION(action), !priv->read_only);
	}
	for (guint i = 0; priv->write_actions && i < priv->write_actions->len; i++) {
		GAction *action = g_action_map_lookup_action(map, g_ptr_array_index(priv->write_actions, i));

		if (action)
			g_simple_action_set_enabled(G_SIMPLE_ACTION(action), !priv->read_only);
	}
}

void sond_treeviewfm_register_write_action(SondTreeviewFM *self, gchar const *name) {
	SondTreeviewFMPrivate *priv = NULL;
	GAction *action = NULL;

	g_return_if_fail(SOND_IS_TREEVIEWFM(self) && name);
	priv = sond_treeviewfm_get_instance_private(self);

	if (!priv->write_actions)
		priv->write_actions = g_ptr_array_new_with_free_func(g_free);
	g_ptr_array_add(priv->write_actions, g_strdup(name));

	action = g_action_map_lookup_action(sond_treeview_get_action_map(SOND_TREEVIEW(self)), name);
	if (action)
		g_simple_action_set_enabled(G_SIMPLE_ACTION(action), !priv->read_only);
}

gchar const* sond_treeviewfm_get_read_only(SondTreeviewFM *self) {
	SondTreeviewFMPrivate *priv = NULL;

	g_return_val_if_fail(SOND_IS_TREEVIEWFM(self), NULL);
	priv = sond_treeviewfm_get_instance_private(self);

	return priv->read_only;
}

gint sond_treeviewfm_set_root(SondTreeviewFM *self, const gchar *root,
		GError **error) {
	SondTreeviewFMPrivate *priv = NULL;
	SondTVFMItem *root_item = NULL;
	GPtrArray *children = NULL;
	GListModel *model = NULL;
	SondFilePartClass *sfp_class = g_type_class_peek_static(SOND_TYPE_FILE_PART);

	g_return_val_if_fail(SOND_IS_TREEVIEWFM(self), -1);
	priv = sond_treeviewfm_get_instance_private(self);

	fm_root_monitor_stop(priv);
#ifdef G_OS_WIN32
	fm_watcher_stop(self);
#endif
	g_free(priv->root);
	priv->root = NULL;
	priv->is_seadrive = FALSE;
	g_free(sfp_class->path_root);
	sfp_class->path_root = NULL;

	if (!root) {
		sond_treeview_set_roots(SOND_TREEVIEW(self),
				G_LIST_MODEL(g_list_store_new(SOND_TYPE_TVFM_ITEM)));
		update_pin_actions(self);

		return 0;
	}

	priv->root = g_strdup(root);
	//abschließende Trennzeichen entfernen ("C:/" bleibt unverändert)
	while (strlen(priv->root) > 1 && (g_str_has_suffix(priv->root, "/") ||
			g_str_has_suffix(priv->root, "\\")) && !g_str_has_suffix(priv->root, ":/")
			&& !g_str_has_suffix(priv->root, ":\\"))
		priv->root[strlen(priv->root) - 1] = '\0';

	//die Dateiteile lösen ihre Pfade gegen die Wurzel auf (klassenweit, s. sond_fileparts.h)
	sfp_class->path_root = g_strdup(priv->root);

#ifdef G_OS_WIN32
	priv->is_seadrive = sond_seadrive_is_seadrive_path(priv->root);
#endif
	update_pin_actions(self);

	//die Wurzel ist ein Verzeichnis-Item ohne Pfad; seine Kinder sind die oberste Ebene
	root_item = sond_tvfm_item_create(self, NULL, NULL);
	if (!root_item) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
				"Wurzel '%s' nicht lesbar", root);
		g_free(sfp_class->path_root);
		sfp_class->path_root = NULL;
		g_clear_pointer(&priv->root, g_free);
		priv->is_seadrive = FALSE;

		return -1;
	}

	if (sond_tvfm_item_load_children(root_item, &children, NULL, error)) {
		g_object_unref(root_item);
		g_free(sfp_class->path_root);
		sfp_class->path_root = NULL;
		g_clear_pointer(&priv->root, g_free);
		priv->is_seadrive = FALSE;

		return -1;
	}

	model = G_LIST_MODEL(fm_lazy_new_loaded(children));
	g_object_unref(root_item);

	sond_treeview_set_roots(SOND_TREEVIEW(self), model);

#ifdef G_OS_WIN32
	fm_watcher_start(self);
#endif
	fm_root_monitor_start(self);

	return 0;
}

GPtrArray* sond_treeviewfm_get_selected_fileparts(SondTreeviewFM *self) {
	GPtrArray *items = NULL;
	GPtrArray *fileparts = g_ptr_array_new_with_free_func(g_free);

	g_return_val_if_fail(SOND_IS_TREEVIEWFM(self), fileparts);

	items = sond_treeview_get_selected_items(SOND_TREEVIEW(self));
	for (guint i = 0; i < items->len; i++) {
		SondTVFMItem *item = g_ptr_array_index(items, i);
		SondFilePart *sfp = NULL;
		gchar *filepart = NULL;

		if (sond_tvfm_item_get_item_type(item) != SOND_TVFM_ITEM_TYPE_LEAF)
			continue;

		sfp = sond_tvfm_item_get_sond_file_part(item);
		if (!sfp || !sond_file_part_get_parent(sfp))
			continue;

		filepart = sond_tvfm_item_get_filepart(item);
		if (filepart)
			g_ptr_array_add(fileparts, filepart);
	}
	g_ptr_array_unref(items);

	return fileparts;
}

GPtrArray* sond_treeviewfm_get_selected_paths(SondTreeviewFM *self) {
	GPtrArray *items = NULL;
	GPtrArray *paths = g_ptr_array_new_with_free_func(g_free);

	g_return_val_if_fail(SOND_IS_TREEVIEWFM(self), paths);

	items = sond_treeview_get_selected_items(SOND_TREEVIEW(self));
	for (guint i = 0; i < items->len; i++) {
		gchar *full = fm_item_full_path(self, g_ptr_array_index(items, i), NULL);

		if (full)
			g_ptr_array_add(paths, full);
	}
	g_ptr_array_unref(items);

	return paths;
}

void sond_treeviewfm_refresh_status(SondTreeviewFM *self) {
	g_return_if_fail(SOND_IS_TREEVIEWFM(self));

	g_signal_emit(self, fm_signals[SIGNAL_PINS_CHANGED], 0);
}

#ifdef G_OS_WIN32
/* Meldung über gescheiterte Einträge (die ersten fünf) */
static void report_failures(SondTreeviewFM *self, GPtrArray *failed) {
	GString *text = g_string_new(NULL);
	GtkAlertDialog *dialog = NULL;
	GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(self));

	if (failed->len == 1)
		g_string_append(text, "1 Eintrag konnte nicht geändert werden:\n");
	else
		g_string_append_printf(text,
				"%u Einträge konnten nicht geändert werden:\n", failed->len);
	for (guint i = 0; i < MIN(failed->len, 5); i++)
		g_string_append_printf(text, "\n%s",
				(const gchar*) g_ptr_array_index(failed, i));
	if (failed->len > 5)
		g_string_append_printf(text, "\n... und %u weitere", failed->len - 5);

	dialog = gtk_alert_dialog_new("SeaDrive");
	gtk_alert_dialog_set_detail(dialog, text->str);
	gtk_alert_dialog_set_modal(dialog, TRUE);
	gtk_alert_dialog_show(dialog, GTK_IS_WINDOW(root) ? GTK_WINDOW(root) : NULL);
	g_object_unref(dialog);
	g_string_free(text, TRUE);
}
#endif

void sond_treeviewfm_pin_root(SondTreeviewFM *self, guint pin_state) {
#ifdef G_OS_WIN32
	SondTreeviewFMPrivate *priv = NULL;
	GPtrArray *failed = NULL;
	GError *error = NULL;

	g_return_if_fail(SOND_IS_TREEVIEWFM(self));
	priv = sond_treeviewfm_get_instance_private(self);

	if (!priv->is_seadrive || !priv->root)
		return;

	//das ganze Projektverzeichnis rekursiv
	if (!sond_seadrive_set_pin_state(priv->root, pin_state, TRUE, &error)) {
		failed = g_ptr_array_new_with_free_func(g_free);
		sond_seadrive_pin_note_failure(failed, priv->root, error);
		report_failures(self, failed);
		g_ptr_array_unref(failed);
	}
	g_clear_error(&error);

	sond_treeviewfm_refresh_status(self);
#else
	(void) self;
	(void) pin_state;
#endif
}

void sond_treeviewfm_pin_selection(SondTreeviewFM *self, guint pin_state) {
#ifdef G_OS_WIN32
	SondTreeviewFMPrivate *priv = NULL;
	GPtrArray *items = NULL;
	GPtrArray *failed = NULL;

	g_return_if_fail(SOND_IS_TREEVIEWFM(self));
	priv = sond_treeviewfm_get_instance_private(self);

	if (!priv->is_seadrive)
		return;

	items = sond_treeview_get_selected_items(SOND_TREEVIEW(self));
	failed = g_ptr_array_new_with_free_func(g_free);

	for (guint i = 0; i < items->len; i++) {
		gboolean dir = FALSE;
		gchar *full = fm_item_full_path(self, g_ptr_array_index(items, i), &dir);
		GError *error = NULL;

		if (!full)
			continue;

		//Verzeichnisse rekursiv: der Zustand gilt für alles darunter
		if (!sond_seadrive_set_pin_state(full, pin_state, dir, &error))
			sond_seadrive_pin_note_failure(failed, full, error);
		g_clear_error(&error);
		g_free(full);
	}

	sond_treeviewfm_refresh_status(self);
	if (failed->len > 0)
		report_failures(self, failed);

	g_ptr_array_unref(failed);
	g_ptr_array_unref(items);
#else
	(void) self;
	(void) pin_state;
#endif
}

GtkWindow* fm_parent_window(SondTreeviewFM *self) {
	GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(self));

	return GTK_IS_WINDOW(root) ? GTK_WINDOW(root) : NULL;
}

void fm_show_error(SondTreeviewFM *self, gchar const *title, gchar const *message) {
	GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", title);

	gtk_alert_dialog_set_detail(dialog, message ? message : "");
	gtk_alert_dialog_set_modal(dialog, TRUE);
	gtk_alert_dialog_show(dialog, fm_parent_window(self));
	g_object_unref(dialog);
}

/* Das Eltern-Item einer Zeile (neue Referenz): das Item der Elternzeile, bei
 * Einträgen der obersten Ebene das Wurzel-Verzeichnis */
SondTVFMItem* fm_parent_item_of_row(SondTreeviewFM *self, GtkTreeListRow *row) {
	GtkTreeListRow *parent = gtk_tree_list_row_get_parent(row);
	SondTVFMItem *item = NULL;

	if (parent) {
		item = gtk_tree_list_row_get_item(parent);
		g_object_unref(parent);

		return item;
	}

	return sond_tvfm_item_create(self, NULL, NULL);
}

gboolean fm_match_same(gpointer item, gpointer user_data) {
	return item == user_data;
}

gboolean fm_match_path(gpointer item, gpointer user_data) {
	return !g_strcmp0(sond_tvfm_item_get_path_or_section(SOND_TVFM_ITEM(item)),
			(gchar const*) user_data);
}

/* ------------------------------------------------------------------------
 * Kontextmenü
 * --------------------------------------------------------------------- */

static void action_pin(GSimpleAction *action, GVariant *parameter, gpointer data) {
	(void) action;
	(void) parameter;

	sond_treeviewfm_pin_selection(SOND_TREEVIEWFM(data), STVFM_PIN_STATE_PINNED);
}

static void action_unpin(GSimpleAction *action, GVariant *parameter, gpointer data) {
	(void) action;
	(void) parameter;

	sond_treeviewfm_pin_selection(SOND_TREEVIEWFM(data),
			STVFM_PIN_STATE_UNSPECIFIED);
}

static void action_free(GSimpleAction *action, GVariant *parameter, gpointer data) {
	(void) action;
	(void) parameter;

	sond_treeviewfm_pin_selection(SOND_TREEVIEWFM(data), STVFM_PIN_STATE_UNPINNED);
}

static void action_refresh(GSimpleAction *action, GVariant *parameter, gpointer data) {
	(void) action;
	(void) parameter;

	sond_treeviewfm_refresh_status(SOND_TREEVIEWFM(data));
}

/* ------------------------------------------------------------------------
 * Klasse
 * --------------------------------------------------------------------- */

static void sond_treeviewfm_finalize(GObject *object) {
	SondTreeviewFM *self = SOND_TREEVIEWFM(object);
	SondTreeviewFMPrivate *priv = sond_treeviewfm_get_instance_private(self);
	SondFilePartClass *sfp_class = g_type_class_peek_static(SOND_TYPE_FILE_PART);

	//path_root liegt auf Klassenebene (s. sond_fileparts.h): nur freigeben, wenn es unser Pfad ist
	if (sfp_class && priv->root && !g_strcmp0(sfp_class->path_root, priv->root)) {
		g_free(sfp_class->path_root);
		sfp_class->path_root = NULL;
	}
	fm_root_monitor_stop(priv);
#ifdef G_OS_WIN32
	if (priv->watcher) {
		g_signal_handlers_disconnect_by_data(priv->watcher, object);
		sond_seadrive_watcher_stop_async(priv->watcher);
		g_clear_object(&priv->watcher);
	}
#endif
	g_free(priv->search_text);
	g_free(priv->root);
	g_free(priv->read_only);
	if (priv->write_actions)
		g_ptr_array_unref(priv->write_actions);

	G_OBJECT_CLASS(sond_treeviewfm_parent_class)->finalize(object);
}

/* Standard: Offline-Menü (SeaDrive), das auch Zond verwendet */
static void default_setup_menu(SondTreeviewFM *self) {
	SondTreeview *base = SOND_TREEVIEW(self);
	GMenu *menu = sond_treeview_get_menu(base);
	GMenu *section = g_menu_new();
	GMenu *edit = g_menu_new();
	GMenu *new_dir = g_menu_new();
	GMenu *open_sec = g_menu_new();
	GMenu *search_sec = g_menu_new();
	GMenu *paste_sub = g_menu_new();
	GtkEventController *shortcuts = NULL;
	static const GActionEntry entries[] = {
		{ "open", fm_action_open, NULL, NULL, NULL },
		{ "open-with", fm_action_open_with, NULL, NULL, NULL },
		{ "copy", fm_action_copy, NULL, NULL, NULL },
		{ "cut", fm_action_cut, NULL, NULL, NULL },
		{ "paste", fm_action_paste, NULL, NULL, NULL },
		{ "paste-same", fm_action_paste_same, NULL, NULL, NULL },
		{ "paste-child", fm_action_paste_child, NULL, NULL, NULL },
		{ "mkdir-same", fm_action_mkdir_same, NULL, NULL, NULL },
		{ "mkdir-child", fm_action_mkdir_child, NULL, NULL, NULL },
		{ "rename", fm_action_rename, NULL, NULL, NULL },
		{ "delete", fm_action_delete, NULL, NULL, NULL },
		{ "pin", action_pin, NULL, NULL, NULL },
		{ "unpin", action_unpin, NULL, NULL, NULL },
		{ "free", action_free, NULL, NULL, NULL },
		{ "dateisuche", fm_action_search, NULL, NULL, NULL },
		{ "unlock", fm_action_unlock, NULL, NULL, NULL },
		{ "refresh", action_refresh, NULL, NULL, NULL }
	};

	g_action_map_add_action_entries(sond_treeview_get_action_map(base), entries,
			G_N_ELEMENTS(entries), self);
	//Öffnen
	g_menu_append(open_sec, "Öffnen", "tv.open");
	g_menu_append(open_sec, "Öffnen mit", "tv.open-with");
	g_menu_append_section(menu, NULL, G_MENU_MODEL(open_sec));
	g_object_unref(open_sec);

	//Bearbeiten
	g_menu_append(edit, "Kopieren", "tv.copy");
	g_menu_append(edit, "Ausschneiden", "tv.cut");
	g_menu_append(paste_sub, "Gleiche Ebene", "tv.paste-same");
	g_menu_append(paste_sub, "Unterebene", "tv.paste-child");
	g_menu_append_submenu(edit, "Einfügen", G_MENU_MODEL(paste_sub));
	g_object_unref(paste_sub);
	g_menu_append(new_dir, "Gleiche Ebene", "tv.mkdir-same");
	g_menu_append(new_dir, "Unterebene", "tv.mkdir-child");
	g_menu_append_submenu(edit, "Neues Verzeichnis", G_MENU_MODEL(new_dir));
	g_menu_append(edit, "Umbenennen", "tv.rename");
	g_menu_append(edit, "Löschen", "tv.delete");
	g_menu_append_section(menu, NULL, G_MENU_MODEL(edit));
	g_object_unref(new_dir);
	g_object_unref(edit);

	//Tastenkürzel: F2 umbenennen, Entf löschen
	shortcuts = gtk_shortcut_controller_new();
	gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(shortcuts),
			gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_F2, 0),
					gtk_named_action_new("tv.rename")));
	gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(shortcuts),
			gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_Delete, 0),
					gtk_named_action_new("tv.delete")));
	gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(shortcuts),
			gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_c, GDK_CONTROL_MASK),
					gtk_named_action_new("tv.copy")));
	gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(shortcuts),
			gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_x, GDK_CONTROL_MASK),
					gtk_named_action_new("tv.cut")));
	gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(shortcuts),
			gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_v, GDK_CONTROL_MASK),
					gtk_named_action_new("tv.paste")));
	gtk_widget_add_controller(GTK_WIDGET(self), shortcuts);

	g_menu_append(search_sec, "Dateisuche …", "tv.dateisuche");
	g_menu_append(search_sec, "Dauerhaft entsperren …", "tv.unlock");
	g_menu_append_section(menu, NULL, G_MENU_MODEL(search_sec));
	g_object_unref(search_sec);

	g_menu_append(menu, "Immer offline halten", "tv.pin");
	g_menu_append(menu, "Offline-Haltung aufheben", "tv.unpin");
	g_menu_append(menu, "Platz freigeben (nur online)", "tv.free");
	g_menu_append(section, "Anzeige aktualisieren", "tv.refresh");
	g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
	g_object_unref(section);
}

/* Spalten und Menü erst hier aufbauen, damit Unterklassen (deren Vfuncs schon
 * stehen) sie überschreiben oder ergänzen können */
static void sond_treeviewfm_constructed(GObject *object) {
	SondTreeviewFM *self = SOND_TREEVIEWFM(object);
	SondTreeviewFMClass *klass = SOND_TREEVIEWFM_GET_CLASS(self);

	G_OBJECT_CLASS(sond_treeviewfm_parent_class)->constructed(object);

	fm_register_icons();

	g_signal_connect(sond_treeview_get_column_view(SOND_TREEVIEW(self)), "activate",
			G_CALLBACK(fm_on_activate), self);

	if (klass->setup_columns)
		klass->setup_columns(self);
	if (klass->setup_menu)
		klass->setup_menu(self);

	update_pin_actions(self);
}

static void sond_treeviewfm_class_init(SondTreeviewFMClass *klass) {
	G_OBJECT_CLASS(klass)->finalize = sond_treeviewfm_finalize;
	G_OBJECT_CLASS(klass)->constructed = sond_treeviewfm_constructed;

	//SondFilePart braucht seine Klasse (path_root, geöffnete Dateien) schon vor der
	//ersten Instanz; die Referenz bleibt für die Laufzeit des Programms bestehen
	g_type_class_ref(SOND_TYPE_FILE_PART);
	SOND_TREEVIEW_CLASS(klass)->create_children = fm_create_children;

	klass->setup_columns = fm_default_setup_columns;
	klass->setup_menu = default_setup_menu;
	klass->file_badge = fm_default_file_badge;
	klass->dir_status = fm_default_dir_status;
	klass->index_status = fm_default_index_status;
	klass->get_section_index_status = NULL;
	klass->has_sections = NULL;
	klass->load_sections = NULL;
	klass->delete_section = NULL;
	klass->fs_entry_deleted = NULL;
	klass->fs_entry_renamed = NULL;
	klass->fs_entry_moved = NULL;
	klass->open_item = fm_default_open_item;
	klass->confirm_delete = TRUE;
	klass->file_part_busy = NULL;
	klass->get_eingang = NULL;
	klass->text_from_section = NULL;
	klass->deter_background = NULL;
	klass->text_edited = fm_default_text_edited;
	fm_search_register_signals(G_OBJECT_CLASS(klass));

	//der Pin-Zustand einiger Einträge hat sich geändert: Anzeige neu lesen
	fm_signals[SIGNAL_PINS_CHANGED] = g_signal_new("pins-changed",
			G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			G_TYPE_NONE, 0);

	//die Eingangsangaben haben sich geändert: Spalte neu lesen
	fm_signals[SIGNAL_EINGANG_CHANGED] = g_signal_new("eingang-changed",
			G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			G_TYPE_NONE, 0);

	//Text und Hervorhebung der Namenszellen haben sich geändert (Vfuncs text_from_section,
	//deter_background): neu lesen
	fm_signals[SIGNAL_NAMES_CHANGED] = g_signal_new("names-changed",
			G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			G_TYPE_NONE, 0);

	//auf der obersten Ebene sind von außen Einträge hinzugekommen (Import) und in den Baum eingefügt
	fm_signals[SIGNAL_ROOT_ENTRIES_ADDED] = g_signal_new("root-entries-added",
			G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			G_TYPE_NONE, 0);

	//die Zähler "wird heruntergeladen" und "nicht synchron" im SeaDrive-Laufwerk
	//haben sich geändert (beim Schließen: 0, 0)
	fm_signals[SIGNAL_SEADRIVE_STATUS] = g_signal_new("seadrive-status",
			G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

	//Signale vor und nach Änderungen (Item-Schicht über SondTVFMOwner)
	klass->signal_before_move = g_signal_new("before-move",
			G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			G_TYPE_INT, 6,
			SOND_TYPE_TVFM_ITEM,
			SOND_TYPE_TVFM_ITEM,
			G_TYPE_STRING,
			G_TYPE_INT,
			G_TYPE_POINTER,   /* GError** */
			G_TYPE_POINTER);  /* Transaktions-Kontext (out) */

	klass->signal_before_insert = g_signal_new("before-insert",
			G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			G_TYPE_INT, 5,
			SOND_TYPE_TVFM_ITEM,
			SOND_TYPE_TVFM_ITEM,
			G_TYPE_STRING,
			G_TYPE_INT,
			G_TYPE_POINTER);

	klass->signal_before_delete = g_signal_new("before-delete",
			G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			G_TYPE_INT, 3,
			SOND_TYPE_TVFM_ITEM,
			G_TYPE_POINTER,   /* GError** */
			G_TYPE_POINTER);  /* Transaktions-Kontext (out) */

	klass->signal_after = g_signal_new("after",
			G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			G_TYPE_NONE, 2,
			G_TYPE_BOOLEAN,
			G_TYPE_POINTER);  /* der von before-* gelieferte Kontext */

	klass->signal_after_insert = g_signal_new("after-insert",
			G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			G_TYPE_NONE, 1,
			G_TYPE_BOOLEAN);
}

static void sond_treeviewfm_init(SondTreeviewFM *self) {
	(void) self;
}

GtkWidget* sond_treeviewfm_new(void) {
	return g_object_new(SOND_TYPE_TREEVIEWFM, NULL);
}
