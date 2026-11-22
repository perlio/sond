/*
 sond (gtk3/sond_dialoge.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_dialoge.h"

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>

#include "../misc_stdlib.h"
#include "../sond_log_and_error.h"

/** Zeigt Fenster, in dem Liste übergebener strings angezeigt wird.
 *   parent-window kann NULL sein, dann Warnung
 *   text1 darf nicht NULL sein
 *   Abschluß der Liste mit NULL
 */
static void my_dialog_run_response(GtkDialog *dialog, gint response,
		gpointer data) {
	*(gint*) data = response;
}

static gboolean my_dialog_run_delete(GtkWidget *dialog, GdkEvent *event,
		gpointer data) {
	gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
	return TRUE; /* verhindert automatisches Zerstören */
}

gint my_dialog_run(GtkDialog *dialog) {
	gint result = GTK_RESPONSE_NONE;
	GMainLoop *loop = g_main_loop_new(NULL, FALSE);

	g_signal_connect(dialog, "response",
			G_CALLBACK(my_dialog_run_response), &result);
	g_signal_connect_swapped(dialog, "response",
			G_CALLBACK(g_main_loop_quit), loop);
	g_signal_connect(dialog, "delete-event",
			G_CALLBACK(my_dialog_run_delete), NULL);

	gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
	gtk_widget_show_all(GTK_WIDGET(dialog));
	gtk_grab_add(GTK_WIDGET(dialog));

	g_main_loop_run(loop);

	gtk_grab_remove(GTK_WIDGET(dialog));
	g_main_loop_unref(loop);

	return result;
}

void display_message(GtkWidget *window, ...) {
	va_list ap;
	gchar *message = NULL;
	const gchar *str = NULL;

	va_start(ap, window);
	while ((str = va_arg(ap, const gchar*)))
		message = add_string(message, g_strdup(str));

	va_end(ap);

	GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
			GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
			"%s", message);
	g_free(message);

	my_dialog_run(GTK_DIALOG(dialog));

	gtk_widget_destroy(dialog);

	return;
}

static void cb_entry_text(GtkEntry *entry, gpointer data) {
	GtkWidget *dialog = NULL;

	dialog = GTK_WIDGET(data);

	gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_YES);

	return;
}

/** ... response_id, next_button_text, next_response_id, ..., NULL
 **/
gint dialog_with_buttons(GtkWidget *window, const gchar *message,
		const gchar *secondary, gchar **text, ...) {
	gint res;
	GtkWidget *entry = NULL;
	va_list arg_pointer;
	const gchar *button_text = NULL;

	GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
			GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_QUESTION,
			GTK_BUTTONS_NONE, "%s", message);
	gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s",
			secondary);

	//buttons einfügen
	va_start(arg_pointer, text);

	while ((button_text = va_arg(arg_pointer, const gchar*))) {
		gint response_id = 0;

		response_id = va_arg(arg_pointer, gint);

		gtk_dialog_add_button(GTK_DIALOG(dialog), button_text, response_id);
	}

	va_end(arg_pointer);

	if (text) {
		GtkWidget *content = gtk_message_dialog_get_message_area(
				GTK_MESSAGE_DIALOG(dialog));
		entry = gtk_entry_new();
		gtk_container_add(GTK_CONTAINER(content), entry);
		if (*text)
			gtk_entry_set_text(GTK_ENTRY(entry), *text);
		g_free(*text);
		*text = NULL;

		gtk_widget_show_all(content);

		g_signal_connect(entry, "activate", G_CALLBACK(cb_entry_text),
				(gpointer ) dialog);
	}

	res = my_dialog_run(GTK_DIALOG(dialog));

	if (text)
		*text = g_strdup(gtk_entry_get_text( GTK_ENTRY(entry) ));

	gtk_widget_destroy(dialog);

	return res;
}

/** wrapper
 **/
gint abfrage_frage(GtkWidget *window, const gchar *message,
		const gchar *secondary, gchar **text) {
	gint res;

	res = dialog_with_buttons(window, message, secondary, text, "Ja",
			GTK_RESPONSE_YES, "Nein", GTK_RESPONSE_NO, NULL);

	return res;
}

static gchar*
choose_file(const GtkWidget *window, const gchar *path,
		const gchar *title_text, gchar const* accept_text, gint action,
		const gchar *ext) {
	GtkWidget *dialog = NULL;
	gint rc = 0;
	gchar *dir_start = NULL;
	gchar* filename = NULL;

	dialog = gtk_file_chooser_dialog_new(title_text, GTK_WINDOW(window), action,
			"_Abbrechen", GTK_RESPONSE_CANCEL, accept_text, GTK_RESPONSE_ACCEPT,
			NULL);

	if (!path || !g_strcmp0(path, ""))
		dir_start = g_get_current_dir();
	else
		dir_start = g_strdup(path);

	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), dir_start);
	g_free(dir_start);
	gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), FALSE);
	gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog),
	TRUE);
	if (action == GTK_FILE_CHOOSER_ACTION_SAVE && ext)
		gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), ext);

	rc = my_dialog_run(GTK_DIALOG(dialog));
	if (rc == GTK_RESPONSE_ACCEPT) {
		filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		//NULL z.B. bei nicht lokaler Auswahl
		for (gchar *p = filename; p && *p; p++) {
		    if (*p == '\\') *p = '/';
		}
	}

	//Dialog schließen
	gtk_widget_destroy(dialog);

	return filename;
}

gchar*
filename_speichern(GtkWindow *window, const gchar *titel, const gchar *ext) {
	gchar* filename = NULL;

	filename = choose_file(GTK_WIDGET(window), NULL, titel, "Speichern",
			GTK_FILE_CHOOSER_ACTION_SAVE, ext);

	if (!filename)
		return NULL;

	return filename; //muß g_freed werden
}

/* Öffnet den Dateiauswahldialog. start_path (kann NULL sein) ist der Ordner,
 * in dem er initial öffnet. Ohne expliziten Pfad fällt choose_file() auf
 * g_get_current_dir() zurück, und das Arbeitsverzeichnis des Prozesses ist
 * nach dem Öffnen eines Projekts das SeaDrive-Projektverzeichnis
 * (g_chdir() in sond_treeviewfm_set_root(), root!=NULL-Zweig;
 * project_close() setzt es nicht zurück). Der GtkFileChooserDialog müsste
 * dann das potentiell riesige Projektverzeichnis (alle Fallakten-Dateien)
 * einlesen, um seine Dateiliste zu füllen - der Cloud-Filtertreiber ist pro
 * Datei langsam, und das liegt in GTKs eigenem Dialog, nicht in unserem
 * Code. project_load() übergibt deshalb das Elternverzeichnis des zuletzt
 * geöffneten Projekts (typischerweise nur eine Handvoll Fallakten-Ordner,
 * nicht deren Inhalt). */
gchar*
filename_oeffnen(GtkWindow *window, const gchar *start_path) {
	gchar* filename = NULL;

	filename = choose_file(GTK_WIDGET(window), start_path, "Datei auswählen",
			"Öffnen", GTK_FILE_CHOOSER_ACTION_OPEN, NULL);

	if (!filename)
		return NULL;

	return filename; //muß g_freed werden
}

GtkWidget*
result_listbox_new(GtkWindow *parent_window, const gchar *titel) {
	GtkWidget *window = NULL;
	GtkWidget *vbox = NULL;
	GtkWidget *header_box = NULL;
	GtkWidget *scrolled_window = NULL;
	GtkWidget *listbox = NULL;
	GtkWidget *headerbar = NULL;

	//Fenster erzeugen
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(window), 800, 350);
	if (parent_window) {
		gtk_window_set_transient_for(GTK_WINDOW(window), parent_window);
		gtk_window_set_modal(GTK_WINDOW(window), FALSE);
	}

	/* vbox: header_box (für optionale Tabellen-Spaltenüberschriften - bleibt
	 * beim Scrollen der Liste fest stehen, da außerhalb von scrolled_window)
	 * oberhalb der scrollbaren Listbox. header_box bleibt hier leer - der
	 * Aufrufer füllt sie bei Bedarf selbst (s. Objekt-Daten "header-box"). */
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

	scrolled_window = gtk_scrolled_window_new( NULL, NULL);
	listbox = gtk_list_box_new();
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(listbox), GTK_SELECTION_MULTIPLE);
	gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(listbox), FALSE);

	gtk_container_add(GTK_CONTAINER(scrolled_window), listbox);

	gtk_box_pack_start(GTK_BOX(vbox), header_box, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), scrolled_window, TRUE, TRUE, 0);

	gtk_container_add(GTK_CONTAINER(window), vbox);

	//Headerbar erzeugen
	headerbar = gtk_header_bar_new();
	gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(headerbar),
			":minimize,close");
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
	gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), titel);
	gtk_window_set_titlebar(GTK_WINDOW(window), headerbar);

	g_object_set_data(G_OBJECT(window), "listbox", listbox);
	g_object_set_data(G_OBJECT(window), "headerbar", headerbar);
	g_object_set_data(G_OBJECT(window), "header-box", header_box);

	return window;
}
