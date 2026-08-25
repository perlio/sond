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

/* Aus viewer.c herausgelöst: die Textsuche im angezeigten Dokument (Vor/
 * Zurück-Suche über das Such-Entry im Viewer) sowie das Anspringen/
 * Markieren einer über Zeichenposition adressierten Fundstelle, wie es
 * von der Index-Suche (zond_indexsuche.c) benutzt wird. Beides in sich
 * geschlossen und ohne Berührungspunkte zum Speichern/Layout in viewer.c
 * bzw. viewer_save.c. */

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

static void viewer_anzeigen_text_occ(PdfViewer *pv) {
	PdfPos pdf_pos = { 0 };
	fz_quad quad = { 0 };

	quad = g_array_index(pv->text_occ.arr_quad, fz_quad,
			pv->text_occ.index_act);

	pv->highlight.page[0] = pv->text_occ.page_act;
	pv->highlight.quad[0] = quad;
	pv->highlight.page[1] = -1;

	pdf_pos.seite = pv->text_occ.page_act;
	pdf_pos.index = (gint) quad.ul.y;

	viewer_springen_zu_pos_pdf(pv, pdf_pos, 40);
	gtk_widget_queue_draw(pv->layout); //für den Fall, daß auf gleicher Höhe - dann zeichnet viewer_spring... nicht neu

	return;
}

/* Hilfsstruct für den fz_search_stext_page_cb-Callback */
typedef struct {
	GArray *hits; /* Array von GArray* (jeder Eintrag = Array von fz_quad) */
} SearchHitData;

static gint search_hit_cb(fz_context *ctx, void *opaque,
		gint num_quads, fz_quad *quads) {
	SearchHitData *d = (SearchHitData *) opaque;
	GArray *hit = g_array_new(FALSE, FALSE, sizeof(fz_quad));
	g_array_append_vals(hit, quads, (guint) num_quads);
	g_array_append_val(d->hits, hit);
	return 0; /* weitersuchen */
}

void viewer_highlight_at_char_pos(PdfViewer *pv, gint page_nr,
		gint char_pos_in_page, gchar const *term) {
	ViewerPageNew *viewer_page = NULL;
	gint page_idx = -1;

	if (!term || !*term)
		return;

	/* Passende ViewerPage für page_nr (page_akt) suchen */
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

	/* stext_page synchron sicherstellen. Beim erstmaligen Öffnen eines
	 * Dokuments (frisch erzeugte Seite) ist der asynchrone Render-Task
	 * oft noch gar nicht gestartet - viewer_render_wait_for_transfer()
	 * wartet aber nur auf einen BEREITS laufenden Task und kehrt sonst
	 * sofort zurück, sodass "thread & 8" fälschlich "nicht bereit"
	 * anzeigt und Markierung+Scroll stillschweigend übersprungen wurden.
	 * viewer_render_stext_page_fast() (schon für die Textsuche im
	 * Dokument benutzt, siehe viewer_anzeigen_text_occ) lädt/rendert bei
	 * Bedarf synchron nach. */
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

	/* Alle Treffer des Terms auf der Seite sammeln (case-insensitive) */
	SearchHitData hit_data;
	hit_data.hits = g_array_new(FALSE, TRUE, sizeof(GArray *));

	fz_try(ctx) {
		fz_search_stext_page_cb(ctx, stext, term,
				(fz_search_callback_fn *) search_hit_cb, &hit_data);
	}
	fz_catch(ctx) {
		/* Fehler ignorieren, hits bleibt leer */
	}

	if (hit_data.hits->len == 0) {
		g_array_free(hit_data.hits, TRUE);
		return;
	}

	/* Den richtigen Treffer per char_pos_in_page identifizieren:
	 * Flat-Text der Seite mit map erzeugen (FZ_TEXT_FLATTEN_ALL),
	 * map[char_pos_in_page] liefert den fz_stext_char am gesuchten Offset.
	 * Dann den Treffer wählen, der diesen Char enthält. */
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
		/* Zeichenindex aus Byte-Offset: fz_runeidx */
		unsigned char *flat_data = NULL;
		/* flat_len aus dem Rückgabewert von fz_buffer_storage() - nicht per
		 * strlen() ermitteln: flat_data ist kein garantiert NUL-
		 * terminierter String ohne eingebettete NUL-Bytes. Für Glyphen
		 * ohne Unicode-Mapping kann FZ_TEXT_FLATTEN_ALL ein NUL-Byte
		 * mitten in den Text setzen - strlen() würde dann zu früh
		 * abbrechen (falsche, zu kleine Länge) oder, falls der Buffer gar
		 * nicht terminiert ist, über dessen Ende hinauslaufen. */
		gsize flat_len = fz_buffer_storage(ctx, flat_buf, &flat_data);
		if (flat_data) {
			gint  safe_pos = (char_pos_in_page < (gint) flat_len)
					? char_pos_in_page : (gint) flat_len - 1;
			if (safe_pos >= 0) {
				gsize char_idx = fz_runeidx((gchar *) flat_data,
						(gchar *) flat_data + safe_pos);
				fz_stext_char *target_ch = map[char_idx].ch;

				if (target_ch) {
					/* Treffer suchen, der target_ch enthält */
					for (guint h = 0; h < hit_data.hits->len; h++) {
						GArray *hit = g_array_index(hit_data.hits,
								GArray *, h);
						if (hit->len == 0) continue;
						/* Prüfen ob target_ch innerhalb der
						 * Bounding-Box dieses Treffers liegt */
						fz_quad first = g_array_index(hit, fz_quad, 0);
						fz_quad last  = g_array_index(hit, fz_quad,
								hit->len - 1);
						fz_rect hit_rect = fz_union_rect(
								fz_rect_from_quad(first),
								fz_rect_from_quad(last));
						fz_rect ch_rect = fz_rect_from_quad(
								target_ch->quad);
						if (!fz_is_empty_rect(fz_intersect_rect(
								hit_rect, ch_rect))) {
							chosen_hit = hit;
							break;
						}
					}
					/* Fallback: nächsten Treffer nach char_pos_in_page */
					if (!chosen_hit) {
						fz_point target_origin = target_ch->origin;
						float best_dist = 1e30f;
						for (guint h = 0; h < hit_data.hits->len; h++) {
							GArray *hit = g_array_index(
									hit_data.hits, GArray *, h);
							if (hit->len == 0) continue;
							fz_quad q = g_array_index(hit, fz_quad, 0);
							float dx = q.ll.x - target_origin.x;
							float dy = q.ll.y - target_origin.y;
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

	if (flat_buf) fz_drop_buffer(ctx, flat_buf);
	if (map) fz_free(ctx, map); /* map ist separat alloziert */

	/* Fallback falls map-Methode fehlschlug: ersten Treffer nehmen */
	if (!chosen_hit && hit_data.hits->len > 0)
		chosen_hit = g_array_index(hit_data.hits, GArray *, 0);

	if (!chosen_hit) {
		for (guint h = 0; h < hit_data.hits->len; h++)
			g_array_free(g_array_index(hit_data.hits, GArray *, h), TRUE);
		g_array_free(hit_data.hits, TRUE);
		return;
	}

	/* Erste Quad des Treffers merken (unbeschnitten), um anschließend zur
	 * Trefferposition zu scrollen - analog zu viewer_anzeigen_text_occ(). */
	fz_quad first_quad = g_array_index(chosen_hit, fz_quad, 0);

	/* Quads des gewählten Treffers crop-adjustiert in pv->highlight schreiben */
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

	/* Speicher freigeben */
	for (guint h = 0; h < hit_data.hits->len; h++)
		g_array_free(g_array_index(hit_data.hits, GArray *, h), TRUE);
	g_array_free(hit_data.hits, TRUE);

	if (n_quads > 0) {
		/* Zur Trefferposition scrollen. Beim erstmaligen Öffnen eines neuen
		 * Viewers plant viewer_display_document() den Sprung zur Seite
		 * bereits als Idle-Callback ein (viewer_springen_zu_pos_pdf_idle()),
		 * noch bevor wir hier ankommen - ein sofortiger Sprung würde
		 * von diesem später ausgeführten Callback wieder auf den
		 * Seitenanfang zurückgesetzt. Indem wir denselben Mechanismus
		 * benutzen, reiht sich unser Sprung als zweiter (später
		 * hinzugefügter) Idle-Callback dahinter ein und gewinnt. Bei einem
		 * bereits offenen Viewer (rein synchroner Sprung dort) ist unserer
		 * ohnehin der einzige/letzte. */
		PdfPos pdf_pos = { 0 };

		pdf_pos.seite = page_idx;
		pdf_pos.index = (gint) first_quad.ul.y;
		viewer_springen_zu_pos_pdf_idle(pv, pdf_pos, 40.0);

		gtk_widget_queue_draw(pv->layout);
	}
}

static gint viewer_text_occ_search_next(PdfViewer *pv, gint index, gint dir) {
	if (dir == 1) {
		for (guint i = 0; i < pv->text_occ.arr_quad->len; i++) {
			fz_quad quad = g_array_index(pv->text_occ.arr_quad, fz_quad, i);
			if ((gint) quad.ul.y >= index)
				return (gint) i;
		}
	} else {
		for (gint i = (gint) pv->text_occ.arr_quad->len - 1; i >= 0; i--) {
			fz_quad quad = g_array_index(pv->text_occ.arr_quad, fz_quad, i);
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

	//dokument durchsucht und kein Fund: return
	if (pv->text_occ.not_found == TRUE)
		return 0;

	dir = (widget == pv->button_vorher) ? -1 : 1;

	// Fund angezeigt?
	if (pv->text_occ.index_act >= 0) {
		//nicht erste oder letzte Fundstelle auf dieser Seite
		if (!((dir == 1
				&& pv->text_occ.index_act + dir
						== pv->text_occ.arr_quad->len)
				|| (dir == -1 && pv->text_occ.index_act == 0))) {
			pv->text_occ.index_act += dir;
			viewer_anzeigen_text_occ(pv);

			return 0;
		} else //doch
		{
			pdf_pos.seite = pv->text_occ.page_act + dir;

			//Überlauf??
			if (dir == 1) {
				pdf_pos.index = 0;
				if (pv->text_occ.page_act == pv->arr_pages->len - 1)
					pdf_pos.seite = 0;
			} else if (dir == -1) {
				pdf_pos.index = EOP;
				if (pv->text_occ.page_act == 0)
					pdf_pos.seite = pv->arr_pages->len - 1;
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
			gint anzahl = 0;
			fz_quad quads[100] = { 0 };
			fz_context *ctx = NULL;

			//array leeren
			g_array_remove_range(pv->text_occ.arr_quad, 0,
					pv->text_occ.arr_quad->len);

			//page_act durchsuchen
			ViewerPageNew *viewer_page = g_ptr_array_index(pv->arr_pages,
					pdf_pos.seite);

			ctx = zond_pdf_document_get_ctx(
					viewer_page->pdf_document_page->document);

			rc = viewer_render_stext_page_fast(ctx,
					viewer_page->pdf_document_page, error);
			if (rc)
				return -1;

			fz_try( ctx )
				anzahl = fz_search_stext_page(ctx,
						viewer_page->pdf_document_page->stext_page,
						search_text,
						NULL, quads, 99);
			fz_catch(ctx) {
				pv->text_occ.not_found = TRUE;
				ERROR_PDF
			}

			for (gint u = 0; u < anzahl; u++) {
				fz_rect text_rect = fz_rect_from_quad(quads[u]);
				fz_rect cropped_text_rect = fz_intersect_rect(
						viewer_page->crop, text_rect);

				if (!fz_is_empty_rect(cropped_text_rect)) {
					fz_quad quad = { 0 };

					cropped_text_rect = fz_translate_rect(cropped_text_rect,
							-viewer_page->crop.x0, -viewer_page->crop.y0);

					quad = fz_quad_from_rect(cropped_text_rect);

					g_array_append_val(pv->text_occ.arr_quad, quad);
				}
			}
		}

		//Treffer in soeben oder schon früher durchsuchter Seite?
		if (pv->text_occ.arr_quad->len) //dann gucken, ob im sichtbaren Teil der Seite
		{
			pv->text_occ.index_act = viewer_text_occ_search_next(pv,
					pdf_pos.index, dir);

			if (pv->text_occ.index_act > -1) {
				pv->text_occ.page_act = pdf_pos.seite;
				viewer_anzeigen_text_occ(pv);

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
		if (dir == 1 && pdf_pos.seite == pv->arr_pages->len)
			pdf_pos.seite = 0;
		else if (dir == -1 && pdf_pos.seite == -1)
			pdf_pos.seite = pv->arr_pages->len - 1;

		if (pdf_pos.seite == pdf_punkt.seite) //Ausgangsseite wieder erreicht?
				{
			//vielleicht Treffer vor/nach index?
			if ((dir == 1 && pdf_pos.index <= (gint) pdf_punkt.punkt.y)
					|| (dir == -1
							&& pdf_pos.index >= (gint) pdf_punkt.punkt.y)) {
				pv->text_occ.not_found = TRUE;
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
