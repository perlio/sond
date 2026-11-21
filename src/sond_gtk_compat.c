/*
 sond (sond_gtk_compat.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_gtk_compat.h"

#ifdef SOND_GTK4

void sond_compat_message(GtkWindow *parent, gchar const *text) {
	GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", text ? text : "");

	gtk_alert_dialog_show(dialog, parent);
	g_object_unref(dialog);
}

typedef struct {
	SondCompatSaveFunc func;
	gpointer data;
} SaveCtx;

static void save_finished(GObject *source, GAsyncResult *res, gpointer p) {
	SaveCtx *ctx = p;
	GFile *file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), res, NULL);
	gchar *path = file ? g_file_get_path(file) : NULL;

	if (file)
		g_object_unref(file);
	ctx->func(path, ctx->data);
	g_free(ctx);
}

void sond_compat_save_file(GtkWindow *parent, gchar const *title, gchar const *initial_name,
		SondCompatSaveFunc func, gpointer data) {
	GtkFileDialog *dialog = gtk_file_dialog_new();
	SaveCtx *ctx = g_new(SaveCtx, 1);

	ctx->func = func;
	ctx->data = data;
	gtk_file_dialog_set_title(dialog, title);
	if (initial_name)
		gtk_file_dialog_set_initial_name(dialog, initial_name);
	gtk_file_dialog_save(dialog, parent, NULL, save_finished, ctx);
	g_object_unref(dialog);
}

#else

void sond_compat_message(GtkWindow *parent, gchar const *text) {
	GtkWidget *dialog = gtk_message_dialog_new(parent, GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "%s", text ? text : "");

	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}

void sond_compat_save_file(GtkWindow *parent, gchar const *title, gchar const *initial_name,
		SondCompatSaveFunc func, gpointer data) {
	GtkFileChooserNative *dialog = gtk_file_chooser_native_new(title, parent,
			GTK_FILE_CHOOSER_ACTION_SAVE, "Speichern", "Abbrechen");
	gchar *path = NULL;

	gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
	if (initial_name)
		gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), initial_name);
	if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
		path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
	g_object_unref(dialog);

	func(path, data);
}

#endif
