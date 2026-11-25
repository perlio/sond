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
 * @brief Widget "Dokumentenverzeichnis" einer Akte
 *
 * Zeigt das Verzeichnis der Akte im SeaDrive-Laufwerk als Baum (SondTreeviewFM)
 * und erlaubt, Dateien und Ordner dauerhaft offline verfügbar zu machen
 * ("Immer offline halten"). Ein Schalter hält die Projektdateien (Projektdatei,
 * Index) offline, damit zond unterwegs arbeitet.
 */

#ifndef SOND_DOKUMENTE_H
#define SOND_DOKUMENTE_H

#include <gtk/gtk.h>

#include "../../sond_client.h"
#include "sond_akte_lease.h"

G_BEGIN_DECLS

/* Baut das Widget "Dokumentenverzeichnis" für die Akte (lfd_nr/year): Kopfzeile, Schalter für die
 * Offline-Haltung und der Dateibaum. Der Aufrufer setzt es in ein Fenster oder eine Seite ein und
 * zerstört es, wenn die Akte nicht mehr gezeigt wird (dabei räumt es Timer, Datenbanken und
 * Passwörter auf). NULL mit fehler, wenn SeaDrive oder die Library nicht erreichbar ist oder die
 * Akte schon in einem Widget gezeigt wird. lease und akte_id (0 = unbekannt) brauchen die Übergabe
 * der Schreibberechtigung an einen Index-Job; die Lease gehört dem Aufrufer und muss das Widget
 * überdauern. */
GtkWidget* sond_dokumente_widget_new(SondClient *client, SondAkteLease *lease,
        guint lfd_nr, guint year, gint64 akte_id, GError **fehler);

/* Sperre des Baums sofort neu bestimmen (läuft ein Index-Job oder zond mit der Akte, sind
 * Änderungen ausgegraut). Das Widget tut das auch selbst alle 5 Sekunden. */
void sond_dokumente_sperre_pruefen(GtkWidget *widget);

/* Läuft im Widget ein Index-Job? Dann darf die Akte nicht gewechselt werden. */
gboolean sond_dokumente_busy(GtkWidget *widget);

G_END_DECLS

#endif /* SOND_DOKUMENTE_H */
