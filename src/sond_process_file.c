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
#include "sond_log_and_error.h"
#include "sond_mime.h"
#include "sond_file_helper.h"


static void sond_process_file_do_rec(SondProcessFileCtx* wctx,
		guchar* data, gsize size, gchar const* filename,
		guchar** out_data, gsize* out_size, gint* out_pdf_count,
		SondPageRange const* range);

SondPageRange* sond_page_range_new(gint von, gint bis) {
	SondPageRange* range = g_new0(SondPageRange, 1);
	range->von = von;
	range->bis = bis;
	return range;
}

SondPageRange* sond_page_range_new_gmessage_header(void) {
	SondPageRange* range = g_new0(SondPageRange, 1);
	range->von = -1;
	range->bis = -1;
	range->gmessage_header_only = TRUE;
	return range;
}

SondPageRange* sond_page_range_new_gmessage_message(void) {
	SondPageRange* range = g_new0(SondPageRange, 1);
	range->von = -1;
	range->bis = -1;
	range->gmessage_message = TRUE;
	return range;
}

SondPageRange* sond_page_range_new_pdf_pagetree(void) {
	SondPageRange* range = g_new0(SondPageRange, 1);
	range->von = -1;
	range->bis = -1;
	range->pdf_pagetree_only = TRUE;
	return range;
}

SondPageRange* sond_page_range_copy(SondPageRange const *range) {
	SondPageRange* copy = NULL;

	if (!range)
		return NULL;

	copy = g_memdup2(range, sizeof(SondPageRange));
	copy->more = range->more ? g_array_copy(range->more) : NULL;

	return copy;
}

void sond_page_range_free(gpointer p) {
	SondPageRange* range = (SondPageRange*) p;

	if (!range)
		return;

	g_clear_pointer(&range->more, g_array_unref);
	g_free(range);
}

static gint page_span_cmp(gconstpointer a, gconstpointer b) {
	SondPageSpan const* sa = a;
	SondPageSpan const* sb = b;

	return (sa->von > sb->von) - (sa->von < sb->von);
}

void sond_page_range_add(SondPageRange* range, gint von, gint bis) {
	GArray* all = NULL;
	GArray* more = NULL;
	SondPageSpan cur = { 0 };

	if (!range || range->von < 0 || von < 0)
		return;

	if (bis < von)
		bis = von;

	all = g_array_new(FALSE, FALSE, sizeof(SondPageSpan));
	cur.von = range->von;
	cur.bis = range->bis;
	g_array_append_val(all, cur);
	if (range->more)
		g_array_append_vals(all, range->more->data, range->more->len);
	cur.von = von;
	cur.bis = bis;
	g_array_append_val(all, cur);
	g_array_sort(all, page_span_cmp);

	/* überlappende und aneinander grenzende Bereiche verschmelzen */
	more = g_array_new(FALSE, FALSE, sizeof(SondPageSpan));
	cur = g_array_index(all, SondPageSpan, 0);
	for (guint i = 1; i < all->len; i++) {
		SondPageSpan s = g_array_index(all, SondPageSpan, i);

		if ((gint64) s.von <= (gint64) cur.bis + 1)
			cur.bis = MAX(cur.bis, s.bis);
		else {
			g_array_append_val(more, cur);
			cur = s;
		}
	}
	g_array_append_val(more, cur);
	g_array_unref(all);

	/* der erste (kleinste) Bereich steht in von/bis, alle weiteren in more */
	range->von = g_array_index(more, SondPageSpan, 0).von;
	range->bis = g_array_index(more, SondPageSpan, 0).bis;
	g_clear_pointer(&range->more, g_array_unref);
	if (more->len > 1) {
		g_array_remove_index(more, 0);
		range->more = more;
	}
	else
		g_array_unref(more);
}

gint sond_page_range_count(SondPageRange const* range) {
	if (range && range->von >= 0 && range->more)
		return 1 + (gint) range->more->len;

	return 1;
}

void sond_page_range_get(SondPageRange const* range, gint i, gint* von,
		gint* bis) {
	SondPageSpan s = { -1, -1 };

	if (range && range->von >= 0) {
		if (i == 0) {
			s.von = range->von;
			s.bis = range->bis;
		}
		else if (range->more && i - 1 < (gint) range->more->len)
			s = g_array_index(range->more, SondPageSpan, i - 1);
	}

	if (von)
		*von = s.von;
	if (bis)
		*bis = s.bis;
}

gboolean sond_page_range_contains(SondPageRange const* range, gint page) {
	if (!range || range->von < 0)
		return TRUE;

	if (page >= range->von && page <= range->bis)
		return TRUE;

	for (guint i = 0; range->more && i < range->more->len; i++) {
		SondPageSpan const* s = &g_array_index(range->more, SondPageSpan, i);

		if (page >= s->von && page <= s->bis)
			return TRUE;
	}

	return FALSE;
}

void sond_page_range_merge(GHashTable* ht, gpointer sfp, SondPageRange* range) {
	gpointer key = NULL;
	gpointer value = NULL;
	SondPageRange* existing = NULL;

	if (!g_hash_table_lookup_extended(ht, sfp, &key, &value)) {
		g_hash_table_insert(ht, sfp, range);

		return;
	}

	existing = (SondPageRange*) value;

	if (!existing || !range) {
		/* einer von beiden will die ganze Datei -> ganze Datei */
		sond_page_range_free(range);
		if (existing)
			g_hash_table_insert(ht, sfp, NULL); /* gibt den Wert und die
					* überzählige Referenz auf sfp frei */
		else
			g_object_unref(sfp);

		return;
	}

	if (existing->pdf_pagetree_only || range->pdf_pagetree_only) {
		/* alle Seiten, aber keine eingebetteten Dateien */
		existing->pdf_pagetree_only = TRUE;
		existing->von = -1;
		existing->bis = -1;
		g_clear_pointer(&existing->more, g_array_unref);
	}
	else if (existing->von >= 0 && range->von >= 0) {
		/* Vereinigung der Seitenbereiche, nicht deren Hülle */
		for (gint i = 0; i < sond_page_range_count(range); i++) {
			gint von = 0;
			gint bis = 0;

			sond_page_range_get(range, i, &von, &bis);
			sond_page_range_add(existing, von, bis);
		}
	}
	else {
		/* Teile einer E-Mail: Header + Inline-Teile umfassen den Header */
		existing->gmessage_header_only |= range->gmessage_header_only;
		existing->gmessage_message |= range->gmessage_message;
	}

	sond_page_range_free(range);
	g_object_unref(sfp);
}

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

	/* Für die Index-Suche (check_coverage_one() in zond_indexsuche.c): Zahl
	 * der direkten Einträge dieses Containers DB-seitig festhalten, ohne
	 * dass dafür je wieder in die ZIP hineingesehen werden müsste. Nur
	 * eine Ebene - was sich innerhalb eines Eintrags, der selbst wieder
	 * ein Container ist, verbirgt, zählt hier bewusst nicht mit (ToDo.c,
	 * 12.-14.09.2026). Unabhängig von "modified" setzen: die Zahl der
	 * Einträge ist auch dann bekannt, wenn keiner von ihnen tatsächlich
	 * verändert wurde. */
	if (wctx->index_ctx) {
		GError *error_entrycount = NULL;

		if (!sond_index_ctx_set_entry_count(wctx->index_ctx, filename,
				(gint) num_entries, &error_entrycount)) {
			if (wctx->log_func)
				wctx->log_func(wctx->log_func_data,
						"ZIP '%s': entry_count nicht gespeichert: %s",
						filename, error_entrycount ?
								error_entrycount->message : "?");
			g_clear_error(&error_entrycount);
		}
	}

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

		/* Größe steht im Archiv und kann beschädigt oder gefälscht sein:
		 * g_malloc() würde das Programm beenden */
		if (zstat.size > SOND_ZIP_ENTRY_MAX_SIZE) {
			if (wctx->log_func)
				wctx->log_func(wctx->log_func_data,
					"ZIP '%s': Eintrag '%s' zu groß (%" G_GUINT64_FORMAT
					" Byte) - übersprungen", filename, entry_name,
					(guint64) zstat.size);
			continue;
		}

		guchar* entry_data = g_try_malloc(zstat.size);
		if (!entry_data && zstat.size > 0) {
			if (wctx->log_func)
				wctx->log_func(wctx->log_func_data,
					"ZIP '%s': Eintrag '%s' (%" G_GUINT64_FORMAT " Byte): "
					"nicht genug Speicher - übersprungen", filename,
					entry_name, (guint64) zstat.size);
			continue;
		}

		zip_file_t* zf = zip_fopen_index(archive, (zip_uint64_t)i, 0);
		if (!zf) {
			if (wctx->log_func)
				wctx->log_func(wctx->log_func_data,
					"ZIP '%s': Kann Eintrag '%s' nicht öffnen: %s",
					filename, entry_name,
					zip_error_strerror(zip_get_error(archive)));
			g_free(entry_data);
			continue;
		}

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
				&processed_data, &processed_size, out_pdf_count, NULL);
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
					"ZIP '%s': Eintrag '%s' nicht ersetzt: %s",
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
					"ZIP '%s': Eintrag '%s' nicht ersetzt: %s",
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

/* Textteil, dessen Inhalt nicht UTF-8 ist, aber einen Zeichensatz deklariert
 * (z.B. ISO-8859-2, UTF-16): nach UTF-8 gewandelt, damit der Index den Text
 * liest und nicht Bytes in windows-1252-Deutung. Wie beim Gesamttext einer
 * Mail (sond_text_extract.c) nur, wenn der Inhalt kein gültiges UTF-8 ist.
 * NULL, wenn nichts zu wandeln ist oder die Wandlung scheitert - dann gilt
 * der Inhalt unverändert. */
static guchar* gmessage_part_text_to_utf8(GMimePart* part,
		guchar const* data, gsize size, gsize* out_size) {
	GMimeContentType* ct = g_mime_object_get_content_type(GMIME_OBJECT(part));
	gchar const* charset = NULL;

	if (!ct || !g_mime_content_type_is_type(ct, "text", "*"))
		return NULL;

	charset = g_mime_content_type_get_parameter(ct, "charset");
	if (!charset || !g_ascii_strcasecmp(charset, "utf-8") ||
			!g_ascii_strcasecmp(charset, "utf8") ||
			!g_ascii_strcasecmp(charset, "us-ascii"))
		return NULL;

	if (g_utf8_validate((gchar const*) data, (gssize) size, NULL))
		return NULL;

	return (guchar*) g_convert((gchar const*) data, (gssize) size, "UTF-8",
			charset, NULL, out_size, NULL);
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
		SondProcessFileCtx* wctx, gint part_index, gint* out_pdf_count,
		gboolean inline_only) {
	gboolean modified = FALSE;

	if (g_atomic_int_get(&wctx->cancel))
		return FALSE;

	/* inline_only: angebundene Mail = Header + Inline-Teile (ToDo.c #197) -
	 * Anhänge (Content-Disposition "attachment") überspringen */
	if (inline_only && !GMIME_IS_MULTIPART(object)) {
		GMimeContentDisposition* disp =
				g_mime_object_get_content_disposition(object);
		gchar const* dval = disp ?
				g_mime_content_disposition_get_disposition(disp) : NULL;

		if (dval && !g_ascii_strcasecmp(dval, "attachment"))
			return FALSE;
	}

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
					wctx, i, out_pdf_count, inline_only))
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
						&processed, &proc_size, out_pdf_count, NULL);
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
		gsize conv_size = 0;
		guchar* converted = gmessage_part_text_to_utf8(part, part_data,
				part_size, &conv_size);
		sond_process_file_do_rec(wctx, converted ? converted : part_data,
				converted ? conv_size : part_size, part_filename,
				&processed, &proc_size, out_pdf_count, NULL);
		g_free(part_filename);
		g_free(converted);
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
		gboolean inline_only, GError** error) {
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
			wctx, 0, out_pdf_count, inline_only);

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
	gboolean*      changed; /* wird auf TRUE gesetzt, sobald mindestens ein
	                          * embedded file tatsächlich ersetzt wurde
	                          * (pdf_update_stream erfolgreich) - s.
	                          * process_pdf_for_ocr() */
	GHashTable*    addresses; /* s. pdf_emb_addresses_new() */
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
				"'%s': eingebettete Datei nicht lesbar: %s",
				((ProcessPdfData*)data)->filename,
				(error && *error) ? (*error)->message : "unknown error");
		g_clear_error(error);
		return 0; //kein Abbruch, nur Fehler protokollieren
	}

	//Pfad im Index ist die Adresse, nicht der Dateiname (ToDo.c #193)
	path = g_hash_table_lookup(((ProcessPdfData*)data)->addresses, val);
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
		g_free(filename_emb);
		return 0;
	}

	guchar* data_buf = NULL;
	gsize len = 0;
	len = fz_buffer_storage(((ProcessPdfData*)data)->wctx->ctx, buf, &data_buf);

	guchar* data_out = NULL;
	gsize size_out = 0;

	sond_process_file_do_rec(((ProcessPdfData*)data)->wctx, data_buf, len, filename_emb,
			&data_out, &size_out, ((ProcessPdfData*)data)->out_pdf_count, NULL);
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

	/* An dieser Stelle nur erreicht, wenn pdf_update_stream() nicht
	 * geworfen hat - das äußere Dokument wurde also tatsächlich
	 * verändert (s. process_pdf_for_ocr(): entscheidet danach zusammen
	 * mit dem OCR-eigenen "changed", ob ein Rewrite nötig ist). */
	if (((ProcessPdfData*)data)->changed)
		*((ProcessPdfData*)data)->changed = TRUE;

	g_free(filename_emb);

	return 0;
}

/* with_embedded: eingebettete Dateien mitverarbeiten (nur bei ganzer
 * Datei). Gezählt werden sie immer (out_n_emb), damit der Aufrufer
 * erkennt, ob "nur Seiten" hier zugleich die ganze Datei ist. */
static gint process_pdf_for_ocr(guchar* data, gsize size,
		gchar const* filename, SondProcessFileCtx* wctx,
		guchar** out_data, gsize* out_size, gint* out_pdf_count,
		gint seite_von, gint seite_bis, gboolean with_embedded,
		gint* out_n_emb, GError** error) {
	pdf_document* doc = NULL;
	fz_stream* file = NULL;
	fz_buffer* buf = NULL;
	gint rc = 0;
	gboolean emb_changed = FALSE;
	gboolean ocr_changed = FALSE;

	ProcessPdfData process_data = {filename, wctx, out_pdf_count, &emb_changed};

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

	/* Adressen der Anhänge: zählen, in der Index-DB festhalten (pdf_embedded,
	 * ToDo.c #199 - bei jedem Lauf, auch nur Seiten/Seitenbereich) und bei
	 * ganzer Datei die Anhänge verarbeiten */
	process_data.addresses = pdf_emb_addresses_new(wctx->ctx, doc, error);
	if (!process_data.addresses) {
		pdf_drop_document(wctx->ctx, doc);
		return -1;
	}
	*out_n_emb = (gint) g_hash_table_size(process_data.addresses);

	if (wctx->index_ctx) {
		GPtrArray* list = g_ptr_array_new();
		GHashTableIter iter = { 0 };
		gpointer value = NULL;
		GError* error_emb = NULL;

		g_hash_table_iter_init(&iter, process_data.addresses);
		while (g_hash_table_iter_next(&iter, NULL, &value))
			g_ptr_array_add(list, value);
		if (!sond_index_ctx_set_pdf_embedded(wctx->index_ctx, filename, list,
				&error_emb)) {
			if (wctx->log_func)
				wctx->log_func(wctx->log_func_data, "pdf_embedded '%s': %s",
						filename, error_emb ? error_emb->message : "?");
			g_clear_error(&error_emb);
		}
		g_ptr_array_unref(list);
	}

	if (with_embedded)
		rc = pdf_walk_embedded_files(wctx->ctx, doc, process_emb_file,
				&process_data, error);
	g_hash_table_destroy(process_data.addresses);
	if (rc) {
		pdf_drop_document(wctx->ctx, doc);
		return -1;
	}

	//pdf-page-tree OCRen
	rc = sond_ocr_pdf_doc(wctx->ctx, wctx->ocr_pool, doc,
			(SondOcrMode) wctx->ocr_mode, seite_von, seite_bis,
			wctx->log_func, wctx->log_func_data, &ocr_changed, error);
	if (rc == -1) {
		pdf_drop_document(wctx->ctx, doc);
		return -1;
	}

	*out_pdf_count += 1;

	if (!emb_changed && !ocr_changed) {
		/* Weder an eingebetteten Dateien noch an den eigenen Seiten wurde
		 * tatsächlich etwas verändert (z.B. weil überall schon Text
		 * vorlag). Der Rewrite über pdf_doc_to_buf() ist dann nicht nur
		 * unnötig, sondern kann bei strukturell fragilen PDFs sogar
		 * fehlschlagen, obwohl gar nichts zu tun gewesen wäre - out_data/
		 * out_size bleiben NULL/0, der Aufrufer nutzt dann die
		 * unveränderten Originaldaten (s. sond_process_file_do_rec()). */
		pdf_drop_document(wctx->ctx, doc);
		return 0;
	}

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
		guchar** out_data, gsize* out_size, gint* out_pdf_count,
		SondPageRange const* range) {
	GError* error = NULL;
	gchar* mime_type = NULL;
	gint rc = 0;
	gint seite_von = range ? range->von : -1;
	gint seite_bis = range ? range->bis : -1;
	gboolean pdf_pagetree_only = range ? range->pdf_pagetree_only : FALSE;
	/* angebundene Mail (Header + Inline-Teile, ToDo.c #197): Header wie
	 * bei gmessage_header_only, dazu die Inline-Mimeparts */
	gboolean gmessage_message = range ? range->gmessage_message : FALSE;
	gboolean gmessage_header_only = range ?
			(range->gmessage_header_only || gmessage_message) : FALSE;

	if (g_atomic_int_get(&wctx->cancel))
		return;

	if (wctx->log_func)
		wctx->log_func(wctx->log_func_data,
				"Entering File '%s'", filename);

	mime_type = mime_guess_content_type(data, size, filename, &error);
	if (!mime_type) {
		if (wctx->log_func)
			wctx->log_func(wctx->log_func_data,
					"Failed to guess MIME type for file '%s': %s",
					filename, error ? error->message : "unknown error");
		g_clear_error(&error);
		return;
	}

	if (!g_strcmp0(mime_type, "application/pdf")) {
		/* Eingebettete Dateien nur bei ganzer Datei - "nur Seiten" und ein
		 * Seitenbereich meinen den PageTree, die Einbettungen sind eigene
		 * Fileparts. Hat die PDF keine Einbettungen, ist "nur Seiten"
		 * zugleich die ganze Datei (dann normal "x.pdf" abdecken). */
		gboolean whole = (seite_von == -1 && seite_bis == -1 &&
				!pdf_pagetree_only);
		gint n_emb = 0;

		rc = process_pdf_for_ocr(data, size, filename, wctx,
				out_data, out_size, out_pdf_count, seite_von, seite_bis,
				whole, &n_emb, &error);
		if (pdf_pagetree_only && n_emb == 0)
			pdf_pagetree_only = FALSE;
	}
	else if (!g_strcmp0(mime_type, "application/zip"))
		rc = process_zip_for_ocr(data, size, filename, wctx,
				out_data, out_size, out_pdf_count, &error);
	else if (!g_strcmp0(mime_type, "message/rfc822") &&
			(!gmessage_header_only || gmessage_message))
		/* Bei gmessage_header_only wird ohnehin nur der Header indiziert
		 * (Schritt 3, s. ToDo.c 17.09.2026) - das OCRen/Bearbeiten
		 * eingebetteter Inhalte (Bilder, PDF-Attachments) wäre hier
		 * verschwendete Arbeit und wird deshalb übersprungen. Bei
		 * gmessage_message nur die Inline-Teile, keine Anhänge. */
		rc = process_gmessage_for_ocr(data, size, filename, wctx,
				out_data, out_size, out_pdf_count, gmessage_message, &error);

	if (rc == -1) {
		if (wctx->log_func)
		wctx->log_func(wctx->log_func_data,
				"Failed to process file '%s': %s",
				filename, error ? error->message : "unknown error");
		g_clear_error(&error);

		/* PDF: scheitert die OCR-/Anhang-Stufe (z.B. Seitenbaum
		 * beschädigt), den vorhandenen Text der Originaldaten trotzdem
		 * indizieren. Mit Modus "kein OCR" vermerkt: ein späterer Lauf mit
		 * OCR-Prüfung versucht es erneut, die Datei gilt nicht als
		 * vollständig bearbeitet. */
		if (!g_strcmp0(mime_type, "application/pdf") &&
				!g_atomic_int_get(&wctx->cancel)) {
			if (wctx->log_func)
				wctx->log_func(wctx->log_func_data,
						"'%s': nur der vorhandene Text wird indiziert",
						filename);
			sond_index(wctx->ctx, wctx->log_func, wctx->log_func_data,
					wctx->index_ctx, filename, data, size, mime_type,
					seite_von, seite_bis, SOND_OCR_MODE_NONE, &wctx->cancel,
					FALSE, pdf_pagetree_only);
		}

		g_free(mime_type);

		return;
	}

	/* Indizierung: einmal am Schluss, mit dem aktuellsten Buffer */
	sond_index(wctx->ctx, wctx->log_func, wctx->log_func_data,
			wctx->index_ctx, filename,
			(*out_data && *out_size > 0) ? *out_data : data,
			(*out_data && *out_size > 0) ? *out_size : size,
			mime_type, seite_von, seite_bis, wctx->ocr_mode, &wctx->cancel,
			gmessage_header_only, pdf_pagetree_only);

	g_free(mime_type);

	if (wctx->log_func)
		wctx->log_func(wctx->log_func_data,
				"Leaving File '%s'", filename);

	return;
}

void sond_process_file(SondProcessFileCtx* wctx,
		guchar* data, gsize size, gchar const* file_part,
		guchar** out_data, gsize* out_size, gint* out_pdf_count,
		SondPageRange const* range) {

	/* Kein pauschales clear_file mehr vor dem (Neu-)Indizieren: die
	 * Invalidierung/Ersetzung passiert jetzt seitenweise innerhalb von
	 * sond_index() (sond_index_ctx_should_process_page/_clear_page), damit
	 * a) ein auf einen Seitenbereich beschränkter Lauf nicht versehentlich
	 * andere, bereits indizierte Seiten derselben Datei mitlöscht, und
	 * b) unveränderte, bereits ausreichend indizierte Seiten übersprungen
	 * werden können. */
	sond_process_file_do_rec(wctx, data, size, file_part,
			out_data, out_size, out_pdf_count, range);

	return;
}

static void clean_hashtable(GHashTable* files) {
	GSList *to_remove = NULL;

	// Erst alle zu löschenden Elemente sammeln
	GHashTableIter iter;
	gpointer key;

	gpointer value;

	g_hash_table_iter_init(&iter, files);
	while (g_hash_table_iter_next(&iter, &key, &value))
	{
		GPtrArray* arr_children = NULL;

		/* Nur ein Eintrag für die ganze Datei (value == NULL) umfasst die
		 * enthaltenen Teile - "nur Seiten"/Seitenbereich/"nur Header"
		 * nicht, deren Kinder werden eigens verarbeitet. */
		if (value)
			continue;

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

gint sond_process_file_record_structure(SondIndexCtx* index_ctx,
		SondFilePart* container, GError** error) {
	g_autofree gchar* filepart = NULL;

	if (!index_ctx || !container)
		return 0;

	filepart = sond_file_part_get_filepart(container);

	if (SOND_IS_FILE_PART_PDF(container)) {
		GPtrArray* addresses = sond_file_part_pdf_get_emb_addresses(
				SOND_FILE_PART_PDF(container), error);
		gboolean ok = FALSE;

		if (!addresses)
			return -1;

		ok = sond_index_ctx_set_pdf_embedded(index_ctx, filepart, addresses,
				error);
		g_ptr_array_unref(addresses);

		return ok ? 0 : -1;
	}

	if (SOND_IS_FILE_PART_GMESSAGE(container)) {
		GBytes* bytes = sond_file_part_get_bytes(container, error);
		gsize size = 0;
		gconstpointer data = NULL;
		gboolean ok = FALSE;

		if (!bytes)
			return -1;

		data = g_bytes_get_data(bytes, &size);
		ok = sond_index_ctx_record_gmessage_structure(index_ctx, filepart,
				data, size, error);
		g_bytes_unref(bytes);

		return ok ? 0 : -1;
	}

	return 0;
}

/* Sind Header und alle Inline-Teile der Mail schon mindestens mit dem
 * angeforderten Modus abgedeckt? Unbekannte Inline-Teile (Mail noch nie
 * indiziert) -> nein. */
static gboolean gmessage_message_covered(SondProcessFileCtx* wctx,
		gchar const* file_part) {
	gboolean known = FALSE;
	gboolean covered = TRUE;
	GPtrArray* parts = sond_index_ctx_gmessage_message_parts(wctx->index_ctx,
			file_part, &known);

	for (guint i = 0; covered && i < parts->len; i++)
		if (sond_index_ctx_coverage_get(wctx->index_ctx,
				g_ptr_array_index(parts, i)) < wctx->ocr_mode)
			covered = FALSE;
	g_ptr_array_unref(parts);

	return known && covered;
}

/* Datei im Dateisystem (kein Teil eines Containers), die sich allein am Namen
 * als nicht indizierbar erkennen läßt: die Index-DB samt Journal, Projekt-
 * dateien (.ZND) und alles, dessen Endung einem bekannten, nicht
 * indizierbaren Typ entspricht. Unbekannte oder fehlende Endung: nicht
 * erkennbar, die Datei wird wie bisher über den Inhalt geprüft (z.B. eine
 * Mail ohne Endung). Container (ZIP) bleiben im Lauf. */
static gboolean file_part_not_indexable(SondFilePart* sfp,
		gchar const* file_part) {
	gchar const* base = NULL;
	gchar const* mime = NULL;
	gsize len = 0;

	if (sond_file_part_get_parent(sfp))
		return FALSE;

	base = strrchr(file_part, '/');
	base = base ? base + 1 : file_part;

	if (g_str_has_prefix(base, ".sond_index.db"))
		return TRUE;

	len = strlen(base);
	if (len >= 4 && !g_ascii_strcasecmp(base + len - 4, ".znd"))
		return TRUE;

	mime = mime_from_extension(base);
	if (!mime)
		return FALSE;

	return !sond_index_mime_type_supported(mime) &&
			g_strcmp0(mime, "application/zip");
}

void sond_process_fileparts(SondProcessFileCtx* wctx, GHashTable* files) {
	GHashTableIter iter = { 0 };
	gpointer key = NULL;
	gpointer value = NULL;

	clean_hashtable(files);

	g_hash_table_iter_init(&iter, files);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		GBytes *bytes = NULL;
		gconstpointer data = NULL;
		GError* error = NULL;
		gsize length = 0;
		gchar* file_part = NULL;
		guchar* out_data = NULL;
		gsize out_size = 0;
		gint out_pdf_count = 0;
		SondPageRange* range = (SondPageRange*) value; /* NULL = ganze Datei */
		/* angebundene Mail (Header + Inline-Teile, ToDo.c #197): der Header
		 * ist ihr Coverage-Schlüssel wie bei gmessage_header_only, die
		 * Inline-Teile haben ihre eigenen */
		gboolean gmessage_message = range ? range->gmessage_message : FALSE;
		gboolean gmessage_header_only = range ?
				(range->gmessage_header_only || gmessage_message) : FALSE;
		gboolean pdf_pagetree_only = range ? range->pdf_pagetree_only : FALSE;
		/* Nur Seiten oder Seitenbereich einer PDF: Coverage-Schlüssel der
		 * Seiten "x.pdf//" (s. sond_index()). coverage_get() findet darüber
		 * auch eine Abdeckung der ganzen Datei "x.pdf". */
		gboolean pages_key = range && !gmessage_header_only &&
				(pdf_pagetree_only || range->von >= 0);
		gchar const *collapse_key = NULL;

		if (g_atomic_int_get(&wctx->cancel))
			break;

		SondFilePart* sfp = SOND_FILE_PART(key);
		file_part = sond_file_part_get_filepart(sfp);

		/* Bei gmessage_header_only wird - wie in sond_index() - unter dem
		 * eigenen Pfad "file_part//header" abgedeckt, unabhängig vom Rest
		 * der Mail (s. ToDo.c, 17.09.2026, E-Mail-Coverage-Redesign,
		 * Schritt 3/6). Die beiden coverage_get()-Prüfungen unten (Vorab-
		 * Kurzschluss und Nach-Prüfung fürs Coalescing) müssen deshalb
		 * denselben Pfad abfragen, den sond_index() tatsächlich beschreibt -
		 * sonst würde hier immer "nicht abgedeckt" gesehen, obwohl der
		 * Header schon indiziert ist (oder umgekehrt fälschlich der Pfad
		 * der ganzen Datei geprüft). */
		gchar *coverage_key = gmessage_header_only ?
				g_strdup_printf("%s//header", file_part) :
				pages_key ? g_strdup_printf("%s//", file_part) : file_part;

		/* Schneller Vorab-Check über die coalescierte coverage-Tabelle
		 * (dieselbe wie beim Abdeckungs-Check der Indexsuche, s.
		 * check_coverage_one() in zond_indexsuche.c): ist coverage_key (oder
		 * ein abdeckender Vorfahre) schon bei mindestens dem angeforderten
		 * OCR-Modus vollständig indiziert, muss die Datei für diesen Lauf
		 * gar nicht erst geöffnet werden - spart bei "Gesamtes
		 * Projektverzeichnis" auf einem bereits durchindizierten Projekt
		 * das Öffnen/OCR-Prüfen jeder einzelnen Datei. Bei "erzwingen"
		 * (FORCE) nie überspringen - das entspricht demselben Vorbehalt
		 * wie bei sond_index_ctx_should_process_page(). Ist range gesetzt
		 * (nur ein Seitenbereich angefragt), ist eine volle Datei-Abdeckung
		 * immer hinreichend (impliziert jeden Teilbereich) - eine NICHT
		 * ausreichende Datei-Abdeckung wird hier bewusst NICHT als "range
		 * auch nicht abgedeckt" gewertet, sondern führt einfach zum
		 * normalen (langsameren, aber korrekten) Weg unten. */
		if (wctx->index_ctx && wctx->ocr_mode != SOND_OCR_MODE_FORCE &&
				(gmessage_message ?
						gmessage_message_covered(wctx, file_part) :
						sond_index_ctx_coverage_get(wctx->index_ctx, coverage_key)
								>= wctx->ocr_mode)) {
			if (coverage_key != file_part) g_free(coverage_key);
			g_free(file_part);
			continue;
		}

		/* Dateien, die schon an Name/Endung als nicht indizierbar zu erkennen
		 * sind (Bilder, Videos, Datenbanken, die Index-DB selbst, ...), gar
		 * nicht erst lesen: sie würden komplett in den Speicher geladen (bei
		 * SeaDrive-Platzhaltern auch heruntergeladen), nur damit
		 * sond_index() sie danach verwirft. Sie haben nie einen
		 * coverage-Eintrag, wurden also in jedem Lauf erneut gelesen. */
		if (file_part_not_indexable(sfp, file_part)) {
			if (coverage_key != file_part) g_free(coverage_key);
			g_free(file_part);
			continue;
		}

		bytes = sond_file_part_get_bytes(sfp, &error);
		if (!bytes) {
			if (wctx->log_func)
				wctx->log_func(wctx->log_func_data,
						"sond_process_fileparts: get_bytes '%s': %s",
						file_part,
						error ? error->message : "unknown error");
			g_clear_error(&error);
			if (coverage_key != file_part) g_free(coverage_key);
			g_free(file_part);

			continue;
		}

		data = g_bytes_get_data(bytes, &length);

		/* Mehrere Seitenbereiche (disjunkt): nacheinander, jeweils auf dem
		 * Ergebnis des vorigen (OCR-Text bleibt erhalten); geschrieben wird
		 * die Datei einmal am Schluss. */
		{
			gint n_ranges = sond_page_range_count(range);
			guchar* cur_data = (guchar*) data;
			gsize cur_size = length;

			for (gint r = 0; r < n_ranges; r++) {
				SondPageRange range_one = { 0 };
				SondPageRange const* range_use = range;
				guchar* out_one = NULL;
				gsize out_one_size = 0;

				if (n_ranges > 1) {
					range_one = *range;
					range_one.more = NULL;
					sond_page_range_get(range, r, &range_one.von, &range_one.bis);
					range_use = &range_one;
				}

				sond_process_file(wctx, cur_data, cur_size, file_part,
						&out_one, &out_one_size, &out_pdf_count, range_use);

				if (out_one && out_one_size > 0) {
					g_free(out_data);
					out_data = out_one;
					out_size = out_one_size;
					cur_data = out_data;
					cur_size = out_size;
				}
				else
					g_free(out_one);

				if (g_atomic_int_get(&wctx->cancel))
					break;
			}
		}
		g_bytes_unref(bytes);

		if (out_data && out_size > 0) {
			GBytes* out_bytes = g_bytes_new_take(out_data, out_size);
			gint rc = sond_file_part_replace(sfp, out_bytes, &error);
			g_bytes_unref(out_bytes);
			if (rc) {
				if (wctx->log_func)
					wctx->log_func(wctx->log_func_data,
							"sond_process_fileparts: replace '%s': %s",
							file_part,
							error ? error->message : "unknown error");
				g_clear_error(&error);
			}
		}

		/* Teil einer PDF oder Mail: deren Struktur (Anhänge bzw. Mimeparts)
		 * festhalten - die Eltern-Datei wurde zum Lesen des Teils ohnehin
		 * geöffnet. Grundlage fürs Auflösen/Zusammenfassen der Coverage
		 * (ToDo.c #199). */
		if (!g_atomic_int_get(&wctx->cancel) && wctx->index_ctx) {
			SondFilePart* parent = sond_file_part_get_parent(sfp);

			if (SOND_IS_FILE_PART_PDF(parent) || SOND_IS_FILE_PART_GMESSAGE(parent)) {
				GError* error_struct = NULL;

				if (sond_process_file_record_structure(wctx->index_ctx, parent,
						&error_struct)) {
					if (wctx->log_func)
						wctx->log_func(wctx->log_func_data,
								"sond_process_fileparts: Struktur '%s': %s",
								file_part, error_struct ? error_struct->message : "?");
					g_clear_error(&error_struct);
				}
			}
		}

		/* Coverage-Hochprüfen (Coalescing mit den Geschwistern): das
		 * eigentliche Markieren der Datei als vollständig abgedeckt
		 * passiert bereits zuverlässig in sond_index() selbst (dort ist
		 * die wahre Gesamtseitenzahl bekannt - hier nur "nicht
		 * abgebrochen" zu prüfen wäre nicht sicher genug, s. dortiger
		 * Kommentar). Hier deshalb nur per coverage_get() nachsehen, ob
		 * das gerade tatsächlich passiert ist, und wenn ja, nach oben
		 * weiterprüfen, ob jetzt auch das Elternverzeichnis komplett ist.
		 * Auch für gmessage_header_only jetzt zulässig (Schritt 4,
		 * 17.09.2026, s. ToDo.c): sond_index_ctx_coverage_try_collapse()
		 * ist GMessage-bewusst geworden und erkennt "file_part//header"
		 * als E-Mail-internes Kind (container_entrycount statt
		 * sond_dir_open()) - keine Berührung mehr mit dem alten
		 * "//"-Ahnen-Walk-Bug.
		 * Bei pages_key vom Seiten-Eintrag "x.pdf//" aus: try_collapse() fasst
		 * Seiten und Anhänge (pdf_embedded) zu "x.pdf" zusammen bzw. geht
		 * gleich von "x.pdf" weiter, wenn das schon abgedeckt ist (ToDo.c
		 * #199). */
		collapse_key = coverage_key;

		if (!g_atomic_int_get(&wctx->cancel) &&
				wctx->index_ctx && wctx->project_dir &&
				sond_index_ctx_coverage_get(wctx->index_ctx, collapse_key)
						>= wctx->ocr_mode) {
			GError *coverage_error = NULL;

			if (!sond_index_ctx_coverage_try_collapse(wctx->index_ctx,
					collapse_key, wctx->project_dir,
					&coverage_error)) {
				if (wctx->log_func)
					wctx->log_func(wctx->log_func_data,
							"sond_process_fileparts: coverage_try_collapse '%s': %s",
							collapse_key,
							coverage_error ? coverage_error->message : "?");
				g_clear_error(&coverage_error);
			}
		}

		if (coverage_key != file_part) g_free(coverage_key);
		g_free(file_part);
	}

	return;
}

SondProcessFileCtx* sond_process_file_create_wctx(fz_context* ctx,
		void (*log_func)(void*, gchar const*, ...), gpointer log_func_data,
		gchar const* tessdata_path, gint num_ocr_threads,
		gchar const* index_db_filename, gchar const* embedding_model_path,
		gchar const* project_dir, GError **error) {

	SondProcessFileCtx* wctx = g_new0(SondProcessFileCtx, 1);

	/* fz_context */
	wctx->ctx = ctx;

	wctx->progress = 0;
	wctx->cancel = 0;
	wctx->ocr_mode = SOND_OCR_MODE_CHECK; /* bisheriges Standardverhalten */

	wctx->log_func = log_func;
	wctx->log_func_data = (gpointer) log_func_data;

	wctx->project_dir = g_strdup(project_dir);

	wctx->ocr_pool = sond_ocr_pool_new(tessdata_path, "deu",
			num_ocr_threads, &wctx->cancel, &wctx->progress, error);
	if (!wctx->ocr_pool) {
		g_free(wctx->project_dir);
		g_free(wctx);

		return NULL;
	}

	wctx->index_ctx = sond_index_ctx_new(index_db_filename,
			embedding_model_path, 0, 0, error);
	if (!wctx->index_ctx) {
		sond_ocr_pool_free(wctx->ocr_pool);
		g_free(wctx->project_dir);
		g_free(wctx);

		return NULL;
	}

	return wctx;
}

void sond_process_file_destroy_wctx(SondProcessFileCtx *wctx) {
	if (wctx->index_ctx)
		sond_index_ctx_free(wctx->index_ctx);
	if (wctx->ocr_pool)
		sond_ocr_pool_free(wctx->ocr_pool);
	g_free(wctx->project_dir);

	g_free(wctx);

	return;
}

