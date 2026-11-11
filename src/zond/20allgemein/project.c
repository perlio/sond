/*
 zond (project.c) - Akten, Beweisstücke, Unterlagen
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

#include <sqlite3.h>
#include <gtk/gtk.h>
#include <glib/gstdio.h>

#include "../../sond_log_and_error.h"
#include "../../sond_file_helper.h"
#include "../../sond_fileparts.h"
#include "../../sond_mime.h"
#include "../../sond_treeviewfm.h"
#include "../../sond_tvfm_item.h"
#include "../../sond_seadrive.h"
#include "../../sond_process_file.h"
#include "../../sond_password.h"
#include "../../misc.h"

#include "../zond_init.h"
#include "../zond_dbase.h"
#include "../zond_treeview.h"
#include "../zond_treeviewfm.h"
#include "../zond_pdf_document.h"

#include "../10init/app_window.h"
#include "../10init/headerbar.h"
#include "../40viewer/document.h"
#include "../anbindung.h"

#include "../40viewer/viewer.h"
#include "../40viewer/viewer_save.h"
#include "../zond_tree_store.h"

#include "project.h"
#include "db_check.h"
#include "db_check_fenster.h"

// Constants
#define AUTOSAVE_INTERVAL_SECONDS (10 * 60)  // 10 minutes

// ============================================================================
// Project State Management
// ============================================================================

/**
 * Reset the "changed" state of the project
 * @param zond The project structure
 * @param changed TRUE if project has unsaved changes, FALSE otherwise
 */
void project_reset_changed(Projekt *zond, gboolean changed) {
	zond->dbase_zond->changed = changed;
	g_simple_action_set_enabled(zond->menu.speichern, changed);
	g_settings_set_boolean(zond->settings, "speichern", changed);

	return;
}

/**
 * Mark the project as having unsaved changes
 * @param user_data Pointer to Projekt structure
 */
static void project_set_changed(gpointer user_data) {
	Projekt *zond = (Projekt*) user_data;
	project_reset_changed(zond, TRUE);

	return;
}

/**
 * Enable or disable project-related widgets
 * @param zond The project structure
 * @param active TRUE to enable, FALSE to disable
 */
void project_set_widgets_sensitive(Projekt *zond, gboolean active) {
	gboolean seadrive_active;

	gtk_widget_set_sensitive(GTK_WIDGET(zond->treeview[BAUM_FS]), active);
	gtk_widget_set_sensitive(GTK_WIDGET(zond->treeview[BAUM_INHALT]), active);
	gtk_widget_set_sensitive(GTK_WIDGET(zond->treeview[BAUM_AUSWERTUNG]), active);

	g_simple_action_set_enabled(zond->menu.schliessen,  active);
	g_simple_action_set_enabled(zond->menu.export_odt,  active);
	g_simple_action_set_enabled(zond->menu.struktur,    active);
	g_simple_action_set_enabled(zond->menu.ansicht,     active);
	gtk_widget_set_sensitive(zond->fs_button,           active);
	g_simple_action_set_enabled(zond->menu.extras,      active);
	g_simple_action_set_enabled(zond->menu.db_pruefen,  active);

	if (!active)
		g_simple_action_set_enabled(zond->menu.speichern, FALSE);

	/* SeaDrive-Menüpunkte (Haupt- und Kontextmenüs) nur anwählbar, wenn ein
	 * Projekt offen UND dessen Wurzel (BAUM_FS) tatsächlich ein SeaDrive-
	 * Verzeichnis ist - sond_treeviewfm_set_root() (weiter oben in diesem
	 * bzw. dem aufrufenden Codepfad) hat is_seadrive_path zu diesem
	 * Zeitpunkt bereits aktuell gesetzt. Ohne SeaDrive-Projekt sollen die
	 * Punkte ausgegraut statt nur wirkungslos sein. */
	seadrive_active = active &&
			sond_treeviewfm_is_seadrive_path(SOND_TREEVIEWFM(zond->treeview[BAUM_FS]));
	sond_treeviewfm_seadrive_set_contextmenu_sensitive(
			SOND_TREEVIEWFM(zond->treeview[BAUM_FS]), seadrive_active);
	zond_treeview_seadrive_set_contextmenu_sensitive(
			ZOND_TREEVIEW(zond->treeview[BAUM_INHALT]), seadrive_active);
	zond_treeview_seadrive_set_contextmenu_sensitive(
			ZOND_TREEVIEW(zond->treeview[BAUM_AUSWERTUNG]), seadrive_active);
	headerbar_set_seadrive_sensitive(zond, seadrive_active);

	return;
}

// ============================================================================
// Database Creation and Cleanup
// ============================================================================

/* Wiederherstellung nach Neuaufbau der Verbindung (zond_dbase_rollback(),
 * restart_con() in zond_dbase.c): store braucht work wieder als Schema, idx
 * ist weg (wird je Transaktion neu angehängt); work braucht seinen Hook. */
static gint project_restart_store(ZondDBase *zond_dbase, gpointer data,
		GError **error) {
	Projekt *zond = (Projekt*) data;
	gchar *sql = NULL;
	gchar *errmsg = NULL;
	gint rc = 0;

	zond->dbase_zond->index_attached = FALSE;

	sql = sqlite3_mprintf("ATTACH DATABASE %Q AS work;",
			zond_dbase_get_path(zond->dbase_zond->zond_dbase_work));
	rc = sqlite3_exec(zond_dbase_get_dbase(zond_dbase), sql, NULL, NULL,
			&errmsg);
	sqlite3_free(sql);
	if (rc != SQLITE_OK) {
		g_set_error(error, SOND_ERROR, 0, "%s: ATTACH work: %s", __func__,
				errmsg ? errmsg : "?");
		sqlite3_free(errmsg);

		return -1;
	}

	return 0;
}

static gint project_restart_work(ZondDBase *zond_dbase, gpointer data,
		GError **error) {
	sqlite3_update_hook(zond_dbase_get_dbase(zond_dbase),
			(void*) project_set_changed, data);

	return 0;
}

/**
 * Create and initialize the project databases
 * @param zond The project structure
 * @param create TRUE to create new database, FALSE to open existing
 * @param error GError for error reporting
 * @return 0 on success, -1 on error
 */
static gint project_create_dbase_zond(Projekt *zond, gboolean create, GError **error) {
	gint rc = 0;
	ZondDBase *zond_dbase_work = NULL;
	ZondDBase *zond_dbase_store = NULL;
	g_autofree gchar* path = NULL;
	gchar *path_tmp = NULL;

	path = g_strdup_printf("%s/%s", zond->project_dir, zond->project_name);
	zond_dbase_store = zond_dbase_new(path, FALSE, create, error);
	if (!zond_dbase_store)
		return -1;

	/* Bewußt NICHT mehr neben der Projektdatei (die auf einem Cloud-Sync-
	 * Laufwerk liegen kann) - s. Kommentar bei project_get_local_tmp_path(). */
	path_tmp = project_get_local_tmp_path(path, error);
	if (!path_tmp) {
		g_object_unref(zond_dbase_store);
		return -1;
	}

	zond_dbase_work = zond_dbase_new(path_tmp, TRUE, FALSE, error);
	if (!zond_dbase_work) {
		g_free(path_tmp);
		g_object_unref(zond_dbase_store);
		return -1;
	}

	rc = zond_dbase_backup(zond_dbase_store, zond_dbase_work, error);
	if (rc) {
		g_free(path_tmp);
		g_object_unref(zond_dbase_store);
		g_object_unref(zond_dbase_work);
		return -1;
	}

	/* Performance-Indizes verlässlich auf "work" setzen - unabhängig
	 * davon, ob das analoge (unkritische, per zond_dbase_open() nicht
	 * mehr fatale) CREATE INDEX auf "store" gerade funktioniert hat.
	 * "work" liegt bewusst lokal (s.o., project_get_local_tmp_path()),
	 * ein Fehlschlagen hier ist also ein echter, unerwarteter lokaler
	 * I/O-Fehler und bleibt daher fatal (s. zond_dbase.c). */
	rc = zond_dbase_ensure_performance_indexes(zond_dbase_work, error);
	if (rc) {
		g_free(path_tmp);
		g_object_unref(zond_dbase_store);
		g_object_unref(zond_dbase_work);
		return -1;
	}

	/* work zusätzlich als zweites Schema an store anhängen (Atomarität
	 * store/work): work behält
	 * daneben seine eigene, unten registrierte Verbindung für alle
	 * "normalen" Einzel-DB-Operationen - die angehängte Verbindung wird
	 * nur von den Dual-Write-Funktionen (dbase_zond_update_sections()/
	 * _path()/_gmessage_index(), project.c) benutzt. %Q escaped/quoted
	 * path_tmp automatisch (könnte theoretisch ein Apostroph enthalten). */
	{
		gchar *sql_attach = NULL;
		gchar *errmsg_sqlite = NULL;
		gint rc_attach = 0;

		sql_attach = sqlite3_mprintf("ATTACH DATABASE %Q AS work;", path_tmp);
		rc_attach = sqlite3_exec(zond_dbase_get_dbase(zond_dbase_store),
				sql_attach, NULL, NULL, &errmsg_sqlite);
		sqlite3_free(sql_attach);
		g_free(path_tmp);

		if (rc_attach != SQLITE_OK) {
			if (error)
				*error = g_error_new(g_quark_from_static_string("SQLITE3"),
						rc_attach, "%s: ATTACH work: %s", __func__,
						errmsg_sqlite);
			sqlite3_free(errmsg_sqlite);
			g_object_unref(zond_dbase_store);
			g_object_unref(zond_dbase_work);
			return -1;
		}
	}

	sqlite3_update_hook(zond_dbase_get_dbase(zond_dbase_work),
			(void*) project_set_changed, (gpointer) zond);

	zond_dbase_set_restart_func(zond_dbase_store, project_restart_store, zond);
	zond_dbase_set_restart_func(zond_dbase_work, project_restart_work, zond);

	zond->dbase_zond = g_malloc0(sizeof(DBaseZond));

	zond->dbase_zond->zond_dbase_store = zond_dbase_store;
	zond->dbase_zond->zond_dbase_work = zond_dbase_work;

	zond->dbase_zond->changed = FALSE;

	return 0;
}

/**
 * Clean up and free database resources
 * @param dbase_zond Pointer to database structure pointer
 */
static void project_clear_dbase_zond(DBaseZond **dbase_zond) {
	g_object_unref((*dbase_zond)->zond_dbase_store);
	g_object_unref((*dbase_zond)->zond_dbase_work);
	g_free(*dbase_zond);

	*dbase_zond = NULL;

	return;
}

// ============================================================================
// Project Save Functions
// ============================================================================

/**
 * Save the project to disk
 * @param zond The project structure
 * @param error GError for error reporting
 * @return 0 on success, -1 on error
 */
gint project_save(Projekt *zond, GError **error) {
	gint rc = 0;

	if (!(zond->dbase_zond) || !(zond->dbase_zond->changed))
		return 0;

	rc = zond_dbase_backup(zond->dbase_zond->zond_dbase_work,
			zond->dbase_zond->zond_dbase_store, error);
	if (rc)
		return -1;

	project_reset_changed(zond, FALSE);

	return 0;
}

/**
 * Autosave timeout callback
 * @param data Pointer to Projekt structure
 * @return TRUE to continue timeout, FALSE to stop
 */
gboolean project_timeout_autosave(gpointer data) {
	GError *error = NULL;
	Projekt *zond = (Projekt*) data;

	if (zond->dbase_zond) {
		gint rc = project_save(zond, &error);
		if (rc) {
			display_message(zond->app_window,
					"Automatisches Speichern fehlgeschlagen\n\n", error->message, NULL);
			g_error_free(error);
		}
	}

	return TRUE;
}

// ============================================================================
// Project Close Functions
// ============================================================================

/**
 * Close the current project
 * @param zond The project structure
 * @param error GError for error reporting
 * @return 0 on success, -1 on error, 1 if user cancelled
 */
gint project_close(Projekt *zond, GError **error) {
	GError *error_remove = NULL;

	if (!zond->dbase_zond)
		return 0;

	if (!zond->arr_pv || !zond->app_window)
		return 0;

	// Ask to save if there are unsaved changes
	if (zond->dbase_zond->changed) {
		gint rc = abfrage_frage(zond->app_window, "Datei schließen",
				"Änderungen aktuelles Projekt speichern?", NULL);

		if (rc == GTK_RESPONSE_YES) {
			gint save_rc = project_save(zond, error);
			if (save_rc) {
				/* Nutzervorgabe: Speichern kann fehlschlagen (z.B.
				 * Netzlaufwerk kurzzeitig nicht erreichbar), ohne dass
				 * der Nutzer die Möglichkeit verlieren soll, das
				 * Projekt trotzdem - ohne die fehlgeschlagene
				 * Speicherung - zu schließen (und dabei wie gewohnt
				 * nach den offenen PDF-Viewern gefragt zu werden).
				 * Vorher wurde hier sofort abgebrochen (return -1),
				 * ohne diese Möglichkeit. */
				g_autofree gchar *msg_save_failed = g_strdup_printf(
						"Speichern fehlgeschlagen: %s",
						(error && *error) ? (*error)->message : "unbekannter Fehler");
				gint rc_trotzdem = abfrage_frage(zond->app_window,
						msg_save_failed,
						"Projekt trotzdem ohne Speichern schließen?", NULL);

				if (rc_trotzdem != GTK_RESPONSE_YES)
					return -1;

				if (error && *error)
					g_clear_error(error);
			}
		} else if (rc != GTK_RESPONSE_NO) {
			return 1;  // User cancelled
		}
	}

	//Diagnose-Fenster arbeitet auf der Datenbank des Projekts
	db_check_fenster_schliessen();

	// Close all viewer windows
	for (gint i = 0; i < zond->arr_pv->len; i++)
		viewer_save_and_close(g_ptr_array_index(zond->arr_pv, i));

	// Gemerkte Passwörter vergessen und überschreiben
	sond_password_clear();

	// Disable menus and widgets
	project_set_widgets_sensitive(zond, FALSE);

	// Clear text view
	GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(zond->textview));
	gtk_text_buffer_set_text(buffer, "", -1);

	// Pin-Button zurücksetzen
	if (zond->textview_pin_button)
		gtk_toggle_button_set_active(
				GTK_TOGGLE_BUTTON(zond->textview_pin_button), FALSE);

	zond->node_id_act = 0;

	project_reset_changed(zond, FALSE);

	// Clear treeviews
	zond_tree_store_clear(
			ZOND_TREE_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(zond->treeview[BAUM_INHALT]))));
	zond_tree_store_clear(
			ZOND_TREE_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(zond->treeview[BAUM_AUSWERTUNG]))));

	// Must be before project_clear_dbase_zond because it triggers callbacks
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(zond->fs_button), FALSE);

	/* Bewußt NICHT mehr neben der Projektdatei - s. Kommentar bei
	 * project_get_local_tmp_path(). Derselbe volle Projektpfad wie in
	 * project_create_dbase_zond() liefert deterministisch denselben
	 * lokalen Pfad zurück. */
	gchar *project_path_full = g_strdup_printf("%s/%s", zond->project_dir,
			zond->project_name);
	gchar *working_copy = project_get_local_tmp_path(project_path_full,
			&error_remove);
	g_free(project_path_full);

	sond_treeviewfm_set_root(SOND_TREEVIEWFM(zond->treeview[BAUM_FS]), NULL, NULL);
	sond_eingang_cache_free(zond->eingang_cache);
	zond->eingang_cache = NULL;
	project_clear_dbase_zond(&(zond->dbase_zond));

	// Disable autosave if active
	if (g_settings_get_boolean(zond->settings, "autosave")) {
		if (!g_source_remove_by_user_data(zond))
			display_message(zond->app_window,
					"autosave-Timeout konnte nicht entfernt werdern", NULL);
	}

	// Remove temporary database
	if (!working_copy) {
		display_message(zond->app_window, "Fehler beim Ermitteln des Pfads "
				"der temporären Datenbank: ", error_remove->message, NULL);
		g_error_free(error_remove);
	} else if (!sond_remove(working_copy, &error_remove)) {
		display_message(zond->app_window, "Fehler beim Löschen der "
				"temporären Datenbank: ", error_remove->message, NULL);
		g_error_free(error_remove);
	}

	g_free(working_copy);

	// Clear window title
	gtk_header_bar_set_title(
			GTK_HEADER_BAR(gtk_window_get_titlebar(GTK_WINDOW(zond->app_window))), "");

	// Clear project setting
	g_settings_set_string(zond->settings, "project", "");

	sond_process_file_destroy_wctx(zond->wctx);

	return 0;
}

// ============================================================================
// Project Load Functions
// ============================================================================

/**
 * Load the project tree structures from database
 * @param zond The project structure
 * @param error GError for error reporting
 * @return 0 on success, -1 on error
 */
gint project_load_trees(Projekt *zond, GError **error) {
	gint rc = 0;
	GtkTreeIter iter = { 0 };

	rc = zond_treeview_load_baum(ZOND_TREEVIEW(zond->treeview[BAUM_INHALT]), error);
	if (rc == -1) {
		g_prefix_error(error, "%s\n", __func__);
		return -1;
	}

	rc = zond_treeview_load_baum(ZOND_TREEVIEW(zond->treeview[BAUM_AUSWERTUNG]), error);
	if (rc == -1) {
		g_prefix_error(error, "%s\n", __func__);
		return -1;
	}

	g_object_set(
			sond_treeview_get_cell_renderer_text(zond->treeview[BAUM_AUSWERTUNG]),
			"editable", FALSE, NULL);
	g_object_set(
			sond_treeview_get_cell_renderer_text(zond->treeview[BAUM_INHALT]),
			"editable", TRUE, NULL);

	gtk_widget_grab_focus(GTK_WIDGET(zond->treeview[BAUM_INHALT]));

	if (gtk_tree_model_get_iter_first(
			gtk_tree_view_get_model(GTK_TREE_VIEW(zond->treeview[BAUM_AUSWERTUNG])), &iter)) {
		sond_treeview_set_cursor(zond->treeview[BAUM_AUSWERTUNG], &iter);
		gtk_tree_selection_unselect_all(zond->selection[BAUM_AUSWERTUNG]);
	}

	return 0;
}

// ============================================================================
// Project Open Functions
// ============================================================================

/**
 * Clean up partially opened project on error
 * Clears treeviews, filesystem root, databases and frees allocated strings
 * @param zond The project structure to clean up
 *
 * Die Baumansichten werden immer geleert, unabhängig davon, ob
 * project_load_trees() vollständig durchgelaufen ist: zond_tree_store_clear()
 * ist auch auf einem leeren/nie befüllten Store gefahrlos aufrufbar. Ein
 * vorheriges "trees_loaded"-Flag war hier fehleranfällig, weil
 * project_load_trees() auch dann schon reale Zeilen eingefügt haben kann,
 * wenn es anschließend (z.B. bei der Link-Auflösung) mit Fehler abbricht -
 * mit trees_loaded==FALSE blieben solche Zeilen ungeleert im Modell stehen
 * und ein nachfolgender Redraw griff auf das bereits freigegebene
 * dbase_zond zu (Absturz).
 */
static void project_open_cleanup(Projekt* zond) {
	// Baumansichten immer leeren (auch bei nur teilweise geladenen Bäumen)
	zond_tree_store_clear(
			ZOND_TREE_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(zond->treeview[BAUM_INHALT]))));
	zond_tree_store_clear(
			ZOND_TREE_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(zond->treeview[BAUM_AUSWERTUNG]))));

	// Clear filesystem root (safe to call even if not set)
	sond_treeviewfm_set_root(SOND_TREEVIEWFM(zond->treeview[BAUM_FS]), NULL, NULL);

	// Eingangsangaben (erst nach dem Leeren des Dateibaums, der sie abfragt)
	sond_eingang_cache_free(zond->eingang_cache);
	zond->eingang_cache = NULL;

	// Close databases if they were opened
	if (zond->dbase_zond) {
		project_clear_dbase_zond(&zond->dbase_zond);
	}

	// Free allocated strings and clear pointers
	g_free(zond->project_name);
	g_free(zond->project_dir);
	zond->project_name = NULL;
	zond->project_dir = NULL;
}

/* Pfad zu einer GGUF-Modelldatei ermitteln: konfigurierter GSettings-Wert
 * (falls per Einstellungsdialog gesetzt), sonst Standardablage
 * <exe_dir>/../models/<default_filename> - analog zum bestehenden
 * <exe_dir>/../share/tessdata-Muster. Leerer GSettings-Wert ('') bedeutet
 * "kein eigener Pfad gesetzt", genau wie beim schon vorhandenen
 * "project"-Schlüssel. */
gchar* resolve_model_path(Projekt* zond, gchar const* settings_key,
		gchar const* default_filename) {
	gchar *configured = g_settings_get_string(zond->settings, settings_key);

	if (configured && *configured)
		return configured;

	g_free(configured);
	return g_build_filename(zond->exe_dir, "../models", default_filename, NULL);
}

gchar* project_get_local_tmp_path(gchar const *project_path, GError **error) {
	gchar *hash = NULL;
	gchar *dir = NULL;
	gchar *filename = NULL;
	gchar *path_tmp = NULL;

	hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, project_path, -1);

	dir = g_build_filename(g_get_user_data_dir(), "zond", NULL);
	if (!sond_mkdir_with_parents(dir, error)) {
		g_free(hash);
		g_free(dir);
		return NULL;
	}

	filename = g_strconcat(hash, ".tmp", NULL);
	path_tmp = g_build_filename(dir, filename, NULL);

	g_free(filename);
	g_free(hash);
	g_free(dir);

	return path_tmp;
}

/* Sicherungskopie der Projektdatei, einmal pro geöffnetem Projekt, vor der
 * ersten Reparatur. *path_bak (g_free) nur beim Anlegen, sonst NULL.
 * Rückgabe 0 bei Erfolg (auch wenn schon vorhanden), -1 bei Fehler. */
gint project_sicherungskopie(Projekt *zond, gchar **path_bak, GError **error) {
	g_autofree gchar *path_src = NULL;
	g_autofree gchar *zeit = NULL;
	g_autoptr(GDateTime) jetzt = NULL;
	gchar *path_neu = NULL;

	if (path_bak)
		*path_bak = NULL;

	if (zond->dbase_zond->sicherung_gemacht)
		return 0;

	jetzt = g_date_time_new_now_local();
	zeit = g_date_time_format(jetzt, "%Y%m%d-%H%M%S");
	path_src = g_strdup_printf("%s/%s", zond->project_dir, zond->project_name);
	path_neu = g_strdup_printf("%s.vor-reparatur-%s", path_src, zeit);

	if (!sond_copy(path_src, path_neu, FALSE, error)) {
		g_free(path_neu);

		return -1;
	}

	zond->dbase_zond->sicherung_gemacht = TRUE;
	if (path_bak)
		*path_bak = path_neu;
	else
		g_free(path_neu);

	return 0;
}

/* Eingangsangaben des Projekts einlesen: die Zettel in .sond/eingang der
 * Library kommen in eine lokale Nachschlagetabelle (neben der lokalen
 * Arbeitsdatenbank, nie auf dem Cloud-Laufwerk). Ein Fehlschlag ist nicht
 * kritisch - der Dateibaum zeigt dann keine Eingangsangaben. */
static void project_eingang_cache_open(Projekt *zond, const gchar *project_path) {
	GError *error = NULL;
	gchar *path_tmp = NULL;
	gchar *path_cache = NULL;

	path_tmp = project_get_local_tmp_path(project_path, &error);
	if (!path_tmp) {
		LOG_WARN("%s: lokaler Pfad für die Eingangsangaben: %s", __func__,
				error ? error->message : "?");
		g_clear_error(&error);

		return;
	}

	path_cache = g_strconcat(path_tmp, ".eingang.db", NULL);
	g_free(path_tmp);

	zond->eingang_cache = sond_eingang_cache_open(path_cache,
			zond->project_dir, &error);
	g_free(path_cache);
	if (!zond->eingang_cache) {
		LOG_WARN("%s: Eingangsangaben nicht verfügbar: %s", __func__,
				error ? error->message : "?");
		g_clear_error(&error);

		return;
	}

	if (sond_eingang_cache_reload(zond->eingang_cache, NULL, &error)) {
		LOG_WARN("%s: Zettel nicht vollständig eingelesen: %s", __func__,
				error ? error->message : "?");
		g_clear_error(&error);
	}

	return;
}

/**
 * Open a project (create new or open existing)
 * @param zond The project structure
 * @param abs_path Absolute path to the project file
 * @param create TRUE to create new project, FALSE to open existing
 * @param error GError for error reporting
 * @return 0 on success, -1 on error, 1 if user cancelled
 */
gint project_open(Projekt *zond, const gchar *abs_path, gboolean create, GError **error) {
	gint rc = 0;
	gboolean repariert = FALSE;
	gboolean nach_laden_pruefen = FALSE;

	// Close current project if open
	rc = project_close(zond, error);
	if (rc) {
		if (rc == -1)
			return -1;
		else
			return 0;  // User cancelled
	}

	// Extract project name and directory from path
	zond->project_name = g_path_get_basename(abs_path);
	zond->project_dir = g_strndup(abs_path,
			strlen(abs_path) - strlen(zond->project_name) -
			(((strlen(abs_path) - strlen(zond->project_name)) > 1) ? 1 : 0));
	// Ensure that if dir is root, '/' remains

#ifdef _WIN32
	//Projektdatei auf SeaDrive: SQLite öffnet einen nicht geladenen Platzhalter nicht
	if (!create && sond_seadrive_needs_hydration(abs_path)) {
		GError *error_hydrate = NULL;

		if (!sond_seadrive_hydrate(abs_path, &error_hydrate)) {
			LOG_WARN("%s: sond_seadrive_hydrate('%s'): %s", __func__, abs_path,
					error_hydrate ? error_hydrate->message : "?");
			g_clear_error(&error_hydrate);
			g_set_error(error, SOND_FILE_ERROR_CLOUD, 0,
					"%s - Download gescheitert", zond->project_name);
			project_open_cleanup(zond);

			return -1;
		}
	}
#endif

	// Create or open databases
	rc = project_create_dbase_zond(zond, create, error);
	if (rc) {
		project_open_cleanup(zond);
		return -1;
	}

	// Eingangsangaben vor dem Dateibaum bereitstellen (er fragt sie beim Anzeigen ab)
	project_eingang_cache_open(zond, abs_path);

	// Load tree structures if opening existing project
	if (!create) {
		GError *error_load = NULL;

		rc = project_load_trees(zond, &error_load);
		if (rc) {
			/* Ein defekter Eintrag (z.B. ein Verweis ohne Ziel) kann das Laden
			 * verhindern. Dann das Diagnose-Fenster VOR dem Laden zeigen (modal,
			 * die Bäume gibt es noch nicht) und danach erneut versuchen. */
			g_clear_error(&error_load);
			repariert = db_check_fenster_vor_laden(zond);

			rc = project_load_trees(zond, error);
			if (rc) {
				g_prefix_error(error, "Die Bäume ließen sich nicht laden. Ist die "
						"Projektdatenbank defekt, wird beim erneuten Öffnen eine "
						"Prüfung mit Reparatur angeboten.\n\n");
				project_open_cleanup(zond);
				return -1;
			}
		} else
			nach_laden_pruefen = TRUE;
	}

	// Set filesystem root
	rc = sond_treeviewfm_set_root(SOND_TREEVIEWFM(zond->treeview[BAUM_FS]),
			zond->project_dir, error);
	if (rc) {
		project_open_cleanup(zond);
		return -1;
	}

	/* Einmalige Initialisierung des geteilten magic_t-Handles (s. Doc-
	 * Kommentar mime_guess_content_type_init(), sond_mime.c) - MUSS hier
	 * (Hauptthread, vor dem Start des Indizier-Hintergrundthreads gleich
	 * unten) passieren, nicht erst beim ersten tatsächlichen Aufruf.
	 * Fehlschlag ist unkritisch (mime_guess_content_type() fällt dann auf
	 * das alte, langsamere Open/Load/Close pro Aufruf zurück) - deshalb
	 * hier nur eine Warnung, kein Abbruch des Projekt-Ladens. */
	{
		GError *error_magic = NULL;
		if (!mime_guess_content_type_init(&error_magic)) {
			LOG_WARN("%s: mime_guess_content_type_init() fehlgeschlagen "
					"(unkritisch, nur langsamer): %s", __func__,
					error_magic->message);
			g_error_free(error_magic);
		}
	}

#ifdef _WIN32
	/* sond_process_file_create_wctx() unten ruft über sond_index_ctx_new()
	 * (sond_index.c) sqlite3_open() auf die eigene Volltextindex-Datenbank
	 * (.sond_index.db) auf. SQLite nutzt dafür SEINE EIGENE Windows-VFS -
	 * anders als bei der .sond_index.db-shm-Begleitdatei (s. sond_fopen())
	 * greift hier also weder sond_fopen() noch dessen eingebaute
	 * Hydrierung-und-Retry-Logik, das schlägt bei einem nicht hydrierten
	 * SeaDrive-Platzhalter einfach mit "unable to open database file" fehl.
	 * Da SQLite grundsätzlich keinen partiellen/gestreamten Zugriff auf eine
	 * Cloud-Datei unterstützt, MUSS die Datei vollständig lokal vorliegen,
	 * bevor sqlite3_open() überhaupt versucht wird - anders als beim Öffnen
	 * einer einzelnen Nutzer-Datei (dort Fire-and-forget + sofortige Rückkehr
	 * an die UI möglich, s. sond_treeviewfm_open()) gibt es hier keine
	 * Alternative zu synchronem (blockierendem) Warten: ohne geöffnete
	 * Index-DB kann das Projekt nicht sinnvoll weiterladen. Geprüft werden
	 * alle drei möglichen Dateien (.sond_index.db selbst sowie die
	 * SQLite-eigenen WAL-Begleitdateien -wal/-shm, die nur existieren, wenn
	 * beim letzten Schließen kein Checkpoint durchlief) - für ein neues
	 * Projekt (Datei existiert noch nicht) liefert sond_seadrive_needs_
	 * hydration() dabei einfach FALSE, kein Sonderfall nötig. */
	if (sond_treeviewfm_is_seadrive_path(SOND_TREEVIEWFM(zond->treeview[BAUM_FS]))) {
		static const gchar *index_db_suffixes[] = { "", "-wal", "-shm" };
		guint i;

		for (i = 0; i < G_N_ELEMENTS(index_db_suffixes); i++) {
			gchar *full_path = g_strdup_printf("%s/.sond_index.db%s",
					zond->project_dir, index_db_suffixes[i]);

			if (sond_seadrive_needs_hydration(full_path)) {
				GError *error_hydrate = NULL;

				if (!sond_seadrive_hydrate(full_path, &error_hydrate)) {
					gchar *base = g_path_get_basename(full_path);

					LOG_WARN("%s: sond_seadrive_hydrate('%s'): %s", __func__,
							full_path,
							error_hydrate ? error_hydrate->message : "?");
					g_clear_error(&error_hydrate);

					//ohne Index-DB lädt das Projekt nicht sinnvoll weiter
					g_set_error(error, SOND_FILE_ERROR_CLOUD, 0,
							"%s - Download gescheitert", base);
					g_free(base);
					g_free(full_path);
					project_open_cleanup(zond);

					return -1;
				}
			}
			g_free(full_path);
		}
	}
#endif

	gchar* datadir = g_build_filename(zond->exe_dir, "../share/tessdata", NULL);
	gchar* embedding_model_path = resolve_model_path(zond, "embedding-model-path",
			"Qwen3-Embedding-0.6B-Q8_0.gguf");
	zond->wctx = sond_process_file_create_wctx(
			(void (*)(gpointer, gchar const*, ...)) info_window_set_message_thread_safe,
			NULL, datadir, 4, ".sond_index.db", embedding_model_path,
			zond->project_dir, error);
	g_free(datadir);
	g_free(embedding_model_path);
	if (!zond->wctx) {
		project_open_cleanup(zond);

		return -1;
	}

	// Success - enable widgets and finalize
	project_set_widgets_sensitive(zond, TRUE);

	// Set window title
	gtk_header_bar_set_title(
			GTK_HEADER_BAR(gtk_window_get_titlebar(GTK_WINDOW(zond->app_window))),
			zond->project_name);

	// Save project path to settings
	g_settings_set_string(zond->settings, "project", abs_path);

	//nach einer Reparatur vor dem Laden ist die Arbeitskopie nicht gespeichert
	project_reset_changed(zond, repariert);

	// Enable autosave if configured
	if (g_settings_get_boolean(zond->settings, "autosave"))
		g_timeout_add_seconds(AUTOSAVE_INTERVAL_SECONDS, project_timeout_autosave, zond);

	//Bäume sind geladen: bei Mängeln das Diagnose-Fenster daneben zeigen
	if (nach_laden_pruefen)
		db_check_fenster_oeffnen_wenn_maengel(zond);

	return 0;
}

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Ask user if they want to close current project
 * @param zond The project structure
 * @return 0 to proceed, 1 to cancel
 */
static gint project_confirm_switch(Projekt *zond) {
	gint rc = 0;

	if (!zond->dbase_zond)
		return 0;

	rc = abfrage_frage(zond->app_window, zond->project_name,
			"Projekt schließen?", NULL);
	if (rc != GTK_RESPONSE_YES)
		return 1;  // Cancel

	return 0;
}

/*
 * filepart_oeffnen(): container-bewusste Alternative zu filename_oeffnen()
 * (misc.c) - der normale GTK-Dateiauswahldialog (GtkFileChooserDialog)
 * kann nicht in einen noch nicht entpackten Container (ZIP etc.)
 * hinabsteigen. Eine vorherige Markierung im Dateiverzeichnis wäre keine
 * Alternative: die Selektion in BAUM_FS geht verloren, sobald im
 * Bestandsverzeichnis die Zielposition markiert wird - zwei Selektionen
 * gleichzeitig "lebendig" zu halten ist wenig intuitiv.
 *
 * Löst das statt dessen mit einem eigenen modalen Dialog, der einen
 * frischen ZondTreeviewFM einbettet - dieselbe Baum-Komponente wie das
 * normale Dateiverzeichnis (BAUM_FS), kann also genauso in ZIP-Archive
 * hinabsteigen (Aufklappen eines Knotens lädt dessen Kinder wie gewohnt
 * per "row-expanded"-Signal, s. sond_treeviewfm.c) - unabhängig von der
 * Selektion im Dateiverzeichnis des Hauptfensters. sond_treeviewfm_set_root()
 * wird mit demselben zond->project_dir wie beim Hauptfenster-Baum
 * aufgerufen - der klassenweite path_root (s. DESIGNFEHLER-Kommentar in
 * sond_fileparts.h) bleibt dadurch unverändert derselbe, nur der neue
 * Dialog-Baum bekommt seine eigene, frisch geladene Wurzelebene.
 *
 * extra (optional, NULL erlaubt): zusätzliches Widget (z.B. Checkboxen)
 * unterhalb des Dateibaums. Wird zusammen mit dem Dialog zerstört -
 * Zustände daher per Signalhandler in Aufruferdaten festhalten.
 *
 * Rückgabe: neue Referenz auf das ausgewählte SondFilePart (Aufrufer muss
 * g_object_unref()en), oder NULL - dann bedeutet *error == NULL: Nutzer
 * hat abgebrochen (Abbrechen-Knopf oder Fenster geschlossen); *error
 * gesetzt: echter Fehler (z.B. Wurzel des Dateiverzeichnisses konnte
 * nicht geladen werden, oder es war kein Verzeichnis, sondern eine Datei
 * markiert).
 */
SondFilePart* filepart_oeffnen(Projekt *zond, GtkWidget *extra,
		GError **error) {
	GtkWidget *dialog = NULL;
	GtkWidget *scrolled = NULL;
	ZondTreeviewFM *ztvfm = NULL;
	SondFilePart *result = NULL;
	gint response = 0;
	gint rc = 0;

	ztvfm = zond_treeviewfm_new(zond);

	rc = sond_treeviewfm_set_root(SOND_TREEVIEWFM(ztvfm), zond->project_dir,
			error);
	if (rc) {
		gtk_widget_destroy(GTK_WIDGET(ztvfm));
		if (extra) { //schwebendes Widget, das nie eingebaut wird
			g_object_ref_sink(extra);
			gtk_widget_destroy(extra);
			g_object_unref(extra);
		}
		return NULL;
	}

	dialog = gtk_dialog_new_with_buttons("Datei auswählen",
			GTK_WINDOW(zond->app_window),
			GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
			"Öffnen", GTK_RESPONSE_OK,
			"Abbrechen", GTK_RESPONSE_CANCEL,
			NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
	gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 600);

	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
			GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	/* Ohne min-content-height/-width fällt GtkScrolledWindow auf die
	 * (sehr kleine) natürliche Größe des GtkTreeView zurück (der
	 * Scrollbereich wäre nur ca. zwei Zeilen hoch). Explizit
	 * gesetzt statt sich auf gtk_window_set_default_size() allein zu
	 * verlassen (setzt nur die Fenster-Startgröße, erzwingt aber keine
	 * Mindestgröße des Scroll-Bereichs selbst). */
	gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scrolled),
			500);
	gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scrolled),
			480);
	gtk_widget_set_vexpand(scrolled, TRUE);
	gtk_widget_set_hexpand(scrolled, TRUE);
	gtk_container_add(GTK_CONTAINER(scrolled), GTK_WIDGET(ztvfm));
	gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(
			GTK_DIALOG(dialog))), scrolled);
	if (extra)
		gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(
				GTK_DIALOG(dialog))), extra, FALSE, FALSE, 6);

	gtk_widget_show_all(dialog);
	response = my_dialog_run(GTK_DIALOG(dialog));

	if (response == GTK_RESPONSE_OK) {
		GtkTreeIter iter = { 0 };

		if (sond_treeview_get_cursor(SOND_TREEVIEW(ztvfm), &iter)) {
			SondTVFMItem *stvfm_item = NULL;

			gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(ztvfm)),
					&iter, 0, &stvfm_item, -1);
			g_object_unref(stvfm_item); //Modell hält eigene Referenz

			if (sond_tvfm_item_get_item_type(stvfm_item)
					!= SOND_TVFM_ITEM_TYPE_DIR) {
				SondFilePart *sfp = sond_tvfm_item_get_sond_file_part(
						stvfm_item);

				if (sfp)
					result = g_object_ref(sfp);
			}
		}

		if (!result)
			g_set_error(error, ZOND_ERROR, 0,
					"Bitte eine Datei (kein Verzeichnis) auswählen.");
	}
	//sonst: Abbruch (GTK_RESPONSE_CANCEL, Fenster geschlossen o.ä.) -
	//result bleibt NULL, kein *error gesetzt

	gtk_widget_destroy(dialog);

	return result;
}

// ============================================================================
// Menu Callbacks
// ============================================================================

/**
 * Load existing project
 */
gint project_load(Projekt* zond, GError** error) {
	gint rc = 0;
	gchar *start_dir = NULL;

	rc = project_confirm_switch(zond);
	if (rc)
		return 0;

	/* Das noch geöffnete Projekt (project_close() läuft erst gleich in
	 * project_open()) kennt sein eigenes project_dir (das komplette, ggf.
	 * riesige SeaDrive-Fallakten-verzeichnis) - dessen ELTERNverzeichnis
	 * (typischerweise nur eine Handvoll Fallakten-Ordner) ist ein deutlich
	 * günstigerer Startpunkt für den Dateiauswahldialog als das zufällige
	 * aktuelle Arbeitsverzeichnis (s. ausführlichen Kommentar an
	 * filename_oeffnen(), misc.c). */
	if (zond->project_dir)
		start_dir = g_path_get_dirname(zond->project_dir);

	gchar *abs_path = filename_oeffnen(GTK_WINDOW(zond->app_window), start_dir);
	g_free(start_dir);
	if (!abs_path)
		return 0;

	rc = project_open(zond, abs_path, FALSE, error);
	g_free(abs_path);
	if (rc)
		return -1;

	return 0;
}

/**
 * Create new project
 */
gint project_new(Projekt* zond, GError** error) {
	gint rc = 0;

	rc = project_confirm_switch(zond);
	if (rc)
		return 0;

	gchar *abs_path = filename_speichern(GTK_WINDOW(zond->app_window),
			"Projekt anlegen", ".ZND");
	if (!abs_path)
		return 0;

	rc = project_open(zond, abs_path, TRUE, error);
	g_free(abs_path);
	if (rc)
		return -1;

	return 0;
}
