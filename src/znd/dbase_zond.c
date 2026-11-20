/*
 zond (dbase_zond.c) - Akten, Beweisstücke, Unterlagen
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

gint dbase_zond_begin(DBaseZond* dbase_zond, GError** error) {
	gint rc = 0;

	/* journal_mode/synchronous können sich jederzeit von außen geändert
	 * haben (s. zond_dbase_check_journal_settings()) - deshalb hier vor
	 * jeder Dual-Write-Transaktion erneut geprüft, nicht nur beim Öffnen.
	 * Für BEIDE Dateien, obwohl die Transaktion selbst nur noch auf EINER
	 * Connection läuft (s.u.) - work hat trotz ATTACH weiterhin seine
	 * eigene, unabhängige Datei/Verbindung für alle Einzel-DB-Operationen,
	 * deren journal_mode/synchronous unabhängig von außen verstellt worden
	 * sein könnte. */
	rc = zond_dbase_check_journal_settings(dbase_zond->zond_dbase_store, error);
	if (rc)
		return -1;

	rc = zond_dbase_check_journal_settings(dbase_zond->zond_dbase_work, error);
	if (rc)
		return -1;

	/* Ein BEGIN auf der store-Connection - work ist per ATTACH
	 * (project_create_dbase_zond()) als zweites Schema mit eingehängt,
	 * die Dual-Write-Funktionen (dbase_zond_update_sections()/_path()/
	 * _gmessage_index()) schreiben schema-qualifiziert auf main+work
	 * innerhalb dieser einen Transaktion (Atomarität store/work). */
	return zond_dbase_begin(dbase_zond->zond_dbase_store, error);
}

void dbase_zond_rollback(DBaseZond* dbase_zond, GError** error) {
	GError* error_int = NULL;

	/* Ein ROLLBACK auf der store-Connection (work hängt als Schema daran).
	 * Über einen lokalen error_int,
	 * NICHT direkt über den übergebenen error-Parameter: der hält an
	 * dieser Stelle i.d.R. schon den Fehler, WEGEN dem gerade
	 * zurückgerollt wird (z.B. den gescheiterten Commit, s.
	 * dbase_zond_commit()) - würde das fehlschlagende ROLLBACK-Statement
	 * selbst *error direkt überschreiben, ginge dieser eigentliche Grund
	 * verloren (und die alte GError würde geleakt). Daher: anhängen statt
	 * überschreiben, wie zuvor. */
	zond_dbase_rollback(dbase_zond->zond_dbase_store, &error_int);
	if (!error_int)
		return;   //Normalfall: Rollback erfolgreich

	if (!error)
		g_error_free(error_int);
	else if (*error) {
		gchar *message = g_strdup_printf("%s\n\n%s", (*error)->message,
				error_int->message);

		g_free((*error)->message);
		(*error)->message = message;
		g_error_free(error_int);
	} else
		*error = error_int;

	return;
}

gint dbase_zond_commit(DBaseZond* dbase_zond, GError** error) {
	gint rc = 0;

	/* Nur noch EIN COMMIT (Punkt 4). Der frühere "Katastrophe"-Fall (zweiter
	 * Commit scheitert, nachdem der erste schon erfolgreich war -
	 * store/work dadurch inkonsistent) kann strukturell nicht mehr
	 * auftreten, weil es nur noch einen einzigen Commit auf einer
	 * einzigen Connection gibt - der schlägt entweder ganz oder gar nicht
	 * fehl (vorausgesetzt journal_mode/synchronous halten, s.
	 * zond_dbase_check_journal_settings() oben). Entfällt daher
	 * ersatzlos (Punkt 4/5). */
	rc = zond_dbase_commit(dbase_zond->zond_dbase_store, error);
	if (rc) {
		dbase_zond_rollback(dbase_zond, error);
		return -1;
	}

	return 0;
}

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
static gint dbase_zond_check_index_names(sqlite3 *db, GError **error) {
	sqlite3_stmt *stmt = NULL;
	g_autofree gchar *name = NULL;

	if (sqlite3_prepare_v2(db,
			"SELECT name FROM idx.sqlite_master WHERE type='table' "
			"AND name NOT LIKE 'sqlite\\_%' ESCAPE '\\' "
			"AND (name IN (SELECT name FROM main.sqlite_master) "
			"OR name IN (SELECT name FROM work.sqlite_master)) LIMIT 1;",
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
			|| dbase_zond_check_index_names(db, error)) {
		dbase_zond_detach_index(dbase_zond);

		return -1;
	}

	return 0;
}

// ============================================================================
// Database Update Functions
// ============================================================================

/* Die folgenden Funktionen laufen auf der EINEN store-Connection mit
 * angehängtem work-Schema (project_create_dbase_zond(), ATTACH DATABASE
 * ... AS work) - dieselbe schema-qualifizierte SQL wird einmal für "main"
 * (=store) und einmal für "work" ausgeführt, innerhalb derselben, von
 * dbase_zond_begin() geöffneten Transaktion. work behält daneben
 * unverändert seine eigene Connection für alle anderen (Einzel-DB-)
 * Operationen. */

gint dbase_zond_update_path(DBaseZond* dbase_zond, gchar const* prefix_old,
		gchar const* prefix_new, GError** error) {
	sqlite3 *db = zond_dbase_get_dbase(dbase_zond->zond_dbase_store);
	static gchar const *schemas[] = { "main", "work" };

	for (gint i = 0; i < 2; i++) {
		gchar *sql = NULL;
		sqlite3_stmt *stmt = NULL;
		gint rc = 0;

		/* Nur prefix_old selbst und was darunter liegt ("/" bzw. "//") -
		 * ohne WHERE traf das Ersetzen auch Pfade, die nur zufällig mit
		 * prefix_old beginnen ("Akte" -> auch "Akte_alt"). SUBSTR statt
		 * LIKE, weil "%" und "_" in Pfaden vorkommen. */
		sql = g_strdup_printf("UPDATE %s.knoten SET file_part = "
				"?2 || SUBSTR( file_part, LENGTH( ?1 ) + 1 ) "
				"WHERE file_part = ?1 OR "
				"SUBSTR( file_part, 1, LENGTH( ?1 ) + 1 ) = ?1 || '/';",
				schemas[i]);
		rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
		g_free(sql);
		if (rc != SQLITE_OK) {
			if (error)
				*error = g_error_new(g_quark_from_static_string("SQLITE3"), rc,
						"%s (%s): %s", __func__, schemas[i], sqlite3_errmsg(db));
			return -1;
		}

		sqlite3_bind_text(stmt, 1, prefix_old, -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 2, prefix_new, -1, SQLITE_STATIC);

		rc = sqlite3_step(stmt);
		sqlite3_finalize(stmt);
		if (rc != SQLITE_DONE) {
			if (error)
				*error = g_error_new(g_quark_from_static_string("SQLITE3"), rc,
						"%s (%s): %s", __func__, schemas[i], sqlite3_errmsg(db));
			return -1;
		}
	}

	return 0;
}

gint dbase_zond_remove_filepart(DBaseZond* dbase_zond, gchar const* path,
		GError** error) {
	sqlite3 *db = zond_dbase_get_dbase(dbase_zond->zond_dbase_store);
	static gchar const *schemas[] = { "main", "work" };

	for (gint i = 0; i < 2; i++) {
		gchar *sql = NULL;
		sqlite3_stmt *stmt = NULL;
		gint rc = 0;

		//Sections und Kinder folgen per ON DELETE CASCADE
		sql = g_strdup_printf("DELETE FROM %s.knoten WHERE type=5 AND "
				"file_part=?1 AND section IS NULL;", schemas[i]);
		rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
		g_free(sql);
		if (rc != SQLITE_OK) {
			if (error)
				*error = g_error_new(g_quark_from_static_string("SQLITE3"), rc,
						"%s (%s): %s", __func__, schemas[i], sqlite3_errmsg(db));
			return -1;
		}

		sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);

		rc = sqlite3_step(stmt);
		sqlite3_finalize(stmt);
		if (rc != SQLITE_DONE) {
			if (error)
				*error = g_error_new(g_quark_from_static_string("SQLITE3"), rc,
						"%s (%s): %s", __func__, schemas[i], sqlite3_errmsg(db));
			return -1;
		}
	}

	return 0;
}

gint dbase_zond_update_gmessage_index(DBaseZond* dbase_zond,
		gchar const* prefix, gint index, gboolean into, GError** error) {
	sqlite3 *db = zond_dbase_get_dbase(dbase_zond->zond_dbase_store);
	static gchar const *schemas[] = { "main", "work" };

	for (gint i = 0; i < 2; i++) {
		gchar *sql = NULL;
		sqlite3_stmt *stmt = NULL;
		gint rc = 0;

		/* Nur rein numerische Segmente: CAST macht aus "alpha" (Platzhalter
		 * beim Verschieben in die Mail) 0 - beim Einfügen an Position 0 wurde
		 * der Platzhalter sonst mitgezählt und das anschließende
		 * "alpha" -> "0" fand nichts mehr. SUBSTR statt LIKE ("_" und "%"
		 * im Namen). */
		sql = g_strdup_printf(
				"UPDATE %s.knoten "
				"SET file_part = ?1 || " //?1 ist prefix
				"(CAST(SUBSTR(SUBSTR(file_part, LENGTH(?1) + 1), 1, "
				"INSTR(SUBSTR(file_part, LENGTH(?1) + 1) || '/', '/') - 1) "
				"AS INTEGER) + ?2) || " //?2 ist Zahl, die hinzugesetzt/abgezogen wird
				"SUBSTR(SUBSTR(file_part, LENGTH(?1) + 1), "
				"INSTR(SUBSTR(file_part, LENGTH(?1) + 1) || '/', '/')) "
				"WHERE SUBSTR(file_part, 1, LENGTH(?1)) = ?1 "
				"AND SUBSTR(SUBSTR(file_part, LENGTH(?1) + 1), 1, "
						"INSTR(SUBSTR(file_part, LENGTH(?1) + 1) || '/', '/') - 1) <> '' "
				"AND SUBSTR(SUBSTR(file_part, LENGTH(?1) + 1), 1, "
						"INSTR(SUBSTR(file_part, LENGTH(?1) + 1) || '/', '/') - 1) "
						"NOT GLOB '*[^0-9]*' "
				"AND CAST(SUBSTR(SUBSTR(file_part, LENGTH(?1) + 1), 1, "
						"INSTR(SUBSTR(file_part, LENGTH(?1) + 1) || '/', '/') - 1) "
						"AS INTEGER) >= ?3; ", //?3 ist Schwellenwert
				schemas[i]);
		rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
		g_free(sql);
		if (rc != SQLITE_OK) {
			if (error)
				*error = g_error_new(g_quark_from_static_string("SQLITE3"), rc,
						"%s (%s): %s", __func__, schemas[i], sqlite3_errmsg(db));
			return -1;
		}

		sqlite3_bind_text(stmt, 1, prefix, -1, SQLITE_STATIC);
		sqlite3_bind_int(stmt, 2, into ? 1 : -1);
		sqlite3_bind_int(stmt, 3, index);

		rc = sqlite3_step(stmt);
		sqlite3_finalize(stmt);
		if (rc != SQLITE_DONE) {
			if (error)
				*error = g_error_new(g_quark_from_static_string("SQLITE3"), rc,
						"%s (%s): %s", __func__, schemas[i], sqlite3_errmsg(db));
			return -1;
		}
	}

	return 0;
}
