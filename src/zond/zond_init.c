/*
 zond (main.c) - Akten, Beweisstücke, Unterlagen
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

//#include <stdio.h>
#include "locale.h"
#include <unistd.h>
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <sqlite3.h>

#ifdef __WIN32
#include <libloaderapi.h>
#include <errhandlingapi.h>
#include <gdk/gdkwin32.h>
#endif // __WIN32

#include "../gtk3/sond_dialoge.h"
#include "../misc_stdlib.h"
#include "../sond_fileparts.h"
#include "../sond_log_and_error.h"
#include "../sond_file_helper.h"
/* sond_chat.h bewußt nicht mehr eingebunden - Chat-Funktion fürs Release
 * abgeklemmt (siehe Makefile-Kommentar, Ziel "zond"). zond->app.chat_ctx bleibt
 * dadurch immer NULL (nichts setzt ihn mehr), daher hier auch kein
 * Freigeben mehr nötig. */

#include "zond_init.h"
#include "zond_convert.h"
#include "../viewer/zond_pdf_document.h"
#include "viewer_host.h"
#include "../viewer/viewer_session.h"

#include "20allgemein/project.h"

#include "../icons.h"
#include "10init/app_window.h"
#include "20allgemein/zond_update.h"

/* Rückgabe TRUE: Merker project/speichern in den Settings müssen erhalten
 * bleiben (Abbruch oder Fehler), damit die Wiederherstellung nicht verloren
 * geht. Unabhängig davon prüft project_open() auf eine vorhandene
 * Arbeitskopie. */
static gboolean recover(Projekt *zond, gchar *project, GApplication *app) {
	gchar *path_tmp = NULL;
	GError* error = NULL;
	gboolean behalten = FALSE;

	gchar *text_abfrage = g_strconcat("Projekt ", project,
			" wurde nicht richtig "
					"geschlossen. Wiederherstellen?", NULL);
	gint res = abfrage_frage(zond->win.app_window, "Wiederherstellen", text_abfrage,
			NULL);
	g_free(text_abfrage);

	if (res != GTK_RESPONSE_YES && res != GTK_RESPONSE_NO) {
		//[X]/Esc: beenden, Merker behalten (g_application_quit() kehrt zurück)
		g_application_quit(app);

		return TRUE;
	}

	/* Bewußt NICHT mehr <project>.tmp - s. Kommentar bei
	 * project_get_local_tmp_path(). Derselbe project-Pfad wie beim
	 * ursprünglichen Anlegen (project_create_dbase_zond()) liefert
	 * deterministisch denselben lokalen Pfad zurück. */
	path_tmp = project_get_local_tmp_path(project, &error);
	if (!path_tmp) {
		display_message(zond->win.app_window,
				"Konnte Pfad der temporären Datenbank nicht ermitteln: ",
				error->message, NULL);
		g_clear_error(&error);
		behalten = TRUE;
	} else if (!g_file_test(path_tmp, G_FILE_TEST_EXISTS)) {
		//vor jeder Umbenennung prüfen: sonst wäre die Projektdatei weg
		if (res == GTK_RESPONSE_YES)
			display_message(zond->win.app_window,
					"Wiederherstellung nicht möglich - Arbeitskopie nicht "
					"gefunden: ", path_tmp, NULL);
	} else if (res == GTK_RESPONSE_YES) {
		if (!project_arbeitskopie_uebernehmen(project, path_tmp, &error)) {
			display_message(zond->win.app_window,
					"Wiederherstellung fehlgeschlagen: ", error->message, NULL);
			g_clear_error(&error);
			behalten = TRUE;
		} else
			display_message(zond->win.app_window, project,
					" erfolgreich wiederhergestellt", NULL);
	} else { //Nein: Arbeitskopie aufbewahren, nicht liegen lassen
		gchar *path_neu = NULL;

		if (!project_arbeitskopie_sichern(project, path_tmp, &path_neu, &error)) {
			display_message(zond->win.app_window,
					"Arbeitskopie nicht sicherbar: ", error->message, NULL);
			g_clear_error(&error);
			behalten = TRUE;
		} else {
			display_message(zond->win.app_window, "Arbeitskopie gesichert als\n",
					path_neu, NULL);
			g_free(path_neu);
		}
	}
	g_free(path_tmp);

	if (!behalten) {
		g_settings_set_string(zond->app.settings, "project", "");
		g_settings_set_boolean(zond->app.settings, "speichern", FALSE);
	}

	return behalten;
}


static void set_icon(Icon *icon, const gchar *icon_name,
		const gchar *display_name) {
	icon->icon_name = icon_name;
	icon->display_name = display_name;

	return;
}

static void init_icons(Projekt *zond) {
	GResource *resource = icons_get_resource();
	g_resources_register(resource);

	GtkIconTheme *icon_theme = gtk_icon_theme_get_default();
	gtk_icon_theme_add_resource_path(icon_theme, "/icons");

//    zond->app.icon[ICON_NOTHING] = { "dialog-error", "Nix" };
	set_icon(&zond->app.icon[ICON_NOTHING], "dialog-error", "Nix");
	set_icon(&zond->app.icon[ICON_NORMAL], "media-record", "Punkt");
	set_icon(&zond->app.icon[ICON_ORDNER], "folder", "Ordner");
	set_icon(&zond->app.icon[ICON_PDF_FOLDER], "pdf-folder", "PDF-Datei");
	set_icon(&zond->app.icon[ICON_DATEI], "document-open", "Datei");
	set_icon(&zond->app.icon[ICON_PDF], "pdf", "PDF");
	set_icon(&zond->app.icon[ICON_ANBINDUNG], "anbindung", "Anbindung");
	set_icon(&zond->app.icon[ICON_AKTE], "akte", "Akte");
	set_icon(&zond->app.icon[ICON_EXE], "application-x-executable", "Ausführbar");
	set_icon(&zond->app.icon[ICON_TEXT], "text-x-generic", "Text");
	set_icon(&zond->app.icon[ICON_DOC], "x-office-document", "Writer/Word");
	set_icon(&zond->app.icon[ICON_PPP], "x-office-presentation", "PowerPoint");
	set_icon(&zond->app.icon[ICON_SPREAD], "x-office-spreadsheet", "Tabelle");
	set_icon(&zond->app.icon[ICON_IMAGE], "emblem-photos", "Bild");
	set_icon(&zond->app.icon[ICON_VIDEO], "video-x-generic", "Video");
	set_icon(&zond->app.icon[ICON_AUDIO], "audio-x-generic", "Audio");
	set_icon(&zond->app.icon[ICON_EMAIL], "mail-unread", "E-Mail");
	set_icon(&zond->app.icon[ICON_HTML], "emblem-web", "HTML"); //16

	//Platzhalter
	set_icon(&zond->app.icon[17], "process-stop", "Frei");
	set_icon(&zond->app.icon[18], "process-stop", "Frei");
	set_icon(&zond->app.icon[19], "process-stop", "Frei");
	set_icon(&zond->app.icon[20], "process-stop", "Frei");
	set_icon(&zond->app.icon[21], "process-stop", "Frei");
	set_icon(&zond->app.icon[22], "process-stop", "Frei");
	set_icon(&zond->app.icon[23], "process-stop", "Frei");
	set_icon(&zond->app.icon[24], "process-stop", "Frei");

	set_icon(&zond->app.icon[ICON_DURCHS], "system-log-out", "Durchsuchung");
	set_icon(&zond->app.icon[ICON_ORT], "mark-location", "Ort");
	set_icon(&zond->app.icon[ICON_PHONE], "phone", "TKÜ");
	set_icon(&zond->app.icon[ICON_WICHTIG], "emblem-important", "Wichtig");
	set_icon(&zond->app.icon[ICON_OBS], "camera-web", "Observation");
	set_icon(&zond->app.icon[ICON_CD], "media-optical", "CD");
	set_icon(&zond->app.icon[ICON_PERSON], "user-info", "Person");
	set_icon(&zond->app.icon[ICON_PERSONEN], "system-users", "Personen");
	set_icon(&zond->app.icon[ICON_ORANGE], "orange", "Orange");
	set_icon(&zond->app.icon[ICON_BLAU], "blau", "Blau");
	set_icon(&zond->app.icon[ICON_ROT], "rot", "Rot");
	set_icon(&zond->app.icon[ICON_GRUEN], "gruen", "Grün");
	set_icon(&zond->app.icon[ICON_TUERKIS], "tuerkis", "Türkis");
	set_icon(&zond->app.icon[ICON_MAGENTA], "magenta", "Magenta");

	return;
}

static void init_schema(Projekt* zond) {
    GSettingsSchemaSource *source;
    GSettingsSchema *schema;
    GError *error = NULL;
    gchar* path_to_schema_source = NULL;

    path_to_schema_source = g_build_filename(zond->app.exe_dir, "../share/glib-2.0/schemas", NULL);

    // Schema-Source aus lokalem Verzeichnis erstellen
    source = g_settings_schema_source_new_from_directory(
        path_to_schema_source,                                // Lokaler Pfad
        g_settings_schema_source_get_default(),        // Parent (Standard-Schemas)
        FALSE,                                         // trusted
        &error
    );
    g_free(path_to_schema_source);

    if (error) {
        LOG_ERROR("Fehler beim Laden der Schemas: %s", error->message);
        g_error_free(error);
        return;
    }

    // Schema lookup
    schema = g_settings_schema_source_lookup(
        source,
        "de.perlio.zond",  // Schema-ID
        FALSE                 // recursive
    );
    g_settings_schema_source_unref(source);

    if (!schema) {
        LOG_ERROR("Schema nicht gefunden!");
        return;
    }

	//GSettings
	zond->app.settings = g_settings_new_full(schema, NULL, NULL);
	g_settings_schema_unref(schema);

	if (!zond->app.settings)
		LOG_ERROR("Settings konnten nicht erzeugt werden");

	return;
}

static void init_css(void) {
	GtkCssProvider *provider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(provider,
	    "tooltip {"
	    "  background-color: #ffffdd;"
	    "  border: 1px solid #cccc99;"
	    "  border-radius: 4px;"
	    "}"
	    "tooltip label {"
	    "  color: #000000;"
	    "  padding: 4px;"
	    "}",
	    -1, NULL);

	gtk_style_context_add_provider_for_screen(
	    gdk_screen_get_default(),
	    GTK_STYLE_PROVIDER(provider),
	    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	g_object_unref(provider);
}

void zond_init(GtkApplication *app, Projekt *zond) {
	setlocale(LC_NUMERIC, "C");

	//Rückfrage und Konvertierung für Projektdateien anderer Version (zond_dbase ist GTK-frei)
	zond_convert_register_handler();

	zond->app.ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
	if (!zond->app.ctx)
		g_error("fz_context konnte nicht initialisiert werden");

    zond->app.exe_dir = get_exe_dir();

    g_mime_init();
    init_css();

    init_schema(zond);

	//Viewer-Sitzung: gemeinsamer Zustand aller Viewer, zond als Host
	zond->app.viewer_session = pdf_viewer_session_new(zond->app.ctx, zond->app.settings);
	viewer_host_connect(zond, zond->app.viewer_session);

	zond->app.app = GTK_APPLICATION(app);

	init_icons(zond);

	init_app_window(zond);
	gtk_application_add_window(app, GTK_WINDOW(zond->win.app_window));

	//Wiederherstellung bei Absturz
	//(d.h. in den Settings wurde project nicht auf "" gesetzt)
	gchar *proj_settings = g_settings_get_string(zond->app.settings, "project");
	gboolean speichern = g_settings_get_boolean(zond->app.settings, "speichern");
	gboolean merker_behalten = FALSE;
	if (g_strcmp0(proj_settings, "") != 0 && speichern)
		merker_behalten = recover(zond, proj_settings, G_APPLICATION(app));
	g_free(proj_settings);

	project_set_widgets_sensitive(zond, FALSE);
	if (!merker_behalten)
		g_settings_set_boolean(zond->app.settings, "speichern", FALSE);

	/* Fenster links einrasten — plattformübergreifend per GDK */
	GdkDisplay *display = gdk_display_get_default();
	GdkMonitor *monitor = gdk_display_get_primary_monitor(display);
	GdkRectangle workarea;
	gdk_monitor_get_workarea(monitor, &workarea);
	gtk_window_move(GTK_WINDOW(zond->win.app_window), workarea.x, workarea.y);
	gtk_window_resize(GTK_WINDOW(zond->win.app_window),
			workarea.width / 2, workarea.height);

	gtk_widget_show_all(zond->win.app_window);
	gtk_widget_hide(gtk_paned_get_child1(GTK_PANED(zond->win.hpaned)));

	SOND_FILE_PART_CLASS(g_type_class_get(SOND_TYPE_FILE_PART))->arr_opened_files =
			g_ptr_array_new( );

	zond_update_check_on_start(zond);

	return;
}

void zond_cleanup(Projekt* zond) {
	// aufräumen
	zond_project_reset(zond);
	g_free(zond->app.last_project_dir);
	g_object_unref(zond->app.viewer_session); //gibt die Zwischenablage frei, vor ctx

	gtk_widget_destroy(zond->win.popover);
	gtk_widget_destroy(zond->win.app_window);

	fz_drop_context(zond->app.ctx);
	g_free(zond->app.exe_dir);
	g_object_unref(zond->app.settings);
	g_ptr_array_unref(SOND_FILE_PART_CLASS(g_type_class_get(SOND_TYPE_FILE_PART))->arr_opened_files);

	g_mime_shutdown();

	return;
}
