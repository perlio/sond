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

/* Setzt html (HTML-Fragment) auf A4-Seiten und hängt diese an dest an.
 * Ein langer Text läuft über mehrere Seiten. */
gint export_pdf_infoseiten(fz_context *ctx, pdf_document *dest,
		const gchar *html, GError **error);

#endif // EXPORT_PDF_SEITEN_H_INCLUDED
