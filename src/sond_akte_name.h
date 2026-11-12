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
 * Aktenbezeichnung als Dateiname: Der Server legt in jeder Akten-Library
 * .sond/akte/<kodierte Bezeichnung> an (leere Datei). Clients lesen den Namen
 * aus dem Verzeichnislisting, ohne eine Datei laden zu müssen (SeaDrive hält
 * nur bereits geöffnete Dateien im Cache, Verzeichniseinträge sind bekannt).
 *
 * Kodierung: Zeichen, die in Windows-Dateinamen nicht erlaubt sind, '%' selbst,
 * Steuerzeichen sowie ein Punkt oder Leerzeichen am Ende werden als %XX
 * (UTF-8-Bytes, Großbuchstaben) geschrieben. Zu lange Bezeichnungen werden an
 * einer Zeichengrenze gekürzt.
 *
 * Frei von GTK; nur glib.
 */

#ifndef SOND_AKTE_NAME_H
#define SOND_AKTE_NAME_H

#include <glib.h>

/* Unterverzeichnis der Library (Pfade relativ zur Library-Wurzel) */
#define SOND_AKTE_NAME_DIR ".sond/akte"

/* Höchstlänge des kodierten Dateinamens in Bytes (Dateisysteme erlauben 255) */
#define SOND_AKTE_NAME_MAX 200

/**
 * sond_akte_name_escape:
 * @bezeichnung: Aktenbezeichnung, z. B. "Meier ./. Müller"
 *
 * Returns: (transfer full) (nullable): kodierter Dateiname (g_free()),
 *          NULL bei leerer Bezeichnung
 */
gchar* sond_akte_name_escape(const gchar *bezeichnung);

/**
 * sond_akte_name_unescape:
 * @dateiname: Name aus sond_akte_name_escape()
 *
 * Returns: (transfer full): Bezeichnung (g_free()); ungültige %-Folgen bleiben
 *          unverändert
 */
gchar* sond_akte_name_unescape(const gchar *dateiname);

#endif /* SOND_AKTE_NAME_H */
