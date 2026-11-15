/*
 sond (sond_dokumente.h) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_dokumente.h
 * @brief Fenster "Dokumentenverzeichnis" einer Akte
 *
 * Zeigt das Verzeichnis der Akte im SeaDrive-Laufwerk als Baum (SondTreeviewFM)
 * und erlaubt, Dateien und Ordner dauerhaft offline verfügbar zu machen
 * ("Immer offline halten"). Ein Schalter hält die Projektdateien (Projektdatei,
 * Index) offline, damit zond unterwegs arbeitet.
 */

#ifndef SOND_DOKUMENTE_H
#define SOND_DOKUMENTE_H

#include <gtk/gtk.h>

#include "../sond_client.h"

G_BEGIN_DECLS

/* Öffnet das Fenster für die Akte (lfd_nr/year); bezeichnung (nullable) für die
 * Titelzeile. Meldungen bei Fehlern (SeaDrive oder Library nicht gefunden) kommen
 * als Dialog über parent. */
void sond_dokumente_show(SondClient *client, GtkWindow *parent, guint lfd_nr,
        guint year, const gchar *bezeichnung);

G_END_DECLS

#endif /* SOND_DOKUMENTE_H */
