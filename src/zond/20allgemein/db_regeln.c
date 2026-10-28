/*
 zond (db_regeln.c) - Akten, Beweisstücke, Unterlagen
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
#include <glib.h>
#include <sqlite3.h>

#include "../../sond_log_and_error.h"

#include "../zond_dbase.h"
#include "../zond_version.h"

#include "db_check.h"
#include "db_regeln.h"

#define M(a) (1u << (a))

#define DB_REGELN_MAX_INTEGRITAET 50

//Spalten, die in einer Zeile gesetzt sind (nicht NULL)
#define F_LINK 1u
#define F_FP 2u
#define F_SECTION 4u
#define F_ICON 8u
#define F_NODETEXT 16u
#define F_TEXT 32u

/* ---- Katalog ---------------------------------------------------------------- */

static struct {
	gchar const *name;
	gchar const *titel;
	gchar const *kurz;
	gchar const *gilt; //ein Satz: Was gilt in einer gültigen Datei?
	gchar const *erklaerung; //ausführlich, für Tooltip und Legende
} const katalog[DB_NUM_REGELN] = {
	[DB_REGEL_A1] = { "A1", "Grundstruktur des Projekts beschädigt", "Grundstruktur",
		"Version, Bestandsverzeichnis und Auswertung brauchen je einen festen Grundeintrag.",
		"Jedes Projekt hat drei feste Grundeinträge: die Versionsangabe sowie den "
		"Anfang des Bestandsverzeichnisses und den Anfang der Auswertung. Hier "
		"fehlt einer oder ist verändert. Ohne diese Einträge lässt sich das "
		"Projekt nicht sauber laden.\n"
		"»Neu anlegen« bzw. »Angaben korrigieren« stellt sie wieder her." },
	[DB_REGEL_A2] = { "A2", "Einträge mit unbekanntem Typ", "Unbekannter Typ",
		"Jeder Eintrag ist ein Punkt, eine Anbindung, eine Copy, ein Link oder eine Datei/ein Abschnitt.",
		"Jeder Eintrag im Projekt hat eine feste Art: Punkt (Gliederung), Anbindung "
		"(angebundene Datei im Bestandsverzeichnis), Copy, Link (beide in "
		"der Auswertung) oder Datei/Abschnitt. Dieser Eintrag hat eine andere oder "
		"gar keine Art und wird von keiner Funktion des Programms verstanden.\n"
		"»In normalen Punkt umwandeln« macht ihn zu einem Punkt; sein Text bleibt "
		"erhalten. »Löschen« entfernt ihn mit allem, was darunter hängt." },
	[DB_REGEL_B1] = { "B1", "Einträge an unzulässiger Stelle", "Unzulässige Stelle",
		"Unter Anbindungen und Links steht nichts; Abschnitte stehen nur unter Dateien oder Abschnitten.",
		"Nicht jeder Eintrag darf unter jedem stehen. Anbindungen und "
		"Links haben keine Unterpunkte. Abschnitte stehen nur unter einer "
		"Datei oder einem Abschnitt, alles andere nur unter dem Anfang des "
		"Bestandsverzeichnisses bzw. der Auswertung, unter einem Punkt oder (in "
		"der Auswertung) unter einer Copy. Dieser Eintrag steht an einer Stelle, "
		"an die er nicht gehört.\n"
		"»In »Wiedergefundene Knoten« ablegen« verschiebt ihn samt allem, was "
		"darunter hängt, in einen neuen Sammelpunkt, von wo Sie ihn einsortieren "
		"können." },
	[DB_REGEL_B2] = { "B2", "Einträge im falschen Bereich", "Falscher Bereich",
		"Anbindungen gehören ins Bestandsverzeichnis, Copies und Links in die Auswertung.",
		"Das Bestandsverzeichnis enthält die angebundenen Dateien (Anbindungen), "
		"die Auswertung enthält Copies und Links. Dieser Eintrag steht im "
		"jeweils anderen Bereich.\n"
		"»In »Wiedergefundene Knoten« ablegen« legt ihn im richtigen Bereich "
		"ab." },
	[DB_REGEL_B3] = { "B3", "Einträge mit unpassenden Angaben", "Unpassende Angaben",
		"Jede Art von Eintrag hat bestimmte Angaben: Punkte Symbol und Text, Anbindungen und Links nur einen Verweis.",
		"Zu jeder Art von Eintrag gehören bestimmte Angaben: Ein Punkt hat Symbol "
		"und Text, aber keinen Verweis. Eine Anbindung oder ein Link hat nur "
		"einen Verweis und weder Text noch Symbol. Hier fehlt eine nötige Angabe "
		"oder es ist eine gesetzt, die nicht zur Art passt.\n"
		"»Angaben korrigieren« ergänzt Standardwerte (Symbol, Text) und entfernt "
		"Überflüssiges. Bei Dateien und Abschnitten ist keine automatische "
		"Korrektur möglich." },
	[DB_REGEL_D1] = { "D1", "Einträge, die im Baum nicht angezeigt werden", "Nicht angezeigt",
		"Die Einträge unter einem Eintrag bilden eine lückenlose Kette; nur eine solche wird angezeigt.",
		"Die Einträge unter einem Punkt sind in der Datei wie eine Kette "
		"aufgereiht: Jeder verweist auf den, der vor ihm kommt. Hier ist die "
		"Kette gerissen oder verzweigt sich, deshalb zeigt das Programm nur einen "
		"Teil an. Die übrigen Einträge sind in der Datei vorhanden, werden aber "
		"nicht angezeigt.\n"
		"»In »Wiedergefundene Knoten« ablegen« macht sie wieder sichtbar. Bei "
		"Abschnitten stellt »An die richtige Stelle einordnen« die Reihenfolge "
		"nach der Lage in der Datei her." },
	[DB_REGEL_D2] = { "D2", "Einträge ohne Verbindung zum Baum", "Ohne Verbindung",
		"Jeder Eintrag muss über seine übergeordneten Einträge bis zum Bestandsverzeichnis oder zur Auswertung führen.",
		"Jeder Eintrag gehört zu einem übergeordneten Eintrag, und dessen Kette "
		"führt bis zum Anfang des Bestandsverzeichnisses oder der Auswertung. "
		"Hier ist der übergeordnete Eintrag verschwunden, nie vorhanden gewesen "
		"oder die Zuordnung läuft im Kreis. Der Eintrag samt allem darunter ist "
		"vom Baum abgeschnitten und deshalb unsichtbar.\n"
		"»In »Wiedergefundene Knoten« ablegen« hängt ihn in den passenden Bereich "
		"(Anbindungen ins Bestandsverzeichnis, sonst in die Auswertung)." },
	[DB_REGEL_E1] = { "E1", "Verweise ohne Ziel", "Verweis ohne Ziel",
		"Anbindungen, Copies und Links müssen auf etwas verweisen, das es gibt.",
		"Anbindungen, Copies und Links zeigen auf etwas: eine Datei, einen "
		"Abschnitt oder einen anderen Eintrag. Hier ist das Ziel gelöscht worden "
		"oder wurde nie gesetzt. Eine solche Anbindung oder ein solcher Link ist "
		"nutzlos und kann beim Laden Fehler verursachen.\n"
		"Anbindungen und Links werden gelöscht. Eine Copy behält ihren "
		"eigenen Text und wird »in einen normalen Punkt umgewandelt«." },
	[DB_REGEL_E2] = { "E2", "Verweise auf ein unzulässiges Ziel", "Unzulässiges Ziel",
		"Anbindungen verweisen nur auf Dateien/Abschnitte, Links nie auf Links.",
		"Das Ziel des Verweises existiert, ist aber von einer Art, auf die so "
		"nicht verwiesen werden darf: Eine Anbindung verweist nur auf eine Datei "
		"oder einen Abschnitt, eine Copy auf eine Anbindung oder Datei, ein "
		"Link auf einen Punkt, eine Anbindung, eine Copy oder eine "
		"Datei, aber nie auf einen anderen Link.\n"
		"Behandlung wie bei »Verweise ohne Ziel«." },
	[DB_REGEL_E3] = { "E3", "Datei mehrfach angebunden", "Mehrfach angebunden",
		"Jede Datei und jeder Abschnitt darf nur einmal angebunden sein.",
		"Der Baum zeigt eine Datei oder einen Abschnitt nur an einer Stelle. Ist "
		"sie mehrfach angebunden, wandert der Eintrag zur zuletzt geladenen "
		"Anbindung; die übrigen erscheinen leer oder gar nicht.\n"
		"Die überzähligen Anbindungen lassen sich löschen; die Datei selbst "
		"bleibt dabei erhalten." },
	[DB_REGEL_F1] = { "F1", "Zustand der Datenbankdatei", "Datenbankdatei",
		"Die Datenbankdatei selbst muss für SQLite in Ordnung sein.",
		"SQLite hat bei der Prüfung der Datei selbst (nicht der Baumstruktur) "
		"einen Fehler gefunden, zum Beispiel eine beschädigte Seite oder einen "
		"Eintrag, der auf einen nicht vorhandenen verweist. Das wird nur "
		"angezeigt und lässt sich hier nicht beheben. Im Zweifel hilft die letzte "
		"Sicherungskopie." }
};

static gchar const *const akt_namen[DB_NUM_AKT] = {
	[DB_AKT_BEHALTEN] = "Nichts tun",
	[DB_AKT_LOESCHEN] = "Löschen",
	[DB_AKT_SAMMELPUNKT] = "In »Wiedergefundene Knoten« ablegen",
	[DB_AKT_EINORDNEN] = "An die richtige Stelle einordnen",
	[DB_AKT_UMWANDELN] = "In normalen Punkt umwandeln",
	[DB_AKT_BEREINIGEN] = "Angaben korrigieren",
	[DB_AKT_ANLEGEN] = "Neu anlegen"
};

gchar const* db_regel_gilt(DbRegel regel) {
	return (regel < DB_NUM_REGELN) ? katalog[regel].gilt : "";
}

gchar const* db_regel_erklaerung(DbRegel regel) {
	return (regel < DB_NUM_REGELN) ? katalog[regel].erklaerung : "";
}

gchar const* db_regel_kurz(DbRegel regel) {
	return (regel < DB_NUM_REGELN) ? katalog[regel].kurz : "?";
}

//Bezeichnung der Knotenart für den Nutzer
gchar const* db_typ_name(gint typ) {
	switch (typ) {
	case 0:
		return "Wurzel";
	case 1:
		return "Punkt";
	case 2:
		return "Anbindung";
	case 3:
		return "Copy";
	case 4:
		return "Link";
	case 5:
		return "Datei/Abschnitt";
	default:
		return "Eintrag unbekannter Art";
	}
}

static gchar const* grund_name(gint64 id) {
	return (id == 0) ? "Version" : ((id == 1) ? "Bestandsverzeichnis" :
			"Auswertung");
}

gchar const* db_regel_name(DbRegel regel) {
	return (regel < DB_NUM_REGELN) ? katalog[regel].name : "?";
}

gchar const* db_regel_titel(DbRegel regel) {
	return (regel < DB_NUM_REGELN) ? katalog[regel].titel : "?";
}

gchar const* db_akt_beschreibung(DbAkt akt) {
	switch (akt) {
	case DB_AKT_BEHALTEN:
		return "Der Eintrag bleibt unverändert, der Befund bleibt bestehen.";
	case DB_AKT_LOESCHEN:
		return "Der Eintrag wird samt allem, was darunter hängt, gelöscht. "
				"Dateien und Abschnitte werden nie gelöscht, ebenso nichts, "
				"worauf noch etwas von außerhalb verweist.";
	case DB_AKT_SAMMELPUNKT:
		return "Der Eintrag (bzw. die nicht angezeigten Einträge) wird mit allem "
				"Darunterliegenden in einen neuen Punkt »Wiedergefundene Knoten« "
				"gelegt. Dort können Sie ihn einsortieren oder löschen.";
	case DB_AKT_EINORDNEN:
		return "Die Abschnitte werden nach ihrer Lage in der Datei neu "
				"angeordnet.";
	case DB_AKT_UMWANDELN:
		return "Der Eintrag wird zu einem normalen Punkt. Sein Text bleibt "
				"erhalten, der Verweis entfällt.";
	case DB_AKT_BEREINIGEN:
		return "Fehlende Angaben (Symbol, Text) werden ergänzt, nicht passende "
				"entfernt.";
	case DB_AKT_ANLEGEN:
		return "Der fehlende Grundeintrag wird neu angelegt.";
	default:
		return "";
	}
}

gchar const* db_akt_name(DbAkt akt) {
	return (akt < DB_NUM_AKT) ? akt_namen[akt] : "?";
}

gint db_akt_rang(DbAkt akt) {
	switch (akt) {
	case DB_AKT_LOESCHEN:
		return 5;
	case DB_AKT_SAMMELPUNKT:
		return 4;
	case DB_AKT_EINORDNEN:
	case DB_AKT_UMWANDELN:
		return 3;
	case DB_AKT_BEREINIGEN:
		return 2;
	case DB_AKT_ANLEGEN:
		return 1;
	default:
		return 0;
	}
}

void db_mangel_free(DbMangel *mangel) {
	if (!mangel)
		return;

	if (mangel->knoten_ids)
		g_array_unref(mangel->knoten_ids);
	g_free(mangel->text);
	g_free(mangel);

	return;
}

void db_regel_ergebnis_clear(DbRegelErgebnis *erg) {
	if (erg->maengel)
		g_ptr_array_unref(erg->maengel);
	erg->maengel = NULL;

	return;
}

void db_regel_aktion_ergebnis_clear(DbRegelAktionErgebnis *erg) {
	if (erg->meldungen)
		g_ptr_array_unref(erg->meldungen);
	memset(erg, 0, sizeof(DbRegelAktionErgebnis));

	return;
}

gint db_regel_anzahl(DbRegelErgebnis const *erg, DbRegel regel) {
	gint n = 0;

	if (!erg->maengel)
		return 0;

	for (guint i = 0; i < erg->maengel->len; i++)
		if (((DbMangel*) g_ptr_array_index(erg->maengel, i))->regel == regel)
			n++;

	return n;
}

void db_mangel_setze_aktion(GPtrArray *maengel, DbMangel const *mangel,
		DbAkt aktion) {
	for (guint i = 0; i < maengel->len; i++) {
		DbMangel *m = g_ptr_array_index(maengel, i);

		if (m->knoten_id == mangel->knoten_id && (m->aktionen & M(aktion)))
			m->gewaehlt = aktion;
	}

	return;
}

void db_regel_ergebnis_standard(DbRegelErgebnis *erg) {
	for (guint i = 0; erg->maengel && i < erg->maengel->len; i++) {
		DbMangel *m = g_ptr_array_index(erg->maengel, i);

		m->gewaehlt = m->standard;
	}

	return;
}

static void set_sqlite_error(sqlite3 *db, gchar const *func, GError **error) {
	g_set_error(error, SOND_ERROR, 0, "%s\n%s", func, sqlite3_errmsg(db));

	return;
}

/* ---- Die Tabelle im Speicher ------------------------------------------------ */

typedef struct {
	gint64 id;
	gint64 parent;
	gint64 older;
	gint64 link;
	gint type; //-1 wenn NULL
	guint flags;
} Zeile;

typedef struct {
	GArray *rows; //Zeile, nach ID aufsteigend
	GHashTable *pos; //ID -> Index + 1
	GArray **kinder; //je Index: Indizes der Kinder (gint) oder NULL
	guint8 *baum; //1 Inhalt, 2 Auswertung, 3 Dateibaum, 4 ohne Wurzel
	gboolean *zyklus; //Knoten, der eine Schleife der Elternkette vertritt
} Ctx;

static Zeile* zeile(Ctx *c, guint i) {
	return &g_array_index(c->rows, Zeile, i);
}

//Index der Zeile mit dieser ID, -1 wenn es sie nicht gibt
static gint finde(Ctx *c, gint64 id) {
	return (gint) GPOINTER_TO_UINT(g_hash_table_lookup(c->pos, &id)) - 1;
}

static void ctx_leeren(Ctx *c) {
	if (c->kinder)
		for (guint i = 0; i < c->rows->len; i++)
			if (c->kinder[i])
				g_array_unref(c->kinder[i]);
	g_free(c->kinder);
	g_free(c->baum);
	g_free(c->zyklus);
	if (c->pos)
		g_hash_table_unref(c->pos);
	if (c->rows)
		g_array_unref(c->rows);
	memset(c, 0, sizeof(Ctx));

	return;
}

static gboolean typ_gueltig(gint typ) {
	return typ >= 1 && typ <= 5;
}

static gint ctx_laden(sqlite3 *db, Ctx *c, GError **error) {
	sqlite3_stmt *stmt = NULL;
	gint rc = 0;

	rc = sqlite3_prepare_v2(db, "SELECT ID, parent_ID, older_sibling_ID, type, "
			"link, file_part IS NOT NULL, section IS NOT NULL, "
			"icon_name IS NOT NULL, node_text IS NOT NULL, text IS NOT NULL "
			"FROM knoten ORDER BY ID;", -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		set_sqlite_error(db, __func__, error);

		return -1;
	}

	c->rows = g_array_new(FALSE, FALSE, sizeof(Zeile));
	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		Zeile z = { 0 };

		z.id = sqlite3_column_int64(stmt, 0);
		z.parent = sqlite3_column_int64(stmt, 1);
		z.older = sqlite3_column_int64(stmt, 2);
		z.type = (sqlite3_column_type(stmt, 3) == SQLITE_NULL) ? -1 :
				sqlite3_column_int(stmt, 3);
		if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) {
			z.link = sqlite3_column_int64(stmt, 4);
			z.flags |= F_LINK;
		}
		if (sqlite3_column_int(stmt, 5))
			z.flags |= F_FP;
		if (sqlite3_column_int(stmt, 6))
			z.flags |= F_SECTION;
		if (sqlite3_column_int(stmt, 7))
			z.flags |= F_ICON;
		if (sqlite3_column_int(stmt, 8))
			z.flags |= F_NODETEXT;
		if (sqlite3_column_int(stmt, 9))
			z.flags |= F_TEXT;
		g_array_append_val(c->rows, z);
	}
	sqlite3_finalize(stmt);
	if (rc != SQLITE_DONE) {
		set_sqlite_error(db, __func__, error);
		ctx_leeren(c);

		return -1;
	}

	c->pos = g_hash_table_new(g_int64_hash, g_int64_equal);
	for (guint i = 0; i < c->rows->len; i++)
		g_hash_table_insert(c->pos, &zeile(c, i)->id, GUINT_TO_POINTER(i + 1));

	c->kinder = g_new0(GArray*, c->rows->len);
	for (guint i = 0; i < c->rows->len; i++) {
		Zeile *z = zeile(c, i);
		gint p = (z->parent != 0) ? finde(c, z->parent) : -1;
		gint index = (gint) i;

		if (p < 0 || p == (gint) i)
			continue;
		if (!c->kinder[p])
			c->kinder[p] = g_array_new(FALSE, FALSE, sizeof(gint));
		g_array_append_val(c->kinder[p], index);
	}

	return 0;
}

//In welchem Baum liegt jeder Knoten (über die Elternkette bis zur Wurzel)?
static void ctx_baeume(Ctx *c) {
	guint n = c->rows->len;
	gboolean *aufpfad = g_new0(gboolean, n);
	g_autoptr(GArray) pfad = g_array_new(FALSE, FALSE, sizeof(gint));

	c->baum = g_new0(guint8, n);
	c->zyklus = g_new0(gboolean, n);

	for (guint i = 0; i < n; i++) {
		gint cur = (gint) i;
		guint8 b = 0;

		if (c->baum[i])
			continue;

		g_array_set_size(pfad, 0);
		while (TRUE) {
			Zeile *z = zeile(c, cur);
			gint p = 0;

			if (c->baum[cur]) {
				b = c->baum[cur];
				break;
			}
			if (z->id == 1 || z->id == 2) {
				b = (guint8) z->id;
				c->baum[cur] = b;
				break;
			}
			if (z->parent == 0) {
				b = (z->type == 5) ? 3 : 4;
				c->baum[cur] = b;
				break;
			}
			p = finde(c, z->parent);
			if (p < 0) {
				b = 4;
				c->baum[cur] = b;
				break;
			}
			if (aufpfad[cur]) { //Schleife: der Knoten mit der kleinsten ID vertritt sie
				gint rep = cur;
				gboolean im_kreis = FALSE;

				for (guint k = 0; k < pfad->len; k++) {
					gint x = g_array_index(pfad, gint, k);

					if (x == cur)
						im_kreis = TRUE;
					if (im_kreis && zeile(c, x)->id < zeile(c, rep)->id)
						rep = x;
				}
				c->zyklus[rep] = TRUE;
				b = 4;
				break;
			}
			aufpfad[cur] = TRUE;
			g_array_append_val(pfad, cur);
			cur = p;
		}

		for (guint k = 0; k < pfad->len; k++) {
			gint x = g_array_index(pfad, gint, k);

			c->baum[x] = b;
			aufpfad[x] = FALSE;
		}
	}

	g_free(aufpfad);

	return;
}

//Was steckt im Teilbaum ab Index idx?
static void klassifiziere(Ctx *c, gint idx, gboolean *anker, gboolean *copylink,
		gboolean *filepart) {
	g_autoptr(GArray) stapel = g_array_new(FALSE, FALSE, sizeof(gint));
	gboolean *besucht = g_new0(gboolean, c->rows->len);

	*anker = *copylink = *filepart = FALSE;
	g_array_append_val(stapel, idx);

	while (stapel->len) {
		gint cur = g_array_index(stapel, gint, stapel->len - 1);
		Zeile *z = zeile(c, cur);

		g_array_set_size(stapel, stapel->len - 1);
		if (besucht[cur])
			continue;
		besucht[cur] = TRUE;

		if (z->type == 2)
			*anker = TRUE;
		else if (z->type == 3 || z->type == 4)
			*copylink = TRUE;
		else if (z->type == 5)
			*filepart = TRUE;

		if (c->kinder[cur])
			for (guint k = 0; k < c->kinder[cur]->len; k++)
				g_array_append_val(stapel, g_array_index(c->kinder[cur], gint, k));
	}

	g_free(besucht);

	return;
}

/* ---- Mängel anlegen --------------------------------------------------------- */

static DbMangel* neu_mangel(GPtrArray *m, DbRegel regel, gint64 key,
		gint64 eltern, gint baum, guint aktionen, DbAkt standard, gchar *text) {
	DbMangel *mg = g_new0(DbMangel, 1);

	mg->regel = regel;
	mg->knoten_id = key;
	mg->knoten_ids = g_array_new(FALSE, FALSE, sizeof(gint64));
	g_array_append_val(mg->knoten_ids, key);
	mg->eltern_id = eltern;
	mg->baum = baum;
	mg->text = text;
	mg->aktionen = aktionen;
	mg->standard = standard;
	mg->gewaehlt = standard;
	g_ptr_array_add(m, mg);

	return mg;
}

/* ---- Regel A1 --------------------------------------------------------------- */

static void pruefe_a1(Ctx *c, GPtrArray *m) {
	for (gint64 id = 0; id <= 2; id++) {
		gint idx = finde(c, id);
		Zeile *z = (idx >= 0) ? zeile(c, idx) : NULL;
		gboolean ok = TRUE;

		if (!z) {
			neu_mangel(m, DB_REGEL_A1, id, 0, 4, M(DB_AKT_ANLEGEN) | M(DB_AKT_BEHALTEN),
					DB_AKT_ANLEGEN, g_strdup_printf("Grundeintrag »%s« fehlt",
							grund_name(id)));

			continue;
		}

		if (id == 0)
			ok = z->parent == 0 && z->older == 0 && z->type == -1;
		else
			ok = z->parent == 0 && z->older == 0 && z->type == 0 && !z->flags;

		if (!ok)
			neu_mangel(m, DB_REGEL_A1, id, 0, 4, M(DB_AKT_BEREINIGEN) | M(DB_AKT_BEHALTEN),
					DB_AKT_BEREINIGEN, g_strdup_printf("Grundeintrag »%s« ist beschädigt",
							grund_name(id)));
	}

	return;
}

/* ---- Regeln A2, B1, B2, B3, D2, E1, E2 -------------------------------------- */

static gboolean eltern_zulaessig(gint kind, gint eltern) {
	switch (kind) {
	case 1:
	case 3:
	case 4:
		return eltern == 0 || eltern == 1 || eltern == 3;
	case 2:
		return eltern == 0 || eltern == 1;
	case 5:
		return eltern == 5;
	default:
		return TRUE;
	}
}

static gboolean falscher_baum(Ctx *c, gint i) {
	Zeile *z = zeile(c, i);

	if (z->type == 2)
		return c->baum[i] == 2;
	if (z->type == 3 || z->type == 4)
		return c->baum[i] == 1;

	return FALSE;
}

//Ziel der Sammelpunkt-Aktion nach Typ und Lage
static gint ziel_nach_typ(Ctx *c, gint i) {
	Zeile *z = zeile(c, i);

	if (z->type == 2)
		return 1;
	if (z->type == 3 || z->type == 4)
		return 2;

	return (c->baum[i] == 1 || c->baum[i] == 2) ? c->baum[i] : 2;
}

static void mangel_d2(Ctx *c, GPtrArray *m, gint i, gchar *text) {
	Zeile *z = zeile(c, i);
	gboolean anker = FALSE;
	gboolean copylink = FALSE;
	gboolean filepart = FALSE;
	DbMangel *mg = NULL;

	klassifiziere(c, i, &anker, &copylink, &filepart);

	if (filepart || z->type == 5) {
		mg = neu_mangel(m, DB_REGEL_D2, z->id, z->parent, 4, M(DB_AKT_BEHALTEN),
				DB_AKT_BEHALTEN, g_strdup_printf("%s (enthält Dateien oder "
						"Abschnitte)", text));
	} else if (anker && copylink) {
		mg = neu_mangel(m, DB_REGEL_D2, z->id, z->parent, 4,
				M(DB_AKT_LOESCHEN) | M(DB_AKT_BEHALTEN), DB_AKT_BEHALTEN,
				g_strdup_printf("%s (darunter sowohl Anbindungen als auch "
						"Copies oder Links)", text));
	} else {
		mg = neu_mangel(m, DB_REGEL_D2, z->id, z->parent, 4,
				M(DB_AKT_SAMMELPUNKT) | M(DB_AKT_LOESCHEN) | M(DB_AKT_BEHALTEN),
				DB_AKT_SAMMELPUNKT, g_strdup(text));
		mg->ziel_baum = anker ? 1 : 2;
	}
	g_free(text);

	return;
}

//Spalten, die zum Typ passen
static void pruefe_b3(Ctx *c, GPtrArray *m, gint i) {
	Zeile *z = zeile(c, i);
	GString *probleme = g_string_new(NULL);
	gboolean bereinigbar = TRUE;
	guint darf_nicht = 0;
	guint muss = 0;
	struct { guint f; gchar const *name; } const spalten[] = {
		{ F_LINK, "Verweis" }, { F_FP, "Dateipfad" }, { F_SECTION, "Abschnitt" },
		{ F_ICON, "Symbol" }, { F_NODETEXT, "Text" }, { F_TEXT, "Notiz" }
	};

	switch (z->type) {
	case 1:
		darf_nicht = F_LINK | F_FP | F_SECTION;
		muss = F_ICON | F_NODETEXT;
		break;
	case 2:
	case 4:
		darf_nicht = F_FP | F_SECTION | F_ICON | F_NODETEXT | F_TEXT;
		break;
	case 3:
		darf_nicht = F_FP | F_SECTION;
		muss = F_ICON | F_NODETEXT;
		break;
	case 5:
		darf_nicht = F_LINK;
		muss = F_FP | F_ICON | F_NODETEXT;
		if (z->parent == 0)
			darf_nicht |= F_SECTION;
		else
			muss |= F_SECTION;
		break;
	default:
		break;
	}

	for (guint k = 0; k < G_N_ELEMENTS(spalten); k++) {
		if ((z->flags & spalten[k].f) && (darf_nicht & spalten[k].f))
			g_string_append_printf(probleme, "%s»%s« darf hier nicht gesetzt sein",
					probleme->len ? "; " : "", spalten[k].name);
		else if (!(z->flags & spalten[k].f) && (muss & spalten[k].f)) {
			g_string_append_printf(probleme, "%s»%s« fehlt", probleme->len ? "; " : "",
					spalten[k].name);
			if (z->type == 5)
				bereinigbar = FALSE; //Pfad, Abschnitt, Symbol und Text lassen sich nicht erraten
		}
	}

	//Dateien und Abschnitte lassen sich nicht automatisch korrigieren
	if (z->type == 5)
		bereinigbar = FALSE;

	if (probleme->len)
		neu_mangel(m, DB_REGEL_B3, z->id, z->parent, c->baum[i],
				bereinigbar ? (M(DB_AKT_BEREINIGEN) | M(DB_AKT_LOESCHEN)
						| M(DB_AKT_BEHALTEN)) : M(DB_AKT_BEHALTEN),
				bereinigbar ? DB_AKT_BEREINIGEN : DB_AKT_BEHALTEN,
				g_string_free(probleme, FALSE));
	else
		g_string_free(probleme, TRUE);

	return;
}

//Anker, Copy und Link: Ziel vorhanden und vom zulässigen Typ?
static void pruefe_verweis(Ctx *c, GPtrArray *m, gint i) {
	Zeile *z = zeile(c, i);
	gint t = 0;
	gboolean ok = FALSE;
	guint aktionen = 0;
	DbAkt standard = DB_AKT_BEHALTEN;

	if (z->type != 2 && z->type != 3 && z->type != 4)
		return;

	if (z->type == 3) {
		aktionen = M(DB_AKT_UMWANDELN) | M(DB_AKT_LOESCHEN) | M(DB_AKT_BEHALTEN);
		standard = DB_AKT_UMWANDELN;
	} else {
		aktionen = M(DB_AKT_LOESCHEN) | M(DB_AKT_BEHALTEN);
		standard = DB_AKT_LOESCHEN;
	}

	if (!(z->flags & F_LINK)) {
		neu_mangel(m, DB_REGEL_E1, z->id, z->parent, c->baum[i], aktionen,
				standard, g_strdup("Verweist auf nichts"));

		return;
	}

	t = finde(c, z->link);
	if (t < 0) {
		neu_mangel(m, DB_REGEL_E1, z->id, z->parent, c->baum[i], aktionen,
				standard, g_strdup("Das Ziel, auf das verwiesen wird, existiert "
						"nicht mehr"));

		return;
	}

	{
		gint tt = zeile(c, t)->type;

		switch (z->type) {
		case 2:
			ok = tt == 5;
			break;
		case 3:
			ok = tt == 2 || tt == 5;
			break;
		default: //Link: Punkt, Anker, Copy, Datei/Abschnitt; nie ein Link
			ok = tt == 1 || tt == 2 || tt == 3 || tt == 5;
			break;
		}
		if (!ok)
			neu_mangel(m, DB_REGEL_E2, z->id, z->parent, c->baum[i], aktionen,
					standard, g_strdup_printf("Das Ziel ist von der Art »%s« - darauf darf "
							"%s nicht verweisen", db_typ_name(tt),
							(z->type == 2) ? "eine Anbindung" :
									((z->type == 3) ? "eine Copy" :
											"ein Link")));
	}

	return;
}

//E3: mehrere Anbindungen auf dieselbe Datei/denselben Abschnitt; die mit der höchsten ID bleibt
static void pruefe_mehrfach(Ctx *c, GPtrArray *m) {
	g_autoptr(GHashTable) letzte = g_hash_table_new_full(g_int64_hash,
			g_int64_equal, NULL, g_free);

	for (guint i = 0; i < c->rows->len; i++) {
		Zeile *z = zeile(c, i);

		if (z->type == 2 && (z->flags & F_LINK))
			g_hash_table_insert(letzte, &z->link, g_memdup2(&z->id, sizeof(gint64)));
	}

	for (guint i = 0; i < c->rows->len; i++) {
		Zeile *z = zeile(c, i);
		gint64 *id = NULL;

		if (z->type != 2 || !(z->flags & F_LINK))
			continue;
		id = g_hash_table_lookup(letzte, &z->link);
		if (id && *id != z->id)
			neu_mangel(m, DB_REGEL_E3, z->id, z->parent, c->baum[i],
					M(DB_AKT_LOESCHEN) | M(DB_AKT_BEHALTEN), DB_AKT_BEHALTEN,
					g_strdup("Dieselbe Datei ist noch einmal angebunden - im Baum "
							"erscheint sie nur an einer Stelle"));
	}

	return;
}

static void pruefe_zeilen(Ctx *c, GPtrArray *m) {
	for (guint i = 0; i < c->rows->len; i++) {
		Zeile *z = zeile(c, i);
		gint p = -1;

		if (z->id <= 2)
			continue;

		if (!typ_gueltig(z->type)) {
			neu_mangel(m, DB_REGEL_A2, z->id, z->parent, c->baum[i],
					M(DB_AKT_UMWANDELN) | M(DB_AKT_LOESCHEN) | M(DB_AKT_BEHALTEN),
					DB_AKT_UMWANDELN, (z->type < 0) ?
							g_strdup("Der Eintrag hat keinen Typ") :
							g_strdup_printf("Der Eintrag hat einen unbekannten Typ "
									"(%d)", z->type));

			continue;
		}

		if (z->parent != 0)
			p = finde(c, z->parent);

		//D2: keine Verbindung zu einer Wurzel
		if (z->parent == 0 && z->type != 5)
			mangel_d2(c, m, (gint) i, g_strdup("Gehört zu keinem übergeordneten Eintrag"));
		else if (z->parent != 0 && p < 0)
			mangel_d2(c, m, (gint) i, g_strdup("Der übergeordnete Eintrag existiert nicht mehr"));
		else if (c->zyklus[i])
			mangel_d2(c, m, (gint) i, g_strdup("Die übergeordneten Einträge verweisen "
					"im Kreis aufeinander"));

		//B1: Elternknoten passt nicht zum Typ
		if (p >= 0 && z->parent != 0) {
			Zeile *pz = zeile(c, p);
			gboolean pt_gueltig = typ_gueltig(pz->type) || (pz->type == 0
					&& (pz->id == 1 || pz->id == 2));

			if (pt_gueltig && !eltern_zulaessig(z->type, pz->type)) {
				DbMangel *mg = neu_mangel(m, DB_REGEL_B1, z->id, z->parent,
						c->baum[i], (z->type == 5) ? M(DB_AKT_BEHALTEN) :
								(M(DB_AKT_SAMMELPUNKT) | M(DB_AKT_LOESCHEN)
										| M(DB_AKT_BEHALTEN)),
						(z->type == 5) ? DB_AKT_BEHALTEN : DB_AKT_SAMMELPUNKT,
						g_strdup_printf("%s steht unter einem Eintrag der Art "
								"»%s« - dort darf er nicht stehen",
								db_typ_name(z->type), db_typ_name(pz->type)));

				mg->ziel_baum = ziel_nach_typ(c, (gint) i);
			}
		}

		//B2: falscher Baum - nur der oberste betroffene Knoten
		if (falscher_baum(c, (gint) i) && !(p >= 0 && falscher_baum(c, p))) {
			DbMangel *mg = neu_mangel(m, DB_REGEL_B2, z->id, z->parent, c->baum[i],
					M(DB_AKT_SAMMELPUNKT) | M(DB_AKT_LOESCHEN) | M(DB_AKT_BEHALTEN),
					DB_AKT_SAMMELPUNKT, g_strdup_printf("%s steht %s, gehört aber %s",
							db_typ_name(z->type),
							(z->type == 2) ? "in der Auswertung" :
									"im Bestandsverzeichnis",
							(z->type == 2) ? "ins Bestandsverzeichnis" :
									"in die Auswertung"));

			mg->ziel_baum = (z->type == 2) ? 1 : 2;
		}

		pruefe_b3(c, m, (gint) i);
		pruefe_verweis(c, m, (gint) i);
	}

	return;
}

/* ---- Regel D1: unsichtbare Zweige ------------------------------------------- */

//Text eines Knotens für Meldungen; ohne Text die Art
static gchar* kurzname(sqlite3 *db, Ctx *c, gint64 id) {
	sqlite3_stmt *stmt = NULL;
	gchar *text = NULL;
	gint idx = finde(c, id);

	if (sqlite3_prepare_v2(db, "SELECT node_text FROM knoten WHERE ID=?1;", -1,
			&stmt, NULL) == SQLITE_OK) {
		sqlite3_bind_int64(stmt, 1, id);
		if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_text(stmt, 0)
				&& *sqlite3_column_text(stmt, 0))
			text = g_strdup((gchar const*) sqlite3_column_text(stmt, 0));
		sqlite3_finalize(stmt);
	}

	if (text)
		return text;

	return g_strdup_printf("(%s)", db_typ_name((idx >= 0) ? zeile(c, idx)->type : -1));
}

/* Beschreibung einer Schleife kurz als "A -> B -> A"; die Liste der Knoten
 * steht in Kettenreihenfolge, der Rest wird nicht aufgezählt. */
static gchar* schleifentext(sqlite3 *db, Ctx *c, GArray const *ids) {
	GString *s = g_string_new("Folgen im Kreis aufeinander: ");
	guint max = MIN(ids->len, 5);
	gint idx_erster = finde(c, g_array_index(ids, gint64, 0));
	gboolean geschlossen = idx_erster >= 0 && zeile(c, idx_erster)->older
			== g_array_index(ids, gint64, ids->len - 1);

	for (guint i = 0; i < max; i++) {
		g_autofree gchar *name = kurzname(db, c, g_array_index(ids, gint64, i));

		g_string_append_printf(s, "%s»%s«", i ? " → " : "", name);
	}
	if (ids->len > max)
		g_string_append(s, " → …");
	else if (geschlossen) {
		g_autofree gchar *name = kurzname(db, c, g_array_index(ids, gint64, 0));

		g_string_append_printf(s, " → »%s«", name);
	}

	return g_string_free(s, FALSE);
}

static gint pruefe_d1(ZondDBase *zond_dbase, Ctx *c, GPtrArray *m,
		GError **error) {
	g_auto(DbCheckErgebnis) alt = { 0 };
	sqlite3 *db_d1 = zond_dbase_get_dbase(zond_dbase);

	if (db_check_ausfuehren(zond_dbase, FALSE, &alt, error))
		return -1;

	for (guint i = 0; i < alt.befunde->len; i++) {
		DbBefund *b = g_ptr_array_index(alt.befunde, i);
		gint e = finde(c, b->eltern_id);
		guint aktionen = M(DB_AKT_BEHALTEN);
		DbAkt standard = DB_AKT_BEHALTEN;

		if (b->eltern_typ < 0)
			continue; //Waisen: D2

		if (b->eltern_typ == 5) {
			aktionen = M(DB_AKT_EINORDNEN) | M(DB_AKT_BEHALTEN);
			standard = DB_AKT_EINORDNEN;
		} else if (b->eltern_typ == 0 || b->eltern_typ == 1 || b->eltern_typ == 3) {
			aktionen = M(DB_AKT_SAMMELPUNKT) | M(DB_AKT_LOESCHEN) | M(DB_AKT_BEHALTEN);
			standard = DB_AKT_SAMMELPUNKT;
		}

		for (guint k = 0; k < b->zweige->len; k++) {
			DbZweig *z = g_ptr_array_index(b->zweige, k);
			DbMangel *mg = NULL;

			if (z->sichtbar || !z->knoten_ids->len)
				continue;

			mg = neu_mangel(m, DB_REGEL_D1, g_array_index(z->knoten_ids, gint64, 0),
					b->eltern_id, (e >= 0) ? c->baum[e] : 4, aktionen, standard,
					(z->knoten_ids->len == 1) ?
							g_strdup("1 Eintrag wird im Baum nicht angezeigt, weil "
									"die Reihenfolge der Geschwister fehlerhaft "
									"ist") :
							g_strdup_printf("%u Einträge werden im Baum nicht "
									"angezeigt, weil die Reihenfolge der "
									"Geschwister fehlerhaft ist",
									z->knoten_ids->len));
			g_array_set_size(mg->knoten_ids, 0);
			g_array_append_vals(mg->knoten_ids, z->knoten_ids->data,
					z->knoten_ids->len);

			if (z->art == DB_ZWEIG_ZYKLUS) {
				g_free(mg->text);
				mg->text = schleifentext(db_d1, c, z->knoten_ids);
				mg->schleife = TRUE;
			}
		}
	}

	return 0;
}

/* ---- Regel F1 --------------------------------------------------------------- */

static gint pruefe_f1(sqlite3 *db, GPtrArray *m, GError **error) {
	sqlite3_stmt *stmt = NULL;
	GHashTable *bekannt = g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free,
			NULL);
	gint n = 0;
	gint rc = 0;

	//Knoten mit anderem Mangel werden nicht noch einmal als Integritätsfehler gemeldet
	for (guint i = 0; i < m->len; i++) {
		DbMangel *mg = g_ptr_array_index(m, i);

		for (guint k = 0; k < mg->knoten_ids->len; k++) {
			gint64 *key = g_new(gint64, 1);

			*key = g_array_index(mg->knoten_ids, gint64, k);
			g_hash_table_insert(bekannt, key, GINT_TO_POINTER(1));
		}
	}

	rc = sqlite3_prepare_v2(db, "PRAGMA quick_check;", -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		set_sqlite_error(db, __func__, error);
		g_hash_table_unref(bekannt);

		return -1;
	}
	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		gchar const *zeile_text = (gchar const*) sqlite3_column_text(stmt, 0);

		if (!g_strcmp0(zeile_text, "ok") || n >= DB_REGELN_MAX_INTEGRITAET)
			continue;

		neu_mangel(m, DB_REGEL_F1, 0, 0, 4, M(DB_AKT_BEHALTEN), DB_AKT_BEHALTEN,
				g_strdup_printf("Datenbankdatei: %s", zeile_text));
		n++;
	}
	sqlite3_finalize(stmt);
	if (rc != SQLITE_DONE) {
		set_sqlite_error(db, __func__, error);
		g_hash_table_unref(bekannt);

		return -1;
	}

	n = 0;
	rc = sqlite3_prepare_v2(db, "PRAGMA foreign_key_check;", -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		set_sqlite_error(db, __func__, error);
		g_hash_table_unref(bekannt);

		return -1;
	}
	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		gint64 rowid = sqlite3_column_int64(stmt, 1);

		if (n >= DB_REGELN_MAX_INTEGRITAET || g_hash_table_contains(bekannt, &rowid))
			continue;

		neu_mangel(m, DB_REGEL_F1, 0, 0, 4, M(DB_AKT_BEHALTEN), DB_AKT_BEHALTEN,
				g_strdup_printf("Datenbankdatei: Eintrag %s (%s) verweist auf "
						"nicht vorhandenen Eintrag (%s)",
						(gchar const*) sqlite3_column_text(stmt, 0),
						(gchar const*) sqlite3_column_text(stmt, 1),
						(gchar const*) sqlite3_column_text(stmt, 2)));
		n++;
	}
	sqlite3_finalize(stmt);
	g_hash_table_unref(bekannt);
	if (rc != SQLITE_DONE) {
		set_sqlite_error(db, __func__, error);

		return -1;
	}

	return 0;
}

/* ---- Zusammenführen je Knoten und Sortieren --------------------------------- */

typedef struct {
	guint maske;
	DbAkt standard;
	gint ziel_baum;
} Gruppe;

/* Eine Aktion pro Knoten: angeboten wird, was ALLE seine Mängel zulassen
 * (Schnittmenge), damit keine Teilkorrektur entsteht. Standard ist die
 * wirksamste Standardaktion seiner Mängel, die in der Schnittmenge liegt,
 * sonst »Nichts tun«. */
static void vereinheitliche(GPtrArray *m) {
	GHashTable *gruppen = g_hash_table_new_full(g_int64_hash, g_int64_equal, NULL,
			g_free);

	for (guint i = 0; i < m->len; i++) {
		DbMangel *mg = g_ptr_array_index(m, i);
		Gruppe *g = g_hash_table_lookup(gruppen, &mg->knoten_id);

		if (!g) {
			g = g_new0(Gruppe, 1);
			g->maske = ~0u;
			g->standard = DB_AKT_BEHALTEN;
			g_hash_table_insert(gruppen, &mg->knoten_id, g);
		}
		g->maske &= mg->aktionen;
		if (!g->ziel_baum)
			g->ziel_baum = mg->ziel_baum;
	}

	for (guint i = 0; i < m->len; i++) {
		DbMangel *mg = g_ptr_array_index(m, i);
		Gruppe *g = g_hash_table_lookup(gruppen, &mg->knoten_id);

		if ((g->maske & M(mg->standard))
				&& db_akt_rang(mg->standard) > db_akt_rang(g->standard))
			g->standard = mg->standard;
	}

	for (guint i = 0; i < m->len; i++) {
		DbMangel *mg = g_ptr_array_index(m, i);
		Gruppe *g = g_hash_table_lookup(gruppen, &mg->knoten_id);

		mg->aktionen = g->maske | M(DB_AKT_BEHALTEN);
		mg->standard = g->standard;
		mg->gewaehlt = g->standard;
		if (!mg->ziel_baum)
			mg->ziel_baum = g->ziel_baum;
	}

	g_hash_table_unref(gruppen);

	return;
}

//Wird der Eintrag im geladenen Baum angezeigt? Bestimmt auch den nächsten sichtbaren Vorfahren.
static void berechne_sichtbarkeit(Ctx *c, GPtrArray *m) {
	guint n = c->rows->len;
	gboolean *unsichtbar = g_new0(gboolean, n);
	g_autoptr(GArray) stapel = g_array_new(FALSE, FALSE, sizeof(gint));

	//ohne Verbindung zu einer Wurzel
	for (guint i = 0; i < n; i++)
		if (zeile(c, i)->id > 2 && c->baum[i] == 4)
			unsichtbar[i] = TRUE;

	//Kinder eines Links werden beim Laden verworfen (der Link wird durch sein Ziel ersetzt)
	for (guint i = 0; i < n; i++) {
		gint p = (zeile(c, i)->parent != 0) ? finde(c, zeile(c, i)->parent) : -1;

		if (p >= 0 && zeile(c, p)->type == 4)
			unsichtbar[i] = TRUE;
	}

	for (guint i = 0; i < m->len; i++) {
		DbMangel *mg = g_ptr_array_index(m, i);
		gint idx = finde(c, mg->knoten_id);

		if (mg->knoten_id <= 0 || idx < 0)
			continue;

		if (mg->regel == DB_REGEL_D1) //nicht angezeigte Zweige
			for (guint k = 0; k < mg->knoten_ids->len; k++) {
				gint x = finde(c, g_array_index(mg->knoten_ids, gint64, k));

				if (x >= 0)
					unsichtbar[x] = TRUE;
			}
		else if (mg->regel == DB_REGEL_D2 || mg->regel == DB_REGEL_E3)
			unsichtbar[idx] = TRUE;
		else if (mg->regel == DB_REGEL_E1 || mg->regel == DB_REGEL_E2) {
			//beim Laden übergangen: Anbindung ohne Ziel, Link ohne brauchbares Ziel
			Zeile *z = zeile(c, idx);
			gint t = (z->flags & F_LINK) ? finde(c, z->link) : -1;

			if (z->type == 2 && mg->regel == DB_REGEL_E1)
				unsichtbar[idx] = TRUE;
			else if (z->type == 4 && (mg->regel == DB_REGEL_E1
					|| (t >= 0 && zeile(c, t)->type == 4)))
				unsichtbar[idx] = TRUE;
		}
	}

	//alles unter einem nicht sichtbaren Eintrag ist ebenfalls nicht sichtbar
	for (guint i = 0; i < n; i++)
		if (unsichtbar[i])
			g_array_append_val(stapel, i);
	while (stapel->len) {
		guint cur = g_array_index(stapel, guint, stapel->len - 1);

		g_array_set_size(stapel, stapel->len - 1);
		if (c->kinder[cur])
			for (guint k = 0; k < c->kinder[cur]->len; k++) {
				gint j = g_array_index(c->kinder[cur], gint, k);

				if (!unsichtbar[j]) {
					guint jj = (guint) j;

					unsichtbar[j] = TRUE;
					g_array_append_val(stapel, jj);
				}
			}
	}

	for (guint i = 0; i < m->len; i++) {
		DbMangel *mg = g_ptr_array_index(m, i);
		gint idx = finde(c, mg->knoten_id);

		mg->sichtbar = TRUE;
		mg->gehoert_unter = 0;
		if (mg->regel == DB_REGEL_F1 || idx < 0 || mg->knoten_id <= 0)
			continue;

		mg->sichtbar = !unsichtbar[idx];
		if (!mg->sichtbar) { //nächster sichtbarer Vorfahre
			gint cur = idx;

			for (guint schritt = 0; schritt <= n; schritt++) {
				Zeile *z = zeile(c, cur);
				gint p = (z->parent != 0) ? finde(c, z->parent) : -1;

				if (p < 0)
					break;
				if (!unsichtbar[p]) {
					mg->gehoert_unter = zeile(c, p)->id;

					break;
				}
				cur = p;
			}
		}
	}

	g_free(unsichtbar);

	return;
}

static gint vergleiche_mangel(gconstpointer pa, gconstpointer pb) {
	DbMangel const *a = *(DbMangel* const*) pa;
	DbMangel const *b = *(DbMangel* const*) pb;

	if (a->regel != b->regel)
		return (a->regel < b->regel) ? -1 : 1;

	return (a->knoten_id > b->knoten_id) - (a->knoten_id < b->knoten_id);
}

gint db_regeln_pruefen(ZondDBase *zond_dbase, gboolean mit_integritaet,
		DbRegelErgebnis *erg, GError **error) {
	sqlite3 *db = zond_dbase_get_dbase(zond_dbase);
	Ctx c = { 0 };

	db_regel_ergebnis_clear(erg);
	erg->maengel = g_ptr_array_new_with_free_func((GDestroyNotify) db_mangel_free);

	if (ctx_laden(db, &c, error))
		return -1;
	ctx_baeume(&c);

	pruefe_a1(&c, erg->maengel);
	pruefe_zeilen(&c, erg->maengel);
	pruefe_mehrfach(&c, erg->maengel);
	if (pruefe_d1(zond_dbase, &c, erg->maengel, error)) {
		ctx_leeren(&c);

		return -1;
	}
	berechne_sichtbarkeit(&c, erg->maengel);
	ctx_leeren(&c);

	if (mit_integritaet && pruefe_f1(db, erg->maengel, error))
		return -1;

	vereinheitliche(erg->maengel);
	g_ptr_array_sort(erg->maengel, vergleiche_mangel);

	return 0;
}

/* ---- Ausführung ------------------------------------------------------------- */

static void melde(DbRegelAktionErgebnis *erg, gchar *text) {
	g_ptr_array_add(erg->meldungen, text);

	return;
}

static gchar* hole_text(sqlite3 *db, gint64 id) {
	sqlite3_stmt *stmt = NULL;
	gchar *text = NULL;

	if (sqlite3_prepare_v2(db, "SELECT node_text FROM knoten WHERE ID=?1;", -1,
			&stmt, NULL) == SQLITE_OK) {
		sqlite3_bind_int64(stmt, 1, id);
		if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_text(stmt, 0)
				&& *sqlite3_column_text(stmt, 0))
			text = g_strdup((gchar const*) sqlite3_column_text(stmt, 0));
		sqlite3_finalize(stmt);
	}

	return (text) ? text : g_strdup_printf("Knoten %" G_GINT64_FORMAT, id);
}

static gint fuehre_aus(sqlite3 *db, gchar const *sql, GError **error) {
	gchar *errmsg = NULL;

	if (sqlite3_exec(db, sql, NULL, NULL, &errmsg) != SQLITE_OK) {
		g_set_error(error, SOND_ERROR, 0, "%s\n%s\n%s", __func__, sql,
				errmsg ? errmsg : sqlite3_errmsg(db));
		sqlite3_free(errmsg);

		return -1;
	}

	return 0;
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

static gchar* id_liste(GArray const *ids) {
	GString *s = g_string_new(NULL);

	for (guint i = 0; i < ids->len; i++)
		g_string_append_printf(s, "%s%" G_GINT64_FORMAT, i ? "," : "",
				g_array_index(ids, gint64, i));

	return g_string_free(s, FALSE);
}

//Das jüngere Geschwister von id übernimmt das ältere von id
static gint entkette(sqlite3 *db, gint64 id, GError **error) {
	g_autofree gchar *sql = g_strdup_printf("UPDATE knoten SET older_sibling_ID="
			"(SELECT older_sibling_ID FROM knoten WHERE ID=%" G_GINT64_FORMAT ") "
			"WHERE older_sibling_ID=%" G_GINT64_FORMAT " AND ID<>%"
			G_GINT64_FORMAT " AND parent_ID=(SELECT parent_ID FROM knoten WHERE "
			"ID=%" G_GINT64_FORMAT ");", id, id, id, id);

	return fuehre_aus(db, sql, error);
}

static GHashTable* neue_idmenge(void) {
	return g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free, NULL);
}

static gboolean idmenge_fuege_ein(GHashTable *set, gint64 id) {
	gint64 *key = NULL;

	if (g_hash_table_contains(set, &id))
		return FALSE;

	key = g_new(gint64, 1);
	*key = id;
	g_hash_table_insert(set, key, GINT_TO_POINTER(1));

	return TRUE;
}

//Aktion je Knoten: die mit dem höchsten Rang unter den gewählten seiner Mängel
static GHashTable* effektive_aktionen(GPtrArray *m) {
	GHashTable *eff = g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free,
			NULL);

	for (guint i = 0; i < m->len; i++) {
		DbMangel *mg = g_ptr_array_index(m, i);
		gpointer alt = g_hash_table_lookup(eff, &mg->knoten_id);
		DbAkt cur = alt ? (DbAkt) (GPOINTER_TO_INT(alt) - 1) : DB_AKT_BEHALTEN;

		if (!alt || db_akt_rang(mg->gewaehlt) > db_akt_rang(cur)) {
			gint64 *key = g_new(gint64, 1);

			*key = mg->knoten_id;
			g_hash_table_insert(eff, key, GINT_TO_POINTER((gint) mg->gewaehlt + 1));
		}
	}

	return eff;
}

static DbAkt eff_aktion(GHashTable *eff, gint64 key) {
	gpointer p = g_hash_table_lookup(eff, &key);

	return p ? (DbAkt) (GPOINTER_TO_INT(p) - 1) : DB_AKT_BEHALTEN;
}

static gint a1_anlegen(sqlite3 *db, gint64 id, DbRegelAktionErgebnis *erg,
		GError **error) {
	g_autofree gchar *sql = NULL;

	if (id == 0)
		sql = g_strdup_printf("INSERT INTO knoten (ID, parent_ID, "
				"older_sibling_ID, node_text) VALUES (0, 0, 0, '%s');",
				G_STRINGIFY(ZOND_VERSION_MAJOR));
	else
		sql = g_strdup_printf("INSERT INTO knoten (ID, parent_ID, "
				"older_sibling_ID, type) VALUES (%" G_GINT64_FORMAT ", 0, 0, 0);",
				id);
	if (fuehre_aus(db, sql, error))
		return -1;
	erg->n_angelegt++;

	return 0;
}

static gint a1_bereinigen(sqlite3 *db, gint64 id, DbRegelAktionErgebnis *erg,
		GError **error) {
	g_autofree gchar *sql = NULL;

	if (id == 0)
		sql = g_strdup("UPDATE knoten SET parent_ID=0, older_sibling_ID=0, "
				"type=NULL WHERE ID=0;");
	else
		sql = g_strdup_printf("UPDATE knoten SET parent_ID=0, older_sibling_ID=0, "
				"type=0, link=NULL, file_part=NULL, section=NULL, icon_name=NULL, "
				"node_text=NULL, text=NULL WHERE ID=%" G_GINT64_FORMAT ";", id);
	if (fuehre_aus(db, sql, error))
		return -1;
	erg->n_bereinigt++;

	return 0;
}

//Knoten samt Unterbäumen löschen, soweit die Schranken es zulassen
static gint loesche_gruppe(sqlite3 *db, GArray *roots, DbRegelAktionErgebnis *erg,
		GError **error) {
	g_autofree gchar *alle = id_liste(roots);
	g_autoptr(GArray) angenommen = g_array_new(FALSE, FALSE, sizeof(gint64));

	for (guint i = 0; i < roots->len; i++) {
		gint64 id = g_array_index(roots, gint64, i);
		g_autofree gchar *name = hole_text(db, id);
		g_autofree gchar *sql = NULL;
		gint64 anzahl = 0;

		//Dateien und Abschnitte werden nie gelöscht
		sql = g_strdup_printf("WITH RECURSIVE sub(ID) AS (SELECT ID FROM knoten "
				"WHERE ID=%" G_GINT64_FORMAT " UNION SELECT k.ID FROM knoten k "
				"JOIN sub ON k.parent_ID=sub.ID) SELECT count(*) FROM knoten WHERE "
				"ID IN (SELECT ID FROM sub) AND type IN (5,6) AND file_part IS NOT "
				"NULL;", id);
		if (hole_zahl(db, sql, &anzahl, error))
			return -1;
		if (anzahl) {
			melde(erg, g_strdup_printf("»%s«: nicht gelöscht, der Teilbaum enthält "
					"Dateien oder Abschnitte", name));

			continue;
		}
		g_free(sql);

		/* Verweise von außerhalb der gesamten Löschmenge: link auf den Teilbaum
		 * oder ein älteres-Geschwister-Verweis aus einem anderen Elternknoten */
		sql = g_strdup_printf("WITH RECURSIVE allsub(ID) AS (SELECT ID FROM "
				"knoten WHERE ID IN (%s) UNION SELECT k.ID FROM knoten k JOIN "
				"allsub ON k.parent_ID=allsub.ID), mysub(ID) AS (SELECT ID FROM "
				"knoten WHERE ID=%" G_GINT64_FORMAT " UNION SELECT k.ID FROM "
				"knoten k JOIN mysub ON k.parent_ID=mysub.ID) SELECT count(*) FROM "
				"knoten WHERE ID NOT IN (SELECT ID FROM allsub) AND (link IN "
				"(SELECT ID FROM mysub) OR (older_sibling_ID IN (SELECT ID FROM "
				"mysub) AND parent_ID<>(SELECT parent_ID FROM knoten WHERE ID=%"
				G_GINT64_FORMAT ")));", alle, id, id);
		if (hole_zahl(db, sql, &anzahl, error))
			return -1;
		if (anzahl) {
			melde(erg, g_strdup_printf("»%s«: nicht gelöscht, es verweist noch "
					"etwas von außerhalb darauf", name));

			continue;
		}

		g_array_append_val(angenommen, id);
	}

	if (!angenommen->len)
		return 0;

	for (guint i = 0; i < angenommen->len; i++)
		if (entkette(db, g_array_index(angenommen, gint64, i), error))
			return -1;

	{
		g_autofree gchar *liste = id_liste(angenommen);
		g_autofree gchar *sql = g_strdup_printf("WITH RECURSIVE sub(ID) AS (SELECT "
				"ID FROM knoten WHERE ID IN (%s) UNION SELECT k.ID FROM knoten "
				"k JOIN sub ON k.parent_ID=sub.ID) DELETE FROM knoten WHERE ID IN "
				"(SELECT ID FROM sub);", liste);

		if (fuehre_aus(db, sql, error))
			return -1;
	}
	erg->n_geloescht += (gint) angenommen->len;

	return 0;
}

static gint verschiebe_gruppe(sqlite3 *db, gint baum, GArray *knoten,
		DbRegelAktionErgebnis *erg, GError **error) {
	gint64 tail = 0;
	gint64 sp = 0;
	gint64 prev = 0;

	if (db_check_sichtbarer_tail(db, baum, &tail, error))
		return -1;
	if (db_sammelpunkt_neu(db, baum, tail, &sp, error))
		return -1;
	erg->n_sammelpunkte++;

	for (guint i = 0; i < knoten->len; i++) {
		gint64 id = g_array_index(knoten, gint64, i);
		g_autofree gchar *sql = NULL;

		if (entkette(db, id, error))
			return -1;

		sql = g_strdup_printf("UPDATE knoten SET parent_ID=%" G_GINT64_FORMAT ", "
				"older_sibling_ID=%" G_GINT64_FORMAT " WHERE ID=%" G_GINT64_FORMAT
				";", sp, prev, id);
		if (fuehre_aus(db, sql, error))
			return -1;
		prev = id;
		erg->n_verschoben++;
	}

	return 0;
}

static gint schicht1(sqlite3 *db, GPtrArray *m, GHashTable *eff,
		DbRegelAktionErgebnis *erg, GError **error) {
	g_autoptr(GArray) roots = g_array_new(FALSE, FALSE, sizeof(gint64));
	GArray *bewegen[3] = { NULL, NULL, NULL };
	GHashTable *d1 = neue_idmenge();
	GHashTable *gesehen = neue_idmenge();
	gint rc = 0;

	bewegen[1] = g_array_new(FALSE, FALSE, sizeof(gint64));
	bewegen[2] = g_array_new(FALSE, FALSE, sizeof(gint64));

	for (guint i = 0; i < m->len; i++) {
		DbMangel *mg = g_ptr_array_index(m, i);

		if (mg->regel == DB_REGEL_D1)
			idmenge_fuege_ein(d1, mg->knoten_id);
	}

	//Grundzeilen
	for (guint i = 0; i < m->len && !rc; i++) {
		DbMangel *mg = g_ptr_array_index(m, i);
		DbAkt a = eff_aktion(eff, mg->knoten_id);

		if (mg->regel != DB_REGEL_A1 || !(mg->aktionen & M(a)))
			continue;
		if (a == DB_AKT_ANLEGEN)
			rc = a1_anlegen(db, mg->knoten_id, erg, error);
		else if (a == DB_AKT_BEREINIGEN)
			rc = a1_bereinigen(db, mg->knoten_id, erg, error);
	}

	//Verschieben und Löschen von Knoten, die nicht zu einem D1-Zweig gehören
	for (guint i = 0; i < m->len && !rc; i++) {
		DbMangel *mg = g_ptr_array_index(m, i);
		DbAkt a = eff_aktion(eff, mg->knoten_id);

		if (mg->regel == DB_REGEL_D1 || mg->regel == DB_REGEL_A1
				|| mg->regel == DB_REGEL_F1 || mg->knoten_id <= 2
				|| g_hash_table_contains(d1, &mg->knoten_id)
				|| !(mg->aktionen & M(a)))
			continue;

		if (a == DB_AKT_SAMMELPUNKT) {
			gint ziel = (mg->ziel_baum == 1) ? 1 : 2;

			if (idmenge_fuege_ein(gesehen, mg->knoten_id))
				g_array_append_val(bewegen[ziel], mg->knoten_id);
		} else if (a == DB_AKT_LOESCHEN) {
			if (idmenge_fuege_ein(gesehen, mg->knoten_id))
				g_array_append_val(roots, mg->knoten_id);
		}
	}

	for (gint baum = 1; baum <= 2 && !rc; baum++)
		if (bewegen[baum]->len)
			rc = verschiebe_gruppe(db, baum, bewegen[baum], erg, error);
	if (!rc && roots->len)
		rc = loesche_gruppe(db, roots, erg, error);

	g_hash_table_unref(d1);
	g_hash_table_unref(gesehen);
	g_array_unref(bewegen[1]);
	g_array_unref(bewegen[2]);

	return rc;
}

static void zweig_lokal_free(DbZweig *z) {
	if (!z)
		return;

	if (z->knoten_ids)
		g_array_unref(z->knoten_ids);
	g_free(z);

	return;
}

static gint hole_typ(sqlite3 *db, gint64 id) {
	sqlite3_stmt *stmt = NULL;
	gint typ = -2;

	if (sqlite3_prepare_v2(db, "SELECT ifnull(type, -1) FROM knoten WHERE ID=?1;",
			-1, &stmt, NULL) != SQLITE_OK)
		return -2;

	sqlite3_bind_int64(stmt, 1, id);
	if (sqlite3_step(stmt) == SQLITE_ROW)
		typ = sqlite3_column_int(stmt, 0);
	sqlite3_finalize(stmt);

	return typ;
}

static gint bereinige_knoten(sqlite3 *db, gint64 id, DbRegelAktionErgebnis *erg,
		GError **error) {
	gint typ = hole_typ(db, id);
	g_autofree gchar *sql = NULL;

	switch (typ) {
	case 1:
		sql = g_strdup_printf("UPDATE knoten SET link=NULL, file_part=NULL, "
				"section=NULL, icon_name=COALESCE(icon_name,'media-record'), "
				"node_text=COALESCE(node_text,'Punkt') WHERE ID=%" G_GINT64_FORMAT
				";", id);
		break;
	case 2:
	case 4:
		sql = g_strdup_printf("UPDATE knoten SET file_part=NULL, section=NULL, "
				"icon_name=NULL, node_text=NULL, text=NULL WHERE ID=%"
				G_GINT64_FORMAT ";", id);
		break;
	case 3:
		sql = g_strdup_printf("UPDATE knoten SET file_part=NULL, section=NULL, "
				"icon_name=COALESCE(icon_name,'media-record'), "
				"node_text=COALESCE(node_text,'Copy') WHERE ID=%" G_GINT64_FORMAT
				";", id);
		break;
	default:
		melde(erg, g_strdup_printf("Knoten %" G_GINT64_FORMAT ": für diesen Typ "
				"gibt es keine Bereinigung", id));

		return 0;
	}

	if (fuehre_aus(db, sql, error))
		return -1;
	erg->n_bereinigt++;

	return 0;
}

static gint schicht2(ZondDBase *zond_dbase, sqlite3 *db, GPtrArray *m,
		DbRegelAktionErgebnis *erg, GError **error) {
	g_autoptr(GPtrArray) ordnen = g_ptr_array_new_with_free_func(
			(GDestroyNotify) db_befund_free);
	g_autoptr(GPtrArray) verwerfen = g_ptr_array_new_with_free_func(
			(GDestroyNotify) db_befund_free);
	GHashTable *bef_ordnen = g_hash_table_new(g_int64_hash, g_int64_equal);
	GHashTable *bef_verwerfen = g_hash_table_new(g_int64_hash, g_int64_equal);
	g_autoptr(GHashTable) eff = effektive_aktionen(m);
	GHashTable *erledigt = neue_idmenge();
	gint rc = 0;

	for (guint i = 0; i < m->len && !rc; i++) {
		DbMangel *mg = g_ptr_array_index(m, i);
		DbAkt a = eff_aktion(eff, mg->knoten_id);

		if (!(mg->aktionen & M(a)) || a == DB_AKT_BEHALTEN)
			continue;

		if (mg->regel == DB_REGEL_B3 && a == DB_AKT_BEREINIGEN) {
			if (idmenge_fuege_ein(erledigt, mg->knoten_id))
				rc = bereinige_knoten(db, mg->knoten_id, erg, error);
		} else if (mg->regel == DB_REGEL_A2 && a == DB_AKT_UMWANDELN) {
			if (idmenge_fuege_ein(erledigt, mg->knoten_id)) {
				g_autofree gchar *sql = g_strdup_printf("UPDATE knoten SET type=1, "
						"link=NULL, file_part=NULL, section=NULL, "
						"icon_name=COALESCE(icon_name,'media-record'), "
						"node_text=COALESCE(node_text,'Punkt') WHERE ID=%"
						G_GINT64_FORMAT ";", mg->knoten_id);

				rc = fuehre_aus(db, sql, error);
				if (!rc)
					erg->n_umgewandelt++;
			}
		} else if ((mg->regel == DB_REGEL_E1 || mg->regel == DB_REGEL_E2)
				&& a == DB_AKT_UMWANDELN) {
			if (idmenge_fuege_ein(erledigt, mg->knoten_id)) {
				g_autofree gchar *sql = g_strdup_printf("UPDATE knoten SET type=1, "
						"link=NULL, file_part=NULL, section=NULL, "
						"icon_name=COALESCE(icon_name,'media-record'), "
						"node_text=COALESCE(node_text,'Punkt') WHERE ID=%"
						G_GINT64_FORMAT " AND type=3;", mg->knoten_id);

				rc = fuehre_aus(db, sql, error);
				if (!rc)
					erg->n_umgewandelt++;
			}
		} else if (mg->regel == DB_REGEL_D1
				&& (a == DB_AKT_SAMMELPUNKT || a == DB_AKT_EINORDNEN
						|| a == DB_AKT_LOESCHEN)) {
			GHashTable *tab = (a == DB_AKT_LOESCHEN) ? bef_verwerfen : bef_ordnen;
			GPtrArray *liste = (a == DB_AKT_LOESCHEN) ? verwerfen : ordnen;
			DbBefund *b = g_hash_table_lookup(tab, &mg->eltern_id);
			DbZweig *z = NULL;

			if (!b) {
				b = g_new0(DbBefund, 1);
				b->eltern_id = mg->eltern_id;
				b->eltern_typ = hole_typ(db, mg->eltern_id);
				b->reparierbar = TRUE;
				b->zweige = g_ptr_array_new_with_free_func(
						(GDestroyNotify) zweig_lokal_free);
				g_ptr_array_add(liste, b);
				g_hash_table_insert(tab, &b->eltern_id, b);
			}
			z = g_new0(DbZweig, 1);
			z->gewaehlt = TRUE;
			z->knoten_ids = g_array_new(FALSE, FALSE, sizeof(gint64));
			g_array_append_val(z->knoten_ids, mg->knoten_id);
			g_ptr_array_add(b->zweige, z);
		}
	}

	if (!rc && ordnen->len) {
		g_auto(DbAktionErgebnis) ae = { 0 };

		rc = db_aktion_ausfuehren_innen(zond_dbase, DB_AKTION_REPARIEREN, ordnen,
				&ae, error);
		if (!rc) {
			erg->n_ketten += ae.n_zweige;
			erg->n_sammelpunkte += ae.n_sammelpunkte;
			for (guint i = 0; ae.meldungen && i < ae.meldungen->len; i++)
				melde(erg, g_strdup(g_ptr_array_index(ae.meldungen, i)));
		}
	}
	if (!rc && verwerfen->len) {
		g_auto(DbAktionErgebnis) ae = { 0 };

		rc = db_aktion_ausfuehren_innen(zond_dbase, DB_AKTION_VERWERFEN, verwerfen,
				&ae, error);
		if (!rc) {
			erg->n_geloescht += ae.n_zweige;
			for (guint i = 0; ae.meldungen && i < ae.meldungen->len; i++)
				melde(erg, g_strdup(g_ptr_array_index(ae.meldungen, i)));
		}
	}

	g_hash_table_unref(bef_ordnen);
	g_hash_table_unref(bef_verwerfen);
	g_hash_table_unref(erledigt);

	return rc;
}

static gchar* wahl_schluessel(DbMangel const *mg) {
	return g_strdup_printf("%d:%" G_GINT64_FORMAT, mg->regel, mg->knoten_id);
}

static gint ausfuehren_ohne_fk(ZondDBase *zond_dbase, GPtrArray *maengel,
		DbRegelAktionErgebnis *erg, GError **error) {
	sqlite3 *db = zond_dbase_get_dbase(zond_dbase);
	g_autoptr(GHashTable) eff = effektive_aktionen(maengel);
	g_autoptr(GHashTable) wahl = g_hash_table_new_full(g_str_hash, g_str_equal,
			g_free, NULL);
	g_auto(DbRegelErgebnis) neu = { 0 };

	db_regel_aktion_ergebnis_clear(erg);
	erg->meldungen = g_ptr_array_new_with_free_func(g_free);

	//Wahl der Aktion je Regel und Knoten merken, um sie nach Schicht 1 wiederzufinden
	for (guint i = 0; i < maengel->len; i++) {
		DbMangel *mg = g_ptr_array_index(maengel, i);

		g_hash_table_insert(wahl, wahl_schluessel(mg),
				GINT_TO_POINTER((gint) eff_aktion(eff, mg->knoten_id) + 1));
	}

	if (zond_dbase_begin(zond_dbase, error))
		return -1;

	if (schicht1(db, maengel, eff, erg, error)) {
		zond_dbase_rollback(zond_dbase, NULL);

		return -1;
	}

	if (db_regeln_pruefen(zond_dbase, FALSE, &neu, error)) {
		zond_dbase_rollback(zond_dbase, NULL);

		return -1;
	}

	for (guint i = 0; i < neu.maengel->len; i++) {
		DbMangel *mg = g_ptr_array_index(neu.maengel, i);
		g_autofree gchar *key = wahl_schluessel(mg);
		gpointer p = g_hash_table_lookup(wahl, key);

		mg->gewaehlt = p ? (DbAkt) (GPOINTER_TO_INT(p) - 1) : DB_AKT_BEHALTEN;
	}

	if (schicht2(zond_dbase, db, neu.maengel, erg, error)) {
		zond_dbase_rollback(zond_dbase, NULL);

		return -1;
	}

	if (zond_dbase_commit(zond_dbase, error)) {
		zond_dbase_rollback(zond_dbase, NULL);

		return -1;
	}

	return 0;
}

/* Ohne aktive Fremdschlüssel: sonst weist SQLite jede Änderung an einer Zeile
 * ab, die selbst schon einen ungültigen Verweis hat, und Kaskaden würden
 * unbeabsichtigt Geschwister mitlöschen. Löschen von Teilbäumen geschieht
 * deshalb ausdrücklich. Die Einstellung gilt pro Verbindung und ist nur
 * außerhalb einer Transaktion änderbar. */
gint db_regeln_ausfuehren(ZondDBase *zond_dbase, GPtrArray *maengel,
		DbRegelAktionErgebnis *erg, GError **error) {
	sqlite3 *db = zond_dbase_get_dbase(zond_dbase);
	gint rc = 0;

	if (fuehre_aus(db, "PRAGMA foreign_keys=OFF;", error))
		return -1;

	rc = ausfuehren_ohne_fk(zond_dbase, maengel, erg, error);

	sqlite3_exec(db, "PRAGMA foreign_keys=ON;", NULL, NULL, NULL);

	return rc;
}
