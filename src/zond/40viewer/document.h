#ifndef DOCUMENT_H_INCLUDED
#define DOCUMENT_H_INCLUDED

#include "../zond_pdf_document.h"

typedef int gint;
typedef char gchar;

typedef struct _Pdf_Document_Page PdfDocumentPage;
typedef struct _Anbindung Anbindung;
typedef struct _Pdf_Viewer PdfViewer;
typedef struct _SondFilePartPDF SondFilePartPDF;

typedef struct _Displayed_Document {
	ZPDFDPart* zpdfd_part;
	DisplayedDocument* next;
} DisplayedDocument;

void document_free_displayed_documents(DisplayedDocument*);

DisplayedDocument* document_new_displayed_document(SondFilePartPDF*, Anbindung*,
		GError**);

	#endif // DOCUMENT_H_INCLUDED
