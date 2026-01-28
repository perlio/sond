/*
 zond (main.c) - Akten, Beweisstücke, Unterlagen
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

#include <gtk/gtk.h>

#include "../sond_log_and_error.h"
#include "zond_init.h"
#include "20allgemein/project.h"

/**
 * Callback: Application startup
 *
 * Wird einmal beim Start der Anwendung aufgerufen.
 * Hier werden einmalige Initialisierungen vorgenommen.
 */
static void cb_startup(GApplication *app, gpointer user_data) {
    Projekt *zond = (Projekt*) user_data;

	zond_init(GTK_APPLICATION(app), zond);

	LOG_INFO("zond gestartet");

	return;
}

/**
 * Callback: Application activate
 *
 * Wird aufgerufen wenn die Anwendung aktiviert wird.
 * Beim ersten Start oder wenn die Anwendung erneut gestartet wird
 * während sie bereits läuft.
 */
static void cb_activate(GApplication *app, gpointer user_data) {
    Projekt *zond = (Projekt*) user_data;

	gtk_window_present(GTK_WINDOW(zond->app_window));

	return;
}

/**
 * Callback: Application open
 *
 * Wird aufgerufen wenn Dateien beim Start übergeben werden.
 * Z.B. beim Doppelklick auf eine .zond Datei.
 */
static void cb_open(GApplication *app, GFile **files, gint n_files,
                    const gchar *hint, gpointer user_data) {
	gint rc = 0;
	gchar *errmsg = NULL;
	GFile **g_file;
	gchar *uri = NULL;
	gchar *uri_unesc = NULL;

    Projekt *zond = (Projekt*) user_data;

	g_file = (GFile**) files;

	uri = g_file_get_uri(g_file[0]);
	uri_unesc = g_uri_unescape_string(uri, NULL);
	g_free(uri);

	rc = project_open(zond, uri_unesc + 8, FALSE, &errmsg);
	g_free(uri_unesc);
	if (rc == -1) {
		LOG_INFO("Fehler - Projekt kann nicht geöffnet werden: %s", errmsg);
		g_free(errmsg);
	}

	return;
}

static void cb_shutdown(GApplication *app, gpointer user_data) {
    Projekt *zond = (Projekt*) user_data;

    LOG_INFO("Application wird beendet - Cleanup läuft...");
    zond_cleanup(zond);
}

/**
 * Haupteinstiegspunkt der Anwendung
 */
int main(int argc, char **argv) {
	GtkApplication *app = NULL;
	Projekt zond = { 0 };
	gint status = 0;

	//ApplicationApp erzeugen
	app = gtk_application_new("de.perlio.zond", G_APPLICATION_HANDLES_OPEN);

    // Callbacks registrieren
    g_signal_connect(app, "startup", G_CALLBACK(cb_startup), &zond);
    g_signal_connect(app, "activate", G_CALLBACK(cb_activate), &zond);
    g_signal_connect(app, "open", G_CALLBACK(cb_open), &zond);
    g_signal_connect(app, "shutdown", G_CALLBACK(cb_shutdown), &zond);

	logging_init("zond");

	status = g_application_run(G_APPLICATION(app), argc, argv);

	g_object_unref(app);

	LOG_INFO("zond beendet");
	logging_cleanup();

	return status;
}
