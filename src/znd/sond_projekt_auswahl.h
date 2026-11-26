/*
 sond (sond_projekt_auswahl.h) - Akten, Beweisstücke, Unterlagen
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

/**
 * @file sond_projekt_auswahl.h
 * @brief Deutung einer Section (Anbindung) bei der Auswahl für Index-Operationen (GTK-frei)
 *
 * Passt zu SondTVFMFilepartsOpts.section_range (tvfm/sond_tvfm_fileparts.h): in Projekt-Bäumen
 * ist eine Section ein Seitenbereich einer Anbindung.
 */

#ifndef SOND_PROJEKT_AUSWAHL_H_INCLUDED
#define SOND_PROJEKT_AUSWAHL_H_INCLUDED

#include <glib.h>

#include "../sond_process_file.h"

G_BEGIN_DECLS

typedef struct {
	/* Unterseitige Anbindungen (beginnen/enden nicht an einer Seitengrenze) ablehnen: für
	 * Indizieren und Index löschen nicht zulässig, s. anbindung_ist_unterseitig() */
	gboolean reject_unterseitig;
} SondProjektSectionRegeln;

//section_range-Funktion für SondTVFMFilepartsOpts; section_data = SondProjektSectionRegeln* (oder NULL)
gint sond_projekt_section_range(gchar const *section, gpointer regeln, SondPageRange **range,
		GError **error);

G_END_DECLS

#endif /* SOND_PROJEKT_AUSWAHL_H_INCLUDED */
