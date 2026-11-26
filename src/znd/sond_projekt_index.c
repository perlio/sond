/*
 zond (sond_projekt_index.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_projekt_index.h"

#include "anbindung.h"

SondIndexStatus sond_projekt_index_status_abschnitt(SondIndexCtx *index_ctx,
		gchar const *filepart, gchar const *section) {
	Anbindung anbindung = { 0 };

	if (!index_ctx || !filepart || !section)
		return SOND_INDEX_STATUS_NONE;

	anbindung_parse_file_section(section, &anbindung);

	return sond_index_ctx_get_file_status(index_ctx, filepart, anbindung.von.seite,
			anbindung_bis_seite(anbindung));
}
