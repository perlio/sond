/*
 zond (zond_eingang.h) - Akten, Beweisstücke, Unterlagen
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

#ifndef ZOND_EINGANG_H_INCLUDED
#define ZOND_EINGANG_H_INCLUDED

#include <glib.h>

typedef struct _Projekt Projekt;

/* Eingang für die im Dateiverzeichnis markierten Dateien und Ordner setzen:
 * fragt Datum (Feld und Kalender) und Art ab, legt einen Zettel
 * (quelle "manuell") in .sond/eingang der Library ab, trägt ihn in die lokale
 * Nachschlagetabelle ein und frischt den Dateibaum auf. Einträge in
 * Containern (ZIP, E-Mail, PDF-Anhänge) lassen sich nicht stempeln. */
void zond_eingang_setzen(Projekt *zond);

/* Eigene Eingangsangaben der markierten Einträge aufheben; sie erben danach
 * wieder vom übergeordneten Ordner. Gleiche Ablage wie beim Setzen. */
void zond_eingang_entfernen(Projekt *zond);

/* Ein Eintrag des Dateisystems wurde gelöscht: dessen eigene Eingangsangaben und
 * die aller Einträge darunter werden aufgehoben (Zettel mit quelle "loeschen").
 * Still; ein Fehlschlag wird nur protokolliert, die Löschung selbst bleibt
 * gültig (zurückbleibende Stempel sind harmlos, bis dort wieder etwas liegt). */
void zond_eingang_geloescht(Projekt *zond, const gchar *rel_path);

/* Ein Eintrag des Dateisystems wurde verschoben oder kopiert (neu = Pfad am
 * Ziel, NULL = das Ziel liegt in einem Container). Die Stempel des Eintrags und
 * der Einträge darunter ziehen mit (Verschieben) bzw. werden mitkopiert
 * (Kopieren). Hat der Eintrag keinen eigenen Stempel und würde am Ziel etwas
 * anderes erben, wird der bisher wirksame Stempel am Ziel festgeschrieben.
 * Wandert ein Eintrag in einen Container, entfallen seine Stempel (mit
 * Hinweis). Rückgabe: TRUE, wenn sich Eingangsangaben geändert haben. */
gboolean zond_eingang_verschoben(Projekt *zond, const gchar *alt,
        const gchar *neu, gboolean ist_ordner, gboolean kopiert);

/* Ein Eintrag des Dateisystems wurde umbenannt: die Stempel des Eintrags und
 * der Einträge darunter ziehen auf den neuen Pfad um (Zettel mit quelle
 * "umbenennen"; Datum und Art bleiben). Ein Fehlschlag wird gemeldet, weil die
 * Angaben sonst verloren wären. */
void zond_eingang_umbenannt(Projekt *zond, const gchar *alt, const gchar *neu);

#endif /* ZOND_EINGANG_H_INCLUDED */
