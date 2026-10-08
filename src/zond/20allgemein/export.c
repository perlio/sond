/*
 zond (export.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2020  pelo america

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU Affero General Public License as
 published by the Free Software Foundation, either version 3 of the
 License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU Affero General Public License for more details.

 You should have received a copy of the GNU Affero General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <gtk/gtk.h>

#include "../../sond_log_and_error.h"
#include "../../misc.h"

#include "export.h"
#include "export_selection.h"
#include "export_dialog.h"
#include "export_odt.h"
#include "export_docx.h"
#include "export_pdf.h"

//zuletzt gewählte Optionen, gelten bis zum Programmende
static ExportOptionen optionen = { EXPORT_FORMAT_ODT, FALSE, EXPORT_TIEFE_ALLE,
		TRUE, TRUE, FALSE, FALSE, TRUE, FALSE };

gint export_activate(Projekt *zond, GError **error) {
	Baum baum = KEIN_BAUM;
	const gchar *endung = NULL;
	gchar *vorschlag = NULL;
	gchar *filename = NULL;
	GPtrArray *eintraege = NULL;
	gint rc = 0;

	baum = export_selection_baum(zond);
	if (baum == KEIN_BAUM) {
		g_set_error(error, SOND_ERROR, 0,
				"Kein Baum ausgewählt - bitte zuerst im Inhalts- oder "
				"Auswertungsbaum arbeiten");

		return -1;
	}

	if (!export_dialog_run(zond, baum, &optionen))
		return 0;

	if (!optionen.nodetext && !optionen.text && !optionen.anbindung
			&& !optionen.pfad && !optionen.nummern && !optionen.dokumente) {
		g_set_error(error, SOND_ERROR, 0, "Nichts zum Exportieren gewählt");

		return -1;
	}

	if (optionen.format == EXPORT_FORMAT_PDF)
		endung = ".pdf";
	else if (optionen.format == EXPORT_FORMAT_DOCX)
		endung = ".docx";
	else
		endung = ".odt";

	vorschlag = g_strdup_printf("%s%s",
			zond->project_name ? zond->project_name : "Export", endung);
	filename = filename_speichern(GTK_WINDOW(zond->app_window), "Datei wählen",
			vorschlag);
	g_free(vorschlag);
	if (!filename)
		return 0;

	if (!g_str_has_suffix(filename, endung)) {
		gchar *neu = g_strconcat(filename, endung, NULL);

		g_free(filename);
		filename = neu;
	}

	eintraege = export_selection_build(zond, baum, &optionen, error);
	if (!eintraege) {
		g_free(filename);

		return -1;
	}

	if (optionen.format == EXPORT_FORMAT_PDF)
		rc = export_pdf_schreiben(zond, eintraege, &optionen, filename, error);
	else if (optionen.format == EXPORT_FORMAT_DOCX)
		rc = export_docx_schreiben(zond, eintraege, &optionen, filename, error);
	else
		rc = export_odt_schreiben(zond, eintraege, &optionen, filename, error);

	g_ptr_array_unref(eintraege);
	g_free(filename);

	return rc;
}
