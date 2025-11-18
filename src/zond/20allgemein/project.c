/*
 zond (project.c) - Akten, Beweisstücke, Unterlagen
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

#include <sqlite3.h>
#include <gtk/gtk.h>
#include <glib/gstdio.h>

#include "../../misc.h"
#include "../../sond_fileparts.h"
#include "../../sond_treeviewfm.h"

#include "../global_types.h"
#include "../zond_dbase.h"
#include "../zond_treeview.h"
#include "../zond_pdf_document.h"

#include "../10init/app_window.h"
#include "../40viewer/document.h"
#include "../99conv/general.h"

#include "../40viewer/viewer.h"
#include "../zond_tree_store.h"

#include "project.h"

gint dbase_zond_begin(DBaseZond* dbase_zond, GError** error) {
	gint rc = 0;

	rc = zond_dbase_begin(dbase_zond->zond_dbase_store, error);
	if (rc)
		ERROR_Z

	rc = zond_dbase_begin(dbase_zond->zond_dbase_work, error);
	if (rc)
		ERROR_ROLLBACK_Z(dbase_zond->zond_dbase_store)

	return 0;
}

void dbase_zond_rollback(DBaseZond* dbase_zond, GError** error) {
	zond_dbase_rollback(dbase_zond->zond_dbase_store, error);
	zond_dbase_rollback(dbase_zond->zond_dbase_work, error);

	return;
}

gint dbase_zond_commit(DBaseZond* dbase_zond, GError** error) {
	gint rc = 0;

	rc = zond_dbase_commit(dbase_zond->zond_dbase_store, error);
	if (rc) {
		dbase_zond_rollback(dbase_zond, error);

		ERROR_Z
	}

	rc = zond_dbase_commit(dbase_zond->zond_dbase_work, error);
	if (rc) //wenn 2. commit fehlschlägt, ist Katastrophe
		g_error("Katastrophe - Datenbanken inkonsistent - 2. commit: %s", (*error)->message);

	return 0;
}

static gint dbase_zond_update_section_dbase(ZondDBase* zond_dbase,
		DisplayedDocument* dd, GError** error) {
	gint rc = 0;
	g_autoptr(GArray) arr_sections = NULL;
	SondFilePartPDF* sfp_pdf = NULL;
	gchar* filepart = NULL;

	sfp_pdf = zond_pdf_document_get_sfp_pdf(dd->zpdfd_part->zond_pdf_document);
	filepart = sond_file_part_get_filepart(SOND_FILE_PART(sfp_pdf));

	rc = zond_dbase_get_arr_sections(zond_dbase, filepart, &arr_sections, error);
	g_free(filepart);
	if (rc)
		ERROR_Z

	for (gint i = 0; i < arr_sections->len; i++) {
		Section section = { 0 };
		Anbindung anbindung_int = { 0 };
		gint rc = 0;
		gchar* section_new = NULL;

		section = g_array_index(arr_sections, Section, i);
		anbindung_parse_file_section(section.section, &anbindung_int);

		anbindung_aktualisieren(dd->zpdfd_part->zond_pdf_document, &anbindung_int);
		//rechnet die Änderungen wieder heraus, die beim Speichern entfallen
		anbindung_korrigieren(dd->zpdfd_part, &anbindung_int);

		anbindung_build_file_section(anbindung_int, &section_new);
		rc = zond_dbase_update_section(zond_dbase, section.ID, section_new, error);
		g_free(section_new);
		if (rc)
			ERROR_Z
	}

	return 0;
}

gint dbase_zond_update_sections(DBaseZond* dbase_zond, DisplayedDocument* dd,
		GError** error) {
	gint rc = 0;

	rc = dbase_zond_update_section_dbase(dbase_zond->zond_dbase_store,
			dd, error);
	if (rc)
		ERROR_Z

	rc = dbase_zond_update_section_dbase(dbase_zond->zond_dbase_work,
			dd, error);
	if (rc)
		ERROR_Z

	return 0;
}

gint dbase_zond_update_path(DBaseZond* dbase_zond, gchar const* prefix_old,
		gchar const* prefix_new, GError** error) {
	gint rc = 0;

	rc = zond_dbase_update_path(dbase_zond->zond_dbase_store, prefix_old,
			prefix_new, error);
	if (rc)
		ERROR_Z

	rc = zond_dbase_update_path(dbase_zond->zond_dbase_work, prefix_old, prefix_new,
			error);
	if (rc)
		ERROR_Z

	return 0;
}

void project_reset_changed(Projekt *zond, gboolean changed) {
	zond->dbase_zond->changed = changed;
	gtk_widget_set_sensitive(zond->menu.speichernitem, changed);
	g_settings_set_boolean(zond->settings, "speichern", changed);

	return;
}

void project_set_changed(gpointer user_data) {
	Projekt *zond = (Projekt*) user_data;

	project_reset_changed(zond, TRUE);

	return;
}

void projekt_set_widgets_sensitiv(Projekt *zond, gboolean active) {
	gtk_widget_set_sensitive(GTK_WIDGET(zond->treeview[BAUM_FS]), active);
	gtk_widget_set_sensitive(GTK_WIDGET(zond->treeview[BAUM_INHALT]), active);
	gtk_widget_set_sensitive(GTK_WIDGET(zond->treeview[BAUM_AUSWERTUNG]),
			active);

	gtk_widget_set_sensitive(GTK_WIDGET(zond->menu.schliessenitem), active);
	gtk_widget_set_sensitive(GTK_WIDGET(zond->menu.exportitem), active);
	gtk_widget_set_sensitive(GTK_WIDGET(zond->menu.item_search_fs), active);
	gtk_widget_set_sensitive(GTK_WIDGET(zond->menu.pdf), active);
	gtk_widget_set_sensitive(GTK_WIDGET(zond->menu.struktur), active);
	gtk_widget_set_sensitive(GTK_WIDGET(zond->menu.ansicht), active);
	gtk_widget_set_sensitive(GTK_WIDGET(zond->fs_button), active);
	gtk_widget_set_sensitive(GTK_WIDGET(zond->menu.extras), active);

	return;
}

static gint project_create_dbase_zond(Projekt *zond, gboolean create, gchar **errmsg) {
	gint rc = 0;
	ZondDBase *zond_dbase_work = NULL;
	ZondDBase *zond_dbase_store = NULL;
	g_autofree gchar* path = NULL;
	gchar *path_tmp = NULL;

	path = g_strdup_printf("%s/%s", zond->project_dir, zond->project_name);
	zond_dbase_store = zond_dbase_new(path, FALSE, create, errmsg);
	if (!zond_dbase_store)
		ERROR_S

	path_tmp = g_strconcat(path, ".tmp", NULL);

	zond_dbase_work = zond_dbase_new(path_tmp, TRUE, FALSE, errmsg);
	g_free(path_tmp);
	if (!zond_dbase_work) {
		g_object_unref(zond_dbase_store);
		ERROR_S
	}

	rc = zond_dbase_backup(zond_dbase_store, zond_dbase_work, errmsg);
	if (rc) {
		g_object_unref(zond_dbase_store);
		g_object_unref(zond_dbase_work);
		ERROR_S
	}

	sqlite3_update_hook(zond_dbase_get_dbase(zond_dbase_work),
			(void*) project_set_changed, (gpointer) zond);

	zond->dbase_zond = g_malloc0(sizeof(DBaseZond));

	zond->dbase_zond->zond_dbase_store = zond_dbase_store;
	zond->dbase_zond->zond_dbase_work = zond_dbase_work;

	zond->dbase_zond->changed = FALSE;

	return 0;
}

static void project_clear_dbase_zond(DBaseZond **dbase_zond) {
	g_object_unref((*dbase_zond)->zond_dbase_store);
	g_object_unref((*dbase_zond)->zond_dbase_work);
	g_free(*dbase_zond);

	*dbase_zond = NULL;

	return;
}

gint project_speichern(Projekt *zond, gchar **errmsg) {
	gint rc = 0;

	if (!(zond->dbase_zond->changed))
		return 0;

	rc = zond_dbase_backup(zond->dbase_zond->zond_dbase_work,
			zond->dbase_zond->zond_dbase_store, errmsg);
	if (rc)
		ERROR_S

	project_reset_changed(zond, FALSE);

	return 0;
}

gboolean project_timeout_autosave(gpointer data) {
	gchar *errmsg = NULL;

	Projekt *zond = (Projekt*) data;

	if (zond->dbase_zond) {
		gint rc = 0;

		rc = project_speichern(zond, &errmsg);
		if (rc) {
			display_message(zond->app_window,
					"Automatisches Speichern fehlgeschlagen\n\n", errmsg, NULL);
			g_free(errmsg);
		}
	}

	return TRUE;
}

void cb_menu_datei_speichern_activate(GtkMenuItem *item, gpointer user_data) {
	gint rc = 0;
	gchar *errmsg = NULL;

	Projekt *zond = (Projekt*) user_data;

	rc = project_speichern(zond, &errmsg);
	if (rc) {
		display_message(zond->app_window, "Fehler beim Speichern -\n\n", errmsg,
				NULL);
		g_free(errmsg);
	}

	return;
}

gint projekt_schliessen(Projekt *zond, gchar **errmsg) {
	gboolean ret = FALSE;

	if (!zond->dbase_zond)
		return 0;

	if (zond->dbase_zond->changed) {
		gint rc = 0;

		rc = abfrage_frage(zond->app_window, "Datei schließen", "Änderungen "
				"aktuelles Projekt speichern?", NULL);

		if (rc == GTK_RESPONSE_YES) {
			gint rc = 0;
			rc = project_speichern(zond, errmsg);
			if (rc)
				ERROR_S
		} else if (rc != GTK_RESPONSE_NO)
			return 1;
	}

	for (gint i = 0; i < zond->arr_pv->len; i++)
		viewer_save_and_close(g_ptr_array_index(zond->arr_pv, i));

	//Menus aktivieren/ausgrauen
	projekt_set_widgets_sensitiv(zond, FALSE);

	//damit focus aus text_view rauskommt und changed-signal abgeschaltet wird
	gtk_widget_grab_focus(GTK_WIDGET(zond->treeview[BAUM_INHALT]));

	//textview leeren
	GtkTextBuffer *buffer = gtk_text_view_get_buffer(
			GTK_TEXT_VIEW(zond->textview));
	gtk_text_buffer_set_text(buffer, "", -1);

	//Text-Fenster verstecken (falls nicht schon ist, Überprüfung aber überflüssig
	g_signal_emit_by_name(zond->textview_window, "delete-event", zond, &ret);

	zond->node_id_act = 0;

	project_reset_changed(zond, FALSE);

	//treeviews leeren
	zond_tree_store_clear(
			ZOND_TREE_STORE(
					gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[BAUM_INHALT]) )));
	zond_tree_store_clear(
			ZOND_TREE_STORE(
					gtk_tree_view_get_model( GTK_TREE_VIEW(zond->treeview[BAUM_AUSWERTUNG]) )));

	//muß vor project_destroy..., weil callback ausgelöst wird, der db_get_node_id... aufruft
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(zond->fs_button), FALSE);

	gchar *working_copy = g_strconcat(zond->project_dir, "/",
			zond->project_name, ".tmp", NULL);

	sond_treeviewfm_set_root(SOND_TREEVIEWFM(zond->treeview[BAUM_FS]), NULL,
			NULL);
	project_clear_dbase_zond(&(zond->dbase_zond));

	//Ggf. autosave abschalten
	if (g_settings_get_boolean(zond->settings, "autosave")) {
		if (!g_source_remove_by_user_data(zond))
			display_message(zond->app_window,
					"autosave-Timeout konnte nicht entfernt werdern", NULL);
	}

	gint res = g_remove(working_copy);
	if (res == -1)
		display_message(zond->app_window, "Fehler beim Löschen der "
				"temporären Datenbank:\n", strerror(errno), NULL);
	g_free(working_copy);

	gtk_header_bar_set_title(
			GTK_HEADER_BAR(
					gtk_window_get_titlebar( GTK_WINDOW(zond->app_window) )),
			"");

	//project in settings auf leeren String setzen
	g_settings_set_string(zond->settings, "project", "");

	return 0;
}

void cb_menu_datei_schliessen_activate(GtkMenuItem *item, gpointer user_data) {
	gint rc = 0;
	gchar *errmsg = NULL;

	Projekt *zond = (Projekt*) user_data;

	rc = projekt_schliessen(zond, &errmsg);
	if (rc == -1) {
		display_message(zond->app_window,
				"Fehler bei Schließen des Projekts -\n\n"
						"Bei Aufruf projekt_schliessen:\n", errmsg, NULL);
		g_free(errmsg);
	}

	return;
}

gint project_load_baeume(Projekt *zond, GError **error) {
	gint rc = 0;
	GtkTreeIter iter = { 0 };

	rc = zond_treeview_load_baum(ZOND_TREEVIEW(zond->treeview[BAUM_INHALT]),
			error);
	if (rc == -1) {
		g_prefix_error(error, "%s\n", __func__);

		return -1;
	}

	rc = zond_treeview_load_baum(ZOND_TREEVIEW(zond->treeview[BAUM_AUSWERTUNG]),
			error);
	if (rc == -1) {
		g_prefix_error(error, "%s\n", __func__);

		return -1;
	}

	g_object_set(
			sond_treeview_get_cell_renderer_text(
					zond->treeview[BAUM_AUSWERTUNG]), "editable", FALSE, NULL);
	g_object_set(
			sond_treeview_get_cell_renderer_text(zond->treeview[BAUM_INHALT]),
			"editable", TRUE, NULL);

	gtk_widget_grab_focus(GTK_WIDGET(zond->treeview[BAUM_INHALT]));

	if (gtk_tree_model_get_iter_first(
			gtk_tree_view_get_model(
					GTK_TREE_VIEW(zond->treeview[BAUM_AUSWERTUNG])), &iter)) {
		sond_treeview_set_cursor(zond->treeview[BAUM_AUSWERTUNG], &iter);
		gtk_tree_selection_unselect_all(zond->selection[BAUM_AUSWERTUNG]);
	}

	return 0;
}

gint project_oeffnen(Projekt *zond, const gchar *abs_path, gboolean create,
		gchar **errmsg) {
	gint rc = 0;

	rc = projekt_schliessen(zond, errmsg);
	if (rc) {
		if (rc == -1)
			ERROR_S
		else
			return 0;
	}

	zond->project_name = g_path_get_basename(abs_path);
	zond->project_dir = //soll ohne '/' enden
			g_strndup(abs_path, strlen(abs_path) - strlen(zond->project_name) -
					(((strlen(abs_path) - strlen(zond->project_name)) > 1) ? 1 : 0));
	//letzte Zeile soll sicherstellen, dass, wenn dir linux-root ist, '/' bleibt

	rc = project_create_dbase_zond(zond, create, errmsg);
	if (rc) {
		g_free(zond->project_name);
		g_free(zond->project_dir);
		zond->project_name = NULL;
		zond->project_dir = NULL;

		ERROR_S
	}

	if (!create) {
		GError *error = NULL;
		gint rc = 0;

		rc = project_load_baeume(zond, &error);
		if (rc) {
			if (errmsg)
				*errmsg = g_strdup_printf("%s\n%s", __func__, error->message);
			g_error_free(error);
			project_clear_dbase_zond(&(zond->dbase_zond));
			g_free(zond->project_name);
			g_free(zond->project_dir);
			zond->project_name = NULL;
			zond->project_dir = NULL;

			return -1;
		}
	}

	rc = sond_treeviewfm_set_root(SOND_TREEVIEWFM(zond->treeview[BAUM_FS]),
			zond->project_dir, errmsg); //setzt auch PWD
	if (rc) {
		project_clear_dbase_zond(&(zond->dbase_zond));
		g_free(zond->project_name);
		g_free(zond->project_dir);
		zond->project_name = NULL;
		zond->project_dir = NULL;

		ERROR_S
	}

	projekt_set_widgets_sensitiv(zond, TRUE);

	//project_name als Titel Headerbar
	gtk_header_bar_set_title(
			GTK_HEADER_BAR(
					gtk_window_get_titlebar(GTK_WINDOW(zond->app_window))),
			zond->project_name);

	//project_name in settings schreiben
	g_settings_set_string(zond->settings, "project", abs_path);

	project_reset_changed(zond, FALSE);

	if (g_settings_get_boolean(zond->settings, "autosave"))
		g_timeout_add_seconds(10 * 60, project_timeout_autosave, zond);

	return 0;
}

static gint project_wechseln(Projekt *zond) {
	gint rc = 0;
	//nachfragen, ob aktuelles Projekt gespeichert werden soll
	if (!zond->dbase_zond)
		return 0;

	rc = abfrage_frage(zond->app_window, zond->project_name,
			"Projekt schließen?", NULL);
	if ((rc != GTK_RESPONSE_YES))
		return 1; //Abbrechen -> nicht öffnen

	return 0;
}

void cb_menu_datei_oeffnen_activate(GtkMenuItem *item, gpointer user_data) {
	gint rc = 0;
	gchar *errmsg = NULL;

	Projekt *zond = (Projekt*) user_data;

	rc = project_wechseln(zond);
	if (rc)
		return;

	gchar *abs_path = filename_oeffnen(GTK_WINDOW(zond->app_window));
	if (!abs_path)
		return;

	rc = project_oeffnen(zond, abs_path, FALSE, &errmsg);
	g_free(abs_path);
	if (rc == -1) {
		display_error(zond->app_window, "Fehler beim Öffnen", errmsg);
		g_free(errmsg);

		return;
	}

	return;
}

void cb_menu_datei_neu_activate(GtkMenuItem *item, gpointer user_data) {
	gint rc = 0;
	gchar *errmsg = NULL;

	Projekt *zond = (Projekt*) user_data;

	rc = project_wechseln(zond);
	if (rc)
		return;

	gchar *abs_path = filename_speichern(GTK_WINDOW(zond->app_window),
			"Projekt anlegen", ".ZND");
	if (!abs_path)
		return;

	rc = project_oeffnen(zond, abs_path, TRUE, &errmsg);
	g_free(abs_path);
	if (rc == -1) {
		display_message(zond->app_window, "Fehler beim Öffnen-\n\n", errmsg,
				NULL);
		g_free(errmsg);
	}

	return;
}

