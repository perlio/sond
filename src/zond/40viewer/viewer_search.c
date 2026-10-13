/*
 zond (viewer_search.c) - Akten, Beweisstücke, Unterlagen
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

//Textsuche im angezeigten Dokument (Vor/Zurück über das Such-Entry) und
//Anspringen/Markieren einer über Zeichenposition adressierten Fundstelle
//(Index-Suche, zond_indexsuche.c)

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

//Position des aktuellen Treffers neben dem Such-Entry; wrapped: Suche ist am
//Dokumentende/-anfang umgebrochen
static void viewer_text_occ_update_label(PdfViewer *pv, gboolean wrapped) {
	gchar *text = g_strdup_printf("%sS. %i · %i/%u",
			wrapped ? "↻ " : "", pv->text_occ.page_act + 1,
			pv->text_occ.index_act + 1, pv->text_occ.arr_hit->len);

	gtk_label_set_text(GTK_LABEL(pv->label_search), text);
	g_free(text);

	return;
}

static void viewer_anzeigen_text_occ(PdfViewer *pv, gboolean wrapped) {
	PdfPos pdf_pos = { 0 };
	fz_quad quad = { 0 };
	TextHit hit = g_array_index(pv->text_occ.arr_hit, TextHit,
			pv->text_occ.index_act);
	guint n = MIN(hit.n, 999);

	//alle Quads des Treffers (mehrzeilig bei Silbentrennung)
	for (guint i = 0; i < n; i++) {
		pv->highlight.quad[i] = g_array_index(pv->text_occ.arr_quad, fz_quad,
				hit.first + i);
		pv->highlight.page[i] = pv->text_occ.page_act;
	}
	pv->highlight.page[n] = -1;

	viewer_text_occ_update_label(pv, wrapped);

	quad = pv->highlight.quad[0];
	pdf_pos.seite = pv->text_occ.page_act;
	pdf_pos.index = (gint) quad.ul.y;

	viewer_springen_zu_pos_pdf(pv, pdf_pos, 40);
	gtk_widget_queue_draw(pv->layout); //für den Fall, daß auf gleicher Höhe - dann zeichnet viewer_spring... nicht neu

	return;
}

//Array von GArray* (je Treffer ein Array von fz_quad)
typedef struct {
	GArray *hits;
} SearchHitData;

static gint search_hit_cb(fz_context *ctx, void *opaque,
		gint num_quads, fz_quad *quads) {
	SearchHitData *d = (SearchHitData *) opaque;
	GArray *hit = g_array_new(FALSE, FALSE, sizeof(fz_quad));
	g_array_append_vals(hit, quads, (guint) num_quads);
	g_array_append_val(d->hits, hit);
	return 0; //weitersuchen
}

void viewer_highlight_at_char_pos(PdfViewer *pv, gint page_nr,
		gint char_pos_in_page, gchar const *term) {
	ViewerPageNew *viewer_page = NULL;
	gint page_idx = -1;

	if (!term || !*term)
		return;

	//ViewerPage zu page_nr (page_akt) suchen
	for (guint i = 0; i < pv->arr_pages->len; i++) {
		ViewerPageNew *vp = g_ptr_array_index(pv->arr_pages, i);
		if (vp->pdf_document_page->page_akt == page_nr) {
			viewer_page = vp;
			page_idx = (gint) i;
			break;
		}
	}

	if (!viewer_page || page_idx < 0)
		return;

	fz_context *ctx = zond_pdf_document_get_ctx(
			viewer_page->dd->zpdfd_part->zond_pdf_document);

	//stext_page synchron sicherstellen: beim erstmaligen Öffnen ist der
	//Render-Task oft noch nicht gestartet, und wait_for_transfer wartet nur
	//auf einen bereits laufenden
	{
		GError *error = NULL;
		gint rc = 0;

		rc = viewer_render_stext_page_fast(ctx,
				viewer_page->pdf_document_page, &error);
		if (rc) {
			g_clear_error(&error);
			return;
		}
	}

	fz_stext_page *stext = viewer_page->pdf_document_page->stext_page;

	//alle Treffer des Terms auf der Seite sammeln (ohne Groß-/Kleinschreibung)
	SearchHitData hit_data;
	hit_data.hits = g_array_new(FALSE, TRUE, sizeof(GArray *));

	fz_try(ctx) {
		fz_search_stext_page_cb(ctx, stext, term,
				(fz_search_callback_fn *) search_hit_cb, &hit_data);
	}
	fz_catch(ctx) {
		LOG_WARN("Suche in Seite %i fehlgeschlagen: %s", page_nr,
				fz_caught_message(ctx));
	}

	if (hit_data.hits->len == 0) {
		g_array_free(hit_data.hits, TRUE);
		return;
	}

	//Treffer über char_pos_in_page bestimmen: map des Flat-Texts liefert das
	//Zeichen am Offset, gewählt wird der Treffer, der es enthält
	GArray *chosen_hit = NULL;

	fz_stext_position *map = NULL;
	fz_buffer *flat_buf = NULL;

	fz_try(ctx) {
		flat_buf = fz_new_buffer_from_flattened_stext_page(
				ctx, stext, FZ_TEXT_FLATTEN_ALL, &map);
	}
	fz_catch(ctx) {
		flat_buf = NULL;
		map = NULL;
	}

	if (map && flat_buf) {
		unsigned char *flat_data = NULL;
		//Länge aus fz_buffer_storage(), nicht strlen(): Puffer ist nicht
		//terminiert und kann NUL-Bytes enthalten (Glyphen ohne Unicode)
		gsize flat_len = fz_buffer_storage(ctx, flat_buf, &flat_data);
		if (flat_data) {
			gint safe_pos = (char_pos_in_page < (gint) flat_len)
					? char_pos_in_page : (gint) flat_len - 1;

			//Offset nicht mitten in einem UTF-8-Zeichen
			while (safe_pos > 0 && (flat_data[safe_pos] & 0xC0) == 0x80)
				safe_pos--;

			if (safe_pos >= 0) {
				gsize char_idx = fz_runeidx((gchar *) flat_data,
						(gchar *) flat_data + safe_pos);
				fz_stext_char *target_ch = map[char_idx].ch;

				if (target_ch) {
					//Mittelpunkt des Zeichens in einem Quad des Treffers?
					//(je Quad, nicht Vereinigungsbox: Treffer können mehrzeilig sein)
					fz_quad cq = target_ch->quad;
					fz_point center = { (cq.ul.x + cq.lr.x) / 2,
							(cq.ul.y + cq.lr.y) / 2 };

					for (guint h = 0; h < hit_data.hits->len && !chosen_hit; h++) {
						GArray *hit = g_array_index(hit_data.hits, GArray *, h);

						for (guint q = 0; q < hit->len; q++)
							if (fz_is_point_inside_quad(center,
									g_array_index(hit, fz_quad, q))) {
								chosen_hit = hit;
								break;
							}
					}

					//Fallback: Treffer mit nächstgelegenem Quad
					if (!chosen_hit) {
						float best_dist = 1e30f;

						for (guint h = 0; h < hit_data.hits->len; h++) {
							GArray *hit = g_array_index(hit_data.hits, GArray *, h);

							for (guint q = 0; q < hit->len; q++) {
								fz_quad hq = g_array_index(hit, fz_quad, q);
								float dx = hq.ll.x - target_ch->origin.x;
								float dy = hq.ll.y - target_ch->origin.y;
								float dist = dx * dx + dy * dy;

								if (dist < best_dist) {
									best_dist = dist;
									chosen_hit = hit;
								}
							}
						}
					}
				}
			}
		}
	}

	if (flat_buf) fz_drop_buffer(ctx, flat_buf);
	if (map) fz_free(ctx, map); //map ist separat alloziert

	//Fallback, falls die map-Methode nichts ergab: ersten Treffer nehmen
	if (!chosen_hit && hit_data.hits->len > 0)
		chosen_hit = g_array_index(hit_data.hits, GArray *, 0);

	if (!chosen_hit) {
		for (guint h = 0; h < hit_data.hits->len; h++)
			g_array_free(g_array_index(hit_data.hits, GArray *, h), TRUE);
		g_array_free(hit_data.hits, TRUE);
		return;
	}

	//erste Quad (unbeschnitten) für den Sprung merken
	fz_quad first_quad = g_array_index(chosen_hit, fz_quad, 0);

	//Quads des Treffers beschnitten in pv->highlight schreiben
	pv->highlight.page[0] = -1;
	gint n_quads = 0;
	for (guint q = 0; q < chosen_hit->len && n_quads < 999; q++) {
		fz_quad quad    = g_array_index(chosen_hit, fz_quad, q);
		fz_rect r       = fz_rect_from_quad(quad);
		fz_rect cropped = fz_intersect_rect(viewer_page->crop, r);
		if (!fz_is_empty_rect(cropped)) {
			cropped = fz_translate_rect(cropped,
					-viewer_page->crop.x0, -viewer_page->crop.y0);
			pv->highlight.quad[n_quads] = fz_quad_from_rect(cropped);
			pv->highlight.page[n_quads] = page_idx;
			n_quads++;
		}
	}
	pv->highlight.page[n_quads] = -1;

	for (guint h = 0; h < hit_data.hits->len; h++)
		g_array_free(g_array_index(hit_data.hits, GArray *, h), TRUE);
	g_array_free(hit_data.hits, TRUE);

	if (n_quads > 0) {
		//als Idle-Sprung: viewer_display_document() plant beim ersten Öffnen
		//schon einen zum Seitenanfang ein, der sonst unseren überschriebe
		PdfPos pdf_pos = { 0 };

		pdf_pos.seite = page_idx;
		pdf_pos.index = (gint) first_quad.ul.y;
		viewer_springen_zu_pos_pdf_idle(pv, pdf_pos, 40.0);

		gtk_widget_queue_draw(pv->layout);
	}
}

//Hilfsstruct für den Seitentext-Suchlauf in viewer_handle_text_search()
typedef struct {
	PdfViewer *pv;
	ViewerPageNew *viewer_page;
	guint n_dedup; //Treffer aus dem ersten Lauf, gegen die geprüft wird
} PageSearchData;

//Gleicher Trefferanfang wie einer der ersten n_dedup Treffer?
static gboolean page_hit_known(PdfViewer *pv, guint first, guint n_dedup) {
	fz_quad q = g_array_index(pv->text_occ.arr_quad, fz_quad, first);

	for (guint i = 0; i < n_dedup; i++) {
		TextHit hit = g_array_index(pv->text_occ.arr_hit, TextHit, i);
		fz_quad k = g_array_index(pv->text_occ.arr_quad, fz_quad, hit.first);

		if (fabsf(q.ul.x - k.ul.x) < 0.5f && fabsf(q.ul.y - k.ul.y) < 0.5f)
			return TRUE;
	}

	return FALSE;
}

//Übernimmt einen Treffer (alle Quads) beschnitten auf den sichtbaren Teil
static gint page_hit_cb(fz_context *ctx, void *opaque, gint num_quads,
		fz_quad *quads, gint chapter, gint page) {
	PageSearchData *d = (PageSearchData *) opaque;
	GArray *arr_quad = d->pv->text_occ.arr_quad;
	TextHit hit = { arr_quad->len, 0 };

	for (gint i = 0; i < num_quads; i++) {
		fz_rect cropped = fz_intersect_rect(d->viewer_page->crop,
				fz_rect_from_quad(quads[i]));

		if (fz_is_empty_rect(cropped))
			continue;

		cropped = fz_translate_rect(cropped, -d->viewer_page->crop.x0,
				-d->viewer_page->crop.y0);
		fz_quad quad = fz_quad_from_rect(cropped);
		g_array_append_val(arr_quad, quad);
		hit.n++;
	}

	if (hit.n == 0)
		return 0;

	if (d->n_dedup > 0 && page_hit_known(d->pv, hit.first, d->n_dedup))
		g_array_set_size(arr_quad, hit.first);
	else
		g_array_append_val(d->pv->text_occ.arr_hit, hit);

	return 0; //weitersuchen
}

static gint page_hit_compare(gconstpointer a, gconstpointer b,
		gpointer data) {
	PdfViewer *pv = (PdfViewer *) data;
	fz_quad qa = g_array_index(pv->text_occ.arr_quad, fz_quad,
			((const TextHit *) a)->first);
	fz_quad qb = g_array_index(pv->text_occ.arr_quad, fz_quad,
			((const TextHit *) b)->first);

	if (qa.ul.y != qb.ul.y)
		return (qa.ul.y > qb.ul.y) - (qa.ul.y < qb.ul.y);

	return (qa.ul.x > qb.ul.x) - (qa.ul.x < qb.ul.x);
}

static gboolean needle_has_hyphen(gchar const *needle) {
	return strchr(needle, '-') || strstr(needle, "\u2010")
			|| strstr(needle, "\u2011");
}

//Sucht needle in der Seite und legt die Treffer in pv->text_occ ab. Enthält
//needle einen Bindestrich, wird zusätzlich ohne Zusammenziehen getrennter
//Zeilenenden gesucht ("E-" / "Mail" = "E-Mail"), da der erste Lauf diesen
//Bindestrich entfernt.
static void viewer_search_page(fz_context *ctx, PageSearchData *sd,
		fz_stext_page *stext, gchar const *needle, fz_search_options options) {
	PdfViewer *pv = sd->pv;

	fz_match_stext_page_cb(ctx, stext, needle, page_hit_cb, sd, options);

	if (needle_has_hyphen(needle)) {
		guint n_first = pv->text_occ.arr_hit->len;

		sd->n_dedup = n_first;
		fz_match_stext_page_cb(ctx, stext, needle, page_hit_cb, sd,
				options | FZ_SEARCH_KEEP_HYPHENS);
		sd->n_dedup = 0;

		if (pv->text_occ.arr_hit->len > n_first)
			g_array_sort_with_data(pv->text_occ.arr_hit,
					(GCompareDataFunc) page_hit_compare, pv);
	}

	return;
}

static gint viewer_text_occ_search_next(PdfViewer *pv, gint index, gint dir) {
	if (dir == 1) {
		for (guint i = 0; i < pv->text_occ.arr_hit->len; i++) {
			TextHit hit = g_array_index(pv->text_occ.arr_hit, TextHit, i);
			fz_quad quad = g_array_index(pv->text_occ.arr_quad, fz_quad,
					hit.first);
			if ((gint) quad.ul.y >= index)
				return (gint) i;
		}
	} else {
		for (gint i = (gint) pv->text_occ.arr_hit->len - 1; i >= 0; i--) {
			TextHit hit = g_array_index(pv->text_occ.arr_hit, TextHit, i);
			fz_quad quad = g_array_index(pv->text_occ.arr_quad, fz_quad,
					hit.first);
			if ((gint) quad.ul.y <= index)
				return i;
		}
	}

	return -1;
}

gint viewer_handle_text_search(PdfViewer* pv, GtkWidget *widget, GError **error) {
	gint dir = 0;
	PdfPos pdf_pos = { 0 };
	PdfPunkt pdf_punkt = { 0 };
	const gchar *search_text = NULL;
	gboolean wrapped = FALSE;

	//dokument durchsucht und kein Fund: return
	if (pv->text_occ.not_found == TRUE) {
		display_message(pv->vf, "Kein Treffer", NULL);

		return 0;
	}

	dir = (widget == pv->button_vorher) ? -1 : 1;

	// Fund angezeigt?
	if (pv->text_occ.index_act >= 0) {
		//nicht erste oder letzte Fundstelle auf dieser Seite
		if (!((dir == 1
				&& pv->text_occ.index_act + dir
						== pv->text_occ.arr_hit->len)
				|| (dir == -1 && pv->text_occ.index_act == 0))) {
			pv->text_occ.index_act += dir;
			viewer_anzeigen_text_occ(pv, FALSE);

			return 0;
		} else //doch
		{
			pdf_pos.seite = pv->text_occ.page_act + dir;

			//Überlauf??
			if (dir == 1) {
				pdf_pos.index = 0;
				if (pv->text_occ.page_act == pv->arr_pages->len - 1) {
					pdf_pos.seite = 0;
					wrapped = TRUE;
				}
			} else if (dir == -1) {
				pdf_pos.index = EOP;
				if (pv->text_occ.page_act == 0) {
					pdf_pos.seite = pv->arr_pages->len - 1;
					wrapped = TRUE;
				}
			}

			//damit festgestellt werden kann, ob einmal durch...
			//pdf_punkt sonst 0
			pdf_punkt.seite = pdf_pos.seite;
			pdf_punkt.punkt.y = (float) pdf_pos.index;
		}
	} else //kein Fund angezeigt: pdf_pos ermitteln
	{
		fz_point point = { 0.0, 0.0 };

		point.y = gtk_adjustment_get_value(pv->v_adj);
		viewer_abfragen_pdf_punkt(pv, point, &pdf_punkt);

		pdf_pos.seite = pdf_punkt.seite;
		pdf_pos.index = (gint) pdf_punkt.punkt.y;
	}

	//kein Fund angezeigt oder erster/letzter Fund auf durchsuchter Seite:
	//nächste/vorherige Seiten müssen so lange durchsucht werden, bis Erfolg oder wieder am Anfang
	search_text = gtk_entry_get_text(GTK_ENTRY(pv->entry_search));

	//wenn entry leer: nichts machen
	if (!g_strcmp0(search_text, ""))
		return 0;

	do {
		//Seite ist aktuell nicht durchsucht - durchsuchen
		if (pdf_pos.seite != pv->text_occ.page_act) {
			gint rc = 0;
			fz_context *ctx = NULL;
			PageSearchData sd = { pv, NULL, 0 };

			//arrays leeren; page_act erst nach erfolgreicher Suche setzen
			pv->text_occ.page_act = -1;
			g_array_remove_range(pv->text_occ.arr_quad, 0,
					pv->text_occ.arr_quad->len);
			g_array_remove_range(pv->text_occ.arr_hit, 0,
					pv->text_occ.arr_hit->len);

			//page_act durchsuchen
			ViewerPageNew *viewer_page = g_ptr_array_index(pv->arr_pages,
					pdf_pos.seite);

			ctx = zond_pdf_document_get_ctx(
					viewer_page->pdf_document_page->document);

			rc = viewer_render_stext_page_fast(ctx,
					viewer_page->pdf_document_page, error);
			if (rc)
				return -1;

			sd.viewer_page = viewer_page;

			fz_try( ctx )
				viewer_search_page(ctx, &sd,
						viewer_page->pdf_document_page->stext_page, search_text,
						FZ_SEARCH_IGNORE_CASE
								| (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(
										pv->check_diakritika)) ?
										FZ_SEARCH_IGNORE_DIACRITICS : 0));
			fz_catch(ctx) {
				g_array_remove_range(pv->text_occ.arr_quad, 0,
						pv->text_occ.arr_quad->len);
				g_array_remove_range(pv->text_occ.arr_hit, 0,
						pv->text_occ.arr_hit->len);
				//kein not_found: Fehler, nächster Klick versucht es erneut
				ERROR_PDF
			}

			//auch ohne Treffer: arr_hit gehört jetzt zu dieser Seite
			pv->text_occ.page_act = pdf_pos.seite;
		}

		//Treffer in soeben oder schon früher durchsuchter Seite?
		if (pv->text_occ.arr_hit->len) //dann gucken, ob im sichtbaren Teil der Seite
		{
			pv->text_occ.index_act = viewer_text_occ_search_next(pv,
					pdf_pos.index, dir);

			if (pv->text_occ.index_act > -1) {
				pv->text_occ.page_act = pdf_pos.seite;
				viewer_anzeigen_text_occ(pv, wrapped);

				return 0;
			}
		}

		//auf der durchsuchten Seite paßt nix: weiterspulen
		pdf_pos.seite += dir;
		if (dir == 1)
			pdf_pos.index = 0;
		else
			pdf_pos.index = EOP;

		//Überlauf
		if (dir == 1 && pdf_pos.seite == pv->arr_pages->len) {
			pdf_pos.seite = 0;
			wrapped = TRUE;
		} else if (dir == -1 && pdf_pos.seite == -1) {
			pdf_pos.seite = pv->arr_pages->len - 1;
			wrapped = TRUE;
		}

		if (pdf_pos.seite == pdf_punkt.seite) //Ausgangsseite wieder erreicht?
				{
			//vielleicht Treffer vor/nach index?
			if ((dir == 1 && pdf_punkt.punkt.y == 0.0)
					|| (dir == -1 && pdf_punkt.punkt.y == (gfloat) EOP)) {
				pv->text_occ.not_found = TRUE;
				gtk_label_set_text(GTK_LABEL(pv->label_search), "");
				display_message(pv->vf, "Kein Treffer", NULL);

				return 0;
			} else //ansonsten referenz-index ( pdf_punkt) verstellen, damit das nur einmal durchlaufen wird
			{
				if (dir == 1)
					pdf_punkt.punkt.y = 0;
				else
					pdf_punkt.punkt.y = (float) EOP;
			}
		}
	} while (1);

	return 0;
}
