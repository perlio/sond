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

#include <glib.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "misc.h"
#include "misc_stdlib.h"
#include "sond_fileparts.h"

//SOND_TVFM_ITEM

typedef struct {
	SondTreeviewFM* stvfm;
	gchar *icon_name;
	gchar* display_name;
	gboolean has_children;
	SondTVFMItemType type;
	SondFilePart* sond_file_part;
	gchar* path_or_section;
} SondTVFMItemPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondTVFMItem, sond_tvfm_item, G_TYPE_OBJECT)

static void sond_tvfm_item_finalize(GObject *self) {
	SondTVFMItemPrivate *sond_tvfm_item_priv =
			sond_tvfm_item_get_instance_private(SOND_TVFM_ITEM(self));

	g_free(sond_tvfm_item_priv->icon_name);
	g_free(sond_tvfm_item_priv->display_name);

	//sfp-Type oder root
	if (sond_tvfm_item_priv->sond_file_part)
		g_object_unref(sond_tvfm_item_priv->sond_file_part);

	if (sond_tvfm_item_priv->path_or_section) g_free(sond_tvfm_item_priv->path_or_section);

	G_OBJECT_CLASS(sond_tvfm_item_parent_class)->finalize(self);

	return;
}

static void sond_tvfm_item_class_init(SondTVFMItemClass *klass) {
	G_OBJECT_CLASS(klass)->finalize = sond_tvfm_item_finalize;

	klass->load_sections = NULL;

	return;
}

static void sond_tvfm_item_init(SondTVFMItem *self) {

	return;
}

SondTVFMItemType sond_tvfm_item_get_item_type(SondTVFMItem *stvfm_item) {
	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);
	return stvfm_item_priv->type;
}

gchar const* sond_tvfm_item_get_path_or_section(SondTVFMItem *stvfm_item) {
	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	return stvfm_item_priv->path_or_section;
}

SondFilePart* sond_tvfm_item_get_sond_file_part(SondTVFMItem *stvfm_item) {
	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	return stvfm_item_priv->sond_file_part;
}

SondTreeviewFM* sond_tvfm_item_get_stvfm(SondTVFMItem *stvfm_item) {
	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	return stvfm_item_priv->stvfm;
}

static gchar const* sond_tvfm_item_get_icon_name(SondTVFMItem* stvfm_item) {
	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	return stvfm_item_priv->icon_name;
}

static gchar const* sond_tvfm_item_get_display_name(SondTVFMItem* stvfm_item) {
	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	return stvfm_item_priv->display_name;
}


gboolean sond_tvfm_item_get_has_children(SondTVFMItem *stvfm_item) {
	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	return stvfm_item_priv->has_children;
}

void sond_tvfm_item_set_has_children(SondTVFMItem *stvfm_item,
		gboolean has_children) {
	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	stvfm_item_priv->has_children = has_children;
}

static gint sond_tvfm_item_load_fs_dir(SondTVFMItem*, gboolean, GPtrArray**, GError**);

SondTVFMItem* sond_tvfm_item_create(SondTreeviewFM* stvfm, SondTVFMItemType type,
		SondFilePart *sond_file_part, gchar const* path_or_section) {
	SondTVFMItem *stvfm_item = NULL;
	SondTVFMItemPrivate *stvfm_item_priv = NULL;

	stvfm_item = g_object_new(SOND_TYPE_TVFM_ITEM, NULL);
	stvfm_item_priv = sond_tvfm_item_get_instance_private(stvfm_item);

	stvfm_item_priv->stvfm = stvfm;
	stvfm_item_priv->type = type;
	stvfm_item_priv->path_or_section = g_strdup(path_or_section);
	if (sond_file_part)
		stvfm_item_priv->sond_file_part = g_object_ref(sond_file_part);

	//Wenn sond_file_part != NULL dann ist es ein FilePart nicht im Dateisystem
	if (type == SOND_TVFM_ITEM_TYPE_LEAF) {
		gchar const* content_type = NULL;

		if (SOND_IS_FILE_PART_PDF(sond_file_part))
			content_type = "application/pdf";
		else
			content_type = sond_file_part_leaf_get_content_type(SOND_FILE_PART_LEAF(sond_file_part));

		stvfm_item_priv->icon_name = g_content_type_get_generic_icon_name(content_type);

		stvfm_item_priv->display_name = g_path_get_basename(
				sond_file_part_get_path(sond_file_part));
		if (SOND_TREEVIEWFM_GET_CLASS(stvfm)->has_sections)
			stvfm_item_priv->has_children =
					SOND_TREEVIEWFM_GET_CLASS(stvfm)->has_sections(stvfm_item);
	}
	else if (type == SOND_TVFM_ITEM_TYPE_LEAF_SECTION) {
		stvfm_item_priv->icon_name = g_strdup("anbindung");
		stvfm_item_priv->display_name = g_strdup(stvfm_item_priv->path_or_section);
		if (SOND_TREEVIEWFM_GET_CLASS(stvfm)->has_sections)
			stvfm_item_priv->has_children =
					SOND_TREEVIEWFM_GET_CLASS(stvfm)->has_sections(stvfm_item);
	}
	else if (type == SOND_TVFM_ITEM_TYPE_DIR) {
		if (!sond_file_part) { //kein SondFilePart, dann ist es ein Verzeichnis im Dateisystem
			gint rc = 0;
			GError* error = NULL;
			gchar* basename = NULL;

			if (path_or_section) basename = g_path_get_basename(path_or_section);

			rc = sond_tvfm_item_load_fs_dir(stvfm_item, FALSE, NULL, &error);
			if (rc == -1) {
				gchar* text = NULL;

				text = g_strdup_printf("'%s' konnte nicht geöffnet werden: %s",
						basename, error->message);
				g_error_free(error);
				g_free(basename);

				stvfm_item_priv->display_name = text;
			}
			else {
				stvfm_item_priv->display_name = basename;

				if (rc == 1) stvfm_item_priv->has_children = TRUE;
			}

			stvfm_item_priv->icon_name = g_strdup("folder");
		}
		else if (SOND_IS_FILE_PART_ZIP(sond_file_part)) {
			if (!path_or_section) {
				stvfm_item_priv->icon_name = g_strdup("zip");

				stvfm_item_priv->display_name =
						g_path_get_basename(sond_file_part_get_path(sond_file_part));
			}
			else {
				stvfm_item_priv->icon_name = g_strdup("folder");
				stvfm_item_priv->display_name =
						g_path_get_basename(stvfm_item_priv->path_or_section);
			}

			stvfm_item_priv->has_children = sond_file_part_has_children(sond_file_part);
		}
		else if (SOND_IS_FILE_PART_PDF(sond_file_part)) {
			stvfm_item_priv->icon_name = g_strdup("folder-templates");
			stvfm_item_priv->display_name =
					g_path_get_basename(sond_file_part_get_path(sond_file_part));

			stvfm_item_priv->has_children = TRUE; //sonst wären wir nicht hier
		}
		//else if (SOND_IS_FILE_PART_GMESSAGE(sond_file_part)) {

	}

	return stvfm_item;
}

static SondTVFMItem* sond_tvfm_item_create_from_content_type(SondTVFMItem* stvfm_item,
		gchar const* content_type, gchar const* rel_path_child, GError** error) {
	SondTVFMItem* stvfm_item_child = NULL;
	SondTVFMItemPrivate* stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	if (g_content_type_is_mime_type(content_type, "inode/directory"))
		stvfm_item_child = sond_tvfm_item_create(stvfm_item_priv->stvfm,
				SOND_TVFM_ITEM_TYPE_DIR, stvfm_item_priv->sond_file_part, rel_path_child);
	else {
		SondFilePart* sfp = NULL;

		sfp = sond_file_part_create_from_content_type(rel_path_child,
				stvfm_item_priv->sond_file_part, content_type);

		if (sond_file_part_has_children(sfp))
			stvfm_item_child = sond_tvfm_item_create(stvfm_item_priv->stvfm,
					SOND_TVFM_ITEM_TYPE_DIR, sfp, NULL);
		else
			stvfm_item_child = sond_tvfm_item_create(stvfm_item_priv->stvfm,
					SOND_TVFM_ITEM_TYPE_LEAF, sfp, NULL);
	}

	return stvfm_item_child;
}

static gint sond_tvfm_item_load_fs_dir(SondTVFMItem* stvfm_item,
		gboolean load, GPtrArray** arr_children, GError **error) {
	GPtrArray* loaded_children = NULL;
	GFile* file_dir = NULL;
	GFileEnumerator *enumer = NULL;
	gboolean dir_has_children = FALSE;
	gchar* path_dir = NULL;

	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	path_dir = g_strconcat(SOND_FILE_PART_CLASS(g_type_class_peek_static(SOND_TYPE_FILE_PART))->path_root,
			"/", stvfm_item_priv->path_or_section, NULL);

	file_dir = g_file_new_for_path(path_dir);
	g_free(path_dir);

	enumer = g_file_enumerate_children(file_dir, "*", G_FILE_QUERY_INFO_NONE, NULL,
			error);
	g_object_unref(file_dir);
	if (!enumer)
		ERROR_Z

	if (load)
		loaded_children = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);

	while (1) {
		GFileInfo *info_child = NULL;
		gboolean res = FALSE;
		gchar const* content_type = NULL;
		SondTVFMItem* stvfm_item_child = NULL;
		gchar* rel_path_child = NULL;

		res = g_file_enumerator_iterate(enumer, &info_child, NULL, NULL, error);
		if (!res) {
			g_object_unref(enumer);
			if (load) g_ptr_array_unref(loaded_children);
			ERROR_Z
		}

		if (!info_child) //keine weiteren Dateien
			break;
		else dir_has_children = TRUE;

		if (!load)
			break; //nur prüfen, ob Kinder vorhanden

		if (stvfm_item_priv->path_or_section)
			rel_path_child = g_strconcat(stvfm_item_priv->path_or_section, "/",
					g_file_info_get_name(info_child), NULL);
		else rel_path_child = g_strdup(g_file_info_get_name(info_child));

		content_type = g_file_info_get_content_type(info_child);

		stvfm_item_child =
				sond_tvfm_item_create_from_content_type(stvfm_item, content_type, rel_path_child, error);
		g_free(rel_path_child);
		if (!stvfm_item_child) {
			g_ptr_array_unref(loaded_children); //Test nicht nötig - wenn !load, kommen wir nicht hier hin
			ERROR_Z
		}

		g_ptr_array_add(loaded_children, stvfm_item_child);
	}

	g_object_unref(enumer); //unreferenziert auch alle infos und gfiles

	if (load) *arr_children = loaded_children;
	else if (dir_has_children) return 1;

	return 0;
}

static gint sond_tvfm_item_load_zip_dir(SondTVFMItem* stvfm_item, gboolean load,
		GPtrArray** arr_children, GError** error) {

	return 0;
}

static gint sond_tvfm_item_load_pdf_dir(SondTVFMItem* stvfm_item, GPtrArray** arr_children,
		GError** error) {
	gint rc = 0;
	GPtrArray* arr_emb_files = NULL;
	SondTVFMItemPrivate* stvfm_item_priv = NULL;
	SondTVFMItem* stvfm_item_pdf_page_tree = NULL;

	stvfm_item_priv = sond_tvfm_item_get_instance_private(stvfm_item);

	rc = sond_file_part_pdf_load_embedded_files(SOND_FILE_PART_PDF(stvfm_item_priv->sond_file_part),
			&arr_emb_files, error);
	if (rc)
		ERROR_Z

	*arr_children = g_ptr_array_new_with_free_func((GDestroyNotify) g_object_unref);

	stvfm_item_pdf_page_tree = sond_tvfm_item_create(stvfm_item_priv->stvfm,
			SOND_TVFM_ITEM_TYPE_LEAF, stvfm_item_priv->sond_file_part, NULL);
	g_ptr_array_add(*arr_children, stvfm_item_pdf_page_tree);

	for (guint i = 0; i < arr_emb_files->len; i++) {
		SondFilePart* sfp = NULL;
		SondTVFMItem* stvfm_item_child = NULL;

		sfp = g_ptr_array_index(arr_emb_files, i);

		stvfm_item_child = sond_tvfm_item_create(stvfm_item_priv->stvfm,
				SOND_TVFM_ITEM_TYPE_LEAF, sfp, NULL);

		g_ptr_array_add(*arr_children, stvfm_item_child);
	}

	return 0;
}

gint sond_tvfm_item_load_children(SondTVFMItem* stvfm_item,
		GPtrArray** arr_children, GError** error) {
	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_DIR) {
		gint rc = 0;

		//untergliedern: dir in FileSystem, zip-Archiv oder GMessage
		if (stvfm_item_priv->sond_file_part == NULL) //FileSystem
			rc = sond_tvfm_item_load_fs_dir(stvfm_item, TRUE, arr_children, error);
		else if(SOND_IS_FILE_PART_ZIP(stvfm_item_priv->sond_file_part))
			rc = sond_tvfm_item_load_zip_dir(stvfm_item, TRUE, arr_children, error);
		else if (SOND_IS_FILE_PART_PDF(stvfm_item_priv->sond_file_part))
			rc = sond_tvfm_item_load_pdf_dir(stvfm_item, arr_children, error);
		//else if(SIND_IS_FILE_PART_GMESSAGE(stvfm_item_priv->sond_file_part))

		if (rc)
			ERROR_Z
	}
	else if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF ||
			stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF_SECTION) {
		if (SOND_TREEVIEWFM_GET_CLASS(stvfm_item_priv->stvfm)->load_sections) {
			gint rc = 0;

			rc = SOND_TREEVIEWFM_GET_CLASS(stvfm_item_priv->stvfm)->load_sections(stvfm_item,
					arr_children, error);
			if (rc) ERROR_Z
		}
	}

	return 0;
}

//Nun geht's mit SondTreeviewFM weiter
typedef struct {
	gchar *root;
	GtkTreeViewColumn *column_eingang;
} SondTreeviewFMPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondTreeviewFM, sond_treeviewfm, SOND_TYPE_TREEVIEW)

static gboolean sond_treeviewfm_other_fm(SondTreeviewFM *stvfm) {
	Clipboard *clipboard = NULL;

	clipboard =
			((SondTreeviewClass*) g_type_class_peek_static(SOND_TYPE_TREEVIEW))->clipboard;

	if (clipboard->tree_view != SOND_TREEVIEW(stvfm)) return FALSE;

	return TRUE;
}

static gint sond_treeviewfm_dbase(SondTreeviewFM *stvfm, gint mode,
		const gchar *rel_path_source, const gchar *rel_path_dest,
		GError **error) {
/*
	gint rc = 0;
	Clipboard *clipboard = NULL;

	clipboard =
			((SondTreeviewClass*) g_type_class_peek( SOND_TYPE_TREEVIEW))->clipboard;

	if (mode == 2 && sond_treeviewfm_other_fm(stvfm)) {
		rc = SOND_TREEVIEWFM_GET_CLASS(stvfm)->test_item(
				SOND_TREEVIEWFM(clipboard->tree_view), rel_path_source, error);
		if (rc) //aufräumen...
		{
			if (rc == -1)
				ERROR_Z
			else if (rc == 1)
				return 1;
		}
	}

	if (mode == 4) {
		gint rc = 0;

		rc = SOND_TREEVIEWFM_GET_CLASS(stvfm)->test_item(stvfm, rel_path_dest,
				error);
		if (rc) {
			if (rc == -1)
				ERROR_Z
			else if (rc == 1)
				return 1;
		}
	}

	rc = SOND_TREEVIEWFM_GET_CLASS(stvfm)->dbase_begin(stvfm, error);
	if (rc)
		ERROR_Z

	if (mode == 2 || mode == 3) //mode == 2: beyond wurde schon ausgeschlossen - mode == 3: ausgeschlossen
			{
		rc = SOND_TREEVIEWFM_GET_CLASS(stvfm)->dbase_update_path(stvfm,
				rel_path_source, rel_path_dest, error);
		if (rc)
			ERROR_Z
	}
*/
	return 0;
}

/** mode:
 0 - insert dir
 1 - copy file
 2 - move file oder dir
 3 - row edited
 4 - delete
 Rückgabe:
 -1 - Fehler - *errmsg wird "gefüllt"
 0 - Aktion erfolgreich abgeschlossen
 1 - keine Veränderung am Filesystem
 2 - keine Veränderung am Filesystem - Abbruch gewählt
 **/
static gint sond_treeviewfm_move_copy_create_delete(SondTreeviewFM *stvfm,
		GFile *file_source, GFile **file_dest, gint mode, GError **error) {
	gint zaehler = 0;
	gchar *basename = NULL;

	basename = g_file_get_basename(*file_dest);

	while (1) {
		gboolean suc = FALSE;

		if (mode == 0)
			suc = g_file_make_directory(*file_dest, NULL, error);
		else //if ( mode == 1 || mode == 2 || mode == 3 || mode == 4 )
		{
			gint rc = 0;
			Clipboard *clipboard = NULL;
			gchar *rel_path_source = NULL;
			gchar *rel_path_dest = NULL;

			clipboard = ((SondTreeviewClass*) g_type_class_peek(
					SOND_TYPE_TREEVIEW))->clipboard;

			if (mode == 1 || mode == 2)
				rel_path_source = get_rel_path_from_file(
						sond_treeviewfm_get_root(
								SOND_TREEVIEWFM(clipboard->tree_view)),
						file_source);
			else if (mode == 3)
				rel_path_source = get_rel_path_from_file(
						sond_treeviewfm_get_root(stvfm), file_source);
			rel_path_dest = get_rel_path_from_file(
					sond_treeviewfm_get_root(stvfm), *file_dest);

			rc = sond_treeviewfm_dbase(stvfm, mode, rel_path_source,
					rel_path_dest, error);

			g_free(rel_path_dest);
			g_free(rel_path_source);

			if (rc == -1) {
				g_free(basename);
				ERROR_Z
			} else if (rc == 1)
				return 1;

			if (mode == 1)
				suc = g_file_copy(file_source, *file_dest, G_FILE_COPY_NONE,
						NULL, NULL, NULL, error);
			else if (mode == 2 || mode == 3)
				suc = g_file_move(file_source, *file_dest, G_FILE_COPY_NONE,
						NULL, NULL, NULL, error);
			else if (mode == 4)
				suc = g_file_delete(*file_dest, NULL, error);

		}

		if (suc)
			break;
		else {
			if (g_error_matches(*error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
				GFile *file_parent = NULL;

				g_clear_error(error);
				if (mode == 3) //Filename editiert
						{
					g_free(basename);
					return 1; //nichts geändert!
				}

				gchar *basename_new_try = NULL;
				gchar *zusatz = NULL;

				if (mode == 1 && zaehler == 0)
					basename_new_try = g_strconcat(basename, " - Kopie", NULL);
				else if (mode == 1 && zaehler > 0) {
					zusatz = g_strdup_printf(" (%i)", zaehler + 1);
					basename_new_try = g_strconcat(basename, "- Kopie", zusatz,
							NULL);
				} else if (mode == 2 || mode == 0) {
					zusatz = g_strdup_printf(" (%i)", zaehler + 2);
					basename_new_try = g_strconcat(basename, zusatz, NULL);
				}

				g_free(zusatz);

				zaehler++;

				file_parent = g_file_get_parent(*file_dest);
				g_object_unref(*file_dest);

				*file_dest = g_file_get_child(file_parent, basename_new_try);
				g_object_unref(file_parent);
				g_free(basename_new_try);

				continue;
			} else if (g_error_matches(*error, G_IO_ERROR,
					G_IO_ERROR_PERMISSION_DENIED)) {
				g_clear_error(error);

				gint res = dialog_with_buttons(
						gtk_widget_get_toplevel(GTK_WIDGET(stvfm)),
						"Zugriff nicht erlaubt",
						"Datei möglicherweise geöffnet", NULL,
						"Erneut versuchen", 1, "Überspringen", 2, "Abbrechen",
						3, NULL);

				if (res == 1)
					continue; //Namensgleichheit - wird oben behandelt
				else if (res == 2) {
					g_free(basename);
					return 1; //Überspringen
				} else if (res == 3) {
					g_free(basename);
					return 2;
				}
			} else {
				g_free(basename);
				ERROR_Z
			}
		}
	}

	g_free(basename);

	return 0;
}

static gint sond_treeviewfm_text_edited(SondTreeviewFM *stvfm,
		GtkTreeIter *iter, SondTVFMItem *stvfm_item, const gchar *new_text,
		GError **error) {
	SondTVFMItemPrivate* stvfm_item_priv = sond_tvfm_item_get_instance_private(stvfm_item);

	if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_DIR) {

	}
	else if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF) {

	}
	//SOND_TVFM_ITEM_TYPE_LEAF_SECTION muß in abgeleitetem TreeviewFM bearbeitet werden!

	return 0;
}

static void sond_treeviewfm_cell_edited(GtkCellRenderer *cell,
		gchar *path_string, gchar *new_text, gpointer data) {
	GtkTreeModel *model = NULL;
	GtkTreeIter iter = { 0 };
	SondTVFMItem *stvfm_item = NULL;
	GError *error = NULL;
	gint rc = 0;
	gchar const* old_text = NULL;

	SondTreeviewFM *stvfm = (SondTreeviewFM*) data;
	SondTVFMItemPrivate *stvfm_item_priv = NULL;
	model = gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm));
	gtk_tree_model_get_iter_from_string(
			gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)), &iter, path_string);

	gtk_tree_model_get(model, &iter, 0, &stvfm_item, -1);

	stvfm_item_priv = sond_tvfm_item_get_instance_private(
			stvfm_item);

	old_text = stvfm_item_priv->display_name;

	if (!g_strcmp0(old_text, new_text)) {
		g_object_unref(stvfm_item);
		return;
	}

	rc = SOND_TREEVIEWFM_GET_CLASS(stvfm)->text_edited(stvfm, &iter, stvfm_item,
			new_text, &error);
	g_object_unref(stvfm_item);
	if (rc) {
		display_message(gtk_widget_get_toplevel(GTK_WIDGET(stvfm)),
				"Umbenennen nicht möglich\n\n", error->message, NULL);
		g_error_free(error);
	}

	g_free(stvfm_item_priv->display_name);
	stvfm_item_priv->display_name = g_strdup(new_text);

	return;
}

static void sond_treeviewfm_row_collapsed(GtkTreeView *tree_view,
		GtkTreeIter *iter, GtkTreePath *path, gpointer data) {
	GtkTreeIter iter_child;
	gboolean not_empty = TRUE;

	gtk_tree_model_iter_children(gtk_tree_view_get_model(tree_view),
			&iter_child, iter);

	do {
		not_empty = gtk_tree_store_remove(
				GTK_TREE_STORE(gtk_tree_view_get_model(tree_view)),
				&iter_child);
	} while (not_empty);

	//dummy einfügen, dir ist ja nicht leer
	gtk_tree_store_insert(GTK_TREE_STORE(gtk_tree_view_get_model(tree_view)),
			&iter_child, iter, -1);

	gtk_tree_view_columns_autosize(tree_view);

	return;
}

static const gchar*
sond_treeviewfm_get_name(SondTreeviewFM *stvfm, GtkTreeIter *iter) {
	GFileInfo *info = NULL;
	const gchar *name = NULL;

	gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)), iter, 0,
			&info, -1);

	if (!info)
		return NULL;

	name = g_file_info_get_name(info);
	g_object_unref(info);

	return name;
}

/** rc == -1: Fähler
 rc == 0: alles ausgeführt, sämtliche Callbacks haben 0 zurückgegeben
 rc == 1: alles ausgeführt, mindestens ein Callback hat 1 zurückgegeben
 rc == 2: nicht alles ausgeführt, Callback hat 2 zurückgegeben -> sofortiger Abbruch
 **/
static gint sond_treeviewfm_dir_foreach(SondTreeviewFM *stvfm,
		GtkTreeIter *iter_dir, GFile *file, gboolean rec,
		gint (*foreach)(SondTreeviewFM*, GtkTreeIter*, GFile*, GFileInfo*,
				GtkTreeIter*, gpointer, GError**), gpointer data, GError **error) {
	gboolean flag = FALSE;
	GFileEnumerator *enumer = NULL;

	SondTreeviewFMPrivate *stvfm_priv = sond_treeviewfm_get_instance_private(
			stvfm);

	enumer = g_file_enumerate_children(file, "*", G_FILE_QUERY_INFO_NONE, NULL,
			error);
	if (!enumer)
		ERROR_Z

	while (1) {
		GFile *file_child = NULL;
		GFileInfo *info_child = NULL;

		if (!g_file_enumerator_iterate(enumer, &info_child, &file_child, NULL,
				error)) {
			g_object_unref(enumer);

			ERROR_Z
		}

		if (file_child) //es gibt noch Datei in Verzeichnis
		{
			gint rc = 0;
			GtkTreeIter iter_file = { 0 };
			gboolean found = FALSE;
			gboolean root = FALSE;

			if (!iter_dir) {
				GFile *file_root = NULL;

				file_root = g_file_new_for_path(stvfm_priv->root);
				if (g_file_equal(file, file_root))
					root = TRUE;
				g_object_unref(file_root);
			}

			if ((iter_dir || root)
					&& gtk_tree_model_iter_children(
							gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)),
							&iter_file, iter_dir)) {
				do {
					//den Namen des aktuellen Kindes holen
					if (!g_strcmp0(sond_treeviewfm_get_name(stvfm, &iter_file),
							g_file_info_get_name(info_child)))
						found = TRUE; //paßt?

					if (found)
						break;
				} while (gtk_tree_model_iter_next(
						gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)),
						&iter_file));
			}

			rc = foreach(stvfm, iter_dir, file_child, info_child,
					(found) ? &iter_file : NULL, data, error);
			if (rc == -1) {
				g_object_unref(enumer);
				ERROR_Z
			} else if (rc == 1)
				flag = TRUE;
			else if (rc == 2) {//Abbruch gewählt
				g_object_unref(enumer);
				return 2;
			}

			if (rec
					&& g_file_info_get_file_type(info_child)
							== G_FILE_TYPE_DIRECTORY) {
				gint rc = 0;

				rc = sond_treeviewfm_dir_foreach(stvfm,
						(found) ? &iter_file : NULL, file_child, TRUE, foreach,
						data, error);
				if (rc == -1) {
					g_object_unref(enumer);
					ERROR_Z
				} else if (rc == 1)
					flag = TRUE; //Abbruch gewählt
				else if (rc == 2) {
					g_object_unref(enumer);
					return 2;
				}
			}
		} //ende if ( file_child )
		else
			break;
	}

	g_object_unref(enumer); //unreferenziert auch alle infos und gfiles

	return (flag) ? 1 : 0;
}

static gint sond_treeviewfm_expand_dummy(SondTreeviewFM *stvfm, GtkTreeIter *iter,
		SondTVFMItem *stvfm_item, GError **error) {
	GPtrArray *arr_children = NULL;
	gint rc = 0;

	rc = sond_tvfm_item_load_children(stvfm_item, &arr_children, error);
	if (rc)
		ERROR_Z

	for (gint i = 0; i < arr_children->len; i++) {
		GtkTreeIter iter_new = { 0 };
		SondTVFMItem* child_item = NULL;

		child_item = g_ptr_array_index(arr_children, i);

		gtk_tree_store_insert(GTK_TREE_STORE(
				gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm) )),
				&iter_new, iter, -1);
		gtk_tree_store_set(GTK_TREE_STORE(
				gtk_tree_view_get_model( GTK_TREE_VIEW(stvfm) )),
				&iter_new, 0, G_OBJECT(child_item), -1);

		if (sond_tvfm_item_get_has_children(child_item)) { //Dummy einfügen
			GtkTreeIter newest_iter = { 0 };

			gtk_tree_store_insert(
				GTK_TREE_STORE(
						gtk_tree_view_get_model( GTK_TREE_VIEW(stvfm) )),
				&newest_iter, &iter_new, -1);
		}
	}

	g_ptr_array_unref(arr_children);

	return 0;
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
		display_message(gtk_widget_get_toplevel(GTK_WIDGET(tree_view)),
				"Zeile konnte nicht expandiert werden\n\n", error->message,
				NULL);
		g_error_free(error);

		return;
	}

	g_object_unref(stvfm_item);

	gtk_tree_store_remove(GTK_TREE_STORE(gtk_tree_view_get_model(tree_view)),
			&iter_dummy);

	gtk_tree_view_columns_autosize(tree_view);

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
		return 0; //keine Kinder, also kann filepart nicht gefunden werden

	//jetzt überprüfen, ob Kind dummy ist
	gtk_tree_model_get(
			gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)),
			&iter_child, 0, &stvfm_item, -1);

	if (!stvfm_item) {
		if (!open)
			return 0; //wenn's nicht geöffnet werden soll, sind wir hier fertig

		sond_treeview_expand_row(SOND_TREEVIEW(stvfm), iter_parent);

		//jetzt nochmal iter_child holen
		gtk_tree_model_iter_children(
				gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)), &iter_child,
				iter_parent);
	}
	else
		g_object_unref(stvfm_item);

	do {
		SondTVFMItem* stvfm_item = NULL;
		SondTVFMItemPrivate* stvfm_item_priv = NULL;
		SondTVFMItemType type = 0;

		gtk_tree_model_get(
				gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)), &iter_child, 0,
				&stvfm_item, -1);
		stvfm_item_priv =
				sond_tvfm_item_get_instance_private(stvfm_item);

		type = stvfm_item_priv->type;
		g_object_unref(stvfm_item);

		if (type == SOND_TVFM_ITEM_TYPE_DIR) {
			gchar* filepart_sfp = NULL;
			gchar const* path_item = NULL;
			gchar* filepart_part = NULL;
			gboolean path_found = FALSE;

			if (stvfm_item_priv->sond_file_part)
				filepart_sfp = sond_file_part_get_filepart(stvfm_item_priv->sond_file_part);
			path_item = stvfm_item_priv->path_or_section;

			if (filepart_sfp && path_item) filepart_part =
					g_strconcat(filepart_sfp, "//", path_item, NULL);
			else if (filepart_sfp) filepart_part = g_strdup(filepart_sfp);
			else if (path_item) filepart_part = g_strdup(path_item);
			else filepart_part = g_strdup("");

			g_free(filepart_sfp);

			path_found = g_str_has_prefix(filepart, filepart_part);
			g_free(filepart_part);

			if (path_found) { //sind auf dem richtigen Weg
				gint rc = 0;

				rc = sond_treeviewfm_file_part_visible(stvfm, &iter_child,
						filepart, open, iter_res, error);
				if (rc == -1)
					ERROR_Z
				else
					return rc; //wenn hier nicht gefunden, dann auch nirgendwo anders
			} //weitersuchen
		}
		else if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF) {
			SondFilePart* sfp = NULL;
			gchar* filepart_sfp = NULL;
			gboolean bingo = FALSE;

			sfp = stvfm_item_priv->sond_file_part;
			filepart_sfp = sond_file_part_get_filepart(sfp);

			bingo = (g_strcmp0(filepart, filepart_sfp) == 0);
			g_free(filepart_sfp);

			if (bingo) {
				if (iter_res) *iter_res = iter_child;

				return 1;
			}
		} //weitersuchen
	} while (gtk_tree_model_iter_next(
			gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)), &iter_child));

	return 0;
}

static void sond_treeviewfm_results_row_activated(GtkWidget *listbox,
		GtkWidget *row, gpointer user_data) {
	gint rc = 0;
	GtkWidget *label = NULL;
	const gchar *filepart = NULL;
	GtkTreeIter iter = { 0 };
	GError *error = NULL;

	SondTreeviewFM *stvfm = (SondTreeviewFM*) user_data;

	label = gtk_bin_get_child(GTK_BIN(row));
	filepart = gtk_label_get_label(GTK_LABEL(label));

	rc = sond_treeviewfm_file_part_visible(stvfm, NULL, filepart, TRUE, &iter,
			&error);
	if (rc == -1) {
		display_message(gtk_widget_get_toplevel(GTK_WIDGET(stvfm)),
				"Fehler\n\n", error->message, NULL);

		g_error_free(error);
	}
	else if (rc == 0) {
		display_message(gtk_widget_get_toplevel(GTK_WIDGET(stvfm)),
				"Datei nicht gefunden", filepart, NULL);

		return;
	}

	sond_treeview_set_cursor(SOND_TREEVIEW(stvfm), &iter);

	return;
}

static void sond_treeviewfm_render_text_cell(GtkTreeViewColumn *column,
		GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter,
		gpointer data) {
	SondTVFMItem *stvfm_item = NULL;
	gchar const *node_text = NULL;
	gint rc = 0;

	SondTreeviewFM *stvfm = SOND_TREEVIEWFM(data);

	gtk_tree_model_get(model, iter, 0, &stvfm_item, -1);
	if (!stvfm_item)
		return;

	g_object_unref(stvfm_item);

	node_text = sond_tvfm_item_get_display_name(stvfm_item);

	g_object_set(G_OBJECT(
			sond_treeview_get_cell_renderer_text(SOND_TREEVIEW(stvfm))),
			"text", node_text, NULL);

	if (SOND_TREEVIEWFM_GET_CLASS(stvfm)->deter_background) {
		GError *error = NULL;

		rc = SOND_TREEVIEWFM_GET_CLASS(stvfm)->deter_background(stvfm_item,
				&error);
		if (rc == -1) {
			g_warning("Fehler bei Ermittlung background: %s", error->message);
			g_error_free(error);

			return;
		}
	}

	g_object_set(G_OBJECT(
			sond_treeview_get_cell_renderer_text(SOND_TREEVIEW(stvfm))),
			"background-set", (rc == 1) ? TRUE : FALSE, NULL);

	return;
}

static void sond_treeviewfm_finalize(GObject *g_object) {
	Clipboard *clipboard = NULL;

	SondTreeviewFMPrivate *stvfm_priv = sond_treeviewfm_get_instance_private(
			SOND_TREEVIEWFM(g_object));

	g_free(stvfm_priv->root);

	clipboard =
			((SondTreeviewClass*) g_type_class_peek( SOND_TYPE_TREEVIEW))->clipboard;
	if ( G_OBJECT(clipboard->tree_view) == g_object)
		g_ptr_array_remove_range(clipboard->arr_ref, 0,
				clipboard->arr_ref->len);

	G_OBJECT_CLASS (sond_treeviewfm_parent_class)->finalize(g_object);

	return;
}

static void sond_treeviewfm_constructed(GObject *self) {
	//Text-Spalte wird editiert
	g_signal_connect(sond_treeview_get_cell_renderer_text(SOND_TREEVIEW(self)),
			"edited", G_CALLBACK(sond_treeviewfm_cell_edited), self); //Klick in textzelle = Datei umbenennen

	G_OBJECT_CLASS(sond_treeviewfm_parent_class)->constructed(self);

	return;
}

static gint sond_treeviewfm_open_stvfm_item(SondTreeviewFM* stvfm, SondTVFMItem* stvfm_item,
		gboolean open_with, GError** error) {
	gchar* filename = NULL;
	gint rc = 0;
	SondTVFMItemPrivate *stvfm_item_priv = sond_tvfm_item_get_instance_private(stvfm_item);

	filename = sond_file_part_write_to_tmp_file(stvfm_item_priv->sond_file_part, error);
	if (!filename)
		ERROR_Z

	rc = misc_datei_oeffnen(filename, open_with, error);
	g_free(filename);
	if (rc)
		ERROR_Z

	return 0;
}

static gint sond_treeviewfm_open(SondTreeviewFM *tree_view,
		SondTVFMItem *stvfm_item, gboolean open_with, GError **error) {
	gint rc = 0;
	SondTVFMItemPrivate *stvfm_item_priv = sond_tvfm_item_get_instance_private(stvfm_item);

	if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_DIR)
		return 0;

	rc = SOND_TREEVIEWFM_GET_CLASS(SOND_TREEVIEWFM(tree_view))->
			open_stvfm_item(SOND_TREEVIEWFM(tree_view), stvfm_item, open_with, error);
	if (rc)
		ERROR_Z

	return 0;
}

static void sond_treeviewfm_class_init(SondTreeviewFMClass *klass) {
	G_OBJECT_CLASS(klass)->finalize = sond_treeviewfm_finalize;
	G_OBJECT_CLASS(klass)->constructed = sond_treeviewfm_constructed;

	SOND_TREEVIEW_CLASS(klass)->render_text_cell =
			sond_treeviewfm_render_text_cell;

	klass->deter_background = NULL;
	klass->before_delete = NULL;
	klass->before_move = NULL;
	klass->after_move = NULL;
	klass->text_edited = sond_treeviewfm_text_edited;
	klass->results_row_activated = sond_treeviewfm_results_row_activated;
	klass->open_stvfm_item = sond_treeviewfm_open_stvfm_item;

	return;
}

static GtkTreeIter*
sond_treeviewfm_insert_node(SondTreeviewFM *stvfm, GtkTreeIter *iter,
		gboolean child) {
	GtkTreeIter new_iter;
	GtkTreeStore *treestore = GTK_TREE_STORE(
			gtk_tree_view_get_model( GTK_TREE_VIEW(stvfm) ));

	//Hauptknoten erzeugen
	if (!child)
		gtk_tree_store_insert_after(treestore, &new_iter, NULL, iter);
	//Unterknoten erzeugen
	else
		gtk_tree_store_insert_after(treestore, &new_iter, iter, NULL);

	GtkTreeIter *ret_iter = gtk_tree_iter_copy(&new_iter);

	return ret_iter; //muß nach Gebrauch gtk_tree_iter_freed werden!!!
}

static gint sond_treeviewfm_create_dir(SondTreeviewFM *stvfm, gboolean child,
		GError **error) {
	gint rc = 0;
	GObject *object = NULL;
	gchar *full_path = NULL;
	GFile *file = NULL;
	GFile *parent = NULL;
	GFileType type = G_FILE_TYPE_UNKNOWN;
	GtkTreeIter iter = { 0 };

	if (!sond_treeview_get_cursor(SOND_TREEVIEW(stvfm), &iter))
		return 0;

	gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)), &iter, 0,
			&object, -1);

	//Wenn etwas anderes als Datei - nix machen
	if (!G_IS_FILE_INFO(object)) {
		g_object_unref(object);
		return 0;
	}

	//wenn kein dir und als Kind eingefügt werden soll - nix machen
	type = g_file_info_get_file_type(G_FILE_INFO(object));
	g_object_unref(object);
	if (!(type == G_FILE_TYPE_DIRECTORY) && child)
		return 0;

	full_path = sond_treeviewfm_get_full_path(stvfm, &iter);
	file = g_file_new_for_path(full_path);
	g_free(full_path);

	if (child)
		parent = file;
	else {
		parent = g_file_get_parent(file);
		g_object_unref(file);
	} //nur noch parent muß unrefed werden - file wurde übernommen

	GFile *file_dir = g_file_get_child(parent, "Neues Verzeichnis");
	g_object_unref(parent);

	rc = sond_treeviewfm_move_copy_create_delete(stvfm, NULL, &file_dir, 0,
			error);
	//anderer Fall tritt nicht ein
	if (rc == -1)
		ERROR_Z

			//In Baum tun
	GtkTreeIter *iter_new = NULL;
	GFileInfo *info_new = NULL;

	iter_new = sond_treeviewfm_insert_node(stvfm, &iter, child);

	info_new = g_file_query_info(file_dir, "*", G_FILE_QUERY_INFO_NONE, NULL,
			error);
	g_object_unref(file_dir);
	if (!info_new)
		ERROR_Z

	gtk_tree_store_set(
			GTK_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(stvfm) )),
			iter_new, 0, info_new, -1);

	g_object_unref(info_new);

	sond_treeview_set_cursor(SOND_TREEVIEW(stvfm), iter_new);

	gtk_tree_iter_free(iter_new);

	return 0;
}

static void sond_treeviewfm_punkt_einfuegen_activate(GtkMenuItem *item,
		gpointer data) {
	gint rc = 0;
	GError *error = NULL;
	gboolean child = FALSE;

	SondTreeviewFM *stvfm = (SondTreeviewFM*) data;

	child = (gboolean) GPOINTER_TO_INT(
			g_object_get_data( G_OBJECT(item), "kind" ));

	rc = sond_treeviewfm_create_dir(stvfm, child, &error);
	if (rc) {
		display_message(gtk_widget_get_toplevel(GTK_WIDGET(stvfm)),
				"Verzeichnis kann nicht eingefügt werden\n\n", error->message, NULL);
		g_error_free(error);
	}

	return;
}

/* Rekursiv Inhalte eines Verzeichnisses kopieren */
static gboolean
copy_directory_contents(GFile *src, GFile *dst, GError **error)
{
    GError *tmp_err = NULL;
    GFileEnumerator *enumerator =
        g_file_enumerate_children(src,
                                  G_FILE_ATTRIBUTE_STANDARD_NAME ","
                                  G_FILE_ATTRIBUTE_STANDARD_TYPE,
                                  G_FILE_QUERY_INFO_NONE,
                                  NULL, &tmp_err);
    if (!enumerator) {
        g_propagate_error(error, tmp_err);
        return FALSE;
    }

    GFileInfo *info;
    while ((info = g_file_enumerator_next_file(enumerator, NULL, &tmp_err)) != NULL) {
        const char *child_name = g_file_info_get_name(info);
        GFileType ftype = g_file_info_get_file_type(info);

        GFile *child_src = g_file_get_child(src, child_name);
        GFile *child_dst = g_file_get_child(dst, child_name);

        gboolean ok = FALSE;
        if (ftype == G_FILE_TYPE_DIRECTORY) {
            if (!g_file_make_directory(child_dst, NULL, &tmp_err)) {
                g_propagate_error(error, tmp_err);
                g_object_unref(child_src);
                g_object_unref(child_dst);
                g_object_unref(info);
                g_object_unref(enumerator);
                return FALSE;
            }
            ok = copy_directory_contents(child_src, child_dst, &tmp_err);
        } else if (ftype == G_FILE_TYPE_REGULAR) {
            ok = g_file_copy(child_src, child_dst,
                             G_FILE_COPY_NONE,
                             NULL, NULL, NULL, &tmp_err);
        }

        g_object_unref(child_src);
        g_object_unref(child_dst);
        g_object_unref(info);

        if (!ok) {
            g_object_unref(enumerator);
            g_propagate_error(error, tmp_err);
            return FALSE;
        }
    }

    g_object_unref(enumerator);
    if (tmp_err) {
        g_propagate_error(error, tmp_err);
        return FALSE;
    }

    return TRUE;
}

/* Race-sicher Dateien oder Verzeichnisse kopieren, nur Top-Level prüft Suffix */
static gchar* copy_path(const gchar *src_path,
                        const gchar *dest_dir,
                               GError     **error)
{
    guint max_tries = 100;

    g_autoptr(GFile) src = g_file_new_for_path(src_path);

    // Typ bestimmen
    GFileType ftype = g_file_query_file_type(src, G_FILE_QUERY_INFO_NONE, NULL);
    if (ftype == G_FILE_TYPE_UNKNOWN) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                    "Quelle %s nicht gefunden oder Typ unbekannt", src_path);
        return NULL;
    }

    g_autofree gchar *base = g_path_get_basename(src_path);

    const gchar *dot = strrchr(base, '.');
    gboolean has_ext = (ftype == G_FILE_TYPE_REGULAR) && dot && dot != base;
    g_autofree gchar *name = has_ext ? g_strndup(base, (gsize)(dot - base))
                                     : g_strdup(base);
    const gchar *ext = has_ext ? dot : "";

    for (guint i = 0; i <= max_tries; i++) {
        g_autofree gchar *trial_base = NULL;
        if (i == 0) {
            trial_base = g_strdup(base);
        } else {
            g_autofree gchar *suf = g_strdup_printf(" (%u)", i);
            trial_base = g_strconcat(name, suf, ext, NULL);
        }

        g_autofree gchar *trial_path = g_build_filename(dest_dir, trial_base, NULL);
        g_autoptr(GFile) dst = g_file_new_for_path(trial_path);

        if (ftype == G_FILE_TYPE_DIRECTORY) {
            // Nur Top-Level prüft Suffix
            if (!g_file_make_directory(dst, NULL, error)) {
                if (g_error_matches(*error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
                    g_clear_error(error);
                    continue; // nächster Name
                }
                return NULL;
            }
            if (!copy_directory_contents(src, dst, error)) {
                g_file_delete(dst, NULL, NULL);
                return NULL;
            }
            return g_strdup(trial_path);
        } else {
            // Dateien: Race-sicher reservieren
            GError *tmp_err = NULL;
            GFileOutputStream *out = g_file_create(dst, G_FILE_CREATE_NONE, NULL, &tmp_err);
            if (!out) {
                if (g_error_matches(tmp_err, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
                    g_clear_error(&tmp_err);
                    continue; // nächster Kandidat
                }
                g_propagate_error(error, tmp_err);
                return NULL;
            }
            g_output_stream_close(G_OUTPUT_STREAM(out), NULL, NULL);
            g_object_unref(out);

            if (g_file_copy(src, dst,
                            G_FILE_COPY_OVERWRITE,
                            NULL, NULL, NULL, &tmp_err)) {
                return g_strdup(trial_path);
            }
            g_file_delete(dst, NULL, NULL);
            g_propagate_error(error, tmp_err);
            return NULL;
        }
    }

    g_set_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS,
                "Kein eindeutiger Zielname nach %u Versuchen", max_tries);
    return NULL;
}

static gint copy_stvfm_item(SondTVFMItem* stvfm_item, SondTVFMItemPrivate* stvfm_item_parent_priv,
		SondTVFMItem** stvfm_item_new, GError** error) {

	return 0;
}

static gint move_path(SondTreeviewFM* stvfm,
					  const gchar *src_path,
                      const gchar *dest_dir,
					  gchar** toplevel_path,
                      GError     **error) {
	guint max_tries = 100;

	g_autoptr(GFile) src = g_file_new_for_path(src_path);
	GFileType ftype = g_file_query_file_type(src, G_FILE_QUERY_INFO_NONE, NULL);
	if (ftype == G_FILE_TYPE_UNKNOWN) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
					"Quelle %s nicht gefunden oder Typ unbekannt", src_path);
		return -1;
	}

	g_autofree gchar *base = g_path_get_basename(src_path);

	const gchar *dot = strrchr(base, '.');
	gboolean has_ext = (ftype == G_FILE_TYPE_REGULAR) && dot && dot != base;
	g_autofree gchar *name = has_ext
		? g_strndup(base, (gsize)(dot - base))
		: g_strdup(base);
	const gchar *ext = has_ext ? dot : "";

	for (guint i = 0; i <= max_tries; i++) {
		gboolean suc = FALSE;
		g_autofree gchar *trial_base = (i == 0) ? g_strdup(base) :
				g_strconcat(name, g_strdup_printf(" (%u)", i), ext, NULL);
		g_autofree gchar *trial_path =
				(dest_dir != NULL) ? g_strconcat(dest_dir, "/", trial_base, NULL) : g_strdup(trial_base);
		g_autoptr(GFile) dst = g_file_new_for_path(trial_path);

		if (SOND_TREEVIEWFM_GET_CLASS(stvfm)->before_move) {
			gint rc = 0;

			rc = SOND_TREEVIEWFM_GET_CLASS(stvfm)->before_move(stvfm, src_path, trial_path, error);
			if (rc == -1)
				ERROR_Z
		}

		suc = g_file_move(src, dst,
						G_FILE_COPY_OVERWRITE,
						NULL, NULL, NULL,
						error);

		//Fehlschlag von after_move ist nicht vorgesehen!
		if (SOND_TREEVIEWFM_GET_CLASS(stvfm)->after_move)
			SOND_TREEVIEWFM_GET_CLASS(stvfm)->after_move(stvfm, suc, error);

		if (suc) {
			*toplevel_path = g_strdup(trial_base);

			return 0; // Erfolg
		}

		if (g_error_matches(*error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
			g_clear_error(error);
			continue;  // nächster Suffix versuchen
		}
		else if (g_error_matches(*error, G_IO_ERROR, G_IO_ERROR_BUSY)) {
			g_clear_error(error);

			gint res = dialog_with_buttons(
					gtk_widget_get_toplevel(GTK_WIDGET(stvfm)),
					"Zugriff nicht erlaubt",
					"Datei möglicherweise geöffnet", NULL,
					"Erneut versuchen", 1, "Überspringen", 2, "Abbrechen",
					3, NULL);

			if (res == 1)
				continue; //Namensgleichheit - wird oben behandelt
			else if (res == 2)
				return 1; //Überspringen
			else if (res == 3)
				return 2; //Abbrechen
		}

		g_prefix_error(error, "%s\n", __func__);
		return -1;  // anderer Fehler – Abbruch
	}

	g_set_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS,
				"Kein eindeutiger Zielname nach %u Versuchen", max_tries);
	return -1;
}

static gint move_across_sfps(SondTVFMItemPrivate* stvfm_item_priv_src,
		SondTVFMItemPrivate* stvfm_item_parent_priv_dst,
		gchar** toplevel_path, GError** error) {

	if (1) {
		if (error) *error = g_error_new(g_quark_from_static_string("sond"), 0,
				"Verschieben zwischen verschiedenen Dateisystemen nicht implementiert");
		return -1;
	}

	/*
			if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_DIR) {
				if (stvfm_item_priv->sond_file_part &&
						SOND_IS_FILE_PART_ZIP(stvfm_item_priv->sond_file_part) &&
						!stvfm_item_priv->path_or_section) //Root-Zip-Dir soll als Datei kopiert werden
					rc = paste_leaf_in_fs(s_fm_paste_selection->stvfm_item_priv_parent->path_or_section,
						stvfm_item_priv->sond_file_part, clipboard->ausschneiden,
						&path_new, error);
				else
					rc = paste_dir_in_fs(s_fm_paste_selection->stvfm_item_priv_parent->path_or_section,
							stvfm_item_priv->path_or_section, stvfm_item_priv->sond_file_part,
							clipboard->ausschneiden, &dir_with_children, &path_new, error);
			}
			else if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF)
				rc = paste_leaf_in_fs(
						s_fm_paste_selection->stvfm_item_priv_parent->path_or_section,
						stvfm_item_priv->sond_file_part, clipboard->ausschneiden,
						&path_new, error);
			else {
				if (error) *error = g_error_new(
						g_quark_from_static_string("sond"), 0,
						"%s\nEinfügen von Section nicht unterstützt",
						__func__);

				return -1;
			}

			if (rc)
				ERROR_Z
		}
		else if (SOND_IS_FILE_PART_PDF(s_fm_paste_selection->stvfm_item_priv_parent->sond_file_part)) {
			if (error) *error = g_error_new(
					g_quark_from_static_string("sond"), 0,
					"%s\nEinfügen in PDF-Datei nicht unterstützt", __func__);

			return -1;
		}
		else if (SOND_IS_FILE_PART_ZIP(s_fm_paste_selection->stvfm_item_priv_parent->sond_file_part)) {
			if (error) *error = g_error_new(
					g_quark_from_static_string("sond"), 0,
					"%s\nEinfügen in ZIP-Datei nicht unterstützt", __func__);

			return -1;
		}
	*/
	return 0;
}

static gint move_stvfm_item(SondTVFMItem* stvfm_item, SondTVFMItemPrivate* stvfm_item_parent_priv,
		GError** error) {
	gchar* toplevel_path = NULL;
	gint rc = 0;
	SondTVFMItemPrivate* stvfm_item_priv = sond_tvfm_item_get_instance_private(stvfm_item);

	//Sonderbehandlung, wenn Datei innnerhalb Dateisystem verschoben wird -
	//weil move geht schneller
	if ((!stvfm_item_parent_priv || !stvfm_item_parent_priv->sond_file_part) &&
			(!stvfm_item_priv->sond_file_part ||
					!sond_file_part_get_parent(stvfm_item_priv->sond_file_part))) {
		gchar const* path_src = NULL;
		gchar const* path_dst = NULL;

		if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_DIR &&
				!stvfm_item_priv->sond_file_part) //z.B. zip-Datei ist als Dir "gemountet"
			path_src = stvfm_item_priv->path_or_section;
		else
			path_src = sond_file_part_get_path(stvfm_item_priv->sond_file_part);

		if (stvfm_item_parent_priv)
			path_dst = stvfm_item_parent_priv->path_or_section;

		rc = move_path(stvfm_item_priv->stvfm, path_src, path_dst, &toplevel_path, error);
	}
	else //Verschieben zwischen zwei Welten
		rc = move_across_sfps(stvfm_item_priv, stvfm_item_parent_priv,
				&toplevel_path, error);

	if (rc == -1)
		ERROR_Z
	else if (rc)
		return rc; //g_free(toplevel_path) nicht erforderlich

	//Ziel-sfp
	SondFilePart* sfp_dst = NULL;

	if (stvfm_item_parent_priv)
		sfp_dst = stvfm_item_parent_priv->sond_file_part;

	//Wenn leaf oder root-dir verschoben wird
	if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF ||
			(stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_DIR &&
					!stvfm_item_priv->path_or_section)) {
		SondFilePart* sfp_parent = NULL;

		sfp_parent = sond_file_part_get_parent(stvfm_item_priv->sond_file_part);

		if (sfp_parent != sfp_dst)
			//neues Eltern-sfp
			sond_file_part_set_parent(stvfm_item_priv->sond_file_part, sfp_dst);

		//path von sfp anpassen - item hat ja keinen!
		gchar const* path_dir = NULL;
		gchar* path_new = NULL;

		path_dir = (stvfm_item_parent_priv) ? stvfm_item_parent_priv->path_or_section : NULL;
		path_new = g_strconcat((path_dir) ? path_dir : "", (path_dir) ? "/" : "", toplevel_path, NULL);

		sond_file_part_set_path(stvfm_item_priv->sond_file_part, path_new);
		g_free(path_new);
	}
	else { //Wenn "wirkliches" dir verschoben wird:

		//bisherigen path merken - für Längenbestimmung
		gchar* path_old = NULL;

		path_old = stvfm_item_priv->path_or_section;

		//dann neuen path für item
		stvfm_item_priv->path_or_section = g_strconcat(
				(stvfm_item_parent_priv) ? stvfm_item_parent_priv->path_or_section : "",
				(stvfm_item_parent_priv && stvfm_item_parent_priv->path_or_section) ? "/" : "",
				toplevel_path, NULL);

		//dann alle geöffneten sfps, die in diesem dir liegen, anpassen
		//welche sfps sind in diesem Raum geöffnet?
		GPtrArray* arr_opened_children = NULL;

		arr_opened_children = sond_file_part_get_arr_opened_files(stvfm_item_priv->sond_file_part); //NULL ist ok

		//welche liegen "unterhalb"?
		for (guint i = 0; arr_opened_children && i < arr_opened_children->len; i++) {
			SondFilePart* sfp_child = NULL;
			gchar const* path_child = NULL;

			sfp_child = g_ptr_array_index(arr_opened_children, i);
			path_child = sond_file_part_get_path(sfp_child);

			if (!stvfm_item_priv->path_or_section ||
					g_str_has_prefix(path_child, stvfm_item_priv->path_or_section)) { //Treffer
				//ggf. neues Eltern-sfp
				if (stvfm_item_priv->sond_file_part != sfp_dst)
				{
					if (stvfm_item_parent_priv)
						sond_file_part_set_parent(sfp_child, stvfm_item_parent_priv->sond_file_part);
					else
						sond_file_part_set_parent(sfp_child, NULL);
				}

				//Pfad von sfp_child anpassen
				gchar* path_new = NULL;

				//stvfm_item_priv->path_or_section ist der neue Pfad des Verzeichnisses
				//kann nicht NULL sein, ist mindestens toplevel_path!
				path_new = g_strconcat(stvfm_item_priv->path_or_section, "/",
						path_child + ((path_old) ? strlen(path_old) + 1 : 0), NULL);
				sond_file_part_set_path(sfp_child, path_new);
				g_free(path_new);
			}
		}

		g_free(path_old);
	}

	g_free(toplevel_path);

	return 0;
}

typedef struct _S_FM_Paste_Selection {
	SondTVFMItemPrivate *stvfm_item_priv_parent;
	GtkTreeIter *iter_parent;
	GtkTreeIter *iter_dest;
	GtkTreeIter *iter_cursor;
	gboolean kind;
	gboolean expanded;
	gboolean inserted;
} SFMPasteSelection;

static gint sond_treeviewfm_paste_clipboard_foreach(SondTreeview *stv,
		GtkTreeIter *iter, gpointer data, GError **error) {
	SondTVFMItem *stvfm_item = NULL;
	SondTVFMItem *stvfm_item_new = NULL;
	SFMPasteSelection *s_fm_paste_selection = (SFMPasteSelection*) data;
	Clipboard *clipboard = NULL;
	gint rc = 0;

	clipboard = ((SondTreeviewClass*) g_type_class_peek_static(
			SOND_TYPE_TREEVIEW))->clipboard;

	gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(stv)), iter, 0,
			&stvfm_item, -1);
	g_object_unref(stvfm_item); //keine Angst - tree_store hält ref

	//Vers verschieben oder kopieren?
	if (clipboard->ausschneiden) {
		GtkTreeIter parent_iter = { 0 };

		//Prüfen, ob innerhalb des gleichen Verzeichnisses verschoben werden soll
		//dann: return 0;
		if (gtk_tree_model_iter_parent(
				gtk_tree_view_get_model(GTK_TREE_VIEW(stv)), &parent_iter, iter)) {
			if (s_fm_paste_selection->iter_parent &&
					parent_iter.user_data == s_fm_paste_selection->iter_parent->user_data &&
					parent_iter.stamp == s_fm_paste_selection->iter_parent->stamp)
				return 0; //innerhalb des gleichen Verzeichnisses verschieben
		}
		else
			if (!s_fm_paste_selection->iter_parent)
				return 0;

		rc = move_stvfm_item(stvfm_item, s_fm_paste_selection->stvfm_item_priv_parent,
				error);

		if (rc == 0)
			stvfm_item_new = g_object_ref(stvfm_item);
	}
	else
		rc = copy_stvfm_item(stvfm_item, s_fm_paste_selection->stvfm_item_priv_parent,
				&stvfm_item_new, error);

	if (rc == -1)
		ERROR_Z
	else if (rc == 1) //Überspringen gewählt
		return 0;
	else if (rc == 2) //Abbrechen gewählt
		return 1;

	s_fm_paste_selection->inserted = TRUE;

	//wenn neuer Punkt sichtbar, dann neues stvfm_item erzeugen und in Baum einfügen
	//Knoten müssen nur eingefügt werden, wenn Row expanded ist; sonst passiert das im callback beim Öffnen
	if (!s_fm_paste_selection->kind || s_fm_paste_selection->expanded) {
		//Ziel-FS-tree eintragen
		GtkTreeIter *iter_new = NULL;

		iter_new = sond_treeviewfm_insert_node(SOND_TREEVIEWFM(stv),
				s_fm_paste_selection->iter_cursor, s_fm_paste_selection->kind);

		*(s_fm_paste_selection->iter_cursor) = *iter_new;
		gtk_tree_iter_free(iter_new);
		s_fm_paste_selection->kind = FALSE;

		//Falls Verzeichnis mit Datei innendrin: dummy in neuen Knoten einfügen
		if (sond_tvfm_item_get_has_children(stvfm_item_new)) {
			GtkTreeIter iter_tmp = { 0 };

			gtk_tree_store_insert(
					GTK_TREE_STORE(
							gtk_tree_view_get_model( GTK_TREE_VIEW(stv) )),
					&iter_tmp, s_fm_paste_selection->iter_cursor, -1);
		}

		gtk_tree_store_set(
				GTK_TREE_STORE(
						gtk_tree_view_get_model( GTK_TREE_VIEW(stv) )),
				s_fm_paste_selection->iter_cursor, 0, stvfm_item_new, -1);
	}

	g_object_unref(stvfm_item_new);

	//Knoten löschen, wenn ausgeschnitten
	if (clipboard->ausschneiden)
		gtk_tree_store_remove(
				GTK_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(stv) )),
				iter);

	return 0;
}

static gint sond_treeviewfm_paste_clipboard(SondTreeviewFM *stvfm, gboolean kind,
		GError **error) {
	gint rc = 0;
	GtkTreeIter iter_cursor = { 0 };
	GtkTreeIter iter_parent = { 0 };
	gboolean expanded = FALSE;
	Clipboard *clipboard = NULL;
	SondTVFMItem *stvfm_item = NULL;
	SondTVFMItemPrivate *stvfm_item_priv = NULL;
	gboolean parent_root = FALSE;

	clipboard =
			((SondTreeviewClass*) g_type_class_peek_static(SOND_TYPE_TREEVIEW))->clipboard;

	if (clipboard->tree_view != SOND_TREEVIEW(stvfm))
		return 0;

	if (clipboard->arr_ref->len == 0)
		return 0;

	//iter unter cursor holen
	if (!sond_treeview_get_cursor(SOND_TREEVIEW(stvfm), &iter_cursor))
		return 0;

	//iter_parent ermitteln
	if (kind) {
		//prüfen, ob geöffnet ist
		GtkTreePath *path = NULL;

		path = gtk_tree_model_get_path(
				gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)), &iter_cursor);
		expanded = gtk_tree_view_row_expanded(GTK_TREE_VIEW(stvfm), path);
		gtk_tree_path_free(path);

		iter_parent = iter_cursor;
	}
	else
		parent_root = !gtk_tree_model_iter_parent(
				gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)), &iter_parent,
				&iter_cursor);

	SFMPasteSelection s_fm_paste_selection = { NULL, NULL, NULL, &iter_cursor,
			kind, expanded, FALSE };

	if (!parent_root) {
		//STVFM_Item im tree holen
		gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)),
				&iter_parent, 0, &stvfm_item, -1);
		stvfm_item_priv =
				sond_tvfm_item_get_instance_private(stvfm_item);
		g_object_unref(stvfm_item); //mutig sein

		s_fm_paste_selection.iter_parent = &iter_parent;
		s_fm_paste_selection.stvfm_item_priv_parent = stvfm_item_priv;

		//nur in Verzeichnis einfügen möglich, an sich
		//außer z.B. PDF-Datei, die noch keine embFiles hat
		//zwangsläufig ist expanded == FALSE und kind == TRUE,
		//denn sonst gäbe es ja Kinder
		if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF_SECTION ||
				(stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF &&
						//PDF-Datei (bisher) ohne embFiles ist stvfm_item_type LEAF!
						(SOND_IS_FILE_PART_PDF(stvfm_item_priv->sond_file_part) &&
								sond_file_part_has_children(stvfm_item_priv->sond_file_part)))) {
			if (error)
				*error = g_error_new(g_quark_from_static_string("sond"), 0,
						"%s\nEinfügen in Datei nicht unterstützt", __func__);

			return -1;
		}
	}

	rc = sond_treeview_clipboard_foreach(
			sond_treeviewfm_paste_clipboard_foreach,
			(gpointer) &s_fm_paste_selection, error);
	if (rc == -1)
		ERROR_Z

	//Wenn in bisher leere pdf verschoben wird: von LEAF zu DIR ändern
	if (!parent_root && stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF) {//muß ja der Fall sein,
																  //daß z.B. bisher leere PDF eingefügt wurde
		stvfm_item_priv->type = SOND_TVFM_ITEM_TYPE_DIR;
		stvfm_item_priv->has_children = TRUE;
	}


	//Wenn in nicht ausgeklapptes Verzeichnis etwas eingefügt wurde und
	//Verzeichnis bisher leer ist:
	//Dummy einfügen
	if (s_fm_paste_selection.inserted && kind && !expanded &&
			!gtk_tree_model_iter_has_child(
					gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)),
					s_fm_paste_selection.iter_cursor)) {
		GtkTreeIter iter_tmp = { 0 };

		gtk_tree_store_insert(
				GTK_TREE_STORE(gtk_tree_view_get_model( GTK_TREE_VIEW(stvfm) )),
				&iter_tmp, s_fm_paste_selection.iter_cursor, -1);
	}

	gtk_widget_queue_draw(GTK_WIDGET(clipboard->tree_view));

	//Wenn neuer Knoten sichtbar: Cursor setzen
	if (!kind || expanded)
		sond_treeview_set_cursor(SOND_TREEVIEW(stvfm),
				s_fm_paste_selection.iter_cursor);

	return 0;
}

static void sond_treeviewfm_paste_activate(GtkMenuItem *item, gpointer data) {
	gboolean kind = FALSE;
	gint rc = 0;
	GError *error = NULL;

	SondTreeviewFM *stvfm = (SondTreeviewFM*) data;

	kind = (gboolean) GPOINTER_TO_INT(
			g_object_get_data( G_OBJECT(item), "kind" ));

	if (sond_treeview_test_cursor_descendant(SOND_TREEVIEW(stvfm), kind))
		display_message(gtk_widget_get_toplevel(GTK_WIDGET(stvfm)),
				"Unzulässiges Ziel: Abkömmling von zu verschiebendem Knoten",
				NULL);

	rc = sond_treeviewfm_paste_clipboard(stvfm, kind, &error);
	if (rc) {
		display_message(gtk_widget_get_toplevel(GTK_WIDGET(stvfm)),
				"Einfügen nicht möglich\n\n", error->message, NULL);
		g_error_free(error);
	}

	return;
}

static gint sond_treeviewfm_delete_zip_dir(SondTVFMItem* stvfm_item, GError** error) {

	return 0;
}

static gint sond_treeviewfm_foreach_loeschen(SondTreeview *stv,
		GtkTreeIter *iter, gpointer data, GError **error) {
	SondTVFMItem* stvfm_item = NULL;
	SondTVFMItemPrivate* stvfm_item_priv = NULL;

	gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(stv)), iter, 0,
			&stvfm_item, -1);
	g_object_unref(stvfm_item);

	stvfm_item_priv = sond_tvfm_item_get_instance_private(stvfm_item);

	if (SOND_TREEVIEWFM_GET_CLASS(stvfm_item_priv->stvfm)->before_delete) {
		gint rc = 0;

		rc = SOND_TREEVIEWFM_GET_CLASS(stvfm_item_priv->stvfm)->before_delete(stvfm_item, error);
		if (rc == -1)
			ERROR_Z
		else if (rc == 1)
			return 0;
	}

	if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_DIR) {
		if (!stvfm_item_priv->sond_file_part) { //FileSystem - geht schon
			gint rc = 0;
			gchar* path = NULL;
			SondTreeviewFMPrivate* stvfm_priv =
					sond_treeviewfm_get_instance_private(stvfm_item_priv->stvfm);

			if (!stvfm_item_priv->path_or_section)
				return 0; //Root-Verzeichnis kann nicht gelöscht werden!

			path = g_strconcat(stvfm_priv->root, "/", stvfm_item_priv->path_or_section, NULL);

			rc = rm_r(path);
			g_free(path);
			if (rc) {
				if (error) *error = g_error_new(g_quark_from_static_string("stdlib"),
						errno, "%s\n%s", __func__, strerror(errno));

				return -1;
			}
		}
		else if (SOND_IS_FILE_PART_ZIP(stvfm_item_priv->sond_file_part)) {
			if (!stvfm_item_priv->path_or_section) { //ganze zip-Datei
				gint rc = 0;

				rc = sond_file_part_delete_sfp(stvfm_item_priv->sond_file_part, error);
				if (rc) {
					ERROR_Z
				}
			}
			else {//ZIP-Dir löschen
				gint rc = 0;

				rc = sond_treeviewfm_delete_zip_dir(stvfm_item, error);
				if (rc)
					ERROR_Z
			}
		}
		else if (SOND_IS_FILE_PART_PDF(stvfm_item_priv->sond_file_part)) { //PDF-Datei ist Dir - ganz löschen
			gint rc = 0;

			rc = sond_file_part_delete_sfp(stvfm_item_priv->sond_file_part, error);
			if (rc)
				ERROR_Z
		}
		//else if (SOND_IS_FILE_PART_GMESSAGE(sfp))
	}
	else if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF) {
		gint rc = 0;

		if (SOND_IS_FILE_PART_PDF(stvfm_item_priv->sond_file_part) &&
				sond_file_part_has_children(stvfm_item_priv->sond_file_part)) {
			if (error) *error = g_error_new(g_quark_from_static_string("sond"), 0,
					"%s\nLöschen des pagetree aus PDF-Datei nicht unterstützt",
					__func__);

			return -1;
		}

		rc = sond_file_part_delete_sfp(stvfm_item_priv->sond_file_part, error);
		if (rc)
			ERROR_Z
	}
	else if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF_SECTION) {
		if (SOND_TREEVIEWFM_GET_CLASS(stvfm_item_priv->stvfm)->delete_section) {
			gint rc = 0;

			rc = SOND_TREEVIEWFM_GET_CLASS(stvfm_item_priv->stvfm)->delete_section(stvfm_item, error);
			if (rc)
				ERROR_Z
		}
		else
			return 0;
	}

	gtk_tree_store_remove(GTK_TREE_STORE(
			gtk_tree_view_get_model(GTK_TREE_VIEW(stv))), iter);

	return 0;
}

static void sond_treeviewfm_loeschen_activate(GtkMenuItem *item, gpointer data) {
	gint rc = 0;
	GError *error = NULL;

	SondTreeviewFM *stvfm = (SondTreeviewFM*) data;

	rc = sond_treeview_selection_foreach(SOND_TREEVIEW(stvfm),
			sond_treeviewfm_foreach_loeschen, NULL, &error);
	if (rc == -1) {
		display_message(gtk_widget_get_toplevel(GTK_WIDGET(stvfm)),
				"Löschen nicht möglich\n\n", error->message, NULL);
		g_error_free(error);
	}

	return;
}

static void sond_treeviewfm_datei_oeffnen_activate(GtkMenuItem *item,
		gpointer data) {
	SondTreeviewFM *stvfm = (SondTreeviewFM*) data;

	g_signal_emit_by_name(stvfm, "row-activated", NULL, NULL, NULL);

	return;
}

static void sond_treeviewfm_datei_oeffnen_mit_activate(GtkMenuItem *item,
		gpointer data) {
	SondTreeviewFM *stvfm = (SondTreeviewFM*) data;

	g_signal_emit_by_name(stvfm, "row-activated", NULL, NULL,
			GINT_TO_POINTER(1));

	return;
}

static void sond_treeviewfm_show_hits(SondTreeviewFM *stvfm,
		GPtrArray *arr_hits) {
	GtkWidget *window = NULL;
	GtkWidget *listbox = NULL;
	SondTreeviewFMClass *klass = SOND_TREEVIEWFM_GET_CLASS(stvfm);

	//Fenster erzeugen
	window = result_listbox_new(
			GTK_WINDOW(gtk_widget_get_toplevel( GTK_WIDGET(stvfm) )),
			"Suchergebnis", GTK_SELECTION_MULTIPLE);

	listbox = (GtkWidget*) g_object_get_data(G_OBJECT(window), "listbox");

	g_signal_connect(listbox, "row-activated",
			G_CALLBACK(klass->results_row_activated), (gpointer ) stvfm);

	for (gint i = 0; i < arr_hits->len; i++) {
		gchar *path = NULL;
		GtkWidget *label = NULL;

		path = g_ptr_array_index(arr_hits, i);

		label = gtk_label_new((const gchar*) path);

		gtk_list_box_insert(GTK_LIST_BOX(listbox), label, -1);
	}

	gtk_widget_show_all(window);

	return;
}

typedef struct _SearchFS {
	gchar *needle;
	gboolean exact_match;
	gboolean case_sens;
	GPtrArray *arr_hits;
	InfoWindow *info_window;
	volatile gint *atom_ready;
	volatile gint *atom_cancelled;
} SearchFS;

static gint sond_treeviewfm_search_needle(SondTreeviewFM *stvfm,
		GtkTreeIter *iter_dir, GFile *file, GFileInfo *info,
		GtkTreeIter *iter_file, gpointer data, GError **error) {
	gchar *path = NULL;
	gchar *basename = NULL;
	gboolean found = FALSE;

	SearchFS *search_fs = (SearchFS*) data;

	basename = g_file_get_basename(file);

	if (!search_fs->case_sens) {
		gchar *basename_tmp = NULL;

		basename_tmp = g_ascii_strdown(basename, -1);
		g_free(basename);
		basename = basename_tmp;
	}

	if (search_fs->exact_match == TRUE) {
		if (!g_strcmp0(basename, search_fs->needle))
			found = TRUE;
	} else if (strstr(basename, search_fs->needle))
		found = TRUE;
	g_free(basename);

	if (found) {
		path = g_file_get_path(file);
		g_ptr_array_add(search_fs->arr_hits, path);
	}

	if (g_atomic_int_get(search_fs->atom_cancelled))
		g_atomic_int_set(search_fs->atom_ready, 1);

	return 0;
}

typedef struct _DataThread {
	SearchFS *search_fs;
	SondTreeview *stv;
	GtkTreeIter *iter;
	GFile *file;
	GError **error;
} DataThread;

static gpointer sond_treeviewfm_thread_search(gpointer data) {
	DataThread *data_thread = (DataThread*) data;
	GError **error = data_thread->error;

	if (data_thread->iter) //nur, wenn nicht root-Verzeichnis
		sond_treeviewfm_search_needle(SOND_TREEVIEWFM(data_thread->stv),
				data_thread->iter, data_thread->file,
				NULL, NULL, data_thread->search_fs, NULL);

	if (g_file_query_file_type(data_thread->file, G_FILE_QUERY_INFO_NONE, NULL)
			== G_FILE_TYPE_DIRECTORY) {
		gint rc = 0;

		rc = sond_treeviewfm_dir_foreach(SOND_TREEVIEWFM(data_thread->stv),
				data_thread->iter, data_thread->file, TRUE,
				sond_treeviewfm_search_needle, data_thread->search_fs,
				data_thread->error);
		if (rc == -1) {
			g_atomic_int_set(data_thread->search_fs->atom_ready, 1);
			ERROR_Z_VAL(GINT_TO_POINTER(-1))
		}
	}

	g_atomic_int_set(data_thread->search_fs->atom_ready, 1);

	return NULL;
}

static gint sond_treeviewfm_search(SondTreeview *stv, GtkTreeIter *iter,
		gpointer data, GError **error) {
	GFile *file = NULL;
	gchar *path_root = NULL;
	GThread *thread_search = NULL;
	gpointer res_thread = NULL;

	SearchFS *search_fs = (SearchFS*) data;

	path_root = sond_treeviewfm_get_full_path(SOND_TREEVIEWFM(stv), iter);
	info_window_set_message(search_fs->info_window, path_root);
	file = g_file_new_for_path(path_root);
	g_free(path_root);

	DataThread data_thread = { search_fs, stv, iter, file, error };
	thread_search = g_thread_new( NULL, sond_treeviewfm_thread_search,
			&data_thread);

	while (!g_atomic_int_get(search_fs->atom_ready)) {
		if (search_fs->info_window->cancel)
			g_atomic_int_set(search_fs->atom_cancelled, 1);
	}

	res_thread = g_thread_join(thread_search);
	g_object_unref(file);
	if (GPOINTER_TO_INT(res_thread) == -1)
		ERROR_Z

	return 0;
}

static void sond_treeviewfm_search_activate(GtkMenuItem *item, gpointer data) {
	gboolean only_sel = FALSE;
	gint rc = 0;
	gchar *search_text = NULL;
	SearchFS search_fs = { 0 };
	gint ready = 0;
	gint cancelled = 0;
	GError* error = NULL;

	SondTreeviewFM *stvfm = (SondTreeviewFM*) data;

	only_sel = (gboolean) GPOINTER_TO_INT(
			g_object_get_data( G_OBJECT(item), "sel" ));

	if (only_sel
			&& !gtk_tree_selection_count_selected_rows(
					gtk_tree_view_get_selection(GTK_TREE_VIEW(stvfm)))) {
		display_message(gtk_widget_get_toplevel(GTK_WIDGET(stvfm)),
				"Keine Punkte ausgewählt", NULL);
		return;
	}

	rc = abfrage_frage(gtk_widget_get_toplevel(GTK_WIDGET(stvfm)), "Dateisuche",
			"Bitte Suchtext eingeben", &search_text);
	if (rc != GTK_RESPONSE_YES)
		return;
	else if (!g_strcmp0(search_text, "")) {
		g_free(search_text);

		return;
	}

	search_fs.arr_hits = g_ptr_array_new_with_free_func(g_free);
	search_fs.exact_match = FALSE;
	search_fs.case_sens = FALSE;
	search_fs.atom_ready = &ready;
	search_fs.atom_cancelled = &cancelled;

	if (!search_fs.case_sens)
		search_fs.needle = g_utf8_strdown(search_text, -1);
	else
		search_fs.needle = g_strdup(search_text);

	g_free(search_text);

	search_fs.info_window = info_window_open(
			gtk_widget_get_toplevel(GTK_WIDGET(stvfm)),
			"Projektverzeichnis durchduchen");
/*
	if (only_sel)
		rc = sond_treeview_selection_foreach(SOND_TREEVIEW(stvfm),
				sond_treeviewfm_search, &search_fs, &errmsg);
	else */
	rc = sond_treeviewfm_search(SOND_TREEVIEW(stvfm), NULL, &search_fs,
				&error);

	info_window_kill(search_fs.info_window);

	g_free(search_fs.needle);

	if (rc == -1) {
		display_message(gtk_widget_get_toplevel(GTK_WIDGET(stvfm)),
				"Fehler bei Dateisuche\n\n", error->message, NULL);
		g_error_free(error);
		g_ptr_array_unref(search_fs.arr_hits);

		return;
	}

	if (search_fs.arr_hits->len == 0) {
		display_message(gtk_widget_get_toplevel(GTK_WIDGET(stvfm)),
				"Keine Datei gefunden", NULL);
		g_ptr_array_unref(search_fs.arr_hits);

		return;
	}

	sond_treeviewfm_show_hits(stvfm, search_fs.arr_hits);

	g_ptr_array_unref(search_fs.arr_hits);

	return;
}

static void sond_treeviewfm_init_contextmenu(SondTreeviewFM *stvfm) {
	GtkWidget *contextmenu = NULL;

	contextmenu = sond_treeview_get_contextmenu(SOND_TREEVIEW(stvfm));

	//Trennblatt
	GtkWidget *item_separator_0 = gtk_separator_menu_item_new();
	gtk_menu_shell_prepend(GTK_MENU_SHELL(contextmenu), item_separator_0);

	//Punkt einfügen
	GtkWidget *item_punkt_einfuegen = gtk_menu_item_new_with_label(
			"Punkt einfügen");

	GtkWidget *menu_punkt_einfuegen = gtk_menu_new();

	GtkWidget *item_punkt_einfuegen_ge = gtk_menu_item_new_with_label(
			"Gleiche Ebene");
	g_object_set_data(G_OBJECT(contextmenu), "item-punkt-einfuegen-ge",
			item_punkt_einfuegen_ge);
	g_signal_connect(G_OBJECT(item_punkt_einfuegen_ge), "activate",
			G_CALLBACK(sond_treeviewfm_punkt_einfuegen_activate),
			(gpointer ) stvfm);

	GtkWidget *item_punkt_einfuegen_up = gtk_menu_item_new_with_label(
			"Unterebene");
	g_object_set_data(G_OBJECT(contextmenu), "item-punkt-einfuegen-up",
			item_punkt_einfuegen_up);
	g_object_set_data(G_OBJECT(item_punkt_einfuegen_up), "kind",
			GINT_TO_POINTER(1));
	g_signal_connect(G_OBJECT(item_punkt_einfuegen_up), "activate",
			G_CALLBACK(sond_treeviewfm_punkt_einfuegen_activate),
			(gpointer ) stvfm);

	gtk_menu_shell_append(GTK_MENU_SHELL(menu_punkt_einfuegen),
			item_punkt_einfuegen_ge);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_punkt_einfuegen),
			item_punkt_einfuegen_up);

	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_punkt_einfuegen),
			menu_punkt_einfuegen);

	gtk_menu_shell_prepend(GTK_MENU_SHELL(contextmenu), item_punkt_einfuegen);

	//Einfügen
	GtkWidget *item_paste = gtk_menu_item_new_with_label("Einfügen");

	GtkWidget *menu_paste = gtk_menu_new();

	GtkWidget *item_paste_ge = gtk_menu_item_new_with_label("Gleiche Ebene");
	g_object_set_data(G_OBJECT(contextmenu), "item-paste-ge", item_paste_ge);
	g_signal_connect(G_OBJECT(item_paste_ge), "activate",
			G_CALLBACK(sond_treeviewfm_paste_activate), (gpointer ) stvfm);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_paste), item_paste_ge);

	GtkWidget *item_paste_up = gtk_menu_item_new_with_label("Unterebene");
	g_object_set_data(G_OBJECT(item_paste_up), "kind", GINT_TO_POINTER(1));
	g_object_set_data(G_OBJECT(contextmenu), "item-paste-up", item_paste_up);
	g_signal_connect(G_OBJECT(item_paste_up), "activate",
			G_CALLBACK(sond_treeviewfm_paste_activate), (gpointer ) stvfm);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_paste), item_paste_up);

	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_paste), menu_paste);

	gtk_menu_shell_append(GTK_MENU_SHELL(contextmenu), item_paste);

	//Punkt(e) löschen
	GtkWidget *item_loeschen = gtk_menu_item_new_with_label("Löschen");
	g_object_set_data(G_OBJECT(contextmenu), "item-loeschen", item_loeschen);
	g_signal_connect(G_OBJECT(item_loeschen), "activate",
			G_CALLBACK(sond_treeviewfm_loeschen_activate), (gpointer ) stvfm);
	gtk_menu_shell_append(GTK_MENU_SHELL(contextmenu), item_loeschen);

	//Trennblatt
	GtkWidget *item_separator_1 = gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(contextmenu), item_separator_1);

	//Datei Öffnen
	GtkWidget *item_datei_oeffnen = gtk_menu_item_new_with_label("Öffnen");
	g_object_set_data(G_OBJECT(contextmenu), "item-datei-oeffnen",
			item_datei_oeffnen);
	g_signal_connect(item_datei_oeffnen, "activate",
			G_CALLBACK(sond_treeviewfm_datei_oeffnen_activate),
			(gpointer ) stvfm);
	gtk_menu_shell_append(GTK_MENU_SHELL(contextmenu), item_datei_oeffnen);

	//Datei Öffnen mit
	GtkWidget *item_datei_oeffnen_mit = gtk_menu_item_new_with_label(
			"Öffnen mit");
	g_object_set_data(G_OBJECT(contextmenu), "item-datei-oeffnen-mit",
			item_datei_oeffnen_mit);
	g_object_set_data(G_OBJECT(item_datei_oeffnen_mit), "open-with",
			GINT_TO_POINTER(1));
	g_signal_connect(item_datei_oeffnen_mit, "activate",
			G_CALLBACK(sond_treeviewfm_datei_oeffnen_mit_activate),
			(gpointer ) stvfm);
	gtk_menu_shell_append(GTK_MENU_SHELL(contextmenu), item_datei_oeffnen_mit);

	//In Projektverzeichnis suchen
	GtkWidget *item_search = gtk_menu_item_new_with_label("Dateisuche");

	GtkWidget *menu_search = gtk_menu_new();

	GtkWidget *item_search_all = gtk_menu_item_new_with_label(
			"Gesamtes Verzeichnis");
	g_object_set_data(G_OBJECT(contextmenu), "item-search-all",
			item_search_all);
	g_signal_connect(G_OBJECT(item_search_all), "activate",
			G_CALLBACK(sond_treeviewfm_search_activate), (gpointer ) stvfm);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_search), item_search_all);

	GtkWidget *item_search_sel = gtk_menu_item_new_with_label(
			"Nur markierte Punkte");
	g_object_set_data(G_OBJECT(item_search_sel), "sel", GINT_TO_POINTER(1));
	g_object_set_data(G_OBJECT(contextmenu), "item-search-sel",
			item_search_sel);
	g_signal_connect(G_OBJECT(item_search_sel), "activate",
			G_CALLBACK(sond_treeviewfm_search_activate), (gpointer ) stvfm);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu_search), item_search_sel);

	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_search), menu_search);

	gtk_menu_shell_append(GTK_MENU_SHELL(contextmenu), item_search);

	gtk_widget_show_all(contextmenu);

	return;
}

static void sond_treeviewfm_row_activated(GtkTreeView *tree_view,
		GtkTreePath *tree_path, gpointer column, gpointer data) {
	gint rc = 0;
	GError *error = NULL;
	GtkTreePath *path = NULL;
	SondTVFMItem *stvfm_item = NULL;
	GtkTreeIter iter = { 0 };
	gboolean open_with = (gboolean) GPOINTER_TO_INT(data);

	if (tree_path)
		path = gtk_tree_path_copy(tree_path);
	else
		gtk_tree_view_get_cursor(tree_view, &path, NULL);

	gtk_tree_model_get_iter(gtk_tree_view_get_model(tree_view), &iter, path);
	gtk_tree_path_free(path);
	gtk_tree_model_get(gtk_tree_view_get_model(tree_view), &iter, 0, &stvfm_item, -1);

	rc = sond_treeviewfm_open(SOND_TREEVIEWFM(tree_view),
			stvfm_item, open_with, &error);
	g_object_unref(stvfm_item);
	if (rc) {
		display_message(gtk_widget_get_toplevel(GTK_WIDGET(tree_view)),
				"Datei kann nicht geöffnet werden\n\n", error->message, NULL);
		g_error_free(error);
	}

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

		size = g_file_info_get_size(G_FILE_INFO(object));

		text = g_strdup_printf("%lld", size);

		g_object_set(G_OBJECT(renderer), "text", text, NULL);
		g_free(text);
	}

	g_object_unref(object);

	return;
}

static void sond_treeviewfm_render_file_icon(GtkTreeViewColumn *column,
		GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter,
		gpointer data) {
	SondTVFMItem* stvfm_item = NULL;
	gchar const* icon_name = NULL;

	gtk_tree_model_get(model, iter, 0, &stvfm_item, -1);
	if (!stvfm_item)
		return;

	icon_name = sond_tvfm_item_get_icon_name(stvfm_item);

	if (icon_name)
		g_object_set(G_OBJECT(renderer), "icon-name", icon_name, NULL);
	else
		g_object_set(G_OBJECT(renderer), "icon-name", "image-missing", NULL);

	g_object_unref(stvfm_item);

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
		gchar **errmsg) {
	gint rc = 0;
	GError *error = NULL;
	SondFilePartClass* sfp_class = NULL;
	SondTVFMItem* stvfm_item = NULL;

	SondTreeviewFMPrivate *stvfm_priv = sond_treeviewfm_get_instance_private(
			stvfm);

	g_free(stvfm_priv->root);

	if (!root) {
		stvfm_priv->root = NULL;

		sfp_class = g_type_class_peek_static(SOND_TYPE_FILE_PART);
		g_free(sfp_class->path_root);
		sfp_class->path_root = NULL;

		gtk_tree_store_clear(
				GTK_TREE_STORE(
						gtk_tree_view_get_model( GTK_TREE_VIEW(stvfm) )));

		return 0;
	}

	stvfm_priv->root = g_strdup(root);

	sfp_class = g_type_class_peek_static(SOND_TYPE_FILE_PART);
	sfp_class->path_root = g_strdup(root);

	stvfm_item = sond_tvfm_item_create(stvfm, SOND_TVFM_ITEM_TYPE_DIR, NULL, NULL);

	rc = sond_treeviewfm_expand_dummy(stvfm, NULL, stvfm_item, &error);
	g_object_unref(stvfm_item);
	if (rc) {
		g_free(stvfm_priv->root);
		stvfm_priv->root = NULL;
		if (errmsg)
			*errmsg = g_strconcat("Bei Aufruf expand_dummy:\n",
					error->message, NULL);
		g_error_free(error);

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

void sond_treeviewfm_column_eingang_set_visible(SondTreeviewFM *stvfm,
		gboolean vis) {
	SondTreeviewFMPrivate *stvfm_priv = sond_treeviewfm_get_instance_private(
			stvfm);

	gtk_tree_view_column_set_visible(stvfm_priv->column_eingang, vis);

	return;
}

gchar*
sond_treeviewfm_get_full_path(SondTreeviewFM *stvfm, GtkTreeIter *iter) {
	gchar *full_path = NULL;
	gchar *rel_path = NULL;

	SondTreeviewFMPrivate *stvfm_priv = sond_treeviewfm_get_instance_private(
			stvfm);

	rel_path = sond_treeviewfm_get_rel_path(stvfm, iter);
	if (!rel_path)
		return NULL;

	full_path = add_string(g_strconcat(stvfm_priv->root, "/", NULL), rel_path);

	return full_path;
}

gchar*
sond_treeviewfm_get_filepart(SondTreeviewFM* stvfm, GtkTreeIter* iter) {
	SondTVFMItem* stvfm_item = NULL;
	SondFilePart* sfp = NULL;

	gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)), iter, 0, &stvfm_item, -1);
	sfp = sond_tvfm_item_get_sond_file_part(stvfm_item);
	g_object_unref(stvfm_item);

	return sond_file_part_get_filepart(sfp);
}

gchar*
sond_treeviewfm_get_rel_path(SondTreeviewFM *stvfm, GtkTreeIter *iter) {
	gchar *rel_path = NULL;
	GtkTreeIter iter_parent = { 0 };
	GtkTreeIter *iter_seg = NULL;
	GObject *object = NULL;
	gboolean datei = FALSE;

	if (!iter)
		return NULL;

	GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm));

	gtk_tree_model_get(model, iter, 0, &object, -1);
	datei = G_IS_FILE_INFO(object);
	g_object_unref(object);
	if (!datei)
		return NULL;

	iter_seg = gtk_tree_iter_copy(iter);

	rel_path = g_strdup(sond_treeviewfm_get_name(stvfm, iter_seg));

	while (gtk_tree_model_iter_parent(model, &iter_parent, iter_seg)) {
		gchar *path_segment = NULL;

		gtk_tree_iter_free(iter_seg);
		iter_seg = gtk_tree_iter_copy(&iter_parent);

		path_segment = g_strdup(sond_treeviewfm_get_name(stvfm, iter_seg));

		rel_path = add_string(g_strdup("/"), rel_path);
		rel_path = add_string(path_segment, rel_path);
	}

	gtk_tree_iter_free(iter_seg);

	return rel_path;
}

