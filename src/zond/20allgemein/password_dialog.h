/*
 zond (password_dialog.h) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2020  pelo america

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

#ifndef SRC_ZOND_20ALLGEMEIN_PASSWORD_DIALOG_H_
#define SRC_ZOND_20ALLGEMEIN_PASSWORD_DIALOG_H_

#include <glib.h>

#include "../../sond_password.h"

/* Passwortdialog für sond_password_set_prompt_func() (sond_password.h):
 * modal über dem gerade aktiven Fenster, die Eingabe ist maskiert, bei
 * allow_skip mit den Knöpfen Überspringen und Alle überspringen. user_data
 * ist das Projekt (Projekt*). Rückgabe und *password wie
 * SondPasswordPromptFunc. */
SondPromptResult zond_password_dialog(gchar const *what, gboolean wrong,
		gboolean allow_skip, gchar **password, gpointer user_data);

#endif /* SRC_ZOND_20ALLGEMEIN_PASSWORD_DIALOG_H_ */
