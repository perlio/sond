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
#include "sond_renderer.h"
#include "sond_fileparts.h"
#include "sond_pdf_helper.h"


//SOND_TREEVIEWDM
typedef struct {
	gchar *root;
	GtkTreeViewColumn *column_eingang;
} SondTreeviewFMPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondTreeviewFM, sond_treeviewfm, SOND_TYPE_TREEVIEW)

//SOND_TVFM_ITEM
typedef struct {
	SondTreeviewFM* stvfm;
	gchar*display_name;
	gchar const* icon_name;
	gboolean has_children;
	SondTVFMItemType type;
	SondFilePart* sond_file_part;
	gchar* path_or_section;
} SondTVFMItemPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondTVFMItem, sond_tvfm_item, G_TYPE_OBJECT)

static void sond_tvfm_item_finalize(GObject *self) {
	SondTVFMItemPrivate *sond_tvfm_item_priv =
			sond_tvfm_item_get_instance_private(SOND_TVFM_ITEM(self));

	g_free(sond_tvfm_item_priv->display_name);

	//sfp-Type oder root
	if (sond_tvfm_item_priv->sond_file_part)
		g_object_unref(sond_tvfm_item_priv->sond_file_part);

	g_free(sond_tvfm_item_priv->path_or_section);

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

gchar const* sond_tvfm_item_get_display_name(SondTVFMItem *stvfm_item) {
	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	return stvfm_item_priv->display_name;
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

gchar const* sond_tvfm_item_get_icon_name(SondTVFMItem* stvfm_item) {
	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	return stvfm_item_priv->icon_name;
}

void sond_tvfm_item_set_icon_name(SondTVFMItem* stvfm_item,
		gchar const* icon_name) {
	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	stvfm_item_priv->icon_name = icon_name;

	return;
}

static gchar const* sond_tvfm_item_get_basename(SondTVFMItem* stvfm_item) {
	gchar const* path = NULL;
	gchar const* basename = NULL;

	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	if (stvfm_item_priv->path_or_section)
		path = stvfm_item_priv->path_or_section;
	else if (stvfm_item_priv->sond_file_part)
		path = sond_file_part_get_path(stvfm_item_priv->sond_file_part);
	else
		return NULL;

	basename = strrchr(path, '/');

	if (basename)
		basename++; //nach dem '/'
	else
		basename = path; //kein '/', also kompletter Pfad ist der Basename

	return basename;
}

static gint sond_tvfm_item_load_fs_dir(SondTVFMItem*, GPtrArray**, GError**);
static gint sond_tvfm_item_load_zip_dir(SondTVFMItem*, GPtrArray**, GError**);

char const* mime_type_to_icon_name_manual(const char *mime_type)
{
    if (!mime_type)
        return g_strdup("text-x-generic");

    // Exakte Matches
    static const struct {
        const char *mime;
        const char *icon;
    } mime_map[] = {
        // Dokumente
        {"application/pdf", "pdf"},
        {"application/vnd.oasis.opendocument.text", "x-office-document"},
        {"application/msword", "x-office-document"},
        {"application/vnd.openxmlformats-officedocument.wordprocessingml.document", "x-office-document"},

        // Tabellen
        {"application/vnd.oasis.opendocument.spreadsheet", "x-office-spreadsheet"},
        {"application/vnd.ms-excel", "x-office-spreadsheet"},
        {"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", "x-office-spreadsheet"},

        // Präsentationen
        {"application/vnd.oasis.opendocument.presentation", "x-office-presentation"},
        {"application/vnd.ms-powerpoint", "x-office-presentation"},

        // Bilder
        {"image/png", "image-x-generic"},
        {"image/jpeg", "image-x-generic"},
        {"image/gif", "image-x-generic"},
        {"image/svg+xml", "image-x-generic"},

        // Audio
        {"audio/mpeg", "audio-x-generic"},
        {"audio/ogg", "audio-x-generic"},
        {"audio/flac", "audio-x-generic"},

        // Video
        {"video/mp4", "video-x-generic"},
        {"video/x-matroska", "video-x-generic"},
        {"video/webm", "video-x-generic"},

        // Archive
        {"application/zip", "package-x-generic"},
        {"application/x-tar", "package-x-generic"},
        {"application/gzip", "package-x-generic"},
        {"application/x-7z-compressed", "package-x-generic"},
        {"application/x-rar", "package-x-generic"},

        // Text
        {"text/plain", "text-x-generic"},
        {"text/html", "text-html"},
        {"text/xml", "text-xml"},

        // Code
        {"text/x-c", "text-x-script"},
        {"text/x-python", "text-x-script"},
        {"text/x-java", "text-x-script"},
        {"application/javascript", "text-x-script"},

        {NULL, NULL}
    };

    // Exakte Suche
    for (int i = 0; mime_map[i].mime; i++) {
        if (strcmp(mime_type, mime_map[i].mime) == 0) {
            return mime_map[i].icon;
        }
    }

    // Prefix-basierte Suche
    if (g_str_has_prefix(mime_type, "text/")) {
        return "text-x-generic";
    }
    if (g_str_has_prefix(mime_type, "image/")) {
        return "image-x-generic";
    }
    if (g_str_has_prefix(mime_type, "audio/")) {
        return "audio-x-generic";
    }
    if (g_str_has_prefix(mime_type, "video/")) {
        return "video-x-generic";
    }
    if (g_str_has_prefix(mime_type, "application/")) {
        return "application-x-executable";
    }

    // Fallback
    return "text-x-generic";
}

SondTVFMItem* sond_tvfm_item_create(SondTreeviewFM* stvfm,
		SondFilePart *sond_file_part, gchar const* path_or_section) {
	SondTVFMItem *stvfm_item = NULL;
	SondTVFMItemPrivate *stvfm_item_priv = NULL;

	stvfm_item = g_object_new(SOND_TYPE_TVFM_ITEM, NULL);
	stvfm_item_priv = sond_tvfm_item_get_instance_private(stvfm_item);

	stvfm_item_priv->stvfm = stvfm;
	stvfm_item_priv->path_or_section = g_strdup(path_or_section);
	if (sond_file_part)
		stvfm_item_priv->sond_file_part = g_object_ref(sond_file_part);
	stvfm_item_priv->display_name =
			g_strdup(sond_tvfm_item_get_basename(stvfm_item));

	if (!sond_file_part) {
		gint rc = 0;
		GError* error = NULL;

		stvfm_item_priv->type = SOND_TVFM_ITEM_TYPE_DIR;

		//hat Verzeichnis Einträge?
		rc = sond_tvfm_item_load_fs_dir(stvfm_item, NULL, &error);
		if (rc == -1) {
			LOG_WARN("Fehler beim Öffnen des Verzeichnisses '%s':\n%s",
					(sond_tvfm_item_get_basename(stvfm_item)) ?
							sond_tvfm_item_get_basename(stvfm_item) :
							sond_treeviewfm_get_root(sond_tvfm_item_get_stvfm(stvfm_item)),
							error->message);

			g_error_free(error);
		}
		else
			if (rc == 1) stvfm_item_priv->has_children = TRUE;

		stvfm_item_priv->icon_name = "folder";
	}
	else {
		if (SOND_IS_FILE_PART_PDF(sond_file_part)) {
			if (sond_file_part_get_has_children(sond_file_part) &&
					!path_or_section) { //Marker für PageTree nicht gesetzt
				stvfm_item_priv->type = SOND_TVFM_ITEM_TYPE_DIR;
				stvfm_item_priv->has_children = TRUE;
				stvfm_item_priv->icon_name = "pdf-folder";
			}
			else {
				stvfm_item_priv->type = SOND_TVFM_ITEM_TYPE_LEAF;
				stvfm_item_priv->icon_name = "pdf";
				if (!g_strcmp0(path_or_section, "//")) { //Marker gesetzt
					g_free(stvfm_item_priv->path_or_section); //Marker löschen
					stvfm_item_priv->path_or_section = NULL;
					g_free(stvfm_item_priv->display_name); //Display-Name ersetzen
					stvfm_item_priv->display_name = g_strdup("PageTree");
				}
			}
		}
		else if (SOND_IS_FILE_PART_ZIP(sond_file_part)) {
			stvfm_item_priv->type = SOND_TVFM_ITEM_TYPE_DIR;
			stvfm_item_priv->has_children =
					sond_tvfm_item_load_zip_dir(stvfm_item, NULL, NULL) ?
							TRUE : FALSE;
			stvfm_item_priv->icon_name =(path_or_section) ?
					"folder" : "zip";
		}
		else if (SOND_IS_FILE_PART_GMESSAGE(sond_file_part)) {
			stvfm_item_priv->type = SOND_TVFM_ITEM_TYPE_DIR;
			stvfm_item_priv->has_children =
					sond_file_part_get_has_children(sond_file_part);
			stvfm_item_priv->icon_name = (path_or_section) ?
					"folder" : "mail-read";
		}
		else if (SOND_IS_FILE_PART_LEAF(sond_file_part)) {
			stvfm_item_priv->type = SOND_TVFM_ITEM_TYPE_LEAF;
			stvfm_item_priv->icon_name = mime_type_to_icon_name_manual(
					sond_file_part_leaf_get_mime_type(
							SOND_FILE_PART_LEAF(sond_file_part)));
		}
	}

	//section?
	if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF
			&& path_or_section) //Spezialfall "//" wurde oben schon weggefischt
		stvfm_item_priv->type = SOND_TVFM_ITEM_TYPE_LEAF_SECTION;

	//Spezialbehandlung für Sections
	if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF_SECTION ||
			stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF) {
		//Wenn type == SOND_TVFM_ITEM_TYPE_LEAF_SECTION:
		//stvfm_item_priv->icon_name muß in load_children gesetzt werden;
		if (SOND_TREEVIEWFM_GET_CLASS(stvfm)->has_sections)
			stvfm_item_priv->has_children =
					SOND_TREEVIEWFM_GET_CLASS(stvfm)->has_sections(stvfm_item);
	}

	return stvfm_item;
}

static gint sond_tvfm_item_load_fs_dir(SondTVFMItem* stvfm_item,
		GPtrArray** arr_children, GError **error) {
	GPtrArray* loaded_children = NULL;
	gboolean dir_has_children = FALSE;
	gchar* path_dir = NULL;
    GDir* dir = NULL;
    gchar const* filename = NULL;
    fz_context* ctx = NULL;

	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);
	SondTreeviewFMPrivate* stvfm_priv =
			sond_treeviewfm_get_instance_private(stvfm_item_priv->stvfm);

	path_dir = g_strconcat(stvfm_priv->root,
			"/", stvfm_item_priv->path_or_section, NULL);
    dir = g_dir_open(path_dir, 0, error);
    g_free(path_dir);
    if (!dir)
    	ERROR_Z

	ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
	if (!ctx) {
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nfz_context konnte nicht erzeugt werden", __func__);
		g_dir_close(dir);

		return -1;
	}

	if (arr_children)
		loaded_children = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);

    while ((filename = g_dir_read_name(dir)) != NULL) {
		SondTVFMItem* stvfm_item_child = NULL;
		gchar* rel_path_child = NULL;
		GStatBuf st = { 0 };

		dir_has_children = TRUE;

		if (!arr_children) //Es gibt Eintrag - reicht
    		break;

		if (stvfm_item_priv->path_or_section)
			rel_path_child = g_strconcat(stvfm_item_priv->path_or_section, "/",
					filename, NULL);
		else rel_path_child = g_strdup(filename);

		if (g_stat(rel_path_child, &st)) {
			LOG_WARN("g_stat(%s) gibt Fehler zurück: %s", rel_path_child, strerror(errno));
			g_free(rel_path_child);

			continue;
		}

		if (S_ISDIR(st.st_mode)) {
			//Verzeichnis
			stvfm_item_child =
					sond_tvfm_item_create(stvfm_item_priv->stvfm,
							stvfm_item_priv->sond_file_part, rel_path_child);
		}
		else {
			SondFilePart* sfp_child = NULL;
			fz_stream* stream = NULL;

			fz_try(ctx)
				stream = fz_open_file(ctx, rel_path_child);
			fz_catch(ctx) {
				fz_drop_context(ctx);
				g_free(rel_path_child);
				g_dir_close(dir);

				ERROR_PDF
			}

			sfp_child = sond_file_part_create_from_stream(ctx,
					stream, rel_path_child, stvfm_item_priv->sond_file_part, error);
			fz_drop_stream(ctx, stream);
			if (!sfp_child) {
				g_free(rel_path_child);
				g_dir_close(dir);
				fz_drop_context(ctx);

				ERROR_Z
			}

			stvfm_item_child = sond_tvfm_item_create(stvfm_item_priv->stvfm,
						sfp_child, NULL);
		}

		g_free(rel_path_child);

		g_ptr_array_add(loaded_children, stvfm_item_child);
    }

    g_dir_close(dir);
	fz_drop_context(ctx);

	if (arr_children) *arr_children = loaded_children;
	else if (dir_has_children) return 1;

	return 0;
}

static gint sond_tvfm_item_load_zip_dir(SondTVFMItem* stvfm_item,
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

	stvfm_item_pdf_page_tree =
			sond_tvfm_item_create(stvfm_item_priv->stvfm,
					stvfm_item_priv->sond_file_part, "//");
	g_ptr_array_add(*arr_children, stvfm_item_pdf_page_tree);

	for (guint i = 0; i < arr_emb_files->len; i++) {
		SondFilePart* sfp = NULL;
		SondTVFMItem* stvfm_item_child = NULL;

		sfp = g_ptr_array_index(arr_emb_files, i);

		stvfm_item_child =
				sond_tvfm_item_create(stvfm_item_priv->stvfm, sfp, NULL);

		g_ptr_array_add(*arr_children, stvfm_item_child);
	}

	g_ptr_array_unref(arr_emb_files);

	return 0;
}

static gint sond_tvfm_item_load_gmessage_dir(SondTVFMItem* stvfm_item,
		GPtrArray** arr_children, GError** error) {
	gint rc = 0;
	GPtrArray* arr_mimeparts = NULL;

	SondTVFMItemPrivate* stvfm_item_priv = sond_tvfm_item_get_instance_private(stvfm_item);

	rc = sond_file_part_gmessage_load_path(
			SOND_FILE_PART_GMESSAGE(stvfm_item_priv->sond_file_part),
			stvfm_item_priv->path_or_section,
			&arr_mimeparts, error);
	if (rc)
		ERROR_Z

	*arr_children = g_ptr_array_new();

	for (guint i = 0; i < arr_mimeparts->len; i++) {
		SondTVFMItem* stvfm_item_child = NULL;
		GMimeContentType* mime_type = NULL;
		gchar const* mime_string = NULL;
		GMimeObject* mime_child = NULL;
		gchar* path = NULL;
		gchar* base = NULL;
		gchar const* filename = NULL;
		SondTVFMItemPrivate* stvfm_item_child_priv = NULL;

		mime_child = g_ptr_array_index(arr_mimeparts, i);

		mime_type = g_mime_object_get_content_type(
				mime_child);
		mime_string = g_mime_content_type_get_mime_type(mime_type);

		base = g_strdup_printf("%u", i);
		path = g_strconcat(stvfm_item_priv->path_or_section ?
				stvfm_item_priv->path_or_section : "",
				stvfm_item_priv->path_or_section ? "/" : "",
				base, NULL);
		g_free(base);

		if (GMIME_IS_MULTIPART(mime_child))
			stvfm_item_child =
					sond_tvfm_item_create(stvfm_item_priv->stvfm,
							stvfm_item_priv->sond_file_part, path);
		else {
			SondFilePart* sfp_child = NULL;

			if (GMIME_IS_PART(mime_child))
				filename = g_mime_part_get_filename(GMIME_PART(mime_child));
			else { //GMimeMessagepart
				GMimeContentDisposition* disp = NULL;

				disp = g_mime_object_get_content_disposition(mime_child);

				if (disp)
					filename = g_mime_content_disposition_get_parameter(disp, "filename");
			}

			sfp_child = sond_file_part_create_from_mime_type(path,
					stvfm_item_priv->sond_file_part, mime_string);

			stvfm_item_child = sond_tvfm_item_create(stvfm_item_priv->stvfm,
						sfp_child, NULL);
		}

		g_free(path);

		stvfm_item_child_priv =
				sond_tvfm_item_get_instance_private(stvfm_item_child);
		g_free(stvfm_item_child_priv->display_name);
		stvfm_item_child_priv->display_name = filename ?
				g_strdup(filename) : g_strdup(mime_string);

		g_ptr_array_add(*arr_children, stvfm_item_child);
	}

	g_ptr_array_unref(arr_mimeparts);

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
			rc = sond_tvfm_item_load_fs_dir(stvfm_item, arr_children, error);
		else if(SOND_IS_FILE_PART_ZIP(stvfm_item_priv->sond_file_part))
			rc = sond_tvfm_item_load_zip_dir(stvfm_item, arr_children, error);
		else if (SOND_IS_FILE_PART_PDF(stvfm_item_priv->sond_file_part))
			rc = sond_tvfm_item_load_pdf_dir(stvfm_item, arr_children, error);
		else if(SOND_IS_FILE_PART_GMESSAGE(stvfm_item_priv->sond_file_part))
			rc = sond_tvfm_item_load_gmessage_dir(stvfm_item, arr_children, error);

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
		else {
			if (error) *error = g_error_new(g_quark_from_static_string("sond"), 0,
					"%s\nKeine Kinder vorhanden", __func__);

			return -1;
		}
	}

	return 0;
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

	stvfm_item_priv = sond_tvfm_item_get_instance_private(stvfm_item);

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
		SondTVFMItemPrivate* child_item_priv = NULL;

		child_item = g_ptr_array_index(arr_children, i);
		child_item_priv = sond_tvfm_item_get_instance_private(child_item);

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
		display_message(gtk_widget_get_toplevel(GTK_WIDGET(stvfm)),
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

/**
 * Ändert stvfm_item_priv->path_or_section
 */
static void sond_tvfm_item_set_basename(SondTVFMItem* stvfm_item,
		gchar const* new_basename) {
	gchar const* path = NULL;
	gchar const* dir = NULL;
	gchar* path_new = NULL;

	SondTVFMItemPrivate *stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	if (!stvfm_item_priv->path_or_section)
		LOG_WARN("STVFMItem ist Leaf oder root-dir ('%s')",
				sond_file_part_get_path(stvfm_item_priv->sond_file_part));

	path = stvfm_item_priv->path_or_section;

	dir = strrchr(path, '/');

	if (!dir)
		path_new = g_strdup(new_basename);
	else
		path_new = g_strdup_printf("%.*s/%s", (int)(dir - path), path, new_basename);

	g_free(stvfm_item_priv->path_or_section);
	stvfm_item_priv->path_or_section = g_strdup(path_new);

	return;
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

static gint rename_stvfm_item(SondTVFMItem* stvfm_item,
		SondTVFMItem* stvfm_item_parent, gchar const* base_new,
		GError** error) {
	g_autofree gchar* path_new = NULL;

	SondTVFMItemPrivate* stvfm_item_parent_priv =
			sond_tvfm_item_get_instance_private(stvfm_item_parent);
	SondTVFMItemPrivate* stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	path_new = g_strconcat((stvfm_item_parent_priv->path_or_section) ?
				stvfm_item_parent_priv->path_or_section : "",
				(stvfm_item_parent_priv->path_or_section) ? "/" : "",
						base_new, NULL);

	if (!stvfm_item_priv->path_or_section) {
		gint rc = 0;
		rc = sond_file_part_rename(stvfm_item_priv->sond_file_part, path_new, error);
		if (rc)
			ERROR_Z
	}
	else { //richtiges Verzeichnis, nicht LEAF oder Root-Dir (=LEAF)
		//Normale Dateien
		if (!stvfm_item_priv->sond_file_part) {
			gchar const* path_old = NULL;
			g_autoptr(GFile) src = NULL;
			g_autoptr(GFile) dst = NULL;

			path_old = (stvfm_item_priv->path_or_section) ?
					stvfm_item_priv->path_or_section :
					sond_file_part_get_path(stvfm_item_priv->sond_file_part);

			src = g_file_new_for_path(path_old);
			dst = g_file_new_for_path(path_new);

			if (!g_file_move(src, dst,
							G_FILE_COPY_OVERWRITE,
							NULL, NULL, NULL,
							error)) {
				if (g_error_matches(*error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
					g_clear_error(error);
					*error = g_error_new(SOND_ERROR, SOND_ERROR_EXISTS,
							"%s\nZielverzeichnis existiert bereits", __func__);

					return -1;
				}
				if (g_error_matches(*error, G_IO_ERROR, G_IO_ERROR_BUSY)) {
					g_clear_error(error);
					*error = g_error_new(SOND_ERROR, SOND_ERROR_BUSY,
							"%s\nDatei busy", __func__);

					return -1;
				}
				else
					ERROR_Z
			}

			sond_tvfm_item_set_basename(stvfm_item, path_new);
		}
		else if (SOND_IS_FILE_PART_ZIP(stvfm_item_priv->sond_file_part)) {
			//ToDo: zip-Verzeichnis-Namen ändern
			if (error) *error = g_error_new(g_quark_from_static_string("sond"), 0,
					"%s\nrename zip-dir noch nicht implementiert", __func__);

			return -1;
		}
		else if (SOND_IS_FILE_PART_GMESSAGE(stvfm_item_priv->sond_file_part)) {
			//ToDo: Multipart umbenennen
			if (error) *error = g_error_new(g_quark_from_static_string("sond"), 0,
					"%s\nrename GMimeMultipart noch nicht implementiert", __func__);
		}
		//was anderes?
		else {
			if (error) *error = g_error_new(g_quark_from_static_string("sond"), 0,
					"%s\nNicht implementiert", __func__);

			return -1;
		}
	}

	return 0;
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

static gint sond_treeviewfm_text_edited(SondTreeviewFM *stvfm,
		GtkTreeIter *iter, SondTVFMItem *stvfm_item, const gchar *text_new,
		GError **error) {
	gint rc = 0;
	GtkTreeIter iter_parent = { 0 };
	SondTVFMItem* stvfm_item_parent = NULL;
	SondTVFMItemPrivate* stvfm_item_priv = NULL;
	gint res = 0;

	if (!is_valid_filename(text_new))
		return 0;

	if (!gtk_tree_model_iter_parent(gtk_tree_view_get_model(
			GTK_TREE_VIEW(stvfm)), &iter_parent, iter))
		stvfm_item_parent =
				sond_tvfm_item_create(stvfm, NULL, NULL);
	else
		gtk_tree_model_get(gtk_tree_view_get_model(
				GTK_TREE_VIEW(stvfm)), &iter_parent, 0, &stvfm_item_parent, -1);

	stvfm_item_priv = sond_tvfm_item_get_instance_private(stvfm_item);

	g_signal_emit(stvfm_item_priv->stvfm,
			SOND_TREEVIEWFM_GET_CLASS(stvfm_item_priv->stvfm)->signal_before_move,
			0, stvfm_item, stvfm_item_parent, text_new, 0, error, &res);

	if (res) {
		g_object_unref(stvfm_item_parent);
		ERROR_Z
	}

	rc = rename_stvfm_item(stvfm_item, stvfm_item_parent, text_new, error);
	g_object_unref(stvfm_item_parent);
	g_signal_emit(stvfm_item_priv->stvfm,
			SOND_TREEVIEWFM_GET_CLASS(stvfm_item_priv->stvfm)->signal_after, 0,
			(rc == 0) ? TRUE : FALSE);
	if (rc)
		ERROR_Z

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

static gint sond_treeviewfm_open_stvfm_item(SondTVFMItem* stvfm_item,
		gboolean open_with, GError** error) {
	gint rc = 0;
	SondTVFMItemPrivate *stvfm_item_priv = sond_tvfm_item_get_instance_private(stvfm_item);

	rc = sond_file_part_open(stvfm_item_priv->sond_file_part, open_with, error);
	if (rc)
		ERROR_Z

	return 0;
}

static void sond_treeviewfm_class_init(SondTreeviewFMClass *klass) {
	G_OBJECT_CLASS(klass)->finalize = sond_treeviewfm_finalize;
	G_OBJECT_CLASS(klass)->constructed = sond_treeviewfm_constructed;

	SOND_TREEVIEW_CLASS(klass)->render_text_cell =
			sond_treeviewfm_render_text_cell;

	klass->signal_before_move = g_signal_new("before-move",
			SOND_TYPE_TREEVIEWFM, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_INT, 5,
			SOND_TYPE_TVFM_ITEM,
			SOND_TYPE_TVFM_ITEM,
			G_TYPE_CHAR,
			G_TYPE_INT,
			G_TYPE_POINTER);

	klass->signal_before_insert = g_signal_new("before-insert",
			SOND_TYPE_TREEVIEWFM, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_INT, 5,
			SOND_TYPE_TVFM_ITEM,
			SOND_TYPE_TVFM_ITEM,
			G_TYPE_CHAR,
			G_TYPE_INT,
			G_TYPE_POINTER);

	klass->signal_before_delete = g_signal_new("before-delete",
			SOND_TYPE_TREEVIEWFM, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_INT, 2,
			SOND_TYPE_TVFM_ITEM,
			G_TYPE_POINTER);

	klass->signal_after = g_signal_new("after",
			SOND_TYPE_TREEVIEWFM, G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1,
			G_TYPE_BOOLEAN);

	klass->text_from_section = NULL;
	klass->deter_background = NULL;
	klass->text_edited = sond_treeviewfm_text_edited;
	klass->results_row_activated = sond_treeviewfm_results_row_activated;
	klass->open_stvfm_item = sond_treeviewfm_open_stvfm_item;

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

		g_autoptr(GFile) dir = g_file_new_for_path(trial_path);

		suc = g_file_make_directory(dir, NULL, error);
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
				ERROR_Z
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

	stvfm_item_parent_priv = sond_tvfm_item_get_instance_private(stvfm_item_parent);

	if (stvfm_item_parent_priv->type != SOND_TVFM_ITEM_TYPE_DIR)
		return 0; //Wenn etwas anderes als in dir - nix machen

	if (!stvfm_item_parent_priv->sond_file_part)
		rc = insert_dir_in_fs(stvfm_item_parent_priv, &path, error);
//	else if (SOND_IS_FILE_PART_PDF(stvfm_item_parent_priv->sond_file_part))
//		rc = insert_dir_in_pdf(stvfm_item_priv, child, &path, error);
	else if (SOND_IS_FILE_PART_ZIP(stvfm_item_parent_priv->sond_file_part))
		rc = insert_dir_in_zip(stvfm_item_parent_priv, child, &path, error);
	if (rc)
		ERROR_Z

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
static gint copy_path(const gchar *src_path,
                        const gchar *dest_dir,
					   GError     **error)
{
	g_autoptr(GFile) src = g_file_new_for_path(src_path);
	g_autoptr(GFile) dst = g_file_new_for_path(dest_dir);

	// Nur Top-Level prüft Suffix
	if (!g_file_make_directory(dst, NULL, error)) {
		if (g_error_matches(*error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
			g_clear_error(error);
			*error = g_error_new(SOND_ERROR, SOND_ERROR_EXISTS,
					"%s\nZielverzeichnis existiert bereits", __func__);

			return -1;
		}
	}

	//jetzt rekursiv kopieren
	if (!copy_directory_contents(src, dst, error)) {
		g_file_delete(dst, NULL, NULL);
		ERROR_Z;
	}

	return 0;
}

static gint copy_dir_across_sfps(SondTVFMItem* stvfm_item,
		SondTVFMItem* stvfm_item_parent, gchar const* base,
		GError** error) {
	{
		if (error) *error = g_error_new(SOND_ERROR, 0,
			"%s\nKopieren eines Verzeichnisses in anderen SondFilePart-Typen"
				"noch nicht implementiert", __func__);

		return -1;
	}

	return 0;
}

static gint copy_stvfm_item(SondTVFMItem* stvfm_item,
		SondTVFMItem* stvfm_item_parent, gchar const* base,
		gint index_to, GError** error) {
	gint rc = 0;
	gchar* base_real = NULL;

	SondTVFMItemPrivate* stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);
	SondTVFMItemPrivate* stvfm_item_parent_priv =
			sond_tvfm_item_get_instance_private(stvfm_item_parent);

	base_real = (stvfm_item_parent_priv->sond_file_part &&
			SOND_IS_FILE_PART_GMESSAGE(stvfm_item_parent_priv->sond_file_part)) ?
					g_strdup_printf("%u", index_to) :
					g_strdup(base);

	//sfp soll kopiert werden
	if (!stvfm_item_priv->path_or_section) {
		gchar* path = NULL;

		//Page-Tree einer PDF-Datei: geht (noch) nicht
		if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF &&
				SOND_IS_FILE_PART_PDF(stvfm_item_priv->sond_file_part) &&
				sond_file_part_get_has_children(
						stvfm_item_priv->sond_file_part)) {

			//ToDo: Sonderbehandlung für PDF-Dateien (nur page_tree)
			if (error) *error = g_error_new(g_quark_from_static_string("sond"), 0,
					"%s\nKopieren des Page-Tree aus PDF-Dateien nicht implementiert",
					__func__);
			g_free(base_real);

			return -1;
		}

		//Wenn in ein dir kopiert wird, ist der Pfad des dir vom übergeordneten sfp
		//der Beginn des neuen Pfades des sfp
		path = stvfm_item_parent_priv->path_or_section ?
				g_strconcat(stvfm_item_parent_priv->path_or_section, "/", base_real, NULL) :
				g_strdup(base_real);
		g_free(base_real);

		rc = sond_file_part_copy(stvfm_item_priv->sond_file_part,
				stvfm_item_parent_priv->sond_file_part, path, error);
		g_free(path);
	}
	else { //dir soll kopiert werden
		//innerhalt Dateisystem - geht schon
		if (!stvfm_item_parent_priv->sond_file_part &&
			(!stvfm_item_priv->sond_file_part ||
					!sond_file_part_get_parent(stvfm_item_priv->sond_file_part))) {
			gchar* path_dst = NULL;

			path_dst = g_strconcat((stvfm_item_parent_priv->path_or_section) ?
							stvfm_item_parent_priv->path_or_section : "",
							(stvfm_item_parent_priv->path_or_section) ? "/" : "",
							base_real, NULL);
			g_free(base_real);

			rc = copy_path(stvfm_item_priv->path_or_section, path_dst, error);
			g_free(path_dst);
		}
		else { //kopieren Verzeichnis zwischen zwei sfp-Welten
			rc = copy_dir_across_sfps(stvfm_item, stvfm_item_parent,
					base_real, error);
			g_free(base_real);
		}
	}
	if (rc)
		ERROR_Z

	return 0;
}

static void remove_item_from_tree(GtkTreeIter* iter,
		SondTVFMItem* stvfm_item) {
	gboolean is_gmessage = FALSE;

	SondTVFMItemPrivate* stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

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

			stvfm_item_parent_priv = sond_tvfm_item_get_instance_private(
					stvfm_item_parent);
			stvfm_item_parent_priv->type = SOND_TVFM_ITEM_TYPE_LEAF;
			stvfm_item_parent_priv->has_children = FALSE;
			stvfm_item_parent_priv->icon_name = "pdf";
			g_object_unref(stvfm_item_parent);

			end:
			;
		}
	}

	//jetzt auf Zielknoten löschen
	if (gtk_tree_store_remove(
			GTK_TREE_STORE(gtk_tree_view_get_model(
					GTK_TREE_VIEW(stvfm_item_priv->stvfm))), iter)) {
		//Leider, wenn GMessage, die Pfade anpassen
		if (is_gmessage)
			do {
				SondTVFMItem* stvfm_item_sibling = NULL;
				SondTVFMItemPrivate* stvfm_item_sibling_priv = NULL;
				gchar const* path_old = NULL;
				gchar* path_new = NULL;
				gint index = 0;

				gtk_tree_model_get(gtk_tree_view_get_model(
					GTK_TREE_VIEW(stvfm_item_priv->stvfm)), iter, 0,
						&stvfm_item_sibling, -1);
				stvfm_item_sibling_priv =
						sond_tvfm_item_get_instance_private(stvfm_item_sibling);
				g_object_unref(stvfm_item_sibling);

				path_old = (stvfm_item_sibling_priv->path_or_section) ?
						stvfm_item_sibling_priv->path_or_section :
						sond_file_part_get_path(stvfm_item_sibling_priv->sond_file_part);

				if (strrchr(path_old, '/')) {
					gchar const* base_old = NULL;
					gchar* dir = NULL;

					base_old = strrchr(path_old, '/');
					index = atoi(base_old);

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
					GTK_TREE_VIEW(stvfm_item_priv->stvfm)), iter));
	}

	return;
}

static gint delete_item(SondTVFMItem* stvfm_item, GError** error) {
	SondTVFMItemPrivate* stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

	if (!stvfm_item_priv->path_or_section) {
		gint rc = 0;

		rc = sond_file_part_delete(stvfm_item_priv->sond_file_part, error);
		if (rc)
			ERROR_Z
	}
	else {
		if (!stvfm_item_priv->sond_file_part) { //ist dir im Dateisystem
			gint rc = 0;

			rc = rm_r(stvfm_item_priv->path_or_section);
			if (rc) {
				if (error) *error = g_error_new(g_quark_from_static_string("stdlib"),
						errno, "%s\n%s", __func__, strerror(errno));

				return -1;
			}
		}
		else { //zip- oder pdf-dir
			if (error) *error = g_error_new(SOND_ERROR, 0,
					"%s\nNoch nicht implementiert", __func__);

			return -1;
		}
	}

	return 0;
}

static gint move_item(SondTVFMItem* stvfm_item_src,
		SondTVFMItem* stvfm_item_parent_dst,
		gchar const* base, gint index_to, GError** error) {
	gint rc = 0;

	rc = copy_stvfm_item(stvfm_item_src,
			stvfm_item_parent_dst, base, index_to, error);
	if (rc)
		ERROR_Z

	//Jetzt Quelle löschen
	rc = delete_item(stvfm_item_src, error);
	if (rc) {
		LOG_WARN("Item konnte nicht gelöscht werden: %s", (*error)->message);
		g_clear_error(error);
	}

	return 0;
}

static gint move_stvfm_item(SondTVFMItem* stvfm_item,
		SondTVFMItem* stvfm_item_parent, gchar const* base,
		gint index_to, GError** error) {
	gint rc = 0;
	gint res = 0;

	SondTVFMItemPrivate* stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);
	SondTVFMItemPrivate* stvfm_item_parent_priv =
			sond_tvfm_item_get_instance_private(stvfm_item_parent);

	if (stvfm_item_priv->stvfm == stvfm_item_parent_priv->stvfm)
		g_signal_emit(stvfm_item_priv->stvfm,
				SOND_TREEVIEWFM_GET_CLASS(stvfm_item_priv->stvfm)->signal_before_move, 0,
				stvfm_item, stvfm_item_parent, base, index_to, error, &res);
	else {
		g_signal_emit(stvfm_item_priv->stvfm,
				SOND_TREEVIEWFM_GET_CLASS(stvfm_item_priv->stvfm)->signal_before_delete, 0,
				stvfm_item, error, &res);
		if (res == -1)
			ERROR_Z
		else if (res == 1)
			return 1;

		g_signal_emit(stvfm_item_parent_priv->stvfm,
				SOND_TREEVIEWFM_GET_CLASS(stvfm_item_parent_priv->stvfm)->signal_before_insert, 0,
				stvfm_item, stvfm_item_parent, base, index_to, error, &res);
	}

	if (res)
		ERROR_Z

	if (stvfm_item_parent_priv->sond_file_part ==
			((stvfm_item_priv->sond_file_part) ?
					sond_file_part_get_parent(stvfm_item_priv->sond_file_part)
					: NULL) &&
					!SOND_IS_FILE_PART_GMESSAGE(stvfm_item_parent_priv->sond_file_part))
		//Verschieben innerhalb des gleichen sfp, das aber nicht GMessage ist
		rc = rename_stvfm_item(stvfm_item, stvfm_item_parent, base, error);
	else
		rc = move_item(stvfm_item, stvfm_item_parent, base, index_to, error);

	g_signal_emit(stvfm_item_parent_priv->stvfm,
			SOND_TREEVIEWFM_GET_CLASS(stvfm_item_parent_priv->stvfm)->signal_after, 0,
			(rc == 0) ? TRUE : FALSE);
	if (rc)
		ERROR_Z

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
			sond_tvfm_item_get_instance_private(stvfm_item);
	SondTVFMItemPrivate* stvfm_item_parent_priv =
			sond_tvfm_item_get_instance_private(s_paste_sel->stvfm_item_parent);

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
			rc = move_stvfm_item(stvfm_item,
					s_paste_sel->stvfm_item_parent, trial_base,
					s_paste_sel->index_to, error);
		else
			rc = copy_stvfm_item(stvfm_item,
					s_paste_sel->stvfm_item_parent, trial_base,
					s_paste_sel->index_to, error);

		if (rc == -1) {
			if (g_error_matches(*error, SOND_ERROR, SOND_ERROR_EXISTS)) {
				g_clear_error(error);
				i++;

				continue;  // nächster Suffix versuchen
			}
			else if (g_error_matches(*error, SOND_ERROR, SOND_ERROR_BUSY)) {
				g_clear_error(error);

				gint res = dialog_with_buttons(
						gtk_widget_get_toplevel(GTK_WIDGET(stvfm_item_priv->stvfm)),
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
				ERROR_Z
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
		g_set_error(error, SOND_ERROR, SOND_ERROR_EXISTS,
				"%s\nKein eindeutiger Zielname nach %u Versuchen", __func__, max_tries);

		return -1;
	}

	return 0;
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
			sond_tvfm_item_get_instance_private(s_paste_sel->stvfm_item_parent);

	clipboard = ((SondTreeviewClass*) g_type_class_peek(
			SOND_TYPE_TREEVIEW))->clipboard;

	gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(stv)), iter, 0,
			&stvfm_item, -1);
	stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);
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
		ERROR_Z
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

	if (stvfm_item_priv->sond_file_part) {
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
	g_object_unref(sfp_new);

	stvfm_item_new_priv = sond_tvfm_item_get_instance_private(stvfm_item_new);

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
		GtkTreeIter iter_sibling = *(s_paste_sel->iter_cursor);

		g_free(stvfm_item_new_priv->display_name);
		stvfm_item_new_priv->display_name = g_strdup(stvfm_item_priv->display_name);

		//Problem: path von etwaigen jüngeren Geschwistern
		//ist nach Einfügen nicht mehr korrekt
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
					sond_tvfm_item_get_instance_private(stvfm_item_sibling);
			g_object_unref(stvfm_item_sibling);

			if (strchr(stvfm_item_sibling_priv->path_or_section, '/')) {
				gchar const* base_old = NULL;
				gchar* dir = NULL;

				base_old = strrchr(stvfm_item_sibling_priv->path_or_section, '/');
				index = atoi(base_old);

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

	stvfm_item_parent_priv = sond_tvfm_item_get_instance_private(stvfm_item_parent);

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
		ERROR_Z

	//Cursor setzen
	sond_treeview_set_cursor(SOND_TREEVIEW(stvfm),
				s_paste_sel.iter_cursor);

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

static gint loesche_item(SondTVFMItem* stvfm_item, GtkTreeIter* iter, GError** error) {
	SondTVFMItemPrivate* stvfm_item_priv =
			sond_tvfm_item_get_instance_private(stvfm_item);

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
		else if (!stvfm_item_priv->path_or_section) { //sfp existiert - also root
			gint rc = 0;

			rc = sond_file_part_delete(stvfm_item_priv->sond_file_part, error);
			if (rc) {
				ERROR_Z
			}
		}
		else if (SOND_IS_FILE_PART_ZIP(stvfm_item_priv->sond_file_part)) {
			g_set_error(error, SOND_ERROR, 0, "%s\nnicht implementiert", __func__);

			return -1;
		}
		else if (SOND_IS_FILE_PART_PDF(stvfm_item_priv->sond_file_part)) { //PDF-Datei ist Dir - ganz löschen
			g_set_error(error, SOND_ERROR, 0, "%s\nnicht implementiert", __func__);

			return -1;
		}
		else if (SOND_IS_FILE_PART_GMESSAGE(stvfm_item_priv->sond_file_part)) {
			g_set_error(error, SOND_ERROR, 0, "%s\nnicht implementiert", __func__);

			return -1;
		}
	}
	else if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_LEAF) {
		gint rc = 0;

		if (SOND_IS_FILE_PART_PDF(stvfm_item_priv->sond_file_part) &&
				sond_file_part_get_has_children(stvfm_item_priv->sond_file_part)) {
			if (error) *error = g_error_new(g_quark_from_static_string("sond"), 0,
					"%s\nLöschen des pagetree aus PDF-Datei nicht unterstützt",
					__func__);

			return -1;
		}

		rc = sond_file_part_delete(stvfm_item_priv->sond_file_part, error);
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

	remove_item_from_tree(iter, stvfm_item);

	return 0;
}

static gint sond_treeviewfm_foreach_loeschen(SondTreeview *stv,
		GtkTreeIter *iter, gpointer data, GError **error) {
	SondTVFMItem* stvfm_item = NULL;
	gint res = 0;
	gint rc = 0;
	SondTreeviewFM* stvfm = SOND_TREEVIEWFM(stv);

	gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)), iter, 0,
			&stvfm_item, -1);
	g_object_unref(stvfm_item);

	g_signal_emit(stvfm, SOND_TREEVIEWFM_GET_CLASS(stvfm)->signal_before_delete,
			0, stvfm_item, error, &res);
	if (res == -1)
		ERROR_Z
	else if (res == 1)
		return 0;

	rc = loesche_item(stvfm_item, iter, error);
	g_signal_emit(stvfm, SOND_TREEVIEWFM_GET_CLASS(stvfm)->signal_after,
			0, (rc == 0) ? TRUE : FALSE);
	if (rc)
		ERROR_Z

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

static gint sond_treeviewfm_open(SondTVFMItem *stvfm_item,
		gboolean open_with, GError **error) {
	gint rc = 0;
	SondTVFMItemPrivate *stvfm_item_priv = sond_tvfm_item_get_instance_private(stvfm_item);

	if (stvfm_item_priv->type == SOND_TVFM_ITEM_TYPE_DIR)
		return 0;

	rc = SOND_TREEVIEWFM_GET_CLASS(stvfm_item_priv->stvfm)->
			open_stvfm_item(stvfm_item, open_with, error);
	if (rc)
		ERROR_Z

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

	rc = sond_treeviewfm_open(stvfm_item, open_with, &error);
	g_object_unref(stvfm_item);
	if (rc) {
		display_message(GTK_WIDGET(tree_view), "Fehler beim Öffnen\n\n",
				error->message, NULL);
		g_error_free(error);
	}

	return;
}

static void sond_treeviewfm_datei_oeffnen_activate(GtkMenuItem *item,
		gpointer data) {
	open_item(GTK_TREE_VIEW(data), FALSE);

	return;
}

static void sond_treeviewfm_datei_oeffnen_mit_activate(GtkMenuItem *item,
		gpointer data) {
	open_item(GTK_TREE_VIEW(data), TRUE);

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
			"Suchergebnis");

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
		SondTVFMItemPrivate* stvfm_item_priv = sond_tvfm_item_get_instance_private(stvfm_item);

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
					&arr_children, error);
			if (rc)
				ERROR_Z

			for (guint i = 0; i < arr_children->len; i++) {
				SondTVFMItem *child_item = (SondTVFMItem*) g_ptr_array_index(arr_children, i);

				rc = sond_treeviewfm_search_needle(child_item, data, error);
				if (rc)
					ERROR_Z

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
	GError **error = data_thread->error;
	gint rc = 0;

	rc = sond_treeviewfm_search_needle(data_thread->stvfm_item,
			data_thread->search_fs, data_thread->error);
	if (rc)
		ERROR_Z_VAL(GINT_TO_POINTER(-1))

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

	while (!g_atomic_int_get(search_fs->atom_ready)) {
		if (search_fs->info_window->cancel)
			g_atomic_int_set(search_fs->atom_cancelled, 1);
	}

	res_thread = g_thread_join(thread_search);
	g_object_unref(stvfm_item);
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
	if (rc != GTK_RESPONSE_YES || !g_strcmp0(search_text, "")) {
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

	search_fs.info_window = info_window_open(
			gtk_widget_get_toplevel(GTK_WIDGET(stvfm)),
			search_text);
	g_free(search_text);

	if (only_sel)
		rc = sond_treeview_selection_foreach(SOND_TREEVIEW(stvfm),
				sond_treeviewfm_search, &search_fs, &error);
	else
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
	SondTVFMItem *stvfm_item = NULL;
	GtkTreeIter iter = { 0 };

	gtk_tree_model_get_iter(gtk_tree_view_get_model(tree_view), &iter, tree_path);
	gtk_tree_model_get(gtk_tree_view_get_model(tree_view), &iter, 0, &stvfm_item, -1);

	rc = sond_treeviewfm_open(stvfm_item, FALSE, &error);
	g_object_unref(stvfm_item);
	if (rc) {
		display_message(gtk_widget_get_toplevel(GTK_WIDGET(tree_view)),
				"Datei kann nicht geöffnet werden\n\n", error->message, NULL);
		g_error_free(error);
	}

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
#ifdef __WIN32__
		text = g_strdup_printf("%lld", size);
#elif defined __linux__
		text = g_strdup_printf("%ld", size);
#endif
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
	SondTVFMItemPrivate* stvfm_item_priv = NULL;

	gtk_tree_model_get(model, iter, 0, &stvfm_item, -1);
	if (!stvfm_item) {
		LOG_WARN("%s: Kein SondTVFMItem", __func__);
		return;
	}

	stvfm_item_priv = sond_tvfm_item_get_instance_private(stvfm_item);
	g_object_unref(stvfm_item);

	if (stvfm_item_priv->icon_name)
		g_object_set(G_OBJECT(renderer), "icon-name", stvfm_item_priv->icon_name, NULL);
	else
		g_object_set(G_OBJECT(renderer), "icon-name", "image-missing", NULL);

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
	SondTVFMItem* stvfm_item = NULL;

	SondFilePartClass* sfp_class = g_type_class_peek_static(SOND_TYPE_FILE_PART);
	SondTreeviewFMPrivate *stvfm_priv = sond_treeviewfm_get_instance_private(
			stvfm);

	g_free(stvfm_priv->root);
	g_free(sfp_class->path_root);

	if (!root) {
		stvfm_priv->root = NULL;
		sfp_class->path_root = NULL;

		gtk_tree_store_clear(
				GTK_TREE_STORE(
						gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm) )));

		return 0;
	}

	stvfm_priv->root = g_strdup(root);
	sfp_class->path_root = g_strdup(root);

	//zum Arbeitsverzeichnis machen
	g_chdir(stvfm_priv->root);

	stvfm_item = sond_tvfm_item_create(stvfm, NULL, NULL);

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
