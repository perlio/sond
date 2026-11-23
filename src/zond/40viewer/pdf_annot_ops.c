/*
 zond (pdf_annot_ops.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2026  pelo america

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

/* Annotationen in MuPDF anlegen und ändern, ohne Oberfläche: Speichern, Journal und Viewer
 * bauen darauf auf. */

#include <glib.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#include "../zond_pdf_document.h"

#include "pdf_annot_ops.h"

gboolean pdf_annot_ops_is_in_rect(Annot* annot, fz_rect rect) {
	if (annot->type == PDF_ANNOT_TEXT) {
		fz_rect annot_rect = annot->annot_text.rect;

		if (fz_is_valid_rect(fz_intersect_rect(annot_rect, rect)))
			return TRUE;
	}
	else if (annot->type == PDF_ANNOT_HIGHLIGHT
			|| annot->type == PDF_ANNOT_UNDERLINE) {
		for (gint i = 0; i < annot->annot_text_markup.arr_quads->len; i++) {
			fz_quad quad = { 0 };

			quad = g_array_index(annot->annot_text_markup.arr_quads, fz_quad, i);
			if (fz_is_point_inside_rect(quad.ul, rect) ||
					fz_is_point_inside_rect(quad.lr, rect))
				return TRUE;
		}
	}

	return FALSE;
}

static fz_rect pdf_annot_rotate_rect(gint rotate, fz_rect rect) {
	if (rotate == 90) {
		rect.x0 -= ANNOT_ICON_WIDTH;
		rect.x1 -= ANNOT_ICON_WIDTH;
	} else if (rotate == 180) {
		rect.x0 -= ANNOT_ICON_WIDTH;
		rect.x1 -= ANNOT_ICON_WIDTH;
		rect.y0 -= ANNOT_ICON_HEIGHT;
		rect.y1 -= ANNOT_ICON_HEIGHT;
	} else if (rotate == 270) {
		rect.y0 -= ANNOT_ICON_HEIGHT;
		rect.y1 -= ANNOT_ICON_HEIGHT;
	}

	return rect;
}

gint pdf_annot_ops_change(fz_context* ctx, pdf_annot* pdf_annot, gint rotate,
		Annot annot, GError** error) {
	if (annot.type == PDF_ANNOT_HIGHLIGHT || annot.type == PDF_ANNOT_UNDERLINE) {
		for (gint i = 0; i < annot.annot_text_markup.arr_quads->len; i++) {
			fz_try( ctx )
				pdf_add_annot_quad_point(ctx, pdf_annot,
						g_array_index(annot.annot_text_markup.arr_quads, fz_quad, i));
			fz_catch (ctx) {
				if (error) *error = g_error_new(g_quark_from_static_string("mupdf"), fz_caught(ctx),
						"%s\n%s", __func__, fz_caught_message(ctx));

				return -1;
			}
		}
	} else if (annot.type == PDF_ANNOT_TEXT) {
		fz_try(ctx) {
			if (annot.annot_text.content)
				pdf_set_annot_contents(ctx, pdf_annot, annot.annot_text.content);
			pdf_set_annot_rect(ctx, pdf_annot, pdf_annot_rotate_rect(rotate, annot.annot_text.rect));
		}
		fz_catch(ctx) {
			if (error) *error = g_error_new(g_quark_from_static_string("mupdf"), fz_caught(ctx),
					"%s\n%s", __func__, fz_caught_message(ctx));

			return -1;
		}
	}

	fz_try(ctx)
		pdf_update_annot(ctx, pdf_annot);
	fz_catch(ctx) {
		if (error) *error = g_error_new(g_quark_from_static_string("mupdf"), fz_caught(ctx),
				"%s\n%s", __func__, fz_caught_message(ctx));

		return -1;
	}

	return 0;
}

pdf_annot* pdf_annot_ops_create(fz_context* ctx, pdf_page* pdf_page, gint rotate,
		Annot annot, gboolean* created, GError** error) {
	pdf_annot* pdf_annot = NULL;
	gint rc = 0;

	if (created) *created = FALSE;

	fz_try(ctx) pdf_annot = pdf_create_annot(ctx, pdf_page, annot.type);
	fz_catch(ctx) {
		if (error) *error = g_error_new(g_quark_from_static_string("mupdf"), fz_caught(ctx),
				"%s\n%s", __func__, fz_caught_message(ctx));

		return NULL;
	}
	pdf_drop_annot(ctx, pdf_annot); //geht schon jetzt; page behält ref!

	/* Ab hier existiert die Annotation physisch auf pdf_page, auch wenn
	 * einer der folgenden Schritte noch scheitert - das muß der Aufrufer
	 * wissen (s. dort), um die Live-Buchführung (arr_annots) konsistent zu
	 * einer eventuell schon vorhandenen zusätzlichen physischen Annotation
	 * zu halten. */
	if (created) *created = TRUE;

	if (annot.type == PDF_ANNOT_UNDERLINE) {
		const gfloat color[3] = { 0.1, .85, 0 };
		fz_try(ctx) pdf_set_annot_color(ctx, pdf_annot, 3, color);
		fz_catch( ctx )
		{
			if (error) *error = g_error_new(g_quark_from_static_string("mupdf"), fz_caught(ctx),
					"%s\n%s", __func__, fz_caught_message(ctx));

			return NULL;
		}
	} else if (annot.type == PDF_ANNOT_TEXT) {
		fz_try(ctx) pdf_set_annot_icon_name( ctx, pdf_annot, "Comment" );
		fz_catch( ctx )
		{
			if (error) *error = g_error_new(g_quark_from_static_string("mupdf"), fz_caught(ctx),
					"%s\n%s", __func__, fz_caught_message(ctx));

			return NULL;
		}
	}

	rc = pdf_annot_ops_change(ctx, pdf_annot, rotate, annot, error);
	if (rc)
		return NULL;

	return pdf_annot;
}
