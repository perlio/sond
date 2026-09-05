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
 * @embedding_model_changed:
 *                 TRUE, wenn beim Öffnen festgestellt wurde, daß das jetzt
 *                 konfigurierte Embedding-Modell von dem abweicht, mit dem
 *                 die in der DB gespeicherten Embeddings zuletzt berechnet
 *                 wurden (Name oder Dimension unterschiedlich). Vorhandene
 *                 Embeddings sind dann nicht mehr mit neu berechneten
 *                 vergleichbar und müssen neu berechnet werden, siehe
 *                 sond_index_ctx_embedding_model_changed().
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
    gboolean  embedding_model_changed;
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
 * sond_index_ctx_embedding_model_changed:
 * @ctx: SondIndexCtx
 *
 * Siehe @embedding_model_changed in SondIndexCtx. Aufrufer sollten bei TRUE
 * ein Re-Embedding der vorhandenen Chunks anstoßen (bestehende Embeddings
 * stammen von einem anderen Modell und dürfen nicht mit neu berechneten
 * verglichen werden), statt sie unverändert weiterzubenutzen.
 *
 * Returns: FALSE, wenn ctx NULL ist oder kein Modellwechsel festgestellt
 *          wurde.
 */
gboolean sond_index_ctx_embedding_model_changed(SondIndexCtx *ctx);

/**
 * sond_index_ctx_has_embeddings:
 * @ctx: SondIndexCtx
 *
 * Ein fehlendes/nicht ladbares Embedding-Modell (Datei fehlt, da bewußt
 * nicht mit dem Release ausgeliefert - der Nutzer lädt sie sich selbst
 * herunter) läßt sond_index_ctx_new() nicht scheitern, sondern deaktiviert
 * nur die Embedding-Funktion für diese Sitzung. Aufrufer, die eine
 * semantische Suche/Chat-Funktion anbieten, sollten dies hiermit prüfen und
 * dem Nutzer eine klare Meldung zeigen (welche Datei fehlt, woher zu
 * bekommen), statt die Funktion kommentarlos verschwinden zu lassen.
 *
 * Returns: TRUE, wenn ein Embedding-Modell erfolgreich geladen ist.
 */
gboolean sond_index_ctx_has_embeddings(SondIndexCtx *ctx);

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
 * sond_index_ctx_coverage_get:
 * @ctx:  SondIndexCtx
 * @path: Datei- oder Verzeichnispfad
 *
 * Liefert den Modus, mit dem path oder der nächstgelegene abdeckende
 * Vorfahre als vollständig indiziert vermerkt ist (coalescierte
 * "coverage"-Tabelle, eine Ebene oberhalb von "pages" - siehe
 * SQL_CREATE_COVERAGE in sond_index.c).
 *
 * Returns: SondOcrMode-Wert, oder -1, wenn path nicht (auch nicht über
 * einen Vorfahren) als abgedeckt vermerkt ist.
 */
gint sond_index_ctx_coverage_get(SondIndexCtx *ctx, gchar const *path);

/**
 * SondIndexStatus:
 *
 * Ternärer Indizierungs-Status eines "Punkts" (Datei, Verzeichnis, oder
 * Anbindung mit Seitenbereich innerhalb einer Datei) für die Overlay-Icons
 * in den Bäumen (BAUM_FS und ggf. weitere).
 */
typedef enum {
    SOND_INDEX_STATUS_NONE = 0,   /* nicht indiziert */
    SOND_INDEX_STATUS_PARTIAL,    /* teilweise indiziert */
    SOND_INDEX_STATUS_FULL        /* vollständig indiziert */
} SondIndexStatus;

/**
 * sond_index_ctx_get_file_status:
 * @ctx:        SondIndexCtx
 * @filename:   Dateipfad (filepart-Konvention, wie in coverage/pages
 *              gespeichert)
 * @von_seite:  erste Seite einer Anbindung (0-basiert), oder -1 für
 *              "ganze Datei" (auch für Nicht-PDF-Formate)
 * @bis_seite:  letzte Seite einer Anbindung (0-basiert, inklusive),
 *              ignoriert wenn @von_seite == -1
 *
 * Rein DB-basiert, ohne die Datei zu öffnen: bei "ganze Datei" reicht für
 * NONE/PARTIAL die reine Existenzfrage ("gibt es überhaupt eine indizierte
 * Seite"), FULL kommt ausschließlich aus der coalescierten coverage-
 * Tabelle. Bei einer Anbindung (expliziter Seitenbereich) wird zusätzlich
 * gezählt, wie viele der angeforderten Seiten schon einen pages-Eintrag
 * haben.
 *
 * Returns: SondIndexStatus.
 */
SondIndexStatus sond_index_ctx_get_file_status(SondIndexCtx *ctx,
                                                gchar const  *filename,
                                                gint          von_seite,
                                                gint          bis_seite);

/**
 * sond_index_ctx_get_dir_status:
 * @ctx:  SondIndexCtx
 * @path: Verzeichnispfad (filepart-Konvention)
 *
 * Aggregiert über alle Dateien unterhalb von @path per Präfix-Abfrage
 * (schnell, da "pages.filename" bzw. "coverage.path" jeweils führende
 * Spalte ihres Primärschlüssels sind - kein Tabellen-Scan). FULL kommt aus
 * der coverage-Tabelle (path selbst oder ein Vorfahre abgedeckt); sonst
 * PARTIAL, wenn irgendetwas unterhalb von path einen pages- oder
 * coverage-Eintrag hat, sonst NONE.
 *
 * Returns: SondIndexStatus.
 */
SondIndexStatus sond_index_ctx_get_dir_status(SondIndexCtx *ctx,
                                               gchar const  *path);

/**
 * sond_index_ctx_coverage_mark:
 * @ctx:      SondIndexCtx
 * @path:     Datei- oder Verzeichnispfad
 * @ocr_mode: SondOcrMode-Wert
 * @error:    GError
 *
 * Markiert path als vollständig mit ocr_mode indiziert (Coalescing).
 * Löscht dabei automatisch überflüssig gewordene, feinere Einträge
 * unterhalb von path (sowohl in "coverage" als auch in "pages").
 *
 * Returns: FALSE bei Datenbankfehler.
 */
gboolean sond_index_ctx_coverage_mark(SondIndexCtx *ctx, gchar const *path,
                                       gint ocr_mode, GError **error);

/**
 * sond_index_ctx_coverage_clear:
 * @ctx:   SondIndexCtx
 * @path:  Datei- oder Verzeichnispfad, der gerade GELÖSCHT wurde
 * @error: GError
 *
 * Entfernt jeden coverage-Eintrag für genau path sowie für alles, was
 * (per einfachem "/"-Präfix) darunter liegt - reine Fall-1-Bereinigung,
 * OHNE einen eventuell abdeckenden VORFAHREN anzutasten.
 *
 * Bewusst kein Aufbrechen eines Vorfahren-Eintrags (im Unterschied zu
 * sond_index_ctx_coverage_invalidate()): reines Löschen von Inhalt kann
 * die Aussage "unter Vorfahre X ist alles mindestens Modus M" niemals
 * verletzen - es verschwindet nur Inhalt, es kommt kein neuer,
 * ungeprüfter Inhalt hinzu. Diese Funktion ist also nur eine
 * Vorsichtsmaßnahme gegen später am selben Pfad neu auftauchenden,
 * komplett anderen Inhalt (der sonst fälschlich als "schon abgedeckt"
 * gälte).
 *
 * Returns: FALSE bei Datenbankfehler.
 */
gboolean sond_index_ctx_coverage_clear(SondIndexCtx *ctx,
                                        gchar const *path,
                                        GError **error);

/**
 * sond_index_ctx_coverage_expand_to_pages:
 * @ctx:              SondIndexCtx
 * @filename:         Dateiname, der gerade einen EIGENEN (nicht nur über
 *                     einen Vorfahren geerbten) coverage-Eintrag hat oder
 *                     haben könnte
 * @pages_to_write:   Seiten (0-basiert = page_akt), die einzeln
 *                     eingetragen werden sollen - typischerweise alle
 *                     aktuell noch existierenden (nicht gelöschten)
 *                     Seiten AUSSER den gerade neu eingefügten
 * @n_pages_to_write: Anzahl Einträge in pages_to_write
 * @error:            GError
 *
 * Gegenstück zu sond_index_ctx_coverage_mark(): hat filename einen
 * eigenen coverage-Eintrag (Modus M), werden für jede Seite in
 * pages_to_write individuelle "pages"-Zeilen mit Modus M angelegt - der
 * bisher durch Kollabieren "verdichtete" Seiten-Fortschritt wird also vor
 * dem Verwerfen des Datei-Eintrags (s. sond_index_ctx_coverage_invalidate(),
 * im Anschluss aufzurufen) gerettet, statt verloren zu gehen.
 *
 * Bewusst eine explizite Seitenliste statt "Gesamtzahl + Ausschlussliste":
 * gelöschte Seiten bleiben im Viewer als stabile "Karteileichen" im
 * Seiten-Array stehen (keine Kompaktierung mehr) - die rohe Seitenzahl
 * (zond_pdf_document_get_number_of_pages()) ist deshalb NICHT die Anzahl
 * tatsächlich noch existierender Seiten. Der Aufrufer muss die Liste
 * daher unter Berücksichtigung von PdfDocumentPage->deleted selbst bilden.
 *
 * Hat filename keinen eigenen Eintrag (z.B. nur über einen Vorfahren
 * abgedeckt), passiert nichts - dieser Fall wird bereits vollständig von
 * sond_index_ctx_coverage_invalidate() (Fall 2, Geschwister-Dateien)
 * abgedeckt.
 *
 * Returns: FALSE bei Datenbankfehler.
 */
gboolean sond_index_ctx_coverage_expand_to_pages(SondIndexCtx *ctx,
                                                  gchar const  *filename,
                                                  gint const   *pages_to_write,
                                                  guint         n_pages_to_write,
                                                  GError      **error);

/**
 * sond_index_ctx_coverage_invalidate:
 * @ctx:   SondIndexCtx
 * @path:  Datei- oder Verzeichnispfad, unter dem gerade NEUER, noch nicht
 *         geprüfter Inhalt auftaucht (Datei/Verzeichnis wird hierher
 *         verschoben, Seiten werden in eine Datei eingefügt, ...)
 * @error: GError
 *
 * Entwertet path: danach hat path keinen coverage-Eintrag mehr (weder
 * direkt noch über einen Vorfahren). War path nur indirekt über einen
 * Vorfahren abgedeckt, wird dieser aufgelöst und auf jeder Zwischenebene
 * werden die (weiterhin gültigen) Geschwister per flachem
 * Verzeichnis-Listing neu eingetragen - kein rekursiver Scan.
 *
 * Im Unterschied zu sond_index_ctx_coverage_clear(): hier kommt neuer,
 * ungeprüfter Inhalt unter path hinzu, der die Aussage eines abdeckenden
 * Vorfahren ("alles darunter ist mindestens Modus M") tatsächlich
 * verletzen kann - deshalb muss der Vorfahre hier aufgelöst werden.
 *
 * Bekannte Einschränkung: setzt echte Dateisystem-Verzeichnisse zwischen
 * Vorfahre und path voraus (BAUM_FS) - eingebettete ("//"-)Pfade und die
 * Seiten-Ebene innerhalb einer bereits gemeinsam abgedeckten Datei sind
 * noch nicht vorgesehen.
 *
 * Returns: FALSE bei Datenbankfehler.
 */
gboolean sond_index_ctx_coverage_invalidate(SondIndexCtx *ctx,
                                             gchar const *path,
                                             GError **error);

/**
 * sond_index_ctx_coverage_try_collapse:
 * @ctx:      SondIndexCtx
 * @path:     Datei- oder Verzeichnispfad (projektrelativ), gerade per
 *            coverage_mark abgedeckt
 * @root_dir: absolute Projektwurzel (zond->project_dir) - wird nur
 *            gebraucht, um aus einem relativen Verzeichnis-Key einen
 *            echten Dateisystempfad für g_dir_open() zu bauen
 * @error:    GError
 *
 * Prüft von path aus schrittweise nach oben, ob jeweils alle Geschwister
 * (flaches Verzeichnis-Listing) irgendeinen coverage-Eintrag haben, und
 * fasst sie ggf. zu einem Eintrag für das Elternverzeichnis zusammen -
 * dessen ocr_mode ist dabei der MINDESTMODUS aller Geschwister (nicht der
 * Modus der zuletzt verarbeiteten Datei), s. coverage_mark-Kommentar zur
 * Mindestmodus-Konvention.
 *
 * Stoppt spätestens, wenn ein Eintrag bereits direkt im
 * Projektverzeichnis liegt - es wird nie zu einem einzigen
 * projektweiten Eintrag zusammengefasst (root_dir selbst wird nie als
 * coverage-Key verwendet).
 *
 * Returns: FALSE bei Datenbankfehler.
 */
gboolean sond_index_ctx_coverage_try_collapse(SondIndexCtx *ctx,
                                               gchar const *path,
                                               gchar const *root_dir,
                                               GError **error);

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
    gchar  *filename;         /* filepart-Pfad, wie in chunks gespeichert          */
    gint    page_nr;          /* Seitennummer (-1 wenn nicht zutreffend)           */
    gint    char_pos;         /* Zeichenposition im Dokument                       */
    gint    char_pos_in_page; /* Byte-Offset des Treffers innerhalb der Seite      */
    gchar  *snippet;          /* Kontextausschnitt mit markierten Treffern         */
    gdouble score;            /* nur bei sond_index_semantic_search(): Cosine-
                                * Ähnlichkeit (-1..1, höher = ähnlicher). Bei
                                * sond_index_search() immer 0 (unbenutzt). */
} SondIndexHit;

/**
 * sond_index_hit_free:
 * Gibt einen einzelnen SondIndexHit frei.
 */
void sond_index_hit_free(gpointer p);

/**
 * sond_index_search:
 * @ctx:        SondIndexCtx
 * @term:       Hauptsuchbegriff (ein oder mehrere Wörter → Phrasensuche)
 * @context:    Optionaler Kontext-Begriff (AND-Verknüpfung auf Chunk-Ebene),
 *              oder NULL für reine Begriffssuche
 * @whole_word: FALSE (Default/empfohlen): zusätzlich Präfix-Treffer, z.B.
 *              findet "Vertrag" auch "Vertragspartner" - bei einer Phrase
 *              gilt der Präfix nur für deren letztes Wort (FTS5-Grenze).
 *              TRUE: nur exakte, ganze Wörter (bisheriges Verhalten).
 * @error:      GError
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
                              gboolean      whole_word,
                              GError      **error);

/* =======================================================================
 * Semantische Suche (Embeddings)
 * ======================================================================= */

/**
 * sond_index_semantic_search:
 * @ctx:     SondIndexCtx
 * @query:   Anfragetext in natürlicher Sprache (wird mit dem konfigurierten
 *           Embedding-Modell embedded)
 * @top_k:   maximale Anzahl Treffer (<=0: Standardwert 15)
 * @error:   GError
 *
 * Embedded @query und vergleicht das Ergebnis per Cosine-Similarity
 * (brute-force in C, keine sqlite-vec/vec0-Abhängigkeit) gegen alle in
 * chunks gespeicherten Embeddings. Liefert die @top_k ähnlichsten Treffer,
 * absteigend nach SondIndexHit::score sortiert.
 *
 * Einschränkung auf eine Auswahl/Anbindung (wie bei sond_index_search()
 * durch den Aufrufer anhand von filename/page_nr) ist hier bewußt NICHT
 * eingebaut - der Aufrufer filtert das Ergebnis nach denselben Regeln wie
 * bei der Volltextsuche (siehe zond_indexsuche.c).
 *
 * Voraussetzung: sond_index_ctx_has_embeddings(ctx) - sonst wird ein
 * GError gesetzt und NULL zurückgegeben (kein stiller Leerlauf).
 *
 * Returns: (transfer full) GPtrArray* von SondIndexHit*, NULL bei Fehler.
 */
GPtrArray* sond_index_semantic_search(SondIndexCtx *ctx,
                                       gchar const  *query,
                                       gint          top_k,
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
                        gint           ocr_mode,
                        gint const    *cancel);

/**
 * sond_index_mime_type_supported:
 * @mime_type: zu prüfender MIME-Typ (kann NULL sein)
 *
 * Ja/Nein-Entsprechung zur Dispatch-Logik in sond_index() (s.o.) - welche
 * MIME-Typen dort überhaupt zu einer Extraktion führen, ohne selbst etwas
 * zu tun. Gedacht für Stellen, die VOR dem eigentlichen Indizieren wissen
 * müssen, ob eine Datei überhaupt in Frage kommt (z.B. der Abdeckungs-
 * Check in zond_indexsuche.c: Dateien, die ohnehin nie indiziert werden
 * - .db, .znd, Bilder, Archive außer docx/odt, ... - sollen dort nicht
 * als "fehlt noch" auftauchen).
 *
 * Returns: TRUE, wenn sond_index() für diesen MIME-Typ tatsächlich Text
 *          extrahiert; FALSE sonst (auch bei NULL).
 */
gboolean sond_index_mime_type_supported(gchar const *mime_type);

G_END_DECLS

#endif /* SRC_SOND_SERVER_INDEX_H_ */
