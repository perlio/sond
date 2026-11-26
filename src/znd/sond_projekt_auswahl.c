/*
 sond (sond_projekt_auswahl.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2026  pelo america

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

#include "sond_projekt_auswahl.h"

#include <gio/gio.h>

#include "anbindung.h"

gint sond_projekt_section_range(gchar const *section, gpointer data, SondPageRange **range,
		GError **error) {
	SondProjektSectionRegeln const *regeln = data;
	Anbindung anbindung = { 0 };

	anbindung_parse_file_section(section, &anbindung);

	if (regeln && regeln->reject_unterseitig && anbindung_ist_unterseitig(anbindung)) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
				"Die Auswahl enthält eine unterseitige Anbindung (beginnt/endet nicht an einer "
				"Seitengrenze) - das ist für Indizierung/Index löschen nicht zulässig.");

		return -1;
	}

	*range = sond_page_range_new(anbindung.von.seite, anbindung_bis_seite(anbindung));

	return 0;
}
