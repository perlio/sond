/*
 zond (db_check_fenster.c) - Akten, Beweisstücke, Unterlagen
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

#include <gtk/gtk.h>
#include <sqlite3.h>

#include "../../misc.h"
#include "../../sond_log_and_error.h"
#include "../../sond_treeview.h"

#include "../zond_init.h"
#include "../zond_dbase.h"
#include "../zond_tree_store.h"

#include "project.h"
#include "db_regeln.h"
#include "db_check_fenster.h"

#define M(a) (1u << (a))

enum {
	SP_TEXT, //Knoten bzw. Art des Fehlers bzw. Feldname
	SP_DETAIL, //Beschreibung des Fehlers bzw. Feldwert
	SP_ORT, //Pfad des Elternknotens
	SP_AUCH, //voller Text eines gekürzten Feldes (Tooltip)
	SP_AKTION, //gewählte Aktion (nur Knotenzeilen)
	SP_MODEL, //zulässige Aktionen für das Auswahlfeld
	SP_PTR, //DbMangel* (Knoten- und Fehlerzeilen)
	SP_ZEILE, //ZEILE_*
	SP_KNOTEN, //Knoten-ID zum Springen, 0 ohne Ziel
	SP_REGEL, //DbRegel (Regel-Zeilen)
	N_SP
};

enum {
	ZEILE_FEHLER, //ein Fehler eines Knotens
	ZEILE_MANGEL, //ein Knoten mit Fehlern; trägt das Auswahlfeld
	ZEILE_KNOTEN, //Glied eines nicht angezeigten Zweigs
	ZEILE_ABSCHNITT, //Oberste Ebene: sichtbar / nicht sichtbar / Datei
	ZEILE_DATENKOPF, //Überschrift »Datensatz«
	ZEILE_FELD, //ein Feld des Datensatzes
	ZEILE_NACHBAR, //Feld, das auf einen anderen Knoten zeigt; füllt sich beim Aufklappen
	ZEILE_KINDER, //Kinder des Knotens; füllt sich beim Aufklappen
	ZEILE_PLATZHALTER //Kind einer noch nicht gefüllten Nachbarzeile
};

//Abschnitte der obersten Ebene (in SP_REGEL der Abschnittszeile)
enum {
	ABSCHNITT_SICHTBAR,
	ABSCHNITT_UNSICHTBAR,
	ABSCHNITT_DATEI
};

typedef struct {
	Projekt *zond;
	GtkWidget *fenster;
	GtkWidget *label;
	GtkWidget *check_integritaet;
	GtkWidget *treeview;
	GtkTreeViewColumn *spalte_aktion;
	GtkTreeStore *store;
	GHashTable *modelle; //Maske -> GtkListStore der Aktionsnamen
	DbRegelErgebnis erg;
	gboolean vor_laden; //Bäume sind noch nicht geladen (Start des Projekts)
	GMainLoop *loop; //nur vor dem Laden: der Aufrufer wartet auf das Schließen
	gboolean *geaendert_out; //wird TRUE, sobald etwas geändert wurde
} Fenster;

static Fenster *g_fenster = NULL;

/* ---- Texte ------------------------------------------------------------------ */

//Text eines Knotens; Anker tragen keinen, dann der des Ziels
static gchar* knoten_text(ZondDBase *work, gint64 id) {
	gint type = 0;
	gint link = 0;
	gchar *text = NULL;
	GError *error = NULL;

	if (id <= 0)
		return g_strdup("");

	if (zond_dbase_get_node(work, (gint) id, &type, &link, NULL, NULL, NULL,
			&text, NULL, &error)) {
		g_clear_error(&error);

		return g_strdup("(Knoten fehlt)");
	}

	if ((!text || !*text) && link > 0) {
		gchar *fp = NULL;

		g_free(text);
		text = NULL;
		if (zond_dbase_get_node(work, link, NULL, NULL, &fp, NULL, NULL, &text,
				NULL, &error))
			g_clear_error(&error);
		if ((!text || !*text) && fp && *fp) {
			g_free(text);
			text = g_path_get_basename(fp);
		}
		g_free(fp);
	}

	if (!text || !*text) {
		g_free(text);

		if (id == 1)
			return g_strdup("Bestandsverzeichnis");
		if (id == 2)
			return g_strdup("Auswertung");

		return g_strdup_printf("ohne Beschriftung, Nr. %" G_GINT64_FORMAT, id);
	}

	return text;
}

//Art und Text eines Knotens, z.B. "Anbindung »a.pdf«"
static gchar* knoten_bezeichnung(ZondDBase *work, gint64 id) {
	gint type = -1;
	GError *error = NULL;
	g_autofree gchar *text = knoten_text(work, id);

	if (id <= 0)
		return g_strdup("");

	if (zond_dbase_get_type_and_link(work, (gint) id, &type, NULL, &error)) {
		g_clear_error(&error);

		return g_strdup(text);
	}

	return g_strdup_printf("%s »%s«", db_typ_name(type), text);
}

//Kürzt einen Text auf höchstens max Zeichen; übernimmt text
static gchar* kuerze_text(gchar *text, glong max) {
	gchar *ergebnis = NULL;

	if (g_utf8_strlen(text, -1) <= max)
		return text;

	ergebnis = g_strdup_printf("%.*s…", (gint) (g_utf8_offset_to_pointer(text,
			max - 1) - text), text);
	g_free(text);

	return ergebnis;
}

/* ---- Auswahlfelder ---------------------------------------------------------- */

//Liste der Aktionsnamen für eine Maske (gemeinsam genutzt)
static GtkTreeModel* modell_fuer(Fenster *f, guint maske) {
	GtkListStore *ls = g_hash_table_lookup(f->modelle, GUINT_TO_POINTER(maske));

	if (!ls) {
		ls = gtk_list_store_new(1, G_TYPE_STRING);
		for (gint a = 0; a < DB_NUM_AKT; a++)
			if (maske & M(a)) {
				GtkTreeIter iter = { 0 };

				gtk_list_store_append(ls, &iter);
				gtk_list_store_set(ls, &iter, 0, db_akt_name(a), -1);
			}
		g_hash_table_insert(f->modelle, GUINT_TO_POINTER(maske), ls);
	}

	return GTK_TREE_MODEL(ls);
}

static gboolean akt_aus_name(gchar const *name, DbAkt *akt) {
	for (gint a = 0; a < DB_NUM_AKT; a++)
		if (!g_strcmp0(name, db_akt_name(a))) {
			*akt = (DbAkt) a;

			return TRUE;
		}

	return FALSE;
}

//Zeigt die gewählte Aktion in allen Knotenzeilen
static void aktionen_anzeigen(Fenster *f) {
	GtkTreeModel *model = GTK_TREE_MODEL(f->store);
	GtkTreeIter iter_a = { 0 };

	if (!gtk_tree_model_get_iter_first(model, &iter_a))
		return;

	do {
		GtkTreeIter iter_m = { 0 };

		if (!gtk_tree_model_iter_children(model, &iter_m, &iter_a))
			continue;

		do {
			DbMangel *m = NULL;

			gtk_tree_model_get(model, &iter_m, SP_PTR, &m, -1);
			if (m)
				gtk_tree_store_set(f->store, &iter_m, SP_AKTION,
						db_akt_name(m->gewaehlt), -1);
		} while (gtk_tree_model_iter_next(model, &iter_m));
	} while (gtk_tree_model_iter_next(model, &iter_a));

	return;
}

static void cb_aktion_geaendert(GtkCellRendererCombo *renderer, gchar *pfad,
		gchar *neu, gpointer data) {
	Fenster *f = data;
	GtkTreeModel *model = GTK_TREE_MODEL(f->store);
	GtkTreeIter iter = { 0 };
	DbMangel *m = NULL;
	gint zeile = 0;
	DbAkt akt = DB_AKT_BEHALTEN;

	if (!akt_aus_name(neu, &akt) || !gtk_tree_model_get_iter_from_string(model,
			&iter, pfad))
		return;

	gtk_tree_model_get(model, &iter, SP_PTR, &m, SP_ZEILE, &zeile, -1);
	if (zeile != ZEILE_MANGEL || !m)
		return;

	db_mangel_setze_aktion(f->erg.maengel, m, akt);
	aktionen_anzeigen(f);

	return;
}

/* ---- Anzeige ---------------------------------------------------------------- */

//Gehört der Mangel in den Abschnitt (sichtbar / nicht sichtbar / Datei)?
static gboolean in_abschnitt(DbMangel const *m, gint abschnitt) {
	if (m->regel == DB_REGEL_F1)
		return abschnitt == ABSCHNITT_DATEI;

	return abschnitt == (m->sichtbar ? ABSCHNITT_SICHTBAR : ABSCHNITT_UNSICHTBAR);
}

//Gibt es den Knoten?
static gboolean knoten_existiert(ZondDBase *work, gint64 id) {
	sqlite3_stmt *stmt = NULL;
	gboolean da = FALSE;

	if (id <= 0 || sqlite3_prepare_v2(zond_dbase_get_dbase(work),
			"SELECT 1 FROM knoten WHERE ID=?1;", -1, &stmt, NULL) != SQLITE_OK)
		return FALSE;

	sqlite3_bind_int64(stmt, 1, id);
	da = sqlite3_step(stmt) == SQLITE_ROW;
	sqlite3_finalize(stmt);

	return da;
}

//Erste Zeile eines Knotens, z.B. "Nr. 12: Punkt »A«"
static gchar* nr_bezeichnung(ZondDBase *work, gint64 id) {
	g_autofree gchar *b = knoten_bezeichnung(work, id);

	return g_strdup_printf("Nr. %" G_GINT64_FORMAT ": %s", id, b);
}

//Fehlertexte, die zu einem Feld des Datensatzes gehören (sonst leer)
static gchar* feld_fehler(Fenster *f, gint64 id, gchar const *name) {
	GString *s = g_string_new(NULL);
	gboolean verweis = !g_strcmp0(name, "Verweis auf");
	g_autofree gchar *marke = g_strdup_printf("»%s«", verweis ? "Verweis" : name);

	for (guint i = 0; i < f->erg.maengel->len; i++) {
		DbMangel *m = g_ptr_array_index(f->erg.maengel, i);

		if (m->knoten_id != id)
			continue;

		switch (m->regel) {
		case DB_REGEL_A2:
			if (!g_strcmp0(name, "Art"))
				g_string_append_printf(s, "%s%s", s->len ? " | " : "", m->text);
			break;
		case DB_REGEL_B1:
		case DB_REGEL_B2:
		case DB_REGEL_D2:
			if (!g_strcmp0(name, "Übergeordnet"))
				g_string_append_printf(s, "%s%s", s->len ? " | " : "", m->text);
			break;
		case DB_REGEL_D1:
			if (!g_strcmp0(name, "Vorgänger"))
				g_string_append_printf(s, "%s%s", s->len ? " | " : "", m->text);
			break;
		case DB_REGEL_E1:
		case DB_REGEL_E2:
		case DB_REGEL_E3:
			if (verweis)
				g_string_append_printf(s, "%s%s", s->len ? " | " : "", m->text);
			break;
		case DB_REGEL_B3: {
			gchar **teile = g_strsplit(m->text, "; ", -1);

			for (gint k = 0; teile[k]; k++)
				if (strstr(teile[k], marke))
					g_string_append_printf(s, "%s%s", s->len ? " | " : "", teile[k]);
			g_strfreev(teile);
			break;
		}
		default:
			break;
		}
	}

	return g_string_free(s, FALSE);
}

//Feld des Datensatzes: Spalte Eintrag »Name: Wert«, Beschreibung = Fehler zum Feld
static void feld_voll_anfuegen(Fenster *f, GtkTreeIter *eltern, gchar const *name,
		gchar const *wert, gchar const *fehler, gint64 sprung, gchar const *voll) {
	GtkTreeIter iter = { 0 };
	g_autofree gchar *text = g_strdup_printf("%s: %s", name, wert);

	gtk_tree_store_append(f->store, &iter, eltern);
	gtk_tree_store_set(f->store, &iter, SP_TEXT, text, SP_DETAIL, fehler, SP_ORT, "",
			SP_AUCH, voll ? voll : "", SP_AKTION, "", SP_PTR, NULL, SP_ZEILE,
			ZEILE_FELD, SP_KNOTEN, sprung, SP_REGEL, 0, -1);

	return;
}

static void feld_anfuegen(Fenster *f, GtkTreeIter *eltern, gchar const *name,
		gchar const *wert, gchar const *fehler, gint64 sprung) {
	feld_voll_anfuegen(f, eltern, name, wert, fehler, sprung, NULL);

	return;
}

//Zeile, die sich beim Aufklappen füllt (id steht im Platzhalter darunter)
static void aufklappzeile_anfuegen(Fenster *f, GtkTreeIter *eltern, gchar const *text,
		gchar const *fehler, gint zeile, gint64 id, gint64 sprung) {
	GtkTreeIter iter = { 0 };
	GtkTreeIter platz = { 0 };

	gtk_tree_store_append(f->store, &iter, eltern);
	gtk_tree_store_set(f->store, &iter, SP_TEXT, text, SP_DETAIL, fehler, SP_ORT, "",
			SP_AKTION, "", SP_PTR, NULL, SP_ZEILE, zeile, SP_KNOTEN, sprung,
			SP_REGEL, 0, -1);
	gtk_tree_store_append(f->store, &platz, &iter);
	gtk_tree_store_set(f->store, &platz, SP_TEXT, "", SP_DETAIL, "", SP_ORT, "",
			SP_AKTION, "", SP_PTR, NULL, SP_ZEILE, ZEILE_PLATZHALTER, SP_KNOTEN, id,
			SP_REGEL, 0, -1);

	return;
}

//Feld, das auf einen anderen Knoten zeigt; existiert er, lässt es sich aufklappen
static void nachbar_anfuegen(Fenster *f, GtkTreeIter *eltern, gchar const *name,
		gint64 id, gchar const *leer, gint64 sprung, gint64 besitzer) {
	ZondDBase *work = f->zond->dbase_zond->zond_dbase_work;
	g_autofree gchar *fehler = feld_fehler(f, besitzer, name);
	g_autofree gchar *wert = NULL;

	if (id == 0) {
		feld_anfuegen(f, eltern, name, leer, fehler, sprung);

		return;
	}
	if (!knoten_existiert(work, id)) {
		wert = g_strdup_printf("Nr. %" G_GINT64_FORMAT " existiert nicht", id);
		feld_anfuegen(f, eltern, name, wert, fehler, sprung);

		return;
	}

	{
		g_autofree gchar *text = NULL;

		wert = nr_bezeichnung(work, id);
		text = g_strdup_printf("%s: %s", name, wert);
		aufklappzeile_anfuegen(f, eltern, text, fehler, ZEILE_NACHBAR, id, id);
	}

	return;
}

//IDs aus einer Abfrage mit einem gint64-Parameter ?1 (und optional ?2)
static GArray* ids_abfragen(ZondDBase *work, gchar const *sql, gint64 p1, gint64 p2) {
	GArray *ids = g_array_new(FALSE, FALSE, sizeof(gint64));
	sqlite3_stmt *stmt = NULL;

	if (sqlite3_prepare_v2(zond_dbase_get_dbase(work), sql, -1, &stmt, NULL)
			!= SQLITE_OK)
		return ids;

	sqlite3_bind_int64(stmt, 1, p1);
	if (sqlite3_bind_parameter_count(stmt) > 1)
		sqlite3_bind_int64(stmt, 2, p2);
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		gint64 v = sqlite3_column_int64(stmt, 0);

		g_array_append_val(ids, v);
	}
	sqlite3_finalize(stmt);

	return ids;
}

//Fügt unter eltern die Felder des Knotens ein (ohne Text; der steht in der Überschrift)
static void datensatz_anfuegen(Fenster *f, GtkTreeIter *eltern, gint64 id,
		gint64 sprung) {
	ZondDBase *work = f->zond->dbase_zond->zond_dbase_work;
	sqlite3_stmt *stmt = NULL;
	gint type = -1;
	gint64 parent = 0;
	gint64 older = 0;
	gint64 link = 0;
	gboolean hat_link = FALSE;
	gchar *werte[5] = { NULL };
	gchar *voll[5] = { NULL };
	gchar const *felder[] = { "Dateipfad", "Abschnitt", "Symbol", "Text", "Notiz" };
	g_autofree gchar *art = NULL;
	g_autofree gchar *fehler_art = NULL;
	g_autoptr(GArray) nachfolger = NULL;
	g_autoptr(GArray) kinder = NULL;

	if (id <= 0 || sqlite3_prepare_v2(zond_dbase_get_dbase(work),
			"SELECT parent_ID, older_sibling_ID, type, link, file_part, section, "
			"icon_name, node_text, text FROM knoten WHERE ID=?1;", -1, &stmt,
			NULL) != SQLITE_OK)
		return;

	sqlite3_bind_int64(stmt, 1, id);
	if (sqlite3_step(stmt) != SQLITE_ROW) {
		sqlite3_finalize(stmt);

		return;
	}

	parent = sqlite3_column_int64(stmt, 0);
	older = sqlite3_column_int64(stmt, 1);
	if (sqlite3_column_type(stmt, 2) != SQLITE_NULL)
		type = sqlite3_column_int(stmt, 2);
	hat_link = sqlite3_column_type(stmt, 3) != SQLITE_NULL;
	link = sqlite3_column_int64(stmt, 3);
	for (gint k = 0; k < 5; k++)
		if (sqlite3_column_type(stmt, 4 + k) != SQLITE_NULL) {
			werte[k] = g_strdup((gchar const*) sqlite3_column_text(stmt, 4 + k));
			voll[k] = g_strdup(werte[k]);
			g_strdelimit(werte[k], "\r\n\t", ' ');
			werte[k] = kuerze_text(werte[k], 150);
		}
	sqlite3_finalize(stmt);

	nachfolger = ids_abfragen(work, "SELECT ID FROM knoten WHERE "
			"older_sibling_ID=?1 AND parent_ID=?2 AND ID<>?1 ORDER BY ID;", id,
			parent);
	kinder = ids_abfragen(work, "SELECT ID FROM knoten WHERE parent_ID=?1 "
			"AND ID<>?1 ORDER BY ID;", id, 0);

	art = (type >= 0 && type <= 5) ? g_strdup_printf("%s (Typ %d)", db_typ_name(type),
			type) : ((type < 0) ? g_strdup("keine Angabe") :
					g_strdup_printf("unbekannt (Typ %d)", type));
	fehler_art = feld_fehler(f, id, "Art");
	feld_anfuegen(f, eltern, "Art", art, fehler_art, sprung);

	nachbar_anfuegen(f, eltern, "Übergeordnet", parent, "keiner (0)", sprung, id);
	nachbar_anfuegen(f, eltern, "Vorgänger", older, "keiner (erster Eintrag)", sprung,
			id);
	if (!nachfolger->len)
		feld_anfuegen(f, eltern, "Nachfolger", "keiner (letzter Eintrag)", "", sprung);
	for (guint k = 0; k < nachfolger->len; k++)
		nachbar_anfuegen(f, eltern, "Nachfolger", g_array_index(nachfolger, gint64, k),
				"", sprung, 0);
	if (hat_link)
		nachbar_anfuegen(f, eltern, "Verweis auf", link, "", sprung, id);

	for (gint k = 0; k < 5; k++) {
		g_autofree gchar *fehler = feld_fehler(f, id, felder[k]);

		if (k == 3 && !*fehler) //Text steht schon in der Überschrift
			;
		else if (werte[k])
			feld_voll_anfuegen(f, eltern, felder[k], werte[k], fehler, sprung,
					voll[k]);
		else if (*fehler)
			feld_anfuegen(f, eltern, felder[k], "(leer)", fehler, sprung);
		g_free(werte[k]);
		g_free(voll[k]);
	}

	if (!kinder->len)
		feld_anfuegen(f, eltern, "Kinder", "keine", "", sprung);
	else {
		g_autofree gchar *text = g_strdup_printf("Kinder: %u", kinder->len);

		aufklappzeile_anfuegen(f, eltern, text, "", ZEILE_KINDER, id, sprung);
	}

	return;
}

//Id aus dem Platzhalter unter der Zeile; entfernt den Platzhalter. FALSE: schon gefüllt
static gboolean platzhalter_nehmen(Fenster *f, GtkTreeIter *iter, gint64 *id) {
	GtkTreeModel *model = GTK_TREE_MODEL(f->store);
	GtkTreeIter kind = { 0 };
	gint zeile = 0;

	if (!gtk_tree_model_iter_children(model, &kind, iter))
		return FALSE;
	gtk_tree_model_get(model, &kind, SP_ZEILE, &zeile, SP_KNOTEN, id, -1);
	if (zeile != ZEILE_PLATZHALTER)
		return FALSE;
	gtk_tree_store_remove(f->store, &kind);

	return TRUE;
}

//Füllt eine Nachbarzeile beim ersten Aufklappen mit den Feldern des Knotens
static void nachbar_fuellen(Fenster *f, GtkTreeIter *iter) {
	gint64 id = 0;

	if (!platzhalter_nehmen(f, iter, &id))
		return;

	datensatz_anfuegen(f, iter, id, id);

	return;
}

//Füllt die Kinderzeile beim ersten Aufklappen: ein aufklappbarer Eintrag je Kind
static void kinder_fuellen(Fenster *f, GtkTreeIter *iter) {
	ZondDBase *work = f->zond->dbase_zond->zond_dbase_work;
	gint64 id = 0;
	g_autoptr(GArray) kinder = NULL;
	guint grenze = 0;

	if (!platzhalter_nehmen(f, iter, &id))
		return;

	kinder = ids_abfragen(work, "SELECT ID FROM knoten WHERE parent_ID=?1 "
			"AND ID<>?1 ORDER BY ID;", id, 0);
	grenze = MIN(kinder->len, 200);
	for (guint k = 0; k < grenze; k++)
		nachbar_anfuegen(f, iter, "Kind", g_array_index(kinder, gint64, k), "",
				g_array_index(kinder, gint64, k), 0);
	if (kinder->len > grenze) {
		g_autofree gchar *rest = g_strdup_printf("%u weitere", kinder->len - grenze);

		feld_anfuegen(f, iter, "Kind", rest, "", id);
	}

	return;
}

static gboolean cb_test_expand_row(GtkTreeView *tv, GtkTreeIter *iter,
		GtkTreePath *path, gpointer data) {
	Fenster *f = data;
	gint zeile = 0;

	gtk_tree_model_get(GTK_TREE_MODEL(f->store), iter, SP_ZEILE, &zeile, -1);
	if (zeile == ZEILE_NACHBAR)
		nachbar_fuellen(f, iter);
	else if (zeile == ZEILE_KINDER)
		kinder_fuellen(f, iter);

	return FALSE;
}

static gboolean gehoert_zu(DbMangel const *m, DbMangel const *erster) {
	return m == erster || (erster->knoten_id > 0
			&& m->knoten_id == erster->knoten_id);
}

//Legt die Zeile eines Knotens an: Beschreibung = alle seine Fehler, darunter die Felder
static void knoten_anfuegen(Fenster *f, GtkTreeIter *iter_a, gint abschnitt,
		DbMangel *erster) {
	ZondDBase *work = f->zond->dbase_zond->zond_dbase_work;
	GtkTreeIter iter_k = { 0 };
	g_autofree gchar *kt = (erster->knoten_id > 0) ?
			nr_bezeichnung(work, erster->knoten_id) : g_strdup("Datenbankdatei");
	GString *texte = g_string_new(NULL);
	gint64 sprung = erster->sichtbar ? erster->knoten_id : erster->gehoert_unter;

	for (guint i = 0; i < f->erg.maengel->len; i++) {
		DbMangel *m = g_ptr_array_index(f->erg.maengel, i);

		if (gehoert_zu(m, erster) && in_abschnitt(m, abschnitt))
			g_string_append_printf(texte, "%s%s", texte->len ? " | " : "", m->text);
	}

	gtk_tree_store_append(f->store, &iter_k, iter_a);
	gtk_tree_store_set(f->store, &iter_k, SP_TEXT, kt, SP_DETAIL, texte->str, SP_ORT,
			"", SP_AKTION, db_akt_name(erster->gewaehlt), SP_MODEL,
			modell_fuer(f, erster->aktionen), SP_PTR, erster, SP_ZEILE,
			ZEILE_MANGEL, SP_KNOTEN, sprung, SP_REGEL, 0, -1);
	g_string_free(texte, TRUE);

	datensatz_anfuegen(f, &iter_k, erster->knoten_id, sprung);

	//nicht angezeigte Zweigglieder
	for (guint i = 0; i < f->erg.maengel->len; i++) {
		DbMangel *m = g_ptr_array_index(f->erg.maengel, i);

		if (!gehoert_zu(m, erster) || !in_abschnitt(m, abschnitt)
				|| m->regel != DB_REGEL_D1 || m->schleife)
			continue;

		for (guint k = 0; k < m->knoten_ids->len; k++) {
			gint64 kid = g_array_index(m->knoten_ids, gint64, k);
			g_autofree gchar *b = nr_bezeichnung(work, kid);
			g_autofree gchar *text = g_strdup_printf("Nicht angezeigt: %s", b);

			aufklappzeile_anfuegen(f, &iter_k, text, "", ZEILE_NACHBAR, kid, sprung);
		}
	}

	return;
}

typedef struct {
	GtkWidget *treeview;
	gboolean knoten;
} AufklappDaten;

static gboolean cb_aufklappen(GtkTreeModel *model, GtkTreePath *path,
		GtkTreeIter *iter, gpointer data) {
	AufklappDaten *ad = data;
	gint zeile = 0;

	gtk_tree_model_get(model, iter, SP_ZEILE, &zeile, -1);
	if (zeile == ZEILE_ABSCHNITT || (ad->knoten && zeile == ZEILE_MANGEL))
		gtk_tree_view_expand_row(GTK_TREE_VIEW(ad->treeview), path, FALSE);

	return FALSE;
}

static void fenster_fuellen(Fenster *f) {
	g_autofree gchar *summe = NULL;
	gint n_aktiv = 0;
	gint n_knoten = 0;
	static gchar const *const abschnitt_titel[] = { "Im Baum sichtbar",
			"Im Baum nicht sichtbar", "Datenbankdatei" };

	gtk_tree_store_clear(f->store);

	for (gint abschnitt = 0; abschnitt < 3; abschnitt++) {
		GtkTreeIter iter_a = { 0 };
		GHashTable *fertig = g_hash_table_new_full(g_int64_hash, g_int64_equal,
				g_free, NULL);
		GPtrArray *erste = g_ptr_array_new();
		g_autofree gchar *titel_a = NULL;

		//je Knoten der erste Befund; Befunde ohne Knoten (Datei) einzeln
		for (guint i = 0; i < f->erg.maengel->len; i++) {
			DbMangel *m = g_ptr_array_index(f->erg.maengel, i);

			if (!in_abschnitt(m, abschnitt))
				continue;
			if (m->knoten_id > 0) {
				gint64 *k = NULL;

				if (g_hash_table_contains(fertig, &m->knoten_id))
					continue;
				k = g_new(gint64, 1);
				*k = m->knoten_id;
				g_hash_table_add(fertig, k);
			}
			g_ptr_array_add(erste, m);
		}

		if (erste->len) {
			titel_a = g_strdup_printf("%s (%u)", abschnitt_titel[abschnitt],
					erste->len);
			gtk_tree_store_append(f->store, &iter_a, NULL);
			gtk_tree_store_set(f->store, &iter_a, SP_TEXT, titel_a, SP_DETAIL, "",
					SP_ORT, "", SP_AKTION, "", SP_PTR, NULL, SP_ZEILE,
					ZEILE_ABSCHNITT, SP_KNOTEN, (gint64) 0, SP_REGEL, abschnitt, -1);

			for (guint i = 0; i < erste->len; i++) {
				DbMangel *m = g_ptr_array_index(erste, i);

				if (m->aktionen & ~M(DB_AKT_BEHALTEN))
					n_aktiv++;
				n_knoten++;
				knoten_anfuegen(f, &iter_a, abschnitt, m);
			}
		}

		g_ptr_array_unref(erste);
		g_hash_table_unref(fertig);
	}

	aktionen_anzeigen(f);

	if (f->erg.maengel->len == 0)
		summe = g_strdup("Keine Befunde. Die Projektdatei ist stimmig.");
	else
		summe = g_strdup_printf("%d Einträge mit Fehlern (%u Befunde), bei %d davon "
				"können Sie eine Aktion wählen.%s", n_knoten, f->erg.maengel->len,
				n_aktiv, f->vor_laden ? "\nDer Baum wird nach dem Schließen "
						"dieses Fensters aufgebaut." : "");
	gtk_label_set_text(GTK_LABEL(f->label), summe);

	//Abschnitte, bei wenigen Einträgen auch deren Knoten; Nachbarzeilen nie (Kreise)
	{
		AufklappDaten ad = { f->treeview, n_knoten <= 15 };

		gtk_tree_model_foreach(GTK_TREE_MODEL(f->store), cb_aufklappen, &ad);
	}

	return;
}

static void fenster_pruefen(Fenster *f) {
	GError *error = NULL;
	gboolean integritaet = gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(f->check_integritaet));

	if (!f->zond->dbase_zond) {
		gtk_widget_destroy(f->fenster);

		return;
	}

	if (db_regeln_pruefen(f->zond->dbase_zond->zond_dbase_work, integritaet,
			&f->erg, &error)) {
		display_message(f->fenster, "Prüfung fehlgeschlagen:\n\n",
				error->message, NULL);
		g_clear_error(&error);
		gtk_tree_store_clear(f->store);
		gtk_label_set_text(GTK_LABEL(f->label), "Prüfung fehlgeschlagen.");

		return;
	}

	fenster_fuellen(f);

	return;
}

static void cb_pruefen(GtkButton *button, gpointer data) {
	fenster_pruefen((Fenster*) data);

	return;
}

static void cb_standard(GtkButton *button, gpointer data) {
	Fenster *f = data;

	db_regel_ergebnis_standard(&f->erg);
	aktionen_anzeigen(f);

	return;
}

/* ---- Erklärungen ------------------------------------------------------------ */

//Tooltip passend zur Zelle unter dem Mauszeiger
static gboolean cb_tooltip(GtkWidget *tv, gint x, gint y, gboolean keyboard,
		GtkTooltip *tip, gpointer data) {
	static gint const spalten[] = { SP_TEXT, SP_DETAIL, SP_AKTION };
	GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(tv));
	GtkTreePath *path = NULL;
	GtkTreeViewColumn *col = NULL;
	GtkTreeIter iter = { 0 };
	GList *cols = NULL;
	gint bx = 0;
	gint by = 0;
	gint idx = 0;
	gint zeile = 0;
	gint regel = 0;
	DbMangel *m = NULL;
	g_autofree gchar *zelltext = NULL;
	g_autofree gchar *text = NULL;

	if (keyboard)
		return FALSE;

	gtk_tree_view_convert_widget_to_bin_window_coords(GTK_TREE_VIEW(tv), x, y,
			&bx, &by);
	if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(tv), bx, by, &path, &col,
			NULL, NULL))
		return FALSE;
	if (!gtk_tree_model_get_iter(model, &iter, path)) {
		gtk_tree_path_free(path);

		return FALSE;
	}

	cols = gtk_tree_view_get_columns(GTK_TREE_VIEW(tv));
	idx = g_list_index(cols, col);
	g_list_free(cols);
	if (idx < 0 || idx >= (gint) G_N_ELEMENTS(spalten)) {
		gtk_tree_path_free(path);

		return FALSE;
	}

	gtk_tree_model_get(model, &iter, SP_ZEILE, &zeile, SP_REGEL, &regel, SP_PTR,
			&m, -1);

	if (zeile == ZEILE_ABSCHNITT) {
		if (regel == ABSCHNITT_SICHTBAR)
			text = g_strdup("Diese Einträge werden im Baum angezeigt. Ein "
					"Doppelklick auf einen Eintrag springt dorthin.");
		else if (regel == ABSCHNITT_UNSICHTBAR)
			text = g_strdup("Diese Einträge sind in der Datei vorhanden, werden "
					"aber im Baum nicht angezeigt. Ein Doppelklick springt zu dem "
					"sichtbaren Eintrag, an dem sie hängen.");
		else
			text = g_strdup("Ergebnis der Prüfung der Datenbankdatei durch "
					"SQLite. Wird nur angezeigt.");
	} else if (zeile == ZEILE_MANGEL && idx == 1 && m) { //Erklärung der Fehler
		GString *t = g_string_new(NULL);
		Fenster *fe = data;

		for (guint i = 0; i < fe->erg.maengel->len; i++) {
			DbMangel *mm = g_ptr_array_index(fe->erg.maengel, i);

			if (mm == m || (m->knoten_id > 0 && mm->knoten_id == m->knoten_id))
				g_string_append_printf(t, "%s%s\n\nWas gilt: %s\n\n%s", t->len ?
						"\n\n----\n\n" : "", db_regel_titel(mm->regel),
						db_regel_gilt(mm->regel), db_regel_erklaerung(mm->regel));
		}
		text = g_string_free(t, FALSE);
	} else if (idx == 2) { //Aktion: was sie bewirkt
		if (m)
			text = g_strdup_printf("%s\n\n%s", db_akt_name(m->gewaehlt),
					db_akt_beschreibung(m->gewaehlt));
	} else {
		gtk_tree_model_get(model, &iter, spalten[idx], &zelltext, -1);
		if (zeile == ZEILE_FELD && idx == 0) { //gekürztes Feld: voller Text
			gchar *voll = NULL;

			gtk_tree_model_get(model, &iter, SP_AUCH, &voll, -1);
			if (voll && *voll)
				text = voll;
			else
				g_free(voll);
		}
		if (!text && zelltext && *zelltext)
			text = g_strdup(zelltext);
	}

	if (!text || !*text) {
		gtk_tree_path_free(path);

		return FALSE;
	}

	gtk_tooltip_set_text(tip, text);
	gtk_tree_view_set_tooltip_cell(GTK_TREE_VIEW(tv), tip, path, col, NULL);
	gtk_tree_path_free(path);

	return TRUE;
}

//Legende: alle Befundarten und Aktionen in einem Fenster
static void cb_legende(GtkButton *button, gpointer data) {
	Fenster *f = data;
	GtkWidget *dialog = gtk_dialog_new_with_buttons("Erklärung der Befunde",
			GTK_WINDOW(f->fenster), GTK_DIALOG_DESTROY_WITH_PARENT, "_Schließen",
			GTK_RESPONSE_CLOSE, NULL);
	GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
	GtkWidget *textview = gtk_text_view_new();
	GString *text = g_string_new(NULL);

	for (gint r = 0; r < DB_NUM_REGELN; r++)
		g_string_append_printf(text, "%s\n\nWas gilt: %s\n\n%s\n\n\n",
				db_regel_titel(r), db_regel_gilt(r), db_regel_erklaerung(r));

	g_string_append(text, "Aktionen\n\n");
	for (gint a = 0; a < DB_NUM_AKT; a++)
		g_string_append_printf(text, "%s\n%s\n\n", db_akt_name(a),
				db_akt_beschreibung(a));

	gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview)),
			text->str, -1);
	g_string_free(text, TRUE);
	gtk_text_view_set_editable(GTK_TEXT_VIEW(textview), FALSE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(textview), GTK_WRAP_WORD);
	gtk_text_view_set_left_margin(GTK_TEXT_VIEW(textview), 8);
	gtk_text_view_set_right_margin(GTK_TEXT_VIEW(textview), 8);

	gtk_container_add(GTK_CONTAINER(scrolled), textview);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
			scrolled, TRUE, TRUE, 0);
	gtk_window_set_default_size(GTK_WINDOW(dialog), 680, 560);
	gtk_widget_show_all(dialog);

	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);

	return;
}

/* ---- Ausführen -------------------------------------------------------------- */

//Aktion je Knoten (höchster Rang unter den gewählten); Anzahl je Aktion
static void zaehle_aktionen(Fenster *f, gint zahlen[DB_NUM_AKT]) {
	GHashTable *eff = g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free,
			NULL);
	GHashTableIter iter;
	gpointer key, value;

	for (gint a = 0; a < DB_NUM_AKT; a++)
		zahlen[a] = 0;

	for (guint i = 0; i < f->erg.maengel->len; i++) {
		DbMangel *m = g_ptr_array_index(f->erg.maengel, i);
		gpointer alt = g_hash_table_lookup(eff, &m->knoten_id);

		if (m->knoten_id <= 0)
			continue;
		if (!alt || db_akt_rang(m->gewaehlt) > db_akt_rang(
				(DbAkt) (GPOINTER_TO_INT(alt) - 1))) {
			gint64 *k = g_new(gint64, 1);

			*k = m->knoten_id;
			g_hash_table_insert(eff, k, GINT_TO_POINTER((gint) m->gewaehlt + 1));
		}
	}

	g_hash_table_iter_init(&iter, eff);
	while (g_hash_table_iter_next(&iter, &key, &value))
		zahlen[GPOINTER_TO_INT(value) - 1]++;

	g_hash_table_unref(eff);

	return;
}

static void cb_ausfuehren(GtkButton *button, gpointer data) {
	Fenster *f = data;
	GError *error = NULL;
	g_auto(DbRegelAktionErgebnis) ae = { 0 };
	g_autofree gchar *path_bak = NULL;
	g_autoptr(GString) frage = g_string_new(NULL);
	g_autoptr(GString) meldung = NULL;
	gint zahlen[DB_NUM_AKT];
	gint gesamt = 0;

	if (!f->zond->dbase_zond)
		return;

	zaehle_aktionen(f, zahlen);
	for (gint a = 0; a < DB_NUM_AKT; a++)
		if (a != DB_AKT_BEHALTEN)
			gesamt += zahlen[a];

	if (!gesamt) {
		display_message(f->fenster, "Es ist nichts zu tun: Bei allen Einträgen "
				"steht »Nichts tun«.", NULL);

		return;
	}

	g_string_append(frage, "Folgende Änderungen (Anzahl der Einträge bzw. "
			"Zweige) ausführen?\n");
	for (gint a = 0; a < DB_NUM_AKT; a++)
		if (a != DB_AKT_BEHALTEN && zahlen[a])
			g_string_append_printf(frage, "\n- %s: %d", db_akt_name(a), zahlen[a]);
	if (zahlen[DB_AKT_LOESCHEN])
		g_string_append(frage, "\n\nGelöscht wird samt Unterbäumen. Dateien und "
				"Abschnitte werden nie gelöscht, ebenso nichts, worauf noch "
				"etwas von außerhalb verweist.");
	if (!f->zond->dbase_zond->sicherung_gemacht)
		g_string_append(frage, "\n\nVorher wird eine Sicherungskopie der "
				"Projektdatei angelegt.");

	if (abfrage_frage(f->fenster, "Ausführen", frage->str, NULL)
			!= GTK_RESPONSE_YES)
		return;

	if (project_sicherungskopie(f->zond, &path_bak, &error)) {
		display_message(f->fenster, "Sicherungskopie fehlgeschlagen, es wurde "
				"nichts geändert:\n\n", error->message, NULL);
		g_clear_error(&error);

		return;
	}

	if (db_regeln_ausfuehren(f->zond->dbase_zond->zond_dbase_work,
			f->erg.maengel, &ae, &error)) {
		display_message(f->fenster, "Ausführen fehlgeschlagen "
				"(zurückgerollt):\n\n", error->message, NULL);
		g_clear_error(&error);

		return;
	}

	if (f->geaendert_out)
		*f->geaendert_out = TRUE;

	//Die Bäume im Speicher beruhen auf dem alten Stand (vor dem Laden gibt es
	//noch keine)
	if (!f->vor_laden && project_load_trees(f->zond, &error) == -1) {
		display_message(f->fenster, "Bäume konnten nicht neu geladen "
				"werden:\n\n", error->message, NULL);
		g_clear_error(&error);
	}

	meldung = g_string_new("Ausgeführt:");
	if (ae.n_geloescht)
		g_string_append_printf(meldung, "\n- %d gelöscht", ae.n_geloescht);
	if (ae.n_verschoben)
		g_string_append_printf(meldung, "\n- %d in Sammelpunkte gelegt", ae.n_verschoben);
	if (ae.n_ketten)
		g_string_append_printf(meldung, "\n- %d Zweige behandelt", ae.n_ketten);
	if (ae.n_umgewandelt)
		g_string_append_printf(meldung, "\n- %d umgewandelt", ae.n_umgewandelt);
	if (ae.n_bereinigt)
		g_string_append_printf(meldung, "\n- %d bereinigt", ae.n_bereinigt);
	if (ae.n_angelegt)
		g_string_append_printf(meldung, "\n- %d angelegt", ae.n_angelegt);
	if (ae.n_sammelpunkte)
		g_string_append_printf(meldung, "\n\n%d Punkt(e) »Wiedergefundene Knoten« "
				"angelegt.", ae.n_sammelpunkte);
	for (guint i = 0; ae.meldungen && i < ae.meldungen->len; i++)
		g_string_append_printf(meldung, "\n! %s",
				(gchar const*) g_ptr_array_index(ae.meldungen, i));
	if (path_bak)
		g_string_append_printf(meldung, "\n\nSicherungskopie: %s", path_bak);
	g_string_append(meldung, "\n\nDie Änderung gilt erst nach dem Speichern.");

	fenster_pruefen(f); //macht die gewählten Mängel ungültig
	display_message(f->fenster, meldung->str, NULL);

	return;
}

/* ---- Springen --------------------------------------------------------------- */

//Springt im Inhalts- oder Auswertungsbaum zum Knoten
static void springe_zu(Fenster *f, gint64 id) {
	Projekt *zond = f->zond;
	gint root = 0;
	GError *error = NULL;
	Baum baum = KEIN_BAUM;
	GtkTreeIter *iter = NULL;
	gint type = 0;
	gint link = 0;

	/* Anbindungen stehen im Baum unter der Nummer ihres Ziels (Datei/Abschnitt),
	 * Links als Kopfzeile mit der Nummer des Links unter der Nummer ihres Ziels
	 * (diese Kopfzeile liegt in dem Baum, in dem der Link steht). */
	if (!zond_dbase_get_type_and_link(zond->dbase_zond->zond_dbase_work,
			(gint) id, &type, &link, &error) && link > 0) {
		if (type == 2)
			id = link;
		else if (type == 4) {
			gint root_link = 0;
			gint root_ziel = 0;

			if (!zond_dbase_get_tree_root(zond->dbase_zond->zond_dbase_work,
					(gint) id, &root_link, NULL)
					&& !zond_dbase_get_tree_root(zond->dbase_zond->zond_dbase_work,
							link, &root_ziel, NULL)
					&& (root_link == 1 || root_link == 2)
					&& (root_ziel == 1 || root_ziel == 2)) {
				Baum b_ziel = (root_ziel == 1) ? BAUM_INHALT : BAUM_AUSWERTUNG;
				Baum b_link = (root_link == 1) ? BAUM_INHALT : BAUM_AUSWERTUNG;
				GtkTreeIter *iter_link = zond_tree_store_get_iter_link(
						ZOND_TREE_STORE(gtk_tree_view_get_model(
								GTK_TREE_VIEW(zond->treeview[b_ziel]))), link,
						(gint) id);

				if (iter_link) {
					sond_treeview_expand_to_row(zond->treeview[b_link], iter_link);
					sond_treeview_set_cursor(zond->treeview[b_link], iter_link);
					gtk_tree_iter_free(iter_link);

					gtk_window_present(GTK_WINDOW(zond->app_window));
					gtk_widget_grab_focus(GTK_WIDGET(zond->treeview[b_link]));

					return;
				}
			}
			id = link;
		}
	}
	g_clear_error(&error);

	if (zond_dbase_get_tree_root(zond->dbase_zond->zond_dbase_work, (gint) id,
			&root, &error)) {
		g_clear_error(&error);
		display_message(f->fenster, "Der Knoten hat keine Verbindung zu einer "
				"Wurzel.", NULL);

		return;
	}

	if (root == 1)
		baum = BAUM_INHALT;
	else if (root == 2)
		baum = BAUM_AUSWERTUNG;
	else {
		display_message(f->fenster, "Der Knoten gehört zum Dateibaum und kann "
				"nicht angezeigt werden.", NULL);

		return;
	}

	iter = zond_tree_store_get_iter_by_node_id(
			ZOND_TREE_STORE(gtk_tree_view_get_model(
					GTK_TREE_VIEW(zond->treeview[baum]))), (gint) id);
	if (!iter) {
		display_message(f->fenster, "Der Knoten ist im Baum nicht sichtbar.",
				NULL);

		return;
	}

	sond_treeview_expand_to_row(zond->treeview[baum], iter);
	sond_treeview_set_cursor(zond->treeview[baum], iter);
	gtk_tree_iter_free(iter);

	gtk_window_present(GTK_WINDOW(zond->app_window));
	gtk_widget_grab_focus(GTK_WIDGET(zond->treeview[baum]));

	return;
}

static void cb_row_activated(GtkTreeView *tv, GtkTreePath *path,
		GtkTreeViewColumn *col, gpointer data) {
	Fenster *f = data;
	GtkTreeModel *model = gtk_tree_view_get_model(tv);
	GtkTreeIter iter = { 0 };
	gint64 id = 0;
	gint zeile = 0;

	if (!f->zond->dbase_zond || !gtk_tree_model_get_iter(model, &iter, path))
		return;

	gtk_tree_model_get(model, &iter, SP_KNOTEN, &id, SP_ZEILE, &zeile, -1);
	if (zeile == ZEILE_DATENKOPF || zeile == ZEILE_ABSCHNITT
			|| zeile == ZEILE_KINDER) {
		if (gtk_tree_view_row_expanded(tv, path))
			gtk_tree_view_collapse_row(tv, path);
		else
			gtk_tree_view_expand_row(tv, path, FALSE);

		return;
	}
	if (f->vor_laden) {
		display_message(f->fenster, "Der Baum ist noch nicht geladen, ein "
				"Sprung ist erst danach möglich.", NULL);

		return;
	}
	if (id <= 0) {
		display_message(f->fenster, "Dieser Eintrag wird im Baum nicht "
				"angezeigt und hat keinen übergeordneten Eintrag, zu dem man "
				"springen könnte.", NULL);

		return;
	}

	springe_zu(f, id);

	return;
}

/* ---- Fenster ---------------------------------------------------------------- */

static void cb_destroy(GtkWidget *widget, gpointer data) {
	Fenster *f = data;

	if (f->loop)
		g_main_loop_quit(f->loop);
	db_regel_ergebnis_clear(&f->erg);
	g_hash_table_unref(f->modelle);
	g_free(f);
	g_fenster = NULL;

	return;
}

static void spalte_hinzu(GtkWidget *tv, gchar const *titel, gint spalte,
		gboolean expand) {
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
	GtkTreeViewColumn *col = NULL;

	g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
	col = gtk_tree_view_column_new_with_attributes(titel, renderer, "text",
			spalte, NULL);

	gtk_tree_view_column_set_resizable(col, TRUE);
	gtk_tree_view_column_set_min_width(col, 60);
	gtk_tree_view_column_set_max_width(col, 450);
	gtk_tree_view_column_set_expand(col, expand);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tv), col);

	return;
}

//Nur Knotenzeilen haben ein Auswahlfeld
static void aktion_zelldaten(GtkTreeViewColumn *col, GtkCellRenderer *renderer,
		GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
	gint zeile = ZEILE_KNOTEN;

	gtk_tree_model_get(model, iter, SP_ZEILE, &zeile, -1);
	g_object_set(renderer, "editable", zeile == ZEILE_MANGEL, NULL);

	return;
}

static void spalte_aktion_hinzu(Fenster *f) {
	GtkCellRenderer *renderer = gtk_cell_renderer_combo_new();
	GtkTreeViewColumn *col = NULL;

	g_object_set(renderer, "has-entry", FALSE, "text-column", 0, NULL);
	g_signal_connect(renderer, "edited", G_CALLBACK(cb_aktion_geaendert), f);
	col = gtk_tree_view_column_new_with_attributes("Aktion", renderer, "text",
			SP_AKTION, "model", SP_MODEL, NULL);
	gtk_tree_view_column_set_cell_data_func(col, renderer, aktion_zelldaten, NULL,
			NULL);
	gtk_tree_view_column_set_resizable(col, TRUE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(f->treeview), col);
	f->spalte_aktion = col;

	return;
}

/* Ein Klick auf die Zelle "Aktion" öffnet gleich das Auswahlfeld (sonst erst
 * nach Auswählen der Zeile und einem zweiten Klick). */
static gboolean cb_button_press(GtkWidget *tv, GdkEventButton *ev, gpointer data) {
	Fenster *f = data;
	GtkTreePath *path = NULL;
	GtkTreeViewColumn *col = NULL;
	gboolean behandelt = FALSE;

	if (ev->type != GDK_BUTTON_PRESS || ev->button != 1)
		return FALSE;

	if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(tv), (gint) ev->x,
			(gint) ev->y, &path, &col, NULL, NULL))
		return FALSE;

	if (col == f->spalte_aktion) {
		GtkTreeIter iter = { 0 };
		gint zeile = ZEILE_KNOTEN;
		GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(tv));

		if (gtk_tree_model_get_iter(model, &iter, path)) {
			gtk_tree_model_get(model, &iter, SP_ZEILE, &zeile, -1);
			if (zeile == ZEILE_MANGEL) {
				gtk_tree_view_set_cursor(GTK_TREE_VIEW(tv), path, col, TRUE);
				behandelt = TRUE;
			}
		}
	}
	gtk_tree_path_free(path);

	return behandelt;
}

static Fenster* fenster_erzeugen(Projekt *zond, gboolean vor_laden) {
	Fenster *f = NULL;
	GtkWidget *box = NULL;
	GtkWidget *scrolled = NULL;
	GtkWidget *buttons = NULL;
	GtkWidget *b_pruefen = NULL;
	GtkWidget *b_legende = NULL;
	GtkWidget *b_standard = NULL;
	GtkWidget *b_ausfuehren = NULL;

	if (!zond->dbase_zond)
		return NULL;

	if (g_fenster) {
		gtk_window_present(GTK_WINDOW(g_fenster->fenster));

		return NULL;
	}

	f = g_new0(Fenster, 1);
	g_fenster = f;
	f->zond = zond;
	f->vor_laden = vor_laden;
	f->modelle = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL,
			g_object_unref);

	f->fenster = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(f->fenster), "Datenbank prüfen");
	gtk_window_set_transient_for(GTK_WINDOW(f->fenster),
			GTK_WINDOW(zond->app_window));
	{
		/* Das Hauptfenster liegt in der linken Bildschirmhälfte (zond_init.c);
		 * dieses Fenster daneben, damit beide sichtbar sind. */
		GdkMonitor *monitor = gdk_display_get_primary_monitor(
				gdk_display_get_default());
		GdkRectangle workarea = { 0 };

		if (monitor) {
			gdk_monitor_get_workarea(monitor, &workarea);
			gtk_window_set_default_size(GTK_WINDOW(f->fenster),
					workarea.width / 2, workarea.height);
			gtk_window_move(GTK_WINDOW(f->fenster),
					workarea.x + workarea.width / 2, workarea.y);
		} else
			gtk_window_set_default_size(GTK_WINDOW(f->fenster), 1000, 620);
	}

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_container_set_border_width(GTK_CONTAINER(box), 8);
	gtk_container_add(GTK_CONTAINER(f->fenster), box);

	{
		GtkWidget *hilfe = gtk_label_new("Hier stehen Einträge, bei denen die "
				"Projektdatei nicht stimmig ist. Zu jedem Eintrag sind alle seine "
				"Fehler aufgelistet, dazu sein Datensatz (aufklappbar). In der Spalte "
				"»Aktion« ist vorgeschlagen, was mit dem Eintrag geschehen soll; mit "
				"einem Klick auf die Zelle können Sie etwas anderes wählen. »Nichts "
				"tun« lässt einen Eintrag unverändert.\n"
				"Oben stehen die Einträge, die im Baum angezeigt werden (Doppelklick "
				"springt hin), darunter die nicht angezeigten (Doppelklick springt "
				"zu dem Eintrag, an dem sie hängen).\n"
				"Einträge, die nirgends hinpassen oder nicht angezeigt wurden, "
				"werden in einem neuen Punkt »Wiedergefundene Knoten« abgelegt. "
				"Dort können Sie sie einsortieren oder löschen.\n"
				"Mit »Ausführen« werden die Änderungen vorgenommen; vorher wird "
				"eine Sicherungskopie der Projektdatei angelegt.");

		gtk_label_set_xalign(GTK_LABEL(hilfe), 0.0);
		gtk_label_set_line_wrap(GTK_LABEL(hilfe), TRUE);
		gtk_box_pack_start(GTK_BOX(box), hilfe, FALSE, FALSE, 0);
	}

	f->label = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(f->label), 0.0);
	gtk_label_set_line_wrap(GTK_LABEL(f->label), TRUE);
	gtk_box_pack_start(GTK_BOX(box), f->label, FALSE, FALSE, 0);

	f->check_integritaet = gtk_check_button_new_with_label(
			"Zusätzlich die Datenbankdatei selbst prüfen (nur Anzeige, langsamer)");
	gtk_widget_set_tooltip_text(f->check_integritaet, "Lässt SQLite prüfen, ob "
			"die Datei beschädigt ist (quick_check: defekte Seiten oder Indizes) "
			"und ob Zeilen auf nicht vorhandene Zeilen verweisen "
			"(foreign_key_check). Das ist eine Prüfung der Datei, nicht der "
			"Baumstruktur. Gefundenes wird nur angezeigt und lässt sich hier nicht "
			"reparieren.");
	gtk_box_pack_start(GTK_BOX(box), f->check_integritaet, FALSE, FALSE, 0);

	f->store = gtk_tree_store_new(N_SP, G_TYPE_STRING, G_TYPE_STRING,
			G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, GTK_TYPE_TREE_MODEL,
			G_TYPE_POINTER, G_TYPE_INT, G_TYPE_INT64, G_TYPE_INT);
	f->treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(f->store));
	g_object_unref(f->store);
	gtk_tree_view_set_enable_tree_lines(GTK_TREE_VIEW(f->treeview), TRUE);
	gtk_tree_selection_set_mode(
			gtk_tree_view_get_selection(GTK_TREE_VIEW(f->treeview)),
			GTK_SELECTION_SINGLE);
	gtk_widget_set_has_tooltip(f->treeview, TRUE);
	g_signal_connect(f->treeview, "query-tooltip", G_CALLBACK(cb_tooltip), f);
	spalte_hinzu(f->treeview, "Eintrag", SP_TEXT, TRUE);
	spalte_hinzu(f->treeview, "Beschreibung", SP_DETAIL, TRUE);
	spalte_aktion_hinzu(f);
	g_signal_connect(f->treeview, "row-activated", G_CALLBACK(cb_row_activated),
			f);
	g_signal_connect(f->treeview, "test-expand-row",
			G_CALLBACK(cb_test_expand_row), f);
	g_signal_connect(f->treeview, "button-press-event",
			G_CALLBACK(cb_button_press), f);

	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(scrolled), f->treeview);
	gtk_box_pack_start(GTK_BOX(box), scrolled, TRUE, TRUE, 0);

	buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END);
	gtk_box_set_spacing(GTK_BOX(buttons), 6);
	b_legende = gtk_button_new_with_label("Erklärung der Befunde");
	b_pruefen = gtk_button_new_with_label("Erneut prüfen");
	b_standard = gtk_button_new_with_label("Alles auf Standard");
	b_ausfuehren = gtk_button_new_with_label("Ausführen");
	gtk_container_add(GTK_CONTAINER(buttons), b_legende);
	gtk_container_add(GTK_CONTAINER(buttons), b_pruefen);
	gtk_container_add(GTK_CONTAINER(buttons), b_standard);
	gtk_container_add(GTK_CONTAINER(buttons), b_ausfuehren);
	gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);

	g_signal_connect(b_legende, "clicked", G_CALLBACK(cb_legende), f);
	g_signal_connect(b_pruefen, "clicked", G_CALLBACK(cb_pruefen), f);
	g_signal_connect(b_standard, "clicked", G_CALLBACK(cb_standard), f);
	g_signal_connect(b_ausfuehren, "clicked", G_CALLBACK(cb_ausfuehren), f);
	g_signal_connect(f->fenster, "destroy", G_CALLBACK(cb_destroy), f);

	if (vor_laden)
		gtk_window_set_modal(GTK_WINDOW(f->fenster), TRUE);

	gtk_widget_show_all(f->fenster);

	fenster_pruefen(f);

	return f;
}

void db_check_fenster_oeffnen(Projekt *zond) {
	fenster_erzeugen(zond, FALSE);

	return;
}

//Gibt es Mängel, zu denen es mehr als »Behalten« gibt?
static gboolean hat_aktive_maengel(Projekt *zond) {
	g_auto(DbRegelErgebnis) erg = { 0 };
	GError *error = NULL;
	gboolean aktiv = FALSE;

	if (!zond->dbase_zond)
		return FALSE;

	if (db_regeln_pruefen(zond->dbase_zond->zond_dbase_work, FALSE, &erg,
			&error)) {
		LOG_WARN("%s: %s", __func__, error->message);
		g_clear_error(&error);

		return FALSE;
	}

	for (guint i = 0; i < erg.maengel->len; i++)
		if (((DbMangel*) g_ptr_array_index(erg.maengel, i))->aktionen
				& ~M(DB_AKT_BEHALTEN))
			aktiv = TRUE;

	return aktiv;
}

//nach dem Laden der Bäume: Fenster daneben, wenn es Mängel gibt
void db_check_fenster_oeffnen_wenn_maengel(Projekt *zond) {
	if (hat_aktive_maengel(zond))
		db_check_fenster_oeffnen(zond);

	return;
}

gboolean db_check_fenster_vor_laden(Projekt *zond) {
	gboolean geaendert = FALSE;
	Fenster *f = NULL;
	GMainLoop *loop = NULL;

	if (!hat_aktive_maengel(zond))
		return FALSE;

	f = fenster_erzeugen(zond, TRUE);
	if (!f)
		return FALSE;

	f->geaendert_out = &geaendert;
	loop = g_main_loop_new(NULL, FALSE);
	f->loop = loop;
	g_main_loop_run(loop); //kehrt zurück, wenn das Fenster geschlossen ist
	g_main_loop_unref(loop);

	return geaendert;
}

void db_check_fenster_schliessen(void) {
	if (g_fenster)
		gtk_widget_destroy(g_fenster->fenster);

	return;
}
