/*
 sond (sond_index.c) - Akten, Beweisstücke, Unterlagen
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

#include <gmime/gmime.h>

#include "sond_text_extract.h"
#include "sond_ocr.h"
#include "sond_log_and_error.h"
#include "sond_file_helper.h"
#include "sond_gmessage_helper.h"

#ifdef SOND_WITH_EMBEDDINGS
#include <llama.h>
#include <ggml-backend.h>
#endif

#define INDEX_DEFAULT_CHUNK_SIZE    1000
#define INDEX_DEFAULT_CHUNK_OVERLAP  100

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

/* Ohne diesen Index laufen clear_page(), delete_index() und die Suche
 * (Seitenanfang je Treffer) als Vollscan über chunks. */
static const gchar *SQL_CREATE_CHUNKS_IDX =
    "CREATE INDEX IF NOT EXISTS chunks_file_page_idx"
    " ON chunks(filename, page_nr, char_pos);";

/* Pfad-Präfix ohne LIKE (dessen Wildcards "_" und "%" und die
 * Groß-/Kleinschreibung passen nicht zu Pfaden). Bereich statt SUBSTR, damit
 * der Primärschlüsselindex genutzt wird: '/' ist 0x2F, '0' das nächste
 * Zeichen. SQL_UNDER: alles unter ?1 ("?1/..." also auch "?1//..."),
 * SQL_UNDER_EMB: nur eingebettete Teile ("?1//..."). */
#define SQL_UNDER(col)     "(" col " >= ?1 || '/' AND " col " < ?1 || '0')"
#define SQL_UNDER_EMB(col) "(" col " >= ?1 || '//' AND " col " < ?1 || '/0')"

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

static const gchar *SQL_CREATE_COVERAGE =
    "CREATE TABLE IF NOT EXISTS coverage ("
    "  path      TEXT    PRIMARY KEY,"
    "  ocr_mode  INTEGER NOT NULL"
    ");"; /* Coalescierte Abdeckungs-Angaben, eine Ebene oberhalb von
           * "pages": ein Eintrag "path -> ocr_mode" bedeutet "path (Datei
           * oder Verzeichnis) und alles darunter ist vollständig mit
           * mindestens diesem Modus indiziert". Sobald alle Seiten einer
           * Datei (pages) bzw. alle Elemente eines Verzeichnisses
           * (coverage) denselben oder einen stärkeren Modus erreicht
           * haben, werden ihre feineren Einzeleinträge gelöscht und durch
           * einen einzigen Eintrag hier ersetzt (Coalescing) - dieselbe
           * Hierarchie wie Seite -> Datei -> Verzeichnis -> Projekt, nur
           * aus praktischen Gründen (unterschiedliche natürliche
           * Schlüssel: Seitenzahl vs. Pfad) auf zwei Tabellen verteilt.
           * "erzwingen" (SOND_OCR_MODE_FORCE) fragt diese Tabelle nie ab
           * (reindiziert immer bedingungslos, s.
           * sond_index_ctx_should_process_page) - ein einzelner
           * (schwächster gemeinsamer) Modus pro Eintrag reicht daher aus,
           * ohne Information zu verlieren, die für NONE/CHECK-Anfragen
           * relevant wäre. */

static const gchar *SQL_CREATE_PAGECOUNT =
    "CREATE TABLE IF NOT EXISTS file_pagecount ("
    "  filename    TEXT    PRIMARY KEY,"
    "  total_pages INTEGER NOT NULL"
    ");"; /* Zuletzt bekannte GESAMTE Seitenzahl einer PDF-Datei -
           * unabhängig vom Coalescing-Zustand von "coverage"/"pages"
           * (wird NIE automatisch durch coverage_mark()/_try_collapse()
           * gelöscht oder verändert, im Unterschied zu diesen beiden).
           * Einzige Quelle, um nach vollständigem Coalescing (einzelne
           * "pages"-Zeilen sind dann weg) noch zu wissen, wie viele
           * Seiten eine Datei hat, ohne sie erneut zu öffnen (SeaDrive-
           * Hydrierung vermeiden) - wird gebraucht, um beim Löschen
           * einzelner Seiten aus dem Index (sond_index_ctx_delete_index())
           * die ÜBRIGEN Seiten korrekt als weiterhin indiziert
           * wiederherzustellen. Befüllt bei vollständiger Indizierung
           * (sond_index(), echte Seitenzahl aus sond_text_extract_pdf(),
           * NICHT die Anzahl gelieferter Segmente - Seiten ohne
           * extrahierbaren Text liefern kein Segment), aktualisiert bei
           * Seiten-Einfügen/-Löschen im Viewer (viewer_save.c), gelöscht
           * nur wenn die Datei komplett aus dem Index entfernt wird
           * (sond_index_ctx_clear_file()/delete_index() bei ganzer
           * Datei). */

static const gchar *SQL_CREATE_ENTRYCOUNT =
    "CREATE TABLE IF NOT EXISTS container_entrycount ("
    "  filename      TEXT    PRIMARY KEY,"
    "  total_entries INTEGER NOT NULL"
    ");"; /* Zuletzt bekannte Anzahl der internen Einträge eines Container-
           * Formats (ZIP-Archiv, E-Mail mit Anhängen, PDF mit eingebetteten
           * Dateien) - dieselbe Rolle wie file_pagecount, nur "Eintrag"
           * statt "Seite" als Einheit. Das Durchsuchen des Index
           * soll nie eine Datei öffnen müssen (SeaDrive-Hydrierung) - ohne
           * diese Tabelle gäbe es keine Möglichkeit, "X von Y Einträgen
           * fehlen" für einen Container zu ermitteln. Befüllt beim
           * Indizieren (s. sond_index_ctx_set_entry_count()). Keine
           * mtime/Größe nötig: Änderungen an Container-Interna finden nur
           * über zond selbst statt und werden dabei über die bestehende
           * Invalidierung erfasst - der DB-Stand gilt daher immer als
           * aktuell. Eigene Tabelle statt gemeinsam mit file_pagecount:
           * unterschiedliche Einheit (Seiten vs. Einträge). */

static const gchar *SQL_CREATE_GMSG_INLINE =
    "CREATE TABLE IF NOT EXISTS gmessage_inline ("
    "  filename TEXT PRIMARY KEY,"
    "  parts    TEXT NOT NULL"
    ");"; /* Inline-Teile einer E-Mail (alle Mimeparts ohne Content-
           * Disposition "attachment", die sich indizieren lassen), als
           * "\n"-getrennte Pfade relativ zur Mail ("0", "0/1"). Leerer
           * String: keine. Eine angebundene Mail (BAUM_INHALT/_AUSWERTUNG)
           * steht für Header + diese Teile - Badge und
           * Index durchsuchen brauchen die Liste, ohne die Mail zu öffnen.
           * Befüllt bei jeder Indizierung einer Mail (sond_index()),
           * gelöscht mit container_entrycount. */

static const gchar *SQL_CREATE_PDF_EMBEDDED =
    "CREATE TABLE IF NOT EXISTS pdf_embedded ("
    "  filename  TEXT PRIMARY KEY,"
    "  addresses TEXT NOT NULL"
    ");"; /* Adressen der eingebetteten Dateien einer PDF ("\n"-getrennt, s.
           * pdf_emb_addresses_new()), leerer String: keine. Damit kann die
           * Coverage einer PDF in Seiten ("x.pdf//") und Anhänge aufgelöst
           * und wieder zu "x.pdf" zusammengefasst werden, ohne die PDF zu
           * öffnen. Befüllt, sobald irgendein Teil der PDF
           * verarbeitet wird, und nach jeder Änderung an ihren Anhängen. */

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

    rc = sqlite3_exec(ctx->db, SQL_CREATE_CHUNKS_IDX, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "db_init_schema: CREATE INDEX chunks: %s", errmsg);
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

    rc = sqlite3_exec(ctx->db, SQL_CREATE_COVERAGE, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "db_init_schema: CREATE coverage: %s", errmsg);
        sqlite3_free(errmsg);
        return FALSE;
    }

    rc = sqlite3_exec(ctx->db, SQL_CREATE_PAGECOUNT, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "db_init_schema: CREATE file_pagecount: %s", errmsg);
        sqlite3_free(errmsg);
        return FALSE;
    }

    rc = sqlite3_exec(ctx->db, SQL_CREATE_ENTRYCOUNT, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "db_init_schema: CREATE container_entrycount: %s", errmsg);
        sqlite3_free(errmsg);
        return FALSE;
    }

    rc = sqlite3_exec(ctx->db, SQL_CREATE_GMSG_INLINE, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "db_init_schema: CREATE gmessage_inline: %s", errmsg);
        sqlite3_free(errmsg);
        return FALSE;
    }

    rc = sqlite3_exec(ctx->db, SQL_CREATE_PDF_EMBEDDED, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "db_init_schema: CREATE pdf_embedded: %s", errmsg);
        sqlite3_free(errmsg);
        return FALSE;
    }

    return TRUE;
}

/* =======================================================================
 * meta-Tabelle: einfacher Schlüssel-Wert-Speicher
 * ======================================================================= */

#ifdef SOND_WITH_EMBEDDINGS
/* Aktuell ausschließlich für Embeddings genutzt (embedding_model/
 * embedding_dim, s.u.) - deshalb hier mit eingerahmt, sonst "unused
 * function" ohne Embeddings-Build. Falls die meta-Tabelle künftig auch
 * embeddings-unabhängig gebraucht wird, hier wieder herausziehen. */
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
#endif

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

    /* Ist die Datei kurz gesperrt (zweite Verbindung, Sync-Client), bis zu 5
     * Sekunden warten statt sofort mit "database is locked" zu scheitern */
    sqlite3_busy_timeout(ctx->db, 5000);

    /* Kein WAL: .sond_index.db liegt im SeaDrive-synchronisierten Projekt-
     * verzeichnis - WAL braucht
     * verlässliches mmap/Byte-Range-Locking auf der -shm-Datei, was ein
     * Cloud-Sync-Laufwerk nicht zuverlässig bietet ("database disk image is
     * malformed"). Klassisches
     * Rollback-Journal (DELETE) + synchronous=FULL ist auf einem solchen
     * Laufwerk das robustere, wenn auch langsamere Verhalten. */
    {
        gint rc_pragma = 0;

        rc_pragma = sqlite3_exec(ctx->db, "PRAGMA journal_mode=DELETE;", NULL,
                NULL, NULL);
        if (rc_pragma != SQLITE_OK)
            g_warning("sond_index_ctx_new: PRAGMA journal_mode=DELETE "
                    "fehlgeschlagen: %s", sqlite3_errmsg(ctx->db));

        rc_pragma = sqlite3_exec(ctx->db, "PRAGMA synchronous=FULL;", NULL,
                NULL, NULL);
        if (rc_pragma != SQLITE_OK)
            g_warning("sond_index_ctx_new: PRAGMA synchronous=FULL "
                    "fehlgeschlagen: %s", sqlite3_errmsg(ctx->db));
    }

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

    /* vor dem Schließen: sonst scheitert sqlite3_close() an der offenen
     * Anweisung */
    if (ctx->stmt_insert_chunk)
        sqlite3_finalize(ctx->stmt_insert_chunk);

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
    sqlite3_stmt *stmt = NULL;

    /* filename selbst und alles darunter: Unterverzeichnisse und Dateien in
     * einem gelöschten Ordner ("x/...") wie eingebettete Teile ("x//...") */
    gint rc = sqlite3_prepare_v2(ctx->db,
            "DELETE FROM chunks WHERE filename = ?1 OR " SQL_UNDER("filename"),
            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "sond_index_ctx_clear_file: prepare: %s",
                    sqlite3_errmsg(ctx->db));
        return FALSE;
    }

    sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_TRANSIENT);
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
            "DELETE FROM pages WHERE filename = ?1 OR " SQL_UNDER("filename"),
            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "sond_index_ctx_clear_file: prepare pages: %s",
                    sqlite3_errmsg(ctx->db));
        return FALSE;
    }

    sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "sond_index_ctx_clear_file: step pages: %s",
                    sqlite3_errmsg(ctx->db));
        return FALSE;
    }

    /* Datei existiert nicht mehr im Index - die zuletzt bekannte
     * interne Einträgeanzahl (container_entrycount), falls filename ein
     * Container war, ist damit ebenfalls hinfällig. */
    if (!sond_index_ctx_clear_entry_count(ctx, filename, error))
        return FALSE;

    /* Datei existiert nicht mehr im Index - die zuletzt bekannte
     * Seitenzahl (file_pagecount) ist damit ebenfalls hinfällig. */
    if (!sond_index_ctx_clear_page_count(ctx, filename, error))
        return FALSE;

    return TRUE;
}

/* =======================================================================
 * sond_index_ctx_set_page_count / _get_page_count / _clear_page_count
 * ======================================================================= */

gboolean sond_index_ctx_set_page_count(SondIndexCtx *ctx, gchar const *filename,
        gint total_pages, GError **error) {
    sqlite3_stmt *stmt = NULL;

    if (!ctx || !filename)
        return TRUE;

    if (sqlite3_prepare_v2(ctx->db,
            "INSERT INTO file_pagecount(filename, total_pages) VALUES(?, ?)"
            " ON CONFLICT(filename) DO UPDATE SET total_pages = excluded.total_pages",
            -1, &stmt, NULL) != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: prepare: %s", __func__, sqlite3_errmsg(ctx->db));
        return FALSE;
    }
    sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 2, total_pages);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: step: %s", __func__, sqlite3_errmsg(ctx->db));
        sqlite3_finalize(stmt);
        return FALSE;
    }
    sqlite3_finalize(stmt);

    return TRUE;
}

gint sond_index_ctx_get_page_count(SondIndexCtx *ctx, gchar const *filename) {
    sqlite3_stmt *stmt   = NULL;
    gint          result = -1;

    if (!ctx || !filename)
        return -1;

    if (sqlite3_prepare_v2(ctx->db,
            "SELECT total_pages FROM file_pagecount WHERE filename = ?",
            -1, &stmt, NULL) != SQLITE_OK)
        return -1;

    sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW)
        result = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    return result;
}

gboolean sond_index_ctx_clear_page_count(SondIndexCtx *ctx,
        gchar const *filename, GError **error) {
    sqlite3_stmt *stmt = NULL;

    if (!ctx || !filename)
        return TRUE;

    if (sqlite3_prepare_v2(ctx->db,
            "DELETE FROM file_pagecount WHERE filename = ?1 OR "
            SQL_UNDER("filename"),
            -1, &stmt, NULL) != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: prepare: %s", __func__, sqlite3_errmsg(ctx->db));
        return FALSE;
    }
    sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: step: %s", __func__, sqlite3_errmsg(ctx->db));
        sqlite3_finalize(stmt);
        return FALSE;
    }
    sqlite3_finalize(stmt);

    return TRUE;
}

/* =======================================================================
 * sond_index_ctx_set_entry_count / _get_entry_count / _clear_entry_count
 * ======================================================================= */

gboolean sond_index_ctx_set_entry_count(SondIndexCtx *ctx, gchar const *filename,
        gint total_entries, GError **error) {
    sqlite3_stmt *stmt = NULL;

    if (!ctx || !filename)
        return TRUE;

    if (sqlite3_prepare_v2(ctx->db,
            "INSERT INTO container_entrycount(filename, total_entries) VALUES(?, ?)"
            " ON CONFLICT(filename) DO UPDATE SET total_entries = excluded.total_entries",
            -1, &stmt, NULL) != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: prepare: %s", __func__, sqlite3_errmsg(ctx->db));
        return FALSE;
    }
    sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 2, total_entries);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: step: %s", __func__, sqlite3_errmsg(ctx->db));
        sqlite3_finalize(stmt);
        return FALSE;
    }
    sqlite3_finalize(stmt);

    return TRUE;
}

gint sond_index_ctx_get_entry_count(SondIndexCtx *ctx, gchar const *filename) {
    sqlite3_stmt *stmt   = NULL;
    gint          result = -1;

    if (!ctx || !filename)
        return -1;

    if (sqlite3_prepare_v2(ctx->db,
            "SELECT total_entries FROM container_entrycount WHERE filename = ?",
            -1, &stmt, NULL) != SQLITE_OK)
        return -1;

    sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW)
        result = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    return result;
}

gboolean sond_index_ctx_clear_entry_count(SondIndexCtx *ctx,
        gchar const *filename, GError **error) {
    sqlite3_stmt *stmt = NULL;

    if (!ctx || !filename)
        return TRUE;

    if (sqlite3_prepare_v2(ctx->db,
            "DELETE FROM container_entrycount WHERE filename = ?1 OR "
            SQL_UNDER("filename"),
            -1, &stmt, NULL) != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: prepare: %s", __func__, sqlite3_errmsg(ctx->db));
        return FALSE;
    }
    sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: step: %s", __func__, sqlite3_errmsg(ctx->db));
        sqlite3_finalize(stmt);
        return FALSE;
    }
    sqlite3_finalize(stmt);

    //Inline-Teile einer Mail, Anhänge einer PDF: gleiche Lebensdauer wie
    //container_entrycount
    {
        static gchar const *sql[] = {
            "DELETE FROM gmessage_inline WHERE filename = ?1 OR "
            "SUBSTR(filename, 1, LENGTH(?1) + 1) = ?1 || '/'",
            "DELETE FROM pdf_embedded WHERE filename = ?1 OR "
            "SUBSTR(filename, 1, LENGTH(?1) + 1) = ?1 || '/'"
        };

        for (guint i = 0; i < G_N_ELEMENTS(sql); i++) {
            if (sqlite3_prepare_v2(ctx->db, sql[i], -1, &stmt, NULL)
                    != SQLITE_OK) {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "%s: prepare: %s", __func__, sqlite3_errmsg(ctx->db));
                return FALSE;
            }
            sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) != SQLITE_DONE) {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "%s: step: %s", __func__, sqlite3_errmsg(ctx->db));
                sqlite3_finalize(stmt);
                return FALSE;
            }
            sqlite3_finalize(stmt);
        }
    }

    return TRUE;
}

/* =======================================================================
 * Anhänge einer PDF
 * ======================================================================= */

gboolean sond_index_ctx_set_pdf_embedded(SondIndexCtx *ctx,
        gchar const *filename, GPtrArray *addresses, GError **error) {
    sqlite3_stmt *stmt   = NULL;
    GString      *joined = g_string_new(NULL);

    if (!ctx || !filename)
        return TRUE;

    for (guint i = 0; addresses && i < addresses->len; i++) {
        if (i)
            g_string_append_c(joined, '\n');
        g_string_append(joined, g_ptr_array_index(addresses, i));
    }

    if (sqlite3_prepare_v2(ctx->db,
            "INSERT INTO pdf_embedded(filename, addresses) VALUES(?, ?)"
            " ON CONFLICT(filename) DO UPDATE SET addresses = excluded.addresses",
            -1, &stmt, NULL) != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: prepare: %s", __func__, sqlite3_errmsg(ctx->db));
        g_string_free(joined, TRUE);
        return FALSE;
    }
    sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, joined->str, -1, SQLITE_TRANSIENT);
    g_string_free(joined, TRUE);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: step: %s", __func__, sqlite3_errmsg(ctx->db));
        sqlite3_finalize(stmt);
        return FALSE;
    }
    sqlite3_finalize(stmt);

    return TRUE;
}

/* Adressen der Anhänge von filename, NULL wenn unbekannt */
static GPtrArray* pdf_embedded_get(SondIndexCtx *ctx, gchar const *filename) {
    sqlite3_stmt *stmt      = NULL;
    GPtrArray    *addresses = NULL;

    if (sqlite3_prepare_v2(ctx->db,
            "SELECT addresses FROM pdf_embedded WHERE filename = ?",
            -1, &stmt, NULL) != SQLITE_OK)
        return NULL;

    sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        gchar const *joined = (gchar const *) sqlite3_column_text(stmt, 0);

        addresses = g_ptr_array_new_with_free_func(g_free);
        if (joined && *joined) {
            gchar **v = g_strsplit(joined, "\n", -1);

            for (gchar **p = v; *p; p++)
                g_ptr_array_add(addresses, g_strdup(*p));
            g_strfreev(v);
        }
    }
    sqlite3_finalize(stmt);

    return addresses;
}

/* =======================================================================
 * Inline-Teile einer E-Mail
 * ======================================================================= */

static gboolean gmessage_inline_set(SondIndexCtx *ctx, gchar const *filename,
        GPtrArray *parts, GError **error) {
    sqlite3_stmt *stmt   = NULL;
    GString      *joined = g_string_new(NULL);

    for (guint i = 0; i < parts->len; i++) {
        if (i)
            g_string_append_c(joined, '\n');
        g_string_append(joined, g_ptr_array_index(parts, i));
    }

    if (sqlite3_prepare_v2(ctx->db,
            "INSERT INTO gmessage_inline(filename, parts) VALUES(?, ?)"
            " ON CONFLICT(filename) DO UPDATE SET parts = excluded.parts",
            -1, &stmt, NULL) != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: prepare: %s", __func__, sqlite3_errmsg(ctx->db));
        g_string_free(joined, TRUE);
        return FALSE;
    }
    sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, joined->str, -1, SQLITE_TRANSIENT);
    g_string_free(joined, TRUE);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: step: %s", __func__, sqlite3_errmsg(ctx->db));
        sqlite3_finalize(stmt);
        return FALSE;
    }
    sqlite3_finalize(stmt);

    return TRUE;
}

GPtrArray* sond_index_ctx_gmessage_message_parts(SondIndexCtx *ctx,
        gchar const *filename, gboolean *known) {
    sqlite3_stmt *stmt  = NULL;
    GPtrArray    *parts = g_ptr_array_new_with_free_func(g_free);

    if (known)
        *known = FALSE;

    g_ptr_array_add(parts, g_strdup_printf("%s//header", filename));

    if (!ctx || sqlite3_prepare_v2(ctx->db,
            "SELECT parts FROM gmessage_inline WHERE filename = ?",
            -1, &stmt, NULL) != SQLITE_OK)
        return parts;

    sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        gchar const *joined = (gchar const *) sqlite3_column_text(stmt, 0);

        if (known)
            *known = TRUE;

        if (joined && *joined) {
            gchar **v = g_strsplit(joined, "\n", -1);

            for (gchar **p = v; *p; p++)
                g_ptr_array_add(parts, g_strdup_printf("%s//%s", filename, *p));
            g_strfreev(v);
        }
    }
    sqlite3_finalize(stmt);

    return parts;
}

SondIndexStatus sond_index_ctx_get_gmessage_message_status(SondIndexCtx *ctx,
        gchar const *filename) {
    GPtrArray *parts = sond_index_ctx_gmessage_message_parts(ctx, filename, NULL);
    guint      full  = 0;
    gboolean   any   = FALSE;

    for (guint i = 0; i < parts->len; i++) {
        SondIndexStatus st = sond_index_ctx_get_file_status(ctx,
                g_ptr_array_index(parts, i), -1, -1);

        if (st == SOND_INDEX_STATUS_FULL)
            full++;
        if (st != SOND_INDEX_STATUS_NONE)
            any = TRUE;
    }

    {
        guint n = parts->len;

        g_ptr_array_unref(parts);

        if (full == n)
            return SOND_INDEX_STATUS_FULL;
        return any ? SOND_INDEX_STATUS_PARTIAL : SOND_INDEX_STATUS_NONE;
    }
}

/* Inline-Teile (s. SQL_CREATE_GMSG_INLINE) rekursiv sammeln - Pfade nach
 * derselben Konvention wie gmessage_process_part() (sond_process_file.c):
 * Kinder eines Multiparts "i" bzw. "eltern/i", ein Nicht-Multipart als
 * Wurzel "0" */
static void gmessage_collect_inline(GMimeObject *object,
        gchar const *internal_path, GPtrArray *out) {
    if (GMIME_IS_MULTIPART(object)) {
        GMimeMultipart *mp = GMIME_MULTIPART(object);

        for (gint i = 0; i < g_mime_multipart_get_count(mp); i++) {
            gchar *child = internal_path ?
                    g_strdup_printf("%s/%d", internal_path, i) :
                    g_strdup_printf("%d", i);

            gmessage_collect_inline(g_mime_multipart_get_part(mp, i), child, out);
            g_free(child);
        }
    }
    else {
        GMimeContentDisposition *disp =
                g_mime_object_get_content_disposition(object);
        gchar const *dval = disp ?
                g_mime_content_disposition_get_disposition(disp) : NULL;
        gchar *mime = NULL;

        if (dval && !g_ascii_strcasecmp(dval, "attachment"))
            return;

        mime = g_mime_content_type_get_mime_type(
                g_mime_object_get_content_type(object));
        if (sond_index_mime_type_supported(mime))
            g_ptr_array_add(out, g_strdup(internal_path ? internal_path : "0"));
        g_free(mime);
    }
}

/* =======================================================================
 * sond_index_ctx_count_nested_indexed
 * ======================================================================= */

gint sond_index_ctx_count_nested_indexed(SondIndexCtx *ctx, gchar const *path) {
    sqlite3_stmt *stmt       = NULL;
    gsize         prefix_len = 0;
    GHashTable   *ht_children = NULL;
    gint          result     = -1;

    if (!ctx || !path)
        return -1;

    prefix_len = strlen(path) + 2; /* Länge von "path//" */

    /* Nested Pfade (Konvention "path//..."), die IRGENDEINEN Hinweis auf
     * Indizierung haben - entweder noch einzelne Zeilen in "pages"
     * (teilweise/gerade erst indiziert) oder schon zu einem eigenen
     * coverage-Eintrag kollabiert (vollständig indiziert, s.
     * coverage_mark()/_try_collapse()). Rein aus der DB, kein
     * Dateizugriff.
     *
     * Auf DIREKTE Kinder (eine Ebene) reduziert: ein Treffer, der noch
     * tiefer verschachtelt ist ("path//kind//enkel...", z.B. ein PDF
     * innerhalb eines bereits als embedded file indizierten Zip-Eintrags),
     * zählt als Beleg für "kind", nicht als eigener Eintrag - passend zu
     * total_entries (container_entrycount), das ebenfalls nur eine Ebene
     * zählt. */
    if (sqlite3_prepare_v2(ctx->db,
            "SELECT DISTINCT filename AS p FROM pages WHERE "
            SQL_UNDER_EMB("filename")
            "  UNION"
            "  SELECT DISTINCT path AS p FROM coverage WHERE "
            SQL_UNDER_EMB("path"),
            -1, &stmt, NULL) != SQLITE_OK)
        return -1;

    sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT);

    ht_children = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        gchar const *p = (gchar const *) sqlite3_column_text(stmt, 0);
        gchar const *rest = NULL;
        gchar const *next_sep = NULL;
        gchar *child = NULL;

        if (!p || strlen(p) <= prefix_len)
            continue;

        rest     = p + prefix_len;
        next_sep = strstr(rest, "//");
        child    = next_sep ? g_strndup(p, (gsize) (next_sep - p)) : g_strdup(p);

        g_hash_table_add(ht_children, child);
    }
    sqlite3_finalize(stmt);

    result = (gint) g_hash_table_size(ht_children);
    g_hash_table_destroy(ht_children);

    return result;
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
 * coverage: coalescierte Abdeckungs-Angaben (Datei-/Verzeichnis-Ebene)
 * ======================================================================= */

/*
 * coverage_get_exact:
 *
 * Wie sond_index_ctx_coverage_get(), aber OHNE den Ahnen-Walk - liefert
 * nur den Modus, wenn path SELBST einen coverage-Eintrag hat, sonst -1.
 * Für das GMessage-bewusste Collapse/Invalidate unten gebraucht: dort werden
 * gezielt die erwarteten Kind-Pfade
 * ("x.eml//header", "x.eml//0", ...) einzeln geprüft - ein Ahnen-Walk
 * würde dabei (bei verschachtelten Containern) unter Umständen fälschlich
 * bei einem GANZ ANDEREN, weiter oben liegenden Vorfahren landen (derselbe
 * Mechanismus, der den ursprünglichen Bug verursacht hat, s.
 * sond_index_ctx_coverage_invalidate()-Kommentar) - hier ist aber
 * ausschließlich der EXAKTE Zustand des jeweiligen Kindes von Interesse.
 */
static gint coverage_get_exact(SondIndexCtx *ctx, gchar const *path) {
    sqlite3_stmt *stmt   = NULL;
    gint          result = -1;

    if (!ctx || !path)
        return -1;

    if (sqlite3_prepare_v2(ctx->db,
            "SELECT ocr_mode FROM coverage WHERE path = ?", -1, &stmt, NULL)
            != SQLITE_OK)
        return -1;

    sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW)
        result = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    return result;
}

/*
 * gmessage_container_boundary:
 *
 * Sucht das LETZTE "//"-Vorkommen in path (die Container-Grenze der
 * unmittelbar umgebenden E-Mail, s. Konvention bei
 * sond_file_part_get_filepart()). Liefert NULL, wenn path kein "//"
 * enthält (reiner Dateisystem-Pfad).
 */
static gchar* gmessage_find_last_boundary(gchar const *path) {
    gchar *last = NULL;
    gchar *p    = (gchar *) path;

    while ((p = strstr(p, "//"))) {
        last = p;
        p += 2;
    }
    return last;
}

/*
 * is_gmessage_child_segment:
 *
 * Prüft, ob segment (der Teil NACH dem letzten "//" in einem coverage-
 * Pfad) zur E-Mail-Adressierung passt: entweder wörtlich "header" oder
 * eine reine, vorzeichenlose Dezimalzahl (ein direkter, root-naher
 * Mimepart-Index, s. gmessage_container_child_keys()). Ein Segment mit
 * "/" (tiefer verschachteltes Multipart, z.B. "0/1") oder mit
 * beliebigen anderen Zeichen (ein echter, per Namen adressierter Eintrag
 * z.B. innerhalb eines ZIP-Containers, dessen "//"-Konvention NICHTS mit
 * der E-Mail-Header/Mimepart-Zählung hier zu tun hat) liefert FALSE -
 * das GMessage-bewusste Collapse/Invalidate greift dann bewusst NICHT,
 * die Verarbeitung fällt auf das bisherige (unveränderte) Verhalten
 * zurück. Tiefer verschachtelte Multiparts sind damit (noch) nicht
 * Teil des Collapse - bewusste Einschränkung. */
static gboolean is_gmessage_child_segment(gchar const *segment) {
    if (!segment || !*segment)
        return FALSE;

    if (!g_strcmp0(segment, "header"))
        return TRUE;

    for (gchar const *p = segment; *p; p++)
        if (!g_ascii_isdigit(*p))
            return FALSE;

    return TRUE;
}

/*
 * gmessage_container_child_keys:
 *
 * Liefert die vollständige, erwartete Liste der Kind-coverage-Pfade eines
 * GMessage-Containers (container, OHNE trailing "//..."): ein virtueller
 * "container//header"-Slot plus je ein
 * "container//0" .. "container//(N-1)" für die N direkten Mimeparts, N =
 * container_entrycount(container) - 1 (die dort hinterlegte Gesamtzahl
 * zählt den Header-Slot mit, s. gmessage_count_root_entries() in
 * sond_index()). NULL, wenn container_entrycount für container (noch)
 * nicht bekannt ist - dann kann nicht sicher aufgezählt werden.
 */
static GPtrArray* gmessage_container_child_keys(SondIndexCtx *ctx,
        gchar const *container) {
    gint total_entries = sond_index_ctx_get_entry_count(ctx, container);
    GPtrArray *keys = NULL;

    if (total_entries < 1)
        return NULL;

    keys = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(keys, g_strdup_printf("%s//header", container));
    for (gint i = 0; i < total_entries - 1; i++)
        g_ptr_array_add(keys, g_strdup_printf("%s//%d", container, i));

    return keys;
}

/*
 * sond_index_ctx_coverage_get:
 *
 * Liefert den Modus, mit dem path (oder der nächstgelegene abdeckende
 * Vorfahre - path selbst muß keinen eigenen Eintrag haben) als vollständig
 * indiziert vermerkt ist, oder -1, wenn nichts gefunden wird (weder path
 * selbst noch irgendein Vorfahre ist abgedeckt).
 *
 * Geht dazu von path aus schrittweise nach oben (an '/' getrennt) und
 * prüft bei jeder Ebene per Primärschlüssel-Lookup, ob ein coverage-
 * Eintrag existiert - kein SQL-Prefix-Scan nötig, da die Pfadtiefe klein
 * und beschränkt ist.
 */
gint sond_index_ctx_coverage_get(SondIndexCtx *ctx, gchar const *path) {
    sqlite3_stmt *stmt   = NULL;
    gchar        *probe  = NULL;
    gint          result = -1;

    if (!ctx || !path)
        return -1;

    if (sqlite3_prepare_v2(ctx->db,
            "SELECT ocr_mode FROM coverage WHERE path = ?", -1, &stmt, NULL)
            != SQLITE_OK)
        return -1;

    probe = g_strdup(path);
    for (;;) {
        gchar *slash = NULL;

        sqlite3_reset(stmt);
        sqlite3_bind_text(stmt, 1, probe, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result = sqlite3_column_int(stmt, 0);
            break;
        }

        slash = strrchr(probe, '/');
        if (!slash)
            break; /* oberste Ebene erreicht, nichts gefunden */
        *slash = '\0';
    }

    g_free(probe);
    sqlite3_finalize(stmt);
    return result;
}

SondIndexStatus sond_index_ctx_get_file_status(SondIndexCtx *ctx,
        gchar const *filename, gint von_seite, gint bis_seite) {
    if (!ctx || !filename)
        return SOND_INDEX_STATUS_NONE;

    if (sond_index_ctx_coverage_get(ctx, filename) >= 0)
        return SOND_INDEX_STATUS_FULL;

    /* Seiten-Eintrag "filename//" (PDF, nur Seiten - s. sond_index()):
     * Status der Seiten bzw. eines Seitenbereichs, nicht der Einbettungen. */
    {
        gchar *pages_path = g_strdup_printf("%s//", filename);
        gint mode = coverage_get_exact(ctx, pages_path);

        g_free(pages_path);
        if (mode >= 0)
            return SOND_INDEX_STATUS_FULL;
    }

    if (von_seite < 0) {
        /* Ganze Datei (auch Nicht-PDF, dort page_nr immer -1): ohne die
         * Datei zu öffnen reicht hier die Existenzfrage, um NONE von
         * PARTIAL zu unterscheiden - FULL wurde oben bereits über die
         * coverage-Tabelle ausgeschlossen bzw. bestätigt. */
        sqlite3_stmt *stmt = NULL;
        gboolean any = FALSE;

        if (sqlite3_prepare_v2(ctx->db,
                "SELECT 1 FROM pages WHERE filename = ? LIMIT 1",
                -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_TRANSIENT);
            any = (sqlite3_step(stmt) == SQLITE_ROW);
            sqlite3_finalize(stmt);
        }

        return any ? SOND_INDEX_STATUS_PARTIAL : SOND_INDEX_STATUS_NONE;
    }

    /* Anbindung mit explizitem Seitenbereich: zählen, wie viele der
     * angeforderten Seiten schon einen pages-Eintrag haben. */
    {
        GArray     *indexed = sond_index_ctx_get_pages_for_file(ctx, filename);
        GHashTable *set     = g_hash_table_new(NULL, NULL);
        gint        found   = 0;
        gint        total   = bis_seite - von_seite + 1;

        for (guint i = 0; i < indexed->len; i++)
            g_hash_table_add(set,
                    GINT_TO_POINTER(g_array_index(indexed, gint, i)));

        for (gint p = von_seite; p <= bis_seite; p++)
            if (g_hash_table_contains(set, GINT_TO_POINTER(p)))
                found++;

        g_hash_table_destroy(set);
        g_array_free(indexed, TRUE);

        if (found <= 0)
            return SOND_INDEX_STATUS_NONE;
        if (total > 0 && found >= total)
            return SOND_INDEX_STATUS_FULL;
        return SOND_INDEX_STATUS_PARTIAL;
    }
}

SondIndexStatus sond_index_ctx_get_dir_status(SondIndexCtx *ctx,
        gchar const *path) {
    sqlite3_stmt *stmt = NULL;
    gboolean      any  = FALSE;

    if (!ctx || !path)
        return SOND_INDEX_STATUS_NONE;

    if (sond_index_ctx_coverage_get(ctx, path) >= 0)
        return SOND_INDEX_STATUS_FULL;

    if (sqlite3_prepare_v2(ctx->db,
            "SELECT EXISTS(SELECT 1 FROM pages WHERE " SQL_UNDER("filename") ")"
            " OR EXISTS(SELECT 1 FROM coverage WHERE " SQL_UNDER("path") ")",
            -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            any = (sqlite3_column_int(stmt, 0) != 0);
        sqlite3_finalize(stmt);
    }

    return any ? SOND_INDEX_STATUS_PARTIAL : SOND_INDEX_STATUS_NONE;
}

/*
 * sond_index_ctx_coverage_mark:
 *
 * Markiert path (Datei oder Verzeichnis) als vollständig mit ocr_mode
 * indiziert. Löscht dabei automatisch:
 *  - alle jetzt überflüssigen, feineren coverage-Einträge UNTERHALB von
 *    path (falls path ein Verzeichnis ist, dessen Kinder vorher einzeln
 *    eingetragen waren),
 *  - alle pages-Einträge für path selbst bzw. für Pfade, die mit
 *    path + "//" beginnen (eingebettete Inhalte, z.B. Anhänge einer .eml -
 *    dieselbe Konvention wie in sond_index_ctx_clear_file()), falls path
 *    eine Datei ist, deren Seiten vorher einzeln in "pages" standen.
 *
 * Ersetzt einen eventuell schon vorhandenen Eintrag für path selbst
 * (INSERT OR REPLACE).
 */
static gboolean db_savepoint(SondIndexCtx *ctx, GError **error);
static void db_savepoint_end(SondIndexCtx *ctx, gboolean ok);

static gboolean coverage_mark_impl(SondIndexCtx *ctx, gchar const *path,
        gint ocr_mode, GError **error);

gboolean sond_index_ctx_coverage_mark(SondIndexCtx *ctx, gchar const *path,
        gint ocr_mode, GError **error) {
    gboolean ok = FALSE;

    if (!ctx || !path)
        return coverage_mark_impl(ctx, path, ocr_mode, error);

    /* Löschen der feineren Einträge und Eintragen von path nur zusammen */
    if (!db_savepoint(ctx, error))
        return FALSE;
    ok = coverage_mark_impl(ctx, path, ocr_mode, error);
    db_savepoint_end(ctx, ok);

    return ok;
}

static gboolean coverage_mark_impl(SondIndexCtx *ctx, gchar const *path,
        gint ocr_mode, GError **error) {
    sqlite3_stmt *stmt = NULL;
    gint          rc   = 0;

    if (!ctx || !path) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: ctx/path fehlt", __func__);
        return FALSE;
    }

    if (sqlite3_prepare_v2(ctx->db,
            "DELETE FROM coverage WHERE path = ?1 OR " SQL_UNDER("path"),
            -1, &stmt, NULL) != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: prepare DELETE coverage: %s", __func__,
                sqlite3_errmsg(ctx->db));
        return FALSE;
    }
    sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    stmt = NULL;
    if (rc != SQLITE_DONE) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: DELETE coverage '%s': %s", __func__, path,
                sqlite3_errmsg(ctx->db));
        return FALSE;
    }

    if (sqlite3_prepare_v2(ctx->db,
            "DELETE FROM pages WHERE filename = ?1 OR "
            SQL_UNDER_EMB("filename"),
            -1, &stmt, NULL) != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: prepare DELETE pages: %s", __func__,
                sqlite3_errmsg(ctx->db));
        return FALSE;
    }
    sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    stmt = NULL;
    if (rc != SQLITE_DONE) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: DELETE pages '%s': %s", __func__, path,
                sqlite3_errmsg(ctx->db));
        return FALSE;
    }

    if (sqlite3_prepare_v2(ctx->db,
            "INSERT INTO coverage(path, ocr_mode) VALUES(?, ?)"
            " ON CONFLICT(path) DO UPDATE SET ocr_mode = excluded.ocr_mode",
            -1, &stmt, NULL) != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: prepare INSERT coverage: %s", __func__,
                sqlite3_errmsg(ctx->db));
        return FALSE;
    }
    sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 2, ocr_mode);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: INSERT coverage '%s': %s", __func__, path,
                sqlite3_errmsg(ctx->db));
        sqlite3_finalize(stmt);
        return FALSE;
    }
    sqlite3_finalize(stmt);

    return TRUE;
}

/*
 * sond_index_ctx_coverage_clear:
 *
 * Reine Fall-1-Bereinigung für Löschen: entfernt jeden coverage-Eintrag
 * für path selbst und alles darunter (per "/"-Präfix) - rührt einen
 * eventuell abdeckenden VORFAHREN nicht an, s. Kommentar in sond_index.h.
 */
gboolean sond_index_ctx_coverage_clear(SondIndexCtx *ctx, gchar const *path,
        GError **error) {
    sqlite3_stmt *stmt = NULL;

    if (!ctx || !path) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: ctx/path fehlt", __func__);
        return FALSE;
    }

    if (sqlite3_prepare_v2(ctx->db,
            "DELETE FROM coverage WHERE path = ?1 OR " SQL_UNDER("path"),
            -1, &stmt, NULL) != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: prepare DELETE coverage: %s", __func__,
                sqlite3_errmsg(ctx->db));
        return FALSE;
    }
    sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: step DELETE coverage: %s", __func__,
                sqlite3_errmsg(ctx->db));
        sqlite3_finalize(stmt);
        return FALSE;
    }
    sqlite3_finalize(stmt);

    return TRUE;
}

/*
 * sond_index_ctx_coverage_expand_to_pages:
 *
 * Gegenstück zu coverage_mark(): hat filename einen eigenen coverage-
 * Eintrag, wird für jede Seite in pages_to_write eine einzelne
 * pages-Zeile mit demselben Modus angelegt - rettet den Seiten-
 * Fortschritt vor dem anschließenden Verwerfen des Datei-Eintrags (s.
 * Kommentar in sond_index.h). Bewusst eine explizite Seitenliste (statt
 * "Gesamtzahl + Ausschlussliste"): der Aufrufer kennt die tatsächlich
 * noch existierenden Seiten (page_akt bleibt für gelöschte Seiten
 * dauerhaft, aber stabil, im Array stehen - "Karteileichen"; die rohe
 * Array-Länge ist NICHT die aktuelle Seitenzahl).
 */
gboolean sond_index_ctx_coverage_expand_to_pages(SondIndexCtx *ctx,
        gchar const *filename, gint const *pages_to_write,
        guint n_pages_to_write, GError **error) {
    sqlite3_stmt *stmt = NULL;
    gint          mode = -1;

    if (!ctx || !filename) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: ctx/filename fehlt", __func__);
        return FALSE;
    }

    /* Eigener Eintrag oder abdeckender Vorfahre (z.B. das Verzeichnis nach
     * dem Zusammenfassen): in beiden Fällen sind die Seiten als indiziert
     * vermerkt und müssen einzeln gerettet werden, bevor der Eintrag
     * aufgelöst wird (coverage_invalidate() trägt nur die Geschwister neu
     * ein, nicht filename selbst). */
    mode = sond_index_ctx_coverage_get(ctx, filename);

    /* Sonst ggf. der Seiten-Eintrag "filename//" (PDF, nur Seiten - s.
     * sond_index()): auch er steht für "alle Seiten abgedeckt". */
    if (mode < 0) {
        gchar *pages_path = g_strdup_printf("%s//", filename);

        mode = coverage_get_exact(ctx, pages_path);
        g_free(pages_path);
    }

    if (mode < 0)
        return TRUE; /* nicht abgedeckt - nichts zu tun */

    if (sqlite3_prepare_v2(ctx->db,
            "INSERT INTO pages(filename, page_nr, ocr_mode) VALUES(?,?,?)"
            " ON CONFLICT(filename, page_nr) DO UPDATE SET ocr_mode = excluded.ocr_mode",
            -1, &stmt, NULL) != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: prepare INSERT pages: %s", __func__,
                sqlite3_errmsg(ctx->db));
        return FALSE;
    }

    for (guint i = 0; i < n_pages_to_write; i++) {
        gint page = pages_to_write[i];

        sqlite3_reset(stmt);
        sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int (stmt, 2, page);
        sqlite3_bind_int (stmt, 3, mode);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "%s: INSERT pages '%s' Seite %d: %s", __func__,
                    filename, page, sqlite3_errmsg(ctx->db));
            sqlite3_finalize(stmt);
            return FALSE;
        }
    }
    sqlite3_finalize(stmt);

    return TRUE;
}

/*
 * sond_index_ctx_coverage_invalidate:
 *
 * Entwertet path (Datei oder Verzeichnis): danach hat path selbst keinen
 * coverage-Eintrag mehr (weder direkt noch über einen Vorfahren).
 *
 * Fall 1: path selbst hat einen eigenen coverage-Eintrag -> einfach
 * löschen.
 *
 * Fall 2: path ist nur indirekt über einen Vorfahren-Eintrag abgedeckt ->
 * dieser Vorfahre wird aufgelöst und auf jeder Ebene zwischen Vorfahre und
 * path werden die jeweiligen Geschwister (die weiterhin gültig sind) neu
 * eingetragen, mit demselben Modus, den der Vorfahre hatte. Auf einer
 * echten Dateisystem-Ebene ("/"-getrennt) per flachem Verzeichnis-Listing
 * (kein rekursiver Scan); an einer E-Mail-Container-Grenze ("//", Header
 * oder ein nummerierter Mimepart, s. is_gmessage_child_segment()) über die
 * per container_entrycount errechneten erwarteten Kind-Schlüssel, OHNE
 * die Mail zu öffnen.
 *
 * Bekannte Einschränkung: für andere "//"-Container als E-Mail (z.B.
 * ZIP-interne Pfade) sowie für tiefer verschachtelte Multiparts
 * (z.B. "x.eml//0/1") ist die Geschwister-Rekonstruktion nicht
 * vorgesehen. Ebenso unbehandelt: die
 * Seiten-Ebene innerhalb einer bereits gemeinsam abgedeckten Datei (dort
 * gibt es kein "Verzeichnis" zum Auflisten).
 */
/* Endung ".pdf" (ohne sond_mime.c, das der Server nicht linkt) */
static gboolean path_is_pdf(gchar const *path) {
    gsize len = path ? strlen(path) : 0;

    return len >= 4 && !g_ascii_strcasecmp(path + len - 4, ".pdf");
}

/* Savepoint statt BEGIN: verschachtelbar. Die Funktionen unten ändern
 * mehrere Zeilen und Tabellen und laufen auch innerhalb einer schon offenen
 * Transaktion des Aufrufers (z.B. beim Löschen im Dateibaum); ohne offene
 * Transaktion wird der Savepoint selbst zu einer - alles oder nichts, und
 * nur ein Commit statt einem je Statement. */
static gboolean db_savepoint(SondIndexCtx *ctx, GError **error) {
    char *errmsg = NULL;

    if (sqlite3_exec(ctx->db, "SAVEPOINT sond_sp;", NULL, NULL, &errmsg)
            != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "SAVEPOINT: %s", errmsg);
        sqlite3_free(errmsg);

        return FALSE;
    }

    return TRUE;
}

static void db_savepoint_end(SondIndexCtx *ctx, gboolean ok) {
    sqlite3_exec(ctx->db, ok ? "RELEASE sond_sp;" :
            "ROLLBACK TO sond_sp; RELEASE sond_sp;", NULL, NULL, NULL);
}

static gboolean coverage_invalidate_impl(SondIndexCtx *ctx,
        gchar const *path, gchar const *root_dir, GError **error);

gboolean sond_index_ctx_coverage_invalidate(SondIndexCtx *ctx,
        gchar const *path, gchar const *root_dir, GError **error) {
    gboolean ok = FALSE;

    if (!ctx || !path)
        return coverage_invalidate_impl(ctx, path, root_dir, error);

    if (!db_savepoint(ctx, error))
        return FALSE;
    ok = coverage_invalidate_impl(ctx, path, root_dir, error);
    db_savepoint_end(ctx, ok);

    return ok;
}

static gboolean coverage_invalidate_impl(SondIndexCtx *ctx,
        gchar const *path, gchar const *root_dir, GError **error) {
    sqlite3_stmt *stmt     = NULL;
    gchar        *ancestor = NULL;
    gint          mode     = 0;

    if (!ctx || !path) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: ctx/path fehlt", __func__);
        return FALSE;
    }

    /* Der Seiten-Eintrag "path//" (PDF, nur Seiten - s. sond_index())
     * gehört zu path und wird mit entwertet. Für andere Pfade gibt es ihn
     * nicht. */
    {
        gchar *pages_path = g_strdup_printf("%s//", path);

        if (sqlite3_prepare_v2(ctx->db,
                "DELETE FROM coverage WHERE path = ?", -1, &stmt, NULL)
                == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, pages_path, -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        stmt = NULL;
        g_free(pages_path);
    }

    /* Abdeckenden Vorfahren (oder path selbst) suchen - wie
     * sond_index_ctx_coverage_get(), aber wir merken uns, WELCHE Ebene
     * getroffen hat. */
    ancestor = g_strdup(path);
    if (sqlite3_prepare_v2(ctx->db,
            "SELECT ocr_mode FROM coverage WHERE path = ?", -1, &stmt, NULL)
            != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: prepare SELECT coverage: %s", __func__,
                sqlite3_errmsg(ctx->db));
        g_free(ancestor);
        return FALSE;
    }
    for (;;) {
        gchar *slash = NULL;

        sqlite3_reset(stmt);
        sqlite3_bind_text(stmt, 1, ancestor, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            mode = sqlite3_column_int(stmt, 0);
            break;
        }

        slash = strrchr(ancestor, '/');
        if (!slash) {
            /* Weder path noch irgendein Vorfahre abgedeckt - nichts zu tun */
            sqlite3_finalize(stmt);
            g_free(ancestor);
            return TRUE;
        }
        *slash = '\0';
    }
    sqlite3_finalize(stmt);
    stmt = NULL;

    /* Vorfahren-Eintrag (bzw. path selbst, falls Fall 1) löschen */
    if (sqlite3_prepare_v2(ctx->db,
            "DELETE FROM coverage WHERE path = ?", -1, &stmt, NULL)
            != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: prepare DELETE coverage: %s", __func__,
                sqlite3_errmsg(ctx->db));
        g_free(ancestor);
        return FALSE;
    }
    sqlite3_bind_text(stmt, 1, ancestor, -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    stmt = NULL;

    if (!g_strcmp0(ancestor, path)) {
        /* Fall 1: path hatte selbst den Eintrag - fertig, kein Runterbrechen
         * auf Geschwister nötig. */
        g_free(ancestor);
        return TRUE;
    }

     /* Fall 2: von ancestor aus Richtung path absteigen, auf jeder
      * Zwischenebene die Geschwister außer dem jeweils weiterführenden Kind
      * mit mode neu eintragen. Zwei Arten von Zwischenebenen, je nachdem,
      * welcher Trenner im ORIGINALEN path an dieser Stelle stand:
     *  - "/"  : echtes Dateisystem-Verzeichnis -> Geschwister per
     *           sond_dir_open()-Listing (unverändertes Verhalten).
     *  - "//" : Grenze in einen Container hinein. Nur für E-Mail-Kinder
     *           ("header" oder ein reiner Mimepart-Index, s.
     *           is_gmessage_child_segment()) bekannt: Geschwister sind die
     *           per container_entrycount errechneten erwarteten Kind-
     *           Schlüssel (s. gmessage_container_child_keys()), OHNE
     *           Dateizugriff. Für alle anderen "//"-Fälle (z.B. ZIP-interne
     *           Pfade) bewusst UNVERÄNDERTES (eingeschränktes) Verhalten:
     *           sond_dir_open() auf einen Container schlägt fehl (ist keine
     *           Verzeichnis), die Rekonstruktion bricht dort einfach ab -
     *           exakt die schon vorher dokumentierte Einschränkung oben,
     *           jetzt nur nicht mehr fälschlich mit einfachem "/" statt
     *           "//" beim Wiedereintragen vermischt. */
    {
        gsize      ancestor_len = strlen(ancestor);
        gchar const *p          = path + ancestor_len; /* zeigt auf den Trenner */
        GPtrArray *seg_names    = g_ptr_array_new_with_free_func(g_free);
        GArray    *seg_is_cont  = g_array_new(FALSE, FALSE, sizeof(gboolean));
        gchar     *current_dir  = g_strdup(ancestor);

        /* Segmente + jeweiligen Trenner-Typ aus dem ORIGINALEN path
         * herauslösen (statt wie bisher pauschal an "/" zu splitten). */
        while (*p) {
            gboolean is_container;
            gchar const *seg_start;

            if (p[0] == '/' && p[1] == '/') {
                is_container = TRUE;
                p += 2;
            } else if (p[0] == '/') {
                is_container = FALSE;
                p += 1;
            } else {
                break; /* sollte nicht vorkommen */
            }

            seg_start = p;
            while (*p && *p != '/')
                p++;

            g_ptr_array_add(seg_names, g_strndup(seg_start, p - seg_start));
            g_array_append_val(seg_is_cont, is_container);
        }

        /* Lauft ueber JEDES Segment, auch das letzte (der direkte Eltern-
         * ordner von path) - dort muessen die Geschwister genauso markiert
         * werden. Das Weiter-Absteigen danach ist beim letzten Segment
         * harmlos (current_dir wird nur noch nicht mehr benutzt). */
        for (guint i = 0; i < seg_names->len; i++) {
            gchar const *segment     = g_ptr_array_index(seg_names, i);
            gboolean     is_container = g_array_index(seg_is_cont, gboolean, i);
            gchar       *next_dir     = NULL;

            if (is_container && is_gmessage_child_segment(segment)) {
                GPtrArray *child_keys = gmessage_container_child_keys(ctx, current_dir);

                if (!child_keys)
                    break; /* container_entrycount unbekannt - s.o., nicht fatal */

                for (guint k = 0; k < child_keys->len; k++) {
                    gchar const *child_key = g_ptr_array_index(child_keys, k);
                    gchar *expected_this = g_strdup_printf("%s//%s",
                            current_dir, segment);

                    if (g_strcmp0(child_key, expected_this))
                        sond_index_ctx_coverage_mark(ctx, child_key, mode, NULL);
                    g_free(expected_this);
                }
                g_ptr_array_unref(child_keys);

                next_dir = g_strdup_printf("%s//%s", current_dir, segment);
            } else if (is_container && path_is_pdf(current_dir)) {
                /* In eine PDF hinein (eingebettete Datei oder - bei leerem
                 * segment - ihr Seiten-Eintrag selbst): Seiten und übrige
                 * Anhänge bleiben gültig, ausgenommen der, in dessen Richtung
                 * entwertet wird. Die Anhänge kennt pdf_embedded; fehlt die
                 * Liste, verlieren sie ihre Abdeckung. */
                GPtrArray *addresses = pdf_embedded_get(ctx, current_dir);

                if (*segment) {
                    gchar *pages_path = g_strdup_printf("%s//", current_dir);

                    sond_index_ctx_coverage_mark(ctx, pages_path, mode, NULL);
                    g_free(pages_path);
                }

                for (guint k = 0; addresses && k < addresses->len; k++) {
                    gchar const *address = g_ptr_array_index(addresses, k);
                    gchar *emb_path = NULL;

                    if (!g_strcmp0(address, segment))
                        continue;

                    emb_path = g_strdup_printf("%s//%s", current_dir, address);
                    sond_index_ctx_coverage_mark(ctx, emb_path, mode, NULL);
                    g_free(emb_path);
                }
                if (addresses)
                    g_ptr_array_unref(addresses);

                next_dir = g_strdup_printf("%s//%s", current_dir, segment);
            } else {
                SondDir *dir   = NULL;
                GError *dir_error = NULL;
                gchar const *entry_name = NULL;
                /* current_dir ist - wie alle coverage-Keys - projektrelativ;
                 * fürs Öffnen wird der echte Dateisystempfad gebraucht (wie
                 * bei sond_index_ctx_coverage_try_collapse()). Ohne diesen
                 * Präfix schlägt das Öffnen praktisch immer fehl (relativ zum
                 * Prozess-CWD, nicht zur Projektwurzel). sond_dir_open()
                 * statt g_dir_open(): Long-Path-sicher (Windows), wie überall
                 * sonst im Code für Dateisystemzugriffe (sond_file_helper.c). */
                if (!root_dir) {
                    if (error && !*error)
                        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                                "%s: root_dir fehlt", __func__);
                    break;
                }

                {
                    gchar *current_dir_abs = g_strconcat(root_dir, "/",
                            current_dir, NULL);
                    dir = sond_dir_open(current_dir_abs, &dir_error);
                    g_free(current_dir_abs);
                }
                if (!dir) {
                    if (error && !*error)
                        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                                "%s: Verzeichnis '%s' (unter '%s') nicht "
                                "lesbar: %s", __func__, current_dir, root_dir,
                                dir_error ? dir_error->message : "?");
                    g_clear_error(&dir_error);
                    break; /* nicht fatal fuer die Invalidierung selbst -
                            * path ist bereits nicht mehr abgedeckt (s.o.),
                            * es fehlen hoechstens Geschwister-Eintraege. */
                }

                while ((entry_name = sond_dir_read_name(dir))) {
                    gchar *sibling_path = NULL;

                    if (!g_strcmp0(entry_name, segment))
                        continue; /* das ist die Richtung zu path - hier nicht eintragen */

                    /* coverage-Keys sind - wie ueberall im Code - "/"-getrennt
                     * (nicht g_build_filename(), das unter Windows "\" liefern
                     * wuerde und die Keys damit inkompatibel zu allen anderen,
                     * mit "/" gebildeten Lookups machen wuerde, s. Konvention
                     * bei sond_index_ctx_coverage_try_collapse()). */
                    sibling_path = g_strconcat(current_dir, "/", entry_name, NULL);
                    sond_index_ctx_coverage_mark(ctx, sibling_path, mode, NULL);
                    g_free(sibling_path);
                }
                sond_dir_close(dir);

                next_dir = g_strconcat(current_dir, "/", segment, NULL);
            }

            g_free(current_dir);
            current_dir = next_dir;
        }

        g_free(current_dir);
        g_ptr_array_unref(seg_names);
        g_array_free(seg_is_cont, TRUE);
    }

    g_free(ancestor);
    return TRUE;
}

/*
 * sond_index_ctx_coverage_try_collapse:
 *
 * Nach dem path (Datei, Verzeichnis oder E-Mail-Kind wie "x.eml//header")
 * soeben abgedeckt wurde (coverage_mark() ist für path bereits erfolgt),
 * wird von hier aus schrittweise nach oben geprüft. Zwei Ebenen-Arten:
 *  - Liegt current an einer E-Mail-Container-Grenze ("//", Header oder
 *    ein nummerierter Mimepart, s. is_gmessage_child_segment()): sind
 *    ALLE per container_entrycount erwarteten Geschwister (Header + jeder
 *    Mimepart, s. gmessage_container_child_keys()) einzeln abgedeckt -
 *    ohne Dateizugriff -, wird die ganze E-Mail zu EINEM Eintrag
 *    zusammengefasst ("x.eml" bedeutet dann "Header UND alle Mimeparts
 *    vollständig"), danach geht es mit der .eml-Datei selbst normal in
 *    ihrem echten Dateisystem-Verzeichnis weiter.
 *  - Sonst (reine "/"-Ebene): hat auf der jeweils nächsthöheren
 * Ebene JEDES Geschwister (echtes, flaches Verzeichnis-Listing)
 * IRGENDEINEN coverage-Eintrag (per sond_index_ctx_coverage_get() - egal
 * mit welchem Modus, s. Mindestmodus-Konvention)? Wenn ja, wird die ganze
 * Ebene zu einem Eintrag für das Elternverzeichnis zusammengefasst
 * (coverage_mark() löscht dabei automatisch die Kind-Einträge), dessen
 * ocr_mode der MINDESTMODUS aller Geschwister ist - NICHT der Modus der
 * zuletzt verarbeiteten Datei, sonst würde z.B. ein mit "prüfen"
 * abgedecktes Geschwister ein Coalescing verhindern, nur weil die
 * gerade fertig gewordene Datei mit "erzwingen" liefen. Die Prüfung
 * setzt sich dann beim nächsten Elternverzeichnis fort. Stoppt, sobald
 * eine Ebene nicht vollständig abgedeckt ist, die oberste Ebene direkt
 * im Projektverzeichnis erreicht ist, oder ein Verzeichnis nicht
 * gelesen werden kann.
 *
 * path/root_dir: path ist - wie alle coverage-Keys - projektrelativ
 * (Konvention wie überall sonst im Code: "/"-getrennt, kein
 * g_build_filename/g_path_get_dirname-Ergebnis als Key verwenden, da das
 * unter Windows "\" liefern würde). root_dir ist der ABSOLUTE
 * Projektwurzelpfad (zond->project_dir) - wird NUR gebraucht, um aus
 * einem relativen Verzeichnis-Key einen echten Dateisystempfad für
 * g_dir_open() zu bauen.
 *
 * Es wird absichtlich NIE über die oberste Ebene direkt im
 * Projektverzeichnis hinaus zusammengefasst (also nie ein einziger
 * Eintrag für das gesamte Projekt gebildet): von außen (nicht über zond)
 * hinzugefügte Dateien landen laut Vorgabe immer nur direkt im
 * Projektverzeichnis selbst, nie tiefer. Solange die obersten Einträge
 * einzeln stehen bleiben, bleibt die coverage-Tabelle auch bei solchen
 * externen Änderungen gültig, ohne daß wir Verzeichnisänderungen von
 * außen erkennen müssten.
 *
 * Bekannte Einschränkung: jede Datei/jeder Unterordner in einem
 * Verzeichnis muss einen coverage-Eintrag haben, damit die Ebene als
 * vollständig gilt - Dateien, die (noch) nie indiziert wurden oder die
 * grundsätzlich nicht indiziert werden (z.B. projektfremde Beidateien),
 * verhindern das Coalescing auf dieser Ebene dauerhaft. Kein Fehler,
 * lediglich eine verpasste Optimierung.
 */
static gboolean coverage_try_collapse_impl(SondIndexCtx *ctx,
        gchar const *path, gchar const *root_dir, GError **error);

gboolean sond_index_ctx_coverage_try_collapse(SondIndexCtx *ctx,
        gchar const *path, gchar const *root_dir, GError **error) {
    gboolean ok = FALSE;

    if (!ctx || !path)
        return TRUE;

    if (!db_savepoint(ctx, error))
        return FALSE;
    ok = coverage_try_collapse_impl(ctx, path, root_dir, error);
    db_savepoint_end(ctx, ok);

    return ok;
}

static gboolean coverage_try_collapse_impl(SondIndexCtx *ctx,
        gchar const *path, gchar const *root_dir, GError **error) {
    gchar *current = NULL;

    if (!ctx || !path)
        return TRUE;

    current = g_strdup(path);

    for (;;) {
        gchar       *parent      = NULL; /* projektrelativ, "/"-Konvention */
        gchar       *parent_abs  = NULL; /* echter Pfad, nur zum Öffnen */
        SondDir     *dir         = NULL;
        GError      *dir_error   = NULL;
        gchar const *entry_name  = NULL;
        gboolean     all_covered = TRUE;
        gint         min_mode    = G_MAXINT;
        gchar       *slash       = NULL;
        gchar       *boundary    = NULL;

         /* GMessage-Container-Grenze ("//") hat Vorrang vor einer
          * Dateisystem-Ebene ("/"): current ist dann ein E-Mail-internes
          * Kind (Header oder Mimepart), dessen "Elternverzeichnis" die
          * E-Mail selbst ist - kein echtes Verzeichnis, das sond_dir_open()
          * lesen könnte. */
        boundary = gmessage_find_last_boundary(current);

        /* Innerhalb einer PDF (Seiten "x.pdf//" oder Anhang "x.pdf//adr"):
         * sind die Seiten und alle Anhänge laut pdf_embedded einzeln
         * abgedeckt, wird daraus "x.pdf" (Mindestmodus), danach weiter mit
         * der PDF in ihrem Verzeichnis. Ist "x.pdf" schon selbst abgedeckt
         * (PDF ohne Anhänge), gleich dort weiter. */
        if (boundary) {
            gchar *container = g_strndup(current, boundary - current);

            if (path_is_pdf(container)) {
                GPtrArray *addresses = NULL;
                gchar *pages_path = NULL;
                gint entry_mode = 0;

                if (coverage_get_exact(ctx, container) >= 0) {
                    g_free(current);
                    current = container;
                    continue;
                }

                addresses = pdf_embedded_get(ctx, container);
                if (!addresses) { //Anhänge unbekannt - nicht zusammenfassbar
                    g_free(container);
                    break;
                }

                pages_path = g_strdup_printf("%s//", container);
                entry_mode = coverage_get_exact(ctx, pages_path);
                g_free(pages_path);
                if (entry_mode < 0)
                    all_covered = FALSE;
                else
                    min_mode = entry_mode;

                for (guint i = 0; all_covered && i < addresses->len; i++) {
                    gchar *emb_path = g_strdup_printf("%s//%s", container,
                            (gchar const*) g_ptr_array_index(addresses, i));

                    entry_mode = coverage_get_exact(ctx, emb_path);
                    g_free(emb_path);
                    if (entry_mode < 0)
                        all_covered = FALSE;
                    else if (entry_mode < min_mode)
                        min_mode = entry_mode;
                }
                g_ptr_array_unref(addresses);

                if (!all_covered) {
                    g_free(container);
                    break;
                }

                if (!sond_index_ctx_coverage_mark(ctx, container, min_mode, error)) {
                    g_free(container);
                    g_free(current);
                    return FALSE;
                }

                g_free(current);
                current = container;
                continue;
            }
            g_free(container);
        }

        if (boundary && is_gmessage_child_segment(boundary + 2)) {
            gchar *container = g_strndup(current, boundary - current);
            GPtrArray *child_keys = gmessage_container_child_keys(ctx, container);

            if (!child_keys) {
                /* container_entrycount für container unbekannt - kann
                 * nicht sicher aufgezählt werden (kein Fehler, nur keine
                 * weitere Zusammenfassung möglich, analog zu einem nicht
                 * lesbaren Verzeichnis unten). */
                g_free(container);
                g_free(current);
                current = NULL;
                break;
            }

            for (guint i = 0; i < child_keys->len; i++) {
                gchar const *child_key = g_ptr_array_index(child_keys, i);
                gint entry_mode = coverage_get_exact(ctx, child_key);

                if (entry_mode < 0) {
                    all_covered = FALSE;
                    break;
                }
                if (entry_mode < min_mode)
                    min_mode = entry_mode;
            }
            g_ptr_array_unref(child_keys);

            if (!all_covered) {
                g_free(container);
                g_free(current);
                current = NULL;
                break;
            }

            if (!sond_index_ctx_coverage_mark(ctx, container, min_mode, error)) {
                g_free(container);
                g_free(current);
                return FALSE;
            }

            /* Weiter mit der E-Mail-Datei selbst als current - die liegt
             * (anders als ihre internen Kinder) wieder in einem echten
             * Dateisystem-Verzeichnis und wird von der Schleife im
             * nächsten Durchlauf ganz normal über den "/"-Zweig unten
             * weiterbehandelt. */
            g_free(current);
            current = container;
            continue;
        }

        slash = strrchr(current, '/');
        if (!slash) {
            /* current liegt bereits direkt im Projektverzeichnis - hier
             * wird absichtlich gestoppt, s. Doc-Kommentar oben. */
            break;
        }

        parent = g_strndup(current, slash - current);
        parent_abs = g_strconcat(root_dir, "/", parent, NULL);

        /* sond_dir_open() statt g_dir_open(): Long-Path-sicher (Windows),
         * wie überall sonst im Code für Dateisystemzugriffe (s.
         * sond_file_helper.c) - bei tief verschachtelten (z.B. SeaDrive-
         * synchronisierten) Projektverzeichnissen kann der reale Pfad
         * sonst am klassischen MAX_PATH-Limit scheitern. */
        dir = sond_dir_open(parent_abs, &dir_error);
        g_free(parent_abs);
        if (!dir) {
            /* nicht fatal fuer den bereits erfolgten coverage_mark(path,...)
             * - lediglich keine weitere Zusammenfassung nach oben moeglich */
            g_clear_error(&dir_error);
            g_free(parent);
            break;
        }

        while ((entry_name = sond_dir_read_name(dir))) {
            gchar *entry_path = g_strconcat(parent, "/", entry_name, NULL);
            gint   entry_mode = sond_index_ctx_coverage_get(ctx, entry_path);

            g_free(entry_path);
            if (entry_mode < 0) {
                all_covered = FALSE;
                break;
            }
            if (entry_mode < min_mode)
                min_mode = entry_mode;
        }
        sond_dir_close(dir);

        if (!all_covered) {
            g_free(parent);
            break;
        }

        if (!sond_index_ctx_coverage_mark(ctx, parent, min_mode, error)) {
            g_free(parent);
            g_free(current);
            return FALSE;
        }

        g_free(current);
        current = parent;
    }

    g_free(current);
    return TRUE;
}

/* =======================================================================
 * sond_index_ctx_delete_index / sond_index_ctx_delete_all
 * ======================================================================= */

static gboolean delete_index_impl(SondIndexCtx *ctx, gchar const *path,
        gint von_seite, gint bis_seite, gchar const *root_dir,
        GError **error);

gboolean sond_index_ctx_delete_index(SondIndexCtx *ctx, gchar const *path,
        gint von_seite, gint bis_seite, gchar const *root_dir,
        GError **error) {
    gboolean ok = FALSE;

    if (!ctx || !path)
        return delete_index_impl(ctx, path, von_seite, bis_seite, root_dir,
                error);

    if (!db_savepoint(ctx, error))
        return FALSE;
    ok = delete_index_impl(ctx, path, von_seite, bis_seite, root_dir, error);
    db_savepoint_end(ctx, ok);

    return ok;
}

static gboolean delete_index_impl(SondIndexCtx *ctx, gchar const *path,
        gint von_seite, gint bis_seite, gchar const *root_dir,
        GError **error) {
    sqlite3_stmt *stmt = NULL;

    if (!ctx || !path) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: ctx/path fehlt", __func__);
        return FALSE;
    }

    if (von_seite >= 0) {
        /* Seitenbereich innerhalb einer Datei (Anbindung). Fortschritt der
         * verbleibenden Seiten zuerst retten, dann einen eigenen oder
         * abdeckenden Vorfahren-Eintrag auflösen - exakt das Muster aus
         * viewer_save.c beim Einfügen neuer Seiten. */
        GArray *known_pages = sond_index_ctx_get_pages_for_file(ctx, path);
        GArray *pages_to_keep = g_array_new(FALSE, FALSE, sizeof(gint));

        if (known_pages->len > 0) {
            /* Normalfall: path ist (noch) nicht zu einem eigenen
             * coverage-Eintrag kollabiert - einzelne "pages"-Zeilen
             * geben die exakt bekannten Seiten vor. */
            for (guint i = 0; i < known_pages->len; i++) {
                gint page_nr = g_array_index(known_pages, gint, i);

                if (page_nr < von_seite || page_nr > bis_seite)
                    g_array_append_val(pages_to_keep, page_nr);
            }
        } else {
            /* path WAR komplett zu einem eigenen coverage-Eintrag
             * kollabiert (keine einzelnen "pages"-Zeilen mehr) - ohne
             * file_pagecount ginge hier sonst der gesamte übrige
             * Seiten-Fortschritt der Datei verloren (Fall 1, s.
             * coverage_invalidate()-Kommentar). Mit bekannter
             * Gesamtseitenzahl lassen sich stattdessen alle NICHT zu
             * löschenden Seiten (0..total_pages-1 außerhalb
             * [von_seite,bis_seite]) korrekt rekonstruieren, ohne die
             * Datei erneut zu öffnen (SeaDrive-Hydrierung vermeiden). */
            gint total_pages = sond_index_ctx_get_page_count(ctx, path);

            if (total_pages >= 0) {
                for (gint p = 0; p < total_pages; p++)
                    if (p < von_seite || p > bis_seite)
                        g_array_append_val(pages_to_keep, p);
            }
            /* sonst (total_pages < 0 - Gesamtseitenzahl nie erfasst,
             * z.B. Datei vor Einführung von file_pagecount indiziert):
             * pages_to_keep bleibt leer - bekannte, dokumentierte
             * Einschränkung (Fall 1). */
        }
        g_array_unref(known_pages);

        if (!sond_index_ctx_coverage_expand_to_pages(ctx, path,
                (gint const*) pages_to_keep->data, pages_to_keep->len,
                error)) {
            g_array_unref(pages_to_keep);
            return FALSE;
        }
        g_array_unref(pages_to_keep);

        if (!sond_index_ctx_coverage_invalidate(ctx, path, root_dir, error))
            return FALSE;

        static gchar const *sql_range[] = {
            "DELETE FROM chunks WHERE filename = ?1 AND page_nr "
            "BETWEEN ?2 AND ?3",
            "DELETE FROM pages WHERE filename = ?1 AND page_nr "
            "BETWEEN ?2 AND ?3"
        };

        for (guint i = 0; i < G_N_ELEMENTS(sql_range); i++) {
            gint rc = 0;

            if (sqlite3_prepare_v2(ctx->db, sql_range[i], -1, &stmt, NULL)
                    != SQLITE_OK) {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "%s: prepare DELETE: %s", __func__,
                        sqlite3_errmsg(ctx->db));
                return FALSE;
            }
            sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int (stmt, 2, von_seite);
            sqlite3_bind_int (stmt, 3, bis_seite);
            rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            stmt = NULL;
            if (rc != SQLITE_DONE) {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "%s: DELETE '%s': %s", __func__, path,
                        sqlite3_errmsg(ctx->db));
                return FALSE;
            }
        }

        return TRUE;
    }

    /* Ganze Datei/ganzes Verzeichnis: path selbst sowie alles darunter
     * (Unterverzeichnisse, eingebettete Teile - "path/%" deckt per
     * Konvention beides ab, s. Kommentar bei coverage_mark). */
    {
        static gchar const *sql[] = {
            "DELETE FROM chunks WHERE filename = ?1 OR " SQL_UNDER("filename"),
            "DELETE FROM pages WHERE filename = ?1 OR " SQL_UNDER("filename")
        };

        for (guint i = 0; i < G_N_ELEMENTS(sql); i++) {
            gint rc = 0;

            if (sqlite3_prepare_v2(ctx->db, sql[i], -1, &stmt, NULL)
                    != SQLITE_OK) {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "%s: prepare DELETE: %s", __func__,
                        sqlite3_errmsg(ctx->db));
                return FALSE;
            }
            sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT);
            rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            stmt = NULL;
            if (rc != SQLITE_DONE) {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "%s: DELETE '%s': %s", __func__, path,
                        sqlite3_errmsg(ctx->db));
                return FALSE;
            }
        }
    }

    if (!sond_index_ctx_coverage_invalidate(ctx, path, root_dir, error))
        return FALSE;

    if (!sond_index_ctx_coverage_clear(ctx, path, error))
        return FALSE;

    /* Ganze Datei(en)/Verzeichnis gelöscht - zuletzt bekannte
     * Einträgeanzahl(en) etwaiger Container darunter sind damit
     * hinfällig. */
    if (!sond_index_ctx_clear_entry_count(ctx, path, error))
        return FALSE;

    /* Ganze Datei(en) gelöscht - zuletzt bekannte Seitenzahl(en) sind
     * damit hinfällig. */
    if (!sond_index_ctx_clear_page_count(ctx, path, error))
        return FALSE;

    return TRUE;
}

static gboolean delete_all_impl(SondIndexCtx *ctx, GError **error);

gboolean sond_index_ctx_delete_all(SondIndexCtx *ctx, GError **error) {
    gboolean ok = FALSE;

    if (!ctx)
        return delete_all_impl(ctx, error);

    if (!db_savepoint(ctx, error))
        return FALSE;
    ok = delete_all_impl(ctx, error);
    db_savepoint_end(ctx, ok);

    return ok;
}

static gboolean delete_all_impl(SondIndexCtx *ctx, GError **error) {
    char *errmsg = NULL;

    if (!ctx) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: ctx fehlt", __func__);
        return FALSE;
    }

    if (sqlite3_exec(ctx->db, "DELETE FROM chunks;", NULL, NULL, &errmsg)
            != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: DELETE chunks: %s", __func__, errmsg);
        sqlite3_free(errmsg);
        return FALSE;
    }

    if (sqlite3_exec(ctx->db, "DELETE FROM pages;", NULL, NULL, &errmsg)
            != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: DELETE pages: %s", __func__, errmsg);
        sqlite3_free(errmsg);
        return FALSE;
    }

    if (sqlite3_exec(ctx->db, "DELETE FROM coverage;", NULL, NULL, &errmsg)
            != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: DELETE coverage: %s", __func__, errmsg);
        sqlite3_free(errmsg);
        return FALSE;
    }

    if (sqlite3_exec(ctx->db, "DELETE FROM file_pagecount;", NULL, NULL, &errmsg)
            != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: DELETE file_pagecount: %s", __func__, errmsg);
        sqlite3_free(errmsg);
        return FALSE;
    }

    if (sqlite3_exec(ctx->db, "DELETE FROM container_entrycount;", NULL, NULL, &errmsg)
            != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: DELETE container_entrycount: %s", __func__, errmsg);
        sqlite3_free(errmsg);
        return FALSE;
    }

    if (sqlite3_exec(ctx->db, "DELETE FROM gmessage_inline;", NULL, NULL, &errmsg)
            != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: DELETE gmessage_inline: %s", __func__, errmsg);
        sqlite3_free(errmsg);
        return FALSE;
    }

    if (sqlite3_exec(ctx->db, "DELETE FROM pdf_embedded;", NULL, NULL, &errmsg)
            != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%s: DELETE pdf_embedded: %s", __func__, errmsg);
        sqlite3_free(errmsg);
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
    /* prefix_old selbst und alles darunter - "/" (Unterverzeichnis, Datei
     * in umbenanntem Ordner) wie "//" (eingebetteter Inhalt) - bekommt
     * prefix_new als Anfang. SUBSTR statt LIKE, weil "%" und "_" in Pfaden
     * vorkommen (Adressen eingebetteter Dateien). */
    static const struct {
        gchar const *table;
        gchar const *column;
    } targets[] = {
        { "chunks", "filename" }, { "pages", "filename" },
        { "file_pagecount", "filename" }, { "container_entrycount", "filename" },
        { "gmessage_inline", "filename" }, { "pdf_embedded", "filename" },
        { "coverage", "path" }
    };

    for (guint t = 0; t < G_N_ELEMENTS(targets); t++) {
        sqlite3_stmt *stmt = NULL;
        gchar *sql = g_strdup_printf(
                "UPDATE %s SET %s = ?2 || SUBSTR(%s, LENGTH(?1) + 1) "
                "WHERE %s = ?1 OR SUBSTR(%s, 1, LENGTH(?1) + 1) = ?1 || '/'",
                targets[t].table, targets[t].column, targets[t].column,
                targets[t].column, targets[t].column);

        gint rc = sqlite3_prepare_v2(ctx->db, sql, -1, &stmt, NULL);
        g_free(sql);
        if (rc != SQLITE_OK) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "sond_index_ctx_rename_file: prepare %s: %s",
                        targets[t].table, sqlite3_errmsg(ctx->db));
            return FALSE;
        }

        sqlite3_bind_text(stmt, 1, prefix_old, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, prefix_new, -1, SQLITE_STATIC);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "sond_index_ctx_rename_file: step %s: %s",
                        targets[t].table, sqlite3_errmsg(ctx->db));
            return FALSE;
        }
    }

    return TRUE;
}

/* =======================================================================
 * sond_index_ctx_update_gmessage_index / _clear_gmessage_structure
 * ======================================================================= */

/* Segment direkt hinter ?1 (bis zum nächsten "/") bzw. der Rest danach */
#define GMSG_AFTER "SUBSTR(%s, LENGTH(?1) + 1)"
#define GMSG_SEG   "SUBSTR(" GMSG_AFTER ", 1, INSTR(" GMSG_AFTER " || '/', '/') - 1)"
#define GMSG_REST  "SUBSTR(" GMSG_AFTER ", INSTR(" GMSG_AFTER " || '/', '/'))"

gboolean sond_index_ctx_update_gmessage_index(SondIndexCtx *ctx,
        gchar const *prefix, gint index, gboolean into, GError **error) {
    static const struct {
        gchar const *table;
        gchar const *column;
    } targets[] = {
        { "chunks", "filename" }, { "pages", "filename" },
        { "file_pagecount", "filename" }, { "container_entrycount", "filename" },
        { "gmessage_inline", "filename" }, { "coverage", "path" }
    };

    if (!ctx || !prefix)
        return TRUE;

    /* Zwei Durchgänge: erst auf "-N" (kann nicht belegt sein), dann zurück
     * auf "N" - beim Hochzählen wäre das Ziel sonst noch belegt und die
     * eindeutigen Schlüssel der Tabellen würden verletzt. Nur rein
     * numerische Segmente, "header" bleibt. */
    for (guint t = 0; t < G_N_ELEMENTS(targets); t++) {
        gchar const *c = targets[t].column;
        gchar *seg = g_strdup_printf(GMSG_SEG, c, c);
        gchar *rest = g_strdup_printf(GMSG_REST, c, c);
        gchar *sql[2] = { NULL, NULL };

        sql[0] = g_strdup_printf(
                "UPDATE %s SET %s = ?1 || '-' || (CAST(%s AS INTEGER) + ?2) || %s "
                "WHERE SUBSTR(%s, 1, LENGTH(?1)) = ?1 AND %s <> '' "
                "AND %s NOT GLOB '*[^0-9]*' AND CAST(%s AS INTEGER) >= ?3",
                targets[t].table, c, seg, rest, c, seg, seg, seg);
        sql[1] = g_strdup_printf(
                "UPDATE %s SET %s = ?1 || SUBSTR(%s, 2) || %s "
                "WHERE SUBSTR(%s, 1, LENGTH(?1)) = ?1 AND %s GLOB '-[0-9]*' "
                "AND SUBSTR(%s, 2) NOT GLOB '*[^0-9]*'",
                targets[t].table, c, seg, rest, c, seg, seg);
        g_free(seg);
        g_free(rest);

        for (guint s = 0; s < 2; s++) {
            sqlite3_stmt *stmt = NULL;
            gint rc = sqlite3_prepare_v2(ctx->db, sql[s], -1, &stmt, NULL);

            if (rc != SQLITE_OK) {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "%s: prepare %s: %s", __func__, targets[t].table,
                        sqlite3_errmsg(ctx->db));
                g_free(sql[0]);
                g_free(sql[1]);
                return FALSE;
            }

            sqlite3_bind_text(stmt, 1, prefix, -1, SQLITE_STATIC);
            if (s == 0) {
                sqlite3_bind_int(stmt, 2, into ? 1 : -1);
                sqlite3_bind_int(stmt, 3, index);
            }
            rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);

            if (rc != SQLITE_DONE) {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "%s: step %s: %s", __func__, targets[t].table,
                        sqlite3_errmsg(ctx->db));
                g_free(sql[0]);
                g_free(sql[1]);
                return FALSE;
            }
        }
        g_free(sql[0]);
        g_free(sql[1]);
    }

    return TRUE;
}

gboolean sond_index_ctx_clear_gmessage_structure(SondIndexCtx *ctx,
        gchar const *filename, GError **error) {
    static gchar const *sql[] = {
        "DELETE FROM container_entrycount WHERE filename = ?",
        "DELETE FROM gmessage_inline WHERE filename = ?"
    };

    if (!ctx || !filename)
        return TRUE;

    for (guint i = 0; i < G_N_ELEMENTS(sql); i++) {
        sqlite3_stmt *stmt = NULL;

        if (sqlite3_prepare_v2(ctx->db, sql[i], -1, &stmt, NULL) != SQLITE_OK) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "%s: prepare: %s", __func__, sqlite3_errmsg(ctx->db));
            return FALSE;
        }
        sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "%s: step: %s", __func__, sqlite3_errmsg(ctx->db));
            sqlite3_finalize(stmt);
            return FALSE;
        }
        sqlite3_finalize(stmt);
    }

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

/* Leerraum (ASCII); Bytes von Mehrbyte-Zeichen sind nie ASCII */
static gboolean chunk_is_space(gchar c) {
    return c == ' ' || c == '\n' || c == '\t' || c == '\r';
}

/* Zerlegt text in Chunks von höchstens chunk_size BYTE, die sich um etwa
 * chunk_overlap Byte überlappen. Geschnitten wird nach Möglichkeit an
 * Wortgrenzen (Ende: letzter Leerraum innerhalb der letzten chunk_overlap
 * Byte, Anfang des nächsten: erstes Wort im Überlappungsbereich): ein
 * mitten im Wort abgeschnittenes Bruchstück würde sonst bei "ganzes Wort"
 * als eigenes Wort gefunden. Gibt es dort keinen Leerraum, wird wie bisher
 * an beliebiger Stelle (aber nie mitten in einem UTF-8-Zeichen) geschnitten. */
static GPtrArray* text_to_chunks(gchar const *text, gint chunk_size, gint chunk_overlap) {
    GPtrArray *chunks = g_ptr_array_new_with_free_func((GDestroyNotify)sond_chunk_free);
    gint len  = (gint)strlen(text);

    if (chunk_overlap < 0 || chunk_overlap >= chunk_size)
        chunk_overlap = 0;

    for (gint pos = 0; pos < len; ) {
        gint end = utf8_align_to_char_start(text, MIN(pos + chunk_size, len));

        /* Ende an die letzte Wortgrenze zurücknehmen (nur wenn der Text
         * danach noch weitergeht) */
        if (end < len && chunk_overlap > 0) {
            for (gint k = end; k > pos + 1 && k > end - chunk_overlap; k--)
                if (chunk_is_space(text[k - 1])) {
                    end = k;
                    break;
                }
        }

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
            gint next_pos = utf8_align_to_char_start(text,
                    MAX(end - chunk_overlap, pos + 1));

            /* an einen Wortanfang vorrücken, solange das noch innerhalb des
             * Chunks liegt */
            if (next_pos > pos && !chunk_is_space(text[next_pos - 1])) {
                gint q = next_pos;

                while (q < end && !chunk_is_space(text[q]))
                    q++;
                if (q < end)
                    next_pos = q + 1;
            }

            /* Sicherheitsnetz: next_pos muss echt vorwärts gehen, sonst
             * Endlosschleife bei sehr kleinem Überlapp relativ zu Mehrbyte-
             * Zeichen. */
            pos = (next_pos > pos) ? next_pos : end;
        }
    }

    return chunks;
}

/* Hat der Chunk mindestens einen Buchstaben oder eine Ziffer? Ein Chunk aus
 * lauter Steuer-/Ersatzzeichen (kaputte PDF-Schrift, U+FFFD, CR) wird nicht
 * abgelegt: er ist nicht durchsuchbar und füllt nur den Index. */
static gboolean chunk_has_text(gchar const *text) {
    for (gchar const *p = text; *p; p = g_utf8_next_char(p))
        if (g_unichar_isalnum(g_utf8_get_char(p)))
            return TRUE;

    return FALSE;
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
    sqlite3_stmt  *stmt  = sond_index_ctx->stmt_insert_chunk;
    gint           rc    = 0;

    /* Das INSERT wird einmal vorbereitet und für jeden Chunk wiederverwendet
     * (sond_index_ctx_free() gibt es frei) */
    if (!stmt) {
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
        sond_index_ctx->stmt_insert_chunk = stmt;
    }
    else {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
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

    if (rc != SQLITE_DONE) {
        if (log_func)
            log_func(log_func_data,
                    "db_insert_chunk: step: %s", sqlite3_errmsg(sond_index_ctx->db));
        sqlite3_reset(stmt);
        return FALSE;
    }

    /* zurücksetzen, damit die Anweisung keine Sperre auf der Datenbank hält
     * (die gebundenen Texte gelten nur bis hierher) */
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);

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
 * FTS5-Query-String für einen Suchbegriff (term wie context):
 *   - ein oder mehrere Wörter → immer als Phrase: "Wort1 Wort2",
 *     auch ein einzelnes Wort ("Wort") - s. fts_quote()
 *   - whole_word == FALSE    → zusätzlich Präfix-Suche (angehängtes "*"):
 *     bei einem einzelnen Wort auf das Wort selbst (Wort*, findet z.B.
 *     auch "Wortliste"), bei einer Phrase auf deren letztes Wort
 *     ("Wort1 Wort2"*) - mehr erlaubt die FTS5-Syntax für Phrasen nicht
 *     (Präfix nur auf den letzten Token der Phrase). Echte Teilstring-
 *     Suche (Treffer auch mitten im Wort, z.B. "arbeit" in "Mehrarbeit")
 *     unterstützt der verwendete Standard-Tokenizer nicht - dafür bräuchte
 *     es einen Trigram-Tokenizer samt Neuindizierung.
 *
 * Rückgabe: neu allozierter String, mit g_free() freigeben.
 */
/* Ein Suchbegriff als FTS5-String-Literal: immer in Anführungszeichen (ein
 * Wort wie eine Phrase), "\"" im Begriff verdoppelt. Ungequotet wären
 * Bindestrich, Punkt, Doppelpunkt, Klammern und die Operatoren AND/OR/NOT/
 * NEAR Syntax - "Müller-Lüdenscheid" oder "31.12.2024" ergäben einen
 * Fehler. Der Tokenizer zerlegt den Inhalt wie beim Indizieren. */
static gchar* fts_quote(gchar const *term, gboolean whole_word) {
    GString *s = g_string_new("\"");

    for (gchar const *p = term; *p; p++) {
        if (*p == '"')
            g_string_append_c(s, '"');
        g_string_append_c(s, *p);
    }
    g_string_append_c(s, '"');
    if (!whole_word)
        g_string_append_c(s, '*');

    return g_string_free(s, FALSE);
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

/* Chunks überlappen sich nur um chunk_overlap Byte (deutlich weniger als
 * SNIPPET_CTX, s.u.) - liegt ein Treffer nahe am Anfang "seines" Chunks,
 * reicht der Text INNERHALB dieses einen Chunks nicht für den gewünschten
 * Vorlauf, obwohl auf der Seite davor noch mehr steht (im vorangehenden,
 * überlappenden Chunk). Holt bis zu max_chars Zeichen vom Ende des
 * unmittelbar vorangehenden Chunks derselben Datei/Seite (nächstniedrigerer
 * char_pos) - oder NULL, falls keiner existiert oder leer. */
static gchar* fetch_prev_chunk_tail(SondIndexCtx *ctx, gchar const *filename,
        gint page_nr, gint char_pos, gint max_chars) {
    sqlite3_stmt *stmt   = NULL;
    gchar        *result = NULL;

    if (sqlite3_prepare_v2(ctx->db,
            "SELECT text FROM chunks WHERE filename = ?1 AND page_nr = ?2"
            " AND char_pos < ?3 ORDER BY char_pos DESC LIMIT 1",
            -1, &stmt, NULL) != SQLITE_OK)
        return NULL;

    sqlite3_bind_text(stmt, 1, filename, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, page_nr);
    sqlite3_bind_int(stmt, 3, char_pos);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        gchar const *prev_text = (gchar const*) sqlite3_column_text(stmt, 0);

        if (prev_text && *prev_text) {
            gchar const *prev_end = prev_text + strlen(prev_text);
            gchar const *tail     = prev_end;

            for (gint k = 0; k < max_chars && tail > prev_text; k++)
                tail = g_utf8_prev_char(tail);
            result = g_strdup(tail);
        }
    }

    sqlite3_finalize(stmt);
    return result;
}

GPtrArray* sond_index_search(SondIndexCtx *ctx,
                              gchar const  *term,
                              gchar const  *context,
                              gboolean      whole_word,
                              gint          max_hits,
                              SondIndexHitFilter filter,
                              gpointer      filter_data,
                              gboolean     *truncated,
                              GError      **error) {
    GPtrArray    *result = NULL;
    sqlite3_stmt *stmt   = NULL;
    gchar        *query  = NULL;
    gchar        *query_ctx = NULL;
    gchar        *sql    = NULL;
    gint          rc     = 0;
    gboolean      stop   = FALSE; /* max_hits erreicht */
    /* Durch Chunk-Überlappung kann dasselbe Vorkommen aus zwei Chunks
     * gemeldet werden: Duplikate mit gleicher (filename, page_nr,
     * char_pos_in_page) werden gleich beim Sammeln verworfen. */
    GHashTable   *seen   = NULL;

    g_return_val_if_fail(ctx    != NULL, NULL);
    g_return_val_if_fail(term   != NULL, NULL);
    g_return_val_if_fail(*term  != '\0', NULL);

    if (truncated)
        *truncated = FALSE;

    result = g_ptr_array_new_with_free_func(sond_index_hit_free);
    seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    /* ---------------------------------------------------------------
     * Volltextsuche über FTS5
     * ------------------------------------------------------------- */
    query = fts_quote(term, whole_word);
    query_ctx = (context && *context) ? fts_quote(context, whole_word) : NULL;

    /* highlight() markiert jeden Token-Treffer mit \x01...\x02. Bei
     * Präfix-Suche (whole_word == FALSE, Default) markiert es dabei den
     * ganzen getroffenen Token, nicht nur den eingegebenen Präfix - so
     * erscheint z.B. bei Suche nach "Vertrag" auch "Vertragspartner"
     * komplett markiert.
     * Der Kontext steht als Unterabfrage, nicht als "AND" im MATCH: highlight()
     * markiert sonst auch die Kontextwörter, und jede Markierung würde ein
     * Treffer für den Suchbegriff. So zählt der Kontext nur als Bedingung
     * (selber Chunk). */
    sql = g_strdup_printf(
        "SELECT c.filename, c.page_nr, c.char_pos,"
        "       c.char_pos - (SELECT MIN(c2.char_pos) FROM chunks c2"
        "                     WHERE c2.filename = c.filename"
        "                       AND c2.page_nr  = c.page_nr),"
        "       c.text,"
        "       highlight(chunks_fts, 0, '\x01', '\x02')"
        " FROM chunks_fts"
        " JOIN chunks c ON c.id = chunks_fts.rowid"
        " WHERE chunks_fts MATCH ?1%s"
        " ORDER BY c.filename, c.page_nr, c.char_pos",
        query_ctx ?
                " AND c.id IN (SELECT rowid FROM chunks_fts"
                " WHERE chunks_fts MATCH ?2)" : "");
    rc = sqlite3_prepare_v2(ctx->db, sql, -1, &stmt, NULL);
    g_free(sql);

    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "sond_index_search: prepare FTS: %s",
                    sqlite3_errmsg(ctx->db));
        g_free(query);
        g_free(query_ctx);
        g_hash_table_destroy(seen);
        g_ptr_array_unref(result);
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, query, -1, SQLITE_TRANSIENT);
    if (query_ctx)
        sqlite3_bind_text(stmt, 2, query_ctx, -1, SQLITE_TRANSIENT);
    g_free(query);
    g_free(query_ctx);

    while (!stop && (rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        gchar const *chunk_text       = (gchar const *) sqlite3_column_text(stmt, 4);
        gint         chunk_offset_on_page = sqlite3_column_int(stmt, 3);
        gchar const *highlighted      = (gchar const *) sqlite3_column_text(stmt, 5);

        if (!chunk_text || !*chunk_text || !highlighted || !*highlighted) continue;

        /* Nicht zur Auswahl gehörende Chunks, bevor Ausschnitte entstehen */
        if (filter && !filter((gchar const *) sqlite3_column_text(stmt, 0),
                sqlite3_column_int(stmt, 1), filter_data))
            continue;

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

            /* Dublette (Chunk-Überlappung) oder Limit erreicht - vor dem
             * Aufbau des Ausschnitts */
            {
                gchar *key = g_strdup_printf("%s|%d|%d",
                        (gchar const *) sqlite3_column_text(stmt, 0),
                        sqlite3_column_int(stmt, 1), occ_pos_in_page);

                if (g_hash_table_contains(seen, key)) {
                    g_free(key);
                    ph = term_end_hl + 1;
                    marker_bytes++;
                    continue;
                }
                if (max_hits > 0 && (gint) result->len >= max_hits) {
                    g_free(key);
                    if (truncated)
                        *truncated = TRUE;
                    stop = TRUE;
                    break;
                }
                g_hash_table_add(seen, key);
            }

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

            gchar const *snip_start   = orig_start;
            gint         chars_before = 0;
            for (; chars_before < SNIPPET_CTX && snip_start > chunk_text; chars_before++)
                snip_start = g_utf8_prev_char(snip_start);

            gchar const *snip_end = orig_end;
            for (gint k = 0; k < SNIPPET_CTX && snip_end < chunk_end; k++)
                snip_end = g_utf8_next_char(snip_end);

            gboolean  ellipsis_before = FALSE;
            gchar    *prefix_extra    = NULL;

            if (chars_before < SNIPPET_CTX && chunk_offset_on_page > 0) {
                /* Chunk-Anfang erreicht, ohne den vollen Vorlauf zu
                 * bekommen - aber auf der Seite steht davor noch mehr
                 * (vorangehender, überlappender Chunk). Von dort den
                 * fehlenden Rest holen, s. fetch_prev_chunk_tail(). */
                prefix_extra = fetch_prev_chunk_tail(ctx,
                        (gchar const*) sqlite3_column_text(stmt, 0),
                        sqlite3_column_int(stmt, 1),
                        sqlite3_column_int(stmt, 2),
                        SNIPPET_CTX - chars_before);
                ellipsis_before = TRUE; /* vor dem (ggf. ergänzten) Ausschnitt
                                          steht so oder so noch mehr Text */
            } else
                ellipsis_before = (snip_start > chunk_text);

            gboolean ellipsis_after  = (snip_end   < chunk_end);
            gsize    snip_len        = (gsize)(snip_end - snip_start);
            gchar   *snippet_raw = g_strdup_printf("%s%s%.*s%s",
                    ellipsis_before ? "..." : "",
                    prefix_extra ? prefix_extra : "",
                    (gint)snip_len, snip_start,
                    ellipsis_after  ? "..." : "");
            g_free(prefix_extra);
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

    g_hash_table_destroy(seen);
    seen = NULL;

    if (!stop && rc != SQLITE_DONE) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "sond_index_search: step FTS: %s",
                    sqlite3_errmsg(ctx->db));
        sqlite3_finalize(stmt);
        g_ptr_array_unref(result);
        return NULL;
    }

    sqlite3_finalize(stmt);

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
 * sond_index
 * ======================================================================= */

/* Muss mit der Dispatch-Liste unten in sond_index() übereinstimmen. */
/* Textartige Typen außerhalb von "text/": Text mit anderer Kennung, der
 * wie Klartext gelesen wird. */
static gboolean mime_type_is_text_application(gchar const *mime_type) {
    static gchar const *const types[] = {
        "application/json", "application/xml", "application/sql",
        "application/x-sh", "application/x-bat", "application/x-yaml",
        NULL };

    for (gint i = 0; types[i]; i++)
        if (!g_strcmp0(mime_type, types[i]))
            return TRUE;

    return FALSE;
}

gboolean sond_index_mime_type_supported(gchar const *mime_type) {
    if (!mime_type)
        return FALSE;

    if (mime_type_is_text_application(mime_type))
        return TRUE;

    if (!g_strcmp0(mime_type, "application/pdf"))
        return TRUE;
    if (!g_strcmp0(mime_type, "message/rfc822"))
        return TRUE;
    if (!g_strcmp0(mime_type, "text/html"))
        return TRUE;
    if (!g_strcmp0(mime_type,
            "application/vnd.openxmlformats-officedocument.wordprocessingml.document"))
        return TRUE;
    if (!g_strcmp0(mime_type, "application/vnd.oasis.opendocument.text"))
        return TRUE;
    if (g_str_has_prefix(mime_type, "text/"))
        return TRUE;

    return FALSE;
}

/*
 * gmessage_count_root_entries:
 *
 * Anzahl der direkten Mimeparts einer E-Mail (Wurzel-Multipart-Anzahl,
 * oder 1 bei einem Wurzel-Leaf/-MessagePart ohne Multipart) - für
 * container_entrycount (s.u.), rein aus dem bereits im Speicher
 * vorliegenden Puffer ermittelt (kein zusätzlicher Dateizugriff, also
 * SeaDrive-unbedenklich - der Puffer liegt an dieser Stelle ohnehin
 * schon vor, s. sond_process_fileparts()). -1 bei Öffnen-Fehler.
 *
 * Mit der Header/Mimepart-Trennung gibt es einzeln abgedeckte E-Mail-Kinder
 * ("x.eml//header", "x.eml//0", ...), die für das GMessage-bewusste
 * Collapse/Invalidate gezählt werden müssen, ohne die Mail dafür zu öffnen.
 */
gboolean sond_index_ctx_record_gmessage_structure(SondIndexCtx *ctx,
        gchar const *filename, guchar const *buf, gsize size, GError **error) {
    GMimeMessage *message      = NULL;
    GMimeObject  *root         = NULL;
    GPtrArray    *inline_parts = NULL;
    gboolean      ok           = TRUE;

    if (!ctx || !filename)
        return TRUE;

    message = gmessage_open(buf, size);
    if (!message)
        return TRUE; //nicht lesbar - nichts festzuhalten

    //Entry-Count und Inline-Teile gemeinsam oder gar nicht
    if (!db_savepoint(ctx, error)) {
        g_object_unref(message);

        return FALSE;
    }

    root = g_mime_message_get_mime_part(message);

    /* Zahl der direkten Mimeparts +1 für den virtuellen "header"-Slot, der
     * beim GMessage-bewussten Collapse/Invalidate neben den nummerierten
     * Mimeparts mitgezählt wird */
    if (root)
        ok = sond_index_ctx_set_entry_count(ctx, filename,
                (GMIME_IS_MULTIPART(root) ?
                        g_mime_multipart_get_count(GMIME_MULTIPART(root)) : 1) + 1,
                error);

    //Inline-Teile für die angebundene Mail (Header + Inline, #197)
    if (ok) {
        inline_parts = g_ptr_array_new_with_free_func(g_free);
        if (root)
            gmessage_collect_inline(root, NULL, inline_parts);
        ok = gmessage_inline_set(ctx, filename, inline_parts, error);
        g_ptr_array_unref(inline_parts);
    }

    db_savepoint_end(ctx, ok);
    g_object_unref(message);

    return ok;
}

gboolean sond_index_ctx_batch_begin(SondIndexCtx *ctx, GError **error) {
    return ctx ? db_savepoint(ctx, error) : TRUE;
}

void sond_index_ctx_batch_end(SondIndexCtx *ctx, gboolean ok) {
    if (ctx)
        db_savepoint_end(ctx, ok);
}

void sond_index(fz_context* ctx,
		void (*log_func)(void*, gchar const*, ...), gpointer log_func_data,
		SondIndexCtx  *sond_index_ctx, gchar const* filename, guchar const  *buf,
		gsize size, gchar const *mime_type,
		gint seite_von, gint seite_bis, gint ocr_mode, gint const *cancel,
		gboolean gmessage_header_only, gboolean pdf_pagetree_only) {
    if (!sond_index_ctx) return;
    if (!mime_type) return;

    /* Nur für "message/rfc822" sinnvoll (s.o., Doku in sond_index.h) - bei
     * allen anderen MIME-Typen wird das Flag ignoriert. Der Header-Teil
     * einer Mail bekommt einen EIGENEN, von der ganzen Datei
     * unterscheidbaren Pfad ("<filename>//header"), unter dem
     * should_process_page/clear_page/Chunks/coverage_mark unten arbeiten -
     * so bleibt diese Teil-Indizierung unabhängig davon, ob/wie vollständig
     * der Rest der Mail (die einzelnen Mimeparts) bereits indiziert ist. */
    gboolean is_header_only = gmessage_header_only &&
            !g_strcmp0(mime_type, "message/rfc822");
    g_autofree gchar *header_path = is_header_only ?
            g_strdup_printf("%s//header", filename) : NULL;
    gchar const *idx_filename = is_header_only ? header_path : filename;

    /* Nur die Seiten einer PDF: Chunks/pages unter idx_filename (die
     * Seiten werden überall über "x.pdf" + Seitennummer angesprochen),
     * abgedeckt aber nur "x.pdf//" - s. Doku in sond_index.h. */
    g_autofree gchar *pagetree_path =
            (pdf_pagetree_only && !g_strcmp0(mime_type, "application/pdf")) ?
            g_strdup_printf("%s//", filename) : NULL;
    gchar const *coverage_path = pagetree_path ? pagetree_path : idx_filename;

    /* Segmente extrahieren */
    GPtrArray *segs = NULL;
    /* Echte Gesamtseitenzahl (nur bei PDF gesetzt, s.
     * sond_text_extract_pdf()) - NICHT dasselbe wie segs->len (Seiten
     * ohne extrahierbaren Text liefern kein Segment). Für
     * sond_index_ctx_set_page_count() weiter unten. */
    gint n_pages_total = -1;
    /* Seiten, deren Text sich nicht extrahieren ließ (defekte Seite) */
    g_autoptr(GArray) failed_pages = g_array_new(FALSE, FALSE, sizeof(gint));

    if (!g_strcmp0(mime_type, "application/pdf"))
        segs = sond_text_extract_pdf(ctx, buf, size,
        		(SondLogFunc) log_func, log_func_data, seite_von, seite_bis,
        		&n_pages_total, failed_pages);
    else if (!g_strcmp0(mime_type, "message/rfc822"))
        /* Eine Mail steht im Index über ihren Header ("x.eml//header") und
         * ihre Mimeparts ("x.eml//N"), jeweils mit eigener Coverage. Ihr
         * Gesamttext würde alles ein zweites Mal ablegen (doppelte Treffer,
         * auch für geschachtelte Mails) - hier daher kein Text, nur
         * Struktur (oben) und Coverage (unten). */
        segs = is_header_only ? sond_text_extract_gmessage_header(buf, size) :
                g_ptr_array_new_with_free_func(sond_text_segment_free);
    else if (!g_strcmp0(mime_type, "text/html"))
        segs = sond_text_extract_html(buf, size);
    else if (!g_strcmp0(mime_type,
    		"application/vnd.openxmlformats-officedocument.wordprocessingml.document"))
        segs = sond_text_extract_docx(buf, size, NULL);
    else if (!g_strcmp0(mime_type, "application/vnd.oasis.opendocument.text"))
        segs = sond_text_extract_odt(buf, size, NULL);
    else if (g_str_has_prefix(mime_type, "text/") ||
            mime_type_is_text_application(mime_type))
        segs = sond_text_extract_plain(buf, size);
    else
        return; /* MIME-Typ nicht indizierbar */

    if (!segs)
        return;

    /* Eine Datei ohne extrahierbaren Text (leer, Seiten ohne Textebene) gilt
     * trotzdem als verarbeitet - sonst bliebe sie dauerhaft "nicht
     * indiziert" und würde in jedem Lauf neu angefaßt. Nur eine PDF, die
     * sich gar nicht öffnen ließ (Seitenzahl unbekannt), wird nicht
     * vermerkt. */
    gboolean is_pdf = !g_strcmp0(mime_type, "application/pdf");

    if (segs->len == 0 && is_pdf && n_pages_total < 0) {
        g_ptr_array_unref(segs);
        return;
    }

    /* Die Datei ist ein Savepoint (verschachtelbar: der Aufrufer kann mehr
     * Schritte in einer Transaktion bündeln, s. sond_index_ctx_batch_begin()) */
    char *errmsg = NULL;
    GError *begin_error = NULL;
    if (!db_savepoint(sond_index_ctx, &begin_error)) {
        if (log_func)
            log_func(log_func_data, "sond_index: Transaktion nicht gestartet: %s",
                    begin_error ? begin_error->message : "?");
        g_clear_error(&begin_error);
        g_ptr_array_unref(segs);
        return;
    }

    /* container_entrycount und Inline-Teile für die GANZE Mail (filename,
     * nicht idx_filename) auffrischen - unabhängig von gmessage_header_only,
     * da der Puffer hier so oder so schon im Speicher liegt. Innerhalb der
     * Datei-Transaktion: ein Commit für alles. */
    if (!g_strcmp0(mime_type, "message/rfc822")) {
        GError *structure_error = NULL;

        if (!sond_index_ctx_record_gmessage_structure(sond_index_ctx, filename,
                buf, size, &structure_error)) {
            if (log_func)
                log_func(log_func_data,
                        "sond_index: Struktur '%s': %s", filename,
                        structure_error ? structure_error->message : "?");
            g_clear_error(&structure_error);
        }
    }

    gint chunk_idx = 0;
    gboolean cancelled = FALSE;
    for (guint s = 0; s < segs->len; s++) {
        SondTextSegment *seg = g_ptr_array_index(segs, s);

        /* Abbrechen-Button: bei einer einzelnen großen Datei (viele Seiten,
         * jede mit Embedding-Berechnung) kann das Indizieren einer Datei
         * lange dauern - ohne diese Prüfung wurde ein Klick auf "Abbrechen"
         * erst beim nächsten Aufruf von sond_index() (also nächste Datei)
         * wirksam, bei nur einer Datei in der Auswahl gar nicht, bis der
         * ganze Lauf von selbst fertig war. Bereits fertig indizierte
         * Seiten bleiben erhalten (unten committet) - der nächste Lauf
         * überspringt sie dank sond_index_ctx_should_process_page(). */
        if (cancel && g_atomic_int_get(cancel)) {
            cancelled = TRUE;
            break;
        }

        /* Doppelte Arbeit vermeiden: wenn diese Seite bereits mit
         * demselben oder höherem OCR-Modus indiziert wurde (z.B. weil
         * zwei ausgewählte Punkte sich überschneidende Seiten derselben
         * Datei referenzieren, oder ein früherer Lauf sie schon erledigt
         * hat), bleiben ihre vorhandenen Chunks unangetastet. */
        if (!sond_index_ctx_should_process_page(sond_index_ctx, idx_filename,
                seg->page_nr, ocr_mode))
            continue;

        /* Die Seite (Löschen der alten Chunks, neue Chunks, pages-Zeile) ist
         * ein Savepoint: bei Abbruch mitten in der Seite wird sie
         * zurückgerollt, es bleiben weder halbe Seiten noch ein fehlender
         * alter Stand zurück. */
        gboolean sp_page = db_savepoint(sond_index_ctx, NULL);

        /* Vorhandene Chunks dieser Seite entfernen, bevor sie neu
         * eingefügt werden (Löschen-vor-Einfügen wie zuvor auf
         * Datei-Ebene, jetzt auf Seiten-Ebene) - verhindert doppelte
         * Chunks, wenn die Seite zuvor schon (mit niedrigerem Modus oder
         * in einem früheren Lauf) indiziert war. */
        {
            GError *clear_error = NULL;
            if (!sond_index_ctx_clear_page(sond_index_ctx, idx_filename,
                    seg->page_nr, &clear_error)) {
                if (log_func)
                    log_func(log_func_data,
                            "sond_index: clear_page '%s' Seite %d: %s",
                            idx_filename, seg->page_nr,
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
                    idx_filename, seg->page_nr + 1, s + 1, segs->len, chunks->len);

        for (guint i = 0; i < chunks->len; i++) {
            SondChunk   *chunk = g_ptr_array_index(chunks, i);

            /* Auch innerhalb einer Seite prüfen - compute_embedding() pro
             * Chunk kann für sich schon spürbar dauern, bei vielen Chunks
             * pro Seite summiert sich das. */
            if (cancel && g_atomic_int_get(cancel)) {
                cancelled = TRUE;
                break;
            }

            /* Chunk ohne Buchstaben/Ziffern (Müll) nicht ablegen */
            if (!chunk_has_text(chunk->text))
                continue;

            gfloat *embedding  = compute_embedding(sond_index_ctx,
                    log_func, log_func_data, chunk->text);
            if (!db_insert_chunk(sond_index_ctx, log_func, log_func_data, idx_filename, chunk_idx,
                            seg->page_nr,
                            seg->char_pos + chunk->offset,
                            mime_type,
                            chunk->text, embedding)) {
                g_free(embedding);
                g_ptr_array_unref(chunks);
                if (sp_page)
                    db_savepoint_end(sond_index_ctx, FALSE);
                db_savepoint_end(sond_index_ctx, FALSE);
                g_ptr_array_unref(segs);
                return;
            }
            g_free(embedding);
            chunk_idx++;
        }

        g_ptr_array_unref(chunks);

        if (cancelled) {
            if (sp_page)
                db_savepoint_end(sond_index_ctx, FALSE);
            break;
        }

        /* Seite als (mit diesem Modus) indiziert markieren */
        sond_index_page_set(sond_index_ctx, idx_filename, seg->page_nr, ocr_mode);
        if (sp_page)
            db_savepoint_end(sond_index_ctx, TRUE);
    }

    /* Seiten ohne Text (kein Segment) im angeforderten Bereich ebenfalls als
     * verarbeitet vermerken - ohne pages-Zeile blieben sie in jedem
     * Seitenbereich dauerhaft "fehlend", und der Bereich würde nie FULL. */
    if (!cancelled && is_pdf && n_pages_total >= 0) {
        gint i_von = (seite_von >= 0) ? seite_von : 0;
        gint i_bis = (seite_bis >= 0) ? MIN(seite_bis, n_pages_total - 1) :
                n_pages_total - 1;
        guint j = 0;

        for (gint p = i_von; p <= i_bis; p++) {
            while (j < segs->len &&
                    ((SondTextSegment*) g_ptr_array_index(segs, j))->page_nr < p)
                j++;
            if (j < segs->len &&
                    ((SondTextSegment*) g_ptr_array_index(segs, j))->page_nr == p)
                continue; /* hat Text, oben behandelt */

            /* nicht lesbare Seite: weder Text noch "ohne Text" */
            {
                gboolean failed = FALSE;

                for (guint f = 0; !failed && f < failed_pages->len; f++)
                    failed = (g_array_index(failed_pages, gint, f) == p);
                if (failed)
                    continue;
            }

            if (!sond_index_ctx_should_process_page(sond_index_ctx,
                    idx_filename, p, ocr_mode))
                continue;

            sond_index_ctx_clear_page(sond_index_ctx, idx_filename, p, NULL);
            sond_index_page_set(sond_index_ctx, idx_filename, p, ocr_mode);
        }
    }

    /* Coverage-Coalescing: nur wenn die ganze Datei angefordert war
     * (seite_von/seite_bis == -1, d.h. bei PDF wurden wirklich ALLE Seiten
     * geprüft, bei anderen Formaten gibt es ohnehin keine
     * Seitenbereichs-Beschränkung) UND der Durchlauf nicht mitten drin
     * abgebrochen wurde. Nur an dieser Stelle ist zuverlässig bekannt, dass
     * die Datei wirklich komplett durchgesehen wurde - eine Prüfung weiter
     * oben (z.B. in sond_process_fileparts(), das nur "nicht abgebrochen"
     * sieht) könnte eine Datei fälschlich als komplett markieren, die
     * wegen eines anderen Fehlers (nicht Abbruch) nur teilweise
     * verarbeitet wurde. */
    if (failed_pages->len > 0 && log_func)
        log_func(log_func_data,
                "sond_index: '%s': %u Seite(n) nicht lesbar - die Datei gilt "
                "nicht als vollständig indiziert", idx_filename,
                failed_pages->len);

    /* Auch bei nicht lesbaren Seiten kein Voll-Eintrag: sie fehlen im Index.
     * Coverage und Seitenzahl stehen noch in der Transaktion der Chunks und
     * Seiten: entweder alles oder nichts, und ein Commit je Datei. */
    gboolean full_done = !cancelled && failed_pages->len == 0 &&
            seite_von == -1 && seite_bis == -1;

    if (full_done) {
        GError *coverage_error = NULL;

        if (!sond_index_ctx_coverage_mark(sond_index_ctx, coverage_path, ocr_mode,
                &coverage_error)) {
            if (log_func)
                log_func(log_func_data, "sond_index: coverage_mark '%s': %s",
                        coverage_path,
                        coverage_error ? coverage_error->message : "?");
            g_clear_error(&coverage_error);
        }

        /* file_pagecount: nur bei PDF bekannt (n_pages_total bleibt -1
         * bei allen anderen Formaten, insbesondere auch bei
         * gmessage_header_only) - coalescing-unabhängige Gesamtseitenzahl. */
        if (n_pages_total >= 0) {
            GError *pagecount_error = NULL;

            if (!sond_index_ctx_set_page_count(sond_index_ctx, idx_filename,
                    n_pages_total, &pagecount_error)) {
                if (log_func)
                    log_func(log_func_data,
                            "sond_index: set_page_count '%s': %s", idx_filename,
                            pagecount_error ? pagecount_error->message : "?");
                g_clear_error(&pagecount_error);
            }
        }
    }

    /* Bei Abbruch mitten in der Datei: bislang fertig indizierte Seiten
     * trotzdem committen (nicht verwerfen) - resumable dank
     * sond_index_ctx_should_process_page() beim nächsten Lauf. Die
     * angebrochene Seite wurde oben zurückgerollt (Savepoint je Seite), von
     * ihr bleibt nichts im Index. */
    if (sqlite3_exec(sond_index_ctx->db, "RELEASE sond_sp;", NULL, NULL, &errmsg) != SQLITE_OK) {
        if (log_func)
            log_func(log_func_data, "sond_index: COMMIT fehlgeschlagen: %s", errmsg);
        sqlite3_free(errmsg);
        db_savepoint_end(sond_index_ctx, FALSE);
        g_ptr_array_unref(segs);
        return;
    }

    if (full_done) {
        /* Bei einer normalen (nicht auf
         * den Header beschränkten) Ganze-Datei-Indizierung einer E-Mail
         * zusätzlich den Header UNTER SEINEM EIGENEN Pfad ("filename//header")
         * indizieren - per rekursivem Selbstaufruf mit
         * gmessage_header_only=TRUE (is_header_only verhindert dort eine
         * weitere Rekursion). Jeder einzelne Mimepart (auch Anhänge) hat
         * bereits einen eigenen Coverage-Eintrag "filename//N", weil
         * process_gmessage_for_ocr()/gmessage_process_part()
         * (sond_process_file.c) für jedes MIME-Leaf sond_process_file_do_rec()
         * aufrufen, das in sond_index() mündet (sofern der MIME-Typ
         * unterstützt wird). Mit "filename//header" ist der Satz an
         * Kind-Einträgen (Header + jeder Mimepart) vollständig - das
         * GMessage-bewusste Collapse kann "filename" damit auch bei einem
         * "Gesamtes Projekt"-Lauf erreichen (bei flacher Multipart-Struktur
         * ohne verschachtelte Multiparts, s. is_gmessage_child_segment()). */
        if (!is_header_only && !g_strcmp0(mime_type, "message/rfc822"))
            sond_index(ctx, log_func, log_func_data, sond_index_ctx, filename,
                    buf, size, mime_type, seite_von, seite_bis, ocr_mode,
                    cancel, TRUE, FALSE);
    }

    g_ptr_array_unref(segs);
}
