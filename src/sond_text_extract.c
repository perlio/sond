/*
 sond (sond_text_extract.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_text_extract.h"

#include <string.h>
#include <stdarg.h>

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <gmime/gmime.h>
#include <lexbor/html/html.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <zip.h>

#include "sond_log_and_error.h"
#include "sond_gmessage_helper.h"

/* =========================================================================
 * SondTextSegment
 * ======================================================================= */

SondTextSegment* sond_text_segment_new(gchar *text, gint page_nr, gint char_pos) {
    SondTextSegment *seg = g_new0(SondTextSegment, 1);
    seg->text     = text;
    seg->page_nr  = page_nr;
    seg->char_pos = char_pos;
    return seg;
}

void sond_text_segment_free(gpointer p) {
    SondTextSegment *seg = (SondTextSegment*) p;
    if (!seg) return;
    g_free(seg->text);
    g_free(seg);
}

static void log_warn(SondLogFunc log_func, gpointer log_data, gchar const *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (log_func) {
        gchar *msg = g_strdup_vprintf(fmt, args);
        log_func(log_data, "%s", msg);
        g_free(msg);
    } else {
        gchar *msg = g_strdup_vprintf(fmt, args);
        LOG_WARN("%s", msg);
        g_free(msg);
    }
    va_end(args);
}

/* =========================================================================
 * PDF
 * ======================================================================= */

GPtrArray* sond_text_extract_pdf(fz_context *ctx, guchar const *buf, gsize size,
        SondLogFunc log_func, gpointer log_data) {
    GPtrArray *segs = g_ptr_array_new_with_free_func(
            (GDestroyNotify) sond_text_segment_free);

    pdf_document *doc = NULL;
    gint char_pos_total = 0;

    fz_try(ctx) {
        fz_stream *stream = fz_open_memory(ctx, (guchar*) buf, size);
        doc = pdf_open_document_with_stream(ctx, stream);
        fz_drop_stream(ctx, stream);

        gint n_pages = pdf_count_pages(ctx, doc);
        for (gint i = 0; i < n_pages; i++) {
            fz_stext_options opts = { 0 };
            fz_stext_page *stext = fz_new_stext_page_from_page_number(
                    ctx, (fz_document*) doc, i, &opts);

            /* Denselben Flat-Text wie fz_search_stext_page intern verwendet:
             * FZ_TEXT_FLATTEN_ALL -> alle Lücken als einzelnes Leerzeichen,
             * keine Zeilenumbrüche. map wird nicht benötigt (NULL). */
            fz_buffer *fzbuf = fz_new_buffer_from_flattened_stext_page(
                    ctx, stext, FZ_TEXT_FLATTEN_ALL, NULL);
            fz_drop_stext_page(ctx, stext);

            gsize   len  = 0;
            guchar *data = NULL;
            fz_buffer_extract(ctx, fzbuf, &data); /* transfer ownership */
            fz_drop_buffer(ctx, fzbuf);
            len = (data) ? strlen((gchar*) data) : 0;

            if (len > 0) {
                gint page_char_pos = char_pos_total;
                char_pos_total += (gint) len;
                g_ptr_array_add(segs,
                        sond_text_segment_new((gchar*) data, i, page_char_pos));
            } else {
                g_free(data);
            }
        }
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        log_warn(log_func, log_data,
                "sond_text_extract_pdf: %s", fz_caught_message(ctx));
    }

    return segs;
}

/* =========================================================================
 * HTML
 * ======================================================================= */

static void extract_text_from_html_node(lxb_dom_node_t *node, GString *text) {
    if (!node) return;

    if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
        size_t len;
        const lxb_char_t *content = lxb_dom_node_text_content(node, &len);
        if (content && len > 0)
            g_string_append_len(text, (const char*) content, len);
    }

    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        lxb_dom_element_t *elem = lxb_dom_interface_element(node);
        size_t tag_len;
        const lxb_char_t *tag = lxb_dom_element_qualified_name(elem, &tag_len);

        if (tag_len == 2 && memcmp(tag, "br", 2) == 0) {
            g_string_append(text, "\n");
        } else if (tag_len == 1 && memcmp(tag, "p", 1) == 0) {
            if (text->len > 0 && text->str[text->len - 1] != '\n')
                g_string_append(text, "\n\n");
        } else if ((tag_len == 2 && (memcmp(tag, "h1", 2) == 0 ||
                                     memcmp(tag, "h2", 2) == 0 ||
                                     memcmp(tag, "h3", 2) == 0 ||
                                     memcmp(tag, "h4", 2) == 0 ||
                                     memcmp(tag, "h5", 2) == 0 ||
                                     memcmp(tag, "h6", 2) == 0)) ||
                   (tag_len == 3 && memcmp(tag, "div", 3) == 0) ||
                   (tag_len == 10 && memcmp(tag, "blockquote", 10) == 0)) {
            if (text->len > 0 && text->str[text->len - 1] != '\n')
                g_string_append(text, "\n");
        }
    }

    lxb_dom_node_t *child = node->first_child;
    while (child) {
        extract_text_from_html_node(child, text);
        child = child->next;
    }

    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        lxb_dom_element_t *elem = lxb_dom_interface_element(node);
        size_t tag_len;
        const lxb_char_t *tag = lxb_dom_element_qualified_name(elem, &tag_len);
        if ((tag_len == 1 && memcmp(tag, "p", 1) == 0) ||
            (tag_len == 2 && (memcmp(tag, "h1", 2) == 0 ||
                              memcmp(tag, "h2", 2) == 0 ||
                              memcmp(tag, "h3", 2) == 0)) ||
            (tag_len == 3 && memcmp(tag, "div", 3) == 0)) {
            if (text->len > 0 && text->str[text->len - 1] != '\n')
                g_string_append(text, "\n");
        }
    }
}

/* Wandelt HTML-Rohbytes nach UTF-8 um:
 *  1. Ist der Puffer bereits gültiges UTF-8 -> direkt verwenden.
 *  2. Andernfalls: charset=... im <meta>-Tag suchen und mit g_convert
 *     umwandeln. Wird nichts gefunden, Windows-1252 als Fallback
 *     (deckt Latin-1 / CP1252 ab). */
static gchar* html_bytes_to_utf8(guchar const *buf, gsize size) {
    if (g_utf8_validate((const gchar*) buf, (gssize) size, NULL))
        return g_strndup((const gchar*) buf, size);

    gchar *detected_charset = NULL;
    gsize  probe_len = size < 1024 ? size : 1024;
    gchar *probe = g_strndup((const gchar*) buf, probe_len);
    gchar *probe_lower = g_ascii_strdown(probe, -1);
    g_free(probe);

    const gchar *cs_pos = strstr(probe_lower, "charset=");
    if (cs_pos) {
        cs_pos += strlen("charset=");
        while (*cs_pos == '"' || *cs_pos == '\'' || *cs_pos == ' ')
            cs_pos++;
        gchar charset_buf[64] = { 0 };
        gsize ci = 0;
        while (ci < sizeof(charset_buf) - 1 && *cs_pos &&
               *cs_pos != '"' && *cs_pos != '\'' &&
               *cs_pos != ' ' && *cs_pos != ';' && *cs_pos != '>')
            charset_buf[ci++] = *cs_pos++;
        if (ci > 0)
            detected_charset = g_strdup(charset_buf);
    }
    g_free(probe_lower);

    const gchar *from_charset = detected_charset ? detected_charset : "windows-1252";

    GError *conv_err = NULL;
    gchar *html = g_convert((const gchar*) buf, (gssize) size,
            "UTF-8", from_charset, NULL, NULL, &conv_err);
    if (!html) {
        g_clear_error(&conv_err);
        html = g_utf8_make_valid((const gchar*) buf, (gssize) size);
    }
    g_free(detected_charset);
    return html;
}

GPtrArray* sond_text_extract_html(guchar const *buf, gsize size) {
    GPtrArray *segs = g_ptr_array_new_with_free_func(
            (GDestroyNotify) sond_text_segment_free);
    if (!buf || size == 0) return segs;

    gchar *html = html_bytes_to_utf8(buf, size);
    if (!html) return segs;

    lxb_html_document_t *doc = lxb_html_document_create();
    if (!doc) { g_free(html); return segs; }

    lxb_status_t status = lxb_html_document_parse(
            doc, (const lxb_char_t*) html, strlen(html));
    g_free(html);

    if (status != LXB_STATUS_OK) {
        lxb_html_document_destroy(doc);
        return segs;
    }

    GString *text = g_string_new(NULL);
    lxb_dom_node_t *body = lxb_dom_interface_node(doc->body);
    if (body)
        extract_text_from_html_node(body, text);
    lxb_html_document_destroy(doc);

    if (text->len > 0)
        g_ptr_array_add(segs, sond_text_segment_new(
                g_string_free(text, FALSE), -1, 0));
    else
        g_string_free(text, TRUE);

    return segs;
}

/* =========================================================================
 * Plain Text
 * ======================================================================= */

/* Wandelt Rohbytes nach UTF-8 um (wie html_bytes_to_utf8, aber ohne
 * Meta-Charset-Suche - reiner Text hat keine solche Deklaration) und
 * normalisiert CRLF -> LF. */
GPtrArray* sond_text_extract_plain(guchar const *buf, gsize size) {
    GPtrArray *segs = g_ptr_array_new_with_free_func(
            (GDestroyNotify) sond_text_segment_free);
    if (!buf || size == 0) return segs;

    gchar *safe_text = NULL;
    if (g_utf8_validate((const gchar*) buf, (gssize) size, NULL)) {
        safe_text = g_strndup((const gchar*) buf, size);
    } else {
        GError *conv_err = NULL;
        safe_text = g_convert((const gchar*) buf, (gssize) size,
                "UTF-8", "windows-1252", NULL, NULL, &conv_err);
        if (!safe_text) {
            g_clear_error(&conv_err);
            safe_text = g_utf8_make_valid((const gchar*) buf, (gssize) size);
        }
    }
    if (!safe_text) return segs;

    GString *text = g_string_new(NULL);
    const gchar *p = safe_text;
    while (*p) {
        if (*p == '\r' && *(p + 1) == '\n') {
            g_string_append_c(text, '\n');
            p += 2;
        } else {
            g_string_append_c(text, *p++);
        }
    }
    g_free(safe_text);

    if (text->len > 0)
        g_ptr_array_add(segs, sond_text_segment_new(
                g_string_free(text, FALSE), -1, 0));
    else
        g_string_free(text, TRUE);

    return segs;
}

/* =========================================================================
 * E-Mail (message/rfc822)
 * ======================================================================= */

typedef struct {
    gchar  *mime_type;
    gchar  *filename;
    gchar  *text;        /* NULL bei Bild-Parts */
    guchar *image_data;  /* NULL bei Text-Parts */
    gsize   image_len;
} SondEmlPart;

static void sond_eml_part_free(gpointer p) {
    SondEmlPart *ep = (SondEmlPart*) p;
    if (!ep) return;
    g_free(ep->mime_type);
    g_free(ep->filename);
    g_free(ep->text);
    g_free(ep->image_data);
    g_free(ep);
}

void sond_eml_image_free(gpointer p) {
    SondEmlImage *img = (SondEmlImage*) p;
    if (!img) return;
    g_free(img->mime_type);
    g_free(img->filename);
    g_free(img->image_data);
    g_free(img);
}

static gchar* extract_text_from_gmime_part(GMimeObject *part) {
    if (!GMIME_IS_PART(part))
        return NULL;

    GMimeContentDisposition *disp = g_mime_object_get_content_disposition(part);
    if (disp) {
        const char *dval = g_mime_content_disposition_get_disposition(disp);
        if (dval && g_ascii_strcasecmp(dval, "attachment") == 0)
            return NULL;
    }

    GMimeContentType *ct = g_mime_object_get_content_type(part);
    if (!ct) return NULL;
    const char *main_type = g_mime_content_type_get_media_type(ct);
    if (!main_type || g_ascii_strcasecmp(main_type, "text") != 0)
        return NULL;

    GMimeDataWrapper *wrapper = g_mime_part_get_content(GMIME_PART(part));
    if (!wrapper)
        return NULL;

    GMimeStream *mem_stream = g_mime_stream_mem_new();
    g_mime_data_wrapper_write_to_stream(wrapper, mem_stream);
    g_mime_stream_flush(mem_stream);

    GByteArray *ba = g_mime_stream_mem_get_byte_array(GMIME_STREAM_MEM(mem_stream));
    gchar *text = g_strndup((const gchar*) ba->data, ba->len);
    g_object_unref(mem_stream);

    if (text && !g_utf8_validate(text, -1, NULL)) {
        const char *charset = g_mime_content_type_get_parameter(ct, "charset");
        if (!charset) charset = "windows-1252";
        GError *conv_err = NULL;
        gchar *utf8 = g_convert(text, -1, "UTF-8", charset, NULL, NULL, &conv_err);
        g_free(text);
        if (!utf8) {
            g_clear_error(&conv_err);
            utf8 = g_strdup("(Encoding-Fehler beim Lesen des Textteils)");
        }
        text = utf8;
    }

    return text;
}

static void collect_gmessage_parts(GMimeObject *obj, GPtrArray *parts) {
    if (GMIME_IS_MULTIPART(obj)) {
        int count = g_mime_multipart_get_count(GMIME_MULTIPART(obj));
        for (int i = 0; i < count; i++)
            collect_gmessage_parts(
                    g_mime_multipart_get_part(GMIME_MULTIPART(obj), i), parts);
    } else if (GMIME_IS_MESSAGE_PART(obj)) {
        GMimeMessage *inner = g_mime_message_part_get_message(
                GMIME_MESSAGE_PART(obj));
        if (inner)
            collect_gmessage_parts(g_mime_message_get_mime_part(inner), parts);
    } else if (GMIME_IS_PART(obj)) {
        GMimeContentType *ct = g_mime_object_get_content_type(obj);
        if (!ct) return;
        const char *main_type = g_mime_content_type_get_media_type(ct);
        const char *sub_type  = g_mime_content_type_get_media_subtype(ct);
        if (!main_type) return;

        GMimeContentDisposition *disp = g_mime_object_get_content_disposition(obj);
        if (disp) {
            const char *dval = g_mime_content_disposition_get_disposition(disp);
            if (dval && g_ascii_strcasecmp(dval, "attachment") == 0)
                return;
        }

        SondEmlPart *ep = g_new0(SondEmlPart, 1);
        ep->mime_type = (sub_type)
                ? g_strdup_printf("%s/%s", main_type, sub_type)
                : g_strdup(main_type);

        const char *fn = g_mime_object_get_content_disposition_parameter(obj, "filename");
        if (!fn) fn = g_mime_content_type_get_parameter(ct, "name");
        if (fn) ep->filename = g_strdup(fn);

        if (g_ascii_strcasecmp(main_type, "text") == 0) {
            ep->text = extract_text_from_gmime_part(obj);
            if (!ep->text) {
                sond_eml_part_free(ep);
                return;
            }
        } else if (g_ascii_strcasecmp(main_type, "image") == 0) {
            GMimeDataWrapper *wrapper = g_mime_part_get_content(GMIME_PART(obj));
            if (!wrapper) { sond_eml_part_free(ep); return; }
            GMimeStream *mem = g_mime_stream_mem_new();
            g_mime_data_wrapper_write_to_stream(wrapper, mem);
            g_mime_stream_flush(mem);
            GByteArray *ba = g_mime_stream_mem_get_byte_array(GMIME_STREAM_MEM(mem));
            ep->image_data = (guchar*) g_memdup2(ba->data, ba->len);
            ep->image_len  = ba->len;
            g_object_unref(mem);
        } else {
            sond_eml_part_free(ep);
            return;
        }

        g_ptr_array_add(parts, ep);
    }
}

/* Baut Header + Body als einen zusammenhängenden Text auf. Wird
 * IDENTISCH von der Indizierung und vom Renderer verwendet. */
static gchar* build_gmessage_text(GMimeMessage *message) {
    GString *text = g_string_new(NULL);

    InternetAddressList *from_list = g_mime_message_get_from(message);
    if (from_list) {
        gchar *s = internet_address_list_to_string(from_list, NULL, TRUE);
        g_string_append_printf(text, "Von:     %s\n", s ? s : "");
        g_free(s);
    }

    InternetAddressList *to_list = g_mime_message_get_to(message);
    if (to_list) {
        gchar *s = internet_address_list_to_string(to_list, NULL, TRUE);
        g_string_append_printf(text, "An:      %s\n", s ? s : "");
        g_free(s);
    }

    InternetAddressList *cc_list = g_mime_message_get_cc(message);
    if (cc_list && internet_address_list_length(cc_list) > 0) {
        gchar *s = internet_address_list_to_string(cc_list, NULL, TRUE);
        g_string_append_printf(text, "CC:      %s\n", s ? s : "");
        g_free(s);
    }

    InternetAddressList *bcc_list = g_mime_message_get_bcc(message);
    if (bcc_list && internet_address_list_length(bcc_list) > 0) {
        gchar *s = internet_address_list_to_string(bcc_list, NULL, TRUE);
        g_string_append_printf(text, "BCC:     %s\n", s ? s : "");
        g_free(s);
    }

    const gchar *subject = g_mime_message_get_subject(message);
    g_string_append_printf(text, "Betreff: %s\n", subject ? subject : "");

    GDateTime *date = g_mime_message_get_date(message);
    if (date) {
        gchar *date_str = g_date_time_format(date, "%d.%m.%Y %H:%M:%S %Z");
        g_string_append_printf(text, "Datum:   %s\n", date_str ? date_str : "");
        g_free(date_str);
        g_date_time_unref(date);
    }

    g_string_append(text, "\n"
            "────────────────────────────────────────────────────────────"
            "\n\n");

    GPtrArray *parts = g_ptr_array_new_with_free_func(sond_eml_part_free);
    GMimeObject *mime_root = g_mime_message_get_mime_part(message);
    if (mime_root)
        collect_gmessage_parts(mime_root, parts);

    for (guint i = 0; i < parts->len; i++) {
        SondEmlPart *ep = g_ptr_array_index(parts, i);
        if (!ep->text) continue;

        if (ep->filename)
            g_string_append_printf(text, "[%s — %s]\n",
                    ep->mime_type ? ep->mime_type : "?", ep->filename);
        else
            g_string_append_printf(text, "[%s]\n",
                    ep->mime_type ? ep->mime_type : "?");

        g_string_append(text,
                "- - - - - - - - - - - - - - - - - - - - - - - - - - - -"
                "\n\n");

        gboolean is_html = ep->mime_type &&
                g_ascii_strcasecmp(ep->mime_type, "text/html") == 0;

        if (is_html) {
            GPtrArray *html_segs = sond_text_extract_html(
                    (guchar const*) ep->text, strlen(ep->text));
            if (html_segs->len > 0) {
                SondTextSegment *hs = g_ptr_array_index(html_segs, 0);
                g_string_append(text, hs->text);
            }
            g_ptr_array_unref(html_segs);
        } else {
            const gchar *p = ep->text;
            while (*p) {
                if (*p == '\r' && *(p + 1) == '\n') {
                    g_string_append_c(text, '\n');
                    p += 2;
                } else {
                    g_string_append_c(text, *p++);
                }
            }
        }

        gboolean has_more_text = FALSE;
        for (guint j = i + 1; j < parts->len; j++) {
            SondEmlPart *ep2 = g_ptr_array_index(parts, j);
            if (ep2->text) { has_more_text = TRUE; break; }
        }
        if (has_more_text)
            g_string_append(text,
                    "\n\n════════════════════════════════════════════"
                    "═══════════════════\n\n");
    }

    g_ptr_array_unref(parts);

    return g_string_free(text, FALSE);
}

GPtrArray* sond_text_extract_gmessage(guchar const *buf, gsize size) {
    GPtrArray *segs = g_ptr_array_new_with_free_func(
            (GDestroyNotify) sond_text_segment_free);
    if (!buf || size == 0) return segs;

    GMimeMessage *message = gmessage_open(buf, size);
    if (!message) return segs;

    gchar *text = build_gmessage_text(message);
    g_object_unref(message);

    if (!text || *text == '\0') {
        g_free(text);
        return segs;
    }

    g_ptr_array_add(segs, sond_text_segment_new(text, -1, 0));
    return segs;
}

GPtrArray* sond_text_extract_gmessage_images(guchar const *buf, gsize size) {
    GPtrArray *images = g_ptr_array_new_with_free_func(sond_eml_image_free);
    if (!buf || size == 0) return images;

    GMimeMessage *message = gmessage_open(buf, size);
    if (!message) return images;

    GPtrArray *parts = g_ptr_array_new_with_free_func(sond_eml_part_free);
    GMimeObject *mime_root = g_mime_message_get_mime_part(message);
    if (mime_root)
        collect_gmessage_parts(mime_root, parts);

    for (guint i = 0; i < parts->len; i++) {
        SondEmlPart *ep = g_ptr_array_index(parts, i);
        if (!ep->image_data) continue;

        SondEmlImage *img = g_new0(SondEmlImage, 1);
        img->mime_type  = g_strdup(ep->mime_type);
        img->filename   = g_strdup(ep->filename);
        img->image_data = g_memdup2(ep->image_data, ep->image_len);
        img->image_len  = ep->image_len;
        g_ptr_array_add(images, img);
    }

    g_ptr_array_unref(parts);
    g_object_unref(message);

    return images;
}

/* =========================================================================
 * ODT / DOCX
 * ======================================================================= */

static char* extract_from_zip(const unsigned char *zip_data, size_t zip_len,
        const char *filename, size_t *out_len) {
    zip_error_t error;
    zip_source_t *src;
    zip_t *archive;

    zip_error_init(&error);
    src = zip_source_buffer_create(zip_data, zip_len, 0, &error);
    if (!src) {
        LOG_WARN("extract_from_zip: zip_source_buffer_create: %s",
                zip_error_strerror(&error));
        zip_error_fini(&error);
        return NULL;
    }

    archive = zip_open_from_source(src, ZIP_RDONLY, &error);
    if (!archive) {
        LOG_WARN("extract_from_zip: zip_open_from_source: %s",
                zip_error_strerror(&error));
        zip_source_free(src);
        zip_error_fini(&error);
        return NULL;
    }

    struct zip_stat st;
    zip_stat_init(&st);
    if (zip_stat(archive, filename, 0, &st) != 0) {
        LOG_WARN("extract_from_zip: '%s' nicht im Archiv gefunden", filename);
        zip_close(archive);
        return NULL;
    }

    zip_file_t *file = zip_fopen(archive, filename, 0);
    if (!file) {
        LOG_WARN("extract_from_zip: '%s' konnte nicht geöffnet werden", filename);
        zip_close(archive);
        return NULL;
    }

    char *content = g_malloc(st.size + 1);
    zip_int64_t bytes_read = zip_fread(file, content, st.size);

    if (bytes_read < 0) {
        LOG_WARN("extract_from_zip: Lesefehler bei '%s'", filename);
        g_free(content);
        zip_fclose(file);
        zip_close(archive);
        return NULL;
    }

    content[bytes_read] = '\0';
    if (out_len) *out_len = bytes_read;

    zip_fclose(file);
    zip_close(archive);

    return content;
}

static void process_docx_node(xmlNode *node, GString *text,
        PangoAttrList **attr_list, int *char_offset) {
    if (!node) return;

    for (xmlNode *cur = node; cur; cur = cur->next) {
        if (cur->type == XML_TEXT_NODE) {
            if (cur->content) {
                const char *content = (const char*) cur->content;
                g_string_append(text, content);
                *char_offset += strlen(content);
            }
        } else if (cur->type == XML_ELEMENT_NODE) {
            const char *name = (const char*) cur->name;

            if (strcmp(name, "t") == 0) {
                if (cur->children && cur->children->content) {
                    const char *content = (const char*) cur->children->content;
                    g_string_append(text, content);
                    *char_offset += strlen(content);
                }
                continue;
            }

            if (strcmp(name, "p") == 0) {
                process_docx_node(cur->children, text, attr_list, char_offset);
                g_string_append(text, "\n\n");
                *char_offset += 2;
                continue;
            }

            if (strcmp(name, "br") == 0) {
                g_string_append(text, "\n");
                (*char_offset)++;
                continue;
            }

            if (strcmp(name, "tab") == 0) {
                g_string_append(text, "    ");
                *char_offset += 4;
                continue;
            }

            if (strcmp(name, "r") == 0) {
                int run_start = *char_offset;
                gboolean is_bold = FALSE;

                for (xmlNode *child = cur->children; child; child = child->next) {
                    if (child->type == XML_ELEMENT_NODE &&
                            strcmp((char*) child->name, "rPr") == 0) {
                        for (xmlNode *prop = child->children; prop; prop = prop->next) {
                            if (prop->type == XML_ELEMENT_NODE &&
                                    strcmp((char*) prop->name, "b") == 0) {
                                is_bold = TRUE;
                                break;
                            }
                        }
                    }
                }

                process_docx_node(cur->children, text, attr_list, char_offset);

                if (is_bold && attr_list && *attr_list) {
                    PangoAttribute *attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
                    attr->start_index = run_start;
                    attr->end_index   = *char_offset;
                    pango_attr_list_insert(*attr_list, attr);
                }
                continue;
            }

            process_docx_node(cur->children, text, attr_list, char_offset);
        }
    }
}

static void process_odt_node(xmlNode *node, GString *text,
        PangoAttrList **attr_list, int *char_offset) {
    if (!node) return;

    for (xmlNode *cur = node; cur; cur = cur->next) {
        if (cur->type == XML_TEXT_NODE) {
            if (cur->content) {
                const char *content = (const char*) cur->content;
                g_string_append(text, content);
                *char_offset += strlen(content);
            }
        } else if (cur->type == XML_ELEMENT_NODE) {
            const char *name = (const char*) cur->name;

            if (strcmp(name, "h") == 0) {
                g_string_append(text, "\n");
                (*char_offset)++;

                int heading_start = *char_offset;
                process_odt_node(cur->children, text, attr_list, char_offset);

                if (attr_list && *attr_list) {
                    PangoAttribute *attr;

                    attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
                    attr->start_index = heading_start;
                    attr->end_index   = *char_offset;
                    pango_attr_list_insert(*attr_list, attr);

                    attr = pango_attr_scale_new(1.5);
                    attr->start_index = heading_start;
                    attr->end_index   = *char_offset;
                    pango_attr_list_insert(*attr_list, attr);
                }

                g_string_append(text, "\n\n");
                *char_offset += 2;
                continue;
            }

            if (strcmp(name, "p") == 0) {
                process_odt_node(cur->children, text, attr_list, char_offset);
                g_string_append(text, "\n\n");
                *char_offset += 2;
                continue;
            }

            if (strcmp(name, "list-item") == 0) {
                g_string_append(text, "• ");
                *char_offset += 4;
                process_odt_node(cur->children, text, attr_list, char_offset);
                continue;
            }

            if (strcmp(name, "table-cell") == 0) {
                process_odt_node(cur->children, text, attr_list, char_offset);
                g_string_append(text, " | ");
                *char_offset += 3;
                continue;
            }

            if (strcmp(name, "table-row") == 0) {
                process_odt_node(cur->children, text, attr_list, char_offset);
                g_string_append(text, "\n");
                (*char_offset)++;
                continue;
            }

            if (strcmp(name, "span") == 0) {
                xmlChar *style = xmlGetProp(cur, (xmlChar*) "style-name");
                int span_start = *char_offset;

                process_odt_node(cur->children, text, attr_list, char_offset);

                if (style && (strstr((char*) style, "Bold") || strstr((char*) style, "bold"))) {
                    if (attr_list && *attr_list) {
                        PangoAttribute *attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
                        attr->start_index = span_start;
                        attr->end_index   = *char_offset;
                        pango_attr_list_insert(*attr_list, attr);
                    }
                }

                if (style) xmlFree(style);
                continue;
            }

            if (strcmp(name, "line-break") == 0) {
                g_string_append(text, "\n");
                (*char_offset)++;
                continue;
            }

            if (strcmp(name, "tab") == 0) {
                g_string_append(text, "    ");
                *char_offset += 4;
                continue;
            }

            process_odt_node(cur->children, text, attr_list, char_offset);
        }
    }
}

GPtrArray* sond_text_extract_docx(guchar const *buf, gsize size, PangoAttrList **out_attrs) {
    GPtrArray *segs = g_ptr_array_new_with_free_func(
            (GDestroyNotify) sond_text_segment_free);
    if (out_attrs) *out_attrs = NULL;
    if (!buf || size == 0) return segs;

    size_t xml_len = 0;
    char *xml_content = extract_from_zip(buf, size, "word/document.xml", &xml_len);
    if (!xml_content) return segs;

    xmlDoc *doc = xmlReadMemory(xml_content, (int) xml_len,
            "word/document.xml", NULL, 0);
    g_free(xml_content);
    if (!doc) return segs;

    xmlNode *root = xmlDocGetRootElement(doc);
    if (!root) { xmlFreeDoc(doc); return segs; }

    GString *text = g_string_new(NULL);
    PangoAttrList *attr_list = out_attrs ? pango_attr_list_new() : NULL;
    int char_offset = 0;

    for (xmlNode *node = root->children; node; node = node->next) {
        if (node->type == XML_ELEMENT_NODE &&
                strcmp((char*) node->name, "body") == 0)
            process_docx_node(node->children, text,
                    out_attrs ? &attr_list : NULL, &char_offset);
    }
    xmlFreeDoc(doc);

    if (text->len == 0)
        g_string_append(text, "DOCX Document\n\n(No readable content found)");

    if (out_attrs) *out_attrs = attr_list;

    g_ptr_array_add(segs, sond_text_segment_new(
            g_string_free(text, FALSE), -1, 0));
    return segs;
}

GPtrArray* sond_text_extract_odt(guchar const *buf, gsize size, PangoAttrList **out_attrs) {
    GPtrArray *segs = g_ptr_array_new_with_free_func(
            (GDestroyNotify) sond_text_segment_free);
    if (out_attrs) *out_attrs = NULL;
    if (!buf || size == 0) return segs;

    size_t xml_len = 0;
    char *xml_content = extract_from_zip(buf, size, "content.xml", &xml_len);
    if (!xml_content) return segs;

    xmlDoc *doc = xmlReadMemory(xml_content, (int) xml_len,
            "content.xml", NULL, 0);
    g_free(xml_content);
    if (!doc) return segs;

    xmlNode *root = xmlDocGetRootElement(doc);
    if (!root) { xmlFreeDoc(doc); return segs; }

    GString *text = g_string_new(NULL);
    PangoAttrList *attr_list = out_attrs ? pango_attr_list_new() : NULL;
    int char_offset = 0;

    for (xmlNode *node = root->children; node; node = node->next) {
        if (node->type == XML_ELEMENT_NODE &&
                strcmp((char*) node->name, "body") == 0) {
            for (xmlNode *child = node->children; child; child = child->next) {
                if (child->type == XML_ELEMENT_NODE)
                    process_odt_node(child->children, text,
                            out_attrs ? &attr_list : NULL, &char_offset);
            }
        }
    }
    xmlFreeDoc(doc);

    if (text->len == 0)
        g_string_append(text, "ODT Document\n\n(No readable content found)");

    if (out_attrs) *out_attrs = attr_list;

    g_ptr_array_add(segs, sond_text_segment_new(
            g_string_free(text, FALSE), -1, 0));
    return segs;
}
