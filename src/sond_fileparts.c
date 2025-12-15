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
#include <magic.h>

#include "misc.h"
#include "sond.h"
#include "sond_renderer.h"
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
static void sond_file_part_leaf_set_mime_type(SondFilePartLeaf*, gchar const*);

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
			g_warning("%s\n", error->message);
			g_error_free(error);
		}
	}

	return sfp_child;
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
	SondFilePartPrivate *sfp_priv = sond_file_part_get_instance_private(sfp);

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

static gchar* guess_content_type(fz_context* ctx, fz_stream* stream,
		gchar const* path, GError** error) {
    magic_t magic = magic_open(MAGIC_MIME_TYPE);
    if (!magic) {
    	if (error) *error = g_error_new(SOND_ERROR, 0,
    			"%s\nmagic_open fehlgeschlagen", __func__);

        return NULL;
    }

    if (magic_load(magic, NULL) != 0) {
        magic_close(magic);
    	if (error) *error = g_error_new(SOND_ERROR, 0,
    			"%s\nmagic_load fehlgeschlagen", __func__);

        return NULL;
    }

    // Ersten Teil des Streams lesen (meist reichen 2KB für Erkennung)
    size_t buffer_size = 2048;
    size_t bytes_read = 0;
    unsigned char *buffer = g_malloc(buffer_size);

    // Daten lesen
    fz_try(ctx)
    	bytes_read = fz_read(ctx, stream, buffer, buffer_size);
    fz_catch(ctx) {
    	g_free(buffer);
    	magic_close(magic);
    	if (error) *error = g_error_new(g_quark_from_static_string("mupdf"),
    			fz_caught(ctx), "%s\n%s", __func__, fz_caught_message(ctx));

    	return NULL;
    }

    // MIME-Typ aus Puffer erkennen
    const char* mime = magic_buffer(magic, buffer, bytes_read);
    char* result = mime ? strdup(mime) : NULL;

    g_free(buffer);
    magic_close(magic);

    return result;
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

static fz_stream* sond_file_part_pdf_lookup_embedded_file(fz_context*,
		SondFilePartPDF*, gchar const*, GError**);

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
	//Datei in Zip-Archiv
	else if (SOND_IS_FILE_PART_ZIP(sfp_parent)) {

	}
	//Datei in GMimeMessage
	else if (SOND_IS_FILE_PART_GMESSAGE(sfp_parent)) {
		GMimeObject* object = NULL;
		GMimeStream* gmime_stream = NULL;
		fz_buffer* buf = NULL;
		gssize length = 0;
		GByteArray* gbyte_array = NULL;

		object = sond_file_part_gmessage_lookup_part_by_path(
				SOND_FILE_PART_GMESSAGE(sfp_parent), path, error);
		if (!object)
			ERROR_Z_VAL(NULL)

		gmime_stream = g_mime_stream_mem_new();

		if (GMIME_IS_MESSAGE_PART(object)) {
			GMimeObject* part = NULL;

			part = GMIME_OBJECT(g_mime_message_part_get_message(GMIME_MESSAGE_PART(object)));
			if (!part) {
				g_object_unref(object);
				g_object_unref(gmime_stream);
				if (error) *error = g_error_new(ZOND_ERROR, 0,
						"%s\nGMimeMessagePart hat keine Nachricht", __func__);

				ERROR_Z_VAL(NULL)
			}

			length = g_mime_object_write_to_stream(part, NULL, gmime_stream);
		}
		else if (GMIME_IS_PART(object)) {
			GMimeDataWrapper* wrapper = NULL;

			wrapper = g_mime_part_get_content(GMIME_PART(object));
			length = g_mime_data_wrapper_write_to_stream(wrapper, gmime_stream);
		}
		else {
			g_object_unref(object);
			g_object_unref(gmime_stream);
			if (error) *error = g_error_new(SOND_ERROR, 0,
					"%s\nGMimeObject ist kein zulässiger MimePart", __func__);

			ERROR_Z_VAL(NULL)
		}

		g_object_unref(object);
		if (length <= 0) {
			g_object_unref(gmime_stream);
			if (error) *error = g_error_new(ZOND_ERROR, 0,
					"%s\nFehler beim Schreiben des GMimeObject in Stream", __func__);

			ERROR_Z_VAL(NULL)
		}

		gbyte_array = g_mime_stream_mem_get_byte_array(GMIME_STREAM_MEM(gmime_stream));

		fz_try(ctx)
			buf = fz_new_buffer_from_copied_data(ctx, gbyte_array->data, gbyte_array->len);
		fz_always(ctx)
			g_object_unref(gmime_stream);
		fz_catch(ctx) {
			if (error) *error = g_error_new(g_quark_from_static_string("mupdf"),
					fz_caught(ctx), "%s\nfz_new_buffer_from_data: %s", __func__,
					fz_caught_message(ctx));

			ERROR_Z_VAL(NULL)
		}

		fz_try(ctx)
			stream = fz_open_buffer(ctx, buf);
		fz_always(ctx)
			fz_drop_buffer(ctx, buf);
		fz_catch(ctx) {
			if (error) *error = g_error_new(g_quark_from_static_string("mupdf"),
					fz_caught(ctx), "%s\nfz_open_buffer: %s", __func__,
					fz_caught_message(ctx));

			ERROR_Z_VAL(NULL)
		}
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
		if (error) *error = g_error_new(ZOND_ERROR, 0, "%s\nStream für SondFilePart-Typ"
			"kann nicht geöffnet werden", __func__);
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

//Datei unmittelbar oder Buffer - jedenfalls keine tmp-Kopie
static fz_stream* sond_file_part_get_istream(fz_context* ctx,
		SondFilePart* sfp, gboolean need_seekable, GError **error) {
	fz_stream *stream = NULL;

	stream = get_istream(ctx, sond_file_part_get_parent(sfp),
			sond_file_part_get_path(sfp), need_seekable, error);
	if (!stream)
		ERROR_Z_VAL(NULL)

	return stream;
}

//buffer - stream garantiert geschlossen
//kann also geschrieben werden
static fz_buffer* sond_file_part_get_buffer(SondFilePart* sfp,
		fz_context* ctx, GError** error) {
	fz_stream* stream = NULL;
	fz_buffer* buf = NULL;

	stream = sond_file_part_get_istream(ctx, sfp, FALSE, error);
	if (!stream)
		ERROR_Z_VAL(NULL)

	fz_try(ctx)
		buf = fz_read_all(ctx, stream, 0);
	fz_always(ctx)
		fz_drop_stream(ctx, stream);
	fz_catch(ctx) {
		if (error) *error = g_error_new(g_quark_from_static_string("mupdf"),
					fz_caught(ctx), "%s\n%s", __func__,
					fz_caught_message(ctx));

		return NULL;
	}

	return buf;
}

static gchar* sond_file_part_write_to_tmp_file(SondFilePart* sfp, GError **error) {
	gchar *filename = NULL;
	fz_context* ctx = NULL;
	fz_buffer *buf = NULL;

	ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
	if (!ctx) {
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nfz_new_context gibt NULL zurück", __func__);
		return NULL;
	}

	buf = sond_file_part_get_buffer(sfp, ctx, error);
	if (!buf) {
		fz_drop_context(ctx);

		ERROR_Z_VAL(NULL)
	}

	filename = g_strdup_printf("%s/%d", g_get_tmp_dir(),
			g_random_int_range(10000, 99999));

#ifdef __WIN32__
	gchar const* ext = NULL;
	fz_stream* stream = NULL;
	gchar* mime = NULL;

	fz_try(ctx)
		stream = fz_open_buffer(ctx, buf);
	fz_catch(ctx) {
		fz_drop_context(ctx);
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nfz_new_context gibt NULL zurück", __func__);

		return NULL;
	}

	mime = guess_content_type(ctx, stream, mime, error);
	if (!mime)
		ERROR_Z_VAL(NULL)

	ext = mime_to_extension(mime);

	filename = add_string(filename, g_strdup(ext));
#endif

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

static gint open_path(const gchar *path, gboolean open_with, GError **error) {
#ifdef _WIN32
    // Pfad von UTF-8 → UTF-16
    wchar_t *local_filename = g_utf8_to_utf16(path, -1, NULL, NULL, error);
    if (!local_filename)
    	ERROR_Z

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.nShow = SW_SHOWNORMAL;
    sei.lpVerb = open_with ? L"openas" : L"open";
    sei.lpFile = local_filename;
    sei.fMask = SEE_MASK_INVOKEIDLIST;

    BOOL ret = ShellExecuteExW(&sei);
    g_free(local_filename);

    if (!ret) {
        if (error) {
            DWORD dw = GetLastError();
            LPWSTR lpMsgBuf = NULL;
            FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            		FORMAT_MESSAGE_IGNORE_INSERTS, NULL, dw, MAKELANGID(LANG_NEUTRAL,
            				SUBLANG_DEFAULT), (LPWSTR)&lpMsgBuf, 0, NULL);
            gchar *msg_utf8 = g_utf16_to_utf8(lpMsgBuf, -1, NULL, NULL, NULL);
            *error = g_error_new(g_quark_from_static_string("WinApi"), dw,
                                 "ShellExecuteExW failed: %s", msg_utf8);
            g_free(msg_utf8);
            LocalFree(lpMsgBuf);
        }
        return -1;
    }
    return 0;

#elif defined(__APPLE__)
    // macOS: "open" benutzen
    gchar *cmdline = g_strdup_printf("open \"%s\"", path);
    gboolean ret = g_spawn_command_line_async(cmdline, error);
    g_free(cmdline);
    return (ret) ? 0 : -1;

#else
    // Linux / Unix: "xdg-open" oder "gio open"
    gchar *cmdline = g_strdup_printf("xdg-open \"%s\"", path);
    gboolean ret = g_spawn_command_line_async(cmdline, error);
    g_free(cmdline);
    return (ret) ? 0 : -1;
#endif
}

gint sond_file_part_open(SondFilePart* sfp, gboolean open_with,
		GError** error) {
	//hier alle Varianten, in denen eigener Viewer geöffnet wird
	if (!open_with &&
			SOND_IS_FILE_PART_LEAF(sfp) &&
			(!g_strcmp0("text/html", sond_file_part_leaf_get_mime_type(
					SOND_FILE_PART_LEAF(sfp))) ||
			g_str_has_prefix(sond_file_part_leaf_get_mime_type(
					SOND_FILE_PART_LEAF(sfp)), "image") ||
			!g_strcmp0("application/vnd.oasis.opendocument.text",
					sond_file_part_leaf_get_mime_type(
					SOND_FILE_PART_LEAF(sfp))) ||
			!g_strcmp0("application/vnd.openxmlformats-officedocument.wordprocessingml.document",
					sond_file_part_leaf_get_mime_type(
					SOND_FILE_PART_LEAF(sfp)))))
	 {
		fz_context* ctx = NULL;
		fz_stream* stream = NULL;
		gint rc = 0;

		ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
		if (!ctx) {
			if (error) *error = g_error_new(SOND_ERROR, 0,
					"%s\ncontext konnte nicht erzeugt werden", __func__);

			return -1;
		}

		stream = sond_file_part_get_istream(ctx, sfp, FALSE, error);
		if (!stream) {
			fz_drop_context(ctx);
			ERROR_Z
		}

		rc = sond_render(ctx, stream, NULL, error);
		fz_drop_stream(ctx, stream);
		fz_drop_context(ctx);
		if (rc)
			ERROR_Z
	}/*
	else if (!open_with && SOND_IS_FILE_PART_PDF(sfp)) {

	}*/
	else {
		gint rc = 0;
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

		rc = open_path(path, open_with, error);
		if (rc) {
			if (sond_file_part_get_parent(sfp)) { //tmp-Datei wurde erzeugt
				gint rc = 0;

				rc = g_remove(path);
				if (rc)
					g_warning("Datei '%s' konnte nicht gelöscht werden:\n%s",
							path, strerror(errno));
			}

			ERROR_Z
		}
	}

	return 0;
}

static fz_buffer* sond_file_part_pdf_mod_emb_file(SondFilePartPDF*, fz_context*,
		gchar const*, fz_buffer*, GError**);

static fz_buffer* sond_file_part_zip_mod_zip_file(SondFilePartZip*,
		fz_context*, gchar const*, fz_buffer*, GError**);

gint sond_file_part_delete(SondFilePart* sfp, GError** error) {
	SondFilePart* sfp_parent = NULL;

	sfp_parent = sond_file_part_get_parent(sfp);

	if (!sfp_parent) { //Datei im Filesystem
		gint rc = 0;
		gchar* path = NULL;
		SondFilePartPrivate* sfp_priv = sond_file_part_get_instance_private(sfp);

		path = g_strconcat(SOND_FILE_PART_CLASS(g_type_class_peek(SOND_TYPE_FILE_PART))->path_root,
				"/", sfp_priv->path, NULL);
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

		if (SOND_IS_FILE_PART_ZIP(sfp_parent))
			buf_out = sond_file_part_zip_mod_zip_file(SOND_FILE_PART_ZIP(sfp_parent),
					ctx, sond_file_part_get_path(sfp), NULL, error);
		else if (SOND_IS_FILE_PART_PDF(sfp_parent))
			buf_out = sond_file_part_pdf_mod_emb_file(SOND_FILE_PART_PDF(sfp_parent), ctx,
					sond_file_part_get_path(sfp), NULL, error);

		//else if (SOND_IS_FILE_PART_GMESSAGE(sfp_parent)) {

		if (!buf_out) {
			fz_drop_context(ctx);

			ERROR_Z
		}

		//replace in parent
		rc = sond_file_part_replace(sfp_parent, ctx, buf_out, error);
		fz_drop_buffer(ctx, buf_out);
		fz_drop_context(ctx);
		if (rc)
			ERROR_Z

		//testen, ob sfp_parent noch Kinder hat
		rc = sond_file_part_test_for_children(sfp_parent, error);
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
		rc = sond_file_part_delete(sfp, error);
		if (rc)
			ERROR_Z

		//neue Datei aus buffer schreiben
		filename = g_strconcat(SOND_FILE_PART_CLASS(g_type_class_peek(SOND_TYPE_FILE_PART))->path_root,
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
		else if (SOND_IS_FILE_PART_ZIP(sfp_parent)) //ist zip-Datei
			buf_out = sond_file_part_zip_mod_zip_file(SOND_FILE_PART_ZIP(sfp_parent),
					ctx, sond_file_part_get_path(sfp), buf, error);
		//else if (SOND_IS_FILE_PART_GMESSAGE(sfp_parent))

		if (!buf_out)
			ERROR_Z

		rc = sond_file_part_replace(sfp_parent, ctx, buf_out, error);
		fz_drop_buffer(ctx, buf_out);
		if (rc)
			ERROR_Z
	}

	return 0;
}

static gint sond_file_part_pdf_rename_embedded_file(SondFilePartPDF*,
		gchar const*, gchar const*, GError**);

static gint sond_file_part_zip_rename_file(SondFilePartZip*,
		gchar const*, gchar const*, GError**);

gint sond_file_part_rename(SondFilePart* sfp, gchar const* path_new, GError** error) {
	SondFilePartPrivate* sfp_priv = NULL;
	gint rc = 0;

	g_return_val_if_fail(sfp, -1);

	sfp_priv = sond_file_part_get_instance_private(sfp);

	if (!sfp_priv->parent) {//sfp ist im fs gespeichert
		rc = g_rename(sfp_priv->path, path_new);
		if (rc && error) *error = g_error_new(g_quark_from_static_string("stdlib"), errno,
				"g_rename\n%s", strerror(errno)); //error vorbereiten
	}
	else if (SOND_IS_FILE_PART_PDF(sfp_priv->parent))
		rc = sond_file_part_pdf_rename_embedded_file(SOND_FILE_PART_PDF(sfp_priv->parent),
				sfp_priv->path, path_new, error);
	else if (SOND_IS_FILE_PART_ZIP(sfp_priv->parent))
		rc = sond_file_part_zip_rename_file(SOND_FILE_PART_ZIP(sfp_priv->parent),
				sfp_priv->path, path_new, error);
	else {
		if (error) *error = g_error_new(g_quark_from_static_string("sond"), 0,
				"%s\nDerzeit nicht implementiert", __func__);

		return -1;
	}

	if (rc)
		ERROR_Z

	g_free(sfp_priv->path);
	sfp_priv->path = g_strdup(path_new);
	
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
		fz_context*, fz_buffer*, gchar const*, gchar const*, GError**);

static gint sond_file_part_insert(SondFilePart* sfp, fz_context* ctx,
		fz_buffer* buf, gchar const* filename, gchar const* mime_type,
		GError** error) {
	if (!sfp) { //Datei im Filesystem
		gchar* path = NULL;

		//neue Datei aus buffer schreiben
		path = g_strconcat(SOND_FILE_PART_CLASS(g_type_class_peek_static(
				SOND_TYPE_FILE_PART))->path_root, "/", filename, NULL);

		//fz_save_buffer löscht klaglos ein etwaig bestehendes file gleichen Namens
		//Daher ein Test:
		if (g_file_test(path, G_FILE_TEST_EXISTS)) {
			if (error) *error = g_error_new(SOND_ERROR, SOND_ERROR_EXISTS,
					"%s\nDatei existiert", __func__);

			return -1;
		}

		fz_try(ctx)
			fz_save_buffer(ctx, buf, path);
		fz_always(ctx)
			g_free(path);
		fz_catch(ctx)
			ERROR_PDF
	}
	else {
		gint rc = 0;

		if (SOND_IS_FILE_PART_PDF(sfp)) //ist also embedded
			rc = sond_file_part_pdf_insert_embedded_file(SOND_FILE_PART_PDF(sfp),
					ctx, buf, filename, mime_type, error);
		else if (SOND_IS_FILE_PART_ZIP(sfp)) //ist zip-Datei
			rc = sond_file_part_zip_insert_zip_file(SOND_FILE_PART_ZIP(sfp),
					ctx, buf, filename, mime_type, error);
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
		SondFilePart* sfp_dst, gchar const* base, GError** error) {
	fz_context* ctx = NULL;
	fz_buffer* buf = NULL;
	gint rc = 0;

	ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
	if (!ctx) {
		if (error) *error = g_error_new(ZOND_ERROR, 0,
				"%s\nfz_new_context gibt NULL zurück", __func__);
		return -1;
	}
	buf = sond_file_part_get_buffer(sfp_src, ctx, error);
	if (!buf) {
		fz_drop_context(ctx);
		ERROR_Z
	}

	rc = sond_file_part_insert(sfp_dst, ctx, buf, base,
			sond_file_part_get_mime_type(sfp_src), error);
	fz_drop_buffer(ctx, buf);
	fz_drop_context(ctx);
	if (rc)
		ERROR_Z

	return 0;
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
	G_OBJECT_CLASS(klass)->finalize = sond_file_part_zip_finalize;

	return;
}

static void sond_file_part_zip_init(SondFilePartZip* self) {
	SondFilePartPrivate* sfp_priv =
			sond_file_part_get_instance_private(SOND_FILE_PART(self));
	sfp_priv->arr_opened_files =
			g_ptr_array_new( );

	return;
}

static gint sond_file_part_zip_test_for_files(SondFilePartZip* sfp_zip, GError** error) {
	SondFilePartPrivate* sfp_priv = NULL;
	gboolean has_files = FALSE;

	//ToDo: Test auf files

	sfp_priv = sond_file_part_get_instance_private(SOND_FILE_PART(sfp_zip));
	if (has_files)
		sfp_priv->has_children = TRUE;
	else
		sfp_priv->has_children = FALSE;

	return 0;
}

static fz_buffer* sond_file_part_zip_mod_zip_file(SondFilePartZip* sfp_zip,
		fz_context* ctx, gchar const* path, fz_buffer* buf, GError** error) {
	fz_buffer* buf_out = NULL;

	if (error) *error = g_error_new(g_quark_from_static_string("sond"),
			0, "%s\nModifizieren von Zip-Dateien ist noch nicht implementiert", __func__);

	return NULL;

	return buf_out;
}

static gint sond_file_part_zip_rename_file(SondFilePartZip* sfp_zip,
		gchar const* path_old, gchar const* path_new, GError** error) {
	{
		if (error) *error = g_error_new(g_quark_from_static_string("sond"), 0,
				"%s\nUmbenennen von Zip-Dateien noch nicht implementiert", __func__);

		return -1;
	}

	return 0;
}

static gint sond_file_part_zip_insert_zip_file(SondFilePartZip* sfp_zip,
		fz_context* ctx, fz_buffer* buf, gchar const* filename,
		gchar const* mime_type, GError** error) {
	{
		if (error) *error = g_error_new(g_quark_from_static_string("sond"), 0,
				"%s\nEinfügen von Zip-Dateien noch nicht implementiert", __func__);

		return -1;
	}

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
		SondFilePartPDF *sfp_pdf, gboolean read_only, gboolean tmp_file,
		gboolean prompt_for_passwd, GError **error) {
	pdf_document* doc = NULL;
	fz_stream* stream = NULL;
	gint rc = 0;

	if (read_only) {
		stream = sond_file_part_get_istream(ctx, SOND_FILE_PART(sfp_pdf), TRUE, error);
		if (!stream)
			ERROR_Z_VAL(NULL)
	}
	else {
		if (!tmp_file) {
			fz_buffer* buf = NULL;

			buf = sond_file_part_get_buffer(SOND_FILE_PART(sfp_pdf), ctx, error);
			if (!buf)
				ERROR_Z_VAL(NULL)

			fz_try(ctx)
				stream = fz_open_buffer(ctx, buf);
			fz_always(ctx)
				fz_drop_buffer(ctx, buf);
			fz_catch(ctx) {
				if (error) *error = g_error_new(g_quark_from_static_string("mupdf"), fz_caught(ctx),
						"%s\nfz_open_buffer: %s", __func__, fz_caught_message(ctx));

				return NULL;
			}
		}
		else {
			gchar* filename = NULL;

			filename = sond_file_part_write_to_tmp_file(SOND_FILE_PART(sfp_pdf), error);
			if (!filename)
				ERROR_Z_VAL(NULL)

			stream = open_file(ctx, filename, error);
			if (!stream) {
				gint rc = 0;

				rc = g_remove(filename);
				if (rc)
					g_warning("%s\nDatei '%s' konnte nicht gelöscht werden:\n%s",
							__func__, filename, strerror(errno));
				g_free(filename);

				ERROR_Z_VAL(NULL)
			}
		}
	}

	//PDF-Dokument öffnen
	fz_try(ctx)
		doc = pdf_open_document_with_stream(ctx, stream);
	fz_catch(ctx) {
		if (error) *error = g_error_new(g_quark_from_static_string("mupdf"),
				fz_caught(ctx),
				"%s\nPDF-Dokument '%s' konnte nicht geöffnet werden:\n%s", __func__,
				sond_file_part_get_path(SOND_FILE_PART(sfp_pdf)),
				fz_caught_message(ctx));

		if (fz_stream_filename(ctx, stream)) {
			gint rc = 0;

			rc = g_remove(fz_stream_filename(ctx, stream));
			if (rc)
				g_warning("%s\nDatei '%s' konnte nicht gelöscht werden:\n%s",
						__func__, fz_stream_filename(ctx, stream), strerror(errno));
		}

		fz_drop_stream(ctx, stream);

		return NULL; //Fehler beim Öffnen des PDF-Dokuments
	}

	fz_drop_stream(ctx, stream);

	rc = sond_file_part_pdf_authen_doc(sfp_pdf, ctx, doc, prompt_for_passwd, error);
	if (rc) {
		if (rc == -1)
			g_prefix_error(error, "%s\n", __func__);
		else
			if (error) *error = g_error_new(g_quark_from_static_string("sond"), 1,
					"%s\nEntschlüsselung gescheitert", __func__);

		if (fz_stream_filename(ctx, doc->file)) {
			gint rc = 0;

			rc = g_remove(fz_stream_filename(ctx, doc->file));
			if (rc)
				g_warning("%s\nDatei '%s' konnte nicht gelöscht werden:\n%s",
						__func__, fz_stream_filename(ctx, doc->file), strerror(errno));
		}

		pdf_drop_document(ctx, doc);

		return NULL;
	}

	return doc;
}

static fz_buffer* pdf_doc_to_buf(fz_context* ctx, pdf_document* doc, GError** error) {
	fz_output* out = NULL;
	fz_buffer* buf = NULL;
	pdf_write_options in_opts =
//#ifdef __WIN32
			{ 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, ~0, "", "", 0, 0, 0, 0, 0 };
//#elif defined(__linux__)
 //           { 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 1, 0, 0, ~0, "", "" };
//#endif // __win32
//	if (pdf_count_pages(ctx, pdf_doc) < BIG_PDF && !pdf_doc->crypt)
		in_opts.do_garbage = 4;

	fz_try(ctx) {
		buf = fz_new_buffer(ctx, 4096);
	}
	fz_catch(ctx) {
		if (error) *error = g_error_new(g_quark_from_static_string("mupdf"), fz_caught(ctx),
				"%s\n%s", __func__, fz_caught_message(ctx));

		return NULL;
	}

	fz_try(ctx)
		out = fz_new_output_with_buffer(ctx, buf);
	fz_catch(ctx) {
		if (error) *error = g_error_new(g_quark_from_static_string("mupdf"), fz_caught(ctx),
				"%s\n%s", __func__, fz_caught_message(ctx));
		fz_drop_buffer(ctx, buf);

		return NULL;
	}

	//do_appereance wird in pdf_write_document ignoriert. deshalb muß es hier gemacht werden
	if (doc->resynth_required) {
		gint i = 0;
		gint n = 0;

		n = pdf_count_pages(ctx, doc);
		for (i = 0; i < n; ++i)
		{
			pdf_page *page = pdf_load_page(ctx, doc, i);
			fz_try(ctx)
				pdf_update_page(ctx, page);
			fz_always(ctx)
				fz_drop_page(ctx, &page->super);
			fz_catch(ctx)
				fz_warn(ctx, "could not create annotation appearances");

			if (!doc->resynth_required) break;
		}
	}

	//immer noch? weil keine annot im gesamten Dokement
	if (doc->resynth_required)
		doc->resynth_required = 0; //dann mit Gewalt

	fz_try(ctx)
		pdf_write_document(ctx, doc, out, &in_opts);
	fz_always(ctx) {
		fz_close_output(ctx, out);
		fz_drop_output(ctx, out);
	}
	fz_catch(ctx) {
		if (error) *error = g_error_new(g_quark_from_static_string("mupdf"), fz_caught(ctx),
				"%s\npdf_write_document: %s", __func__, fz_caught_message(ctx));
		fz_drop_buffer(ctx, buf);

		return NULL;
	}

	return buf;
}

gint sond_file_part_pdf_save(fz_context *ctx, pdf_document *pdf_doc,
		SondFilePartPDF* sfp_pdf, GError **error) {
	gint rc = 0;
	fz_buffer* buf = NULL;

	buf = pdf_doc_to_buf(ctx, pdf_doc, error);
	if (!buf)
		ERROR_Z

	rc = sond_file_part_replace(SOND_FILE_PART(sfp_pdf), ctx, buf, error);
	fz_drop_buffer(ctx, buf);
	if (rc)
		ERROR_Z

	return 0;
}

static pdf_obj* get_EF_F(fz_context* ctx, pdf_obj* val, gchar const** path, GError** error) {
	gchar const* path_tmp = NULL;
	pdf_obj* EF_F = NULL;
	pdf_obj* F = NULL;
	pdf_obj* UF = NULL;
	pdf_obj* EF = NULL;

	if (!pdf_is_dict(ctx, val)) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("sond"),
					0, "%s\nnamestree malformed", __func__);
		return NULL;
	}

	fz_try(ctx) {
		EF = pdf_dict_get(ctx, val, PDF_NAME(EF));
		EF_F = pdf_dict_get(ctx, EF, PDF_NAME(F));
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

		return NULL;
	}

	if (!path_tmp) {
			if (error)
				*error = g_error_new(ZOND_ERROR, 0, "%s\nEingebettete Datei hat keinen Pfad",
						__func__);
			return NULL;
	}

	*path = path_tmp;

	return EF_F;
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

	EF_F = get_EF_F(ctx, val, &path_embedded, error);
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

	EF_F = get_EF_F(ctx, val, &path_embedded, error);
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
		if (error) *error = g_error_new(ZOND_ERROR, 0,
				"%s\nfz_new_context gibt NULL zurück", __func__);
		return -1;
	}

	doc = sond_file_part_pdf_open_document(ctx, sfp_pdf, TRUE, FALSE, FALSE, error);
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
		if (error) *error = g_error_new(ZOND_ERROR, 0,
				"%s\nfz_new_context gibt NULL zurück", __func__);

		return -1;
	}

	doc = sond_file_part_pdf_open_document(ctx, sfp_pdf, TRUE, FALSE, FALSE, error);
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

static fz_stream* sond_file_part_pdf_lookup_embedded_file(fz_context* ctx,
		SondFilePartPDF* sfp_pdf, gchar const* path, GError** error) {
	pdf_document* doc = NULL;
	Lookup lookup = { 0 };
	gint rc = 0;

	doc = sond_file_part_pdf_open_document(ctx, sfp_pdf, TRUE, FALSE, FALSE, error);
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

	EF_F = get_EF_F(ctx, val, &path_embedded, error);
	if (!EF_F)
		ERROR_Z

	if (g_strcmp0(path_embedded, modify->path) == 0) {
		gint index = 0;

		fz_try(ctx)
			index = pdf_array_find(ctx, names, key);
		fz_catch(ctx)
			ERROR_PDF

		if (index == -1) {
			if (error) *error = g_error_new(ZOND_ERROR, 0,
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

	EF_F = get_EF_F(ctx, val, &path_embedded, error);
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

	doc = sond_file_part_pdf_open_document(ctx, sfp_pdf, FALSE, FALSE, TRUE, error);
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

	EF_F = get_EF_F(ctx, val, &path_embedded, error);
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
			*error = g_error_new(ZOND_ERROR, 0, "%s\nEingebettete Datei hat keinen Pfad",
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

	doc = sond_file_part_pdf_open_document(ctx, sfp_pdf, FALSE, FALSE, TRUE, error);
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

	rc = sond_file_part_pdf_save(ctx, doc, sfp_pdf, error);
	pdf_drop_document(ctx, doc);
	fz_drop_context(ctx);
	if (rc)
		ERROR_Z

	return 0;
}

/* -----------------------------------------------
   Hilfsfunktion: vergleicht zwei PDF-String-Objekte lexikographisch
   ----------------------------------------------- */
static int pdf_compare_strings(fz_context *ctx, pdf_obj *a, pdf_obj *b)
{
	size_t la, lb;
	const char *sa = pdf_to_str_buf(ctx, a);
	const char *sb = pdf_to_str_buf(ctx, b);
	la = pdf_to_str_len(ctx, a);
	lb = pdf_to_str_len(ctx, b);

	int cmp = strncmp(sa, sb, la < lb ? la : lb);
	if (cmp != 0) return cmp;
	return (la < lb) ? -1 : (la > lb) ? 1 : 0;
}

static gint pdf_insert_into_name_tree(fz_context *ctx, pdf_document *doc,
		pdf_obj *node, pdf_obj *key, pdf_obj *value, gboolean is_root, GError** error) {
	pdf_obj *arr = NULL;       /* Für neues Blatt-Names-Array */
	pdf_obj *limits = NULL;    /* Für Limits-Array */

	fz_var(arr);
	fz_var(limits);

	fz_try(ctx)
	{
		/* Bestehende Objekte aus dem Node */
		pdf_obj *kids = pdf_dict_get(ctx, node, PDF_NAME(Kids));
		pdf_obj *names = pdf_dict_get(ctx, node, PDF_NAME(Names));

		/* ---------------- Blatt-Knoten ---------------- */
		if (names)
		{
			int n = pdf_array_len(ctx, names);
			int pos = 0;

			/* Sortierte Position für Key finden */
			while (pos < n)
			{
				pdf_obj *existing_key = pdf_array_get(ctx, names, pos);
				if (pdf_compare_strings(ctx, key, existing_key) < 0)
					break;
				pos += 2;
			}

			pdf_array_insert(ctx, names, key, pos);
			pdf_array_insert(ctx, names, value, pos + 1);

			/* Limits nur setzen, wenn nicht Root */
			if (!is_root)
			{
				pdf_obj *first_key = pdf_array_get(ctx, names, 0);
				pdf_obj *last_key  = pdf_array_get(ctx, names, pdf_array_len(ctx, names) - 2);

				limits = pdf_new_array(ctx, doc, 2);
				pdf_array_push(ctx, limits, first_key);
				pdf_array_push(ctx, limits, last_key);

				pdf_dict_put(ctx, node, PDF_NAME(Limits), limits);
			}

			return 0;
		}

		/* ---------------- Intermediate Node ---------------- */
		if (kids)
		{
			int n = pdf_array_len(ctx, kids);
			const char *skey = NULL;
			skey = pdf_to_str_buf(ctx, key);

			int inserted = 0;
			for (int i = 0; i < n; i++)
			{
				pdf_obj *kid = pdf_array_get(ctx, kids, i);
				pdf_obj *kid_limits = pdf_dict_get(ctx, kid, PDF_NAME(Limits));

				if (!kid_limits || pdf_array_len(ctx, kid_limits) < 2)
				{
					pdf_insert_into_name_tree(ctx, doc, kid, key, value, FALSE, error);
					inserted = 1;
					break;
				}

				const char *low  = pdf_to_str_buf(ctx, pdf_array_get(ctx, kid_limits, 0));
				const char *high = pdf_to_str_buf(ctx, pdf_array_get(ctx, kid_limits, 1));

				if (strcmp(skey, low) >= 0 && strcmp(skey, high) <= 0)
				{
					pdf_insert_into_name_tree(ctx, doc, kid, key, value, FALSE, error);
					inserted = 1;
					break;
				}
			}

			if (!inserted)
				pdf_insert_into_name_tree(ctx, doc,
						pdf_array_get(ctx, kids, n - 1), key, value, FALSE, error);

			/* Limits für Intermediate Node setzen, außer Root */
			if (!is_root)
			{
				pdf_obj *first_child = pdf_array_get(ctx, kids, 0);
				pdf_obj *last_child  = pdf_array_get(ctx, kids, n - 1);

				pdf_obj *first_key = pdf_array_get(ctx,
						pdf_dict_get(ctx, first_child, PDF_NAME(Limits)), 0);
				pdf_obj *last_key  = pdf_array_get(ctx,
						pdf_dict_get(ctx, last_child, PDF_NAME(Limits)), 1);

				limits = pdf_new_array(ctx, doc, 2);
				pdf_array_push(ctx, limits, first_key);
				pdf_array_push(ctx, limits, last_key);

				pdf_dict_put(ctx, node, PDF_NAME(Limits), limits);
			}

			return 0;
		}

		/* ---------------- Leerer Node: Neues Blatt ---------------- */
		arr = pdf_new_array(ctx, doc, 2);
		pdf_array_push(ctx, arr, key);
		pdf_array_push(ctx, arr, value);
		pdf_dict_put(ctx, node, PDF_NAME(Names), arr);

		/* Limits nur setzen, wenn nicht Root */
		if (!is_root)
		{
			limits = pdf_new_array(ctx, doc, 2);
			pdf_array_push(ctx, limits, key);  /* Kein Drop */
			pdf_array_push(ctx, limits, key);  /* Kein Drop */
			pdf_dict_put(ctx, node, PDF_NAME(Limits), limits);
		}
	}
	fz_always(ctx)
	{
		pdf_drop_obj(ctx, arr);
		pdf_drop_obj(ctx, limits);
	}
	fz_catch(ctx)
		ERROR_PDF

	return 0;
}

gint static pdf_insert_emb_file(fz_context* ctx, pdf_document* doc,
		fz_buffer* buf, gchar const* filename,
		gchar const* mime_type, GError** error) {
	pdf_obj* catalog = NULL;
	pdf_obj* names = NULL;
	pdf_obj* emb = NULL;
	pdf_obj* file_stream = NULL;
	pdf_obj* params = NULL;
	pdf_obj* ef = NULL;
	pdf_obj* filespec = NULL;
	pdf_obj* key = NULL;
	gint rc = 0;

	fz_try(ctx)
		catalog = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root));
	fz_catch(ctx)
		ERROR_PDF

	if (!catalog) {
		if (error) *error = g_error_new(g_quark_from_static_string("sond"),
				0, "%s\nCatalog nicht gefunden", __func__);
		pdf_drop_document(ctx, doc);

		return -1;
	}

	fz_try(ctx)
		names = pdf_dict_get(ctx, catalog, PDF_NAME(Names));
	fz_catch(ctx)
		ERROR_PDF

	if (!names)
	{
		fz_var(names);
		fz_try(ctx) {
			names = pdf_new_dict(ctx, doc, 1);
			pdf_dict_put(ctx, catalog, PDF_NAME(Names), names);
		}
		fz_always(ctx)
			pdf_drop_obj(ctx, names);
		fz_catch(ctx) {
			pdf_drop_document(ctx, doc);

			ERROR_PDF
		}
	}

	fz_try(ctx)
		emb = pdf_dict_get(ctx, names, PDF_NAME(EmbeddedFiles));
	fz_catch(ctx)
		ERROR_PDF

	if (!emb) {
		pdf_obj* names_array = NULL;

		fz_var(names_array);
		fz_var(emb);
		fz_try(ctx) {
			emb = pdf_new_dict(ctx, doc, 2);
			pdf_dict_put(ctx, names, PDF_NAME(EmbeddedFiles), emb);

			names_array = pdf_new_array(ctx, doc, 0);
			pdf_dict_put(ctx, emb, PDF_NAME(Names), names_array);
		}
		fz_always(ctx) {
			pdf_drop_obj(ctx, emb);
			pdf_drop_obj(ctx, names_array);
		}
		fz_catch(ctx)
			ERROR_PDF
	}

    /* ---------- Datei-Stream ---------- */
	fz_var(file_stream);
	fz_var(params);
	fz_var(ef);
	fz_var(filespec);
	fz_var(key);

	fz_try(ctx) {
		file_stream = pdf_add_stream(ctx, doc, buf, NULL, 0);

		/* ---------- Params ---------- */
		params = pdf_new_dict(ctx, doc, 2);
		pdf_dict_put_drop(ctx, params, PDF_NAME(Size), pdf_new_int(ctx, buf->len));

		/* ---------- EF ---------- */
		ef = pdf_new_dict(ctx, doc, 1);
		pdf_dict_put(ctx, ef, PDF_NAME(F), file_stream);

		/* ---------- FileSpec ---------- */
		filespec = pdf_new_dict(ctx, doc, 5);
		pdf_dict_put_drop(ctx, filespec, PDF_NAME(Type), pdf_new_name(ctx, "Filespec"));
		pdf_dict_put_drop(ctx, filespec, PDF_NAME(F),
					 pdf_new_string(ctx, filename, strlen(filename)));
		pdf_dict_put(ctx, filespec, PDF_NAME(EF), ef);
		pdf_dict_put(ctx, filespec, PDF_NAME(Params), params);

		if (mime_type)
		{
			pdf_dict_put_drop(ctx, filespec, PDF_NAME(Subtype),
						 pdf_new_string(ctx, mime_type, strlen(mime_type)));
		}

		/* ---------- Key für Namen ---------- */
		key = pdf_new_string(ctx, filename, strlen(filename));
	}
	fz_always(ctx) {
		pdf_drop_obj(ctx, file_stream);
		pdf_drop_obj(ctx, params);
		pdf_drop_obj(ctx, ef);
	}
	fz_catch(ctx) {
		pdf_drop_obj(ctx, filespec);
		pdf_drop_obj(ctx, key);

		ERROR_PDF
	}

	rc = pdf_insert_into_name_tree(ctx, doc, emb, key, filespec, TRUE, error);
	pdf_drop_obj(ctx, filespec);
	pdf_drop_obj(ctx, key);
	if (rc)
		ERROR_Z

	return 0;
}

static gint sond_file_part_pdf_insert_embedded_file(SondFilePartPDF* sfp_pdf,
		fz_context* ctx, fz_buffer* buf, gchar const* filename,
		gchar const* mime_type, GError** error) {
	gint rc = 0;
	pdf_document* doc = NULL;

	doc = sond_file_part_pdf_open_document(ctx, sfp_pdf, FALSE, FALSE, TRUE, error);
	if (!doc)
		ERROR_Z

	rc = pdf_insert_emb_file(ctx, doc, buf, filename, mime_type, error);
	if (rc) {
		pdf_drop_document(ctx, doc);
		ERROR_Z
	}

	rc = sond_file_part_pdf_save(ctx, doc, sfp_pdf, error);
	pdf_drop_document(ctx, doc);
	if (rc)
		ERROR_Z

	return 0;
}

//GMessage
typedef struct {
	fz_context* ctx;
	fz_buffer* buf;
	GMimeMessage* message;
} SondFilePartGMessagePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(SondFilePartGMessage, sond_file_part_gmessage, SOND_TYPE_FILE_PART)

static void sond_file_part_gmessage_init(SondFilePartGMessage *self) {

	return;
}

static void sond_file_part_gmessage_class_init(SondFilePartGMessageClass *klass) {

	return;
}

static GMimeMessage* sond_file_part_gmessage_open(SondFilePartGMessage* sfp_gmessage,
		GError** error) {
	/* load a GMimeMessage from a stream */
	GMimeMessage *message;
	GMimeStream *stream;
	GMimeParser *parser;
	fz_buffer* buf = NULL;
	fz_context* ctx = NULL;

	ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
	if (!ctx) {
		if (error) *error = g_error_new(ZOND_ERROR, 0,
				"%s\nfz_new_context gibt NULL zurück", __func__);
		return NULL;
	}

	buf = sond_file_part_get_buffer(SOND_FILE_PART(sfp_gmessage), ctx, error);
	if (!buf) {
		fz_drop_context(ctx);
		ERROR_Z_VAL(NULL)
	}

	stream = g_mime_stream_mem_new_with_buffer((const gchar*) buf->data, buf->len);
	fz_drop_buffer(ctx, buf);
	fz_drop_context(ctx);
	g_mime_stream_mem_set_owner(GMIME_STREAM_MEM(stream), TRUE);
	parser = g_mime_parser_new_with_stream (stream);

	/* Note: we can unref the stream now since the GMimeParser has a reference to it... */
	g_object_unref (stream);

	message = g_mime_parser_construct_message (parser, NULL);
	if (!message) {
		if (error) *error = g_error_new(ZOND_ERROR, 0, "%s\nParsen fehlgeschlagen", __func__);
		fz_drop_buffer(ctx, buf);
		fz_drop_context(ctx);

		return NULL;
	}

	/* unref the parser since we no longer need it */
	g_object_unref (parser);

	return message;
}

static gint sond_file_part_gmessage_test_for_multipart(SondFilePartGMessage* sfp_gmessage,
		GError** error) {
	GMimeMessage* message = NULL;
	GMimeObject* root = NULL;

	SondFilePartPrivate* sfp_priv =
			sond_file_part_get_instance_private(SOND_FILE_PART(sfp_gmessage));

	message = sond_file_part_gmessage_open(sfp_gmessage, error);
	if (!message)
		ERROR_Z

	root = g_object_ref(g_mime_message_get_mime_part(message));
	g_object_unref(message);
	if (root) {//Auch wenn nur ein einziger part, ist es ein Kind von Message
		sfp_priv->has_children = TRUE;
		g_object_unref(root);
	}

	return 0;
}

static GMimeObject* sond_file_part_gmessage_lookup_part_by_path(
		SondFilePartGMessage* sfp_gmessage, gchar const* path, GError** error) {
	gchar** strv = NULL;
	gint zaehler = 0;
	GMimeMessage* message = NULL;
	GMimeObject* object = NULL;

	message = sond_file_part_gmessage_open(sfp_gmessage, error);
	if (!message)
		ERROR_Z_VAL(NULL)

	object = g_object_ref(g_mime_message_get_mime_part(message));
	g_object_unref(message);
	if (!object) {
		if (error)
			*error = g_error_new(ZOND_ERROR, 0,
					"%s\nNachricht hat keinen MIME-Teil", __func__);

		return NULL;
	}

	if (!path)
		return object;

	strv = g_strsplit(path, "/", -1);

	if (GMIME_IS_MULTIPART(object))
		do {
			GMimeObject* part = NULL;

			part = g_object_ref(g_mime_multipart_get_part(
					GMIME_MULTIPART(object), atoi(strv[zaehler])));
			g_object_unref(object);
			if (!part) {
				if (error)
					*error = g_error_new(ZOND_ERROR, 0,
							"%s\nMimePart mit Index %s nicht gefunden",
							__func__, strv[zaehler]);
				return NULL;
			}

			object = part;
			zaehler++;
		} while (strv[zaehler] != NULL);
	else if (zaehler == 0 && strv[0] && !strv[1]) { //nur wenn root-object
		if (GMIME_IS_MESSAGE_PART(object)) {
			GMimeMessage* msg = NULL;
			GMimeObject* part = NULL;

			msg = g_mime_message_part_get_message(GMIME_MESSAGE_PART(object));
			part = g_object_ref(g_mime_message_get_mime_part(GMIME_MESSAGE(msg)));
			g_object_unref(object);
			object = part;
		}
		else if (!GMIME_IS_PART(object)) {
			g_object_unref(object);
			if (error) *error = g_error_new(SOND_ERROR, 0,
					"%s\nroot-Object ist weder MimePart noch Message",
					__func__);

			return NULL;
		}
	}
	else {
		g_object_unref(object);
		if (error) *error = g_error_new(SOND_ERROR, 0,
				"%s\nunzulässiger Pfad",
				__func__);

		return NULL;
	}

	g_strfreev(strv);

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
			*error = g_error_new(ZOND_ERROR, 0,
					"%s\nPart mit Pfad '%s' ist kein Multipart",
					__func__, path);
		return -1;
	}

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

static void sond_file_part_leaf_set_mime_type(SondFilePartLeaf* sfp_leaf, gchar const* mime_type) {
	SondFilePartLeafPrivate* sfp_leaf_priv =
			sond_file_part_leaf_get_instance_private(sfp_leaf);

	g_free(sfp_leaf_priv->mime_type);
	sfp_leaf_priv->mime_type = g_strdup(mime_type);

	return;
}
