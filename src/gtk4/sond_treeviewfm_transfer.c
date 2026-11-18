/*
 sond (gtk4/sond_treeviewfm_transfer.c) - Akten, Beweisstücke, Unterlagen
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

/* Kopieren, Ausschneiden, Einfügen, Ziehen und Ablegen (Teil von SondTreeviewFM, s. sond_treeviewfm_private.h) */

#include "sond_treeviewfm_private.h"

#include <string.h>
#ifdef G_OS_WIN32
#include <windows.h>
#endif

#include "../sond_log_and_error.h"
#include "../sond_fileparts.h"
#include "../sond_file_helper.h"
#include "sond_seadrive_load_dialog.h"
#include "../tvfm/sond_tvfm_load.h"
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
 * Kopieren, Ausschneiden, Einfügen, Ziehen und Ablegen
 *
 * Quelle und Ziel sind Einträge des Dateisystems (auch SeaDrive); Inhalt von
 * Containern (ZIP, PDF, E-Mail) lässt sich noch nicht verschieben oder kopieren.
 * --------------------------------------------------------------------- */

typedef struct {
	GPtrArray *items; //SondTVFMItem (Referenzen)
	gboolean cut;
	GWeakRef view;
	gboolean view_init;
} FmClipboard;

static FmClipboard fm_clipboard = { NULL, FALSE, { { NULL } }, FALSE };

static void clipboard_clear(void) {
	if (fm_clipboard.items)
		g_ptr_array_unref(fm_clipboard.items);
	fm_clipboard.items = NULL;
	fm_clipboard.cut = FALSE;
}

static void clipboard_set(SondTreeviewFM *self, GPtrArray *items, gboolean cut) {
	if (!fm_clipboard.view_init) {
		g_weak_ref_init(&fm_clipboard.view, NULL);
		fm_clipboard.view_init = TRUE;
	}

	clipboard_clear();
	fm_clipboard.items = items;
	fm_clipboard.cut = cut;
	g_weak_ref_set(&fm_clipboard.view, self);
}

/* Die markierten Einträge ohne solche, deren Vorfahr ebenfalls markiert ist
 * (Referenzen, g_ptr_array_unref()) */
static GPtrArray* selected_top_items(SondTreeviewFM *self) {
	GPtrArray *rows = sond_treeview_get_selected_rows(SOND_TREEVIEW(self));
	GPtrArray *result = g_ptr_array_new_with_free_func(g_object_unref);
	GHashTable *selected = g_hash_table_new(g_direct_hash, g_direct_equal);

	for (guint i = 0; i < rows->len; i++) {
		gpointer item = gtk_tree_list_row_get_item(g_ptr_array_index(rows, i));

		if (item) {
			g_hash_table_add(selected, item);
			g_object_unref(item);
		}
	}

	for (guint i = 0; i < rows->len; i++) {
		GtkTreeListRow *row = g_ptr_array_index(rows, i);
		SondTVFMItem *item = gtk_tree_list_row_get_item(row);
		GtkTreeListRow *ancestor = gtk_tree_list_row_get_parent(row);
		gboolean covered = FALSE;

		while (ancestor && !covered) {
			gpointer ancestor_item = gtk_tree_list_row_get_item(ancestor);
			GtkTreeListRow *next = gtk_tree_list_row_get_parent(ancestor);

			covered = ancestor_item && g_hash_table_contains(selected, ancestor_item);
			if (ancestor_item)
				g_object_unref(ancestor_item);
			g_object_unref(ancestor);
			ancestor = next;
		}
		g_clear_object(&ancestor);

		if (item && !covered)
			g_ptr_array_add(result, item);
		else
			g_clear_object(&item);
	}

	g_hash_table_unref(selected);
	g_ptr_array_unref(rows);

	return result;
}

static gboolean items_all_in_fs(GPtrArray *items) {
	for (guint i = 0; i < items->len; i++)
		if (!sond_tvfm_item_fs_path(g_ptr_array_index(items, i), NULL, NULL))
			return FALSE;

	return items->len > 0;
}

static void copy_or_cut(SondTreeviewFM *self, gboolean cut) {
	GPtrArray *items = selected_top_items(self);

	if (!items_all_in_fs(items)) {
		if (items->len)
			fm_show_error(self, cut ? "Ausschneiden" : "Kopieren",
					"Einträge in Containern lassen sich in dieser Ansicht noch nicht "
					"kopieren oder verschieben.");
		g_ptr_array_unref(items);

		return;
	}

	clipboard_set(self, items, cut);
}

/* Fügt item unter parent ein (Kopie oder Verschieben); bei vergebenem Namen wird
 * ein Zähler angehängt. 0 = ok (*base_inserted: endgültiger Name), 1 = übersprungen,
 * -1 = Fehler. */
static gint paste_one(SondTreeviewFM *self, SondTVFMItem *item, SondTVFMItem *parent,
		gboolean cut, gchar **base_inserted, GError **error) {
	SondTreeviewFMClass *klass = SOND_TREEVIEWFM_GET_CLASS(self);
	SondTVFMItemPrivate *item_priv = sond_tvfm_item_get_priv(item);
	g_autofree gchar *base = NULL;
	gchar const *dot = NULL;
	gboolean has_ext = FALSE;
	gint max_tries = 100;

	base = g_strdup(item_priv->display_name ? item_priv->display_name :
			sond_tvfm_item_get_basename(item));
	if (!base)
		base = g_strdup("UNNAMED");

	dot = strrchr(base, '.');
	has_ext = !item_priv->path_or_section && dot && dot != base;

	for (gint i = 0; i < max_tries; i++) {
		g_autofree gchar *trial = (i == 0) ? g_strdup(base) :
				sond_filename_add_counter(base, (guint) i, has_ext);
		gint rc = 0;

		if (cut)
			rc = sond_tvfm_item_move(item, parent, trial, 0, error);
		else {
			//sond_tvfm_item_copy() löst selbst kein Signal aus (anders als _move())
			gint res = 0;

			g_signal_emit(self, klass->signal_before_insert, 0, item, parent, trial, 0,
					error, &res);
			if (res)
				rc = -1;
			else {
				rc = sond_tvfm_item_copy(item, parent, trial, 0, error);
				g_signal_emit(self, klass->signal_after_insert, 0,
						(rc == 0) ? TRUE : FALSE);
			}
		}

		if (rc == -1) {
			if (g_error_matches(*error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
				g_clear_error(error);

				continue; //nächster Zähler
			}

			return -1;
		}
		if (rc == 1)
			return 1;

		*base_inserted = g_strdup(trial);

		return 0;
	}

	g_set_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS,
			"Kein eindeutiger Zielname nach %d Versuchen", max_tries);

	return -1;
}

//Neuer Eintrag für das eingefügte Objekt (Referenz) am Zielpfad path_new
static SondTVFMItem* pasted_item_new(SondTreeviewFM *self, SondTVFMItem *item,
		gchar const *path_new) {
	SondTVFMItemPrivate *item_priv = sond_tvfm_item_get_priv(item);
	SondFilePart *sfp_new = NULL;
	SondTVFMItem *created = NULL;

	if (item_priv->sond_file_part && !item_priv->path_or_section) {
		SondFilePart *sfp = item_priv->sond_file_part;

		sfp_new = g_object_new(G_OBJECT_TYPE(sfp), NULL);
		sond_file_part_set_path(sfp_new, path_new);
		if (SOND_IS_FILE_PART_LEAF(sfp_new))
			sond_file_part_leaf_set_mime_type(SOND_FILE_PART_LEAF(sfp_new),
					sond_file_part_leaf_get_mime_type(SOND_FILE_PART_LEAF(sfp)));
		sond_file_part_set_has_children(sfp_new, sond_file_part_get_has_children(sfp));
		sond_file_part_set_lock_entry(sfp_new, sond_file_part_get_lock_entry(sfp));
		sond_file_part_set_lock_content(sfp_new, sond_file_part_get_lock_content(sfp));
	}

	created = sond_tvfm_item_create(self, sfp_new,
			item_priv->path_or_section ? path_new : NULL);
	if (sfp_new)
		g_object_unref(sfp_new);

	if (item_priv->path_or_section)
		sond_tvfm_item_get_priv(created)->has_children = item_priv->has_children;

	return created;
}

typedef struct {
	SondTVFMItem *item;
	gchar *old_rel;
} MovedSource;

static void moved_source_free(gpointer data) {
	MovedSource *m = data;

	g_free(m->old_rel);
	g_free(m);
}

static gboolean match_moved_source(gpointer item, gpointer user_data) {
	MovedSource *m = user_data;
	gchar *rel = NULL;
	gboolean match = (item == (gpointer) m->item);

	if (!match && sond_tvfm_item_fs_path(SOND_TVFM_ITEM(item), &rel, NULL))
		match = !g_strcmp0(rel, m->old_rel);
	g_free(rel);

	return match;
}

/* Kopieren liest die Quellen: nicht geladene SeaDrive-Dateien (bei Verzeichnissen alle
 * darunter) werden vorher geladen (Fortschrittsfenster mit Abbrechen). Verschieben im selben
 * Laufwerk benennt nur um und liest nichts. FALSE: Abbruch oder Download gescheitert (schon
 * gemeldet), es geschieht nichts. */
static gboolean paste_preload(SondTreeviewFM *self, GPtrArray *items) {
	GPtrArray *arr = g_ptr_array_new_with_free_func(g_free);
	gboolean ok = TRUE;

	for (guint i = 0; i < items->len; i++)
		sond_tvfm_load_collect(arr, SOND_TVFM_OWNER(self), g_ptr_array_index(items, i));
	if (arr->len > 0)
		ok = sond_seadrive_load_sync(fm_parent_window(self), arr);
	g_ptr_array_unref(arr);

	return ok;
}

/* Fügt items in das Verzeichnis ein, das zur Zeile row gehört: deren Inhalt
 * (child) oder das Verzeichnis, in dem sie steht. cut = verschieben, sonst kopieren. */
static void paste_items(SondTreeviewFM *self, GPtrArray *items, gboolean cut,
		GtkTreeListRow *row, gboolean child) {
	SondTreeview *base = SOND_TREEVIEW(self);
	SondTreeviewFMClass *klass = SOND_TREEVIEWFM_GET_CLASS(self);
	SondTVFMItem *parent = NULL;
	gchar const *parent_rel = NULL;
	GPtrArray *created = g_ptr_array_new_with_free_func(g_object_unref);
	GPtrArray *moved = g_ptr_array_new_with_free_func(moved_source_free);
	GPtrArray *failed = g_ptr_array_new_with_free_func(g_free);
	GError *error = NULL;
	gboolean eingang_changed = FALSE;

	parent = child ? gtk_tree_list_row_get_item(row) : fm_parent_item_of_row(self, row);
	if (!parent || sond_tvfm_item_get_item_type(parent) != SOND_TVFM_ITEM_TYPE_DIR ||
			sond_tvfm_item_get_sond_file_part(parent)) {
		fm_show_error(self, "Einfügen",
				"In diesen Eintrag lässt sich nichts einfügen (nur Verzeichnisse des "
				"Dateisystems).");
		goto out;
	}
	parent_rel = sond_tvfm_item_get_path_or_section(parent);

	if (!cut && !paste_preload(self, items))
		goto out;

	for (guint i = 0; i < items->len; i++) {
		SondTVFMItem *item = g_ptr_array_index(items, i);
		SondTVFMItemPrivate *item_priv = sond_tvfm_item_get_priv(item);
		g_autofree gchar *fs_old = NULL;
		g_autofree gchar *path_old = NULL;
		g_autofree gchar *base_inserted = NULL;
		g_autofree gchar *path_new = NULL;
		gboolean is_dir = FALSE;
		gint rc = 0;

		if (!sond_tvfm_item_fs_path(item, &fs_old, &is_dir)) {
			g_set_error(&error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
					"\"%s\" liegt nicht im Dateisystem", item_priv->display_name ?
							item_priv->display_name : "?");
			break;
		}

		//ein Verzeichnis nicht in sich selbst
		if (is_dir && parent_rel && (!g_strcmp0(parent_rel, fs_old) ||
				(g_str_has_prefix(parent_rel, fs_old) &&
						parent_rel[strlen(fs_old)] == '/'))) {
			g_set_error(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
					"\"%s\" lässt sich nicht in sich selbst einfügen", fs_old);
			break;
		}

		//Verschieben in das eigene Verzeichnis: nichts zu tun
		if (cut) {
			g_autofree gchar *dir = g_path_get_dirname(fs_old);

			if (!g_strcmp0(g_strcmp0(dir, ".") ? dir : "", parent_rel ? parent_rel : ""))
				continue;
		}

		path_old = g_strdup(item_priv->path_or_section);

		rc = paste_one(self, item, parent, cut, &base_inserted, &error);
		if (rc == -1) {
			//nicht geladener SeaDrive-Platzhalter, gesperrte Datei: merken, weiter
			if (sond_error_is_cloud(error) ||
					g_error_matches(error, G_IO_ERROR, G_IO_ERROR_BUSY)) {
				g_ptr_array_add(failed, g_strdup(item_priv->display_name ?
						item_priv->display_name : "(ohne Namen)"));
				g_clear_error(&error);

				continue;
			}

			break;
		}
		if (rc == 1)
			continue;

		path_new = parent_rel ? g_strconcat(parent_rel, "/", base_inserted, NULL) :
				g_strdup(base_inserted);

		//Zusatzangaben (Eingangsstempel) nachführen
		if (klass->fs_entry_moved &&
				klass->fs_entry_moved(self, fs_old, path_new, is_dir, !cut))
			eingang_changed = TRUE;

		//Pfade bereits geöffneter Dateien im verschobenen Verzeichnis nachführen
		if (cut && path_old)
			sond_tvfm_item_adjust_sfps_in_dir(item_priv->sond_file_part, NULL,
					path_old, path_new);

		g_ptr_array_add(created, pasted_item_new(self, item, path_new));

		if (cut) {
			MovedSource *m = g_new0(MovedSource, 1);

			m->item = item;
			m->old_rel = g_strdup(fs_old);
			g_ptr_array_add(moved, m);
		}
	}

	//Anzeige: neue Einträge in die Liste des Ziels
	if (created->len) {
		if (child) {
			SondTVFMItemPrivate *parent_priv = sond_tvfm_item_get_priv(parent);
			GtkTreeListRow *parent_row = g_object_ref(row);

			if (!parent_priv->has_children) {
				//das Verzeichnis war leer, jetzt ist es aufklappbar: Zeile neu aufbauen
				SondLazyChildren *lazy = fm_lazy_from_model(
						sond_treeview_row_get_container(base, row));

				parent_priv->has_children = TRUE;
				if (lazy) {
					fm_lazy_touch(lazy, parent);
					g_object_unref(parent_row);
					parent_row = sond_treeview_find_row(base, fm_match_same, parent, NULL);
				}
			}

			if (parent_row) {
				if (gtk_tree_list_row_get_expanded(parent_row)) {
					SondLazyChildren *lazy = fm_lazy_from_model(
							gtk_tree_list_row_get_children(parent_row));

					for (guint i = 0; lazy && i < created->len; i++)
						fm_lazy_insert(lazy, g_ptr_array_index(created, i));
				} else
					gtk_tree_list_row_set_expanded(parent_row, TRUE); //liest von der Platte
				g_object_unref(parent_row);
			}
		} else {
			SondLazyChildren *lazy = fm_lazy_from_model(
					sond_treeview_row_get_container(base, row));

			for (guint i = 0; lazy && i < created->len; i++)
				fm_lazy_insert(lazy, g_ptr_array_index(created, i));
		}
		sond_treeview_select_item(base, g_ptr_array_index(created, created->len - 1));
	}

	//Quellen beim Verschieben aus der Anzeige nehmen
	for (guint i = 0; i < moved->len; i++) {
		MovedSource *m = g_ptr_array_index(moved, i);
		GtkTreeListRow *source_row = sond_treeview_find_row(base, match_moved_source, m,
				NULL);

		if (source_row) {
			SondLazyChildren *lazy = fm_lazy_from_model(
					sond_treeview_row_get_container(base, source_row));

			if (lazy) {
				SondTVFMItem *shown = gtk_tree_list_row_get_item(source_row);

				fm_lazy_remove(lazy, shown);
				g_object_unref(shown);
			}
			g_object_unref(source_row);
		}
	}

	if (eingang_changed)
		sond_treeviewfm_refresh_status(self);

out:
	if (error || failed->len) {
		GString *text = g_string_new(error ? error->message : "");

		if (failed->len) {
			if (text->len)
				g_string_append(text, "\n\n");
			g_string_append_printf(text, "%u %s konnten nicht %s werden "
					"(nicht geladen oder in Benutzung):", failed->len,
					failed->len == 1 ? "Eintrag" : "Einträge",
					cut ? "verschoben" : "kopiert");
			for (guint i = 0; i < MIN(failed->len, 5); i++)
				g_string_append_printf(text, "\n%s",
						(gchar const*) g_ptr_array_index(failed, i));
			if (failed->len > 5)
				g_string_append_printf(text, "\n... und %u weitere", failed->len - 5);
		}
		fm_show_error(self, cut ? "Verschieben" : "Einfügen", text->str);
		g_string_free(text, TRUE);
	}

	g_clear_error(&error);
	g_clear_object(&parent);
	g_ptr_array_unref(created);
	g_ptr_array_unref(moved);
	g_ptr_array_unref(failed);
}

static void paste_clipboard(SondTreeviewFM *self, gboolean child_forced, gboolean child) {
	GPtrArray *rows = sond_treeview_get_selected_rows(SOND_TREEVIEW(self));
	SondTreeviewFM *source = fm_clipboard.view_init ?
			g_weak_ref_get(&fm_clipboard.view) : NULL;

	if (!fm_clipboard.items || !fm_clipboard.items->len) {
		//nichts zu tun
	} else if (source != self) {
		fm_show_error(self, "Einfügen", "Die Zwischenablage stammt aus einer anderen "
				"Ansicht.");
	} else if (rows->len != 1) {
		fm_show_error(self, "Einfügen", "Bitte genau einen Eintrag als Ziel markieren.");
	} else {
		GtkTreeListRow *row = g_ptr_array_index(rows, 0);
		GPtrArray *items = g_ptr_array_ref(fm_clipboard.items);
		gboolean cut = fm_clipboard.cut;

		if (!child_forced) {
			//ein markiertes Verzeichnis: hinein, sonst auf gleicher Ebene
			SondTVFMItem *selected = gtk_tree_list_row_get_item(row);

			child = selected &&
					sond_tvfm_item_get_item_type(selected) == SOND_TVFM_ITEM_TYPE_DIR &&
					!sond_tvfm_item_get_sond_file_part(selected);
			g_clear_object(&selected);
		}

		paste_items(self, items, cut, row, child);
		g_ptr_array_unref(items);
		if (cut)
			clipboard_clear();
	}

	g_clear_object(&source);
	g_ptr_array_unref(rows);
}

void fm_action_copy(GSimpleAction *action, GVariant *parameter, gpointer data) {
	(void) action;
	(void) parameter;

	copy_or_cut(SOND_TREEVIEWFM(data), FALSE);
}

void fm_action_cut(GSimpleAction *action, GVariant *parameter, gpointer data) {
	(void) action;
	(void) parameter;

	copy_or_cut(SOND_TREEVIEWFM(data), TRUE);
}

void fm_action_paste(GSimpleAction *action, GVariant *parameter, gpointer data) {
	(void) action;
	(void) parameter;

	paste_clipboard(SOND_TREEVIEWFM(data), FALSE, FALSE);
}

void fm_action_paste_same(GSimpleAction *action, GVariant *parameter, gpointer data) {
	(void) action;
	(void) parameter;

	paste_clipboard(SOND_TREEVIEWFM(data), TRUE, FALSE);
}

void fm_action_paste_child(GSimpleAction *action, GVariant *parameter, gpointer data) {
	(void) action;
	(void) parameter;

	paste_clipboard(SOND_TREEVIEWFM(data), TRUE, TRUE);
}

/* --- Ziehen und Ablegen (innerhalb einer Ansicht): Verschieben, mit Strg Kopieren --- */

static GPtrArray *dnd_items = NULL;
static SondTreeviewFM *dnd_view = NULL;
static gboolean dnd_copy = FALSE; //Strg beim Ziehen gedrückt

#define DND_LISTITEM_KEY "sond-dnd-listitem"

static SondTreeviewFM* dnd_view_of(GtkEventController *controller) {
	GtkWidget *widget = gtk_event_controller_get_widget(controller);

	return SOND_TREEVIEWFM(gtk_widget_get_ancestor(widget, SOND_TYPE_TREEVIEWFM));
}

static GdkContentProvider* dnd_prepare(GtkDragSource *source, double x, double y,
		gpointer data) {
	GtkListItem *listitem = g_object_get_data(G_OBJECT(source), DND_LISTITEM_KEY);
	SondTreeviewFM *view = dnd_view_of(GTK_EVENT_CONTROLLER(source));
	SondTVFMItem *item = listitem ? sond_treeview_listitem_get_item(listitem) : NULL;
	GPtrArray *items = NULL;

	(void) x;
	(void) y;
	(void) data;

	if (!view || !item) {
		g_clear_object(&item);

		return NULL;
	}

	if (gtk_list_item_get_selected(listitem))
		items = selected_top_items(view);
	else {
		items = g_ptr_array_new_with_free_func(g_object_unref);
		g_ptr_array_add(items, g_object_ref(item));
	}
	g_object_unref(item);

	if (!items_all_in_fs(items)) {
		g_ptr_array_unref(items);

		return NULL;
	}

	if (dnd_items)
		g_ptr_array_unref(dnd_items);
	dnd_items = items;
	dnd_view = view;

	//Nutzlast: die Einträge (GPtrArray von SondTVFMItem); Ziele anderer Ansichten
	//(Zond: Anbinden im Inhaltsbaum) nehmen denselben Typ an
	return gdk_content_provider_new_typed(G_TYPE_PTR_ARRAY, items);
}

static void dnd_begin(GtkDragSource *source, GdkDrag *drag, gpointer data) {
	GtkWidget *label = NULL;
	gchar *text = NULL;

	(void) source;
	(void) data;

	if (!dnd_items || !dnd_items->len)
		return;

	text = dnd_items->len == 1 ?
			g_strdup(sond_tvfm_item_get_display_name(g_ptr_array_index(dnd_items, 0))) :
			g_strdup_printf("%u Einträge", dnd_items->len);
	label = gtk_label_new(text);
	gtk_widget_add_css_class(label, "app-notification");
	gtk_drag_icon_set_child(GTK_DRAG_ICON(gtk_drag_icon_get_for_drag(drag)), label);
	g_free(text);
}

static void dnd_end(GtkDragSource *source, GdkDrag *drag, gboolean delete_data,
		gpointer data) {
	(void) source;
	(void) drag;
	(void) delete_data;
	(void) data;

	if (dnd_items)
		g_ptr_array_unref(dnd_items);
	dnd_items = NULL;
	dnd_view = NULL;
}

/* Ist Strg gedrückt? Unter Windows läuft der Ziehvorgang in der Schleife von OLE,
 * GTK erhält dabei keine Tastaturereignisse: Ereigniszustand und Tastaturgerät
 * melden Strg nie (gemessen), nur das System weiß es. */
static gboolean ctrl_pressed(GtkEventController *controller) {
#ifdef G_OS_WIN32
	(void) controller;

	return (GetAsyncKeyState(VK_CONTROL) & 0x8000) ? TRUE : FALSE;
#else
	return (gtk_event_controller_get_current_event_state(controller) &
			GDK_CONTROL_MASK) ? TRUE : FALSE;
#endif
}

static GdkDragAction dnd_motion(GtkDropTarget *target, double x, double y,
		gpointer data) {
	(void) x;
	(void) y;
	(void) data;

	dnd_copy = ctrl_pressed(GTK_EVENT_CONTROLLER(target));

	return dnd_copy ? GDK_ACTION_COPY : GDK_ACTION_MOVE;
}

typedef struct {
	SondTreeviewFM *view;   //Referenz
	GPtrArray *items;       //Referenz
	GtkTreeListRow *row;    //Referenz
	gboolean cut;
	gboolean child;
} DropJob;

//erst nach Ende des Ziehvorgangs ausführen: beim Verschieben verschwindet die
//Quellzeile, solange der Ziehvorgang noch läuft
static gboolean dnd_drop_idle(gpointer data) {
	DropJob *job = data;

	paste_items(job->view, job->items, job->cut, job->row, job->child);
	g_object_unref(job->view);
	g_ptr_array_unref(job->items);
	g_object_unref(job->row);
	g_free(job);

	return G_SOURCE_REMOVE;
}

static gboolean dnd_drop(GtkDropTarget *target, const GValue *value, double x, double y,
		gpointer data) {
	GtkListItem *listitem = g_object_get_data(G_OBJECT(target), DND_LISTITEM_KEY);
	SondTreeviewFM *view = dnd_view_of(GTK_EVENT_CONTROLLER(target));
	GtkTreeListRow *row = listitem ? gtk_list_item_get_item(listitem) : NULL;
	GPtrArray *items = G_VALUE_HOLDS(value, G_TYPE_PTR_ARRAY) ?
			g_value_get_boxed(value) : NULL;
	SondTVFMItem *item = NULL;
	DropJob *job = NULL;
	GdkDrop *drop = gtk_drop_target_get_current_drop(target);
	GdkDrag *drag = drop ? gdk_drop_get_drag(drop) : NULL;
	gboolean copy = dnd_copy;

	(void) x;
	(void) y;
	(void) data;

	if (!view || !row || !items || dnd_view != view)
		return FALSE;

	if ((drag && gdk_drag_get_selected_action(drag) == GDK_ACTION_COPY) ||
			ctrl_pressed(GTK_EVENT_CONTROLLER(target)))
		copy = TRUE;

	item = gtk_tree_list_row_get_item(row);
	job = g_new0(DropJob, 1);
	job->view = g_object_ref(view);
	job->items = g_ptr_array_ref(items);
	job->row = g_object_ref(row);
	job->cut = !copy;
	job->child = item && sond_tvfm_item_get_item_type(item) == SOND_TVFM_ITEM_TYPE_DIR &&
			!sond_tvfm_item_get_sond_file_part(item);
	g_clear_object(&item);
	g_idle_add(dnd_drop_idle, job);

	return TRUE;
}

void fm_dnd_attach(GtkWidget *cell, GtkListItem *listitem) {
	GtkDragSource *source = gtk_drag_source_new();
	GtkDropTarget *target = gtk_drop_target_new(G_TYPE_PTR_ARRAY,
			GDK_ACTION_MOVE | GDK_ACTION_COPY);

	gtk_drag_source_set_actions(source, GDK_ACTION_MOVE | GDK_ACTION_COPY);
	g_object_set_data(G_OBJECT(source), DND_LISTITEM_KEY, listitem);
	g_signal_connect(source, "prepare", G_CALLBACK(dnd_prepare), NULL);
	g_signal_connect(source, "drag-begin", G_CALLBACK(dnd_begin), NULL);
	g_signal_connect(source, "drag-end", G_CALLBACK(dnd_end), NULL);
	gtk_widget_add_controller(cell, GTK_EVENT_CONTROLLER(source));

	g_object_set_data(G_OBJECT(target), DND_LISTITEM_KEY, listitem);
	g_signal_connect(target, "motion", G_CALLBACK(dnd_motion), NULL);
	g_signal_connect(target, "drop", G_CALLBACK(dnd_drop), NULL);
	gtk_widget_add_controller(cell, GTK_EVENT_CONTROLLER(target));
}

