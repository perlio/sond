/*
 sond (sond_login_dialog.h) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_login_dialog.h
 * @brief Login-Dialog für Benutzerauthentifizierung
 */

#ifndef SOND_LOGIN_DIALOG_H
#define SOND_LOGIN_DIALOG_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

/**
 * LoginResult:
 *
 * Ergebnis des Login-Dialogs.
 */
typedef struct {
    gboolean success;          /* TRUE wenn Login erfolgreich */
    gchar *username;           /* Username (transfer full) */
    gchar *session_token;      /* Session-Token (transfer full) */
    gchar *seafile_token;     /* Seafile Auth-Token (transfer full) */
    gchar* seafile_url;       /* Seafile-Server URL (transfer full) */
} LoginResult;

/**
 * sond_login_dialog_show:
 * @parent: (nullable): Parent-Window
 * @server_url: Server-URL (z.B. "http://localhost:35002")
 * @error_message: (nullable): Fehlermeldung vom vorherigen Versuch
 *
 * Zeigt modal Login-Dialog und authentifiziert bei Server.
 *
 * Returns: (transfer full): LoginResult - Caller muss freigeben
 */
LoginResult* sond_login_dialog_show(GtkWindow *parent,
                                     const gchar *server_url,
                                     const gchar *error_message);

/**
 * login_result_free:
 * @result: (nullable): Freizugebendes Ergebnis
 *
 * Gibt LoginResult frei.
 */
void login_result_free(LoginResult *result);

G_END_DECLS

#endif /* SOND_LOGIN_DIALOG_H */
