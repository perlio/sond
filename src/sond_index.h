/*
 sond (sond_server_index.h) - Akten, Beweisstücke, Unterlagen
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

#ifndef SRC_SOND_SERVER_INDEX_H_
#define SRC_SOND_SERVER_INDEX_H_

#include <glib.h>
#include <sqlite3.h>
#include <mupdf/fitz.h>

G_BEGIN_DECLS

/**
 * SondIndexCtx:
 *
 * Kontext für die Indizierung von Dateien.
 * NULL bedeutet: keine Indizierung gewünscht.
 *
 * @db:            SQLite-Datenbankverbindung (lokale Arbeitskopie)
 * @db_path:       Pfad zur lokalen SQLite-Datei
 * @llama_model:   llama.cpp-Modell für Embeddings (opaker Zeiger)
 * @llama_ctx:     llama.cpp-Kontext für Embedding-Berechnungen (opaker Zeiger)
 * @n_embd:        Dimension der Embedding-Vektoren
 * @chunk_size:    Maximale Chunk-Größe in Zeichen
 * @chunk_overlap: Überlappung zwischen Chunks in Zeichen
 */
typedef struct _SondIndexCtx {
    sqlite3  *db;
    gchar    *db_path;
#ifdef SOND_WITH_EMBEDDINGS
    gpointer  llama_model;   /* struct llama_model*   - opak */
    gpointer  llama_ctx;     /* struct llama_context* - opak */
    gint      n_embd;
#endif
    gint      chunk_size;
    gint      chunk_overlap;
} SondIndexCtx;

/* =======================================================================
 * SondIndexCtx - Lebenszyklus
 * ======================================================================= */

/**
 * sond_index_ctx_new:
 * @db_path:       Pfad zur lokalen SQLite-Datei (wird erstellt falls nicht vorhanden)
 * @model_path:    Pfad zum GGUF-Embedding-Modell (NULL: kein Embedding)
 * @chunk_size:    Maximale Chunk-Größe in Zeichen (0: Standardwert 1000)
 * @chunk_overlap: Überlappung in Zeichen (0: Standardwert 100)
 * @error:         GError
 *
 * Öffnet/erstellt SQLite-DB, legt Tabellen an, lädt das llama-Modell.
 *
 * Returns: (transfer full) neuer SondIndexCtx, oder NULL bei Fehler.
 */
SondIndexCtx* sond_index_ctx_new(gchar const *db_path,
                                  gchar const *model_path,
                                  gint         chunk_size,
                                  gint         chunk_overlap,
                                  GError     **error);

/**
 * sond_index_ctx_free:
 * @ctx: SondIndexCtx
 *
 * Schließt DB und llama-Kontext und gibt alle Ressourcen frei.
 */
void sond_index_ctx_free(SondIndexCtx *ctx);

/**
 * sond_index_ctx_clear_file:
 * @ctx:      SondIndexCtx
 * @filename: Dateiname (wie in dispatch_buffer)
 * @error:    GError
 *
 * Löscht alle vorhandenen Chunks und files-Einträge für filename
 * (und alle Unter-Pfade, d.h. LIKE 'filename//%') vor der Neuindizierung.
 * Erwartet eine bereits laufende Transaktion (kein eigenes BEGIN/COMMIT).
 *
 * Returns: TRUE bei Erfolg.
 */
gboolean sond_index_ctx_clear_file(SondIndexCtx *ctx,
                                    gchar const  *filename,
                                    GError      **error);

/**
 * sond_index_ctx_rename_file:
 * @ctx:        SondIndexCtx
 * @prefix_old: Alter Pfad-Präfix
 * @prefix_new: Neuer Pfad-Präfix
 * @error:      GError
 *
 * Benennt in chunks und files alle Einträge um, deren filename gleich
 * prefix_old ist oder mit prefix_old// beginnt.
 * Erwartet eine bereits laufende Transaktion (kein eigenes BEGIN/COMMIT).
 *
 * Returns: TRUE bei Erfolg.
 */
gboolean sond_index_ctx_rename_file(SondIndexCtx *ctx,
                                     gchar const  *prefix_old,
                                     gchar const  *prefix_new,
                                     GError      **error);

/* =======================================================================
 * Volltextsuche
 * ======================================================================= */

/**
 * SondIndexHit:
 *
 * Ein einzelner Suchtreffer aus sond_index_search().
 * Alle Felder sind neu alloziert und müssen mit sond_index_hit_free()
 * bzw. über das GPtrArray-free_func freigegeben werden.
 */
typedef struct _SondIndexHit {
    gchar *filename;         /* filepart-Pfad, wie in chunks gespeichert          */
    gint   page_nr;          /* Seitennummer (-1 wenn nicht zutreffend)           */
    gint   char_pos;         /* Zeichenposition im Dokument                       */
    gint   char_pos_in_page; /* Byte-Offset des Treffers innerhalb der Seite      */
    gchar *snippet;          /* Kontextausschnitt mit markierten Treffern         */
} SondIndexHit;

/**
 * sond_index_hit_free:
 * Gibt einen einzelnen SondIndexHit frei.
 */
void sond_index_hit_free(gpointer p);

/**
 * sond_index_search:
 * @ctx:     SondIndexCtx
 * @term:    Hauptsuchbegriff (ein oder mehrere Wörter → Phrasensuche)
 * @context: Optionaler Kontext-Begriff (AND-Verknüpfung auf Chunk-Ebene),
 *           oder NULL für reine Begriffssuche
 * @error:   GError
 *
 * Durchsucht chunks_fts nach @term. Wenn @context angegeben ist, müssen
 * beide Begriffe im selben Chunk vorkommen (AND-Semantik).
 * Mehrere Wörter in @term werden als Phrase gesucht.
 *
 * Returns: (transfer full) GPtrArray* von SondIndexHit*, nach filename
 *          und page_nr sortiert. NULL bei Fehler.
 *          Leeres Array wenn keine Treffer.
 */
GPtrArray* sond_index_search(SondIndexCtx *ctx,
                              gchar const  *term,
                              gchar const  *context,
                              GError      **error);

/* =======================================================================
 * Einstiegspunkt aus dispatch_buffer
 * ======================================================================= */

/**
 * sond_server_index:
 * @ctx:       SondIndexCtx (NULL → sofortiger Rücksprung)
 * @filename:  Dateiname/Pfad der Datei
 * @buf:       Rohdaten (bei PDF: bereits OCR-ter Buffer)
 * @size:      Größe von buf
 * @mime_type: MIME-Typ (entscheidet ob und wie indiziert wird)
 *
 * Indiziert wird für:
 *   application/pdf   – Text aus OCR-tem PDF (MuPDF stext)
 *   message/rfc822    – Header + Textteile (GMime)
 *   text*             – Rohtext direkt
 * Alle anderen MIME-Typen: sofortiger Rücksprung.
 */
void sond_index(fz_context* ctx,
		void (*log_func)(void*, gchar const*, ...),
		gpointer log_func_data, SondIndexCtx  *sond_index_ctx,
                        gchar const   *filename,
                        guchar const  *buf,
                        gsize          size,
                        gchar const   *mime_type);

G_END_DECLS

#endif /* SRC_SOND_SERVER_INDEX_H_ */
