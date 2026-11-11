/*
 sond (sond_zond_schema.h) - Akten, Beweisstücke, Unterlagen
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

#ifndef SOND_ZOND_SCHEMA_H_INCLUDED
#define SOND_ZOND_SCHEMA_H_INCLUDED

#include <glib.h>
#include <sqlite3.h>

G_BEGIN_DECLS

/*
 * Legt in der offenen Datenbank db das Schema einer leeren zond-Projektdatei
 * an (Tabelle "knoten" mit den Wurzelzeilen 0, 1 und 2, Version
 * ZOND_VERSION_MAJOR). Bewusst frei von GTK und zond-Code, damit auch
 * sond_server und sond_client die Struktur erzeugen können; einzige Quelle
 * des Schemas.
 *
 * Rückgabe 0 bei Erfolg, -1 bei Fehler (error gesetzt, Domäne "SQLITE").
 */
gint sond_zond_schema_create(sqlite3 *db, GError **error);

G_END_DECLS

#endif /* SOND_ZOND_SCHEMA_H_INCLUDED */
