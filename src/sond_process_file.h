/*
 sond (sond_process_file.h) - Akten, Beweisstücke, Unterlagen
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

#ifndef SRC_SOND_PROCESS_FILE_H_
#define SRC_SOND_PROCESS_FILE_H_

#include <mupdf/fitz.h>

typedef struct _SondOcrPool SondOcrPool;
typedef struct _SondIndexCtx SondIndexCtx;
typedef struct _GError GError;
typedef struct _GHashTable GHashTable;
typedef char gchar;
typedef unsigned char guchar;
typedef void* gpointer;
typedef long long unsigned gsize;
typedef int gint;
typedef int gboolean;

/**
 * SondProcessFileCtx:
 *
 * Übergeordnete Struktur, die OCR-Pool und Index-Kontext zusammenfasst.
 * Wird als einziger Kontext-Parameter durch dispatch_buffer und alle
 * process_*-Funktionen durchgeschleift.
 * Jedes Feld kann NULL sein — dann wird der jeweilige Schritt übersprungen.
 */
typedef struct _SondProcessFileCtx {
	fz_context* ctx;
	gint cancel;
	gint progress;
	void(*log_func)(void*, gchar const*, ...);
	gpointer log_func_data;
    SondOcrPool  *ocr_pool;   /* NULL → keine OCR         */
    SondIndexCtx *index_ctx;  /* NULL → keine Indizierung */
    /* Wurzel des Projektverzeichnisses (zond->project_dir) - Obergrenze für
     * das Hochprüfen/Coalescen in der coverage-Tabelle nach erfolgreich
     * indizierten Dateien (sond_process_fileparts()). NULL → kein
     * Coalescing (z.B. wenn kein Projekt zugeordnet ist). */
    gchar *project_dir;
    /* Umgang mit bereits vorhandenem verstecktem Text beim OCRen.
     * Werte entsprechen SondOcrMode (sond_ocr.h):
     *   0 = SOND_OCR_MODE_NONE  - kein OCR
     *   1 = SOND_OCR_MODE_CHECK - prüfen, Seite ggf. überspringen (Default)
     *   2 = SOND_OCR_MODE_FORCE - versteckten Text löschen, neu OCRen
     * Als gint gehalten, damit dieser Header ohne sond_ocr.h auskommt. */
    gint ocr_mode;
} SondProcessFileCtx;

/**
 * SondPageRange:
 * @von: erste Seite (0-basiert), -1 = ganze Datei
 * @bis: letzte Seite (0-basiert, inklusive), -1 = ganze Datei
 * @gmessage_header_only: TRUE, wenn dieser Eintrag NICHT einen
 *         Seitenbereich beschreibt, sondern den "Message"-Knoten einer
 *         E-Mail - dort soll (statt der ganzen Mail) nur der Header
 *         (Von/An/CC/BCC/Betreff/Datum) indiziert werden. von/bis sind in
 *         diesem Fall irrelevant (-1/-1). S. ToDo.c, 17.09.2026,
 *         E-Mail-Coverage-Redesign (Schritt 2/6).
 * @pdf_pagetree_only: TRUE, wenn nur die Seiten (PageTree) einer PDF
 *         gemeint sind, ohne die eingebetteten Dateien (die sind eigene
 *         Fileparts "x.pdf//anhang.pdf"). von/bis = -1/-1. Abgedeckt wird
 *         unter dem Coverage-Schlüssel "x.pdf//", Chunks/pages bleiben
 *         unter "x.pdf". S. ToDo.c #191.
 *
 * Seitenbereich, auf den Indizierung/OCR für eine Datei beschränkt werden
 * soll (z.B. weil nur eine an einen Baum-Punkt angebundene Teilstrecke
 * einer großen PDF interessiert). Wird als Wert in der GHashTable an
 * sond_process_fileparts() übergeben - ein NULL-Wert bedeutet "ganze Datei"
 * (bei PDF: Seiten und alle eingebetteten Dateien). Ein Seitenbereich
 * (von >= 0) umfasst nie eingebettete Dateien.
 */
typedef struct _SondPageRange {
    gint von;
    gint bis;
    gboolean gmessage_header_only;
    gboolean pdf_pagetree_only;
    /* angebundene Mail in BAUM_INHALT/_AUSWERTUNG: Header + Inline-Teile
     * (Mimeparts ohne Content-Disposition "attachment"), von/bis -1/-1.
     * Kein eigener Coverage-Schlüssel - Header ("x.eml//header") und
     * Inline-Teile ("x.eml//N") werden einzeln abgedeckt. S. ToDo.c #197. */
    gboolean gmessage_message;
} SondPageRange;

SondPageRange* sond_page_range_new(gint von, gint bis);

/* Für die Anbindung des "Message"-Knotens einer E-Mail (s.o.) - erzeugt
 * einen Range-Eintrag mit von=bis=-1 und gmessage_header_only=TRUE. */
SondPageRange* sond_page_range_new_gmessage_header(void);

/* Nur die Seiten einer PDF (s.o.) - von=bis=-1, pdf_pagetree_only=TRUE. */
SondPageRange* sond_page_range_new_pdf_pagetree(void);

/* Angebundene Mail, Header + Inline-Teile (s.o.) - gmessage_message=TRUE. */
SondPageRange* sond_page_range_new_gmessage_message(void);

/* Kopie inkl. aller Flags; NULL bleibt NULL. */
SondPageRange* sond_page_range_copy(SondPageRange const *range);

void sond_page_range_free(gpointer p);

/* range: NULL = ganze Datei, sonst Seitenbereich bzw. Teil wie oben */
void sond_process_file(SondProcessFileCtx* wctx,
		guchar* data, gsize size, gchar const* filename,
		guchar** out_data, gsize* out_size, gint* out_pdf_count,
		SondPageRange const* range);

void sond_process_fileparts(SondProcessFileCtx* wctx, GHashTable* files);

/* Struktur eines Containers in der Index-DB festhalten: Anhänge einer PDF
 * (pdf_embedded) bzw. Mimeparts einer Mail (container_entrycount,
 * gmessage_inline). Andere Typen: nichts zu tun. Liest die Datei. ToDo.c
 * #199. */
typedef struct _SondFilePart SondFilePart;
gint sond_process_file_record_structure(SondIndexCtx* index_ctx,
		SondFilePart* container, GError** error);

SondProcessFileCtx* sond_process_file_create_wctx(fz_context* ctx,
		void (*log_func)(void*, gchar const*, ...), gpointer log_func_data,
		gchar const* tessdata_path, gint num_ocr_threads,
		gchar const* index_db_filename, gchar const* embedding_model_path,
		gchar const* project_dir, GError **error);

void sond_process_file_destroy_wctx(SondProcessFileCtx*);

#endif /* SRC_SOND_PROCESS_FILE_H_ */
