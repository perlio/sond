#ifndef EXPORT_ODT_H_INCLUDED
#define EXPORT_ODT_H_INCLUDED

#include <glib.h>

#include "export_selection.h"

/* Schreibt die Eintragsliste als odt-Dokument (ZIP mit XML, ohne
 * LibreOffice). Rückgabe 0 oder -1 mit *error. */
gint export_odt_schreiben(Projekt *zond, GPtrArray *eintraege,
		const ExportOptionen *opt, const gchar *filename, GError **error);

#endif // EXPORT_ODT_H_INCLUDED
