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
typedef struct {
	fz_context* ctx;
	void(*log_func)(void*, gchar const*, ...);
	gpointer log_func_data;
    SondOcrPool  *ocr_pool;   /* NULL → keine OCR         */
    SondIndexCtx *index_ctx;  /* NULL → keine Indizierung */
} SondProcessFileCtx;

void sond_process_file(SondProcessFileCtx* wctx,
		guchar* data, gsize size, gchar const* filename,
		guchar** out_data, gsize* out_size, gint* out_pdf_count);


#endif /* SRC_SOND_PROCESS_FILE_H_ */
