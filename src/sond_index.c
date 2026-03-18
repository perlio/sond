/*
 sond (sond_server_index.c) - Akten, Bewäisstücke, Unterlagen
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

#include "sond_index.h"

#include <glib.h>
#include <sqlite3.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <gmime/gmime.h>
#include <string.h>
#include <lexbor/html/html.h>

#include "sond_gmessage_helper.h"

#ifdef SOND_WITH_EMBEDDINGS
#include <llama.h>
#endif

#define INDEX_DEFAULT_CHUNK_SIZE    1000
#define INDEX_DEFAULT_CHUNK_OVERLAP  100
#define INDEX_DB_FILENAME           "/.sond_index.db"

/* =======================================================================
 * Datenbank-Schema
 * ======================================================================= */

static const gchar *SQL_CREATE_CHUNKS =
    "CREATE TABLE IF NOT EXISTS chunks ("
    "  id        INTEGER PRIMARY KEY,"
    "  filename  TEXT    NOT NULL,"
    "  chunk_idx INTEGER NOT NULL,"
    "  page_nr   INTEGER NOT NULL DEFAULT -1,"
    "  char_pos  INTEGER NOT NULL DEFAULT 0,"
    "  mime_type TEXT    NOT NULL,"
    "  text      TEXT    NOT NULL"
    ");";

static const gchar *SQL_CREATE_FTS =
    "CREATE VIRTUAL TABLE IF NOT EXISTS chunks_fts USING fts5("
    "  text,"
    "  content=chunks,"
    "  content_rowid=id"
    ");";

static const gchar *SQL_CREATE_VEC_TMPL =
    "CREATE VIRTUAL TABLE IF NOT EXISTS chunks_vec USING vec0("
    "  embedding FLOAT[%d]"
    ");";

static const gchar *SQL_TRIGGER_INSERT =
    "CREATE TRIGGER IF NOT EXISTS chunks_ai AFTER INSERT ON chunks BEGIN"
    "  INSERT INTO chunks_fts(rowid, text) VALUES (new.id, new.text);"
    "END;";

static const gchar *SQL_TRIGGER_DELETE =
    "CREATE TRIGGER IF NOT EXISTS chunks_ad AFTER DELETE ON chunks BEGIN"
    "  INSERT INTO chunks_fts(chunks_fts, rowid, text)"
    "    VALUES('delete', old.id, old.text);"
    "END;";

static const gchar *SQL_CREATE_FILES =
    "CREATE TABLE IF NOT EXISTS files ("
    "  filename  TEXT    PRIMARY KEY"
    ");"; /* Präsenzliste indizierter leafs */

/* =======================================================================
 * Schema initialisieren
 * ======================================================================= */

static gboolean db_init_schema(SondIndexCtx *ctx, GError **error) {
    char *errmsg = NULL;
    gint  rc     = 0;

    rc = sqlite3_exec(ctx->db, SQL_CREATE_CHUNKS, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "db_init_schema: CREATE chunks: %s", errmsg);
        sqlite3_free(errmsg);
        return FALSE;
    }

    rc = sqlite3_exec(ctx->db, SQL_CREATE_FTS, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "db_init_schema: CREATE chunks_fts: %s", errmsg);
        sqlite3_free(errmsg);
        return FALSE;
    }

    rc = sqlite3_exec(ctx->db, SQL_TRIGGER_INSERT, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "db_init_schema: CREATE trigger insert: %s", errmsg);
        sqlite3_free(errmsg);
        return FALSE;
    }

    rc = sqlite3_exec(ctx->db, SQL_TRIGGER_DELETE, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "db_init_schema: CREATE trigger delete: %s", errmsg);
        sqlite3_free(errmsg);
        return FALSE;
    }

    rc = sqlite3_exec(ctx->db, SQL_CREATE_FILES, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "db_init_schema: CREATE files: %s", errmsg);
        sqlite3_free(errmsg);
        return FALSE;
    }

#ifdef SOND_WITH_EMBEDDINGS
    if (ctx->n_embd > 0) {
        gchar *sql_vec = g_strdup_printf(SQL_CREATE_VEC_TMPL, ctx->n_embd);
        rc = sqlite3_exec(ctx->db, sql_vec, NULL, NULL, &errmsg);
        g_free(sql_vec);
        if (rc != SQLITE_OK) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "db_init_schema: CREATE chunks_vec: %s", errmsg);
            sqlite3_free(errmsg);
            return FALSE;
        }
    }
#endif

    return TRUE;
}

/* =======================================================================
 * sond_index_ctx_new / _free
 * ======================================================================= */

SondIndexCtx* sond_index_ctx_new(gchar const *db_path,
                                  gchar const *model_path,
                                  gint         chunk_size,
                                  gint         chunk_overlap,
                                  GError     **error) {
    SondIndexCtx *ctx = g_new0(SondIndexCtx, 1);

    ctx->db_path       = g_strdup(db_path);
    ctx->chunk_size    = (chunk_size    > 0) ? chunk_size    : INDEX_DEFAULT_CHUNK_SIZE;
    ctx->chunk_overlap = (chunk_overlap > 0) ? chunk_overlap : INDEX_DEFAULT_CHUNK_OVERLAP;

    gint rc = sqlite3_open(db_path, &ctx->db);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "sond_index_ctx_new: sqlite3_open '%s': %s",
                    db_path, sqlite3_errmsg(ctx->db));
        sond_index_ctx_free(ctx);
        return NULL;
    }

    sqlite3_exec(ctx->db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    sqlite3_exec(ctx->db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);

#ifdef SOND_WITH_EMBEDDINGS
    if (model_path) {
        llama_backend_init();

        struct llama_model_params model_params = llama_model_default_params();
        model_params.n_gpu_layers = 0;

        ctx->llama_model = llama_model_load_from_file(model_path, model_params);
        if (!ctx->llama_model) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "sond_index_ctx_new: llama_model_load_from_file '%s' fehlgeschlagen",
                        model_path);
            sond_index_ctx_free(ctx);
            return NULL;
        }

        struct llama_context_params ctx_params = llama_context_default_params();
        ctx_params.embeddings = TRUE;
        ctx_params.n_ctx      = 512;
        ctx_params.n_batch    = 512;

        ctx->llama_ctx = llama_init_from_model(
                (struct llama_model*)ctx->llama_model, ctx_params);
        if (!ctx->llama_ctx) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "sond_index_ctx_new: llama_init_from_model fehlgeschlagen");
            sond_index_ctx_free(ctx);
            return NULL;
        }

        ctx->n_embd = llama_model_n_embd(
                (struct llama_model*)ctx->llama_model);
    }
#endif

    if (!db_init_schema(ctx, error)) {
        sond_index_ctx_free(ctx);
        return NULL;
    }

    return ctx;
}

void sond_index_ctx_free(SondIndexCtx *ctx) {
    if (!ctx) return;

#ifdef SOND_WITH_EMBEDDINGS
    if (ctx->llama_ctx)
        llama_free((struct llama_context*)ctx->llama_ctx);

    if (ctx->llama_model)
        llama_model_free((struct llama_model*)ctx->llama_model);
#endif

    if (ctx->db)
        sqlite3_close(ctx->db);

    g_free(ctx->db_path);
    g_free(ctx);
}

/* =======================================================================
 * sond_index_ctx_clear_file
 * ======================================================================= */

gboolean sond_index_ctx_clear_file(SondIndexCtx *ctx,
                                    gchar const  *filename,
                                    GError      **error) {
    sqlite3_stmt *stmt    = NULL;
    gchar        *pattern = g_strdup_printf("%s//%%", filename);

    gint rc = sqlite3_prepare_v2(ctx->db,
            "DELETE FROM chunks WHERE filename = ? OR filename LIKE ?",
            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "sond_index_ctx_clear_file: prepare: %s",
                    sqlite3_errmsg(ctx->db));
        g_free(pattern);
        return FALSE;
    }

    sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, pattern,  -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "sond_index_ctx_clear_file: step: %s",
                    sqlite3_errmsg(ctx->db));
        return FALSE;
    }

#ifdef SOND_WITH_EMBEDDINGS
    if (ctx->n_embd > 0)
        sqlite3_exec(ctx->db,
                "DELETE FROM chunks_vec WHERE rowid NOT IN (SELECT id FROM chunks);",
                NULL, NULL, NULL);
#endif

    /* files-Einträge löschen */
    rc = sqlite3_prepare_v2(ctx->db,
            "DELETE FROM files WHERE filename = ? OR filename LIKE ?",
            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "sond_index_ctx_clear_file: prepare files: %s",
                    sqlite3_errmsg(ctx->db));
        g_free(pattern);
        return FALSE;
    }

    sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, pattern,  -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    g_free(pattern);

    if (rc != SQLITE_DONE) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "sond_index_ctx_clear_file: step files: %s",
                    sqlite3_errmsg(ctx->db));
        return FALSE;
    }

    return TRUE;
}

/* =======================================================================
 * sond_index_ctx_rename_file
 * ======================================================================= */

gboolean sond_index_ctx_rename_file(SondIndexCtx *ctx,
                                     gchar const  *prefix_old,
                                     gchar const  *prefix_new,
                                     GError      **error) {
    /* SQL:
     * UPDATE <table> SET filename =
     *   ?2 || SUBSTR(filename, LENGTH(?1) + 1)
     * WHERE filename = ?1 OR filename LIKE ?1 || '//%'
     *
     * Das ersetzt den Anfang (prefix_old) durch prefix_new,
     * der Rest (nach dem Präfix) bleibt unverandert.
     */
    const gchar *tables[] = { "chunks", "files" };
    gchar       *pattern  = g_strdup_printf("%s//%%", prefix_old);

    for (guint t = 0; t < G_N_ELEMENTS(tables); t++) {
        sqlite3_stmt *stmt = NULL;
        gchar *sql = g_strdup_printf(
                "UPDATE %s SET filename = ?2 || SUBSTR(filename, LENGTH(?1) + 1) "
                "WHERE filename = ?1 OR filename LIKE ?3",
                tables[t]);

        gint rc = sqlite3_prepare_v2(ctx->db, sql, -1, &stmt, NULL);
        g_free(sql);
        if (rc != SQLITE_OK) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "sond_index_ctx_rename_file: prepare %s: %s",
                        tables[t], sqlite3_errmsg(ctx->db));
            g_free(pattern);
            return FALSE;
        }

        sqlite3_bind_text(stmt, 1, prefix_old, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, prefix_new, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, pattern,    -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "sond_index_ctx_rename_file: step %s: %s",
                        tables[t], sqlite3_errmsg(ctx->db));
            g_free(pattern);
            return FALSE;
        }
    }

    g_free(pattern);
    return TRUE;
}

/* =======================================================================
 * Chunking
 * ======================================================================= */

typedef struct {
    gchar *text;
    gint   offset;   /* Byte-Offset dieses Chunks im Ursprungssegment */
} SondChunk;

static void sond_chunk_free(SondChunk *c) {
    if (!c) return;
    g_free(c->text);
    g_free(c);
}

static GPtrArray* text_to_chunks(gchar const *text, gint chunk_size, gint chunk_overlap) {
    GPtrArray *chunks = g_ptr_array_new_with_free_func((GDestroyNotify)sond_chunk_free);
    gint len  = (gint)strlen(text);
    gint step = chunk_size - chunk_overlap;

    if (step <= 0) step = chunk_size;

    for (gint pos = 0; pos < len; pos += step) {
        gint end = MIN(pos + chunk_size, len);
        SondChunk *c = g_new0(SondChunk, 1);
        c->text   = g_strndup(text + pos, end - pos);
        c->offset = pos;
        g_ptr_array_add(chunks, c);
        if (end == len) break;
    }

    return chunks;
}

/* =======================================================================
 * Embedding
 * ======================================================================= */

static gfloat* compute_embedding(SondIndexCtx *ctx, gchar const *text) {
#ifndef SOND_WITH_EMBEDDINGS
    (void)ctx; (void)text;
    return NULL;
#else
    if (!ctx->llama_ctx || !ctx->llama_model || ctx->n_embd <= 0)
        return NULL;

    struct llama_model   *model = (struct llama_model*)   ctx->llama_model;
    struct llama_context *lctx  = (struct llama_context*) ctx->llama_ctx;

    gint n_tokens_max = llama_n_ctx(lctx);
    llama_token *tokens = g_new(llama_token, n_tokens_max);

    gint n_tokens = llama_tokenize(model, text, (gint)strlen(text),
                                   tokens, n_tokens_max,
                                   TRUE, FALSE);
    if (n_tokens < 0 || n_tokens > n_tokens_max) {
        if (ctx->log_func)
            ctx->log_func(ctx->log_data,
                    "compute_embedding: Tokenisierung fehlgeschlagen (n=%d)", n_tokens);
        g_free(tokens);
        return NULL;
    }

    llama_batch batch = llama_batch_get_one(tokens, n_tokens);
    llama_kv_cache_clear(lctx);

    if (llama_decode(lctx, batch) != 0) {
        if (ctx->log_func)
            ctx->log_func(ctx->log_data, "compute_embedding: llama_decode fehlgeschlagen");
        g_free(tokens);
        return NULL;
    }
    g_free(tokens);

    gfloat const *embd = llama_get_embeddings(lctx);
    if (!embd) {
        if (ctx->log_func)
            ctx->log_func(ctx->log_data, "compute_embedding: llama_get_embeddings NULL");
        return NULL;
    }

    gfloat *result = g_new(gfloat, ctx->n_embd);
    memcpy(result, embd, ctx->n_embd * sizeof(gfloat));
    return result;
#endif
}

/* =======================================================================
 * Chunk in DB schreiben
 * ======================================================================= */

static gboolean db_insert_chunk(SondIndexCtx *sond_index_ctx,
								 void (*log_func)(gpointer, gchar const*, ...),
								 gpointer log_func_data,
                                 gchar const  *filename,
                                 gint          chunk_idx,
                                 gint          page_nr,
                                 gint          char_pos,
                                 gchar const  *mime_type,
                                 gchar const  *text,
                                 gfloat       *embedding) {
    sqlite3_stmt  *stmt  = NULL;
    gint           rc    = 0;

    rc = sqlite3_prepare_v2(sond_index_ctx->db,
            "INSERT INTO chunks(filename, chunk_idx, page_nr, char_pos, mime_type, text)"
            " VALUES(?,?,?,?,?,?)",
            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        if (log_func)
            log_func(log_func_data,
                    "db_insert_chunk: prepare: %s", sqlite3_errmsg(sond_index_ctx->db));
        return FALSE;
    }

    sqlite3_bind_text(stmt, 1, filename,  -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt, 2, chunk_idx);
    sqlite3_bind_int (stmt, 3, page_nr);
    sqlite3_bind_int (stmt, 4, char_pos);
    sqlite3_bind_text(stmt, 5, mime_type, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, text,      -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        if (log_func)
            log_func(log_func_data,
                    "db_insert_chunk: step: %s", sqlite3_errmsg(sond_index_ctx->db));
        return FALSE;
    }

#ifdef SOND_WITH_EMBEDDINGS
    sqlite3_int64  rowid = 0;
    rowid = sqlite3_last_insert_rowid(sond_index_ctx->db);

    if (embedding && sond_index_ctx->n_embd > 0) {
        rc = sqlite3_prepare_v2(sond_index_ctx->db,
                "INSERT INTO chunks_vec(rowid, embedding) VALUES(?,?)",
                -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            if (log_func)
                log_func(log_func_data,
                        "db_insert_chunk: prepare vec: %s", sqlite3_errmsg(sond_index_ctx->db));
            return FALSE;
        }

        sqlite3_bind_int64(stmt, 1, rowid);
        sqlite3_bind_blob (stmt, 2, embedding,
        		sond_index_ctx->n_embd * (gint)sizeof(gfloat), SQLITE_STATIC);

        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE && log_func)
            log_func(log_func_data,
                    "db_insert_chunk: step vec: %s", sqlite3_errmsg(sond_index_ctx->db));
    }
#endif

    return TRUE;
}

/* =======================================================================
 * Textsegment
 * ======================================================================= */

typedef struct {
    gchar *text;      /* Textinhalt des Segments                       */
    gint   page_nr;   /* PDF: Seitennummer (0-basiert); sonst -1        */
    gint   char_pos;  /* Zeichenposition im Gesamttext der Datei        */
} SondTextSegment;

static SondTextSegment* sond_text_segment_new(gchar *text, gint page_nr, gint char_pos) {
    SondTextSegment *seg = g_new0(SondTextSegment, 1);
    seg->text     = text;
    seg->page_nr  = page_nr;
    seg->char_pos = char_pos;
    return seg;
}

static void sond_text_segment_free(SondTextSegment *seg) {
    if (!seg) return;
    g_free(seg->text);
    g_free(seg);
}

/* =======================================================================
 * Textextraktion
 * ======================================================================= */

/* PDF: ein Segment pro Seite */
static GPtrArray* extract_segments_from_pdf(SondIndexCtx* sond_index_ctx, fz_context* ctx,
		guchar const *buf, gsize size, void (*log_func)(gpointer, gchar const*, ...),
		gpointer log_func_data) {
    GPtrArray  *segs = g_ptr_array_new_with_free_func(
            (GDestroyNotify)sond_text_segment_free);

    pdf_document *doc = NULL;
    gint char_pos_total = 0;

    fz_try(ctx) {
        fz_stream *stream = fz_open_memory(ctx, (guchar*)buf, size);
        doc = pdf_open_document_with_stream(ctx, stream);
        fz_drop_stream(ctx, stream);

        gint n_pages = pdf_count_pages(ctx, doc);
        for (gint i = 0; i < n_pages; i++) {
            fz_stext_options opts = { 0 };
            fz_stext_page *stext  = fz_new_stext_page_from_page_number(
                    ctx, (fz_document*) doc, i, &opts);

            /* Denselben Flat-Text wie fz_search_stext_page intern verwendet:
             * FZ_TEXT_FLATTEN_ALL → alle Lücken als einzelnes Leerzeichen,
             * keine Zeilenumbrüche. map wird nicht benötigt (NULL). */
            fz_buffer *buf = fz_new_buffer_from_flattened_stext_page(
                    ctx, stext, FZ_TEXT_FLATTEN_ALL, NULL);
            fz_drop_stext_page(ctx, stext);

            gsize   len      = 0;
            guchar *data     = NULL;
            fz_buffer_extract(ctx, buf, &data); /* transfer ownership */
            fz_drop_buffer(ctx, buf);
            len = (data) ? strlen((gchar*)data) : 0;

            if (len > 0) {
                gint page_char_pos = char_pos_total;
                char_pos_total += (gint)len;
                g_ptr_array_add(segs,
                        sond_text_segment_new(
                                (gchar*) data,
                                i, page_char_pos));
            } else {
                g_free(data);
            }
        }
    }
    fz_always(ctx) {
        if (doc) pdf_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        if (log_func)
            log_func(log_func_data,
                    "extract_segments_from_pdf: %s", fz_caught_message(ctx));
    }

    return segs;
}

/* E-Mail-Header: ein einzelnes Segment, char_pos=0, page_nr=-1 */
static GPtrArray* extract_segments_from_gmessage(guchar const *buf, gsize size) {
    GPtrArray    *segs = g_ptr_array_new_with_free_func(
            (GDestroyNotify)sond_text_segment_free);
    GString      *text = g_string_new(NULL);

    GMimeMessage *message = gmessage_open(buf, size);
    if (!message) {
        g_string_free(text, TRUE);
        return segs;
    }

    InternetAddressList *from = g_mime_message_get_addresses(message,
            GMIME_ADDRESS_TYPE_FROM);
    if (from) {
        gchar *s = internet_address_list_to_string(from, NULL, FALSE);
        if (s) { g_string_append(text, "Von: "); g_string_append(text, s); g_string_append_c(text, '\n'); }
        g_free(s);
    }

    const gchar *subject = g_mime_message_get_subject(message);
    if (subject) { g_string_append(text, "Betreff: "); g_string_append(text, subject); g_string_append_c(text, '\n'); }

    GDateTime *date = g_mime_message_get_date(message);
    if (date) {
        gchar *ds = g_date_time_format_iso8601(date);
        g_string_append(text, "Datum: ");
        g_string_append(text, ds);
        g_string_append_c(text, '\n');
        g_free(ds);
    }

    InternetAddressList *to = g_mime_message_get_addresses(message,
            GMIME_ADDRESS_TYPE_TO);
    if (to) {
        gchar *s = internet_address_list_to_string(to, NULL, FALSE);
        if (s) { g_string_append(text, "An: "); g_string_append(text, s); g_string_append_c(text, '\n'); }
        g_free(s);
    }

    g_object_unref(message);

    if (text->len > 0)
        g_ptr_array_add(segs, sond_text_segment_new(
                g_string_free(text, FALSE), -1, 0));
    else
        g_string_free(text, TRUE);

    return segs;
}

/* HTML: sichtbaren Text mit lexbor extrahieren (identisch zu extract_text_recursive im Renderer) */
static void extract_text_from_html_node(lxb_dom_node_t *node, GString *text) {
    if (!node) return;

    if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
        size_t len;
        const lxb_char_t *content = lxb_dom_node_text_content(node, &len);
        if (content && len > 0)
            g_string_append_len(text, (const char*)content, len);
    }

    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        lxb_dom_element_t *elem = lxb_dom_interface_element(node);
        size_t tag_len;
        const lxb_char_t *tag = lxb_dom_element_qualified_name(elem, &tag_len);

        if (tag_len == 2 && memcmp(tag, "br", 2) == 0) {
            g_string_append(text, "\n");
        } else if (tag_len == 1 && memcmp(tag, "p", 1) == 0) {
            if (text->len > 0 && text->str[text->len-1] != '\n')
                g_string_append(text, "\n\n");
        } else if ((tag_len == 2 && (memcmp(tag, "h1", 2) == 0 ||
                                     memcmp(tag, "h2", 2) == 0 ||
                                     memcmp(tag, "h3", 2) == 0 ||
                                     memcmp(tag, "h4", 2) == 0 ||
                                     memcmp(tag, "h5", 2) == 0 ||
                                     memcmp(tag, "h6", 2) == 0)) ||
                   (tag_len == 3 && memcmp(tag, "div", 3) == 0) ||
                   (tag_len == 10 && memcmp(tag, "blockquote", 10) == 0)) {
            if (text->len > 0 && text->str[text->len-1] != '\n')
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
            if (text->len > 0 && text->str[text->len-1] != '\n')
                g_string_append(text, "\n");
        }
    }
}

static GPtrArray* extract_segments_from_html(guchar const *buf, gsize size) {
    GPtrArray *segs = g_ptr_array_new_with_free_func(
            (GDestroyNotify)sond_text_segment_free);
    if (!buf || size == 0) return segs;

    /* Encoding-Konvertierung nach UTF-8 (identisch zum Renderer) */
    char *html = NULL;
    if (g_utf8_validate((const char*)buf, (gssize)size, NULL)) {
        html = g_strndup((const char*)buf, size);
    } else {
        GError *conv_err = NULL;
        html = g_convert((const char*)buf, (gssize)size,
                         "UTF-8", "windows-1252",
                         NULL, NULL, &conv_err);
        if (!html) {
            g_clear_error(&conv_err);
            html = g_utf8_make_valid((const char*)buf, (gssize)size);
        }
    }
    if (!html) return segs;

    lxb_html_document_t *doc = lxb_html_document_create();
    if (!doc) { g_free(html); return segs; }

    lxb_status_t status = lxb_html_document_parse(
            doc, (const lxb_char_t*)html, strlen(html));
    g_free(html);

    if (status != LXB_STATUS_OK) {
        lxb_html_document_destroy(doc);
        return segs;
    }

    GString *text = g_string_new("");
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

/* Rohtext: ein Segment, char_pos=0, page_nr=-1 */
static GPtrArray* extract_segments_from_text(guchar const *buf, gsize size) {
    GPtrArray *segs = g_ptr_array_new_with_free_func(
            (GDestroyNotify)sond_text_segment_free);
    if (size > 0)
        g_ptr_array_add(segs, sond_text_segment_new(
                g_strndup((gchar const*)buf, size), -1, 0));
    return segs;
}

/* =======================================================================
 * Volltextsuche
 * ======================================================================= */

void sond_index_hit_free(gpointer p) {
    SondIndexHit *hit = (SondIndexHit *) p;
    if (!hit) return;
    g_free(hit->filename);
    g_free(hit->snippet);
    g_free(hit);
}

/*
 * Baut den FTS5-Query-String:
 *   - Mehrere Wörter in term → Phrasensuche: "Wort1 Wort2"
 *   - Ein Wort                → einfaches Token: Wort
 *   - context vorhanden      → AND-Verknüpfung: <term> AND <context>
 *
 * Rückgabe: neu allozierter String, mit g_free() freigeben.
 */
static gchar* build_fts_query(gchar const *term, gchar const *context) {
    gchar *term_q   = NULL;
    gchar *query    = NULL;

    /* Phrase wenn term ein Leerzeichen enthält */
    if (strchr(term, ' '))
        term_q = g_strdup_printf("\"%s\"", term);
    else
        term_q = g_strdup(term);

    if (context && *context) {
        gchar *ctx_q = NULL;

        if (strchr(context, ' '))
            ctx_q = g_strdup_printf("\"%s\"", context);
        else
            ctx_q = g_strdup(context);

        query = g_strdup_printf("%s AND %s", term_q, ctx_q);
        g_free(ctx_q);
    } else {
        query = term_q;
        term_q = NULL; /* Eigentum übertragen */
    }

    g_free(term_q);
    return query;
}

GPtrArray* sond_index_search(SondIndexCtx *ctx,
                              gchar const  *term,
                              gchar const  *context,
                              GError      **error) {
    GPtrArray    *result = NULL;
    sqlite3_stmt *stmt   = NULL;
    gchar        *query  = NULL;
    gint          rc     = 0;

    g_return_val_if_fail(ctx    != NULL, NULL);
    g_return_val_if_fail(term   != NULL, NULL);
    g_return_val_if_fail(*term  != '\0', NULL);

    result = g_ptr_array_new_with_free_func(sond_index_hit_free);

    /* ---------------------------------------------------------------
     * 1. Volltextsuche über FTS5
     * ------------------------------------------------------------- */
    query = build_fts_query(term, context);

    /* highlight() markiert jeden Token-Treffer mit \x01...\x02.
     * Dadurch werden nur ganze Wörter gefunden (FTS5-Tokenisierung),
     * konsistent für alle Texttypen. */
    rc = sqlite3_prepare_v2(ctx->db,
        "SELECT c.filename, c.page_nr, c.char_pos,"
        "       c.char_pos - (SELECT MIN(c2.char_pos) FROM chunks c2"
        "                     WHERE c2.filename = c.filename"
        "                       AND c2.page_nr  = c.page_nr),"
        "       c.text,"
        "       highlight(chunks_fts, 0, '\x01', '\x02')"
        " FROM chunks_fts"
        " JOIN chunks c ON c.id = chunks_fts.rowid"
        " WHERE chunks_fts MATCH ?"
        " ORDER BY c.filename, c.page_nr, c.char_pos",
        -1, &stmt, NULL);

    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "sond_index_search: prepare FTS: %s",
                    sqlite3_errmsg(ctx->db));
        g_free(query);
        g_ptr_array_unref(result);
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, query, -1, SQLITE_TRANSIENT);
    g_free(query);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        gchar const *chunk_text       = (gchar const *) sqlite3_column_text(stmt, 4);
        gint         chunk_offset_on_page = sqlite3_column_int(stmt, 3);
        gchar const *highlighted      = (gchar const *) sqlite3_column_text(stmt, 5);

        if (!chunk_text || !*chunk_text || !highlighted || !*highlighted) continue;

        /* Durchsuche highlighted nach \x01-Markierungen.
         * Für jede Markierung: Offset im Original-chunk_text berechnen.
         * Da highlight() die Marker einfügt ohne den Text zu ändern,
         * ist der Byte-Offset im Original = Offset in highlighted minus
         * Anzahl der bisher eingefügten Marker-Bytes. */
        gchar const *ph  = highlighted;  /* läuft im highlighted-Text */
        gint         marker_bytes = 0;   /* bisher eingefügte \x01/\x02-Bytes */

        while ((ph = strchr(ph, '\x01')) != NULL) {
            /* Byte-Offset des \x01 im highlighted-Text */
            gint offset_in_highlighted = (gint)(ph - highlighted);

            /* Offset im Original-chunk_text = offset_in_highlighted minus
             * alle bisher eingefügten Marker-Bytes */
            gint occ_offset_in_chunk = offset_in_highlighted - marker_bytes;
            gint occ_pos_in_page     = chunk_offset_on_page + occ_offset_in_chunk;

            /* Marker überspringen */
            ph++; /* über \x01 */
            marker_bytes++;

            /* Ende des Treffers suchen (über \x02) */
            gchar const *term_end_hl = strchr(ph, '\x02');
            if (!term_end_hl) break; /* defekter highlight-Text */
            gint term_len_orig = (gint)(term_end_hl - ph);

            /* Snippet aus dem Original-chunk_text um die Fundstelle */
#define SNIPPET_CTX 80
            gchar const *chunk_end  = chunk_text + strlen(chunk_text);
            gchar const *orig_start = chunk_text + occ_offset_in_chunk;
            gchar const *orig_end   = orig_start + term_len_orig;

            gchar const *snip_start = orig_start;
            for (gint k = 0; k < SNIPPET_CTX && snip_start > chunk_text; k++)
                snip_start = g_utf8_prev_char(snip_start);

            gchar const *snip_end = orig_end;
            for (gint k = 0; k < SNIPPET_CTX && snip_end < chunk_end; k++)
                snip_end = g_utf8_next_char(snip_end);

            gboolean ellipsis_before = (snip_start > chunk_text);
            gboolean ellipsis_after  = (snip_end   < chunk_end);
            gsize    snip_len        = (gsize)(snip_end - snip_start);
            gchar   *snippet = g_strdup_printf("%s%.*s%s",
                    ellipsis_before ? "..." : "",
                    (gint)snip_len, snip_start,
                    ellipsis_after  ? "..." : "");

            SondIndexHit *hit = g_new0(SondIndexHit, 1);
            hit->filename         = g_strdup((gchar const *) sqlite3_column_text(stmt, 0));
            hit->page_nr          = sqlite3_column_int(stmt, 1);
            hit->char_pos         = sqlite3_column_int(stmt, 2);
            hit->snippet          = snippet;
            hit->char_pos_in_page = occ_pos_in_page;
            g_ptr_array_add(result, hit);

            /* hinter \x02 weitersuchen */
            ph = term_end_hl + 1; /* über \x02 */
            marker_bytes++;       /* \x02 zählen */
        }
    }

    if (rc != SQLITE_DONE) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "sond_index_search: step FTS: %s",
                    sqlite3_errmsg(ctx->db));
        sqlite3_finalize(stmt);
        g_ptr_array_unref(result);
        return NULL;
    }

    sqlite3_finalize(stmt);

    /* ---------------------------------------------------------------
     * 1b. Deduplizierung: Durch Chunk-Überlappung kann dasselbe Vorkommen
     * aus zwei Chunks gemeldet werden. Duplikate mit gleicher
     * (filename, page_nr, char_pos_in_page) werden entfernt.
     * ------------------------------------------------------------- */
    {
        GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                  g_free, NULL);
        guint i = 0;
        while (i < result->len) {
            SondIndexHit *h   = g_ptr_array_index(result, i);
            gchar        *key = g_strdup_printf("%s|%d|%d",
                    h->filename ? h->filename : "",
                    h->page_nr, h->char_pos_in_page);
            if (g_hash_table_contains(seen, key)) {
                g_free(key);
                g_ptr_array_remove_index(result, i);
            } else {
                g_hash_table_insert(seen, key, GINT_TO_POINTER(1));
                i++;
            }
        }
        g_hash_table_destroy(seen);
    }

    /* ---------------------------------------------------------------
     * 2. Dateinamen-Suche über files-Tabelle (LIKE, Basename)
     *
     * Gesucht wird im letzten Pfadsegment nach dem letzten '/'.
     * Bereits per FTS gefundene Dateien werden nicht doppelt gelistet.
     * Der context-Parameter wird bei der Dateinamensuche ignoriert.
     * ------------------------------------------------------------- */
    {
        /* LIKE-Muster: %term%
         * LIKE-Sonderzeichen (% _) im Suchbegriff werden nicht maskiert –
         * sie treten in Dateinamen so gut wie nie auf. */
        gchar *pattern = g_strdup_printf("%%%s%%", term);

        rc = sqlite3_prepare_v2(ctx->db,
            /* Basename per SQLite-String-Funktionen isolieren:
             * SUBSTR(filename, INSTR(filename,'/')+1) liefert
             * alles nach dem ersten '/'. Da der Pfad immer
             * relative Segmente mit '/' trennt, reicht ein
             * einfacher Vergleich auf den Gesamt-Dateinamen
             * mit dem LIKE-Pattern – false positives durch
             * Verzeichnisnamen sind akzeptabel, da das Ergebnis
             * ohnehin auf den Dateinamen zeigt.
             *
             * Zusätzlich: Ergebnis nur wenn filename NICHT bereits
             * in der FTS-Treffermenge ist, damit keine Duplikate.
             */
            "SELECT DISTINCT filename FROM files"
            " WHERE filename LIKE ? ESCAPE '\\'"
            "   AND filename NOT IN ("
            "     SELECT DISTINCT c.filename FROM chunks_fts"
            "     JOIN chunks c ON c.id = chunks_fts.rowid"
            "     WHERE chunks_fts MATCH ?2"
            "   )"
            " ORDER BY filename",
            -1, &stmt, NULL);

        if (rc != SQLITE_OK) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "sond_index_search: prepare filename: %s",
                        sqlite3_errmsg(ctx->db));
            g_free(pattern);
            g_ptr_array_unref(result);
            return NULL;
        }

        /* ?1 = LIKE-Muster, ?2 = FTS-Query (für NOT IN) */
        gchar *fts_query = build_fts_query(term, context);
        sqlite3_bind_text(stmt, 1, pattern,   -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, fts_query, -1, SQLITE_TRANSIENT);
        g_free(pattern);
        g_free(fts_query);

        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            SondIndexHit *hit = g_new0(SondIndexHit, 1);

            hit->filename = g_strdup((gchar const *) sqlite3_column_text(stmt, 0));
            hit->page_nr  = -1;
            hit->char_pos = 0;
            hit->snippet  = g_strdup("(Dateiname)");

            g_ptr_array_add(result, hit);
        }

        if (rc != SQLITE_DONE) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "sond_index_search: step filename: %s",
                        sqlite3_errmsg(ctx->db));
            sqlite3_finalize(stmt);
            g_ptr_array_unref(result);
            return NULL;
        }

        sqlite3_finalize(stmt);
    }

    return result;
}

/* =======================================================================
 * files-Tabelle: Hilfsfunktionen
 * ======================================================================= */

/* Trägt filename in files ein */
static void sond_index_file_set(SondIndexCtx *ctx, gchar const *filename) {
    sqlite3_stmt *stmt = NULL;

    gint rc = sqlite3_prepare_v2(ctx->db,
            "INSERT OR IGNORE INTO files(filename) VALUES(?)",
            -1, &stmt, NULL);
    if (rc != SQLITE_OK)
        return;

    sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

/* =======================================================================
 * sond_server_index
 * ======================================================================= */

void sond_index(fz_context* ctx,
		void (*log_func)(void*, gchar const*, ...), gpointer log_func_data,
		SondIndexCtx  *sond_index_ctx, gchar const   *filename, guchar const  *buf,
		gsize size, gchar const *mime_type) {
    if (!sond_index_ctx) return;
    if (!mime_type) return;

    /* Segmente extrahieren */
    GPtrArray *segs = NULL;

    if (!g_strcmp0(mime_type, "application/pdf"))
        segs = extract_segments_from_pdf(sond_index_ctx, ctx, buf, size,
        		log_func, log_func_data);
    else if (!g_strcmp0(mime_type, "message/rfc822"))
        segs = extract_segments_from_gmessage(buf, size);
    else if (!g_strcmp0(mime_type, "text/html"))
        segs = extract_segments_from_html(buf, size);
    else if (g_str_has_prefix(mime_type, "text/"))
        segs = extract_segments_from_text(buf, size);
    else
        return; /* MIME-Typ nicht indizierbar */

    if (!segs || segs->len == 0) {
        if (segs) g_ptr_array_unref(segs);
        return;
    }

    char *errmsg = NULL;
    if (sqlite3_exec(sond_index_ctx->db, "BEGIN;", NULL, NULL, &errmsg) != SQLITE_OK) {
        if (log_func)
            log_func(log_func_data, "sond_index: BEGIN fehlgeschlagen: %s", errmsg);
        sqlite3_free(errmsg);
        g_ptr_array_unref(segs);
        return;
    }

    gint chunk_idx = 0;
    for (guint s = 0; s < segs->len; s++) {
        SondTextSegment *seg   = g_ptr_array_index(segs, s);
        GPtrArray       *chunks = text_to_chunks(seg->text,
        		sond_index_ctx->chunk_size,
				sond_index_ctx->chunk_overlap);

        for (guint i = 0; i < chunks->len; i++) {
            SondChunk   *chunk = g_ptr_array_index(chunks, i);
            gfloat *embedding  = compute_embedding(sond_index_ctx, chunk->text);
            if (!db_insert_chunk(sond_index_ctx, log_func, log_func_data, filename, chunk_idx,
                            seg->page_nr,
                            seg->char_pos + chunk->offset,
                            mime_type,
                            chunk->text, embedding)) {
                g_free(embedding);
                g_ptr_array_unref(chunks);
                sqlite3_exec(sond_index_ctx->db, "ROLLBACK;", NULL, NULL, NULL);
                g_ptr_array_unref(segs);
                return;
            }
            g_free(embedding);
            chunk_idx++;
        }

        g_ptr_array_unref(chunks);
    }

    if (sqlite3_exec(sond_index_ctx->db, "COMMIT;", NULL, NULL, &errmsg) != SQLITE_OK) {
        if (log_func)
            log_func(log_func_data, "sond_index: COMMIT fehlgeschlagen: %s", errmsg);
        sqlite3_free(errmsg);
        sqlite3_exec(sond_index_ctx->db, "ROLLBACK;", NULL, NULL, NULL);
        g_ptr_array_unref(segs);
        return;
    }

    g_ptr_array_unref(segs);

    /* leaf als indiziert markieren */
    sond_index_file_set(sond_index_ctx, filename);
}
