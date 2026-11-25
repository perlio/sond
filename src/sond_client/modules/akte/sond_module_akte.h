/*
 sond (sond_module_akte.h) - Akten, Beweisstücke, Unterlagen
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

#ifndef SOND_MODULE_AKTE_H
#define SOND_MODULE_AKTE_H

#include <gtk/gtk.h>
#include "../../sond_client.h"
#include "../../../sond_graph/sond_graph_node.h"

G_BEGIN_DECLS

/**
 * sond_module_akte_new:
 * @client: SondClient
 *
 * Erstellt das Akte-Modul Widget.
 *
 * Returns: (transfer full): Neues Widget
 */
GtkWidget* sond_module_akte_new(SondClient *client);

/**
 * sond_module_akte_focus_entry:
 * @module: das Widget aus sond_module_akte_new()
 *
 * Setzt den Fokus auf das Auswahlfeld (Registernummer), sofern es aktiv ist.
 */
void sond_module_akte_focus_entry(GtkWidget *module);

/**
 * SondAkteStatus:
 * @SOND_AKTE_STATUS_NICHT_ANGELEGT: nie gespeichert (keine "von"-Eigenschaft);
 *                        der Node reserviert nur die RegNr, die Library gibt
 *                        es erst mit dem ersten Speichern
 * @SOND_AKTE_STATUS_AKTIV: angelegt und nicht abgelegt
 * @SOND_AKTE_STATUS_ABGELEGT: abgelegt
 */
typedef enum {
    SOND_AKTE_STATUS_NICHT_ANGELEGT,
    SOND_AKTE_STATUS_AKTIV,
    SOND_AKTE_STATUS_ABGELEGT
} SondAkteStatus;

/**
 * sond_module_akte_get_status:
 * @node: Akten-Node
 *
 * Returns: Zustand der Akte
 */
SondAkteStatus sond_module_akte_get_status(SondGraphNode *node);

G_END_DECLS

#endif /* SOND_MODULE_AKTE_H */
