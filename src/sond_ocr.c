/*
 sond (sond_ocr.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_ocr.h"

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <gtk/gtk.h>
#include <tesseract/capi.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "sond_log_and_error.h"
#include "sond_pdf_helper.h"

static gint add_ocr_layer_to_page(fz_context *ctx,
		fz_buffer* content, pdf_page *page, pdf_obj* font_ref,
		OcrTransform* ocr_transform, GError** error) {
	pdf_obj* resources = NULL;
	pdf_obj* fonts = NULL;
	pdf_obj* font_name = NULL;
	fz_buffer* buf_new = NULL;
	fz_stream* stream = NULL;
	gint rc = 0;

	fz_try(ctx) {
		// Resources-Dictionary erstellen, falls nicht vorhanden
		resources = pdf_dict_get_inheritable(ctx, page->obj, PDF_NAME(Resources));

		if (!resources) {
			resources = pdf_new_dict(ctx, page->doc, 1);
			pdf_dict_put_drop(ctx, page->obj, PDF_NAME(Resources), resources);
		}
	}
	fz_catch(ctx) //resources muß nicht gedropt werden:
		ERROR_PDF //entweder exception bei _new_ -> nicht erzeugt
					//oder bei _put_drop -> da aber _always drop

	fz_try(ctx) {
		fonts = pdf_dict_get(ctx, resources, PDF_NAME(Font));
		if (!fonts) {
			fonts = pdf_new_dict(ctx, page->doc, 1);
			pdf_dict_put_drop(ctx, resources, PDF_NAME(Font), fonts);
		}
	}
	fz_catch(ctx)
		ERROR_PDF

	fz_try(ctx) {
		font_name = pdf_new_name(ctx, "FSond");
		if (!pdf_dict_get(ctx, fonts, font_name))
			pdf_dict_put(ctx, fonts, font_name, font_ref);
	}
	fz_always(ctx)
		pdf_drop_obj(ctx, font_name);
	fz_catch(ctx)
		ERROR_PDF

	//alten content stream holen und in buffer
	fz_try(ctx) {
		pdf_obj* contents = NULL;

		contents = pdf_dict_get(ctx, page->obj, PDF_NAME(Contents));
		stream = pdf_open_contents_stream(ctx,
				page->doc, contents);
		buf_new = fz_read_all(ctx, stream, 1024);
	}
	fz_always( ctx )
		fz_drop_stream(ctx, stream);
	fz_catch (ctx)
		ERROR_PDF

	// OCR an Content Stream anhängen
	fz_try(ctx)
		fz_append_buffer(ctx, buf_new, content);
	fz_catch(ctx) {
		fz_drop_buffer(ctx, content);

		ERROR_PDF
	}

	//Wir müssen ein neues Contents-Objekt erstellen, weil es sein kann,
	//daß das alte auf ein indirektes Objekt verweist
	//und der sanitize-processor beim letzten Speichern
	//alle indirekten Objekte - sofern identisch - zusammengefaßt hat
	//Dann würde ändern des val alle Seiten betreffen
	rc = pdf_set_content_stream(ctx, page, buf_new, error);
	if (rc)
		ERROR_Z

    return 0;
}

static gint calculate_ocr_transform(fz_context *ctx,
                                               pdf_page *page,
											   float scale,
											   OcrTransform *t,
											   GError **error) {
	fz_rect mediabox = { 0 };
	fz_rect cropbox = { 0 };
    // MediaBox der Seite (tatsächliche Seitengröße in PDF-Punkten)
	fz_try(ctx) {
		mediabox = pdf_dict_get_rect(ctx, page->obj, PDF_NAME(MediaBox));

		// Oder CropBox falls vorhanden, sonst MediaBox
		cropbox = pdf_dict_get_rect(ctx, page->obj, PDF_NAME(CropBox));
		if (fz_is_empty_rect(cropbox))
		    cropbox = mediabox;
	}
	fz_catch(ctx)
		ERROR_PDF

	t->page_width = cropbox.x1 - cropbox.x0;
	t->page_height = cropbox.y1 - cropbox.y0;

    // Rotation holen
    fz_try(ctx) {
    	pdf_obj *rotate_obj = pdf_dict_get_inheritable(ctx, page->obj, PDF_NAME(Rotate));
    	t->rotation = rotate_obj ? pdf_to_int(ctx, rotate_obj) : 0;
    }
	fz_catch(ctx)
    	ERROR_PDF

    t->rotation = t->rotation % 360;
    if (t->rotation < 0) t->rotation += 360;

    t->scale_x = scale;
    t->scale_y = scale;

    return 0;
}

static gint sond_ocr_osd(SondOcrTask* task, GError** error) {
	int orient_deg;
	float orient_conf;

	if (task->durchgang == 0 && task->pool->osd_api) {
		// OSD - Orientierung erkennen (schnell!)
		TessBaseAPISetImage(task->pool->osd_api, task->pixmap->samples, task->pixmap->w,
				task->pixmap->h, task->pixmap->n, task->pixmap->stride);

		gboolean suc = TessBaseAPIDetectOrientationScript(task->pool->osd_api, &orient_deg,
				&orient_conf, NULL, NULL);
		TessBaseAPIClear(task->pool->osd_api);
		if (suc) {
			if (orient_deg && orient_conf > 1.7f) {
				gint rc = 0;

				fz_drop_pixmap(task->pool->ctx, task->pixmap);

				rc = pdf_page_rotate(task->pool->ctx, task->page->obj, 360 - orient_deg, error);
				if (rc)
					ERROR_Z

				if (task->pool->log_func)
					task->pool->log_func(task->pool->log_data,
							"Tesseract OSD: Seite %u um %d° rotiert (Konfidenz %.2f)",
							task->page->super.number, (360 - orient_deg), orient_conf);

				task->pixmap = pdf_render_pixmap(task->pool->ctx, task->page, task->scale, error);
				if (!task->pixmap)
					ERROR_Z
			}
			else if (orient_deg && task->pool->log_func)
				task->pool->log_func(task->pool->log_data,
						"OSD Seite %u: conf < 1.7 - keine Rotation angewendet",
						task->page->super.number);
		}
		else if (task->pool->log_func)
			task->pool->log_func(task->pool->log_data,
					"OSD Seite %u fehlgeschlagen", task->page->super.number);
	}

	return 0;
}

gint sond_ocr_do_tasks(GPtrArray* arr_tasks, GError** error) {
	gint pages_done = 0;
	float scale[] = {4.3, 6.4, 8.6};

	while (pages_done < arr_tasks->len) {
		for (gint i = 0; i < arr_tasks->len; i++) {
			SondOcrTask* task = NULL;
			gint rc = 0;
			gboolean hidden = FALSE;
			gint status = 0;

			task = g_ptr_array_index(arr_tasks, i);

			status = g_atomic_int_get(&task->status);

			if (status == 0) {
				rc = pdf_page_has_hidden_text(task->pool->ctx, task->page, &hidden, error);
				if (rc) {
					if (task->pool->log_data)
						task->pool->log_func(task->pool->log_data,
								"Seite %u konnte nicht auf versteckten Text geprüft werden: %s",
							i, (*error)->message);
					g_clear_error(error);
					pages_done++;

					status = 4;
					continue;
				}

				if (hidden) {
					if (task->pool->log_data)
						task->pool->log_func(task->pool->log_data,
								"Seite %u enthält versteckten Text - OCR übersprungen", i);
					pages_done++;

					status = 4;
					continue;
				}

				task->scale = scale[task->durchgang];
				task->pixmap = pdf_render_pixmap(task->pool->ctx, task->page, task->scale, error);
				if (!task->pixmap) {
					if (task->pool->log_data)
						task->pool->log_func(task->pool->log_data,
								"Seite %u konnte nicht gerendert werden: %s",
								i, (*error)->message);
					g_clear_error(error);
					status = 4;
					continue;
				}

				rc = sond_ocr_osd(task, error);
				if (rc) {
					if (task->pool->log_data)
						task->pool->log_func(task->pool->log_data, "OSD Seite %u gescheitert: %s",
								i, (*error)->message);
					g_clear_error(error);
					status = 4;
					continue;
				}

				rc = calculate_ocr_transform(task->pool->ctx, task->page, task->scale,
						&task->ocr_transform, error);
				if (rc) {
					if (task->pool->log_data)
						task->pool->log_func(task->pool->log_data,
								"Transform-Matrix konnte nicht berechnet werden: %s",
								i, (*error)->message);
					g_clear_error(error);
					status = 4;
					continue;
				}

				g_atomic_int_set(&task->status, 1); //started
				gboolean suc = g_thread_pool_push(task->pool->pool, task, error);
				if (!suc) {
					if (task->pool->log_data)
						task->pool->log_func(task->pool->log_data,
								"Thread konnte nicht gepusht werden: %s",
								i, (*error)->message);
					g_clear_error(error);
					status = 4;
					continue;
				}
			}

			if (status == 1) //läuft gerade
				continue;

			if (status == 2) { //fertig und in Ordnung
				gint rc = 0;

				//content einfügen
				rc = add_ocr_layer_to_page(task->pool->ctx, task->content, task->page,
						task->font_ref, &task->ocr_transform, error);
				if (rc) {
					if (task->pool->log_func)
						task->pool->log_func(task->pool->log_data,
								"Einfügen der OCR-Daten in PDF-Page %u fehlgeschlagen: %s",
								i, (*error)->message);
					g_clear_error(error);
				}

				pages_done++;
				status = 4;
			}

			if (status == 3) {
				pages_done++;
				status = 4;
			}

			if (status == 4) //Abstellgleis
				continue;

		}
	}

	return 0;
}

void sond_ocr_task_free(SondOcrTask* task) {
	if (task->page)
		pdf_drop_page(task->pool->ctx, task->page);
	if (task->content)
		fz_drop_buffer(task->pool->ctx, task->content);

	g_free(task);

	return;
}

SondOcrTask* sond_ocr_task_new(SondOcrPool* pool, pdf_document* doc,
		gint page_num, pdf_obj* font_ref, GError** error) {
	SondOcrTask* task = g_new0(SondOcrTask, 1);

	task->pool = pool;

	fz_try(task->pool->ctx)
		task->page = pdf_load_page(task->pool->ctx, doc, page_num);
	fz_catch(task->pool->ctx) {
		if (task->pool->log_data)
			task->pool->log_func(task->pool->log_data,
					"pdf_page %u konnte nicht geladen werden: %s",
					page_num, fz_caught_message(task->pool->ctx));
		g_free(task);

		return NULL;
	}

	task->font_ref = pdf_keep_obj(task->pool->ctx, font_ref);

	return task;
}

gint sond_ocr_pdf_doc(SondOcrPool* ocr_pool, pdf_document* doc,
		GError** error) {
	gint num_pages = 0;
	pdf_obj* font_ref = NULL;
	gint rc = 0;

	fz_try(ocr_pool->ctx)
		num_pages = pdf_count_pages(ocr_pool->ctx, doc);
	fz_catch(ocr_pool->ctx) {
		fz_context* ctx = ocr_pool->ctx;
		ERROR_PDF
	}

	rc = pdf_get_sond_font(ocr_pool->ctx, doc, &font_ref, error);
	if (rc)
		ERROR_Z

	if (!font_ref) {
		font_ref = pdf_put_sond_font(ocr_pool->ctx, doc, error);
		if (!font_ref)
			ERROR_Z
	}

	GPtrArray* arr_tasks =
			g_ptr_array_new_with_free_func((GDestroyNotify) sond_ocr_task_free);

	for (gint i = 0; i < num_pages; i++) {
		SondOcrTask* task =
				sond_ocr_task_new(ocr_pool, doc, i, font_ref, error);
		if (!task) {
			if (ocr_pool->log_data)
				ocr_pool->log_func(ocr_pool->log_data,
						"Task für Seite %u konnte nicht erzeugt werden: %s",
						i, (*error)->message);
			g_clear_error(error);
			continue;
		}

		g_ptr_array_add(arr_tasks, task);
	}

	pdf_drop_obj(ocr_pool->ctx, font_ref);

	rc = sond_ocr_do_tasks(arr_tasks, error);
	g_ptr_array_unref(arr_tasks);
	if (rc)
		ERROR_Z

	return 0;
}

gint sond_ocr_init_tesseract(TessBaseAPI **handle, TessBaseAPI** osd_api,
		gchar const* datadir, GError** error) {
	gint rc = 0;

	//TessBaseAPI
	if (handle) {
		*handle = TessBaseAPICreate();
		if (!(*handle)) {
			g_set_error(error, SOND_ERROR, 0, "%s\nTessBaseAPICreate fehlgeschlagen", __func__);

			return -1;
		}

		rc = TessBaseAPIInit3(*handle, datadir, "deu");
		if (rc) {
			TessBaseAPIEnd(*handle);
			TessBaseAPIDelete(*handle);
			g_set_error(error, SOND_ERROR, 0, "%s\nTessBaseAPIInit3 fehlgeschlagen", __func__);

			return -1;
		}
		TessBaseAPISetPageSegMode(*handle, PSM_AUTO);
	}

	if (osd_api) {
		*osd_api = TessBaseAPICreate();
		if (!(*osd_api)) {
			TessBaseAPIEnd(*handle);
			TessBaseAPIDelete(*handle);
			g_set_error(error, SOND_ERROR, 0, datadir, __func__);

			return -1;
		}
		rc = TessBaseAPIInit3(*osd_api, datadir, "osd");
		if (rc) {
			TessBaseAPIEnd(*handle);
			TessBaseAPIDelete(*handle);
			TessBaseAPIEnd(*osd_api);
			TessBaseAPIDelete(*osd_api);
			g_set_error(error, SOND_ERROR, 0, "%s\nTessBaseAPIInit3 OSD fehlgeschlagen", __func__);
			return -1;
		}

		TessBaseAPISetPageSegMode(*osd_api, PSM_OSD_ONLY);
	}

    return 0;
}

// Transformation berechnen
static void transform_coordinates(const OcrTransform *t,
                                   int img_x, int img_y,
                                   float *pdf_x, float *pdf_y) {
    // img_x/y sind Koordinaten im GERENDERTEN (bereits rotierten) Bild
    // Müssen zurück ins Original-Seiten-System

    switch (t->rotation) {
        case 0:
            // Keine Rotation beim Rendern
            *pdf_x = img_x / t->scale_x;
            *pdf_y = t->page_height - (img_y / t->scale_y);
            break;

        case 90:
            // Seite wurde 90° CW gerendert → zurück-drehen
            *pdf_x = img_y / t->scale_y;
            *pdf_y = img_x / t->scale_x;
            break;

        case 180:
            // Seite wurde 180° gerendert
            *pdf_x = t->page_width - (img_x / t->scale_x);
            *pdf_y = img_y / t->scale_y;
            break;

        case 270:
            // Seite wurde 270° CW (= 90° CCW) gerendert
            *pdf_x = t->page_width - (img_y / t->scale_y);
            *pdf_y = t->page_height - (img_x / t->scale_x);
            break;
        default:
            *pdf_x = img_x / t->scale_x;
            *pdf_y = t->page_height - (img_y / t->scale_y);
            break;
    }

    return;
}

// Hilfsfunktion: UTF-8 → WinAnsi mit Escaping
static gint append_winansi_text(fz_context *ctx, fz_buffer *buf,
		const char *utf8_text, GError **error) {
	fz_try(ctx) {
		fz_append_byte(ctx, buf, '(');

		for (const unsigned char *p = (unsigned char*)utf8_text; *p; p++) {
			if (*p < 128) {
				// ASCII - escapen wenn nötig
				if (*p == '(' || *p == ')' || *p == '\\')
					fz_append_byte(ctx, buf, '\\');
				fz_append_byte(ctx, buf, *p);
			} else if (*p == 0xC3 && *(p+1)) {
				// UTF-8 deutsche Umlaute (ä, ö, ü, ß, Ä, Ö, Ü)
				p++;
				unsigned char c = *p + 0x40;  // Konvertierung UTF-8 → WinAnsi
			    char tmp[8];
			    sprintf(tmp, "\\%03o", c);
			    fz_append_string(ctx, buf, tmp);
				//fz_append_printf(ctx, buf, "\\%03o", c);  // Oktal-Escape
			} else if (*p == 0xC2 && *(p+1)) {
				// Weitere Latin-1 Zeichen (z.B. ©, ®, °, etc.)
				p++;
			    char tmp[8];
			    sprintf(tmp, "\\%03o", *p);
			    fz_append_string(ctx, buf, tmp);
				//fz_append_printf(ctx, buf, "\\%03o", *p);
			} else {
				gboolean multi = FALSE;
				// Nicht darstellbar → Leerzeichen
				fz_append_byte(ctx, buf, ' ');
				// Überspringe Rest des Multi-Byte-Zeichens
				while (*p && (*p & 0xC0) == 0x80) {
					p++;
					multi = TRUE;
				}
				if (multi)
					p--;
			}
		}

		fz_append_byte(ctx, buf, ')');
	}
	fz_catch(ctx)
		ERROR_PDF

    return 0;
}

static void append_text_matrix(fz_context *ctx, fz_buffer *buf,
            const OcrTransform *t,
			float scale_word_x,
            float pdf_x, float pdf_y) {
	switch (t->rotation) {
		case 0:
		// Normal horizontal
		fz_append_printf(ctx, buf, "%.4f 0 0 1 %.2f %.2f Tm\n",
				scale_word_x, pdf_x, pdf_y);
		break;

		case 90:
		// 90° gegen Uhrzeigersinn (PDF-Rotation)
		fz_append_printf(ctx, buf, "0 %.4f -1 0 %.2f %.2f Tm\n",
				scale_word_x, pdf_x, pdf_y);
		break;

		case 180:
		// 180° gedreht
		fz_append_printf(ctx, buf, "%.4f 0 0 -1 %.2f %.2f Tm\n",
				-scale_word_x, pdf_x, pdf_y);
		break;

		case 270:
		// 270° gegen Uhrzeigersinn
		fz_append_printf(ctx, buf, "0 %.4f 1 0 %.2f %.2f Tm\n",
				-scale_word_x, pdf_x, pdf_y);
		break;

		default:
		fz_append_printf(ctx, buf, "%.4f 0 0 1 %.2f %.2f Tm\n",
				scale_word_x, pdf_x, pdf_y);
		break;
	}

	return;
}

static gboolean is_garbage_word(const char *word) {
    if (!word || !*word) return TRUE;

    gboolean has_valid_char = FALSE;

    for (const unsigned char *p = (unsigned char*)word; *p; p++) {
        // Gültiges Zeichen: Buchstabe, Ziffer oder typische Interpunktion
        if (g_ascii_isalnum(*p) ||          // a-z, A-Z, 0-9
            *p == '-' || *p == '.' ||       // Bindestrich, Punkt
            *p == ',' || *p == ';' ||       // Komma, Semikolon
            *p == ':' || *p == '!' ||       // Doppelpunkt, Ausrufezeichen
            *p == '?' || *p == '\'' ||      // Fragezeichen, Apostroph
            *p == '"' || *p == '(' ||       // Anführungszeichen, Klammer
            *p == ')' || *p == '/' ||       // Klammer, Slash
            (*p >= 0x80 && *p <= 0xFF)) {   // Umlaute/Unicode (UTF-8 Start)
            has_valid_char = TRUE;
            break;  // Mindestens ein gültiges Zeichen gefunden
        }
        // Leerzeichen ignorieren, aber zählen nicht als gültig
    }

    return !has_valid_char;  // Müll wenn kein gültiges Zeichen gefunden
}

// Verbesserte Content-Stream-Funktion mit Transformation
static fz_buffer* tesseract_to_content_stream(fz_context *ctx,
                                        TessResultIterator *iter,
                                        const OcrTransform *transform,
										GError **error) {
    static fz_font *helvetica = NULL;
    fz_buffer *content = NULL;

    TessPageIteratorLevel level = RIL_WORD;

    if (!helvetica)
        helvetica = fz_new_base14_font(ctx, "Helvetica");

    fz_try(ctx)
        content = fz_new_buffer(ctx, 4096);
    fz_catch(ctx)
		ERROR_PDF_VAL(NULL);

    fz_try(ctx)
        fz_append_string(ctx, content, "\nq\nBT\n3 Tr\n");
    fz_catch(ctx) {
    	fz_drop_buffer(ctx, content);

    	ERROR_PDF_VAL(NULL);
    }

	float last_font_size = -1;

	do {
		gint rc = 0;

		char *word = TessResultIteratorGetUTF8Text(iter, level);
		if (!word || !*word) {
			if (word) TessDeleteText(word);
			continue;
		}

		if (is_garbage_word(word)) {
			TessDeleteText(word);
			continue;
		}

		// Bounding Box in Bild-Koordinaten
		int x1, y1, x2, y2;
		if (!TessPageIteratorBoundingBox((TessPageIterator*)iter, level, &x1, &y1, &x2, &y2)) {
			TessDeleteText(word);
			continue;
		}

		gint base_x1, base_y1, base_x2, base_y2;
		if (!TessPageIteratorBaseline((TessPageIterator*)iter, RIL_WORD,
				&base_x1, &base_y1, &base_x2, &base_y2)) {
			TessDeleteText(word);
			continue;
		}

		//Wortbreite
		float word_width_pdf = (x2 - x1) / transform->scale_x;

		// Schriftgröße aus Höhe (vor Transformation!)
		float word_height = (base_y2 - y1) / transform->scale_y;
		float font_size = word_height * 1.2; // * 0.85f;
		if (font_size < 1.0f) font_size = 1.0f;

		if (fabsf(font_size - last_font_size) > 0.5f) {
			fz_try(ctx)
				fz_append_printf(ctx, content, "/FSond %.2f Tf\n", font_size);
			fz_catch(ctx) {
				fz_drop_buffer(ctx, content);
				TessDeleteText(word);

				ERROR_PDF_VAL(NULL);
			}

			last_font_size = font_size;
		}

		// 3. **Tatsächliche Textbreite in Helvetica messen**
		float width = 0;
		for (const char *p = word; *p; p++) {
			int gid = fz_encode_character(ctx, helvetica, (unsigned char)*p);
			width += fz_advance_glyph(ctx, helvetica, gid, 0);
		}

		float actual_width = width * font_size;

		// 4. **Horizontale Skalierung berechnen**
		float scale_word_x = (actual_width > 0.01f) ? (word_width_pdf / actual_width) : 1.0f;

		// Koordinaten transformieren (Bild → PDF mit Rotation)
		float pdf_x, pdf_y;
		transform_coordinates(transform, x1, base_y2, &pdf_x, &pdf_y);

		// Text-Matrix setzen
		append_text_matrix(ctx, content, transform, scale_word_x, pdf_x, pdf_y);

		// Text ausgeben
		rc = append_winansi_text(ctx, content, word, error);
		TessDeleteText(word);
		if (rc)
			ERROR_Z_VAL(NULL);

		fz_try(ctx)
            fz_append_printf(ctx, content, " Tj\n");
		fz_catch(ctx) {
			fz_drop_buffer(ctx, content);

            ERROR_PDF_VAL(NULL);
		}
	} while (TessResultIteratorNext(iter, level));

	fz_try(ctx)
        fz_append_string(ctx, content, "ET\nQ\n");
	fz_catch(ctx) {
		fz_drop_buffer(ctx, content);

		ERROR_PDF_VAL(NULL);
	}

    return content;
}

// Private Struktur für Thread-lokale Tesseract-Instanz
typedef struct {
    TessBaseAPI *api;
    ETEXT_DESC* monitor;
} TesseractThreadData;

static gboolean ocr_cancel(void *cancel_this, int words) {
	volatile gboolean *cancelFlag = (volatile gboolean*) cancel_this;
	return *cancelFlag;
}

static gint ocr_pixmap(SondOcrTask* task, SondOcrPool* pool,
		TesseractThreadData* thread_data, GError** error) {
	gint rc = 0;

	//jetzt richtige OCR
	TessBaseAPISetImage(thread_data->api, task->pixmap->samples, task->pixmap->w, task->pixmap->h,
			task->pixmap->n, task->pixmap->stride);
	TessBaseAPISetSourceResolution(thread_data->api, (gint) (task->scale * 72.0));

	rc = TessBaseAPIRecognize(thread_data->api, thread_data->monitor);
	if (rc) { //muß sorum abgefragt werden, weil Abbruch auch rc = 1 macht
		g_set_error(error, SOND_ERROR, 0, "Recognize fehlgeschlagen");

		return -1;
	}

	float conf = TessBaseAPIMeanTextConf(thread_data->api);
	if (conf > 80 || task->durchgang == 2)
		return 0;

	//wenn nicht:
	task->durchgang++;
	if (pool->log_func)
		pool->log_func(pool->log_data,
			"OCR-Konfidenz %d%% für Seite %u zu niedrig, nächster Durchgang %u",
			conf, task->page->super.number, task->durchgang);

	return 1;
}

static TesseractThreadData* get_or_create_thread_data(GPrivate *thread_data_key,
                                                       const gchar *tessdata_path,
                                                       const gchar *language,
													   volatile gboolean* cancel_all,
                                                       GError **error) {
    TesseractThreadData *data = g_private_get(thread_data_key);

    if (data == NULL) {
    	gint rc = 0;

        data = g_new0(TesseractThreadData, 1);

        rc = sond_ocr_init_tesseract(&data->api, NULL,
        		tessdata_path ? tessdata_path : "/usr/share/tesseract-ocr/5/tessdata/",
        				error);
        if (rc) {
			g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
					   "Tesseract Initialisierung fehlgeschlagen für Thread");
			g_free(data);
			return NULL;
		}

		data->monitor = TessMonitorCreate();
		TessMonitorSetCancelFunc(data->monitor, (TessCancelFunc) ocr_cancel);
		TessMonitorSetCancelThis(data->monitor, (gpointer) cancel_all);

        g_private_set(thread_data_key, data);
    }

    return data;
}

static void ocr_worker(gpointer task_data, gpointer user_data) {
    GError *error = NULL;
    gint rc = 0;

    SondOcrTask *task = (SondOcrTask *)task_data;
    SondOcrPool *pool = (SondOcrPool *)user_data;

    //job++
    g_atomic_int_inc(&pool->num_active_jobs);

    // Hole oder erstelle Thread-lokale Tesseract-Instanz
    TesseractThreadData *thread_data = g_private_get(&pool->thread_data_key);

    if (thread_data == NULL) {
        thread_data = get_or_create_thread_data(&pool->thread_data_key,
        		pool->tessdata_path, pool->language, &pool->cancel_all, &error);
        if (!thread_data) {
        	if (pool->log_func)
        		pool->log_func(pool->log_data, "Thread-Data konnte nicht geladen werden: %s",
        				error->message);
        	g_error_free(error);
        	g_atomic_int_set(&task->status, 3);

        	goto dec_active_jobs;
        }
    }

    rc = ocr_pixmap(task, pool, thread_data, &error);
    if (rc == -1) {
		if (pool->log_func)
			pool->log_func(pool->log_data, "Recog failed: %s", error->message);
		g_error_free(error);
    	g_atomic_int_set(&task->status, 3);
    	goto dec_active_jobs;
    }
    else if (rc == 1) {
    	g_atomic_int_set(&task->status, 0);
    	goto dec_active_jobs;
    }

	TessResultIterator* iter = TessBaseAPIGetIterator(thread_data->api);
	if (!iter) {
		if (pool->log_func)
			pool->log_func(pool->log_data, "Couldn't get ResultIter");
		g_atomic_int_set(&task->status, 3);
		goto dec_active_jobs;
	}

	// Content Stream mit korrekten Koordinaten erstellen
	task->content = tesseract_to_content_stream(task->pool->ctx, iter,
			&task->ocr_transform, &error);
	if (!task->content) {
		if (pool->log_func)
			pool->log_func(pool->log_data,
					"Umwandlung Results in content stream fehlgeschlagen: %s",
					error->message);
		g_error_free(error);
		g_atomic_int_set(&task->status, 3);
		goto dec_active_jobs;
	}

	g_atomic_int_set(&task->status, 2);

dec_active_jobs:
    g_atomic_int_dec_and_test(&pool->num_active_jobs);

    return;
}

// Initialisiert Tesseract für den aktuellen Thread
// Cleanup-Funktion für Thread-Daten
static void cleanup_thread_data(TesseractThreadData *data) {
    if (data) {
        if (data->api) {
            TessBaseAPIEnd(data->api);
            TessBaseAPIDelete(data->api);
        }

        TessMonitorDelete(data->monitor);

        g_free(data);
    }
}

static gint sond_ocr_init_osd_api(TessBaseAPI** osd_api,
		gchar const* datadir, GError** error) {
	gint rc = 0;

	*osd_api = TessBaseAPICreate();
	if (!(*osd_api)) {
		g_set_error(error, SOND_ERROR, 0, "Create Tesseract OSD API failed");

		return -1;
	}

	rc = TessBaseAPIInit3(*osd_api, datadir, "osd");
	if (rc) {
		TessBaseAPIEnd(*osd_api);
		TessBaseAPIDelete(*osd_api);
		g_set_error(error, SOND_ERROR, 0, "TessBaseAPIInit3 OSD fehlgeschlagen");
		return -1;
	}

	TessBaseAPISetPageSegMode(*osd_api, PSM_OSD_ONLY);

    return 0;
}

// Worker-Funktion die im Thread-Pool ausgeführt wird
SondOcrPool* sond_ocr_pool_new(const gchar *tessdata_path,
                                   const gchar *language,
                                   gint num_threads,
								   fz_context* ctx,
								   void (*log_func)(void*, gchar const*, ...),
								   gpointer log_data,
                                   GError **error) {
	gint rc = 0;

    g_return_val_if_fail(tessdata_path != NULL, NULL);
    g_return_val_if_fail(language != NULL, NULL);
    g_return_val_if_fail(num_threads > 0, NULL);

    SondOcrPool *pool = g_new0(SondOcrPool, 1);

    // Erstelle Thread-Pool
    // Die Threads werden beim ersten Task automatisch initialisiert
    pool->pool = g_thread_pool_new(ocr_worker,
                                   pool,
                                   num_threads,
                                   TRUE,
                                   error);
    if (!pool->pool) {
    	g_free(pool);
    	return NULL;
    }

    rc = sond_ocr_init_osd_api(&pool->osd_api,
    		tessdata_path ? tessdata_path : "/usr/share/tesseract-ocr/5/tessdata/",
    				error);
    if (rc) {
    	g_thread_pool_free(pool->pool, TRUE, TRUE);
    	g_free(pool);
    	return NULL;
    }

    g_mutex_init(&pool->mutex);
    pool->tessdata_path = g_strdup(tessdata_path);
    pool->language = g_strdup(language);
    pool->ctx = ctx;
    pool->log_func = log_func;
    pool->log_data = log_data;


    // Initialisiere GPrivate mit automatischer Cleanup-Funktion
    // Die Cleanup-Funktion wird beim Thread-Ende automatisch aufgerufen
    pool->thread_data_key = (GPrivate)G_PRIVATE_INIT((GDestroyNotify)cleanup_thread_data);

    return pool;
}

void sond_ocr_pool_free(SondOcrPool *pool) {
    if (pool == NULL) return;

    // Warte auf alle ausstehenden Tasks und beende Thread-Pool
    // Die thread-lokalen Daten werden automatisch durch die
    // DestroyNotify-Funktion aufgeräumt wenn die Threads beendet werden
    g_thread_pool_free(pool->pool, FALSE, TRUE);

    TessBaseAPIEnd(pool->osd_api);
    TessBaseAPIDelete(pool->osd_api);

    // Cleanup Pool-Struktur
    g_free(pool->tessdata_path);
    g_free(pool->language);
    g_mutex_clear(&pool->mutex);
    g_free(pool);
}

