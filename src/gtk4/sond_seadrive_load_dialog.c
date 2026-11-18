/*
 sond (gtk4/sond_seadrive_load_dialog.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_seadrive_load_dialog.h"

#ifdef G_OS_WIN32

#include "../seadrive/sond_seadrive_core.h"

#define LOAD_TICK_MS 300

typedef struct {
	SondSeadriveLoadJob *job;
	GMainLoop *loop;
	GtkWidget *window;
	GtkWidget *button;
	GtkWidget *agg_label;
	GtkWidget *agg_bar;
	GPtrArray *labels; //je Eintrag (nur ohne Sammelanzeige)
	GPtrArray *bars;
	guint timer;
} LoadDlg;

static void dlg_update(LoadDlg *dlg) {
	guint n = sond_seadrive_load_job_n_entries(dlg->job);
	guint done = sond_seadrive_load_job_n_done(dlg->job);
	guint64 loaded = sond_seadrive_load_job_loaded_bytes(dlg->job);
	guint64 total = sond_seadrive_load_job_total_bytes(dlg->job);
	gchar *loaded_str = g_format_size(loaded);
	gchar *total_str = g_format_size(total);
	gchar *title = NULL;

	if (n == 1)
		title = g_strdup_printf("Download läuft – %s von %s", loaded_str, total_str);
	else
		title = g_strdup_printf("Download läuft – %s von %s (%u von %u Dateien)",
				loaded_str, total_str, done, n);
	gtk_window_set_title(GTK_WINDOW(dlg->window), title);
	g_free(title);
	g_free(loaded_str);
	g_free(total_str);

	if (dlg->agg_bar) {
		gchar *text = g_strdup_printf("%u von %u Dateien", done, n);
		gdouble fraction = total ? (gdouble) loaded / (gdouble) total :
				(n ? (gdouble) done / n : 1.0);

		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(dlg->agg_bar), fraction);
		gtk_progress_bar_set_text(GTK_PROGRESS_BAR(dlg->agg_bar), text);
		g_free(text);

		return;
	}

	for (guint i = 0; i < n; i++) {
		SondSeadriveLoadEntry const *entry = sond_seadrive_load_job_entry(dlg->job, i);
		GtkProgressBar *bar = g_ptr_array_index(dlg->bars, i);

		if (entry->done) {
			gtk_progress_bar_set_fraction(bar, entry->ok ? 1.0 : 0.0);
			gtk_progress_bar_set_text(bar, entry->ok ? "fertig" : "nicht geladen");
		} else {
			gchar *on_disk = g_format_size(entry->on_disk);
			gchar *size = g_format_size(entry->file_size);
			gchar *text = g_strdup_printf("%s von %s", on_disk, size);

			gtk_progress_bar_set_fraction(bar, entry->file_size ?
					MIN((gdouble) entry->on_disk / (gdouble) entry->file_size, 1.0) : 0.0);
			gtk_progress_bar_set_text(bar, text);
			g_free(on_disk);
			g_free(size);
			g_free(text);
		}
	}
}

static gboolean on_tick(gpointer data) {
	LoadDlg *dlg = (LoadDlg*) data;

	sond_seadrive_load_job_poll(dlg->job);
	dlg_update(dlg);

	if (sond_seadrive_load_job_all_done(dlg->job)) {
		dlg->timer = 0;
		g_main_loop_quit(dlg->loop);

		return G_SOURCE_REMOVE;
	}

	return G_SOURCE_CONTINUE;
}

static void dlg_cancel(LoadDlg *dlg) {
	if (sond_seadrive_load_job_cancelled(dlg->job))
		return;

	sond_seadrive_load_job_cancel(dlg->job);
	for (guint i = 0; dlg->labels && i < dlg->labels->len; i++)
		gtk_label_set_text(GTK_LABEL(g_ptr_array_index(dlg->labels, i)),
				"Wird abgebrochen …");
	if (dlg->agg_label)
		gtk_label_set_text(GTK_LABEL(dlg->agg_label), "Wird abgebrochen …");
	gtk_widget_set_sensitive(dlg->button, FALSE);
}

static void on_cancel_clicked(GtkButton *button, gpointer data) {
	(void) button;

	dlg_cancel((LoadDlg*) data);
}

//Schließen (X, Esc) wirkt wie Abbrechen; das Fenster schließt der Aufrufer
static gboolean on_close_request(GtkWindow *window, gpointer data) {
	(void) window;

	dlg_cancel((LoadDlg*) data);

	return TRUE;
}

static GtkWidget* new_bar(void) {
	GtkWidget *bar = gtk_progress_bar_new();

	gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(bar), TRUE);

	return bar;
}

static void report_failed(GtkWindow *parent, GPtrArray *failed) {
	GString *text = g_string_new(NULL);
	GtkAlertDialog *alert = NULL;

	if (failed->len == 1)
		g_string_append_printf(text, "%s – Download gescheitert",
				(gchar const*) g_ptr_array_index(failed, 0));
	else {
		g_string_append_printf(text, "%u Dateien – Download gescheitert:\n",
				failed->len);
		for (guint i = 0; i < MIN(failed->len, 5); i++)
			g_string_append_printf(text, "\n%s",
					(gchar const*) g_ptr_array_index(failed, i));
		if (failed->len > 5)
			g_string_append_printf(text, "\n... und %u weitere", failed->len - 5);
	}

	alert = gtk_alert_dialog_new("SeaDrive");
	gtk_alert_dialog_set_detail(alert, text->str);
	gtk_alert_dialog_set_modal(alert, TRUE);
	gtk_alert_dialog_show(alert, parent);
	g_object_unref(alert);
	g_string_free(text, TRUE);
}

gboolean sond_seadrive_load_sync(GtkWindow *parent, GPtrArray *full_paths) {
	SondSeadriveLoadJob *job = sond_seadrive_load_job_new(full_paths);
	LoadDlg dlg = { 0 };
	GtkWidget *scrolled = NULL;
	GtkWidget *vbox = NULL;
	GtkWidget *outer = NULL;
	GPtrArray *failed = NULL;
	gboolean ok = FALSE;
	guint n = 0;

	if (!job)
		return TRUE; //nichts zu laden

	dlg.job = job;
	dlg.loop = g_main_loop_new(NULL, FALSE);
	n = sond_seadrive_load_job_n_entries(job);

	dlg.window = gtk_window_new();
	gtk_window_set_title(GTK_WINDOW(dlg.window), "Download läuft");
	gtk_window_set_modal(GTK_WINDOW(dlg.window), TRUE);
	if (parent)
		gtk_window_set_transient_for(GTK_WINDOW(dlg.window), parent);
	gtk_window_set_default_size(GTK_WINDOW(dlg.window), 460, -1);
	g_signal_connect(dlg.window, "close-request", G_CALLBACK(on_close_request), &dlg);

	outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_widget_set_margin_start(outer, 14);
	gtk_widget_set_margin_end(outer, 14);
	gtk_widget_set_margin_top(outer, 14);
	gtk_widget_set_margin_bottom(outer, 14);
	gtk_window_set_child(GTK_WINDOW(dlg.window), outer);

	//Bei vielen Dateien bleiben alle Zeilen einsehbar, ohne dass das Fenster wächst
	scrolled = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER,
			GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scrolled), 60);
	gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scrolled), 320);
	gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scrolled), TRUE);
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), vbox);
	gtk_box_append(GTK_BOX(outer), scrolled);

	if (sond_seadrive_load_job_is_aggregate(job)) {
		gchar *text = g_strdup_printf("%u Dateien werden geladen", n);

		dlg.agg_label = gtk_label_new(text);
		g_free(text);
		gtk_widget_set_halign(dlg.agg_label, GTK_ALIGN_START);
		gtk_box_append(GTK_BOX(vbox), dlg.agg_label);
		dlg.agg_bar = new_bar();
		gtk_box_append(GTK_BOX(vbox), dlg.agg_bar);
	} else {
		dlg.labels = g_ptr_array_new();
		dlg.bars = g_ptr_array_new();

		for (guint i = 0; i < n; i++) {
			gchar *base = g_path_get_basename(
					sond_seadrive_load_job_entry(job, i)->full_path);
			GtkWidget *label = gtk_label_new(base);
			GtkWidget *bar = new_bar();

			g_free(base);
			gtk_label_set_wrap(GTK_LABEL(label), TRUE);
			gtk_widget_set_halign(label, GTK_ALIGN_START);
			gtk_box_append(GTK_BOX(vbox), label);
			gtk_box_append(GTK_BOX(vbox), bar);
			g_ptr_array_add(dlg.labels, label);
			g_ptr_array_add(dlg.bars, bar);
		}
	}

	dlg.button = gtk_button_new_with_label("Abbrechen");
	gtk_widget_set_halign(dlg.button, GTK_ALIGN_END);
	g_signal_connect(dlg.button, "clicked", G_CALLBACK(on_cancel_clicked), &dlg);
	gtk_box_append(GTK_BOX(outer), dlg.button);

	dlg_update(&dlg);
	gtk_window_present(GTK_WINDOW(dlg.window));

	if (!sond_seadrive_load_job_all_done(job)) {
		dlg.timer = g_timeout_add(LOAD_TICK_MS, on_tick, &dlg);
		g_main_loop_run(dlg.loop);
	}

	if (dlg.timer)
		g_source_remove(dlg.timer);
	g_signal_handlers_disconnect_by_data(dlg.window, &dlg);
	gtk_window_destroy(GTK_WINDOW(dlg.window));
	g_main_loop_unref(dlg.loop);
	if (dlg.labels)
		g_ptr_array_free(dlg.labels, TRUE);
	if (dlg.bars)
		g_ptr_array_free(dlg.bars, TRUE);

	failed = sond_seadrive_load_job_take_failed(job);
	if (!sond_seadrive_load_job_cancelled(job) && failed->len > 0)
		report_failed(parent, failed);
	ok = !sond_seadrive_load_job_cancelled(job) && failed->len == 0;

	g_ptr_array_unref(failed);
	sond_seadrive_load_job_free(job);

	return ok;
}

#else /* !G_OS_WIN32 */

gboolean sond_seadrive_load_sync(GtkWindow *parent, GPtrArray *full_paths) {
	(void) parent;
	(void) full_paths;

	return TRUE;
}

#endif

gboolean sond_seadrive_load_file_sync(GtkWindow *parent, const gchar *full_path) {
	GPtrArray *arr = NULL;
	gboolean ok = FALSE;

	if (!full_path)
		return TRUE;

	arr = g_ptr_array_new();
	g_ptr_array_add(arr, (gpointer) full_path);
	ok = sond_seadrive_load_sync(parent, arr);
	g_ptr_array_free(arr, TRUE);

	return ok;
}
