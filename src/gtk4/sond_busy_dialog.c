/*
 sond (gtk4/sond_busy_dialog.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_busy_dialog.h"

#define BUSY_PULSE_MS 120

struct _SondBusyDialog {
	GtkWidget *window;
	GtkWidget *label;
	GtkWidget *bar;
	GtkWidget *button;
	gint own_cancelled;
	gint *cancelled;
	gboolean pulsing;
	guint timer;
};

static void busy_set_cancelled(SondBusyDialog *dlg) {
	g_atomic_int_set(dlg->cancelled, 1);
	gtk_widget_set_sensitive(dlg->button, FALSE);
	gtk_button_set_label(GTK_BUTTON(dlg->button), "Wird abgebrochen …");
}

static void on_cancel_clicked(GtkButton *button, gpointer data) {
	(void) button;

	busy_set_cancelled((SondBusyDialog*) data);
}

//Schließen bedeutet Abbrechen; das Fenster schließt der Aufrufer
static gboolean on_close_request(GtkWindow *window, gpointer data) {
	(void) window;

	busy_set_cancelled((SondBusyDialog*) data);

	return TRUE;
}

static gboolean on_pulse(gpointer data) {
	SondBusyDialog *dlg = (SondBusyDialog*) data;

	if (dlg->pulsing)
		gtk_progress_bar_pulse(GTK_PROGRESS_BAR(dlg->bar));

	return G_SOURCE_CONTINUE;
}

SondBusyDialog* sond_busy_dialog_new(GtkWindow *parent, gchar const *title,
		gchar const *text, gint *cancelled) {
	SondBusyDialog *dlg = g_new0(SondBusyDialog, 1);
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

	dlg->cancelled = cancelled ? cancelled : &dlg->own_cancelled;
	dlg->pulsing = TRUE;

	dlg->window = gtk_window_new();
	gtk_window_set_title(GTK_WINDOW(dlg->window), title ? title : "");
	gtk_window_set_modal(GTK_WINDOW(dlg->window), TRUE);
	if (parent)
		gtk_window_set_transient_for(GTK_WINDOW(dlg->window), parent);
	gtk_window_set_resizable(GTK_WINDOW(dlg->window), FALSE);
	g_signal_connect(dlg->window, "close-request", G_CALLBACK(on_close_request), dlg);

	gtk_widget_set_margin_start(box, 16);
	gtk_widget_set_margin_end(box, 16);
	gtk_widget_set_margin_top(box, 16);
	gtk_widget_set_margin_bottom(box, 16);
	gtk_window_set_child(GTK_WINDOW(dlg->window), box);

	dlg->label = gtk_label_new(text ? text : "");
	gtk_label_set_xalign(GTK_LABEL(dlg->label), 0.0);
	gtk_label_set_ellipsize(GTK_LABEL(dlg->label), PANGO_ELLIPSIZE_MIDDLE);
	gtk_label_set_max_width_chars(GTK_LABEL(dlg->label), 60);
	gtk_widget_set_size_request(dlg->label, 360, -1);
	gtk_box_append(GTK_BOX(box), dlg->label);

	dlg->bar = gtk_progress_bar_new();
	gtk_box_append(GTK_BOX(box), dlg->bar);

	dlg->button = gtk_button_new_with_label("Abbrechen");
	gtk_widget_set_halign(dlg->button, GTK_ALIGN_END);
	g_signal_connect(dlg->button, "clicked", G_CALLBACK(on_cancel_clicked), dlg);
	gtk_box_append(GTK_BOX(box), dlg->button);

	dlg->timer = g_timeout_add(BUSY_PULSE_MS, on_pulse, dlg);
	gtk_window_present(GTK_WINDOW(dlg->window));

	return dlg;
}

void sond_busy_dialog_set_text(SondBusyDialog *dlg, gchar const *text) {
	g_return_if_fail(dlg);

	gtk_label_set_text(GTK_LABEL(dlg->label), text ? text : "");
}

void sond_busy_dialog_set_fraction(SondBusyDialog *dlg, gdouble fraction) {
	g_return_if_fail(dlg);

	if (fraction < 0.0) {
		dlg->pulsing = TRUE;

		return;
	}

	dlg->pulsing = FALSE;
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(dlg->bar), MIN(fraction, 1.0));
}

gboolean sond_busy_dialog_is_cancelled(SondBusyDialog *dlg) {
	g_return_val_if_fail(dlg, FALSE);

	return g_atomic_int_get(dlg->cancelled) != 0;
}

void sond_busy_dialog_close(SondBusyDialog *dlg) {
	if (!dlg)
		return;

	if (dlg->timer)
		g_source_remove(dlg->timer);
	g_signal_handlers_disconnect_by_data(dlg->window, dlg);
	gtk_window_destroy(GTK_WINDOW(dlg->window));
	g_free(dlg);
}
