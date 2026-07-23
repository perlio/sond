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
 *
 * Seitenbereich, auf den Indizierung/OCR für eine Datei beschränkt werden
 * soll (z.B. weil nur eine an einen Baum-Punkt angebundene Teilstrecke
 * einer großen PDF interessiert). Wird als Wert in der GHashTable an
 * sond_process_fileparts() übergeben - ein NULL-Wert bedeutet "ganze Datei".
 */
typedef struct _SondPageRange {
    gint von;
    gint bis;
} SondPageRange;

SondPageRange* sond_page_range_new(gint von, gint bis);
void sond_page_range_free(gpointer p);

void sond_process_file(SondProcessFileCtx* wctx,
		guchar* data, gsize size, gchar const* filename,
		guchar** out_data, gsize* out_size, gint* out_pdf_count,
		gint seite_von, gint seite_bis);

void sond_process_fileparts(SondProcessFileCtx* wctx, GHashTable* files);

SondProcessFileCtx* sond_process_file_create_wctx(fz_context* ctx,
		void (*log_func)(void*, gchar const*, ...), gpointer log_func_data,
		gchar const* tessdata_path, gint num_ocr_threads,
		gchar const* index_db_filename, GError **error);

void sond_process_file_destroy_wctx(SondProcessFileCtx*);

#endif /* SRC_SOND_PROCESS_FILE_H_ */
