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

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <gtk/gtk.h>
#include <tesseract/capi.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "misc.h"

#include "zond/99conv/pdf.h"

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
	{
		g_array_remove_index(p->arr_Tr, p->arr_Tr->len - 1);

		if (proc && proc->chain && proc->chain->op_Q)
			proc->chain->op_Q(ctx, proc->chain);
	}

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

	if (p->flags & 1 && Tr != 3)
		p->has_visible_text = TRUE;
	else if (p->flags & 2 && Tr == 3)
		p->has_hidden_text = TRUE;
	else if (proc && proc->chain && proc->chain->op_TJ)
		proc->chain->op_TJ(ctx, proc->chain, array);

	return;
}

static void text_analyzer_op_Tj(fz_context *ctx, pdf_processor *proc,
		gchar *str, size_t len) {
	gint Tr = 0;

	pdf_text_analyzer_processor *p = (pdf_text_analyzer_processor*) proc;

	Tr = g_array_index(p->arr_Tr, gint, p->arr_Tr->len - 1);

	if (p->flags & 1 && Tr != 3)
		p->has_visible_text = TRUE;
	else if (p->flags & 2 && Tr == 3)
		p->has_hidden_text = TRUE;
	else if (proc && proc->chain && proc->chain->op_Tj)
		proc->chain->op_Tj(ctx, proc->chain, str, len);

	return;
}

static void text_analyzer_op_squote(fz_context *ctx, pdf_processor *proc,
		gchar *str, size_t len) {
	gint Tr = 0;

	pdf_text_analyzer_processor *p = (pdf_text_analyzer_processor*) proc;

	Tr = g_array_index(p->arr_Tr, gint, p->arr_Tr->len - 1);

	if (p->flags & 1 && Tr != 3)
		p->has_visible_text = TRUE;
	else if (p->flags & 2 && Tr == 3)
		p->has_hidden_text = TRUE;
	else if (proc && proc->chain && proc->chain->op_squote)
		proc->chain->op_squote(ctx, proc->chain, str, len);

	return;
}

static void text_analyzer_op_dquote(fz_context *ctx, pdf_processor *proc,
		float aw, float ac, gchar *str, size_t len) {
	gint Tr = 0;

	pdf_text_analyzer_processor *p = (pdf_text_analyzer_processor*) proc;

	Tr = g_array_index(p->arr_Tr, gint, p->arr_Tr->len - 1);

	if (p->flags & 1 && Tr != 3)
		p->has_visible_text = TRUE;
	else if (p->flags & 2 && Tr == 3)
		p->has_hidden_text = TRUE;
	else if (proc && proc->chain && proc->chain->op_dquote)
		proc->chain->op_dquote(ctx, proc->chain, aw, ac, str, len);

	return;
}

static void drop_text_analyzer_processor(fz_context* ctx, pdf_processor* proc) {
	g_array_unref(((pdf_text_analyzer_processor*) proc)->arr_Tr);

	if (proc && proc->chain && proc->chain->drop_processor)
		proc->chain->drop_processor(ctx, proc->chain);

	return;
}

static void reset_text_analyzer_processor(fz_context* ctx, pdf_processor* proc) {
	((pdf_text_analyzer_processor*) proc)->has_hidden_text = FALSE;
	((pdf_text_analyzer_processor*) proc)->has_hidden_text = TRUE;
	g_array_remove_range(((pdf_text_analyzer_processor*) proc)->arr_Tr, 0,
			((pdf_text_analyzer_processor*) proc)->arr_Tr->len);

	if (proc && proc->chain && proc->chain->reset_processor)
		proc->chain->reset_processor(ctx, proc->chain);

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

	proc->super.op_Q = text_analyzer_op_Q;
	proc->super.op_Tr = text_analyzer_op_Tr;
	proc->super.op_TJ = text_analyzer_op_TJ;
	proc->super.op_Tj = text_analyzer_op_Tj;
	proc->super.op_squote = text_analyzer_op_squote;
	proc->super.op_dquote = text_analyzer_op_dquote;
	proc->super.chain = chain;

	return (pdf_processor*) proc;
}


static gint sond_ocr_run_page(fz_context* ctx, pdf_page* page,
		fz_device* dev, GError** error) {
	pdf_processor * proc = NULL;
	pdf_processor* proc_text = NULL;

	fz_try(ctx)
		proc = pdf_new_run_processor(ctx, page->doc, dev, fz_identity, -1,
				"View", NULL, NULL, NULL, NULL, NULL);
	fz_catch(ctx)
		ERROR_PDF

    // text-analyzer-Processor erstellen (dieser filtert den Text)
	fz_try(ctx)
		proc_text = new_text_analyzer_processor(ctx, proc, 3, error);
	fz_catch(ctx) {
		pdf_drop_processor(ctx, proc);

		ERROR_PDF
	}

    // Content durch Filter-Kette schicken
	fz_try(ctx)
		pdf_process_contents(ctx, proc_text, page->doc,
				pdf_page_contents(ctx, page), pdf_page_resources(ctx, page),
				NULL, NULL);
	fz_catch(ctx) {
		pdf_drop_processor(ctx, proc_text);

		ERROR_PDF
	}

    // Cleanup
    pdf_drop_processor(ctx, proc_text);

    return 0;
}

static fz_pixmap*
sond_ocr_render_pixmap(fz_context *ctx, pdf_page* page,
		float scale, GError** error) {
	gint rc = 0;
	fz_device *draw_device = NULL;
	fz_pixmap *pixmap = NULL;

	fz_rect rect = pdf_bound_page(ctx, page, FZ_CROP_BOX);
	fz_matrix ctm = pdf_ocr_create_matrix(ctx, rect, scale, 0);

	rect = fz_transform_rect(rect, ctm);

	//per draw-device to pixmap
	fz_try(ctx)
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

static gint sond_ocr_page(fz_context* ctx, pdf_document* doc,
		gint num, TessBaseAPI* handle, GError** error) {
	float scale[] = {4.3, 6.4, 8.6 };
	pdf_page* page = NULL;
	page = pdf_load_page(ctx, doc, num);
	gint durchgang = 0;
	gint rc = 0;
	gchar* pdf_out = NULL;

	do {
		gint rc = 0;
		fz_pixmap* pixmap = NULL;
		gint conf = 0;

		pixmap = sond_ocr_render_pixmap(ctx, page, scale[durchgang], error);
		if (!pixmap) {
			pdf_drop_page(ctx, page);

			ERROR_Z
		}

		pdf_out = sond_ocr_do_ocr(handle, pixmap, &conf, error);
		fz_drop_pixmap(ctx, pixmap);
		if (!pdf_out) {
			pdf_drop_page(ctx, page);

			ERROR_PDF
		}

		if (conf > 80 || durchgang == 3)
			break;

		//wenn nicht:
		g_free(pdf_out);
		durchgang++
	} while(1);

	rc = sond_ocr_sandwich_page(ctx, page, pdf_out, error);
	pdf_drop_page(ctx, page);
	g_free(pdf_out);
	if (rc)
		ERROR_Z

	return 0;
}

static gint sond_ocr_pdf_doc(fz_context* ctx, pdf_document* doc,
		SondFilePartPDF* sfp_pdf, TessBaseAPI* handle, GError** error) {
	gint num_pages = 0;

	fz_try(ctx)
		num_pages = pdf_count_pages(ctx, doc);
	fz_catch(ctx)
		ERROR_Z

	for (gint i = 0; i < num_pages; i++) {
		gint rc = 0;

		rc = sond_ocr_page(ctx, doc, i, handle);
		if (rc)
			ERROR_Z
	}

	rc = sond_file_part_pdf_save(ctx, doc, sfp_pdf, error);
	if (rc)
		ERROR_Z
}


static gint init_tesseract(TessBaseAPI **handle, GError** error) {
	gint rc = 0;

	//TessBaseAPI
	*handle = TessBaseAPICreate();
	if (!(*handle)) {
		g_set_error(error, SOND_ERROR, 0, "%s\nTessBaseAPICreate fehlgeschlagen", __func__);

		return -1;
	}

	rc = TessBaseAPIInit3(*handle, NULL, "deu");
	if (rc) {
		TessBaseAPIEnd(*handle);
		TessBaseAPIDelete(*handle);
		g_set_error(error, SOND_ERROR, 0, "%s\nTessBaseAPIInit3 fehlgeschlagen", __func__);

		return -1;
	}

	return 0;
}

gint sond_ocr_pdf(SondFilePartPDF* sfp_pdf, GError** error) {
	gint rc = 0;
	fz_context *ctx = NULL;
	pdf_document* doc = NULL;

	TessBaseAPI *handle = NULL;

	ctx = fz_context_new(NULL, NULL, FZ_STORE_UNLIMITED);
	if (ctx) {
		g_set_error(error, SOND_ERROR, 0,
				"%s\nfz_context konnte nicht geladen werden", __func__);

		return -1;
	}

	doc = sond_file_part_pdf_open_document(ctx, sfp_pdf, FALSE, FALSE, FALSE, error);
	if (!doc) {
		fz_drop_context(ctx);

		ERROR_Z
	}

	rc = init_tesseract(zond, &handle, error);
	if (rc) {
		pdf_drop_document(ctx, doc);
		fz_drop_context(ctx);

		ERROR_Z
	}

	rc = sond_ocr_pdf_doc(ctx, doc, sfp_pdf, handle, error);
	pdf_drop_document(ctx, doc);
	fz_drop_context(ctx);
	TessBaseAPIEnd(handle);
	TessBaseAPIDelete(handle);

	if (rc)
		ERROR_Z

	return 0;
}

