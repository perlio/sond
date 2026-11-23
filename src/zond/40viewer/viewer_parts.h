#ifndef VIEWER_PARTS_H_INCLUDED
#define VIEWER_PARTS_H_INCLUDED

//Speichern-Knopf ist aktiv, wenn ein angezeigter Abschnitt geändert ist
void viewer_speichern_aktualisieren(PdfViewer*);

//Abschnitte des Viewers abonnieren (nach dem Setzen von pv->dd)
void viewer_dd_signale_verbinden(PdfViewer*);

//Abonnement der Abschnitte beenden, vor document_free_displayed_documents()
void viewer_dd_signale_trennen(PdfViewer*);

//Fenster eines Viewers, der den Abschnitt anzeigt, nach vorn holen
void viewer_part_praesentieren(PdfViewerSession*, ZPDFDPart*);

#endif /* VIEWER_PARTS_H_INCLUDED */
