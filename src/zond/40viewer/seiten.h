#ifndef SEITEN_H_INCLUDED
#define SEITEN_H_INCLUDED

typedef struct _GtkMenuItem GtkMenuItem;

typedef void *gpointer;
typedef struct _GPtrArray GPtrArray;
typedef struct _Pdf_Viewer PdfViewer;
typedef struct _Pdf_Document_Page PdfDocumentPage;
typedef struct _Viewer_Page_New ViewerPageNew;
typedef struct _Displayed_Document DisplayedDocument;
typedef char gchar;
typedef int gboolean;
typedef int gint;

/* Fragt die Seiten ab (Dialog); NULL bei Abbruch. winkel: nur beim Drehen.
 * Liefert PdfDocumentPage* (Freigabe mit g_ptr_array_unref). */
GPtrArray* seiten_abfrage_seiten(PdfViewer*, const gchar *title, gint *winkel,
		gboolean);

//Dialog "Seiten einfügen": liefert 1 (Datei) oder 2 (Clipboard) und in *num die Seite, nach der
//eingefügt wird; -1 bei Abbruch oder ungültiger Eingabe
gint seiten_abfrage_seitenzahl(PdfViewer*, unsigned int *num);

//Die im Vorschaubaum markierten Seiten (Indizes im Viewer, gint); NULL, wenn keine markiert
struct _GArray* seiten_markierte_thumbs(PdfViewer*);

//PdfDocumentPage* der Viewer-Seiten (Indizes arr_seiten_pv), ohne Doppelte; Freigabe g_ptr_array_unref
GPtrArray* seiten_get_document_pages(PdfViewer*, struct _GArray *arr_seiten_pv);

/* Nachführung nach einer Drehung der Seite (Seitenobjekt und pdf_document_page->rotate sind schon
 * angepasst): neu laden, Journal-Eintrag, Anzeige. winkel: -90, 90 oder 180. */
void seiten_drehung_nachfuehren(PdfViewer*, PdfDocumentPage*, gint winkel);

//Nachführung in einem Viewer, wenn die Seite des Abschnitts gedreht wurde (winkel in daten)
gint seiten_viewer_drehen(PdfViewer*, ViewerPageNew*, gint page_pv, gpointer daten);

//Daten für das Einfügen von Seiten in die Viewer (seiten_viewer_einfuegen)
typedef struct _DataInsert {
	DisplayedDocument* dd;
	gint page_doc;
	gint count;
	gboolean after_last;
} DataInsert;

/* Nachführung in einem Viewer, wenn Seiten eingefügt wurden (daten: DataInsert); Rückgabe 1,
 * wenn der Viewer geändert wurde */
gint seiten_viewer_einfuegen(PdfViewer*, ViewerPageNew*, gint page_pv, gpointer daten);

void cb_pv_seiten_loeschen(GtkMenuItem*, gpointer);

void cb_pv_seiten_einfuegen(GtkMenuItem*, gpointer);

void cb_pv_seiten_drehen(GtkMenuItem*, gpointer);

void cb_seiten_ausschneiden(GtkMenuItem*, gpointer);

void cb_seiten_kopieren(GtkMenuItem*, gpointer);

#endif // SEITEN_H_INCLUDED
