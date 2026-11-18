/*
 sond (gtk4/sond_treeviewfm_edit.c) - Akten, Beweisstücke, Unterlagen
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

/* Neues Verzeichnis, Umbenennen, Löschen, Öffnen (Teil von SondTreeviewFM, s. sond_treeviewfm_private.h) */

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
 * Bearbeiten: Neues Verzeichnis, Umbenennen, Löschen (über die Item-Schicht, mit
 * denselben Signalen wie in der GTK3-Fassung)
 * --------------------------------------------------------------------- */

/* --- Neues Verzeichnis --- */

static void make_directory(SondTreeviewFM *self, gboolean child) {
	SondTreeview *base = SOND_TREEVIEW(self);
	GPtrArray *rows = sond_treeview_get_selected_rows(base);
	GtkTreeListRow *row = NULL;
	SondTVFMItem *selected = NULL;
	SondTVFMItem *parent = NULL;
	SondTVFMItem *created = NULL;
	gchar *path = NULL;
	GError *error = NULL;

	if (rows->len != 1) {
		fm_show_error(self, "Verzeichnis anlegen", "Bitte genau einen Eintrag markieren.");
		g_ptr_array_unref(rows);

		return;
	}

	row = g_ptr_array_index(rows, 0);
	selected = gtk_tree_list_row_get_item(row);
	parent = child ? g_object_ref(selected) : fm_parent_item_of_row(self, row);

	if (sond_tvfm_item_get_item_type(parent) != SOND_TVFM_ITEM_TYPE_DIR) {
		fm_show_error(self, "Verzeichnis anlegen",
				"In diesem Eintrag kann kein Verzeichnis angelegt werden.");
		goto out;
	}

	if (sond_tvfm_item_mkdir(parent, child, &path, &error)) {
		fm_show_error(self, "Verzeichnis kann nicht eingefügt werden",
				error ? error->message : "unbekannt");
		goto out;
	}

	if (!child) {
		//gleiche Ebene: in die Liste des Eintrags einfügen und markieren
		SondLazyChildren *lazy = fm_lazy_from_model(sond_treeview_row_get_container(base, row));

		created = sond_tvfm_item_create(self, sond_tvfm_item_get_sond_file_part(parent),
				path);
		if (lazy) {
			fm_lazy_insert(lazy, created);
			sond_treeview_select_item(base, created);
		}
	} else {
		//Unterebene
		SondTVFMItemPrivate *parent_priv = sond_tvfm_item_get_priv(parent);
		GtkTreeListRow *parent_row = g_object_ref(row);

		if (!parent_priv->has_children) {
			//der Eintrag war leer, jetzt ist er aufklappbar: Zeile neu aufbauen
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
				//schon aufgeklappt: den neuen Eintrag ergänzen
				SondLazyChildren *lazy = fm_lazy_from_model(
						gtk_tree_list_row_get_children(parent_row));

				created = sond_tvfm_item_create(self,
						sond_tvfm_item_get_sond_file_part(parent), path);
				if (lazy)
					fm_lazy_insert(lazy, created);
			} else {
				//das Aufklappen liest das Verzeichnis, der neue Eintrag ist dabei
				gtk_tree_list_row_set_expanded(parent_row, TRUE);
			}
			g_object_unref(parent_row);
		}
		sond_treeview_select_matching(base, fm_match_path, path);
	}

out:
	g_free(path);
	g_clear_error(&error);
	g_clear_object(&created);
	g_clear_object(&parent);
	g_clear_object(&selected);
	g_ptr_array_unref(rows);
}

void fm_action_mkdir_same(GSimpleAction *action, GVariant *parameter, gpointer data) {
	(void) action;
	(void) parameter;

	make_directory(SOND_TREEVIEWFM(data), FALSE);
}

void fm_action_mkdir_child(GSimpleAction *action, GVariant *parameter, gpointer data) {
	(void) action;
	(void) parameter;

	make_directory(SOND_TREEVIEWFM(data), TRUE);
}

/* --- Umbenennen --- */

/* Benennt den Eintrag um (Prüfung des Namens, Signale vor und nach der Änderung,
 * Nachführen von Zusatzangaben und geöffneten Dateien). 0 bei Erfolg, -1 bei Fehler. */
static gint rename_item(SondTreeviewFM *self, SondTVFMItem *item, SondTVFMItem *parent,
		gchar const *text_new, GError **error) {
	SondTreeviewFMClass *klass = SOND_TREEVIEWFM_GET_CLASS(self);
	SondTVFMItemPrivate *item_priv = sond_tvfm_item_get_priv(item);
	gint res = 0;
	gint rc = 0;
	gpointer ctx = NULL;
	g_autofree gchar *path_old = NULL;
	g_autofree gchar *fs_old = NULL;
	g_autofree gchar *fs_new = NULL;
	gboolean fs_is_dir = FALSE;
	gboolean fs_known = FALSE;

	if (!sond_tvfm_item_is_valid_filename(text_new)) {
		g_set_error(error, g_quark_from_static_string("sond"), 0,
				"Der Name ist leer oder enthält unzulässige Zeichen");

		return -1;
	}

	/* index_to -1: Umbenennen an Ort und Stelle, kein Verschieben */
	g_signal_emit(self, klass->signal_before_move, 0, item, parent, text_new, -1,
			error, &ctx, &res);
	if (res)
		return -1;

	/* Verzeichnis: alten Pfad sichern - sond_tvfm_item_rename() setzt path_or_section
	 * auf den neuen (vollen) Pfad */
	path_old = g_strdup(item_priv->path_or_section);

	//Pfad vor dem Umbenennen merken (für die Nachführung von Zusatzangaben)
	fs_known = sond_tvfm_item_fs_path(item, &fs_old, &fs_is_dir);

	rc = sond_tvfm_item_rename(item, parent, text_new, error);
	g_signal_emit(self, klass->signal_after, 0, (rc == 0) ? TRUE : FALSE, ctx);
	if (rc)
		return -1;

	//Zusatzangaben (Eingangsstempel) auf den neuen Pfad nachführen
	if (fs_known && sond_tvfm_item_fs_path(item, &fs_new, NULL) &&
			g_strcmp0(fs_old, fs_new) && klass->fs_entry_renamed)
		klass->fs_entry_renamed(self, fs_old, fs_new, fs_is_dir);

	//Pfade bereits geöffneter Dateien darin nachführen
	if (path_old && item_priv->path_or_section)
		sond_tvfm_item_adjust_sfps_in_dir(item_priv->sond_file_part,
				item_priv->sond_file_part, path_old, item_priv->path_or_section);

	g_free(item_priv->display_name);
	item_priv->display_name = g_strdup(text_new);

	return 0;
}

typedef struct {
	SondTreeviewFM *self; //Referenz
	SondTVFMItem *item;
	SondTVFMItem *parent;
	SondLazyChildren *container;
	GtkWidget *window;
	GtkWidget *entry;
} RenameDialog;

static void rename_dialog_free(RenameDialog *d) {
	g_clear_object(&d->self);
	g_clear_object(&d->item);
	g_clear_object(&d->parent);
	g_clear_object(&d->container);
	g_free(d);
}

static void rename_dialog_ok(GtkWidget *widget, gpointer user_data) {
	RenameDialog *d = user_data;
	gchar *text = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(d->entry))));
	GError *error = NULL;

	(void) widget;

	if (g_strcmp0(text, sond_tvfm_item_get_display_name(d->item))) {
		if (rename_item(d->self, d->item, d->parent, text, &error)) {
			//das Schließen gibt d frei: vorher alles Nötige sichern
			SondTreeviewFM *self = g_object_ref(d->self);
			gchar *message = g_strdup(error ? error->message : "unbekannt");

			g_clear_error(&error);
			g_free(text);
			gtk_window_close(GTK_WINDOW(d->window));
			fm_show_error(self, "Umbenennen nicht möglich", message);
			g_free(message);
			g_object_unref(self);

			return;
		}
		if (d->container) {
			fm_lazy_touch(d->container, d->item); //neu anzeigen und einsortieren
			sond_treeview_select_item(SOND_TREEVIEW(d->self), d->item);
		}
	}

	g_free(text);
	gtk_window_close(GTK_WINDOW(d->window));
}

static void rename_dialog_cancel(GtkWidget *widget, gpointer user_data) {
	RenameDialog *d = user_data;

	(void) widget;

	gtk_window_close(GTK_WINDOW(d->window));
}

static void rename_dialog_destroyed(GtkWidget *widget, gpointer user_data) {
	(void) widget;

	rename_dialog_free(user_data);
}

static void rename_selected(SondTreeviewFM *self) {
	SondTreeview *base = SOND_TREEVIEW(self);
	GPtrArray *rows = sond_treeview_get_selected_rows(base);
	RenameDialog *d = NULL;
	GtkWidget *box = NULL;
	GtkWidget *buttons = NULL;
	GtkWidget *ok = NULL;
	GtkWidget *cancel = NULL;
	gchar const *name = NULL;
	gchar const *dot = NULL;

	if (rows->len != 1) {
		fm_show_error(self, "Umbenennen", "Bitte genau einen Eintrag markieren.");
		g_ptr_array_unref(rows);

		return;
	}

	d = g_new0(RenameDialog, 1);
	d->self = g_object_ref(self);
	d->item = gtk_tree_list_row_get_item(g_ptr_array_index(rows, 0));
	d->parent = fm_parent_item_of_row(self, g_ptr_array_index(rows, 0));
	d->container = fm_lazy_from_model(sond_treeview_row_get_container(base,
			g_ptr_array_index(rows, 0)));
	if (d->container)
		g_object_ref(d->container);
	g_ptr_array_unref(rows);

	name = sond_tvfm_item_get_display_name(d->item);

	d->window = gtk_window_new();
	gtk_window_set_title(GTK_WINDOW(d->window), "Umbenennen");
	gtk_window_set_modal(GTK_WINDOW(d->window), TRUE);
	gtk_window_set_transient_for(GTK_WINDOW(d->window), fm_parent_window(self));
	gtk_window_set_default_size(GTK_WINDOW(d->window), 420, -1);

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_widget_set_margin_start(box, 14);
	gtk_widget_set_margin_end(box, 14);
	gtk_widget_set_margin_top(box, 14);
	gtk_widget_set_margin_bottom(box, 14);
	gtk_window_set_child(GTK_WINDOW(d->window), box);

	d->entry = gtk_entry_new();
	gtk_editable_set_text(GTK_EDITABLE(d->entry), name ? name : "");
	gtk_entry_set_activates_default(GTK_ENTRY(d->entry), TRUE);
	gtk_box_append(GTK_BOX(box), d->entry);

	buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_halign(buttons, GTK_ALIGN_END);
	gtk_box_append(GTK_BOX(box), buttons);

	cancel = gtk_button_new_with_label("Abbrechen");
	g_signal_connect(cancel, "clicked", G_CALLBACK(rename_dialog_cancel), d);
	gtk_box_append(GTK_BOX(buttons), cancel);

	ok = gtk_button_new_with_label("Ok");
	g_signal_connect(ok, "clicked", G_CALLBACK(rename_dialog_ok), d);
	gtk_box_append(GTK_BOX(buttons), ok);
	gtk_window_set_default_widget(GTK_WINDOW(d->window), ok);

	g_signal_connect(d->window, "destroy", G_CALLBACK(rename_dialog_destroyed), d);

	gtk_window_present(GTK_WINDOW(d->window));

	//ohne die Endung markieren (bei einem Verzeichnis den ganzen Namen)
	dot = name ? strrchr(name, '.') : NULL;
	gtk_widget_grab_focus(d->entry);
	if (dot && dot != name && !(sond_tvfm_item_get_item_type(d->item) ==
			SOND_TVFM_ITEM_TYPE_DIR && !sond_tvfm_item_get_sond_file_part(d->item)))
		gtk_editable_select_region(GTK_EDITABLE(d->entry), 0,
				(gint) g_utf8_pointer_to_offset(name, dot));
	else
		gtk_editable_select_region(GTK_EDITABLE(d->entry), 0, -1);
}

void fm_action_rename(GSimpleAction *action, GVariant *parameter, gpointer data) {
	(void) action;
	(void) parameter;

	rename_selected(SOND_TREEVIEWFM(data));
}

/* --- Löschen --- */

/* Löscht die Markierten. Ist ein Verzeichnis und etwas darin markiert, wird nur das
 * Verzeichnis gelöscht. Beim ersten Fehler ist Schluss (wie in GTK3). */
static void delete_selected(SondTreeviewFM *self) {
	SondTreeview *base = SOND_TREEVIEW(self);
	SondTreeviewFMClass *klass = SOND_TREEVIEWFM_GET_CLASS(self);
	GPtrArray *rows = sond_treeview_get_selected_rows(base);
	GHashTable *selected = g_hash_table_new(g_direct_hash, g_direct_equal);
	GError *error = NULL;

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
		gint res = 0;
		gint rc = 0;
		gpointer ctx = NULL;
		gchar *fs_rel = NULL;
		gboolean fs_is_dir = FALSE;
		gboolean fs_known = FALSE;

		//Vorfahr ebenfalls markiert: mit ihm erledigt
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
		if (covered || !item) {
			g_clear_object(&item);

			continue;
		}

		g_signal_emit(self, klass->signal_before_delete, 0, item, &error, &ctx, &res);
		if (res == 1) {
			g_object_unref(item);

			continue;
		}
		if (res == -1) {
			g_object_unref(item);
			break;
		}

		//Pfad vor dem Löschen merken (danach lässt sich der Eintrag nicht mehr abfragen)
		fs_known = sond_tvfm_item_fs_path(item, &fs_rel, &fs_is_dir);

		rc = sond_tvfm_item_delete(item, &error);
		g_signal_emit(self, klass->signal_after, 0, (rc == 0) ? TRUE : FALSE, ctx);
		if (rc) {
			g_free(fs_rel);
			g_object_unref(item);
			break;
		}

		if (fs_known && klass->fs_entry_deleted)
			klass->fs_entry_deleted(self, fs_rel, fs_is_dir);
		g_free(fs_rel);

		{
			SondLazyChildren *lazy = fm_lazy_from_model(
					sond_treeview_row_get_container(base, row));

			if (lazy)
				fm_lazy_remove(lazy, item);
		}
		g_object_unref(item);
	}

	if (error) {
		fm_show_error(self, "Löschen nicht möglich", error->message);
		g_clear_error(&error);
	}

	g_hash_table_unref(selected);
	g_ptr_array_unref(rows);
}

static void delete_confirmed(GObject *source, GAsyncResult *result, gpointer user_data) {
	SondTreeviewFM *self = SOND_TREEVIEWFM(user_data);
	GError *error = NULL;
	gint button = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result, &error);

	g_clear_error(&error);
	if (button == 0)
		delete_selected(self);

	g_object_unref(self);
}

void fm_action_delete(GSimpleAction *action, GVariant *parameter, gpointer data) {
	SondTreeviewFM *self = SOND_TREEVIEWFM(data);
	GPtrArray *items = sond_treeview_get_selected_items(SOND_TREEVIEW(self));
	GtkAlertDialog *dialog = NULL;
	gchar *text = NULL;
	const char *buttons[] = { "Löschen", "Abbrechen", NULL };

	(void) action;
	(void) parameter;

	if (items->len == 0) {
		g_ptr_array_unref(items);

		return;
	}

	if (!SOND_TREEVIEWFM_GET_CLASS(self)->confirm_delete) {
		g_ptr_array_unref(items);
		delete_selected(self);

		return;
	}

	text = items->len == 1 ?
			g_strdup_printf("\"%s\" löschen?", sond_tvfm_item_get_display_name(
					g_ptr_array_index(items, 0))) :
			g_strdup_printf("%u Einträge löschen?", items->len);
	g_ptr_array_unref(items);

	dialog = gtk_alert_dialog_new("%s", text);
	gtk_alert_dialog_set_detail(dialog, "Das lässt sich nicht rückgängig machen. "
			"Verzeichnisse werden mit ihrem Inhalt gelöscht.");
	gtk_alert_dialog_set_buttons(dialog, buttons);
	gtk_alert_dialog_set_default_button(dialog, 1);
	gtk_alert_dialog_set_cancel_button(dialog, 1);
	gtk_alert_dialog_set_modal(dialog, TRUE);
	gtk_alert_dialog_choose(dialog, fm_parent_window(self), NULL, delete_confirmed,
			g_object_ref(self));
	g_object_unref(dialog);
	g_free(text);
}

/* ------------------------------------------------------------------------
 * Öffnen
 * --------------------------------------------------------------------- */

gint fm_default_open_item(SondTreeviewFM *self, SondTVFMItem *item,
		gboolean open_with, GError **error) {
	gboolean is_dir = FALSE;
	gchar *full = fm_item_full_path(self, item, &is_dir);
	gboolean ok = FALSE;

	if (!full) {
		//Eintrag in einem Container (ZIP-Eintrag, PDF-Anhang, E-Mail-Teil): über seinen Filepart
		SondFilePart *sfp = sond_tvfm_item_get_sond_file_part(item);

		if (!sfp) {
			g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
					"Dieser Eintrag lässt sich nicht öffnen");

			return -1;
		}

		return sond_file_part_open_external(sfp, open_with, error) ? -1 : 0;
	}

	ok = sond_open(full, open_with, error);
	g_free(full);

	return ok ? 0 : -1;
}

/* Ist die Datei des Eintrags (bei Teilen in Containern die oberste) ein nicht geladener
 * SeaDrive-Platzhalter, wird sie zuerst geladen (Fortschrittsfenster mit Abbrechen). *loaded:
 * es wurde geladen, der Baum kann sich währenddessen geändert haben. FALSE: Abbruch oder
 * Download gescheitert (schon gemeldet). */
static gboolean load_item_file(SondTreeviewFM *self, SondTVFMItem *item, gboolean *loaded) {
	SondFilePart *sfp = sond_tvfm_item_get_sond_file_part(item);
	gchar *path = NULL;
	gboolean ok = TRUE;

	*loaded = FALSE;
	if (!sfp || !sond_tvfm_load_not_loaded(SOND_TVFM_OWNER(self), sfp))
		return TRUE;

	path = sond_tvfm_load_root_file_path(SOND_TVFM_OWNER(self), sfp);
	ok = sond_seadrive_load_file_sync(fm_parent_window(self), path);
	g_free(path);
	*loaded = ok;

	return ok;
}

/* Gesperrtes Element (verschlüsselter ZIP-Eintrag, PDF mit Passwort): entsperren (Dialog) und
 * die Zeile durch eine mit dem echten Typ ersetzen. Mit externem Programm (open_with) nur ein
 * Platzhalter-Eintrag, die PDF-Passwortabfrage übernimmt dann das Programm selbst. *item wird
 * durch das neue Element ersetzt (Referenz). 0 = weiter, -1 = Fehler (error; bei Abbruch durch
 * den Nutzer G_IO_ERROR_CANCELLED). */
static gint unlock_before_open(SondTreeviewFM *self, SondTVFMItem **item, gboolean open_with,
		GError **error) {
	SondTVFMItemPrivate *priv = sond_tvfm_item_get_priv(*item);
	SondFilePart *sfp_new = NULL;
	SondTVFMItem *item_new = NULL;

	if (priv->path_or_section || !priv->sond_file_part)
		return 0;
	if (!(open_with ? sond_file_part_is_placeholder(priv->sond_file_part) :
			sond_file_part_needs_unlock(priv->sond_file_part)))
		return 0;

	sfp_new = sond_file_part_unlock(priv->sond_file_part, error);
	if (!sfp_new)
		return -1;

	item_new = sond_tvfm_item_create(self, sfp_new, NULL);
	g_object_unref(sfp_new);
	fm_replace_item(self, *item, item_new);
	g_object_unref(*item);
	*item = item_new;

	return 0;
}

static void open_row(SondTreeviewFM *self, GtkTreeListRow *row, gboolean open_with) {
	SondTVFMItem *item = gtk_tree_list_row_get_item(row);
	GError *error = NULL;
	gboolean loaded = FALSE;

	if (!item)
		return;

	if (!load_item_file(self, item, &loaded)) {
		g_object_unref(item);

		return;
	}

	if (sond_tvfm_item_get_item_type(item) != SOND_TVFM_ITEM_TYPE_DIR) {
		if (unlock_before_open(self, &item, open_with, &error)) {
			if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
				fm_show_error(self, "Fehler beim Öffnen",
						error ? error->message : "unbekannt");
			g_clear_error(&error);
			g_object_unref(item);

			return;
		}

		//jetzt ein Container: der Nutzer klappt ihn selbst auf
		if (sond_tvfm_item_get_item_type(item) == SOND_TVFM_ITEM_TYPE_DIR) {
			g_object_unref(item);

			return;
		}
	}

	if (sond_tvfm_item_get_item_type(item) == SOND_TVFM_ITEM_TYPE_DIR) {
		//Doppelklick auf ein Verzeichnis: auf- bzw. zuklappen. Nach dem Laden die Zeile
		//neu suchen, der Baum kann sich währenddessen geändert haben.
		GtkTreeListRow *row_now = loaded ?
				sond_treeview_find_row(SOND_TREEVIEW(self), fm_match_same, item, NULL) :
				g_object_ref(row);

		if (row_now && !open_with && gtk_tree_list_row_is_expandable(row_now))
			gtk_tree_list_row_set_expanded(row_now,
					!gtk_tree_list_row_get_expanded(row_now));
		g_clear_object(&row_now);
		g_object_unref(item);

		return;
	}

	if (SOND_TREEVIEWFM_GET_CLASS(self)->open_item &&
			SOND_TREEVIEWFM_GET_CLASS(self)->open_item(self, item, open_with, &error))
		fm_show_error(self, "Fehler beim Öffnen", error ? error->message : "unbekannt");
	g_clear_error(&error);
	g_object_unref(item);
}

static void open_selected(SondTreeviewFM *self, gboolean open_with) {
	GPtrArray *rows = sond_treeview_get_selected_rows(SOND_TREEVIEW(self));

	if (rows->len == 1)
		open_row(self, g_ptr_array_index(rows, 0), open_with);
	g_ptr_array_unref(rows);
}

void fm_action_open(GSimpleAction *action, GVariant *parameter, gpointer data) {
	(void) action;
	(void) parameter;

	open_selected(SOND_TREEVIEWFM(data), FALSE);
}

void fm_action_open_with(GSimpleAction *action, GVariant *parameter, gpointer data) {
	(void) action;
	(void) parameter;

	open_selected(SOND_TREEVIEWFM(data), TRUE);
}

void fm_on_activate(GtkColumnView *column_view, guint position, gpointer data) {
	GListModel *model = G_LIST_MODEL(gtk_column_view_get_model(column_view));
	GtkTreeListRow *row = g_list_model_get_item(model, position);

	if (row) {
		open_row(SOND_TREEVIEWFM(data), row, FALSE);
		g_object_unref(row);
	}
}

