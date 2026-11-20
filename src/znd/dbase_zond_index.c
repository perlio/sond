/*
 zond (dbase_zond_index.c) - Akten, Beweisstücke, Unterlagen
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

#include "dbase_zond.h"

#include <sqlite3.h>
#include <glib.h>

#include "../sond_log_and_error.h"
#include "../sond_index.h"

#include "zond_dbase.h"

/* Index-DB (.sond_index.db) als Schema "idx" an die store-Connection hängen: für die atomare
 * Transaktion über store, work (falls vorhanden) und idx. Eigene Datei, damit dbase_zond.c ohne
 * den Index (sond_index.c) übersetzt und gelinkt werden kann. */

#define SQL_INDEX_NAMES_WORK \
	"SELECT name FROM idx.sqlite_master WHERE type='table' " \
	"AND name NOT LIKE 'sqlite\\_%' ESCAPE '\\' " \
	"AND (name IN (SELECT name FROM main.sqlite_master) " \
	"OR name IN (SELECT name FROM work.sqlite_master)) LIMIT 1;"
#define SQL_INDEX_NAMES \
	"SELECT name FROM idx.sqlite_master WHERE type='table' " \
	"AND name NOT LIKE 'sqlite\\_%' ESCAPE '\\' " \
	"AND name IN (SELECT name FROM main.sqlite_master) LIMIT 1;"

/* Journal-Modus der angehängten Index-DB: wie bei store/work Voraussetzung
 * für die atomare Mehrdatei-Transaktion (s. zond_dbase_check_journal_
 * settings()). */
static gint dbase_zond_check_index_journal(sqlite3 *db, GError **error) {
	sqlite3_stmt *stmt = NULL;
	g_autofree gchar *journal_mode = NULL;

	if (sqlite3_prepare_v2(db, "PRAGMA idx.journal_mode;", -1, &stmt, NULL)
			!= SQLITE_OK) {
		g_set_error(error, SOND_ERROR, 0, "%s: PRAGMA idx.journal_mode: %s",
				__func__, sqlite3_errmsg(db));
		return -1;
	}
	if (sqlite3_step(stmt) == SQLITE_ROW)
		journal_mode = g_strdup((gchar const*) sqlite3_column_text(stmt, 0));
	sqlite3_finalize(stmt);

	if (!journal_mode || (g_ascii_strcasecmp(journal_mode, "delete")
			&& g_ascii_strcasecmp(journal_mode, "truncate")
			&& g_ascii_strcasecmp(journal_mode, "persist"))) {
		g_set_error(error, SOND_ERROR, 0,
				"Die Index-Datenbank wird von einem anderen Programm "
				"verwendet (journal_mode)");
		return -1;
	}

	return 0;
}

/* Die Index-Funktionen (sond_index.c) arbeiten mit unqualifizierten
 * Tabellennamen und laufen über die store-Connection gegen idx, weil SQLite
 * unqualifizierte Namen in allen angehängten Datenbanken sucht. Das geht
 * nur, solange kein Tabellenname in idx auch in main oder work vorkommt -
 * sonst würde still in die falsche Datenbank geschrieben. */
static gint dbase_zond_check_index_names(DBaseZond *dbase_zond, GError **error) {
	sqlite3 *db = zond_dbase_get_dbase(dbase_zond->zond_dbase_store);
	sqlite3_stmt *stmt = NULL;
	g_autofree gchar *name = NULL;
	gchar const *sql_with_work = SQL_INDEX_NAMES_WORK;
	gchar const *sql_without_work = SQL_INDEX_NAMES;

	if (sqlite3_prepare_v2(db,
			dbase_zond->zond_dbase_work ? sql_with_work : sql_without_work,
			-1, &stmt, NULL) != SQLITE_OK) {
		g_set_error(error, SOND_ERROR, 0, "%s: %s", __func__,
				sqlite3_errmsg(db));
		return -1;
	}
	if (sqlite3_step(stmt) == SQLITE_ROW)
		name = g_strdup((gchar const*) sqlite3_column_text(stmt, 0));
	sqlite3_finalize(stmt);

	if (name) {
		g_set_error(error, SOND_ERROR, 0,
				"Tabelle '%s' kommt in der Projekt- und der Index-Datenbank "
				"vor", name);
		return -1;
	}

	return 0;
}

void dbase_zond_detach_index(DBaseZond* dbase_zond) {
	sqlite3 *db = NULL;
	gchar *errmsg = NULL;

	if (!dbase_zond->index_attached)
		return;

	db = zond_dbase_get_dbase(dbase_zond->zond_dbase_store);
	if (sqlite3_exec(db, "DETACH DATABASE idx;", NULL, NULL, &errmsg)
			!= SQLITE_OK) {
		LOG_WARN("%s: DETACH idx: %s", __func__, errmsg ? errmsg : "?");
		sqlite3_free(errmsg);

		return; //bleibt angehängt, der nächste attach nutzt es weiter
	}

	dbase_zond->index_attached = FALSE;

	return;
}

void dbase_zond_index_ctx(DBaseZond* dbase_zond, SondIndexCtx* out,
		SondIndexCtx const* src) {
	sond_index_ctx_init_attached(out, src,
			zond_dbase_get_dbase(dbase_zond->zond_dbase_store));

	return;
}

gint dbase_zond_attach_index(DBaseZond* dbase_zond,
		gchar const* index_db_path, GError** error) {
	sqlite3 *db = NULL;
	gchar *sql = NULL;
	gchar *errmsg = NULL;
	gint rc = 0;

	if (dbase_zond->index_attached)
		return 0;

	db = zond_dbase_get_dbase(dbase_zond->zond_dbase_store);

	//läuft die Index-Verbindung gerade (Lesen im Chat-Thread), kurz warten
	sqlite3_busy_timeout(db, 5000);

	sql = sqlite3_mprintf("ATTACH DATABASE %Q AS idx;", index_db_path);
	rc = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
	sqlite3_free(sql);
	if (rc != SQLITE_OK) {
		g_set_error(error, g_quark_from_static_string("SQLITE3"), rc,
				"%s: ATTACH idx: %s", __func__, errmsg ? errmsg : "?");
		sqlite3_free(errmsg);

		return -1;
	}
	dbase_zond->index_attached = TRUE;

	sqlite3_exec(db, "PRAGMA idx.synchronous=FULL;", NULL, NULL, NULL);

	if (dbase_zond_check_index_journal(db, error)
			|| dbase_zond_check_index_names(dbase_zond, error)) {
		dbase_zond_detach_index(dbase_zond);

		return -1;
	}

	return 0;
}

