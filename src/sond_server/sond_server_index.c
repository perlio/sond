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

#include "sond_server_index.h"
#include "../sond_log_and_error.h"

#include <glib.h>
#include <sqlite3.h>
#include <mupdf/fitz.h>
#include <gmime/gmime.h>
#include <string.h>
#include "../sond_gmessage_helper.h"
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
                                  gpointer     fz_ctx,
                                  void       (*log_func)(gpointer, gchar const*, ...),
                                  gpointer     log_data,
                                  GError     **error) {
    SondIndexCtx *ctx = g_new0(SondIndexCtx, 1);

    ctx->db_path       = g_strdup(db_path);
    ctx->chunk_size    = (chunk_size    > 0) ? chunk_size    : INDEX_DEFAULT_CHUNK_SIZE;
    ctx->chunk_overlap = (chunk_overlap > 0) ? chunk_overlap : INDEX_DEFAULT_CHUNK_OVERLAP;
    ctx->fz_ctx        = fz_ctx;
    ctx->log_func      = log_func;
    ctx->log_data      = log_data;

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
    gchar        *pattern = g_strdup_printf("%s%%", filename);

    gint rc = sqlite3_prepare_v2(ctx->db,
            "DELETE FROM chunks WHERE filename LIKE ?",
            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "sond_index_ctx_clear_file: prepare: %s",
                    sqlite3_errmsg(ctx->db));
        g_free(pattern);
        return FALSE;
    }

    sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    g_free(pattern);

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

static gboolean db_insert_chunk(SondIndexCtx *ctx,
                                 gchar const  *filename,
                                 gint          chunk_idx,
                                 gint          page_nr,
                                 gint          char_pos,
                                 gchar const  *mime_type,
                                 gchar const  *text,
                                 gfloat       *embedding) {
    sqlite3_stmt  *stmt  = NULL;
    gint           rc    = 0;
    sqlite3_int64  rowid = 0;

    rc = sqlite3_prepare_v2(ctx->db,
            "INSERT INTO chunks(filename, chunk_idx, page_nr, char_pos, mime_type, text)"
            " VALUES(?,?,?,?,?,?)",
            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        if (ctx->log_func)
            ctx->log_func(ctx->log_data,
                    "db_insert_chunk: prepare: %s", sqlite3_errmsg(ctx->db));
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
        if (ctx->log_func)
            ctx->log_func(ctx->log_data,
                    "db_insert_chunk: step: %s", sqlite3_errmsg(ctx->db));
        return FALSE;
    }

    rowid = sqlite3_last_insert_rowid(ctx->db);

#ifdef SOND_WITH_EMBEDDINGS
    if (embedding && ctx->n_embd > 0) {
        rc = sqlite3_prepare_v2(ctx->db,
                "INSERT INTO chunks_vec(rowid, embedding) VALUES(?,?)",
                -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            if (ctx->log_func)
                ctx->log_func(ctx->log_data,
                        "db_insert_chunk: prepare vec: %s", sqlite3_errmsg(ctx->db));
            return FALSE;
        }

        sqlite3_bind_int64(stmt, 1, rowid);
        sqlite3_bind_blob (stmt, 2, embedding,
                           ctx->n_embd * (gint)sizeof(gfloat), SQLITE_STATIC);

        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE && ctx->log_func)
            ctx->log_func(ctx->log_data,
                    "db_insert_chunk: step vec: %s", sqlite3_errmsg(ctx->db));
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
static GPtrArray* extract_segments_from_pdf(SondIndexCtx *ctx,
                                             guchar const *buf, gsize size) {
    fz_context *fz  = (fz_context*)ctx->fz_ctx;
    GPtrArray  *segs = g_ptr_array_new_with_free_func(
            (GDestroyNotify)sond_text_segment_free);

    if (!fz) return segs;

    fz_document *doc = NULL;
    gint char_pos_total = 0;

    fz_try(fz) {
        fz_stream *stream = fz_open_memory(fz, (guchar*)buf, size);
        doc = fz_open_document_with_stream(fz, "application/pdf", stream);
        fz_drop_stream(fz, stream);

        gint n_pages = fz_count_pages(fz, doc);
        for (gint i = 0; i < n_pages; i++) {
            fz_stext_options opts  = { 0 };
            fz_stext_page   *stext = fz_new_stext_page_from_page_number(
                    fz, doc, i, &opts);
            GString *page_text = g_string_new(NULL);

            for (fz_stext_block *b = stext->first_block; b; b = b->next) {
                if (b->type != FZ_STEXT_BLOCK_TEXT) continue;
                for (fz_stext_line *l = b->u.t.first_line; l; l = l->next) {
                    for (fz_stext_char *c = l->first_char; c; c = c->next) {
                        gchar utf8[8];
                        gint  n = fz_runetochar(utf8, c->c);
                        g_string_append_len(page_text, utf8, n);
                    }
                    g_string_append_c(page_text, '\n');
                }
            }
            fz_drop_stext_page(fz, stext);

            if (page_text->len > 0) {
                gint page_char_pos = char_pos_total;
                char_pos_total += (gint)page_text->len;
                g_ptr_array_add(segs,
                        sond_text_segment_new(
                                g_string_free(page_text, FALSE),
                                i, page_char_pos));
            } else {
                g_string_free(page_text, TRUE);
            }
        }
    }
    fz_always(fz) {
        if (doc) fz_drop_document(fz, doc);
    }
    fz_catch(fz) {
        if (ctx->log_func)
            ctx->log_func(ctx->log_data,
                    "extract_segments_from_pdf: %s", fz_caught_message(fz));
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
 * sond_server_index
 * ======================================================================= */

void sond_server_index(SondIndexCtx  *ctx,
                        gchar const   *filename,
                        guchar const  *buf,
                        gsize          size,
                        gchar const   *mime_type) {
    if (!ctx)       return;
    if (!mime_type) return;

    /* Segmente extrahieren */
    GPtrArray *segs = NULL;

    if (!g_strcmp0(mime_type, "application/pdf"))
        segs = extract_segments_from_pdf(ctx, buf, size);
    else if (!g_strcmp0(mime_type, "message/rfc822"))
        segs = extract_segments_from_gmessage(buf, size);
    else if (g_str_has_prefix(mime_type, "text/"))
        segs = extract_segments_from_text(buf, size);
    else
        return; /* MIME-Typ nicht indizierbar */

    if (!segs || segs->len == 0) {
        if (segs) g_ptr_array_unref(segs);
        return;
    }

    GError *error = NULL;
    if (!sond_index_ctx_clear_file(ctx, filename, &error)) {
        if (ctx->log_func)
            ctx->log_func(ctx->log_data,
                    "sond_server_index: clear_file '%s': %s",
                    filename, error ? error->message : "unknown");
        g_clear_error(&error);
        g_ptr_array_unref(segs);
        return;
    }

    sqlite3_exec(ctx->db, "BEGIN;", NULL, NULL, NULL);

    gint chunk_idx = 0;
    for (guint s = 0; s < segs->len; s++) {
        SondTextSegment *seg   = g_ptr_array_index(segs, s);
        GPtrArray       *chunks = text_to_chunks(seg->text,
                                                  ctx->chunk_size,
                                                  ctx->chunk_overlap);

        for (guint i = 0; i < chunks->len; i++) {
            SondChunk   *chunk = g_ptr_array_index(chunks, i);
            gfloat *embedding  = compute_embedding(ctx, chunk->text);
            db_insert_chunk(ctx, filename, chunk_idx,
                            seg->page_nr,
                            seg->char_pos + chunk->offset,
                            mime_type,
                            chunk->text, embedding);
            g_free(embedding);
            chunk_idx++;
        }

        g_ptr_array_unref(chunks);
    }

    sqlite3_exec(ctx->db, "COMMIT;", NULL, NULL, NULL);

    g_ptr_array_unref(segs);
}
