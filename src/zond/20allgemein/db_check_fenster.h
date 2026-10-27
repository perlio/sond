/*
 zond (db_check_fenster.h) - Akten, Beweisstücke, Unterlagen
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

#ifndef DB_CHECK_FENSTER_H_INCLUDED
#define DB_CHECK_FENSTER_H_INCLUDED

typedef struct _Projekt Projekt;

/* Extras, Datenbank prüfen: nicht-modales Fenster mit den Befunden von
 * db_check_ausfuehren() in einem Baum (Elternknoten, Zweige, Knoten). Doppelklick
 * springt zum Knoten, "Reparieren" und "Verwerfen" rufen db_aktion_ausfuehren()
 * für die gewählten Zweige auf. Ist das Fenster schon offen, wird es nach vorn
 * geholt. */
void db_check_fenster_oeffnen(Projekt*);

/* Beim Öffnen eines Projekts, NACH dem Laden der Bäume: Zeigt das Fenster
 * (nicht modal) neben dem Baum, wenn die Prüfung (ohne Integrität) Mängel mit
 * wählbarer Aktion ergibt; sonst passiert nichts. */
void db_check_fenster_oeffnen_wenn_maengel(Projekt*);

/* Beim Öffnen eines Projekts, VOR dem Laden der Bäume - nur nötig, wenn das
 * Laden an einem defekten Eintrag scheitert: Zeigt das Fenster modal, wenn die
 * Prüfung Mängel mit wählbarer Aktion ergibt, und kehrt nach dem Schließen
 * zurück. Rückgabe TRUE, wenn etwas geändert wurde (das Projekt gilt dann als
 * ungespeichert). */
int db_check_fenster_vor_laden(Projekt*);

//beim Schließen des Projekts: Fenster schließen, falls offen
void db_check_fenster_schliessen(void);

#endif // DB_CHECK_FENSTER_H_INCLUDED
