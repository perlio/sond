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
#include <glib.h>

typedef struct TessBaseAPI TessBaseAPI;
/*
typedef struct _GError GError;
typedef struct _GThreadPool GThreadPool;

typedef void* gpointer;
typedef int gint;
typedef char gchar;
typedef int gboolean;
*/

// Thread-Pool Struktur
typedef struct {
    GThreadPool *pool;
    GMutex mutex;
    gint* cancel_all;
    gint num_tasks;
    gint* global_progress;
    GPrivate thread_data_key;
    gchar *tessdata_path;
    gchar *language;
    TessBaseAPI* osd_api;
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
	gint status; //0: muß noch laufen; 1: läuft; 2: fertig; 3: error; 4: Abstellgleis
	SondOcrPool* pool;
	pdf_page* page;
	pdf_obj* font_ref;
	fz_pixmap* pixmap;
	float scale;
	gint durchgang;
	OcrTransform ocr_transform;
	GString* content;
} SondOcrTask;

gint sond_ocr_do_tasks(GPtrArray* arr_tasks, GError** error);

void sond_ocr_task_free(SondOcrTask* task);

SondOcrTask* sond_ocr_task_new(SondOcrPool* pool, pdf_document* doc,
		gint page_num, pdf_obj* font_ref, GError** error);

gint sond_ocr_pdf_doc(SondOcrPool* ocr_pool, pdf_document* doc,
		GError** error);

SondOcrPool* sond_ocr_pool_new(const gchar *tessdata_path,
		const gchar *language, gint num_threads, fz_context* ctx,
		void (*log_func)(void*, gchar const*, ...), gpointer log_data,
		gint* cancel_all, gint* global_progress, GError **error);

void sond_ocr_pool_free(SondOcrPool *pool);

#endif /* SRC_SOND_OCR_H_ */
