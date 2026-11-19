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
#include <stdlib.h>
#ifdef G_OS_WIN32
#include <windows.h>
#endif

#include "../sond_log_and_error.h"
#include "../sond_fileparts.h"
#include "../sond_file_helper.h"
#include "../sond_pdf_helper.h"
#include "../sond_password.h"
#include "../tvfm/sond_tvfm_preflight.h"
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

/* Kopieren und Verschieben: Dateien, Verzeichnisse und Teile in Containern (ZIP, PDF-Anhänge,
 * E-Mail-Teile); keine Abschnitte (Anbindungen) und keine Marker (PageTree, Message). */
static gboolean items_transferable(GPtrArray *items) {
	for (guint i = 0; i < items->len; i++) {
		SondTVFMItem *item = g_ptr_array_index(items, i);

		if (sond_tvfm_item_get_item_type(item) == SOND_TVFM_ITEM_TYPE_LEAF_SECTION ||
				sond_tvfm_item_is_content_root_marker(item))
			return FALSE;
	}

	return items->len > 0;
}

static void copy_or_cut(SondTreeviewFM *self, gboolean cut) {
	GPtrArray *items = selected_top_items(self);

	if (!items_transferable(items)) {
		if (items->len)
			fm_show_error(self, cut ? "Ausschneiden" : "Kopieren",
					"Abschnitte und Platzhalter für Seiten und Nachrichten lassen sich nicht "
					"kopieren oder verschieben.");
		g_ptr_array_unref(items);

		return;
	}

	clipboard_set(self, items, cut);
}

//TRUE, wenn der Eintrag Inhalt eines Containers (ZIP, PDF, E-Mail) ist
static gboolean item_is_in_container(SondTVFMItemPrivate *priv) {
	if (!priv->sond_file_part)
		return FALSE;

	//Verzeichnis im Container trägt den Filepart des Containers selbst
	if (priv->path_or_section)
		return TRUE;

	return sond_file_part_get_parent(priv->sond_file_part) != NULL;
}

/* Der Container, in dem der Eintrag liegt (NULL im Dateisystem), und sein Verzeichnis darin
 * (*dir, g_free(); NULL = Wurzel des Containers) */
static SondFilePart* item_container(SondTVFMItem *item, gchar **dir) {
	SondTVFMItemPrivate *priv = sond_tvfm_item_get_priv(item);
	SondFilePart *container = NULL;
	gchar const *path = NULL;
	gchar *name = NULL;

	*dir = NULL;
	if (!priv->sond_file_part)
		return NULL;

	if (priv->path_or_section) {
		container = priv->sond_file_part;
		path = priv->path_or_section;
	} else {
		container = sond_file_part_get_parent(priv->sond_file_part);
		path = sond_file_part_get_path(priv->sond_file_part);
	}
	if (!container || !path)
		return NULL;

	name = g_path_get_dirname(path);
	if (g_strcmp0(name, "."))
		*dir = name;
	else
		g_free(name);

	return container;
}

//Liegt sfp in sfp_outer oder ist es selbst (über die Eltern-Fileparts)
static gboolean sfp_is_inside(SondFilePart *sfp, SondFilePart *sfp_outer) {
	for (; sfp; sfp = sond_file_part_get_parent(sfp))
		if (sfp == sfp_outer)
			return TRUE;

	return FALSE;
}

/* Fügt item unter parent ein (Kopie oder Verschieben); bei vergebenem Namen wird
 * ein Zähler angehängt. 0 = ok (*base_inserted: endgültiger Name), 1 = übersprungen,
 * -1 = Fehler. */
static gint paste_one(SondTreeviewFM *self, SondTVFMItem *item, SondTVFMItem *parent,
		gboolean cut, gchar **base_inserted, GError **error) {
	SondTreeviewFMClass *klass = SOND_TREEVIEWFM_GET_CLASS(self);
	SondTVFMItemPrivate *item_priv = sond_tvfm_item_get_priv(item);
	SondTVFMItemPrivate *parent_priv = sond_tvfm_item_get_priv(parent);
	g_autofree gchar *base = NULL;
	gchar const *dot = NULL;
	gboolean has_ext = FALSE;
	gint max_tries = 100;

	base = g_strdup(item_priv->display_name ? item_priv->display_name :
			sond_tvfm_item_get_basename(item));
	if (!base)
		base = g_strdup("UNNAMED");

	//Mime-Teil einer E-Mail: der Anzeigename ist der Typ, daraus eine Endung machen
	if (strrchr(base, '/')) {
		gchar const *ext = mime_to_extension(base);
		gchar *name = g_strconcat("UNNAMED", ext, NULL);

		g_free(base);
		base = name;
	}

	//Namen aus Containern dürfen im Dateisystem keine verbotenen Zeichen tragen
	if (!parent_priv->sond_file_part && item_is_in_container(item_priv)) {
		gchar *sanitized = sond_sanitize_filename(base);

		g_free(base);
		base = sanitized;
	}

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

/* Neuer Eintrag für das eingefügte Objekt (Referenz) im Ziel parent mit dem Pfad path_new
 * (Zielverzeichnis plus endgültiger Name; bei Verzeichnissen und im Dateisystem derselbe
 * Pfad wie im Item). */
static SondTVFMItem* pasted_item_new(SondTreeviewFM *self, SondTVFMItem *item,
		SondTVFMItem *parent, gchar const *path_new, gchar const *base_inserted) {
	SondTVFMItemPrivate *item_priv = sond_tvfm_item_get_priv(item);
	SondTVFMItemPrivate *parent_priv = sond_tvfm_item_get_priv(parent);
	SondFilePart *sfp = item_priv->sond_file_part;
	SondFilePart *sfp_new = NULL;
	SondTVFMItem *created = NULL;

	if (sfp && item_priv->path_or_section && SOND_IS_FILE_PART_ZIP(sfp) &&
			parent_priv->sond_file_part == sfp)
		//ZIP-Verzeichnis im selben Archiv: dasselbe Filepart, nur der Pfad ändert sich
		sfp_new = g_object_ref(sfp);
	else if (sfp && !(item_priv->path_or_section && !parent_priv->sond_file_part)) {
		/* Ein Verzeichnis aus einem Container ins Dateisystem wird nicht geklont: sein Filepart
		 * ist der des Containers; das neue Element ist ein gewöhnliches Dateisystem-Verzeichnis
		 * (ohne Filepart), wie es auf der Platte angelegt wurde. */
		sfp_new = g_object_new(G_OBJECT_TYPE(sfp), NULL);

		if (!item_priv->path_or_section) {
			//Adresse eines PDF-Anhangs ist der kodierte Dateiname
			g_autofree gchar *path_sfp = SOND_IS_FILE_PART_PDF(parent_priv->sond_file_part) ?
					pdf_emb_escape(path_new) : g_strdup(path_new);

			sond_file_part_set_path(sfp_new, path_sfp);
		}

		if (SOND_IS_FILE_PART_LEAF(sfp_new))
			sond_file_part_leaf_set_mime_type(SOND_FILE_PART_LEAF(sfp_new),
					sond_file_part_leaf_get_mime_type(SOND_FILE_PART_LEAF(sfp)));
		sond_file_part_set_has_children(sfp_new, sond_file_part_get_has_children(sfp));
		sond_file_part_set_lock_entry(sfp_new, sond_file_part_get_lock_entry(sfp));
		sond_file_part_set_lock_content(sfp_new, sond_file_part_get_lock_content(sfp));
		sond_file_part_set_parent(sfp_new, parent_priv->sond_file_part);
	}

	created = sond_tvfm_item_create(self, sfp_new, item_priv->path_or_section ? path_new : NULL);
	if (sfp_new)
		g_object_unref(sfp_new);

	//PDF-Anhang: angezeigt wird der Dateiname, nicht die kodierte Adresse
	if (!item_priv->path_or_section && SOND_IS_FILE_PART_PDF(parent_priv->sond_file_part)) {
		g_free(sond_tvfm_item_get_priv(created)->display_name);
		sond_tvfm_item_get_priv(created)->display_name = g_strdup(base_inserted);
	}

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

	if (!match && m->old_rel && sond_tvfm_item_fs_path(SOND_TVFM_ITEM(item), &rel, NULL))
		match = !g_strcmp0(rel, m->old_rel);
	g_free(rel);

	return match;
}

//Index im Pfad eines Mimeparts ("2" oder "1/2") um delta verschieben
static gchar* mime_path_shifted(gchar const *path, gint delta) {
	gchar const *slash = strrchr(path, '/');

	if (slash) {
		g_autofree gchar *dir = g_path_get_dirname(path);

		return g_strdup_printf("%s/%d", dir, atoi(slash + 1) + delta);
	}

	return g_strdup_printf("%d", atoi(path) + delta);
}

/* Nach dem Entfernen eines Mimeparts rücken die späteren Geschwister nach: ihre Pfade werden
 * direkt auf den Items um 1 verringert (ab Position from). Das Message-Item selbst (Blatt der
 * ganzen E-Mail) hat keinen Index. */
static void mime_siblings_shift(SondLazyChildren *lazy, guint from) {
	for (guint i = from; lazy->children && i < lazy->children->len; i++) {
		SondTVFMItem *sibling = g_ptr_array_index(lazy->children, i);
		SondTVFMItemPrivate *sp = sond_tvfm_item_get_priv(sibling);
		gchar const *path_old = NULL;
		gchar *path_new = NULL;

		if (!sp->path_or_section && sp->type == SOND_TVFM_ITEM_TYPE_LEAF &&
				SOND_IS_FILE_PART_GMESSAGE(sp->sond_file_part))
			continue;

		//Multipart: Pfad im Item; Mimepart-Blatt: Pfad des Fileparts
		path_old = sp->path_or_section ? sp->path_or_section :
				(sp->sond_file_part ? sond_file_part_get_path(sp->sond_file_part) : NULL);
		if (!path_old)
			continue;

		path_new = mime_path_shifted(path_old, -1);
		if (sp->path_or_section) {
			//geöffnete Fileparts im Verzeichnis nachführen, dann den Pfad im Item ändern
			sond_tvfm_item_adjust_sfps_in_dir(sp->sond_file_part, sp->sond_file_part,
					sp->path_or_section, path_new);
			g_free(sp->path_or_section);
			sp->path_or_section = path_new;
		} else {
			sond_file_part_set_path(sp->sond_file_part, path_new);
			g_free(path_new);
		}
	}
}

/* Nimmt die Quelle eines Verschiebens aus der Anzeige. Liegt sie in einem Container, sind
 * Sonderfälle nachzuführen: war es der letzte Anhang einer PDF, ist sie wieder ein Blatt (ohne
 * PageTree); beim Entfernen eines Mimeparts rücken die späteren Geschwister nach. */
static void remove_moved_source(SondTreeviewFM *self, MovedSource *m) {
	SondTreeview *base = SOND_TREEVIEW(self);
	GtkTreeListRow *row = sond_treeview_find_row(base, match_moved_source, m, NULL);
	SondTVFMItem *shown = NULL;
	SondTVFMItemPrivate *sp = NULL;
	SondFilePart *sfp = NULL;
	SondFilePart *container = NULL;
	SondLazyChildren *lazy = NULL;
	GtkTreeListRow *row_parent = NULL;
	SondTVFMItem *item_parent = NULL;
	gint index = 0;
	gboolean is_emb = FALSE;
	gboolean is_mime = FALSE;

	if (!row)
		return;

	shown = gtk_tree_list_row_get_item(row);
	lazy = fm_lazy_from_model(sond_treeview_row_get_container(base, row));
	row_parent = gtk_tree_list_row_get_parent(row);
	item_parent = row_parent ? gtk_tree_list_row_get_item(row_parent) : NULL;
	g_object_unref(row);
	if (!shown || !lazy) {
		g_clear_object(&shown);
		g_clear_object(&row_parent);
		g_clear_object(&item_parent);

		return;
	}

	sp = sond_tvfm_item_get_priv(shown);
	sfp = sp->sond_file_part;
	if (sfp && !sp->path_or_section) {
		container = sond_file_part_get_parent(sfp);
		is_emb = container && SOND_IS_FILE_PART_PDF(container);
		is_mime = container && SOND_IS_FILE_PART_GMESSAGE(container);
	} else if (sfp && sp->path_or_section && SOND_IS_FILE_PART_GMESSAGE(sfp)) {
		is_mime = TRUE;
		container = sfp;
	}

	index = fm_lazy_index_of(lazy, shown);
	fm_lazy_remove(lazy, shown);

	if (is_mime && index >= 0)
		mime_siblings_shift(lazy, (guint) index);

	//letzter Anhang entfernt: die PDF ist wieder ein Blatt
	if (is_emb && item_parent && !sond_file_part_get_has_children(container)) {
		SondTVFMItem *item_new = sond_tvfm_item_create(self, container, NULL);

		fm_replace_item(self, item_parent, item_new);
		g_object_unref(item_new);
	}

	g_object_unref(shown);
	g_clear_object(&row_parent);
	g_clear_object(&item_parent);
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

/* Ziel in einer nicht geladenen Datei (ZIP, PDF): das Einfügen schreibt in die Datei, sie muss
 * lokal sein */
static gboolean paste_target_preload(SondTreeviewFM *self, SondTVFMItem *parent) {
	SondFilePart *sfp = sond_tvfm_item_get_sond_file_part(parent);
	gchar *path = NULL;
	gboolean ok = TRUE;

	if (!sfp || !sond_tvfm_load_not_loaded(SOND_TVFM_OWNER(self), sfp))
		return TRUE;

	path = sond_tvfm_load_root_file_path(SOND_TVFM_OWNER(self), sfp);
	ok = sond_seadrive_load_file_sync(fm_parent_window(self), path);
	g_free(path);

	return ok;
}

//Ziel gewählt: PDF ohne Anhänge (Blatt), die dadurch zum Container wird
static gboolean target_is_plain_pdf(SondTVFMItemPrivate *priv) {
	return priv->sond_file_part && !priv->path_or_section &&
			priv->type == SOND_TVFM_ITEM_TYPE_LEAF &&
			SOND_IS_FILE_PART_PDF(priv->sond_file_part) &&
			!sond_file_part_get_has_children(priv->sond_file_part);
}

//Meldung über übersprungene (passwortgeschützte) Einträge beim Kopieren
static void report_skipped(SondTreeviewFM *self) {
	GPtrArray *skipped = sond_password_action_skipped();

	if (skipped->len > 0) {
		GString *text = g_string_new(NULL);

		g_string_append_printf(text, "%u passwortgeschützte%s übersprungen:\n", skipped->len,
				skipped->len == 1 ? "r Eintrag" : " Einträge");
		for (guint i = 0; i < MIN(skipped->len, 5); i++)
			g_string_append_printf(text, "\n%s", (gchar const*) g_ptr_array_index(skipped, i));
		if (skipped->len > 5)
			g_string_append_printf(text, "\n... und %u weitere", skipped->len - 5);
		fm_show_error(self, "Kopieren", text->str);
		g_string_free(text, TRUE);
	}
	g_ptr_array_unref(skipped);
}

/* Fügt items in das Ziel ein, das zur Zeile row gehört: deren Inhalt (child) oder das Verzeichnis
 * bzw. der Container, in dem sie steht. Ziele sind Verzeichnisse des Dateisystems, ZIP-Archive und
 * Verzeichnisse darin sowie PDF (Anhänge). cut = verschieben, sonst kopieren. */
static void paste_items(SondTreeviewFM *self, GPtrArray *items, gboolean cut,
		GtkTreeListRow *row, gboolean child) {
	SondTreeview *base = SOND_TREEVIEW(self);
	SondTreeviewFMClass *klass = SOND_TREEVIEWFM_GET_CLASS(self);
	SondTVFMItem *parent = NULL;
	SondTVFMItemPrivate *parent_priv = NULL;
	SondTVFMItem *anchor = NULL;
	gchar const *parent_rel = NULL;
	GPtrArray *created = g_ptr_array_new_with_free_func(g_object_unref);
	GPtrArray *moved = g_ptr_array_new_with_free_func(moved_source_free);
	GPtrArray *failed = g_ptr_array_new_with_free_func(g_free);
	GError *error = NULL;
	gboolean eingang_changed = FALSE;
	gboolean action_open = FALSE;
	gboolean plain_pdf = FALSE;

	parent = child ? gtk_tree_list_row_get_item(row) : fm_parent_item_of_row(self, row);
	if (!parent) {
		fm_show_error(self, "Einfügen", "Das Ziel ist nicht mehr vorhanden.");
		goto out;
	}
	parent_priv = sond_tvfm_item_get_priv(parent);
	plain_pdf = target_is_plain_pdf(parent_priv) && child;

	if (parent_priv->sond_file_part && SOND_IS_FILE_PART_GMESSAGE(parent_priv->sond_file_part)) {
		fm_show_error(self, "Einfügen", "Einfügen in E-Mails wird noch nicht unterstützt.");
		goto out;
	}
	if (!plain_pdf && (sond_tvfm_item_get_item_type(parent) != SOND_TVFM_ITEM_TYPE_DIR ||
			(parent_priv->sond_file_part &&
			!SOND_IS_FILE_PART_ZIP(parent_priv->sond_file_part) &&
			!SOND_IS_FILE_PART_PDF(parent_priv->sond_file_part)))) {
		fm_show_error(self, "Einfügen",
				"In diesen Eintrag lässt sich nichts einfügen (nur in Verzeichnisse, ZIP-Archive "
				"und PDF-Dateien).");
		goto out;
	}
	parent_rel = parent_priv->path_or_section;
	if (!child)
		anchor = gtk_tree_list_row_get_item(row);

	if (!paste_target_preload(self, parent) || (!cut && !paste_preload(self, items)))
		goto out;

	//Passwörter gesperrter Einträge vorab einholen: Abbruch oder Fehler: nichts kopieren oder
	//verschieben (beim Kopieren dürfen Einträge übersprungen werden)
	sond_password_action_begin(!cut);
	action_open = TRUE;
	{
		SondTVFMPreflightResult pf = { FALSE, NULL };

		if (!sond_tvfm_preflight(items, parent, cut, &pf)) {
			if (pf.failure)
				g_set_error(&error, G_IO_ERROR, G_IO_ERROR_FAILED,
						"Es wurde nichts %s:\n%s", cut ? "verschoben" : "kopiert", pf.failure);
			g_free(pf.failure);

			goto out;
		}
	}

	for (guint i = 0; i < items->len; i++) {
		SondTVFMItem *item = g_ptr_array_index(items, i);
		SondTVFMItemPrivate *item_priv = sond_tvfm_item_get_priv(item);
		g_autofree gchar *fs_old = NULL;
		g_autofree gchar *path_old = NULL;
		g_autofree gchar *base_inserted = NULL;
		g_autofree gchar *path_new = NULL;
		g_autofree gchar *src_dir = NULL;
		SondFilePart *src_container = NULL;
		gboolean fs_known = FALSE;
		gboolean is_dir = FALSE;
		gint rc = 0;

		//wegen fehlenden Passworts übersprungen (Vorab-Phase beim Kopieren)
		if (!item_priv->path_or_section && item_priv->sond_file_part) {
			g_autofree gchar *filepart = sond_file_part_get_filepart(item_priv->sond_file_part);

			if (sond_password_action_is_skipped(filepart))
				continue;
		}

		fs_known = sond_tvfm_item_fs_path(item, &fs_old, &is_dir);
		if (!fs_known)
			is_dir = sond_tvfm_item_get_item_type(item) == SOND_TVFM_ITEM_TYPE_DIR;
		src_container = item_container(item, &src_dir);

		//ein Verzeichnis oder Container nicht in sich selbst
		if (is_dir) {
			gboolean inside = FALSE;

			if (fs_known)
				inside = parent_rel && !parent_priv->sond_file_part &&
						(!g_strcmp0(parent_rel, fs_old) ||
						(g_str_has_prefix(parent_rel, fs_old) &&
						parent_rel[strlen(fs_old)] == '/'));
			else if (item_priv->path_or_section)
				inside = parent_priv->sond_file_part == item_priv->sond_file_part &&
						parent_rel && (!g_strcmp0(parent_rel, item_priv->path_or_section) ||
						(g_str_has_prefix(parent_rel, item_priv->path_or_section) &&
						parent_rel[strlen(item_priv->path_or_section)] == '/'));
			else if (item_priv->sond_file_part)
				inside = sfp_is_inside(parent_priv->sond_file_part, item_priv->sond_file_part);
			if (inside) {
				g_set_error(&error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
						"\"%s\" lässt sich nicht in sich selbst einfügen",
						item_priv->display_name ? item_priv->display_name : "?");
				break;
			}
		}

		//Verschieben in das eigene Verzeichnis (bzw. denselben Container): nichts zu tun
		if (cut) {
			gboolean same = FALSE;

			if (!src_container && !parent_priv->sond_file_part && fs_known) {
				g_autofree gchar *dir = g_path_get_dirname(fs_old);

				same = !g_strcmp0(g_strcmp0(dir, ".") ? dir : "", parent_rel ? parent_rel : "");
			} else if (src_container && src_container == parent_priv->sond_file_part)
				same = !g_strcmp0(src_dir ? src_dir : "", parent_rel ? parent_rel : "");
			if (same)
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

		//Zusatzangaben (Eingangsstempel) nachführen: Quelle ein Dateisystem-Eintrag; Ziel ein
		//Dateisystem-Ordner (Pfad = Zielordner + endgültiger Name) oder ein Container
		if (fs_known && klass->fs_entry_moved &&
				klass->fs_entry_moved(self, fs_old,
						parent_priv->sond_file_part ? NULL : path_new, is_dir, !cut))
			eingang_changed = TRUE;

		//Pfade bereits geöffneter Dateien im verschobenen Verzeichnis nachführen
		if (cut && path_old)
			sond_tvfm_item_adjust_sfps_in_dir(item_priv->sond_file_part,
					parent_priv->sond_file_part, path_old, path_new);

		g_ptr_array_add(created, pasted_item_new(self, item, parent, path_new, base_inserted));

		if (cut) {
			MovedSource *m = g_new0(MovedSource, 1);

			m->item = item;
			m->old_rel = fs_known ? g_strdup(fs_old) : NULL;
			g_ptr_array_add(moved, m);
		}
	}

	//Anzeige: neue Einträge in die Liste des Ziels, am Ort des Einfügens
	if (created->len) {
		if (plain_pdf) {
			//PDF ohne bisherige Anhänge ist jetzt ein Container: Zeile aus der Datei neu aufbauen
			//(PageTree, Sections, Anhänge), aufklappen und den Anhang auswählen
			SondTVFMItem *item_new = sond_tvfm_item_create(self, parent_priv->sond_file_part, NULL);
			GtkTreeListRow *row_new = NULL;

			fm_replace_item(self, parent, item_new);
			row_new = sond_treeview_find_row(base, fm_match_same, item_new, NULL);
			if (row_new) {
				gtk_tree_list_row_set_expanded(row_new, TRUE);
				g_object_unref(row_new);
			}
			g_object_unref(item_new);
		} else if (child) {
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
					//in einer PDF stehen die Anhänge hinter dem PageTree (erstes Kind)
					guint index = (parent_priv->sond_file_part &&
							SOND_IS_FILE_PART_PDF(parent_priv->sond_file_part) &&
							!parent_priv->path_or_section) ? 1 : 0;

					for (guint i = 0; lazy && i < created->len; i++)
						fm_lazy_insert_at(lazy, (gint) (index + i), g_ptr_array_index(created, i));
				} else
					gtk_tree_list_row_set_expanded(parent_row, TRUE); //liest von der Platte
				g_object_unref(parent_row);
			}
		} else {
			SondLazyChildren *lazy = fm_lazy_from_model(
					sond_treeview_row_get_container(base, row));
			gint index = lazy && anchor ? fm_lazy_index_of(lazy, anchor) + 1 : -1;

			for (guint i = 0; lazy && i < created->len; i++)
				fm_lazy_insert_at(lazy, index < 0 ? -1 : (gint) (index + i),
						g_ptr_array_index(created, i));
		}
		sond_treeview_select_item(base, g_ptr_array_index(created, created->len - 1));
	}

	//Quellen beim Verschieben aus der Anzeige nehmen
	for (guint i = 0; i < moved->len; i++)
		remove_moved_source(self, g_ptr_array_index(moved, i));

	if (eingang_changed)
		sond_treeviewfm_refresh_status(self);

out:
	if (action_open) {
		if (!cut)
			report_skipped(self);
		sond_password_action_end();
	}

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
	g_clear_object(&anchor);
	g_ptr_array_unref(created);
	g_ptr_array_unref(moved);
	g_ptr_array_unref(failed);
}

//Zeile als Ziel "hinein": jedes Verzeichnis, auch ZIP-Archive und PDF mit Anhängen
static gboolean row_is_container_target(SondTVFMItem *selected) {
	return selected && sond_tvfm_item_get_item_type(selected) == SOND_TVFM_ITEM_TYPE_DIR &&
			!sond_tvfm_item_is_content_root_marker(selected);
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

			child = row_is_container_target(selected);
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

	if (!items_transferable(items)) {
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
	job->child = row_is_container_target(item);
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

