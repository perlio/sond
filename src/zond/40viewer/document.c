#include <gtk/gtk.h>

#include "viewer.h"
#include "document.h"
#include "../zond_pdf_document.h"

#include "../99conv/general.h"

#include"../../sond_log_and_error.h"

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

static PdfPos get_pdf_pos(ZondPdfDocument* zpdfd, gboolean was_opened,
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
	else
		ges_bis_seite = zond_pdf_document_get_number_of_pages(zpdfd) - 1;

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
		for (guint i = ges_von_seite; i < (guint)pdf_pos.seite; i++) {
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
