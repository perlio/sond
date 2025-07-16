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
#include <glib/gstdio.h>
#include <zip.h>
#include <mupdf/pdf.h>

#include "misc.h"
#include "sond_treeviewfm.h"

#include "zond/global_types.h"
#include "zond/99conv/pdf.h"

//Grundlegendes Object, welches file_parts beschreibt
/*
 * parent: Objekt des Elternelements
 * 	NULL, wenn Datei in Filesystem
 * 	sfp_zip/sfp_pdf, wenn Objekt in zip-Archiv oder pdf-Datei gespeichert
 *
 * path: path zum Root-Element
 */
typedef struct {
	gchar *path; //rel_path zum root-Element
	SondFilePart* parent; //NULL, wenn in Datei rel_path
} SondFilePartPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondFilePart, sond_file_part, G_TYPE_OBJECT)

static void sond_file_part_finalize(GObject* self) {
	SondFilePartPrivate *sfp_priv =
			sond_file_part_get_instance_private(SOND_FILE_PART(self));

	if (!sfp_priv->parent)  //ist "normale" Datei
		g_ptr_array_remove_fast(SOND_FILE_PART_CLASS(
				g_type_class_peek(SOND_TYPE_FILE_PART))->arr_opened_files, self);
	//falls in parent notiert, rauslöschen
	else if (SOND_FILE_PART_GET_CLASS(sfp_priv->parent)->get_arr_opened_files) {
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

	klass->has_children = NULL; //Default: keine Kinder
	klass->get_arr_opened_files = NULL; //Default: keine spezielle Initialisierung

	klass->arr_opened_files = g_ptr_array_new( );
	return;
}

static void sond_file_part_init(SondFilePart* self) {

	return;
}

static SondFilePart* sond_file_part_create_from_content_type(gchar const* path,
		SondFilePart* sfp_parent, gchar const* content_type) {
	SondFilePart* sfp_child = NULL;
	GError* error = NULL;

	if (g_content_type_is_mime_type(content_type, "application/pdf") ||
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

	if (!parent)
		arr_opened_files = SOND_FILE_PART_CLASS(g_type_class_peek(
				SOND_TYPE_FILE_PART))->arr_opened_files;
	else if (parent && SOND_FILE_PART_GET_CLASS(parent)->get_arr_opened_files)
		arr_opened_files = SOND_FILE_PART_GET_CLASS(parent)->get_arr_opened_files(parent);

	if (arr_opened_files)
		//suchen, ob schon geöffnet
		for (guint i = 0; i < arr_opened_files->len; i++) {
			SondFilePart *sfp_tmp = g_ptr_array_index(arr_opened_files, i);
			//bereits geöffnet
			if (g_strcmp0(sond_file_part_get_path(sfp_tmp), path) == 0)
				return g_object_ref(sfp_tmp); //ref zurückgeben
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

gboolean sond_file_part_has_children(SondFilePart *sfp) {
	gboolean has_children = FALSE;

	if (SOND_FILE_PART_GET_CLASS(sfp)->has_children)
		has_children = SOND_FILE_PART_GET_CLASS(sfp)->has_children(sfp);

	return has_children;
}

gchar* sond_file_part_get_filepart(SondFilePart* sfp) {
	GList* list = NULL;
	SondFilePart* sfp_parent = NULL;
	gchar* filepart = NULL;
	GList* ptr_elem = NULL;

	list = g_list_append(list, sfp);

	//erstmal sfps sammeln und Reihenfolge richtig machen
	while((sfp_parent = sond_file_part_get_parent(sfp))) {
		list = g_list_prepend(list, sfp_parent);

		sfp = sfp_parent;
	}

	//dann filepart zusammensetzen
	ptr_elem = list;
	do {
		SondFilePart* sfp_list = NULL;

		sfp_list = SOND_FILE_PART(ptr_elem->data);

		if (filepart) filepart = add_string(filepart, g_strdup("//"));
		filepart = add_string(filepart, g_strdup(sond_file_part_get_path(sfp_list)));

		ptr_elem = ptr_elem->next;
	} while (ptr_elem);

	g_list_free(list);

	return filepart;
}

static gchar* guess_content_type(fz_context* ctx, fz_stream* stream,
		gchar const* path, GError** error) {
	guchar buf[1024] = { 0 };

	fz_try(ctx)
		fz_read(ctx, stream, buf, sizeof(buf));
	fz_catch(ctx) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("mupdf"),
					fz_caught(ctx), "%s\n%s", __func__,
					fz_caught_message(ctx));

		return NULL;
	}

	return g_content_type_guess(path, buf, sizeof(buf), NULL);
}

static fz_stream* sond_file_part_pdf_lookup_embedded_file(fz_context*,
		SondFilePartPDF*, gchar const*, GError**);

static fz_stream* open_file(fz_context* ctx, gchar const* path,
		GError** error) {
	fz_stream* stream = NULL;
	gchar const* path_root = NULL;
	gchar* path_file = NULL;

	path_root = SOND_FILE_PART_CLASS(g_type_class_peek_static(SOND_TYPE_FILE_PART))->path_root;
	path_file = g_strconcat(path_root, "/", path, NULL);

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

	return stream;
}

SondFilePart* sond_file_part_from_filepart(fz_context* ctx,
		gchar const* filepart, GError** error) {
	SondFilePart* sfp = NULL;
	gchar** v_string = NULL;
	gint zaehler = 0;

	v_string = g_strsplit(filepart, "//", -1);

	while (v_string[zaehler])
	{
		fz_stream* stream = NULL;
		gchar* content_type = NULL;
		SondFilePart* sfp_child = NULL;

		if (!sfp) //1. Ebene - File im Filesystem
			stream = open_file(ctx, v_string[zaehler], error);
		else if (SOND_IS_FILE_PART_ZIP(sfp)) {

		}
		else if (SOND_IS_FILE_PART_PDF(sfp))
			stream = sond_file_part_pdf_lookup_embedded_file(ctx,
					SOND_FILE_PART_PDF(sfp), v_string[zaehler], error);
		//else if (SOND_IS_FILE_PART_GMESSAGE(sfp))
		else { //darf nicht sein
			g_object_unref(sfp);
			g_strfreev(v_string);
			if (error) *error = g_error_new(ZOND_ERROR, 0, "%s\nfilepart malformed", __func__);

			return NULL;
		}

		if (!stream) {
			g_object_unref(sfp);
			g_strfreev(v_string);
			ERROR_Z_VAL(NULL)
		}

		content_type = guess_content_type(ctx, stream, v_string[zaehler], error);
		fz_drop_stream(ctx, stream);
		if (!content_type){
			g_object_unref(sfp);
			ERROR_Z_VAL(NULL)
		}

		sfp_child = sond_file_part_create_from_content_type(v_string[zaehler], sfp, content_type);
		if (SOND_IS_FILE_PART_ERROR(sfp_child)) { //können wir hier nicht brauchen
			if (error) *error = g_error_new(ZOND_ERROR, 0, "%s\n%s", __func__,
					sond_file_part_error_get_error(SOND_FILE_PART_ERROR(sfp_child))->message);
			g_strfreev(v_string);
			g_object_unref(sfp);

			return NULL;
		}

		sfp = sfp_child;
		zaehler++;
	}
	g_strfreev(v_string);

	return sfp;
}

fz_stream* sond_file_part_get_istream(fz_context* ctx,
		SondFilePart* sfp, gboolean need_seekable, GError **error) {
	SondFilePart* sfp_parent = NULL;
	fz_stream *stream = NULL;

	sfp_parent = sond_file_part_get_parent(sfp);

	//Datei im Filesystem
	if (!sfp_parent) {
		stream = open_file(ctx, sond_file_part_get_path(sfp), error);
		if (!stream)
			ERROR_Z_VAL(NULL);
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

		if (!stream->seek && need_seekable) { //stream ist nicht seekable - muß in buffer
			fz_buffer *buf = NULL;

			fz_try(ctx)
				buf = fz_read_all(ctx, stream, 4096);
			fz_always(ctx)
				fz_drop_stream(ctx, stream);
			fz_catch(ctx) {
				if (error) *error = g_error_new(g_quark_from_static_string("mupdf"),
						fz_caught(ctx), "%s\nfz_read_all: %s", __func__,
						fz_caught_message(ctx));

				return NULL;
			}

			fz_try(ctx)
				stream = fz_open_buffer(ctx, buf);
			fz_always(ctx)
				fz_drop_buffer(ctx, buf);
			fz_catch(ctx) {
				if (error) *error = g_error_new(g_quark_from_static_string("mupdf"),
						fz_caught(ctx), "%s\nfz_open_buffer: %s", __func__,
						fz_caught_message(ctx));

				return NULL;
			}
		}
	}
	else {
		if (error) *error = g_error_new(ZOND_ERROR, 0, "%s\nStream für SondFilePart-Typ"
			"kann nicht geöffnet werden", __func__);
	}

	return stream;
}

static fz_buffer* sond_file_part_get_buffer(SondFilePart* sfp,
		fz_context* ctx, GError** error) {
	fz_stream* stream = NULL;
	fz_buffer* buf = NULL;

	stream = sond_file_part_get_istream(ctx, sfp, FALSE, error);
	if (!stream) {
		fz_drop_context(ctx);
		ERROR_Z_VAL(NULL)
	}

	fz_try(ctx)
		buf = fz_read_all(ctx, stream, 0);
	fz_always(ctx)
		fz_drop_stream(ctx, stream);
	fz_catch(ctx) {
		if (error) *error = g_error_new(g_quark_from_static_string("mupdf"),
					fz_caught(ctx), "%s\n%s", __func__,
					fz_caught_message(ctx));
		fz_drop_context(ctx);

		return NULL;
	}

	return buf;
}
gchar* sond_file_part_write_to_tmp_file(SondFilePart* sfp, GError **error) {
	gchar *filename = NULL;
	gchar* basename = NULL;
	fz_context* ctx = NULL;
	fz_buffer *buf = NULL;

	ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
	if (!ctx) {
		if (error) *error = g_error_new(ZOND_ERROR, 0,
				"%s\nfz_new_context gibt NULL zurück", __func__);
		return NULL;
	}

	buf = sond_file_part_get_buffer(sfp, ctx, error);
	if (!buf) {
		fz_drop_context(ctx);

		ERROR_Z_VAL(NULL)
	}

	if (sond_file_part_get_path(sfp))
		basename = g_path_get_basename(sond_file_part_get_path(sfp));
	else basename = g_path_get_basename(sond_file_part_get_path(sond_file_part_get_parent(sfp)));

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
		g_free(filename);

		filename = NULL;
	}

	fz_drop_context(ctx);

	return filename;
}

static fz_buffer* sond_file_part_pdf_delete_emb_file(SondFilePartPDF*, fz_context*,
		gchar const*, GError**);

static fz_buffer* sond_file_part_pdf_mod_emb_file(SondFilePartPDF*, fz_context*,
		gchar const*, fz_buffer*, GError**);

gint sond_file_part_delete_sfp(SondFilePart* sfp, GError** error) {
	SondFilePart* sfp_parent = NULL;

	sfp_parent = sond_file_part_get_parent(sfp);

	if (!sfp_parent) { //Datei im Filesystem
		gint rc = 0;
		gchar* path = NULL;
		SondFilePartPrivate* sfp_priv = sond_file_part_get_instance_private(sfp);

		path = g_strconcat(SOND_FILE_PART_GET_CLASS(sfp)->path_root, "/", sfp_priv->path, NULL);
		rc = g_remove(path);
		g_free(path);
		if (rc) {
			if (error) *error = g_error_new(g_quark_from_static_string("stdlib"),
					errno, "%s\n%s", __func__, strerror(errno));

			return -1;
		}
	}
	else {
		gint rc = 0;
		fz_context* ctx = NULL;
		fz_buffer* buf_out = NULL;

		ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
		if (!ctx) {
			if (error) *error =
					g_error_new(g_quark_from_static_string("mupdf"), 0,
							"%s\ncontext konnte nicht erzeutgt werden", __func__);

			return -1;
		}

		if (SOND_IS_FILE_PART_ZIP(sfp_parent)) {
			//open zip -> buffer
			//zip_file_locate (Index Datei ermitteln)
			//zip_delete
			//zip_close
		}
		else if (SOND_IS_FILE_PART_PDF(sfp_parent))
			buf_out = sond_file_part_pdf_delete_emb_file(SOND_FILE_PART_PDF(sfp_parent), ctx,
					sond_file_part_get_path(sfp), error);

		//else if (SOND_IS_FILE_PART_GMESSAGE(sfp_parent)) {

		if (!buf_out) {
			fz_drop_context(ctx);

			ERROR_Z
		}

		//replace in parent(parent)
		rc = sond_file_part_replace(sfp_parent, ctx, buf_out, error);
		fz_drop_buffer(ctx, buf_out);
		fz_drop_context(ctx);
		if (rc)
			ERROR_Z
	}

	return 0;
}

gint sond_file_part_replace(SondFilePart* sfp, fz_context* ctx,
		fz_buffer* buf, GError** error) {
	SondFilePart* sfp_parent = NULL;

	sfp_parent = sond_file_part_get_parent(sfp);

	if (!sfp_parent) { //Datei im Filesystem
		gint rc = 0;
		gchar* filename = NULL;

		//Datei löschen
		rc = sond_file_part_delete_sfp(sfp, error);
		if (rc)
			ERROR_Z

		//neue Datei aus buffer schreiben
		filename = g_strconcat(SOND_FILE_PART_CLASS(g_type_class_peek_static(SOND_TYPE_FILE_PART))->path_root,
				"/", sond_file_part_get_path(sfp), NULL);

		fz_try(ctx)
			fz_save_buffer(ctx, buf, filename);
		fz_always(ctx)
			g_free(filename);
		fz_catch(ctx) {
			if (error) *error = g_error_new(g_quark_from_static_string("mupdf"),
						fz_caught(ctx), "%s\n%s", __func__,
						fz_caught_message(ctx));

			return -1;
		}

		return 0;
	}
	else {
		gint rc = 0;
		fz_buffer* buf_out = NULL;

		if (SOND_IS_FILE_PART_PDF(sfp_parent)) //ist also embedded
			buf_out = sond_file_part_pdf_mod_emb_file(SOND_FILE_PART_PDF(sfp_parent),
					ctx, sond_file_part_get_path(sfp), buf, error);
		else if (SOND_IS_FILE_PART_ZIP(sfp_parent)) { //ist also embedded
			//zip_archive öffnen ->buffer
			//zip_file_locate
			//zip_replace
			//zip-Archiv in neuen buffer schreiben
			//zip_close
		}

		if (!buf_out)
			ERROR_Z

		rc = sond_file_part_replace(sfp_parent, ctx, buf_out, error);
		fz_drop_buffer(ctx, buf_out);
		if (rc)
			ERROR_Z
	}

	return 0;
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

	g_error_free(sfp_error_priv->error); //Fehler freigeben

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

GError* sond_file_part_error_get_error(SondFilePartError* sfp_error) {
	SondFilePartErrorPrivate *sfp_error_priv =
			sond_file_part_error_get_instance_private(SOND_FILE_PART_ERROR(sfp_error));

	return sfp_error_priv->error;
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
	SondFilePartZipPrivate *sfp_zip_priv =
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
 * PDFs
 */
typedef struct {
	gboolean has_embedded_files; //hat diese PDF eingebettete Dateien?
	GPtrArray* arr_embedded_files; //eingebettet und geöffnet
} SondFilePartPDFPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondFilePartPDF, sond_file_part_pdf, SOND_TYPE_FILE_PART)

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

	content_type = guess_content_type(ctx, stream, path, error);
	fz_drop_stream(ctx, stream);
	if (!content_type)
		ERROR_Z

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

	stream = sond_file_part_get_istream(ctx, SOND_FILE_PART(sfp_pdf), TRUE, error);
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

gint sond_file_part_pdf_load_embedded_files(SondFilePartPDF* sfp_pdf,
		GPtrArray** arr_children, GError **error) {
	gint rc = 0;
	pdf_obj* embedded_files_dict = NULL;
	fz_context* ctx = NULL;
	pdf_document* doc = NULL;
	Load load = { 0 };

	ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
	if (!ctx) {
		if (error) *error = g_error_new(ZOND_ERROR, 0,
				"%s\nfz_new_context gibt NULL zurück", __func__);
		return -1;
	}

	doc = sond_file_part_pdf_open_document(ctx, SOND_FILE_PART_PDF(sfp_pdf), error);
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

	load.sfp_pdf = SOND_FILE_PART_PDF(sfp_pdf);
	load.arr_embedded_files = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	rc = pdf_walk_names_dict(ctx, embedded_files_dict,
			NULL, load_embedded_files, &load, error);
	pdf_drop_document(ctx, doc);
	fz_drop_context(ctx);
	if (rc == -1) {
		g_ptr_array_unref(load.arr_embedded_files);
		ERROR_Z
	}

	*arr_children = load.arr_embedded_files;

	return 0;
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

	lookup.path_search = path;

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

	fz_try(ctx) //Prüfen, ob PDF eingebettete Dateien hat
		if (embedded_files_dict && pdf_dict_len(ctx, embedded_files_dict))
			sfp_pdf_priv->has_embedded_files = TRUE;
	fz_always(ctx)
		pdf_drop_document(ctx, doc);
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

typedef struct {
	gchar const* path;
	fz_buffer* buf;
} Modify;

static gint modify_embedded_file(fz_context* ctx, pdf_obj* EF_F,
		gchar const* path, gpointer data, GError** error) {
	Modify* modify = (Modify*) data;

	if (g_strcmp0(path, modify->path) == 0) {
		pdf_document* doc = NULL;

		doc = pdf_pin_document(ctx, EF_F);
		if (!doc) {
			if (error) *error = g_error_new(g_quark_from_static_string("mupdf"),
					fz_caught(ctx), "%s\n%s", __func__,
					fz_caught_message(ctx));

			return -1;
		}
		fz_try(ctx)
			pdf_update_stream(ctx, doc, EF_F, modify->buf, 0);
		fz_always(ctx)
			pdf_drop_document(ctx, doc);
		fz_catch(ctx) {
			if (error)
				*error = g_error_new(g_quark_from_static_string("mupdf"),
						fz_caught(ctx), "%s\n%s", __func__,
						fz_caught_message(ctx));

			return -1;
		}

		return 1;
	}

	return 0;
}

static fz_buffer* sond_file_part_pdf_mod_emb_file(SondFilePartPDF* sfp_pdf,
		fz_context* ctx, gchar const* path, fz_buffer* buf, GError** error) {
	pdf_document* doc = NULL;
	gint rc = 0;
	Modify modify = { path, buf };
	pdf_obj* embedded_files_dict = NULL;
	fz_buffer* buf_out = NULL;

	doc = sond_file_part_pdf_open_document(ctx, sfp_pdf, error);
	if (!doc)
		ERROR_Z_VAL(NULL)

	//mod embedded file
	rc = pdf_get_names_tree_dict(ctx, doc, PDF_NAME(EmbeddedFiles),
			&embedded_files_dict, error);
	if (rc) {
		pdf_drop_document(ctx, doc);
		ERROR_Z_VAL(NULL)
	}

	rc = pdf_walk_names_dict(ctx, embedded_files_dict, NULL,
			modify_embedded_file, &modify, error);
	if (rc != 1) {
		pdf_drop_document(ctx, doc); //stream hält (hoffentlich) ref auf doc
		if (rc == -1)
			ERROR_Z_VAL(NULL)
		else if (rc == 0) {
			if (error) *error = g_error_new(ZOND_ERROR, 0, "%s\nnicht gefunden", __func__);

			return NULL;
		}
	}

	//write pdf to other buffer
	buf_out = pdf_doc_to_buf(ctx, doc, error);
	pdf_drop_document(ctx, doc);
	if (!buf_out)
		ERROR_Z_VAL(NULL)

	return buf_out;
}

static fz_buffer* sond_file_part_pdf_delete_emb_file(SondFilePartPDF* sfp_pdf,
		fz_context* ctx, gchar const* path, GError** error) {
	pdf_document* doc = NULL;
	fz_buffer* buf = NULL;

	doc = sond_file_part_pdf_open_document(ctx, sfp_pdf, error);
	if (!doc)
		ERROR_Z_VAL(NULL)

	//delete embedded file

	//write pdf to other buffer
	buf = pdf_doc_to_buf(ctx, doc, error);
	pdf_drop_document(ctx, doc);
	if (!buf)
		ERROR_Z_VAL(NULL)

	return buf;
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
