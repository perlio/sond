/*
 sond (sond_module_offline.h) - Akten, Beweisstücke, Unterlagen
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

#ifndef SOND_MODULE_OFFLINE_H
#define SOND_MODULE_OFFLINE_H

#include <gtk/gtk.h>
#include "../sond_client.h"

G_BEGIN_DECLS

/**
 * sond_module_offline_new:
 * @client: SondClient
 *
 * Erstellt das Offline-Akten-Modul Widget.
 * Zeigt alle offline-verfügbaren Akten mit Status und Verwaltungsoptionen.
 *
 * Returns: (transfer full): Neues Widget
 */
GtkWidget* sond_module_offline_new(SondClient *client);

G_END_DECLS

#endif /* SOND_MODULE_OFFLINE_H */
