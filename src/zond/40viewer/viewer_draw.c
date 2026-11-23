/*
 zond (viewer_draw.c) - Akten, Beweisstücke, Unterlagen
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

//Zeichnen der Seitenbilder: Markierungen von Text und angeklickten Annotationen über dem Bild

#include <gtk/gtk.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <cairo.h>

#include "../zond_pdf_document.h"

#include "viewer.h"
#include "viewer_draw.h"

static void viewer_render_mark_quad(cairo_t *cr, fz_quad quad,
		fz_matrix transform) {
	fz_rect rect = fz_transform_rect(fz_rect_from_quad(quad), transform);

	float x = rect.x0;
	float y = rect.y0;
	float width = rect.x1 - x;
	float heigth = rect.y1 - y;

	cairo_rectangle(cr, x, y, width, heigth);
	cairo_set_source_rgba(cr, 0, .1, .8, 0.5);
	cairo_fill(cr);
}

gboolean viewer_draw_image_page(GtkWidget *image, cairo_t *cr,
		gpointer data) {
	fz_matrix transform = { 0, };

	ViewerPageNew *viewer_page = (ViewerPageNew*) data;

	if (!gtk_widget_is_visible(image))
		return FALSE;

	transform = fz_translate(0.0, -viewer_page->crop.y0);
	transform = fz_post_scale(transform, viewer_page->pdfv->zoom / 100,
			viewer_page->pdfv->zoom / 100);

	//wenn annot angeclickt wurde
	if (viewer_page->pdfv->clicked_annot && //selbe Seite
			viewer_page->pdfv->clicked_annot->pdf_document_page
			== viewer_page->pdf_document_page) {
		if (viewer_page->pdfv->clicked_annot->annot.type == PDF_ANNOT_HIGHLIGHT
				|| viewer_page->pdfv->clicked_annot->annot.type
						== PDF_ANNOT_UNDERLINE) {
			GArray *arr_quads =
					viewer_page->pdfv->clicked_annot->annot.annot_text_markup.arr_quads;

			for (gint i = 0; i < arr_quads->len; i++) {
				fz_quad quad = g_array_index(arr_quads, fz_quad, i);
				quad = fz_transform_quad(quad, transform);
				cairo_move_to(cr, quad.ul.x, quad.ul.y);
				cairo_line_to(cr, quad.ur.x, quad.ur.y);
				cairo_line_to(cr, quad.lr.x, quad.lr.y);
				cairo_line_to(cr, quad.ll.x, quad.ll.y);
				cairo_line_to(cr, quad.ul.x, quad.ul.y);
				cairo_set_source_rgb(cr, 0, 1, 0);
				cairo_stroke(cr);
			}
		} else if (viewer_page->pdfv->clicked_annot->annot.type == PDF_ANNOT_TEXT) {
			fz_rect rect = { 0, };

			rect = fz_transform_rect(
					viewer_page->pdfv->clicked_annot->annot.annot_text.rect,
					transform);

			cairo_move_to(cr, rect.x0, rect.y0);
			cairo_line_to(cr, rect.x1, rect.y0);
			cairo_line_to(cr, rect.x1, rect.y1);
			cairo_line_to(cr, rect.x0, rect.y1);
			cairo_line_to(cr, rect.x0, rect.y0);
			cairo_set_source_rgb(cr, 0, 1, 0);
			cairo_stroke(cr);
		}
	} else //ansonsten etwaige highlights zeichnen
	{
		gint i = 0;
		while (viewer_page->pdfv->highlight.page[i] >= 0) {
			if (viewer_page
					== g_ptr_array_index(viewer_page->pdfv->arr_pages,
							viewer_page->pdfv->highlight.page[i]))
				viewer_render_mark_quad(cr, viewer_page->pdfv->highlight.quad[i],
						transform);

			i++;
		}
	}

	return FALSE;
}
