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
#define INFOSEITEN_MAX 10000

//A4 und Satzspiegel in Punkt
#define SEITE_B 595.0
#define SEITE_H 842.0
#define RAND 56.0
//Abstand zwischen aufeinanderfolgenden Inhalten
#define ABSTAND 8.0
//Rest einer Seite, unter dem ein Text lieber auf der nächsten Seite beginnt
#define MIN_REST 40.0

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
		"p.hinweis { font-weight: bold; color: #aa0000; }"
		"p.dok { font-size: 10pt; margin-bottom: 0pt; }";

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

		//ans Ende, mit Annotationen, ohne versteckte
		if (pdf_graft_page_mit_annots(ctx, map, dest, src, i, -1, FALSE, error))
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

struct _ExportPdfSatz {
	fz_context *ctx;
	pdf_document *dest;
	fz_device *dev;         //NULL: keine Seite offen
	pdf_obj *resources;
	fz_buffer *contents;
	gdouble y;              //nächste freie Position von oben
};

ExportPdfSatz* export_pdf_satz_new(fz_context *ctx, pdf_document *dest) {
	ExportPdfSatz *satz = g_new0(ExportPdfSatz, 1);

	satz->ctx = ctx;
	satz->dest = dest;

	return satz;
}

static void satz_seite_verwerfen(ExportPdfSatz *satz) {
	if (!satz->dev)
		return;

	fz_drop_device(satz->ctx, satz->dev);
	pdf_drop_obj(satz->ctx, satz->resources);
	fz_drop_buffer(satz->ctx, satz->contents);
	satz->dev = NULL;
	satz->resources = NULL;
	satz->contents = NULL;
}

void export_pdf_satz_free(ExportPdfSatz *satz) {
	if (!satz)
		return;

	satz_seite_verwerfen(satz);
	g_free(satz);
}

//wirft eine MuPDF-Exception
static void satz_seite_oeffnen(ExportPdfSatz *satz) {
	const fz_rect mediabox = { 0, 0, SEITE_B, SEITE_H };

	if (satz->dev)
		return;

	satz->dev = pdf_page_write(satz->ctx, satz->dest, mediabox,
			&satz->resources, &satz->contents);
	satz->y = RAND;
}

//wirft eine MuPDF-Exception; die Seite ist danach in jedem Fall weg
static void satz_seite_abschliessen(ExportPdfSatz *satz) {
	const fz_rect mediabox = { 0, 0, SEITE_B, SEITE_H };
	pdf_obj *page_obj = NULL;
	fz_context *ctx = satz->ctx;

	if (!satz->dev)
		return;

	fz_var(page_obj);

	fz_try(ctx) {
		fz_close_device(ctx, satz->dev);
		page_obj = pdf_add_page(ctx, satz->dest, mediabox, 0, satz->resources,
				satz->contents);
		pdf_insert_page(ctx, satz->dest, -1, page_obj);
	}
	fz_always(ctx) {
		pdf_drop_obj(ctx, page_obj);
		satz_seite_verwerfen(satz);
	}
	fz_catch(ctx)
		fz_rethrow(ctx);
}

gint export_pdf_satz_schliessen(ExportPdfSatz *satz, GError **error) {
	fz_context *ctx = satz->ctx;
	gint rc = 0;

	fz_try(ctx)
		satz_seite_abschliessen(satz);
	fz_catch(ctx) {
		export_pdf_fehler(ctx, error, __func__);
		rc = -1;
	}

	return rc;
}

gint export_pdf_satz_html(ExportPdfSatz *satz, const gchar *html,
		GError **error) {
	fz_context *ctx = satz->ctx;
	fz_buffer *buf = NULL;
	fz_story *story = NULL;
	gint seiten = 0;
	gint rc = 0;

	fz_var(buf);
	fz_var(story);

	fz_try(ctx) {
		gint mehr = 0;

		buf = fz_new_buffer_from_copied_data(ctx, (const unsigned char*) html,
				strlen(html));
		story = fz_new_story(ctx, buf, infoseiten_css, 11, NULL);

		do {
			fz_rect filled = { 0 };
			fz_rect where = { 0 };
			gboolean leer = FALSE;

			if (++seiten > INFOSEITEN_MAX)
				fz_throw(ctx, FZ_ERROR_GENERIC,
						"Text füllt mehr als %d Seiten", INFOSEITEN_MAX);

			satz_seite_oeffnen(satz);

			//zu wenig Platz auf der Seite: auf der nächsten beginnen
			if (SEITE_H - RAND - satz->y < MIN_REST) {
				satz_seite_abschliessen(satz);
				satz_seite_oeffnen(satz);
			}

			where.x0 = RAND;
			where.x1 = SEITE_B - RAND;
			where.y0 = (satz->y > RAND) ? satz->y + ABSTAND : RAND;
			where.y1 = SEITE_H - RAND;

			mehr = fz_place_story(ctx, story, where, &filled);
			leer = (filled.y1 - filled.y0) < 1.0f;

			if (leer && mehr) {
				//nichts passt in den Rest: Seite abschließen, neu versuchen
				if (satz->y <= RAND)
					fz_throw(ctx, FZ_ERROR_GENERIC,
							"Inhalt passt nicht auf eine Seite");
				satz_seite_abschliessen(satz);

				continue;
			}

			fz_draw_story(ctx, story, satz->dev, fz_identity);

			satz->y = MIN(MAX(satz->y, (gdouble) filled.y1), SEITE_H - RAND);

			if (mehr)
				satz_seite_abschliessen(satz);
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

gint export_pdf_satz_bild(ExportPdfSatz *satz, const guchar *data, gsize len,
		gdouble breite_cm, gdouble hoehe_cm, GError **error) {
	fz_context *ctx = satz->ctx;
	fz_buffer *buf = NULL;
	fz_image *image = NULL;
	gint rc = 0;

	const gdouble satz_b = SEITE_B - 2 * RAND;
	const gdouble satz_h = SEITE_H - 2 * RAND;

	fz_var(buf);
	fz_var(image);

	fz_try(ctx) {
		gdouble w = breite_cm / 2.54 * 72.0;
		gdouble h = hoehe_cm / 2.54 * 72.0;
		gdouble faktor = 1.0;
		gdouble y0 = 0;

		if (w > satz_b)
			faktor = satz_b / w;
		if (h * faktor > satz_h)
			faktor = satz_h / h;
		w *= faktor;
		h *= faktor;

		buf = fz_new_buffer_from_copied_data(ctx, data, len);
		image = fz_new_image_from_buffer(ctx, buf);

		satz_seite_oeffnen(satz);

		y0 = (satz->y > RAND) ? satz->y + ABSTAND : RAND;
		if (y0 + h > SEITE_H - RAND) {
			//passt nicht mehr auf die Seite
			satz_seite_abschliessen(satz);
			satz_seite_oeffnen(satz);
			y0 = RAND;
		}

		//Seitenraum und Bildraum von oben nach unten: Bild oben links bei (RAND, y0)
		fz_fill_image(ctx, satz->dev, image, fz_make_matrix(w, 0, 0, h, RAND, y0),
				1, fz_default_color_params);
		satz->y = y0 + h;
	}
	fz_always(ctx) {
		fz_drop_image(ctx, image);
		fz_drop_buffer(ctx, buf);
	}
	fz_catch(ctx) {
		export_pdf_fehler(ctx, error, __func__);
		rc = -1;
	}

	return rc;
}
