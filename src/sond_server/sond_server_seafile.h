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
 * - POST   /seafile/library  - Erstellt Library samt leerer Projektdatei und
 *                              (optional) Aktenbezeichnung (B~...) und
 *                              Gegenstand (G~...) in .sond/akte
 * - POST   /seafile/library-bezeichnung - Setzt/ersetzt Bezeichnung und/oder
 *                              Gegenstand
 * - GET    /seafile/library-id?name=XXX - Library-ID zu einem Namen
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

/**
 * sond_server_seafile_find_group_id_by_name:
 * @server: SondServer Instanz (benutzt server->auth_token des Service-Accounts)
 * @group_name: Name der Gruppe, wie in der Config (seafile.group_name) hinterlegt
 * @error: (nullable): Error Rückgabe - nur bei echtem Kommunikations-/Parse-Fehler
 *         gesetzt, NICHT wenn die Gruppe einfach nicht existiert.
 *
 * Löst einen Gruppennamen über die Seafile-API (GET /api2/groups/ - Gruppen
 * des Service-Accounts) in die numerische Group-ID auf. Erspart das manuelle
 * Nachschlagen der ID über Web-UI/DB - die Gruppe wird in der Config künftig
 * per (sprechendem) Namen statt per ID referenziert.
 *
 * Returns: Group-ID (>0) bei Erfolg, 0 wenn keine Gruppe mit diesem Namen
 *          gefunden wurde (kein Fehler!), -1 bei Kommunikations-/Parse-Fehler.
 */
gint sond_server_seafile_find_group_id_by_name(SondServer *server,
                                                const gchar *group_name,
                                                GError **error);

/**
 * sond_server_seafile_user_in_group:
 * @server: SondServer Instanz
 * @user_token: Seafile-Token des Benutzers (aus dem Login)
 * @group_id: Gruppen-ID (>0)
 * @error: (nullable): nur bei Kommunikations-/Parse-Fehler gesetzt
 *
 * Prüft über GET /api2/groups/ mit dem Token des Benutzers, ob er Mitglied
 * der Gruppe ist.
 *
 * Returns: TRUE bei Mitgliedschaft; FALSE ohne error bei Nichtmitgliedschaft,
 *          FALSE mit error bei Fehler.
 */
gboolean sond_server_seafile_user_in_group(SondServer *server,
                                           const gchar *user_token,
                                           guint group_id,
                                           GError **error);

/**
 * sond_server_seafile_get_library_id_by_name:
 * @server: SondServer Instanz
 * @library_name: Name der Library
 * @error: (nullable): Error Rückgabe
 *
 * Sucht eine Library anhand des Namens und gibt die ID zurück.
 *
 * Returns: (transfer full) (nullable): Library-ID oder NULL bei Fehler.
 *          Caller muss g_free() aufrufen.
 */
gchar* sond_server_seafile_get_library_id_by_name(SondServer *server,
                                                   const gchar *library_name,
                                                   GError **error);

/**
 * sond_server_seafile_api_call:
 *
 * Einfacher Aufruf der Seafile-API. @path_query ist alles hinter der Server-URL,
 * @form_body ein optionaler form-kodierter Body. TRUE, wenn eine Antwort
 * eintraf (HTTP-Status in @status_out, Text in @body_out, g_free());
 * FALSE nur bei Verbindungsfehlern.
 */
gboolean sond_server_seafile_api_call(SondServer *server, const gchar *method,
                                      const gchar *path_query, const gchar *form_body,
                                      guint *status_out, gchar **body_out,
                                      GError **error);

/**
 * sond_server_seafile_ensure_dir:
 *
 * Legt ein Verzeichnis (absoluter Pfad) in der Library an, falls es fehlt.
 */
gboolean sond_server_seafile_ensure_dir(SondServer *server, const gchar *library_id,
                                        const gchar *path, GError **error);

/**
 * sond_server_seafile_create_library:
 *
 * Legt eine Library an (und teilt sie mit der konfigurierten Gruppe).
 *
 * Returns: (transfer full) (nullable): Library-ID.
 */
gchar* sond_server_seafile_create_library(SondServer *server, const gchar *name,
                                          const gchar *desc, GError **error);

/**
 * sond_server_seafile_delete_library:
 *
 * Löscht eine Library.
 */
gboolean sond_server_seafile_delete_library(SondServer *server,
                                            const gchar *library_id, GError **error);

G_END_DECLS

#endif /* SOND_SERVER_SEAFILE_H */
