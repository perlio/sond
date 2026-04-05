/*
 sond (sond_process_file.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2026  peloamerica

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

#include "sond_process_file.h"

#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <zip.h>
#include <gmime/gmime.h>

#include "sond_ocr.h"
#include "sond_index.h"
#include "sond_fileparts.h"
#include "sond_gmessage_helper.h"
#include "sond_pdf_helper.h"
#include "sond_misc.h"
#include "sond_log_and_error.h"


static void sond_process_file_do_rec(SondProcessFileCtx* wctx,
		guchar* data, gsize size, gchar const* filename,
		guchar** out_data, gsize* out_size, gint* out_pdf_count);

static gint process_zip_for_ocr(guchar* data, gsize size,
		gchar const* filename, SondProcessFileCtx* wctx,
		guchar** out_data, gsize* out_size, gint* out_pdf_count,
		GError** error) {
	zip_error_t zip_error = { 0 };
	zip_source_t* src = NULL;
	zip_t* archive = NULL;
	gboolean modified = FALSE;

	/* Eigene Kopie des Puffers, da libzip Eigentuemer wird (freep=1) */
	void* data_copy = g_memdup2(data, size);
	if (!data_copy) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
				"process_zip_for_ocr: g_memdup2 fehlgeschlagen");
		return -1;
	}

	zip_error_init(&zip_error);
	src = zip_source_buffer_create(data_copy, size, 1 /*freep*/, &zip_error);
	if (!src) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
				"process_zip_for_ocr: zip_source_buffer_create: %s",
				zip_error_strerror(&zip_error));
		zip_error_fini(&zip_error);
		g_free(data_copy);
		return -1;
	}

	/* Ref erhöhen, damit src nach zip_close() noch verfügbar ist */
	zip_source_keep(src);

	archive = zip_open_from_source(src, 0, &zip_error);
	if (!archive) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
				"process_zip_for_ocr: zip_open_from_source: %s",
				zip_error_strerror(&zip_error));
		zip_error_fini(&zip_error);
		zip_source_free(src);
		return -1;
	}
	zip_error_fini(&zip_error);

	zip_int64_t num_entries = zip_get_num_entries(archive, 0);

	for (zip_int64_t i = 0; i < num_entries; i++) {
		if (g_atomic_int_get(&wctx->cancel))
			break;

		const char* entry_name = zip_get_name(archive, (zip_uint64_t)i, ZIP_FL_ENC_UTF_8);
		if (!entry_name)
			continue;

		zip_stat_t zstat = { 0 };
		if (zip_stat_index(archive, (zip_uint64_t)i, 0, &zstat) != 0)
			continue;
		if (!(zstat.valid & ZIP_STAT_SIZE))
			continue;

		zip_file_t* zf = zip_fopen_index(archive, (zip_uint64_t)i, 0);
		if (!zf) {
			if (wctx->log_func)
				wctx->log_func(wctx->log_func_data,
					"ZIP '%s': Kann Eintrag '%s' nicht öffnen: %s",
					filename, entry_name,
					zip_error_strerror(zip_get_error(archive)));
			continue;
		}

		guchar* entry_data = g_malloc(zstat.size);
		zip_int64_t bytes_read = zip_fread(zf, entry_data, zstat.size);
		zip_fclose(zf);

		if (bytes_read < 0 || (zip_uint64_t)bytes_read != zstat.size) {
			if (wctx->log_func)
				wctx->log_func(wctx->log_func_data,
					"ZIP '%s': Fehler beim Lesen von Eintrag '%s'",
					filename, entry_name);
			g_free(entry_data);
			continue;
		}

		gchar* entry_filename = g_strdup_printf("%s//%s", filename, entry_name);
		guchar* processed_data = NULL;
		gsize processed_size = 0;

		sond_process_file_do_rec(wctx, entry_data, (gsize)bytes_read, entry_filename,
				&processed_data, &processed_size, out_pdf_count);
		g_free(entry_data);
		g_free(entry_filename);

		if (!processed_data)
			continue; /* kein Fehler, nur nichts zu tun */

		/* Verarbeiteten Inhalt zurückschreiben */
		zip_error_t ze = { 0 };
		zip_error_init(&ze);
		zip_source_t* entry_src = zip_source_buffer_create(
				processed_data, processed_size, 0 /*freep*/, &ze);
		if (!entry_src) {
			if (wctx->log_func)
				wctx->log_func(wctx->log_func_data,
					filename, entry_name, zip_error_strerror(&ze));
			zip_error_fini(&ze);
			g_free(processed_data);
			continue;
		}
		zip_error_fini(&ze);

		if (zip_file_replace(archive, (zip_uint64_t)i, entry_src,
				ZIP_FL_ENC_UTF_8) != 0) {
			if (wctx->log_func)
				wctx->log_func(wctx->log_func_data,
					filename, entry_name,
					zip_error_strerror(zip_get_error(archive)));
			zip_source_free(entry_src);
			g_free(processed_data);
			continue;
		}

		/* processed_data wird jetzt von zip_source verwaltet - NICHT freigeben */
		modified = TRUE;
	}

	if (!modified) {
		zip_discard(archive);
		zip_source_free(src);
		return 0; /* nichts geändert */
	}

	/* ZIP schreiben und Inhalt aus src lesen */
	if (zip_close(archive) != 0) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
				"process_zip_for_ocr: zip_close: %s",
				zip_error_strerror(zip_source_error(src)));
		zip_source_free(src);
		return -1;
	}

	if (zip_source_open(src) != 0) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
				"process_zip_for_ocr: zip_source_open fehlgeschlagen");
		zip_source_free(src);
		return -1;
	}

	zip_source_seek(src, 0, SEEK_END);
	zip_int64_t result_len = zip_source_tell(src);
	zip_source_seek(src, 0, SEEK_SET);

	if (result_len <= 0) {
		zip_source_close(src);
		zip_source_free(src);
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
				"process_zip_for_ocr: zip_source hat 0 Bytes");
		return -1;
	}

	*out_data = g_malloc((gsize)result_len);
	zip_int64_t n = zip_source_read(src, *out_data, (zip_uint64_t)result_len);
	zip_source_close(src);
	zip_source_free(src);

	if (n != result_len) {
		g_free(*out_data);
		*out_data = NULL;
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
				"process_zip_for_ocr: zip_source_read unvollständig");
		return -1;
	}

	*out_size = (gsize)result_len;
	return 0;
}

/* Hilfsfunktion: schreibt GMimeMessage in Puffer */
static guchar* gmessage_to_buffer(GMimeMessage* message, gsize* out_size) {
	GMimeStream* stream = g_mime_stream_mem_new();
	gssize written = g_mime_object_write_to_stream(
			GMIME_OBJECT(message), NULL, stream);
	if (written <= 0) {
		g_object_unref(stream);
		return NULL;
	}
	GByteArray* ba = g_mime_stream_mem_get_byte_array(GMIME_STREAM_MEM(stream));
	guchar* result = g_memdup2(ba->data, ba->len);
	*out_size = ba->len;
	g_object_unref(stream);
	return result;
}

/*
 * Rekursiv alle MIME-Parts durchgehen und ggf. ersetzen.
 *
 * eml_filename  – Pfad der .eml-Datei im Index (z.B. "xxx.eml")
 * internal_path – interner Pfad innerhalb der .eml, mit '/' getrennt
 *                  (z.B. NULL für Root, "0" für ersten Part,
 *                  "0/1" für zweiten Part des ersten Multiparts)
 * Der vollständige Index-Filename lautet dann "eml_filename//internal_path".
 */
static gboolean gmessage_process_part(GMimeObject* object,
		gchar const* eml_filename, gchar const* internal_path,
		SondProcessFileCtx* wctx, gint part_index, gint* out_pdf_count) {
	gboolean modified = FALSE;

	if (g_atomic_int_get(&wctx->cancel))
		return FALSE;

	if (GMIME_IS_MULTIPART(object)) {
		GMimeMultipart* mp = GMIME_MULTIPART(object);
		gint count = g_mime_multipart_get_count(mp);

		for (gint i = 0; i < count; i++) {
			GMimeObject* child = g_mime_multipart_get_part(mp, i);
			/* Interner Pfad: Elternpfad/Index */
			gchar* child_internal = internal_path
					? g_strdup_printf("%s/%d", internal_path, i)
					: g_strdup_printf("%d", i);

			if (gmessage_process_part(child, eml_filename, child_internal,
					wctx, i, out_pdf_count))
				modified = TRUE;

			g_free(child_internal);
		}
	}
	else if (GMIME_IS_MESSAGE_PART(object)) {
		GMimeMessage* inner = g_mime_message_part_get_message(GMIME_MESSAGE_PART(object));
		if (inner) {
			gsize inner_size = 0;
			guchar* inner_buf = gmessage_to_buffer(inner, &inner_size);
			if (inner_buf) {
				guchar* processed = NULL;
				gsize proc_size = 0;

				/* Index-Filename: eml_filename//internal_path
				 * internal_path=NULL nur wenn Root direkt ein MessagePart ist → "0" */
				gchar* msg_filename = internal_path
				? g_strdup_printf("%s//%s", eml_filename, internal_path)
				: g_strdup_printf("%s//0", eml_filename);
				sond_process_file_do_rec(wctx, inner_buf, inner_size, msg_filename,
						&processed, &proc_size, out_pdf_count);
				g_free(msg_filename);
				g_free(inner_buf);

				if (processed) {
					GMimeStream* stream = g_mime_stream_mem_new_with_buffer(
							(const gchar*)processed, proc_size);
					g_free(processed);
					GMimeParser* parser = g_mime_parser_new_with_stream(stream);
					g_object_unref(stream);
					GMimeMessage* new_inner = g_mime_parser_construct_message(parser, NULL);
					g_object_unref(parser);
					if (new_inner) {
						g_mime_message_part_set_message(GMIME_MESSAGE_PART(object), new_inner);
						g_object_unref(new_inner);
						modified = TRUE;
					}
				}
			}
		}
	}
	else if (GMIME_IS_PART(object)) {
		GMimePart* part = GMIME_PART(object);
		GMimeDataWrapper* wrapper = g_mime_part_get_content(part);
		if (!wrapper)
			return FALSE;

		GMimeStream* mem = g_mime_stream_mem_new();
		gssize written = g_mime_data_wrapper_write_to_stream(wrapper, mem);
		if (written <= 0) {
			g_object_unref(mem);
			return FALSE;
		}
		GByteArray* ba = g_mime_stream_mem_get_byte_array(GMIME_STREAM_MEM(mem));
		guchar* part_data = g_memdup2(ba->data, ba->len);
		gsize part_size = ba->len;
		g_object_unref(mem);

		guchar* processed = NULL;
		gsize proc_size = 0;

		/* Index-Filename: eml_filename//internal_path
		 * internal_path=NULL nur wenn Root direkt ein MimePart ist → "0" */
		gchar* part_filename = internal_path
				? g_strdup_printf("%s//%s", eml_filename, internal_path)
				: g_strdup_printf("%s//0", eml_filename);
		sond_process_file_do_rec(wctx, part_data, part_size, part_filename,
				&processed, &proc_size, out_pdf_count);
		g_free(part_filename);
		g_free(part_data);

		if (!processed)
			return FALSE;

		GMimeContentEncoding enc = g_mime_part_get_content_encoding(part);
		GMimeStream* new_stream = g_mime_stream_mem_new_with_buffer(
				(const gchar*)processed, proc_size);
		g_free(processed);
		GMimeDataWrapper* new_wrapper = g_mime_data_wrapper_new_with_stream(new_stream, enc);
		g_mime_part_set_content(part, new_wrapper);
		g_object_unref(new_wrapper);
		g_object_unref(new_stream);

		modified = TRUE;
	}

	return modified;
}

static gint process_gmessage_for_ocr(guchar* data, gsize size,
		gchar const* filename, SondProcessFileCtx* wctx,
		guchar** out_data, gsize* out_size, gint* out_pdf_count,
		GError** error) {
	GMimeMessage* message = NULL;
	GMimeObject* root = NULL;

	message = gmessage_open(data, size);
	if (!message) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
				"process_gmessage_for_ocr: gmessage_open fehlgeschlagen");
		return -1;
	}

	root = g_mime_message_get_mime_part(message);
	if (!root) {
		g_object_unref(message);
		return 0;
	}

	/* Root-Aufruf: internal_path = NULL.
	 * Multipart-Root wird nicht gezählt, seine Kinder kriegen "0", "1" etc.
	 * Leaf/MessagePart-Root kriegt "0" (part_index beim Leaf-Zweig). */
	gboolean modified = gmessage_process_part(root, filename, NULL,
			wctx, 0, out_pdf_count);

	if (!modified) {
		g_object_unref(message);
		return 0;
	}

	*out_data = gmessage_to_buffer(message, out_size);
	g_object_unref(message);

	if (!*out_data) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
				"process_gmessage_for_ocr: Schreiben der geänderten E-Mail fehlgeschlagen");
		return -1;
	}

	return 0;
}

typedef struct {
	gchar const*   filename;
	SondProcessFileCtx* wctx;
	gint*          out_pdf_count;
} ProcessPdfData;

static gint process_emb_file(fz_context* ctx, pdf_obj* dict,
		pdf_obj* key, pdf_obj* val, gpointer data,
		GError** error) {
	pdf_obj* EF_F = NULL;
	fz_stream* stream = NULL;
	gchar const* path = NULL;
	fz_buffer* buf = NULL;
	pdf_document* doc = NULL;

	if (g_atomic_int_get(&((ProcessPdfData*)data)->wctx->cancel))
		return 0; //Abbruch angefordert

	EF_F = pdf_get_EF_F(((ProcessPdfData*)data)->wctx->ctx, val, &path, error);
	if (!EF_F) {
		if (((ProcessPdfData*)data)->wctx->log_func)
			((ProcessPdfData*)data)->wctx->log_func(((ProcessPdfData*)data)->wctx->log_func_data,
				((ProcessPdfData*)data)->filename,
				error ? (*error)->message : "unknown error");
		g_clear_error(error);
		return 0; //kein Abbruch, nur Fehler protokollieren
	}

	if (!path) {
		if (((ProcessPdfData*)data)->wctx->log_func)
			((ProcessPdfData*)data)->wctx->log_func(((ProcessPdfData*)data)->wctx->log_func_data,
					"Path für embedded file '%s' nicht gefunden",
					((ProcessPdfData*)data)->filename);
		return 0;
	}

	gchar* filename_emb = g_strdup_printf("%s//%s", ((ProcessPdfData*)data)->filename, path);

	fz_try(((ProcessPdfData*)data)->wctx->ctx)
		stream = pdf_open_stream(((ProcessPdfData*)data)->wctx->ctx, EF_F);
	fz_catch(((ProcessPdfData*)data)->wctx->ctx) {
		if (((ProcessPdfData*)data)->wctx->log_func)
			((ProcessPdfData*)data)->wctx->log_func(((ProcessPdfData*)data)->wctx->log_func_data,
				"Failed to open stream for embedded file '%s': %s",
				filename_emb, fz_caught_message(((ProcessPdfData*)data)->wctx->ctx));
		g_free(filename_emb);
		return 0;
	}

	fz_try(((ProcessPdfData*)data)->wctx->ctx)
		buf = fz_read_all(((ProcessPdfData*)data)->wctx->ctx, stream, 4096);
	fz_always(((ProcessPdfData*)data)->wctx->ctx)
		fz_drop_stream(((ProcessPdfData*)data)->wctx->ctx, stream);
	fz_catch(((ProcessPdfData*)data)->wctx->ctx) {
		if (((ProcessPdfData*)data)->wctx->log_func)
			((ProcessPdfData*)data)->wctx->log_func(((ProcessPdfData*)data)->wctx->log_func_data,
				"Failed to read stream for embedded file '%s': %s",
				filename_emb, fz_caught_message(((ProcessPdfData*)data)->wctx->ctx));
		return 0;
	}

	guchar* data_buf = NULL;
	gsize len = 0;
	len = fz_buffer_storage(((ProcessPdfData*)data)->wctx->ctx, buf, &data_buf);

	guchar* data_out = NULL;
	gsize size_out = 0;

	sond_process_file_do_rec(((ProcessPdfData*)data)->wctx, data_buf, len, filename_emb,
			&data_out, &size_out, ((ProcessPdfData*)data)->out_pdf_count);
	fz_drop_buffer(((ProcessPdfData*)data)->wctx->ctx, buf);

	if (!data_out) { //kein Fehler, nur nichts zu tun
		g_free(filename_emb);
		return 0;
	}

	/* Eigene Kopie anlegen, damit buf_new den Speicher besitzt und
		 * data_out sofort freigegeben werden kann. fz_new_buffer_from_data
		 * würde den Zeiger nur borgen – nach g_free(data_out) wäre der
		 * Buffer ungültig (use-after-free). */
	fz_buffer* buf_new = NULL;
	fz_try(((ProcessPdfData*)data)->wctx->ctx)
		buf_new = fz_new_buffer_from_copied_data(((ProcessPdfData*)data)->wctx->ctx, data_out, size_out);
	fz_catch(((ProcessPdfData*)data)->wctx->ctx) {
		if (((ProcessPdfData*)data)->wctx->log_func)
			((ProcessPdfData*)data)->wctx->log_func(((ProcessPdfData*)data)->wctx->log_func_data,
				"Failed to create buffer for processed file '%s': %s",
				filename_emb, fz_caught_message(((ProcessPdfData*)data)->wctx->ctx));
		g_free(data_out);
		g_free(filename_emb);
		return 0;
	}
	/* buf_new hat jetzt eine eigene Kopie – data_out wird nicht mehr benötigt */
	g_free(data_out);
	data_out = NULL;

	doc = pdf_pin_document(((ProcessPdfData*)data)->wctx->ctx, EF_F);
	if (!doc) {
		fz_drop_buffer(((ProcessPdfData*)data)->wctx->ctx, buf_new);
		if (((ProcessPdfData*)data)->wctx->log_func)
			((ProcessPdfData*)data)->wctx->log_func(((ProcessPdfData*)data)->wctx->log_func_data,
				"'%s' - Failed to pin PDF document from EF/F-object",
				filename_emb);
		g_free(filename_emb);
		return 0;
	}

	fz_try(((ProcessPdfData*)data)->wctx->ctx)
		pdf_update_stream(((ProcessPdfData*)data)->wctx->ctx, doc, EF_F, buf_new, 0);
	fz_always(((ProcessPdfData*)data)->wctx->ctx) {
		pdf_drop_document(((ProcessPdfData*)data)->wctx->ctx, doc);
		fz_drop_buffer(((ProcessPdfData*)data)->wctx->ctx, buf_new);
		/* data_out wurde bereits oben freigegeben */
	}
	fz_catch(((ProcessPdfData*)data)->wctx->ctx) {
		if (((ProcessPdfData*)data)->wctx->log_func)
			((ProcessPdfData*)data)->wctx->log_func(((ProcessPdfData*)data)->wctx->log_func_data,
				"Failed to update embedded stream for '%s': %s",
				filename_emb, fz_caught_message(((ProcessPdfData*)data)->wctx->ctx));
		g_free(filename_emb);
		return 0;
	}

	g_free(filename_emb);

	return 0;
}

static gint process_pdf_for_ocr(guchar* data, gsize size,
		gchar const* filename, SondProcessFileCtx* wctx,
		guchar** out_data, gsize* out_size, gint* out_pdf_count,
		GError** error) {
	pdf_document* doc = NULL;
	fz_stream* file = NULL;
	fz_buffer* buf = NULL;
	gint rc = 0;

	ProcessPdfData process_data = {filename, wctx, out_pdf_count};

	fz_try(wctx->ctx)
		file = fz_open_memory(wctx->ctx, data, size);
	fz_catch(wctx->ctx) {
		g_set_error(error, g_quark_from_static_string("mupdf"), fz_caught(wctx->ctx),
				"Failed to open PDF memory stream: %s",
				fz_caught_message(wctx->ctx) ? fz_caught_message(wctx->ctx) : "unknown error");
		return -1;
	}

	fz_try(wctx->ctx)
		doc = pdf_open_document_with_stream(wctx->ctx, file);
	fz_always(wctx->ctx)
		fz_drop_stream(wctx->ctx, file);
	fz_catch(wctx->ctx) {
		g_set_error(error, g_quark_from_static_string("mupdf"), fz_caught(wctx->ctx),
				"Failed to open PDF document: %s",
				fz_caught_message(wctx->ctx) ? fz_caught_message(wctx->ctx) : "unknown error");
		return -1;
	}

	//Alle embedded files durchgehen
	rc = pdf_walk_embedded_files(wctx->ctx, doc, process_emb_file, &process_data, error);
	if (rc) {
		pdf_drop_document(wctx->ctx, doc);
		return -1;
	}

	//pdf-page-tree OCRen
	rc = sond_ocr_pdf_doc(wctx->ctx, wctx->ocr_pool, doc,
			wctx->log_func, wctx->log_func_data, error);
	if (rc == -1) {
		pdf_drop_document(wctx->ctx, doc);
		return -1;
	}

	*out_pdf_count += 1;

	//Rückgabe-buffer füllen
	buf = pdf_doc_to_buf(wctx->ctx, doc, error);
	pdf_drop_document(wctx->ctx, doc);
	if (!buf)
		return -1;

	guchar* data_buf = NULL;
	gsize len = 0;
	len = fz_buffer_storage(wctx->ctx, buf, &data_buf);

	/* eigene Kopie anlegen */
	*out_data = g_memdup2(data_buf, len);
	*out_size = len;

	/* buffer freigeben */
	fz_drop_buffer(wctx->ctx, buf);

	return 0;
}

static void sond_process_file_do_rec(SondProcessFileCtx* wctx,
		guchar* data, gsize size, gchar const* filename,
		guchar** out_data, gsize* out_size, gint* out_pdf_count) {
	GError* error = NULL;
	gchar* mime_type = NULL;
	gint rc = 0;

	if (g_atomic_int_get(&wctx->cancel))
		return;

	if (wctx->log_func)
		wctx->log_func(wctx->log_func_data,
				"Entering File '%s'", filename);

	mime_type = mime_guess_content_type(data, size, &error);
	if (!mime_type) {
		if (wctx->log_func)
			wctx->log_func(wctx->log_func_data,
					"Failed to guess MIME type for file '%s': %s",
					filename, error ? error->message : "unknown error");
		g_clear_error(&error);
		return;
	}

	if (!g_strcmp0(mime_type, "application/pdf"))
		rc = process_pdf_for_ocr(data, size, filename, wctx,
				out_data, out_size, out_pdf_count, &error);
	else if (!g_strcmp0(mime_type, "application/zip"))
		rc = process_zip_for_ocr(data, size, filename, wctx,
				out_data, out_size, out_pdf_count, &error);
	else if (!g_strcmp0(mime_type, "message/rfc822"))
		rc = process_gmessage_for_ocr(data, size, filename, wctx,
				out_data, out_size, out_pdf_count, &error);

	if (rc == -1) {
		if (wctx->log_func)
		wctx->log_func(wctx->log_func_data,
				"Failed to process file '%s': %s",
				filename, error ? error->message : "unknown error");
		g_clear_error(&error);
		g_free(mime_type);

		return;
	}

	/* Indizierung: einmal am Schluss, mit dem aktuellsten Buffer */
	sond_index(wctx->ctx, wctx->log_func, wctx->log_func_data,
			wctx->index_ctx, filename,
			(*out_data && *out_size > 0) ? *out_data : data,
			(*out_data && *out_size > 0) ? *out_size : size,
			mime_type);

	g_free(mime_type);

	if (wctx->log_func)
		wctx->log_func(wctx->log_func_data,
				"Leaving File '%s'", filename);

	return;
}

void sond_process_file(SondProcessFileCtx* wctx,
		guchar* data, gsize size, gchar const* file_part,
		guchar** out_data, gsize* out_size, gint* out_pdf_count) {

	if (wctx->index_ctx) {
		GError* error = NULL;

		if (!sond_index_ctx_clear_file(wctx->index_ctx, file_part, &error)) {
			if (wctx->log_func)
				wctx->log_func(wctx->log_func_data,
						"sond_process_file: clear_file '%s': %s",
						file_part, error ? error->message : "unknown");
			g_clear_error(&error);
		}
	}

	sond_process_file_do_rec(wctx, data, size, file_part,
			out_data, out_size, out_pdf_count);

	return;
}

static void clean_hashtable(GHashTable* files) {
	GSList *to_remove = NULL;

	// Erst alle zu löschenden Elemente sammeln
	GHashTableIter iter;
	gpointer key;

	g_hash_table_iter_init(&iter, files);
	while (g_hash_table_iter_next(&iter, &key, NULL))
	{
		GPtrArray* arr_children = NULL;

	    SondFilePart* sfp = SOND_FILE_PART(key);
	    arr_children = sond_file_part_get_arr_opened_files(sfp);

	    if (arr_children)
			for (guint i = 0; i < arr_children->len; i++)
			{
				SondFilePart* sfp_child = g_ptr_array_index(arr_children, i);

				if (g_hash_table_contains(files, sfp_child))
					to_remove = g_slist_prepend(to_remove, sfp_child);
			}
	}

	// Dann löschen
	for (GSList *l = to_remove; l; l = l->next)
	    g_hash_table_remove(files, l->data);

	g_slist_free(to_remove);
}

void sond_process_fileparts(SondProcessFileCtx* wctx, GHashTable* files) {
	GHashTableIter iter = { 0 };
	gpointer key = NULL;

	clean_hashtable(files);

	g_hash_table_iter_init(&iter, files);
	while (g_hash_table_iter_next(&iter, &key, NULL)) {
		GBytes *bytes = NULL;
		gconstpointer data = NULL;
		GError* error = NULL;
		gsize length = 0;
		gchar* file_part = NULL;
		guchar* out_data = NULL;
		gsize out_size = 0;
		gint out_pdf_count = 0;

		if (g_atomic_int_get(&wctx->cancel))
			break;

		SondFilePart* sfp = SOND_FILE_PART(key);
		file_part = sond_file_part_get_filepart(sfp);

		bytes = sond_file_part_get_bytes(sfp, &error);
		if (!bytes) {
			if (wctx->log_func) {
				wctx->log_func(wctx->log_func_data,
						"sond_process_fileparts: get_bytes '%s': %s",
						sond_file_part_get_filepart(sfp),
						error ? error->message : "unknown error");
				g_error_free(error);
			}
			g_free(file_part);

			continue;
		}

		data = g_bytes_get_data(bytes, &length);

		sond_process_file(wctx, (guchar*) data, length, file_part,
				&out_data, &out_size, &out_pdf_count);
		g_bytes_unref(bytes);

		if (out_data && out_size > 0) {
			GBytes* out_bytes = g_bytes_new_take(out_data, out_size);
			gint rc = sond_file_part_replace(sfp, out_bytes, &error);
			g_bytes_unref(out_bytes);
			if (rc) {
				if (wctx->log_func) {
					wctx->log_func(wctx->log_func_data,
							"sond_process_fileparts: replace '%s': %s",
							file_part,
							error ? error->message : "unknown error");
					g_error_free(error);
				}
			}
		}

		g_free(file_part);
	}

	return;
}

SondProcessFileCtx* sond_process_file_create_wctx(fz_context* ctx,
		void (*log_func)(void*, gchar const*, ...), gpointer log_func_data,
		gchar const* tessdata_path, gint num_ocr_threads,
		gchar const* index_db_filename, GError **error) {

	SondProcessFileCtx* wctx = g_new0(SondProcessFileCtx, 1);

	/* fz_context */
	wctx->ctx = ctx;

	wctx->progress = 0;
	wctx->cancel = 0;

	wctx->log_func = log_func;
	wctx->log_func_data = (gpointer) log_func_data;

	wctx->ocr_pool = sond_ocr_pool_new(tessdata_path, "deu",
			num_ocr_threads, &wctx->cancel, &wctx->progress, error);
	if (!wctx->ocr_pool)
		return NULL;

	wctx->index_ctx = sond_index_ctx_new(index_db_filename, NULL, 0, 0, error);
	if (!wctx->index_ctx) {
		sond_ocr_pool_free(wctx->ocr_pool);

		return NULL;
	}

	return wctx;
}

void sond_process_file_destroy_wctx(SondProcessFileCtx *wctx) {
	if (wctx->index_ctx)
		sond_index_ctx_free(wctx->index_ctx);
	if (wctx->ocr_pool)
		sond_ocr_pool_free(wctx->ocr_pool);

	g_free(wctx);

	return;
}

