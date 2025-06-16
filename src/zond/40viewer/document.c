#include <gtk/gtk.h>

#include "viewer.h"
#include "document.h"
#include "../zond_pdf_document.h"

#include "../99conv/general.h"
#include "../99conv/pdf.h"

#include "../global_types.h"

#include"../../misc.h"

void document_free_displayed_documents(DisplayedDocument *dd) {
	if (!dd)
		return;

	do {
		DisplayedDocument *next = dd->next;

		zond_pdf_document_close(dd->zond_pdf_document);
		g_free(dd);

		dd = next;
	} while (dd);

	return;
}

DisplayedDocument*
document_new_displayed_document(const gchar *file_part, Anbindung *anbindung,
		GError **error) {
	ZondPdfDocument *zond_pdf_document = NULL;
	DisplayedDocument *dd = NULL;
	gchar* errmsg = NULL;

	zond_pdf_document = zond_pdf_document_open(file_part,
			(anbindung) ? anbindung->von.seite : 0,
			(anbindung) ? anbindung->bis.seite : -1, &errmsg);
	if (!zond_pdf_document) {
		if (errmsg) {
			if (error) *error = g_error_new(ZOND_ERROR, 0, "%s\n%s", __func__, errmsg);
			g_free(errmsg);

			return NULL;
		}
		else
			return NULL; //Fehler: Passwort funktioniert nicht
	}

	dd = g_malloc0(sizeof(DisplayedDocument));
	dd->zond_pdf_document = zond_pdf_document;

	if (anbindung) {
		dd->first_page =
				zond_pdf_document_get_pdf_document_page(zond_pdf_document, anbindung->von.seite);
		dd->first_index = anbindung->von.index;

		dd->last_page =
				zond_pdf_document_get_pdf_document_page(zond_pdf_document, anbindung->bis.seite);
		dd->last_index = anbindung->bis.index;
	} else {
		dd->first_page =
			zond_pdf_document_get_pdf_document_page(zond_pdf_document, 0);
		dd->first_index = 0; //überflüssig, wg. g_malloc0

		dd->last_page = zond_pdf_document_get_pdf_document_page(zond_pdf_document,
				zond_pdf_document_get_number_of_pages(zond_pdf_document) - 1);
		dd->last_index = EOP;
	}

	return dd;
}
/*
gint document_oeffnen_internal_viewer(Projekt *zond,
		const gchar *file_part, Anbindung *anbindung, PdfPos *pos_pdf,
		gchar **errmsg) {
	PdfPos pos_von = { 0 };
	ZondPdfDocument const* zpdfd = NULL;
	gint rc = 0;
	GError *error = NULL;

	if (anbindung && (zpdfd = zond_pdf_document_is_open(file_part)))
		anbindung_aktualisieren_insert_pages(zpdfd, anbindung);

	//Neue Instanz oder bestehende?
	if (!(zond->state & GDK_SHIFT_MASK)) {
		//Testen, ob pv mit file_part schon geöffnet
		for (gint i = 0; i < zond->arr_pv->len; i++) {
			PdfViewer *pv = g_ptr_array_index(zond->arr_pv, i);
			if (pv->dd->next == NULL && !g_strcmp0(file_part,
					zond_pdf_document_get_file_part(pv->dd->zond_pdf_document))) {
				Anbindung* anbindung_dd = NULL;

				anbindung_dd = document_get_anbindung(pv->dd);

				if ((!anbindung && !anbindung_dd) ||
						(anbindung && anbindung_dd &&
						anbindung_1_gleich_2(*anbindung, *anbindung_dd))) {

					if (pos_pdf)
						pos_von = *pos_pdf;

					gtk_window_present(GTK_WINDOW(pv->vf));

					if (pos_von.seite > (pv->arr_pages->len - 1))
						pos_von.seite = pv->arr_pages->len - 1;

					viewer_springen_zu_pos_pdf(pv, pos_von, 0.0);

					g_free(anbindung_dd);

					return 0;
				}
				else g_free(anbindung_dd);
			}
		}
	}

	DisplayedDocument *dd = document_new_displayed_document(file_part,
			anbindung, errmsg);
	if (!dd && *errmsg)
		ERROR_S
	else if (!dd)
		return 0;

	if (pos_pdf)
		pos_von = *pos_pdf;

	PdfViewer *pv = viewer_start_pv(zond);
	rc = viewer_display_document(pv, dd, pos_von.seite, pos_von.index, &error);
	if (rc) {
		if (errmsg)
				*errmsg = g_strdup_printf("%s\n%s", __func__, error->message);
		g_error_free(error);
		document_free_displayed_documents(dd);

		return -1;
	}

	return 0;
}
*/
Anbindung* document_get_anbindung(DisplayedDocument* dd) {
	gint seite_von = 0;
	gint index_von = 0;
	gint seite_bis = 0;
	gint index_bis = 0;
	Anbindung* anbindung = NULL;

	seite_von = pdf_document_page_get_index(dd->first_page);
	seite_bis = pdf_document_page_get_index(dd->last_page);
	index_von = dd->first_index;
	index_bis = dd->last_index;

	if (seite_von == 0 && index_von == 0 &&
			seite_bis == zond_pdf_document_get_number_of_pages(dd->zond_pdf_document) - 1 &&
			index_bis == EOP) return NULL;

	anbindung = g_malloc0(sizeof(Anbindung));

	anbindung->von.seite = seite_von;
	anbindung->von.index = index_von;
	anbindung->bis.seite = seite_bis;
	anbindung->bis.index = index_bis;

	return anbindung;
}

