/*
 zond (viewer.c) - Akten, Beweisstücke, Unterlagen
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

#include "viewer.h"

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <mupdf/pdf.h>
#include <mupdf/fitz.h>

#include "../../misc.h"
#include "../../sond_fileparts.h"
#include "../../sond_log_and_error.h"
#include "../../sond_ocr.h"
#include "../../sond_pdf_helper.h"
#include "../../sond_index.h"
#include "../../sond_process_file.h"

#include "../zond_pdf_document.h"

#include "../99conv/general.h"

#include "../20allgemein/project.h"
#include "../20allgemein/ziele.h"

#include "document.h"
#include "stand_alone.h"
#include "seiten.h"
#include "viewer_ui.h"
#include "viewer_render.h"
#include "viewer_annot.h"

void viewer_springen_zu_pos_pdf(PdfViewer *pv, PdfPos pdf_pos, gdouble delta) {
	gdouble value = 0.0;
	ViewerPageNew *viewer_page = NULL;

	if (pdf_pos.seite >= pv->arr_pages->len)
		pdf_pos.seite = pv->arr_pages->len - 1;

	viewer_page = g_ptr_array_index(pv->arr_pages, pdf_pos.seite);

	//Länge aktueller Seite ermitteln
	gdouble page = viewer_page->crop.y1 - viewer_page->crop.y0;
	if (pdf_pos.index <= page)
		value = viewer_page->y_pos + pdf_pos.index * pv->zoom / 100;
	else
		value = viewer_page->y_pos + page * pv->zoom / 100;

#ifndef VIEWER
	if (pv->zond->state & GDK_MOD1_MASK) {
		gdouble page_size = gtk_adjustment_get_page_size(pv->v_adj);
		value -= page_size;
	}
#endif // VIEWER
	gtk_adjustment_set_value(pv->v_adj, (value > delta) ? value - delta : 0);

	return;
}

void viewer_get_iter_thumb(PdfViewer *pv, gint page_pv, GtkTreeIter *iter) {
	GtkTreeModel *model = NULL;
	GtkTreePath *path = NULL;

	path = gtk_tree_path_new_from_indices(page_pv, -1);
	model = gtk_tree_view_get_model(GTK_TREE_VIEW(pv->tree_thumb));
	gtk_tree_model_get_iter(model, iter, path);
	gtk_tree_path_free(path);

	return;
}

static void viewer_set_layout_size(PdfViewer* pv,
		gdouble x_max, gdouble y_pos) {
	x_max = x_max * pv->zoom / 100;

	y_pos -= PAGE_SPACE;

	gtk_layout_set_size(GTK_LAYOUT(pv->layout), (gint) (x_max + .5),
			(gint) (y_pos + .5));

	//label mit Gesamtseitenzahl erzeugen
	gchar *text = g_strdup_printf("/ %i ", pv->arr_pages->len);
	gtk_label_set_text(GTK_LABEL(pv->label_anzahl), text);
	g_free(text);

	gtk_widget_set_size_request(pv->layout, (gint) (x_max + 0.5),
			(gint) (y_pos + 0.5));

	gtk_adjustment_set_value(pv->h_adj, (x_max - VIEWER_WIDTH) / 2);

	return;
}

void viewer_refresh_layout(PdfViewer *pv, gint pos) {
	ViewerPageNew *viewer_page = NULL;
	gdouble y_pos = 0;
	gdouble x_max = 0;

	if (pv->arr_pages->len == 0)
		return;

	if (pos > 0) {
		viewer_page = g_ptr_array_index(pv->arr_pages, pos - 1);

		y_pos = viewer_page->y_pos
				+ (viewer_page->crop.y1 - viewer_page->crop.y0) * pv->zoom
						/ 100 + PAGE_SPACE;
	}

	for (gint u = pos; u < pv->arr_pages->len; u++) {
		viewer_page = g_ptr_array_index(pv->arr_pages, u);
		if ((viewer_page->crop.x1 - viewer_page->crop.x0) > x_max)
			x_max = viewer_page->crop.x1 - viewer_page->crop.x0;

		viewer_page->y_pos = (gint) (y_pos + .5);

		y_pos += (viewer_page->crop.y1 - viewer_page->crop.y0) * pv->zoom
				/ 100 + PAGE_SPACE;
	}

	viewer_set_layout_size(pv, x_max, y_pos);

	for (gint u = pos; u < pv->arr_pages->len; u++) {
		ViewerPageNew *viewer_page = NULL;

		viewer_page = g_ptr_array_index(pv->arr_pages, u);
		if (viewer_page->image_page)
			gtk_layout_move(GTK_LAYOUT(pv->layout), viewer_page->image_page,
					(gint) ((x_max - (viewer_page->crop.x1 - viewer_page->crop.x0))
					* pv->zoom / 100) / 2, viewer_page->y_pos);
	}

	return;
}

ViewerPageNew*
viewer_new_page(PdfViewer *pdfv, DisplayedDocument* dd,
		gint page_doc) {
	ViewerPageNew *viewer_page = NULL;
	PdfDocumentPage* pdf_document_page = NULL;

	pdf_document_page = zond_pdf_document_get_pdf_document_page(
			dd->zpdfd_part->zond_pdf_document, page_doc);
	if (pdf_document_page->deleted) return NULL;

	viewer_page = g_malloc0(sizeof(ViewerPageNew));

	viewer_page->pdfv = pdfv;
	viewer_page->dd = dd;
	viewer_page->pdf_document_page = pdf_document_page;
	viewer_page->crop = viewer_page->pdf_document_page->rect;

	return viewer_page;
}

static void viewer_create_layout(PdfViewer *pv) {
	DisplayedDocument *dd = pv->dd;
	gdouble x_max = 0;
	gdouble y_pos = 0;

	do {
		gint von = 0;
		gint bis = 0;

		von = dd->zpdfd_part->first_page->page_akt;
		bis = dd->zpdfd_part->last_page->page_akt;

		for (gint i = von; i <= bis; i++) {
			ViewerPageNew *viewer_page = NULL;
			GtkTreeIter iter_tmp;

			viewer_page = viewer_new_page(pv, dd, i);

			//viewer_new_page gibt NULL zurück, wenn pdf_document_page to_be_deleted ist
			if (!viewer_page) continue;

			viewer_page->y_pos = (gint) (y_pos + .5);

			viewer_page->crop.y0 =
					(i == von) ? (gfloat) dd->zpdfd_part->first_index :
							viewer_page->crop.y0;
			viewer_page->crop.y1 =
					((i == bis)
							&& (dd->zpdfd_part->last_index < EOP)) ?
							(gfloat) dd->zpdfd_part->last_index :
							viewer_page->crop.y1;

			g_ptr_array_add(pv->arr_pages, viewer_page);
			gtk_list_store_insert(
					GTK_LIST_STORE(
							gtk_tree_view_get_model( GTK_TREE_VIEW(pv->tree_thumb) )),
					&iter_tmp, -1);

			y_pos += ((viewer_page->crop.y1 - viewer_page->crop.y0) * pv->zoom
					/ 100) + PAGE_SPACE;
			if ((viewer_page->crop.x1 - viewer_page->crop.x0) > x_max)
				x_max = viewer_page->crop.x1 - viewer_page->crop.x0;
		}

	} while ((dd = dd->next));

	viewer_set_layout_size(pv, x_max, y_pos);

	return;
}

gboolean viewer_has_dirty_dd(PdfViewer* pv) {
	DisplayedDocument* dd = NULL;

	dd = pv->dd;
	do {
		if (dd->zpdfd_part->dirty)
			return TRUE;
	} while ((dd = dd->next));

	return FALSE;
}

typedef struct {
	PdfViewer *pv;
	PdfPos pdf_pos;
	gdouble delta;
} SSpringArgs;

static gboolean viewer_springen_idle(gpointer data) {
	SSpringArgs *args = (SSpringArgs*) data;
	viewer_springen_zu_pos_pdf(args->pv, args->pdf_pos, args->delta);
	g_free(args);
	return G_SOURCE_REMOVE;
}

void viewer_springen_zu_pos_pdf_idle(PdfViewer *pv, PdfPos pdf_pos,
		gdouble delta) {
	SSpringArgs *args = g_new0(SSpringArgs, 1);
	args->pv = pv;
	args->pdf_pos = pdf_pos;
	args->delta = delta;
	g_idle_add(viewer_springen_idle, args);

	return;
}

void viewer_display_document(PdfViewer *pv, DisplayedDocument *dd, gint page,
		gint index) {
	PdfPos pdf_pos = { page, index };

	pv->dd = dd;

	viewer_create_layout(pv);

	if (page || index)
		viewer_springen_zu_pos_pdf_idle(pv, pdf_pos, 0.0);
	else
		g_signal_emit_by_name(pv->v_adj, "value-changed", NULL);

	//Test, ob in Viewer "schmutzige" dds angezeigt werden sollen - dann speichern-icon aktiv
	if (viewer_has_dirty_dd(pv))
		gtk_widget_set_sensitive(pv->button_speichern, TRUE);

	gtk_widget_grab_focus(pv->layout);

	return;
}

void viewer_schliessen(PdfViewer *pv) {
	viewer_close_thread_pool_and_transfer(pv); //..._and_transfer, damit etwaig noch gerenderte GdkPixbufs verarztet werden
	g_idle_remove_by_data(pv);

	g_array_unref(pv->arr_rendered);
	g_mutex_clear(&pv->mutex_arr_rendered);

	g_ptr_array_unref(pv->arr_pages); //vor gtk_widget_destroy(vf), weil freeFunc gesetzt Nein! stimmt nicht! Keine free-func
	g_array_unref(pv->text_occ.arr_quad);

	gtk_widget_destroy(pv->vf);

	document_free_displayed_documents(pv->dd);

	//pv aus Liste der geöffneten pvs entfernen
	g_ptr_array_remove_fast(pv->zond->arr_pv, pv);

	g_free(pv);

	return;
}

/*  punkt:      Koordinate im Layout (ScrolledWindow)
 pdf_punkt:  hier wird Ergebnis abgelegt
 gint:       0 wenn Punkt auf Seite liegt; -1 wenn außerhalb

 Wenn Punkt im Zwischenraum zwischen zwei Seiten oder unterhalb der letzten
 Seite liegt, wird pdf_punkt.seite die davorliegende Seite und
 pdf_punkt.punkt.y = EOP.
 Wenn punkt links oder rechts daneben liegt, ist pdf_punkt.punkt.x negativ
 oder größer als Seitenbreite
 */
gint viewer_abfragen_pdf_punkt(PdfViewer *pv, fz_point punkt,
		PdfPunkt *pdf_punkt) {
	gint ret = 0;
	gdouble v_oben = 0.0;
	gdouble v_unten = 0.0;
	ViewerPageNew *viewer_page = NULL;
	gint width = 0;
	gint x = 0;

	gint i = 0;

	if (punkt.y < 0) {
		pdf_punkt->seite = 0;
		pdf_punkt->punkt.y = 0;
		ret = -1;

		viewer_page = g_ptr_array_index(pv->arr_pages, 0);
	} else {
		for (i = 0; i < pv->arr_pages->len; i++) {
			viewer_page = g_ptr_array_index(pv->arr_pages, i);

			pdf_punkt->delta_y = viewer_page->crop.y0;

			v_unten = v_oben
					+ (viewer_page->crop.y1 - viewer_page->crop.y0)
							* pv->zoom / 100;

			if (punkt.y >= v_oben && punkt.y <= v_unten) {
				pdf_punkt->seite = i;
				pdf_punkt->punkt.y = (punkt.y - v_oben) / pv->zoom * 100
						+ viewer_page->crop.y0;

				break;
			} else if (punkt.y < v_unten) {
				pdf_punkt->seite = i - 1;
				pdf_punkt->punkt.y = EOP;
				ret = -1;

				break;
			}

			v_oben = v_unten + PAGE_SPACE;
		}

		if (i == pv->arr_pages->len) {
			pdf_punkt->seite = i - 1;
			pdf_punkt->punkt.y = EOP;
			ret = -1;
		}
	}

	gtk_widget_get_size_request(pv->layout, &width, NULL);
	x = (gint) (width - (viewer_page->crop.x1 - viewer_page->crop.x0)
			* pv->zoom / 100) / 2;

	if (punkt.x < x)
		ret = -1;

	if (punkt.x
			> (((viewer_page->crop.x1 - viewer_page->crop.x0) * pv->zoom
					/ 100) + x))
		ret = -1;

	pdf_punkt->punkt.x = (punkt.x - x) / pv->zoom * 100;

	return ret;
}

void viewer_handle_page_entry_activated(PdfViewer* pv, GtkEntry *entry) {
	guint page_num = 0;
	gint rc = 0;
	gint erste = 0;
	gint letzte = 0;

	const gchar *text_entry = gtk_entry_get_text(entry);

	rc = string_to_guint(text_entry, &page_num);
	if (rc || (page_num < 1) || (page_num > pv->arr_pages->len)) {
		viewer_render_get_visible_pages(pv, &erste, &letzte);
		gchar *text = NULL;
		text = g_strdup_printf("%i-%i", erste + 1, letzte + 1);
		gtk_entry_set_text(entry, (const gchar*) text);
		g_free(text);
	} else {
		ViewerPageNew *viewer_page = NULL;

		viewer_page = g_ptr_array_index(pv->arr_pages, page_num - 1);
		gtk_adjustment_set_value(pv->v_adj, viewer_page->y_pos);
	}

	gtk_widget_grab_focus(pv->layout);

	return;
}

static gint viewer_on_text(PdfViewer *pv, ViewerPageNew *viewer_page,
		fz_point punkt) {
	for (fz_stext_block *block =
			viewer_page->pdf_document_page->stext_page->first_block; block;
			block = block->next) {
		if (block->type != FZ_STEXT_BLOCK_TEXT)
			continue;

		for (fz_stext_line *line = block->u.t.first_line; line;
				line = line->next) {
			fz_rect box = line->bbox;
			if (punkt.x >= box.x0 && punkt.x <= box.x1 && punkt.y >= box.y0
					&& punkt.y <= box.y1) {
				gboolean quer = FALSE;
				gint rotate = 0;

				rotate = viewer_page->pdf_document_page->rotate;

				if (rotate == 90 || rotate == 180)
					quer = TRUE;

				if (line->wmode == 0 && !quer)
					return 1;
				else if (line->wmode == 1 && quer)
					return 1;
				else if (line->wmode == 0 && quer)
					return 2;
				if (line->wmode == 1 && !quer)
					return 2;
			}
		}
	}

	return 0;
}

static gboolean inside_quad(fz_quad quad, fz_point punkt) {
	fz_rect rect = fz_rect_from_quad(quad);

	return fz_is_point_inside_rect(punkt, rect);
}

static PdfDocumentPageAnnot*
viewer_on_annot(PdfViewer *pv, ViewerPageNew *viewer_page, fz_point point) {
	for (gint i = 0; i < viewer_page->pdf_document_page->arr_annots->len;
			i++) {
		PdfDocumentPageAnnot *pdf_document_page_annot = NULL;
		pdf_document_page_annot = g_ptr_array_index(
				viewer_page->pdf_document_page->arr_annots, i);

		if (pdf_document_page_annot->deleted)
			continue;

		if (pdf_document_page_annot->annot.type == PDF_ANNOT_HIGHLIGHT
				|| pdf_document_page_annot->annot.type == PDF_ANNOT_UNDERLINE
				|| pdf_document_page_annot->annot.type == PDF_ANNOT_STRIKE_OUT
				|| pdf_document_page_annot->annot.type == PDF_ANNOT_SQUIGGLY) {
			for (gint u = 0; u < pdf_document_page_annot->annot.annot_text_markup.arr_quads->len;
					u++) {
				fz_quad quad =
						g_array_index(
								pdf_document_page_annot->annot.annot_text_markup.arr_quads,
								fz_quad, u);
				if (inside_quad(quad, point))
					return pdf_document_page_annot;
			}
		} else if (pdf_document_page_annot->annot.type == PDF_ANNOT_TEXT) {
			if (fz_is_point_inside_rect(point,
					pdf_document_page_annot->annot.annot_text.rect))
				return pdf_document_page_annot;
		}
	}

	return NULL;
}

void viewer_set_cursor(PdfViewer *pv, gint rc,
		ViewerPageNew *viewer_page,
		PdfDocumentPageAnnot *pdf_document_page_annot, PdfPunkt pdf_punkt) {
	gint on_text = 0;

	if (rc)
		gdk_window_set_cursor(pv->gdk_window, pv->cursor_default);
	else if (pdf_document_page_annot)
		gdk_window_set_cursor(pv->gdk_window, pv->cursor_annot);
	else if ((viewer_page->pdf_document_page->thread & 8) && (on_text =
			viewer_on_text(pv, viewer_page, pdf_punkt.punkt))) {
		if (on_text == 1)
			gdk_window_set_cursor(pv->gdk_window, pv->cursor_text);
		else if (on_text == 2)
			gdk_window_set_cursor(pv->gdk_window, pv->cursor_vtext);
	} else
		gdk_window_set_cursor(pv->gdk_window, pv->cursor_default);

	return;
}

void viewer_foreach(PdfViewer *pdfv, PdfDocumentPage *pdf_document_page,
		gint (*cb_foreach_pv)(PdfViewer*, ViewerPageNew*, gint,
				gpointer), gpointer data) {
	for (gint p = 0; p < pdfv->zond->arr_pv->len; p++) {
		gboolean dirty = FALSE;

		PdfViewer *pv_vergleich = g_ptr_array_index(pdfv->zond->arr_pv, p);

		for (gint i = 0; i < pv_vergleich->arr_pages->len; i++) {
			ViewerPageNew* viewer_page = NULL;
			DisplayedDocument* dd = NULL;

			viewer_page = g_ptr_array_index(pv_vergleich->arr_pages, i);
			dd = viewer_page->dd;

			if (pdf_document_page == viewer_page->pdf_document_page) {
				if (cb_foreach_pv) {
					gint rc = 0;

					rc = cb_foreach_pv(pv_vergleich, viewer_page, i, data);
					if (rc) dirty = TRUE;
				} else dirty = TRUE;

				if (dirty) //auch dd auf dirty setzen
					dd->zpdfd_part->dirty = TRUE;

				break;
			}
		}

		if (dirty) //gilt für's pv
			gtk_widget_set_sensitive(pv_vergleich->button_speichern, TRUE);
	}

	return;
}

void viewer_handle_layout_motion_notify(PdfViewer* pv, GdkEvent *event) {
	gint rc = 0;
	PdfPunkt pdf_punkt = { 0 };
	ViewerPageNew *viewer_page = NULL;

	rc = viewer_abfragen_pdf_punkt(pv,
			fz_make_point(event->motion.x, event->motion.y), &pdf_punkt);

	viewer_page = g_ptr_array_index(pv->arr_pages, pdf_punkt.seite);
	viewer_render_wait_for_transfer(viewer_page->pdf_document_page);

	//Text erfassen, wenn linker button gehalten
	if (event->motion.state == GDK_BUTTON1_MASK) {
		if (pv->click_on_text && !pv->clicked_annot) {
			PdfPunkt von = { 0 };
			PdfPunkt bis = { 0 };
			gint zaehler = 0;
			fz_context *ctx = NULL;
			gint n = 0;
			fz_point point_start = { 0, };
			fz_point point_end = { 0, };

			if (pv->click_pdf_punkt.seite < pdf_punkt.seite) {
				von = pv->click_pdf_punkt;
				bis = pdf_punkt;
			} else if (pv->click_pdf_punkt.seite > pdf_punkt.seite) {
				von = pdf_punkt;
				bis = pv->click_pdf_punkt;
			} else //gleiche Seite
			{
				if (pv->click_pdf_punkt.punkt.y < pdf_punkt.punkt.y) {
					von = pv->click_pdf_punkt;
					bis = pdf_punkt;
				} else if (pv->click_pdf_punkt.punkt.y
						> pdf_punkt.punkt.y) {
					von = pdf_punkt;
					bis = pv->click_pdf_punkt;
				} else //gleiche Höhe
				{
					if (pv->click_pdf_punkt.punkt.x < pdf_punkt.punkt.x) {
						von = pv->click_pdf_punkt;
						bis = pdf_punkt;
					} else if (pv->click_pdf_punkt.punkt.x
							>= pdf_punkt.punkt.x) {
						von = pdf_punkt;
						bis = pv->click_pdf_punkt;
					}
				}
			}

			for (gint page = von.seite; page <= bis.seite; page++) {
				ViewerPageNew *viewer_page_loop = NULL;

				if (page == pdf_punkt.seite)
					viewer_page_loop = viewer_page;
				else {
					viewer_page_loop = g_ptr_array_index(pv->arr_pages,
							page);
					viewer_render_wait_for_transfer(viewer_page_loop->pdf_document_page);
				}

				//when stext_page nicht gerendert, dann weiter
				if (!(viewer_page_loop->pdf_document_page->thread & 8))
					continue;

				if (page == von.seite) {
					point_start = von.punkt;
					if (page == bis.seite)
						point_end = bis.punkt;
					else
						point_end = fz_make_point(viewer_page_loop->crop.x1,
								viewer_page_loop->crop.y1);
				} else if (page == bis.seite) { //Der Fall, daß page == von.seite, ist schon abgefrühstückt
					point_start = fz_make_point(0, 0);
					point_end = bis.punkt;
				} else //dazwischen
				{
					point_start = fz_make_point(0, 0);
					point_end = fz_make_point(viewer_page_loop->crop.x1,
							viewer_page_loop->crop.y1);
				}

				ctx = zond_pdf_document_get_ctx(
						viewer_page_loop->pdf_document_page->document);

				n = fz_highlight_selection(ctx,
						viewer_page_loop->pdf_document_page->stext_page,
						point_start, point_end,
						&pv->highlight.quad[zaehler], 999 - zaehler);

				for (gint u = 0; u < n; u++)
					pv->highlight.page[u + zaehler] = page;

				zaehler += n;

				pv->highlight.page[zaehler] = -1;

				gtk_widget_queue_draw(viewer_page_loop->image_page);
			}

			//Wenn Maus ruckartig über Seitengrenzen bewegt wird
			// werden alte Markierungen manchmal nicht mitgenommen
			for (gint range_old = pv->von_alt; range_old <= pv->bis_alt;
					range_old++) {
				if (range_old < von.seite || range_old > bis.seite) {
					ViewerPageNew *viewer_page_old_range = NULL;

					viewer_page_old_range = g_ptr_array_index(pv->arr_pages,
							range_old);
					viewer_render_wait_for_transfer(viewer_page_old_range->pdf_document_page);

					if (!(viewer_page->thread & 2))
						continue;

					gtk_widget_queue_draw(
							viewer_page_old_range->image_page);
				}
			}
			pv->von_alt = von.seite;
			pv->bis_alt = bis.seite;
		} else if (pv->clicked_annot
				&& pv->clicked_annot->annot.type == PDF_ANNOT_TEXT) {
//            if ( rc || pdf_punkt.seite != pv->click_pdf_punkt.seite ) return TRUE;

			if (!(viewer_page->thread & 2))
				return;

			gtk_popover_popdown(GTK_POPOVER(pv->annot_pop));

			pv->clicked_annot->annot.annot_text.rect.x0 -= (pv->x
					- event->motion.x_root) / pv->zoom * 100;
			pv->clicked_annot->annot.annot_text.rect.x1 -= (pv->x
					- event->motion.x_root) / pv->zoom * 100;
			pv->clicked_annot->annot.annot_text.rect.y0 -= (pv->y
					- event->motion.y_root) / pv->zoom * 100;
			pv->clicked_annot->annot.annot_text.rect.y1 -= (pv->y
					- event->motion.y_root) / pv->zoom * 100;

			gtk_widget_queue_draw(viewer_page->image_page);
		}
		else { //nicht auf Text und nicht auf Text-annot
		//layout wird mit Mauszeiger geschoben
			gdouble y = gtk_adjustment_get_value(pv->v_adj);
			gdouble x = gtk_adjustment_get_value(pv->h_adj);
			gtk_adjustment_set_value(pv->v_adj,
					y + pv->y - event->motion.y_root);
			gtk_adjustment_set_value(pv->h_adj,
					x + pv->x - event->motion.x_root);
		}
		pv->y = event->motion.y_root;
		pv->x = event->motion.x_root;
	}
	//kein Button, Mauszeiger wird über annot bewegt
	else {
		PdfDocumentPageAnnot *pdf_document_page_annot = NULL;

		if ((viewer_page->pdf_document_page->thread & 2)
				&& (pdf_document_page_annot = viewer_on_annot(pv,
						viewer_page, pdf_punkt.punkt))) {
			//Popover anzeigen, falls /Contents text enthält
			if (pdf_document_page_annot->annot.type == PDF_ANNOT_TEXT
					&& pdf_document_page_annot->annot.annot_text.content && //Inhalt?
					g_strcmp0(pdf_document_page_annot->annot.annot_text.content, "") && //gefüllt?
					!(pdf_document_page_annot == pv->clicked_annot && //nicht angeklickt und...
					gtk_widget_is_visible(pv->annot_pop_edit))) {//...geöffnet
				GdkRectangle gdk_rectangle = { 0, };
				gint x = 0, y = 0, width = 0, height = 0;

				gtk_container_child_get(GTK_CONTAINER(pv->layout),
						viewer_page->image_page, "y", &y, NULL);
				y += (gint) (pdf_document_page_annot->annot.annot_text.rect.y0
						* pv->zoom / 100);
				y -= gtk_adjustment_get_value(pv->v_adj);

				gtk_container_child_get(GTK_CONTAINER(pv->layout),
						viewer_page->image_page, "x", &x, NULL);
				x += (gint) (pdf_document_page_annot->annot.annot_text.rect.x0
						* pv->zoom / 100);
				x -= gtk_adjustment_get_value(pv->h_adj);

				height =
						(gint) ((pdf_document_page_annot->annot.annot_text.rect.y1
								- pdf_document_page_annot->annot.annot_text.rect.y0)
								* pv->zoom / 100);
				width = (gint) ((pdf_document_page_annot->annot.annot_text.rect.x1
						- pdf_document_page_annot->annot.annot_text.rect.x0)
						* pv->zoom / 100);

				gdk_rectangle.x = x;
				gdk_rectangle.y = y;
				gdk_rectangle.width = width;
				gdk_rectangle.height = height;

				gtk_popover_set_pointing_to(GTK_POPOVER(pv->annot_pop),
						&gdk_rectangle);
				gtk_label_set_text(GTK_LABEL(pv->annot_label),
						pdf_document_page_annot->annot.annot_text.content);
				gtk_popover_popup(GTK_POPOVER(pv->annot_pop));
			}
		} else
			gtk_popover_popdown(GTK_POPOVER(pv->annot_pop));

		viewer_set_cursor(pv, rc, viewer_page, pdf_document_page_annot,
				pdf_punkt); //Kein Knopf gedrückt
	}

	return;
}

gint viewer_handle_button_press(PdfViewer* pv,
		GdkEvent *event, GError** error) {
	gint rc = 0;
	PdfPunkt pdf_punkt = { 0 };

	rc = viewer_abfragen_pdf_punkt(pv,
			fz_make_point(event->button.x, event->button.y), &pdf_punkt);
	if (rc) //daneben!
		return 0;

//Einzelklick
	if (event->button.type == GDK_BUTTON_PRESS
			&& event->button.button == 1) {
		ViewerPageNew *viewer_page = NULL;

		pv->click_pdf_punkt = pdf_punkt;
		pv->highlight.page[0] = -1;
		pv->text_occ.index_act = -1;

		pv->y = event->button.y_root;
		pv->x = event->button.x_root;

		pv->von_alt = pdf_punkt.seite;
		pv->von_alt = pdf_punkt.seite;

		gtk_widget_set_sensitive(pv->item_copy, FALSE);

		viewer_page = g_ptr_array_index(pv->arr_pages, pdf_punkt.seite);
		viewer_render_wait_for_transfer(viewer_page->pdf_document_page);

		PdfDocumentPageAnnot *pdf_document_page_annot = NULL;

		if ((pdf_document_page_annot = viewer_on_annot(pv,
				viewer_page, pdf_punkt.punkt))) {
			if (pv->clicked_annot
					&& pv->clicked_annot->annot.type == PDF_ANNOT_TEXT) {
				if (pdf_document_page_annot != pv->clicked_annot)
					pv->clicked_annot->annot.annot_text.open = FALSE;
				else
					pv->clicked_annot->annot.annot_text.open = TRUE;
			}

			pv->clicked_annot = pdf_document_page_annot;
		}
		else { //nicht auf annot geklickt, z.B. weil neben layout geclickt
			//wird weiter unten geprüft, ob click_on_text wieder angeschaltet werden soll
			pv->click_on_text = FALSE;

			if (pv->clicked_annot
					&& pv->clicked_annot->annot.type == PDF_ANNOT_TEXT)
				pv->clicked_annot->annot.annot_text.open = FALSE;
			pv->clicked_annot = NULL;

			if ((viewer_page->pdf_document_page->thread & 2)
					&& pv->state == 3) { //Neue AnnotText einfügen
				gint rc = 0;

				rc = viewer_annot_create(viewer_page, error);
				if (rc)
					return -1;

				//neu erzeugte Text-Annot soll markiert sein!
				pv->clicked_annot = g_ptr_array_index(
						viewer_page->pdf_document_page->arr_annots,
						viewer_page->pdf_document_page->arr_annots->len
								- 1);
				pv->clicked_annot->annot.annot_text.open = TRUE;

				//Nach Einfügen von annot-text: auf Zeiger zurückflitschen
				gtk_toggle_button_set_active(
						GTK_TOGGLE_BUTTON(pv->button_zeiger), TRUE);
			} else if ((viewer_page->pdf_document_page->thread & 4)
					&& viewer_on_text(pv, viewer_page, pdf_punkt.punkt))
				pv->click_on_text = TRUE;
			else
				gdk_window_set_cursor(pv->gdk_window, pv->cursor_grab);
		}

		gtk_widget_queue_draw(pv->layout); //um ggf. Markierung der annot zu löschen
	}
#ifndef VIEWER
//Doppelklick - nur für Anbindung interessant
	else if (event->button.type == GDK_2BUTTON_PRESS
			&& event->button.button == 1) {
		gboolean punktgenau = FALSE;

		if (event->button.state == GDK_SHIFT_MASK)
			punktgenau = TRUE;

		if (!rc) {
			ViewerPageNew* viewer_page = NULL;
			gint page_pdf = 0;

			viewer_page = g_ptr_array_index(pv->arr_pages, pdf_punkt.seite);
			page_pdf = viewer_page->pdf_document_page->page_akt;

			//Test, ob Seite in Dokument frisch eingefügt
			if (viewer_page->pdf_document_page->inserted) {
				g_set_error(error, VIEWER_ERROR, 0,
						"Die gewählte Seite wurde in das Dokument eingefügt,\n"
								"Dokument ist aber noch nicht gespeichert.\n\n"
								"Um Inkonsistenzen zu vermeiden, speichern Sie "
								"das Dokument zunächst.");

				return -1;
			}

			if (pv->anbindung.von.index == -1) {
				pv->anbindung.von.seite = page_pdf;
				if (punktgenau)
					pv->anbindung.von.index = pdf_punkt.punkt.y;
				else
					pv->anbindung.von.index = 0;

				//Wahl des Beginns irgendwie anzeigen
				gchar *button_label_text = g_strdup_printf(
						"Anbindung Anfang löschen\nSeite: %i, Index: %i",
						pv->anbindung.von.seite, pv->anbindung.von.index);
				gtk_widget_set_tooltip_text(pv->button_anbindung,
						button_label_text);
				gtk_widget_set_sensitive(pv->button_anbindung, TRUE);

				g_free(button_label_text);
			}
			else { //zweiter Doppelklick - pv->anbindung "gefüllt"
				//Wenn nicht zurückliegende Seite oder - wenn punktgenau - gleiche
				//Seite und zurückliegender Index
				//"richtige" Reihenfolge

				if ((page_pdf >= pv->anbindung.von.seite)
						|| ((punktgenau)
								&& (page_pdf == pv->anbindung.von.seite)
								&& (pdf_punkt.punkt.y >= pv->anbindung.von.index))) {
					pv->anbindung.bis.seite = page_pdf;
					if (punktgenau)
						pv->anbindung.bis.index = pdf_punkt.punkt.y;
					else
						pv->anbindung.bis.index = EOP;
				}
				else //umdrehen
				{
					pv->anbindung.bis.seite = pv->anbindung.von.seite;
					if (pv->anbindung.von.index == 0)
						pv->anbindung.bis.index = EOP;
					else
						pv->anbindung.bis.index = pv->anbindung.von.index;

					pv->anbindung.von.seite = page_pdf;
					if (punktgenau)
						pv->anbindung.von.index = pdf_punkt.punkt.y;
					else
						pv->anbindung.von.index = 0;
				}

				rc = zond_anbindung_erzeugen(pv, error);
				if (rc)
					return -1;

				gtk_window_present(GTK_WINDOW(pv->zond->app_window));

				//anbindung.von "löschen"
				pv->anbindung.von.index = -1;

				//Anzeige Beginn rückgängig machen
				gtk_widget_set_tooltip_text(pv->button_anbindung,
						"Anbindung Anfang löschen");
				gtk_widget_set_sensitive(pv->button_anbindung, FALSE);
			}
		}
	}
#endif

	return 0;
}

