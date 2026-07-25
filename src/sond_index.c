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
#include <gio/gio.h>
#include <sqlite3.h>
#include <mupdf/fitz.h>
#include <string.h>
#include <math.h>

#include "sond_text_extract.h"
#include "sond_ocr.h"

#ifdef SOND_WITH_EMBEDDINGS
#include <llama.h>
#include <ggml-backend.h>
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
    "  text      TEXT    NOT NULL,"
    "  embedding BLOB"
    ");"; /* embedding: roher gfloat[n_embd]-Vektor, NULL wenn (noch) nicht
           * berechnet. Bewußt eine normale Spalte statt einer vec0-Virtual-
           * Table - keine sqlite-vec-Abhängigkeit, Ähnlichkeitssuche läuft
           * brute-force in C (siehe sond_index_semantic_search()). */

static const gchar *SQL_CREATE_FTS =
    "CREATE VIRTUAL TABLE IF NOT EXISTS chunks_fts USING fts5("
    "  text,"
    "  content=chunks,"
    "  content_rowid=id"
    ");";

static const gchar *SQL_CREATE_META =
    "CREATE TABLE IF NOT EXISTS meta ("
    "  key   TEXT PRIMARY KEY,"
    "  value TEXT"
    ");"; /* Schlüssel-Wert-Speicher, aktuell für die Identität des
           * zuletzt verwendeten Embedding-Modells (Schlüssel
           * "embedding_model" / "embedding_dim") - siehe
           * sond_index_ctx_new() und sond_index_ctx_embedding_model_changed(). */

static const gchar *SQL_TRIGGER_INSERT =
    "CREATE TRIGGER IF NOT EXISTS chunks_ai AFTER INSERT ON chunks BEGIN"
    "  INSERT INTO chunks_fts(rowid, text) VALUES (new.id, new.text);"
    "END;";

static const gchar *SQL_TRIGGER_DELETE =
    "CREATE TRIGGER IF NOT EXISTS chunks_ad AFTER DELETE ON chunks BEGIN"
    "  INSERT INTO chunks_fts(chunks_fts, rowid, text)"
    "    VALUES('delete', old.id, old.text);"
    "END;";

static const gchar *SQL_CREATE_PAGES =
    "CREATE TABLE IF NOT EXISTS pages ("
    "  filename  TEXT    NOT NULL,"
    "  page_nr   INTEGER NOT NULL DEFAULT -1,"
    "  ocr_mode  INTEGER NOT NULL DEFAULT 0,"
    "  PRIMARY KEY(filename, page_nr)"
    ");"; /* Präsenzliste indizierter Seiten je Datei, inkl. zuletzt
           * angewandtem OCR-Modus (SondOcrMode-Wert). Nicht-PDF-Formate:
           * ein Eintrag mit page_nr = -1. Ersetzt die frühere reine
           * Datei-Präsenzliste "files" - jetzt seitenweise, damit sich
           * a) Anbindungen (Seitenbereiche) gezielt (neu) indizieren lassen
           * und b) doppelte Arbeit bei überlappenden Anbindungen bzw.
           * erneuten Läufen anhand des zuletzt angewandten OCR-Modus
           * vermieden werden kann. */

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

    /* Migration: embedding-Spalte in DBs nachrüsten, die vor dieser
     * Änderung angelegt wurden. SQLite kennt kein ADD COLUMN IF NOT
     * EXISTS - der erwartbare Fehler ("duplicate column name"), wenn die
     * Spalte schon existiert, wird deshalb bewußt ignoriert. */
    sqlite3_exec(ctx->db, "ALTER TABLE chunks ADD COLUMN embedding BLOB;",
            NULL, NULL, NULL);

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

    rc = sqlite3_exec(ctx->db, SQL_CREATE_PAGES, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "db_init_schema: CREATE pages: %s", errmsg);
        sqlite3_free(errmsg);
        return FALSE;
    }

    rc = sqlite3_exec(ctx->db, SQL_CREATE_META, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "db_init_schema: CREATE meta: %s", errmsg);
        sqlite3_free(errmsg);
        return FALSE;
    }

    return TRUE;
}

/* =======================================================================
 * meta-Tabelle: einfacher Schlüssel-Wert-Speicher
 * ======================================================================= */

static gchar* db_meta_get(SondIndexCtx *ctx, gchar const *key) {
    sqlite3_stmt *stmt  = NULL;
    gchar        *value = NULL;

    if (sqlite3_prepare_v2(ctx->db,
            "SELECT value FROM meta WHERE key = ?", -1, &stmt, NULL) != SQLITE_OK)
        return NULL;

    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW)
        value = g_strdup((gchar const*) sqlite3_column_text(stmt, 0));

    sqlite3_finalize(stmt);
    return value;
}

static void db_meta_set(SondIndexCtx *ctx, gchar const *key, gchar const *value) {
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(ctx->db,
            "INSERT INTO meta(key, value) VALUES(?,?)"
            " ON CONFLICT(key) DO UPDATE SET value = excluded.value",
            -1, &stmt, NULL) != SQLITE_OK)
        return;

    sqlite3_bind_text(stmt, 1, key,   -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

#ifdef SOND_WITH_EMBEDDINGS
/* sond_llama_ensure_backends:
 *
 * llama_backend_init() ruft ggml_backend_load_all() nur auf, solange noch
 * kein Backend registriert ist - das durchsucht dabei aber nur zwei Orte:
 * das Verzeichnis der .exe selbst und das aktuelle Arbeitsverzeichnis
 * (siehe ggml_backend_load_best() in ggml-backend-reg.cpp). Der normale
 * DLL-Suchpfad (PATH), über den Windows z.B. ggml.dll/libllama.dll für den
 * Programmstart selbst longst gefunden hat, wird dabei NICHT konsultiert.
 *
 * In der MSYS2/UCRT64-Entwicklungsumgebung liegen die eigentlichen
 * Recheneinheiten (ggml-base.dll, ggml-cpu-*.dll) unter /ucrt64/bin - im
 * PATH vorhanden, aber weder neben zond.exe (Debug/Release) noch im
 * Arbeitsverzeichnis. Resultat: "no backends are loaded" trotz
 * erfolgreich gestartetem Programm. Fallback hier: PATH selbst nach einem
 * Verzeichnis mit ggml-base.dll absuchen und darüber laden.
 *
 * Für einen künftigen Release (Backend-DLLs neben der .exe ausgeliefert)
 * greift bereits der eingebaute Standard-Suchpfad - dieser Fallback stört
 * dann nicht (ggml_backend_reg_count() ist zu diesem Zeitpunkt schon > 0,
 * die Funktion kehrt sofort zurück). */
static void
sond_llama_ensure_backends(void) {
    if (ggml_backend_reg_count() > 0)
        return;

    gchar const *path_env = g_getenv("PATH");
    if (!path_env)
        return;

    gchar **dirs = g_strsplit(path_env, G_SEARCHPATH_SEPARATOR_S, -1);
    for (gint i = 0; dirs[i] && ggml_backend_reg_count() == 0; i++) {
        gchar *probe = g_build_filename(dirs[i], "ggml-base.dll", NULL);
        if (g_file_test(probe, G_FILE_TEST_EXISTS))
            ggml_backend_load_all_from_path(dirs[i]);
        g_free(probe);
    }
    g_strfreev(dirs);
}
#endif

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

    /* Schema (inkl. meta-Tabelle) muß vor dem Modell-Metadaten-Abgleich
     * unten stehen - deshalb hier vor dem Laden des llama-Modells, anders
     * als früher. */
    if (!db_init_schema(ctx, error)) {
        sond_index_ctx_free(ctx);
        return NULL;
    }

#ifdef SOND_WITH_EMBEDDINGS
    /* Ein fehlendes/nicht ladbares Embedding-Modell darf sond_index_ctx_new()
     * NICHT insgesamt scheitern lassen - Volltextsuche und Indizierung ohne
     * Embeddings müssen weiter funktionieren (das Modell wird bewußt nicht
     * mit dem Release ausgeliefert, der Nutzer lädt es sich selbst nach
     * Wahl herunter, siehe sond_index_ctx_has_embeddings()). Fehler beim
     * Laden werden deshalb nur geloggt (g_warning), nicht über *error
     * zurückgegeben. */
    if (model_path) {
        llama_backend_init();
        sond_llama_ensure_backends();

        struct llama_model_params model_params = llama_model_default_params();
        /* Testweise echtes GPU-Offload (Intel-UHD-iGPU über Vulkan/OpenCL,
         * beide von llama.cpp bereits geladen) statt reinem CPU-Betrieb -
         * die frühere Vermutung, die bloße Anwesenheit der GPU in der
         * Geräteliste (bei n_gpu_layers=0, also 0 tatsächlich ausgelagerten
         * Layern) verursache die Verlangsamung, hat sich als falsch
         * herausgestellt (eigentliche Ursache war der an gdb hängende
         * Debug-Lauf). 999 ist die in llama.cpp übliche Konvention für
         * "so viele Layer wie möglich auslagern" (mehr als das Modell hat -
         * llama.cpp kappt automatisch auf die tatsächliche Layerzahl).
         * Ob die iGPU für dieses kleine Modell tatsächlich schneller ist
         * als 12 CPU-Threads, zeigt erst der Test (siehe Konsolenausgabe
         * "load_tensors: offloaded X/29 layers to GPU" beim nächsten Start). */
        model_params.n_gpu_layers = 999;

        ctx->llama_model = llama_model_load_from_file(model_path, model_params);
        if (!ctx->llama_model) {
            g_warning("sond_index_ctx_new: Embedding-Modell '%s' konnte nicht "
                    "geladen werden (Datei fehlt/beschädigt?) - Embeddings "
                    "sind für diese Sitzung deaktiviert.", model_path);
        } else {
            struct llama_context_params ctx_params = llama_context_default_params();
            ctx_params.embeddings = TRUE;
            /* 512 Token reichten nicht: ein Chunk von chunk_size=1000
             * Zeichen (Standard, s.o.) kann bei dichter Tokenisierung
             * (deutsche Rechtstexte, viele Komposita) schon über 512 Token
             * ergeben - beobachtet wurde n=528 bei genau dieser Konstellation.
             * 2048 läßt reichlich Luft (auch für chunk_overlap und ggf.
             * größere chunk_size-Werte), ohne spürbar mehr Rechenzeit/RAM zu
             * kosten (Qwen3-Embedding unterstützt nativ deutlich mehr). */
            ctx_params.n_ctx      = 2048;
            ctx_params.n_batch    = 2048;
            /* llama_context_default_params() setzt hier nur einen
             * konservativen Festwert (4), unabhängig von der tatsächlichen
             * Kernzahl - auf CPU-only-Systemen (s. Vorgabe: "nur CPU,
             * normaler PC") bringt die Nutzung aller verfügbaren Kerne einen
             * spürbaren Geschwindigkeitsgewinn bei der Embedding-Berechnung. */
            {
                gint n_cpu = g_get_num_processors();
                ctx_params.n_threads       = n_cpu;
                ctx_params.n_threads_batch = n_cpu;
            }

            ctx->llama_ctx = llama_init_from_model(
                    (struct llama_model*)ctx->llama_model, ctx_params);
            if (!ctx->llama_ctx) {
                g_warning("sond_index_ctx_new: llama_init_from_model fehlgeschlagen "
                        "- Embeddings sind für diese Sitzung deaktiviert.");
                llama_model_free((struct llama_model*)ctx->llama_model);
                ctx->llama_model = NULL;
            } else {
                ctx->n_embd = llama_model_n_embd(
                        (struct llama_model*)ctx->llama_model);

                /* Modell-Identität (Dateiname + Dimension) mit dem Metadatum
                 * abgleichen, mit dem die vorhandenen Embeddings zuletzt
                 * berechnet wurden. Weicht es ab (anderes Modell/andere
                 * Version/andere Quantisierung), sind die gespeicherten
                 * Vektoren mit neu berechneten nicht mehr vergleichbar - der
                 * Aufrufer muß dann ein Re-Embedding anstoßen
                 * (sond_index_ctx_embedding_model_changed()). Das Metadatum
                 * selbst wird hier bereits auf den jetzt konfigurierten
                 * Stand aktualisiert. */
                gchar *model_name     = g_path_get_basename(model_path);
                gchar *stored_name    = db_meta_get(ctx, "embedding_model");
                gchar *stored_dim_str = db_meta_get(ctx, "embedding_dim");
                gint   stored_dim     = stored_dim_str ? atoi(stored_dim_str) : -1;
                gchar *dim_str        = NULL;

                if (stored_name != NULL &&
                        (g_strcmp0(stored_name, model_name) != 0 || stored_dim != ctx->n_embd))
                    ctx->embedding_model_changed = TRUE;

                db_meta_set(ctx, "embedding_model", model_name);
                dim_str = g_strdup_printf("%d", ctx->n_embd);
                db_meta_set(ctx, "embedding_dim", dim_str);

                g_free(dim_str);
                g_free(model_name);
                g_free(stored_name);
                g_free(stored_dim_str);
            }
        }
    }
#endif

    return ctx;
}

gboolean sond_index_ctx_has_embeddings(SondIndexCtx *ctx) {
#ifdef SOND_WITH_EMBEDDINGS
    return ctx && ctx->llama_ctx && ctx->n_embd > 0;
#else
    (void) ctx;
    return FALSE;
#endif
}

gboolean sond_index_ctx_embedding_model_changed(SondIndexCtx *ctx) {
    return ctx ? ctx->embedding_model_changed : FALSE;
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

    /* pages-Einträge löschen */
    rc = sqlite3_prepare_v2(ctx->db,
            "DELETE FROM pages WHERE filename = ? OR filename LIKE ?",
            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "sond_index_ctx_clear_file: prepare pages: %s",
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
                    "sond_index_ctx_clear_file: step pages: %s",
                    sqlite3_errmsg(ctx->db));
        return FALSE;
    }

    return TRUE;
}

/* =======================================================================
 * sond_index_ctx_clear_page
 * ======================================================================= */

gboolean sond_index_ctx_clear_page(SondIndexCtx *ctx,
                                    gchar const  *filename,
                                    gint          page_nr,
                                    GError      **error) {
    sqlite3_stmt *stmt = NULL;

    gint rc = sqlite3_prepare_v2(ctx->db,
            "DELETE FROM chunks WHERE filename = ? AND page_nr = ?",
            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "sond_index_ctx_clear_page: prepare chunks: %s",
                    sqlite3_errmsg(ctx->db));
        return FALSE;
    }

    sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 2, page_nr);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "sond_index_ctx_clear_page: step chunks: %s",
                    sqlite3_errmsg(ctx->db));
        return FALSE;
    }

    rc = sqlite3_prepare_v2(ctx->db,
            "DELETE FROM pages WHERE filename = ? AND page_nr = ?",
            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "sond_index_ctx_clear_page: prepare pages: %s",
                    sqlite3_errmsg(ctx->db));
        return FALSE;
    }

    sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 2, page_nr);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "sond_index_ctx_clear_page: step pages: %s",
                    sqlite3_errmsg(ctx->db));
        return FALSE;
    }

    return TRUE;
}

/* =======================================================================
 * sond_index_ctx_renumber_page
 * ======================================================================= */

static gboolean sond_index_ctx_renumber_one_pass(SondIndexCtx *ctx,
        gchar const *filename, gint from_page_nr, gint to_page_nr,
        gchar const *caller, GError **error) {
    gchar const *tables[] = { "chunks", "pages" };

    for (guint t = 0; t < G_N_ELEMENTS(tables); t++) {
        sqlite3_stmt *stmt = NULL;
        gchar *sql = g_strdup_printf(
                "UPDATE %s SET page_nr = ? WHERE filename = ? AND page_nr = ?",
                tables[t]);

        gint rc = sqlite3_prepare_v2(ctx->db, sql, -1, &stmt, NULL);
        g_free(sql);
        if (rc != SQLITE_OK) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "%s: prepare %s: %s",
                        caller, tables[t], sqlite3_errmsg(ctx->db));
            return FALSE;
        }

        sqlite3_bind_int (stmt, 1, to_page_nr);
        sqlite3_bind_text(stmt, 2, filename, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int (stmt, 3, from_page_nr);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "%s: step %s: %s",
                        caller, tables[t], sqlite3_errmsg(ctx->db));
            return FALSE;
        }
    }

    return TRUE;
}

gboolean sond_index_ctx_renumber_page(SondIndexCtx *ctx,
                                       gchar const  *filename,
                                       gint          old_page_nr,
                                       gint          new_page_nr,
                                       GError      **error) {
    return sond_index_ctx_renumber_pages(ctx, filename,
            &old_page_nr, &new_page_nr, 1, error);
}

gboolean sond_index_ctx_renumber_pages(SondIndexCtx *ctx,
                                        gchar const  *filename,
                                        gint const   *old_page_nrs,
                                        gint const   *new_page_nrs,
                                        guint         n,
                                        GError      **error) {
    /* Kollisionsfreier Zwischenwert: pages hat PRIMARY KEY(filename,
     * page_nr) - ein direktes UPDATE auf new_page_nr kann fehlschlagen,
     * wenn diese Seite gerade erst durch eine andere, noch nicht
     * verarbeitete Zeile derselben Umnumerierungs-Serie frei wird (z.B.
     * Seite 5->4, während Seite 4->3 noch aussteht: Seite 4 ist zum
     * Zeitpunkt von "5->4" noch belegt). Daher zwei komplett getrennte
     * Durchgänge über ALLE Seiten: erst alle auf einen Zwischenwert,
     * dann alle vom Zwischenwert auf die endgültige neue Seitenzahl - so
     * ist die Reihenfolge der Einträge in old_page_nrs/new_page_nrs
     * beliebig. */
    for (guint i = 0; i < n; i++) {
        if (old_page_nrs[i] == new_page_nrs[i])
            continue;

        gint tmp_page_nr = -1000000 - old_page_nrs[i];
        if (!sond_index_ctx_renumber_one_pass(ctx, filename,
                old_page_nrs[i], tmp_page_nr,
                "sond_index_ctx_renumber_pages (tmp)", error))
            return FALSE;
    }

    for (guint i = 0; i < n; i++) {
        if (old_page_nrs[i] == new_page_nrs[i])
            continue;

        gint tmp_page_nr = -1000000 - old_page_nrs[i];
        if (!sond_index_ctx_renumber_one_pass(ctx, filename,
                tmp_page_nr, new_page_nrs[i],
                "sond_index_ctx_renumber_pages", error))
            return FALSE;
    }

    return TRUE;
}

/* =======================================================================
 * sond_index_ctx_get_page_ocr_mode / sond_index_ctx_should_process_page
 * ======================================================================= */

GArray* sond_index_ctx_get_pages_for_file(SondIndexCtx *ctx,
                                           gchar const  *filename) {
    GArray       *result = g_array_new(FALSE, FALSE, sizeof(gint));
    sqlite3_stmt *stmt   = NULL;

    gint rc = sqlite3_prepare_v2(ctx->db,
            "SELECT page_nr FROM pages WHERE filename = ?",
            -1, &stmt, NULL);
    if (rc != SQLITE_OK)
        return result;

    sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        gint page_nr = sqlite3_column_int(stmt, 0);
        g_array_append_val(result, page_nr);
    }

    sqlite3_finalize(stmt);
    return result;
}

gint sond_index_ctx_get_page_ocr_mode(SondIndexCtx *ctx,
                                       gchar const  *filename,
                                       gint          page_nr) {
    sqlite3_stmt *stmt   = NULL;
    gint          result = -1;

    gint rc = sqlite3_prepare_v2(ctx->db,
            "SELECT ocr_mode FROM pages WHERE filename = ? AND page_nr = ?",
            -1, &stmt, NULL);
    if (rc != SQLITE_OK)
        return -1;

    sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 2, page_nr);

    if (sqlite3_step(stmt) == SQLITE_ROW)
        result = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);
    return result;
}

gboolean sond_index_ctx_should_process_page(SondIndexCtx *ctx,
                                             gchar const  *filename,
                                             gint          page_nr,
                                             gint          requested_mode) {
    gint applied_mode = 0;

    /* erzwingen: immer neu verarbeiten, unabhängig vom bisherigen Stand */
    if (requested_mode >= SOND_OCR_MODE_FORCE)
        return TRUE;

    applied_mode = sond_index_ctx_get_page_ocr_mode(ctx, filename, page_nr);

    if (applied_mode < 0)
        return TRUE; /* noch nie verarbeitet */

    return (requested_mode > applied_mode);
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
    const gchar *tables[] = { "chunks", "pages" };
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

/* Falls sich pos mitten in einer Mehrbyte-UTF-8-Sequenz befindet
 * (Fortsetzungsbyte 10xxxxxx), auf den Anfang dieses Zeichens
 * zurückspringen. So wird nie mitten in einem UTF-8-Zeichen geschnitten
 * (relevant u.a. für Umlaute/ß, die als Mehrbyte-Sequenzen kodiert sind). */
static gint utf8_align_to_char_start(gchar const *text, gint pos) {
    while (pos > 0 && ((guchar) text[pos] & 0xC0) == 0x80)
        pos--;
    return pos;
}

static GPtrArray* text_to_chunks(gchar const *text, gint chunk_size, gint chunk_overlap) {
    GPtrArray *chunks = g_ptr_array_new_with_free_func((GDestroyNotify)sond_chunk_free);
    gint len  = (gint)strlen(text);
    gint step = chunk_size - chunk_overlap;

    if (step <= 0) step = chunk_size;

    for (gint pos = 0; pos < len; ) {
        gint end = utf8_align_to_char_start(text, MIN(pos + chunk_size, len));

        /* utf8_align_to_char_start kann end bis auf pos zurückwerfen, wenn
         * bereits das erste Zeichen ab pos länger als chunk_size (in Byte)
         * ist. Dann trotzdem mindestens dieses eine Zeichen vollständig
         * aufnehmen, statt einen leeren Chunk zu erzeugen. */
        if (end <= pos) {
            end = pos + 1;
            while (end < len && ((guchar) text[end] & 0xC0) == 0x80)
                end++;
        }

        SondChunk *c = g_new0(SondChunk, 1);
        c->text   = g_strndup(text + pos, end - pos);
        c->offset = pos;
        g_ptr_array_add(chunks, c);

        if (end == len) break;

        {
            gint next_pos = utf8_align_to_char_start(text, pos + step);
            /* Sicherheitsnetz: next_pos muss echt vorwärts gehen, sonst
             * Endlosschleife bei sehr kleinem step relativ zu Mehrbyte-
             * Zeichen. */
            pos = (next_pos > pos) ? next_pos : end;
        }
    }

    return chunks;
}

/* =======================================================================
 * Embedding
 * ======================================================================= */

static gfloat* compute_embedding(SondIndexCtx *ctx,
        void (*log_func)(gpointer, gchar const*, ...), gpointer log_func_data,
        gchar const *text) {
#ifndef SOND_WITH_EMBEDDINGS
    (void)ctx; (void)log_func; (void)log_func_data; (void)text;
    return NULL;
#else
    if (!ctx->llama_ctx || !ctx->llama_model || ctx->n_embd <= 0)
        return NULL;

    struct llama_model   *model = (struct llama_model*)   ctx->llama_model;
    struct llama_context *lctx  = (struct llama_context*) ctx->llama_ctx;
    /* Ab neueren llama.cpp-Versionen erwartet llama_tokenize() das
     * llama_vocab* (aus dem Modell herausgelöst), nicht mehr das
     * llama_model* selbst. */
    const struct llama_vocab *vocab = llama_model_get_vocab(model);

    gint n_tokens_max = llama_n_ctx(lctx);
    llama_token *tokens = g_new(llama_token, n_tokens_max);

    gint n_tokens = llama_tokenize(vocab, text, (gint)strlen(text),
                                   tokens, n_tokens_max,
                                   TRUE, FALSE);
    if (n_tokens < 0 || n_tokens > n_tokens_max) {
        if (log_func)
            log_func(log_func_data,
                    "compute_embedding: Tokenisierung fehlgeschlagen (n=%d)", n_tokens);
        g_free(tokens);
        return NULL;
    }

    /* Diagnose: genaue Zeit je Aufruf messen, statt sie nur grob aus dem
     * Abstand der Seiten-Fortschrittsmeldungen zu schätzen. */
    gint64 t0 = g_get_monotonic_time();

    llama_batch batch = llama_batch_get_one(tokens, n_tokens);
    /* llama_kv_cache_clear() wurde durch die Memory-API ersetzt (siehe
     * llama.h: llama_get_memory()/llama_memory_clear()). data=FALSE reicht:
     * jeder Aufruf ist ein unabhängiger, frischer Chunk ohne Fortsetzung
     * eines vorherigen Verlaufs - es genügt, die Positions-/Sequenz-
     * verwaltung zurückzusetzen, damit neue Tokens wieder ab Position 0
     * geschrieben werden. Das physische Nullen der 224-MB-Datenpuffer
     * (data=TRUE) ist dafür nicht nötig, da neue Tokens die alten Werte an
     * denselben Positionen ohnehin überschreiben - spart das beobachtete
     * memset bei jedem einzelnen Chunk. */
    llama_memory_clear(llama_get_memory(lctx), FALSE);

    if (llama_decode(lctx, batch) != 0) {
        if (log_func)
            log_func(log_func_data, "compute_embedding: llama_decode fehlgeschlagen");
        g_free(tokens);
        return NULL;
    }
    g_free(tokens);

    if (log_func)
        log_func(log_func_data,
                "compute_embedding: %d Token in %.2fs",
                n_tokens, (gdouble)(g_get_monotonic_time() - t0) / 1e6);

    gfloat const *embd = llama_get_embeddings(lctx);
    if (!embd) {
        if (log_func)
            log_func(log_func_data, "compute_embedding: llama_get_embeddings NULL");
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
            "INSERT INTO chunks(filename, chunk_idx, page_nr, char_pos, mime_type, text, embedding)"
            " VALUES(?,?,?,?,?,?,?)",
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

#ifdef SOND_WITH_EMBEDDINGS
    if (embedding && sond_index_ctx->n_embd > 0)
        sqlite3_bind_blob(stmt, 7, embedding,
                sond_index_ctx->n_embd * (gint) sizeof(gfloat), SQLITE_STATIC);
    else
        sqlite3_bind_null(stmt, 7);
#else
    (void) embedding;
    sqlite3_bind_null(stmt, 7);
#endif

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        if (log_func)
            log_func(log_func_data,
                    "db_insert_chunk: step: %s", sqlite3_errmsg(sond_index_ctx->db));
        return FALSE;
    }

    return TRUE;
}

/* =======================================================================
 * Textextraktion
 *
 * Die eigentliche Extraktion (PDF/HTML/E-Mail/Text/DOCX/ODT) lebt in
 * sond_text_extract.c - und zwar für Indizierung UND Renderer identisch,
 * damit die hier berechneten char_pos-Offsets im Renderer exakt an der
 * richtigen Stelle landen (siehe sond_text_extract.h für Details).
 * ======================================================================= */

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

/* PDF-Schriften mit defektem/nicht standardkonformem Encoding mappen
 * einzelne Glyphen oft auf Private-Use-Area-Codepunkte oder liefern
 * REPLACEMENT CHARACTER/Steuerzeichen, wenn die Zuordnung zu echtem
 * Unicode fehlschlägt (typisch bei älteren/gescannten PDFs). Mit dem
 * größeren Suchtreffer-Kontext (SNIPPET_CTX) landen solche Stellen jetzt
 * öfter im Ausschnitt und erscheinen als Kästchen/Sonderzeichen-Kauderwelsch
 * über mehrere Zeilen. Daher hier herausfiltern. */
static gboolean is_snippet_garbage_char(gunichar ch) {
    if (ch == 0xFFFD) /* REPLACEMENT CHARACTER */
        return TRUE;

    if ((ch >= 0xE000  && ch <= 0xF8FF)   || /* Private Use Area (BMP) */
        (ch >= 0xF0000 && ch <= 0xFFFFD)  || /* Private Use Area-A */
        (ch >= 0x100000 && ch <= 0x10FFFD))  /* Private Use Area-B */
        return TRUE;

    if (!g_unichar_isprint(ch) && !g_unichar_isspace(ch))
        return TRUE; /* Steuerzeichen u.ä. - Whitespace wird gesondert behandelt */

    return FALSE;
}

/* PDF-/OCR-Text enthält oft harte Zeilenumbrüche an jedem Original-
 * Zeilenende (schmale Spalte, kurze Zeilen) statt fortlaufendem Fließtext.
 * In einem kurzen Suchtreffer-Ausschnitt sorgt das für unnötig viele
 * Zeilen. Fasst daher jede Folge von Whitespace (auch Zeilenumbrüche,
 * Tabs) zu einem einzelnen Leerzeichen zusammen, damit die Fundstelle als
 * fortlaufender Text erscheint - und filtert dabei nicht darstellbare
 * "Sonderzeichen" (s.o.) komplett heraus. */
static gchar* normalize_snippet_whitespace(gchar const *text) {
    GString  *out            = NULL;
    gboolean  last_was_space = FALSE;

    if (!text)
        return NULL;

    out = g_string_new(NULL);

    for (gchar const *p = text; *p; p = g_utf8_next_char(p)) {
        gunichar ch = g_utf8_get_char(p);

        if (is_snippet_garbage_char(ch))
            continue; /* stillschweigend entfernen, kein Ersatzzeichen */

        if (g_unichar_isspace(ch)) {
            if (!last_was_space)
                g_string_append_c(out, ' ');
            last_was_space = TRUE;
        } else {
            g_string_append_unichar(out, ch);
            last_was_space = FALSE;
        }
    }

    return g_strstrip(g_string_free(out, FALSE));
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
/* Zeichen Kontext vor/hinter dem Treffer. Großzügig bemessen, seit das
 * Ergebnisfenster einen Detailbereich hat (Master-Detail): die Liste
 * zeigt ohnehin nur max. RV_SNIPPET_MAX_LINES Zeilen an (Rest ellipsiert),
 * im Detailbereich aber liest man so mehr Kontext, ohne die Datei öffnen
 * zu müssen. */
#define SNIPPET_CTX 400
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
            gchar   *snippet_raw = g_strdup_printf("%s%.*s%s",
                    ellipsis_before ? "..." : "",
                    (gint)snip_len, snip_start,
                    ellipsis_after  ? "..." : "");
            gchar   *snippet = normalize_snippet_whitespace(snippet_raw);
            g_free(snippet_raw);

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
            "SELECT DISTINCT filename FROM pages"
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
 * Semantische Suche (Embeddings) - brute-force Cosine-Similarity in C,
 * keine sqlite-vec/vec0-Abhängigkeit (siehe embedding-Spalte in chunks).
 * ======================================================================= */

#ifdef SOND_WITH_EMBEDDINGS
static gdouble cosine_similarity(gfloat const *a, gfloat const *b, gint n) {
    gdouble dot = 0.0, norm_a = 0.0, norm_b = 0.0;

    for (gint i = 0; i < n; i++) {
        dot    += (gdouble) a[i] * (gdouble) b[i];
        norm_a += (gdouble) a[i] * (gdouble) a[i];
        norm_b += (gdouble) b[i] * (gdouble) b[i];
    }

    if (norm_a <= 0.0 || norm_b <= 0.0)
        return 0.0;

    return dot / (sqrt(norm_a) * sqrt(norm_b));
}

/* GCompareFunc für g_ptr_array_sort(): Elemente sind Zeiger auf die
 * Array-Einträge (also SondIndexHit**), absteigend nach score. */
static gint hit_score_compare(gconstpointer a, gconstpointer b) {
    SondIndexHit *ha = *(SondIndexHit * const *) a;
    SondIndexHit *hb = *(SondIndexHit * const *) b;

    if (ha->score < hb->score) return 1;
    if (ha->score > hb->score) return -1;
    return 0;
}
#endif

GPtrArray* sond_index_semantic_search(SondIndexCtx *ctx,
                                       gchar const  *query,
                                       gint          top_k,
                                       GError      **error) {
    g_return_val_if_fail(ctx   != NULL, NULL);
    g_return_val_if_fail(query != NULL, NULL);

    if (top_k <= 0)
        top_k = 15;

#ifndef SOND_WITH_EMBEDDINGS
    (void) top_k;
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
            "sond_index_semantic_search: ohne SOND_WITH_EMBEDDINGS gebaut");
    return NULL;
#else
    GPtrArray    *result = NULL;
    sqlite3_stmt *stmt   = NULL;
    gfloat       *qvec   = NULL;
    gint          rc     = 0;

    if (!sond_index_ctx_has_embeddings(ctx)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "sond_index_semantic_search: kein Embedding-Modell geladen "
                "(Datei fehlt oder nicht konfiguriert)");
        return NULL;
    }

    qvec = compute_embedding(ctx, NULL, NULL, query);
    if (!qvec) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "sond_index_semantic_search: Embedding der Anfrage fehlgeschlagen");
        return NULL;
    }

    result = g_ptr_array_new_with_free_func(sond_index_hit_free);

    /* char_pos_in_page wie bei sond_index_search(): Offset relativ zum
     * Seitenanfang, nicht zum Dateianfang - für Navigation/Highlight im
     * Viewer gebraucht (siehe zond_indexsuche_row_activated()). */
    rc = sqlite3_prepare_v2(ctx->db,
            "SELECT filename, page_nr, char_pos,"
            "       char_pos - (SELECT MIN(c2.char_pos) FROM chunks c2"
            "                   WHERE c2.filename = chunks.filename"
            "                     AND c2.page_nr  = chunks.page_nr),"
            "       text, embedding"
            " FROM chunks WHERE embedding IS NOT NULL",
            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "sond_index_semantic_search: prepare: %s", sqlite3_errmsg(ctx->db));
        g_free(qvec);
        g_ptr_array_unref(result);
        return NULL;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        gint blob_bytes = sqlite3_column_bytes(stmt, 5);

        /* Fremdes/verwaistes Embedding (z.B. Rest eines früheren, anderen
         * Modells - siehe sond_index_ctx_embedding_model_changed()/Re-
         * Embedding) mit falscher Dimension überspringen, statt mit
         * falscher Länge zu vergleichen. */
        if (blob_bytes != ctx->n_embd * (gint) sizeof(gfloat))
            continue;

        gfloat const *cvec = (gfloat const *) sqlite3_column_blob(stmt, 5);
        gdouble       score = cosine_similarity(qvec, cvec, ctx->n_embd);

        gchar const *chunk_text       = (gchar const *) sqlite3_column_text(stmt, 4);
        gint         char_pos_in_page = sqlite3_column_int(stmt, 3);
        gchar       *snippet          = NULL;

#define SEMANTIC_SNIPPET_LEN 600
        if (chunk_text) {
            gsize len = strlen(chunk_text);
            if (len > SEMANTIC_SNIPPET_LEN) {
                gchar const *end = chunk_text + SEMANTIC_SNIPPET_LEN;
                /* nicht mitten in einem UTF-8-Zeichen abschneiden */
                while (end > chunk_text && ((guchar) *end & 0xC0) == 0x80)
                    end--;
                snippet = g_strdup_printf("%.*s...", (gint)(end - chunk_text), chunk_text);
            } else
                snippet = g_strdup(chunk_text);

            if (snippet) {
                gchar *snippet_norm = normalize_snippet_whitespace(snippet);
                g_free(snippet);
                snippet = snippet_norm;
            }
        }

        SondIndexHit *hit = g_new0(SondIndexHit, 1);
        hit->filename         = g_strdup((gchar const *) sqlite3_column_text(stmt, 0));
        hit->page_nr          = sqlite3_column_int(stmt, 1);
        hit->char_pos         = sqlite3_column_int(stmt, 2);
        hit->char_pos_in_page = char_pos_in_page;
        hit->snippet          = snippet;
        hit->score            = score;

        g_ptr_array_add(result, hit);
    }

    sqlite3_finalize(stmt);
    g_free(qvec);

    if (rc != SQLITE_DONE) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "sond_index_semantic_search: step: %s", sqlite3_errmsg(ctx->db));
        g_ptr_array_unref(result);
        return NULL;
    }

    g_ptr_array_sort(result, hit_score_compare);

    if (result->len > (guint) top_k)
        g_ptr_array_remove_range(result, top_k, result->len - top_k);

    return result;
#endif
}

/* =======================================================================
 * pages-Tabelle: Hilfsfunktionen
 * ======================================================================= */

/* Trägt (filename, page_nr) mit ocr_mode in pages ein, oder aktualisiert
 * ocr_mode, falls die Seite schon einen Eintrag hat (z.B. war sie zuerst
 * mit "kein OCR" markiert und wird jetzt mit "prüfen" neu verarbeitet). */
static void sond_index_page_set(SondIndexCtx *ctx, gchar const *filename,
        gint page_nr, gint ocr_mode) {
    sqlite3_stmt *stmt = NULL;

    gint rc = sqlite3_prepare_v2(ctx->db,
            "INSERT INTO pages(filename, page_nr, ocr_mode) VALUES(?,?,?)"
            " ON CONFLICT(filename, page_nr) DO UPDATE SET ocr_mode = excluded.ocr_mode",
            -1, &stmt, NULL);
    if (rc != SQLITE_OK)
        return;

    sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt, 2, page_nr);
    sqlite3_bind_int (stmt, 3, ocr_mode);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

/* =======================================================================
 * sond_server_index
 * ======================================================================= */

void sond_index(fz_context* ctx,
		void (*log_func)(void*, gchar const*, ...), gpointer log_func_data,
		SondIndexCtx  *sond_index_ctx, gchar const* filename, guchar const  *buf,
		gsize size, gchar const *mime_type,
		gint seite_von, gint seite_bis, gint ocr_mode) {
    if (!sond_index_ctx) return;
    if (!mime_type) return;

    /* Segmente extrahieren */
    GPtrArray *segs = NULL;

    if (!g_strcmp0(mime_type, "application/pdf"))
        segs = sond_text_extract_pdf(ctx, buf, size,
        		(SondLogFunc) log_func, log_func_data, seite_von, seite_bis);
    else if (!g_strcmp0(mime_type, "message/rfc822"))
        segs = sond_text_extract_gmessage(buf, size);
    else if (!g_strcmp0(mime_type, "text/html"))
        segs = sond_text_extract_html(buf, size);
    else if (!g_strcmp0(mime_type,
    		"application/vnd.openxmlformats-officedocument.wordprocessingml.document"))
        segs = sond_text_extract_docx(buf, size, NULL);
    else if (!g_strcmp0(mime_type, "application/vnd.oasis.opendocument.text"))
        segs = sond_text_extract_odt(buf, size, NULL);
    else if (g_str_has_prefix(mime_type, "text/"))
        segs = sond_text_extract_plain(buf, size);
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
        SondTextSegment *seg = g_ptr_array_index(segs, s);

        /* Doppelte Arbeit vermeiden: wenn diese Seite bereits mit
         * demselben oder höherem OCR-Modus indiziert wurde (z.B. weil
         * zwei ausgewählte Punkte sich überschneidende Seiten derselben
         * Datei referenzieren, oder ein früherer Lauf sie schon erledigt
         * hat), bleiben ihre vorhandenen Chunks unangetastet. */
        if (!sond_index_ctx_should_process_page(sond_index_ctx, filename,
                seg->page_nr, ocr_mode))
            continue;

        /* Vorhandene Chunks dieser Seite entfernen, bevor sie neu
         * eingefügt werden (Löschen-vor-Einfügen wie zuvor auf
         * Datei-Ebene, jetzt auf Seiten-Ebene) - verhindert doppelte
         * Chunks, wenn die Seite zuvor schon (mit niedrigerem Modus oder
         * in einem früheren Lauf) indiziert war. */
        {
            GError *clear_error = NULL;
            if (!sond_index_ctx_clear_page(sond_index_ctx, filename,
                    seg->page_nr, &clear_error)) {
                if (log_func)
                    log_func(log_func_data,
                            "sond_index: clear_page '%s' Seite %d: %s",
                            filename, seg->page_nr,
                            clear_error ? clear_error->message : "unknown");
                g_clear_error(&clear_error);
            }
        }

        GPtrArray *chunks = text_to_chunks(seg->text,
        		sond_index_ctx->chunk_size,
				sond_index_ctx->chunk_overlap);

        /* Fortschritt melden: ohne dies bleibt die Anzeige bei einer
         * einzelnen großen Datei (z.B. 200-Seiten-PDF) über die gesamte
         * Dauer der Embedding-Berechnung hinweg auf "Entering File '...'"
         * stehen - Chunk-für-Chunk kann Minuten dauern, ohne daß der Nutzer
         * erkennen kann, ob noch gearbeitet wird oder alles hängt. Eine
         * Meldung pro Seite (nicht pro Chunk, das wäre zu viel Grafik-
         * Update-Overhead) reicht als sichtbarer Lebenszeichen-Takt. */
        if (log_func)
            log_func(log_func_data,
                    "Indiziere '%s': Seite %d (%u/%u), %u Chunk(s) ...",
                    filename, seg->page_nr + 1, s + 1, segs->len, chunks->len);

        for (guint i = 0; i < chunks->len; i++) {
            SondChunk   *chunk = g_ptr_array_index(chunks, i);
            gfloat *embedding  = compute_embedding(sond_index_ctx,
                    log_func, log_func_data, chunk->text);
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

        /* Seite als (mit diesem Modus) indiziert markieren */
        sond_index_page_set(sond_index_ctx, filename, seg->page_nr, ocr_mode);
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
}
