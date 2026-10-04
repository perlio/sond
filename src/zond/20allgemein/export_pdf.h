#ifndef EXPORT_PDF_H_INCLUDED
#define EXPORT_PDF_H_INCLUDED

#include <glib.h>

#include "export_selection.h"

/* Schreibt die Eintragsliste als PDF: je Knoten Infoseite(n), danach die
 * Seiten des angebundenen Dokuments (opt->dokumente). Nicht darstellbare
 * Dokumente erscheinen als Hinweis auf der Infoseite. Rückgabe 0 oder -1
 * mit *error. */
gint export_pdf_schreiben(Projekt *zond, GPtrArray *eintraege,
		const ExportOptionen *opt, const gchar *filename, GError **error);

#endif // EXPORT_PDF_H_INCLUDED
