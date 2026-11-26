#include <gtk/gtk.h>

#include "viewer.h"
#include "document.h"
#include "zond_pdf_document.h"
#include "anbindung_zaehlung.h"
#include "pdf_annot_ops.h"

#include "../znd/anbindung.h"

#include"../sond_log_and_error.h"

void document_free_displayed_documents(DisplayedDocument *dd) {
	if (!dd)
		return;

	do {
		DisplayedDocument* next = NULL;

		zpdfd_part_drop(dd->zpdfd_part);
		next = dd->next;

		g_free(dd);

		dd = next;
	} while (dd);

	return;
}

/* Nicht mehr static, Deklaration in document.h - direkt von document.c
 * (hier) und zond_treeview.c genutzt (s. Doc-Kommentar dort). */
PdfPos get_pdf_pos(ZondPdfDocument* zpdfd, gboolean was_opened,
		Anbindung* anbindung_ges, Anbindung* anbindung_node, gboolean end) {
	PdfPos pdf_pos = { 0 };
	gint ges_von_seite = 0;
	gint ges_von_index = 0;
	gint ges_bis_seite = 0;
	gint node_von_seite = 0;
	gint node_von_index = 0;
	gint node_bis_seite = 0;
	gint node_bis_index = 0;


	if (anbindung_ges && !anbindung_is_empty(anbindung_ges)) {
		ges_von_seite = anbindung_ges->von.seite;
		ges_von_index = anbindung_ges->von.index;
		ges_bis_seite = anbindung_ges->bis.seite;
	}
	else
		ges_bis_seite = zond_pdf_document_get_number_of_pages(zpdfd) - 1;

	if (anbindung_node && !anbindung_is_empty(anbindung_node)) {
		node_von_seite = anbindung_node->von.seite;
		node_von_index = anbindung_node->von.index;
		node_bis_seite = anbindung_node->bis.seite;
		node_bis_index = anbindung_node->bis.index;
	}
	/* War anbindung_node leer, bleiben node_von_seite/node_von_index/
	 * node_bis_seite/node_bis_index korrekt auf ihrem Default 0 (s.
	 * Deklarationen oben) - kein else nötig. Hier stand fälschlich
	 * (Copy-Paste aus dem ges-Zweig oben) nochmal eine Zuweisung an
	 * ges_bis_seite, die dessen bereits korrekt gesetzten Wert (Ende der
	 * tatsächlichen anbindung_ges, falls diese nicht leer war) wieder mit
	 * der Gesamtseitenzahl des ganzen Dokuments überschrieben hat - führte
	 * beim Sprung ans Ende einer Anbindung (Zeile ~71: node_bis_seite == 0
	 * && node_bis_index == 0) zur falschen (zu späten) Seite. */

	if (!end) {
		pdf_pos.seite = node_von_seite - ges_von_seite;
		if (pdf_pos.seite == 0)
			pdf_pos.index = node_von_index - ges_von_index;
		else
			pdf_pos.index = node_von_index;
	} else {
		if (node_bis_seite || node_bis_index) {
			pdf_pos.seite = node_bis_seite - ges_von_seite;
			if (pdf_pos.seite == 0)
				pdf_pos.index = node_bis_index - ges_von_index;
			else
				pdf_pos.index = node_bis_index;
		} else {
			pdf_pos.seite = ges_bis_seite - ges_von_seite;
			pdf_pos.index = EOP;
		}
	}

	/* gelöschte Seiten herausrechnen (nur wenn PDF offen) */
	if (was_opened && pdf_pos.seite != EOP && pdf_pos.seite > 0) {
		guint bis_seite_abs = (guint) ges_von_seite + (guint) pdf_pos.seite;

		for (guint i = (guint) ges_von_seite; i < bis_seite_abs; i++) {
			PdfDocumentPage* pdfp = g_ptr_array_index(
					zond_pdf_document_get_arr_pages(zpdfd), i);
			if (pdfp && pdfp->deleted)
				pdf_pos.seite--;
		}
	}

	return pdf_pos;
}

DisplayedDocument*
document_new_displayed_document(SondFilePartPDF* sfp_pdf,
		Anbindung *anbindung_ges, Anbindung* anbindung_node, gboolean end,
		PdfPos* pdf_pos, GError **error) {
	ZondPdfDocument* zpdfd = NULL;
	ZPDFDPart* zpdfd_part = NULL;
	DisplayedDocument *dd = NULL;
	PdfPos pdf_pos_int = { 0 };

	zpdfd = zond_pdf_document_is_open(sfp_pdf);
	if (zpdfd) {
		if (anbindung_node && !anbindung_is_empty(anbindung_node))
			anbindung_aktualisieren(zpdfd, anbindung_node);
		if (anbindung_ges && !anbindung_is_empty(anbindung_ges))
			anbindung_aktualisieren(zpdfd, anbindung_ges);
	}

	zpdfd_part = zpdfd_part_peek(sfp_pdf, anbindung_ges, error);
	if (!zpdfd_part)
		return NULL;

	//Position node berechnen
	pdf_pos_int = get_pdf_pos(zpdfd_part->zond_pdf_document,
			(zpdfd != NULL), anbindung_ges, anbindung_node, end);

	dd = g_malloc0(sizeof(DisplayedDocument));
	dd->zpdfd_part = zpdfd_part;

	if (pdf_pos)
		*pdf_pos = pdf_pos_int;

	return dd;
}

/* Gehört der Journal-Eintrag zum Ausschnitt (Abschnitt) zpdfd_part? */
gboolean document_entry_in_dd(JournalEntry* entry,
		ZPDFDPart* zpdfd_part) {
	if (entry->pdf_document_page->page_akt >= zpdfd_part->first_page->page_akt &&
			entry->pdf_document_page->page_akt <= zpdfd_part->last_page->page_akt) {
		Annot* annot = NULL;

		if (entry->type == JOURNAL_TYPE_PAGES_INSERTED ||
				entry->type == JOURNAL_TYPE_PAGE_DELETED ||
				entry->type == JOURNAL_TYPE_ROTATE ||
				entry->type == JOURNAL_TYPE_OCR) return TRUE;

		/* Sind ja nur Annots übrig. Für die Randseiten-Zuordnung zählt die
		 * Position, die die Annotation zum Zeitpunkt dieses Journal-
		 * Eintrags tatsächlich hatte: bei CREATED/CHANGED ist das der
		 * aktuelle/neue Zustand (annot_after; annot_before ist bei CREATED
		 * gar nicht gesetzt). Bei DELETED gibt es keinen "nachher"-Zustand
		 * mehr (annot_after bleibt {0}) - maßgeblich ist hier annot_before,
		 * der letzte bekannte Zustand vor dem Löschen. */
		annot = (entry->type == JOURNAL_TYPE_ANNOT_DELETED) ?
				&entry->annot_changed.annot_before :
				&entry->annot_changed.annot_after;

		{
			gboolean is_first = FALSE;
			gboolean is_last = FALSE;
			gfloat y0 = 0.0;
			gfloat y1 = 0.0;

			is_first = (entry->pdf_document_page->page_akt ==
					zpdfd_part->first_page->page_akt);
			is_last = (entry->pdf_document_page->page_akt ==
					zpdfd_part->last_page->page_akt);

			//Seite mittendrin (weder erste noch letzte) - immer vollständig im dd
			if (!is_first && !is_last)
				return TRUE;

			/* Obere Grenze nur relevant, wenn diese Seite die erste ist,
			 * untere Grenze nur, wenn sie die letzte ist - eine Seite, die
			 * BEIDES ist (einseitiger Ausschnitt, oben UND unten
			 * beschnitten), bekommt beide Grenzen; eine Seite, die nur eine
			 * der beiden Rollen hat, nur die jeweils passende (auf der
			 * anderen Seite ist der dd nicht begrenzt, der Inhalt geht auf
			 * Nachbarseiten weiter). Vorher wurde bei is_first&&is_last
			 * (einseitiger Ausschnitt) wegen der if/else-if-Reihenfolge nur
			 * die obere Grenze angewandt, last_index komplett ignoriert. */
			y0 = (is_first && zpdfd_part->first_index > 0) ?
					(gfloat) zpdfd_part->first_index : 0.0;
			y1 = (is_last && zpdfd_part->last_index != EOP) ?
					(gfloat) zpdfd_part->last_index :
					entry->pdf_document_page->rect.y1;

			if (y0 == 0.0 && y1 == entry->pdf_document_page->rect.y1)
				return TRUE; //keine Beschneidung auf dieser Seite

			{
				fz_rect rect = { 0.0, y0, entry->pdf_document_page->rect.x1, y1 };

				if (pdf_annot_ops_is_in_rect(annot, rect))
					return 1;
			}
		}
	}

	return FALSE;
}

/* Ist mindestens ein angezeigter Abschnitt des Viewers geändert? */
gboolean document_has_dirty(PdfViewer* pv) {
	DisplayedDocument* dd = NULL;

	dd = pv->dd;
	do {
		if (dd->zpdfd_part->dirty)
			return TRUE;
	} while ((dd = dd->next));

	return FALSE;
}

/* dirty-Status aller Abschnitte der Dokumente, die der Viewer anzeigt, neu aus dem Journal
 * herleiten (auch die Abschnitte in anderen Viewern: das Journal gehört zum Dokument). */
void document_reset_dirty(PdfViewer* pdfv) {
	DisplayedDocument* dd = pdfv->dd;

	do {
		GPtrArray* arr_parts = zond_pdf_document_get_arr_parts(dd->zpdfd_part->zond_pdf_document);
		GArray* arr_journal = zond_pdf_document_get_arr_journal(dd->zpdfd_part->zond_pdf_document);

		for (guint i = 0; i < arr_parts->len; i++) {
			ZPDFDPart* zpdfd_part = g_ptr_array_index(arr_parts, i);
			gboolean in_dd = FALSE;

			for (guint u = 0; u < arr_journal->len; u++) {
				JournalEntry entry = g_array_index(arr_journal, JournalEntry, u);

				in_dd = document_entry_in_dd(&entry, zpdfd_part);
				if (in_dd)
					break;
			}

			zpdfd_part_set_dirty(zpdfd_part, in_dd);
		}
	} while ((dd = dd->next));

	return;
}
