/*
 sond (sond_tvfm_fileparts.h) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_tvfm_fileparts.h
 * @brief Auswahl im Dateibaum -> Fileparts mit Seitenbereichen (GTK-frei)
 *
 * Aus einem Eintrag des Dateibaums (Datei, Verzeichnis, Teil in einem Container, Section)
 * entsteht die Hashtabelle, die sond_process_fileparts() erwartet: Schlüssel SondFilePart
 * (eigene Referenz), Wert SondPageRange* (NULL = ganze Datei). Anlegen mit
 * g_hash_table_new_full(NULL, NULL, g_object_unref, sond_page_range_free).
 *
 * Die Auswahl selbst (markierte Zeilen) sammelt die Ansicht; hier steht die Auflösung eines
 * einzelnen Eintrags. Was eine Section bedeutet, weiß nur der Besitzer des Baums (bei zond und
 * im Projekt-Baum ein Seitenbereich einer Anbindung): er liefert es über section_range.
 */

#ifndef SOND_TVFM_FILEPARTS_H_INCLUDED
#define SOND_TVFM_FILEPARTS_H_INCLUDED

#include <glib.h>

#include "../sond_index.h"
#include "../sond_process_file.h"
#include "sond_tvfm_item.h"

G_BEGIN_DECLS

typedef struct {
	gchar const *root; //Projektverzeichnis für den readdir-Scanner (Pflicht bei Verzeichnissen)
	SondIndexCtx *index_ctx; //optional: für skip_fully_covered
	gboolean skip_fully_covered; //vollständig indizierte Verzeichnisse überspringen

	/* Deutung einer Section durch den Besitzer. Rückgabe 0 oder -1 (error gesetzt); *range NULL =
	 * ganze Datei. Fehlt die Funktion, ist eine Section ein Fehler. */
	gint (*section_range)(gchar const *section, gpointer user_data, SondPageRange **range,
			GError **error);
	gpointer section_data;
} SondTVFMFilepartsOpts;

/* Fügt die Fileparts des Eintrags in ht ein (Vereinigung der Seitenbereiche derselben Datei).
 * 0 bei Erfolg, -1 bei Fehler (ht bleibt teilweise gefüllt). */
gint sond_tvfm_item_get_fileparts(SondTVFMItem *item, GHashTable *ht,
		SondTVFMFilepartsOpts const *opts, GError **error);

/* Alle indizierbaren Dateien unterhalb von rel_dir (NULL = Wurzel) per readdir, ohne Inhalts-
 * Sniffing und ohne in Container zu steigen. */
gint sond_tvfm_fileparts_readdir(gchar const *rel_dir, GHashTable *ht,
		SondTVFMFilepartsOpts const *opts, GError **error);

G_END_DECLS

#endif /* SOND_TVFM_FILEPARTS_H_INCLUDED */
