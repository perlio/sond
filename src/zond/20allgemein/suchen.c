/*
 zond (suchen.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2020  pelo america

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

#include "../../sond_log_and_error.h"
#include "../../misc.h"
#include "../zond_dbase.h"
#include "../zond_treeview.h"
#include "../zond_treeviewfm.h"

#include "../zond_dbase.h"
#include "../zond_treeview.h"
#include "../zond_tree_store.h"

#include "../99conv/general.h"
#include "../20allgemein/ziele.h"

#include "project.h"
#include "suchen.h"
#include "verwendung.h"
#include "../10init/app_window.h"

typedef struct _Node {
	gint zond_suchen;
	gint node_id;
} Node;

//Kopiert Knoten node_id (samt Teilbaum) nach BAUM_AUSWERTUNG hinter bzw. unter iter
static gint suchen_kopieren_knoten(Projekt *zond, gint node_id,
		GtkTreeIter *iter, gint *anchor_id, gboolean *child,
		GtkTreeIter *iter_new, GError **error) {
	gint rc = 0;
	gint node_id_new = 0;

	rc = zond_dbase_begin(zond->dbase_zond->zond_dbase_work, error);
	if (rc)
		return -1;

	rc = zond_treeview_walk_tree(ZOND_TREEVIEW(zond->treeview[BAUM_AUSWERTUNG]),
	FALSE, node_id, iter, *child, iter_new, *anchor_id, &node_id_new,
			zond_treeview_copy_node_to_baum_auswertung, error);
	if (rc)
		ERROR_ROLLBACK_Z(zond->dbase_zond->zond_dbase_work)

	rc = zond_dbase_commit(zond->dbase_zond->zond_dbase_work, error);
	if (rc)
		ERROR_ROLLBACK_Z(zond->dbase_zond->zond_dbase_work)

	sond_treeview_expand_row(zond->treeview[BAUM_AUSWERTUNG], iter_new);
	sond_treeview_set_cursor(zond->treeview[BAUM_AUSWERTUNG], iter_new);

	*anchor_id = node_id_new;
	*child = FALSE;

	return 0;
}

//Kontextmenü "In Baum Auswertung kopieren": markierte Ergebniszeilen
static void cb_suchen_nach_auswertung(GtkMenuItem *item, gpointer user_data) {
	Projekt *zond = (Projekt*) user_data;
	GtkTreeView *treeview = g_object_get_data(G_OBJECT(item), "treeview");
	gboolean child = (gboolean) GPOINTER_TO_INT(
			g_object_get_data(G_OBJECT(item), "child"));
	GtkTreeModel *model = NULL;
	GList *selected = NULL;
	gint anchor_id = 0;
	GtkTreeIter iter_anchor = { 0, };
	gboolean in_link = FALSE;
	gboolean kopiert = FALSE;
	gint rc = 0;
	GError *error = NULL;

	selected = gtk_tree_selection_get_selected_rows(
			gtk_tree_view_get_selection(treeview), &model);
	if (!selected) {
		display_message(zond->app_window,
				"Kopieren nicht möglich - keine Punkte ausgewählt", NULL);

		return;
	}

	/* Ziel: markierter Punkt in BAUM_AUSWERTUNG, das den Fokus im App-Fenster
	 * hat (bleibt beim Wechsel ins Ergebnisfenster erhalten). Bei
	 * eingeblendetem BAUM_FS ist BAUM_AUSWERTUNG und damit die Markierung
	 * nicht zu sehen. */
	if (zond_baum_aktuell(zond) != BAUM_AUSWERTUNG
			|| !gtk_tree_selection_count_selected_rows(
					zond->selection[BAUM_AUSWERTUNG])
			|| gtk_toggle_button_get_active(
					GTK_TOGGLE_BUTTON(zond->fs_button))) {
		display_message(zond->app_window,
				"Treffer können nur in BAUM_AUSWERTUNG kopiert werden - "
						"bitte dort einen Zielpunkt markieren", NULL);
		goto end;
	}

	rc = zond_treeview_get_anchor(zond, BAUM_AUSWERTUNG, &child, NULL,
			&iter_anchor, &anchor_id, &in_link, &error);
	if (rc) {
		display_message(zond->app_window,
				"Fehler beim Abfragen des Ankerpunkts in BAUM_AUSWERTUNG -\n\n"
						"Bei Aufruf zond_treeview_get_anchor:\n",
				error->message, NULL);
		g_error_free(error);
		goto end;
	}

	if (in_link) {
		display_message(zond->app_window, "Einfügen in Link nicht zulässig",
				NULL);
		goto end;
	}

	for (GList *list = selected; list; list = list->next) {
		GtkTreeIter iter = { 0 };
		GtkTreeIter iter_new = { 0, };
		gint baum = KEIN_BAUM;
		gint node_id = 0;

		if (!gtk_tree_model_get_iter(model, &iter, list->data))
			continue;
		//Datei-, Section- und Link-Zeilen haben keinen kopierbaren Knoten
		if (!verwendung_zeile_knoten(model, &iter, &baum, &node_id))
			continue;

		rc = suchen_kopieren_knoten(zond, node_id, &iter_anchor, &anchor_id,
				&child, &iter_new, &error);
		if (rc) {
			display_message(zond->app_window,
					"Fehler in Suchen/Kopieren in Auswertung -\n\n"
							"Bei Aufruf suchen_kopieren_knoten:\n",
					error->message, NULL);
			g_error_free(error);
			goto end;
		}

		iter_anchor = iter_new;
		kopiert = TRUE;
	}

	if (!kopiert)
		display_message(zond->app_window, "Keiner der ausgewählten Punkte "
				"kann kopiert werden (Datei-, Section- und Link-Zeilen sind "
				"keine Knoten)", NULL);

	end:
	g_list_free_full(selected, (GDestroyNotify) gtk_tree_path_free);

	return;
}

/* Springt im Baum "baum" (BAUM_INHALT oder BAUM_AUSWERTUNG - node_id ist
 * immer eine "knoten"-Tabellen-ID aus suchen_db(), BAUM_FS kennt solche IDs
 * nicht und ist daher kein gültiges Ziel, s.u.) zum Knoten "node_id".
 * BAUM_AUSWERTUNG teilt sich die Fläche mit BAUM_FS (zond->hpaned, s.
 * app_window.c) und wird über zond->fs_button eingeblendet; BAUM_INHALT ist
 * immer sichtbar. Analoges Umschalten schon vorhanden in
 * zond_treeview_jump_to_iter() (zond_treeview.c) bzw. spiegelbildlich in
 * app_window.c (cb_jump_button_clicked). node_id==0 hat kein Sprungziel.
 *
 * Lookup+Sprung jetzt wie überall sonst im Code über zond_tree_store_get_
 * iter_by_node_id() (O(1)-Hashtable) + sond_treeview_expand_to_row() +
 * sond_treeview_set_cursor() (Nutzer-Hinweis 22.09.2026, im Anschluß an
 * den analogen BAUM_FS-Sprung: "Und der Sprung zum Knoten in den anderen
 * beiden Bäumen? Kannst Du da nicht auch etwas wiederverwenden?") - vorher
 * per zond_treeview_get_path(), dem einzigen verbliebenen Aufrufer des
 * älteren O(n) gtk_tree_model_foreach()-Ansatzes, den Task #39-41 überall
 * sonst schon ersetzt hatten. Das manuelle, temporäre Verbinden/Trennen
 * von "cursor-changed" (um Label/Textview auch ohne bestehenden Fokus zu
 * aktualisieren) entfällt dabei ersatzlos: sond_treeview_set_cursor()
 * ruft am Ende gtk_widget_grab_focus() auf, was über cb_treeview_focus_in()
 * (app_window.c) automatisch genau dasselbe erledigt - Verbinden von
 * "cursor-changed" UND einmaliges erzwungenes Emittieren, s. dortigen
 * Code. Das ist derselbe offizielle Mechanismus, den auch der neue
 * BAUM_FS-Sprung (suchen_springe_zu_baum_fs()) und praktisch jeder andere
 * programmatische Tree-Sprung im Code nutzt. */
void suchen_springe_zu_iter(Projekt *zond, gint baum, GtkTreeIter *iter) {
	if (!iter || (baum != BAUM_INHALT && baum != BAUM_AUSWERTUNG))
		return;

	//BAUM_AUSWERTUNG teilt sich die Fläche mit BAUM_FS (zond->hpaned) - ggf.
	//umschalten; BAUM_INHALT ist immer sichtbar, BAUM_FS an dieser Stelle
	//durch den obigen Guard bereits ausgeschlossen.
	if (baum == BAUM_AUSWERTUNG
			&& gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(zond->fs_button)))
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(zond->fs_button), FALSE);

	/* Manuelles unselect_all bleibt nötig, obwohl cb_treeview_focus_in()
	 * (app_window.c) genau das eigentlich schon erledigt: gtk_widget_grab_
	 * focus() (in sond_treeview_set_cursor() unten) löst focus-in-event bei
	 * GTK3 nur dann synchron aus, wenn app_window auch tatsächlich die
	 * Fenstermanager-Fokus hat - hier hält aber das separate Ergebnisfenster
	 * den echten Fokus, app_window bekommt ihn dadurch nicht automatisch
	 * zurück. cb_treeview_focus_in() feuert also nicht zuverlässig; das
	 * Entfernen dieser Zeilen (Nachtrag #182) wurde vom Nutzer getestet und
	 * per "Kein unselect" widerlegt - wieder eingebaut. BAUM_FS gehört mit
	 * dazu (Nachtrag #183: "Wenn man in BAUM_FS gesprungen ist und springt
	 * in BAUM_INHALT, wird Markierung BAUM_FS nicht gelöscht") - ursprünglich
	 * vergessen, weil BAUM_FS hier selbst nie Sprungziel sein kann (s. Guard
	 * oben), als Sprungherkunft aber sehr wohl in Frage kommt. */
	gtk_tree_selection_unselect_all(zond->selection[BAUM_FS]);
	gtk_tree_selection_unselect_all(zond->selection[BAUM_INHALT]);
	gtk_tree_selection_unselect_all(zond->selection[BAUM_AUSWERTUNG]);

	sond_treeview_expand_to_row(zond->treeview[baum], iter);
	sond_treeview_set_cursor(zond->treeview[baum], iter);

	/* Label/Textview erzwungen aktualisieren (Nachtrag #184) - verläßt sich
	 * nicht mehr (wie vor #182/#183 angenommen) darauf, daß grab_focus() in
	 * sond_treeview_set_cursor() zuverlässig ein echtes focus-in-event
	 * auslöst: das geschieht bei GTK3 nur, wenn app_window bereits die
	 * echte Fenstermanager-Fokus hat, die hier aber das separate
	 * Ergebnisfenster hält. zond_treeview_cursor_changed() (zond_treeview.c)
	 * ist eine normale öffentliche Funktion (kein Callback-Interna) und
	 * bricht bei bereits aktuellem Knoten selbst ab - ein Aufruf schadet
	 * also auch dann nicht, wenn "cursor-changed" zufällig schon verbunden
	 * ist und den Callback ohnehin ausgelöst hat. */
	zond_treeview_cursor_changed(ZOND_TREEVIEW(zond->treeview[baum]), zond);

	return;
}

void suchen_springe_zu_knoten(Projekt *zond, gint baum, gint node_id) {
	GtkTreeIter *iter = NULL;

	if (!node_id)
		return;

	/* baum==BAUM_FS kann aus dieser Suche strukturell nie ein gültiges
	 * Sprungziel sein - node_id ist immer eine "knoten"-Tabellen-ID
	 * (suchen_db()), BAUM_FS hat aber ein eigenes, dateisystembasiertes
	 * Baummodell ohne solche IDs. Tritt das trotzdem auf, ist es ein
	 * Verdrahtungsfehler beim Befüllen der Ergebniszeile (s. #164-Nachtrag
	 * in ToDo.c) - kein normaler Aufruf. */
	if (baum == BAUM_FS) {
		g_warning("suchen_springe_zu_knoten: BAUM_FS als Sprungziel "
				"angefordert (node_id=%d) - kein gültiges Sprungziel aus "
				"der Suche, wird ignoriert.", node_id);
		return;
	}

	iter = zond_tree_store_get_iter_by_node_id(
			ZOND_TREE_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(zond->treeview[baum]))),
			node_id);
	if (!iter) {
		g_warning("suchen_springe_zu_knoten: Knoten (node_id=%d) nicht "
				"(mehr) in Baum %d gefunden.", node_id, baum);
		return;
	}

	suchen_springe_zu_iter(zond, baum, iter);

	gtk_tree_iter_free(iter);

	return;
}

/* Springt in BAUM_FS zur Datei "file_part"+"section" (wie in der knoten-
 * Tabelle gespeichert) -
 * Nutzer-Vorgabe (22.09.2026): "Und jetzt noch implementieren, daß man
 * zum Knoten im BAUM_FS springen kann." Nutzt bewußt dieselbe Funktion wie
 * der bestehende "Sprung zur Herkunft" (zond_treeview_jump_to_origin(),
 * zond_treeview.c, FILE_PART-Fall) statt eigener sond_treeviewfm_file_
 * part_visible()-Verdrahtung - Nutzer-Hinweis (22.09.2026): "Kanns Du da
 * nicht die Implementierung aus jump-to-origin verwenden?" zond_
 * treeviewfm_set_cursor_on_section() berücksichtigt dabei zusätzlich die
 * section (z.B. Seitenbereich einer PDF-Datei), was die vorige,
 * file_part-only Fassung ignorierte. Schaltet BAUM_FS bei Bedarf sichtbar
 * (teilt sich die Fläche mit BAUM_AUSWERTUNG, analog zum Umschalten in
 * suchen_springe_zu_knoten() bzw. in zond_treeview_jump_to_origin()
 * selbst). Manuelles unselect_all auf BAUM_INHALT/BAUM_AUSWERTUNG bleibt
 * nötig: zwar löst zond_treeviewfm_set_cursor_on_section() intern ebenfalls
 * sond_treeview_set_cursor()/grab_focus() aus, aber solange das separate
 * Ergebnisfenster die echte Fenstermanager-Fokus hält, bekommt app_window
 * sie dadurch nicht automatisch zurück und cb_treeview_focus_in() (app_
 * window.c) feuert nicht zuverlässig - anders als beim direkten Vorbild
 * zond_treeview_jump_to_origin(), das immer aus dem bereits fokussierten
 * BAUM_INHALT/BAUM_AUSWERTUNG selbst ausgelöst wird. Testweise entfernt
 * (Nachtrag #182) und vom Nutzer per "Kein unselect" widerlegt - wieder
 * eingebaut. */
void suchen_springe_zu_baum_fs(Projekt *zond, gchar const *file_part,
		gchar const *section) {
	GError *error = NULL;
	gint rc = 0;

	if (!file_part || !*file_part)
		return;

	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(zond->fs_button)))
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(zond->fs_button), TRUE);

	gtk_tree_selection_unselect_all(zond->selection[BAUM_INHALT]);
	gtk_tree_selection_unselect_all(zond->selection[BAUM_AUSWERTUNG]);

	rc = zond_treeviewfm_set_cursor_on_section(
			ZOND_TREEVIEWFM(zond->treeview[BAUM_FS]), file_part, section,
			&error);
	if (rc) {
		display_message(zond->app_window,
				"Fehler beim Springen zu BAUM_FS -\n\n"
						"Bei Aufruf zond_treeviewfm_set_cursor_on_section:\n",
				error->message, NULL);
		g_error_free(error);
	}

	return;
}

#define ERROR_SQL(x) { g_set_error(error, g_quark_from_static_string("SQLITE3"), rc, \
                       "Bei Aufruf " x ":\n%s", \
                       sqlite3_errmsg(zond_dbase_get_dbase( zond->dbase_zond->zond_dbase_work ) ) ); \
                       return -1; }

static gint suchen_db(Projekt *zond, const gchar *text, GArray *arr_treffer,
		GError **error) {
	gint rc = 0;
	Node node = { 0, };
	sqlite3_stmt *stmt = NULL;

	rc =
			sqlite3_prepare_v2(
					zond_dbase_get_dbase(zond->dbase_zond->zond_dbase_work),
					"SELECT 0, ID FROM knoten WHERE LOWER(file_part) LIKE LOWER(?1) "
							"UNION "
							"SELECT 1, ID FROM knoten WHERE LOWER(node_text) LIKE LOWER(?1) "
							"UNION "
							"SELECT 2, ID FROM knoten WHERE LOWER(text) LIKE LOWER(?1) ",
					-1, &stmt, NULL);
	if (rc != SQLITE_OK)
		ERROR_SQL("sqlite3_prepare_v2")

	rc = sqlite3_bind_text(stmt, 1, text, -1, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_finalize(stmt);
		ERROR_SQL("sqlite3_bind_text")
	}

	do {
		rc = sqlite3_step(stmt);
		if ((rc != SQLITE_ROW) && rc != SQLITE_DONE) {
			sqlite3_finalize(stmt);
			ERROR_SQL("sqlite3_step")
		} else if (rc == SQLITE_ROW) {
			node.zond_suchen = sqlite3_column_int(stmt, 0);
			node.node_id = sqlite3_column_int(stmt, 1);
			g_array_append_val(arr_treffer, node);
		}
	} while (rc == SQLITE_ROW);

	sqlite3_finalize(stmt);

	return 0;
}

//Ein Ursprung im Suchergebnis (Strukturpunkt oder Wurzel-FILE_PART)
typedef struct {
	gint strukt_id;
	gint file_part_id;
} SuchenUrsprung;

/* Gruppiert die Rohtreffer nach Ursprung (Reihenfolge des ersten Treffers)
 * und sammelt die hervorzuhebenden Zeilen. Treffer im Dateipfad gelten der
 * Datei (Sections derselben Datei treffen mit), alle anderen der Zeile des
 * Knotens selbst. */
static gint suchen_gruppieren(Projekt *zond, GArray *arr_treffer,
		GArray **arr_ursprung, GArray **arr_markieren, GError **error) {
	GHashTable *ht = g_hash_table_new(g_direct_hash, g_direct_equal);
	GHashTable *ht_markiert = g_hash_table_new(g_direct_hash, g_direct_equal);
	gint rc = 0;

	*arr_ursprung = g_array_new(FALSE, FALSE, sizeof(SuchenUrsprung));
	*arr_markieren = g_array_new(FALSE, FALSE, sizeof(VerwendungTreffer));

	for (guint i = 0; i < arr_treffer->len; i++) {
		Node node = g_array_index(arr_treffer, Node, i);
		SuchenUrsprung u = { 0 };
		VerwendungTreffer t = { 0 };
		gint type = 0;
		gint key_id = 0;

		//Wurzeln, Versionsknoten (node_text = Versionsnummer)
		if (node.node_id <= BAUM_AUSWERTUNG)
			continue;

		rc = zond_dbase_get_type_and_link(zond->dbase_zond->zond_dbase_work,
				node.node_id, &type, NULL, error);
		if (rc)
			break;

		//Anker und Links tragen keinen eigenen Text
		if (type != ZOND_DBASE_TYPE_BAUM_STRUKT
				&& type != ZOND_DBASE_TYPE_FILE_PART
				&& type != ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_COPY)
			continue;

		rc = verwendung_ursprung_ermitteln(zond, node.node_id, &u.strukt_id,
				&u.file_part_id, &key_id, error);
		if (rc)
			break;

		if (node.zond_suchen == 0) {
			t.key_id = u.file_part_id ? u.file_part_id : key_id;
			t.art = VERWENDUNG_TREFFER_PFAD;
		} else {
			t.key_id = key_id;
			t.art = VERWENDUNG_TREFFER_TEXT;
		}
		//Pfadtreffer auf allen Sections einer Datei nur einmal
		if (g_hash_table_add(ht_markiert, GINT_TO_POINTER(t.key_id * 3 + t.art)))
			g_array_append_val(*arr_markieren, t);

		if (!g_hash_table_add(ht, GINT_TO_POINTER(
				u.strukt_id ? u.strukt_id : u.file_part_id)))
			continue; //Ursprung schon vorhanden

		g_array_append_val(*arr_ursprung, u);
	}

	g_hash_table_unref(ht);
	g_hash_table_unref(ht_markiert);

	if (rc) {
		g_array_unref(*arr_ursprung);
		g_array_unref(*arr_markieren);
		*arr_ursprung = NULL;
		*arr_markieren = NULL;

		return -1;
	}

	return 0;
}

static gboolean cb_suchen_button_press(GtkWidget *treeview,
		GdkEventButton *event, gpointer menu) {
	GtkTreePath *path = NULL;
	GtkTreeSelection *sel = NULL;

	if (event->type != GDK_BUTTON_PRESS || event->button != GDK_BUTTON_SECONDARY)
		return FALSE;

	//Rechtsklick auf nicht markierte Zeile: diese markieren, sonst Auswahl lassen
	sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
	if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(treeview),
			(gint) event->x, (gint) event->y, &path, NULL, NULL, NULL)) {
		if (!gtk_tree_selection_path_is_selected(sel, path)) {
			gtk_tree_selection_unselect_all(sel);
			gtk_tree_selection_select_path(sel, path);
		}
		gtk_tree_path_free(path);
	}

	gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*) event);

	return TRUE;
}

static GtkWidget* suchen_kontextmenu(Projekt *zond, GtkWidget *treeview) {
	GtkWidget *menu = gtk_menu_new();
	GtkWidget *item = gtk_menu_item_new_with_label(
			"In Baum Auswertung kopieren");
	GtkWidget *submenu = gtk_menu_new();
	GtkWidget *gleiche_ebene = gtk_menu_item_new_with_label("Gleiche Ebene");
	GtkWidget *unterpunkt = gtk_menu_item_new_with_label("Unterpunkt");

	gtk_menu_shell_append(GTK_MENU_SHELL(submenu), gleiche_ebene);
	gtk_menu_shell_append(GTK_MENU_SHELL(submenu), unterpunkt);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	g_object_set_data(G_OBJECT(gleiche_ebene), "treeview", treeview);
	g_object_set_data(G_OBJECT(unterpunkt), "treeview", treeview);
	g_object_set_data(G_OBJECT(unterpunkt), "child", GINT_TO_POINTER(1));

	g_signal_connect(gleiche_ebene, "activate",
			G_CALLBACK(cb_suchen_nach_auswertung), zond);
	g_signal_connect(unterpunkt, "activate",
			G_CALLBACK(cb_suchen_nach_auswertung), zond);

	gtk_widget_show_all(menu);

	return menu;
}

static void suchen_ergebnisfenster(Projekt *zond, gchar const *titel,
		gchar const *untertitel, GtkTreeStore *store, GArray *arr_markieren) {
	GtkWidget *window = NULL;
	GtkWidget *headerbar = NULL;
	GtkWidget *menu_button = NULL;
	GtkWidget *swindow = NULL;
	GtkWidget *treeview = NULL;
	GtkWidget *menu = NULL;
	GtkWidget *menu_header = NULL;

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(window), 1000, 450);
	gtk_window_set_transient_for(GTK_WINDOW(window),
			GTK_WINDOW(zond->app_window));

	headerbar = gtk_header_bar_new();
	gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(headerbar),
			":minimize,close");
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
	gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), titel);
	gtk_header_bar_set_subtitle(GTK_HEADER_BAR(headerbar), untertitel);
	gtk_window_set_titlebar(GTK_WINDOW(window), headerbar);

	treeview = verwendung_treeview_new(zond, store);
	gtk_tree_selection_set_mode(
			gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview)),
			GTK_SELECTION_MULTIPLE);

	//Kontextmenü (Rechtsklick) und dasselbe Menü am Button der Titelleiste
	menu = suchen_kontextmenu(zond, treeview);
	gtk_menu_attach_to_widget(GTK_MENU(menu), treeview, NULL);
	g_signal_connect(treeview, "button-press-event",
			G_CALLBACK(cb_suchen_button_press), menu);

	menu_header = suchen_kontextmenu(zond, treeview);
	menu_button = gtk_menu_button_new();
	gtk_menu_button_set_popup(GTK_MENU_BUTTON(menu_button), menu_header);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(headerbar), menu_button);

	swindow = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(swindow), treeview);
	gtk_container_add(GTK_CONTAINER(window), swindow);

	verwendung_hervorheben(GTK_TREE_VIEW(treeview),
			(VerwendungTreffer const*) arr_markieren->data, arr_markieren->len,
			FALSE);

	gtk_widget_show_all(window);

	return;
}

/* Baumsuche (Popup-Suchfeld): Treffer in file_part, node_text und text der
 * zond-Bäume. Ergebnis: je Ursprung der vollständige Baum wie in
 * "Herkunft und Verwendung", aufgeklappt bis zu den Treffern (s. ToDo.c
 * #188). */
gint suchen_treeviews(Projekt *zond, const gchar *text, GError **error) {
	gint rc = 0;
	GArray *arr_treffer = NULL;
	GArray *arr_ursprung = NULL;
	GArray *arr_markieren = NULL;
	GtkTreeStore *store = NULL;

	arr_treffer = g_array_new( FALSE, FALSE, sizeof(Node));

	rc = suchen_db(zond, text, arr_treffer, error);
	if (!rc)
		rc = suchen_gruppieren(zond, arr_treffer, &arr_ursprung,
				&arr_markieren, error);
	g_array_unref(arr_treffer);
	if (rc)
		return -1;

	if (!arr_ursprung->len) {
		display_message(zond->app_window, "Keine Treffer", NULL);
		goto end;
	}

	store = verwendung_store_new();
	for (guint i = 0; i < arr_ursprung->len; i++) {
		SuchenUrsprung u = g_array_index(arr_ursprung, SuchenUrsprung, i);

		rc = verwendung_store_add_ursprung(zond, store, u.strukt_id,
				u.file_part_id, error);
		if (rc)
			goto end;
	}
	verwendung_store_zaehlen(store);

	{
		//Platzhalter % aus dem LIKE-Muster nicht mit anzeigen
		gchar *begriff = g_strdup(text);
		gchar *titel = NULL;
		gchar *untertitel = NULL;

		g_strdelimit(begriff, "%", ' ');
		g_strstrip(begriff);
		titel = g_strdup_printf("Suche nach: '%s'", begriff);
		untertitel = g_strdup_printf("%u Fundstelle(n) in %u Datei(en)/"
				"Strukturpunkt(en)", arr_markieren->len, arr_ursprung->len);

		suchen_ergebnisfenster(zond, titel, untertitel, store, arr_markieren);

		g_free(begriff);
		g_free(titel);
		g_free(untertitel);
	}

	end:
	if (store)
		g_object_unref(store); //gehört jetzt der Baumansicht
	g_array_unref(arr_ursprung);
	g_array_unref(arr_markieren);

	return rc ? -1 : 0;
}
