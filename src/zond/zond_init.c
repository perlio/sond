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

#ifdef __WIN32
#include <libloaderapi.h>
#include <errhandlingapi.h>
#endif // __WIN32

#include "../misc.h"
#include "../misc_stdlib.h"
#include "../sond_fileparts.h"
#include "../sond_log_and_error.h"
#include "../sond_file_helper.h"

#include "zond_init.h"
#include "zond_pdf_document.h"

#include "20allgemein/project.h"

#include "10init/icons.h"
#include "10init/app_window.h"
#include "10init/headerbar.h"

static void recover(Projekt *zond, gchar *project, GApplication *app) {
	gint rc = 0;
	gchar *path_bak = NULL;
	gchar *path_tmp = NULL;
	GError* error = NULL;

	gchar *text_abfrage = g_strconcat("Projekt ", project,
			" wurde nicht richtig "
					"geschlossen. Wiederherstellen?", NULL);
	gint res = abfrage_frage(zond->app_window, "Wiederherstellen", text_abfrage,
			NULL);
	g_free(text_abfrage);

	if (res == GTK_RESPONSE_YES) {
		path_bak = g_strconcat(project, ".bak", NULL);
		rc = sond_rename(project, path_bak, &error);
		if (rc) {
			display_message(zond->app_window,
					"Konnte Sicherungskopie (.bak) nicht erstellen: ",
					error->message, NULL);
			g_clear_error(&error);
		}
		g_free(path_bak);

		path_tmp = g_strconcat(project, ".tmp", NULL);
		rc = sond_rename(path_tmp, project, &error);
		if (rc) {
			display_message(zond->app_window,
					"Konnte wiederhergestellte Datei (.tmp) nicht umbenennen: ",
					error->message, NULL);
			g_clear_error(&error);
		}
		else
			display_message(zond->app_window, project,
					" erfolgreich wiederhergestellt", NULL);
		g_free(path_tmp);
	} else if (res != GTK_RESPONSE_NO)
		g_application_quit(app);

	g_settings_set_string(zond->settings, "project", "");
	g_settings_set_boolean(zond->settings, "speichern", FALSE);

	return;
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

//    zond->icon[ICON_NOTHING] = { "dialog-error", "Nix" };
	set_icon(&zond->icon[ICON_NOTHING], "dialog-error", "Nix");
	set_icon(&zond->icon[ICON_NORMAL], "media-record", "Punkt");
	set_icon(&zond->icon[ICON_ORDNER], "folder", "Ordner");
	set_icon(&zond->icon[ICON_PDF_FOLDER], "pdf-folder", "PDF-Datei");
	set_icon(&zond->icon[ICON_DATEI], "document-open", "Datei");
	set_icon(&zond->icon[ICON_PDF], "pdf", "PDF");
	set_icon(&zond->icon[ICON_ANBINDUNG], "anbindung", "Anbindung");
	set_icon(&zond->icon[ICON_AKTE], "akte", "Akte");
	set_icon(&zond->icon[ICON_EXE], "application-x-executable", "Ausführbar");
	set_icon(&zond->icon[ICON_TEXT], "text-x-generic", "Text");
	set_icon(&zond->icon[ICON_DOC], "x-office-document", "Writer/Word");
	set_icon(&zond->icon[ICON_PPP], "x-office-presentation", "PowerPoint");
	set_icon(&zond->icon[ICON_SPREAD], "x-office-spreadsheet", "Tabelle");
	set_icon(&zond->icon[ICON_IMAGE], "emblem-photos", "Bild");
	set_icon(&zond->icon[ICON_VIDEO], "video-x-generic", "Video");
	set_icon(&zond->icon[ICON_AUDIO], "audio-x-generic", "Audio");
	set_icon(&zond->icon[ICON_EMAIL], "mail-unread", "E-Mail");
	set_icon(&zond->icon[ICON_HTML], "emblem-web", "HTML"); //16

	//Platzhalter
	set_icon(&zond->icon[17], "process-stop", "Frei");
	set_icon(&zond->icon[18], "process-stop", "Frei");
	set_icon(&zond->icon[19], "process-stop", "Frei");
	set_icon(&zond->icon[20], "process-stop", "Frei");
	set_icon(&zond->icon[21], "process-stop", "Frei");
	set_icon(&zond->icon[22], "process-stop", "Frei");
	set_icon(&zond->icon[23], "process-stop", "Frei");
	set_icon(&zond->icon[24], "process-stop", "Frei");

	set_icon(&zond->icon[ICON_DURCHS], "system-log-out", "Durchsuchung");
	set_icon(&zond->icon[ICON_ORT], "mark-location", "Ort");
	set_icon(&zond->icon[ICON_PHONE], "phone", "TKÜ");
	set_icon(&zond->icon[ICON_WICHTIG], "emblem-important", "Wichtig");
	set_icon(&zond->icon[ICON_OBS], "camera-web", "Observation");
	set_icon(&zond->icon[ICON_CD], "media-optical", "CD");
	set_icon(&zond->icon[ICON_PERSON], "user-info", "Person");
	set_icon(&zond->icon[ICON_PERSONEN], "system-users", "Personen");
	set_icon(&zond->icon[ICON_ORANGE], "orange", "Orange");
	set_icon(&zond->icon[ICON_BLAU], "blau", "Blau");
	set_icon(&zond->icon[ICON_ROT], "rot", "Rot");
	set_icon(&zond->icon[ICON_GRUEN], "gruen", "Grün");
	set_icon(&zond->icon[ICON_TUERKIS], "tuerkis", "Türkis");
	set_icon(&zond->icon[ICON_MAGENTA], "magenta", "Magenta");

	return;
}

static void init_schema(Projekt* zond) {
    GSettingsSchemaSource *source;
    GSettingsSchema *schema;
    GError *error = NULL;
    gchar* path_to_schema_source = NULL;

    path_to_schema_source = g_build_filename(zond->exe_dir, "../share/glib-2.0/schemas", NULL);

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
	zond->settings = g_settings_new_full(schema, NULL, NULL);
	g_settings_schema_unref(schema);

	if (!zond->settings)
		LOG_ERROR("Settings konnten nicht erzeugt werden");

	return;
}


void zond_init(GtkApplication *app, Projekt *zond) {
	setlocale(LC_NUMERIC, "C");

    zond->base_dir = get_base_dir();
    zond->exe_dir = get_exe_dir();

    g_mime_init();

    init_schema(zond);

	//benötigte Arrays erzeugen
	zond->arr_pv = g_ptr_array_new();

	init_icons(zond);

	init_app_window(zond);
	gtk_application_add_window(app, GTK_WINDOW(zond->app_window));

	init_headerbar(zond);

	//Wiederherstellung bei Absturz
	//(d.h. in den Settings wurde project nicht auf "" gesetzt)
	gchar *proj_settings = g_settings_get_string(zond->settings, "project");
	gboolean speichern = g_settings_get_boolean(zond->settings, "speichern");
	if (g_strcmp0(proj_settings, "") != 0 && speichern)
		recover(zond, proj_settings, G_APPLICATION(app));
	g_free(proj_settings);

	project_set_widgets_sensitive(zond, FALSE);
	gtk_widget_set_sensitive(zond->menu.speichernitem, FALSE);
	g_settings_set_boolean(zond->settings, "speichern", FALSE);

	gtk_widget_show_all(zond->app_window);
	gtk_widget_hide(gtk_paned_get_child1(GTK_PANED(zond->hpaned)));

	SOND_FILE_PART_CLASS(g_type_class_get(SOND_TYPE_FILE_PART))->arr_opened_files =
			g_ptr_array_new( );

	zond->ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
	if (!zond->ctx)
		g_error("fz_context konnte nicht initialisiert werden");

	return;
}

void zond_cleanup(Projekt* zond) {
	// aufräumen
	if (zond->pv_clip)
		pdf_drop_document(zond->ctx, zond->pv_clip);
	if (zond->ocr_font)
		pdf_drop_document(zond->ctx, zond->ocr_font);

	gtk_widget_destroy(zond->textview_window);
	gtk_widget_destroy(zond->popover);
	gtk_widget_destroy(zond->app_window);

	fz_drop_context(zond->ctx);
	g_ptr_array_unref(zond->arr_pv);
	g_free(zond->base_dir);
	g_free(zond->exe_dir);
	g_object_unref(zond->settings);
	g_ptr_array_unref(SOND_FILE_PART_CLASS(g_type_class_get(SOND_TYPE_FILE_PART))->arr_opened_files);

	g_mime_shutdown();

	return;
}
