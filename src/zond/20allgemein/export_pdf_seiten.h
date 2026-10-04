#ifndef EXPORT_PDF_SEITEN_H_INCLUDED
#define EXPORT_PDF_SEITEN_H_INCLUDED

#include <glib.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

/* Seitenbereich einer Quell-PDF. von und bis sind nullbasierte Seiten
 * (einschließlich). y0/y1 sind Zuschnittgrenzen in Seitenkoordinaten (wie
 * der Index einer Anbindung: von oben, in Punkt, gedrehte Seite); negativ
 * heißt kein Zuschnitt. y0 gilt auf der ersten, y1 auf der letzten Seite. */
typedef struct _ExportPdfBereich {
	gint von;
	gint bis;
	gdouble y0;
	gdouble y1;
	//je Seite von..bis: 1 = nicht ausgeben (im Viewer gelöscht); darf NULL sein
	guint8 *auslassen;
} ExportPdfBereich;

/* Hängt die Seiten des Bereichs an dest an. Ein Zuschnitt setzt nur die
 * CropBox - der abgeschnittene Inhalt bleibt in der Datei, ist aber nicht
 * sichtbar. map muss zu dest gehören (gemeinsame Ressourcen werden pro
 * Quelle nur einmal kopiert). */
gint export_pdf_seiten_kopieren(fz_context *ctx, pdf_document *dest,
		pdf_document *src, pdf_graft_map *map, const ExportPdfBereich *bereich,
		GError **error);

/* Fortlaufender Satz auf A4-Seiten: HTML-Fragmente und Bilder folgen
 * untereinander, eine Seite wird erst abgeschlossen, wenn der nächste
 * Inhalt nicht mehr hineinpasst oder export_pdf_satz_schliessen() es
 * verlangt (vor dem Anhängen fremder Seiten, am Ende). Klassen für p:
 * pfad, anb, hinweis, dok (Dokumenttext). */
typedef struct _ExportPdfSatz ExportPdfSatz;

ExportPdfSatz* export_pdf_satz_new(fz_context *ctx, pdf_document *dest);

/* Verwirft eine noch offene Seite */
void export_pdf_satz_free(ExportPdfSatz *satz);

/* Setzt html (HTML-Fragment) ab der aktuellen Position; ein langer Text
 * läuft über mehrere Seiten. */
gint export_pdf_satz_html(ExportPdfSatz *satz, const gchar *html,
		GError **error);

/* Setzt ein Bild (png oder jpeg) ab der aktuellen Position, auf die
 * nächste Seite, wenn es auf der aktuellen nicht mehr passt.
 * breite_cm/hoehe_cm ist die natürliche Größe; zu große Bilder werden auf
 * den Satzspiegel verkleinert, nie vergrößert. */
gint export_pdf_satz_bild(ExportPdfSatz *satz, const guchar *data, gsize len,
		gdouble breite_cm, gdouble hoehe_cm, GError **error);

/* Schließt die offene Seite ab (ohne offene Seite ohne Wirkung) */
gint export_pdf_satz_schliessen(ExportPdfSatz *satz, GError **error);

#endif // EXPORT_PDF_SEITEN_H_INCLUDED
