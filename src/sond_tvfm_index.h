/*
 sond (sond_tvfm_index.h) - Akten, Beweisstücke, Unterlagen
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
 * Indizierungsstatus eines Baum-Eintrags aus der Index-DB: gemeinsam für die GTK3-
 * und die GTK4-Fassung des Dateibaums, ohne GTK.
 */

#ifndef SOND_TVFM_INDEX_H_INCLUDED
#define SOND_TVFM_INDEX_H_INCLUDED

#include <glib.h>

#include "sond_fileparts.h"
#include "sond_index.h"
#include "sond_tvfm_item.h"

G_BEGIN_DECLS

/* Lässt sich der Inhalt (filepart) indizieren? PDF und E-Mail immer, sonst nach MIME-Typ */
gboolean sond_tvfm_file_part_indexable(SondFilePart *sfp, gchar const *filepart);

/* Status für das Overlay-Symbol; NONE, wenn nicht indiziert, nicht indizierbar oder kein
 * Kontext. Sections (Anbindungen) beantwortet die Unterklasse, hier liefern sie NONE. */
SondIndexStatus sond_tvfm_item_index_status(SondTVFMItem*, SondIndexCtx*);

G_END_DECLS

#endif /* SOND_TVFM_INDEX_H_INCLUDED */
