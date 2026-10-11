/*
 zond (password_dialog.c) - Akten, Beweisstücke, Unterlagen
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

#include "../zond_init.h"

#include "password_dialog.h"

gchar* zond_password_dialog(gchar const *what, gboolean wrong,
		gpointer user_data) {
	Projekt *zond = (Projekt*) user_data;
	GtkWidget *dialog = NULL;
	GtkWidget *content = NULL;
	GtkWidget *label = NULL;
	GtkWidget *entry = NULL;
	gchar *password = NULL;

	dialog = gtk_dialog_new_with_buttons("Passwort",
			(zond && zond->app_window) ? GTK_WINDOW(zond->app_window) : NULL,
			GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
			"Abbrechen", GTK_RESPONSE_CANCEL, "OK", GTK_RESPONSE_OK, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

	content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	gtk_box_set_spacing(GTK_BOX(content), 6);
	gtk_container_set_border_width(GTK_CONTAINER(content), 12);

	label = gtk_label_new(what);
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 0);

	if (wrong) {
		label = gtk_label_new("Das Passwort ist falsch.");
		gtk_label_set_xalign(GTK_LABEL(label), 0.0);
		gtk_widget_set_name(label, "passwort-falsch");
		gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 0);
	}

	entry = gtk_entry_new();
	gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE); //maskiert
	gtk_entry_set_input_purpose(GTK_ENTRY(entry), GTK_INPUT_PURPOSE_PASSWORD);
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_widget_set_size_request(entry, 320, -1);
	gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 0);

	gtk_widget_show_all(dialog);
	gtk_widget_grab_focus(entry);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
		gchar const *text = gtk_entry_get_text(GTK_ENTRY(entry));

		if (text && *text)
			password = g_strdup(text);
	}

	gtk_entry_set_text(GTK_ENTRY(entry), ""); //Eingabepuffer leeren
	gtk_widget_destroy(dialog);

	return password;
}
