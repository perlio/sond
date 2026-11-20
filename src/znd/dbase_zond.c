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

	if (dbase_zond->zond_dbase_work) {
		rc = zond_dbase_check_journal_settings(dbase_zond->zond_dbase_work, error);
		if (rc)
			return -1;
	}

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

/* Die Schemata, auf denen die Dual-Write-Funktionen schreiben: "main" (store) und, wenn vorhanden,
 * "work" (per ATTACH an die store-Connection gehängt). Ohne work (Client) nur "main". */
static guint dbase_zond_schemas(DBaseZond *dbase_zond, gchar const *out[2]) {
	out[0] = "main";
	out[1] = NULL;

	if (!dbase_zond->zond_dbase_work)
		return 1;

	out[1] = "work";

	return 2;
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
	gchar const *schemas[2] = { NULL, NULL };
	guint n_schemas = dbase_zond_schemas(dbase_zond, schemas);

	for (guint i = 0; i < n_schemas; i++) {
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
	gchar const *schemas[2] = { NULL, NULL };
	guint n_schemas = dbase_zond_schemas(dbase_zond, schemas);

	for (guint i = 0; i < n_schemas; i++) {
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
	gchar const *schemas[2] = { NULL, NULL };
	guint n_schemas = dbase_zond_schemas(dbase_zond, schemas);

	for (guint i = 0; i < n_schemas; i++) {
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

// ============================================================================
// Anlegen, primäre Datenbank, Prüfen auf Verweise
// ============================================================================

DBaseZond* dbase_zond_new_store(gchar const *path, GError **error) {
	DBaseZond *dbase_zond = NULL;
	ZondDBase *store = zond_dbase_new(path, FALSE, FALSE, error);

	if (!store)
		return NULL;

	dbase_zond = g_new0(DBaseZond, 1);
	dbase_zond->zond_dbase_store = store;

	return dbase_zond;
}

void dbase_zond_free(DBaseZond *dbase_zond) {
	if (!dbase_zond)
		return;

	//eine angehängte Index-DB löst sich mit dem Schließen der store-Verbindung
	if (dbase_zond->zond_dbase_store)
		g_object_unref(dbase_zond->zond_dbase_store);
	if (dbase_zond->zond_dbase_work)
		g_object_unref(dbase_zond->zond_dbase_work);
	g_free(dbase_zond);
}

ZondDBase* dbase_zond_primary(DBaseZond *dbase_zond) {
	return dbase_zond->zond_dbase_work ? dbase_zond->zond_dbase_work :
			dbase_zond->zond_dbase_store;
}

/* Zuerst die primäre DB, bei Treffer fertig (REF_PRIMARY). Sonst, mit work, auch den store: ein
 * Treffer nur dort heißt, die Anbindung ist in work schon entfernt, aber noch nicht gespeichert. */
gint dbase_zond_test_path(DBaseZond *dbase_zond, gchar const *path_prefix,
		DBaseZondRef *ref, GError **error) {
	gint rc = 0;

	*ref = DBASE_ZOND_REF_NONE;

	rc = zond_dbase_test_path(dbase_zond_primary(dbase_zond), path_prefix, error);
	if (rc == -1)
		return -1;
	if (rc == 1) {
		*ref = DBASE_ZOND_REF_PRIMARY;

		return 0;
	}

	if (dbase_zond->zond_dbase_work) {
		rc = zond_dbase_test_path(dbase_zond->zond_dbase_store, path_prefix, error);
		if (rc == -1)
			return -1;
		if (rc == 1)
			*ref = DBASE_ZOND_REF_ONLY_STORE;
	}

	return 0;
}

gint dbase_zond_test_path_section(DBaseZond *dbase_zond, gchar const *filepart,
		gchar const *section, gboolean smaller, DBaseZondRef *ref, GError **error) {
	gint rc = 0;

	*ref = DBASE_ZOND_REF_NONE;

	rc = zond_dbase_test_path_section(dbase_zond_primary(dbase_zond), filepart, section,
			smaller, error);
	if (rc == -1)
		return -1;
	if (rc == 1) {
		*ref = DBASE_ZOND_REF_PRIMARY;

		return 0;
	}

	if (dbase_zond->zond_dbase_work) {
		rc = zond_dbase_test_path_section(dbase_zond->zond_dbase_store, filepart, section,
				smaller, error);
		if (rc == -1)
			return -1;
		if (rc == 1)
			*ref = DBASE_ZOND_REF_ONLY_STORE;
	}

	return 0;
}
