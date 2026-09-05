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

/*
 * SondOcrMode - Umgang mit bereits vorhandenem "verstecktem" Text (Tr 3,
 * z.B. von einer früheren OCR oder einem PDF mit Textebene) beim OCRen
 * einer PDF-Seite.
 *
 * Wird pro Aufruf (nicht im SondOcrPool, der nur die Thread-Infrastruktur
 * hält) übergeben, damit sowohl zond als auch sond_server denselben Pool
 * für unterschiedliche Läufe mit unterschiedlichem Modus verwenden können.
 */
typedef enum {
	SOND_OCR_MODE_NONE  = 0, /* kein OCR                                    */
	SOND_OCR_MODE_CHECK = 1, /* vorhandenen versteckten Text prüfen, Seite  *
	                          * ggf. überspringen (bisheriger Standard)     */
	SOND_OCR_MODE_FORCE = 2  /* vorhandenen versteckten Text löschen und    *
	                          * Seite in jedem Fall neu OCRen               */
} SondOcrMode;

// Thread-Pool Struktur
typedef struct _SondOcrPool {
    GThreadPool *pool;
    GMutex mutex;
    gint* cancel_all;
    gint* global_progress;
    GPrivate thread_data_key;
    gchar *tessdata_path;
    gchar *language;
    TessBaseAPI* osd_api;
} SondOcrPool;

// Transformations-Kontext für OCR-Koordinaten
typedef struct {
    fz_matrix ctm_inv;  // Inverse der Render-CTM: Screen -> PDF
    int pixmap_x;       // Pixmap-Ursprung im Screen-System (pixmap->x)
    int pixmap_y;       // Pixmap-Ursprung im Screen-System (pixmap->y)
    float scale_x;
    float scale_y;
    int rotation;  // 0, 90, 180, 270
    float page_width;
    float page_height;
} OcrTransform;

// Struktur für OCR-Aufgaben
typedef struct {
	gint status; //0: muß noch laufen; 1: läuft; 2: fertig; 3: error; 4: Abstellgleis
	fz_context* ctx;
	void(*log_func)(void*, gchar const*, ...);
	gpointer log_func_data;
	pdf_page* page;
	pdf_obj* font_ref;
	fz_pixmap* pixmap;
	float scale;
	gint durchgang;
	float confidence; //von Tesseract erreichte Konfidenz (TessBaseAPIMeanTextConf) des letzten Durchgangs
	OcrTransform ocr_transform;
	GString* content;
	gboolean content_changed; /* TRUE nur, wenn add_ocr_layer_to_page() den
	 * Content-Stream der Seite tatsächlich verändert hat. Bleibt FALSE, wenn
	 * die Seite wegen bereits vorhandenem verstecktem Text übersprungen
	 * wurde (SOND_OCR_MODE_CHECK) oder das Einfügen fehlschlug - Aufrufer
	 * (seiten.c) benutzt das, um für unveränderte Seiten keinen
	 * Journal-Eintrag anzulegen und die Datei nicht fälschlich als
	 * geändert zu markieren. */
	fz_buffer* buf_content_new; /* NUR gesetzt, wenn content_changed==TRUE:
	 * der von add_ocr_layer_to_page() bereits fertig zusammengesetzte neue
	 * Content-Stream (OCR-Block + alter Stream) - derselbe, der auch in
	 * die Seite geschrieben wurde. Aufrufer (seiten.c) übernimmt ihn direkt
	 * für den Journal-Eintrag (entry.ocr.buf_new), statt ihn nach dem
	 * Schreiben fehleranfällig aus der Seite neu einzulesen. Ownership geht
	 * beim Auslesen auf den Aufrufer über (task->buf_content_new danach auf
	 * NULL setzen); wird er nicht abgeholt, droppt sond_ocr_task_free()
	 * ihn. */
} SondOcrTask;

gint sond_ocr_do_tasks(GPtrArray* arr_tasks, SondOcrPool* pool,
		SondOcrMode mode, GError** error);

void sond_ocr_task_free(SondOcrTask* task);

SondOcrTask* sond_ocr_task_new(fz_context* ctx, pdf_document* doc,
		gint page_num, pdf_obj* font_ref,
		void (*log_func)(void*, gchar const*, ...), gpointer log_func_data,
		GError** error);

/* seite_von/seite_bis (0-basiert, inklusive): nur dieser Seitenbereich wird
 * OCRt, -1/-1 = ganzes Dokument.
 * out_changed (kann NULL sein): wird auf TRUE gesetzt, wenn mindestens eine
 * Seite tatsächlich verändert wurde (neue OCR-Textebene eingefügt oder
 * versteckter Text entfernt) - FALSE, wenn z.B. alle Seiten schon Text
 * hatten und deshalb übersprungen wurden, oder OCR für jede Seite
 * fehlgeschlagen ist. Der Aufrufer kann damit einen unnötigen (und bei
 * strukturell fragilen PDFs riskanten) Rewrite des Dokuments vermeiden,
 * wenn ohnehin nichts zu schreiben ist. */
gint sond_ocr_pdf_doc(fz_context* ctx, SondOcrPool* ocr_pool, pdf_document* doc,
		SondOcrMode mode, gint seite_von, gint seite_bis,
		void (*log_func)(void*, gchar const*, ...), gpointer log_func_data,
		gboolean* out_changed, GError** error);

SondOcrPool* sond_ocr_pool_new(const gchar *tessdata_path,
		const gchar *language, gint num_threads,
		gint* cancel_all, gint* global_progress, GError **error);

void sond_ocr_pool_free(SondOcrPool *pool);

#endif /* SRC_SOND_OCR_H_ */
