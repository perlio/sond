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
#include "../sond_fileparts.h"
#include "../sond_log_and_error.h"

SondIndexStatus sond_projekt_index_status_abschnitt(SondIndexCtx *index_ctx,
		gchar const *filepart, gchar const *section) {
	Anbindung anbindung = { 0 };

	if (!index_ctx || !filepart || !section)
		return SOND_INDEX_STATUS_NONE;

	anbindung_parse_file_section(section, &anbindung);

	return sond_index_ctx_get_file_status(index_ctx, filepart, anbindung.von.seite,
			anbindung_bis_seite(anbindung));
}

/* Löscht einen Teil des Index: file_part mit Seitenbereich von bis (-1 = ganze Datei). */
static gboolean loesche_bereich(SondIndexCtx *ctx, gchar const *file_part, gint von, gint bis,
		gchar const *project_dir) {
	GError *error = NULL;

	if (sond_index_ctx_delete_index(ctx, file_part, von, bis, project_dir, &error))
		return TRUE;

	LOG_WARN("%s: sond_index_ctx_delete_index('%s'): %s", __func__, file_part,
			error ? error->message : "?");
	g_clear_error(&error);

	return FALSE;
}

gint sond_projekt_index_loeschen(SondIndexCtx *ctx, GHashTable *ht, gchar const *project_dir) {
	GHashTableIter iter = { 0 };
	gpointer key = NULL;
	gpointer value = NULL;
	gint fehler = 0;
	gboolean in_tx = FALSE;

	if (!ctx || !ht)
		return 0;

	//ein Commit statt einer je Löschung; jede Löschung ist für sich atomar (Savepoint)
	in_tx = (sqlite3_exec(ctx->db, "BEGIN;", NULL, NULL, NULL) == SQLITE_OK);

	g_hash_table_iter_init(&iter, ht);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		SondPageRange *range = value; //NULL = ganze Datei
		gchar *file_part_raw = sond_file_part_get_filepart(SOND_FILE_PART(key));
		gchar *file_part = file_part_raw;
		gint von = range ? range->von : -1;
		gint bis = range ? range->bis : -1;

		//angebundene E-Mail: Header und Inline-Teile einzeln löschen, Anhänge bleiben
		if (range && range->gmessage_message) {
			GPtrArray *parts = sond_index_ctx_gmessage_message_parts(ctx, file_part_raw, NULL);

			for (guint i = 0; parts && i < parts->len; i++) {
				if (!loesche_bereich(ctx, g_ptr_array_index(parts, i), -1, -1, project_dir))
					fehler++;
			}
			if (parts)
				g_ptr_array_unref(parts);
			g_free(file_part_raw);

			continue;
		}

		/* "Message"-Knoten einer E-Mail: derselbe SondFilePart wie die ganze eml, der Pfad
		 * lautet aber "x.eml//header" (wie beim Indizieren). Mit dem nackten Dateinamen würde die
		 * Abdeckung der ganzen Mail samt aller Mimepart-Badges verschwinden. */
		if (range && range->gmessage_header_only)
			file_part = g_strdup_printf("%s//header", file_part_raw);

		//nur die Seiten einer PDF: als Seitenbereich über alle Seiten ("ganze Datei" träfe auch
		//die Einbettungen x.pdf//...); der Seiten-Eintrag "x.pdf//" fällt dabei mit weg
		if (range && range->pdf_pagetree_only) {
			gint n_pages = sond_index_ctx_get_page_count(ctx, file_part);

			von = 0;
			bis = (n_pages > 0) ? n_pages - 1 : G_MAXINT;
		}

		//mehrere getrennte Seitenbereiche einzeln löschen; Seiten dazwischen gehören nicht dazu
		if (range && !range->pdf_pagetree_only && range->von >= 0) {
			for (gint r = 0; r < sond_page_range_count(range); r++) {
				sond_page_range_get(range, r, &von, &bis);
				if (!loesche_bereich(ctx, file_part, von, bis, project_dir))
					fehler++;
			}
		} else if (!loesche_bereich(ctx, file_part, von, bis, project_dir)) {
			fehler++;
		}

		if (file_part != file_part_raw)
			g_free(file_part);
		g_free(file_part_raw);
	}

	if (in_tx)
		sqlite3_exec(ctx->db, "COMMIT;", NULL, NULL, NULL);

	return fehler;
}
