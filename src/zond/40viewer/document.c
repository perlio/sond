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
		dd->first_page =
			zond_pdf_document_get_pdf_document_page(zond_pdf_document, 0);
		dd->first_index = 0; //überflüssig, wg. g_malloc0

		dd->last_page = zond_pdf_document_get_pdf_document_page(zond_pdf_document,
				zond_pdf_document_get_number_of_pages(zond_pdf_document) - 1);
		dd->last_index = EOP;
	}

	return dd;
}

