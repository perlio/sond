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

#include "zond/99conv/pdf.h"

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

static void sond_file_part_finalize(GObject* self) {
	SondFilePartPrivate *sfp_priv =
			sond_file_part_get_instance_private(SOND_FILE_PART(self));

	//falls in parent notiert, rauslöschen
	if (sfp_priv->parent &&
			SOND_FILE_PART_GET_CLASS(sfp_priv->parent)->get_arr_opened_files) {
		GPtrArray* arr_opened_files = NULL;

		arr_opened_files =
				SOND_FILE_PART_GET_CLASS(sfp_priv->parent)->get_arr_opened_files(sfp_priv->parent);
		g_ptr_array_remove_fast(arr_opened_files, self);
	}

	//array_opened_files löschen
	if (SOND_FILE_PART_GET_CLASS(self)->get_arr_opened_files) {
		GPtrArray* arr_opened_files = NULL;

		//Referenzen brauchen - glaube ich - nicht gelöscht zu werden?!
		arr_opened_files =
				SOND_FILE_PART_GET_CLASS(self)->
				get_arr_opened_files(SOND_FILE_PART(self));
		g_ptr_array_unref(arr_opened_files);
	}

	g_free(sfp_priv->path);
	g_object_unref(sfp_priv->parent);

	G_OBJECT_CLASS(sond_file_part_parent_class)->finalize(self);

	return;
}

static void sond_file_part_class_init(SondFilePartClass *klass) {
	G_OBJECT_CLASS(klass)->finalize = sond_file_part_finalize;

	klass->load_children = NULL; //Default: keine Kinder
	klass->has_children = NULL; //Default: keine Kinder
	klass->get_arr_opened_files = NULL; //Default: keine spezielle Initialisierung

	return;
}

static void sond_file_part_init(SondFilePart* self) {

	return;
}

static SondFilePart* sond_file_part_create_from_content_type(gchar const* path,
		SondFilePart* sfp_parent, gchar const* content_type) {
	SondFilePart* sfp_child = NULL;
	GError* error = NULL;

	if (g_content_type_is_mime_type(content_type, "inode/directory"))
		sfp_child = SOND_FILE_PART(sond_file_part_dir_create(path,
				sfp_parent, &error)); //dir ist parent von gar nix!
	else if (g_content_type_is_mime_type(content_type, "application/pdf") ||
			g_strcmp0(content_type, ".pdf") == 0)
		sfp_child = SOND_FILE_PART(sond_file_part_pdf_create(path,
				sfp_parent, &error));
	else if (g_content_type_is_mime_type(content_type, "application/zip") ||
			g_strcmp0(content_type, ".zip") == 0)
		sfp_child = SOND_FILE_PART(sond_file_part_zip_create(path,
				sfp_parent));
	else //alles andere = leaf
		sfp_child = SOND_FILE_PART(sond_file_part_leaf_create(path, sfp_parent,
				content_type));

	if (!sfp_child)
		sfp_child = //übernimmt error
				SOND_FILE_PART(sond_file_part_error_create(path, sfp_parent, error));

	return sfp_child;
}

static SondFilePart* sond_file_part_create(GType sfp_type, const gchar *path,
		SondFilePart* parent) {
	SondFilePart *sfp = NULL;
	SondFilePartPrivate *sfp_priv = NULL;
	GPtrArray *arr_opened_files = NULL;

	if (parent && SOND_FILE_PART_GET_CLASS(parent)->get_arr_opened_files) {
		arr_opened_files = SOND_FILE_PART_GET_CLASS(parent)->get_arr_opened_files(parent);

		//suchen, ob schon geöffnet
		for (guint i = 0; i < arr_opened_files->len; i++) {
			SondFilePart *sfp_tmp = g_ptr_array_index(arr_opened_files, i);
			//bereits geöffnet
			if (g_strcmp0(sond_file_part_get_path(sfp_tmp), path) == 0)
				return g_object_ref(sfp_tmp); //ref zurückgeben
		}
	}

	//sonst neu machen
	sfp = g_object_new(sfp_type, NULL);
	sfp_priv = sond_file_part_get_instance_private(sfp);

	sfp_priv->path = g_strdup(path);
	if (parent) sfp_priv->parent = SOND_FILE_PART(g_object_ref(parent));

	//Und ggf. im Elternobekt notieren
	if (arr_opened_files) {
		//Array von geöffneten Dateien im Filesystem
		g_ptr_array_add(arr_opened_files, sfp);
	}
g_message("sfp erstellt - path: %s    path_parent: %s", sfp_priv->path,
		(sfp_priv->parent) ? sond_file_part_get_path(sfp_priv->parent) : NULL);
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

GPtrArray* sond_file_part_load_children(SondFilePart* sfp, GError **error) {
	GPtrArray *arr_children = NULL;

	if (SOND_FILE_PART_GET_CLASS(sfp)->load_children) {
		arr_children = SOND_FILE_PART_GET_CLASS(sfp)->load_children(sfp, error);
		if (!arr_children) {
			g_prefix_error(error, "%s\n", __func__);
			return NULL;
		}
	}

	return arr_children; //NULL ohne error - nicht unterstützt
}

gboolean sond_file_part_has_children(SondFilePart *sfp) {
	gboolean has_children = FALSE;

	if (SOND_FILE_PART_GET_CLASS(sfp)->has_children)
		has_children = SOND_FILE_PART_GET_CLASS(sfp)->has_children(sfp);

	return has_children;
}

static fz_stream* sond_file_part_pdf_lookup_embedded_file(fz_context*,
		SondFilePartPDF*, gchar const*, GError**);

static fz_stream* sond_file_part_get_istream(fz_context* ctx,
		SondFilePart* sfp, GError **error) {
	SondFilePart* sfp_parent = NULL;
	fz_stream *stream = NULL;

	sfp_parent = sond_file_part_get_parent(sfp);

	//Datei im Filesystem
	if (SOND_IS_FILE_PART_ROOT(sfp_parent)) {
		gchar const* path_root = NULL;
		gchar* path_file = NULL;

		path_root = sond_file_part_get_path(sfp_parent);
		path_file = g_strconcat(path_root, "/",
				sond_file_part_get_path(sfp), NULL);

		fz_try(ctx)
			stream = fz_open_file(ctx, path_file);
		fz_always(ctx)
			g_free(path_file);
		fz_catch(ctx) {
			if (error) *error = g_error_new(g_quark_from_static_string("mupdf"),
					fz_caught(ctx), "%s\nfz_open_file: %s", __func__,
					fz_caught_message(ctx));

			return NULL; //Fehler beim Öffnen des Streams
		}
	}
	//Datei in Zip-Archiv
	else if (SOND_IS_FILE_PART_ZIP(sfp_parent)) {

	}
	//Datei in GMimeMessage
	//else if (SOND_IS_FILE_PART_MIME(sfp_parent)
	//Datei in PDF
	else if (SOND_IS_FILE_PART_PDF(sfp_parent)) {
		stream = sond_file_part_pdf_lookup_embedded_file(ctx,
				SOND_FILE_PART_PDF(sfp_parent),
				sond_file_part_get_path(sfp), error);
		if (!stream)
			ERROR_Z_VAL(NULL)
	}
	else {
		if (error) *error = g_error_new(ZOND_ERROR, 0, "%s\nStream für SondFilePart-Typ"
			"kann nicht geöffnet werden", __func__);
	}

	return stream;
}

gchar* sond_file_part_write_to_tmp_file(SondFilePart* sfp, GError **error) {
	gint rc = 0;
	gchar *filename = NULL;
	gchar* basename = NULL;
	gint fd_write = 0;
	fz_context* ctx = NULL;
	fz_stream* stream = NULL;
	fz_buffer *buf = NULL;

	ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
	if (!ctx) {
		if (error) *error = g_error_new(ZOND_ERROR, 0,
				"%s\nfz_new_context gibt NULL zurück", __func__);
		return NULL;
	}

	stream = sond_file_part_get_istream(ctx, sfp, error);
	if (!stream) {
		fz_drop_context(ctx);
		ERROR_Z_VAL(NULL)
	}

	fz_try(ctx)
		buf = fz_read_all(ctx, stream, 4096);
	fz_always(ctx)
		fz_drop_stream(ctx, stream);
	fz_catch(ctx) {
		if (error) *error = g_error_new(g_quark_from_static_string("mupdf"),
					fz_caught(ctx), "%s\n%s", __func__,
					fz_caught_message(ctx));
		fz_drop_context(ctx);

		return NULL;
	}

	basename = g_path_get_basename(sond_file_part_get_path(sfp));
	filename = g_strdup_printf("%s/%d%s", g_get_tmp_dir(),
			g_random_int_range(10000, 99999), basename);
	g_free(basename);

	fz_try(ctx)
		fz_save_buffer(ctx, buf, filename);
	fz_always(ctx)
		fz_drop_buffer(ctx, buf);
	fz_catch(ctx) {
		if (error) *error = g_error_new(g_quark_from_static_string("mupdf"),
					fz_caught(ctx), "%s\n%s", __func__,
					fz_caught_message(ctx));
		fz_drop_context(ctx);
		g_free(filename);

		return NULL;
	}

	return filename;
}

/*
 * Error
 */
typedef struct {
	GError *error; //Fehler, der aufgetreten ist
} SondFilePartErrorPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondFilePartError, sond_file_part_error, SOND_TYPE_FILE_PART)

static void sond_file_part_error_finalize(GObject *self) {
	SondFilePartErrorPrivate *sfp_error_priv =
			sond_file_part_error_get_instance_private(SOND_FILE_PART_ERROR(self));

	g_error_free(
			sfp_error_priv->error); //Fehler freigeben

	G_OBJECT_CLASS(sond_file_part_error_parent_class)->finalize(self);

	return;
}

static void sond_file_part_error_class_init(SondFilePartErrorClass *klass) {
	G_OBJECT_CLASS(klass)->finalize = sond_file_part_error_finalize;

	return;
}

static void sond_file_part_error_init(SondFilePartError* self) {

	return;
}

SondFilePartError* sond_file_part_error_create(gchar const* path,
		SondFilePart* parent, GError* error) {
	SondFilePartError *sfp_error = NULL;
	SondFilePartErrorPrivate *sfp_error_priv = NULL;

	sfp_error = (SondFilePartError*) sond_file_part_create(
			SOND_TYPE_FILE_PART_ERROR, path, parent);
	sfp_error_priv =
			sond_file_part_error_get_instance_private(SOND_FILE_PART_ERROR(sfp_error));
	sfp_error_priv->error = error;

	return sfp_error;
}

/*
 * Root
 */
typedef struct {
	GPtrArray* arr_opened_files; //Array von Dateien im Filesystem, die als SondFilePart-Objekten geöffnet sind
} SondFilePartRootPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondFilePartRoot, sond_file_part_root, SOND_TYPE_FILE_PART)

static void sond_file_part_root_finalize(GObject *self) {
	G_OBJECT_CLASS(sond_file_part_root_parent_class)->finalize(self);

	return;
}

static GPtrArray* sond_file_part_root_get_arr_opened_files(SondFilePart *sfp) {
	SondFilePartRootPrivate *sfp_root_priv =
			sond_file_part_root_get_instance_private(SOND_FILE_PART_ROOT(sfp));

	return sfp_root_priv->arr_opened_files;
}

static void sond_file_part_root_class_init(SondFilePartRootClass *klass) {
	G_OBJECT_CLASS(klass)->finalize = sond_file_part_root_finalize;

	SOND_FILE_PART_CLASS(klass)->get_arr_opened_files =
			sond_file_part_root_get_arr_opened_files;

	return;
}

static void sond_file_part_root_init(SondFilePartRoot* self) {
	SondFilePartRootPrivate *sfp_root_priv =
			sond_file_part_root_get_instance_private(SOND_FILE_PART_ROOT(self));

	sfp_root_priv->arr_opened_files =
			g_ptr_array_new( );

	return;
}

SondFilePartRoot* sond_file_part_root_create(gchar const* path) {
	SondFilePartRoot *sfp_root = NULL;

	sfp_root = (SondFilePartRoot*) sond_file_part_create(
			SOND_TYPE_FILE_PART_ROOT, path, NULL);

	return sfp_root;
}

/*
 * ZIPs
 */
typedef struct {
	zip_t* archive;
	GPtrArray* arr_opened_files; //Array von SondFilePart-Objekten, die in diesem Zip geöffnet sind
} SondFilePartZipPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondFilePartZip, sond_file_part_zip, SOND_TYPE_FILE_PART)

static void sond_file_part_zip_finalize(GObject *self) {
	SondFilePartZipPrivate *sfp_zip_priv =
			sond_file_part_zip_get_instance_private(SOND_FILE_PART_ZIP(self));

	g_ptr_array_unref(sfp_zip_priv->arr_opened_files);
	zip_discard(sfp_zip_priv->archive);

	G_OBJECT_CLASS(sond_file_part_zip_parent_class)->finalize(self);

	return;
}

static GPtrArray* sond_file_part_zip_get_arr_opened_files(SondFilePart *sfp) {
	SondFilePartRootPrivate *sfp_zip_priv =
			sond_file_part_zip_get_instance_private(SOND_FILE_PART_ZIP(sfp));

	return sfp_zip_priv->arr_opened_files;
}

static void sond_file_part_zip_class_init(SondFilePartZipClass *klass) {
	G_OBJECT_CLASS(klass)->finalize = sond_file_part_zip_finalize;

	SOND_FILE_PART_CLASS(klass)->get_arr_opened_files =
			sond_file_part_zip_get_arr_opened_files; //gleich wie bei Root

	return;
}

static void sond_file_part_zip_init(SondFilePartZip* self) {
	SondFilePartZipPrivate *sfp_zip_priv =
			sond_file_part_zip_get_instance_private(SOND_FILE_PART_ZIP(self));
	sfp_zip_priv->arr_opened_files =
			g_ptr_array_new( );

	return;
}

SondFilePartZip* sond_file_part_zip_create(gchar const* path, SondFilePart* sfp_parent) {
	SondFilePartZip *sfp_zip = NULL;

	sfp_zip = (SondFilePartZip*) sond_file_part_create(
			SOND_TYPE_FILE_PART_ZIP, path, sfp_parent);

	return sfp_zip;
}

/*
 * Dirs
 */
typedef struct {
	gboolean has_children; //hat dieses Verzeichnis Kinder?
} SondFilePartDirPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondFilePartDir, sond_file_part_dir, SOND_TYPE_FILE_PART)

static void sond_file_part_dir_finalize(GObject *self) {
	G_OBJECT_CLASS(sond_file_part_dir_parent_class)->finalize(self);

	return;
}

static gint sond_file_part_dir_get_children(SondFilePartDir* sfp_dir,
		gboolean load, GPtrArray** arr_children, GError **error) {
	SondFilePartDirPrivate *sfp_dir_priv =
			sond_file_part_dir_get_instance_private(sfp_dir);
	SondFilePart *sfp_parent = NULL;
	gchar const* path = NULL;

	if (load)
		*arr_children = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);

	path = sond_file_part_get_path(SOND_FILE_PART(sfp_dir));

	//Ist ein dir im Filesystem
	if (SOND_IS_FILE_PART_ROOT(sfp_parent = sond_file_part_get_parent(SOND_FILE_PART(sfp_dir)))) {
		GFile* file_dir = NULL;
		GFileEnumerator *enumer = NULL;
		gboolean dir_has_children = FALSE;
		gchar* path_dir = NULL;

		path_dir = g_strconcat(
				sond_file_part_get_path(SOND_FILE_PART(sfp_parent)), "/", path, NULL);

		file_dir = g_file_new_for_path(path_dir);
		g_free(path_dir);

		enumer = g_file_enumerate_children(file_dir, "*", G_FILE_QUERY_INFO_NONE, NULL,
				error);
		g_object_unref(file_dir);
		if (!enumer) {
			g_ptr_array_unref(*arr_children);

			ERROR_Z
		}

		while (1) {
			GFileInfo *info_child = NULL;
			gboolean res = FALSE;
			gchar const* content_type = NULL;
			SondFilePart *sfp_child = NULL;
			gchar* rel_path_child = NULL;

			res = g_file_enumerator_iterate(enumer, &info_child, NULL, NULL, error);
			if (!res) {
				g_object_unref(enumer);
				g_ptr_array_unref(*arr_children);
				ERROR_Z
			}

			if (!info_child) //keine weiteren Dateien
				break;
			else dir_has_children = TRUE;

			if (!load)
				break; //nur prüfen, ob Kinder vorhanden

			if (path)
				rel_path_child = g_strconcat(path, "/",
						g_file_info_get_name(info_child), NULL);
			else rel_path_child = g_strdup(g_file_info_get_name(info_child));

			content_type = g_file_info_get_content_type(info_child);

			sfp_child = sond_file_part_create_from_content_type(
					rel_path_child, sfp_parent, content_type);

			g_free(rel_path_child);

			g_ptr_array_add(*arr_children, sfp_child);
		}

		g_object_unref(enumer); //unreferenziert auch alle infos und gfiles

		sfp_dir_priv->has_children = dir_has_children;
	}
	//oder dir in zip-Archiv?
	else if (SOND_IS_FILE_PART_ZIP(sfp_parent)) { //dir in zip

	}

	return 0;
}

static GPtrArray* sond_file_part_dir_load_children(SondFilePart* sfp_dir, GError **error) {
	gint rc = 0;
	GPtrArray *arr_children = NULL;

	rc = sond_file_part_dir_get_children(SOND_FILE_PART_DIR(sfp_dir), TRUE, &arr_children, error);
	if (rc)
		ERROR_Z_VAL(NULL)

	return arr_children;
}

static gboolean sond_file_part_dir_has_children(SondFilePart* sfp_dir) {
	SondFilePartDirPrivate *sfp_dir_priv =
			sond_file_part_dir_get_instance_private(SOND_FILE_PART_DIR(sfp_dir));

	return sfp_dir_priv->has_children;
}

static void sond_file_part_dir_class_init(SondFilePartDirClass *klass) {
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	object_class->finalize = sond_file_part_dir_finalize;

	SOND_FILE_PART_CLASS(klass)->load_children =
			sond_file_part_dir_load_children;
	SOND_FILE_PART_CLASS(klass)->has_children =
			sond_file_part_dir_has_children;

	return;
}

static void sond_file_part_dir_init(SondFilePartDir* self) {

	return;
}

SondFilePartDir* sond_file_part_dir_create(gchar const* path, SondFilePart* sfp_parent,
		GError **error) {
	gint rc = 0;
	SondFilePartDir *sfp_dir = NULL;

	sfp_dir = (SondFilePartDir*) sond_file_part_create(
			SOND_TYPE_FILE_PART_DIR, path, sfp_parent);

	//Initialisieren, damit arr_children ggf. != NULL
	rc = sond_file_part_dir_get_children(sfp_dir, FALSE, NULL, error);
	if (rc)
		ERROR_Z_VAL(NULL)

	return sfp_dir;
}

/*
 * PDFs
 */
typedef struct {
	SondFilePartPDFPageTree* sfp_pdf_page_tree;
	gboolean has_embedded_files; //hat diese PDF eingebettete Dateien?
	GPtrArray* arr_embedded_files; //eingebettet und geöffnet
} SondFilePartPDFPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondFilePartPDF, sond_file_part_pdf, SOND_TYPE_FILE_PART)

static void sond_file_part_pdf_finalize(GObject *self) {
	SondFilePartPDFPrivate *sfp_pdf_priv =
			sond_file_part_pdf_get_instance_private(SOND_FILE_PART_PDF(self));

	G_OBJECT_CLASS(sond_file_part_pdf_parent_class)->finalize(self);

	return;
}

typedef struct {
	gchar const* path_search;
	fz_stream* stream;
} Lookup;

static gint lookup_embedded_file(fz_context* ctx, pdf_obj* EF_F,
		gchar const* path, gpointer data, GError** error) {
	Lookup* lookup = (Lookup*) data;

	if (g_strcmp0(path, lookup->path_search) == 0) {
		fz_stream* stream = NULL;

		fz_try(ctx)
			stream = pdf_open_stream(ctx, EF_F);
		fz_catch(ctx) {
			if (error)
				*error = g_error_new(g_quark_from_static_string("mupdf"),
						fz_caught(ctx), "%s\n%s", __func__,
						fz_caught_message(ctx));

			return -1;
		}
		lookup->stream = stream;

		return 1;
	}

	return 0;
}

typedef struct {
	SondFilePartPDF* sfp_pdf;
	GPtrArray* arr_embedded_files;
}Load;

static gint load_embedded_files(fz_context* ctx, pdf_obj* EF_F, gchar const* path,
		gpointer data, GError** error) {
	Load* load = (Load*) data;
	guchar buf[1024] = { 0 };
	fz_stream* stream = NULL;
	gchar* content_type = NULL;
	SondFilePart* sfp_embedded_file = NULL;

	fz_try(ctx)
		stream = pdf_open_stream(ctx, EF_F);
	fz_catch(ctx) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("mupdf"),
					fz_caught(ctx), "%s\n%s", __func__,
					fz_caught_message(ctx));

		return -1;
	}

	fz_try(ctx)
		fz_read(ctx, stream, buf, sizeof(buf));
	fz_always(ctx)
		fz_drop_stream(ctx, stream);
	fz_catch(ctx) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("mupdf"),
					fz_caught(ctx), "%s\n%s", __func__,
					fz_caught_message(ctx));

		return -1;
	}

	content_type = g_content_type_guess(path, buf, sizeof(buf), NULL);
	sfp_embedded_file = sond_file_part_create_from_content_type(
			path, SOND_FILE_PART(load->sfp_pdf), content_type);
	g_free(content_type);

	g_ptr_array_add(load->arr_embedded_files, sfp_embedded_file);

	return 0;
}

static pdf_document* sond_file_part_pdf_open_document(fz_context* ctx, SondFilePartPDF *sfp_pdf,
		GError **error) {
	pdf_document* doc = NULL;
	fz_stream* stream = NULL;

	stream = sond_file_part_get_istream(ctx, SOND_FILE_PART(sfp_pdf), error);
	if (!stream)
		ERROR_Z_VAL(NULL)

	//PDF-Dokument öffnen
	fz_try(ctx)
		doc = pdf_open_document_with_stream(ctx, stream);
	fz_always(ctx)
		fz_drop_stream(ctx, stream);
	fz_catch(ctx) {
		if (error) *error = g_error_new(g_quark_from_static_string("mupdf"),
				fz_caught(ctx),
				"%s\nkonnte PDF-Dokument '%s' nicht öffnen: %s", __func__,
				sond_file_part_get_path(SOND_FILE_PART(sfp_pdf)),
				fz_caught_message(ctx));

		return NULL; //Fehler beim Öffnen des PDF-Dokuments
	}

	return doc;
}

static GPtrArray* sond_file_part_pdf_load_embedded_files(SondFilePart* sfp_pdf,
		GError **error) {
	gint rc = 0;
	pdf_obj* embedded_files_dict = NULL;
	fz_context* ctx = NULL;
	pdf_document* doc = NULL;
	Load load = { 0 };
	SondFilePartPDFPageTree* sfp_pdf_page_tree = NULL;
	SondFilePartPDFPrivate *sfp_pdf_priv =
			sond_file_part_pdf_get_instance_private(SOND_FILE_PART_PDF(sfp_pdf));

	ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
	if (!ctx) {
		if (error) *error = g_error_new(ZOND_ERROR, 0,
				"%s\nfz_new_context gibt NULL zurück", __func__);
	}

	doc = sond_file_part_pdf_open_document(ctx, SOND_FILE_PART_PDF(sfp_pdf), error);
	if (rc) {
		fz_drop_context(ctx);
		ERROR_Z_VAL(NULL)
	}

	rc = pdf_get_names_tree_dict(ctx, doc, PDF_NAME(EmbeddedFiles),
			&embedded_files_dict, error);
	if (rc) {
		pdf_drop_document(ctx, doc);
		fz_drop_context(ctx);
		ERROR_Z_VAL(NULL)
	}

	load.sfp_pdf = SOND_FILE_PART_PDF(sfp_pdf);
	load.arr_embedded_files = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	rc = pdf_walk_names_dict(ctx, embedded_files_dict,
			NULL, load_embedded_files, &load, error);
	pdf_drop_document(ctx, doc);
	fz_drop_context(ctx);
	if (rc == -1) {
		g_ptr_array_unref(load.arr_embedded_files);
		ERROR_Z_VAL(NULL)
	}

	sfp_pdf_page_tree = sond_file_part_pdf_page_tree_create(NULL,
			sfp_pdf, error);
	if (!sfp_pdf_page_tree) {
		g_ptr_array_unref(load.arr_embedded_files);
		ERROR_Z_VAL(NULL)
	}

	g_ptr_array_insert(load.arr_embedded_files, 0, sfp_pdf_priv->sfp_pdf_page_tree);

	return load.arr_embedded_files;
}

static gboolean sond_file_part_pdf_has_embedded_files(SondFilePart* sfp_pdf) {
	SondFilePartPDFPrivate *sfp_pdf_priv =
			sond_file_part_pdf_get_instance_private(SOND_FILE_PART_PDF(sfp_pdf));

	return sfp_pdf_priv->has_embedded_files;
}

static GPtrArray* sond_file_part_pdf_get_array_embedded_files(SondFilePart *sfp_pdf) {
	SondFilePartPDFPrivate *sfp_pdf_priv =
			sond_file_part_pdf_get_instance_private(SOND_FILE_PART_PDF(sfp_pdf));

	return sfp_pdf_priv->arr_embedded_files;
}

static void sond_file_part_pdf_class_init(SondFilePartPDFClass *klass) {
	G_OBJECT_CLASS(klass)->finalize = sond_file_part_pdf_finalize;

	SOND_FILE_PART_CLASS(klass)->load_children =
			sond_file_part_pdf_load_embedded_files;
	SOND_FILE_PART_CLASS(klass)->has_children =
			sond_file_part_pdf_has_embedded_files;
	SOND_FILE_PART_CLASS(klass)->get_arr_opened_files =
			sond_file_part_pdf_get_array_embedded_files;

	return;
}

static void sond_file_part_pdf_init(SondFilePartPDF* self) {
	SondFilePartPDFPrivate *sfp_pdf_priv =
			sond_file_part_pdf_get_instance_private(SOND_FILE_PART_PDF(self));

	sfp_pdf_priv->arr_embedded_files = g_ptr_array_new( );

	return;
}

static void mupdf_unlock(void *user, gint lock) {
	GMutex *mutex = (GMutex*) user;

	g_mutex_unlock(&(mutex[lock]));

	return;
}

static void mupdf_lock(void *user, gint lock) {
	GMutex *mutex = (GMutex*) user;

	g_mutex_lock(&(mutex[lock]));

	return;
}

static void*
mupdf_malloc(void *user, size_t size) {
	return g_malloc(size);
}

static void*
mupdf_realloc(void *user, void *old, size_t size) {
	return g_realloc(old, size);
}

static void mupdf_free(void *user, void *ptr) {
	g_free(ptr);

	return;
}

static fz_context*
init_context(void) {
	GMutex *mutex = NULL;
	fz_context *ctx = NULL;
	fz_locks_context locks_context = { 0, };
	fz_alloc_context alloc_context = { 0, };

	//mutex für document
	mutex = g_malloc0(sizeof(GMutex) * FZ_LOCK_MAX);
	for (gint i = 0; i < FZ_LOCK_MAX; i++)
		g_mutex_init(&(mutex[i]));

	locks_context.user = mutex;
	locks_context.lock = mupdf_lock;
	locks_context.unlock = mupdf_unlock;

	alloc_context.user = NULL;
	alloc_context.malloc = mupdf_malloc;
	alloc_context.realloc = mupdf_realloc;
	alloc_context.free = mupdf_free;

	/* Create a context to hold the exception stack and various caches. */
	ctx = fz_new_context(&alloc_context, &locks_context, FZ_STORE_UNLIMITED);
	if (!ctx) {
		for (gint i = 0; i < FZ_LOCK_MAX; i++)
			g_mutex_clear(&mutex[i]);
		g_free(mutex);
	}

	return ctx;
}

static fz_stream* sond_file_part_pdf_lookup_embedded_file(fz_context* ctx,
		SondFilePartPDF* sfp_pdf, gchar const* path, GError** error) {
	pdf_document* doc = NULL;
	pdf_obj* embedded_files_dict = NULL;
	Lookup lookup = { 0 };
	gint rc = 0;

	doc = sond_file_part_pdf_open_document(ctx, sfp_pdf, error);
	if (!doc)
		ERROR_Z_VAL(NULL)

	rc = pdf_get_names_tree_dict(ctx, doc, PDF_NAME(EmbeddedFiles), &embedded_files_dict, error);
	if (rc) {
		pdf_drop_document(ctx, doc);
		ERROR_Z_VAL(NULL)
	}

	lookup.path_search = sond_file_part_get_path(SOND_FILE_PART(sfp_pdf));

	rc = pdf_walk_names_dict(ctx, embedded_files_dict, NULL,
			lookup_embedded_file, &lookup, error);
	pdf_drop_document(ctx, doc); //stream hält (hoffentlich) ref auf doc
	if (rc == -1)
		ERROR_Z_VAL(NULL)
	else if (rc == 0) {
		if (error) *error = g_error_new(ZOND_ERROR, 0, "%s\nnicht gefunden", __func__);

		return NULL;
	}

	return lookup.stream;
}

static gint sond_file_part_pdf_test_for_embedded_files(
		SondFilePartPDF *sfp_pdf, GError **error) {
	gint rc = 0;
	fz_context* ctx = NULL;
	pdf_document* doc = NULL;
	pdf_obj* embedded_files_dict = NULL;
	SondFilePartPDFPrivate* sfp_pdf_priv =
			sond_file_part_pdf_get_instance_private(sfp_pdf);

	ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
	if (!ctx) {
		if (error) *error = g_error_new(ZOND_ERROR, 0,
				"%s\nfz_new_context gibt NULL zurück", __func__);

		return -1;
	}

	doc = sond_file_part_pdf_open_document(ctx, sfp_pdf, error);
	if (!doc) {
		fz_drop_context(ctx);
		ERROR_Z
	}

	rc = pdf_get_names_tree_dict(ctx, doc, PDF_NAME(EmbeddedFiles),
			&embedded_files_dict, error);
	if (rc) {
		pdf_drop_document(ctx, doc);
		fz_drop_context(ctx);
		ERROR_Z
	}

	fz_try(ctx) {
		//Prüfen, ob PDF eingebettete Dateien hat
		if (embedded_files_dict && pdf_dict_len(ctx, embedded_files_dict))
			sfp_pdf_priv->has_embedded_files = TRUE;
	}
	fz_always(ctx) {
		pdf_drop_document(ctx, doc);
	}
	fz_catch(ctx) {
		if (error) *error = g_error_new(g_quark_from_static_string("mupdf"),
				fz_caught(ctx),
				"%s\nkonnte eingebettete Dateien in PDF '%s' nicht prüfen: %s",
				__func__, sond_file_part_get_path(SOND_FILE_PART(sfp_pdf)),
				fz_caught_message(ctx));
		fz_drop_context(ctx);

		return -1; //Fehler beim Prüfen auf eingebettete Dateien
	}

	fz_drop_context(ctx);

	return 0;
}

SondFilePartPDF* sond_file_part_pdf_create(gchar const* path,
		SondFilePart* parent, GError **error) {
	SondFilePartPDF *sfp_pdf = NULL;
	gint rc = 0;

	sfp_pdf = (SondFilePartPDF*) sond_file_part_create(
			SOND_TYPE_FILE_PART_PDF, path, parent);

	rc = sond_file_part_pdf_test_for_embedded_files(sfp_pdf, error);
	if (rc) {
		g_object_unref(sfp_pdf);
		ERROR_Z_VAL(NULL)
	}

	return sfp_pdf;
}

static void sond_file_part_pdf_set_root_page_tree(SondFilePartPDF* sfp_pdf,
		SondFilePartPDFPageTree* sfp_pdf_page_tree) {
	SondFilePartPDFPrivate* sfp_pdf_priv =
			sond_file_part_pdf_get_instance_private(sfp_pdf);

	sfp_pdf_priv->sfp_pdf_page_tree = sfp_pdf_page_tree;
}

/*
 * PDFPageTree
 */
typedef struct {
	gchar* section;
	gboolean has_children;
	GPtrArray* arr_opened_children;
} SondFilePartPDFPageTreePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondFilePartPDFPageTree, sond_file_part_pdf_page_tree, SOND_TYPE_FILE_PART)

static void sond_file_part_pdf_page_tree_finalize(GObject *self) {
	SondFilePartPDFPageTreePrivate* sfp_pdf_page_tree_priv =
			sond_file_part_pdf_page_tree_get_instance_private(
					SOND_FILE_PART_PDF_PAGE_TREE(self));

	g_free(sfp_pdf_page_tree_priv->section);
	G_OBJECT_CLASS(sond_file_part_pdf_page_tree_parent_class)->finalize(self);

	return;
}

static GPtrArray* sond_file_part_pdf_page_tree_load_children(
		SondFilePart* sfp_pdf_page_tree, GError **error) {
	GPtrArray *arr_children = NULL;

	//Hier Seitenbaum-Kinder laden
	//z.B. wenn es sich um einen PDF-Abschnitt handelt, der Seiten enthält

	return arr_children; //NULL ohne error - nicht unterstützt
}

static gboolean sond_file_part_pdf_page_tree_has_children(
		SondFilePart* sfp_pdf_page_tree) {
	SondFilePartPDFPageTreePrivate *sfp_pdf_page_tree_priv =
			sond_file_part_pdf_page_tree_get_instance_private(
					SOND_FILE_PART_PDF_PAGE_TREE(sfp_pdf_page_tree));

	return sfp_pdf_page_tree_priv->has_children;
}

static GPtrArray* sond_file_part_pdf_page_tree_get_arr_opened_children(
		SondFilePart* sfp_pdf_page_tree) {
	SondFilePartPDFPageTreePrivate *sfp_pdf_page_tree_priv =
			sond_file_part_pdf_page_tree_get_instance_private(
					SOND_FILE_PART_PDF_PAGE_TREE(sfp_pdf_page_tree));

	return sfp_pdf_page_tree_priv->arr_opened_children;
}

static void sond_file_part_pdf_page_tree_class_init(SondFilePartPDFPageTreeClass *klass) {
	G_OBJECT_CLASS(klass)->finalize = sond_file_part_pdf_page_tree_finalize;

	SOND_FILE_PART_CLASS(klass)->load_children =
			sond_file_part_pdf_page_tree_load_children;
	SOND_FILE_PART_CLASS(klass)->has_children =
			sond_file_part_pdf_page_tree_has_children;
	SOND_FILE_PART_CLASS(klass)->get_arr_opened_files =
			sond_file_part_pdf_page_tree_get_arr_opened_children;

	return;
}

static void sond_file_part_pdf_page_tree_init(SondFilePartPDFPageTree* self) {
	SondFilePartPDFPageTreePrivate *sfp_pdf_page_tree_priv =
			sond_file_part_pdf_page_tree_get_instance_private(self);

	sfp_pdf_page_tree_priv->arr_opened_children = g_ptr_array_new();

	return;
}

static gboolean sond_file_part_pdf_page_tree_has_sections(
		SondFilePartPDFPageTree *sfp_pdf_page_tree, gchar const* section) {
	gboolean has_sections = FALSE;

	//Hier prüfen, ob Seitenbaum Kinder hat
	//z.B. wenn es sich um einen PDF-Abschnitt handelt, der Seiten enthält

	return has_sections;
}

SondFilePartPDFPageTree* sond_file_part_pdf_page_tree_create(gchar const* section,
		SondFilePart* parent, GError **error) {
	SondFilePartPDFPageTreePrivate *sfp_pdf_page_tree_priv = NULL;
	SondFilePartPDFPageTree *sfp_pdf_page_tree = NULL;

	sfp_pdf_page_tree = (SondFilePartPDFPageTree*) sond_file_part_create(
			SOND_TYPE_FILE_PART_PDF_PAGE_TREE, section, parent);

	sond_file_part_pdf_set_root_page_tree(SOND_FILE_PART_PDF(parent), sfp_pdf_page_tree);

	sfp_pdf_page_tree_priv =
			sond_file_part_pdf_page_tree_get_instance_private(sfp_pdf_page_tree);

	sfp_pdf_page_tree_priv->has_children =
			sond_file_part_pdf_page_tree_has_sections(sfp_pdf_page_tree, section);
	if (sfp_pdf_page_tree_priv->has_children == FALSE && error && *error) {
		g_prefix_error(error, "%s\n", __func__);
		g_object_unref(sfp_pdf_page_tree);
		return NULL; //Fehler beim Prüfen, ob Seitenbaum Kinder hat
	}

	return sfp_pdf_page_tree;
}

gchar const* sond_file_part_pdf_page_tree_get_section(
		SondFilePartPDFPageTree* sfp_pdf_page_tree) {
	SondFilePartPDFPageTreePrivate* sfp_pdf_page_tree_priv =
			sond_file_part_pdf_page_tree_get_instance_private(sfp_pdf_page_tree);

	return sfp_pdf_page_tree_priv->section;
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

SondFilePartLeaf* sond_file_part_leaf_create(gchar const* path,
		SondFilePart* parent, gchar const* content_type) {
	SondFilePartLeaf *sfp_leaf = NULL;
	SondFilePartLeafPrivate *sfp_leaf_priv = NULL;

	sfp_leaf = (SondFilePartLeaf*) sond_file_part_create(
			SOND_TYPE_FILE_PART_LEAF, path, parent);

	sfp_leaf_priv = sond_file_part_leaf_get_instance_private(sfp_leaf);
	sfp_leaf_priv->content_type = g_strdup(content_type);

	return sfp_leaf;
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
