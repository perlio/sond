/*
 sond (sond_module_projekt.h) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_module_projekt.h
 * @brief Seite "Projekt": Dokumentenverzeichnis und Projektdaten einer Akte
 *
 * Die Seite hat ein eigenes Auswahlfeld für die Akte (Registernummer oder Suchtext,
 * arbeitet ohne Server auf den Libraries im SeaDrive-Laufwerk) und zeigt darunter das
 * Dokumentenverzeichnis der gewählten Akte (sond_dokumente_widget_new()). Es wird immer
 * genau eine Akte gezeigt.
 */

#ifndef SOND_MODULE_PROJEKT_H
#define SOND_MODULE_PROJEKT_H

#include <gtk/gtk.h>

#include "../../sond_client.h"
#include "sond_akte_lease.h"

G_BEGIN_DECLS

/**
 * sond_module_projekt_new:
 * @client: SondClient
 *
 * Das Modul verwaltet die Schreibberechtigung (Lease) der Akten: Es legt sie an, startet den
 * Hintergrundtakt und zeigt Stand und "Freigeben" der gezeigten Akte.
 *
 * Returns: (transfer full): Widget für den Stack des Hauptfensters
 */
GtkWidget* sond_module_projekt_new(SondClient *client);

/**
 * sond_module_projekt_focus_entry:
 * @module: das Widget aus sond_module_projekt_new()
 *
 * Setzt den Fokus auf das Auswahlfeld der Akte, sofern es bedienbar ist.
 */
void sond_module_projekt_focus_entry(GtkWidget *module);

G_END_DECLS

#endif /* SOND_MODULE_PROJEKT_H */
