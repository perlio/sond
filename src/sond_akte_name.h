/*
 sond (sond_akte_name.h) - Akten, Beweisstücke, Unterlagen
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

/*
 * Aktenbezeichnung und Gegenstand als Dateinamen: Der Server legt in jeder
 * Akten-Library im Verzeichnis .sond/akte leere Dateien an:
 *
 *   B~<kodierte Bezeichnung>   z. B. "B~Müller, Heinz"
 *   G~<kodierter Gegenstand>   z. B. "G~Verkehrsunfall A9"
 *
 * Clients lesen die Namen aus dem Verzeichnislisting, ohne eine Datei laden zu
 * müssen (SeaDrive legt die Verzeichniseinträge als Platzhalter an, Inhalte
 * werden erst bei Zugriff geladen). Ein Verzeichnis genügt für beide Angaben.
 * Ein Eintrag ohne Vorsilbe gilt als Bezeichnung (ältere Fassung).
 *
 * Kodierung der Werte: Zeichen, die in Windows-Dateinamen nicht erlaubt sind,
 * '%' und '~' selbst, Steuerzeichen sowie ein Punkt oder Leerzeichen am Ende
 * werden als %XX (UTF-8-Bytes, Großbuchstaben) geschrieben. Zu lange Werte
 * werden an einer Zeichengrenze gekürzt.
 *
 * Frei von GTK; nur glib.
 */

#ifndef SOND_AKTE_NAME_H
#define SOND_AKTE_NAME_H

#include <glib.h>

/* Verzeichnis der Library (Pfad relativ zur Library-Wurzel) */
#define SOND_AKTE_NAME_DIR ".sond/akte"

/* Art des Eintrags */
#define SOND_AKTE_NAME_BEZEICHNUNG 'B'
#define SOND_AKTE_NAME_GEGENSTAND  'G'

/* Höchstlänge des kodierten Werts in Bytes (Dateisysteme erlauben 255) */
#define SOND_AKTE_NAME_MAX 200

/**
 * sond_akte_name_escape:
 * @wert: Bezeichnung oder Gegenstand, z. B. "Meier ./. Müller"
 *
 * Returns: (transfer full) (nullable): kodierter Wert (g_free()),
 *          NULL bei leerem Wert
 */
gchar* sond_akte_name_escape(const gchar *wert);

/**
 * sond_akte_name_unescape:
 * @dateiname: kodierter Wert aus sond_akte_name_escape()
 *
 * Returns: (transfer full): Wert (g_free()); ungültige %-Folgen bleiben
 *          unverändert
 */
gchar* sond_akte_name_unescape(const gchar *dateiname);

/**
 * sond_akte_name_make:
 * @art: SOND_AKTE_NAME_BEZEICHNUNG oder SOND_AKTE_NAME_GEGENSTAND
 * @wert: Bezeichnung bzw. Gegenstand
 *
 * Returns: (transfer full) (nullable): Dateiname "B~..." bzw. "G~...",
 *          NULL bei leerem Wert
 */
gchar* sond_akte_name_make(gchar art, const gchar *wert);

/**
 * sond_akte_name_parse:
 * @dateiname: Name eines Eintrags in .sond/akte
 * @wert: (out) (optional) (transfer full): dekodierter Wert
 * @neues_format: (out) (optional): TRUE bei Eintrag mit Vorsilbe
 *
 * Returns: SOND_AKTE_NAME_BEZEICHNUNG oder SOND_AKTE_NAME_GEGENSTAND; ein
 *          Eintrag ohne Vorsilbe gilt als Bezeichnung
 */
gchar sond_akte_name_parse(const gchar *dateiname, gchar **wert,
        gboolean *neues_format);

#endif /* SOND_AKTE_NAME_H */
