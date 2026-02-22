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

#ifdef __WIN32
#include <windows.h>
#include <shellapi.h>
#endif // __WIN32

#include <glib-object.h>
#include <glib/gstdio.h>
#include <gmime/gmime.h>
#include <zip.h>
#include <mupdf/pdf.h>
#include <mupdf/fitz.h>

#include "misc.h"
#include "sond_misc.h"
#include "sond_renderer.h"
#include "sond_treeviewfm.h"
#include "sond_log_and_error.h"
#include "sond_pdf_helper.h"
#include "sond_gmessage_helper.h"
#include "sond_file_helper.h"

//Grundlegendes Object, welches file_parts beschreibt
/*
 * parent: Objekt des Elternelements
 * 	NULL, wenn Datei in Filesystem
 * 	sfp_zip/sfp_pdf, wenn Objekt in zip-Archiv oder pdf-Datei gespeichert
 *
 * path: Pfad zum Elternelement
 */
typedef struct {
	gchar *path; //rel_path zum root-Element
	SondFilePart* parent; //NULL, wenn Datei in fs
	GPtrArray* arr_opened_files; //Kinder, wenn sfp Kinder haben kann (pdf, zip, GMessage)
	gboolean has_children;
} SondFilePartPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondFilePart, sond_file_part, G_TYPE_OBJECT)

static void sond_file_part_finalize(GObject* self) {
	SondFilePartPrivate *sfp_priv =
			sond_file_part_get_instance_private(SOND_FILE_PART(self));

	if (!sfp_priv->parent)  //ist "normale" Datei
		g_ptr_array_remove_fast(SOND_FILE_PART_CLASS(
				g_type_class_peek(SOND_TYPE_FILE_PART))->arr_opened_files, self);
	//falls in parent notiert, rauslöschen
	else {
		SondFilePartPrivate* sfp_parent_priv =
				sond_file_part_get_instance_private(sfp_priv->parent);

		if (sfp_parent_priv->arr_opened_files)
			g_ptr_array_remove_fast(sfp_parent_priv->arr_opened_files, self);
	}

	//array_opened_files löschen
	if (sfp_priv->arr_opened_files)
		g_ptr_array_unref(sfp_priv->arr_opened_files);

	g_free(sfp_priv->path);
	if (sfp_priv->parent)
		g_object_unref(sfp_priv->parent);

	G_OBJECT_CLASS(sond_file_part_parent_class)->finalize(self);

	return;
}

static void sond_file_part_class_init(SondFilePartClass *klass) {
	G_OBJECT_CLASS(klass)->finalize = sond_file_part_finalize;

	return;
}

static void sond_file_part_init(SondFilePart* self) {

	return;
}

static SondFilePart* sond_file_part_create(GType sfp_type, const gchar *path,
		SondFilePart* parent) {
	SondFilePart *sfp = NULL;
	SondFilePartPrivate *sfp_priv = NULL;
	GPtrArray *arr_opened_files = NULL;

	if (!parent)
		arr_opened_files = SOND_FILE_PART_CLASS(g_type_class_peek(
				SOND_TYPE_FILE_PART))->arr_opened_files;
	else {
		SondFilePartPrivate* sfp_parent_priv = sond_file_part_get_instance_private(parent);
		arr_opened_files = sfp_parent_priv->arr_opened_files;
	}

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
	if (arr_opened_files) //Array von geöffneten Dateien im Filesystem
		g_ptr_array_add(arr_opened_files, sfp);

	return sfp;
}

static gint sond_file_part_pdf_test_for_embedded_files(SondFilePartPDF*, GError**);
static gint sond_file_part_zip_test_for_files(SondFilePartZip*, GError**);
static gint sond_file_part_gmessage_test_for_multipart(SondFilePartGMessage*, GError**);

static gint sond_file_part_test_for_children(SondFilePart* sfp, GError** error) {
	gint rc = 0;

	if (SOND_IS_FILE_PART_PDF(sfp))
		rc = sond_file_part_pdf_test_for_embedded_files(SOND_FILE_PART_PDF(sfp), error);
	else if (SOND_IS_FILE_PART_ZIP(sfp))
		rc = sond_file_part_zip_test_for_files(SOND_FILE_PART_ZIP(sfp), error);
	else if (SOND_IS_FILE_PART_GMESSAGE(sfp))
		rc = sond_file_part_gmessage_test_for_multipart(SOND_FILE_PART_GMESSAGE(sfp), error);

	if (rc)
		ERROR_Z

	return 0;
}

SondFilePart* sond_file_part_create_from_mime_type(gchar const* path,
		SondFilePart* sfp_parent, gchar const* mime_type) {
	SondFilePart* sfp_child = NULL;
	GType type = 0;

	if (!g_strcmp0(mime_type, "application/pdf"))
		type = SOND_TYPE_FILE_PART_PDF;
	else if (!g_strcmp0(mime_type, "application/zip"))
		type = SOND_TYPE_FILE_PART_ZIP;
	else if (!g_strcmp0(mime_type, "message/rfc822"))
		type = SOND_TYPE_FILE_PART_GMESSAGE;
	else
		type = SOND_TYPE_FILE_PART_LEAF;

	sfp_child = sond_file_part_create(type, path, sfp_parent);

	//Nachbehandlung
	//häßlich, aber geht nicht in sond_file_part_..._init,
	//weil da versch. member (path, mime_type) noch nicht bekannt sind
	if (type == SOND_TYPE_FILE_PART_LEAF)
		sond_file_part_leaf_set_mime_type(SOND_FILE_PART_LEAF(sfp_child), mime_type);
	else {
		gint rc = 0;
		GError* error = NULL;

		rc = sond_file_part_test_for_children(sfp_child, &error);
		if (rc) {
			LOG_WARN("%s\n", error->message);
			g_error_free(error);
		}
	}

	return sfp_child;
}

static gchar* guess_content_type(fz_context* ctx, fz_stream* stream,
		gchar const* path, GError** error) {
	gchar* result = NULL;

    // Ersten Teil des Streams lesen (meist reichen 2KB für Erkennung)
    size_t buffer_size = 2048;
    size_t bytes_read = 0;
    unsigned char *buffer = g_malloc(buffer_size);

    // Daten lesen
    fz_try(ctx)
    	bytes_read = fz_read(ctx, stream, buffer, buffer_size);
    fz_catch(ctx) {
    	g_free(buffer);

    	ERROR_PDF_VAL(NULL)
    }

    result = mime_guess_content_type(buffer, bytes_read, error);
    g_free(buffer);

    return result;
}

SondFilePart* sond_file_part_create_from_stream(fz_context* ctx,
		fz_stream* stream, gchar const* path, SondFilePart* sfp_parent,
		GError** error) {
	gchar* mime_type = NULL;
	SondFilePart* sfp = NULL;

	mime_type = guess_content_type(ctx, stream, path, error);
	if (!mime_type)
		ERROR_Z_VAL(NULL)

	sfp = sond_file_part_create_from_mime_type(path, sfp_parent, mime_type);
	g_free(mime_type);

	return sfp;
}

SondFilePart* sond_file_part_get_parent(SondFilePart *sfp) {
	SondFilePartPrivate *sfp_priv = NULL;

	if (!sfp)
		return NULL;

	sfp_priv = sond_file_part_get_instance_private(sfp);

	return sfp_priv->parent;
}

void sond_file_part_set_parent(SondFilePart *sfp, SondFilePart* parent) {
	SondFilePartPrivate *sfp_priv = sond_file_part_get_instance_private(sfp);

	if (sond_file_part_get_arr_opened_files(sfp_priv->parent))
		g_ptr_array_remove_fast(sond_file_part_get_arr_opened_files(sfp_priv->parent), sfp);

	if (sfp_priv->parent)
			g_object_unref(sfp_priv->parent);

	if (parent)
		sfp_priv->parent = SOND_FILE_PART(g_object_ref(parent));
	else
		sfp_priv->parent = NULL;

	if (sond_file_part_get_arr_opened_files(parent))
		g_ptr_array_add(sond_file_part_get_arr_opened_files(parent), sfp);

	return;
}

gchar const* sond_file_part_get_path(SondFilePart *sfp) {
	SondFilePartPrivate *sfp_priv = NULL;

	if (!sfp)
		return NULL;

	sfp_priv = sond_file_part_get_instance_private(sfp);

	return sfp_priv->path;
}

void sond_file_part_set_path(SondFilePart *sfp, const gchar *path) {
	SondFilePartPrivate *sfp_priv = sond_file_part_get_instance_private(sfp);

	g_free(sfp_priv->path);
	sfp_priv->path = g_strdup(path);

	return;
}

gboolean sond_file_part_get_has_children(SondFilePart *sfp) {
	SondFilePartPrivate* sfp_priv =
			sond_file_part_get_instance_private(sfp);

	return sfp_priv->has_children;
}

void sond_file_part_set_has_children(SondFilePart *sfp, gboolean children) {
	SondFilePartPrivate* sfp_priv =
			sond_file_part_get_instance_private(sfp);

	sfp_priv->has_children = children;

	return;
}

GPtrArray* sond_file_part_get_arr_opened_files(SondFilePart* sfp) {
	GPtrArray* arr_opened_files = NULL;

	if (!sfp) //NULL = Filesystem
		arr_opened_files = SOND_FILE_PART_CLASS(g_type_class_peek(
				SOND_TYPE_FILE_PART))->arr_opened_files;
	else {
		SondFilePartPrivate* sfp_priv =
				sond_file_part_get_instance_private(sfp);

		arr_opened_files = sfp_priv->arr_opened_files;
	}

	//gibt NULL zurück, wenn sfp gar keine haben kann; sonst ggf. leeres Array
	return arr_opened_files;
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

static fz_stream* open_file(fz_context* ctx, gchar const* path,
		GError** error) {
	fz_stream* stream = NULL;

	fz_try(ctx)
		stream = fz_open_file(ctx, path);
	fz_catch(ctx) {
		if (error) *error = g_error_new(g_quark_from_static_string("mupdf"),
				fz_caught(ctx), "%s\nfz_open_file: %s", __func__,
				fz_caught_message(ctx));

		return NULL; //Fehler beim Öffnen des Streams
	}

	return stream;
}

/**
 * Liest aus GMimeStream für fz_stream next() Callback
 */
static int gmime_fz_stream_next(fz_context *ctx, fz_stream *stm, size_t max)
{
    GMimeStream *gmime_stream = (GMimeStream *)stm->state;
    unsigned char *buf = stm->rp;
    ssize_t n;

    n = g_mime_stream_read(gmime_stream, (char *)buf, max);

    if (n < 0)
        fz_throw(ctx, FZ_ERROR_GENERIC, "GMime stream read error");

    stm->rp = buf;
    stm->wp = buf + n;
    stm->pos += n;

    return n > 0 ? *stm->rp++ : EOF;
}

/**
 * Seek-Callback für fz_stream
 */
static void gmime_fz_stream_seek(fz_context *ctx, fz_stream *stm, int64_t offset, int whence)
{
    GMimeStream *gmime_stream = (GMimeStream *)stm->state;
    GMimeSeekWhence gmime_whence;

    switch (whence) {
        case SEEK_SET: gmime_whence = GMIME_STREAM_SEEK_SET; break;
        case SEEK_CUR: gmime_whence = GMIME_STREAM_SEEK_CUR; break;
        case SEEK_END: gmime_whence = GMIME_STREAM_SEEK_END; break;
        default: fz_throw(ctx, FZ_ERROR_GENERIC, "Invalid whence");
    }

    if (g_mime_stream_seek(gmime_stream, offset, gmime_whence) == -1)
        fz_throw(ctx, FZ_ERROR_GENERIC, "GMime stream seek failed");

    stm->pos = g_mime_stream_tell(gmime_stream);
    stm->rp = stm->wp;
}

/**
 * Drop-Callback für fz_stream (cleanup)
 */
static void gmime_fz_stream_drop(fz_context *ctx, void *state)
{
    GMimeStream *gmime_stream = (GMimeStream *)state;
    if (gmime_stream)
        g_object_unref(gmime_stream);
}

/**
 * Erstellt einen fz_stream aus einem GMimeStream
 *
 * @param ctx MuPDF-Kontext
 * @param gmime_stream GMimeStream (z.B. von g_mime_stream_mem_new())
 * @return fz_stream der von MuPDF verwendet werden kann
 *
 * WICHTIG: Der GMimeStream wird vom fz_stream verwaltet.
 *          Nach diesem Aufruf sollte der Caller seinen eigenen Ref mit
 *          g_object_unref() freigeben, wenn er den Stream nicht mehr benötigt.
 */
fz_stream* fz_open_gmime_stream(fz_context *ctx, GMimeStream *gmime_stream)
{
    fz_stream *stm;

    if (!gmime_stream)
        return NULL;

    // Ref count erhöhen, da fz_stream Ownership übernimmt
    g_object_ref(gmime_stream);

    // Stream an Anfang setzen
    g_mime_stream_reset(gmime_stream);

    // fz_stream erstellen
    stm = fz_new_stream(ctx, gmime_stream, gmime_fz_stream_next, gmime_fz_stream_drop);
    stm->seek = gmime_fz_stream_seek;

    return stm;
}

// =============================================================================
// VERWENDUNGSBEISPIEL
// =============================================================================

/*
// Von GMimePart zu fz_stream:
GMimePart *part = ...;
GMimeDataWrapper *content = g_mime_part_get_content(part);
GMimeStream *gmime_stream = g_mime_stream_mem_new();

// Dekodiert in Memory-Stream schreiben
g_mime_data_wrapper_write_to_stream(content, gmime_stream);
g_mime_stream_reset(gmime_stream);

// In fz_stream wrappen (seekable!)
fz_stream *fz_stm = fz_open_gmime_stream(ctx, gmime_stream);
g_object_unref(gmime_stream); // fz_stream hat jetzt die Kontrolle

// Verwenden
fz_archive *zip = fz_open_archive_with_stream(ctx, fz_stm);
// ...
fz_drop_stream(ctx, fz_stm);
*/

static fz_stream* sond_file_part_pdf_lookup_embedded_file(fz_context*,
		SondFilePartPDF*, gchar const*, GError**);

static zip_t* sond_file_part_zip_open_archive(SondFilePartZip*,
		gboolean, zip_source_t**, GError**);

static GMimeObject* sond_file_part_gmessage_lookup_part_by_path(SondFilePartGMessage*,
		gchar const*, GError**);

static fz_stream* get_istream(fz_context* ctx, SondFilePart* sfp_parent, gchar const* path,
		gboolean need_seekable, GError** error) {
	fz_stream* stream = NULL;

	//Datei im Filesystem
	if (!sfp_parent) {
		stream = open_file(ctx, path, error);
		if (!stream)
			ERROR_Z_VAL(NULL)
	}
	//Datei in PDF
	else if (SOND_IS_FILE_PART_PDF(sfp_parent)) {
		stream = sond_file_part_pdf_lookup_embedded_file(ctx,
				SOND_FILE_PART_PDF(sfp_parent), path, error);
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
		if (error) *error = g_error_new(SOND_ERROR, 0, "%s\nStream für SondFilePart-Typ"
			"kann nicht geöffnet werden", __func__);

		ERROR_Z_VAL(NULL)
	}

	return stream;
}

SondFilePart* sond_file_part_from_filepart(fz_context* ctx,
		gchar const* filepart, GError** error) {
	g_autoptr(SondFilePart) sfp = NULL;
	gchar** v_string = NULL;
	gint zaehler = 0;

	v_string = g_strsplit(filepart, "//", -1);

	while (v_string[zaehler])
	{
		fz_stream* stream = NULL;
		gchar* content_type = NULL;
		SondFilePart* sfp_child = NULL;

		stream = get_istream(ctx, sfp, v_string [zaehler], FALSE, error);
		if (!stream) {
			g_strfreev(v_string);
			ERROR_Z_VAL(NULL)
		}

		content_type = guess_content_type(ctx, stream, v_string[zaehler], error);
		fz_drop_stream(ctx, stream);
		if (!content_type){
			g_strfreev(v_string);
			ERROR_Z_VAL(NULL)
		}

		sfp_child = sond_file_part_create_from_mime_type(v_string[zaehler], sfp, content_type);
		g_free(content_type);

		sfp = sfp_child;
		zaehler++;
	}
	g_strfreev(v_string);

	return (sfp) ? g_object_ref(sfp) : NULL;
}

static GBytes* sond_file_part_zip_read_file(SondFilePartZip* sfp_zip,
		gchar const* path, GError** error) {
	zip_t* archive = NULL;
	zip_file_t* zf = NULL;
	zip_stat_t zstat = { 0 };
	guchar* data = NULL;

	archive = sond_file_part_zip_open_archive(sfp_zip, FALSE, NULL, error);
	if (!archive)
		ERROR_Z_VAL(NULL)

	if (zip_stat(archive, path, 0, &zstat) != 0 ||
			!(zstat.valid & ZIP_STAT_SIZE)) {
		zip_discard(archive);
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nDatei '%s' nicht im ZIP-Archiv oder Größe unbekannt",
				__func__, path);
		return NULL;
	}

	zf = zip_fopen(archive, path, 0);
	if (!zf) {
		zip_discard(archive);
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nzip_fopen('%s'): %s", __func__, path,
				zip_error_strerror(zip_get_error(archive)));
		return NULL;
	}

	data = g_malloc(zstat.size);
	zip_int64_t bytes_read = zip_fread(zf, data, zstat.size);
	zip_fclose(zf);
	zip_discard(archive);

	if (bytes_read < 0 || (zip_uint64_t)bytes_read != zstat.size) {
		g_free(data);
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nzip_fread('%s'): unvollständig gelesen", __func__, path);
		return NULL;
	}

	return g_bytes_new_take(data, (gsize)zstat.size);
}

static GBytes* sond_file_part_gmessage_read_part(SondFilePartGMessage* sfp_gmessage,
		gchar const* path, GError** error) {
	GMimeObject* object = NULL;
	GMimeStream* gmime_stream = NULL;
	gssize length = 0;

	object = sond_file_part_gmessage_lookup_part_by_path(sfp_gmessage, path, error);
	if (!object)
		ERROR_Z_VAL(NULL)

	gmime_stream = g_mime_stream_mem_new();

	if (GMIME_IS_MESSAGE_PART(object)) {
		GMimeObject* part =
				GMIME_OBJECT(g_mime_message_part_get_message(GMIME_MESSAGE_PART(object)));
		if (!part) {
			g_object_unref(object);
			g_object_unref(gmime_stream);
			if (error) *error = g_error_new(SOND_ERROR, 0,
					"%s\nGMimeMessagePart hat keine Nachricht", __func__);
			return NULL;
		}
		length = g_mime_object_write_to_stream(part, NULL, gmime_stream);
	}
	else if (GMIME_IS_PART(object)) {
		GMimeDataWrapper* wrapper = g_mime_part_get_content(GMIME_PART(object));
		length = g_mime_data_wrapper_write_to_stream(wrapper, gmime_stream);
	}
	else {
		g_object_unref(object);
		g_object_unref(gmime_stream);
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nGMimeObject ist kein zulässiger MimePart", __func__);
		return NULL;
	}

	g_object_unref(object);
	if (length <= 0) {
		g_object_unref(gmime_stream);
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nGMimeObject leer oder Fehler beim Schreiben", __func__);
		return NULL;
	}

	GByteArray* byte_array = g_mime_stream_mem_get_byte_array(GMIME_STREAM_MEM(gmime_stream));
	GBytes* result = g_bytes_new(byte_array->data, byte_array->len);
	g_object_unref(gmime_stream);

	return result;
}

static GBytes* sond_file_part_get_bytes(SondFilePart* sfp, GError** error) {
	SondFilePart* sfp_parent = sond_file_part_get_parent(sfp);
	gchar const* path = sond_file_part_get_path(sfp);

	if (SOND_IS_FILE_PART_ZIP(sfp_parent))
		return sond_file_part_zip_read_file(SOND_FILE_PART_ZIP(sfp_parent), path, error);

	if (SOND_IS_FILE_PART_GMESSAGE(sfp_parent))
		return sond_file_part_gmessage_read_part(SOND_FILE_PART_GMESSAGE(sfp_parent), path, error);

	/* Filesystem oder PDF-embedded: über fz_stream */
	guchar* data = NULL;
	gsize len = 0;

	if (!sfp_parent) {
		/* Filesystem: direkt mit GLib lesen */
		g_autofree gchar* full_path = g_strconcat(
				SOND_FILE_PART_CLASS(g_type_class_peek(SOND_TYPE_FILE_PART))->path_root,
				"/", path, NULL);
		if (!g_file_get_contents(full_path, (gchar**)&data, &len, error))
			ERROR_Z_VAL(NULL)
		return g_bytes_new_take(data, len);
	}

	/* PDF-embedded: über fz_stream */
	fz_context* ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
	if (!ctx) {
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nfz_new_context gibt NULL zurück", __func__);
		return NULL;
	}

	fz_stream* stream = get_istream(ctx, sfp_parent, path, FALSE, error);
	if (!stream) {
		fz_drop_context(ctx);
		ERROR_Z_VAL(NULL)
	}

	fz_buffer* buf = NULL;
	fz_try(ctx)
		buf = fz_read_all(ctx, stream, 0);
	fz_always(ctx)
		fz_drop_stream(ctx, stream);
	fz_catch(ctx) {
		fz_drop_context(ctx);
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nfz_read_all fehlgeschlagen", __func__);
		return NULL;
	}

	GBytes* result = g_bytes_new(buf->data, buf->len);
	fz_drop_buffer(ctx, buf);
	fz_drop_context(ctx);

	return result;
}

static gboolean
save_bytes_longpath(GBytes* bytes, const gchar *filename, GError **error)
{
    FILE *file = NULL;
    gconstpointer data = NULL;
    gsize len = 0;

    file = sond_fopen(filename, "wb", error);
    if (!file)
        return FALSE;

    data = g_bytes_get_data(bytes, &len);
    if (len > 0 && fwrite(data, 1, len, file) != len) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "%s\nfwrite fehlgeschlagen", __func__);
        fclose(file);
        return FALSE;
    }

    fclose(file);
    return TRUE;
}

gchar* sond_file_part_write_to_tmp_file(SondFilePart* sfp, GError **error) {
	gchar *filename = NULL;
	GBytes *bytes = NULL;

	bytes = sond_file_part_get_bytes(sfp, error);
	if (!bytes)
		ERROR_Z_VAL(NULL)

	filename = g_strdup_printf("%s/%d", g_get_tmp_dir(),
			g_random_int_range(10000, 99999));

#ifdef __WIN32__
	{
		gsize len = 0;
		gconstpointer data = g_bytes_get_data(bytes, &len);
		gchar* mime = mime_guess_content_type((const guchar*)data,
				MIN(len, (gsize)2048), error);
		if (!mime) {
			g_bytes_unref(bytes);
			g_free(filename);
			ERROR_Z_VAL(NULL)
		}
		gchar const* ext = mime_to_extension(mime);
		g_free(mime);
		filename = add_string(filename, g_strdup(ext));
	}
#endif

	if (!save_bytes_longpath(bytes, filename, error)) {
		g_bytes_unref(bytes);
		g_free(filename);
		ERROR_Z_VAL(NULL)
	}

	g_bytes_unref(bytes);
	return filename;
}

gint sond_file_part_open(SondFilePart* sfp, gboolean open_with,
		GError** error) {
	//hier alle Varianten, in denen eigener Viewer geöffnet wird
	if (!open_with &&
			SOND_IS_FILE_PART_LEAF(sfp) &&
			(g_str_has_prefix(sond_file_part_leaf_get_mime_type(
					SOND_FILE_PART_LEAF(sfp)), "text/") ||
			g_str_has_prefix(sond_file_part_leaf_get_mime_type(
					SOND_FILE_PART_LEAF(sfp)), "image/") ||
			!g_strcmp0("application/vnd.oasis.opendocument.text",
					sond_file_part_leaf_get_mime_type(
					SOND_FILE_PART_LEAF(sfp))) ||
			!g_strcmp0("application/vnd.openxmlformats-officedocument.wordprocessingml.document",
					sond_file_part_leaf_get_mime_type(
					SOND_FILE_PART_LEAF(sfp)))))
	{
		GBytes* bytes = NULL;
		gint rc = 0;
		fz_context* ctx = NULL;
		fz_buffer* buf = NULL;
		gsize len = 0;

		bytes = sond_file_part_get_bytes(sfp, error);
		if (!bytes)
			ERROR_Z

		ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
		if (!ctx) {
			g_bytes_unref(bytes);
			if (error) *error = g_error_new(SOND_ERROR, 0,
					"%s\ncontext konnte nicht erzeugt werden", __func__);
			return -1;
		}

		gconstpointer data = g_bytes_get_data(bytes, &len);
		fz_try(ctx)
			buf = fz_new_buffer_from_copied_data(ctx, data, len);
		fz_catch(ctx) {
			g_bytes_unref(bytes);
			fz_drop_context(ctx);
			if (error) *error = g_error_new(SOND_ERROR, 0,
					"%s\nfz_new_buffer_from_copied_data fehlgeschlagen", __func__);
			return -1;
		}
		g_bytes_unref(bytes);

		rc = sond_render(ctx, buf, NULL, error);
		fz_drop_buffer(ctx, buf);
		fz_drop_context(ctx);
		if (rc)
			ERROR_Z
	}/*
	else if (!open_with && SOND_IS_FILE_PART_PDF(sfp)) {

	}*/
	else {
		g_autofree gchar* path = NULL;

		if (!sond_file_part_get_parent(sfp)) //Datei im Filesystem
			path = g_strconcat(SOND_FILE_PART_CLASS(
					g_type_class_peek(SOND_TYPE_FILE_PART))->path_root,
					"/", sond_file_part_get_path(sfp), NULL);
		else { //Datei in zip/pdf/gmessage
			path = sond_file_part_write_to_tmp_file(sfp, error);
			if (!path)
				ERROR_Z
		}

		if (!sond_open(path, open_with, error)) {
			if (sond_file_part_get_parent(sfp)) { //tmp-Datei wurde erzeugt
				GError* error_rem = NULL;

				if (!sond_remove(path, &error_rem)) {
					LOG_WARN("Datei '%s' konnte nicht gelöscht werden:\n%s",
							path, error_rem->message);
					g_error_free(error_rem);
				}
			}
			ERROR_Z
		}
	}

	return 0;
}

static fz_buffer* sond_file_part_pdf_mod_emb_file(SondFilePartPDF*, fz_context*,
		gchar const*, fz_buffer*, GError**);

static GBytes* sond_file_part_zip_mod_zip_file(SondFilePartZip*,
		gchar const*, GBytes*, GError**);

static GBytes* sond_file_part_gmessage_mod_part(SondFilePartGMessage*,
		gchar const*, GBytes*, GError**);

static gint sond_file_part_replace(SondFilePart*, GBytes*, GError**);

gint sond_file_part_delete(SondFilePart* sfp, GError** error) {
	SondFilePart* sfp_parent = NULL;

	sfp_parent = sond_file_part_get_parent(sfp);

	if (!sfp_parent) { //Datei im Filesystem
		gboolean res = FALSE;
		gchar* path = NULL;
		SondFilePartPrivate* sfp_priv = sond_file_part_get_instance_private(sfp);

		path = g_strconcat(SOND_FILE_PART_CLASS(g_type_class_peek(SOND_TYPE_FILE_PART))->path_root,
				"/", sfp_priv->path, NULL);
		res = sond_remove(path, error);
		g_free(path);
		if (!res)
			ERROR_Z
	}
	else {
		gint rc = 0;
		GBytes* bytes_out = NULL;

		if (SOND_IS_FILE_PART_ZIP(sfp_parent))
			bytes_out = sond_file_part_zip_mod_zip_file(SOND_FILE_PART_ZIP(sfp_parent),
					sond_file_part_get_path(sfp), NULL, error);
		else if (SOND_IS_FILE_PART_PDF(sfp_parent)) {
			/* PDF braucht ctx und fz_buffer */
			fz_context* ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
			if (!ctx) {
				if (error) *error = g_error_new(SOND_ERROR, 0,
						"%s\nfz_new_context fehlgeschlagen", __func__);
				return -1;
			}
			fz_buffer* buf_pdf = sond_file_part_pdf_mod_emb_file(
					SOND_FILE_PART_PDF(sfp_parent), ctx,
					sond_file_part_get_path(sfp), NULL, error);
			if (buf_pdf) {
				bytes_out = g_bytes_new(buf_pdf->data, buf_pdf->len);
				fz_drop_buffer(ctx, buf_pdf);
			}
			fz_drop_context(ctx);
		}
		else if (SOND_IS_FILE_PART_GMESSAGE(sfp_parent))
			bytes_out = sond_file_part_gmessage_mod_part(SOND_FILE_PART_GMESSAGE(sfp_parent),
					sond_file_part_get_path(sfp), NULL, error);

		if (!bytes_out)
			ERROR_Z

		rc = sond_file_part_replace(sfp_parent, bytes_out, error);
		g_bytes_unref(bytes_out);
		if (rc)
			ERROR_Z

		rc = sond_file_part_test_for_children(sfp_parent, error);
		if (rc)
			ERROR_Z
	}

	return 0;
}

/**
 *	setzt bytes als neuen Inhalt von sfp in dessen parent
 */
static gint sond_file_part_replace(SondFilePart* sfp, GBytes* bytes, GError** error) {
	SondFilePart* sfp_parent = NULL;

	sfp_parent = sond_file_part_get_parent(sfp);

	if (!sfp_parent) { //Datei im Filesystem
		gint rc = 0;
		gchar* filename = NULL;

		rc = sond_file_part_delete(sfp, error);
		if (rc)
			ERROR_Z

		filename = g_strconcat(SOND_FILE_PART_CLASS(g_type_class_peek(SOND_TYPE_FILE_PART))->path_root,
				"/", sond_file_part_get_path(sfp), NULL);

		gboolean suc = save_bytes_longpath(bytes, filename, error);
		g_free(filename);
		if (!suc)
			ERROR_Z
	}
	else {
		gint rc = 0;
		GBytes* bytes_out = NULL;

		if (SOND_IS_FILE_PART_PDF(sfp_parent)) {
			/* PDF-Grenze: GBytes → fz_buffer → mod → fz_buffer → GBytes */
			fz_context* ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
			if (!ctx) {
				if (error) *error = g_error_new(SOND_ERROR, 0,
						"%s\nfz_new_context fehlgeschlagen", __func__);
				return -1;
			}
			gsize len = 0;
			gconstpointer data = g_bytes_get_data(bytes, &len);
			fz_buffer* buf_in = NULL;
			fz_try(ctx)
				buf_in = fz_new_buffer_from_copied_data(ctx, data, len);
			fz_catch(ctx) {
				fz_drop_context(ctx);
				if (error) *error = g_error_new(SOND_ERROR, 0,
						"%s\nfz_new_buffer_from_copied_data fehlgeschlagen", __func__);
				return -1;
			}
			fz_buffer* buf_out = sond_file_part_pdf_mod_emb_file(
					SOND_FILE_PART_PDF(sfp_parent), ctx,
					sond_file_part_get_path(sfp), buf_in, error);
			fz_drop_buffer(ctx, buf_in);
			if (buf_out) {
				bytes_out = g_bytes_new(buf_out->data, buf_out->len);
				fz_drop_buffer(ctx, buf_out);
			}
			fz_drop_context(ctx);
		}
		else if (SOND_IS_FILE_PART_ZIP(sfp_parent))
			bytes_out = sond_file_part_zip_mod_zip_file(SOND_FILE_PART_ZIP(sfp_parent),
					sond_file_part_get_path(sfp), bytes, error);
		else if (SOND_IS_FILE_PART_GMESSAGE(sfp_parent))
			bytes_out = sond_file_part_gmessage_mod_part(SOND_FILE_PART_GMESSAGE(sfp_parent),
					sond_file_part_get_path(sfp), bytes, error);

		if (!bytes_out)
			ERROR_Z

		rc = sond_file_part_replace(sfp_parent, bytes_out, error);
		g_bytes_unref(bytes_out);
		if (rc)
			ERROR_Z
	}

	return 0;
}

static gint sond_file_part_pdf_rename_embedded_file(SondFilePartPDF*,
		gchar const*, gchar const*, GError**);

static gint sond_file_part_zip_rename_file(SondFilePartZip*,
		gchar const*, gchar const*, GError**);

static gint sond_file_part_gmessage_rename_file(SondFilePartGMessage*,
		gchar const*, gchar const*, GError**);

gint sond_file_part_rename(SondFilePart* sfp, gchar const* path_new, GError** error) {
	SondFilePartPrivate* sfp_priv = NULL;
	gint rc = 0;

	g_return_val_if_fail(sfp, -1);

	sfp_priv = sond_file_part_get_instance_private(sfp);

	if (!sfp_priv->parent) {//sfp ist im fs gespeichert
		if (!sond_rename(sfp_priv->path, path_new, error))
			rc = -1;
	}
	else if (SOND_IS_FILE_PART_PDF(sfp_priv->parent))
		rc = sond_file_part_pdf_rename_embedded_file(SOND_FILE_PART_PDF(sfp_priv->parent),
				sfp_priv->path, path_new, error);
	else if (SOND_IS_FILE_PART_ZIP(sfp_priv->parent))
		rc = sond_file_part_zip_rename_file(SOND_FILE_PART_ZIP(sfp_priv->parent),
				sfp_priv->path, path_new, error);
	else if (SOND_IS_FILE_PART_GMESSAGE(sfp_priv->parent))
		rc = sond_file_part_gmessage_rename_file(SOND_FILE_PART_GMESSAGE(sfp_priv->parent),
				sfp_priv->path, path_new, error);
	else {
		if (error) *error = g_error_new(g_quark_from_static_string("sond"), 0,
				"Derzeit nicht implementiert");

		rc = -1;
	}

	if (rc)
		ERROR_Z

	//bei E-Mail nicht - da wird ja nur der Anzeigename geändert
	if (!SOND_IS_FILE_PART_GMESSAGE(sfp_priv->parent)) {
		g_free(sfp_priv->path);
		sfp_priv->path = g_strdup(path_new);
	}
	
	return 0;
}

static gchar const* sond_file_part_get_mime_type(SondFilePart* sfp) {
	if (SOND_IS_FILE_PART_LEAF(sfp))
		return sond_file_part_leaf_get_mime_type(SOND_FILE_PART_LEAF(sfp));
	else if (SOND_IS_FILE_PART_PDF(sfp))
		return "application/pdf";
	else if (SOND_IS_FILE_PART_ZIP(sfp))
		return "application/zip";
	else if (SOND_IS_FILE_PART_GMESSAGE(sfp))
		return "message/rfc822";

	return "application/octet-stream";
}

static gint sond_file_part_pdf_insert_embedded_file(SondFilePartPDF*,
		fz_context*, fz_buffer*, gchar const*, gchar const*, GError**);
static gint sond_file_part_zip_insert_zip_file(SondFilePartZip*,
		GBytes*, gchar const*, gchar const*, GError**);

static gint sond_file_part_insert(SondFilePart* sfp, GBytes* bytes,
		gchar const* filename, gchar const* mime_type, GError** error) {
	if (!sfp) { //Datei im Filesystem
		gchar* path = NULL;

		path = g_strconcat(SOND_FILE_PART_CLASS(g_type_class_peek_static(
				SOND_TYPE_FILE_PART))->path_root, "/", filename, NULL);

		if (sond_exists(path)) {
			if (error) *error = g_error_new(G_IO_ERROR, G_IO_ERROR_EXISTS,
					"%s\nDatei existiert", __func__);
			g_free(path);
			return -1;
		}

		gboolean suc = save_bytes_longpath(bytes, path, error);
		g_free(path);
		if (!suc)
			ERROR_Z
	}
	else {
		gint rc = 0;

		if (SOND_IS_FILE_PART_PDF(sfp)) {
			/* PDF-Grenze: GBytes → fz_buffer */
			fz_context* ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
			if (!ctx) {
				if (error) *error = g_error_new(SOND_ERROR, 0,
						"%s\nfz_new_context fehlgeschlagen", __func__);
				return -1;
			}
			gsize len = 0;
			gconstpointer data = g_bytes_get_data(bytes, &len);
			fz_buffer* buf = NULL;
			fz_try(ctx)
				buf = fz_new_buffer_from_copied_data(ctx, data, len);
			fz_catch(ctx) {
				fz_drop_context(ctx);
				if (error) *error = g_error_new(SOND_ERROR, 0,
						"%s\nfz_new_buffer_from_copied_data fehlgeschlagen", __func__);
				return -1;
			}
			rc = sond_file_part_pdf_insert_embedded_file(SOND_FILE_PART_PDF(sfp),
					ctx, buf, filename, mime_type, error);
			fz_drop_buffer(ctx, buf);
			fz_drop_context(ctx);
		}
		else if (SOND_IS_FILE_PART_ZIP(sfp))
			rc = sond_file_part_zip_insert_zip_file(SOND_FILE_PART_ZIP(sfp),
					bytes, filename, mime_type, error);
		else if (SOND_IS_FILE_PART_GMESSAGE(sfp)) {
			if (error) *error = g_error_new(SOND_ERROR, 0,
					"%s\nEinfügen in E-Mail noch nicht unterstützt", __func__);
			return -1;
		}

		if (rc)
			ERROR_Z
	}

	return 0;
}

gint sond_file_part_copy(SondFilePart* sfp_src,
		SondFilePart* sfp_dst, gchar const* path, GError** error) {
	GBytes* bytes = NULL;
	gint rc = 0;

	bytes = sond_file_part_get_bytes(sfp_src, error);
	if (!bytes)
		ERROR_Z

	rc = sond_file_part_insert(sfp_dst, bytes, path,
			sond_file_part_get_mime_type(sfp_src), error);
	g_bytes_unref(bytes);
	if (rc)
		ERROR_Z

	return 0;
}

/*
 * ZIPs
 */
G_DEFINE_TYPE(SondFilePartZip, sond_file_part_zip, SOND_TYPE_FILE_PART)

static void sond_file_part_zip_finalize(GObject *self) {
	G_OBJECT_CLASS(sond_file_part_zip_parent_class)->finalize(self);

	return;
}

static void sond_file_part_zip_class_init(SondFilePartZipClass *klass) {
	G_OBJECT_CLASS(klass)->finalize = sond_file_part_zip_finalize;

	return;
}

static void sond_file_part_zip_init(SondFilePartZip* self) {
	SondFilePartPrivate* sfp_priv =
			sond_file_part_get_instance_private(SOND_FILE_PART(self));

	sfp_priv->arr_opened_files = g_ptr_array_new( );

	return;
}

/**
 * Öffnet das ZIP-Archiv aus dem Buffer des übergeordneten Elements.
 * Bei writeable=TRUE wird src_out (falls nicht NULL) mit der zip_source_t* befüllt,
 * die nach zip_close() die geänderten Daten hält (für sond_file_part_zip_archive_to_buf_with_src).
 * Rückgabe: zip_t* (muss mit zip_discard() oder zip_close() freigegeben werden)
 */
static zip_t* sond_file_part_zip_open_archive(SondFilePartZip* sfp_zip,
		gboolean writeable, zip_source_t** src_out, GError** error) {
	GBytes* bytes = NULL;
	zip_error_t zip_error = { 0 };
	zip_source_t* src = NULL;
	zip_t* archive = NULL;
	void* data_copy = NULL;
	gsize data_len = 0;
	int flags = 0;

	bytes = sond_file_part_get_bytes(SOND_FILE_PART(sfp_zip), error);
	if (!bytes)
		ERROR_Z_VAL(NULL)

	/* libzip verwaltet den Puffer selbst (freep=1), daher eigene Kopie */
	gconstpointer raw = g_bytes_get_data(bytes, &data_len);
	data_copy = g_memdup2(raw, data_len);
	g_bytes_unref(bytes);
	if (!data_copy) {
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\ng_memdup2 fehlgeschlagen", __func__);
		return NULL;
	}

	zip_error_init(&zip_error);
	src = zip_source_buffer_create(data_copy, data_len, 1 /*freep*/, &zip_error);
	if (!src) {
		g_free(data_copy);
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nzip_source_buffer_create: %s", __func__,
				zip_error_strerror(&zip_error));
		zip_error_fini(&zip_error);
		return NULL;
	}

	flags = writeable ? 0 : ZIP_RDONLY;

	/* Bei writeable: ref erhöhen, damit src nach zip_close() noch verfügbar ist */
	if (writeable && src_out)
		zip_source_keep(src);

	archive = zip_open_from_source(src, flags, &zip_error);
	if (!archive) {
		if (writeable && src_out)
			zip_source_free(src); /* extra ref wieder freigeben */
		zip_source_free(src);
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nzip_open_from_source: %s", __func__,
				zip_error_strerror(&zip_error));
		zip_error_fini(&zip_error);
		return NULL;
	}
	zip_error_fini(&zip_error);

	if (writeable && src_out)
		*src_out = src;

	return archive;
}

/**
 * Schreibt ein geändertes ZIP-Archiv in einen fz_buffer.
 * src muss die buffer-basierte Quelle sein, die beim Öffnen des Archivs
 * erzeugt wurde. Das Archiv wird mit zip_close() geschlossen.
 */
static GBytes* sond_file_part_zip_archive_to_bytes_with_src(zip_t* archive,
		zip_source_t* src, GError** error) {
	zip_int64_t len = 0;

	/* Archiv schreiben: zip_close() schreibt Änderungen in die source zurück */
	if (zip_close(archive) != 0) {
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nzip_close: %s", __func__,
				zip_error_strerror(zip_source_error(src)));
		zip_discard(archive);
		return NULL;
	}

	/* Source öffnen und Länge bestimmen */
	if (zip_source_open(src) != 0) {
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nzip_source_open: %s", __func__,
				zip_error_strerror(zip_source_error(src)));
		return NULL;
	}

	zip_source_seek(src, 0, SEEK_END);
	len = zip_source_tell(src);
	zip_source_seek(src, 0, SEEK_SET);

	if (len <= 0) {
		zip_source_close(src);
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nzip_source hat 0 Bytes", __func__);
		return NULL;
	}

	guchar* data = g_malloc((gsize)len);
	zip_int64_t bytes_read = zip_source_read(src, data, (zip_uint64_t)len);
	zip_source_close(src);

	if (bytes_read != len) {
		g_free(data);
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nzip_source_read: gelesen=%" G_GINT64_FORMAT
				" erwartet=%" G_GINT64_FORMAT, __func__,
				(gint64)bytes_read, (gint64)len);
		return NULL;
	}

	return g_bytes_new_take(data, (gsize)len);
}

static gint sond_file_part_zip_test_for_files(SondFilePartZip* sfp_zip, GError** error) {
	zip_t* archive = NULL;
	zip_int64_t num_entries = 0;
	SondFilePartPrivate* sfp_priv = NULL;

	archive = sond_file_part_zip_open_archive(sfp_zip, FALSE, NULL, error);
	if (!archive)
		ERROR_Z

	num_entries = zip_get_num_entries(archive, 0);
	zip_discard(archive);

	sfp_priv = sond_file_part_get_instance_private(SOND_FILE_PART(sfp_zip));
	sfp_priv->has_children = (num_entries > 0);

	return 0;
}

/*
 * Listet eine Ebene des ZIP-Archivs.
 * prefix: Verzeichnispäfix ohne abschließenden '/', NULL = Wurzel.
 * Gibt GPtrArray* mit gchar* zurück; Verzeichnisse enden auf '/'.
 */
GPtrArray* sond_file_part_zip_list_dir(SondFilePartZip* sfp_zip,
		gchar const* prefix, GError** error) {
	zip_t* archive = NULL;
	zip_int64_t num_entries = 0;
	GHashTable* seen_dirs = NULL;
	GPtrArray* result = NULL;
	gsize prefix_len = prefix ? strlen(prefix) + 1 : 0; /* +1 für '/' */

	archive = sond_file_part_zip_open_archive(sfp_zip, FALSE, NULL, error);
	if (!archive)
		return NULL;

	num_entries = zip_get_num_entries(archive, 0);
	seen_dirs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	result = g_ptr_array_new_with_free_func(g_free);

	for (zip_int64_t i = 0; i < num_entries; i++) {
		gchar const* name = zip_get_name(archive, (zip_uint64_t)i, ZIP_FL_ENC_UTF_8);
		if (!name) continue;

		/* Prüfen ob Eintrag unter dem gewünschten Präfix liegt */
		if (prefix) {
			/* Muss mit "prefix/" beginnen */
			if (!g_str_has_prefix(name, prefix)) continue;
			if (name[strlen(prefix)] != '/') continue;
		}

		/* Relativer Pfad innerhalb dieser Ebene */
		gchar const* rel = name + prefix_len;
		if (*rel == '\0') continue; /* Der Präfix-Eintrag selbst (z.B. "dir/") */

		/* Suchen ob weiterer Schrägstrich folgt */
		gchar const* slash = strchr(rel, '/');

		if (!slash || *(slash + 1) == '\0') {
			/* Expliziter Verzeichnis-Eintrag (endet auf '/') - überspringen */
			if (slash && *(slash + 1) == '\0')
				continue;

			/* Normale Datei - vollständigen Pfad speichern */
			g_ptr_array_add(result, g_strdup(name));
		} else {
			/* Datei in Unterverzeichnis → Unterverzeichnis ableiten */
			gsize dir_len = (gsize)(slash - name);
			gchar* dir_key = g_strndup(name, dir_len + 1); /* inkl. '/' */

			if (!g_hash_table_contains(seen_dirs, dir_key)) {
				g_hash_table_add(seen_dirs, g_strdup(dir_key));
				g_ptr_array_add(result, dir_key);
			} else
				g_free(dir_key);
		}
	}

	zip_discard(archive);
	g_hash_table_destroy(seen_dirs);

	return result;
}

gchar* sond_file_part_zip_guess_mime(SondFilePartZip* sfp_zip,
		gchar const* path, GError** error) {
	zip_t* archive = NULL;
	zip_file_t* zf = NULL;
	guchar buf[2048];
	zip_int64_t n = 0;
	gchar* mime = NULL;

	archive = sond_file_part_zip_open_archive(sfp_zip, FALSE, NULL, error);
	if (!archive)
		return NULL;

	zf = zip_fopen(archive, path, 0);
	if (!zf) {
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nzip_fopen('%s'): %s", __func__, path,
				zip_error_strerror(zip_get_error(archive)));
		zip_discard(archive);
		return NULL;
	}

	n = zip_fread(zf, buf, sizeof(buf));
	zip_fclose(zf);
	zip_discard(archive);

	if (n <= 0) {
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nzip_fread('%s') fehlgeschlagen", __func__, path);
		return NULL;
	}

	mime = mime_guess_content_type(buf, (gsize)n, error);

	return mime;
}

static GBytes* sond_file_part_zip_mod_zip_file(SondFilePartZip* sfp_zip,
		gchar const* path, GBytes* bytes, GError** error) {
	zip_t* archive = NULL;
	zip_source_t* src = NULL;

	archive = sond_file_part_zip_open_archive(sfp_zip, TRUE, &src, error);
	if (!archive)
		ERROR_Z_VAL(NULL)

	if (!bytes) {
		/* Löschen: Datei im Archiv suchen und entfernen */
		zip_int64_t idx = zip_name_locate(archive, path, 0);
		if (idx < 0) {
			zip_source_free(src);
			zip_discard(archive);
			if (error) *error = g_error_new(SOND_ERROR, 0,
					"%s\nDatei '%s' nicht im ZIP-Archiv gefunden", __func__, path);
			return NULL;
		}

		if (zip_delete(archive, idx) != 0) {
			if (error) *error = g_error_new(SOND_ERROR, 0,
					"%s\nzip_delete('%s'): %s", __func__, path,
					zip_error_strerror(zip_get_error(archive)));
			zip_source_free(src);
			zip_discard(archive);
			return NULL;
		}
	} else {
		/* Ersetzen: Datei suchen und Inhalt aktualisieren */
		zip_source_t* entry_src = NULL;
		zip_error_t zip_error = { 0 };
		zip_int64_t idx = 0;
		gsize data_len = 0;
		gconstpointer data = g_bytes_get_data(bytes, &data_len);

		zip_error_init(&zip_error);
		entry_src = zip_source_buffer_create(data, data_len, 0 /*freep*/, &zip_error);
		if (!entry_src) {
			zip_source_free(src);
			zip_discard(archive);
			if (error) *error = g_error_new(SOND_ERROR, 0,
					"%s\nzip_source_buffer_create: %s", __func__,
					zip_error_strerror(&zip_error));
			zip_error_fini(&zip_error);
			return NULL;
		}
		zip_error_fini(&zip_error);

		idx = zip_name_locate(archive, path, 0);
		if (idx < 0) {
			/* Datei existiert nicht → neu hinzufügen */
			if (zip_file_add(archive, path, entry_src, ZIP_FL_ENC_UTF_8) < 0) {
				zip_source_free(entry_src);
				zip_source_free(src);
				if (error) *error = g_error_new(SOND_ERROR, 0,
						"%s\nzip_file_add('%s'): %s", __func__, path,
						zip_error_strerror(zip_get_error(archive)));
				zip_discard(archive);
				return NULL;
			}
		} else {
			/* Datei existiert → ersetzen */
			if (zip_file_replace(archive, (zip_uint64_t)idx, entry_src, ZIP_FL_ENC_UTF_8) != 0) {
				zip_source_free(entry_src);
				zip_source_free(src);
				if (error) *error = g_error_new(SOND_ERROR, 0,
						"%s\nzip_file_replace('%s'): %s", __func__, path,
						zip_error_strerror(zip_get_error(archive)));
				zip_discard(archive);
				return NULL;
			}
		}
	}

	GBytes* result = sond_file_part_zip_archive_to_bytes_with_src(archive, src, error);
	zip_source_free(src); /* extra ref freigeben */

	return result;
}

static gint sond_file_part_zip_rename_file(SondFilePartZip* sfp_zip,
		gchar const* path_old, gchar const* path_new, GError** error) {
	zip_t* archive = NULL;
	zip_int64_t idx = 0;
	gint rc = 0;
	zip_source_t* src = NULL;

	archive = sond_file_part_zip_open_archive(sfp_zip, TRUE, &src, error);
	if (!archive)
		ERROR_Z

	/* Prüfen, ob Zieldatei schon existiert */
	if (zip_name_locate(archive, path_new, 0) >= 0) {
		zip_source_free(src);
		zip_discard(archive);
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nDatei '%s' existiert bereits im ZIP-Archiv", __func__, path_new);
		return -1;
	}

	idx = zip_name_locate(archive, path_old, 0);
	if (idx < 0) {
		zip_source_free(src);
		zip_discard(archive);
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nDatei '%s' nicht im ZIP-Archiv gefunden", __func__, path_old);
		return -1;
	}

	if (zip_file_rename(archive, (zip_uint64_t)idx, path_new, ZIP_FL_ENC_UTF_8) != 0) {
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nzip_file_rename: %s", __func__,
				zip_error_strerror(zip_get_error(archive)));
		zip_source_free(src);
		zip_discard(archive);
		return -1;
	}

	GBytes* bytes_out = sond_file_part_zip_archive_to_bytes_with_src(archive, src, error);
	zip_source_free(src); /* extra ref freigeben */
	if (!bytes_out)
		ERROR_Z

	rc = sond_file_part_replace(SOND_FILE_PART(sfp_zip), bytes_out, error);
	g_bytes_unref(bytes_out);
	if (rc)
		ERROR_Z

	return 0;
}

static gint sond_file_part_zip_insert_zip_file(SondFilePartZip* sfp_zip,
		GBytes* bytes, gchar const* filename, gchar const* mime_type, GError** error) {
	zip_t* archive = NULL;
	zip_source_t* arch_src = NULL;
	zip_source_t* entry_src = NULL;
	zip_error_t zip_error = { 0 };
	gint rc = 0;

	archive = sond_file_part_zip_open_archive(sfp_zip, TRUE, &arch_src, error);
	if (!archive)
		ERROR_Z

	/* Prüfen, ob Datei mit dem Namen schon existiert */
	if (zip_name_locate(archive, filename, 0) >= 0) {
		zip_source_free(arch_src);
		zip_discard(archive);
		if (error) *error = g_error_new(SOND_ERROR, SOND_ERROR_EXISTS,
				"%s\nDatei '%s' existiert bereits im ZIP-Archiv", __func__, filename);
		return -1;
	}

	gsize data_len = 0;
	gconstpointer data = g_bytes_get_data(bytes, &data_len);

	zip_error_init(&zip_error);
	entry_src = zip_source_buffer_create(data, data_len, 0 /*freep*/, &zip_error);
	if (!entry_src) {
		zip_source_free(arch_src);
		zip_discard(archive);
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nzip_source_buffer_create: %s", __func__,
				zip_error_strerror(&zip_error));
		zip_error_fini(&zip_error);
		return -1;
	}
	zip_error_fini(&zip_error);

	if (zip_file_add(archive, filename, entry_src, ZIP_FL_ENC_UTF_8) < 0) {
		zip_source_free(entry_src);
		zip_source_free(arch_src);
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nzip_file_add('%s'): %s", __func__, filename,
				zip_error_strerror(zip_get_error(archive)));
		zip_discard(archive);
		return -1;
	}

	GBytes* bytes_out = sond_file_part_zip_archive_to_bytes_with_src(archive, arch_src, error);
	zip_source_free(arch_src);
	if (!bytes_out)
		ERROR_Z

	rc = sond_file_part_replace(SOND_FILE_PART(sfp_zip), bytes_out, error);
	g_bytes_unref(bytes_out);
	if (rc)
		ERROR_Z

	return 0;
}

/*
 * PDFs
 */
typedef struct {
	gchar* passwd;
	gint auth;
} SondFilePartPDFPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondFilePartPDF, sond_file_part_pdf, SOND_TYPE_FILE_PART)

static gint sond_file_part_pdf_authen_doc(SondFilePartPDF* sfp_pdf, fz_context* ctx,
		pdf_document* doc, gboolean prompt, GError** error) {
	gchar *password_try = NULL;

	SondFilePartPDFPrivate* sfp_pdf_priv = sond_file_part_pdf_get_instance_private(sfp_pdf);

	if (sfp_pdf_priv->passwd)
		password_try = sfp_pdf_priv->passwd;

	do {
		gint res_auth = 0;
		gint res_dialog = 0;

		fz_try(ctx)
			res_auth = pdf_authenticate_password(ctx, doc, password_try);
		fz_catch(ctx) {
			if (error) *error = g_error_new(g_quark_from_static_string("mupdf"), fz_caught(ctx),
					"%s\n%s", __func__, fz_caught_message(ctx));

			return -1;
		}
		if (res_auth) //erfolgreich!
		{
			sfp_pdf_priv->auth = res_auth;
			if (password_try) //Passwort überhaupt erforderlich
				sfp_pdf_priv->passwd = password_try;
			break;
		} else if (!prompt)
			return 1;

		res_dialog = dialog_with_buttons(NULL, "PDF verschlüsselt", "Passwort eingeben:",
				&password_try, "Ok", GTK_RESPONSE_OK, "Abbrechen",
				GTK_RESPONSE_CANCEL, NULL);
		if (res_dialog != GTK_RESPONSE_OK)
			return 1;
	} while (1);

	return 0;
}

pdf_document* sond_file_part_pdf_open_document(fz_context* ctx,
		SondFilePartPDF *sfp_pdf, gboolean prompt_for_passwd, GError **error) {
	gint rc = 0;
	pdf_document* doc = NULL;
	fz_stream* stream = NULL;

	stream = get_istream(ctx,
			sond_file_part_get_parent(SOND_FILE_PART(sfp_pdf)),
			sond_file_part_get_path(SOND_FILE_PART(sfp_pdf)), TRUE, error);
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
				"%s\nPDF-Dokument '%s' konnte nicht geöffnet werden:\n%s", __func__,
				sond_file_part_get_path(SOND_FILE_PART(sfp_pdf)),
				fz_caught_message(ctx));

		return NULL; //Fehler beim Öffnen des PDF-Dokuments
	}

	rc = sond_file_part_pdf_authen_doc(sfp_pdf, ctx, doc, prompt_for_passwd, error);
	if (rc) {
		if (rc == -1)
			g_prefix_error(error, "%s\n", __func__);
		else
			if (error) *error = g_error_new(g_quark_from_static_string("sond"), 1,
					"%s\nEntschlüsselung gescheitert", __func__);
		pdf_drop_document(ctx, doc);

		return NULL;
	}

	return doc;
}

gint sond_file_part_pdf_save_and_close(fz_context *ctx, pdf_document *pdf_doc,
		SondFilePartPDF* sfp_pdf, GError **error) {
	gint rc = 0;
	fz_buffer* buf = NULL;
	GBytes* bytes = NULL;

	buf = pdf_doc_to_buf(ctx, pdf_doc, error);
	if (!buf)
		ERROR_Z

	pdf_drop_document(ctx, pdf_doc);

	bytes = g_bytes_new(buf->data, buf->len);
	fz_drop_buffer(ctx, buf);

	rc = sond_file_part_replace(SOND_FILE_PART(sfp_pdf), bytes, error);
	g_bytes_unref(bytes);
	if (rc)
		ERROR_Z

	return 0;
}

typedef struct {
	SondFilePartPDF* sfp_pdf;
	GPtrArray* arr_embedded_files;
}Load;

static gint load_embedded_files(fz_context* ctx, pdf_obj* names, pdf_obj* key,
		pdf_obj* val, gpointer data, GError** error) {
	pdf_obj* EF_F = NULL;
	gchar const* path_embedded = NULL;
	fz_stream* stream = NULL;
	gchar* content_type = NULL;
	SondFilePart* sfp_embedded_file = NULL;
	Load* load = (Load*) data;

	EF_F = pdf_get_EF_F(ctx, val, &path_embedded, error);
	if (!EF_F)
		ERROR_Z

	fz_try(ctx)
		stream = pdf_open_stream(ctx, EF_F);
	fz_catch(ctx) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("mupdf"),
					fz_caught(ctx), "%s\n%s", __func__,
					fz_caught_message(ctx));

		return -1;
	}

	content_type = guess_content_type(ctx, stream, path_embedded, error);
	fz_drop_stream(ctx, stream);
	if (!content_type)
		ERROR_Z

	sfp_embedded_file = sond_file_part_create_from_mime_type(
			path_embedded, SOND_FILE_PART(load->sfp_pdf), content_type);
	g_free(content_type);

	g_ptr_array_add(load->arr_embedded_files, sfp_embedded_file);

	return 0;
}

gint sond_file_part_pdf_load_embedded_files(SondFilePartPDF* sfp_pdf,
		GPtrArray** arr_children, GError **error) {
	gint rc = 0;
	fz_context* ctx = NULL;
	pdf_document* doc = NULL;
	Load load = { 0 };

	ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
	if (!ctx) {
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nfz_new_context gibt NULL zurück", __func__);
		return -1;
	}

	doc = sond_file_part_pdf_open_document(ctx, sfp_pdf, FALSE, error);
	if (!doc) {
		fz_drop_context(ctx);
		ERROR_Z
	}

	load.sfp_pdf = SOND_FILE_PART_PDF(sfp_pdf);
	load.arr_embedded_files = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	rc = pdf_walk_embedded_files(ctx, doc, load_embedded_files, &load, error);
	pdf_drop_document(ctx, doc);
	fz_drop_context(ctx);
	if (rc) {
		g_ptr_array_unref(load.arr_embedded_files);
		ERROR_Z
	}

	if (load.arr_embedded_files->len == 0) { //darf ja nicht sein
		g_ptr_array_unref(load.arr_embedded_files);
		if (error) *error = g_error_new(g_quark_from_static_string("sond"),
				0, "%s\nKein embedded file gefunden", __func__);

		return -1;
	}

	*arr_children = load.arr_embedded_files;

	return 0;
}

static void sond_file_part_pdf_class_init(SondFilePartPDFClass *klass) {

	return;
}

static gint test_for_emb_files(fz_context* ctx, pdf_obj* names, pdf_obj* key,
		pdf_obj* val, gpointer data, GError** error) {
	gboolean* has_emb_files = (gboolean*) data;

	*has_emb_files = TRUE;

	return 1; //kann sofort abgebrochen werden
}

static gint sond_file_part_pdf_test_for_embedded_files(
		SondFilePartPDF *sfp_pdf, GError **error) {
	gint rc = 0;
	fz_context* ctx = NULL;
	pdf_document* doc = NULL;
	SondFilePartPrivate* sfp_priv = NULL;
	gboolean has_emb_file = FALSE;

	ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
	if (!ctx) {
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nfz_new_context gibt NULL zurück", __func__);

		return -1;
	}

	doc = sond_file_part_pdf_open_document(ctx, sfp_pdf, FALSE, error);
	if (!doc) {
		fz_drop_context(ctx);
		ERROR_Z
	}

	rc = pdf_walk_embedded_files(ctx, doc, test_for_emb_files, &has_emb_file, error);
	pdf_drop_document(ctx, doc);
	fz_drop_context(ctx);
	if (rc)
		ERROR_Z

	sfp_priv = sond_file_part_get_instance_private(SOND_FILE_PART(sfp_pdf));
	if (has_emb_file)
		sfp_priv->has_children = TRUE;
	else
		sfp_priv->has_children = FALSE; //aktiv FALSE setzen - kann sich ja ändern

	return 0;
}

static void sond_file_part_pdf_init(SondFilePartPDF* self) {
	SondFilePartPrivate *sfp_priv =
			sond_file_part_get_instance_private(SOND_FILE_PART(self));

	sfp_priv->arr_opened_files = g_ptr_array_new( );

	return;
}

typedef struct {
	gchar const* path_search;
	fz_stream* stream;
} Lookup;

static gint lookup_embedded_file(fz_context* ctx, pdf_obj* names, pdf_obj* key,
		pdf_obj* val, gpointer data, GError** error) {
	pdf_obj* EF_F = NULL;
	gchar const* path_embedded = NULL;
	Lookup* lookup = (Lookup*) data;

	EF_F = pdf_get_EF_F(ctx, val, &path_embedded, error);
	if (!EF_F)
		ERROR_Z

	if (g_strcmp0(path_embedded, lookup->path_search) == 0) {
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

static fz_stream* sond_file_part_pdf_lookup_embedded_file(fz_context* ctx,
		SondFilePartPDF* sfp_pdf, gchar const* path, GError** error) {
	Lookup lookup = { 0 };
	gint rc = 0;
	pdf_document* doc = NULL;

	doc = sond_file_part_pdf_open_document(ctx,
			sfp_pdf, FALSE, error);
	if (!doc)
		ERROR_Z_VAL(NULL)

	lookup.path_search = path;
	rc = pdf_walk_embedded_files(ctx, doc, lookup_embedded_file, &lookup, error);
	pdf_drop_document(ctx, doc);
	if (rc)
		ERROR_Z_VAL(NULL)

	if (!lookup.stream) { //Datei nicht gefunden
		if (error) *error = g_error_new(g_quark_from_static_string("sond"),
				0, "%s\embedded file '%s' nicht gefunden", __func__, path);

		return NULL;
	}

	return lookup.stream;
}

typedef struct {
	gchar const* path;
	fz_buffer* buf;
	gboolean found;
} Modify;

static gint delete_embedded_file(fz_context* ctx, pdf_obj*names, pdf_obj* key,
		pdf_obj* val, gpointer data, GError** error) {
	pdf_obj* EF_F = NULL;
	gchar const* path_embedded = NULL;
	Modify* modify = (Modify*) data;

	EF_F = pdf_get_EF_F(ctx, val, &path_embedded, error);
	if (!EF_F)
		ERROR_Z

	if (g_strcmp0(path_embedded, modify->path) == 0) {
		gint index = 0;

		fz_try(ctx)
			index = pdf_array_find(ctx, names, key);
		fz_catch(ctx)
			ERROR_PDF

		if (index == -1) {
			if (error) *error = g_error_new(SOND_ERROR, 0,
					"%s\nkey nicht gefunden", __func__);

			return -1;
		}

		fz_try(ctx) {
			pdf_array_delete(ctx, names, index);
			pdf_array_delete(ctx, names, index);
		}
		fz_catch(ctx)
			ERROR_PDF

		modify->found = TRUE;

		return 1;
	}

	return 0;
}

static gint modify_embedded_file(fz_context* ctx, pdf_obj* names, pdf_obj* key,
		pdf_obj* val, gpointer data, GError** error) {
	pdf_obj* EF_F = NULL;
	gchar const* path_embedded = NULL;
	Modify* modify = (Modify*) data;

	EF_F = pdf_get_EF_F(ctx, val, &path_embedded, error);
	if (!EF_F)
		ERROR_Z

	if (g_strcmp0(path_embedded, modify->path) == 0) {
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

		modify->found = TRUE;

		return 1;
	}

	return 0;
}

static fz_buffer* sond_file_part_pdf_mod_emb_file(SondFilePartPDF* sfp_pdf,
		fz_context* ctx, gchar const* path, fz_buffer* buf, GError** error) {
	pdf_document* doc = NULL;
	gint rc = 0;
	fz_buffer* buf_out = NULL;
	Modify modify = { path, buf, FALSE };

	doc = sond_file_part_pdf_open_document(ctx, sfp_pdf, TRUE, error);
	if (!doc)
		ERROR_Z_VAL(NULL)

	//mod embedded file
	rc = pdf_walk_embedded_files(ctx, doc,
			(buf) ? modify_embedded_file : delete_embedded_file, &modify, error);
	if (rc) {
		pdf_drop_document(ctx, doc);
		ERROR_Z_VAL(NULL)
	}

	if (!modify.found) {
		pdf_drop_document(ctx, doc);
		if (error) *error = g_error_new(g_quark_from_static_string("sond"),
				0, "%s\nembedded file '%s' nicht gefunden", __func__, path);

		return NULL;
	}

	//write pdf to other buffer
	buf_out = pdf_doc_to_buf(ctx, doc, error);
	pdf_drop_document(ctx, doc);
	if (!buf_out)
		ERROR_Z_VAL(NULL)

	return buf_out;
}

static gint look_for_embedded_file(fz_context* ctx, pdf_obj* names, pdf_obj* key,
		pdf_obj* val, gpointer data, GError** error) {
	pdf_obj* EF_F = NULL;
	gchar const* path_embedded = NULL;
	gchar const* path = (gchar const*) data;

	EF_F = pdf_get_EF_F(ctx, val, &path_embedded, error);
	if (!EF_F)
		ERROR_Z

	if (g_strcmp0(path_embedded, path) == 0)
		return 1;

	return 0;
}

typedef struct {
	gchar const* path_old;
	gchar const* path_new;
	gboolean found;
} Rename;

static gint rename_embedded_file(fz_context* ctx, pdf_obj* names, pdf_obj* key,
		pdf_obj* val, gpointer data, GError** error) {
	gchar const* path_tmp = NULL;
	pdf_obj* F = NULL;
	pdf_obj* UF = NULL;
	Rename* rename = (Rename*) data;

	if (!pdf_is_dict(ctx, val)) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("sond"),
					0, "%s\nnamestree malformed", __func__);
		return -1;
	}

	fz_try(ctx) {
		F = pdf_dict_get(ctx, val, PDF_NAME(F));
		UF = pdf_dict_get(ctx, val, PDF_NAME(UF));

		if (pdf_is_string(ctx, UF))
			path_tmp = pdf_to_text_string(ctx, UF);
		else if (pdf_is_string(ctx, F))
			path_tmp = pdf_to_text_string(ctx, F);
	}
	fz_catch(ctx) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("mupdf"),
					fz_caught(ctx), "%s\n%s", __func__,
					fz_caught_message(ctx));

		return -1;
	}

	if (!path_tmp) {
		if (error)
			*error = g_error_new(SOND_ERROR, 0, "%s\nEingebettete Datei hat keinen Pfad",
					__func__);
		return -1;
	}

	if (g_strcmp0(path_tmp, rename->path_old) != 0)
		return 0; //nächstes

	fz_try(ctx) {
		pdf_dict_put_text_string(ctx, val, PDF_NAME(F), rename->path_new);
		pdf_dict_put_text_string(ctx, val, PDF_NAME(UF), rename->path_new);
	}
	fz_catch(ctx) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("mupdf"),
					fz_caught(ctx), "%s\n%s", __func__,
					fz_caught_message(ctx));

		return -1;
	}

	return 1;
}

static gint sond_file_part_pdf_rename_embedded_file(SondFilePartPDF* sfp_pdf,
		gchar const* path_old, gchar const* path_new, GError** error) {
	gint rc = 0;
	fz_context* ctx = NULL;
	pdf_document* doc = NULL;
	Rename rename = {path_old, path_new, FALSE};

	ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
	if (!ctx) {
		if (error) *error = g_error_new(g_quark_from_static_string("mupdf"), 0,
				"%s\nfz_new_context gibt NULL zurück", __func__);
		return -1;
	}

	doc = sond_file_part_pdf_open_document(ctx, sfp_pdf, TRUE, error);
	if (!doc) {
		fz_drop_context(ctx);
		ERROR_Z
	}

	//erster Durchgang: gibt's schon embFile mit Namen, in den umgenannt werden soll?
	rc = pdf_walk_embedded_files(ctx, doc, look_for_embedded_file,
			&rename, error);
	if (rc) {
		pdf_drop_document(ctx, doc);
		fz_drop_context(ctx);

		ERROR_Z
	}

	if (rename.found) { //Ziel-Datei existiert schon!
		pdf_drop_document(ctx, doc);
		fz_drop_context(ctx);
		if (error) *error = g_error_new(g_quark_from_static_string("sond"),
				0, "%s\nDatei '%s' existiert bereits als embedded file",
				__func__, rename.path_new);

		return -1;
	}

	rc = pdf_walk_embedded_files(ctx, doc, rename_embedded_file,
			&rename, error);
	if (rc) {
		pdf_drop_document(ctx, doc);
		fz_drop_context(ctx);
		ERROR_Z
	}

	rc = sond_file_part_pdf_save_and_close(ctx, doc, sfp_pdf, error);
	fz_drop_context(ctx);
	if (rc)
		ERROR_Z

	return 0;
}

static gint sond_file_part_pdf_insert_embedded_file(SondFilePartPDF* sfp_pdf,
		fz_context* ctx, fz_buffer* buf, gchar const* filename,
		gchar const* mime_type, GError** error) {
	gint rc = 0;
	pdf_document* doc = NULL;

	doc = sond_file_part_pdf_open_document(ctx, sfp_pdf, TRUE, error);
	if (!doc)
		ERROR_Z

	rc = pdf_insert_emb_file(ctx, doc, buf, filename, mime_type, error);
	if (rc) {
		pdf_drop_document(ctx, doc);
		ERROR_Z
	}

	rc = sond_file_part_pdf_save_and_close(ctx, doc, sfp_pdf, error);
	if (rc)
		ERROR_Z

	return 0;
}

//GMessage
typedef struct {
	GMimeMessage* message;
} SondFilePartGMessagePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondFilePartGMessage, sond_file_part_gmessage, SOND_TYPE_FILE_PART)

static void sond_file_part_gmessage_init(SondFilePartGMessage *self) {
	SondFilePartPrivate *sfp_priv =
			sond_file_part_get_instance_private(SOND_FILE_PART(self));

	sfp_priv->arr_opened_files = g_ptr_array_new( );

	return;
}

static void sond_file_part_gmessage_class_init(SondFilePartGMessageClass *klass) {

	return;
}

static void sond_file_part_gmessage_close(SondFilePartGMessage* sfp_gmessage) {
	SondFilePartGMessagePrivate* sfp_gmessage_priv =
			sond_file_part_gmessage_get_instance_private(sfp_gmessage);

	if (sfp_gmessage_priv->message == NULL) {
		LOG_WARN("%s\nSondFilePartGMessage war schon geschlossen", __func__);

		return;
	}

	g_object_unref(sfp_gmessage_priv->message);

	return;
}

static gint sond_file_part_gmessage_open(SondFilePartGMessage* sfp_gmessage,
		GError** error) {
	SondFilePartGMessagePrivate* sfp_gmessage_priv =
			sond_file_part_gmessage_get_instance_private(sfp_gmessage);

	if (sfp_gmessage_priv->message)
		return 0; //bereits geöffnet

	GBytes* bytes = sond_file_part_get_bytes(SOND_FILE_PART(sfp_gmessage), error);
	if (!bytes)
		ERROR_Z

	gsize len = 0;
	gconstpointer data = g_bytes_get_data(bytes, &len);
	sfp_gmessage_priv->message = gmessage_open((const guchar*)data, len);
	g_bytes_unref(bytes);

	if (!sfp_gmessage_priv->message) {
		g_set_error(error, SOND_ERROR, 0,
				"%s\nGMessage öffnen fehlgeschlagen", __func__);
		return -1;
	}

    // Weak pointer registrieren für den Fall, dass andere das Objekt zerstören
    g_object_add_weak_pointer(G_OBJECT(sfp_gmessage_priv->message),
            (gpointer*) &sfp_gmessage_priv->message);

    return 0;
}

static gint sond_file_part_gmessage_test_for_multipart(SondFilePartGMessage* sfp_gmessage,
		GError** error) {
	gint rc = 0;
	GMimeObject* root = NULL;

	SondFilePartPrivate* sfp_priv =
			sond_file_part_get_instance_private(SOND_FILE_PART(sfp_gmessage));
	SondFilePartGMessagePrivate* sfp_gmessage_priv =
			sond_file_part_gmessage_get_instance_private(sfp_gmessage);

	rc = sond_file_part_gmessage_open(sfp_gmessage, error);
	if (rc)
		ERROR_Z

	root = g_mime_message_get_mime_part(sfp_gmessage_priv->message);
	if (root)//Auch wenn nur ein einziger part, ist es ein Kind von Message
		sfp_priv->has_children = TRUE;
	sond_file_part_gmessage_close(sfp_gmessage);

	return 0;
}

static GMimeObject* sond_file_part_gmessage_lookup_part_by_path(
		SondFilePartGMessage* sfp_gmessage, gchar const* path, GError** error) {
	gint rc = 0;
	GMimeObject* object = NULL;

	SondFilePartGMessagePrivate* sfp_gmessage_priv =
			sond_file_part_gmessage_get_instance_private(sfp_gmessage);

	rc = sond_file_part_gmessage_open(sfp_gmessage, error);
	if (rc)
		ERROR_Z_VAL(NULL)

	object = gmessage_lookup_part_by_path(sfp_gmessage_priv->message, path, error);
	sond_file_part_gmessage_close(sfp_gmessage);
	if (!object)
		ERROR_Z_VAL(NULL)

	return object;
}

gint sond_file_part_gmessage_load_path(SondFilePartGMessage* sfp_gmessage,
		gchar const* path, GPtrArray** arr_mime_parts, GError** error) {
	GMimeObject* object = NULL;

	object = sond_file_part_gmessage_lookup_part_by_path(
				sfp_gmessage, path, error);
	if (!object)
		ERROR_Z

	*arr_mime_parts = g_ptr_array_new_with_free_func(g_object_unref);

	if (GMIME_IS_MESSAGE_PART(object) || GMIME_IS_PART(object)) {
		g_ptr_array_add(*arr_mime_parts, object);

		return 0;
	}

	if (GMIME_IS_MULTIPART(object)) {
		for (gint i = 0; i < g_mime_multipart_get_count(GMIME_MULTIPART(object)); i++) {
			GMimeObject* mime_part = g_mime_multipart_get_part(GMIME_MULTIPART(object), i);

			g_ptr_array_add(*arr_mime_parts, g_object_ref(mime_part));
		}

		g_object_unref(object);
	}
	else {
		g_ptr_array_unref(*arr_mime_parts);
		g_object_unref(object);
		if (error)
			*error = g_error_new(SOND_ERROR, 0,
					"%s\nPart mit Pfad '%s' ist kein Multipart",
					__func__, path);
		return -1;
	}

	return 0;
}

static GBytes* sond_file_part_gmessage_to_bytes(SondFilePartGMessage* sfp_gmessage,
		GError** error) {
	gsize length = 0;

	SondFilePartGMessagePrivate* sfp_gmessage_priv =
			sond_file_part_gmessage_get_instance_private(sfp_gmessage);

	if (!sfp_gmessage_priv->message) {
		g_set_error(error, SOND_ERROR, 0, "%s\nGMessage nicht geöffnet", __func__);
		return NULL;
	}

	GMimeStream* stream = g_mime_stream_mem_new();

	length = g_mime_object_write_to_stream(GMIME_OBJECT(sfp_gmessage_priv->message),
			NULL, stream);
	if (length < 0) {
		g_set_error(error, SOND_ERROR, 0, "%s\nFehler beim Schreiben der Message in stream",
				__func__);
		g_object_unref(stream);
		return NULL;
	}
	else if (length == 0) {
		g_set_error(error, SOND_ERROR, 0, "%s\n0 bytes in stream geschrieben", __func__);
		g_object_unref(stream);
		return NULL;
	}

	GByteArray* byte_array = g_mime_stream_mem_get_byte_array(GMIME_STREAM_MEM(stream));
	GBytes* result = g_bytes_new(byte_array->data, byte_array->len);
	g_object_unref(stream);

	return result;
}

static GBytes* sond_file_part_gmessage_mod_part(SondFilePartGMessage* sfp_gmessage,
		gchar const* path, GBytes* bytes, GError** error) {
	gint rc = 0;

	SondFilePartGMessagePrivate* sfp_gmessage_priv =
			sond_file_part_gmessage_get_instance_private(sfp_gmessage);

	rc = sond_file_part_gmessage_open(sfp_gmessage, error);
	if (rc)
		ERROR_Z_VAL(NULL)

	gsize data_len = 0;
	gconstpointer data = bytes ? g_bytes_get_data(bytes, &data_len) : NULL;

	rc = gmessage_mod_part(sfp_gmessage_priv->message, path,
			(guchar*)data, data_len, error);
	if (rc) {
		sond_file_part_gmessage_close(sfp_gmessage);
		ERROR_Z_VAL(NULL)
	}

	GBytes* result = sond_file_part_gmessage_to_bytes(sfp_gmessage, error);
	sond_file_part_gmessage_close(sfp_gmessage);
	if (!result)
		ERROR_Z_VAL(NULL)

	return result;
}


static gint sond_file_part_gmessage_save(SondFilePartGMessage* sfp_gmessage,
		GError** error) {
	GBytes* bytes = NULL;
	gint rc = 0;

	bytes = sond_file_part_gmessage_to_bytes(sfp_gmessage, error);
	if (!bytes)
		ERROR_Z

	rc = sond_file_part_replace(SOND_FILE_PART(sfp_gmessage), bytes, error);
	g_bytes_unref(bytes);
	if (rc)
		ERROR_Z

	return 0;
}

static gint sond_file_part_gmessage_rename_file(SondFilePartGMessage* sfp_gmessage,
		gchar const* path_old, gchar const* path_new, GError** error) {
	gint rc = 0;

	SondFilePartGMessagePrivate* sfp_gmessage_priv =
			sond_file_part_gmessage_get_instance_private(sfp_gmessage);

	rc = sond_file_part_gmessage_open(sfp_gmessage, error);
	if (rc)
		ERROR_Z

	rc = gmessage_set_filename(sfp_gmessage_priv->message, path_old, path_new, error);
	if (rc) {
		sond_file_part_gmessage_close(sfp_gmessage);

		ERROR_Z
	}

	rc = sond_file_part_gmessage_save(sfp_gmessage, error);
	sond_file_part_gmessage_close(sfp_gmessage);
	if (rc)
		ERROR_Z

	return 0;
}

/*
 * Leafs
 */
typedef struct {
	gchar* mime_type;
} SondFilePartLeafPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondFilePartLeaf, sond_file_part_leaf, SOND_TYPE_FILE_PART)

static void sond_file_part_leaf_finalize(GObject *self) {
	SondFilePartLeafPrivate *sfp_leaf_priv =
			sond_file_part_leaf_get_instance_private(SOND_FILE_PART_LEAF(self));

	g_free(sfp_leaf_priv->mime_type);

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

gchar const* sond_file_part_leaf_get_mime_type(SondFilePartLeaf *sfp_leaf) {
	SondFilePartLeafPrivate *sfp_leaf_priv =
			sond_file_part_leaf_get_instance_private(sfp_leaf);

	return sfp_leaf_priv->mime_type;
}

void sond_file_part_leaf_set_mime_type(SondFilePartLeaf* sfp_leaf, gchar const* mime_type) {
	SondFilePartLeafPrivate* sfp_leaf_priv =
			sond_file_part_leaf_get_instance_private(sfp_leaf);

	g_free(sfp_leaf_priv->mime_type);
	sfp_leaf_priv->mime_type = g_strdup(mime_type);

	return;
}
