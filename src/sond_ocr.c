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

typedef struct {
	pdf_processor super;
	gint flags;
	GArray* arr_Tr;
	gboolean has_visible_text;
	gboolean has_hidden_text;
} pdf_text_analyzer_processor;

static void text_analyzer_op_q(fz_context *ctx, pdf_processor *proc) {
	gint Tr = 0;

	pdf_text_analyzer_processor *p = (pdf_text_analyzer_processor*) proc;

	Tr = g_array_index(p->arr_Tr, gint, p->arr_Tr->len - 1);

	g_array_append_val(p->arr_Tr, Tr);

	//chain-up
	if (proc && proc->chain && proc->chain->op_q)
		proc->chain->op_q(ctx, proc->chain);

	return;
}

static void text_analyzer_op_Q(fz_context *ctx, pdf_processor *proc) {
	pdf_text_analyzer_processor *p = (pdf_text_analyzer_processor*) proc;

	if (p->arr_Tr->len) //wenn mehr Q als q, dann braucht man auch nicht weiterleiten...
		g_array_remove_index(p->arr_Tr, p->arr_Tr->len - 1);

	if (proc && proc->chain && proc->chain->op_Q)
		proc->chain->op_Q(ctx, proc->chain);

	return;
}

static void text_analyzer_op_Tr(fz_context *ctx, pdf_processor *proc,
		gint render) {
	pdf_text_analyzer_processor *p = (pdf_text_analyzer_processor*) proc;

	(((gint*) (void*) (p->arr_Tr)->data)[(p->arr_Tr->len - 1)]) = render;

	if (proc && proc->chain && proc->chain->op_Tr)
		proc->chain->op_Tr(ctx, proc->chain, render);

	return;
}

static void text_analyzer_op_TJ(fz_context *ctx, pdf_processor *proc,
		pdf_obj *array) {
	gint Tr = 0;

	pdf_text_analyzer_processor *p = (pdf_text_analyzer_processor*) proc;

	Tr = g_array_index(p->arr_Tr, gint, p->arr_Tr->len - 1);

	if (Tr == 3)
		p->has_hidden_text = TRUE;
	else
		p->has_visible_text = TRUE;

	if ((p->flags & 1) && Tr != 3)
		return;
	else if ((p->flags & 2) && Tr == 3)
		return;

	if (proc && proc->chain && proc->chain->op_TJ)
		proc->chain->op_TJ(ctx, proc->chain, array);

	return;
}

static void text_analyzer_op_Tj(fz_context *ctx, pdf_processor *proc,
		gchar *str, size_t len) {
	gint Tr = 0;

	pdf_text_analyzer_processor *p = (pdf_text_analyzer_processor*) proc;

	Tr = g_array_index(p->arr_Tr, gint, p->arr_Tr->len - 1);

	if (Tr == 3)
		p->has_hidden_text = TRUE;
	else
		p->has_visible_text = TRUE;

	if ((p->flags & 1) && Tr != 3)
		return;
	else if ((p->flags & 2) && Tr == 3)
		return;

	if (proc && proc->chain && proc->chain->op_Tj)
		proc->chain->op_Tj(ctx, proc->chain, str, len);

	return;
}

static void text_analyzer_op_squote(fz_context *ctx, pdf_processor *proc,
		gchar *str, size_t len) {
	gint Tr = 0;

	pdf_text_analyzer_processor *p = (pdf_text_analyzer_processor*) proc;

	Tr = g_array_index(p->arr_Tr, gint, p->arr_Tr->len - 1);

	if (Tr == 3)
		p->has_hidden_text = TRUE;
	else
		p->has_visible_text = TRUE;

	if ((p->flags & 1) && Tr != 3)
		return;
	else if ((p->flags & 2) && Tr == 3)
		return;

	if (proc && proc->chain && proc->chain->op_squote)
		proc->chain->op_squote(ctx, proc->chain, str, len);

	return;
}

static void text_analyzer_op_dquote(fz_context *ctx, pdf_processor *proc,
		float aw, float ac, gchar *str, size_t len) {
	gint Tr = 0;

	pdf_text_analyzer_processor *p = (pdf_text_analyzer_processor*) proc;

	Tr = g_array_index(p->arr_Tr, gint, p->arr_Tr->len - 1);

	if (Tr == 3)
		p->has_hidden_text = TRUE;
	else
		p->has_visible_text = TRUE;

	if ((p->flags & 1) && Tr != 3)
		return;
	else if ((p->flags & 2) && Tr == 3)
		return;

	if (proc && proc->chain && proc->chain->op_dquote)
		proc->chain->op_dquote(ctx, proc->chain, aw, ac, str, len);

	return;
}

static void drop_text_analyzer_processor(fz_context* ctx, pdf_processor* proc) {
	g_array_unref(((pdf_text_analyzer_processor*) proc)->arr_Tr);

//	if (proc && proc->chain && proc->chain->drop_processor)
//		proc->chain->drop_processor(ctx, proc->chain);

	return;
}

static void reset_text_analyzer_processor(fz_context* ctx, pdf_processor* proc) {
	gint zero = 0;

	((pdf_text_analyzer_processor*) proc)->has_hidden_text = FALSE;
	((pdf_text_analyzer_processor*) proc)->has_visible_text = FALSE;
	g_array_remove_range(((pdf_text_analyzer_processor*) proc)->arr_Tr, 0,
			((pdf_text_analyzer_processor*) proc)->arr_Tr->len);
	g_array_append_val(((pdf_text_analyzer_processor*) proc)->arr_Tr, zero);

	if (proc && proc->chain)
		pdf_reset_processor(ctx, proc->chain);

	return;
}

pdf_processor*
new_text_analyzer_processor(fz_context *ctx, pdf_processor* chain, gint flags, GError** error) {
	gint zero = 0;
	pdf_text_analyzer_processor *proc = NULL;

	proc = pdf_new_processor(ctx, sizeof(pdf_text_analyzer_processor));

	//Funktionen "umleiten"
	proc->super.drop_processor = drop_text_analyzer_processor;
	proc->super.reset_processor = reset_text_analyzer_processor;

	proc->arr_Tr = g_array_new( FALSE, FALSE, sizeof(gint));
	g_array_append_val(proc->arr_Tr, zero);

	proc->flags = flags;

	proc->super.op_q = text_analyzer_op_q;
	proc->super.op_Q = text_analyzer_op_Q;
	proc->super.op_Tr = text_analyzer_op_Tr;
	proc->super.op_TJ = text_analyzer_op_TJ;
	proc->super.op_Tj = text_analyzer_op_Tj;
	proc->super.op_squote = text_analyzer_op_squote;
	proc->super.op_dquote = text_analyzer_op_dquote;

	proc->super.chain = chain;

	if (chain) {
		proc->super.requirements = proc->super.chain->requirements;
		proc->super.chain->rstack = proc->super.rstack;
	}

	return (pdf_processor*) proc;
}

static gint add_ocr_layer_to_page(fz_context *ctx,
		fz_buffer* content, pdf_page *page, pdf_obj* font_ref,
		OcrTransform* ocr_transform, GError** error) {
	pdf_obj* resources = NULL;
	pdf_obj* fonts = NULL;
	pdf_obj* font_name = NULL;
	fz_buffer* buf_new = NULL;
	gint rc = 0;
	fz_stream* stream = NULL;

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
		if (!pdf_dict_get(ctx, fonts, font_name))
			font_name = pdf_new_name(ctx, "FSond");
	}
	fz_catch(ctx)
		ERROR_PDF

	fz_try(ctx)
		pdf_dict_put(ctx, fonts, font_name, font_ref);
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
	fz_try(ctx) {
		pdf_obj* contents_new = pdf_add_object_drop(ctx, page->doc,
				pdf_new_dict(ctx, page->doc, 1));
		pdf_dict_put_drop(ctx, page->obj, PDF_NAME(Contents),
				contents_new);
	}
	fz_catch(ctx) {
		fz_drop_buffer(ctx, buf_new);

		ERROR_PDF
	}

	fz_try( ctx ) {
		pdf_obj* contents = pdf_dict_get(ctx, page->obj, PDF_NAME(Contents));
		pdf_update_stream(ctx, page->doc, contents, buf_new, 0);
	}
	fz_always(ctx)
		fz_drop_buffer(ctx, buf_new);
	fz_catch(ctx)
		ERROR_PDF

    return 0;
}

static gint sond_ocr_run_page(fz_context* ctx, pdf_page* page,
		fz_device* dev, GError** error) {
	pdf_processor* proc_run = NULL;
	pdf_processor* proc_text = NULL;

	fz_try(ctx)
		proc_run = pdf_new_run_processor(ctx, page->doc, dev, fz_identity, -1,
			"View", NULL, NULL, NULL, NULL, NULL);
	fz_catch(ctx)
		ERROR_PDF

		// text-analyzer-Processor erstellen (dieser filtert den Text)
	fz_try(ctx) //flag == 3: aller Text weg, nur Bilder ocr-en
		proc_text = new_text_analyzer_processor(ctx, proc_run, 3, error);
	fz_catch(ctx) {
		pdf_close_processor(ctx, proc_run);
		pdf_drop_processor(ctx, proc_run);

		ERROR_PDF
	}

    // Content durch Filter-Kette schicken
	fz_try(ctx)
		pdf_process_contents(ctx, proc_text, page->doc,
				pdf_page_resources(ctx, page), pdf_page_contents(ctx, page),
				NULL, NULL);
	fz_always(ctx) {
		pdf_close_processor(ctx, proc_text);
		pdf_drop_processor(ctx, proc_text);
		pdf_drop_processor(ctx, proc_run);
	}
	fz_catch(ctx)
		ERROR_PDF

	return 0;
}

static fz_pixmap*
sond_ocr_render_pixmap(fz_context *ctx, pdf_page* page,
		float scale, GError** error) {
	gint rc = 0;
	fz_device *draw_device = NULL;
	fz_pixmap *pixmap = NULL;
	fz_rect rect = { 0 };
	fz_matrix ctm = { 0 };

	pdf_page_transform(ctx, page, &rect, &ctm);
	ctm = fz_pre_scale(ctm, scale, scale);
	rect = fz_transform_rect(rect, ctm);

	//per draw-device to pixmap
	fz_try( ctx )
		pixmap = fz_new_pixmap_with_bbox(ctx, fz_device_rgb(ctx),
				fz_irect_from_rect(rect), NULL, 0);
	fz_catch(ctx)
		ERROR_PDF_VAL(NULL)

	fz_try( ctx)
		fz_clear_pixmap_with_value(ctx, pixmap, 255);
	fz_catch(ctx) {
		fz_drop_pixmap(ctx, pixmap);

		ERROR_PDF_VAL(NULL)
	}

	fz_try(ctx)
		draw_device = fz_new_draw_device(ctx, ctm, pixmap);
	fz_catch(ctx) {
		fz_drop_pixmap(ctx, pixmap);

		ERROR_PDF_VAL(NULL)
	}

	rc = sond_ocr_run_page(ctx, page, draw_device, error);
	fz_close_device(ctx, draw_device);
	fz_drop_device(ctx, draw_device);
	if (rc) {
		fz_drop_pixmap(ctx, pixmap);

		ERROR_PDF_VAL(NULL)
	}

	return pixmap;
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

gint sond_ocr_page(SondOcrPool* pool, pdf_page* page, pdf_obj* font_ref,
		SondOcrTask* task, TessBaseAPI* osd_api, GError** error) {
	float scale[] = {4.3, 6.4, 8.6};
	gint max_durchgang = 3;
	gint rc = 0;
	gint conf = 0;
	int orient_deg;
	float orient_conf;

	task->pixmap = sond_ocr_render_pixmap(ctx, page, scale[task->durchgang], error);
	if (!task->pixmap)
		ERROR_Z

	task->ctx = ctx;
	task->scale = scale[task->durchgang];
	if (task->durchgang == 0 && osd_api) {
		// OSD - Orientierung erkennen (schnell!)
		TessBaseAPISetImage(osd_api, task->pixmap->samples, task->pixmap->w,
				task->pixmap->h, task->pixmap->n, task->pixmap->stride);

		if (TessBaseAPIDetectOrientationScript(osd_api, &orient_deg,
				&orient_conf, NULL, NULL)) {
			if (orient_deg && orient_conf > 1.7f) {
				gint rc = 0;

				rc = pdf_page_rotate(ctx, page->obj, 360 - orient_deg, error);
				fz_drop_pixmap(ctx, task->pixmap);
				if (rc)
					ERROR_Z

				if (pool->log_func)
					pool->log_func(pool->log_data,
							"Tesseract OSD: Seite %u um %d° rotiert (Konfidenz %.2f)",
							page->super.number, (360 - orient_deg), orient_conf);

				task->pixmap = sond_ocr_render_pixmap(ctx, page, scale[task->durchgang], error);
				if (!task->pixmap)
					ERROR_Z
			}
			else if (orient_deg && pool->log_func)
				pool->log_func(pool->log_data,
						"Tesseract OSD: conf < 1.7 - keine Rotation angewendet");
		}
	}

	rc = calculate_ocr_transform(ctx, page, task->scale,
			&task->ocr_transform, error);
	if (rc)
		ERROR_Z

	g_atomic_int_set(task->status, 1); //started
	gboolean suc = g_thread_pool_push(pool->pool, task, error);
	if (!suc)
		ERROR_Z

	return 0;
}

gint sond_ocr_get_num_sond_font(fz_context* ctx, pdf_document* doc, GError** error) {
	gint num_pages = 0;
	gint num = 0;

	fz_try(ctx)
		num_pages = pdf_count_pages(ctx, doc);
	fz_catch(ctx)
		ERROR_PDF

	for (gint u = 0; u < num_pages; u++) {
		pdf_obj* page_ref = NULL;
		pdf_obj* resources = NULL;
		pdf_obj* font_dict = NULL;

		fz_try(ctx) {
			pdf_obj* fsond = NULL;

			page_ref = pdf_lookup_page_obj(ctx, doc, u);
			resources = pdf_dict_get_inheritable(ctx, page_ref,
					PDF_NAME(Resources));
			font_dict = pdf_dict_get(ctx, resources, PDF_NAME(Font));
			fsond = pdf_dict_gets(ctx, font_dict, "FSond");
			if (fsond)
				num = pdf_to_num(ctx, fsond);
		}
		fz_catch(ctx)
			ERROR_PDF
	}

	return num;
}

pdf_obj* sond_ocr_put_sond_font(fz_context* ctx, pdf_document* doc, GError** error) {
	pdf_obj* font = NULL;
	pdf_obj* font_ref = NULL;

	fz_try(ctx)
		// Font neu anlegen als indirektes Objekt
		font = pdf_new_dict(ctx, doc, 4);
	fz_catch(ctx)
		ERROR_PDF_VAL(NULL)

	fz_try(ctx) {
		pdf_dict_put(ctx, font, PDF_NAME(Type), PDF_NAME(Font));
		pdf_dict_put(ctx, font, PDF_NAME(Subtype), PDF_NAME(Type1));
		pdf_dict_put_drop(ctx, font, PDF_NAME(BaseFont), pdf_new_name(ctx, "Helvetica"));
		pdf_dict_put(ctx, font, PDF_NAME(Encoding), PDF_NAME(WinAnsiEncoding));

		// Als indirektes Objekt hinzufügen
		font_ref = pdf_add_object(ctx, doc, font);
	}
	fz_always(ctx)
		pdf_drop_obj(ctx, font);
	fz_catch(ctx)
		ERROR_PDF_VAL(NULL)

	return font_ref;
}

gint sond_ocr_page_has_hidden_text(fz_context* ctx, pdf_page* page,
		gboolean* hidden, GError** error) {
	pdf_processor* proc = NULL;

	proc = new_text_analyzer_processor(ctx, NULL, 3, error);
	if (!proc)
		ERROR_Z

	fz_try(ctx)
		pdf_process_contents(ctx, proc, page->doc, pdf_page_resources(ctx, page),
				pdf_page_contents(ctx, page), NULL, NULL);
	fz_catch(ctx) {
		pdf_close_processor(ctx, proc);
		pdf_drop_processor(ctx, proc);

		ERROR_PDF
	}

	*hidden = ((pdf_text_analyzer_processor*) proc)->has_hidden_text;
	pdf_close_processor(ctx, proc);
	pdf_drop_processor(ctx, proc);

	return 0;
}

gint sond_ocr_pdf_doc(fz_context* ctx, pdf_document* doc, SondOcrPool* ocr_pool,
		TessBaseAPI osd_api, GError** error) {
	gint num_pages = 0;
	pdf_obj* font_ref = NULL;
	gint pages_done = 0;

	fz_try(ctx)
		num_pages = pdf_count_pages(ctx, doc);
	fz_catch(ctx)
		ERROR_PDF

	font_ref = sond_ocr_put_sond_font(ctx, doc, error);
	if (!font_ref)
		ERROR_Z

	SondOcrTask* task = g_new0(SondOcrTask, num_pages);

	while (pages_done < num_pages || ocr_pool->cancel_all) {
		for (gint i = 0; i < num_pages; i++) {
			gint rc = 0;
			pdf_page* page = NULL;
			gboolean hidden = FALSE;
			gint progress = 0;
			gint status = 0;

			status = g_atomic_int_get(task[i].status);
			if (status == 1) //läuft gerade
				continue;

			if (status == 2) { //fertig und in Ordnung
				gint rc = 0;

				//content einfügen
				rc = add_ocr_layer_to_page(ctx, task->content, page, font_ref,
						&task[i].ocr_transform, error);
				if (rc) {
					if (ocr_pool->log_func)
						ocr_pool->log_func(ocr_pool->log_data,
								"Einfügen der OCR-Daten in PDF-Page fehlgeschlagen: %s",
								(*error)->message);
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

			fz_try(ctx)
				page = pdf_load_page(ctx, doc, i);
			fz_catch(ctx) {
				log_func(log_data, "Seite %u: pdf_load_page: %s", i, fz_caught_message(ctx));
				continue;
			}

			rc = sond_ocr_page_has_hidden_text(ctx, page, &hidden, error);
			if (rc) {
				pdf_drop_page(ctx, page);
				log_func(log_data, "Seite %u: Prüfung auf versteckten Text fehlgeschlagen: %s",
						i, (*error)->message);
				g_clear_error(error);

				continue;
			}

			if (hidden) {
				log_func(log_data, "Seite %u: enthält versteckten Text - OCR übersprungen",
						page->super.number);
				pdf_drop_page(ctx, page);

				continue;
			}

			rc = sond_ocr_page(ctx, page, font_ref, ocr_pool, task[i], osd_api,
					log_func, log_data, progress_func, &progress, error);
			if (rc) {
				log_func(log_data, "Seite %u: OCR failed: %s",
						(*error)->message);
				g_clear_error(error);
			}
		}
	}
	pdf_drop_obj(ctx, font_ref);

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

static gint ocr_pixmap(SondOcrTask* task, SondOcrPool* pool,
		TesseractThreadData* thread_data, GError** error) {

	//jetzt richtige OCR
	TessBaseAPISetImage(thread_data->api, task->pixmap->samples, task->pixmap->w, task->pixmap->h,
			task->pixmap->n, task->pixmap->stride);
	TessBaseAPISetSourceResolution(thread_data->api, (gint) (task->scale * 72.0));

	rc = TessBaseAPIRecognize(thread_data->api, thread_data->monitor);
	if (rc) { //muß sorum abgefragt werden, weil Abbruch auch rc = 1 macht
		if (pool->log_func)
			pool->log_func(pool->log_data, "Recognize fehlgeschlagen");

		return -1;
	}

	conf = TessBaseAPIMeanTextConf(thread_data->api);
	if (conf > 80 || task->durchgang == 2)
		return 0;

	//wenn nicht:
	task->durchgang++;
	if (pool->log_func)
		pool->log_func(pool->log_data,
			"OCR-Konfidenz %d%% zu niedrig, nächster Durchgang", conf);

	return 1;
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
        	g_atomic_int_set(task->status, 3);

        	goto dec_active_jobs;
        }
    }

    rc = ocr_pixmap(task, pool, thread_data, &error);

    fz_context* ctx_clone = fz_clone_context(pool->ctx);
    fz_drop_pixmap(ctx_clone, task->pixmap)
    if (rc == -1) {
		if (pool->log_func)
			pool->log_func(pool->log_data, "Recog failed: %s", error->message);
		g_error_free(error);
    	g_atomic_int_set(task->status, 3);
    	goto dec_active_jobs;
    }
    else if (rc == 1) {
    	g_atomic_int_set(task->status, 0);
    	goto dec_active_jobs;
    }

	TessResultIterator* iter = TessBaseAPIGetIterator(thread_data->api);
	if (!iter) {
		if (pool->log_func)
			pool->log_func(pool->log_data, "Couldn't get ResultIter");
		g_atomic_int_set(task->status, 3);
		goto dec_active_jobs;
	}

	// Content Stream mit korrekten Koordinaten erstellen
	task->content = tesseract_to_content_stream(ctx_clone, iter,
			&task->ocr_transform, &error);
	if (!ocr_content) {
		if (pool->log_func)
			pool->log_func(pool->log_data,
					"Umwandlung Results in content stream fehlgeschlagen: %s",
					error->message);
		g_error_free(error)
		g_atomic_int_set(task->status, 3);
		goto dec_active_jobs;
	}

	g_atomic_int_set(task->status, 2);

dec_active_jobs:
    g_atomic_int_dec_and_test(pool->num_active_jobs);
    fz_drop_context(ctx_clone);

    return;
}

static gboolean ocr_cancel(void *cancel_this, int words) {
	volatile gboolean *cancelFlag = (volatile gboolean*) cancel_this;
	return *cancelFlag;
}

// Initialisiert Tesseract für den aktuellen Thread
static TesseractThreadData* get_or_create_thread_data(GPrivate *thread_data_key,
                                                       const gchar *tessdata_path,
                                                       const gchar *language,
													   gboolean* cancel_all,
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

// Worker-Funktion die im Thread-Pool ausgeführt wird
SondOcrPool* sond_ocr_pool_new(const gchar *tessdata_path,
                                   const gchar *language,
                                   gint num_threads,
								   fz_context* ctx,
								   void (*log_func)(void*, gchar const*, ...),
								   gpointer log_data,
                                   GError **error) {
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
    if (!pool->pool)
    	return NULL;

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

    // Cleanup Pool-Struktur
    g_free(pool->tessdata_path);
    g_free(pool->language);
    g_mutex_clear(&pool->mutex);
    g_free(pool);
}

