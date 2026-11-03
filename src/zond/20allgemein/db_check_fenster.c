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
#include "../../sond_file_helper.h"

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
	SP_ICON, //Symbolname (Symbol-Zeile)
	SP_NR, //Nummer des Knotens
	SP_ART, //Art des Knotens (oberste Ebene)
	N_SP
};

enum {
	ZEILE_FEHLER, //ein Fehler eines Knotens
	ZEILE_MANGEL, //ein Knoten mit Fehlern; trägt das Auswahlfeld
	ZEILE_KNOTEN, //Glied eines nicht angezeigten Zweigs
	ZEILE_ABSCHNITT, //Oberste Ebene: Art der Einträge (Gruppe)
	ZEILE_DATENKOPF, //Überschrift »Datensatz«
	ZEILE_FELD, //ein Feld des Datensatzes
	ZEILE_NACHBAR, //Feld, das auf einen anderen Knoten zeigt; füllt sich beim Aufklappen
	ZEILE_KINDER, //Kinder des Knotens; füllt sich beim Aufklappen
	ZEILE_PLATZHALTER //Kind einer noch nicht gefüllten Nachbarzeile
};

//Gruppen der obersten Ebene: Art des Eintrags (in SP_REGEL der Gruppenzeile)
#define GRUPPE_UNBEKANNT 6
#define GRUPPE_DATEI 7

static struct {
	gchar const *titel;
	gchar const *gilt;
} const gruppen[] = {
	{ "Strukturpunkte", "Ein Strukturpunkt hat Symbol und Text (Notiz möglich), aber keinen Verweis "
			"und keinen Dateipfad. Er steht unter einer Wurzel, einem Strukturpunkt oder "
			"einer Copy." },
	{ "Anbindungen", "Eine Anbindung steht im Bestandsverzeichnis unter einem "
			"Strukturpunkt und verweist auf eine Datei oder einen Abschnitt. Sie trägt sonst "
			"keine Angaben und hat keine Einträge unter sich. Jede Datei ist nur "
			"einmal angebunden." },
	{ "Copies", "Eine Copy steht in der Auswertung und verweist auf eine Anbindung "
			"oder eine Datei. Sie hat Symbol und Text (Notiz möglich) und darf "
			"Einträge unter sich haben." },
	{ "Links", "Ein Link steht in der Auswertung und verweist auf einen Strukturpunkt, eine "
			"Anbindung, eine Copy oder eine Datei, nie auf einen anderen Link. Er "
			"trägt sonst keine Angaben und hat keine Einträge unter sich." },
	{ "Dateien und Abschnitte", "Dateien und Abschnitte bilden den Dateibaum: eine "
			"Datei mit Dateipfad, darunter ihre Abschnitte. Gelöscht werden "
			"sie nur auf Ihre Wahl und nur, wenn nichts darauf verweist." },
	{ "Wurzeln", "Es gibt genau zwei Wurzeln: Bestandsverzeichnis und Auswertung." },
	{ "Einträge mit unbekanntem Typ", "Jeder Eintrag ist ein Strukturpunkt, eine Anbindung, "
			"eine Copy, ein Link oder eine Datei/ein Abschnitt." },
	{ "Datenbankdatei", "Ergebnis der Prüfung der Datenbankdatei durch SQLite. Wird "
			"nur angezeigt." }
};

typedef struct {
	Projekt *zond;
	GtkWidget *fenster;
	GtkWidget *label;
	GtkWidget *check_dateien;
	GtkWidget *buttons;
	GtkWidget *b_abbrechen;
	struct _DateiPruefung *datei; //läuft gerade, sonst NULL
	guint timer; //Fortschrittsanzeige
	gboolean zerstoert; //Fenster wurde während der Prüfung geschlossen
	GtkWidget *treeview;
	GtkTreeViewColumn *spalte_aktion;
	GtkTreeStore *store;
	GHashTable *modelle; //Maske -> GtkListStore der Aktionsnamen
	DbRegelErgebnis erg;
	gboolean vor_laden; //Bäume sind noch nicht geladen (Start des Projekts)
	GMainLoop *loop; //nur vor dem Laden: der Aufrufer wartet auf das Schließen
	gboolean *geaendert_out; //wird TRUE, sobald etwas geändert wurde
	gint n_knoten; //Einträge mit Fehlern
	gint n_aktiv; //davon mit wählbarer Aktion
	gint sort_spalte; //SORT_*
	gboolean sort_ab; //absteigend
	GtkTreeViewColumn *sort_cols[3];
} Fenster;

enum {
	SORT_NR,
	SORT_ART,
	SORT_BESCHREIBUNG
};

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

static void summe_anzeigen(Fenster *f);

//Zeigt die gewählte Aktion in allen Zeilen mit Mangel (Knotenzeilen und Nachbarzeilen)
static void aktionen_ab(Fenster *f, GtkTreeIter *eltern) {
	GtkTreeModel *model = GTK_TREE_MODEL(f->store);
	GtkTreeIter iter = { 0 };
	gboolean da = eltern ? gtk_tree_model_iter_children(model, &iter, eltern) :
			gtk_tree_model_get_iter_first(model, &iter);

	while (da) {
		DbMangel *m = NULL;

		gtk_tree_model_get(model, &iter, SP_PTR, &m, -1);
		if (m)
			gtk_tree_store_set(f->store, &iter, SP_AKTION, db_akt_name(m->gewaehlt),
					-1);
		aktionen_ab(f, &iter);
		da = gtk_tree_model_iter_next(model, &iter);
	}

	return;
}

static void aktionen_anzeigen(Fenster *f) {
	aktionen_ab(f, NULL);
	summe_anzeigen(f);

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
	if ((zeile != ZEILE_MANGEL && zeile != ZEILE_NACHBAR) || !m)
		return;

	db_mangel_setze_aktion(f->erg.maengel, m, akt);
	aktionen_anzeigen(f);

	return;
}

/* ---- Anzeige ---------------------------------------------------------------- */

//Typ des Knotens, -1 ohne Angabe oder ohne Knoten
static gint knoten_typ(ZondDBase *work, gint64 id) {
	sqlite3_stmt *stmt = NULL;
	gint typ = -1;

	if (id <= 0 || sqlite3_prepare_v2(zond_dbase_get_dbase(work),
			"SELECT type FROM knoten WHERE ID=?1;", -1, &stmt, NULL) != SQLITE_OK)
		return -1;

	sqlite3_bind_int64(stmt, 1, id);
	if (sqlite3_step(stmt) == SQLITE_ROW
			&& sqlite3_column_type(stmt, 0) != SQLITE_NULL)
		typ = sqlite3_column_int(stmt, 0);
	sqlite3_finalize(stmt);

	return typ;
}

//Gruppe (Art des Eintrags), in der der Mangel steht
static gint gruppe_von(ZondDBase *work, DbMangel const *m) {
	gint typ = 0;

	if (m->regel == DB_REGEL_F1 || m->knoten_id <= 0)
		return GRUPPE_DATEI;

	typ = knoten_typ(work, m->knoten_id);
	switch (typ) {
	case 1:
		return 0;
	case 2:
		return 1;
	case 3:
		return 2;
	case 4:
		return 3;
	case 5:
		return 4;
	case 0:
		return 5;
	default:
		return GRUPPE_UNBEKANNT;
	}
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

//Wird der Knoten im geladenen Baum angezeigt? Leer bei Wurzeln, Dateien und unbekannten Knoten
static gchar const* im_baum_text(Fenster *f, gint64 id) {
	gint typ = knoten_typ(f->zond->dbase_zond->zond_dbase_work, id);

	if (id <= 0 || typ <= 0)
		return "";
	if (typ == 5) { //Dateien und Abschnitte: angezeigt, wo ihre Datei angebunden ist
		if (f->vor_laden)
			return "";
		for (gint k = 0; k < 2; k++) {
			Baum baum = (k == 0) ? BAUM_INHALT : BAUM_AUSWERTUNG;
			GtkTreeIter *iter = zond_tree_store_get_iter_by_node_id(
					ZOND_TREE_STORE(gtk_tree_view_get_model(
							GTK_TREE_VIEW(f->zond->treeview[baum]))), (gint) id);

			if (iter) {
				gtk_tree_iter_free(iter);

				return "ja";
			}
		}

		return "nein";
	}
	if (f->erg.unsichtbar && g_hash_table_contains(f->erg.unsichtbar,
			GINT_TO_POINTER((gint) id)))
		return "nein";

	return (typ == 2) ? "über die Datei" : "ja";
}

//Erster Mangel des Knotens (für Beschreibung und Aktion in dessen Nachbarzeilen)
static DbMangel* mangel_von(Fenster *f, gint64 id) {
	for (guint i = 0; id > 0 && i < f->erg.maengel->len; i++) {
		DbMangel *m = g_ptr_array_index(f->erg.maengel, i);

		if (m->knoten_id == id)
			return m;
	}

	return NULL;
}

//Text und Symbol, wie der Knoten im Baum steht; Anbindungen zeigen, was ihre Datei hat
static void knoten_anzeige(ZondDBase *work, gint64 id, gchar **text, gchar **icon) {
	sqlite3_stmt *stmt = NULL;

	*text = NULL;
	*icon = NULL;
	if (id <= 0 || sqlite3_prepare_v2(zond_dbase_get_dbase(work),
			"SELECT k.type, k.node_text, COALESCE(NULLIF(k.icon_name,''), "
			"l.icon_name) FROM knoten k LEFT JOIN knoten l ON l.ID=k.link AND "
			"k.type=2 WHERE k.ID=?1;", -1, &stmt, NULL) != SQLITE_OK)
		return;

	sqlite3_bind_int64(stmt, 1, id);
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		gboolean anbindung = sqlite3_column_type(stmt, 0) != SQLITE_NULL
				&& sqlite3_column_int(stmt, 0) == 2;

		if (sqlite3_column_type(stmt, 1) != SQLITE_NULL
				&& *sqlite3_column_text(stmt, 1))
			*text = g_strdup((gchar const*) sqlite3_column_text(stmt, 1));
		else if (anbindung)
			*text = knoten_text(work, id);
		if (sqlite3_column_type(stmt, 2) != SQLITE_NULL)
			*icon = g_strdup((gchar const*) sqlite3_column_text(stmt, 2));
	}
	sqlite3_finalize(stmt);

	return;
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
		case DB_REGEL_C1:
			if (!g_strcmp0(name, "Art"))
				g_string_append_printf(s, "%s%s", s->len ? " | " : "", m->text);
			break;
		case DB_REGEL_C2:
		case DB_REGEL_C3:
		case DB_REGEL_B1:
			if (!g_strcmp0(name, "Übergeordnet"))
				g_string_append_printf(s, "%s%s", s->len ? " | " : "", m->text);
			break;
		case DB_REGEL_B2:
			if (!g_strcmp0(name, "Vorgänger"))
				g_string_append_printf(s, "%s%s", s->len ? " | " : "", m->text);
			break;
		case DB_REGEL_D1:
		case DB_REGEL_D2:
			if (verweis)
				g_string_append_printf(s, "%s%s", s->len ? " | " : "", m->text);
			break;
		case DB_REGEL_C4: {
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
	gboolean fehlerhaft = fehler && *fehler;
	g_autofree gchar *text = g_strdup_printf("%s%s: %s", fehlerhaft ? "⚠ " : "", name,
			wert);
	g_autofree gchar *tipp = NULL;

	if (fehlerhaft) //Problem des Knotens an diesem Feld
		tipp = g_strdup_printf("%s%sFehler an diesem Feld:\n%s", voll ? voll : "",
				voll ? "\n\n" : "", fehler);

	gtk_tree_store_append(f->store, &iter, eltern);
	gtk_tree_store_set(f->store, &iter, SP_TEXT, text, SP_DETAIL, fehler, SP_ORT, "",
			SP_AUCH, tipp ? tipp : (voll ? voll : ""), SP_AKTION, "", SP_PTR, NULL,
			SP_ZEILE, ZEILE_FELD, SP_KNOTEN, sprung, SP_REGEL, 0, -1);

	return;
}

static void feld_anfuegen(Fenster *f, GtkTreeIter *eltern, gchar const *name,
		gchar const *wert, gchar const *fehler, gint64 sprung) {
	feld_voll_anfuegen(f, eltern, name, wert, fehler, sprung, NULL);

	return;
}

//Steht der Knoten schon in einer Nachbarzeile oberhalb (Kreis)? Dann bleibt die Zeile ohne Unterzeilen.
static gboolean schon_oben(Fenster *f, GtkTreeIter *eltern, gint64 id) {
	GtkTreeModel *model = GTK_TREE_MODEL(f->store);
	GtkTreeIter cur = *eltern;
	GtkTreeIter up = { 0 };

	while (TRUE) {
		gint zeile = 0;
		gint nr = 0;

		gtk_tree_model_get(model, &cur, SP_ZEILE, &zeile, SP_REGEL, &nr, -1);
		if (zeile == ZEILE_NACHBAR && nr == (gint) id) //die Hauptzeile zählt nicht
			return TRUE;
		if (!gtk_tree_model_iter_parent(model, &up, &cur))
			return FALSE;
		cur = up;
	}
}

//Zeile, die sich beim Aufklappen füllt (id steht im Platzhalter darunter). Eine
//Nachbarzeile zeigt Beschreibung, Im Baum und Aktion des Nachbarknotens; fehler ist
//dann ein Fehler am Feld des Besitzers und wird nur am Eintrag markiert.
static void aufklappzeile_anfuegen(Fenster *f, GtkTreeIter *eltern, gchar const *text,
		gchar const *fehler, gint zeile, gint64 id, gint64 sprung) {
	GtkTreeIter iter = { 0 };
	GtkTreeIter platz = { 0 };
	g_autofree gchar *anzeige = NULL;
	g_autofree gchar *tipp = NULL;
	g_autofree gchar *eigene = NULL;
	gchar const *detail = fehler;
	gchar const *im_baum = "";
	DbMangel *m = NULL;
	gboolean oben = zeile == ZEILE_NACHBAR && schon_oben(f, eltern, id);
	g_autofree gchar *nr = (zeile == ZEILE_NACHBAR) ?
			g_strdup_printf("%" G_GINT64_FORMAT, id) : g_strdup("");

	if (zeile == ZEILE_NACHBAR) {
		GString *s = g_string_new(NULL);

		for (guint i = 0; i < f->erg.maengel->len; i++) {
			DbMangel *mm = g_ptr_array_index(f->erg.maengel, i);

			if (mm->knoten_id == id)
				g_string_append_printf(s, "%s%s", s->len ? " | " : "", mm->text);
		}
		eigene = g_string_free(s, FALSE);
		detail = eigene;
		im_baum = im_baum_text(f, id);
		m = mangel_von(f, id);
		if (fehler && *fehler) {
			anzeige = g_strdup_printf("⚠ %s", text);
			tipp = g_strdup_printf("Fehler an diesem Feld:\n%s", fehler);
			text = anzeige;
		}
		if (oben) {
			gchar *mit = g_strdup_printf("%s (siehe oben)", text);

			g_free(anzeige);
			anzeige = mit;
			text = anzeige;
		}
	}

	gtk_tree_store_append(f->store, &iter, eltern);
	gtk_tree_store_set(f->store, &iter, SP_TEXT, text, SP_DETAIL, detail, SP_ORT,
			im_baum, SP_AUCH, tipp ? tipp : "", SP_AKTION,
			m ? db_akt_name(m->gewaehlt) : "", SP_MODEL,
			m ? modell_fuer(f, m->aktionen) : NULL, SP_PTR, m, SP_ZEILE, zeile,
			SP_KNOTEN, sprung, SP_REGEL, (zeile == ZEILE_NACHBAR) ? (gint) id : 0,
			SP_NR, nr, -1);
	if (oben)
		return;
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

//Fügt unter eltern die Beschreibung des Knotens (Art, Notiz, Pfad, Abschnitt) und
//seine Verbindungen zu anderen Knoten ein; Symbol und Text stehen in der Hauptzeile
static void datensatz_anfuegen(Fenster *f, GtkTreeIter *eltern, gint64 id,
		gint64 sprung) {
	ZondDBase *work = f->zond->dbase_zond->zond_dbase_work;
	sqlite3_stmt *stmt = NULL;
	gint type = -1;
	gint64 parent = 0;
	gint64 older = 0;
	gint64 link = 0;
	gboolean hat_link = FALSE;
	gchar *werte[4] = { NULL };
	gchar *voll[4] = { NULL };
	gchar const *felder[] = { "Dateipfad", "Abschnitt", "Text", "Notiz" };
	g_autoptr(GArray) nachfolger = NULL;
	g_autoptr(GArray) kinder = NULL;

	if (id <= 0 || sqlite3_prepare_v2(zond_dbase_get_dbase(work),
			"SELECT parent_ID, older_sibling_ID, type, link, file_part, section, "
			"node_text, text FROM knoten WHERE ID=?1;", -1, &stmt, NULL)
			!= SQLITE_OK)
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
	for (gint k = 0; k < 4; k++)
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

	//Art des Knotens
	{
		g_autofree gchar *art = (type >= 0 && type <= 5) ? g_strdup_printf("%s (Typ %d)",
				db_typ_name(type), type) : ((type < 0) ? g_strdup("keine Angabe") :
						g_strdup_printf("unbekannt (Typ %d)", type));
		g_autofree gchar *fehler_art = feld_fehler(f, id, "Art");

		feld_anfuegen(f, eltern, "Art", art, fehler_art, sprung);
	}

	//Notiz, Dateipfad und Abschnitt, soweit vorhanden oder fehlerhaft
	{
		gint reihe[] = { 3, 0, 1 }; //Notiz, Dateipfad, Abschnitt
		for (guint r = 0; r < G_N_ELEMENTS(reihe); r++) {
			gint k = reihe[r];
			g_autofree gchar *fehler = feld_fehler(f, id, felder[k]);

			if (werte[k])
				feld_voll_anfuegen(f, eltern, felder[k], werte[k], fehler, sprung,
						voll[k]);
			else if (*fehler)
				feld_anfuegen(f, eltern, felder[k], "(leer)", fehler, sprung);
		}
	}
	for (gint k = 0; k < 4; k++) {
		g_free(werte[k]);
		g_free(voll[k]);
	}

	//Verbindungen zu anderen Knoten
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

//»Alles darunter aufklappen« (Taste *, Umschalt+Pfeil rechts) klappt nur die Zeile selbst auf:
//die Nachbarzeilen füllen sich beim Aufklappen immer weiter, das gäbe einen riesigen Baum
static gboolean cb_aufklappen_rekursiv(GtkTreeView *tv, gboolean logical,
		gboolean expand, gboolean open_all, gpointer data) {
	GtkTreePath *path = NULL;

	if (!expand || !open_all)
		return FALSE;

	g_signal_stop_emission_by_name(tv, "expand-collapse-cursor-row");
	gtk_tree_view_get_cursor(tv, &path, NULL);
	if (path) {
		gtk_tree_view_expand_row(tv, path, FALSE);
		gtk_tree_path_free(path);
	}

	return TRUE;
}

static gboolean gehoert_zu(DbMangel const *m, DbMangel const *erster) {
	return m == erster || (erster->knoten_id > 0
			&& m->knoten_id == erster->knoten_id);
}

//Legt die Zeile eines Knotens an: Beschreibung = alle seine Fehler, darunter die Felder
static void knoten_anfuegen(Fenster *f, GtkTreeIter *iter_a, DbMangel *erster) {
	ZondDBase *work = f->zond->dbase_zond->zond_dbase_work;
	GtkTreeIter iter_k = { 0 };
	g_autofree gchar *kt = NULL;
	g_autofree gchar *kt_text = NULL;
	g_autofree gchar *kt_icon = NULL;
	g_autofree gchar *kt_tipp = NULL;
	g_autofree gchar *kt_nr = NULL;
	gint kt_typ = knoten_typ(work, erster->knoten_id);
	GString *texte = g_string_new(NULL);
	//ein nicht angezeigter Knoten springt nicht selbst, sondern über seine Nachbarzeilen
	gint64 sprung = (erster->knoten_id > 0 && erster->sichtbar) ? erster->knoten_id : 0;
	gchar const *im_baum = im_baum_text(f, erster->knoten_id);

	if (erster->knoten_id > 0) {
		knoten_anzeige(work, erster->knoten_id, &kt_text, &kt_icon);
		kt = g_strdup(kt_text ? kt_text : "(kein Text)");
		kt_tipp = g_strdup_printf("%s\n\nSymbol: %s", kt_text ? kt_text :
				"(kein Text)", (kt_icon && *kt_icon) ? kt_icon : "keines");
		kt_nr = g_strdup_printf("%" G_GINT64_FORMAT, erster->knoten_id);
	} else
		kt = g_strdup("Datenbankdatei");

	for (guint i = 0; i < f->erg.maengel->len; i++) {
		DbMangel *m = g_ptr_array_index(f->erg.maengel, i);

		if (gehoert_zu(m, erster))
			g_string_append_printf(texte, "%s%s", texte->len ? " | " : "", m->text);
	}

	gtk_tree_store_append(f->store, &iter_k, iter_a);
	gtk_tree_store_set(f->store, &iter_k, SP_TEXT, kt, SP_DETAIL, texte->str, SP_ORT,
			im_baum, SP_AUCH, kt_tipp ? kt_tipp : "", SP_ICON, kt_icon ? kt_icon : "",
			SP_AKTION, db_akt_name(erster->gewaehlt), SP_MODEL,
			modell_fuer(f, erster->aktionen), SP_PTR, erster, SP_ZEILE,
			ZEILE_MANGEL, SP_KNOTEN, sprung, SP_REGEL,
			(gint) erster->knoten_id, SP_NR, kt_nr ? kt_nr : "", SP_ART,
			(erster->knoten_id > 0) ? ((kt_typ >= 0) ? db_typ_name(kt_typ) : "ohne Art") :
					"", -1);
	g_string_free(texte, TRUE);

	datensatz_anfuegen(f, &iter_k, erster->knoten_id, sprung);

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
	if (ad->knoten && zeile == ZEILE_MANGEL)
		gtk_tree_view_expand_row(GTK_TREE_VIEW(ad->treeview), path, FALSE);

	return FALSE;
}

//Sortierung der obersten Ebene nach Nr., Art oder Beschreibung; Einträge ohne Nr. zuletzt
static gint cmp_mangel_sort(gconstpointer pa, gconstpointer pb, gpointer data) {
	Fenster *f = data;
	DbMangel const *a = *(DbMangel* const*) pa;
	DbMangel const *b = *(DbMangel* const*) pb;
	ZondDBase *work = f->zond->dbase_zond->zond_dbase_work;
	gint c = 0;

	if ((a->knoten_id <= 0) != (b->knoten_id <= 0))
		return (a->knoten_id <= 0) ? 1 : -1;

	if (f->sort_spalte == SORT_ART)
		c = gruppe_von(work, a) - gruppe_von(work, b);
	else if (f->sort_spalte == SORT_BESCHREIBUNG)
		c = g_utf8_collate(a->text, b->text);
	if (!c)
		c = (a->knoten_id > b->knoten_id) - (a->knoten_id < b->knoten_id);
	if (!c)
		c = (a->regel > b->regel) - (a->regel < b->regel);

	return f->sort_ab ? -c : c;
}

//Pfeil an der sortierten Spaltenüberschrift
static void sortierung_anzeigen(Fenster *f) {
	for (gint k = 0; k < 3; k++) {
		gtk_tree_view_column_set_sort_indicator(f->sort_cols[k], k == f->sort_spalte);
		gtk_tree_view_column_set_sort_order(f->sort_cols[k], f->sort_ab ?
				GTK_SORT_DESCENDING : GTK_SORT_ASCENDING);
	}

	return;
}

static void fenster_fuellen(Fenster *f) {
	gint n_aktiv = 0;
	gint n_knoten = 0;
	g_autoptr(GHashTable) fertig = g_hash_table_new_full(g_int64_hash, g_int64_equal,
			g_free, NULL);
	g_autoptr(GPtrArray) erste = g_ptr_array_new();

	gtk_tree_store_clear(f->store);

	//je Knoten der erste Befund; Befunde ohne Knoten (Datenbankdatei) einzeln
	for (guint i = 0; i < f->erg.maengel->len; i++) {
		DbMangel *m = g_ptr_array_index(f->erg.maengel, i);

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
	g_ptr_array_sort_with_data(erste, cmp_mangel_sort, f);
	sortierung_anzeigen(f);

	for (guint i = 0; i < erste->len; i++) {
		DbMangel *m = g_ptr_array_index(erste, i);

		if (m->aktionen & ~M(DB_AKT_BEHALTEN))
			n_aktiv++;
		n_knoten++;
		knoten_anfuegen(f, NULL, m);
	}

	f->n_knoten = n_knoten;
	f->n_aktiv = n_aktiv;
	aktionen_anzeigen(f);

	//bei wenigen Einträgen auch deren Knoten; Nachbarzeilen nie (Kreise)
	{
		AufklappDaten ad = { f->treeview, n_knoten <= 15 };

		gtk_tree_model_foreach(GTK_TREE_MODEL(f->store), cb_aufklappen, &ad);
	}

	return;
}

//Klick auf eine Spaltenüberschrift: danach sortieren, ein zweiter Klick dreht um
static void cb_sortieren(GtkTreeViewColumn *col, gpointer data) {
	Fenster *f = data;
	gint k = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(col), "sort-key"));

	if (k == f->sort_spalte)
		f->sort_ab = !f->sort_ab;
	else {
		f->sort_spalte = k;
		f->sort_ab = FALSE;
	}
	fenster_fuellen(f);

	return;
}


static void zaehle_aktionen(Fenster *f, gint zahlen[DB_NUM_AKT]);

//Zusammenfassung über der Tabelle, mit der Zahl der Einträge, bei denen eine Aktion gewählt ist
static void summe_anzeigen(Fenster *f) {
	g_autofree gchar *summe = NULL;
	gint zahlen[DB_NUM_AKT];
	gint gewaehlt = 0;

	if (!f->erg.maengel || f->erg.maengel->len == 0) {
		gtk_label_set_text(GTK_LABEL(f->label),
				"Keine Befunde. Die Projektdatei ist stimmig.");

		return;
	}

	zaehle_aktionen(f, zahlen);
	for (gint a = 0; a < DB_NUM_AKT; a++)
		if (a != DB_AKT_BEHALTEN)
			gewaehlt += zahlen[a];

	summe = g_strdup_printf("%d Einträge mit Fehlern (%u Befunde), bei %d davon "
			"können Sie eine Aktion wählen. Gewählt: %d.%s", f->n_knoten,
			f->erg.maengel->len, f->n_aktiv, gewaehlt, f->vor_laden ?
					"\nDer Baum wird nach dem Schließen dieses Fensters aufgebaut." : "");
	gtk_label_set_text(GTK_LABEL(f->label), summe);

	return;
}

static void fenster_pruefen_mit(Fenster *f, GHashTable *fehlend) {
	GError *error = NULL;

	if (!f->zond->dbase_zond) {
		gtk_widget_destroy(f->fenster);

		return;
	}

	if (db_regeln_pruefen(f->zond->dbase_zond->zond_dbase_work, TRUE,
			fehlend, f->zond->project_dir, &f->erg, &error)) {
		display_message(f->fenster, "Prüfung fehlgeschlagen:\n\n",
				error->message, NULL);
		g_clear_error(&error);
		gtk_tree_store_clear(f->store);
		gtk_label_set_text(GTK_LABEL(f->label), "Prüfung fehlgeschlagen.");

		return;
	}

	//beim Öffnen und nach jeder Prüfung ist nichts gewählt
	for (guint i = 0; i < f->erg.maengel->len; i++)
		((DbMangel*) g_ptr_array_index(f->erg.maengel, i))->gewaehlt = DB_AKT_BEHALTEN;

	fenster_fuellen(f);

	return;
}

/* ---- Prüfung der Dateien auf der Festplatte (Regel E4) ----------------------- */

typedef struct _DateiPruefung {
	GPtrArray *pfade; //äußere Pfade der Dateizeilen
	gchar *projekt_dir;
	GHashTable *fehlend; //Ergebnis: fehlende Pfade
	gint fortschritt; //atomar
	gint abbruch; //atomar
} DateiPruefung;

static void datei_pruefung_free(DateiPruefung *d) {
	if (!d)
		return;

	if (d->pfade)
		g_ptr_array_unref(d->pfade);
	if (d->fehlend)
		g_hash_table_unref(d->fehlend);
	g_free(d->projekt_dir);
	g_free(d);

	return;
}

//Läuft im Hintergrund, damit ein hängendes Laufwerk das Fenster nicht einfriert;
//nur Pfade, keine Datenbank, keine Oberfläche. Nichts wird geöffnet oder geladen.
static void datei_thread(GTask *task, gpointer source, gpointer data,
		GCancellable *cancel) {
	DateiPruefung *d = data;

	for (guint i = 0; i < d->pfade->len && !g_atomic_int_get(&d->abbruch); i++) {
		gchar const *pfad = g_ptr_array_index(d->pfade, i);
		//absolute Pfade (ältere Einträge von Containern) gelten, wie sie sind
		g_autofree gchar *voll = g_path_is_absolute(pfad) ? g_strdup(pfad) :
				g_build_filename(d->projekt_dir, pfad, NULL);

		//sond_exists kennt lange Pfade (über 260 Zeichen), wie sie auf SeaDrive vorkommen
		if (!sond_exists(voll))
			g_hash_table_add(d->fehlend, g_strdup(pfad));
		g_atomic_int_inc(&d->fortschritt);
	}
	g_task_return_boolean(task, TRUE);

	return;
}

static gboolean cb_datei_fortschritt(gpointer data) {
	Fenster *f = data;
	g_autofree gchar *text = NULL;

	if (!f->datei)
		return G_SOURCE_REMOVE;

	text = g_strdup_printf("Prüfe Dateien … %d von %u",
			g_atomic_int_get(&f->datei->fortschritt), f->datei->pfade->len);
	gtk_label_set_text(GTK_LABEL(f->label), text);

	return G_SOURCE_CONTINUE;
}

static void fenster_freigeben(Fenster *f) {
	db_regel_ergebnis_clear(&f->erg);
	g_hash_table_unref(f->modelle);
	g_free(f);

	return;
}

static void cb_datei_fertig(GObject *quelle, GAsyncResult *res, gpointer data) {
	Fenster *f = data;
	DateiPruefung *d = f->datei;
	gboolean abgebrochen = g_atomic_int_get(&d->abbruch);
	guint n = d->pfade->len;
	guint fehlt = g_hash_table_size(d->fehlend);

	f->datei = NULL;
	if (f->timer)
		g_source_remove(f->timer);
	f->timer = 0;

	if (f->zerstoert) {
		datei_pruefung_free(d);
		fenster_freigeben(f);

		return;
	}

	gtk_widget_set_sensitive(f->buttons, TRUE);
	gtk_widget_set_sensitive(f->check_dateien, TRUE);
	gtk_widget_hide(f->b_abbrechen);

	if (abgebrochen) {
		display_message(f->fenster, "Die Prüfung der Dateien wurde abgebrochen. "
				"Es werden nur die übrigen Regeln angezeigt.", NULL);
		fenster_pruefen_mit(f, NULL);
	} else if (n >= 5 && fehlt * 10 >= n * 9) {
		g_autofree gchar *meldung = g_strdup_printf("Von %u Dateien fehlen %u. Das "
				"liegt fast sicher an einem nicht verbundenen Laufwerk oder einem "
				"falschen Pfad, nicht an den Dateien. Die Dateien werden deshalb "
				"nicht als fehlend gemeldet; es werden nur die übrigen Regeln "
				"angezeigt.", n, fehlt);

		display_message(f->fenster, meldung, NULL);
		fenster_pruefen_mit(f, NULL);
	} else
		fenster_pruefen_mit(f, d->fehlend);

	datei_pruefung_free(d);

	return;
}

static void cb_abbrechen(GtkButton *button, gpointer data) {
	Fenster *f = data;

	if (f->datei)
		g_atomic_int_set(&f->datei->abbruch, 1);

	return;
}

static void datei_pruefung_starten(Fenster *f) {
	GError *error = NULL;
	DateiPruefung *d = NULL;
	GTask *task = NULL;

	if (!f->zond->project_dir || !sond_exists(f->zond->project_dir)) {
		display_message(f->fenster, "Das Projektverzeichnis ist nicht erreichbar "
				"(Laufwerk verbunden?). Die Dateien werden nicht geprüft.", NULL);
		fenster_pruefen_mit(f, NULL);

		return;
	}

	d = g_new0(DateiPruefung, 1);
	d->pfade = db_regeln_dateipfade(f->zond->dbase_zond->zond_dbase_work, &error);
	if (!d->pfade) {
		display_message(f->fenster, "Prüfung fehlgeschlagen:\n\n", error->message,
				NULL);
		g_clear_error(&error);
		datei_pruefung_free(d);

		return;
	}
	d->projekt_dir = g_strdup(f->zond->project_dir);
	d->fehlend = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

	if (!d->pfade->len) {
		datei_pruefung_free(d);
		fenster_pruefen_mit(f, NULL);

		return;
	}

	f->datei = d;
	gtk_widget_set_sensitive(f->buttons, FALSE);
	gtk_widget_set_sensitive(f->check_dateien, FALSE);
	gtk_widget_show(f->b_abbrechen);
	gtk_widget_set_sensitive(f->b_abbrechen, TRUE);
	cb_datei_fortschritt(f);
	f->timer = g_timeout_add(200, cb_datei_fortschritt, f);

	task = g_task_new(NULL, NULL, cb_datei_fertig, f);
	g_task_set_task_data(task, d, NULL);
	g_task_run_in_thread(task, datei_thread);
	g_object_unref(task);

	return;
}

static void fenster_pruefen(Fenster *f) {
	if (!f->zond->dbase_zond) {
		gtk_widget_destroy(f->fenster);

		return;
	}

	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(f->check_dateien)))
		datei_pruefung_starten(f);
	else
		fenster_pruefen_mit(f, NULL);

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
	static gint const spalten[] = { SP_NR, SP_TEXT, SP_ART, SP_DETAIL, SP_ORT,
			SP_AKTION };
	GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(tv));
	GtkTreePath *path = NULL;
	GtkTreeViewColumn *col = NULL;
	GtkTreeIter iter = { 0 };
	GList *cols = NULL;
	gint bx = 0;
	gint by = 0;
	gint idx = 0;
	gint zeile = 0;
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

	gtk_tree_model_get(model, &iter, SP_ZEILE, &zeile, SP_PTR, &m, -1);

	if (idx == 0) {
		gtk_tree_path_free(path);

		return FALSE;
	}
	if (zeile == ZEILE_MANGEL && idx == 2 && m) { //was für diese Art gilt
		gint g = gruppe_von(((Fenster*) data)->zond->dbase_zond->zond_dbase_work, m);

		if (g >= 0 && g < (gint) G_N_ELEMENTS(gruppen))
			text = g_strdup(gruppen[g].gilt);
	} else if ((zeile == ZEILE_MANGEL || zeile == ZEILE_NACHBAR) && idx == 3
			&& m) { //Erklärung der Fehler
		GString *t = g_string_new(NULL);
		Fenster *fe = data;

		for (guint i = 0; i < fe->erg.maengel->len; i++) {
			DbMangel *mm = g_ptr_array_index(fe->erg.maengel, i);

			if (mm == m || (m->knoten_id > 0 && mm->knoten_id == m->knoten_id))
				g_string_append_printf(t, "%s%s\n\nWas gilt: %s\n\n%s", t->len ?
						"\n\n----\n\n" : "", mm->text, db_regel_gilt(mm->regel),
						db_regel_erklaerung(mm->regel));
		}
		text = g_string_free(t, FALSE);
	} else if ((zeile == ZEILE_MANGEL || zeile == ZEILE_NACHBAR) && idx == 4) {
		text = g_strdup("Ob der Eintrag im geladenen Baum angezeigt wird. "
				"Anbindungen haben keine eigene Zeile, sie erscheinen als ihre Datei. "
				"Bei »nein« springt die Zeile nicht selbst; Übergeordnet, Vorgänger "
				"und Nachfolger darunter führen zur Umgebung.");
	} else if (idx == 5) { //Aktion: was sie bewirkt
		if (m)
			text = g_strdup_printf("%s\n\n%s", db_akt_name(m->gewaehlt),
					db_akt_beschreibung(m->gewaehlt));
	} else {
		gtk_tree_model_get(model, &iter, spalten[idx], &zelltext, -1);
		if ((zeile == ZEILE_FELD || zeile == ZEILE_NACHBAR || zeile == ZEILE_MANGEL)
				&& idx == 1) { //voller Text bzw. Feldfehler
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
		g_string_append(frage, "\n\nGelöscht wird samt Unterbäumen, die folgenden "
				"Einträge rücken nach. Nichts wird gelöscht, worauf noch etwas von "
				"außerhalb verweist. Die Dateien auf der Festplatte bleiben "
				"erhalten.");
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
	if (ae.n_relativ)
		g_string_append_printf(meldung, "\n- %d auf relative Pfade umgestellt",
				ae.n_relativ);
	if (ae.n_sammelpunkte)
		g_string_append_printf(meldung, "\n\n%d Strukturpunkt(e) »Wiedergefundene Knoten« "
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

//Springt im Inhalts- oder Auswertungsbaum zum Knoten; FALSE, wenn er dort nicht steht
static gboolean springe_versuch(Fenster *f, gint64 id) {
	Projekt *zond = f->zond;
	gint root = 0;
	GError *error = NULL;
	Baum baum = KEIN_BAUM;
	GtkTreeIter *iter = NULL;
	gint type = 0;
	gint link = 0;
	gint root_anker = 0;

	/* Anbindungen stehen im Baum unter der Nummer ihres Ziels (Datei/Abschnitt),
	 * Links als Kopfzeile mit der Nummer des Links unter der Nummer ihres Ziels
	 * (diese Kopfzeile liegt in dem Baum, in dem der Link steht). */
	if (!zond_dbase_get_type_and_link(zond->dbase_zond->zond_dbase_work,
			(gint) id, &type, &link, &error) && link > 0) {
		if (type == 2) {
			//die Datei liegt in der Datenbank im Dateibaum; im Baum steht sie dort, wo die Anbindung steht
			zond_dbase_get_tree_root(zond->dbase_zond->zond_dbase_work, (gint) id,
					&root_anker, NULL);
			id = link;
		} else if (type == 4) {
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

					return TRUE;
				}
			}
			id = link;
		}
	}
	g_clear_error(&error);

	if (root_anker == 1 || root_anker == 2)
		root = root_anker;
	else if (zond_dbase_get_tree_root(zond->dbase_zond->zond_dbase_work, (gint) id,
			&root, &error)) {
		g_clear_error(&error);

		return FALSE;
	}

	//Dateien und Abschnitte liegen in der Datenbank im Dateibaum; angezeigt werden
	//sie dort, wo ihre Datei angebunden ist
	for (gint k = 0; k < 2 && !iter; k++) {
		Baum b = (k == 0) ? BAUM_INHALT : BAUM_AUSWERTUNG;

		if ((root == 1 && b != BAUM_INHALT) || (root == 2 && b != BAUM_AUSWERTUNG))
			continue;
		iter = zond_tree_store_get_iter_by_node_id(
				ZOND_TREE_STORE(gtk_tree_view_get_model(
						GTK_TREE_VIEW(zond->treeview[b]))), (gint) id);
		baum = b;
	}
	if (!iter)
		return FALSE;

	sond_treeview_expand_to_row(zond->treeview[baum], iter);
	sond_treeview_set_cursor(zond->treeview[baum], iter);
	gtk_tree_iter_free(iter);

	gtk_window_present(GTK_WINDOW(zond->app_window));
	gtk_widget_grab_focus(GTK_WIDGET(zond->treeview[baum]));

	return TRUE;
}

//Springt zum Knoten, sonst zum nächsten übergeordneten, der im Baum steht
static void springe_zu(Fenster *f, gint64 id) {
	ZondDBase *work = f->zond->dbase_zond->zond_dbase_work;
	gint64 cur = id;

	if (springe_versuch(f, id))
		return;

	for (gint schritt = 0; schritt < 200 && cur > 0; schritt++) {
		gint parent = 0;
		GError *error = NULL;

		if (zond_dbase_get_parent(work, (gint) cur, &parent, &error)) {
			g_clear_error(&error);

			break;
		}
		if (parent <= 0)
			break;
		cur = parent;
		if (springe_versuch(f, cur)) {
			display_message(f->fenster, "Der Eintrag ist im Baum nicht sichtbar. "
					"Angezeigt wird der nächste sichtbare übergeordnete Eintrag.",
					NULL);

			return;
		}
	}

	display_message(f->fenster, "Der Eintrag ist im Baum nicht sichtbar, und kein "
			"übergeordneter Eintrag wird dort angezeigt.", NULL);

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
	if (zeile == ZEILE_DATENKOPF || zeile == ZEILE_KINDER) {
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
				"angezeigt. Ein Doppelklick auf Übergeordnet, Vorgänger oder "
				"Nachfolger darunter springt zu seiner Umgebung.", NULL);

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
	g_fenster = NULL;

	if (f->datei) { //die Prüfung der Dateien läuft noch: sie gibt das Fenster frei
		g_atomic_int_set(&f->datei->abbruch, 1);
		f->zerstoert = TRUE;

		return;
	}
	fenster_freigeben(f);

	return;
}

static GtkTreeViewColumn* spalte_hinzu(GtkWidget *tv, gchar const *titel, gint spalte,
		gboolean expand) {
	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
	GtkTreeViewColumn *col = NULL;

	g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
	col = gtk_tree_view_column_new_with_attributes(titel, renderer, "text",
			spalte, NULL);
	if (spalte == SP_TEXT) { //Symbol des Knotens direkt vor dem Text
		GtkCellRenderer *symbol = gtk_cell_renderer_pixbuf_new();

		g_object_set(symbol, "xpad", 2, NULL);
		gtk_tree_view_column_pack_start(col, symbol, FALSE);
		gtk_tree_view_column_add_attribute(col, symbol, "icon-name", SP_ICON);
		gtk_cell_layout_reorder(GTK_CELL_LAYOUT(col), symbol, 0);
	}

	gtk_tree_view_column_set_resizable(col, TRUE);
	gtk_tree_view_column_set_min_width(col, 60);
	gtk_tree_view_column_set_max_width(col, 450);
	gtk_tree_view_column_set_expand(col, expand);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tv), col);

	return col;
}

//Spalte, die sich durch Klick auf die Überschrift sortieren lässt
static void spalte_sortierbar(Fenster *f, GtkTreeViewColumn *col, gint k) {
	f->sort_cols[k] = col;
	g_object_set_data(G_OBJECT(col), "sort-key", GINT_TO_POINTER(k));
	gtk_tree_view_column_set_clickable(col, TRUE);
	g_signal_connect(col, "clicked", G_CALLBACK(cb_sortieren), f);

	return;
}

//Nur Knotenzeilen haben ein Auswahlfeld
static void aktion_zelldaten(GtkTreeViewColumn *col, GtkCellRenderer *renderer,
		GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
	gint zeile = ZEILE_KNOTEN;
	DbMangel *m = NULL;

	gtk_tree_model_get(model, iter, SP_ZEILE, &zeile, SP_PTR, &m, -1);
	g_object_set(renderer, "editable", zeile == ZEILE_MANGEL
			|| (zeile == ZEILE_NACHBAR && m), NULL);

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

/* Mehrfachauswahl: markierte Zeilen sammeln und die Aktion für alle setzen, die sie
 * erlauben. */
static void auswahl_sammeln(GtkTreeModel *model, GtkTreePath *path,
		GtkTreeIter *iter, gpointer data) {
	GPtrArray *liste = data;
	DbMangel *m = NULL;
	gint zeile = 0;

	gtk_tree_model_get(model, iter, SP_PTR, &m, SP_ZEILE, &zeile, -1);
	if (m && (zeile == ZEILE_MANGEL || zeile == ZEILE_NACHBAR))
		g_ptr_array_add(liste, m);

	return;
}

static void cb_menue_aktion(GtkMenuItem *item, gpointer data) {
	Fenster *f = data;
	DbAkt akt = (DbAkt) GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "akt"));
	g_autoptr(GPtrArray) liste = g_ptr_array_new();

	gtk_tree_selection_selected_foreach(
			gtk_tree_view_get_selection(GTK_TREE_VIEW(f->treeview)),
			auswahl_sammeln, liste);
	for (guint i = 0; i < liste->len; i++) {
		DbMangel *m = g_ptr_array_index(liste, i);

		if (m->aktionen & M(akt))
			db_mangel_setze_aktion(f->erg.maengel, m, akt);
	}
	aktionen_anzeigen(f);

	return;
}

//Menü mit allen Aktionen, die mindestens eine markierte Zeile erlaubt
static void aktionsmenue_zeigen(Fenster *f, GdkEventButton *ev) {
	g_autoptr(GPtrArray) liste = g_ptr_array_new();
	GtkWidget *menu = gtk_menu_new();
	guint maske = 0;

	gtk_tree_selection_selected_foreach(
			gtk_tree_view_get_selection(GTK_TREE_VIEW(f->treeview)),
			auswahl_sammeln, liste);
	for (guint i = 0; i < liste->len; i++)
		maske |= ((DbMangel*) g_ptr_array_index(liste, i))->aktionen;

	for (gint a = 0; a < DB_NUM_AKT; a++) {
		GtkWidget *item = NULL;

		if (!(maske & M(a)))
			continue;
		item = gtk_menu_item_new_with_label(db_akt_name(a));
		gtk_widget_set_tooltip_text(item, db_akt_beschreibung(a));
		g_object_set_data(G_OBJECT(item), "akt", GINT_TO_POINTER(a));
		g_signal_connect(item, "activate", G_CALLBACK(cb_menue_aktion), f);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	}
	gtk_widget_show_all(menu);
	g_signal_connect(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), NULL);
	gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*) ev);

	return;
}

/* Ein Klick auf die Zelle "Aktion" öffnet gleich das Auswahlfeld (sonst erst
 * nach Auswählen der Zeile und einem zweiten Klick). Sind mehrere Zeilen markiert und
 * der Klick trifft eine davon, erscheint stattdessen das Menü für alle markierten. */
static gboolean cb_button_press(GtkWidget *tv, GdkEventButton *ev, gpointer data) {
	Fenster *f = data;
	GtkTreePath *path = NULL;
	GtkTreeViewColumn *col = NULL;
	gboolean behandelt = FALSE;
	GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(tv));
	gboolean ohne_taste = !(ev->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK));

	if (ev->type != GDK_BUTTON_PRESS || (ev->button != 1 && ev->button != 3))
		return FALSE;

	if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(tv), (gint) ev->x,
			(gint) ev->y, &path, &col, NULL, NULL))
		return FALSE;

	if (ohne_taste && gtk_tree_selection_count_selected_rows(sel) > 1
			&& gtk_tree_selection_path_is_selected(sel, path)
			&& (ev->button == 3 || col == f->spalte_aktion)) {
		aktionsmenue_zeigen(f, ev);
		behandelt = TRUE;
	} else if (ev->button == 1 && ohne_taste && col == f->spalte_aktion) {
		GtkTreeIter iter = { 0 };
		gint zeile = ZEILE_KNOTEN;
		DbMangel *m = NULL;
		GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(tv));

		if (gtk_tree_model_get_iter(model, &iter, path)) {
			gtk_tree_model_get(model, &iter, SP_ZEILE, &zeile, SP_PTR, &m, -1);
			if (zeile == ZEILE_MANGEL || (zeile == ZEILE_NACHBAR && m)) {
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
				"Fehler aufgelistet, dazu seine Angaben und Verbindungen (aufklappbar). "
				"In der Spalte »Aktion« steht zunächst überall »Nichts tun«; mit einem "
				"Klick auf die Zelle wählen Sie, was mit dem Eintrag geschehen soll. "
				"»Standard vorschlagen« belegt die Aktionen mit dem üblichen Vorschlag "
				"vor.\n"
				"Ein Klick auf die Überschrift Nr., Art oder Beschreibung sortiert die Liste. "
				"Mit Strg oder Umschalt lassen sich mehrere Zeilen markieren; ein Klick auf "
				"die Zelle Aktion einer markierten Zeile setzt dann die gewählte Aktion für "
				"alle markierten, die sie erlauben. "
				"Ein Doppelklick springt im "
				"Baum zum Eintrag; ist er nicht sichtbar, springen die Zeilen "
				"Übergeordnet, Vorgänger und Nachfolger zu seiner Umgebung.\n"
				"Einträge, die nirgends hinpassen oder nicht angezeigt wurden, "
				"werden in einem neuen Strukturpunkt »Wiedergefundene Knoten« abgelegt. "
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

	f->check_dateien = gtk_check_button_new_with_label(
			"Zusätzlich prüfen, ob die Dateien noch vorhanden sind (langsamer)");
	gtk_widget_set_tooltip_text(f->check_dateien, "Prüft, ob es zu jeder Datei "
			"der Dateiliste die Datei im Projektverzeichnis noch gibt. Es wird nur "
			"der Pfad geprüft; nichts wird geöffnet oder aus der Cloud geladen. "
			"Einträge innerhalb von ZIP-Dateien, E-Mails und PDFs werden nicht "
			"geprüft. Die Prüfung läuft nie beim Start eines Projekts und lässt "
			"sich abbrechen.");
	gtk_box_pack_start(GTK_BOX(box), f->check_dateien, FALSE, FALSE, 0);

	f->store = gtk_tree_store_new(N_SP, G_TYPE_STRING, G_TYPE_STRING,
			G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, GTK_TYPE_TREE_MODEL,
			G_TYPE_POINTER, G_TYPE_INT, G_TYPE_INT64, G_TYPE_INT, G_TYPE_STRING,
			G_TYPE_STRING, G_TYPE_STRING);
	f->treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(f->store));
	g_object_unref(f->store);
	gtk_tree_view_set_enable_tree_lines(GTK_TREE_VIEW(f->treeview), TRUE);
	gtk_tree_selection_set_mode(
			gtk_tree_view_get_selection(GTK_TREE_VIEW(f->treeview)),
			GTK_SELECTION_MULTIPLE);
	gtk_widget_set_has_tooltip(f->treeview, TRUE);
	g_signal_connect(f->treeview, "query-tooltip", G_CALLBACK(cb_tooltip), f);
	{
		GtkTreeViewColumn *c_nr = spalte_hinzu(f->treeview, "Nr.", SP_NR, FALSE);
		GtkTreeViewColumn *c_eintrag = spalte_hinzu(f->treeview, "Eintrag", SP_TEXT, TRUE);
		GtkTreeViewColumn *c_art = spalte_hinzu(f->treeview, "Art", SP_ART, FALSE);
		GtkTreeViewColumn *c_beschr = spalte_hinzu(f->treeview, "Beschreibung",
				SP_DETAIL, TRUE);

		gtk_tree_view_set_expander_column(GTK_TREE_VIEW(f->treeview), c_eintrag);
		spalte_sortierbar(f, c_nr, SORT_NR);
		spalte_sortierbar(f, c_art, SORT_ART);
		spalte_sortierbar(f, c_beschr, SORT_BESCHREIBUNG);
	}
	spalte_hinzu(f->treeview, "Im Baum", SP_ORT, FALSE);
	spalte_aktion_hinzu(f);
	g_signal_connect(f->treeview, "row-activated", G_CALLBACK(cb_row_activated),
			f);
	g_signal_connect(f->treeview, "test-expand-row",
			G_CALLBACK(cb_test_expand_row), f);
	g_signal_connect(f->treeview, "expand-collapse-cursor-row",
			G_CALLBACK(cb_aufklappen_rekursiv), f);
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
	b_standard = gtk_button_new_with_label("Standard vorschlagen");
	b_ausfuehren = gtk_button_new_with_label("Ausführen");
	f->b_abbrechen = gtk_button_new_with_label("Abbrechen");
	gtk_widget_set_no_show_all(f->b_abbrechen, TRUE);
	f->buttons = buttons;
	gtk_container_add(GTK_CONTAINER(buttons), b_legende);
	gtk_container_add(GTK_CONTAINER(buttons), b_pruefen);
	gtk_container_add(GTK_CONTAINER(buttons), b_standard);
	gtk_container_add(GTK_CONTAINER(buttons), b_ausfuehren);
	gtk_box_pack_start(GTK_BOX(box), f->b_abbrechen, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);

	g_signal_connect(f->b_abbrechen, "clicked", G_CALLBACK(cb_abbrechen), f);
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

	if (db_regeln_pruefen(zond->dbase_zond->zond_dbase_work, FALSE, NULL,
			zond->project_dir, &erg,
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
