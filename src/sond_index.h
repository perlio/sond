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
 * Löscht alle vorhandenen Chunks und pages-Einträge für filename
 * (und alle Unter-Pfade, d.h. LIKE 'filename//%') vor der Neuindizierung.
 * Erwartet eine bereits laufende Transaktion (kein eigenes BEGIN/COMMIT).
 *
 * Returns: TRUE bei Erfolg.
 */
gboolean sond_index_ctx_clear_file(SondIndexCtx *ctx,
                                    gchar const  *filename,
                                    GError      **error);

/**
 * sond_index_ctx_clear_page:
 * @ctx:      SondIndexCtx
 * @filename: Dateiname
 * @page_nr:  Seite (0-basiert)
 * @error:    GError
 *
 * Löscht Chunks und pages-Eintrag nur für genau diese eine Seite (z.B.
 * nach OCR einer einzelnen Seite - die Seitenzahl selbst ändert sich
 * dabei nicht, nur ihr Inhalt). Erwartet eine bereits laufende
 * Transaktion (kein eigenes BEGIN/COMMIT).
 *
 * Returns: TRUE bei Erfolg.
 */
gboolean sond_index_ctx_clear_page(SondIndexCtx *ctx,
                                    gchar const  *filename,
                                    gint          page_nr,
                                    GError      **error);

/**
 * sond_index_ctx_renumber_page:
 * @ctx:         SondIndexCtx
 * @filename:    Dateiname
 * @old_page_nr: bisherige Seitenzahl
 * @new_page_nr: neue Seitenzahl (nach Seiten-Löschen/-Einfügen)
 * @error:       GError
 *
 * Bequemlichkeitswrapper um sond_index_ctx_renumber_pages() für genau
 * eine Seite. ACHTUNG: werden in einem Rutsch MEHRERE Seiten derselben
 * Datei umnummeriert, sond_index_ctx_renumber_pages() (Plural) benutzen,
 * nicht mehrere Einzelaufrufe hiervon - sonst können Zwischenzustände
 * kollidieren (siehe dort).
 *
 * Returns: TRUE bei Erfolg.
 */
gboolean sond_index_ctx_renumber_page(SondIndexCtx *ctx,
                                       gchar const  *filename,
                                       gint          old_page_nr,
                                       gint          new_page_nr,
                                       GError      **error);

/**
 * sond_index_ctx_renumber_pages:
 * @ctx:          SondIndexCtx
 * @filename:     Dateiname
 * @old_page_nrs: bisherige Seitenzahlen
 * @new_page_nrs: zugehörige neue Seitenzahlen (gleiche Länge/Reihenfolge
 *                wie old_page_nrs)
 * @n:            Anzahl Einträge
 * @error:        GError
 *
 * Setzt page_nr in chunks und pages für mehrere Seiten auf einen Schlag
 * um (Inhalt/Chunks bleiben erhalten - nur die Seitenzahl wird
 * korrigiert), z.B. nach Seiten-Löschen/-Einfügen im Viewer, wenn sich
 * mehrere Seitenzahlen gleichzeitig verschieben. Kollisionssicher
 * UNABHÄNGIG von der Reihenfolge der Einträge (zwei komplett getrennte
 * Durchgänge über alle Seiten, mit Zwischenwert). Erwartet eine bereits
 * laufende Transaktion (kein eigenes BEGIN/COMMIT).
 *
 * Returns: TRUE bei Erfolg.
 */
gboolean sond_index_ctx_renumber_pages(SondIndexCtx *ctx,
                                        gchar const  *filename,
                                        gint const   *old_page_nrs,
                                        gint const   *new_page_nrs,
                                        guint         n,
                                        GError      **error);

/**
 * sond_index_ctx_get_pages_for_file:
 * @ctx:      SondIndexCtx
 * @filename: Dateiname
 *
 * Liefert alle Seiten (page_nr), für die diese Datei aktuell Einträge in
 * der pages-Tabelle hat - z.B. um sie beim Speichern eines im Viewer
 * bearbeiteten PDF (Seiten gelöscht/eingefügt) gezielt umzunummerieren
 * oder zu verwerfen (siehe sond_index_ctx_renumber_page/_clear_page).
 *
 * Returns: (transfer full) neu alloziertes GArray von gint, mit
 *          g_array_unref() freizugeben. Leer (nicht NULL), wenn die Datei
 *          nicht (seitenweise) indiziert ist.
 */
GArray* sond_index_ctx_get_pages_for_file(SondIndexCtx *ctx,
                                           gchar const  *filename);

/**
 * sond_index_ctx_get_page_ocr_mode:
 * @ctx:      SondIndexCtx
 * @filename: Dateiname
 * @page_nr:  Seite (0-basiert; -1 für Nicht-PDF-Formate)
 *
 * Returns: zuletzt für diese Seite angewandter OCR-Modus (SondOcrMode-Wert),
 *          oder -1, wenn die Seite noch nie indiziert wurde.
 */
gint sond_index_ctx_get_page_ocr_mode(SondIndexCtx *ctx,
                                       gchar const  *filename,
                                       gint          page_nr);

/**
 * sond_index_ctx_should_process_page:
 * @ctx:            SondIndexCtx
 * @filename:        Dateiname
 * @page_nr:         Seite (0-basiert; -1 für Nicht-PDF-Formate)
 * @requested_mode:  gewünschter OCR-Modus für den aktuellen Lauf (SondOcrMode)
 *
 * Entscheidet, ob eine Seite (neu) verarbeitet werden muss, um doppelte
 * Arbeit zu vermeiden (z.B. wenn mehrere ausgewählte Punkte sich
 * überschneidende Seiten derselben Datei referenzieren, oder die Seite in
 * einem früheren Lauf schon ausreichend behandelt wurde):
 *   - erzwingen (SOND_OCR_MODE_FORCE): immer TRUE.
 *   - sonst: TRUE nur, wenn die Seite noch nie oder mit einem niedrigeren
 *     Modus als @requested_mode behandelt wurde.
 *
 * Returns: TRUE, wenn die Seite (neu) verarbeitet werden soll.
 */
gboolean sond_index_ctx_should_process_page(SondIndexCtx *ctx,
                                             gchar const  *filename,
                                             gint          page_nr,
                                             gint          requested_mode);

/**
 * sond_index_ctx_rename_file:
 * @ctx:        SondIndexCtx
 * @prefix_old: Alter Pfad-Präfix
 * @prefix_new: Neuer Pfad-Präfix
 * @error:      GError
 *
 * Benennt in chunks und pages alle Einträge um, deren filename gleich
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
 * @seite_von: erste zu indizierende Seite (0-basiert), -1 = ganze Datei
 *             (nur für PDF relevant; bei anderen MIME-Typen ignoriert)
 * @seite_bis: letzte zu indizierende Seite (0-basiert, inklusive),
 *             -1 = ganze Datei
 * @ocr_mode:  angewandter OCR-Modus (SondOcrMode-Wert), wird pro indizierter
 *             Seite in der pages-Tabelle vermerkt, um künftige Läufe
 *             doppelte Arbeit sparen zu lassen (siehe
 *             sond_index_ctx_should_process_page())
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
                        gchar const   *mime_type,
                        gint           seite_von,
                        gint           seite_bis,
                        gint           ocr_mode);

G_END_DECLS

#endif /* SRC_SOND_SERVER_INDEX_H_ */
