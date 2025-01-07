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
		gchar **errmsg) {
	ZondPdfDocument *zond_pdf_document = NULL;
	DisplayedDocument *dd = NULL;

	zond_pdf_document = zond_pdf_document_open(file_part,
			(anbindung) ? anbindung->von.seite : 0,
			(anbindung) ? anbindung->bis.seite : -1, errmsg);
	if (!zond_pdf_document) {
		if (errmsg && *errmsg)
			ERROR_S_MESSAGE_VAL("zond_pdf_document_open", NULL)
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
		dd->first_page = 0; //überflüssig, wg. g_malloc0
		dd->first_index = 0; //dto.

		dd->last_page = zond_pdf_document_get_pdf_document_page(zond_pdf_document,
				zond_pdf_document_get_number_of_pages(zond_pdf_document) - 1);
		dd->last_index = EOP;
	}

	return dd;
}

static gint document_get_num_of_pages_of_dd(DisplayedDocument *dd) {
	gint anz_seiten = 0;

	anz_seiten = pdf_document_page_get_index(dd->last_page)-
			pdf_document_page_get_index(dd->last_page) + 1;

	return anz_seiten;
}

DisplayedDocument*
document_get_dd(PdfViewer *pv, gint page, PdfDocumentPage **pdf_document_page) {
	gint zaehler = 0;
	DisplayedDocument *dd = NULL;

	dd = pv->dd;

	do {
		gint pages_dd = 0;

		pages_dd = document_get_num_of_pages_of_dd(dd);

		if (page < zaehler + pages_dd) {
			gint von = 0;

			von = pdf_document_page_get_index(dd->first_page);

			if (pdf_document_page)
				*pdf_document_page = g_ptr_array_index(
						zond_pdf_document_get_arr_pages(dd->zond_pdf_document),
						page - zaehler + von);
/*
			if (page_dd)
				*page_dd = page - zaehler;
			if (page_doc)
				*page_doc = page - zaehler + von;
*/
			return dd;
		}

		zaehler += pages_dd;
	} while ((dd = dd->next));

	return NULL;
}

