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

#include <string.h>
#include <stdio.h>
#include <glib.h>
#include <sqlite3.h>

#include "../../sond_log_and_error.h"

#include "../zond_dbase.h"

#include "db_check.h"

#define DB_CHECK_MAX_INTEGRITAET 50

//Typen in "knoten"
#define TYP_WURZEL 0
#define TYP_STRUKT 1
#define TYP_COPY 3
#define TYP_FILE_PART 5
#define TYP_VIRT_PDF 6

/* ---- Grundfunktionen -------------------------------------------------------- */

static void dbzweig_free(DbZweig *z) {
	if (!z)
		return;

	if (z->knoten_ids)
		g_array_unref(z->knoten_ids);
	g_free(z);

	return;
}

void db_befund_free(DbBefund *befund) {
	if (!befund)
		return;

	if (befund->zweige)
		g_ptr_array_unref(befund->zweige);
	g_free(befund->text);
	g_free(befund);

	return;
}

void db_check_ergebnis_clear(DbCheckErgebnis *erg) {
	if (erg->befunde)
		g_ptr_array_unref(erg->befunde);
	erg->befunde = NULL;

	return;
}

void db_aktion_ergebnis_clear(DbAktionErgebnis *erg) {
	if (erg->meldungen)
		g_ptr_array_unref(erg->meldungen);
	memset(erg, 0, sizeof(DbAktionErgebnis));

	return;
}

gchar const* db_befund_art_name(DbBefundArt art) {
	switch (art) {
	case DB_BEFUND_VERZWEIGUNG:
		return "Verzweigung";
	case DB_BEFUND_LUECKE:
		return "Lücke";
	case DB_BEFUND_UNERREICHBAR:
		return "Nicht erreichbar";
	case DB_BEFUND_WAISE:
		return "Elternknoten fehlt";
	case DB_BEFUND_INTEGRITAET:
		return "Integrität";
	default:
		return "?";
	}
}

gint db_check_anzahl(DbCheckErgebnis const *erg, DbBefundArt art) {
	gint n = 0;

	if (!erg->befunde)
		return 0;

	for (guint i = 0; i < erg->befunde->len; i++)
		if (((DbBefund*) g_ptr_array_index(erg->befunde, i))->arten
				& (1u << art))
			n++;

	return n;
}

gboolean db_check_ok(DbCheckErgebnis const *erg) {
	return !erg->befunde || erg->befunde->len == 0;
}

gint db_check_anzahl_reparierbar(DbCheckErgebnis const *erg) {
	gint n = 0;

	if (!erg->befunde)
		return 0;

	for (guint i = 0; i < erg->befunde->len; i++)
		if (((DbBefund*) g_ptr_array_index(erg->befunde, i))->reparierbar)
			n++;

	return n;
}

static void set_sqlite_error(sqlite3 *db, gchar const *func, GError **error) {
	g_set_error(error, SOND_ERROR, 0, "%s\n%s", func, sqlite3_errmsg(db));

	return;
}

//Typ des Knotens, -1 wenn es ihn nicht gibt
static gint hole_typ(sqlite3 *db, gint64 id) {
	sqlite3_stmt *stmt = NULL;
	gint typ = -1;

	if (sqlite3_prepare_v2(db, "SELECT type FROM knoten WHERE ID=?1;", -1, &stmt,
			NULL) != SQLITE_OK)
		return -1;

	sqlite3_bind_int64(stmt, 1, id);
	if (sqlite3_step(stmt) == SQLITE_ROW)
		typ = sqlite3_column_int(stmt, 0);
	sqlite3_finalize(stmt);

	return typ;
}

//Text des Knotens für Meldungen
static gchar* hole_text(sqlite3 *db, gint64 id) {
	sqlite3_stmt *stmt = NULL;
	gchar *text = NULL;

	if (sqlite3_prepare_v2(db, "SELECT node_text FROM knoten WHERE ID=?1;", -1,
			&stmt, NULL) == SQLITE_OK) {
		sqlite3_bind_int64(stmt, 1, id);
		if (sqlite3_step(stmt) == SQLITE_ROW
				&& sqlite3_column_text(stmt, 0)
				&& *sqlite3_column_text(stmt, 0))
			text = g_strdup((gchar const*) sqlite3_column_text(stmt, 0));
		sqlite3_finalize(stmt);
	}

	return (text) ? text : g_strdup("(ohne Text)");
}

static DbBefund* befund_neu(gint64 eltern_id) {
	DbBefund *befund = g_new0(DbBefund, 1);

	befund->eltern_id = eltern_id;
	befund->eltern_typ = -1;
	befund->zweige = g_ptr_array_new_with_free_func((GDestroyNotify) dbzweig_free);

	return befund;
}

/* ---- Kinder eines Elternknotens als Graph ---------------------------------- */

typedef struct {
	GArray *ids; //gint64, aufsteigend
	GArray *older; //gint64, parallel zu ids
	GHashTable *pos; //ID -> Index + 1
	GArray **succ; //je Index: Indizes der jüngeren Geschwister (aufsteigend) oder NULL
	GArray *koepfe; //gint: Knoten mit older_sibling_ID = 0
	GArray *luecken; //gint: Knoten, deren älteres Geschwister nicht dazugehört
} Gruppe;

static void gruppe_leeren(Gruppe *g) {
	if (g->succ)
		for (guint i = 0; i < g->ids->len; i++)
			if (g->succ[i])
				g_array_unref(g->succ[i]);
	g_free(g->succ);
	if (g->pos)
		g_hash_table_unref(g->pos);
	if (g->koepfe)
		g_array_unref(g->koepfe);
	if (g->luecken)
		g_array_unref(g->luecken);
	if (g->ids)
		g_array_unref(g->ids);
	if (g->older)
		g_array_unref(g->older);
	memset(g, 0, sizeof(Gruppe));

	return;
}

//übernimmt ids und older
static void gruppe_aufbauen(Gruppe *g, GArray *ids, GArray *older) {
	guint n = ids->len;

	g->ids = ids;
	g->older = older;
	g->pos = g_hash_table_new(g_int64_hash, g_int64_equal);
	g->succ = g_new0(GArray*, n);
	g->koepfe = g_array_new(FALSE, FALSE, sizeof(gint));
	g->luecken = g_array_new(FALSE, FALSE, sizeof(gint));

	for (guint i = 0; i < n; i++)
		g_hash_table_insert(g->pos, &g_array_index(ids, gint64, i),
				GUINT_TO_POINTER(i + 1));

	for (guint i = 0; i < n; i++) {
		gint64 o = g_array_index(older, gint64, i);
		gint index = (gint) i;

		if (o == 0)
			g_array_append_val(g->koepfe, index);
		else {
			guint p = GPOINTER_TO_UINT(g_hash_table_lookup(g->pos, &o));

			if (!p)
				g_array_append_val(g->luecken, index);
			else {
				if (!g->succ[p - 1])
					g->succ[p - 1] = g_array_new(FALSE, FALSE, sizeof(gint));
				g_array_append_val(g->succ[p - 1], index);
			}
		}
	}

	return;
}

typedef struct {
	gint idx;
	DbZweigArt art;
	gint64 vorgaenger;
} Start;

static Start* start_neu(gint idx, DbZweigArt art, gint64 vorgaenger) {
	Start *s = g_new(Start, 1);

	s->idx = idx;
	s->art = art;
	s->vorgaenger = vorgaenger;

	return s;
}

/* Zerlegt die Kinder in Zweige. Der erste Zweig ist der sichtbare, wenn es einen
 * Kopf gibt: Start beim Kopf mit der kleinsten ID, dann jeweils zum jüngeren
 * Geschwister mit der kleinsten ID - wie beim Laden des Baums. Danach die
 * übrigen Köpfe, die Knoten mit Lücke, die Zweige an Gabelungen und zuletzt
 * Zyklen. Jedes Kind kommt in genau einem Zweig vor. */
static GPtrArray* gruppe_zweige(Gruppe *g) {
	guint n = g->ids->len;
	GPtrArray *zweige = g_ptr_array_new_with_free_func((GDestroyNotify) dbzweig_free);
	gboolean *besucht = g_new0(gboolean, n);
	GQueue queue = G_QUEUE_INIT;
	guint naechster = 0;

	for (guint i = 0; i < g->koepfe->len; i++)
		g_queue_push_tail(&queue,
				start_neu(g_array_index(g->koepfe, gint, i), DB_ZWEIG_KOPF, 0));
	for (guint i = 0; i < g->luecken->len; i++)
		g_queue_push_tail(&queue,
				start_neu(g_array_index(g->luecken, gint, i), DB_ZWEIG_LUECKE, 0));

	while (TRUE) {
		Start *s = g_queue_pop_head(&queue);
		DbZweig *z = NULL;
		gint cur = 0;

		if (!s) {
			while (naechster < n && besucht[naechster])
				naechster++;
			if (naechster >= n)
				break;
			s = start_neu((gint) naechster, DB_ZWEIG_ZYKLUS, 0);
		}

		if (besucht[s->idx]) {
			g_free(s);

			continue;
		}

		z = g_new0(DbZweig, 1);
		z->art = s->art;
		z->vorgaenger_id = s->vorgaenger;
		z->knoten_ids = g_array_new(FALSE, FALSE, sizeof(gint64));
		cur = s->idx;
		g_free(s);

		while (cur >= 0 && !besucht[cur]) {
			gint next = -1;

			besucht[cur] = TRUE;
			g_array_append_val(z->knoten_ids,
					g_array_index(g->ids, gint64, cur));

			if (g->succ[cur])
				for (guint k = 0; k < g->succ[cur]->len; k++) {
					gint j = g_array_index(g->succ[cur], gint, k);

					if (besucht[j])
						continue;
					if (next < 0)
						next = j;
					else
						g_queue_push_tail(&queue, start_neu(j, DB_ZWEIG_GABEL,
								g_array_index(g->ids, gint64, cur)));
				}

			cur = next;
		}

		g_ptr_array_add(zweige, z);
	}

	g_free(besucht);

	if (zweige->len
			&& ((DbZweig*) g_ptr_array_index(zweige, 0))->art == DB_ZWEIG_KOPF)
		((DbZweig*) g_ptr_array_index(zweige, 0))->sichtbar = TRUE;

	return zweige;
}

/* Befund zu einem Elternknoten, falls die Kinder nicht in genau einer
 * lückenlosen Kette stehen oder der Elternknoten fehlt. */
static void gruppe_befund(sqlite3 *db, gint64 eltern, Gruppe *g,
		GPtrArray *befunde) {
	GPtrArray *zweige = gruppe_zweige(g);
	gint typ = hole_typ(db, eltern);
	gboolean gesund = zweige->len == 1
			&& ((DbZweig*) g_ptr_array_index(zweige, 0))->art == DB_ZWEIG_KOPF;
	DbBefund *b = NULL;

	if (gesund && typ >= 0) {
		g_ptr_array_unref(zweige);

		return;
	}

	b = befund_neu(eltern);
	b->eltern_typ = typ;
	g_ptr_array_unref(b->zweige);
	b->zweige = zweige;

	if (typ < 0) {
		b->arten = 1u << DB_BEFUND_WAISE;
		b->reparierbar = FALSE; //Rettung folgt später, bisher nur melden
	} else {
		b->reparierbar = TRUE;
		for (guint i = 0; i < zweige->len; i++) {
			DbZweig *z = g_ptr_array_index(zweige, i);

			if (z->art == DB_ZWEIG_GABEL || (z->art == DB_ZWEIG_KOPF && i > 0))
				b->arten |= 1u << DB_BEFUND_VERZWEIGUNG;
			else if (z->art == DB_ZWEIG_LUECKE)
				b->arten |= 1u << DB_BEFUND_LUECKE;
			else if (z->art == DB_ZWEIG_ZYKLUS)
				b->arten |= 1u << DB_BEFUND_UNERREICHBAR;
		}
	}

	g_ptr_array_add(befunde, b);

	return;
}

/* ---- Prüfung ---------------------------------------------------------------- */

static DbBefund* integritaet_befund(gchar *text) {
	DbBefund *b = befund_neu(0);

	b->arten = 1u << DB_BEFUND_INTEGRITAET;
	b->text = text;
	b->reparierbar = FALSE;

	return b;
}

static gint integritaet_pruefen(sqlite3 *db, GPtrArray *befunde, GError **error) {
	sqlite3_stmt *stmt = NULL;
	gint rc = 0;
	gint n = 0;

	rc = sqlite3_prepare_v2(db, "PRAGMA quick_check;", -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		set_sqlite_error(db, __func__, error);

		return -1;
	}
	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		gchar const *zeile = (gchar const*) sqlite3_column_text(stmt, 0);

		if (!g_strcmp0(zeile, "ok") || n >= DB_CHECK_MAX_INTEGRITAET)
			continue;

		g_ptr_array_add(befunde,
				integritaet_befund(g_strdup_printf("quick_check: %s", zeile)));
		n++;
	}
	sqlite3_finalize(stmt);
	if (rc != SQLITE_DONE) {
		set_sqlite_error(db, __func__, error);

		return -1;
	}

	n = 0;
	rc = sqlite3_prepare_v2(db, "PRAGMA foreign_key_check;", -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		set_sqlite_error(db, __func__, error);

		return -1;
	}
	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		if (n >= DB_CHECK_MAX_INTEGRITAET)
			continue;

		g_ptr_array_add(befunde, integritaet_befund(
				g_strdup_printf("foreign_key_check: Tabelle %s, Zeile %s "
						"verweist auf fehlende Zeile in %s",
						(gchar const*) sqlite3_column_text(stmt, 0),
						(gchar const*) sqlite3_column_text(stmt, 1),
						(gchar const*) sqlite3_column_text(stmt, 2))));
		n++;
	}
	sqlite3_finalize(stmt);
	if (rc != SQLITE_DONE) {
		set_sqlite_error(db, __func__, error);

		return -1;
	}

	return 0;
}

gint db_check_ausfuehren(ZondDBase *zond_dbase, gboolean mit_integritaet,
		DbCheckErgebnis *erg, GError **error) {
	sqlite3 *db = zond_dbase_get_dbase(zond_dbase);
	sqlite3_stmt *stmt = NULL;
	GArray *ids = NULL;
	GArray *older = NULL;
	gint64 eltern = 0;
	gint rc = 0;

	db_check_ergebnis_clear(erg);
	erg->befunde = g_ptr_array_new_with_free_func((GDestroyNotify) db_befund_free);

	//alle Kinder von Elternknoten > 0, gruppiert nach Elternknoten
	rc = sqlite3_prepare_v2(db, "SELECT parent_ID, ID, older_sibling_ID "
			"FROM knoten WHERE parent_ID<>0 ORDER BY parent_ID, ID;", -1, &stmt,
			NULL);
	if (rc != SQLITE_OK) {
		set_sqlite_error(db, __func__, error);

		return -1;
	}

	ids = g_array_new(FALSE, FALSE, sizeof(gint64));
	older = g_array_new(FALSE, FALSE, sizeof(gint64));

	while (TRUE) {
		gint64 p = 0;
		gboolean ende = FALSE;

		rc = sqlite3_step(stmt);
		if (rc == SQLITE_ROW)
			p = sqlite3_column_int64(stmt, 0);
		else if (rc == SQLITE_DONE)
			ende = TRUE;
		else {
			set_sqlite_error(db, __func__, error);
			sqlite3_finalize(stmt);
			g_array_unref(ids);
			g_array_unref(older);

			return -1;
		}

		if ((ende || p != eltern) && ids->len) {
			Gruppe g = { 0 };

			gruppe_aufbauen(&g, ids, older);
			gruppe_befund(db, eltern, &g, erg->befunde);
			gruppe_leeren(&g); //gibt ids und older frei

			ids = g_array_new(FALSE, FALSE, sizeof(gint64));
			older = g_array_new(FALSE, FALSE, sizeof(gint64));
		}
		if (ende)
			break;

		eltern = p;
		{
			gint64 id = sqlite3_column_int64(stmt, 1);
			gint64 o = sqlite3_column_int64(stmt, 2);

			g_array_append_val(ids, id);
			g_array_append_val(older, o);
		}
	}
	sqlite3_finalize(stmt);
	g_array_unref(ids);
	g_array_unref(older);

	if (mit_integritaet)
		return integritaet_pruefen(db, erg->befunde, error);

	return 0;
}

/* ---- Aktionen --------------------------------------------------------------- */

//Kinder eines Elternknotens laden; FALSE bei Fehler
static gboolean lade_gruppe(sqlite3 *db, gint64 eltern, Gruppe *g,
		GError **error) {
	sqlite3_stmt *stmt = NULL;
	GArray *ids = g_array_new(FALSE, FALSE, sizeof(gint64));
	GArray *older = g_array_new(FALSE, FALSE, sizeof(gint64));
	gint rc = 0;

	rc = sqlite3_prepare_v2(db, "SELECT ID, older_sibling_ID FROM knoten "
			"WHERE parent_ID=?1 ORDER BY ID;", -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		set_sqlite_error(db, __func__, error);
		g_array_unref(ids);
		g_array_unref(older);

		return FALSE;
	}
	sqlite3_bind_int64(stmt, 1, eltern);

	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		gint64 id = sqlite3_column_int64(stmt, 0);
		gint64 o = sqlite3_column_int64(stmt, 1);

		g_array_append_val(ids, id);
		g_array_append_val(older, o);
	}
	sqlite3_finalize(stmt);
	if (rc != SQLITE_DONE) {
		set_sqlite_error(db, __func__, error);
		g_array_unref(ids);
		g_array_unref(older);

		return FALSE;
	}

	gruppe_aufbauen(g, ids, older);

	return TRUE;
}

static gint setze_knoten(sqlite3 *db, gint64 id, gint64 parent, gint64 older,
		GError **error) {
	sqlite3_stmt *stmt = NULL;
	gint rc = 0;

	rc = sqlite3_prepare_v2(db, "UPDATE knoten SET parent_ID=?1, "
			"older_sibling_ID=?2 WHERE ID=?3;", -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		set_sqlite_error(db, __func__, error);

		return -1;
	}
	sqlite3_bind_int64(stmt, 1, parent);
	sqlite3_bind_int64(stmt, 2, older);
	sqlite3_bind_int64(stmt, 3, id);
	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	if (rc != SQLITE_DONE) {
		set_sqlite_error(db, __func__, error);

		return -1;
	}

	return 0;
}

static void melde(DbAktionErgebnis *erg, gchar *text) {
	g_ptr_array_add(erg->meldungen, text);

	return;
}

/* Anzahl nicht sichtbarer Zweige und Zweige insgesamt einer frisch geladenen
 * Gruppe; -1 bei Fehler */
static gint zaehle_zweige(sqlite3 *db, gint64 eltern, gint *n_gesamt,
		GError **error) {
	Gruppe g = { 0 };
	GPtrArray *zweige = NULL;
	gint n = 0;

	if (!lade_gruppe(db, eltern, &g, error))
		return -1;
	zweige = gruppe_zweige(&g);
	for (guint i = 0; i < zweige->len; i++)
		if (!((DbZweig*) g_ptr_array_index(zweige, i))->sichtbar)
			n++;
	if (n_gesamt)
		*n_gesamt = (gint) zweige->len;
	g_ptr_array_unref(zweige);
	gruppe_leeren(&g);

	return n;
}

//ID-Liste für SQL (nur Zahlen)
static gchar* id_liste(GArray const *ids) {
	GString *s = g_string_new(NULL);

	for (guint i = 0; i < ids->len; i++)
		g_string_append_printf(s, "%s%" G_GINT64_FORMAT, i ? "," : "",
				g_array_index(ids, gint64, i));

	return g_string_free(s, FALSE);
}

static gint hole_zahl(sqlite3 *db, gchar const *sql, gint64 *zahl,
		GError **error) {
	sqlite3_stmt *stmt = NULL;
	gint rc = 0;

	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		set_sqlite_error(db, __func__, error);

		return -1;
	}
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
		set_sqlite_error(db, __func__, error);
		sqlite3_finalize(stmt);

		return -1;
	}
	*zahl = (rc == SQLITE_ROW) ? sqlite3_column_int64(stmt, 0) : 0;
	sqlite3_finalize(stmt);

	return 0;
}

/* Legt unter eltern (Wurzel oder Punkt) einen Sammelpunkt an, dessen älteres
 * Geschwister tail ist (0 = erstes Kind). */
gint db_sammelpunkt_neu(sqlite3 *db, gint64 eltern, gint64 tail, gint64 *sp_id,
		GError **error) {
	sqlite3_stmt *stmt = NULL;
	g_autoptr(GDateTime) jetzt = g_date_time_new_now_local();
	g_autofree gchar *datum = g_date_time_format(jetzt, "%d.%m.%Y");
	g_autofree gchar *titel = g_strdup_printf("Wiedergefundene Knoten "
			"(Reparatur %s)", datum);
	g_autofree gchar *notiz = g_strdup_printf("Diese Knoten waren in der "
			"Datenbank vorhanden, aber an einer Stelle, an der sie nicht "
			"hingehören oder nicht sichtbar waren. Sie wurden bei der Reparatur "
			"am %s hier abgelegt. Bitte einsortieren oder diesen Strukturpunkt "
			"löschen.", datum);
	gint rc = 0;

	rc = sqlite3_prepare_v2(db, "INSERT INTO knoten (parent_ID, older_sibling_ID, "
			"type, icon_name, node_text, text) VALUES (?1, ?2, 1, "
			"'emblem-important', ?3, ?4);", -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		set_sqlite_error(db, __func__, error);

		return -1;
	}
	sqlite3_bind_int64(stmt, 1, eltern);
	sqlite3_bind_int64(stmt, 2, tail);
	sqlite3_bind_text(stmt, 3, titel, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 4, notiz, -1, SQLITE_TRANSIENT);
	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	if (rc != SQLITE_DONE) {
		set_sqlite_error(db, __func__, error);

		return -1;
	}
	*sp_id = sqlite3_last_insert_rowid(db);

	return 0;
}

//Ende der sichtbaren Kette der Kinder von eltern (0, wenn es keine gibt)
gint db_check_sichtbarer_tail(sqlite3 *db, gint64 eltern, gint64 *tail,
		GError **error) {
	Gruppe g = { 0 };
	GPtrArray *zweige = NULL;

	*tail = 0;

	if (!lade_gruppe(db, eltern, &g, error))
		return -1;
	zweige = gruppe_zweige(&g);
	if (zweige->len) {
		DbZweig *z0 = g_ptr_array_index(zweige, 0);

		if (z0->sichtbar && z0->knoten_ids->len)
			*tail = g_array_index(z0->knoten_ids, gint64, z0->knoten_ids->len - 1);
	}
	g_ptr_array_unref(zweige);
	gruppe_leeren(&g);

	return 0;
}

/* Sammelpunkt am Ende der sichtbaren Kette von eltern, darunter die Knoten
 * der gewählten Zweige. */
static gint sammelpunkt_anlegen(sqlite3 *db, gint64 eltern, GPtrArray *zweige,
		GPtrArray *wahl, gint64 *sp_id, GError **error) {
	gint64 tail = 0;
	gint64 prev = 0;

	if (zweige->len) {
		DbZweig *z0 = g_ptr_array_index(zweige, 0);

		if (z0->sichtbar && z0->knoten_ids->len)
			tail = g_array_index(z0->knoten_ids, gint64, z0->knoten_ids->len - 1);
	}

	if (db_sammelpunkt_neu(db, eltern, tail, sp_id, error))
		return -1;

	for (guint i = 0; i < wahl->len; i++) {
		DbZweig *z = g_ptr_array_index(wahl, i);

		for (guint k = 0; k < z->knoten_ids->len; k++) {
			gint64 id = g_array_index(z->knoten_ids, gint64, k);

			if (setze_knoten(db, id, *sp_id, prev, error))
				return -1;
			prev = id;
		}
	}

	return 0;
}

/* ---- Abschnitte (FILE_PART) nach Lage ordnen ------------------------------- */

typedef struct {
	gint64 id;
	gint64 older;
	gint vs, vi, bs, bi;
	gboolean ok;
} Abschnitt;

//"{{seite,index}{seite,index}}" oder "{seite,index}" (PDF-Punkt)
static gboolean parse_abschnitt(gchar const *s, Abschnitt *a) {
	if (!s)
		return FALSE;

	if (sscanf(s, "{{%d,%d}{%d,%d}}", &a->vs, &a->vi, &a->bs, &a->bi) == 4)
		return TRUE;
	if (sscanf(s, "{%d,%d}", &a->vs, &a->vi) == 2) {
		a->bs = a->vs;
		a->bi = a->vi;

		return TRUE;
	}

	return FALSE;
}

static gint cmp_abschnitt(gconstpointer pa, gconstpointer pb) {
	Abschnitt const *a = pa;
	Abschnitt const *b = pb;

	if (a->ok != b->ok)
		return a->ok ? -1 : 1; //unlesbare ans Ende
	if (a->ok) {
		if (a->vs != b->vs)
			return (a->vs < b->vs) ? -1 : 1;
		if (a->vi != b->vi)
			return (a->vi < b->vi) ? -1 : 1;
	}

	return (a->id > b->id) - (a->id < b->id);
}

/* Baut die Kette der Abschnitte unter eltern nach ihrer Lage in der Datei neu
 * auf. *geaendert = TRUE, wenn geschrieben wurde. Bei Überschneidungen oder
 * unlesbaren Abschnitten wird nichts geändert (Meldung). */
static gint sortiere_abschnitte(sqlite3 *db, gint64 eltern, gchar const *name,
		DbAktionErgebnis *erg, gboolean *geaendert, GError **error) {
	sqlite3_stmt *stmt = NULL;
	g_autoptr(GArray) arr = g_array_new(FALSE, FALSE, sizeof(Abschnitt));
	gint rc = 0;

	*geaendert = FALSE;

	rc = sqlite3_prepare_v2(db, "SELECT ID, older_sibling_ID, section FROM "
			"knoten WHERE parent_ID=?1;", -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		set_sqlite_error(db, __func__, error);

		return -1;
	}
	sqlite3_bind_int64(stmt, 1, eltern);
	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		Abschnitt a = { 0 };

		a.id = sqlite3_column_int64(stmt, 0);
		a.older = sqlite3_column_int64(stmt, 1);
		a.ok = parse_abschnitt((gchar const*) sqlite3_column_text(stmt, 2), &a);
		g_array_append_val(arr, a);
	}
	sqlite3_finalize(stmt);
	if (rc != SQLITE_DONE) {
		set_sqlite_error(db, __func__, error);

		return -1;
	}

	g_array_sort(arr, cmp_abschnitt);

	for (guint i = 0; i < arr->len; i++) {
		Abschnitt *a = &g_array_index(arr, Abschnitt, i);

		if (!a->ok) {
			melde(erg, g_strdup_printf("»%s«: Abschnitte nicht lesbar, Reihenfolge "
					"nicht geändert", name));

			return 0;
		}
		if (i > 0) {
			Abschnitt *v = &g_array_index(arr, Abschnitt, i - 1);

			if (v->bs > a->vs || (v->bs == a->vs && v->bi >= a->vi)) {
				melde(erg, g_strdup_printf("»%s«: Abschnitte überschneiden sich, "
						"Reihenfolge nicht geändert", name));

				return 0;
			}
		}
	}

	for (guint i = 0; i < arr->len; i++) {
		Abschnitt *a = &g_array_index(arr, Abschnitt, i);
		gint64 neu = (i == 0) ? 0 : g_array_index(arr, Abschnitt, i - 1).id;

		if (neu == a->older)
			continue;
		if (setze_knoten(db, a->id, eltern, neu, error))
			return -1;
		*geaendert = TRUE;
	}

	return 0;
}

/* ---- Verwerfen -------------------------------------------------------------- */

#define SQL_TEILBAUM "WITH RECURSIVE sub(ID) AS (SELECT ID FROM knoten WHERE " \
	"ID IN (%s) UNION SELECT k.ID FROM knoten k JOIN sub " \
	"ON k.parent_ID=sub.ID) "

//1 = gelöscht, 0 = nicht gelöscht (Meldung), -1 = Fehler
static gint verwerfe_zweig(sqlite3 *db, DbZweig *z, gchar const *name,
		DbAktionErgebnis *erg, GError **error) {
	g_autofree gchar *liste = id_liste(z->knoten_ids);
	g_autofree gchar *sql = NULL;
	gint64 anzahl = 0;

	//Dateien und Abschnitte werden nie verworfen
	sql = g_strdup_printf(SQL_TEILBAUM "SELECT count(*) FROM knoten WHERE ID IN "
			"(SELECT ID FROM sub) AND type IN (%d,%d);", liste, TYP_FILE_PART,
			TYP_VIRT_PDF);
	if (hole_zahl(db, sql, &anzahl, error))
		return -1;
	if (anzahl) {
		melde(erg, g_strdup_printf("»%s«: Zweig nicht verworfen, er enthält "
				"Dateien oder Abschnitte", name));

		return 0;
	}
	g_free(sql);

	//Verweise von außerhalb: Anker, Copies, Links und Geschwisterverweise
	sql = g_strdup_printf(SQL_TEILBAUM "SELECT count(*) FROM knoten WHERE ID NOT "
			"IN (SELECT ID FROM sub) AND (link IN (SELECT ID FROM sub) OR "
			"older_sibling_ID IN (SELECT ID FROM sub));", liste);
	if (hole_zahl(db, sql, &anzahl, error))
		return -1;
	if (anzahl) {
		melde(erg, g_strdup_printf("»%s«: Zweig nicht verworfen, es verweist noch "
				"etwas von außerhalb darauf", name));

		return 0;
	}
	g_free(sql);

	//ausdrücklich samt Unterbäumen, nicht über die Kaskade der Fremdschlüssel
	sql = g_strdup_printf(SQL_TEILBAUM "DELETE FROM knoten WHERE ID IN (SELECT ID "
			"FROM sub);", liste);
	{
		gchar *errmsg = NULL;

		if (sqlite3_exec(db, sql, NULL, NULL, &errmsg) != SQLITE_OK) {
			g_set_error(error, SOND_ERROR, 0, "%s\n%s", __func__,
					errmsg ? errmsg : sqlite3_errmsg(db));
			sqlite3_free(errmsg);

			return -1;
		}
	}

	return 1;
}

/* ---- Aktion für einen Elternknoten ------------------------------------------ */

static gboolean erster_in(GArray const *erste, gint64 id) {
	for (guint i = 0; i < erste->len; i++)
		if (g_array_index(erste, gint64, i) == id)
			return TRUE;

	return FALSE;
}

static gint aktion_eltern(sqlite3 *db, DbAktion aktion, gint64 eltern,
		GArray const *erste, DbAktionErgebnis *erg, GError **error) {
	Gruppe g = { 0 };
	GPtrArray *zweige = NULL;
	g_autoptr(GPtrArray) wahl = g_ptr_array_new();
	g_autofree gchar *name = hole_text(db, eltern);
	gint typ = hole_typ(db, eltern);
	gint unsichtbar_vorher = 0;
	gint behandelt = 0;
	gint64 sp_id = 0;
	gboolean geaendert = FALSE;
	gint rc = 0;

	if (!lade_gruppe(db, eltern, &g, error))
		return -1;

	zweige = gruppe_zweige(&g);
	for (guint i = 0; i < zweige->len; i++) {
		DbZweig *z = g_ptr_array_index(zweige, i);

		if (z->sichtbar)
			continue;
		unsichtbar_vorher++;
		if (z->knoten_ids->len
				&& erster_in(erste, g_array_index(z->knoten_ids, gint64, 0)))
			g_ptr_array_add(wahl, z);
	}

	if (!wahl->len) {
		melde(erg, g_strdup_printf("»%s«: Der Zustand hat sich geändert, bitte "
				"erneut prüfen", name));
	} else if (aktion == DB_AKTION_REPARIEREN) {
		if (typ == TYP_FILE_PART) {
			rc = sortiere_abschnitte(db, eltern, name, erg, &geaendert, error);
			if (!rc && geaendert)
				behandelt = (gint) wahl->len;
		} else if (typ == TYP_WURZEL || typ == TYP_STRUKT || typ == TYP_COPY) {
			rc = sammelpunkt_anlegen(db, eltern, zweige, wahl, &sp_id, error);
			if (!rc) {
				geaendert = TRUE;
				behandelt = (gint) wahl->len;
				erg->n_sammelpunkte++;
			}
		} else
			melde(erg, g_strdup_printf("»%s«: Für diesen Knotentyp ist keine "
					"Reparatur vorgesehen", name));
	} else {
		if (typ == TYP_FILE_PART || typ == TYP_VIRT_PDF)
			melde(erg, g_strdup_printf("»%s«: Abschnitte werden nicht verworfen",
					name));
		else if (typ == TYP_WURZEL || typ == TYP_STRUKT || typ == TYP_COPY) {
			for (guint i = 0; i < wahl->len && !rc; i++) {
				gint r = verwerfe_zweig(db, g_ptr_array_index(wahl, i), name, erg,
						error);

				if (r < 0)
					rc = -1;
				else if (r > 0) {
					behandelt++;
					geaendert = TRUE;
				}
			}
		} else
			melde(erg, g_strdup_printf("»%s«: Für diesen Knotentyp ist kein "
					"Verwerfen vorgesehen", name));
	}

	g_ptr_array_unref(zweige);
	gruppe_leeren(&g);
	if (rc)
		return -1;

	//Kontrolle: der Zustand muss dem erwarteten entsprechen
	if (geaendert) {
		gint gesamt = 0;
		gint unsichtbar_nachher = zaehle_zweige(db, eltern, &gesamt, error);
		gint erwartet = unsichtbar_vorher - behandelt;
		gboolean ok = TRUE;

		if (unsichtbar_nachher < 0)
			return -1;

		if (typ == TYP_FILE_PART)
			ok = unsichtbar_nachher == 0 && gesamt == 1;
		else
			ok = unsichtbar_nachher == erwartet;

		if (ok && sp_id) {
			gint sp_gesamt = 0;
			gint sp_unsichtbar = zaehle_zweige(db, sp_id, &sp_gesamt, error);

			if (sp_unsichtbar < 0)
				return -1;
			ok = sp_unsichtbar == 0 && sp_gesamt == 1;
		}

		if (!ok) {
			g_set_error(error, SOND_ERROR, 0, "%s\nKontrolle nach der Änderung "
					"fehlgeschlagen (»%s«) - zurückgerollt", __func__, name);

			return -1;
		}

		erg->n_eltern++;
		erg->n_zweige += behandelt;
	}

	return 0;
}

gint db_aktion_ausfuehren_innen(ZondDBase *zond_dbase, DbAktion aktion,
		GPtrArray *befunde, DbAktionErgebnis *erg, GError **error) {
	sqlite3 *db = zond_dbase_get_dbase(zond_dbase);

	db_aktion_ergebnis_clear(erg);
	erg->meldungen = g_ptr_array_new_with_free_func(g_free);

	for (guint i = 0; befunde && i < befunde->len; i++) {
		DbBefund *b = g_ptr_array_index(befunde, i);
		g_autoptr(GArray) erste = g_array_new(FALSE, FALSE, sizeof(gint64));

		if (!b->reparierbar || b->eltern_id <= 0)
			continue;

		for (guint k = 0; k < b->zweige->len; k++) {
			DbZweig *z = g_ptr_array_index(b->zweige, k);

			if (z->gewaehlt && !z->sichtbar && z->knoten_ids->len)
				g_array_append_val(erste,
						g_array_index(z->knoten_ids, gint64, 0));
		}
		if (!erste->len)
			continue;

		if (aktion_eltern(db, aktion, b->eltern_id, erste, erg, error))
			return -1;
	}

	return 0;
}

static gint aktion_mit_transaktion(ZondDBase *zond_dbase, DbAktion aktion,
		GPtrArray *befunde, DbAktionErgebnis *erg, GError **error) {
	if (zond_dbase_begin(zond_dbase, error))
		return -1;

	if (db_aktion_ausfuehren_innen(zond_dbase, aktion, befunde, erg, error)) {
		zond_dbase_rollback(zond_dbase, NULL);

		return -1;
	}

	if (zond_dbase_commit(zond_dbase, error)) {
		zond_dbase_rollback(zond_dbase, NULL);

		return -1;
	}

	return 0;
}

//Fremdschlüssel sind währenddessen aus, s. db_regeln_ausfuehren()
gint db_aktion_ausfuehren(ZondDBase *zond_dbase, DbAktion aktion,
		GPtrArray *befunde, DbAktionErgebnis *erg, GError **error) {
	sqlite3 *db = zond_dbase_get_dbase(zond_dbase);
	gint rc = 0;

	if (sqlite3_exec(db, "PRAGMA foreign_keys=OFF;", NULL, NULL, NULL)
			!= SQLITE_OK) {
		set_sqlite_error(db, __func__, error);

		return -1;
	}

	rc = aktion_mit_transaktion(zond_dbase, aktion, befunde, erg, error);

	sqlite3_exec(db, "PRAGMA foreign_keys=ON;", NULL, NULL, NULL);

	return rc;
}
