/*
 sond (sond_dokumente.c) - Akten, Beweisstücke, Unterlagen
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
 * Passwortdialog für den GTK4-Client: die Dialogfunktion für sond_password.h. Der Dialog
 * läuft in einer eigenen Hauptschleife und kehrt erst nach der Eingabe des Nutzers zurück,
 * wie es die Aufrufer von sond_password_prompt() erwarten.
 */

#ifndef SOND_CLIENT_PASSWORD_DIALOG_H
#define SOND_CLIENT_PASSWORD_DIALOG_H

#include <gtk/gtk.h>

#include "../sond_password.h"

G_BEGIN_DECLS

/* Meldet den Dialog bei sond_password.h an (Hauptthread, mehrfaches Aufrufen ist harmlos) */
void sond_client_password_dialog_init(void);

G_END_DECLS

#endif /* SOND_CLIENT_PASSWORD_DIALOG_H */
