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

#include "../../misc.h"
#include "../../sond_log_and_error.h"
#include "../../sond_treeview.h"

#include "../zond_init.h"
#include "../zond_dbase.h"
#include "../zond_tree_store.h"

#include "project.h"
#include "db_regeln.h"
#include "db_check_fenster.h"

#define PFAD_MAX_TIEFE 40

#define M(a) (1u << (a))

enum {
	SP_TEXT, //Knoten bzw. Regel
	SP_DETAIL, //Beschreibung der Verletzung
	SP_ORT, //Pfad des Elternknotens
	SP_AUCH, //weitere Regeln, gegen die derselbe Knoten verstößt
	SP_AKTION, //gewählte Aktion
	SP_MODEL, //zulässige Aktionen für das Auswahlfeld
	SP_PTR, //DbMangel* (Mangel-Zeilen)
	SP_ZEILE, //ZEILE_*
	SP_KNOTEN, //Knoten-ID zum Springen, 0 ohne Ziel
	SP_REGEL, //DbRegel (Regel-Zeilen)
	N_SP
};

enum {
	ZEILE_REGEL,
	ZEILE_MANGEL,
	ZEILE_KNOTEN
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
		g_free(text);
		text = NULL;
		if (zond_dbase_get_node(work, link, NULL, NULL, NULL, NULL, NULL, &text,
				NULL, &error))
			g_clear_error(&error);
	}

	if (!text || !*text) {
		g_free(text);

		if (id == 1)
			return g_strdup("Inhalt");
		if (id == 2)
			return g_strdup("Auswertung");

		return g_strdup_printf("(ohne Text, Typ %d)", type);
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

//Texte der Vorfahren bis zur Wurzel, getrennt durch " › "
static gchar* pfad_text(ZondDBase *work, gint64 eltern_id) {
	GPtrArray *teile = g_ptr_array_new_with_free_func(g_free);
	gint64 id = eltern_id;
	GString *s = g_string_new(NULL);
	gint type = 0;
	GError *error = NULL;

	if (eltern_id <= 0) {
		g_ptr_array_unref(teile);
		g_string_free(s, TRUE);

		return g_strdup("");
	}

	if (zond_dbase_get_node(work, (gint) eltern_id, &type, NULL, NULL, NULL, NULL,
			NULL, NULL, &error)) {
		g_clear_error(&error);
		g_ptr_array_unref(teile);
		g_string_free(s, TRUE);

		return g_strdup("(Elternknoten fehlt)");
	}

	{
		GHashTable *gesehen = g_hash_table_new_full(g_int64_hash, g_int64_equal,
				g_free, NULL);
		gboolean kreis = FALSE;

		for (gint tiefe = 0; id > 0 && tiefe < PFAD_MAX_TIEFE; tiefe++) {
			gint parent = 0;
			gint64 *key = NULL;

			if (g_hash_table_contains(gesehen, &id)) { //Elternkette im Kreis
				kreis = TRUE;

				break;
			}
			key = g_new(gint64, 1);
			*key = id;
			g_hash_table_insert(gesehen, key, GINT_TO_POINTER(1));

			g_ptr_array_add(teile, kuerze_text(knoten_text(work, id), 40));
			if (zond_dbase_get_parent(work, (gint) id, &parent, &error)) {
				g_clear_error(&error);

				break;
			}
			id = parent;
		}
		g_hash_table_unref(gesehen);

		//lange Pfade: Anfang und Ende, die Mitte ausgelassen
		for (guint i = teile->len; i > 0; i--) {
			guint pos = teile->len - i; //0 = Wurzel

			if (teile->len > 6 && pos >= 2 && pos < teile->len - 3) {
				if (pos == 2)
					g_string_append(s, " › …");

				continue;
			}
			g_string_append_printf(s, "%s%s", pos ? " › " : "",
					(gchar const*) g_ptr_array_index(teile, i - 1));
		}
		if (kreis)
			g_string_append(s, " › … (Kreis)");
	}
	g_ptr_array_unref(teile);

	return g_string_free(s, FALSE);
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

//Zeigt die gewählten Aktionen in allen Zeilen
static void aktionen_anzeigen(Fenster *f) {
	GtkTreeModel *model = GTK_TREE_MODEL(f->store);
	GtkTreeIter iter_r = { 0 };

	if (!gtk_tree_model_get_iter_first(model, &iter_r))
		return;

	do {
		GtkTreeIter iter_m = { 0 };
		gboolean erste = TRUE;
		DbAkt gemeinsam = DB_AKT_BEHALTEN;
		gboolean gemischt = FALSE;

		if (!gtk_tree_model_iter_children(model, &iter_m, &iter_r))
			continue;

		do {
			DbMangel *m = NULL;

			gtk_tree_model_get(model, &iter_m, SP_PTR, &m, -1);
			if (!m)
				continue;

			gtk_tree_store_set(f->store, &iter_m, SP_AKTION,
					db_akt_name(m->gewaehlt), -1);
			if (erste) {
				gemeinsam = m->gewaehlt;
				erste = FALSE;
			} else if (m->gewaehlt != gemeinsam)
				gemischt = TRUE;
		} while (gtk_tree_model_iter_next(model, &iter_m));

		gtk_tree_store_set(f->store, &iter_r, SP_AKTION,
				gemischt ? "(gemischt)" : db_akt_name(gemeinsam), -1);
	} while (gtk_tree_model_iter_next(model, &iter_r));

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

	if (zeile == ZEILE_MANGEL && m)
		db_mangel_setze_aktion(f->erg.maengel, m, akt);
	else if (zeile == ZEILE_REGEL) { //gilt für alle Mängel darunter
		GtkTreeIter kind = { 0 };

		if (gtk_tree_model_iter_children(model, &kind, &iter))
			do {
				DbMangel *km = NULL;

				gtk_tree_model_get(model, &kind, SP_PTR, &km, -1);
				if (km && (km->aktionen & M(akt)))
					db_mangel_setze_aktion(f->erg.maengel, km, akt);
			} while (gtk_tree_model_iter_next(model, &kind));
	} else
		return;

	aktionen_anzeigen(f);

	return;
}

/* ---- Anzeige ---------------------------------------------------------------- */

static void fenster_fuellen(Fenster *f) {
	ZondDBase *work = f->zond->dbase_zond->zond_dbase_work;
	g_autofree gchar *summe = NULL;
	GHashTable *auch = g_hash_table_new_full(g_int64_hash, g_int64_equal, NULL,
			g_free); //Knoten -> Maske der Regeln, gegen die er verstößt
	gint n_aktiv = 0;

	gtk_tree_store_clear(f->store);

	//je Knoten die Regeln sammeln, gegen die er verstößt
	for (guint i = 0; i < f->erg.maengel->len; i++) {
		DbMangel *m = g_ptr_array_index(f->erg.maengel, i);
		guint *maske = g_hash_table_lookup(auch, &m->knoten_id);

		if (m->knoten_id <= 0)
			continue;
		if (!maske) {
			maske = g_new0(guint, 1);
			g_hash_table_insert(auch, &m->knoten_id, maske);
		}
		*maske |= M(m->regel);
	}

	for (gint regel = 0; regel < DB_NUM_REGELN; regel++) {
		gint n = db_regel_anzahl(&f->erg, regel);
		GtkTreeIter iter_r = { 0 };
		g_autofree gchar *titel = NULL;
		guint maske_regel = 0;

		if (!n)
			continue;

		for (guint i = 0; i < f->erg.maengel->len; i++) {
			DbMangel *m = g_ptr_array_index(f->erg.maengel, i);

			if (m->regel == (DbRegel) regel)
				maske_regel |= m->aktionen;
		}

		titel = g_strdup_printf("%s (%d)", db_regel_titel(regel), n);
		gtk_tree_store_append(f->store, &iter_r, NULL);
		gtk_tree_store_set(f->store, &iter_r, SP_TEXT, titel, SP_DETAIL,
				db_regel_gilt(regel),
				SP_ORT, "", SP_AUCH, "", SP_AKTION, "", SP_MODEL,
				modell_fuer(f, maske_regel), SP_PTR, NULL, SP_ZEILE, ZEILE_REGEL,
				SP_KNOTEN, (gint64) 0, SP_REGEL, regel, -1);

		for (guint i = 0; i < f->erg.maengel->len; i++) {
			DbMangel *m = g_ptr_array_index(f->erg.maengel, i);
			GtkTreeIter iter_m = { 0 };
			g_autofree gchar *kt = NULL;
			g_autofree gchar *ort = NULL;
			g_autofree gchar *auch_text = NULL;
			guint *maske = NULL;

			if (m->regel != (DbRegel) regel)
				continue;

			if (m->aktionen & ~M(DB_AKT_BEHALTEN))
				n_aktiv++;

			kt = knoten_bezeichnung(work, m->knoten_id);
			ort = pfad_text(work, m->eltern_id);
			maske = g_hash_table_lookup(auch, &m->knoten_id);
			if (maske && (*maske & ~M(m->regel))) {
				GString *t = g_string_new(NULL);

				for (gint r = 0; r < DB_NUM_REGELN; r++)
					if ((*maske & M(r)) && r != (gint) m->regel)
						g_string_append_printf(t, "%s%s", t->len ? ", " : "",
								db_regel_kurz(r));
				auch_text = g_string_free(t, FALSE);
			} else
				auch_text = g_strdup("");

			if (m->regel == DB_REGEL_D1 && m->schleife) {
				g_free(kt);
				kt = g_strdup("Einträge im Kreis");
			} else if (m->regel == DB_REGEL_D1) {
				g_autofree gchar *erster = knoten_bezeichnung(work, m->knoten_id);

				g_free(kt);
				kt = g_strdup_printf("Ab hier nicht angezeigt: %s", erster);
			}

			gtk_tree_store_append(f->store, &iter_m, &iter_r);
			gtk_tree_store_set(f->store, &iter_m, SP_TEXT, kt, SP_DETAIL, m->text,
					SP_ORT, ort, SP_AUCH, auch_text, SP_AKTION,
					db_akt_name(m->gewaehlt), SP_MODEL,
					modell_fuer(f, m->aktionen), SP_PTR, m, SP_ZEILE, ZEILE_MANGEL,
					SP_KNOTEN, m->knoten_id, SP_REGEL, regel, -1);

			if (m->regel == DB_REGEL_D1 && !m->schleife)
				for (guint k = 0; k < m->knoten_ids->len; k++) {
					gint64 kid = g_array_index(m->knoten_ids, gint64, k);
					GtkTreeIter iter_k = { 0 };
					g_autofree gchar *kt2 = knoten_bezeichnung(work, kid);

					gtk_tree_store_append(f->store, &iter_k, &iter_m);
					gtk_tree_store_set(f->store, &iter_k, SP_TEXT, kt2, SP_DETAIL, "",
							SP_ORT, "", SP_AUCH, "", SP_AKTION, "", SP_PTR, NULL,
							SP_ZEILE, ZEILE_KNOTEN, SP_KNOTEN, kid, SP_REGEL, regel,
							-1);
				}
		}
	}

	aktionen_anzeigen(f);

	if (f->erg.maengel->len == 0)
		summe = g_strdup("Keine Befunde. Die Projektdatei ist stimmig.");
	else
		summe = g_strdup_printf("%u Befund(e), bei %d davon können Sie eine Aktion "
				"wählen.%s", f->erg.maengel->len, n_aktiv, f->vor_laden ?
						"\nDer Baum wird nach dem Schließen dieses Fensters "
						"aufgebaut." : "");
	gtk_label_set_text(GTK_LABEL(f->label), summe);

	if (f->erg.maengel->len <= 40)
		gtk_tree_view_expand_all(GTK_TREE_VIEW(f->treeview));

	g_hash_table_unref(auch);

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
	static gint const spalten[] = { SP_TEXT, SP_DETAIL, SP_ORT, SP_AUCH,
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

	if (zeile == ZEILE_REGEL) { //Überschrift: ausführliche Erklärung der Regel
		text = g_strdup_printf("%s\n\n%s", db_regel_titel(regel),
				db_regel_erklaerung(regel));
	} else if (idx == 4) { //Aktion: was sie bewirkt
		if (m)
			text = g_strdup_printf("%s\n\n%s", db_akt_name(m->gewaehlt),
					db_akt_beschreibung(m->gewaehlt));
	} else {
		gtk_tree_model_get(model, &iter, spalten[idx], &zelltext, -1);
		if (zelltext && *zelltext) {
			if (idx == 1 && zeile == ZEILE_MANGEL) //Beschreibung + was gilt
				text = g_strdup_printf("%s\n\nWas gilt: %s", zelltext,
						db_regel_gilt(regel));
			else
				text = g_strdup(zelltext);
		}
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
	if (f->vor_laden && zeile != ZEILE_REGEL) {
		display_message(f->fenster, "Der Baum ist noch nicht geladen, ein "
				"Sprung ist erst danach möglich.", NULL);

		return;
	}
	if (zeile == ZEILE_REGEL || id <= 0) {
		if (gtk_tree_view_row_expanded(tv, path))
			gtk_tree_view_collapse_row(tv, path);
		else
			gtk_tree_view_expand_row(tv, path, FALSE);

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

//Nur Regel- und Mangel-Zeilen haben ein Auswahlfeld
static void aktion_zelldaten(GtkTreeViewColumn *col, GtkCellRenderer *renderer,
		GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
	gint zeile = ZEILE_KNOTEN;

	gtk_tree_model_get(model, iter, SP_ZEILE, &zeile, -1);
	g_object_set(renderer, "editable", zeile != ZEILE_KNOTEN, NULL);

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
			if (zeile != ZEILE_KNOTEN) {
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
				"Projektdatei nicht stimmig ist. In der Spalte »Aktion« ist je "
				"Eintrag vorgeschlagen, was damit geschehen soll; mit einem Klick "
				"auf die Zelle können Sie etwas anderes wählen (auf einer "
				"Überschrift gilt die Wahl für alle Einträge darunter). »Nichts "
				"tun« lässt einen Eintrag unverändert.\n"
				"Einträge, die nirgends hinpassen oder nicht angezeigt wurden, "
				"werden in einem neuen Punkt »Wiedergefundene Knoten« abgelegt. "
				"Dort können Sie sie einsortieren oder löschen.\n"
				"Mit »Ausführen« werden die Änderungen vorgenommen; vorher wird "
				"eine Sicherungskopie der Projektdatei angelegt. Ein Doppelklick "
				"auf einen Eintrag zeigt ihn im Baum.");

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
	spalte_hinzu(f->treeview, "Ort im Baum", SP_ORT, TRUE);
	spalte_hinzu(f->treeview, "Weitere Befunde", SP_AUCH, FALSE);
	spalte_aktion_hinzu(f);
	g_signal_connect(f->treeview, "row-activated", G_CALLBACK(cb_row_activated),
			f);
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
