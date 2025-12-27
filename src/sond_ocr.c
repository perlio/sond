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

#include "sond.h"
#include "sond_fileparts.h"
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

	if (p->flags & 1 && Tr != 3)
		return;
	else if (p->flags & 2 && Tr == 3)
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

	if (p->flags & 1 && Tr != 3)
		return;
	else if (p->flags & 2 && Tr == 3)
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

	if (p->flags & 1 && Tr != 3)
		return;
	else if (p->flags & 2 && Tr == 3)
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

	if (p->flags & 1 && Tr != 3)
		return;
	else if (p->flags & 2 && Tr == 3)
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
	((pdf_text_analyzer_processor*) proc)->has_hidden_text = FALSE;
	((pdf_text_analyzer_processor*) proc)->has_visible_text = FALSE;
	g_array_remove_range(((pdf_text_analyzer_processor*) proc)->arr_Tr, 0,
			((pdf_text_analyzer_processor*) proc)->arr_Tr->len);

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

	return (pdf_processor*) proc;
}

static void write_log(SondFilePartPDF* sfp_pdf, gint num, gchar const* errmsg) {
	g_message("%s  %u  %s", sond_file_part_get_path(SOND_FILE_PART(sfp_pdf)), num, errmsg);

	return;
}

gint rotate_page(fz_context *ctx, pdf_obj *page_obj, gint winkel,
		GError** error) {
	pdf_obj *rotate_obj = NULL;
	gint rotate = 0;

	fz_try(ctx) //erstmal existierenden rotate-Wert ermitteln
		rotate_obj = pdf_dict_get_inheritable(ctx, page_obj, PDF_NAME(Rotate));
	fz_catch(ctx)
		ERROR_PDF

	if (rotate_obj) //sonst halt 0
		rotate = pdf_to_int(ctx, rotate_obj); //Anfangswert

	rotate = rotate + winkel;
	if (rotate < 0)
		rotate += 360;
	else if (rotate > 360)
		rotate -= 360;
	else if (rotate == 360)
		rotate = 0;

	//prüfen, ob page-Knoten einen /Rotate-Eintrag hat, nicht nur geerbt
	if (!rotate_obj || pdf_dict_get(ctx, rotate_obj, PDF_NAME(Parent)) != page_obj) {
		pdf_obj* rotate_page = NULL;

		//dann erzeugen und einfügen
		rotate_page = pdf_new_int(ctx, (int64_t) rotate);
		fz_try(ctx)
			pdf_dict_put(ctx, page_obj, PDF_NAME(Rotate), rotate_page);
		fz_always(ctx)
			pdf_drop_obj(ctx, rotate_obj);
		fz_catch(ctx)
			ERROR_PDF
	}
	else
		pdf_set_int(ctx, rotate_obj, (int64_t) rotate);

	return 0;
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
				fz_append_printf(ctx, buf, "\\\\%03o", c);  // Oktal-Escape
			} else if (*p == 0xC2 && *(p+1)) {
				// Weitere Latin-1 Zeichen (z.B. ©, ®, °, etc.)
				p++;
				fz_append_printf(ctx, buf, "\\\\%03o", *p);
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

// Transformations-Kontext für OCR-Koordinaten
typedef struct {
    float scale_x;
    float scale_y;
    int rotation;  // 0, 90, 180, 270
    float page_width;
    float page_height;
} ocr_transform;

// Transformation berechnen
static void transform_coordinates(const ocr_transform *t,
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

static void append_text_matrix(fz_context *ctx, fz_buffer *buf,
            const ocr_transform *t,
            float pdf_x, float pdf_y) {
	switch (t->rotation) {
		case 0:
		// Normal horizontal
		fz_append_printf(ctx, buf, "1 0 0 1 %.2f %.2f Tm\n", pdf_x, pdf_y);
		break;

		case 90:
		// 90° gegen Uhrzeigersinn (PDF-Rotation)
		fz_append_printf(ctx, buf, "0 1 -1 0 %.2f %.2f Tm\n", pdf_x, pdf_y);
		break;

		case 180:
		// 180° gedreht
		fz_append_printf(ctx, buf, "-1 0 0 -1 %.2f %.2f Tm\n", pdf_x, pdf_y);
		break;

		case 270:
		// 270° gegen Uhrzeigersinn
		fz_append_printf(ctx, buf, "0 -1 1 0 %.2f %.2f Tm\n", pdf_x, pdf_y);
		break;

		default:
		fz_append_printf(ctx, buf, "1 0 0 1 %.2f %.2f Tm\n", pdf_x, pdf_y);
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
fz_buffer* tesseract_to_content_stream(fz_context *ctx,
                                        TessBaseAPI *api,
                                        const ocr_transform *transform,
										GError **error) {
    fz_buffer *content = NULL;

	TessResultIterator *iter = TessBaseAPIGetIterator(api);
	if (!iter) {
		g_set_error(error, SOND_ERROR, 0,
				"Tesseract: Kein ResultIterator verfügbar");

		return NULL;
	}

	TessPageIteratorLevel level = RIL_WORD;

    fz_try(ctx)
        content = fz_new_buffer(ctx, 4096);
    fz_catch(ctx)
		ERROR_PDF_VAL(NULL);

    fz_try(ctx)
        fz_append_string(ctx, content, "\nq\nBT\n");
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

		// Schriftgröße aus Höhe (vor Transformation!)
		float word_height = (y2 - y1) / transform->scale_y;
		float font_size = word_height * 0.85f;
		if (font_size < 1.0f) font_size = 1.0f;

		if (fabsf(font_size - last_font_size) > 0.5f) {
			fz_try(ctx)
				fz_append_printf(ctx, content, "/FSond %.2f Tf\n", font_size);
			fz_catch(ctx) {
				fz_drop_buffer(ctx, content);

				ERROR_PDF_VAL(NULL);
			}

			last_font_size = font_size;
		}
if (font_size < 2.0f || font_size > 7.0f)
		g_message("'%s': BBox w=%d h=%d, font_size=%.2f\n",
				word ? word : "", x2 - x1, y2 - y1, font_size);

		// Koordinaten transformieren (Bild → PDF mit Rotation)
		float pdf_x, pdf_y;
		transform_coordinates(transform, x1, y2, &pdf_x, &pdf_y);

		// Text-Matrix setzen
		append_text_matrix(ctx, content, transform, pdf_x, pdf_y);

		// Text ausgeben
		rc = append_winansi_text(ctx, content, word, error);
		if (rc)
			ERROR_Z_VAL(NULL);

		fz_try(ctx)
            fz_append_printf(ctx, content, " Tj\n");
		fz_catch(ctx) {
			fz_drop_buffer(ctx, content);
            TessDeleteText(word);

            ERROR_PDF_VAL(NULL);
		}
	} while (TessResultIteratorNext(iter, level));

	fz_try(ctx)
        fz_append_string(ctx, content, "ET\nQ\n");
	fz_catch(ctx) {
		fz_drop_buffer(ctx, content);

		ERROR_PDF_VAL(NULL);
	}

	TessResultIteratorDelete(iter);

    return content;
}
#include "zond/99conv/test.h"

// Aktualisierte Hauptfunktion
static gint calculate_ocr_transform(fz_context *ctx,
                                               pdf_page *page,
											   float scale,
											   ocr_transform *t,
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

static gint add_ocr_layer_to_page(fz_context *ctx,
                           pdf_page *page,
						   pdf_obj* font_ref,
						   float scale,
                           TessBaseAPI *api,
						   TessOrientation orientation,
						   float deskew_angle,
						   GError** error) {
	ocr_transform ocr_transform = { 0 };
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

	// Transformation berechnen
	rc = calculate_ocr_transform(ctx, page, scale, &ocr_transform, error);
	if (rc)
		ERROR_Z

	// Content Stream mit korrekten Koordinaten erstellen
	fz_buffer *ocr_content = tesseract_to_content_stream(ctx, api, &ocr_transform, error);
	if (!ocr_content)
		ERROR_Z

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
	fz_catch (ctx) {
		fz_drop_buffer(ctx, ocr_content);
		ERROR_PDF
	}

	// OCR an Content Stream anhängen
	fz_try(ctx)
		fz_append_buffer(ctx, buf_new, ocr_content);
	fz_catch(ctx) {
		fz_drop_buffer(ctx, ocr_content);

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
/*
    // text-analyzer-Processor erstellen (dieser filtert den Text)
	fz_try(ctx) //flag == 3: aller Text weg
		proc_text = new_text_analyzer_processor(ctx, proc_run, 3, error);
	fz_catch(ctx) {
		pdf_close_processor(ctx, proc_run);
		pdf_drop_processor(ctx, proc_run);

		ERROR_PDF
	}
*/
    // Content durch Filter-Kette schicken
	fz_try(ctx)
		pdf_process_contents(ctx, proc_run, page->doc,
				pdf_page_resources(ctx, page), pdf_page_contents(ctx, page),
				NULL, NULL);
	fz_always(ctx) {
		pdf_close_processor(ctx, proc_run);
//		pdf_drop_processor(ctx, proc_text);
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

static gint page_has_hidden_text(fz_context* ctx, pdf_page* page,
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

static gint sond_ocr_page(SondFilePartPDF* sfp_pdf, fz_context* ctx,
		pdf_page* page, pdf_obj* font_ref, TessBaseAPI* handle, GError** error) {
	float scale[] = {4.3, 6.4, 8.6 };
	gint durchgang = 0;
	gint rc = 0;
	gboolean hidden = FALSE;
	TessResultIterator* iter = NULL;
	TessOrientation orientation = ORIENTATION_PAGE_UP;
	float deskew_angle;

	rc = page_has_hidden_text(ctx, page, &hidden, error);
	if (rc)
		ERROR_Z

	if (hidden) {
		write_log(sfp_pdf, page->super.number, "Seite enthält bereits hidden text");

		return 0;
	}

	do {
		gint rc = 0;
		fz_pixmap* pixmap = NULL;

		pixmap = sond_ocr_render_pixmap(ctx, page, scale[durchgang], error);
		if (!pixmap)
			ERROR_Z

		TessBaseAPISetImage(handle, pixmap->samples, pixmap->w, pixmap->h,
				pixmap->n, pixmap->stride);
		TessBaseAPISetSourceResolution(handle, (gint) (1. / scale[durchgang] * 72.0 / 70.0));

		rc = TessBaseAPIRecognize(handle, NULL);
		fz_drop_pixmap(ctx, pixmap);
		if (rc) {
			g_set_error(error, SOND_ERROR,  0, "%s\nRecognize fehlgeschlagen", __func__);

			return -1;
		}

		if (TessBaseAPIMeanTextConf(handle) > 80 || durchgang == 2 || TRUE)
			break;

		//wenn nicht:
		durchgang++;
	} while(1);

	TessPageIterator* iter_page = TessBaseAPIAnalyseLayout(handle);
	if (iter_page) {
		TessWritingDirection writing_direction;
		TessTextlineOrder textline_order;
		TessPageIteratorOrientation(iter_page, &orientation, &writing_direction, &textline_order, &deskew_angle);
		TessResultIteratorDelete(iter);

		if (orientation != ORIENTATION_PAGE_UP) {
			gint rc = 0;

			rc = rotate_page(ctx, page->obj, orientation * 90, error);
			if (rc)
				ERROR_Z
		}
	}
	else
		write_log(sfp_pdf, page->super.number, "Konnte Orientierung nicht ermitteln");

	rc = add_ocr_layer_to_page(ctx, page, font_ref, scale[durchgang],
			handle, orientation, deskew, error);
	if (rc)
		ERROR_Z

	return 0;
}

static gint sond_ocr_pdf_doc(fz_context* ctx, pdf_document* doc,
		SondFilePartPDF* sfp_pdf, TessBaseAPI* handle, GError** error) {
	gint num_pages = 0;
	gint rc = 0;
	pdf_obj* font = NULL;
	pdf_obj* font_ref = NULL;

	fz_try(ctx)
		num_pages = pdf_count_pages(ctx, doc);
	fz_catch(ctx)
		ERROR_PDF

	fz_var(font);

	fz_try(ctx) {
	// Font neu anlegen als indirektes Objekt
		font = pdf_new_dict(ctx, doc, 4);
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
		ERROR_PDF

	for (gint i = 0; i < 1; i++) {//num_pages; i++) {
		gint rc = 0;
		pdf_page* page = NULL;

		fz_try(ctx)
			page = pdf_load_page(ctx, doc, i);
		fz_catch(ctx) {
			write_log(sfp_pdf, i, fz_caught_message(ctx));
			continue;
		}

		rc = sond_ocr_page(sfp_pdf, ctx, page, font_ref, handle, error);
		pdf_drop_page(ctx, page);
		if (rc) { //Fehler auf Seitenebene loggen
			write_log(sfp_pdf, i, (*error)->message);
			g_clear_error(error);
		}
	}

	rc = sond_file_part_pdf_save(ctx, doc, sfp_pdf, error);
	if (rc)
		ERROR_Z

	return 0;
}


static gint init_tesseract(TessBaseAPI **handle, GError** error) {
	gint rc = 0;

	//TessBaseAPI
	*handle = TessBaseAPICreate();
	if (!(*handle)) {
		g_set_error(error, SOND_ERROR, 0, "%s\nTessBaseAPICreate fehlgeschlagen", __func__);

		return -1;
	}

	rc = TessBaseAPIInit3(*handle, "C:\\msys64/ucrt64/share/tessdata", "deu");
	if (rc) {
		TessBaseAPIEnd(*handle);
		TessBaseAPIDelete(*handle);
		g_set_error(error, SOND_ERROR, 0, "%s\nTessBaseAPIInit3 fehlgeschlagen", __func__);

		return -1;
	}
    TessBaseAPISetPageSegMode(*handle, PSM_AUTO);

	return 0;
}

gint sond_ocr_pdf(SondFilePartPDF* sfp_pdf, GError** error) {
	gint rc = 0;
	fz_context *ctx = NULL;
	pdf_document* doc = NULL;

	TessBaseAPI *handle = NULL;

	ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
	if (!ctx) {
		g_set_error(error, SOND_ERROR, 0,
				"%s\nfz_context konnte nicht geladen werden", __func__);

		return -1;
	}

	doc = sond_file_part_pdf_open_document(ctx, sfp_pdf, FALSE, FALSE, FALSE, error);
	if (!doc) {
		fz_drop_context(ctx);

		ERROR_Z
	}

	rc = init_tesseract(&handle, error);
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

