/*
 sond (sond_tvfm_index_sync.h) - Akten, Beweisstücke, Unterlagen
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
 * Pflege der Index-DB, wenn Dateien oder Ordner des Dateisystems gelöscht, verschoben,
 * umbenannt oder kopiert werden: GTK-frei, ohne Projekt-DB. Die Pfade sind
 * projektrelativ ("/"-Trenner, ohne führenden "/"), wie im Index. Jede Funktion
 * bündelt ihre Schritte in einer Transaktion. Die Container-Fälle (Teile in ZIP, PDF,
 * E-Mail) sind nicht enthalten, sie bleiben bei zond.
 *
 * Reihenfolge beim Aufrufer: vorher prepare_*() (verlässt sich nicht auf den Erfolg der
 * Dateioperation, schadet bei einem Fehlschlag nicht), nach erfolgreicher Dateioperation
 * deleted() bzw. moved().
 */

#ifndef SOND_TVFM_INDEX_SYNC_H_INCLUDED
#define SOND_TVFM_INDEX_SYNC_H_INCLUDED

#include <glib.h>

#include "../sond_index.h"

G_BEGIN_DECLS

/* Vor dem Verschieben an einen neuen Ort oder dem Kopieren: eine abdeckende Aussage
 * ("alles unter X ist indiziert") am Ziel auflösen, weil dort neuer, ungeprüfter Inhalt
 * entsteht. Nicht beim Umbenennen an Ort und Stelle. root_dir: absolute Projektwurzel. */
gboolean sond_tvfm_index_sync_prepare_insert(SondIndexCtx *ctx, gchar const *new_rel,
		gchar const *root_dir, GError **error);

/* Nach dem erfolgreichen Verschieben oder Umbenennen: Einträge von old_rel auf new_rel
 * umhängen (Datei, Ordner mit allem darunter) */
gboolean sond_tvfm_index_sync_moved(SondIndexCtx *ctx, gchar const *old_rel,
		gchar const *new_rel, GError **error);

/* Nach dem erfolgreichen Löschen: Index und Abdeckung von rel und allem darunter entfernen */
gboolean sond_tvfm_index_sync_deleted(SondIndexCtx *ctx, gchar const *rel, GError **error);

G_END_DECLS

#endif /* SOND_TVFM_INDEX_SYNC_H_INCLUDED */
