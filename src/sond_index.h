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
 * sond_index_file_needs_update:
 * @ctx:      SondIndexCtx
 * @filename: vollständiger filepart-Pfad (wie in chunks)
 * @mtime:    aktueller mtime der Wurzel-FS-Datei
 *
 * Returns: TRUE wenn Datei (neu) indiziert werden soll.
 */
gboolean sond_index_file_needs_update(SondIndexCtx *ctx,
                                       gchar const  *filename,
                                       gint64        mtime);

/**
 * sond_index_file_update_root_mtime:
 * @ctx:          SondIndexCtx
 * @fs_root_path: Pfad der Wurzel-FS-Datei (wie in sond_file_part_get_path des obersten sfp)
 * @mtime:        neuer mtime
 *
 * Setzt den mtime aller files-Einträge, deren filename gleich fs_root_path
 * ist oder mit fs_root_path// beginnt, auf mtime.
 */
void sond_index_file_update_root_mtime(SondIndexCtx *ctx,
                                        gchar const  *fs_root_path,
                                        gint64        mtime);

/**
 * sond_index_get_outdated_files:
 * @ctx:   SondIndexCtx
 * @error: GError
 *
 * Gibt alle filename-Einträge aus files zurück, deren gespeicherter mtime
 * nicht mehr mit dem aktuellen mtime der Wurzel-FS-Datei übereinstimmt.
 * Die Wurzel-FS-Datei ist der Teil vor dem ersten "//" im filename,
 * bzw. der filename selbst wenn kein "//" vorhanden.
 *
 * Returns: (transfer full) GPtrArray* von gchar* (filename), oder NULL bei Fehler.
 */
GPtrArray* sond_index_get_outdated_files(SondIndexCtx *ctx, GError **error);

/**
 * sond_index_ctx_clear_file:
 * @ctx:      SondIndexCtx
 * @filename: Dateiname (wie in dispatch_buffer)
 * @error:    GError
 *
 * Löscht alle vorhandenen Chunks für filename (und alle Unter-Pfade,
 * d.h. LIKE 'filename%') vor der Neuindizierung.
 *
 * Returns: TRUE bei Erfolg.
 */
gboolean sond_index_ctx_clear_file(SondIndexCtx *ctx,
                                    gchar const  *filename,
                                    GError      **error);

/**
 * sond_index_file_needs_update:
 * @ctx:      SondIndexCtx
 * @filename: Vollständiger filepart-Pfad (wie in chunks)
 * @mtime:    Aktueller mtime der Wurzel-FS-Datei
 *
 * Prüft ob filename in der files-Tabelle fehlt oder einen abweichenden mtime hat.
 *
 * Returns: TRUE wenn neu indiziert werden muss.
 */
gboolean sond_index_file_needs_update(SondIndexCtx *ctx,
                                       gchar const  *filename,
                                       gint64        mtime);

/**
 * sond_index_file_update_root_mtime:
 * @ctx:          SondIndexCtx
 * @fs_root_path: Pfad der Wurzel-FS-Datei (ohne Trennzeichen)
 * @mtime:        Neuer mtime
 *
 * Setzt alle Einträge in files, deren filename mit fs_root_path beginnt,
 * auf den neuen mtime. Damit bleiben bereits indizierte eingebettete Dateien
 * gültig, wenn sich nur der Container-mtime durch eine Teiländerung verändert hat.
 */
void sond_index_file_update_root_mtime(SondIndexCtx *ctx,
                                        gchar const  *fs_root_path,
                                        gint64        mtime);

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
