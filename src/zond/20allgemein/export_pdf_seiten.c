/*
 zond (export_pdf_seiten.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2020  pelo america

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

#include <glib.h>
#include <math.h>

#include "../../sond_pdf_helper.h"

#include "export_pdf_seiten.h"

//Schutz gegen Endlosschleife, wenn die Story keinen Fortschritt macht
#define INFOSEITEN_MAX 2000

static const gchar *infoseiten_css =
		"body { font-family: sans-serif; font-size: 11pt; }"
		"h1 { font-size: 20pt; margin-top: 0pt; margin-bottom: 6pt; }"
		"h2 { font-size: 17pt; margin-top: 0pt; margin-bottom: 6pt; }"
		"h3 { font-size: 15pt; margin-top: 0pt; margin-bottom: 5pt; }"
		"h4 { font-size: 13pt; margin-top: 0pt; margin-bottom: 5pt; }"
		"h5 { font-size: 12pt; margin-top: 0pt; margin-bottom: 4pt; }"
		"h6 { font-size: 11pt; margin-top: 0pt; margin-bottom: 4pt; }"
		"p { margin-top: 0pt; margin-bottom: 6pt; }"
		"p.pfad { font-size: 9pt; color: #666666; }"
		"p.anb { font-size: 9pt; font-style: italic; }"
		"p.hinweis { font-weight: bold; color: #aa0000; }";

static void export_pdf_fehler(fz_context *ctx, GError **error,
		const gchar *was) {
	g_set_error(error, g_quark_from_static_string("mupdf"), fz_caught(ctx),
			"%s\n%s", was, fz_caught_message(ctx));
}

//Sichtbaren Bereich der zuletzt angehängten Seite von dest beschneiden
static void export_pdf_zuschneiden(fz_context *ctx, pdf_document *dest,
		gdouble y0, gdouble y1) {
	pdf_obj *obj = NULL;
	fz_rect box = { 0 };
	fz_matrix ctm = { 0 };
	fz_rect r = { 0 };
	fz_rect r_pdf = { 0 };

	obj = pdf_lookup_page_obj(ctx, dest, pdf_count_pages(ctx, dest) - 1);

	//box im PDF-Raum, ctm in den Seitenraum (von oben, gedreht), wie der Index
	pdf_page_obj_transform(ctx, obj, &box, &ctm);
	r = fz_transform_rect(box, ctm);

	if (y0 >= 0)
		r.y0 = MAX(r.y0, (float) y0);
	if (y1 >= 0)
		r.y1 = MIN(r.y1, (float) y1);

	if (r.y1 <= r.y0)
		return;

	r_pdf = fz_transform_rect(r, fz_invert_matrix(ctm));
	r_pdf = fz_intersect_rect(r_pdf, box);

	pdf_dict_put_rect(ctx, obj, PDF_NAME(CropBox), r_pdf);
}

gint export_pdf_seiten_kopieren(fz_context *ctx, pdf_document *dest,
		pdf_document *src, pdf_graft_map *map, const ExportPdfBereich *bereich,
		GError **error) {
	for (gint i = bereich->von; i <= bereich->bis; i++) {
		if (bereich->auslassen && bereich->auslassen[i - bereich->von])
			continue;

		//mit Annotationen, die pdf_graft_mapped_page() allein weglässt
		if (pdf_graft_page_mit_annots(ctx, map, dest, src, i, error))
			return -1;

		if ((i == bereich->von && bereich->y0 >= 0)
				|| (i == bereich->bis && bereich->y1 >= 0)) {
			fz_try(ctx)
				export_pdf_zuschneiden(ctx, dest,
						(i == bereich->von) ? bereich->y0 : -1.0,
						(i == bereich->bis) ? bereich->y1 : -1.0);
			fz_catch(ctx) {
				export_pdf_fehler(ctx, error, __func__);

				return -1;
			}
		}
	}

	return 0;
}

gint export_pdf_infoseiten(fz_context *ctx, pdf_document *dest,
		const gchar *html, GError **error) {
	fz_buffer *buf = NULL;
	fz_story *story = NULL;
	gint seiten = 0;
	gint rc = 0;

	const fz_rect mediabox = { 0, 0, 595, 842 };
	const fz_rect where = { 56, 56, 539, 786 };

	fz_try(ctx) {
		gint mehr = 0;

		buf = fz_new_buffer_from_copied_data(ctx, (const unsigned char*) html,
				strlen(html));
		story = fz_new_story(ctx, buf, infoseiten_css, 11, NULL);

		do {
			fz_rect filled = { 0 };
			fz_device *dev = NULL;
			pdf_obj *resources = NULL;
			fz_buffer *contents = NULL;
			pdf_obj *page_obj = NULL;

			mehr = fz_place_story(ctx, story, where, &filled);

			if (++seiten > INFOSEITEN_MAX)
				fz_throw(ctx, FZ_ERROR_GENERIC,
						"Infotext füllt mehr als %d Seiten", INFOSEITEN_MAX);

			dev = pdf_page_write(ctx, dest, mediabox, &resources, &contents);
			fz_try(ctx) {
				fz_draw_story(ctx, story, dev, fz_identity);
				fz_close_device(ctx, dev);
			}
			fz_always(ctx)
				fz_drop_device(ctx, dev);
			fz_catch(ctx) {
				pdf_drop_obj(ctx, resources);
				fz_drop_buffer(ctx, contents);
				fz_rethrow(ctx);
			}

			fz_try(ctx) {
				page_obj = pdf_add_page(ctx, dest, mediabox, 0, resources,
						contents);
				pdf_insert_page(ctx, dest, -1, page_obj);
			}
			fz_always(ctx) {
				pdf_drop_obj(ctx, page_obj);
				pdf_drop_obj(ctx, resources);
				fz_drop_buffer(ctx, contents);
			}
			fz_catch(ctx)
				fz_rethrow(ctx);
		} while (mehr);
	}
	fz_always(ctx) {
		fz_drop_story(ctx, story);
		fz_drop_buffer(ctx, buf);
	}
	fz_catch(ctx) {
		export_pdf_fehler(ctx, error, __func__);
		rc = -1;
	}

	return rc;
}
