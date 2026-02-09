/*
 sond (sond_ocr.h) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2025  peloamerica

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

#ifndef SRC_SOND_OCR_H_
#define SRC_SOND_OCR_H_

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

typedef struct TessBaseAPI TessBaseAPI;
typedef struct _GError GError;

typedef void* gpointer;
typedef int gint;
typedef char gchar;
typedef int gboolean;

gint sond_ocr_set_content_stream(fz_context*, pdf_page*, fz_buffer*, GError**);

gint sond_ocr_page_has_hidden_text(fz_context*, pdf_page*,
		gboolean*, GError**);

gint sond_ocr_get_num_sond_font(fz_context*, pdf_document*, GError**);

pdf_obj* sond_ocr_put_sond_font(fz_context*, pdf_document*, GError**);

// Thread-Pool Struktur
typedef struct {
    GThreadPool *pool;
    GMutex mutex;
    volatile gboolean cancel_all;
    GPrivate thread_data_key;
    gchar *tessdata_path;
    gchar *language;
    fz_context* ctx;
    void (*log_func)(void*, gchar const*, ...);
    gpointer log_data;
    gint num_active_jobs;
} SondOcrPool;

// Transformations-Kontext für OCR-Koordinaten
typedef struct {
    float scale_x;
    float scale_y;
    int rotation;  // 0, 90, 180, 270
    float page_width;
    float page_height;
} OcrTransform;

// Struktur für OCR-Aufgaben
typedef struct {
	gint status; //bit 1 = started; bit 2 = done
	fz_pixmap* pixmap;
	float scale;
	gint durchgang;
	OcrTransform ocr_transform;
	fz_buffer* content;
} SondOcrTask;

gint sond_ocr_page(SondOcrPool* pool, pdf_page* page, pdf_obj* font_ref,
		SondOcrTask* task, TessBaseAPI* osd_api, GError** error);

gint sond_ocr_pdf_doc(fz_context* ctx, pdf_document* doc, SondOcrPool* ocr_pool,
		TessBaseAPI osd_api, GError** error);

gint sond_ocr_init_tesseract(TessBaseAPI**, TessBaseAPI**, gchar const*, GError**);

SondOcrPool* sond_ocr_pool_new(const gchar *tessdata_path,
		const gchar *language, gint num_threads, fz_context* ctx,
		void (*log_func)(void*, gchar const*, ...), gpointer log_data, GError **error);

void sond_ocr_pool_free(SondOcrPool *pool);

#endif /* SRC_SOND_OCR_H_ */
