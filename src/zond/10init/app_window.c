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
#include "../../sond_treeviewfm.h"
#include "../../sond_treeviewfm_seadrive.h"
#include "../zond_init.h"
#include "../zond_dbase.h"
#include "../zond_treeview.h"
#include "../zond_treeviewfm.h"

#include "../20allgemein/project.h"
#include "../20allgemein/suchen.h"

#include "headerbar.h"

/* =============================================================================
 * KONSTANTEN
 * ========================================================================== */

#define APP_WINDOW_DEFAULT_WIDTH  800
#define APP_WINDOW_DEFAULT_HEIGHT 1000
#define PANED_LEFT_WIDTH          400
#define PANED_AUSWERTUNG_HEIGHT   500
#define TEXTVIEW_WINDOW_MIN_WIDTH 400
#define TEXTVIEW_WINDOW_MIN_HEIGHT 250
#define MIN_SEARCH_TEXT_LENGTH    3

/* =============================================================================
 * HILFS-FUNKTIONEN
 * ========================================================================== */

static void show_error_message(GtkWidget *window, const gchar *context,
		const gchar *error_msg) {
	display_message(window, context, error_msg, NULL);
}

static gint get_active_node_id(Projekt *zond, GtkTextBuffer *buffer) {
	if (buffer == gtk_text_view_get_buffer(GTK_TEXT_VIEW(zond->textview)))
		return zond->node_id_act;
	else
		return zond->node_id_extra;
}

static void save_text_buffer_to_database(Projekt *zond, GtkTextBuffer *buffer) {
	GtkTextIter start, end;
	GError *error = NULL;
	gchar *text = NULL;
	gint node_id = 0;
	gint rc = 0;

	gtk_text_buffer_get_bounds(buffer, &start, &end);
	text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
	node_id = get_active_node_id(zond, buffer);

	rc = zond_dbase_update_text(zond->dbase_zond->zond_dbase_work,
			node_id, text, &error);
	g_free(text);

	if (rc) {
		show_error_message(zond->app_window,
				"Fehler beim Speichern des Textes:\n\n"
				"Bei Aufruf zond_dbase_update_text:\n",
				error->message);
		g_error_free(error);
	}
}

/* =============================================================================
 * CALLBACK-FUNKTIONEN - FENSTER
 * ========================================================================== */

static gboolean cb_delete_event(GtkWidget *app_window, GdkEvent *event,
		gpointer user_data) {
	Projekt *zond = (Projekt*) user_data;
	gchar *errmsg = NULL;
	gint rc = 0;

	rc = project_close(zond, &errmsg);

	if (rc == -1) {
		show_error_message(zond->app_window,
				"Fehler beim Schließen des Projekts -\n\n"
				"Bei Aufruf projekt_schliessen:\n",
				errmsg);
		g_free(errmsg);
		return TRUE;
	} else if (rc == 1) {
		return TRUE;
	}

	g_application_quit(G_APPLICATION(
			gtk_window_get_application(GTK_WINDOW(zond->app_window))));

	return TRUE;
}

static gboolean cb_close_textview(GtkWidget *window_textview, GdkEvent *event,
		gpointer user_data) {
	Projekt *zond = (Projekt*) user_data;

	gtk_widget_hide(window_textview);
	g_simple_action_set_enabled(zond->menu.textview_extra, TRUE);
	zond->node_id_extra = 0;

	return TRUE;
}

static gboolean cb_button_event(GtkWidget *app_window, GdkEvent *event,
		gpointer data) {
	((Projekt*) data)->state = event->button.state;
	return FALSE;
}

/* =============================================================================
 * CALLBACK-FUNKTIONEN - TEXTVIEW
 * ========================================================================== */

static void cb_text_buffer_changed(GtkTextBuffer *buffer, gpointer data) {
	Projekt *zond = (Projekt*) data;
	save_text_buffer_to_database(zond, buffer);
}

/* GTK3: focus via classic signals */
static gboolean cb_textview_focus_in(GtkWidget *textview, GdkEvent *event,
		gpointer user_data) {
	Projekt *zond = (Projekt*) user_data;

	zond->text_buffer_changed_signal = g_signal_connect(
			gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview)),
			"changed",
			G_CALLBACK(cb_text_buffer_changed),
			zond);

	return FALSE;
}

static gboolean cb_textview_focus_out(GtkWidget *textview, GdkEvent *event,
		gpointer user_data) {
	Projekt *zond = (Projekt*) user_data;

	if (zond->text_buffer_changed_signal) {
		g_signal_handler_disconnect(
				gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview)),
				zond->text_buffer_changed_signal);
		zond->text_buffer_changed_signal = 0;
	}

	gtk_widget_queue_draw(GTK_WIDGET(zond->treeview[BAUM_AUSWERTUNG]));

	return FALSE;
}

/* =============================================================================
 * CALLBACK-FUNKTIONEN - TREEVIEW
 * ========================================================================== */

/* GTK3: focus via classic signals */
static gboolean cb_treeview_focus_out(GtkWidget *treeview, GdkEvent *event,
		gpointer user_data) {
	Projekt *zond = (Projekt*) user_data;
	Baum baum = (Baum) sond_treeview_get_id(SOND_TREEVIEW(treeview));

	zond->baum_active = KEIN_BAUM;

	if (baum != BAUM_FS && zond->cursor_changed_signal) {
		g_signal_handler_disconnect(treeview, zond->cursor_changed_signal);
		zond->cursor_changed_signal = 0;
	}

	g_object_set(sond_treeview_get_cell_renderer_text(zond->treeview[baum]),
			"editable", FALSE, NULL);

	zond->baum_prev = baum;

	return FALSE;
}

static gboolean cb_treeview_focus_in(GtkWidget *treeview, GdkEvent *event,
		gpointer user_data) {
	Projekt *zond = (Projekt*) user_data;

	zond->baum_active = (Baum) sond_treeview_get_id(SOND_TREEVIEW(treeview));

	if (zond->baum_active != BAUM_FS) {
		zond->cursor_changed_signal = g_signal_connect(
				treeview,
				"cursor-changed",
				G_CALLBACK(zond_treeview_cursor_changed),
				zond);

		g_signal_emit_by_name(treeview, "cursor-changed", user_data, NULL);
	}

	if (zond->baum_active != zond->baum_prev) {
		gtk_tree_selection_unselect_all(zond->selection[zond->baum_prev]);

		GtkTreePath *path = NULL;
		gtk_tree_view_get_cursor(GTK_TREE_VIEW(treeview), &path, NULL);
		if (path) {
			gtk_tree_selection_select_path(
					zond->selection[zond->baum_active], path);
			gtk_tree_path_free(path);
		}
	}

	g_object_set(
			sond_treeview_get_cell_renderer_text(zond->treeview[zond->baum_active]),
			"editable", TRUE, NULL);

	if (zond->baum_active == BAUM_AUSWERTUNG) {
		GtkTreePath *path = NULL;
		gtk_tree_view_get_cursor(GTK_TREE_VIEW(treeview), &path, NULL);
		if (path) {
			gtk_widget_set_sensitive(GTK_WIDGET(zond->textview), TRUE);
			gtk_tree_path_free(path);
		}
	} else {
		gtk_widget_set_sensitive(GTK_WIDGET(zond->textview), FALSE);
	}

	return FALSE;
}

/* Key-Controller-Callback — GTK3 + GTK4 (GtkEventControllerKey ab 3.24) */
static gboolean cb_treeview_key_press(GtkEventControllerKey *ctrl,
		guint keyval, guint keycode, GdkModifierType state, gpointer data) {
	Projekt *zond = (Projekt*) data;

	if ((state & GDK_CONTROL_MASK) || (keyval < 0x21) || (keyval > 0x7e))
		return FALSE;

	gtk_popover_popup(GTK_POPOVER(zond->popover));

	return FALSE;
}

/* =============================================================================
 * CALLBACK-FUNKTIONEN - SUCHE
 * ========================================================================== */

static void cb_entry_search(GtkWidget *entry, gpointer data) {
	Projekt *zond = (Projekt*) data;
	const gchar *text = NULL;
	gchar *search_text = NULL;
	gchar *errmsg = NULL;
	gint rc = 0;

	text = gtk_entry_get_text(GTK_ENTRY(entry));

	if (!text || strlen(text) < MIN_SEARCH_TEXT_LENGTH)
		return;

	search_text = g_strconcat("%", text, "%", NULL);
	rc = suchen_treeviews(zond, search_text, &errmsg);
	g_free(search_text);

	if (rc) {
		show_error_message(zond->app_window,
				"Fehler bei der Suche -\n\n"
				"Bei Aufruf suchen_treeviews:\n",
				errmsg);
		g_free(errmsg);
	}

	gtk_popover_popdown(GTK_POPOVER(zond->popover));
}

/* =============================================================================
 * INITIALISIERUNGS-FUNKTIONEN
 * ========================================================================== */

static void init_treeviews(Projekt *zond) {
	zond->treeview[BAUM_FS] = SOND_TREEVIEW(zond_treeviewfm_new(zond));
	zond->treeview[BAUM_INHALT] = SOND_TREEVIEW(
			zond_treeview_new(zond, (gint) BAUM_INHALT));
	zond->treeview[BAUM_AUSWERTUNG] = SOND_TREEVIEW(
			zond_treeview_new(zond, (gint) BAUM_AUSWERTUNG));

	gtk_widget_set_hexpand(GTK_WIDGET(zond->treeview[BAUM_INHALT]), FALSE);
	gtk_widget_set_hexpand(GTK_WIDGET(zond->treeview[BAUM_AUSWERTUNG]), FALSE);

	for (Baum baum = BAUM_FS; baum < NUM_BAUM; baum++) {
		zond->selection[baum] = gtk_tree_view_get_selection(
				GTK_TREE_VIEW(zond->treeview[baum]));

		/* Focus — GTK3 classic signals */
		g_signal_connect(zond->treeview[baum], "focus-in-event",
				G_CALLBACK(cb_treeview_focus_in), zond);
		g_signal_connect(zond->treeview[baum], "focus-out-event",
				G_CALLBACK(cb_treeview_focus_out), zond);

		/* Key-Controller (GtkEventControllerKey ab GTK 3.24) */
		GtkEventController *key_ctrl =
				gtk_event_controller_key_new(GTK_WIDGET(zond->treeview[baum]));
		g_signal_connect(key_ctrl, "key-pressed",
				G_CALLBACK(cb_treeview_key_press), zond);
		gtk_event_controller_set_propagation_phase(key_ctrl, GTK_PHASE_BUBBLE);

		/* Referenz für sond_treeview.c speichern (Editing schaltet ihn ab) */
		g_object_set_data(G_OBJECT(zond->treeview[baum]),
				"key-controller", key_ctrl);
	}
}

static GtkWidget* create_configured_textview(Projekt *zond) {
	GtkWidget *text_view = gtk_text_view_new();

	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD);
	gtk_text_view_set_accepts_tab(GTK_TEXT_VIEW(text_view), FALSE);

	/* Focus — GTK3 classic signals */
	g_signal_connect(text_view, "focus-in-event",
			G_CALLBACK(cb_textview_focus_in), zond);
	g_signal_connect(text_view, "focus-out-event",
			G_CALLBACK(cb_textview_focus_out), zond);

	return text_view;
}

static GtkWidget* create_scrolled_window(GtkWidget *child) {
	GtkWidget *swindow = gtk_scrolled_window_new(NULL, NULL);

	if (child)
		gtk_container_add(GTK_CONTAINER(swindow), child);

	return swindow;
}

static void init_search_popover(Projekt *zond) {
	GtkWidget *entry_search = NULL;

	zond->popover = gtk_popover_new(GTK_WIDGET(zond->treeview[BAUM_INHALT]));
	entry_search = gtk_entry_new();
	gtk_widget_show(entry_search);
	gtk_container_add(GTK_CONTAINER(zond->popover), entry_search);

	g_signal_connect(entry_search, "activate",
			G_CALLBACK(cb_entry_search), zond);
}

static void init_extra_textview_window(Projekt *zond) {
	GtkWidget *swindow = NULL;

	zond->textview_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_widget_set_size_request(zond->textview_window,
			TEXTVIEW_WINDOW_MIN_WIDTH, TEXTVIEW_WINDOW_MIN_HEIGHT);

	swindow = create_scrolled_window(NULL);
	gtk_container_add(GTK_CONTAINER(zond->textview_window), swindow);

	zond->textview_ii = create_configured_textview(zond);
	gtk_container_add(GTK_CONTAINER(swindow), GTK_WIDGET(zond->textview_ii));

	g_signal_connect(zond->textview_window, "delete-event",
			G_CALLBACK(cb_close_textview), zond);
}

static void init_treeview_layout(Projekt *zond) {
	GtkWidget *swindow_baum_fs = NULL;
	GtkWidget *swindow_baum_inhalt = NULL;
	GtkWidget *hpaned_inner = NULL;
	GtkWidget *paned_baum_auswertung = NULL;
	GtkWidget *swindow_treeview_auswertung = NULL;
	GtkWidget *swindow_textview = NULL;

	swindow_baum_fs = create_scrolled_window(GTK_WIDGET(zond->treeview[BAUM_FS]));
	gtk_paned_add1(GTK_PANED(zond->hpaned), swindow_baum_fs);

	hpaned_inner = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_paned_pack2(GTK_PANED(zond->hpaned), hpaned_inner, FALSE, TRUE);

	swindow_baum_inhalt = create_scrolled_window(
			GTK_WIDGET(zond->treeview[BAUM_INHALT]));

	paned_baum_auswertung = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
	gtk_paned_set_position(GTK_PANED(paned_baum_auswertung), PANED_AUSWERTUNG_HEIGHT);

	swindow_treeview_auswertung = create_scrolled_window(
			GTK_WIDGET(zond->treeview[BAUM_AUSWERTUNG]));
	gtk_scrolled_window_set_min_content_width(
			GTK_SCROLLED_WINDOW(swindow_treeview_auswertung), 0);
	gtk_paned_pack1(GTK_PANED(paned_baum_auswertung),
			swindow_treeview_auswertung, FALSE, TRUE);

	swindow_textview = create_scrolled_window(NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(swindow_textview),
			GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_paned_pack2(GTK_PANED(paned_baum_auswertung),
			swindow_textview, FALSE, TRUE);

	zond->textview = create_configured_textview(zond);

	GtkTextIter text_iter = { 0 };
	gtk_text_buffer_get_end_iter(
			gtk_text_view_get_buffer(GTK_TEXT_VIEW(zond->textview)), &text_iter);
	gtk_text_buffer_create_mark(
			gtk_text_view_get_buffer(GTK_TEXT_VIEW(zond->textview)),
			"ende-text", &text_iter, FALSE);

	gtk_container_add(GTK_CONTAINER(swindow_textview),
			GTK_WIDGET(zond->textview));

	gtk_paned_pack1(GTK_PANED(hpaned_inner), swindow_baum_inhalt, FALSE, TRUE);
	gtk_paned_pack2(GTK_PANED(hpaned_inner), paned_baum_auswertung, FALSE, TRUE);
}

static void init_statusbar(Projekt *zond, GtkWidget *vbox) {
	zond->label_status = GTK_LABEL(gtk_label_new(""));
	gtk_label_set_ellipsize(zond->label_status, PANGO_ELLIPSIZE_END);
	gtk_label_set_xalign(zond->label_status, 0.02);
	gtk_box_pack_end(GTK_BOX(vbox), GTK_WIDGET(zond->label_status),
			FALSE, FALSE, 0);
}

/* =============================================================================
 * HAUPT-INITIALISIERUNGS-FUNKTION
 * ========================================================================== */

#ifdef _WIN32
static void cb_seadrive_status_app_window(SondTreeviewFM *stvfm,
		guint pending_down, guint pending_up,
		gpointer user_data) {
	GtkLabel *label = GTK_LABEL(user_data);
	gchar *text = NULL;
	if (!sond_treeviewfm_is_seadrive_path(stvfm)) {
		gtk_label_set_text(label, "");
		return;
	}
	if (pending_down == 0 && pending_up == 0)
		text = g_strdup("SeaDrive: ✓");
	else {
		GString *s = g_string_new("SeaDrive:");
		if (pending_down > 0) g_string_append_printf(s, " ↓ %u", pending_down);
		if (pending_up   > 0) g_string_append_printf(s, " ↑ %u", pending_up);
		text = g_string_free(s, FALSE);
	}
	gtk_label_set_text(label, text);
	g_free(text);
}
#endif

void init_app_window(Projekt *zond) {
	GtkWidget *vbox = NULL;

	zond->app_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(zond->app_window),
			APP_WINDOW_DEFAULT_WIDTH, APP_WINDOW_DEFAULT_HEIGHT);

	init_headerbar(zond);

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(zond->app_window), vbox);

	zond->hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_paned_set_position(GTK_PANED(zond->hpaned), PANED_LEFT_WIDTH);
	gtk_box_pack_start(GTK_BOX(vbox), zond->hpaned, TRUE, TRUE, 0);

	init_treeviews(zond);

#ifdef _WIN32
	{
		GtkWidget *label_seadrive = g_object_get_data(
				G_OBJECT(zond->app_window), "seadrive-label");
		if (label_seadrive)
			g_signal_connect(
					zond->treeview[BAUM_FS], "seadrive-status",
					G_CALLBACK(cb_seadrive_status_app_window),
					label_seadrive);
	}
#endif

	init_treeview_layout(zond);

	/* Maus-Button-Events für Modifier-Tracking */
	g_signal_connect(zond->app_window, "button-press-event",
			G_CALLBACK(cb_button_event), zond);
	g_signal_connect(zond->app_window, "button-release-event",
			G_CALLBACK(cb_button_event), zond);

	init_search_popover(zond);
	init_statusbar(zond, vbox);

	g_signal_connect(zond->app_window, "delete-event",
			G_CALLBACK(cb_delete_event), zond);

	init_extra_textview_window(zond);

	gtk_widget_set_sensitive(GTK_WIDGET(zond->textview), FALSE);
	gtk_text_buffer_set_text(
			gtk_text_view_get_buffer(GTK_TEXT_VIEW(zond->textview)), "", -1);
	zond->node_id_act = 0;
}
