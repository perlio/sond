/*
 sond (sond_tvfm_load.h) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_tvfm_load.h
 * @brief Welche Dateien eines Baums sind nicht geladene SeaDrive-Platzhalter? (GTK-frei)
 *
 * Gemeinsame Hilfen für Aktionen, die Dateien lesen (Öffnen, Kopieren, Suche): sie
 * stellen fest, welche Dateien erst geladen werden müssen. Das Laden selbst (Fortschritt,
 * Abbrechen) ist Sache der Oberfläche (sond_seadrive_load_sync()). Außerhalb von Windows
 * und ohne SeaDrive-Wurzel liefern alle Funktionen "nichts zu laden".
 */

#ifndef SOND_TVFM_LOAD_H_INCLUDED
#define SOND_TVFM_LOAD_H_INCLUDED

#include <glib.h>

#include "sond_tvfm_item.h"
#include "sond_tvfm_owner.h"

G_BEGIN_DECLS

/* Absoluter Pfad der Datei, die zum Filepart gehört: bei Teilen in Containern die
 * oberste Datei (ohne Parent); nur diese trägt in path den echten, projektrelativen Pfad.
 * g_free(). NULL (kein Fehler), wenn die Wurzel nicht in SeaDrive liegt, sfp NULL ist oder
 * kein Pfad zu ermitteln ist. */
gchar* sond_tvfm_load_root_file_path(SondTVFMOwner*, SondFilePart *sfp);

/* Ist die Datei des Fileparts ein nicht geladener Platzhalter? Löst keinen Download aus. */
gboolean sond_tvfm_load_not_loaded(SondTVFMOwner*, SondFilePart *sfp);

/* Fügt arr (GPtrArray mit g_free) die nicht geladenen Dateien hinzu, die item braucht:
 * bei einer Datei bzw. einem Teil davon die oberste Datei, bei einem Verzeichnis des
 * Dateisystems alle Dateien darunter. */
void sond_tvfm_load_collect(GPtrArray *arr, SondTVFMOwner*, SondTVFMItem *item);

G_END_DECLS

#endif // SOND_TVFM_LOAD_H_INCLUDED
