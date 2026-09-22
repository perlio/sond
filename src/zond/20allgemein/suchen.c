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

typedef struct _Node {
	gint zond_suchen;
	gint node_id;
} Node;

/* Eine einzelne Copy (BAUM_AUSWERTUNG_COPY) innerhalb einer aggregierten
 * ResultRow (s.u.). */
typedef struct _ResultCopy {
	gint node_id;
	gchar *node_text;
	gchar *text;
} ResultCopy;

static void result_copy_clear(gpointer data) {
	ResultCopy *copy = data;

	g_free(copy->node_text);
	g_free(copy->text);

	return;
}

/* Ein aggregiertes Suchergebnis für EINEN file_part-Knoten (Section) - s.
 * ToDo.c (#164). Wird für jeden Treffer, der sich auf einen file_part-
 * Knoten zurückführen lässt (der Knoten selbst, dessen Anbindung in
 * BAUM_INHALT, oder eine Copy davon in BAUM_AUSWERTUNG), genau einmal
 * gebaut und zeigt dann IMMER vollständig die Anbindung (falls
 * vorhanden) und alle ihre Copies. */
typedef struct _ResultRow {
	gint file_part_node_id;
	gchar *file_part;
	gchar *section;
	gboolean has_anbindung;
	gint anbindung_node_id;
	gchar *anbindung_node_text;
	gchar *anbindung_text;
	GArray *arr_copies; //ResultCopy
} ResultRow;

static void result_row_free(gpointer data) {
	ResultRow *row = data;

	if (!row)
		return;

	g_free(row->file_part);
	g_free(row->section);
	g_free(row->anbindung_node_text);
	g_free(row->anbindung_text);
	if (row->arr_copies)
		g_array_unref(row->arr_copies);
	g_free(row);

	return;
}

/* Ein Eintrag in der Ergebnisliste: entweder eine aggregierte ResultRow
 * (Treffer mit Datei-Bezug) oder ein einfacher Treffer wie bisher
 * (Knoten ohne Datei-Bezug, z.B. ein reiner Strukturpunkt). */
typedef struct _SuchenItem {
	gboolean is_row;
	union {
		Node simple;
		ResultRow *row;
	} u;
} SuchenItem;

static void suchen_item_free(gpointer data) {
	SuchenItem *item = data;

	if (item->is_row)
		result_row_free(item->u.row);
	g_free(item);

	return;
}

static gint suchen_kopieren_listenpunkt(Projekt *zond, GList *list,
		GtkTreeIter *iter, gint *anchor_id, gboolean *child,
		GtkTreeIter *iter_new, GError **error) {
	gint rc = 0;
	gint node_id = 0;
	gint node_id_new = 0;

	node_id = GPOINTER_TO_INT(
			g_object_get_data( G_OBJECT(list->data), "node-id" ));

	if (!node_id) { //Kopfzeile ohne Anbindung (s. suchen_fuellen_row_composite()) - überspringen
		*iter_new = *iter;

		return 0;
	}

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

static void cb_suchen_nach_auswertung(GtkMenuItem *item, gpointer user_data) {
	GList *selected = NULL;
	GList *list = NULL;
	gint anchor_id = 0;
	GtkTreeIter iter_anchor = { 0, };
	gboolean in_link = FALSE;
	gint rc = 0;
	GError *error = NULL;

	Projekt *zond = (Projekt*) user_data;

	GtkWidget *list_box = g_object_get_data(G_OBJECT(item), "listbox");
	gboolean child = (gboolean) GPOINTER_TO_INT(
			g_object_get_data( G_OBJECT(item), "child" ));

	selected = gtk_list_box_get_selected_rows(GTK_LIST_BOX(list_box));

	if (!selected) {
		display_message(zond->app_window,
				"Kopieren nicht möglich - keine Punkte "
						"ausgewählt", NULL);

		return;
	}

	//aktuellen cursor im BAUM_AUSWERTUNG: node_id und iter abfragen
	if (zond->baum_active != BAUM_AUSWERTUNG) {
		display_message(zond->app_window,
				"Treffer können nur in BAUM_AUSWERTUNG kopiert werden", NULL);

		return;
	}

	rc = zond_treeview_get_anchor(zond, &child, NULL, &iter_anchor, &anchor_id, &in_link, &error);
	if (rc) {
		display_message(zond->app_window,
				"Fehler beim Abfragen des Ankerpunkts in BAUM_AUSWERTUNG -\n\n"
						"Bei Aufruf zond_treeview_get_anchor:\n",
				error->message, NULL);
		g_error_free(error);

		return;
	}

	if (in_link) {
		display_message(zond->app_window,
				"Einfügen in Link nicht zulässig", NULL);

		return;
	}


	list = selected;
	do {
		gint rc = 0;
		GError *error = NULL;
		GtkTreeIter iter_new = { 0, };

		rc = suchen_kopieren_listenpunkt(zond, list, &iter_anchor,
				&anchor_id, &child, &iter_new, &error);
		if (rc) {
			display_message(zond->app_window,
					"Fehler in Suchen/Kopieren in Auswertung -\n\n"
							"Bei Aufruf suchen_kopieren_listenpunkt:\n",
					error->message, NULL);
			g_error_free(error);

			return;
		}

		iter_anchor = iter_new;
	} while ((list = list->next));

	g_list_free(selected);

	return;
}

/* Springt im Baum "baum" (BAUM_INHALT oder BAUM_AUSWERTUNG - node_id ist
 * immer eine "knoten"-Tabellen-ID aus suchen_db(), BAUM_FS kennt solche IDs
 * nicht und ist daher kein gültiges Ziel, s.u.) zum Knoten "node_id".
 * BAUM_AUSWERTUNG teilt sich die Fläche mit BAUM_FS (zond->hpaned, s.
 * app_window.c) und wird über zond->fs_button eingeblendet; BAUM_INHALT ist
 * immer sichtbar. Analoges Umschalten schon vorhanden in
 * zond_treeview_jump_to_iter() (zond_treeview.c) bzw. spiegelbildlich in
 * app_window.c (cb_jump_button_clicked). node_id==0 (Zeile ohne
 * Anbindung, s. suchen_fuellen_row_composite()) hat kein Sprungziel.
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
static void suchen_springe_zu_knoten(Projekt *zond, Baum baum, gint node_id) {
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

	iter = zond_tree_store_get_iter_by_node_id(
			ZOND_TREE_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(zond->treeview[baum]))),
			node_id);
	if (!iter) {
		g_warning("suchen_springe_zu_knoten: Knoten (node_id=%d) nicht "
				"(mehr) in Baum %d gefunden.", node_id, baum);
		return;
	}

	sond_treeview_expand_to_row(zond->treeview[baum], iter);
	sond_treeview_set_cursor(zond->treeview[baum], iter);

	gtk_tree_iter_free(iter);

	return;
}

static void cb_lb_row_activated(GtkWidget *listbox, GtkWidget *row,
		gpointer user_data) {
	Projekt *zond = (Projekt*) user_data;

	Baum baum = (Baum) GPOINTER_TO_INT(
			g_object_get_data( G_OBJECT(row), "baum" ));
	gint node_id = GPOINTER_TO_INT(
			g_object_get_data( G_OBJECT(row), "node-id" ));

	suchen_springe_zu_knoten(zond, baum, node_id);

	return;
}

/* Springt in BAUM_FS zur Datei "file_part"+"section" (wie in der knoten-
 * Tabelle gespeichert, s. Kommentar in suchen_resolve_file_part_node()) -
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
static void suchen_springe_zu_baum_fs(Projekt *zond, gchar const *file_part,
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

/* Klick auf die Spalte "Dateiname" (s. suchen_fuellen_row_composite()) -
 * file_part/section stehen als eigene Kopien auf dem Button
 * (g_object_set_data_full mit g_free), da die ResultRow (und damit
 * row->file_part/row->section) bereits kurz nach dem Befüllen des
 * Ergebnisfensters wieder freigegeben wird (s.
 * suchen_anzeigen_ergebnisse()). */
static void cb_suchen_dateiname_geklickt(GtkButton *button,
		gpointer user_data) {
	Projekt *zond = (Projekt*) user_data;

	gchar const *file_part = g_object_get_data(G_OBJECT(button), "file-part");
	gchar const *section = g_object_get_data(G_OBJECT(button), "section");

	suchen_springe_zu_baum_fs(zond, file_part, section);

	return;
}

/* Klick auf eine Knoten-"Box" (s. suchen_box_knoten()) - baum/node-id
 * stehen auf dem Button selbst, nicht auf der Listbox-Zeile, da eine
 * Zeile mehrere solcher Boxen enthalten kann (z.B. mehrere Copies in der
 * Spalte "Auswertung"), jede mit ihrem eigenen Sprungziel. */
static void cb_suchen_box_geklickt(GtkButton *button, gpointer user_data) {
	Projekt *zond = (Projekt*) user_data;

	Baum baum = (Baum) GPOINTER_TO_INT(
			g_object_get_data( G_OBJECT(button), "baum" ));
	gint node_id = GPOINTER_TO_INT(
			g_object_get_data( G_OBJECT(button), "node-id" ));

	suchen_springe_zu_knoten(zond, baum, node_id);

	return;
}

/* Baut eine klickbare "Box" für einen einzelnen Knoten: node_text oben,
 * darunter der Kommentartext "text" (falls vorhanden), beides linksbündig
 * - Klick springt zu genau diesem Knoten, unabhängig von anderen Boxen
 * derselben Zeile (Nutzer-Vorgabe: "Klick auf Spalte führt zu Sprung zu
 * Knoten"). node_text/text werden nur gelesen (GtkLabel kopiert intern),
 * der Aufrufer bleibt Eigentümer und muss sie selbst freigeben. */
static GtkWidget* suchen_box_knoten(Projekt *zond, Baum baum, gint node_id,
		const gchar *node_text, const gchar *text) {
	GtkWidget *button = NULL;
	GtkWidget *vbox = NULL;
	GtkWidget *label_node_text = NULL;

	button = gtk_button_new();
	gtk_widget_set_valign(button, GTK_ALIGN_START);

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	label_node_text = gtk_label_new(node_text ? node_text : "");
	gtk_widget_set_halign(label_node_text, GTK_ALIGN_START);
	gtk_box_pack_start(GTK_BOX(vbox), label_node_text, FALSE, FALSE, 0);

	if (text && *text) {
		GtkWidget *label_text = NULL;

		label_text = gtk_label_new(text);
		gtk_widget_set_halign(label_text, GTK_ALIGN_START);
		gtk_label_set_line_wrap(GTK_LABEL(label_text), TRUE);
		gtk_label_set_max_width_chars(GTK_LABEL(label_text), 40);
		gtk_box_pack_start(GTK_BOX(vbox), label_text, FALSE, FALSE, 0);
	}

	gtk_container_add(GTK_CONTAINER(button), vbox);

	g_object_set_data(G_OBJECT(button), "baum", GINT_TO_POINTER(baum));
	g_object_set_data(G_OBJECT(button), "node-id", GINT_TO_POINTER(node_id));
	g_signal_connect(button, "clicked", G_CALLBACK(cb_suchen_box_geklickt),
			zond);

	return button;
}

/* Leere Platzhalter-Zelle für eine Spalte, die für diese Zeile nicht
 * zutrifft (z.B. Spalte "Auswertung" bei einem reinen BAUM_INHALT-
 * Strukturtreffer, s. suchen_fuellen_row_simple()) - Aufrufer fügt sie der
 * jeweiligen sizegroup hinzu, damit die Spaltenbreite trotzdem mit den
 * anderen Zeilen übereinstimmt. */
static GtkWidget* suchen_zelle_leer(void) {
	GtkWidget *label = NULL;

	label = gtk_label_new("");
	gtk_widget_set_halign(label, GTK_ALIGN_START);
	gtk_widget_set_valign(label, GTK_ALIGN_START);

	return label;
}

/* EINE Tabellenzeile (Listbox-Zeile) für eine aggregierte ResultRow
 * (#164): Spalte "Dateiname" (file_part+section), Spalte "Bestands-
 * verzeichnis" (Anbindung in BAUM_INHALT, falls vorhanden, als klickbare
 * Box) und Spalte "Auswertung" (alle Copies der Anbindung, als eigene,
 * untereinander angeordnete Boxen) - Nutzer-Vorgabe (22.09.2026): "Ich
 * stelle mir eine Tabelle vor: Links filepart ..., daneben node_text
 * BAUM_INHALT ..., Spalte daneben ... die Copies ... in eigenen Boxen ...
 * untereinander." sg_filepart/sg_inhalt/sg_auswertung (GtkSizeGroup, s.
 * suchen_erzeugen_ergebnisfenster()) halten die drei Spalten über alle
 * Zeilen und die Kopfzeile hinweg gleich breit, obwohl jede Zeile eine
 * eigenständige GtkBox ist. Die Zeile selbst trägt zusätzlich "baum"/
 * "node-id" der Anbindung (0/0 ohne Anbindung), damit "In Baum Auswertung
 * kopieren" (suchen_kopieren_listenpunkt(), arbeitet auf der
 * Zeilenauswahl) ein sinnvolles Ziel hat - dafür wird bewußt die echte
 * ID des BAUM_INHALT_FILE-Anker-Knotens (row->anbindung_node_id)
 * gebraucht, da zond_treeview_copy_node_to_baum_auswertung() anhand
 * dieses Typs entscheidet, wie es den Unterbaum kopiert.
 *
 * Für den Sprung per Klick auf die Box selbst (suchen_box_knoten(),
 * BAUM_INHALT) darf dagegen NICHT diese Anker-ID verwendet werden: beim
 * Anbinden wird der sichtbare Tree-Store-Eintrag nicht unter der ID des
 * Anker-Knotens registriert, sondern unter der ID des verlinkten,
 * dauerhaften file_part-Knotens (s. Kommentar "Angezeigt wird ID_file_part
 * (nicht new_node_id!)" in zond_treeview.c, zond_treeview_leaf_anbinden())
 * - zond_treeview_get_path() sucht per node_id-Spalte im Tree-Store und
 * fände mit der Anker-ID nie etwas. Bug (22.09.2026, Nutzer-Fund): "Klick
 * auf Spalte Bestandsverzeichnis springt nur dann zum Knoten, wenn keine
 * Datei angebunden" - bei Strukturpunkten (suchen_fuellen_row_simple())
 * tritt das nicht auf, weil dort node_id mit der echten, unaliasierten
 * Knoten-ID identisch ist. Doppelklick auf die Zeile selbst
 * (cb_lb_row_activated()) bleibt von diesem Fix unberührt und springt bei
 * Anbindungen weiterhin nicht - dafür müßte "node-id" der Zeile auf
 * dieselbe Weise gedoppelt werden wie hier für die Box; nicht Teil dieses
 * Fixes (Klick auf die Box ist laut Nutzer-Vorgabe der eigentliche
 * Sprung-Weg). */
static void suchen_fuellen_row_composite(Projekt *zond, GtkWidget *list_box,
		GtkSizeGroup *sg_filepart, GtkSizeGroup *sg_inhalt,
		GtkSizeGroup *sg_auswertung, ResultRow *row) {
	GtkWidget *hbox = NULL;
	GtkWidget *label_kopf = NULL;
	GtkWidget *zelle_inhalt = NULL;
	GtkWidget *zelle_auswertung = NULL;
	GtkWidget *list_box_row = NULL;
	gchar *text_kopf = NULL;
	Baum baum_row = 0;
	gint node_id_row = 0;

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

	if (row->section && *row->section)
		text_kopf = g_strdup_printf("%s (%s)",
				row->file_part ? row->file_part : "", row->section);
	else
		text_kopf = g_strdup(row->file_part ? row->file_part : "");

	//Spalte "Dateiname" jetzt klickbar - springt zur Datei in BAUM_FS (s.
	//suchen_springe_zu_baum_fs()). Bewußt ein GtkButton wie bei den
	//übrigen Spalten (suchen_box_knoten()), nicht Klick auf die ganze
	//Zeile - Nutzer-Vorgabe (22.09.2026): "Entweder beim Click auf die
	//Zeile oder auch Button einbauen".
	label_kopf = gtk_button_new();
	gtk_widget_set_valign(label_kopf, GTK_ALIGN_START);
	gtk_container_add(GTK_CONTAINER(label_kopf), gtk_label_new(text_kopf));
	gtk_widget_set_halign(gtk_bin_get_child(GTK_BIN(label_kopf)),
			GTK_ALIGN_START);
	g_object_set_data_full(G_OBJECT(label_kopf), "file-part",
			g_strdup(row->file_part ? row->file_part : ""), g_free);
	if (row->section && *row->section)
		g_object_set_data_full(G_OBJECT(label_kopf), "section",
				g_strdup(row->section), g_free);
	g_signal_connect(label_kopf, "clicked",
			G_CALLBACK(cb_suchen_dateiname_geklickt), zond);
	g_free(text_kopf);
	gtk_size_group_add_widget(sg_filepart, label_kopf);
	gtk_box_pack_start(GTK_BOX(hbox), label_kopf, FALSE, FALSE, 0);

	if (row->has_anbindung) {
		//Sprungziel bewußt file_part_node_id, nicht anbindung_node_id - s.
		//Funktionskommentar oben.
		zelle_inhalt = suchen_box_knoten(zond, BAUM_INHALT,
				row->file_part_node_id, row->anbindung_node_text,
				row->anbindung_text);

		baum_row = BAUM_INHALT;
		node_id_row = row->anbindung_node_id;
	} else
		zelle_inhalt = suchen_zelle_leer();

	gtk_size_group_add_widget(sg_inhalt, zelle_inhalt);
	gtk_box_pack_start(GTK_BOX(hbox), zelle_inhalt, FALSE, FALSE, 0);

	//Spalte "Auswertung": alle Copies als eigene Boxen untereinander
	zelle_auswertung = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_widget_set_valign(zelle_auswertung, GTK_ALIGN_START);

	for (guint i = 0; row->arr_copies && i < row->arr_copies->len; i++) {
		ResultCopy *copy = &g_array_index(row->arr_copies, ResultCopy, i);

		gtk_box_pack_start(GTK_BOX(zelle_auswertung),
				suchen_box_knoten(zond, BAUM_AUSWERTUNG, copy->node_id,
						copy->node_text, copy->text),
				FALSE, FALSE, 0);
	}

	gtk_size_group_add_widget(sg_auswertung, zelle_auswertung);
	gtk_box_pack_start(GTK_BOX(hbox), zelle_auswertung, FALSE, FALSE, 0);

	gtk_list_box_insert(GTK_LIST_BOX(list_box), hbox, -1);
	list_box_row = gtk_widget_get_parent(hbox);

	g_object_set_data(G_OBJECT(list_box_row), "baum",
			GINT_TO_POINTER(baum_row));
	g_object_set_data(G_OBJECT(list_box_row), "node-id",
			GINT_TO_POINTER(node_id_row));

	return;
}

/* EINE Tabellenzeile (s. suchen_fuellen_row_composite()) für einen
 * Treffer ohne Datei-Bezug (reiner Strukturpunkt). Nutzer-Vorgabe
 * (22.09.2026): "Bei Strukturpunkten bleiben die anderen Spalten
 * natürlich leer, Strukturpunkte in beiden Bäumen haben ja keinerlei
 * Beziehungen untereinander" - Spalte "Dateiname" bleibt leer, und je
 * nachdem, in welchem Baum der Knoten liegt, füllt sich genau eine der
 * beiden anderen Spalten mit einer einzelnen Box (node_text+text).
 *
 * Bug (22.09.2026, Nutzer-Fund "Anklicken BAUM_FS und BAUM_INHALT
 * funktioniert nicht", weiterhin gültig): der zond_suchen==2-Fall
 * (Treffer im Kommentartext "text") muss "baum" genauso über
 * zond_dbase_get_tree_root() ermitteln wie zond_suchen==1 (Treffer im
 * node_text) - sonst bliebe "baum" beim Default 0 = BAUM_FS stehen, wohin
 * ein Sprung mit einer "knoten"-Tabellen-ID nie funktionieren kann. Da
 * beide Fälle jetzt ohnehin dieselbe Box zeigen (node_text UND text,
 * unabhängig davon, welches Feld den Treffer ausgelöst hat), werden sie
 * hier direkt gleich behandelt statt weiterhin nach zond_suchen==1/2 zu
 * unterscheiden. */
static gint suchen_fuellen_row_simple(Projekt *zond, GtkWidget *list_box,
		GtkSizeGroup *sg_filepart, GtkSizeGroup *sg_inhalt,
		GtkSizeGroup *sg_auswertung, gint zond_suchen, gint node_id,
		GError **error) {
	GtkWidget *hbox = NULL;
	GtkWidget *zelle_filepart = NULL;
	GtkWidget *zelle_inhalt = NULL;
	GtkWidget *zelle_auswertung = NULL;
	GtkWidget *list_box_row = NULL;
	gint baum = 0;
	gint node_id_jump = node_id;

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

	zelle_filepart = suchen_zelle_leer();
	gtk_size_group_add_widget(sg_filepart, zelle_filepart);
	gtk_box_pack_start(GTK_BOX(hbox), zelle_filepart, FALSE, FALSE, 0);

	if (zond_suchen == 0) {
		/* FILE_PART-Knoten sind reine Datensätze (referenziert über "link"
		 * von BAUM_INHALT_FILE/BAUM_AUSWERTUNG_COPY, s.
		 * suchen_resolve_file_part_node()) und haben selbst keine sichtbare
		 * Zeile im Baum - kein Sprungziel. Nach der #164-Aggregation ohnehin
		 * unerreichbar (ein FILE_PART-Treffer wird dort immer schon zur
		 * ResultRow), hier trotzdem defensiv als leere Zeile statt
		 * fälschlich BAUM_FS. */
		zelle_inhalt = suchen_zelle_leer();
		zelle_auswertung = suchen_zelle_leer();
		node_id_jump = 0;
	} else {
		gint rc = 0;
		gchar *node_text = NULL;
		gchar *text = NULL;

		rc = zond_dbase_get_node(zond->dbase_zond->zond_dbase_work, node_id,
				NULL, NULL, NULL, NULL, NULL, &node_text, &text, error);
		if (rc)
			return -1;

		rc = zond_dbase_get_tree_root(zond->dbase_zond->zond_dbase_work,
				node_id, &baum, error);
		if (rc) {
			g_free(node_text);
			g_free(text);
			return -1;
		}

		if (baum == BAUM_INHALT) {
			zelle_inhalt = suchen_box_knoten(zond, BAUM_INHALT, node_id,
					node_text, text);
			zelle_auswertung = suchen_zelle_leer();
		} else {
			zelle_inhalt = suchen_zelle_leer();
			zelle_auswertung = suchen_box_knoten(zond, BAUM_AUSWERTUNG,
					node_id, node_text, text);
		}

		g_free(node_text);
		g_free(text);
	}

	gtk_size_group_add_widget(sg_inhalt, zelle_inhalt);
	gtk_box_pack_start(GTK_BOX(hbox), zelle_inhalt, FALSE, FALSE, 0);

	gtk_size_group_add_widget(sg_auswertung, zelle_auswertung);
	gtk_box_pack_start(GTK_BOX(hbox), zelle_auswertung, FALSE, FALSE, 0);

	gtk_list_box_insert(GTK_LIST_BOX(list_box), hbox, -1);
	list_box_row = gtk_widget_get_parent(hbox);

	g_object_set_data(G_OBJECT(list_box_row), "baum", GINT_TO_POINTER(baum));
	g_object_set_data(G_OBJECT(list_box_row), "node-id",
			GINT_TO_POINTER(node_id_jump));

	return 0;
}

static gint suchen_fuellen_ergebnisfenster(Projekt *zond,
		GtkWidget *ergebnisfenster, GPtrArray *arr_items, GError **error) {
	gint rc = 0;
	GtkWidget *list_box = NULL;
	GtkSizeGroup *sg_filepart = NULL;
	GtkSizeGroup *sg_inhalt = NULL;
	GtkSizeGroup *sg_auswertung = NULL;

	list_box = g_object_get_data(G_OBJECT(ergebnisfenster), "listbox");
	sg_filepart = g_object_get_data(G_OBJECT(ergebnisfenster), "sg-filepart");
	sg_inhalt = g_object_get_data(G_OBJECT(ergebnisfenster), "sg-inhalt");
	sg_auswertung = g_object_get_data(G_OBJECT(ergebnisfenster),
			"sg-auswertung");

	for (guint i = 0; i < arr_items->len; i++) {
		SuchenItem *item = g_ptr_array_index(arr_items, i);

		if (item->is_row)
			suchen_fuellen_row_composite(zond, list_box, sg_filepart,
					sg_inhalt, sg_auswertung, item->u.row);
		else {
			rc = suchen_fuellen_row_simple(zond, list_box, sg_filepart,
					sg_inhalt, sg_auswertung, item->u.simple.zond_suchen,
					item->u.simple.node_id, error);
			if (rc)
				return -1;
		}
	}

	gtk_widget_show_all(list_box);

	return 0;
}

static GtkWidget*
suchen_erzeugen_ergebnisfenster(Projekt *zond, const gchar *titel) {
	GtkWidget *window = NULL;
	GtkWidget *listbox = NULL;
	GtkWidget *headerbar = NULL;
	GtkWidget *header_box = NULL;
	GtkSizeGroup *sg_filepart = NULL;
	GtkSizeGroup *sg_inhalt = NULL;
	GtkSizeGroup *sg_auswertung = NULL;
	GtkWidget *label_filepart = NULL;
	GtkWidget *label_inhalt = NULL;
	GtkWidget *label_auswertung = NULL;

	//Fenster erzeugen
	window = result_listbox_new(GTK_WINDOW(zond->app_window), titel);

	/* Tabellen-Spaltenüberschriften (Nutzer-Vorgabe: "Ich stelle mir eine
	 * Tabelle vor", s. suchen_fuellen_row_composite()) - header_box (aus
	 * result_listbox_new(), bleibt beim Scrollen der Liste fest stehen) und
	 * je eine GtkSizeGroup pro Spalte, die Kopf- und Datenzellen über alle
	 * (voneinander unabhängigen) Zeilen hinweg gleich breit hält. Die
	 * sizegroups werden über g_object_set_data_full() am Fenster verankert
	 * (Lebensdauer/Freigabe an das Fenster gekoppelt) und in
	 * suchen_fuellen_ergebnisfenster() wieder abgefragt. */
	header_box = (GtkWidget*) g_object_get_data(G_OBJECT(window),
			"header-box");

	sg_filepart = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
	sg_inhalt = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
	sg_auswertung = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);

	label_filepart = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label_filepart), "<b>Dateiname</b>");
	gtk_widget_set_halign(label_filepart, GTK_ALIGN_START);
	gtk_size_group_add_widget(sg_filepart, label_filepart);

	label_inhalt = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label_inhalt), "<b>Bestandsverzeichnis</b>");
	gtk_widget_set_halign(label_inhalt, GTK_ALIGN_START);
	gtk_size_group_add_widget(sg_inhalt, label_inhalt);

	label_auswertung = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label_auswertung), "<b>Auswertung</b>");
	gtk_widget_set_halign(label_auswertung, GTK_ALIGN_START);
	gtk_size_group_add_widget(sg_auswertung, label_auswertung);

	gtk_box_pack_start(GTK_BOX(header_box), label_filepart, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(header_box), label_inhalt, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(header_box), label_auswertung, FALSE, FALSE,
			0);

	g_object_set_data_full(G_OBJECT(window), "sg-filepart", sg_filepart,
			g_object_unref);
	g_object_set_data_full(G_OBJECT(window), "sg-inhalt", sg_inhalt,
			g_object_unref);
	g_object_set_data_full(G_OBJECT(window), "sg-auswertung", sg_auswertung,
			g_object_unref);

	//Menu Button
	GtkWidget *suchen_menu_button = gtk_menu_button_new();

	//Menu erzeugen
	GtkWidget *suchen_menu = gtk_menu_new();

	//Items erzeugen
	GtkWidget *suchen_nach_auswertung = gtk_menu_item_new_with_label(
			"In Baum Auswertung kopieren");

	//Füllen
	gtk_menu_shell_append(GTK_MENU_SHELL(suchen_menu), suchen_nach_auswertung);

	//Untermenu
	GtkWidget *suchen_nach_auswertung_ebene = gtk_menu_new();

	GtkWidget *gleiche_ebene = gtk_menu_item_new_with_label("Gleiche Ebene");
	GtkWidget *unterpunkt = gtk_menu_item_new_with_label("Unterpunkt");

	//Füllen
	gtk_menu_shell_append(GTK_MENU_SHELL(suchen_nach_auswertung_ebene),
			gleiche_ebene);
	gtk_menu_shell_append(GTK_MENU_SHELL(suchen_nach_auswertung_ebene),
			unterpunkt);

	gtk_menu_item_set_submenu(GTK_MENU_ITEM(suchen_nach_auswertung),
			suchen_nach_auswertung_ebene);

	//menu sichtbar machen
	gtk_widget_show_all(suchen_menu);

	listbox = (GtkWidget*) g_object_get_data(G_OBJECT(window), "listbox");
	g_object_set_data(G_OBJECT(gleiche_ebene), "listbox", listbox);
	g_object_set_data(G_OBJECT(unterpunkt), "listbox", listbox);
	g_object_set_data(G_OBJECT(unterpunkt), "child", GINT_TO_POINTER(1));

	//einfügen
	headerbar = (GtkWidget*) g_object_get_data(G_OBJECT(window), "headerbar");
	gtk_menu_button_set_popup(GTK_MENU_BUTTON(suchen_menu_button), suchen_menu);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(headerbar), suchen_menu_button);

	gtk_widget_show_all(window);

	g_signal_connect(listbox, "row-activated", G_CALLBACK(cb_lb_row_activated),
			(gpointer ) zond);
	g_signal_connect(gleiche_ebene, "activate",
			G_CALLBACK(cb_suchen_nach_auswertung), (gpointer ) zond);
	g_signal_connect(unterpunkt, "activate",
			G_CALLBACK(cb_suchen_nach_auswertung), (gpointer ) zond);

	return window;
}

static gint suchen_anzeigen_ergebnisse(Projekt *zond, const gchar *titel,
		GPtrArray *arr_items, GError **error) {
	gint rc = 0;
	GtkWidget *ergebnisfenster = 0;

	ergebnisfenster = suchen_erzeugen_ergebnisfenster(zond, titel);

	rc = suchen_fuellen_ergebnisfenster(zond, ergebnisfenster, arr_items,
			error);
	if (rc)
		return -1;

	return 0;
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

/* Löst einen Rohtreffer (Knoten-ID aus suchen_db()) auf die ID des
 * zugehörigen file_part-Knotens auf (Basis-Datei oder Section) - über
 * den Knoten selbst (FILE_PART), seine Anbindung (BAUM_INHALT_FILE) oder
 * eine Copy davon (BAUM_AUSWERTUNG_COPY). Bei der Copy zeigt "link" je
 * nachdem, WELCHER Knoten kopiert wurde, unterschiedlich weit: wurde die
 * Anbindung selbst kopiert (zond_treeview_copy_node_to_baum_auswertung()),
 * übernimmt die Copy deren "link" 1:1 - das ist bereits die file_part-ID
 * (ein Hop). Wurde dagegen ein Kind-Knoten innerhalb der Anbindung
 * kopiert, zeigt "link" auf die BAUM_INHALT_FILE-Anbindung selbst, deren
 * "link" erst die file_part-ID liefert (zwei Hops). Beide Fälle müssen
 * hier unterschieden werden - ursprünglich wurde nur der Zwei-Hop-Fall
 * behandelt, wodurch Copies der Anbindung selbst fälschlich als "kein
 * Datei-Bezug" galten und als eigenständige Zeile statt zusammen mit der
 * Anbindung erschienen (Nutzer-Fund 22.09.2026).
 *
 * *file_part_node_id bleibt 0, wenn der Knoten keinen Datei-Bezug hat
 * (z.B. ein reiner Strukturpunkt) - 0 ist als Knoten-ID nie vergeben
 * (echte Knoten beginnen ab ID 3, s. zond_dbase_create_db_maj_1()). */
static gint suchen_resolve_file_part_node(Projekt *zond, gint node_id,
		gint *file_part_node_id, GError **error) {
	gint rc = 0;
	gint type = 0;
	gint link = 0;

	*file_part_node_id = 0;

	rc = zond_dbase_get_type_and_link(zond->dbase_zond->zond_dbase_work,
			node_id, &type, &link, error);
	if (rc)
		return -1;

	if (type == ZOND_DBASE_TYPE_FILE_PART)
		*file_part_node_id = node_id;
	else if (type == ZOND_DBASE_TYPE_BAUM_INHALT_FILE)
		*file_part_node_id = link;
	else if (type == ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_COPY) {
		gint type_ziel = 0;
		gint link_ziel = 0;

		rc = zond_dbase_get_type_and_link(zond->dbase_zond->zond_dbase_work,
				link, &type_ziel, &link_ziel, error);
		if (rc)
			return -1;

		if (type_ziel == ZOND_DBASE_TYPE_FILE_PART)
			//Ein Hop: Copy der Anbindung selbst, "link" ist bereits die
			//file_part-ID (s. Funktionskommentar oben).
			*file_part_node_id = link;
		//Zwei Hops: Copy eines Kind-Knotens, "link" zeigt auf die
		//BAUM_INHALT_FILE-Anbindung - der dort ebenfalls erwähnte
		//VIRT_PDF-Fall (Doc-Kommentar zond_dbase_create_db_maj_1()) wird
		//aktuell nirgends erzeugt und hier defensiv als "kein Datei-Bezug
		//auflösbar" behandelt.
		else if (type_ziel == ZOND_DBASE_TYPE_BAUM_INHALT_FILE)
			*file_part_node_id = link_ziel;
	}

	return 0;
}

/* Baut eine vollständige ResultRow zu einem file_part-Knoten: file_part
 * +section, sowie - falls vorhanden - die Anbindung in BAUM_INHALT und
 * alle ihre Copies in BAUM_AUSWERTUNG, jeweils mit node_text+text.
 *
 * node_text/text der Anbindung (BAUM_INHALT_FILE) werden bewußt NICHT vom
 * Anker-Knoten selbst gelesen, sondern vom verlinkten, dauerhaften
 * file_part-Knoten (also aus demselben get_node()-Aufruf wie row->file_part/
 * row->section) - der Anker-Knoten trägt laut Konvention nie eigenes
 * icon_name/node_text/text (s. Kommentar in
 * zond_treeview_copy_node_to_baum_auswertung(), zond_treeview.c), sondern
 * nur die Position im knoten-Baum. Vorher wurde hier fälschlich vom
 * Anker-Knoten gelesen, der dafür immer NULL lieferte - Bestandsverzeichnis-
 * Spalte blieb dadurch fast immer leer (Nutzer-Fund 22.09.2026). */
static gint suchen_baue_row(Projekt *zond, gint file_part_node_id,
		ResultRow **out_row, GError **error) {
	gint rc = 0;
	ResultRow *row = NULL;
	gint id_anbindung = 0;

	row = g_new0(ResultRow, 1);
	row->file_part_node_id = file_part_node_id;

	rc = zond_dbase_get_node(zond->dbase_zond->zond_dbase_work,
			file_part_node_id, NULL, NULL, &row->file_part, &row->section,
			NULL, &row->anbindung_node_text, &row->anbindung_text, error);
	if (rc) {
		result_row_free(row);
		return -1;
	}

	rc = zond_dbase_get_baum_inhalt_file_from_file_part(
			zond->dbase_zond->zond_dbase_work, file_part_node_id,
			&id_anbindung, error);
	if (rc) {
		result_row_free(row);
		return -1;
	}

	if (id_anbindung) {
		GArray *arr_copy_ids = NULL;
		GArray *arr_copy_ids_direkt = NULL;

		row->has_anbindung = TRUE;
		row->anbindung_node_id = id_anbindung;

		//Copies eines Kind-Knotens der Anbindung zeigen mit "link" auf
		//id_anbindung (Zwei-Hop-Fall, s. suchen_resolve_file_part_node()).
		rc = zond_dbase_get_baum_auswertung_copies(
				zond->dbase_zond->zond_dbase_work, id_anbindung,
				&arr_copy_ids, error);
		if (rc) {
			result_row_free(row);
			return -1;
		}

		//Copies der Anbindung SELBST zeigen mit "link" direkt auf
		//file_part_node_id (Ein-Hop-Fall) - separat abfragen und anhängen,
		//sonst blieben sie in der Auswertungs-Spalte unsichtbar (Nutzer-Fund
		//22.09.2026, gleiche Ursache wie bei suchen_resolve_file_part_node()).
		rc = zond_dbase_get_baum_auswertung_copies(
				zond->dbase_zond->zond_dbase_work, file_part_node_id,
				&arr_copy_ids_direkt, error);
		if (rc) {
			g_array_unref(arr_copy_ids);
			result_row_free(row);
			return -1;
		}

		row->arr_copies = g_array_new(FALSE, FALSE, sizeof(ResultCopy));
		g_array_set_clear_func(row->arr_copies,
				(GDestroyNotify) result_copy_clear);

		for (guint pass = 0; pass < 2; pass++) {
			GArray *arr = pass == 0 ? arr_copy_ids : arr_copy_ids_direkt;

			for (guint i = 0; i < arr->len; i++) {
				gint copy_id = g_array_index(arr, gint, i);
				ResultCopy copy = { 0 };

				copy.node_id = copy_id;

				rc = zond_dbase_get_node(zond->dbase_zond->zond_dbase_work,
						copy_id, NULL, NULL, NULL, NULL, NULL,
						&copy.node_text, &copy.text, error);
				if (rc) {
					g_array_unref(arr_copy_ids);
					g_array_unref(arr_copy_ids_direkt);
					result_row_free(row);
					return -1;
				}

				g_array_append_val(row->arr_copies, copy);
			}
		}

		g_array_unref(arr_copy_ids);
		g_array_unref(arr_copy_ids_direkt);
	}

	*out_row = row;

	return 0;
}

/* Aggregiert die Rohtreffer aus suchen_db() zu Anzeige-Elementen: pro
 * getroffenem file_part-Knoten (Section) genau EINE vollständige
 * ResultRow (mehrfache Rohtreffer zum selben Knoten werden übersprungen,
 * die Row wird nur beim ersten Mal gebaut), Knoten ohne Datei-Bezug
 * bleiben einzelne, einfache Elemente wie bisher. */
static gint suchen_aggregieren(Projekt *zond, GArray *arr_treffer,
		GPtrArray **arr_items, GError **error) {
	gint rc = 0;
	GHashTable *ht_rows = NULL; //file_part_node_id -> ResultRow* (nicht owning)

	*arr_items = g_ptr_array_new_with_free_func(suchen_item_free);
	ht_rows = g_hash_table_new(g_direct_hash, g_direct_equal);

	for (guint i = 0; i < arr_treffer->len; i++) {
		Node node = g_array_index(arr_treffer, Node, i);
		gint file_part_node_id = 0;

		rc = suchen_resolve_file_part_node(zond, node.node_id,
				&file_part_node_id, error);
		if (rc) {
			g_hash_table_unref(ht_rows);
			g_ptr_array_unref(*arr_items);
			return -1;
		}

		if (!file_part_node_id) {
			SuchenItem *item = g_new0(SuchenItem, 1);

			item->is_row = FALSE;
			item->u.simple = node;

			g_ptr_array_add(*arr_items, item);

			continue;
		}

		if (g_hash_table_contains(ht_rows, GINT_TO_POINTER(file_part_node_id)))
			continue; //schon aggregiert

		{
			ResultRow *row = NULL;
			SuchenItem *item = NULL;

			rc = suchen_baue_row(zond, file_part_node_id, &row, error);
			if (rc) {
				g_hash_table_unref(ht_rows);
				g_ptr_array_unref(*arr_items);
				return -1;
			}

			item = g_new0(SuchenItem, 1);
			item->is_row = TRUE;
			item->u.row = row;

			g_ptr_array_add(*arr_items, item);
			g_hash_table_insert(ht_rows, GINT_TO_POINTER(file_part_node_id),
					row);
		}
	}

	g_hash_table_unref(ht_rows);

	return 0;
}

gint suchen_treeviews(Projekt *zond, const gchar *text, GError **error) {
	gint rc = 0;
	GArray *arr_treffer = NULL;
	GPtrArray *arr_items = NULL;
	gchar *titel = NULL;

	arr_treffer = g_array_new( FALSE, FALSE, sizeof(Node));

	rc = suchen_db(zond, text, arr_treffer, error);
	if (rc) {
		g_array_unref(arr_treffer);
		return -1;
	}

	rc = suchen_aggregieren(zond, arr_treffer, &arr_items, error);
	g_array_unref(arr_treffer);
	if (rc)
		return -1;

	if (arr_items->len) {
		titel = g_strconcat("Suche nach: '", text, "'", NULL);
		rc = suchen_anzeigen_ergebnisse(zond, titel, arr_items, error);
		g_free(titel);
	}
	g_ptr_array_unref(arr_items);
	if (rc)
		return -1;

	return 0;
}
