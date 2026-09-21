#ifndef DOCUMENT_H_INCLUDED
#define DOCUMENT_H_INCLUDED

typedef int gint;
typedef char gchar;

typedef struct _Pdf_Document_Page PdfDocumentPage;
typedef struct _ZPDFD_Part ZPDFDPart;
typedef struct _Anbindung Anbindung;
typedef struct _Pdf_Viewer PdfViewer;
typedef struct _SondFilePartPDF SondFilePartPDF;
typedef struct _Pdf_Pos PdfPos;
typedef struct _ZondPdfDocument ZondPdfDocument;

typedef struct _Displayed_Document {
	ZPDFDPart* zpdfd_part;
	DisplayedDocument* next;
} DisplayedDocument;

void document_free_displayed_documents(DisplayedDocument*);

DisplayedDocument*
document_new_displayed_document(SondFilePartPDF* sfp_pdf,
		Anbindung *anbindung_ges, Anbindung* anbindung_node, gboolean end,
		PdfPos* pdf_pos, GError **error);

/* Position von anbindung_node innerhalb von anbindung_ges (inkl.
 * Bereinigung um gelöschte Seiten, falls was_opened - s. Doc-Kommentar an
 * der Definition, document.c). zpdfd muss vom Aufrufer bereits offen
 * vorliegen (z.B. aus einem schon existierenden DisplayedDocument->
 * zpdfd_part->zond_pdf_document); was_opened muss der Aufrufer VOR dem
 * eigenen Öffnen dieses zpdfd separat ermittelt haben
 * (zond_pdf_document_is_open() wäre hier sonst immer TRUE, weil der
 * Aufrufer das Dokument ja selbst gerade geöffnet hat). */
PdfPos get_pdf_pos(ZondPdfDocument* zpdfd, gboolean was_opened,
		Anbindung* anbindung_ges, Anbindung* anbindung_node, gboolean end);

	#endif // DOCUMENT_H_INCLUDED
