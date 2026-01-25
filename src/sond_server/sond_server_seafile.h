/*
 sond (sond_server_seafile.h) - Akten, Beweisstücke, Unterlagen
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

/*
 sond (sond_server_seafile.h) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2025  peloamerica

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
 * @file sond_server_seafile.h
 * @brief Seafile Integration für SOND Server
 */

#ifndef SOND_SERVER_SEAFILE_H
#define SOND_SERVER_SEAFILE_H

#include <glib.h>
#include "sond_server.h"

G_BEGIN_DECLS

/**
 * sond_server_seafile_register_handlers:
 * @server: SondServer Instanz
 *
 * Registriert alle Seafile-Endpoints beim Server.
 *
 * Endpoints:
 * - POST   /seafile/library  - Erstellt Library
 * - DELETE /seafile/library  - Löscht Library
 */
void sond_server_seafile_register_handlers(SondServer *server);

/**
 * sond_server_seafile_get_auth_token:
 * @server: SondServer Instanz
 * @user: Seafile Username
 * @password: Seafile Passwort
 * @error: (nullable): Error Rückgabe
 *
 * Authentifiziert bei Seafile und holt Auth-Token.
 *
 * Returns: (transfer full) (nullable): Auth-Token oder NULL bei Fehler.
 *          Caller muss g_free() aufrufen.
 */
gchar* sond_server_seafile_get_auth_token(SondServer *server,
                                           const gchar *user,
                                           const gchar *password,
                                           GError **error);

G_END_DECLS

#endif /* SOND_SERVER_SEAFILE_H */
