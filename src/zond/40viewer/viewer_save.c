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

#include "../../gtk3/sond_dialoge.h"
#include "../../sond_fileparts.h"
#include "../../sond_log_and_error.h"
#include "../../sond_pdf_helper.h"

#include "../zond_pdf_document.h"
#include "../anbindung_zaehlung.h"

#include "../../znd/anbindung.h"


#include "document.h"
#include "seiten.h"
#include "viewer_ui.h"
#include "viewer_render.h"
#include "pdf_annot_ops.h"

/* Liefert die physische Annot-Position auf der frisch geöffneten pdf_page
 * (0-basiert) - im Unterschied zu pdf_document_page_annot_get_index()
 * (rohe arr_annots-Position inkl. Karteileichen) werden hier bereits bei
 * einem früheren Speichern physisch gelöschte Annotationen (->deleted &&
 * ->on_disk_deleted) nicht mitgezählt, weil sie auf pdf_page (Kopie vom
 * letzten Speicherstand) gar nicht mehr existieren - dieselbe Logik wie
 * in der "Annots löschen"-Schleife weiter unten (dort pdf_ann bewußt
 * nicht vorrücken statt hier den Zähler nicht erhöhen). Rückgabe -1, wenn
 * target nicht in arr_annots gefunden (sollte nicht vorkommen). */
static gint viewer_save_get_physical_annot_index(PdfDocumentPage* pdfp,
		PdfDocumentPageAnnot* target) {
	gint physical = 0;

	for (gint u = 0; u < pdfp->arr_annots->len; u++) {
		PdfDocumentPageAnnot* pdfp_annot = g_ptr_array_index(pdfp->arr_annots, u);

		if (pdfp_annot == target)
			return physical;

		if (!(pdfp_annot->deleted && pdfp_annot->on_disk_deleted))
			physical++;
	}

	return -1;
}

/* Zustand eines Speichervorgangs für einen Abschnitt (dd) */
typedef struct {
	fz_context* ctx;
	pdf_document* doc; //die frisch geladene Arbeitskopie
	DisplayedDocument* dd;
	GArray* arr_journal;
	gint num; //Objektnummer der OCR-Schrift: nur einmal suchen oder anlegen
} SaveCtx;

/* Bereitet die Seite pdfp (Nr. i im Live-Dokument) in der Arbeitskopie vor: ausgeschlossene Seiten
 * entfernen, eingefügte kopieren, dann die Seite laden. *page_orig ist die Position der Seite in
 * der Kopie und wird mitgeführt.
 * 0: Seite in *page_out geladen, 1: nichts weiter zu tun, -1: Fehler (die Kopie ist dann schon
 * freigegeben). */
static gint viewer_save_seite_vorbereiten(SaveCtx* s, PdfDocumentPage* pdfp, gint i,
		gint* page_orig, pdf_page** page_out, GError** error) {
	fz_context* ctx = s->ctx;
	pdf_document* doc = s->doc;
	pdf_page* page = NULL;

	if (pdfp->deleted && !pdfp->inserted) {
		/* Schon bei einem früheren Speichern aus der Datei
		 * ausgeschlossen (nur noch live als Karteileiche in
		 * arr_pages vorhanden, s. PdfDocumentPage->on_disk_deleted) -
		 * existiert in der hier frisch geöffneten doc gar nicht
		 * mehr. Nichts zu tun, page_orig unverändert lassen (sonst
		 * würde eine nie in doc vorhandene Seite fälschlich
		 * abgezogen). */
		if (pdfp->on_disk_deleted)
			return 1;

		fz_try(ctx)
			pdf_delete_page(ctx, doc, *page_orig);
		fz_catch(ctx) {
			pdf_drop_document(ctx, doc);

			ERROR_PDF
		}

		pdfp->on_disk_deleted = TRUE;

		(*page_orig)--;

		return 1;
	}

	/* In dieser Sitzung eingefügt UND wieder gelöscht (Phantom-Seite):
	 * existiert nie in doc und wurde von anbindung_get_orig() schon
	 * von Anfang an nicht in page_orig mitgezählt (siehe dortige
	 * Behandlung von pdfp->inserted, unabhängig von ->deleted) - also
	 * weder pdf_delete_page() noch page_orig--, sonst würde eine nie
	 * gezählte Seite doch abgezogen und alle folgenden (früheren)
	 * Seiten dieser Schleife um 1 verschoben. */
	if (pdfp->deleted && pdfp->inserted)
		return 1;

	if (pdfp->inserted && !pdfp->deleted) {
		gint rc = 0;

		//mit Annotationen aus dem Live-Dokument, auch den versteckten:
		//die Löschschleife arbeitet positionsweise über arr_annots
		zond_pdf_document_mutex_lock(s->dd->zpdfd_part->zond_pdf_document);
		rc = pdf_copy_page(ctx,
				zond_pdf_document_get_pdf_doc(s->dd->zpdfd_part->zond_pdf_document),
				i, i, doc, *page_orig, TRUE, error);
		zond_pdf_document_mutex_unlock(s->dd->zpdfd_part->zond_pdf_document);
		if (rc) {
			pdf_drop_document(ctx, doc);

			return -1;
		}

		(*page_orig)++;
	}

	fz_try(ctx)
		page = pdf_load_page(ctx, doc, *page_orig);
	fz_catch(ctx) {
		pdf_drop_document(ctx, doc);

		ERROR_PDF
	}

	*page_out = page;

	return 0;
}

/* Pflegt einen Journal-Eintrag in die geladene Seite ein (Drehung, OCR, Annotation anlegen oder
 * ändern). Bei einem Fehler gibt der Aufrufer Seite und Kopie frei. */
static gint viewer_save_eintrag(SaveCtx* s, PdfDocumentPage* pdfp, pdf_page* page,
		JournalEntry* entry, GError** error) {
	fz_context* ctx = s->ctx;

	if (entry->type == JOURNAL_TYPE_ROTATE)
		return pdf_page_rotate(ctx, page->obj, entry->rotate.winkel, error);

	if (entry->type == JOURNAL_TYPE_OCR) {
		if (!s->num) {
			pdf_obj* font_ref = NULL;

			if (pdf_get_sond_font(ctx, s->doc, &font_ref, error))
				return -1;

			if (!font_ref) {
				font_ref = pdf_put_sond_font(ctx, s->doc, error);
				if (!font_ref)
					return -1;
			}

			fz_try(ctx)
				s->num = pdf_to_num(ctx, font_ref);
			fz_catch(ctx)
				ERROR_PDF
		}

		return pdf_set_content_stream(ctx, page, entry->ocr.buf_new, error);
	}

	if (entry->type == JOURNAL_TYPE_ANNOT_CREATED) {
		//pdf_ann borrowed pointer
		//created (NULL): bei einem Fehlschlag hier wird doc ohnehin
		//komplett verworfen (s. Aufrufer), kein Bookkeeping nötig
		if (!pdf_annot_ops_create(ctx, page, entry->pdf_document_page->rotate,
				entry->annot_changed.annot_after, NULL, error))
			return -1;
	}
	else if (entry->type == JOURNAL_TYPE_ANNOT_CHANGED) {
		gint index = viewer_save_get_physical_annot_index(pdfp,
				entry->annot_changed.pdf_document_page_annot);
		pdf_annot* pdf_ann = (index >= 0) ?
				pdf_annot_lookup_index(ctx, page, index) : NULL;

		if (!pdf_ann) {
			*error = g_error_new(VIEWER_ERROR, 0,
					"%s\nZu ändernde Annotation auf Seite %i nicht gefunden",
					__func__, pdfp->page_akt + 1);

			return -1;
		}

		return pdf_annot_ops_change(ctx, pdf_ann, entry->pdf_document_page->rotate,
				entry->annot_changed.annot_after, error);
	}

	return 0;
}

//Geht das Journal durch und pflegt die Einträge dieser Seite ein
static gint viewer_save_journal_einpflegen(SaveCtx* s, PdfDocumentPage* pdfp, pdf_page* page,
		GError** error) {
	for (gint u = 0; u < s->arr_journal->len; u++) {
		JournalEntry entry = { 0 };

		entry = g_array_index(s->arr_journal, JournalEntry, u);

		if (pdfp != entry.pdf_document_page)
			continue;

		if (entry.type == JOURNAL_TYPE_PAGES_INSERTED ||
				entry.type == JOURNAL_TYPE_PAGE_DELETED ||
				entry.type == JOURNAL_TYPE_ANNOT_DELETED)
			continue;

		if (entry.pdf_document_page->deleted)
			continue;

		//eingefügte Seite: Annotationen kamen schon mit der Kopie
		//aus dem Live-Dokument (samt allen Änderungen)
		if (pdfp->inserted && (entry.type == JOURNAL_TYPE_ANNOT_CREATED ||
				entry.type == JOURNAL_TYPE_ANNOT_CHANGED))
			continue;

		if (viewer_save_eintrag(s, pdfp, page, &entry, error))
			return -1;
	}

	return 0;
}

//Löscht die als gelöscht markierten Annotationen der Seite aus der Kopie
static gint viewer_save_annots_loeschen(fz_context* ctx, pdf_page* page, PdfDocumentPage* pdfp,
		GError** error) {
	pdf_annot* pdf_ann = NULL;

	//erstmal sicherstellen, daß nichts mehr läuft
	viewer_render_wait_for_transfer(pdfp);

	if (!pdfp->arr_annots)
		return 0;

	pdf_ann = pdf_first_annot(ctx, page);

	for (gint u = 0; u < pdfp->arr_annots->len; u++) {
		PdfDocumentPageAnnot* pdfp_annot = NULL;
		pdf_annot* annot_next = NULL;

		pdfp_annot = g_ptr_array_index(pdfp->arr_annots, u);

		/* Schon bei einem früheren Speichern aus der Datei
		 * ausgeschlossen (s. on_disk_deleted an PdfDocumentPage)
		 * - existiert auf der frisch geladenen Seite gar nicht
		 * mehr. pdf_ann bewußt NICHT voranrücken, sonst
		 * verschiebt sich die positionsweise Zuordnung zu den
		 * übrigen, tatsächlich noch vorhandenen Annotationen. */
		if (pdfp_annot->deleted && pdfp_annot->on_disk_deleted)
			continue;

		if (pdf_ann) {
			annot_next = pdf_next_annot(ctx, pdf_ann);
			if (pdfp_annot->deleted) {
				fz_try(ctx)
					pdf_delete_annot(ctx, page, pdf_ann);
				fz_catch(ctx)
					ERROR_PDF

				pdfp_annot->on_disk_deleted = TRUE;
			}
		}
		else
			LOG_WARN("%s\nzu viele annots", __func__);

		pdf_ann = annot_next;
	}

	return 0;
}

//Entfernt die Journal-Einträge dieses Abschnitts, nachdem er gespeichert ist
static void viewer_save_journal_bereinigen(GArray* arr_journal, DisplayedDocument* dd) {
	for (gint i = arr_journal->len - 1; i >= 0; i--) {
		JournalEntry entry = { 0 };

		entry = g_array_index(arr_journal, JournalEntry, i);
		if (document_entry_in_dd(&entry, dd->zpdfd_part))
			g_array_remove_index(arr_journal, i);
	}
}

/* Live-Buchführung nachziehen: nur noch Flags setzen, kein Zugriff
 * mehr auf das Live-pdf_doc. Das tatsächliche Ausschließen aus der
 * gespeicherten Datei ist in viewer_save_seite_vorbereiten() bereits
 * passiert (inkl. Markieren via on_disk_deleted); hier wird nur noch
 * "inserted" zurückgesetzt (Seite ist jetzt nicht mehr "neu, noch
 * nicht gespeichert") und - rein informativ für spätere best-effort-
 * Aufräumversuche - on_disk_deleted an bereits gelöschten Seiten/
 * Annotationen (falls dort noch nicht gesetzt, z.B. bei Phantom-Seiten)
 * nachgezogen. Gelöschte Seiten und Annotationen bleiben bewußt
 * dauerhaft in arr_pages/arr_annots liegen ("Karteileichen") - kein
 * pdf_delete_page()/pdf_delete_annot() mehr auf dem Live-pdf_doc, keine
 * Umnumerierung, kein Anpassen von first_page/last_page. Sie sind für
 * Anzeige (viewer_new_page()) und künftige Speichervorgänge
 * (on_disk_deleted-Prüfung) bereits korrekt unsichtbar/ausgeschlossen.
 * Reine Struct-Feld-Zuweisungen - kann nicht fehlschlagen. */
static void viewer_save_buchfuehrung_nachziehen(DisplayedDocument* dd) {
	gint i = dd->zpdfd_part->last_page->page_akt;

	do {
		PdfDocumentPage* pdfp = NULL;

		pdfp = zond_pdf_document_get_pdf_document_page(dd->zpdfd_part->zond_pdf_document, i);

		if (pdfp) {
			pdfp->inserted = FALSE;
			pdfp->inserted_in = NULL;

			if (pdfp->deleted)
				pdfp->on_disk_deleted = TRUE;

			if (pdfp->arr_annots) {
				for (gint u = 0; u < pdfp->arr_annots->len; u++) {
					PdfDocumentPageAnnot* pdfp_annot = NULL;

					pdfp_annot = g_ptr_array_index(pdfp->arr_annots, u);
					if (pdfp_annot->deleted)
						pdfp_annot->on_disk_deleted = TRUE;
				}
			}
		}

		i--;
	} while (i >= dd->zpdfd_part->first_page->page_akt);
}

static gint viewer_do_save_dd(PdfViewer* pv, DisplayedDocument* dd,
		fz_context* ctx, pdf_document* doc, GError** error) {
	SaveCtx s = { ctx, doc, dd, NULL, 0 };
	gint rc = 0;
	Anbindung anbindung = { 0 };
	Anbindung anbindung_orig = { 0 };
	gint page_orig = 0;

	//jetzt speichern.
	//Dafür erstmal rausfinden,
	//welche Seite im doc die letzte Seite vor 1. Seite des dd ist
	zpdfd_part_get_anbindung(dd->zpdfd_part, &anbindung);
	anbindung_orig = anbindung;
	anbindung_get_orig(dd->zpdfd_part->zond_pdf_document, &anbindung_orig);
	page_orig = anbindung_orig.bis.seite;

	s.arr_journal = zond_pdf_document_get_arr_journal(dd->zpdfd_part->zond_pdf_document);

	for (gint i = anbindung.bis.seite; i >= anbindung.von.seite; i--) {
		PdfDocumentPage* pdfp = NULL;
		pdf_page* page = NULL;

		pdfp = zond_pdf_document_get_pdf_document_page(dd->zpdfd_part->zond_pdf_document, i);

		rc = viewer_save_seite_vorbereiten(&s, pdfp, i, &page_orig, &page, error);
		if (rc == 1)
			continue;
		else if (rc)
			return -1; //die Kopie ist schon freigegeben

		//Einträge durchgehen und ggf. einpflegen, dann Annotationen löschen
		rc = viewer_save_journal_einpflegen(&s, pdfp, page, error);
		if (!rc)
			rc = viewer_save_annots_loeschen(ctx, page, pdfp, error);
		if (rc) {
			pdf_drop_page(ctx, page);
			pdf_drop_document(ctx, doc);

			return -1;
		}

		pdf_drop_page(ctx, page);

		page_orig--;
	}

	//alles geändert, dann speichern
	zond_pdf_document_mutex_lock(dd->zpdfd_part->zond_pdf_document);
	rc = sond_file_part_pdf_save_and_close(ctx, doc,
			zond_pdf_document_get_sfp_pdf(dd->zpdfd_part->zond_pdf_document), error);
	zond_pdf_document_mutex_unlock(dd->zpdfd_part->zond_pdf_document);
	if (rc)
		return -1;

	viewer_save_journal_bereinigen(s.arr_journal, dd);
	viewer_save_buchfuehrung_nachziehen(dd);

	return 0;
}


gint viewer_save_dirty_dds(PdfViewer *pdfv, GError** error) {
	DisplayedDocument *dd = NULL;

	dd = pdfv->dd;
	if (!dd)
		return 0;

	pdf_viewer_session_save_started(pdfv->session, pdfv);

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
				FALSE, error);
		if (!doc)
			return -1;

		//Host vorbereiten lassen (z.B. Datenbank-Transaktion, Anbindungen)
		rc = pdf_viewer_session_save_prepare(pdfv->session, pdfv, dd, error);
		if (rc) {
			g_prefix_error(error, "%s\n", __func__);
			pdf_drop_document(ctx, doc);

			return -1;
		}

		/* doc wird ab hier NICHT mehr selbst gedroppt: viewer_do_save_dd()
		 * hat das - auf jedem seiner Rückkehrpfade, egal ob Erfolg oder
		 * Fehler - bereits selbst erledigt (entweder direkt, oder über
		 * sond_file_part_pdf_save_and_close(), die pdf_doc seit dem Fix
		 * dort unabhängig vom Ergebnis droppt). Ein weiterer
		 * pdf_drop_document(ctx, doc) hier wäre ein Doppel-Free. */
		rc = viewer_do_save_dd(pdfv, dd, ctx, doc, error);
		if (rc) {
			g_prefix_error(error, "%s\n", __func__);
			pdf_viewer_session_save_finished(pdfv->session, pdfv, dd, FALSE,
					error);

			return -1;
		}

		rc = pdf_viewer_session_save_finished(pdfv->session, pdfv, dd, TRUE,
				error);
		if (rc) {
			g_prefix_error(error, "%s\n", __func__);

			return -1;
		}
	} while ((dd = dd->next));

	pdf_viewer_session_save_done(pdfv->session, pdfv);

	/* dirty-Status aller Abschnitte der gespeicherten Dokumente aus dem (jetzt bereinigten)
	 * Journal neu herleiten; über "dirty-changed" schalten die Viewer ihren Speichern-Knopf. */
	document_reset_dirty(pdfv);

	return 0;
}
