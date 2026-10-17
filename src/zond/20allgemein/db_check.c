/*
 zond (db_check.c) - Akten, Beweisstücke, Unterlagen
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

#include <glib.h>
#include <sqlite3.h>

#include "../../sond_log_and_error.h"

#include "../zond_dbase.h"

#include "db_check.h"

#define DB_CHECK_MAX_BEISPIELE 20

//Mehrere Köpfe: Elternknoten, die mehr als einen Knoten mit older_sibling_ID=0 haben
#define SQL_ELTERN_MIT_KOEPFEN \
	"SELECT parent_ID FROM knoten WHERE older_sibling_ID=0 AND parent_ID<>0 " \
	"GROUP BY parent_ID HAVING count(*)>1"

void db_check_ketten_clear(DbCheckKetten *erg) {
	if (erg->beispiele)
		g_ptr_array_unref(erg->beispiele);
	erg->beispiele = NULL;

	return;
}

gboolean db_check_ketten_ok(DbCheckKetten const *erg) {
	return !erg->n_koepfe && !erg->n_gabelungen && !erg->n_luecken
			&& !erg->n_unerreichbar;
}

static void set_sqlite_error(sqlite3 *db, gchar const *func, GError **error) {
	g_set_error(error, SOND_ERROR, 0, "%s\n%s", func, sqlite3_errmsg(db));

	return;
}

/* Führt eine Abfrage aus, die je Befund eine Zeile (Elternknoten, Beschreibung)
 * liefert. Zählt die Zeilen und merkt sich die ersten als Beispiele. */
static gint zaehle_befunde(sqlite3 *db, gchar const *sql, gchar const *titel,
		gint *anzahl, GPtrArray *beispiele, GError **error) {
	sqlite3_stmt *stmt = NULL;
	gint rc = 0;

	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		set_sqlite_error(db, __func__, error);

		return -1;
	}

	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		(*anzahl)++;

		if (beispiele->len < DB_CHECK_MAX_BEISPIELE)
			g_ptr_array_add(beispiele,
					g_strdup_printf("%s: Elternknoten %s, %s", titel,
							(gchar const*) sqlite3_column_text(stmt, 0),
							(gchar const*) sqlite3_column_text(stmt, 1)));
	}
	sqlite3_finalize(stmt);

	if (rc != SQLITE_DONE) {
		set_sqlite_error(db, __func__, error);

		return -1;
	}

	return 0;
}

gint db_check_ketten(ZondDBase *zond_dbase, DbCheckKetten *erg, GError **error) {
	sqlite3 *db = zond_dbase_get_dbase(zond_dbase);
	gint rc = 0;

	erg->n_koepfe = 0;
	erg->n_gabelungen = 0;
	erg->n_luecken = 0;
	erg->n_unerreichbar = 0;
	if (erg->beispiele)
		g_ptr_array_unref(erg->beispiele);
	erg->beispiele = g_ptr_array_new_with_free_func(g_free);

	rc = zaehle_befunde(db,
			"SELECT parent_ID, 'Knoten '||group_concat(ID) FROM knoten "
			"WHERE older_sibling_ID=0 AND parent_ID<>0 "
			"GROUP BY parent_ID HAVING count(*)>1;",
			"Mehrere Köpfe", &erg->n_koepfe, erg->beispiele, error);
	if (rc)
		return -1;

	rc = zaehle_befunde(db,
			"SELECT parent_ID, 'jüngere Geschwister von '||older_sibling_ID||': '"
			"||group_concat(ID) FROM knoten WHERE older_sibling_ID<>0 "
			"GROUP BY parent_ID, older_sibling_ID HAVING count(*)>1;",
			"Gabelung", &erg->n_gabelungen, erg->beispiele, error);
	if (rc)
		return -1;

	rc = zaehle_befunde(db,
			"SELECT k.parent_ID, 'Knoten '||k.ID||' hat älteres Geschwister '"
			"||k.older_sibling_ID FROM knoten k "
			"LEFT JOIN knoten o ON o.ID=k.older_sibling_ID "
			"WHERE k.older_sibling_ID<>0 "
			"AND (o.ID IS NULL OR o.parent_ID<>k.parent_ID);",
			"Lücke", &erg->n_luecken, erg->beispiele, error);
	if (rc)
		return -1;

	//Erreichbarkeit; Elternknoten mit Mehrfachköpfen sind oben schon gezählt
	rc = zaehle_befunde(db,
			"WITH RECURSIVE ch(par, id) AS ( "
			"SELECT parent_ID, ID FROM knoten "
			"WHERE older_sibling_ID=0 AND parent_ID<>0 "
			"UNION ALL SELECT k.parent_ID, k.ID FROM knoten k JOIN ch "
			"ON k.older_sibling_ID=ch.id AND k.parent_ID=ch.par), "
			"reach AS (SELECT par, count(*) AS n FROM ch GROUP BY par), "
			"tot AS (SELECT parent_ID AS par, count(*) AS n FROM knoten "
			"WHERE parent_ID<>0 GROUP BY parent_ID) "
			"SELECT tot.par, 'Kinder '||tot.n||', erreichbar '||ifnull(reach.n,0) "
			"FROM tot LEFT JOIN reach USING(par) WHERE reach.n IS NOT tot.n "
			"AND tot.par NOT IN (" SQL_ELTERN_MIT_KOEPFEN ");",
			"Nicht erreichbar", &erg->n_unerreichbar, erg->beispiele, error);
	if (rc)
		return -1;

	return 0;
}

//Ein Wert (Integer) aus einer Abfrage mit bis zu zwei gebundenen Integer-Parametern
static gint hole_wert(sqlite3 *db, gchar const *sql, gint64 p1, gint64 p2,
		gint64 *wert, GError **error) {
	sqlite3_stmt *stmt = NULL;
	gint rc = 0;

	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		set_sqlite_error(db, __func__, error);

		return -1;
	}

	if (sqlite3_bind_parameter_count(stmt) >= 1)
		sqlite3_bind_int64(stmt, 1, p1);
	if (sqlite3_bind_parameter_count(stmt) >= 2)
		sqlite3_bind_int64(stmt, 2, p2);

	rc = sqlite3_step(stmt);
	if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
		set_sqlite_error(db, __func__, error);
		sqlite3_finalize(stmt);

		return -1;
	}
	*wert = (rc == SQLITE_ROW) ? sqlite3_column_int64(stmt, 0) : 0;
	sqlite3_finalize(stmt);

	return 0;
}

//Spalte 0 aller Zeilen als gint64
static gint hole_liste(sqlite3 *db, gchar const *sql, gint64 p1, GArray *liste,
		GError **error) {
	sqlite3_stmt *stmt = NULL;
	gint rc = 0;

	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		set_sqlite_error(db, __func__, error);

		return -1;
	}

	if (sqlite3_bind_parameter_count(stmt) >= 1)
		sqlite3_bind_int64(stmt, 1, p1);

	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		gint64 wert = sqlite3_column_int64(stmt, 0);

		g_array_append_val(liste, wert);
	}
	sqlite3_finalize(stmt);

	if (rc != SQLITE_DONE) {
		set_sqlite_error(db, __func__, error);

		return -1;
	}

	return 0;
}

/* Schwanz der Kette ab kopf: Knoten ohne jüngeres Geschwister. *tail = 0, wenn
 * die Kette nicht eindeutig ist (Gabelung) oder zu lang (Zyklus). */
static gint finde_schwanz(sqlite3 *db, gint64 eltern, gint64 kopf,
		gint64 n_kinder, gint64 *tail, GError **error) {
	gint64 aktuell = kopf;

	*tail = 0;

	for (gint64 schritt = 0; schritt <= n_kinder; schritt++) {
		g_autoptr(GArray) juenger = g_array_new(FALSE, FALSE, sizeof(gint64));
		sqlite3_stmt *stmt = NULL;
		gint rc = 0;

		rc = sqlite3_prepare_v2(db,
				"SELECT ID FROM knoten WHERE older_sibling_ID=?1 "
				"AND parent_ID=?2 LIMIT 2;", -1, &stmt, NULL);
		if (rc != SQLITE_OK) {
			set_sqlite_error(db, __func__, error);

			return -1;
		}
		sqlite3_bind_int64(stmt, 1, aktuell);
		sqlite3_bind_int64(stmt, 2, eltern);
		while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
			gint64 id = sqlite3_column_int64(stmt, 0);

			g_array_append_val(juenger, id);
		}
		sqlite3_finalize(stmt);
		if (rc != SQLITE_DONE) {
			set_sqlite_error(db, __func__, error);

			return -1;
		}

		if (juenger->len == 0) {
			*tail = aktuell;

			return 0;
		}
		if (juenger->len > 1)
			return 0; //Gabelung

		aktuell = g_array_index(juenger, gint64, 0);
	}

	return 0; //Kette länger als Kinderzahl: Zyklus
}

/* Ein Elternknoten. *repariert = FALSE, wenn er übersprungen wurde (Gabelung,
 * nicht eindeutige Kette). Schreibt erst, wenn alle Schwänze feststehen. */
static gint repariere_eltern(sqlite3 *db, gint64 eltern, gboolean *repariert,
		GError **error) {
	g_autoptr(GArray) koepfe = g_array_new(FALSE, FALSE, sizeof(gint64));
	g_autoptr(GArray) schwaenze = g_array_new(FALSE, FALSE, sizeof(gint64));
	gint64 gabeln = 0;
	gint64 n_kinder = 0;
	gint rc = 0;

	*repariert = FALSE;

	rc = hole_liste(db, "SELECT ID FROM knoten WHERE parent_ID=?1 "
			"AND older_sibling_ID=0 ORDER BY ID;", eltern, koepfe, error);
	if (rc)
		return -1;

	rc = hole_wert(db, "SELECT count(*) FROM (SELECT 1 FROM knoten "
			"WHERE parent_ID=?1 AND older_sibling_ID<>0 "
			"GROUP BY older_sibling_ID HAVING count(*)>1);", eltern, 0, &gabeln,
			error);
	if (rc)
		return -1;
	if (gabeln > 0 || koepfe->len < 2)
		return 0;

	rc = hole_wert(db, "SELECT count(*) FROM knoten WHERE parent_ID=?1;",
			eltern, 0, &n_kinder, error);
	if (rc)
		return -1;

	for (guint i = 0; i + 1 < koepfe->len; i++) {
		gint64 tail = 0;

		rc = finde_schwanz(db, eltern, g_array_index(koepfe, gint64, i),
				n_kinder, &tail, error);
		if (rc)
			return -1;
		if (!tail)
			return 0;

		g_array_append_val(schwaenze, tail);
	}

	for (guint i = 0; i + 1 < koepfe->len; i++) {
		sqlite3_stmt *stmt = NULL;

		rc = sqlite3_prepare_v2(db,
				"UPDATE knoten SET older_sibling_ID=?1 WHERE ID=?2;", -1, &stmt,
				NULL);
		if (rc != SQLITE_OK) {
			set_sqlite_error(db, __func__, error);

			return -1;
		}
		sqlite3_bind_int64(stmt, 1, g_array_index(schwaenze, gint64, i));
		sqlite3_bind_int64(stmt, 2, g_array_index(koepfe, gint64, i + 1));
		rc = sqlite3_step(stmt);
		sqlite3_finalize(stmt);
		if (rc != SQLITE_DONE) {
			set_sqlite_error(db, __func__, error);

			return -1;
		}
	}

	*repariert = TRUE;

	return 0;
}

gint db_repair_koepfe(ZondDBase *zond_dbase, gint *n_repariert,
		gint *n_uebersprungen, GError **error) {
	sqlite3 *db = zond_dbase_get_dbase(zond_dbase);
	g_autoptr(GArray) eltern = g_array_new(FALSE, FALSE, sizeof(gint64));
	gint rep = 0;
	gint skip = 0;
	gint64 rest = 0;
	gint rc = 0;

	if (n_repariert)
		*n_repariert = 0;
	if (n_uebersprungen)
		*n_uebersprungen = 0;

	rc = hole_liste(db, SQL_ELTERN_MIT_KOEPFEN " ORDER BY parent_ID;", 0,
			eltern, error);
	if (rc)
		return -1;
	if (eltern->len == 0)
		return 0;

	rc = zond_dbase_begin(zond_dbase, error);
	if (rc)
		return -1;

	for (guint i = 0; i < eltern->len; i++) {
		gboolean repariert = FALSE;

		rc = repariere_eltern(db, g_array_index(eltern, gint64, i), &repariert,
				error);
		if (rc) {
			zond_dbase_rollback(zond_dbase, NULL);

			return -1;
		}

		if (repariert)
			rep++;
		else
			skip++;
	}

	//Kontrolle: Mehrfachköpfe dürfen nur noch bei den übersprungenen bleiben
	rc = hole_wert(db, "SELECT count(*) FROM (" SQL_ELTERN_MIT_KOEPFEN ");", 0, 0,
			&rest, error);
	if (rc) {
		zond_dbase_rollback(zond_dbase, NULL);

		return -1;
	}
	if (rest != skip) {
		g_set_error(error, SOND_ERROR, 0,
				"%s\nKontrolle nach Reparatur fehlgeschlagen "
				"(%" G_GINT64_FORMAT " statt %d Elternknoten mit Mehrfachköpfen) "
				"- zurückgerollt", __func__, rest, skip);
		zond_dbase_rollback(zond_dbase, NULL);

		return -1;
	}

	rc = zond_dbase_commit(zond_dbase, error);
	if (rc) {
		zond_dbase_rollback(zond_dbase, NULL);

		return -1;
	}

	if (n_repariert)
		*n_repariert = rep;
	if (n_uebersprungen)
		*n_uebersprungen = skip;

	return 0;
}
