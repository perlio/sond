/*
 zond (app_window.c) - Akten, Beweisstücke, Unterlagen
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
#include <mupdf/fitz.h>

#include "../../misc.h"
#include "../global_types.h"

#include "../zond_dbase.h"
#include "../zond_treeview.h"
#include "../zond_treeviewfm.h"

#include "../20allgemein/project.h"
#include "../20allgemein/suchen.h"

static gboolean cb_delete_event(GtkWidget *app_window, GdkEvent *event,
		gpointer user_data) {
	gint rc = 0;
	gchar *errmsg = NULL;

	Projekt *zond = (Projekt*) user_data;

	rc = projekt_schliessen(zond, &errmsg);
	if (rc == -1) {
		display_message(zond->app_window,
				"Fehler bei Schließen des Projekts -\n\n"
						"Bei Aufruf projekt_schliessen:\n", errmsg, NULL);
		g_free(errmsg);

		return TRUE;
	} else if (rc == 1)
		return TRUE;

	g_application_quit(G_APPLICATION(gtk_window_get_application(GTK_WINDOW(zond->app_window))));

	return TRUE;
}

static gboolean cb_close_textview(GtkWidget *window_textview, GdkEvent *event,
		gpointer user_data) {
	Projekt *zond = (Projekt*) user_data;

	gtk_widget_hide(window_textview);
	gtk_widget_set_sensitive(zond->menu.textview_extra, TRUE);

	zond->node_id_extra = 0;

	return TRUE;
}

static gboolean cb_pao_button_event(GtkWidget *app_window, GdkEvent *event,
		gpointer data) {
	((Projekt*) data)->state = event->button.state;

	return FALSE;
}

static void cb_text_buffer_changed(GtkTextBuffer *buffer, gpointer data) {
	gint rc = 0;
	gint node_id = 0;
	GError *error = NULL;

	Projekt *zond = (Projekt*) data;

	//inhalt textview abspeichern
	GtkTextIter start;
	GtkTextIter end;
	gtk_text_buffer_get_bounds(buffer, &start, &end);

	gchar *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

	//wenn text_buffer aus normalem Fenster:
	//dessen node_id, wo cursor gerade steht
	if (buffer == gtk_text_view_get_buffer(GTK_TEXT_VIEW(zond->textview)))
		node_id = zond->node_id_act;
	//sonst: node_id aus Extra-Fenster
	else
		node_id = zond->node_id_extra;

	rc = zond_dbase_update_text(zond->dbase_zond->zond_dbase_work, node_id,
			text, &error);
	g_free(text);
	if (rc) {
		display_message(zond->app_window, "Fehler in cb_textview_focus_out:\n\n"
				"Bei Aufruf zond_dbase_set_text:\n", error->message, NULL);
		g_error_free(error);

		return;
	}

	return;
}

static gboolean cb_textview_focus_in(GtkWidget *textview, GdkEvent *event,
		gpointer user_data) {
	Projekt *zond = (Projekt*) user_data;

	zond->text_buffer_changed_signal = g_signal_connect(
			gtk_text_view_get_buffer( GTK_TEXT_VIEW(textview) ), "changed",
			G_CALLBACK(cb_text_buffer_changed), (gpointer ) zond);

	return FALSE;
}

static gboolean cb_textview_focus_out(GtkWidget *textview, GdkEvent *event,
		gpointer user_data) {
	Projekt *zond = (Projekt*) user_data;

	g_signal_handler_disconnect(
			gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview)),
			zond->text_buffer_changed_signal);
	zond->text_buffer_changed_signal = 0;

	gtk_widget_queue_draw(GTK_WIDGET(zond->treeview[BAUM_AUSWERTUNG]));

	return FALSE;
}

static void cb_entry_search(GtkWidget *entry, gpointer data) {
	gint rc = 0;
	gchar *errmsg = NULL;
	const gchar *text = NULL;
	gchar *search_text = NULL;

	Projekt *zond = (Projekt*) data;

	text = gtk_entry_get_text(GTK_ENTRY(entry));

	if (!text || strlen(text) < 3)
		return;

	search_text = g_strconcat("%", text, "%", NULL);
	rc = suchen_treeviews(zond, search_text, &errmsg);
	g_free(search_text);
	if (rc) {
		display_message(zond->app_window, "Fehler Suche -\n\nBei Aufruf "
				"suchen_treeviews:\n", errmsg, NULL);
		g_free(errmsg);
	}

	gtk_popover_popdown(GTK_POPOVER(zond->popover));

	return;
}

static gboolean cb_focus_out(GtkWidget *treeview, GdkEvent *event,
		gpointer user_data) {
	Projekt *zond = (Projekt*) user_data;

	zond->baum_active = KEIN_BAUM;

	Baum baum = (Baum) sond_treeview_get_id(SOND_TREEVIEW(treeview));

	//cursor-changed-signal ausschalten
	if (baum != BAUM_FS && zond->cursor_changed_signal) {
		g_signal_handler_disconnect(treeview, zond->cursor_changed_signal);
		zond->cursor_changed_signal = 0;
	}

	g_object_set(sond_treeview_get_cell_renderer_text(zond->treeview[baum]),
			"editable", FALSE, NULL);

	zond->baum_prev = baum;

	return FALSE;
}

static gboolean cb_focus_in(GtkWidget *treeview, GdkEvent *event,
		gpointer user_data) {
	Projekt *zond = (Projekt*) user_data;

	zond->baum_active = (Baum) sond_treeview_get_id(SOND_TREEVIEW(treeview));

	//cursor-changed-signal für den aktivierten treeview anschalten
	if (zond->baum_active != BAUM_FS) {
		zond->cursor_changed_signal = g_signal_connect(treeview,
				"cursor-changed", G_CALLBACK(zond_treeview_cursor_changed),
				zond);

		g_signal_emit_by_name(treeview, "cursor-changed", user_data, NULL);
	}

	if (zond->baum_active != zond->baum_prev) {
		//selection in "altem" treeview löschen
		gtk_tree_selection_unselect_all(zond->selection[zond->baum_prev]);
// wennüberhaupt, dann in cb row_collapse oder _expand verschieben, aber eher besser so!!!
		//Cursor gewählter treeview selektieren
		GtkTreePath *path = NULL;
		gtk_tree_view_get_cursor(GTK_TREE_VIEW(treeview), &path, NULL);
		if (path) {
			gtk_tree_selection_select_path(zond->selection[zond->baum_active],
					path);
			gtk_tree_path_free(path);
		}
	}

	g_object_set(
			sond_treeview_get_cell_renderer_text(
					zond->treeview[zond->baum_active]), "editable", TRUE, NULL);

	return FALSE;
}

static void init_treeviews(Projekt *zond) {
	//der treeview
	zond->treeview[BAUM_FS] = SOND_TREEVIEW(zond_treeviewfm_new(zond));

	zond->treeview[BAUM_INHALT] = SOND_TREEVIEW(
			zond_treeview_new(zond, (gint) BAUM_INHALT));
	zond->treeview[BAUM_AUSWERTUNG] = SOND_TREEVIEW(
			zond_treeview_new(zond, (gint) BAUM_AUSWERTUNG));

	for (Baum baum = BAUM_FS; baum < NUM_BAUM; baum++) {
		//die Selection
		zond->selection[baum] = gtk_tree_view_get_selection(
				GTK_TREE_VIEW(zond->treeview[baum]));

		//focus-in
		g_signal_connect(zond->treeview[baum], "focus-in-event",
				G_CALLBACK(cb_focus_in), (gpointer ) zond);
		g_signal_connect(zond->treeview[baum], "focus-out-event",
				G_CALLBACK(cb_focus_out), (gpointer ) zond);
	}

	return;
}

static GtkWidget*
init_create_text_view(Projekt *zond) {
	GtkWidget *text_view = NULL;

	text_view = gtk_text_view_new();
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD);
	gtk_text_view_set_accepts_tab(GTK_TEXT_VIEW(text_view), FALSE);

	//Hört die Signale
	g_signal_connect(text_view, "focus-in-event",
			G_CALLBACK(cb_textview_focus_in), (gpointer ) zond);
	g_signal_connect(text_view, "focus-out-event",
			G_CALLBACK(cb_textview_focus_out), (gpointer ) zond);

	return text_view;
}

static gboolean cb_key_press(GtkWidget *treeview, GdkEventKey event,
		gpointer data) {
	Projekt *zond = (Projekt*) data;

	if (event.is_modifier || (event.state & GDK_CONTROL_MASK)
			|| (event.keyval < 0x21) || (event.keyval > 0x7e))
		return FALSE;

	gtk_popover_popup(GTK_POPOVER(zond->popover));

	return FALSE;
}

void init_app_window(Projekt *zond) {
	GtkWidget *entry_search = NULL;
	GtkTextIter text_iter = { 0 };
	SondTreeviewClass *stv_class = NULL;

	//ApplicationWindow erzeugen
	zond->app_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(zond->app_window), 800, 1000);

	//vbox für Statuszeile im unteren Bereich
	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(zond->app_window), vbox);

	/*
	 **  oberer Teil der VBox  */
	//zuerst HBox
	zond->hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_paned_set_position(GTK_PANED(zond->hpaned), 400);

	//jetzt in oberen Teil der vbox einfügen
	gtk_box_pack_start(GTK_BOX(vbox), zond->hpaned, TRUE, TRUE, 0);

	//vor erzeugung des ersten Sond_treeviews bzw. Derivat, damit Werte kopiert werden
	stv_class = g_type_class_ref(SOND_TYPE_TREEVIEW);
	stv_class->callback_key_press_event = cb_key_press;
	stv_class->callback_key_press_event_func_data = zond;

	//TreeView erzeugen und in das scrolled window
	init_treeviews(zond);

	//BAUM_FS
	GtkWidget *swindow_baum_fs = gtk_scrolled_window_new( NULL, NULL);
	gtk_container_add(GTK_CONTAINER(swindow_baum_fs),
			GTK_WIDGET(zond->treeview[BAUM_FS]));
	gtk_paned_add1(GTK_PANED(zond->hpaned), swindow_baum_fs);

	GtkWidget *hpaned_inner = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_paned_add2(GTK_PANED(zond->hpaned), hpaned_inner);

	//BAUM_INHALT
	GtkWidget *swindow_baum_inhalt = gtk_scrolled_window_new( NULL, NULL);
	gtk_container_add(GTK_CONTAINER(swindow_baum_inhalt),
			GTK_WIDGET(zond->treeview[BAUM_INHALT]));

	//BAUM_AUSWERTUNG
	//VPaned erzeugen
	GtkWidget *paned_baum_auswertung = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
	gtk_paned_set_position(GTK_PANED(paned_baum_auswertung), 500);

	GtkWidget *swindow_treeview_auswertung = gtk_scrolled_window_new( NULL,
			NULL);
	gtk_container_add(GTK_CONTAINER(swindow_treeview_auswertung),
			GTK_WIDGET(zond->treeview[BAUM_AUSWERTUNG]));

	gtk_paned_pack1(GTK_PANED(paned_baum_auswertung),
			swindow_treeview_auswertung, TRUE, TRUE);

	//in die untere Hälfte des vpaned kommt text_view in swindow
	//Scrolled Window zum Anzeigen erstellen
	GtkWidget *swindow_textview = gtk_scrolled_window_new( NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(swindow_textview),
			GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_paned_pack2(GTK_PANED(paned_baum_auswertung), swindow_textview, TRUE,
			TRUE);

	//text_view erzeugen
	zond->textview = init_create_text_view(zond);

	//der buffer des "normalen" TextViews wird präpariert,
	gtk_text_buffer_get_end_iter(
			gtk_text_view_get_buffer(GTK_TEXT_VIEW(zond->textview)),
			&text_iter);
	gtk_text_buffer_create_mark(
			gtk_text_view_get_buffer(GTK_TEXT_VIEW(zond->textview)),
			"ende-text", &text_iter, FALSE);

	//Und dann in untere Hälfte des übergebenen vpaned reinpacken
	gtk_container_add(GTK_CONTAINER(swindow_textview),
			GTK_WIDGET(zond->textview));

	//Zum Start: links BAUM_INHALT, rechts BAUM_AUSWERTUNG
	gtk_paned_pack1(GTK_PANED(hpaned_inner), swindow_baum_inhalt, TRUE, TRUE);
	gtk_paned_pack2(GTK_PANED(hpaned_inner), paned_baum_auswertung, TRUE, TRUE);

	g_signal_connect(zond->app_window, "button-press-event",
			G_CALLBACK(cb_pao_button_event), zond);
	g_signal_connect(zond->app_window, "button-release-event",
			G_CALLBACK(cb_pao_button_event), zond);

	zond->popover = gtk_popover_new(GTK_WIDGET(zond->treeview[BAUM_INHALT]));
	entry_search = gtk_entry_new();
	gtk_widget_show(entry_search);
	gtk_container_add(GTK_CONTAINER(zond->popover), entry_search);

	g_signal_connect(entry_search, "activate", G_CALLBACK(cb_entry_search),
			zond);

	/*
	 **  untere Hälfte vbox: Status-Zeile  */
	//Label erzeugen
	zond->label_status = GTK_LABEL(gtk_label_new(""));
	gtk_label_set_xalign(zond->label_status, 0.02);
	gtk_box_pack_end(GTK_BOX(vbox), GTK_WIDGET(zond->label_status), FALSE,
			FALSE, 0);

	//Signal für App-Fenster schließen
	g_signal_connect(zond->app_window, "delete-event",
			G_CALLBACK(cb_delete_event), zond);

//Und jetzt gesondertes text-view:

	//neues Fenster
	zond->textview_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	//muß in Kontext gespeichert werden, damit Fenster vor Beendigung der App
	//geschlossen wird. Sonst geht der focus rein und alles wird schlimm

	gtk_widget_set_size_request(zond->textview_window, 400, 250);

	//scrolled_window darein
	GtkWidget *swindow = gtk_scrolled_window_new( NULL, NULL);
	gtk_container_add(GTK_CONTAINER(zond->textview_window), swindow);

	//Text-view erzeugen und in scrolled-window
	zond->textview_ii = init_create_text_view(zond);
	gtk_container_add(GTK_CONTAINER(swindow), GTK_WIDGET(zond->textview_ii));

	g_signal_connect(zond->textview_window, "delete-event",
			G_CALLBACK(cb_close_textview), zond);

	// TextView initial deaktivieren (wird aktiviert wenn Row in BAUM_AUSWERTUNG gewählt)
	gtk_widget_set_sensitive(GTK_WIDGET(zond->textview), FALSE);
	gtk_text_buffer_set_text(
			gtk_text_view_get_buffer(GTK_TEXT_VIEW(zond->textview)), "", -1);
	zond->node_id_act = 0;

	return;
}

