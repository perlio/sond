/*
 sond (sond_server_archiv.h) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2026 pelo america

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
 * @file sond_server_archiv.h
 * @brief Archivieren einer Library als ZIP in die Library "abgelegteAkten"
 *
 * Der Server kennt hier nur Library-Namen und Archivnamen; wann und unter
 * welchem Namen archiviert wird, entscheidet der Aufrufer (Aufgabe des Clients).
 */

#ifndef SOND_SERVER_ARCHIV_H
#define SOND_SERVER_ARCHIV_H

#include <glib.h>
#include "sond_server.h"

G_BEGIN_DECLS

/** Name der Library, in der die ZIP-Archive liegen (wird bei Bedarf angelegt) */
#define SOND_SERVER_ARCHIV_LIBRARY "abgelegteAkten"

/**
 * sond_server_archiv_library:
 * @server: SondServer Instanz
 * @library_name: Name der zu archivierenden Library
 * @archiv_name: Name des Archivs ohne Endung (".zip" wird angehängt);
 *   keine Pfadtrenner
 * @delete_after: Library nach erfolgreicher Archivierung löschen
 * @error: (nullable): Error Rückgabe
 *
 * Lädt alle Dateien der Library, packt sie in ein ZIP (Verzeichnisstruktur
 * und Änderungszeiten bleiben erhalten), prüft das ZIP (Einträge, Größen,
 * CRC), lädt es nach "abgelegteAkten" und prüft dort die Größe. Erst danach
 * wird die Library gelöscht (@delete_after). Bei jedem Fehler bleibt die
 * Library unverändert. Ein vorhandenes Archiv gleichen Namens wird nicht
 * überschrieben (Fehler).
 *
 * Das temporäre Verzeichnis (TMPDIR) braucht Platz für Library plus ZIP.
 * Die Funktion blockiert, bis alles erledigt ist.
 *
 * Returns: TRUE bei Erfolg.
 */
gboolean sond_server_archiv_library(SondServer *server, const gchar *library_name,
                                    const gchar *archiv_name, gboolean delete_after,
                                    GError **error);

G_END_DECLS

#endif /* SOND_SERVER_ARCHIV_H */
