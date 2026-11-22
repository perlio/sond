/*
 sond (gtk3/sond_info_window.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2023  pelo america

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

#include "sond_info_window.h"

#include "sond_dialoge.h"

#include <gtk/gtk.h>
#include <string.h>

void info_window_kill(InfoWindow *info_window) {
	gtk_widget_destroy(info_window->dialog);

	g_free(info_window);

	return;
}

void info_window_close(InfoWindow *info_window) {
	GtkWidget *button = g_object_get_data(
			G_OBJECT(info_window->dialog), "abbrechen-button");
	gtk_button_set_label(GTK_BUTTON(button), "Schließen");
	gtk_widget_grab_focus(button);

	my_dialog_run(GTK_DIALOG(info_window->dialog));

	info_window_kill(info_window);

	return;
}

void info_window_set_progress_bar_fraction(InfoWindow *info_window,
		gdouble fraction) {
	gtk_progress_bar_set_fraction(
			GTK_PROGRESS_BAR(info_window->progress_bar), fraction);

	while (gtk_events_pending())
		gtk_main_iteration();

	return;
}

/* Scrollt ans Textende: ein GtkTextMark mit left_gravity=FALSE am Textende
 * bleibt automatisch am Ende, während Text eingefügt wird (s. end_mark in
 * InfoWindow) - gtk_text_view_scroll_mark_onscreen() dafür ist der
 * vorgesehene, robuste GTK-Weg (kein Warten auf einen Layout-Durchlauf). */
static void info_window_scroll(InfoWindow *info_window) {
	gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(info_window->text_view),
			info_window->end_mark);
	return;
}

void info_window_set_progress_bar(InfoWindow *info_window) {
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(info_window->progress_bar), 0.0);
	gtk_widget_show(info_window->progress_bar);

	while (gtk_events_pending())
		gtk_main_iteration();

	return;
}

void info_window_display_progress(InfoWindow *info_window, gint progress) {
	if (progress == -1)
		info_window_set_progress_bar(info_window);
	else
		info_window_set_progress_bar_fraction(info_window,
				((gdouble) progress) / 100.);

	return;
}

void info_window_set_message(InfoWindow *info_window, const gchar *format, ...) {
	va_list args;
	gchar *message = NULL;
	GtkTextBuffer *buffer = NULL;
	GtkTextIter end_iter;

	va_start(args, format);
	message = g_strdup_vprintf(format, args);
	va_end(args);

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(info_window->text_view));
	gtk_text_buffer_get_end_iter(buffer, &end_iter);
	gtk_text_buffer_insert(buffer, &end_iter, message, -1);
	gtk_text_buffer_insert(buffer, &end_iter, "\n", -1);
	g_free(message);

	info_window_scroll(info_window);

	return;
}

typedef struct {
    InfoWindow *info_window;
    gchar *message;
} MessageData;

static gboolean show_message_main(gpointer data) {
    MessageData *md = (MessageData*) data;
	GtkTextBuffer *buffer = NULL;
	GtkTextIter end_iter;

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(md->info_window->text_view));
	gtk_text_buffer_get_end_iter(buffer, &end_iter);
	gtk_text_buffer_insert(buffer, &end_iter, md->message, -1);
	gtk_text_buffer_insert(buffer, &end_iter, "\n", -1);
    g_free(md->message);

	info_window_scroll(md->info_window);
    g_free(md);

    return G_SOURCE_REMOVE;
}

// Von jedem Thread aus aufrufbar (auch vom Hauptthread):
void info_window_set_message_thread_safe(InfoWindow *info_window, const gchar *format, ...) {
    MessageData *md = g_new(MessageData, 1);
    md->info_window = info_window;

    va_list args;
    va_start(args, format);
    md->message = g_strdup_vprintf(format, args);
    va_end(args);

    /* Immer über g_idle_add() einreihen - auch bei Aufruf vom Hauptthread
     * aus. So laufen ALLE Meldungen (gleich ob Haupt- oder Worker-Thread)
     * durch dieselbe FIFO-Queue und erscheinen automatisch in der
     * richtigen zeitlichen Reihenfolge (GLib-Garantie: idle-Quellen werden
     * in Einreihungsreihenfolge abgearbeitet) - ohne dass hierfür ein
     * rekursiver gtk_main_iteration()-Aufruf nötig wäre. Letzterer war
     * die vermutete Ursache eines seltenen, schwer reproduzierbaren
     * Absturzes: er pumpte die Hauptschleife verschachtelt, während
     * z.B. zond_index_erstellen_ht() bereits selbst mitten in einer
     * eigenen gtk_main_iteration_do()-Pump-Schleife steckte. */
    g_idle_add(show_message_main, md);

    return;
}

static void cb_abbrechen_clicked(GtkButton *button, gpointer data) {
	gtk_dialog_response(GTK_DIALOG(data), GTK_RESPONSE_CANCEL);
	return;
}

static void cb_info_window_response(GtkDialog *dialog, gint id, gpointer data) {
	InfoWindow *info_window = (InfoWindow*) data;

	if (g_atomic_int_get(info_window->cancel))
		return;

	info_window_set_message(info_window, "...abgebrochen");
	g_atomic_int_set(info_window->cancel, 1);

	return;
}

/* X-Button (Fenster schließen) während der Hintergrund-Thread noch läuft:
 * ohne diesen Handler übersetzt GTK ein delete-event NICHT automatisch in
 * ein "response"-Signal - das passiert nur innerhalb von gtk_dialog_run(),
 * welches hier während der laufenden Verarbeitung bewusst nicht verwendet
 * wird (siehe zond_index_erstellen_ht/eigene Poll-Schleife statt
 * modalem Run). Ohne Behandlung würde GTKs Default-Verhalten das Fenster
 * (info_window->dialog) sofort zerstören, während der Hintergrund-Thread
 * noch aktiv Widgets darin nachträgt (info_window_set_message_thread_safe)
 * - Zugriff auf bereits zerstörte Widgets. Deshalb: wie Abbrechen
 * behandeln (cancel setzen), aber TRUE zurückgeben, damit GTK das Fenster
 * NICHT zerstört - info_window_close() (aufgerufen, sobald der
 * Hintergrund-Thread wirklich fertig ist) schließt es dann sauber über
 * my_dialog_run(), wo die genannte Übersetzung wieder normal greift. */
static gboolean cb_info_window_delete_event(GtkWidget *widget, GdkEvent *event,
		gpointer data) {
	InfoWindow *info_window = (InfoWindow*) data;

	if (!g_atomic_int_get(info_window->cancel)) {
		info_window_set_message(info_window, "...abgebrochen");
		g_atomic_int_set(info_window->cancel, 1);
	}

	return TRUE; //Event konsumiert - Fenster nicht zerstören
}

InfoWindow*
info_window_open(GtkWidget *window, gint* cancel, const gchar *title) {
	GtkWidget *content = NULL;
	GtkWidget *swindow = NULL;
	GtkWidget *bottom_box = NULL;
	GtkWidget *button = NULL;
	GtkTextBuffer *buffer = NULL;
	GtkTextIter end_iter;

	InfoWindow *info_window = g_malloc0(sizeof(InfoWindow));

	info_window->cancel = cancel;

	info_window->dialog = gtk_dialog_new_with_buttons(title, GTK_WINDOW(window),
			GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL, NULL, NULL);

	gtk_window_set_default_size(GTK_WINDOW(info_window->dialog), 900, 190);

	content = gtk_dialog_get_content_area(GTK_DIALOG(info_window->dialog));
	swindow = gtk_scrolled_window_new( NULL, NULL);
	gtk_box_pack_start(GTK_BOX(content), swindow, TRUE, TRUE, 0);

	/* Ein einzelnes GtkTextView statt eines GtkLabel pro Nachricht (s.
	 * Kommentar an InfoWindow.text_view in sond_info_window.h) - Text anhängen bleibt auch
	 * bei sehr vielen Zeilen (z.B. eine pro Datei beim Anbinden) günstig. */
	info_window->text_view = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(info_window->text_view), FALSE);
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(info_window->text_view), FALSE);
	gtk_container_add(GTK_CONTAINER(swindow), info_window->text_view);

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(info_window->text_view));
	gtk_text_buffer_get_end_iter(buffer, &end_iter);
	/* left_gravity = FALSE: Mark bleibt am Textende, während neuer Text
	 * eingefügt wird - Grundlage für info_window_scroll(). */
	info_window->end_mark = gtk_text_buffer_create_mark(buffer, NULL,
			&end_iter, FALSE);

	/* Untere Leiste: Progress-Bar + Abbrechen-Button nebeneinander, nicht scrollbar */
	bottom_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_margin_start(bottom_box, 6);
	gtk_widget_set_margin_end(bottom_box, 6);
	gtk_widget_set_margin_top(bottom_box, 4);
	gtk_widget_set_margin_bottom(bottom_box, 4);
	gtk_box_pack_end(GTK_BOX(content), bottom_box, FALSE, FALSE, 0);

	info_window->progress_bar = gtk_progress_bar_new();
	gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(info_window->progress_bar), TRUE);
	gtk_widget_set_valign(info_window->progress_bar, GTK_ALIGN_CENTER);
	gtk_widget_set_no_show_all(info_window->progress_bar, TRUE);
	gtk_box_pack_start(GTK_BOX(bottom_box), info_window->progress_bar, TRUE, TRUE, 0);

	button = gtk_button_new_with_label("Abbrechen");
	gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
	gtk_box_pack_end(GTK_BOX(bottom_box), button, FALSE, FALSE, 0);
	g_signal_connect(button, "clicked",
			G_CALLBACK(cb_abbrechen_clicked),
			info_window->dialog);
	g_object_set_data(G_OBJECT(info_window->dialog), "abbrechen-button", button);

	gtk_widget_show_all(info_window->dialog);
	while (gtk_events_pending())
		gtk_main_iteration();

	g_signal_connect(GTK_DIALOG(info_window->dialog), "response",
			G_CALLBACK(cb_info_window_response), info_window);
	g_signal_connect(GTK_DIALOG(info_window->dialog), "delete-event",
			G_CALLBACK(cb_info_window_delete_event), info_window);

	return info_window;
}


