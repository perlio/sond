/*
 sond (sond_server.h) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_server.h
 * @brief HTTP REST Server für Graph-Datenbank
 *
 * Stellt REST-Endpoints für Node-Operationen bereit.
 */

#ifndef SOND_SERVER_H
#define SOND_SERVER_H

#include <glib-object.h>
#include <libsoup/soup.h>
#include <mysql/mysql.h>

G_BEGIN_DECLS

/* ========================================================================
 * SondServer
 * ======================================================================== */

#define SOND_TYPE_SERVER (sond_server_get_type())
G_DECLARE_FINAL_TYPE(SondServer, sond_server, SOND, SERVER, GObject)

/**
 * sond_server_new:
 * @db_host: MySQL-Host
 * @db_user: MySQL-User
 * @db_password: MySQL-Passwort
 * @db_name: MySQL-Datenbank
 * @port: Server-Port (z.B. 8080)
 * @error: (nullable): Fehler-Rückgabe
 *
 * Erstellt einen neuen Server mit DB-Verbindung.
 *
 * Returns: (transfer full) (nullable): Server oder %NULL bei Fehler
 */
SondServer* sond_server_new(const gchar *db_host,
                             const gchar *db_user,
                             const gchar *db_password,
                             const gchar *db_name,
                             guint port,
                             GError **error);

/**
 * sond_server_start:
 * @server: Server
 * @error: (nullable): Fehler-Rückgabe
 *
 * Startet den Server.
 *
 * Returns: %TRUE bei Erfolg
 */
gboolean sond_server_start(SondServer *server, GError **error);

/**
 * sond_server_stop:
 * @server: Server
 *
 * Stoppt den Server.
 */
void sond_server_stop(SondServer *server);

/**
 * sond_server_is_running:
 * @server: Server
 *
 * Returns: %TRUE wenn Server läuft
 */
gboolean sond_server_is_running(SondServer *server);

/**
 * sond_server_get_port:
 * @server: Server
 *
 * Returns: Server-Port
 */
guint sond_server_get_port(SondServer *server);

/**
 * sond_server_get_url:
 * @server: Server
 *
 * Gibt die Server-URL zurück.
 *
 * Returns: (transfer full): URL (z.B. "http://localhost:8080")
 */
gchar* sond_server_get_url(SondServer *server);

G_END_DECLS

#endif /* SOND_SERVER_H */
