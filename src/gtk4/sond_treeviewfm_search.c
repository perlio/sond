/*
 sond (gtk4/sond_treeviewfm_search.c) - Akten, Beweisstücke, Unterlagen
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

/* Dateisuche und "Treffer im Baum zeigen" (Teil von SondTreeviewFM, s.
 * sond_treeviewfm_private.h). Die Suche selbst ist GTK-frei (tvfm/sond_tvfm_search.c);
 * hier liegen Dialog, Fortschritt, Ergebnisfenster und das Aufklappen. */

#include "sond_treeviewfm_private.h"

#include <string.h>

#include "sond_busy_dialog.h"
#include "../sond_log_and_error.h"
#include "../tvfm/sond_tvfm_search.h"

static guint signal_hit_activated = 0;

/* ------------------------------------------------------------------------
 * Treffer im Baum zeigen
 * --------------------------------------------------------------------- */

/* Sucht den Eintrag zu filepart Ebene für Ebene: auf jeder Ebene den Eintrag mit genau diesem Pfad
 * (dann gefunden) oder den, dessen Pfad ein Vorfahr ist (Trenner "/" direkt dahinter; das schließt
 * "//" bei Containern ein) und eine Ebene tiefer weitermachen. Mit open werden die Vorfahren
 * dabei aufgeklappt (laden Verzeichnisse und Container), sonst werden nur schon aufgeklappte Zeilen
 * durchsucht und der Baum bleibt unverändert. Der PageTree-/Message-Marker trägt denselben Pfad
 * wie sein Container, enthält aber nie Anhänge: kein Abstieg. *found: neue Referenz. */
static gboolean find_filepart(SondTreeviewFM *self, gchar const *filepart, gboolean open,
		SondTVFMItem **found) {
	SondTreeview *base = SOND_TREEVIEW(self);
	GListModel *level = sond_treeview_get_roots(base);
	GtkTreeListRow *row_keep = NULL;
	gboolean ok = FALSE;

	if (found)
		*found = NULL;

	while (level) {
		guint n = g_list_model_get_n_items(level);
		SondTVFMItem *exact = NULL;
		SondTVFMItem *descend = NULL;
		GtkTreeListRow *row = NULL;

		for (guint i = 0; i < n && !exact; i++) {
			SondTVFMItem *item = g_list_model_get_item(level, i);
			gchar *fp = item ? sond_tvfm_item_get_filepart(item) : NULL;

			if (fp) {
				gsize len = strlen(fp);

				if (!g_strcmp0(fp, filepart))
					exact = g_object_ref(item);
				else if (!descend && g_str_has_prefix(filepart, fp)
						&& filepart[len] == '/'
						&& !sond_tvfm_item_is_content_root_marker(item))
					descend = g_object_ref(item);
			}
			g_free(fp);
			g_clear_object(&item);
		}

		if (exact) {
			if (found)
				*found = g_object_ref(exact);
			g_object_unref(exact);
			g_clear_object(&descend);
			ok = TRUE;

			break;
		}

		if (!descend)
			break;

		row = sond_treeview_find_row(base, fm_match_same, descend, NULL);
		g_object_unref(descend);
		if (!row)
			break;

		if (!open && !gtk_tree_list_row_get_expanded(row)) {
			g_object_unref(row);

			break;
		}

		if (open)
			gtk_tree_list_row_set_expanded(row, TRUE);
		level = gtk_tree_list_row_get_children(row);
		g_clear_object(&row_keep);
		row_keep = row;
	}

	g_clear_object(&row_keep);

	return ok;
}

gboolean sond_treeviewfm_filepart_visible(SondTreeviewFM *self, gchar const *filepart,
		gboolean open, SondTVFMItem **item) {
	g_return_val_if_fail(SOND_IS_TREEVIEWFM(self), FALSE);
	g_return_val_if_fail(filepart && *filepart, FALSE);

	return find_filepart(self, filepart, open, item);
}

//Klappt den Baum bis zum Eintrag auf, wählt ihn aus und zeigt ihn
gboolean fm_reveal_filepart(SondTreeviewFM *self, gchar const *filepart) {
	SondTreeview *base = SOND_TREEVIEW(self);
	SondTVFMItem *item = NULL;
	gboolean found = FALSE;

	g_return_val_if_fail(SOND_IS_TREEVIEWFM(self), FALSE);
	g_return_val_if_fail(filepart && *filepart, FALSE);

	if (!find_filepart(self, filepart, TRUE, &item))
		return FALSE;

	found = sond_treeview_select_item(base, item);
	g_object_unref(item);
	if (found)
		gtk_widget_grab_focus(GTK_WIDGET(sond_treeview_get_column_view(base)));

	return found;
}

gboolean sond_treeviewfm_reveal_filepart(SondTreeviewFM *self, gchar const *filepart) {
	return fm_reveal_filepart(self, filepart);
}

static void default_hit_activated(SondTreeviewFM *self, gchar const *filepart) {
	if (!fm_reveal_filepart(self, filepart))
		fm_show_error(self, "Eintrag nicht gefunden", filepart);
}

void fm_search_register_signals(GObjectClass *klass) {
	signal_hit_activated = g_signal_new_class_handler("search-hit-activated",
			G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
			G_CALLBACK(default_hit_activated), NULL, NULL, NULL,
			G_TYPE_NONE, 1, G_TYPE_STRING);
}

/* ------------------------------------------------------------------------
 * Suchdialog
 * --------------------------------------------------------------------- */

typedef struct {
	GMainLoop *loop;
	gint response; //1 = Suchen, 0 = Abbrechen
	GtkWidget *window;
	GtkWidget *entry;
	GtkWidget *radio_contains;
	GtkWidget *radio_whole;
	GtkWidget *radio_wildcard;
	GtkWidget *check_case;
	GtkWidget *check_files;
	GtkWidget *check_dirs;
	GtkWidget *check_containers;
	GtkWidget *radio_all;
	GtkWidget *radio_sel;
	GtkWidget *button_ok;
} SearchDlg;

static void dlg_update(SearchDlg *sd) {
	gchar const *text = gtk_editable_get_text(GTK_EDITABLE(sd->entry));

	gtk_widget_set_sensitive(sd->button_ok, text && *text
			&& (gtk_check_button_get_active(GTK_CHECK_BUTTON(sd->check_files))
			|| gtk_check_button_get_active(GTK_CHECK_BUTTON(sd->check_dirs))));
}

static void on_dlg_changed(GtkEditable *editable, gpointer data) {
	(void) editable;

	dlg_update((SearchDlg*) data);
}

static void on_dlg_toggled(GtkCheckButton *button, gpointer data) {
	(void) button;

	dlg_update((SearchDlg*) data);
}

static void dlg_finish(SearchDlg *sd, gint response) {
	sd->response = response;
	g_main_loop_quit(sd->loop);
}

static void on_dlg_ok(GtkButton *button, gpointer data) {
	(void) button;

	dlg_finish((SearchDlg*) data, 1);
}

static void on_dlg_cancel(GtkButton *button, gpointer data) {
	(void) button;

	dlg_finish((SearchDlg*) data, 0);
}

static void on_dlg_activate(GtkEntry *entry, gpointer data) {
	SearchDlg *sd = (SearchDlg*) data;

	(void) entry;

	if (gtk_widget_get_sensitive(sd->button_ok))
		dlg_finish(sd, 1);
}

static gboolean on_dlg_close(GtkWindow *window, gpointer data) {
	(void) window;

	dlg_finish((SearchDlg*) data, 0);

	return TRUE; //das Fenster zerstört der Aufrufer nach der Schleife
}

static GtkWidget* dlg_label(gchar const *text) {
	GtkWidget *label = gtk_label_new(text);

	gtk_widget_set_halign(label, GTK_ALIGN_END);
	gtk_widget_set_valign(label, GTK_ALIGN_START);

	return label;
}

/* Fragt Suchtext und Optionen ab; die Einstellungen bleiben in der Instanz für die
 * nächste Suche erhalten. FALSE bei Abbruch. */
static gboolean search_dialog(SondTreeviewFM *self, guint n_selected,
		gboolean *selected_only) {
	SondTreeviewFMPrivate *priv = fm_get_priv(self);
	SondTVFMSearchOpts *opts = &priv->search_opts;
	SearchDlg sd = { 0 };
	GtkWidget *grid = gtk_grid_new();
	GtkWidget *box = NULL;
	GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget *button_cancel = gtk_button_new_with_label("Abbrechen");
	GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gboolean ok = FALSE;

	if (!priv->search_settings_valid) {
		opts->mode = SOND_TVFM_SEARCH_CONTAINS;
		opts->case_sensitive = FALSE;
		opts->match_files = TRUE;
		opts->match_dirs = FALSE;
		opts->in_containers = FALSE;
		priv->search_settings_valid = TRUE;
	}

	sd.loop = g_main_loop_new(NULL, FALSE);
	sd.window = gtk_window_new();
	gtk_window_set_title(GTK_WINDOW(sd.window), "Dateisuche");
	gtk_window_set_modal(GTK_WINDOW(sd.window), TRUE);
	if (fm_parent_window(self))
		gtk_window_set_transient_for(GTK_WINDOW(sd.window), fm_parent_window(self));
	gtk_window_set_resizable(GTK_WINDOW(sd.window), FALSE);
	g_signal_connect(sd.window, "close-request", G_CALLBACK(on_dlg_close), &sd);

	gtk_widget_set_margin_start(outer, 14);
	gtk_widget_set_margin_end(outer, 14);
	gtk_widget_set_margin_top(outer, 14);
	gtk_widget_set_margin_bottom(outer, 14);
	gtk_window_set_child(GTK_WINDOW(sd.window), outer);

	gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 12);

	//Suchtext
	sd.entry = gtk_entry_new();
	gtk_widget_set_hexpand(sd.entry, TRUE);
	gtk_widget_set_size_request(sd.entry, 300, -1);
	if (priv->search_text)
		gtk_editable_set_text(GTK_EDITABLE(sd.entry), priv->search_text);
	gtk_grid_attach(GTK_GRID(grid), dlg_label("Suchtext:"), 0, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), sd.entry, 1, 0, 1, 1);

	//Vergleich
	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
	sd.radio_contains = gtk_check_button_new_with_label("Name enthält Text");
	sd.radio_whole = gtk_check_button_new_with_label("Ganzer Name");
	sd.radio_wildcard = gtk_check_button_new_with_label("Muster mit * und ?");
	gtk_check_button_set_group(GTK_CHECK_BUTTON(sd.radio_whole),
			GTK_CHECK_BUTTON(sd.radio_contains));
	gtk_check_button_set_group(GTK_CHECK_BUTTON(sd.radio_wildcard),
			GTK_CHECK_BUTTON(sd.radio_contains));
	gtk_box_append(GTK_BOX(box), sd.radio_contains);
	gtk_box_append(GTK_BOX(box), sd.radio_whole);
	gtk_box_append(GTK_BOX(box), sd.radio_wildcard);
	gtk_check_button_set_active(GTK_CHECK_BUTTON(
			(opts->mode == SOND_TVFM_SEARCH_WHOLE_NAME) ? sd.radio_whole :
			(opts->mode == SOND_TVFM_SEARCH_WILDCARD) ? sd.radio_wildcard :
					sd.radio_contains), TRUE);
	gtk_grid_attach(GTK_GRID(grid), dlg_label("Vergleich:"), 0, 1, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), box, 1, 1, 1, 1);

	//Groß-/Kleinschreibung
	sd.check_case = gtk_check_button_new_with_label("Groß-/Kleinschreibung beachten");
	gtk_check_button_set_active(GTK_CHECK_BUTTON(sd.check_case), opts->case_sensitive);
	gtk_grid_attach(GTK_GRID(grid), sd.check_case, 1, 2, 1, 1);

	//Suchen in
	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
	sd.check_files = gtk_check_button_new_with_label("Dateien");
	sd.check_dirs = gtk_check_button_new_with_label("Verzeichnisse");
	sd.check_containers = gtk_check_button_new_with_label(
			"In Containern suchen (ZIP, PDF-Anhänge, E-Mails)");
	gtk_check_button_set_active(GTK_CHECK_BUTTON(sd.check_files), opts->match_files);
	gtk_check_button_set_active(GTK_CHECK_BUTTON(sd.check_dirs), opts->match_dirs);
	gtk_check_button_set_active(GTK_CHECK_BUTTON(sd.check_containers),
			opts->in_containers);
	gtk_box_append(GTK_BOX(box), sd.check_files);
	gtk_box_append(GTK_BOX(box), sd.check_dirs);
	gtk_box_append(GTK_BOX(box), sd.check_containers);
	gtk_grid_attach(GTK_GRID(grid), dlg_label("Suchen in:"), 0, 3, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), box, 1, 3, 1, 1);

	//Umfang
	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
	sd.radio_all = gtk_check_button_new_with_label("Gesamter Dateibaum");
	sd.radio_sel = gtk_check_button_new_with_label("Nur unterhalb der Markierung");
	gtk_check_button_set_group(GTK_CHECK_BUTTON(sd.radio_sel),
			GTK_CHECK_BUTTON(sd.radio_all));
	gtk_check_button_set_active(GTK_CHECK_BUTTON(n_selected ? sd.radio_sel :
			sd.radio_all), TRUE);
	gtk_widget_set_sensitive(sd.radio_sel, n_selected > 0);
	gtk_box_append(GTK_BOX(box), sd.radio_all);
	gtk_box_append(GTK_BOX(box), sd.radio_sel);
	gtk_grid_attach(GTK_GRID(grid), dlg_label("Umfang:"), 0, 4, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), box, 1, 4, 1, 1);

	gtk_box_append(GTK_BOX(outer), grid);

	//Knöpfe
	sd.button_ok = gtk_button_new_with_label("Suchen");
	gtk_widget_add_css_class(sd.button_ok, "suggested-action");
	gtk_widget_set_halign(buttons, GTK_ALIGN_END);
	gtk_box_append(GTK_BOX(buttons), button_cancel);
	gtk_box_append(GTK_BOX(buttons), sd.button_ok);
	gtk_box_append(GTK_BOX(outer), buttons);

	g_signal_connect(sd.button_ok, "clicked", G_CALLBACK(on_dlg_ok), &sd);
	g_signal_connect(button_cancel, "clicked", G_CALLBACK(on_dlg_cancel), &sd);
	g_signal_connect(sd.entry, "activate", G_CALLBACK(on_dlg_activate), &sd);
	g_signal_connect(sd.entry, "changed", G_CALLBACK(on_dlg_changed), &sd);
	g_signal_connect(sd.check_files, "toggled", G_CALLBACK(on_dlg_toggled), &sd);
	g_signal_connect(sd.check_dirs, "toggled", G_CALLBACK(on_dlg_toggled), &sd);
	dlg_update(&sd);

	gtk_window_set_default_widget(GTK_WINDOW(sd.window), sd.button_ok);
	gtk_window_present(GTK_WINDOW(sd.window));
	gtk_widget_grab_focus(sd.entry);

	g_main_loop_run(sd.loop);

	if (sd.response == 1) {
		g_free(priv->search_text);
		priv->search_text = g_strdup(gtk_editable_get_text(GTK_EDITABLE(sd.entry)));
		opts->mode = gtk_check_button_get_active(GTK_CHECK_BUTTON(sd.radio_whole)) ?
				SOND_TVFM_SEARCH_WHOLE_NAME :
				gtk_check_button_get_active(GTK_CHECK_BUTTON(sd.radio_wildcard)) ?
						SOND_TVFM_SEARCH_WILDCARD : SOND_TVFM_SEARCH_CONTAINS;
		opts->case_sensitive = gtk_check_button_get_active(GTK_CHECK_BUTTON(
				sd.check_case));
		opts->match_files = gtk_check_button_get_active(GTK_CHECK_BUTTON(
				sd.check_files));
		opts->match_dirs = gtk_check_button_get_active(GTK_CHECK_BUTTON(
				sd.check_dirs));
		opts->in_containers = gtk_check_button_get_active(GTK_CHECK_BUTTON(
				sd.check_containers));
		*selected_only = gtk_check_button_get_active(GTK_CHECK_BUTTON(sd.radio_sel));
		ok = TRUE;
	}

	g_signal_handlers_disconnect_by_data(sd.window, &sd);
	gtk_window_destroy(GTK_WINDOW(sd.window));
	g_main_loop_unref(sd.loop);

	return ok;
}

/* ------------------------------------------------------------------------
 * Ergebnisfenster
 * --------------------------------------------------------------------- */

static void hit_setup(GtkSignalListItemFactory *factory, GtkListItem *listitem,
		gpointer data) {
	GtkWidget *label = gtk_label_new(NULL);

	(void) factory;
	(void) data;

	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_widget_set_margin_start(label, 6);
	gtk_widget_set_margin_end(label, 6);
	gtk_widget_set_margin_top(label, 2);
	gtk_widget_set_margin_bottom(label, 2);
	gtk_list_item_set_child(listitem, label);
}

static void hit_bind(GtkSignalListItemFactory *factory, GtkListItem *listitem,
		gpointer data) {
	GtkStringObject *so = gtk_list_item_get_item(listitem);

	(void) factory;
	(void) data;

	gtk_label_set_text(GTK_LABEL(gtk_list_item_get_child(listitem)),
			so ? gtk_string_object_get_string(so) : "");
}

static void on_hit_activated(GtkListView *view, guint position, gpointer data) {
	SondTreeviewFM *self = SOND_TREEVIEWFM(data);
	GListModel *model = G_LIST_MODEL(gtk_list_view_get_model(view));
	GtkStringObject *so = g_list_model_get_item(model, position);
	gchar *filepart = so ? g_strdup(gtk_string_object_get_string(so)) : NULL;

	if (filepart) {
		g_signal_emit(self, signal_hit_activated, 0, filepart);
		if (fm_parent_window(self))
			gtk_window_present(fm_parent_window(self));
	}

	g_free(filepart);
	g_clear_object(&so);
}

static void show_hits(SondTreeviewFM *self, GPtrArray *hits) {
	GtkStringList *list = gtk_string_list_new(NULL);
	GtkSingleSelection *selection = NULL;
	GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
	GtkWidget *view = NULL;
	GtkWidget *scrolled = gtk_scrolled_window_new();
	GtkWidget *window = gtk_window_new();
	gchar *title = g_strdup_printf("Suchergebnis (%u)", hits->len);

	for (guint i = 0; i < hits->len; i++)
		gtk_string_list_append(list, g_ptr_array_index(hits, i));

	selection = gtk_single_selection_new(G_LIST_MODEL(list));
	g_signal_connect(factory, "setup", G_CALLBACK(hit_setup), NULL);
	g_signal_connect(factory, "bind", G_CALLBACK(hit_bind), NULL);
	view = gtk_list_view_new(GTK_SELECTION_MODEL(selection), factory);
	g_signal_connect_object(view, "activate", G_CALLBACK(on_hit_activated), self, 0);

	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), view);
	gtk_window_set_title(GTK_WINDOW(window), title);
	gtk_window_set_default_size(GTK_WINDOW(window), 640, 420);
	if (fm_parent_window(self)) {
		gtk_window_set_transient_for(GTK_WINDOW(window), fm_parent_window(self));
		gtk_window_set_destroy_with_parent(GTK_WINDOW(window), TRUE);
	}
	gtk_window_set_child(GTK_WINDOW(window), scrolled);
	gtk_window_present(GTK_WINDOW(window));

	g_free(title);
}

/* ------------------------------------------------------------------------
 * Ablauf
 * --------------------------------------------------------------------- */

typedef struct {
	SondTVFMSearch *search;
	GPtrArray *start;
	GError *error;
	gint rc;
	gint ready;
} SearchThread;

static gpointer search_thread_func(gpointer data) {
	SearchThread *st = (SearchThread*) data;

	st->rc = sond_tvfm_search_run(st->search, st->start, &st->error);

	//in jedem Fall, sonst wartet die Oberfläche endlos
	g_atomic_int_set(&st->ready, 1);

	return NULL;
}

/* Die Suche selbst ohne Dialog: Fortschrittsfenster mit Abbrechen, die Oberfläche bleibt bedienbar.
 * Liefert die Fileparts der Treffer (GPtrArray mit g_free) oder NULL mit error. */
GPtrArray* sond_treeviewfm_search(SondTreeviewFM *self, SondTVFMSearchOpts const *opts,
		gboolean selected_only, guint *n_skipped, guint *n_not_loaded, GError **error) {
	GPtrArray *selected = NULL;
	GPtrArray *start = NULL;
	GPtrArray *hits = NULL;
	SondTVFMSearch *search = NULL;
	SearchThread st = { 0 };
	SondBusyDialog *busy = NULL;
	GThread *thread = NULL;
	gint cancelled = 0;

	g_return_val_if_fail(SOND_IS_TREEVIEWFM(self), NULL);
	g_return_val_if_fail(opts && opts->text && *opts->text, NULL);

	if (n_skipped)
		*n_skipped = 0;
	if (n_not_loaded)
		*n_not_loaded = 0;

	selected = sond_treeview_get_selected_items(SOND_TREEVIEW(self));
	if (selected_only && selected->len > 0)
		start = g_ptr_array_ref(selected);
	else {
		start = g_ptr_array_new_with_free_func(g_object_unref);
		g_ptr_array_add(start, sond_tvfm_item_create(self, NULL, NULL));
	}
	g_ptr_array_unref(selected);

	search = sond_tvfm_search_new(SOND_TVFM_OWNER(self), opts);

	st.search = search;
	st.start = start;
	busy = sond_busy_dialog_new(fm_parent_window(self), "Dateisuche", opts->text, &cancelled);
	thread = g_thread_new("tvfm-search", search_thread_func, &st);

	//Ereignisse abarbeiten, damit das Fenster und Abbrechen reagieren
	while (!g_atomic_int_get(&st.ready)) {
		if (g_atomic_int_get(&cancelled))
			sond_tvfm_search_cancel(search);

		while (g_main_context_pending(NULL))
			g_main_context_iteration(NULL, FALSE);
		g_usleep(20000);
	}

	g_thread_join(thread);
	sond_busy_dialog_close(busy);

	if (st.rc == -1)
		g_propagate_error(error, st.error);
	else {
		hits = sond_tvfm_search_take_hits(search);
		if (n_skipped)
			*n_skipped = sond_tvfm_search_get_n_skipped(search);
		if (n_not_loaded)
			*n_not_loaded = sond_tvfm_search_get_n_not_loaded(search);
	}

	sond_tvfm_search_free(search);
	g_ptr_array_unref(start);

	return hits;
}

void sond_treeviewfm_search_activate(SondTreeviewFM *self) {
	SondTreeviewFMPrivate *priv = NULL;
	GPtrArray *selected = NULL;
	GPtrArray *hits = NULL;
	SondTVFMSearchOpts opts = { 0 };
	GError *error = NULL;
	GString *msg = NULL;
	gboolean selected_only = FALSE;
	guint n_skipped = 0;
	guint n_not_loaded = 0;

	g_return_if_fail(SOND_IS_TREEVIEWFM(self));
	priv = fm_get_priv(self);

	if (!priv->root)
		return;

	selected = sond_treeview_get_selected_items(SOND_TREEVIEW(self));
	if (!search_dialog(self, selected->len, &selected_only)) {
		g_ptr_array_unref(selected);

		return;
	}
	g_ptr_array_unref(selected);

	opts = priv->search_opts;
	opts.text = priv->search_text;

	hits = sond_treeviewfm_search(self, &opts, selected_only, &n_skipped, &n_not_loaded, &error);
	if (!hits) {
		fm_show_error(self, "Fehler bei Dateisuche", error ? error->message : "?");
		g_clear_error(&error);

		return;
	}

	msg = g_string_new(NULL);
	if (n_skipped)
		g_string_append_printf(msg, "%u Verzeichnis(se) konnten nicht gelesen "
				"werden und wurden übersprungen.", n_skipped);
	if (n_not_loaded) {
		if (msg->len)
			g_string_append(msg, "\n\n");
		g_string_append_printf(msg, "%u nicht geladene Container (ZIP, E-Mail) "
				"wurden nicht durchsucht.", n_not_loaded);
	}

	if (hits->len == 0)
		fm_show_error(self, "Keine Datei gefunden", msg->str);
	else {
		if (msg->len)
			fm_show_error(self, "Suche unvollständig", msg->str);
		show_hits(self, hits);
	}

	g_string_free(msg, TRUE);
	g_ptr_array_unref(hits);
}

void fm_action_search(GSimpleAction *action, GVariant *parameter, gpointer data) {
	(void) action;
	(void) parameter;

	sond_treeviewfm_search_activate(SOND_TREEVIEWFM(data));
}
