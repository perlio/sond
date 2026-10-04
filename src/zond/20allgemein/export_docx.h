#ifndef EXPORT_DOCX_H_INCLUDED
#define EXPORT_DOCX_H_INCLUDED

#include <glib.h>

#include "export_selection.h"

/* Schreibt die Eintragsliste als docx-Dokument (ZIP mit XML, ohne Office).
 * Aufbau wie export_odt_schreiben(). Rückgabe 0 oder -1 mit *error. */
gint export_docx_schreiben(Projekt *zond, GPtrArray *eintraege,
		const ExportOptionen *opt, const gchar *filename, GError **error);

#endif // EXPORT_DOCX_H_INCLUDED
