/*
 sond (sond_import_dialog.h) - Akten, Beweisstücke, Unterlagen
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

#ifndef SOND_IMPORT_DIALOG_H
#define SOND_IMPORT_DIALOG_H

#include <gtk/gtk.h>
#include "../sond_client.h"

G_BEGIN_DECLS

/**
 * sond_import_dialog_show:
 * @client: SondClient
 * @parent: (nullable): Elternfenster
 * @lfd_nr: laufende Nummer der Akte
 * @year: Jahr der Akte (vierstellig)
 * @bezeichnung: (nullable): Aktenbezeichnung für die Anzeige
 *
 * Öffnet den Importdialog für eine Akte: kopiert Dokumente aus beliebigen
 * Verzeichnissen in die Library der Akte (über das SeaDrive-Laufwerk). Der
 * Import selbst braucht keine Verbindung zum Server. Ist der Client angemeldet,
 * wird zusätzlich der Aktenstatus geprüft (abgelegte Akten sind gesperrt);
 * sonst steht ein Hinweis, dass der Status nicht geprüft wurde.
 * Die Logik steckt in sond_import.c.
 */
void sond_import_dialog_show(SondClient *client, GtkWindow *parent,
        guint lfd_nr, guint year, const gchar *bezeichnung);

G_END_DECLS

#endif /* SOND_IMPORT_DIALOG_H */
