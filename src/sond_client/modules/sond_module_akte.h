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
#include "../sond_client.h"
#include "../../sond_graph/sond_graph_node.h"

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
 * sond_module_akte_find_by_regnr:
 * @client: SondClient
 * @lfd_nr: Laufende Nummer
 * @year: Jahr (vollständig, z.B. 2026)
 * @error: Fehler-Rückgabe (G_IO_ERROR_NOT_FOUND, wenn es die Akte nicht gibt)
 *
 * Sucht den Akten-Node zu einer Registernummer (nur Lesen, kein Lock).
 *
 * Returns: (transfer full) (nullable): Node oder NULL
 */
SondGraphNode* sond_module_akte_find_by_regnr(SondClient *client,
                                              guint lfd_nr, guint year,
                                              GError **error);

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

/**
 * sond_module_akte_parse_regnr:
 * @regnr_str: Eingabe, z.B. "23/26" oder "2326"
 * @lfd_nr: (out): Laufende Nummer
 * @year: (out): Jahr (vollständig)
 *
 * Returns: TRUE, wenn die Eingabe eine Registernummer ist
 */
gboolean sond_module_akte_parse_regnr(const gchar *regnr_str, guint *lfd_nr,
                                      guint *year);

/**
 * sond_module_akte_library_name:
 * @lfd_nr: Laufende Nummer
 * @year: Jahr (vollständig)
 *
 * Returns: (transfer full): Name der zugehörigen Library, z.B. "2026-23"
 */
gchar* sond_module_akte_library_name(guint lfd_nr, guint year);

G_END_DECLS

#endif /* SOND_MODULE_AKTE_H */
