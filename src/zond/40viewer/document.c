#include <gtk/gtk.h>

#include "viewer.h"
#include "document.h"
#include "../zond_pdf_document.h"

#include "../99conv/general.h"
#include "../99conv/pdf.h"

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

DisplayedDocument*
document_new_displayed_document(SondFilePartPDF* sfp_pdf,
		Anbindung *anbindung_akt, GError **error) {
	ZPDFDPart* zpdfd_part = NULL;
	DisplayedDocument *dd = NULL;

	zpdfd_part = zpdfd_part_peek(sfp_pdf, anbindung_akt, error);
	if (!zpdfd_part)
		ERROR_Z_VAL(NULL)

	dd = g_malloc0(sizeof(DisplayedDocument));
	dd->zpdfd_part = zpdfd_part;

	return dd;
}
