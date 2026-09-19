/*
 sond (sond_treeviewfm.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2022  pelo america

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

#include "sond_treeviewfm.h"

#include <dirent.h>
#include <glib.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "misc.h"
#include "misc_stdlib.h"
#include "sond_log_and_error.h"
#include "sond_result_view.h"
#include "sond_renderer.h"
#include "sond_fileparts.h"
#include "sond_file_helper.h"
#include "sond_ocr.h"
#include "sond_index.h"
#include "sond_mime.h"
#include "sond_process_file.h"
#include "sond_seadrive.h"
#include "sond_icon_util.h"
#include "sond_treeviewfm_private.h"

G_DEFINE_TYPE_WITH_PRIVATE(SondTreeviewFM, sond_treeviewfm, SOND_TYPE_TREEVIEW)

/* Freund-Accessor für sond_seadrive.c - s. ausführl. Kommentar in
 * sond_treeviewfm_private.h. */
SondTreeviewFMPrivate *sond_treeviewfm_get_priv(SondTreeviewFM *stvfm) {
	return sond_treeviewfm_get_instance_private(stvfm);
}


//Nun geht's mit SondTreeviewFM weiter
static void sond_treeviewfm_render_text_cell(GtkTreeViewColumn *column,
		GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter,
		gpointer data) {
	SondTVFMItem *stvfm_item = NULL;
	gint rc = 0;
	gchar* text = NULL;
	SondTVFMItemPrivate* stvfm_item_priv = NULL;

	SondTreeviewFM *stvfm = SOND_TREEVIEWFM(data);

	gtk_tree_model_get(model, iter, 0, &stvfm_item, -1);
	if (!stvfm_item) {
		LOG_WARN("Keine Objekt im Baum");
		return;
	}

	stvfm_item_priv = sond_tvfm_item_get_priv(stvfm_item);

	if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_DIR ||
			stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF)
		text = g_strdup(stvfm_item_priv->display_name);
	else if (SOND_TREEVIEWFM_GET_CLASS(stvfm)->text_from_section) {
		GError* error = NULL;

		rc = SOND_TREEVIEWFM_GET_CLASS(stvfm)->text_from_section(stvfm_item, &text,
				&error);
		if (rc == -1) {

			text = g_strdup(error->message);
			g_error_free(error);
		}
	}

	g_object_set(G_OBJECT(
			sond_treeview_get_cell_renderer_text(SOND_TREEVIEW(stvfm))),
			"text", text, NULL);
	g_free(text);

	g_object_unref(stvfm_item);

	if (SOND_TREEVIEWFM_GET_CLASS(stvfm)->deter_background) {
		GError *error = NULL;

		rc = SOND_TREEVIEWFM_GET_CLASS(stvfm)->deter_background(stvfm_item,
				&error);
		if (rc == -1) {
			LOG_WARN("Fehler bei Ermittlung background: %s", error->message);
			g_error_free(error);

			return;
		}
	}

	g_object_set(G_OBJECT(
			sond_treeview_get_cell_renderer_text(SOND_TREEVIEW(stvfm))),
			"background-set", (rc == 1) ? TRUE : FALSE, NULL);

	return;
}

gint sond_treeviewfm_file_part_visible(SondTreeviewFM *stvfm, GtkTreeIter *iter_parent,
		gchar const* filepart, gboolean open,
		GtkTreeIter *iter_res, GError **error) {
	GtkTreeIter iter_child = { 0 };
	gboolean children = FALSE;
	SondTVFMItem* stvfm_item = NULL;

	if (!iter_parent)
		children = gtk_tree_model_get_iter_first(
				gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)), &iter_child);
	else
		children = gtk_tree_model_iter_children(
				gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)), &iter_child, iter_parent);

	if (!children)
		return 0;

	/* Dummy-Prüfung */
	gtk_tree_model_get(
			gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)),
			&iter_child, 0, &stvfm_item, -1);

	if (!stvfm_item) {
		if (!open)
			return 0;

		sond_treeview_expand_row(SOND_TREEVIEW(stvfm), iter_parent);

		gtk_tree_model_iter_children(
				gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)), &iter_child,
				iter_parent);
	} else
		g_object_unref(stvfm_item);

	do {
		gchar* filepart_item = NULL;
		gchar* filepart_sfp = NULL;
		gchar const* path_item = NULL;

		gtk_tree_model_get(
				gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)), &iter_child, 0,
				&stvfm_item, -1);

		SondTVFMItemPrivate* stvfm_item_priv =
				sond_tvfm_item_get_priv(stvfm_item);
		g_object_unref(stvfm_item);

		path_item = stvfm_item_priv->path_or_section;

		if (stvfm_item_priv->sond_file_part)
			filepart_sfp = sond_file_part_get_filepart(stvfm_item_priv->sond_file_part);

		if (filepart_sfp && path_item)
			filepart_item = g_strconcat(filepart_sfp, "//", path_item, NULL);
		else if (filepart_sfp)
			filepart_item = g_strdup(filepart_sfp);
		else if (path_item)
			filepart_item = g_strdup(path_item);
		else
			filepart_item = g_strdup("");

		g_free(filepart_sfp);

		if (g_strcmp0(filepart, filepart_item) == 0) {
			/* Exakter Treffer */
			g_free(filepart_item);
			if (iter_res) *iter_res = iter_child;
			return 1;
		}

		/* Präfix-Treffer nur, wenn direkt danach ein Trenner folgt -
		 * "/" bzw. der erste Schrägstrich von "//" - sonst würde z.B.
		 * "sub" fälschlich als Präfix von "sub2/d.pdf" durchgehen. */
		if (g_str_has_prefix(filepart, filepart_item)
				&& filepart[strlen(filepart_item)] == '/') {
			/* Präfix-Treffer: rekursiv in Kinder */
			g_free(filepart_item);
			gint rc = sond_treeviewfm_file_part_visible(stvfm, &iter_child,
					filepart, open, iter_res, error);
			if (rc == -1)
				return -1;
			else
				return rc;
		}

		g_free(filepart_item);

	} while (gtk_tree_model_iter_next(
			gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)), &iter_child));

	return 0;
}

static void sond_treeviewfm_finalize(GObject *g_object) {
	Clipboard *clipboard = NULL;

	SondTreeviewFMPrivate *stvfm_priv = sond_treeviewfm_get_instance_private(
			SOND_TREEVIEWFM(g_object));

#ifdef _WIN32
	sond_treeviewfm_seadrive_stop_watcher(SOND_TREEVIEWFM(g_object));
	if (stvfm_priv->seadrive_not_in_sync)
		g_hash_table_destroy(stvfm_priv->seadrive_not_in_sync);
	if (stvfm_priv->seadrive_pending_down_paths)
		g_hash_table_destroy(stvfm_priv->seadrive_pending_down_paths);
	if (stvfm_priv->seadrive_dir_counts)
		g_hash_table_destroy(stvfm_priv->seadrive_dir_counts);
	if (stvfm_priv->seadrive_file_badges)
		g_hash_table_destroy(stvfm_priv->seadrive_file_badges);
#endif

	g_free(stvfm_priv->root);

	clipboard =
			((SondTreeviewClass*) g_type_class_peek( SOND_TYPE_TREEVIEW))->clipboard;
	if ( G_OBJECT(clipboard->tree_view) == g_object)
		g_ptr_array_remove_range(clipboard->arr_ref, 0,
				clipboard->arr_ref->len);

	G_OBJECT_CLASS (sond_treeviewfm_parent_class)->finalize(g_object);

	return;
}

static void sond_treeviewfm_cell_edited(GtkCellRenderer *cell,
		gchar *path_string, gchar *new_text, gpointer data) {
	GtkTreeIter iter = { 0 };
	g_autoptr (SondTVFMItem) stvfm_item = NULL;
	GError *error = NULL;
	gint rc = 0;

	SondTreeviewFM *stvfm = (SondTreeviewFM*) data;
	
	gtk_tree_model_get_iter_from_string(
			gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)), &iter, path_string);
	gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)),
			&iter, 0, &stvfm_item, -1);

	if (!g_strcmp0(sond_tvfm_item_get_basename(stvfm_item), new_text))
		return;

	rc = SOND_TREEVIEWFM_GET_CLASS(stvfm)->text_edited(stvfm, &iter, stvfm_item,
		new_text, &error);
	if (rc) {
		display_message(SOND_GET_TOPLEVEL(stvfm),
		"Umbenennen nicht möglich\n\n", error->message, NULL);
		g_error_free(error);

		return;
	}

	return;
}

static void sond_treeviewfm_constructed(GObject *self) {
	//Text-Spalte wird editiert
	g_signal_connect(sond_treeview_get_cell_renderer_text(SOND_TREEVIEW(self)),
			"edited", G_CALLBACK(sond_treeviewfm_cell_edited), self); //Klick in textzelle = Datei umbenennen

	G_OBJECT_CLASS(sond_treeviewfm_parent_class)->constructed(self);

	return;
}


static gboolean is_valid_filename(const gchar *filename) {
    if (filename == NULL || *filename == '\0')
        return FALSE;

    // Prüfe auf ungültige Zeichen (für Unix/Linux)
    if (strchr(filename, '/') != NULL)
        return FALSE;

    // Prüfe auf reservierte Namen
    if (g_strcmp0(filename, ".") == 0 || g_strcmp0(filename, "..") == 0)
        return FALSE;

#ifdef _WIN32
    // Erweiterte Prüfung für Windows
    if (strchr(filename, '\\') != NULL ||
        strchr(filename, ':') != NULL ||
        strchr(filename, '*') != NULL ||
        strchr(filename, '?') != NULL ||
        strchr(filename, '"') != NULL ||
        strchr(filename, '<') != NULL ||
        strchr(filename, '>') != NULL ||
        strchr(filename, '|') != NULL)
        return FALSE;
#endif

    return TRUE;
}

static void adjust_sfps_in_dir(SondFilePart* sfp_dir, SondFilePart* sfp_dst,
		gchar const* path_old, gchar const* path_new) {
	GPtrArray* arr_opened_children = NULL;

	arr_opened_children = sond_file_part_get_arr_opened_files(sfp_dir); //NULL ist ok

	//welche liegen "unterhalb"?
	for (guint i = 0; arr_opened_children && i < arr_opened_children->len; i++) {
		SondFilePart* sfp_child = NULL;
		gchar const* path_child = NULL;

		sfp_child = g_ptr_array_index(arr_opened_children, i);
		path_child = sond_file_part_get_path(sfp_child);

		if (g_str_has_prefix(path_child, path_old)) { //Treffer
			//ggf. neues Eltern-sfp
			if (sfp_dir != sfp_dst)
				sond_file_part_set_parent(sfp_child, sfp_dst);

			//Pfad von sfp_child anpassen
			gchar* path_child_new = NULL;

			//stvfm_item_priv->path_or_section ist der neue Pfad des Verzeichnisses
			//kann nicht NULL sein, ist mindestens toplevel_path!
			path_child_new = g_strconcat(path_new, "/",
					path_child + ((path_old) ? strlen(path_old) + 1 : 0), NULL);
			sond_file_part_set_path(sfp_child, path_child_new);
			g_free(path_child_new);
		}
	}

	return;
}

static gint sond_treeviewfm_text_edited(SondTreeviewFM *stvfm,
		GtkTreeIter *iter, SondTVFMItem *stvfm_item, const gchar *text_new,
		GError **error) {
	gint rc = 0;
	GtkTreeIter iter_parent = { 0 };
	SondTVFMItem* stvfm_item_parent = NULL;
	SondTVFMItemPrivate* stvfm_item_priv = NULL;
	gint res = 0;
	gpointer ctx = NULL;

	if (!is_valid_filename(text_new))
		return 0;

	if (!gtk_tree_model_iter_parent(gtk_tree_view_get_model(
			GTK_TREE_VIEW(stvfm)), &iter_parent, iter))
		stvfm_item_parent =
				sond_tvfm_item_create(stvfm, NULL, NULL);
	else
		gtk_tree_model_get(gtk_tree_view_get_model(
				GTK_TREE_VIEW(stvfm)), &iter_parent, 0, &stvfm_item_parent, -1);

	stvfm_item_priv = sond_tvfm_item_get_priv(stvfm_item);

	g_signal_emit(stvfm_item_priv->stvfm,
			SOND_TREEVIEWFM_GET_CLASS(stvfm_item_priv->stvfm)->signal_before_move,
			0, stvfm_item, stvfm_item_parent, text_new, 0, error, &ctx, &res);

	if (res) {
		g_object_unref(stvfm_item_parent);
		return -1;
	}

	rc = sond_tvfm_item_rename(stvfm_item, stvfm_item_parent, text_new, error);
	g_object_unref(stvfm_item_parent);
	g_signal_emit(stvfm_item_priv->stvfm,
			SOND_TREEVIEWFM_GET_CLASS(stvfm_item_priv->stvfm)->signal_after, 0,
			(rc == 0) ? TRUE : FALSE, ctx);
	if (rc)
		return -1;

	//sfp-Pfade ändern, soweit erforderlich
	if (stvfm_item_priv->path_or_section)
		adjust_sfps_in_dir(stvfm_item_priv->sond_file_part,
				stvfm_item_priv->sond_file_part, stvfm_item_priv->path_or_section, text_new);

	//nur display-name - etwaig erforderliche Pfadanpassungen in rename_stvfm_item bzw.
	//den spezialisierten Unterfunktionen
	g_free(stvfm_item_priv->display_name);
	stvfm_item_priv->display_name = g_strdup(text_new);

	return 0;
}

static void sond_treeviewfm_results_row_activated(GtkTreeView *treeview,
		GtkTreePath *tree_path, GtkTreeViewColumn *col, gpointer user_data) {
	gint rc = 0;
	gchar *filepart = NULL;
	GtkTreeIter iter = { 0 };
	GError *error = NULL;
	GtkTreeModel *model = NULL;
	GtkTreeIter row_iter = { 0 };

	SondTreeviewFM *stvfm = (SondTreeviewFM*) user_data;

	/* Dateipfad aus Spalte 0 holen */
	model = gtk_tree_view_get_model(treeview);
	gtk_tree_model_get_iter(model, &row_iter, tree_path);
	gtk_tree_model_get(model, &row_iter, 0, &filepart, -1);

	rc = sond_treeviewfm_file_part_visible(stvfm, NULL, filepart, TRUE, &iter,
			&error);
	g_free(filepart);
	if (rc == -1) {
		display_message(SOND_GET_TOPLEVEL(stvfm),
		"Fehler\n\n", error->message, NULL);

		g_error_free(error);
		}
		else if (rc == 0) {
		display_message(SOND_GET_TOPLEVEL(stvfm),
		"Datei nicht gefunden", filepart, NULL);

		return;
	}

	sond_treeview_set_cursor(SOND_TREEVIEW(stvfm), &iter);

	return;
}

static gint sond_treeviewfm_open_stvfm_item(GtkTreeIter* iter, SondTVFMItem* stvfm_item,
		gboolean open_with, GError** error) {
	gint rc = 0;
	SondTVFMItemPrivate *stvfm_item_priv = sond_tvfm_item_get_priv(stvfm_item);

	rc = sond_file_part_open(stvfm_item_priv->sond_file_part, open_with, error);
	if (rc)
		return -1;

	return 0;
}

void sond_treeviewfm_add_base_menu(GMenu *gmenu) {
    sond_treeview_add_base_menu(gmenu);

	GMenu *sec_einf = g_menu_new();
	GMenu *sub_einf = g_menu_new();
	g_menu_append(sub_einf, "Gleiche Ebene", "stv.einf-ge");
	g_menu_append(sub_einf, "Unterebene",    "stv.einf-up");
	g_menu_append_submenu(sec_einf, "Punkt einf\u00fcgen",
			G_MENU_MODEL(sub_einf));
	g_object_unref(sub_einf);
	g_menu_prepend_section(gmenu, NULL, G_MENU_MODEL(sec_einf));
	g_object_unref(sec_einf);

	GMenu *sec_paste = g_menu_new();
	GMenu *sub_paste = g_menu_new();
	g_menu_append(sub_paste, "Gleiche Ebene", "stv.paste-ge");
	g_menu_append(sub_paste, "Unterebene",    "stv.paste-up");
	g_menu_append_submenu(sec_paste, "Einf\u00fcgen",
			G_MENU_MODEL(sub_paste));
	g_object_unref(sub_paste);
	g_menu_append_section(gmenu, NULL, G_MENU_MODEL(sec_paste));
	g_object_unref(sec_paste);

	GMenu *sec_loeschen = g_menu_new();
	g_menu_append(sec_loeschen, "L\u00f6schen", "stv.loeschen");
	g_menu_append_section(gmenu, NULL, G_MENU_MODEL(sec_loeschen));
	g_object_unref(sec_loeschen);

	GMenu *sec_oeffnen = g_menu_new();
	g_menu_append(sec_oeffnen, "\u00d6ffnen",     "stv.oeffnen");
	g_menu_append(sec_oeffnen, "\u00d6ffnen mit", "stv.oeffnen-mit");
	g_menu_append_section(gmenu, NULL, G_MENU_MODEL(sec_oeffnen));
	g_object_unref(sec_oeffnen);

	GMenu *sec_search = g_menu_new();
	GMenu *sub_search = g_menu_new();
	g_menu_append(sub_search, "Gesamtes Verzeichnis", "stv.dateisuche");
	g_menu_append(sub_search, "Nur markierte Punkte", "stv.dateisuche-sel");
	g_menu_append_submenu(sec_search, "Dateisuche",
			G_MENU_MODEL(sub_search));
	g_object_unref(sub_search);
	g_menu_append_section(gmenu, NULL, G_MENU_MODEL(sec_search));
	g_object_unref(sec_search);

	/* SeaDrive-Section: eigenes Untermen\u00fc "SeaDrive" (Nutzer-Feedback
	 * 11.09.2026, damit klar ist, dass die drei Punkte zusammengeh\u00f6ren),
	 * darin nur noch "Auswahl". "Gesamtes Projekt" (wirkte schon immer auf
	 * die Projekt-Wurzel, unabh\u00e4ngig von Selektion/Rechtsklick-Ziel) ist
	 * seit 11.09.2026 ins Hauptmen\u00fc gewandert ("Projekt > SeaDrive",
	 * win.sd-*-all in headerbar.c) - das geh\u00f6rte eigentlich nie in ein
	 * Kontextmen\u00fc, das ja "dieser Punkt"/"diese Auswahl" suggeriert
	 * (Nutzer-Feedback, s. ToDo.c). */
	GMenu *sec_sd = g_menu_new();
	GMenu *sub_sd = g_menu_new();
	g_menu_append(sub_sd, "Immer offline verf\u00fcgbar",
			"stv.sd-pin-sel");
	g_menu_append(sub_sd, "Offline verf\u00fcgbar aufheben",
			"stv.sd-unspec-sel");
	g_menu_append(sub_sd, "Cache leeren", "stv.sd-unpin-sel");
	g_menu_append_submenu(sec_sd, "SeaDrive", G_MENU_MODEL(sub_sd));
	g_object_unref(sub_sd);
	g_menu_append_section(gmenu, NULL, G_MENU_MODEL(sec_sd));
	g_object_unref(sec_sd);
}

static void sond_treeviewfm_class_init(SondTreeviewFMClass *klass) {
	G_OBJECT_CLASS(klass)->finalize = sond_treeviewfm_finalize;
	G_OBJECT_CLASS(klass)->constructed = sond_treeviewfm_constructed;

	SOND_TREEVIEW_CLASS(klass)->render_text_cell =
			sond_treeviewfm_render_text_cell;

	SOND_TREEVIEW_CLASS(klass)->gmenu = g_menu_new();
	sond_treeviewfm_add_base_menu(SOND_TREEVIEW_CLASS(klass)->gmenu);

	klass->signal_before_move = g_signal_new("before-move",
			SOND_TYPE_TREEVIEWFM, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_INT, 6,
			SOND_TYPE_TVFM_ITEM,
			SOND_TYPE_TVFM_ITEM,
			G_TYPE_STRING,
			G_TYPE_INT,
			G_TYPE_POINTER,   /* GError** */
			G_TYPE_POINTER);  /* Transaktions-Kontext (out) */

	klass->signal_before_insert = g_signal_new("before-insert",
			SOND_TYPE_TREEVIEWFM, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_INT, 5,
			SOND_TYPE_TVFM_ITEM,
			SOND_TYPE_TVFM_ITEM,
			G_TYPE_STRING,
			G_TYPE_INT,
			G_TYPE_POINTER);

	klass->signal_before_delete = g_signal_new("before-delete",
			SOND_TYPE_TREEVIEWFM, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_INT, 3,
			SOND_TYPE_TVFM_ITEM,
			G_TYPE_POINTER,   /* GError** */
			G_TYPE_POINTER);  /* Transaktions-Kontext (out) */

	klass->signal_after = g_signal_new("after",
			SOND_TYPE_TREEVIEWFM, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 2,
			G_TYPE_BOOLEAN,
			G_TYPE_POINTER);  /* der von before-* gelieferte Kontext */

	klass->text_from_section = NULL;
	klass->deter_background = NULL;
	klass->text_edited = sond_treeviewfm_text_edited;
	klass->results_row_activated = sond_treeviewfm_results_row_activated;
	klass->open_stvfm_item = sond_treeviewfm_open_stvfm_item;

#ifdef _WIN32
	klass->signal_seadrive_status = g_signal_new("seadrive-status",
			SOND_TYPE_TREEVIEWFM, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
			G_TYPE_NONE, 2,
			G_TYPE_UINT,     /* pending_down */
			G_TYPE_UINT);    /* pending_up */
#endif

	return;
}

static GtkTreeIter*
sond_treeviewfm_insert_node(SondTreeviewFM *stvfm, GtkTreeIter *iter,
		gboolean child) {
	GtkTreeIter new_iter = { 0 };
	GtkTreeIter* ret_iter = NULL;
	GtkTreeStore *treestore = GTK_TREE_STORE(
			gtk_tree_view_get_model( GTK_TREE_VIEW(stvfm) ));

	//Hauptknoten erzeugen
	if (!child)
		gtk_tree_store_insert_after(treestore, &new_iter, NULL, iter);
	//Unterknoten erzeugen
	else
		gtk_tree_store_insert_after(treestore, &new_iter, iter, NULL);

	ret_iter = gtk_tree_iter_copy(&new_iter);

	return ret_iter; //muß nach Gebrauch gtk_tree_iter_freed werden!!!
}

static gint insert_dir_in_fs(SondTVFMItemPrivate* stvfm_item_priv,
		gchar** path, GError** error) {
	guint max_tries = 100;

	for (guint i = 0; i <= max_tries; i++) {
		gboolean suc = FALSE;
		g_autofree gchar *trial_path = NULL;

		if (i == 0)
			trial_path = (stvfm_item_priv->path_or_section) ?
					g_strconcat(stvfm_item_priv->path_or_section,
							"/Neues Verzeichnis", NULL) : g_strdup("Neues Verzeichnis");
		else trial_path = (stvfm_item_priv->path_or_section) ?
				g_strdup_printf("%s/Neues Verzeichnis (%u)", stvfm_item_priv->path_or_section, i) :
				g_strdup_printf("Neues Verzeichnis (%u)", i);

		suc = sond_mkdir(trial_path, error);
		if (suc) {
			*path = g_strdup(trial_path);

			return 0;
		}

		if (!suc) {
			if (g_error_matches(*error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
				g_clear_error(error);
				continue;  // nächster Suffix versuchen
			}
			else
				return -1;
		}
	}

	g_set_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS,
				"Kein eindeutiger Zielname nach %u Versuchen", max_tries);

	return -1;
}

static gint insert_dir_in_zip(SondTVFMItemPrivate* stvfm_item_priv, gboolean child,
		gchar** base, GError** error) {
	if (error) *error = g_error_new(g_quark_from_static_string("sond"), 0,
			"Einfügen in ZIP noch nicht implementiert");

	return -1;
}

static gint sond_treeviewfm_create_dir(SondTreeviewFM *stvfm, gboolean child,
		GError **error) {
	gint rc = 0;
	SondTVFMItemPrivate* stvfm_item_parent_priv = NULL;
	SondTVFMItem *stvfm_item_new = NULL;
	gchar *path = NULL;
	GtkTreeIter iter = { 0 };
	GtkTreeIter *iter_new = NULL;
	GtkTreeIter iter_parent = { 0 };
	g_autoptr(SondTVFMItem) stvfm_item_parent = NULL;
	gboolean first_child = FALSE;

	if (!sond_treeview_get_cursor(SOND_TREEVIEW(stvfm), &iter))
		return 0;

	if (!child) {
		if (!gtk_tree_model_iter_parent(
			gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)), &iter_parent,
			&iter))
				stvfm_item_parent =
						sond_tvfm_item_create(stvfm,  NULL, NULL); //Root
	}

	if (!stvfm_item_parent)
		gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)),
				&iter, 0, &stvfm_item_parent, -1);

	stvfm_item_parent_priv = sond_tvfm_item_get_priv(stvfm_item_parent);

	if (stvfm_item_parent_priv->type != SOND_TVFM_ITEM_TYPE_DIR)
		return 0; //Wenn etwas anderes als in dir - nix machen

	if (!stvfm_item_parent_priv->sond_file_part)
		rc = insert_dir_in_fs(stvfm_item_parent_priv, &path, error);
//	else if (SOND_IS_FILE_PART_PDF(stvfm_item_parent_priv->sond_file_part))
//		rc = insert_dir_in_pdf(stvfm_item_priv, child, &path, error);
	else if (SOND_IS_FILE_PART_ZIP(stvfm_item_parent_priv->sond_file_part))
		rc = insert_dir_in_zip(stvfm_item_parent_priv, child, &path, error);
	if (rc)
		return -1;

	first_child = (gtk_tree_model_iter_n_children(
				gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)), &iter) == 0);

	//In Baum tun? ggf. nur als dummy?
	if (!child || first_child || sond_treeview_row_expanded(SOND_TREEVIEW(stvfm), &iter))
		iter_new = sond_treeviewfm_insert_node(stvfm, &iter, child);

	//Wenn sichtbar, dann Inhalt in dummy
	if (!child || sond_treeview_row_expanded(SOND_TREEVIEW(stvfm), &iter)) {
		stvfm_item_new =
				sond_tvfm_item_create(stvfm,
						stvfm_item_parent_priv->sond_file_part, path);

		gtk_tree_store_set(
				GTK_TREE_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm) )),
				iter_new, 0, stvfm_item_new, -1);

		g_object_unref(stvfm_item_new);
		sond_treeview_set_cursor(SOND_TREEVIEW(stvfm), iter_new);

		gtk_tree_iter_free(iter_new);
	}
	else { //wenn nicht sichtbar, dann öffnen
		GtkTreeIter iter_child = { 0 };

		sond_treeview_expand_row(SOND_TREEVIEW(stvfm), &iter);

		if (first_child){ //Wenn erstes Kind, Cursor darauf setzen - dann ist es das richtige
			gtk_tree_model_iter_children(
					gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)), &iter_child, &iter);

			sond_treeview_set_cursor(SOND_TREEVIEW(stvfm), &iter_child);
		}
		else { //neues Verzeichnis suchen...
			gint num_children = 0;

			num_children = gtk_tree_model_iter_n_children(
					gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)), &iter);
			for (guint i = 0; i < num_children; i++) {
				SondTVFMItem* stvfm_item_child = NULL;

				gtk_tree_model_iter_nth_child(
						gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)),
						&iter_child, &iter, i);
				gtk_tree_model_get(
						gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)),
						&iter_child, 0, &stvfm_item_child, -1);
				g_object_unref(stvfm_item_child);

				if (g_strcmp0(sond_tvfm_item_get_path_or_section(stvfm_item_child),
						path) == 0) {
					sond_treeview_set_cursor(SOND_TREEVIEW(stvfm), &iter_child);
					break;
				}
			}
		}
	}

	return 0;
}

typedef struct _S_FM_Paste_Selection {
	SondTVFMItem* stvfm_item_parent;
	GtkTreeIter *iter_parent;
	GtkTreeIter *iter_cursor;
	gboolean kind;
	gboolean expanded;
	gchar* base_inserted;
	gint index_to;
} SFMPasteSelection;

static gint process_stvfm_item_move_or_copy(SondTVFMItem* stvfm_item,
		SFMPasteSelection* s_paste_sel, gboolean move, GError** error) {
	gint rc = 0;

	guint max_tries = 100;
	const gchar *dot = NULL;
	gboolean has_ext = FALSE;
	const gchar *ext = NULL;
	gint i = 0;

	g_autofree gchar *name = NULL;
	g_autofree gchar *base = NULL;

	SondTVFMItemPrivate* stvfm_item_priv =
			sond_tvfm_item_get_priv(stvfm_item);
	SondTVFMItemPrivate* stvfm_item_parent_priv =
			sond_tvfm_item_get_priv(s_paste_sel->stvfm_item_parent);

	//Einfügen in GMessage
	if(SOND_IS_FILE_PART_GMESSAGE(stvfm_item_parent_priv->sond_file_part))
		base = g_strdup_printf("%u", s_paste_sel->index_to);
	else {
		base = g_strdup(stvfm_item_priv->display_name);

		//base ändern, wenn MimePart als displayName
		if (strrchr(base, '/')) {
			gchar const* ext = NULL;

			ext = mime_to_extension(base);
			g_free(base);
			base = g_strconcat("UNNAMED", ext, NULL);
		}
	}

	dot = strrchr(base, '.');
	has_ext = (!stvfm_item_priv->path_or_section) && dot && dot != base;
	name = has_ext ? g_strndup(base, (gsize)(dot - base)) : g_strdup(base);
	ext = has_ext ? dot : "";

	do {
		g_autofree gchar *trial_base = NULL;

		trial_base = (i == 0) ? g_strdup(base) :
				g_strconcat(name, g_strdup_printf(" (%u)", i), ext, NULL);

		if (move)
			rc = sond_tvfm_item_move(stvfm_item,
					s_paste_sel->stvfm_item_parent, trial_base,
					s_paste_sel->index_to, error);
		else {
			/* Echtes Kopieren (nicht Ausschneiden): sond_tvfm_item_copy()
			 * selbst emittiert kein Signal (anders als sond_tvfm_item_move(),
			 * das "before-move" nutzt) - deshalb hier "before-insert" senden,
			 * damit z.B. die Index-Coverage am Zielort aufgelöst werden kann,
			 * bevor dort neuer, ungeprüfter Inhalt entsteht (Bug-Fix
			 * 11.09.2026: Kopieren einer nicht indizierten Datei in einen als
			 * komplett indiziert markierten Ordner ließ den Ordner fälschlich
			 * grün, weil dieser Pfad bislang gar nicht auf Coverage hörte). */
			gint res = 0;

			g_signal_emit(stvfm_item_parent_priv->stvfm,
					SOND_TREEVIEWFM_GET_CLASS(stvfm_item_parent_priv->stvfm)->signal_before_insert, 0,
					stvfm_item, s_paste_sel->stvfm_item_parent, trial_base,
					s_paste_sel->index_to, error, &res);

			if (res)
				rc = -1;
			else
				rc = sond_tvfm_item_copy(stvfm_item,
						s_paste_sel->stvfm_item_parent, trial_base,
						s_paste_sel->index_to, error);
		}

		if (rc == -1) {
			if (g_error_matches(*error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
				g_clear_error(error);
				i++;

				continue;  // nächster Suffix versuchen
			}
			else if (g_error_matches(*error, G_IO_ERROR, G_IO_ERROR_BUSY)) {
				g_clear_error(error);

				gint res = dialog_with_buttons(
						SOND_GET_TOPLEVEL(stvfm_item_priv->stvfm),
						"Zugriff nicht erlaubt",
						"Datei möglicherweise geöffnet", NULL,
						"Erneut versuchen", 1, "Überspringen", 2, "Abbrechen",
						3, NULL);

				if (res == 1)
					continue; //Pfad bleibt gleich - einfach nochemal
				else if (res == 2)
					return 1; //Überspringen
				else if (res == 3)
					return 2; //Abbrechen
			}
			else
				return -1;
		}
		else if (rc == 1) //überspringen
			return 1;
		else {
			g_free(s_paste_sel->base_inserted);
			s_paste_sel->base_inserted = g_strdup(trial_base);

			break;
		}
	} while (i < max_tries);

	if (i == max_tries) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS,
				"%s\nKein eindeutiger Zielname nach %u Versuchen", __func__, max_tries);

		return -1;
	}

	return 0;
}

static void remove_item_from_tree(GtkTreeIter* iter,
		SondTVFMItem* stvfm_item) {
	gboolean is_gmessage = FALSE;

	SondTVFMItemPrivate* stvfm_item_priv =
			sond_tvfm_item_get_priv(stvfm_item);

	if (stvfm_item_priv->path_or_section)
		is_gmessage = SOND_IS_FILE_PART_GMESSAGE(stvfm_item_priv->sond_file_part);
	else
		is_gmessage = SOND_IS_FILE_PART_GMESSAGE(
				sond_file_part_get_parent(stvfm_item_priv->sond_file_part));

	//Wenn gelöschte Datei embedded file in PDF war:
	if (SOND_IS_FILE_PART_PDF(sond_file_part_get_parent(
			stvfm_item_priv->sond_file_part))) {
		//Falls es das letzte war, muß alles umgestellt werdem
		if (!sond_file_part_get_has_children(sond_file_part_get_parent(
				stvfm_item_priv->sond_file_part))) {
			GtkTreeIter iter_parent = { 0 };
			GtkTreeIter iter_page_tree = { 0 };
			SondTVFMItem* stvfm_item_parent = NULL;
			SondTVFMItemPrivate* stvfm_item_parent_priv = NULL;

			//stvfm mit page_tree muß gelöscht werden
			iter_page_tree = *iter; //iter brauchen wir noch
			if (!gtk_tree_model_iter_previous(
					gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm_item_priv->stvfm)),
					&iter_page_tree)) {
				LOG_ERROR("PageTree-Item fehlt");

				goto parent;
			}

			if (!gtk_tree_store_remove(GTK_TREE_STORE(
					gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm_item_priv->stvfm))),
					&iter_page_tree))
				LOG_ERROR("PageTree-Item konnte nicht gelöscht werden");

parent:
			//Jetzt muß stvfm_item (parent) angepaßt werden
			if (!gtk_tree_model_iter_parent(
					gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm_item_priv->stvfm)),
					&iter_parent, iter)) {
				LOG_ERROR("Parent-Item nicht vorhanden");

				goto end;
			}

			gtk_tree_model_get(gtk_tree_view_get_model(
					GTK_TREE_VIEW(stvfm_item_priv->stvfm)),
					&iter_parent, 0, &stvfm_item_parent, -1);

			if (!SOND_IS_TVFM_ITEM(stvfm_item_parent)) {
				LOG_ERROR("Parent enthält kein STVFM-Item");

				goto end;
			}

			stvfm_item_parent_priv = sond_tvfm_item_get_priv(
					stvfm_item_parent);
			stvfm_item_parent_priv->type = SOND_TVFM_ITEM_TYPE_LEAF;
			stvfm_item_parent_priv->has_children = FALSE;
			stvfm_item_parent_priv->icon_name = "pdf";
			g_object_unref(stvfm_item_parent);

			end:
			;
		}
	}

	//stvfm vor remove sichern, da stvfm_item_priv danach undefiniert sein kann
	//(TreeStore gibt GObject frei → Use-after-free)
	SondTreeviewFM* stvfm = stvfm_item_priv->stvfm;

	//jetzt auf Zielknoten löschen
	if (gtk_tree_store_remove(
			GTK_TREE_STORE(gtk_tree_view_get_model(
					GTK_TREE_VIEW(stvfm))), iter)) {
		//Leider, wenn GMessage, die Pfade anpassen
		if (is_gmessage)
			do {
				SondTVFMItem* stvfm_item_sibling = NULL;
				SondTVFMItemPrivate* stvfm_item_sibling_priv = NULL;
				gchar const* path_old = NULL;
				gchar* path_new = NULL;
				gint index = 0;

				gtk_tree_model_get(gtk_tree_view_get_model(
				GTK_TREE_VIEW(stvfm)), iter, 0,
				&stvfm_item_sibling, -1);

				stvfm_item_sibling_priv =
				sond_tvfm_item_get_priv(stvfm_item_sibling);
				g_object_unref(stvfm_item_sibling);

				/* Message-Item überspringen: path_or_section == NULL bei GMessage-LEAF
				* (analog zu PageTree bei PDF) - hat keinen numerischen Index */
				if (!stvfm_item_sibling_priv->path_or_section &&
				stvfm_item_sibling_priv->type == SOND_TVFM_ITEM_TYPE_LEAF &&
				SOND_IS_FILE_PART_GMESSAGE(stvfm_item_sibling_priv->sond_file_part))
					continue;

				path_old = (stvfm_item_sibling_priv->path_or_section) ?
						stvfm_item_sibling_priv->path_or_section :
						sond_file_part_get_path(stvfm_item_sibling_priv->sond_file_part);

				if (strrchr(path_old, '/')) {
					gchar const* base_old = NULL;
					gchar* dir = NULL;

					base_old = strrchr(path_old, '/');
					index = atoi(base_old + 1);

					dir = g_path_get_dirname(path_old);
					path_new = g_strdup_printf("%s/%u", dir, index - 1);
					g_free(dir);
				}
				else {
					index = atoi(path_old);
					path_new = g_strdup_printf("%u", index - 1);
				}

				//sibling ist dir, also multipart:
				if (stvfm_item_sibling_priv->path_or_section) {
					//sfps im "scope" anpassen
					adjust_sfps_in_dir(stvfm_item_sibling_priv->sond_file_part,
							stvfm_item_sibling_priv->sond_file_part,
							stvfm_item_sibling_priv->path_or_section, path_new);

					//jetzt path von stvfm_item_sibling ändern
					g_free(stvfm_item_sibling_priv->path_or_section);
					stvfm_item_sibling_priv->path_or_section = path_new;
				}
				else //ist selbst sfp - dessen Pfad muß geändert werden
					sond_file_part_set_path(
							stvfm_item_sibling_priv->sond_file_part, path_new);

				g_free(path_new);
			} while (gtk_tree_model_iter_next(gtk_tree_view_get_model(
					GTK_TREE_VIEW(stvfm)), iter));
	}

	return;
}


static gint sond_treeviewfm_paste_clipboard_foreach(SondTreeview *stv,
		GtkTreeIter *iter, gpointer data, GError **error) {
	SondTVFMItem *stvfm_item = NULL;

	SFMPasteSelection *s_paste_sel = (SFMPasteSelection*) data;
	Clipboard *clipboard = NULL;
	gint rc = 0;
	SondTVFMItemPrivate* stvfm_item_priv = NULL;
	GtkTreeIter *iter_new = NULL;
	SondTVFMItem *stvfm_item_new = NULL;
	SondTVFMItemPrivate* stvfm_item_new_priv = NULL;
	SondFilePart* sfp_new = NULL;
	gchar* path_new = NULL;

	SondTVFMItemPrivate* stvfm_item_parent_priv =
			sond_tvfm_item_get_priv(s_paste_sel->stvfm_item_parent);

	clipboard = ((SondTreeviewClass*) g_type_class_peek(
			SOND_TYPE_TREEVIEW))->clipboard;

	gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(stv)), iter, 0,
			&stvfm_item, -1);
	stvfm_item_priv =
			sond_tvfm_item_get_priv(stvfm_item);
	g_object_unref(stvfm_item); //keine Angst - tree_store hält ref

	//Verschieben im selben Verzeichnis?
	if (clipboard->ausschneiden && (SOND_TREEVIEWFM(stv) == stvfm_item_parent_priv->stvfm)) {
		GtkTreeIter parent_iter = { 0 };

		//Prüfen, ob innerhalb des gleichen Verzeichnisses verschoben werden soll
		//dann: return 0;
		if (gtk_tree_model_iter_parent(
				gtk_tree_view_get_model(GTK_TREE_VIEW(stv)), &parent_iter, iter)) {
			if (s_paste_sel->iter_parent &&
					parent_iter.user_data == s_paste_sel->iter_parent->user_data &&
					parent_iter.stamp == s_paste_sel->iter_parent->stamp)
				return 0; //innerhalb des gleichen Verzeichnisses verschieben
		}
		else
			if (!s_paste_sel->iter_parent)
				return 0;
	}

	rc = process_stvfm_item_move_or_copy(stvfm_item,
			s_paste_sel, clipboard->ausschneiden, error);
	if (rc == -1)
		return -1;
	else if (rc == 1) //Überspringen gewählt
		return 0;
	else if (rc == 2) //Abbrechen gewählt
		return 1;

	s_paste_sel->index_to++; //> 0 zugleich Marker, daß Knoten eingefügt wurde

	iter_new = sond_treeviewfm_insert_node(stvfm_item_parent_priv->stvfm,
			s_paste_sel->iter_cursor, s_paste_sel->kind);
	*(s_paste_sel->iter_cursor) = *iter_new;
	gtk_tree_iter_free(iter_new);

	s_paste_sel->kind = FALSE;

	path_new = g_strconcat(
					(stvfm_item_parent_priv->path_or_section) ?
							stvfm_item_parent_priv->path_or_section : "",
							(stvfm_item_parent_priv->path_or_section) ?
									"/" : "",
									s_paste_sel->base_inserted, NULL);

	/* Nutzer-Fund 16.09.2026: stvfm_item_priv->path_or_section gesetzt heißt
	 * hier: das kopierte Element ist ein Verzeichnis-Marker INNERHALB eines
	 * Containers (ZIP/PDF/GMessage) - sein sond_file_part ist der
	 * umschließende Container selbst, keine eigenständige Identität (s.
	 * Erzeugung z.B. in sond_tvfm_item_load_zip_dir(): Verzeichnis-Einträge
	 * bekommen das sond_file_part des Eltern-Containers, nur
	 * path_or_section unterscheidet den Unterpfad). Landet so ein
	 * Verzeichnis per Kopie in einem Ziel OHNE eigenen sond_file_part (also
	 * im echten Dateisystem - genau das, was
	 * copy_dir_across_sfps()/copy_container_dir_to_fs() tatsächlich
	 * anlegen), ist ein geklontes sond_file_part witzlos bis kaputt: die
	 * neue Instanz (hier z.B. eine SondFilePartZip) bekommt weder Pfad
	 * (sond_file_part_set_path() läuft nur für !path_or_section) noch ein
	 * Eltern-Archiv (Ziel-Parent hat ja keins) und versucht beim ersten
	 * Kinder-Check trotzdem, sich selbst als Archiv zu öffnen - scheitert
	 * mit "No such file or directory" (Projektwurzel + "/" landet als
	 * Dateiname in fopen(), weil sond_file_part_get_path() NULL liefert und
	 * g_strconcat() dort abbricht). Der Fehler zeigte sich erst beim
	 * tatsächlichen Aufklappen (nicht schon beim Einfügen), weil
	 * sond_tvfm_item_load_zip_dir(...) ? TRUE : FALSE einen Fehler (-1)
	 * fälschlich als "hat Kinder" wertet. Fix: in diesem Fall gar nicht
	 * klonen - das neue Element ist ein ganz normales
	 * Dateisystem-Verzeichnis (sond_file_part bleibt NULL), was ohnehin dem
	 * entspricht, was auf der Platte real angelegt wurde; der bestehende
	 * NULL-sichere Zweig unten (sond_tvfm_item_create() mit
	 * sond_file_part == NULL) übernimmt das korrekt. Alle anderen Fälle
	 * (Dateien; Kopien innerhalb von ZIP/PDF/GMessage) bleiben unverändert. */
	if (stvfm_item_priv->sond_file_part &&
			!(stvfm_item_priv->path_or_section &&
					!stvfm_item_parent_priv->sond_file_part)) {
		sfp_new = g_object_new(G_OBJECT_TYPE(stvfm_item_priv->sond_file_part), NULL);

		if (!stvfm_item_priv->path_or_section)
			sond_file_part_set_path(sfp_new, path_new);

		if (SOND_IS_FILE_PART_LEAF(sfp_new))
			sond_file_part_leaf_set_mime_type(SOND_FILE_PART_LEAF(sfp_new),
					sond_file_part_leaf_get_mime_type(
							SOND_FILE_PART_LEAF(stvfm_item_priv->sond_file_part)));

		sond_file_part_set_has_children(sfp_new,
				sond_file_part_get_has_children(stvfm_item_priv->sond_file_part));

		sond_file_part_set_parent(sfp_new, stvfm_item_parent_priv->sond_file_part);
	}

	stvfm_item_new = sond_tvfm_item_create(
			stvfm_item_parent_priv->stvfm,
					sfp_new,
					(stvfm_item_priv->path_or_section) ?
							path_new : NULL);
	/* sfp_new bleibt NULL, wenn ein "echtes" Dateisystem-Verzeichnis
	 * (kein sond_file_part) kopiert/verschoben wird - g_object_unref(NULL)
	 * würde dann nur eine GLib-CRITICAL auslösen (bzw. bei
	 * G_DEBUG=fatal-warnings abstürzen), s. Code-Review 09/2026. */
	if (sfp_new)
		g_object_unref(sfp_new);

	stvfm_item_new_priv = sond_tvfm_item_get_priv(stvfm_item_new);

	if (clipboard->ausschneiden && stvfm_item_priv->path_or_section) //stvfm_item ist jedenfalls ein DIR
		adjust_sfps_in_dir(stvfm_item_priv->sond_file_part,
				stvfm_item_parent_priv->sond_file_part,
				stvfm_item_priv->path_or_section,
				path_new);

	g_free(path_new);

	gtk_tree_store_set(GTK_TREE_STORE(
			gtk_tree_view_get_model(GTK_TREE_VIEW(stv))),
			s_paste_sel->iter_cursor, 0, stvfm_item_new, -1);

	//Falls Verzeichnis mit Datei innendrin: dummy in neuen Knoten einfügen
	if (stvfm_item_new_priv->has_children) {
		GtkTreeIter iter_tmp = { 0 };

		gtk_tree_store_insert(
				GTK_TREE_STORE(
						gtk_tree_view_get_model(GTK_TREE_VIEW(stv))),
				&iter_tmp, s_paste_sel->iter_cursor, -1);
	}

	//Falls jetzt in GMessage: alten display_name übernehmen
	if (SOND_IS_FILE_PART_GMESSAGE(stvfm_item_new_priv->sond_file_part)) {
		g_free(stvfm_item_new_priv->display_name);
		stvfm_item_new_priv->display_name = g_strdup(stvfm_item_priv->display_name);
	}

	//Wenn in GMessage eingefügt wird:
	//path von etwaigen jüngeren Geschwistern nach Einfügen nicht mehr korrekt
	if (SOND_IS_FILE_PART_GMESSAGE(stvfm_item_parent_priv->sond_file_part)) {
		GtkTreeIter iter_sibling = *(s_paste_sel->iter_cursor);

		while (gtk_tree_model_iter_next(gtk_tree_view_get_model(
				GTK_TREE_VIEW(stvfm_item_parent_priv->stvfm)), &iter_sibling)) {
			SondTVFMItem* stvfm_item_sibling = NULL;
			SondTVFMItemPrivate* stvfm_item_sibling_priv = NULL;
			gchar* path_new = NULL;
			gint index = 0;

			gtk_tree_model_get(gtk_tree_view_get_model(
				GTK_TREE_VIEW(stvfm_item_parent_priv->stvfm)), &iter_sibling, 0,
					&stvfm_item_sibling, -1);
			stvfm_item_sibling_priv =
					sond_tvfm_item_get_priv(stvfm_item_sibling);
			g_object_unref(stvfm_item_sibling);

			if (strchr(stvfm_item_sibling_priv->path_or_section, '/')) {
				gchar const* base_old = NULL;
				gchar* dir = NULL;

				base_old = strrchr(stvfm_item_sibling_priv->path_or_section, '/');
				index = atoi(base_old + 1);

				dir = g_path_get_dirname(stvfm_item_sibling_priv->path_or_section);
				path_new = g_strdup_printf("%s/%u", dir, index + 1);
				g_free(dir);
			}
			else {
				index = atoi(stvfm_item_sibling_priv->path_or_section);
				path_new = g_strdup_printf("%u", index + 1);
			}

			//sfps im "scope" anpassen
			adjust_sfps_in_dir(stvfm_item_sibling_priv->sond_file_part,
					stvfm_item_sibling_priv->sond_file_part,
					stvfm_item_sibling_priv->path_or_section, path_new);

			//jetzt path von stvfm_item_sibling ändern
			g_free(stvfm_item_sibling_priv->path_or_section);
			stvfm_item_sibling_priv->path_or_section = path_new;
		}
	}

	g_object_unref(stvfm_item_new);

	//Knoten löschen, wenn ausgeschnitten
	if (clipboard->ausschneiden)
		remove_item_from_tree(iter, stvfm_item);

	return 0;
}

static gint sond_treeviewfm_paste_clipboard(SondTreeviewFM *stvfm, gboolean kind,
		GError **error) {
	gint rc = 0;
	GtkTreeIter iter_cursor = { 0 };
	GtkTreeIter iter_parent = { 0 };
	gboolean expanded = FALSE;
	Clipboard *clipboard = NULL;
	SondTVFMItem* stvfm_item_parent = NULL;
	gboolean parent_is_root = FALSE;
	SondTVFMItemPrivate* stvfm_item_parent_priv = NULL;

	clipboard =
			((SondTreeviewClass*) g_type_class_peek(SOND_TYPE_TREEVIEW))->clipboard;

	if (!SOND_IS_TREEVIEWFM(clipboard->tree_view))
		return 0;

	if (clipboard->arr_ref->len == 0)
		return 0;

	//iter unter cursor holen
	if (!sond_treeview_get_cursor(SOND_TREEVIEW(stvfm), &iter_cursor))
		return 0;

	//iter_parent ermitteln
	if (kind) {
		expanded = sond_treeview_row_expanded(SOND_TREEVIEW(stvfm), &iter_cursor);
		iter_parent = iter_cursor;
	}
	else
		parent_is_root = !gtk_tree_model_iter_parent(
				gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)), &iter_parent,
				&iter_cursor);

	SFMPasteSelection s_paste_sel = {NULL, NULL, &iter_cursor,
			kind, expanded, NULL, 0};

	if (!parent_is_root) {
		//STVFM_Item im tree holen
		gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)),
				&iter_parent, 0, &stvfm_item_parent, -1);

		s_paste_sel.iter_parent = &iter_parent;
	}
	else  //damit item_priv_parent immer gesetzt ist
		stvfm_item_parent =
				sond_tvfm_item_create(stvfm, NULL, NULL);

	stvfm_item_parent_priv = sond_tvfm_item_get_priv(stvfm_item_parent);

	//index der einzufügenden Stelle ermitteln, falls !kind
	//denn wenn kind == TRUE ist index_to 0
	if (!s_paste_sel.kind) {
		GtkTreePath* path = NULL;
		gint* indices = NULL;
		gint depth = 0;

		path = gtk_tree_model_get_path(
				gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm_item_parent_priv->stvfm)),
				s_paste_sel.iter_cursor);
		indices = gtk_tree_path_get_indices(path);
		depth = gtk_tree_path_get_depth(path);

		s_paste_sel.index_to = indices[depth - 1];
		gtk_tree_path_free(path);
	}

	//nur in Verzeichnis einfügen möglich, an sich
	//außer z.B. PDF-Datei, die noch keine embFiles hat
	//zwangsläufig ist expanded == FALSE und kind == TRUE,
	//denn sonst gäbe es ja Kinder
	if (stvfm_item_parent_priv->type !=
			SOND_TVFM_ITEM_TYPE_DIR &&
			//PDF-Datei (bisher) ohne embFiles ist stvfm_item_type LEAF!
			!(SOND_IS_FILE_PART_PDF(
					stvfm_item_parent_priv->sond_file_part) &&
					!sond_file_part_get_has_children(
							stvfm_item_parent_priv->sond_file_part))) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("sond"), 0,
					"%s\nEinfügen in Datei nicht unterstützt", __func__);
		g_object_unref(stvfm_item_parent); //mutig sein

		return -1;
	}

	//Wenn in nicht geöffnetes Verzeichnis eingefügt werden solL:
	//wenn schon Kind, dann expandieren
	if (stvfm_item_parent_priv->has_children
			&& s_paste_sel.kind && !s_paste_sel.expanded)
		sond_treeview_expand_row(SOND_TREEVIEW(stvfm), s_paste_sel.iter_cursor);

	s_paste_sel.stvfm_item_parent = stvfm_item_parent;

	rc = sond_treeview_clipboard_foreach(
			sond_treeviewfm_paste_clipboard_foreach,
			(gpointer) &s_paste_sel, error);

	if (s_paste_sel.index_to) { //heißt: entweder !kind oder mind. 1 node eingefügt
		//Wenn in bisher leere pdf verschoben wird: von LEAF zu DIR ändern
		if (stvfm_item_parent_priv->type == SOND_TVFM_ITEM_TYPE_LEAF) {
			stvfm_item_parent_priv->type = SOND_TVFM_ITEM_TYPE_DIR;
			stvfm_item_parent_priv->has_children = TRUE;
			stvfm_item_parent_priv->icon_name = "pdf-folder";
		} //sonst funktioniert expand nämlich nicht
	}

	//Knoten war ursprünglich nicht geöffnet und nichts wurde eingefügt
	if (!expanded && kind) { //kind wird beim 1. Einfügen FALSE
		GtkTreePath* path = NULL;

		path = gtk_tree_model_get_path(
				gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)),
				s_paste_sel.iter_cursor);
		gtk_tree_view_collapse_row(GTK_TREE_VIEW(stvfm), path);
		gtk_tree_path_free(path);
	}

	g_object_unref(stvfm_item_parent);
	if (rc == -1)
		return -1;

	//Cursor setzen
	sond_treeview_set_cursor(SOND_TREEVIEW(stvfm),
				s_paste_sel.iter_cursor);

	return 0;
}

static gint sond_treeviewfm_foreach_loeschen(SondTreeview *stv,
		GtkTreeIter *iter, gpointer data, GError **error) {
	SondTVFMItem* stvfm_item = NULL;
	gint res = 0;
	gint rc = 0;
	gpointer ctx = NULL;
	SondTreeviewFM* stvfm = SOND_TREEVIEWFM(stv);

	gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)), iter, 0,
			&stvfm_item, -1);
	g_object_unref(stvfm_item);

	g_signal_emit(stvfm, SOND_TREEVIEWFM_GET_CLASS(stvfm)->signal_before_delete,
			0, stvfm_item, error, &ctx, &res);
	if (res == -1)
		return -1;
	else if (res == 1)
		return 0;

	rc = sond_tvfm_item_delete(stvfm_item, error);
	g_signal_emit(stvfm, SOND_TREEVIEWFM_GET_CLASS(stvfm)->signal_after,
			0, (rc == 0) ? TRUE : FALSE, ctx);
	if (rc)
		return -1;

	remove_item_from_tree(iter, stvfm_item);

	return 0;
}

static gint sond_treeviewfm_open(GtkTreeIter* iter, SondTVFMItem *stvfm_item,
		gboolean open_with, GError **error) {
	gint rc = 0;
	SondTVFMItemPrivate *stvfm_item_priv = sond_tvfm_item_get_priv(stvfm_item);

	if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_DIR)
		return 0;

#ifdef _WIN32
	/* Bei offline-Dateien im SeaDrive-Pfad: Download über die offizielle
	 * CfHydratePlaceholder()-API anstoßen (sond_seadrive_hydrate_async(),
	 * sond_treeviewfm_seadrive.c/h). Den Pin-State nicht ändern.
	 *
	 * Bis 17.09.2026 stand hier ein roher CreateFileW(GENERIC_READ)+
	 * ReadFile()-"Trick". Regressions-Fund 18.09.2026 (s. ToDo.c): nach
	 * dem Windows-Update KB5124008 (09/2026) schlägt dieser Trick
	 * zuverlässig mit ERROR_CLOUD_FILE_ACCESS_DENIED fehl (auch nach
	 * KB5129195 und komplettem Neu-Build von zond - kein zond-Bug).
	 * Ersetzt durch sond_seadrive_hydrate(), das stattdessen die dafür
	 * vorgesehene CF-API verwendet.
	 *
	 * Weiterer Nutzer-Fund, ebenfalls 18.09.2026: sond_seadrive_hydrate()
	 * blockiert synchron bis die Datei (bzw. der angeforderte Bereich)
	 * lokal verfügbar ist - bei einer 51-GB-Datei fror das Programm
	 * dadurch minutenlang komplett ein, ohne Rückmeldung oder Abbrechen-
	 * Möglichkeit. Nutzer-Entscheidung: beim ERSTEN Doppelklick kein
	 * Info-Fenster (der Download läuft ohnehin im Hintergrund weiter) -
	 * stattdessen sofort in die UI zurückkehren und die eigentliche
	 * Hydrierung in einem Hintergrund-Thread erledigen
	 * (sond_seadrive_hydrate_async(), Fire-and-forget). Vorab ein
	 * schneller, nicht-blockierender Check (sond_seadrive_needs_
	 * hydration()), ob überhaupt hydriert werden muss - schon lokale
	 * Dateien fallen unten auf den normalen Öffnen-Weg durch.
	 *
	 * Ergänzung, ebenfalls 18.09.2026: bei einem erneuten Doppelklick auf
	 * dieselbe, noch laufende Datei (sond_seadrive_is_hydrating() ==
	 * TRUE) jetzt statt eines stillen No-Ops ein Fortschritts-/Abbrechen-
	 * Dialog (sond_seadrive_show_hydrate_progress_dialog(),
	 * sond_treeviewfm_seadrive.c/h) - Nutzerwunsch, um den SeaDrive-Server
	 * bei versehentlichen Großdatei-Downloads nicht unnötig weiter zu
	 * belasten.
	 *
	 * Nutzer-Hinweis 18.09.2026: dieselbe Check-und-Reagiere-Sequenz war
	 * wortgleich auch in zond_treeview_open_node() (zond_treeview.c, für
	 * BAUM_INHALT/BAUM_AUSWERTUNG) nötig geworden - in
	 * sond_seadrive_ensure_hydrated() (sond_treeviewfm_seadrive.c/h)
	 * konsolidiert. */
	{
		SondTreeviewFM *stvfm = sond_tvfm_item_get_stvfm(stvfm_item);
		if (sond_treeviewfm_is_seadrive_path(stvfm)) {
			const gchar *root = sond_treeviewfm_get_root(stvfm);
			/* Nutzer-Fund 19.09.2026: DIR-Knoten sind oben schon
			 * ausgeschlossen (return 0) - alles, was hier ankommt (LEAF
			 * wie LEAF_SECTION, unabhängig davon, ob der jeweilige
			 * sond_file_part vom Typ Leaf/PDF/ZIP/GMessage ist), steckt
			 * letztlich in GENAU EINER echten Datei auf der Platte.
			 * Vormals wurde hier per SOND_IS_FILE_PART_LEAF() +
			 * fehlendem Parent nur der Sonderfall "direkte, unverschach-
			 * telte Leaf-Datei" abgedeckt - eine PDF-/GMessage-Section
			 * (LEAF_SECTION, sond_file_part bleibt vom Container-Typ,
			 * s. sond_tvfm_item_create()) fiel dadurch komplett durch
			 * und bekam nie eine Hydrierungsprüfung. Statt die Typen zu
			 * unterscheiden: immer zum obersten Vorfahren (ohne Parent)
			 * hochlaufen - nur der trägt in seinem path-Feld den echten,
			 * projektrelativen Pfad (s. sond_file_part_do_create()); bei
			 * verschachtelten Parts (ZIP-Eintrag, PDF-Embedded-File,
			 * GMessage-Mimepart) ist path nur ein container-interner
			 * Bezeichner, den man nicht naiv an root anhängen darf. */
			SondFilePart *sfp_root = stvfm_item_priv->sond_file_part;
			SondFilePart *sfp_parent = NULL;
			while ((sfp_parent = sond_file_part_get_parent(sfp_root)))
				sfp_root = sfp_parent;

			const gchar *sfp_path = sond_file_part_get_path(sfp_root);
			if (root && sfp_path) {
				gchar *full_path = g_strconcat(root, "/", sfp_path, NULL);
				gboolean is_local = sond_seadrive_ensure_hydrated(
						GTK_WINDOW(SOND_GET_TOPLEVEL(stvfm)), full_path);
				g_free(full_path);

				/* Bei Bedarf sofort zurück an die UI. Erneuter Doppelklick
				 * später (nach Abschluss des Downloads) öffnet dann normal
				 * über den unten stehenden Weg. */
				if (!is_local)
					return 0;
			}
		}
	}
#endif

	rc = SOND_TREEVIEWFM_GET_CLASS(stvfm_item_priv->stvfm)->
			open_stvfm_item(iter, stvfm_item, open_with, error);
	if (rc)
		return -1;

	return 0;
}

static void open_item(GtkTreeView* tree_view, gboolean open_with) {
	SondTVFMItem* stvfm_item = NULL;
	GtkTreePath* path = NULL;
	GtkTreeIter iter = { 0 };
	gint rc = 0;
	GError* error = NULL;

	gtk_tree_view_get_cursor(tree_view, &path, NULL);
	gtk_tree_model_get_iter(gtk_tree_view_get_model(tree_view), &iter, path);
	gtk_tree_path_free(path);
	gtk_tree_model_get(gtk_tree_view_get_model(tree_view), &iter, 0, &stvfm_item, -1);

	rc = sond_treeviewfm_open(&iter, stvfm_item, open_with, &error);
	g_object_unref(stvfm_item);
	if (rc) {
		display_message(SOND_GET_TOPLEVEL(tree_view), "Fehler beim Öffnen\n\n",
				error->message, NULL);
		g_error_free(error);
	}

	return;
}

static void sond_treeviewfm_show_hits(SondTreeviewFM *stvfm,
		GPtrArray *arr_hits) {
	GtkWidget *window = NULL;
	SondTreeviewFMClass *klass = SOND_TREEVIEWFM_GET_CLASS(stvfm);

	/* Einspaltiges Ergebnisfenster */
	gchar const *cols[] = { "Datei", NULL };
	window = sond_result_view_new(
			GTK_WINDOW(SOND_GET_TOPLEVEL(stvfm)),
			"Suchergebnis",
			cols,
			G_CALLBACK(klass->results_row_activated),
			stvfm);

	for (gint i = 0; i < arr_hits->len; i++) {
		gchar const *path = g_ptr_array_index(arr_hits, i);
		gchar const *row[] = { path, NULL };
		sond_result_view_append(window, row);
	}

#if GTK_MAJOR_VERSION < 4
	gtk_widget_show_all(window);
#else
	gtk_widget_set_visible(window, TRUE);
#endif

	return;
}

typedef struct {
	gchar *needle;
	gboolean exact_match;
	gboolean case_sens;
	GPtrArray *arr_hits;
	InfoWindow *info_window;
	volatile gint *atom_ready;
	volatile gint *atom_cancelled;
} SearchFS;

static gint sond_treeviewfm_search_needle(SondTVFMItem* stvfm_item,
		gpointer data, GError **error) {
	gboolean found = FALSE;

	SearchFS *search_fs = (SearchFS*) data;

	if (g_atomic_int_get(search_fs->atom_cancelled))
		g_atomic_int_set(search_fs->atom_ready, 1);
	else {
		SondTVFMItemPrivate* stvfm_item_priv = sond_tvfm_item_get_priv(stvfm_item);

		if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF) {
			gchar* basename = NULL;

			if (!search_fs->case_sens)
				basename = g_ascii_strdown(sond_tvfm_item_get_basename(stvfm_item), -1);
			else
				basename = g_strdup(sond_tvfm_item_get_basename(stvfm_item));

			if (search_fs->exact_match == TRUE) {
				if (!g_strcmp0(basename, search_fs->needle))
					found = TRUE;
			} else if (strstr(basename, search_fs->needle))
				found = TRUE;
			g_free(basename);

			if (found) {
				gchar* filepart = NULL;

				filepart = sond_file_part_get_filepart(stvfm_item_priv->sond_file_part);
				g_ptr_array_add(search_fs->arr_hits, filepart);
			}
		}
		else if (stvfm_item_priv->has_children) { //Muß ja DIR sein
			GPtrArray *arr_children = NULL;
			gint rc = 0;

			rc = sond_tvfm_item_load_children(stvfm_item,
					&arr_children, NULL, error);
			if (rc)
				return -1;

			for (guint i = 0; i < arr_children->len; i++) {
				SondTVFMItem *child_item = (SondTVFMItem*) g_ptr_array_index(arr_children, i);

				rc = sond_treeviewfm_search_needle(child_item, data, error);
				if (rc)
					return -1;

				g_object_unref(child_item);
			}
		}
	}

	return 0;
}

typedef struct {
	SearchFS *search_fs;
	SondTVFMItem* stvfm_item;
	GError **error;
} DataThread;

static gpointer sond_treeviewfm_thread_search(gpointer data) {
	DataThread *data_thread = (DataThread*) data;
	gint rc = 0;

	rc = sond_treeviewfm_search_needle(data_thread->stvfm_item,
			data_thread->search_fs, data_thread->error);
	if (rc)
		return GINT_TO_POINTER(-1);

	g_atomic_int_set(data_thread->search_fs->atom_ready, 1);

	return NULL;
}

static gint sond_treeviewfm_search(SondTreeview *stv, GtkTreeIter *iter,
		gpointer data, GError **error) {
	GThread *thread_search = NULL;
	gpointer res_thread = NULL;
	SondTVFMItem *stvfm_item = NULL;

	SearchFS *search_fs = (SearchFS*) data;

	if (iter) //nur bei Auswahl
		gtk_tree_model_get(gtk_tree_view_get_model(
				GTK_TREE_VIEW(stv)), iter, 0, &stvfm_item, -1);
	else //bei kompletter Suche
		stvfm_item =
				sond_tvfm_item_create(SOND_TREEVIEWFM(stv), NULL, NULL);

	DataThread data_thread = { search_fs, stvfm_item, error };
	thread_search = g_thread_new( NULL, sond_treeviewfm_thread_search,
			&data_thread);

	/* NICHT als reine Busy-Loop ohne jede Pause - das friert die UI (und
	 * damit auch den eigenen Abbrechen-Button im info_window) komplett ein
	 * und beansprucht einen ganzen CPU-Kern nur fürs Pollen. Analog zur
	 * Wartschleife von zond_index_erstellen_ht() (headerbar.c): anstehende
	 * Events abarbeiten, damit Fortschrittsfenster/Abbrechen reagieren,
	 * danach kurz schlafen statt sofort erneut zu pollen. */
	while (!g_atomic_int_get(search_fs->atom_ready)) {
		if (*(search_fs->info_window->cancel))
			g_atomic_int_set(search_fs->atom_cancelled, 1);

		while (gtk_events_pending())
			gtk_main_iteration_do(FALSE);
		g_usleep(20000);
	}

	res_thread = g_thread_join(thread_search);
	g_object_unref(stvfm_item);
	if (GPOINTER_TO_INT(res_thread) == -1)
		return -1;

	return 0;
}


static gint sond_treeviewfm_get_fileparts_foreach(SondTreeview *stv,
		GtkTreeIter *iter, gpointer data, GError **error) {
	SondTVFMItem *stvfm_item = NULL;
	gint rc = 0;

	GHashTable* ht = (GHashTable*) data;

	gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(stv)),
			iter, 0, &stvfm_item, -1);
	rc = sond_tvfm_item_get_fileparts(stvfm_item, ht, error);
	g_object_unref(stvfm_item);
	if (rc)
		return -1;

	return 0;
}

GHashTable* sond_treeviewfm_get_fileparts(SondTreeviewFM *stvfm, gboolean selected_only,
		GError **error) {
	GHashTable* ht = NULL;
	gint rc = 0;

	ht = g_hash_table_new_full(NULL, NULL, g_object_unref, NULL);

	if (selected_only)
		rc = sond_treeview_selection_foreach(SOND_TREEVIEW(stvfm),
				sond_treeviewfm_get_fileparts_foreach, ht, error);
	else {
		SondTVFMItem *stvfm_item = NULL;

		stvfm_item = sond_tvfm_item_create(stvfm, NULL, NULL);
		rc = sond_tvfm_item_get_fileparts(stvfm_item, ht, error);
		g_object_unref(stvfm_item);
	}

	if (rc)
		return NULL;

	return ht;
}

/* --------------------------------------------------------------------------
 * GSimpleAction-Wrapper fuer FM-Kontextmenu
 * -------------------------------------------------------------------------- */
static void sond_treeviewfm_action_einf_ge(GSimpleAction *a, GVariant *p,
		gpointer d) {
	gint rc = 0;
	GError *error = NULL;
	SondTreeviewFM *stvfm = (SondTreeviewFM*) d;
	rc = sond_treeviewfm_create_dir(stvfm, FALSE, &error);
	if (rc) {
		display_message(SOND_GET_TOPLEVEL(stvfm),
				"Verzeichnis kann nicht eingef\u00fcgt werden\n\n",
				error->message, NULL);
		g_error_free(error);
	}
}

static void sond_treeviewfm_action_einf_up(GSimpleAction *a, GVariant *p,
		gpointer d) {
	gint rc = 0;
	GError *error = NULL;
	SondTreeviewFM *stvfm = (SondTreeviewFM*) d;
	rc = sond_treeviewfm_create_dir(stvfm, TRUE, &error);
	if (rc) {
		display_message(SOND_GET_TOPLEVEL(stvfm),
				"Verzeichnis kann nicht eingef\u00fcgt werden\n\n",
				error->message, NULL);
		g_error_free(error);
	}
}

static void sond_treeviewfm_action_paste_ge(GSimpleAction *a, GVariant *p,
		gpointer d) {
	gint rc = 0;
	GError *error = NULL;
	SondTreeviewFM *stvfm = (SondTreeviewFM*) d;
	if (sond_treeview_test_cursor_descendant(SOND_TREEVIEW(stvfm), FALSE))
		display_message(SOND_GET_TOPLEVEL(stvfm),
				"Unzul\u00e4ssiges Ziel: Abk\u00f6mmling von zu verschiebendem Knoten",
				NULL);
	rc = sond_treeviewfm_paste_clipboard(stvfm, FALSE, &error);
	if (rc) {
		display_message(SOND_GET_TOPLEVEL(stvfm),
				"Einf\u00fcgen nicht m\u00f6glich\n\n", error->message, NULL);
		g_error_free(error);
	}
}

static void sond_treeviewfm_action_paste_up(GSimpleAction *a, GVariant *p,
		gpointer d) {
	gint rc = 0;
	GError *error = NULL;
	SondTreeviewFM *stvfm = (SondTreeviewFM*) d;
	if (sond_treeview_test_cursor_descendant(SOND_TREEVIEW(stvfm), TRUE))
		display_message(SOND_GET_TOPLEVEL(stvfm),
				"Unzul\u00e4ssiges Ziel: Abk\u00f6mmling von zu verschiebendem Knoten",
				NULL);
	rc = sond_treeviewfm_paste_clipboard(stvfm, TRUE, &error);
	if (rc) {
		display_message(SOND_GET_TOPLEVEL(stvfm),
				"Einf\u00fcgen nicht m\u00f6glich\n\n", error->message, NULL);
		g_error_free(error);
	}
}

static void sond_treeviewfm_action_loeschen(GSimpleAction *a, GVariant *p,
		gpointer d) {
	gint rc = 0;
	GError *error = NULL;
	SondTreeviewFM *stvfm = (SondTreeviewFM*) d;
	rc = sond_treeview_selection_foreach(SOND_TREEVIEW(stvfm),
			sond_treeviewfm_foreach_loeschen, NULL, &error);
	if (rc == -1) {
		display_message(SOND_GET_TOPLEVEL(stvfm),
				"L\u00f6schen nicht m\u00f6glich\n\n", error->message, NULL);
		g_error_free(error);
	}
}

static void sond_treeviewfm_action_oeffnen(GSimpleAction *a, GVariant *p,
		gpointer d) {
	open_item(GTK_TREE_VIEW(d), FALSE);
}

static void sond_treeviewfm_action_oeffnen_mit(GSimpleAction *a, GVariant *p,
		gpointer d) {
	open_item(GTK_TREE_VIEW(d), TRUE);
}

static void sond_treeviewfm_action_search(GSimpleAction *a, GVariant *p,
		gpointer d) {
	gint rc = 0;
	gchar *search_text = NULL;
	SearchFS search_fs = { 0 };
	gint ready = 0;
	gint cancelled = 0;
	GError *error = NULL;
	SondTreeviewFM *stvfm = (SondTreeviewFM*) d;
	rc = abfrage_frage(SOND_GET_TOPLEVEL(stvfm), "Dateisuche",
			"Bitte Suchtext eingeben", &search_text);
	if (rc != GTK_RESPONSE_YES || !g_strcmp0(search_text, "")) {
		g_free(search_text);
		return;
	}
	search_fs.arr_hits = g_ptr_array_new_with_free_func(g_free);
	search_fs.exact_match = FALSE;
	search_fs.case_sens = FALSE;
	search_fs.atom_ready = &ready;
	search_fs.atom_cancelled = &cancelled;
	search_fs.needle = g_utf8_strdown(search_text, -1);
	search_fs.info_window = info_window_open(SOND_GET_TOPLEVEL(stvfm),
			&cancelled, search_text);
	g_free(search_text);
	rc = sond_treeviewfm_search(SOND_TREEVIEW(stvfm), NULL, &search_fs, &error);
	info_window_kill(search_fs.info_window);
	g_free(search_fs.needle);
	if (rc == -1) {
		display_message(SOND_GET_TOPLEVEL(stvfm),
				"Fehler bei Dateisuche\n\n", error->message, NULL);
		g_error_free(error);
		g_ptr_array_unref(search_fs.arr_hits);
		return;
	}
	if (search_fs.arr_hits->len == 0) {
		display_message(SOND_GET_TOPLEVEL(stvfm), "Keine Datei gefunden", NULL);
		g_ptr_array_unref(search_fs.arr_hits);
		return;
	}
	sond_treeviewfm_show_hits(stvfm, search_fs.arr_hits);
	g_ptr_array_unref(search_fs.arr_hits);
}

static void sond_treeviewfm_action_search_sel(GSimpleAction *a, GVariant *p,
		gpointer d) {
	gint rc = 0;
	gchar *search_text = NULL;
	SearchFS search_fs = { 0 };
	gint ready = 0;
	gint cancelled = 0;
	GError *error = NULL;
	SondTreeviewFM *stvfm = (SondTreeviewFM*) d;
	if (!gtk_tree_selection_count_selected_rows(
			gtk_tree_view_get_selection(GTK_TREE_VIEW(stvfm)))) {
		display_message(SOND_GET_TOPLEVEL(stvfm),
				"Keine Punkte ausgew\u00e4hlt", NULL);
		return;
	}
	rc = abfrage_frage(SOND_GET_TOPLEVEL(stvfm), "Dateisuche",
			"Bitte Suchtext eingeben", &search_text);
	if (rc != GTK_RESPONSE_YES || !g_strcmp0(search_text, "")) {
		g_free(search_text);
		return;
	}
	search_fs.arr_hits = g_ptr_array_new_with_free_func(g_free);
	search_fs.exact_match = FALSE;
	search_fs.case_sens = FALSE;
	search_fs.atom_ready = &ready;
	search_fs.atom_cancelled = &cancelled;
	search_fs.needle = g_utf8_strdown(search_text, -1);
	search_fs.info_window = info_window_open(SOND_GET_TOPLEVEL(stvfm),
			&cancelled, search_text);
	g_free(search_text);
	rc = sond_treeview_selection_foreach(SOND_TREEVIEW(stvfm),
			sond_treeviewfm_search, &search_fs, &error);
	info_window_kill(search_fs.info_window);
	g_free(search_fs.needle);
	if (rc == -1) {
		display_message(SOND_GET_TOPLEVEL(stvfm),
				"Fehler bei Dateisuche\n\n", error->message, NULL);
		g_error_free(error);
		g_ptr_array_unref(search_fs.arr_hits);
		return;
	}
	if (search_fs.arr_hits->len == 0) {
		display_message(SOND_GET_TOPLEVEL(stvfm), "Keine Datei gefunden", NULL);
		g_ptr_array_unref(search_fs.arr_hits);
		return;
	}
	sond_treeviewfm_show_hits(stvfm, search_fs.arr_hits);
	g_ptr_array_unref(search_fs.arr_hits);
}

static void sond_treeviewfm_init_contextmenu(SondTreeviewFM *stvfm) {
	/* Instanzspezifische Aktionen in die ActionGroup eintragen.
	 * Die GMenu-Sections wurden bereits einmalig in class_init aufgebaut. */
	GSimpleActionGroup *ag = sond_treeview_get_action_group(SOND_TREEVIEW(stvfm));

	GSimpleAction *act_einf_ge = g_simple_action_new("einf-ge", NULL);
	g_signal_connect(act_einf_ge, "activate",
			G_CALLBACK(sond_treeviewfm_action_einf_ge), stvfm);
	g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(act_einf_ge));
	g_object_unref(act_einf_ge);

	GSimpleAction *act_einf_up = g_simple_action_new("einf-up", NULL);
	g_signal_connect(act_einf_up, "activate",
			G_CALLBACK(sond_treeviewfm_action_einf_up), stvfm);
	g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(act_einf_up));
	g_object_unref(act_einf_up);

	GSimpleAction *act_paste_ge = g_simple_action_new("paste-ge", NULL);
	g_signal_connect(act_paste_ge, "activate",
			G_CALLBACK(sond_treeviewfm_action_paste_ge), stvfm);
	g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(act_paste_ge));
	g_object_unref(act_paste_ge);

	GSimpleAction *act_paste_up = g_simple_action_new("paste-up", NULL);
	g_signal_connect(act_paste_up, "activate",
			G_CALLBACK(sond_treeviewfm_action_paste_up), stvfm);
	g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(act_paste_up));
	g_object_unref(act_paste_up);

	GSimpleAction *act_loeschen = g_simple_action_new("loeschen", NULL);
	g_signal_connect(act_loeschen, "activate",
			G_CALLBACK(sond_treeviewfm_action_loeschen), stvfm);
	g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(act_loeschen));
	g_object_unref(act_loeschen);

	GSimpleAction *act_oeffnen = g_simple_action_new("oeffnen", NULL);
	g_signal_connect(act_oeffnen, "activate",
			G_CALLBACK(sond_treeviewfm_action_oeffnen), stvfm);
	g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(act_oeffnen));
	g_object_unref(act_oeffnen);

	GSimpleAction *act_oeffnen_mit = g_simple_action_new("oeffnen-mit", NULL);
	g_signal_connect(act_oeffnen_mit, "activate",
			G_CALLBACK(sond_treeviewfm_action_oeffnen_mit), stvfm);
	g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(act_oeffnen_mit));
	g_object_unref(act_oeffnen_mit);

	GSimpleAction *act_search = g_simple_action_new("dateisuche", NULL);
	g_signal_connect(act_search, "activate",
			G_CALLBACK(sond_treeviewfm_action_search), stvfm);
	g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(act_search));
	g_object_unref(act_search);

	GSimpleAction *act_search_sel = g_simple_action_new("dateisuche-sel", NULL);
	g_signal_connect(act_search_sel, "activate",
			G_CALLBACK(sond_treeviewfm_action_search_sel), stvfm);
	g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(act_search_sel));
	g_object_unref(act_search_sel);

	/* SeaDrive-Aktionen */
	sond_treeviewfm_seadrive_init_contextmenu(stvfm);

	return;
}

static void sond_treeviewfm_row_activated(GtkTreeView *tree_view,
		GtkTreePath *tree_path, gpointer column, gpointer data) {
	gint rc = 0;
	GError *error = NULL;
	SondTVFMItem *stvfm_item = NULL;
	GtkTreeIter iter = { 0 };

	gtk_tree_model_get_iter(gtk_tree_view_get_model(tree_view), &iter, tree_path);
	gtk_tree_model_get(gtk_tree_view_get_model(tree_view), &iter, 0, &stvfm_item, -1);

	rc = sond_treeviewfm_open(&iter, stvfm_item, FALSE, &error);
	g_object_unref(stvfm_item);
	if (rc) {
		display_message(SOND_GET_TOPLEVEL(tree_view),
				"Datei kann nicht geöffnet werden\n\n", error->message, NULL);
		g_error_free(error);
	}

	return;
}

static void sond_treeviewfm_row_collapsed(GtkTreeView *tree_view,
		GtkTreeIter *iter, GtkTreePath *path, gpointer data) {
	GtkTreeIter iter_child = { 0 };
	gboolean has_child = FALSE;

	has_child = gtk_tree_model_iter_children(gtk_tree_view_get_model(tree_view),
			&iter_child, iter);

	/* gtk_tree_model_iter_children() kann FALSE liefern, wenn das
	 * Verzeichnis inzwischen (während es expandiert war) leer geworden
	 * ist - z.B. weil sein letztes Kind gelöscht oder verschoben wurde,
	 * ohne dass dabei re-kollabiert wurde. iter_child wäre dann
	 * uninitialisiert; die Schleife darf in diesem Fall gar nicht erst
	 * laufen (Bug-Fix 09/2026, Absturz-Untersuchung: vorher lief hier
	 * unbedingt ein erster do-while-Durchlauf mit ungültigem Iterator). */
	while (has_child)
		has_child = gtk_tree_store_remove(
				GTK_TREE_STORE(gtk_tree_view_get_model(tree_view)),
				&iter_child);

	//dummy einfügen, dir ist ja nicht leer
	gtk_tree_store_insert(GTK_TREE_STORE(gtk_tree_view_get_model(tree_view)),
			&iter_child, iter, -1);

	return;
}

static gint sond_treeviewfm_expand_dummy(SondTreeviewFM *stvfm, GtkTreeIter *iter,
		SondTVFMItem *stvfm_item, GError **error) {
	GPtrArray *arr_children = NULL;
	gint rc = 0;

	rc = sond_tvfm_item_load_children(stvfm_item, &arr_children, NULL, error);
	if (rc)
		return -1;

	for (gint i = 0; i < arr_children->len; i++) {
		GtkTreeIter iter_new = { 0 };
		SondTVFMItem* child_item = NULL;
		SondTVFMItemPrivate* child_item_priv = NULL;

		child_item = g_ptr_array_index(arr_children, i);
		child_item_priv = sond_tvfm_item_get_priv(child_item);

		gtk_tree_store_insert(GTK_TREE_STORE(
				gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm) )),
				&iter_new, iter, -1);
		gtk_tree_store_set(GTK_TREE_STORE(
				gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm) )),
				&iter_new, 0, G_OBJECT(child_item), -1);

		if (child_item_priv->has_children) { //Dummy einfügen
			GtkTreeIter newest_iter = { 0 };

			gtk_tree_store_insert(GTK_TREE_STORE(
					gtk_tree_view_get_model( GTK_TREE_VIEW(stvfm) )),
					&newest_iter, &iter_new, -1);
		}
	}

	g_ptr_array_unref(arr_children);

	return 0;
}

/* Nutzer-Fund 18.09.2026: Verzeichnis mit "Invalid argument" nicht
 * expandierbar (bekannte CRT-_wfopen()-Einschränkung bei Pfadkomponenten
 * mit Leerzeichen/Punkt am Ende - s. ausführliche Doku in ToDo.c,
 * Einträge 11./16.09.2026 - "Stabilität hat Vorrang", bewusst nicht
 * behoben). Bislang blieb die Zeile trotz des Fehlschlags GTK-seitig
 * "expandiert" (der Expander-Pfeil war schon umgeschaltet, bevor dieser
 * Handler überhaupt lief) - mit der (nie entfernten) Dummy-Zeile als
 * einzigem sichtbaren Kind. Dieses Dummy-Kind hat bewusst KEIN
 * SondTVFMItem (Spalte 0 bleibt NULL - dient nur dazu, den Expander-Pfeil
 * anzuzeigen, bevor die echten Kinder geladen sind); beim Rendern dieser
 * jetzt sichtbaren Zeile liefen deshalb dauerhaft "Keine Objekt im
 * Baum"/"Kein SondTVFMItem"-Warnungen aus den Cell-Renderern auf - auch
 * beim bloßen Vorbeiscrollen an dieser Zeile, ohne dass das ursächliche
 * Verzeichnis selbst je wieder angeklickt wurde. Separat gefundener,
 * unabhängiger Leak auf demselben Fehlerpfad: stvfm_item (oben per
 * gtk_tree_model_get() gereffet) wurde nie wieder unreffed. */
static gboolean row_expand_failed_collapse_idle(gpointer data) {
	GtkTreeView *tree_view = ((gpointer *) data)[0];
	GtkTreePath *path = ((gpointer *) data)[1];

	/* Per g_idle_add() entkoppelt statt direkt aus dem "row-expanded"-
	 * Handler heraus zu kollabieren - vermeidet, die Baumstruktur mitten
	 * in dessen eigener Signal-Verarbeitung zu verändern.
	 * gtk_tree_view_collapse_row() löst "row-collapsed" aus, dessen
	 * Handler (sond_treeviewfm_row_collapsed()) ohnehin schon alle Kinder
	 * entfernt und einen frischen Dummy einfügt - die Zeile landet damit
	 * exakt im normalen, für einen erneuten Versuch bereiten
	 * "eingeklappt, noch nicht geladen"-Zustand. */
	gtk_tree_view_collapse_row(tree_view, path);

	gtk_tree_path_free(path);
	g_free(data);

	return G_SOURCE_REMOVE;
}

static void sond_treeviewfm_row_expanded(GtkTreeView *tree_view,
		GtkTreeIter *iter, GtkTreePath *path, gpointer data) {
	gint rc = 0;
	GtkTreeIter iter_dummy = { 0 };
	GError *error = NULL;
	SondTVFMItem* stvfm_item = NULL;

	//
	gtk_tree_model_iter_nth_child(gtk_tree_view_get_model(tree_view), &iter_dummy,
			iter, 0);

	gtk_tree_model_get(gtk_tree_view_get_model(tree_view), iter, 0,
			&stvfm_item, -1);

	rc = sond_treeviewfm_expand_dummy(SOND_TREEVIEWFM(tree_view), iter, stvfm_item, &error);
	if (rc) {
		gpointer *idle_data = NULL;

		/* error kann NULL sein, wenn der Fehlerpfad (z.B. ein
		 * g_return_val_if_fail() tiefer im Aufrufbaum) keinen GError setzt -
		 * error->message wäre dann ein Absturz statt nur einer fehlenden
		 * Fehlermeldung (Absturz-Untersuchung 09/2026). */
		display_message(SOND_GET_TOPLEVEL(tree_view),
				"Zeile konnte nicht expandiert werden\n\n",
				error ? error->message : "(keine Fehlermeldung verfügbar)",
				NULL);
		if (error)
			g_error_free(error);

		/* S. ausführlichen Doc-Kommentar oben (18.09.2026) - Zeile wieder
		 * einklappen statt sie mit sichtbarer, item-loser Dummy-Zeile
		 * "expandiert" zu belassen. */
		idle_data = g_new0(gpointer, 2);
		idle_data[0] = tree_view;
		idle_data[1] = gtk_tree_path_copy(path);
		g_idle_add(row_expand_failed_collapse_idle, idle_data);

		g_object_unref(stvfm_item); /* s.o. - war hier bisher geleakt */

		return;
	}

	g_object_unref(stvfm_item);

	gtk_tree_store_remove(GTK_TREE_STORE(gtk_tree_view_get_model(tree_view)),
			&iter_dummy);

	return;
}

static void sond_treeviewfm_render_eingang(GtkTreeViewColumn *column,
		GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter,
		gpointer data) {
	/*
	 gint rc = 0;
	 gint eingang_id = 0;
	 gchar* rel_path = NULL;
	 gchar* errmsg = NULL;

	 SondTreeviewFM* stvfm = (SondTreeviewFM*) data;
	 SondTreeviewFMPrivate* stvfm_priv = sond_treeviewfm_get_instance_private( stvfm );

	 rel_path = sond_treeviewfm_get_rel_path( stvfm, iter );
	 if ( !rel_path ) return;

	 //    rc = eingang_for_rel_path( stvfm_priv->zond_dbase, rel_path, &eingang_id, &eingang, NULL, &errmsg );
	 g_free( rel_path );
	 if ( rc == -1 )
	 {
	 display_message( gtk_widget_get_toplevel( GTK_WIDGET(stvfm) ),
	 "Warnung -\n\nBei Aufruf eingang_for_rel_path:\n",
	 errmsg, NULL );
	 g_free( errmsg );
	 }
	 else if ( rc == 1 )
	 {
	 if ( eingang_id ) g_object_set( G_OBJECT(renderer), "text",
	 "Datum", NULL );
	 else g_object_set( G_OBJECT(renderer), "text", "----", NULL );
	 }
	 else g_object_set( G_OBJECT(renderer), "text", "", NULL );
	 */
	return;
}

static void sond_treeviewfm_render_file_modify(GtkTreeViewColumn *column,
		GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter,
		gpointer data) {
	GObject *object = NULL;

	gtk_tree_model_get(model, iter, 0, &object, -1);

	if (G_IS_FILE_INFO(object)) {
		GDateTime *datetime = NULL;
		gchar *text = NULL;

		datetime = g_file_info_get_modification_date_time(G_FILE_INFO(object));

		text = g_date_time_format(datetime, "%d.%m.%Y %T");
		g_date_time_unref(datetime);
		g_object_set(G_OBJECT(renderer), "text", text, NULL);
		g_free(text);
	}

	g_object_unref(object);

	return;
}

static void sond_treeviewfm_render_file_size(GtkTreeViewColumn *column,
		GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter,
		gpointer data) {
	GObject *object = NULL;

	gtk_tree_model_get(model, iter, 0, &object, -1);

	if (G_IS_FILE_INFO(object)) {
		goffset size = 0;
		gchar *text = NULL;

//		size = g_file_info_get_size(G_FILE_INFO(object));
#ifdef _WIN32
		text = g_strdup_printf("%lld", (long long) size);
#else
		text = g_strdup_printf("%ld", (long) size);
#endif
		g_object_set(G_OBJECT(renderer), "text", text, NULL);
		g_free(text);
	}

	g_object_unref(object);

	return;
}

/* Ermittelt den filepart-Pfad (Datei/Verzeichnis, "//"-Konvention bei
 * eingebetteten Inhalten) für die Indizierungsstatus-Abfrage - unabhängig
 * vom OS-Pfad "rel", den der SeaDrive-Zweig weiter unten für den
 * Dateisystem-Zugriff braucht. NULL, wenn für dieses Item kein sinnvoller
 * Pfad existiert (z.B. eine reine Anbindung/Section ohne eigene Datei). */
static gchar* sond_treeviewfm_get_coverage_path(SondTVFMItemPrivate *priv,
		gboolean *out_is_dir) {
	*out_is_dir = FALSE;

	if (priv->type == SOND_TVFM_ITEM_TYPE_LEAF && priv->sond_file_part)
		return sond_file_part_get_filepart(priv->sond_file_part);

	if (priv->type == SOND_TVFM_ITEM_TYPE_DIR) {
		*out_is_dir = TRUE;
		if (!priv->sond_file_part) //DIR im Filesystem
			return g_strdup(priv->path_or_section);
		else //Container (z.B. Zip/GMessage) als "Verzeichnis" dargestellt
			return sond_file_part_get_filepart(priv->sond_file_part);
	}

	return NULL;
}

/* Indizierungsstatus für das Overlay-Icon, oder SOND_INDEX_STATUS_NONE
 * (kein Overlay - s. Absprache: nur teilweise/vollständig werden markiert,
 * sonst zu viel visuelles Rauschen in einem frischen, noch nicht
 * indizierten Projekt). Liefert außerdem NONE, wenn der Punkt gar nicht
 * indizierbar ist (z.B. .db/.znd) - dieselbe Prüfung wie beim
 * Abdeckungs-Check der Indexsuche (check_coverage_one()). */
static SondIndexStatus sond_treeviewfm_get_index_status(
		SondTreeviewFM *stvfm, SondTVFMItem *stvfm_item) {
	SondTVFMItemPrivate *priv = sond_tvfm_item_get_priv(stvfm_item);
	SondTreeviewFMPrivate *stvfm_priv = sond_treeviewfm_get_instance_private(stvfm);
	SondIndexCtx *index_ctx = NULL;
	gchar *coverage_path = NULL;
	gboolean is_dir = FALSE;
	SondIndexStatus status = SOND_INDEX_STATUS_NONE;

	if (!stvfm_priv->index_ctx_func)
		return SOND_INDEX_STATUS_NONE;

	index_ctx = stvfm_priv->index_ctx_func(stvfm_priv->index_ctx_func_data);
	if (!index_ctx)
		return SOND_INDEX_STATUS_NONE;

	/* Section (Anbindung o.ä.): komplett an die Unterklasse delegiert.
	 * Nutzer-Einwand 16.09.2026: früher lieferte die Unterklasse hier nur
	 * zwei Ints (von_seite/bis_seite), die DIESE Basisklasse dann selbst
	 * als PDF-artigen Seitenbereich interpretierte und an
	 * sond_index_ctx_get_file_status() weiterreichte - eine Vermischung,
	 * da "Section = Seitenbereich" eine zond/PDF-spezifische Annahme ist
	 * (bei zond zufällig immer zutreffend), die die generische
	 * Basisklasse nicht voraussetzen darf (s. ausführlichen Kommentar an
	 * get_section_index_status(), sond_treeviewfm.h). Die Unterklasse
	 * bekommt jetzt den bereits ermittelten index_ctx übergeben und
	 * liefert den fertigen Status direkt. */
	if (priv->type == SOND_TVFM_ITEM_TYPE_LEAF_SECTION) {
		if (SOND_TREEVIEWFM_GET_CLASS(stvfm)->get_section_index_status)
			return SOND_TREEVIEWFM_GET_CLASS(stvfm)->get_section_index_status(
					stvfm_item, index_ctx);

		return SOND_INDEX_STATUS_NONE;
	}

	coverage_path = sond_treeviewfm_get_coverage_path(priv, &is_dir);
	if (!coverage_path)
		return SOND_INDEX_STATUS_NONE;

	if (is_dir)
		status = sond_index_ctx_get_dir_status(index_ctx, coverage_path);
	else {
		/* Nicht indizierbare Dateitypen (.db, .znd, Bilder, ...) gar nicht
		 * erst prüfen - sonst zeigt jede solche Datei dauerhaft "nicht
		 * indiziert" an, obwohl sie nie indiziert werden wird.
		 *
		 * Nutzer-Fund 16.09.2026: der MIME-Typ wird jetzt bevorzugt vom
		 * SondFilePart selbst geholt statt ihn hier ein zweites Mal (und
		 * unzuverlässig) aus der Endung von coverage_path zu raten. Bei
		 * einem SondFilePartLeaf ist das der beim Erzeugen per echtem
		 * Content-Sniffing ermittelte und gespeicherte Typ (s.
		 * sond_file_part_create()). Betraf v.a. eingebettete
		 * Container-Einträge ohne aussagekräftige Endung - z.B. einzelne
		 * MIME-Parts einer E-Mail (eine HTML-Alternative, ein
		 * Inline-Bild): deren "Pfad" ist ein interner, von der
		 * MIME-Bibliothek vergebener Name ohne (oder mit irreführender)
		 * Endung - der echte Typ ("text/html" etc.) steht aber längst auf
		 * dem SondFilePartLeaf. PDF und GMessage (E-Mail) als LEAF (kein
		 * Multipart/keine Einbettungen) sind unabhängig von der Endung
		 * immer unterstützt - analog zur schon bestehenden PDF-Ausnahme,
		 * jetzt auch für GMessage ergänzt. */
		/* Message-Knoten einer E-Mail: eindeutig erkennbar wie schon in
		 * zond_treeviewfm_item_get_fileparts() (Schritt 2, E-Mail-Coverage-
		 * Redesign, 17.09.2026) - LEAF ohne path_or_section, dessen
		 * sond_file_part derselbe wie der der ganzen eml ist (kein eigener
		 * Mimepart-Kind-sfp). coverage_path ist dafür bewusst der BARE
		 * Dateiname ("mail.eml", s. sond_treeviewfm_get_coverage_path()) -
		 * der Header wird aber seit Schritt 3 gezielt unter
		 * "mail.eml//header" abgedeckt, nicht unter "mail.eml" selbst.
		 * Badge zeigt FULL, wenn ENTWEDER der Header gezielt indiziert ist
		 * ODER die ganze Mail als ein Block/per Collapse (Schritt 4)
		 * unter "mail.eml" selbst abgedeckt ist - beides bedeutet "Header
		 * ist durchsucht". Sonst der jeweils bessere Teilstatus (NONE <
		 * PARTIAL < FULL), damit ein begonnener, aber noch nicht
		 * abgeschlossener Zustand nicht fälschlich als "gar nichts"
		 * erscheint. */
		if (priv->type == SOND_TVFM_ITEM_TYPE_LEAF && !priv->path_or_section &&
				SOND_IS_FILE_PART_GMESSAGE(priv->sond_file_part)) {
			gchar *header_path = g_strconcat(coverage_path, "//header", NULL);
			SondIndexStatus header_status =
					sond_index_ctx_get_file_status(index_ctx, header_path, -1, -1);
			SondIndexStatus whole_status =
					sond_index_ctx_get_file_status(index_ctx, coverage_path, -1, -1);

			g_free(header_path);
			status = MAX(header_status, whole_status);
		}
		else if (SOND_IS_FILE_PART_PDF(priv->sond_file_part) ||
				SOND_IS_FILE_PART_GMESSAGE(priv->sond_file_part))
			status = sond_index_ctx_get_file_status(index_ctx, coverage_path, -1, -1);
		else {
			gchar const *mime_type = SOND_IS_FILE_PART_LEAF(priv->sond_file_part) ?
					sond_file_part_leaf_get_mime_type(
							SOND_FILE_PART_LEAF(priv->sond_file_part)) :
					mime_from_extension(coverage_path);

			if (!sond_index_mime_type_supported(mime_type)) {
				g_free(coverage_path);
				return SOND_INDEX_STATUS_NONE;
			}
			status = sond_index_ctx_get_file_status(index_ctx, coverage_path, -1, -1);
		}
	}

	g_free(coverage_path);

	return status;
}

static void sond_treeviewfm_render_file_icon(GtkTreeViewColumn *column,
		GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter,
		gpointer data) {
	SondTVFMItem* stvfm_item = NULL;
	SondTVFMItemPrivate* stvfm_item_priv = NULL;
	SondTreeviewFM *stvfm = SOND_TREEVIEWFM(data);
	SondSeadriveBadge seadrive_badge = SOND_SEADRIVE_BADGE_NONE; //unten rechts
	SondSeadriveDirStatus dir_status = SOND_SEADRIVE_DIR_STATUS_NONE; //unten rechts, nur Verzeichnisse
	SondIndexStatus index_status = SOND_INDEX_STATUS_NONE; //unten links: Indizierung

	gtk_tree_model_get(model, iter, 0, &stvfm_item, -1);
	if (!stvfm_item) {
		LOG_WARN("%s: Kein SondTVFMItem", __func__);
		return;
	}

	stvfm_item_priv = sond_tvfm_item_get_priv(stvfm_item);
	g_object_unref(stvfm_item);

	/* Overlay-Icon für SeaDrive-Cloud-Status ermitteln */
	if (sond_treeviewfm_is_seadrive_path(stvfm)) {
		gchar *full_path = NULL;

		const gchar *root = sond_treeviewfm_get_root(stvfm);
		const gchar* rel = NULL;

		/* Dateien (LEAF) und Filesystem-Verzeichnisse erhalten Overlay-Icons.
		 *
		 * Nutzer-Fund 18.09.2026: eine als "immer verfügbar" (gepinnt)
		 * markierte .eml bekam selbst das grüne Badge, ihre Mime-Parts
		 * (Anhänge/Inline-Teile, als eigene LEAF-Kindzeilen mit
		 * sond_file_part_get_parent() != NULL dargestellt) aber nicht.
		 * Ursache: hier wurde bisher per !sond_file_part_get_parent(...)
		 * genau auf Top-Level-Objekte ohne Parent eingeschränkt - Mime-
		 * Parts (und ebenso ZIP-Einträge, PDF-Seiten als eigene Zeilen
		 * usw.) fielen dadurch grundsätzlich raus. Der SeaDrive-Pin-/
		 * Hydrierungsstatus gehört aber zur realen Datei im Dateisystem,
		 * nicht zum einzelnen (virtuellen) Teil - alle Kinder EINER realen
		 * Datei müssen also dasselbe Badge zeigen wie die Datei selbst.
		 * Fix: statt die Top-Level-Bedingung zu prüfen, wird jetzt immer
		 * zum obersten Vorfahren hochgelaufen (Schleife wie in
		 * sond_file_part_get_filepart()/zond_treeview_get_seadrive_badge())
		 * und dessen Pfad für den full_path/Hashtable-Lookup verwendet -
		 * bei einem Top-Level-Objekt (kein Parent) macht die Schleife
		 * nichts, verhält sich also für den bisherigen Fall unverändert. */
		if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF &&
				// stvfm_item_priv->sond_file_part && - überflüssig?!
				//PDF mit children - Pagetree
				!(SOND_IS_FILE_PART_PDF(stvfm_item_priv->sond_file_part) &&
						sond_file_part_get_has_children(stvfm_item_priv->sond_file_part))) {
			SondFilePart *top = stvfm_item_priv->sond_file_part;

			while (sond_file_part_get_parent(top))
				top = sond_file_part_get_parent(top);

			rel = sond_file_part_get_path(top);
		}
		else if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_DIR) {
			if (!stvfm_item_priv->sond_file_part) //DIR im Filesystem
				rel = stvfm_item_priv->path_or_section;
			else {
				/* Nutzer-Fund 18.09.2026: "Die (virtuellen) Verzeichnisse
				 * in einem Container (zip-Verzeichnis, multipart) werden
				 * nicht mit badge markiert." - der bisherige zusätzliche
				 * !path_or_section-Check schloss genau diesen Fall aus:
				 * ein bereits aufgeklapptes ZIP-Unterverzeichnis oder ein
				 * Multipart-Verzeichnis einer E-Mail hat sond_file_part
				 * (dasselbe Objekt wie das Container-Top-Level-Item) UND
				 * path_or_section (den internen Pfad/die Kennung
				 * innerhalb des Containers) gesetzt - beides sind aber
				 * virtuelle Ansichten EINER realen Datei, für die
				 * genauso das Badge der realen Datei gelten muss (s.
				 * Mime-Part-Fix oben, gleicher Tag). Deshalb jetzt ohne
				 * die path_or_section-Bedingung immer zum obersten
				 * Vorfahren hochgelaufen. */
				SondFilePart *top = stvfm_item_priv->sond_file_part;

				while (sond_file_part_get_parent(top))
					top = sond_file_part_get_parent(top);

				rel = sond_file_part_get_path(top);
			}
		}

		if (rel)
			full_path = g_strconcat(root, "/", rel, NULL);

		if (full_path) {
#ifdef _WIN32
			if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF) {
				/* Reiner Hashtable-Lookup statt live GetFileAttributesW -
				 * der Watcher hält seadrive_file_badges ohnehin schon
				 * aktuell (Konsistenz mit dir_status, das schon vorher aus
				 * der Hashtable las - Untersuchung "Ordner-Badges",
				 * 09/2026). */
				seadrive_badge = sond_treeviewfm_seadrive_get_file_badge(
						stvfm, full_path);
			} else if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_DIR) {
				/* Ordner selbst werden vom Scan/Watcher NICHT in
				 * seadrive_file_badges geführt (der bezieht sich nur auf
				 * Dateien) - eigenes Attribut bleibt hier bewusst ein
				 * live-Check, da Ordner praktisch nie PINNED/offline
				 * markiert sind (seltener Sonderfall, kein Performance-
				 * Thema). Aussagekräftig ist ohnehin der aggregierte
				 * Teilbaum-Status (dir_status) darunter. */
				wchar_t *lp = prepare_long_path(full_path, NULL);
				if (lp) {
					DWORD attrs = GetFileAttributesW(lp);
					g_free(lp);
					if (attrs != INVALID_FILE_ATTRIBUTES) {
						gboolean pinned   = (attrs & FILE_ATTRIBUTE_PINNED) != 0;
						gboolean offline  = (attrs & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS) != 0;

						/* Prioritätsreihenfolge s. Kommentar bei
						 * SondSeadriveBadge (sond_icon_util.h). */
						if (offline && pinned)
							seadrive_badge = SOND_SEADRIVE_BADGE_PENDING;
						else if (offline)
							seadrive_badge = SOND_SEADRIVE_BADGE_OFFLINE;
						else if (pinned)
							seadrive_badge = SOND_SEADRIVE_BADGE_PINNED;
					}
				}

				/* Aggregierten Teilbaum-Status konsultieren, wenn das
				 * Ordner-eigene Attribut nichts zeigt (Normalfall). */
				if (seadrive_badge == SOND_SEADRIVE_BADGE_NONE)
					dir_status = sond_treeviewfm_seadrive_get_dir_status(
							stvfm, full_path);
			}
#endif
			g_free(full_path);
		}
	}

	/* Overlay-Icon für Indizierungsstatus ermitteln (unabhängig von
	 * SeaDrive - beide können gleichzeitig zutreffen, dann je eine Ecke) */
	index_status = sond_treeviewfm_get_index_status(stvfm, stvfm_item);

	/* Immer über sond_icon_util_render_with_overlays() rendern (auch mit
	 * 0 Overlays), NIE mehr direkt "icon-name" auf dem Renderer setzen:
	 * GtkCellRendererPixbufs eigene icon-name-Aufloesung bestimmt die
	 * Pixelgroesse ueber die "stock-size"-Property und lieferte in dieser
	 * Umgebung für Ordner (icon_name "folder") einfach GAR KEIN Icon -
	 * während derselbe Name über gtk_icon_theme_load_icon() mit expliziter
	 * Pixelgroesse (s. sond_icon_util_renderer_get_size()) zuverlässig
	 * funktioniert. Dateien fielen das vorher nicht auf, weil sie fast
	 * immer schon ein Overlay-Badge hatten und damit ohnehin über
	 * render_with_overlays liefen (Untersuchung "Ordner ohne Icon",
	 * 09/2026). */
	{
		SondIconOverlay overlays[3];
		guint n_overlays = 0;
		gint overlay_px = MAX(sond_icon_util_renderer_get_size(renderer) / 2, 8);
		GdkPixbuf *seadrive_pb = NULL;
		GdkPixbuf *index_pb = NULL;
		GdkPixbuf *attachment_pb = NULL;

		if (seadrive_badge != SOND_SEADRIVE_BADGE_NONE) {
			/* SeaDrive-Status unten rechts (einzelne Datei/Ordner selbst) */
			seadrive_pb = sond_icon_util_seadrive_badge_pixbuf(seadrive_badge,
					overlay_px);
			if (seadrive_pb) {
				overlays[n_overlays].pixbuf = seadrive_pb;
				overlays[n_overlays].corner = SOND_ICON_CORNER_BOTTOM_RIGHT;
				n_overlays++;
			}
		} else if (dir_status != SOND_SEADRIVE_DIR_STATUS_NONE) {
			/* SeaDrive-Status unten rechts (Ordner-Teilbaum-Aggregation -
			 * nur wenn kein Ordner-eigenes Attribut greift, s.o.) */
			seadrive_pb = sond_icon_util_seadrive_dir_badge_pixbuf(dir_status,
					overlay_px);
			if (seadrive_pb) {
				overlays[n_overlays].pixbuf = seadrive_pb;
				overlays[n_overlays].corner = SOND_ICON_CORNER_BOTTOM_RIGHT;
				n_overlays++;
			}
		}

		if (index_status != SOND_INDEX_STATUS_NONE) {
			/* Indizierungsstatus unten links */
			index_pb = sond_icon_util_status_badge_pixbuf(index_status, overlay_px);
			if (index_pb) {
				overlays[n_overlays].pixbuf = index_pb;
				overlays[n_overlays].corner = SOND_ICON_CORNER_BOTTOM_LEFT;
				n_overlays++;
			}
		}

		/* Attachment-Badge oben rechts (16.09.2026, Nutzerwunsch: Attachment/
		 * Inline im Baum unterscheidbar machen) - unabhängig von DIR/LEAF,
		 * da ein Attachment je nach Inhalt auch ein Container (ZIP/PDF/
		 * verschachtelte E-Mail, dann als DIR dargestellt) sein kann; das
		 * Attribut hängt am sond_file_part selbst (s. Doc-Kommentar an
		 * sond_file_part_get_is_attachment()), nicht am Baum-Item-Typ. */
		if (stvfm_item_priv->sond_file_part &&
				sond_file_part_get_is_attachment(stvfm_item_priv->sond_file_part)) {
			attachment_pb = sond_icon_util_attachment_badge_pixbuf(
					GTK_WIDGET(stvfm), overlay_px);
			if (attachment_pb) {
				overlays[n_overlays].pixbuf = attachment_pb;
				overlays[n_overlays].corner = SOND_ICON_CORNER_TOP_RIGHT;
				n_overlays++;
			}
		}

		sond_icon_util_render_with_overlays(GTK_WIDGET(stvfm), renderer,
				stvfm_item_priv->icon_name, overlays, n_overlays);

		if (seadrive_pb) g_object_unref(seadrive_pb);
		if (index_pb) g_object_unref(index_pb);
		if (attachment_pb) g_object_unref(attachment_pb);
	}

	return;
}

static void sond_treeviewfm_init(SondTreeviewFM *stvfm) {
	SondTreeviewFMPrivate *stvfm_priv = sond_treeviewfm_get_instance_private(
			stvfm);

	gtk_tree_view_column_set_cell_data_func(
			gtk_tree_view_get_column(GTK_TREE_VIEW(stvfm), 0),
			sond_treeview_get_cell_renderer_icon(SOND_TREEVIEW(stvfm)),
			sond_treeviewfm_render_file_icon, stvfm, NULL);

	//Größe
	GtkCellRenderer *renderer_size = gtk_cell_renderer_text_new();

	GtkTreeViewColumn *fs_tree_column_size = gtk_tree_view_column_new();
	gtk_tree_view_column_set_resizable(fs_tree_column_size, FALSE);
	gtk_tree_view_column_set_sizing(fs_tree_column_size,
			GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_column_pack_start(fs_tree_column_size, renderer_size, FALSE);
	gtk_tree_view_column_set_cell_data_func(fs_tree_column_size, renderer_size,
			sond_treeviewfm_render_file_size, NULL, NULL);

	//Änderungsdatum
	GtkCellRenderer *renderer_modify = gtk_cell_renderer_text_new();

	GtkTreeViewColumn *fs_tree_column_modify = gtk_tree_view_column_new();
	gtk_tree_view_column_set_resizable(fs_tree_column_modify, FALSE);
	gtk_tree_view_column_set_sizing(fs_tree_column_modify,
			GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_column_pack_start(fs_tree_column_modify, renderer_modify,
			FALSE);
	gtk_tree_view_column_set_cell_data_func(fs_tree_column_modify,
			renderer_modify, sond_treeviewfm_render_file_modify, NULL, NULL);

	//Eingang
	GtkCellRenderer *renderer_eingang = gtk_cell_renderer_text_new();

	stvfm_priv->column_eingang = gtk_tree_view_column_new();
	gtk_tree_view_column_set_resizable(stvfm_priv->column_eingang, FALSE);
	gtk_tree_view_column_set_sizing(stvfm_priv->column_eingang,
			GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_column_pack_start(stvfm_priv->column_eingang,
			renderer_eingang, FALSE);
	gtk_tree_view_column_set_cell_data_func(stvfm_priv->column_eingang,
			renderer_eingang, sond_treeviewfm_render_eingang, stvfm, NULL);
//    gtk_tree_view_column_set_visible( stvfm_priv->column_eingang, FALSE );

	gtk_tree_view_append_column(GTK_TREE_VIEW(stvfm),
			stvfm_priv->column_eingang);
	gtk_tree_view_append_column(GTK_TREE_VIEW(stvfm), fs_tree_column_size);
	gtk_tree_view_append_column(GTK_TREE_VIEW(stvfm), fs_tree_column_modify);

	gtk_tree_view_column_set_title(
			gtk_tree_view_get_column(GTK_TREE_VIEW(stvfm), 0), "Datei");
	gtk_tree_view_column_set_title(fs_tree_column_size, "Größe");
	gtk_tree_view_column_set_title(fs_tree_column_modify, "Änderungsdatum");
	gtk_tree_view_column_set_title(stvfm_priv->column_eingang, "Eingang");

	GtkTreeStore *tree_store = gtk_tree_store_new(1, SOND_TYPE_TVFM_ITEM);
	gtk_tree_view_set_model(GTK_TREE_VIEW(stvfm), GTK_TREE_MODEL(tree_store));
	g_object_unref(tree_store);

	//Zeile expandiert
	g_signal_connect(stvfm, "row-expanded",
			G_CALLBACK(sond_treeviewfm_row_expanded), NULL);
	//Zeile kollabiert
	g_signal_connect(stvfm, "row-collapsed",
			G_CALLBACK(sond_treeviewfm_row_collapsed), NULL);
	// Doppelklick = angebundene Datei anzeigen
	g_signal_connect(stvfm, "row-activated",
			G_CALLBACK(sond_treeviewfm_row_activated), NULL);

	sond_treeviewfm_init_contextmenu(stvfm);

	return;
}

gint sond_treeviewfm_set_root(SondTreeviewFM *stvfm, const gchar *root,
		GError **error) {
	gint rc = 0;
	SondTVFMItem* stvfm_item = NULL;

	SondFilePartClass* sfp_class = g_type_class_peek_static(SOND_TYPE_FILE_PART);
	SondTreeviewFMPrivate *stvfm_priv = sond_treeviewfm_get_instance_private(
			stvfm);

	g_free(stvfm_priv->root);
	g_free(sfp_class->path_root);

#ifdef _WIN32
	sond_treeviewfm_seadrive_stop_watcher_async(stvfm);
	/* Setzt Zähler zurück und leert (im Hintergrund, s. dortigen
	 * ausführlichen Kommentar zum "Schließen dauert 20 Sek."-Fund
	 * 18.09.2026) die vier SeaDrive-Ground-Truth-Hashtables - MUSS hier
	 * passieren, sonst bleiben Pfade/Zähler einer vorigen Projekt-Session
	 * stehen und verfälschen die Anzeige beim nächsten Öffnen desselben
	 * Projekts. Jetzt in sond_seadrive.c (Refactoring 18.09.2026, "in
	 * _treeviewfm.c sind auch Funktionen, die in sond_treeviewfm_seadrive
	 * gehören"). */
	sond_seadrive_reset_ground_truth(stvfm);
#endif

	if (!root) {
		stvfm_priv->root = NULL;
		sfp_class->path_root = NULL;
		stvfm_priv->is_seadrive_path = FALSE;

		gtk_tree_store_clear(
				GTK_TREE_STORE(
						gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm) )));

		return 0;
	}

	stvfm_priv->root = g_strdup(root);
	sfp_class->path_root = g_strdup(root); /* synchron halten - siehe Kommentar in sond_fileparts.h */

#ifdef _WIN32
	stvfm_priv->is_seadrive_path = sond_seadrive_is_seadrive_path(root);
	if (stvfm_priv->is_seadrive_path)
		sond_treeviewfm_seadrive_start_watcher(stvfm);
	/* SeaDrive-Menüpunkt aktivieren/deaktivieren */
	{
		GtkWidget *menu_item = g_object_get_data(
				G_OBJECT(stvfm), "seadrive-menu-item");
		if (menu_item)
			gtk_widget_set_sensitive(menu_item,
					stvfm_priv->is_seadrive_path);
	}
#else
	stvfm_priv->is_seadrive_path = FALSE;
#endif

	//zum Arbeitsverzeichnis machen
	g_chdir(stvfm_priv->root);

	stvfm_item = sond_tvfm_item_create(stvfm, NULL, NULL);

	rc = sond_treeviewfm_expand_dummy(stvfm, NULL, stvfm_item, error);
	g_object_unref(stvfm_item);
	if (rc) {
		g_free(stvfm_priv->root);
		stvfm_priv->root = NULL;

		return -1;
	}

	return 0;
}

const gchar*
sond_treeviewfm_get_root(SondTreeviewFM *stvfm) {
	if (!stvfm)
		return NULL;

	SondTreeviewFMPrivate *stvfm_priv = sond_treeviewfm_get_instance_private(
			stvfm);

	return stvfm_priv->root;
}

gboolean
sond_treeviewfm_is_seadrive_path(SondTreeviewFM *stvfm) {
	if (!stvfm)
		return FALSE;

	SondTreeviewFMPrivate *stvfm_priv = sond_treeviewfm_get_instance_private(
			stvfm);

	return stvfm_priv->is_seadrive_path;
}

void
sond_treeviewfm_set_index_ctx_func(SondTreeviewFM *stvfm,
		SondTreeviewFMIndexCtxFunc func, gpointer user_data) {
	if (!stvfm)
		return;

	SondTreeviewFMPrivate *stvfm_priv = sond_treeviewfm_get_instance_private(
			stvfm);

	stvfm_priv->index_ctx_func = func;
	stvfm_priv->index_ctx_func_data = user_data;

	gtk_widget_queue_draw(GTK_WIDGET(stvfm));

	return;
}


