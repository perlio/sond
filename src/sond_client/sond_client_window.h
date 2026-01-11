/*
 sond (sond_client_window.h) - Akten, Beweisstücke, Unterlagen
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

#ifndef SOND_CLIENT_WINDOW_H
#define SOND_CLIENT_WINDOW_H

#include <gtk/gtk.h>
#include "sond_client.h"

G_BEGIN_DECLS

#define SOND_TYPE_CLIENT_WINDOW (sond_client_window_get_type())
G_DECLARE_FINAL_TYPE(SondClientWindow, sond_client_window, SOND, CLIENT_WINDOW, GtkApplicationWindow)

/**
 * sond_client_window_new:
 * @app: GTK Application
 * @client: SondClient
 *
 * Erstellt das Hauptfenster.
 *
 * Returns: (transfer full): Neues Fenster
 */
SondClientWindow* sond_client_window_new(GtkApplication *app, SondClient *client);

G_END_DECLS

#endif /* SOND_CLIENT_WINDOW_H */
