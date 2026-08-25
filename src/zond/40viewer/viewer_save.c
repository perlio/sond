/*
 zond (viewer_save.c) - Akten, Beweisstücke, Unterlagen
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

/* Aus viewer.c herausgelöst: alles, was mit dem Zurückschreiben der im
 * Journal gesammelten Änderungen (Annots/Rotation/OCR/Seiten) in die
 * PDF-Arbeitskopie, dem physischen Speichern und dem Nachziehen von
 * Anbindungen/FTS-Index zu tun hat. Bewußt von der reinen Anzeige-/
 * Interaktionslogik in viewer.c getrennt, weil die beiden Themen kaum
 * Berührungspunkte haben. */

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

static gboolean viewer_entry_in_dd(JournalEntry* entry,
		ZPDFDPart* zpdfd_part) {
	if (entry->pdf_document_page->page_akt >= zpdfd_part->first_page->page_akt &&
			entry->pdf_document_page->page_akt <= zpdfd_part->last_page->page_akt) {
		if (entry->type == JOURNAL_TYPE_PAGES_INSERTED ||
				entry->type == JOURNAL_TYPE_PAGE_DELETED ||
				entry->type == JOURNAL_TYPE_ROTATE ||
				entry->type == JOURNAL_TYPE_OCR) return TRUE;

		//Sind ja nur Annots überig
		if (entry->pdf_document_page->page_akt == zpdfd_part->first_page->page_akt) {
			if (zpdfd_part->first_index == 0) return TRUE;
			else {
				fz_rect rect = {0.0, entry->pdf_document_page->rect.x1,
						(gfloat) zpdfd_part->first_index,
						entry->pdf_document_page->rect.y1};

				if (viewer_annot_is_in_rect(&entry->annot_changed.annot_after, rect))
					return 1;
			}
		}
		else if (entry->pdf_document_page->page_akt == zpdfd_part->last_page->page_akt) {
			if (zpdfd_part->last_index == EOP) return TRUE;
			else {
				fz_rect rect = {0.0, entry->pdf_document_page->rect.x1, 0.0,
						(gfloat) zpdfd_part->last_index};

				if (viewer_annot_is_in_rect(&entry->annot_changed.annot_after, rect))
					return 1;
			}
		}
		else return TRUE;
	}

	return FALSE;
}

/* Vor dem physischen Speichern (viewer_do_save_dd()) den FTS-Index für
 * diese Datei auf den nach dem Speichern gültigen Stand bringen:
 * - Seiten mit einem JOURNAL_TYPE_OCR-Eintrag: Inhalt ändert sich (neue
 *   versteckte Textebene) - nur diese eine Seite verwerfen, kein
 *   pauschales Verwerfen der ganzen Datei.
 * - Seiten, die gelöscht werden (pdfp->deleted): ebenfalls verwerfen.
 * - Überlebende Seiten, deren Nummer sich durch anderswo gelöschte/
 *   eingefügte Seiten verschiebt: in chunks/pages umnummerieren (Inhalt
 *   bleibt erhalten). Wiederverwendet dieselbe, für Anbindungen bereits
 *   bewährte Korrektur wie dbase_zond_update_section_dbase() - eine
 *   einzelne Seite ist dafür einfach eine entartete Anbindung
 *   {seite,0}-{seite,EOP} ("ganze Seite").
 * Muss VOR viewer_do_save_dd() laufen (genau wie dbase_zond_update_
 * sections()), weil die dort ablaufende Kompaktierung (pdfp->deleted/
 * inserted-Flags werden gelöscht/page_akt neu gesetzt) genau das ist,
 * was anbindung_korrigieren() auswertet - danach sind die Flags weg. */
#ifndef VIEWER
static void viewer_update_index_for_save(PdfViewer *pdfv, DisplayedDocument *dd) {
	SondIndexCtx *index_ctx = NULL;
	gchar *filename = NULL;
	GArray *arr_journal = NULL;
	GArray *pages = NULL;
	GError *error = NULL;
	GArray *old_nrs = NULL;
	GArray *new_nrs = NULL;

	if (!pdfv->zond->wctx || !pdfv->zond->wctx->index_ctx)
		return;

	index_ctx = pdfv->zond->wctx->index_ctx;
	filename = sond_file_part_get_filepart(
			SOND_FILE_PART(zond_pdf_document_get_sfp_pdf(
					dd->zpdfd_part->zond_pdf_document)));

	/* OCR: Inhalt der betroffenen Seite ändert sich - verwerfen. Zusätzlich
	 * (nur hier, nicht bei reinem Löschen/Umnumerieren weiter unten - dort
	 * ändert sich der Inhalt überlebender Seiten nicht): eine eventuell
	 * bestehende coverage-Abdeckung dieser Datei (oder eines Vorfahre-
	 * Verzeichnisses) ist jetzt falsch, weil clear_page() gerade Chunks
	 * ohne Ersatz entfernt hat - ohne Invalidierung würde eine spätere
	 * Abdeckungs-Prüfung fälschlich "vollständig indiziert" melden. */
	arr_journal = zond_pdf_document_get_arr_journal(dd->zpdfd_part->zond_pdf_document);
	for (guint u = 0; u < arr_journal->len; u++) {
		JournalEntry entry = g_array_index(arr_journal, JournalEntry, u);

		if (entry.type != JOURNAL_TYPE_OCR)
			continue;
		if (!viewer_entry_in_dd(&entry, dd->zpdfd_part))
			continue;

		if (!sond_index_ctx_clear_page(index_ctx, filename,
				entry.pdf_document_page->page_akt, &error)) {
			LOG_WARN("%s\n", error->message);
			g_clear_error(&error);
		}

		if (!sond_index_ctx_coverage_invalidate(index_ctx, filename, &error)) {
			LOG_WARN("%s\n", error->message);
			g_clear_error(&error);
		}
	}

	/* Gelöschte Seiten verwerfen, überlebende ggf. umnummerieren - über
	 * ALLE aktuell im Index geführten Seiten dieser Datei (nicht nur den
	 * Bereich dieses dd), da anbindung_korrigieren() ohnehin nur Seiten
	 * berücksichtigt, die tatsächlich betroffen sind. */
	pages = sond_index_ctx_get_pages_for_file(index_ctx, filename);
	old_nrs = g_array_new(FALSE, FALSE, sizeof(gint));
	new_nrs = g_array_new(FALSE, FALSE, sizeof(gint));

	for (guint u = 0; u < pages->len; u++) {
		gint page_nr = g_array_index(pages, gint, u);
		PdfDocumentPage *pdfp = NULL;

		if (page_nr < 0) /* Nicht-PDF-Konvention (page_nr == -1) betrifft hier nicht */
			continue;

		/* Verteidigung gegen g_ptr_array_index() ohne Bounds-Check:
		 * sollte der Index eine Seitenzahl kennen, die es (mehr) gar
		 * nicht gibt, verwerfen statt out-of-bounds zuzugreifen. */
		if (page_nr >= zond_pdf_document_get_number_of_pages(
				dd->zpdfd_part->zond_pdf_document)) {
			if (!sond_index_ctx_clear_page(index_ctx, filename, page_nr, &error)) {
				LOG_WARN("%s\n", error->message);
				g_clear_error(&error);
			}
			continue;
		}

		pdfp = zond_pdf_document_get_pdf_document_page(
				dd->zpdfd_part->zond_pdf_document, page_nr);

		if (!pdfp || pdfp->deleted) {
			if (!sond_index_ctx_clear_page(index_ctx, filename, page_nr, &error)) {
				LOG_WARN("%s\n", error->message);
				g_clear_error(&error);
			}
			continue;
		}

		{
			Anbindung anbindung = { { page_nr, 0 }, { page_nr, EOP } };

			anbindung_korrigieren(dd->zpdfd_part, &anbindung);

			if (anbindung.von.seite != page_nr) {
				g_array_append_val(old_nrs, page_nr);
				g_array_append_val(new_nrs, anbindung.von.seite);
			}
		}
	}

	if (old_nrs->len > 0) {
		if (!sond_index_ctx_renumber_pages(index_ctx, filename,
				(gint const*) old_nrs->data, (gint const*) new_nrs->data,
				old_nrs->len, &error)) {
			LOG_WARN("%s\n", error->message);
			g_clear_error(&error);
		}
	}

	g_array_unref(old_nrs);
	g_array_unref(new_nrs);
	g_array_unref(pages);
	g_free(filename);

	return;
}
#endif //VIEWER

static void  viewer_reset_dirty_dds(PdfViewer* pdfv) {
	for (guint i = 0; i < pdfv->zond->arr_pv->len; i++) {
		PdfViewer* pdfv_test = NULL;
		DisplayedDocument* dd = NULL;

		pdfv_test = g_ptr_array_index(pdfv->zond->arr_pv, i);
		dd = pdfv_test->dd;

		do {
			GArray* arr_journal = NULL;
			gboolean in_dd = FALSE;

			arr_journal = zond_pdf_document_get_arr_journal(dd->zpdfd_part->zond_pdf_document);

			for (guint u = 0; u < arr_journal->len; u++) {
				JournalEntry entry = { 0 };

				entry = g_array_index(arr_journal, JournalEntry, u);

				in_dd = viewer_entry_in_dd(&entry, dd->zpdfd_part);
				if (in_dd)
					break;
			}

			if (!in_dd)
				dd->zpdfd_part->dirty = FALSE;
			else
				dd->zpdfd_part->dirty = TRUE;

			dd = dd->next;
		} while (dd);
	}

	return;
}

static gint viewer_do_save_dd(PdfViewer* pv, DisplayedDocument* dd,
		fz_context* ctx, pdf_document* doc, GError** error) {
	GArray* arr_journal = NULL;
	gint rc = 0;
	Anbindung anbindung = { 0 };
	Anbindung anbindung_orig = { 0 };
	gint page_orig = 0;
	gint num = 0; //für ocr-Font - nur einmal suchen oder kopieren

	//jetzt speichern.
	//Dafür ersemal rausfinden,
	//welche Seite im doc die letzte Seite vor 1. Seite des dd ist
	zpdfd_part_get_anbindung(dd->zpdfd_part, &anbindung);
	anbindung_orig = anbindung;
	anbindung_get_orig(dd->zpdfd_part->zond_pdf_document, &anbindung_orig);
	page_orig = anbindung_orig.bis.seite;

	arr_journal = zond_pdf_document_get_arr_journal(dd->zpdfd_part->zond_pdf_document);

	for (gint i = anbindung.bis.seite; i >= anbindung.von.seite; i--) {
		PdfDocumentPage* pdfp = NULL;
		pdf_page* pdf_page = NULL;

		pdfp = zond_pdf_document_get_pdf_document_page(dd->zpdfd_part->zond_pdf_document, i);

		if (pdfp->deleted && !pdfp->inserted) {
			fz_try(ctx)
				pdf_delete_page(ctx, doc, page_orig);
			fz_catch(ctx) {
				pdf_drop_document(ctx, doc);

				ERROR_PDF
			}

			page_orig--;

			continue;
		}

		/* In dieser Sitzung eingefügt UND wieder gelöscht (Phantom-Seite):
		 * existiert nie in doc und wurde von anbindung_get_orig() schon
		 * von Anfang an nicht in page_orig mitgezählt (siehe dortige
		 * Behandlung von pdfp->inserted, unabhängig von ->deleted) - also
		 * weder pdf_delete_page() noch page_orig--, sonst würde eine nie
		 * gezählte Seite doch abgezogen und alle folgenden (früheren)
		 * Seiten dieser Schleife um 1 verschoben. */
		if (pdfp->deleted && pdfp->inserted)
			continue;

		if (pdfp->inserted && !pdfp->deleted) {
			gint rc = 0;

			zond_pdf_document_mutex_lock(dd->zpdfd_part->zond_pdf_document);
			rc = pdf_copy_page(ctx,
					zond_pdf_document_get_pdf_doc(dd->zpdfd_part->zond_pdf_document),
					i, i, doc, page_orig, error);
			zond_pdf_document_mutex_unlock(dd->zpdfd_part->zond_pdf_document);
			if (rc) {
				pdf_drop_document(ctx, doc);

				return -1;
			}

			page_orig++;
		}

		fz_try(ctx)
			pdf_page = pdf_load_page(ctx, doc, page_orig);
		fz_catch(ctx) {
			pdf_drop_document(ctx, doc);

			ERROR_PDF
		}

		//entries durchgehen und ggf. einpflegen
		for (gint u = 0; u < arr_journal->len; u++) {
			JournalEntry entry = { 0 };

			entry = g_array_index(arr_journal, JournalEntry, u);

			if (pdfp != entry.pdf_document_page)
				continue;

			if (entry.type == JOURNAL_TYPE_PAGES_INSERTED ||
					entry.type == JOURNAL_TYPE_PAGE_DELETED ||
					entry.type == JOURNAL_TYPE_ANNOT_DELETED)
				continue;

			if (entry.pdf_document_page->deleted)
				continue;

			if (entry.type == JOURNAL_TYPE_ROTATE) {
				gint rc = 0;

				rc = pdf_page_rotate(ctx, pdf_page->obj, entry.rotate.winkel, error);
				if (rc) {
					pdf_drop_page(ctx, pdf_page);
					pdf_drop_document(ctx, doc);

					return -1;
				}
			}
			else if (entry.type == JOURNAL_TYPE_OCR) {
				gint rc = 0;

				if (!num) {
					pdf_obj* font_ref = NULL;

					rc = pdf_get_sond_font(ctx, doc, &font_ref, error);
					if (rc) {
						pdf_drop_page(ctx, pdf_page);
						pdf_drop_document(ctx, doc);

						return -1;
					}
					else if (!font_ref) {

						font_ref = pdf_put_sond_font(ctx, doc, error);
						if (!font_ref) {
							pdf_drop_page(ctx, pdf_page);
							pdf_drop_document(ctx, doc);

							return -1;
						}
					}

					fz_try(ctx)
						num = pdf_to_num(ctx, font_ref);
					fz_catch(ctx) {
						pdf_drop_page(ctx, pdf_page);
						pdf_drop_document(ctx, doc);

						ERROR_PDF
					}
				}

				rc = pdf_set_content_stream(ctx, pdf_page, entry.ocr.buf_new, error);
				if (rc)
				{
					pdf_drop_page(ctx, pdf_page);
					pdf_drop_document(ctx, doc);

					return -1;
				}
			}
			else if (entry.type == JOURNAL_TYPE_ANNOT_CREATED) {
				pdf_annot* pdf_ann = NULL;

				//pdf_ann borrowed pointer
				pdf_ann = viewer_annot_do_create(ctx, pdf_page, entry.pdf_document_page->rotate,
						entry.annot_changed.annot_after, error);
				if (!pdf_ann) {
					pdf_drop_page(ctx, pdf_page);
					pdf_drop_document(ctx, doc);
					return -1;
				}
			}
			else if (entry.type == JOURNAL_TYPE_ANNOT_CHANGED) {
				gint index = 0;
				gint rc = 0;
				pdf_annot* pdf_ann = NULL;

				index = pdf_document_page_annot_get_index(
						entry.annot_changed.pdf_document_page_annot);
				pdf_ann = pdf_annot_lookup_index(ctx, pdf_page, index);

				rc = viewer_annot_do_change(ctx, pdf_ann,
						entry.pdf_document_page->rotate,
						entry.annot_changed.annot_after, error);
				if (rc) {
					pdf_drop_page(ctx, pdf_page);
					pdf_drop_document(ctx, doc);
					return -1;
				}
			}
		}

		//Annots löschen
		//erstmal sicherstellen, daß nichts mehr läuft
		viewer_render_wait_for_transfer(pdfp);

		if (pdfp->arr_annots) {
			pdf_annot* pdf_ann = NULL;

			pdf_ann = pdf_first_annot(ctx, pdf_page);

			for (gint u = 0; u < pdfp->arr_annots->len; u++) {
				PdfDocumentPageAnnot* pdfp_annot = NULL;
				pdf_annot* annot_next = NULL;

				pdfp_annot = g_ptr_array_index(pdfp->arr_annots, u);
				if (pdf_ann) {
					annot_next = pdf_next_annot(ctx, pdf_ann);
					if (pdfp_annot->deleted)
						pdf_delete_annot(ctx, pdf_page, pdf_ann);
				}
				else
					LOG_WARN("%s\nzu viele annots", __func__);

				pdf_ann = annot_next;
			}
		}

		pdf_drop_page(ctx, pdf_page);

		page_orig--;
	}

	//alles geändert, dann speichern
	zond_pdf_document_mutex_lock(dd->zpdfd_part->zond_pdf_document);
	rc = sond_file_part_pdf_save_and_close(ctx, doc,
			zond_pdf_document_get_sfp_pdf(dd->zpdfd_part->zond_pdf_document), error);
	zond_pdf_document_mutex_unlock(dd->zpdfd_part->zond_pdf_document);
	if (rc)
		return -1;

	//Journal bereinigen
	for (gint i = arr_journal->len - 1; i >= 0; i--) {
		JournalEntry entry = { 0 };

		entry = g_array_index(arr_journal, JournalEntry, i);
		if (viewer_entry_in_dd(&entry, dd->zpdfd_part))
			g_array_remove_index(arr_journal, i);
	}

	//gelöschte Seiten aus geöffnetem dd löschen
	gint i = dd->zpdfd_part->last_page->page_akt;

	//ist oben schon sichergestellt, daß kein rendering mehr stattfindet
	do {
		PdfDocumentPage* pdfp = NULL;

		pdfp = zond_pdf_document_get_pdf_document_page(dd->zpdfd_part->zond_pdf_document, i);

		if (pdfp)
			pdfp->inserted = NULL;

		if (pdfp && pdfp->deleted) { //Seite aus pdf_document löschen
			fz_try(zond_pdf_document_get_ctx(dd->zpdfd_part->zond_pdf_document)) {
				zond_pdf_document_mutex_lock(dd->zpdfd_part->zond_pdf_document);

				pdf_delete_page(zond_pdf_document_get_ctx(dd->zpdfd_part->zond_pdf_document),
						zond_pdf_document_get_pdf_doc(dd->zpdfd_part->zond_pdf_document), i);
			}
			fz_always(zond_pdf_document_get_ctx(dd->zpdfd_part->zond_pdf_document))
				zond_pdf_document_mutex_unlock(dd->zpdfd_part->zond_pdf_document);
			fz_catch(zond_pdf_document_get_ctx(dd->zpdfd_part->zond_pdf_document)) {
				if (error) *error = g_error_new(g_quark_from_static_string("mupdf"),
						fz_caught(zond_pdf_document_get_ctx(dd->zpdfd_part->zond_pdf_document)),
						"%s\n%s", __func__,
						fz_caught_message(zond_pdf_document_get_ctx(dd->zpdfd_part->zond_pdf_document)));

				return -1;
			}

			//ggf. dd anpassen, falls erste oder letzte Seite gelöscht wird
			//kann derzeit nur passieren, wenn dd ganzes Dokument umfaßt und keine Anbindung ist
			if (pdfp == dd->zpdfd_part->first_page) //Dokument muß mindestens zwei Seiten haben
				dd->zpdfd_part->first_page =
						zond_pdf_document_get_pdf_document_page(
								dd->zpdfd_part->zond_pdf_document, i + 1);
			else if (pdfp == dd->zpdfd_part->last_page)
				dd->zpdfd_part->last_page =
						zond_pdf_document_get_pdf_document_page(
								dd->zpdfd_part->zond_pdf_document, i - 1);

			g_ptr_array_remove_index(zond_pdf_document_get_arr_pages(
					dd->zpdfd_part->zond_pdf_document), i);

			//Seitenzahlen der folgenden Seiten anpassen
			for (gint f = i; f < zond_pdf_document_get_arr_pages(
					dd->zpdfd_part->zond_pdf_document)->len; f++) {
				PdfDocumentPage* pdfp_loop = NULL;

				pdfp_loop = g_ptr_array_index(zond_pdf_document_get_arr_pages(
						dd->zpdfd_part->zond_pdf_document), f);
				if (pdfp_loop)
					pdfp_loop->page_akt--;
			}
		}
		else if (pdfp->arr_annots) {
			pdf_annot* annot_pdf = NULL;

			annot_pdf = pdf_first_annot(ctx, pdfp->page); //gibt es, sonst kein arr_annots!

			//gelöschte annots aus arr_annot löschen
			for (gint u = 0; u < pdfp->arr_annots->len; u++) {
				PdfDocumentPageAnnot* pdfp_annot = NULL;
				pdf_annot* annot_next = NULL;

				pdfp_annot = g_ptr_array_index(pdfp->arr_annots, u);
				annot_next = pdf_next_annot(ctx, annot_pdf);

				if (pdfp_annot->deleted) {
					g_ptr_array_remove_index(pdfp->arr_annots, u);

					fz_try(ctx)
						pdf_delete_annot(ctx, pdfp->page, annot_pdf);
					fz_catch(ctx)
						ERROR_PDF

					u--;
				}

				annot_pdf = annot_next;
			}
		}

		i--;
	} while (i >= dd->zpdfd_part->first_page->page_akt);

	return 0;
}

gint viewer_save_dirty_dds(PdfViewer *pdfv, GError** error) {
	DisplayedDocument *dd = NULL;

	dd = pdfv->dd;
	if (!dd)
		return 0;

#ifndef VIEWER
	gboolean changed = FALSE;

	//Projekt-Zustand (geändert oder nicht) zwischenspeichern
	changed = pdfv->zond->dbase_zond->changed;
#endif //VIEWER

	//Alle Dds, die im Viewer angezeigt werden, durchgehen
	do {
		gint rc = 0;
		fz_context* ctx = NULL;
		pdf_document* doc = NULL;

		if (!dd->zpdfd_part->dirty)
			continue;

		//PDF-Datei in buf laden
		ctx = zond_pdf_document_get_ctx(dd->zpdfd_part->zond_pdf_document);
		doc = sond_file_part_pdf_open_document(ctx,
				zond_pdf_document_get_sfp_pdf(dd->zpdfd_part->zond_pdf_document),
				TRUE, FALSE, error);
		if (!doc)
			return -1;

#ifndef VIEWER
		rc = dbase_zond_begin(pdfv->zond->dbase_zond, error);
		if (rc) {
			g_prefix_error(error, "%s\n", __func__);
			pdf_drop_document(ctx, doc);

			return rc;
		}

		//FTS-Index für diese Datei auf den nach dem Speichern gültigen
		//Stand bringen (Seiten verwerfen/umnummerieren) - muss vor dem
		//physischen Speichern laufen, s. Kommentar an der Funktion.
		viewer_update_index_for_save(pdfv, dd);

		//Anbindungen in db anpassen
		rc = dbase_zond_update_sections(pdfv->zond->dbase_zond, dd, error);
		if (rc)
		{
			g_prefix_error(error, "%s\n", __func__);
			pdf_drop_document(ctx, doc);

			dbase_zond_rollback(pdfv->zond->dbase_zond, error);

			return -1;
		}
#endif //VIEWER

		/* doc wird ab hier NICHT mehr selbst gedroppt: viewer_do_save_dd()
		 * hat das - auf jedem seiner Rückkehrpfade, egal ob Erfolg oder
		 * Fehler - bereits selbst erledigt (entweder direkt, oder über
		 * sond_file_part_pdf_save_and_close(), die pdf_doc seit dem Fix
		 * dort unabhängig vom Ergebnis droppt). Ein weiterer
		 * pdf_drop_document(ctx, doc) hier wäre ein Doppel-Free. */
		rc = viewer_do_save_dd(pdfv, dd, ctx, doc, error);
		if (rc) {
			g_prefix_error(error, "%s\n", __func__);
#ifndef VIEWER
			dbase_zond_rollback(pdfv->zond->dbase_zond, error);

			return -1;
		}

		rc = dbase_zond_commit(pdfv->zond->dbase_zond, error);
		if (rc) {
			g_prefix_error(error, "%s\n", __func__);
#endif //viewer

			return -1;
		}
	} while ((dd = dd->next));

#ifndef VIEWER
		//ggf. zurücksetzen
		if (!changed) project_reset_changed(pdfv->zond, FALSE);
#endif //VIEWER

	/* dirty-Status aller dd's alle offenen pvs einmalig aus dem (jetzt
	 * bereinigten) Journal neu herleiten. viewer_reset_dirty_dds() geht
	 * dafür intern ohnehin schon über pdfv->zond->arr_pv, also über ALLE
	 * offenen pvs - das Ergebnis hängt gar nicht vom übergebenen pdfv ab.
	 * Ein einziger Aufruf genügt daher (vorher wurde hier dieselbe,
	 * für alle pvs identische Arbeit pro offenem pv redundant wiederholt). */
	viewer_reset_dirty_dds(pdfv);

	//Bei allen sauberen pvs Speichern insensitiv
	for (gint i = 0; i < pdfv->zond->arr_pv->len; i++) {
		PdfViewer *pdfv_test = NULL;

		pdfv_test = g_ptr_array_index(pdfv->zond->arr_pv, i);

		if (!viewer_has_dirty_dd(pdfv_test))
			gtk_widget_set_sensitive(pdfv_test->button_speichern, FALSE);
	}

	return 0;
}

void viewer_save_and_close(PdfViewer *pdfv) {
	gtk_popover_popdown(GTK_POPOVER(pdfv->annot_pop_edit));

	//ggf. fragen, ob gespeichert werden soll
	if (gtk_widget_get_sensitive(pdfv->button_speichern)) {
		gint rc = 0;

		rc = abfrage_frage( NULL, "Viewer enthält Änderungen", "Speichern?",
				NULL);

		if (rc == GTK_RESPONSE_YES) {
			gint ret = 0;
			GError* error = NULL;

			ret = viewer_save_dirty_dds(pdfv, &error);
			if (ret) {
				display_message(pdfv->vf, "Speichern nicht erfoglreich\n\n", error->message, NULL);
				g_error_free(error);

				return;
			}
		}
		else if (rc != GTK_RESPONSE_NO) return;
	}

	viewer_schliessen(pdfv);

	return;
}
