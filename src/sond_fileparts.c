/*
 sond (sond_fileparts.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2025  peloamerica

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

#include "sond_fileparts.h"

#include <glib-object.h>
#include <zip.h>

#include "misc.h"

#include "zond/global_types.h"

#include "zond/sond_treeviewfm.h"

//Grundlegendes Object, welches file_parts beschreibt
/*
 * parent: Objekt des Elternelements
 * 	NULL, wenn Datei in Filesystem
 * 	sfp_zip/sfp_pdf, wenn Objekt in zip-Archiv oder pdf-Datei gespeichert
 *
 * path: path zum Root-Element
 * 	abs-path, wenn parent == NULL
 * 	"/" für root-Verzeichnis eines zip-Archivs
 */
typedef struct {
	gchar *path; //rel_path zum root-Element
	SondFilePart* parent; //NULL, wenn in Datei rel_path
} SondFilePartPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondFilePart, sond_file_part, G_TYPE_OBJECT)

static void sond_file_part_finalize(GObject *self) {
	SondFilePartPrivate *sfp_priv =
			sond_file_part_get_instance_private(SOND_FILE_PART(self));

	g_free(sfp_priv->path);
	g_object_unref(sfp_priv->parent);

	G_OBJECT_CLASS(sond_file_part_parent_class)->finalize(self);

	return;
}

static void sond_file_part_class_init(SondFilePartClass *klass) {
	G_OBJECT_CLASS(klass)->finalize = sond_file_part_finalize;

	klass->get_children = NULL; //Default: keine Kinder
	klass->has_children = NULL;

	return;
}

static void sond_file_part_init(SondFilePart* self) {

	return;
}

SondFilePart* sond_file_part_create(GType sfp_type, const gchar *path,
		SondFilePart* parent) {
	SondFilePart *sfp = NULL;
	SondFilePartPrivate *sfp_priv = NULL;

	sfp = g_object_new(sfp_type, NULL);
	sfp_priv = sond_file_part_get_instance_private(sfp);

	sfp_priv->path = g_strdup(path);
	if (parent) sfp_priv->parent = SOND_FILE_PART(g_object_ref(parent));

	return sfp;
}

SondFilePart* sond_file_part_get_parent(SondFilePart *sfp) {
	SondFilePartPrivate *sfp_priv = sond_file_part_get_instance_private(sfp);

	return sfp_priv->parent;
}

gchar const* sond_file_part_get_path(SondFilePart *sfp) {
	SondFilePartPrivate *sfp_priv = sond_file_part_get_instance_private(sfp);

	return sfp_priv->path;
}

GPtrArray* sond_file_part_get_children(SondFilePart* sfp, GError **error) {
	GPtrArray *arr_children = NULL;

	if (SOND_FILE_PART_GET_CLASS(sfp)->get_children) {
		arr_children = SOND_FILE_PART_GET_CLASS(sfp)->get_children(sfp, error);
		if (!arr_children) {
			g_prefix_error(error, "%s\n", __func__);
			return NULL;
		}
	} else {
		if (error) *error = g_error_new(ZOND_ERROR, 0,
				"%s: get_children not implemented for %s",
				__func__, G_OBJECT_TYPE_NAME(sfp));
		return NULL;
	}

	return arr_children;
}

gboolean sond_file_part_has_children(SondFilePart *sfp, GError **error) {
	gboolean has_children = FALSE;

	if (SOND_FILE_PART_GET_CLASS(sfp)->has_children) {
		has_children = SOND_FILE_PART_GET_CLASS(sfp)->has_children(sfp, error);
		if (error && *error) { //Fehler zurückgegeben
			g_prefix_error(error, "%s\n", __func__);
			return FALSE;
		}
	}
//	else has children = FALSE;

	return has_children;
}

/*
 * Dirs
 */
typedef struct {
} SondFilePartDirPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondFilePartDir, sond_file_part_dir, SOND_TYPE_FILE_PART)

static void sond_file_part_dir_finalize(GObject *self) {
	SondFilePartDirPrivate *sfp_dir_priv =
			sond_file_part_dir_get_instance_private(SOND_FILE_PART_DIR(self));

	G_OBJECT_CLASS(sond_file_part_dir_parent_class)->finalize(self);

	return;
}

static void sond_file_part_dir_class_init(SondFilePartDirClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = sond_file_part_dir_finalize;

	SOND_FILE_PART_CLASS(klass)->get_children =
			sond_file_part_dir_get_children;
	SOND_FILE_PART_CLASS(klass)->has_children =
			sond_file_part_dir_has_children;

	return;
}

static void sond_file_part_dir_init(SondFilePartDir* self) {

	return;
}

GPtrArray* sond_file_part_dir_get_children(SondFilePart* sfp_dir, GError **error) {
	SondFilePartDirPrivate *sfp_dir_priv =
			sond_file_part_dir_get_instance_private(SOND_FILE_PART_DIR(sfp_dir));
	SondFilePart *sfp_parent = NULL;
	GPtrArray *arr_children = g_ptr_array_new_with_free_func(g_object_unref);
	gchar const* path = NULL;

	path = sond_file_part_get_path(sfp_dir);

	//Ist ein dir im Filesystem
	if ((sfp_parent = sond_file_part_get_parent(sfp_dir)) == NULL) {
		GFile* file_dir = NULL;
		GFileEnumerator *enumer = NULL;

		file_dir = g_file_new_for_path(path);

		enumer = g_file_enumerate_children(file_dir, "*", G_FILE_QUERY_INFO_NONE, NULL,
				error);
		if (!enumer) {
			g_object_unref(file_dir);
			g_ptr_array_unref(arr_children);
			g_prefix_error(error, "%s\n", __func__);

			return NULL;
		}

		while (1) {
			GFileInfo *info_child = NULL;
			gboolean res = FALSE;
			gchar const* content_type = NULL;
			SondFilePart *sfp_child = NULL;
			GType sfp_type = G_TYPE_NONE;
			gchar* rel_path_child = NULL;

			res = g_file_enumerator_iterate(enumer, &info_child, NULL, NULL, error);
			if (!res) {
				g_object_unref(enumer);
				g_object_unref(file_dir);
				g_ptr_array_unref(arr_children);
				g_prefix_error(error, "%s\n", __func__);

				return NULL;
			}

			if (!info_child) //keine weiteren Dateien
				break;

			rel_path_child = g_strconcat(path, "/",
					g_file_info_get_name(info_child), NULL);

			content_type = g_file_info_get_content_type(info_child);
			if (g_content_type_is_mime_type(content_type, "inode/directory"))
				sfp_child = sond_file_part_create(SOND_TYPE_FILE_PART_DIR, rel_path_child,
						NULL);
			else if (g_content_type_is_mime_type(content_type, "application/pdf"))
				sfp_child = sond_file_part_create(SOND_TYPE_FILE_PART_PDF, rel_path_child,
						NULL);
			else if (g_content_type_is_mime_type(content_type, "application/zip"))
				sfp_child = sond_file_part_create(SOND_TYPE_FILE_PART_ZIP, rel_path_child,
						NULL);
			else {//alles andere = leaf
				sfp_child = sond_file_part_create(SOND_TYPE_FILE_PART_LEAF, rel_path_child,
						NULL);
				sond_file_part_leaf_set_content_type(
						SOND_FILE_PART_LEAF(sfp_child), content_type);
			}

			g_free(rel_path_child);

			g_ptr_array_add(arr_children, sfp_child);
		}

		g_object_unref(enumer); //unreferenziert auch alle infos und gfiles
		g_object_unref(file_dir);
	}
	//oder dir in zip-Archiv?
	else if (SOND_IS_FILE_PART_ZIP(sfp_parent)) { //dir in zip

	}

	return arr_children;
}

gboolean sond_file_part_dir_has_children(SondFilePart* sfp_dir, GError **error) {
	SondFilePartDirPrivate *sfp_dir_priv =
			sond_file_part_dir_get_instance_private(SOND_FILE_PART_DIR(sfp_dir));
	gboolean has_children = FALSE;
	SondFilePart *sfp_parent = NULL;
	gchar const* path = NULL;

	path = sond_file_part_get_path(sfp_dir);

//Ist ein dir im Filesystem
	if ((sfp_parent = sond_file_part_get_parent(sfp_dir)) == NULL) {
		GFile* file_dir = NULL;
		GFileEnumerator *enumer = NULL;
		gboolean res = FALSE;
		GFile* file_child = NULL;

		file_dir = g_file_new_for_path(path);
		enumer = g_file_enumerate_children(file_dir, "*", G_FILE_QUERY_INFO_NONE, NULL,
				error);
		if (!enumer) {
			g_object_unref(file_dir);
			ERROR_Z
		}

		res = g_file_enumerator_iterate(enumer, NULL, &file_child, NULL, error);
		if (!res) {
			g_object_unref(enumer);
			g_object_unref(file_dir);
			ERROR_Z
		}

		if (file_child) has_children = TRUE;

		g_object_unref(enumer);
		g_object_unref(file_dir);

	}
	//oder dir in zip-Archiv?
	else if (SOND_IS_FILE_PART_ZIP(sfp_parent)) { //dir in zip

	}

	return has_children;
}

/*
 * ZIPs
 */
typedef struct {
	zip_t* archive;
} SondFilePartZipPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondFilePartZip, sond_file_part_zip, SOND_TYPE_FILE_PART)

static void sond_file_part_zip_finalize(GObject *self) {
	SondFilePartZipPrivate *sfp_zip_priv =
			sond_file_part_zip_get_instance_private(SOND_FILE_PART_ZIP(self));

	zip_discard(sfp_zip_priv->archive);

	G_OBJECT_CLASS(sond_file_part_zip_parent_class)->finalize(self);

	return;
}

static void sond_file_part_zip_class_init(SondFilePartZipClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = sond_file_part_zip_finalize;

	return;
}

static void sond_file_part_zip_init(SondFilePartZip* self) {

	return;
}

/*
 * PDFs
 */
typedef struct {
	fz_context* ctx;
	pdf_document* pdf_doc;
} SondFilePartPDFPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondFilePartPDF, sond_file_part_pdf, SOND_TYPE_FILE_PART)

static void sond_file_part_pdf_finalize(GObject *self) {
	SondFilePartPDFPrivate *sfp_pdf_priv =
			sond_file_part_pdf_get_instance_private(SOND_FILE_PART_PDF(self));

	pdf_drop_document(sfp_pdf_priv->ctx, sfp_pdf_priv->pdf_doc);
	fz_drop_context(sfp_pdf_priv->ctx);

	G_OBJECT_CLASS(sond_file_part_pdf_parent_class)->finalize(self);

	return;
}

static void sond_file_part_pdf_class_init(SondFilePartPDFClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = sond_file_part_pdf_finalize;

	return;
}

static void sond_file_part_pdf_init(SondFilePartPDF* self) {

	return;
}

/*
 * Leafs
 */
typedef struct {
	gchar* content_type;
} SondFilePartLeafPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondFilePartLeaf, sond_file_part_leaf, SOND_TYPE_FILE_PART)

static void sond_file_part_leaf_finalize(GObject *self) {
	SondFilePartLeafPrivate *sfp_leaf_priv =
			sond_file_part_leaf_get_instance_private(SOND_FILE_PART_LEAF(self));

	g_free(sfp_leaf_priv->content_type);

	G_OBJECT_CLASS(sond_file_part_leaf_parent_class)->finalize(self);

	return;
}

static void sond_file_part_leaf_class_init(SondFilePartLeafClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = sond_file_part_leaf_finalize;

	return;
}

static void sond_file_part_leaf_init(SondFilePartLeaf* self) {

	return;
}

gchar const* sond_file_part_leaf_get_content_type(SondFilePartLeaf *sfp_leaf) {
	SondFilePartLeafPrivate *sfp_leaf_priv =
			sond_file_part_leaf_get_instance_private(sfp_leaf);

	return sfp_leaf_priv->content_type;
}

void sond_file_part_leaf_set_content_type(SondFilePartLeaf *sfp_leaf,
		gchar const* content_type) {
	SondFilePartLeafPrivate *sfp_leaf_priv =
			sond_file_part_leaf_get_instance_private(sfp_leaf);

	if (sfp_leaf_priv->content_type)
		g_free(sfp_leaf_priv->content_type);
	sfp_leaf_priv->content_type = g_strdup(content_type);

	return;
}
