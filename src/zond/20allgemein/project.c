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
#include "../../sond_treeviewfm.h"
#include "../../sond_process_file.h"
#include "../../misc.h"

#include "../zond_init.h"
#include "../zond_dbase.h"
#include "../zond_treeview.h"
#include "../zond_pdf_document.h"

#include "../10init/app_window.h"
#include "../40viewer/document.h"
#include "../99conv/general.h"

#include "../40viewer/viewer.h"
#include "../40viewer/viewer_save.h"
#include "../zond_tree_store.h"

#include "project.h"

// Constants
#define AUTOSAVE_INTERVAL_SECONDS (10 * 60)  // 10 minutes

// ============================================================================
// Database Transaction Functions
// ============================================================================

gint dbase_zond_begin(DBaseZond* dbase_zond, GError** error) {
	gint rc = 0;

	/* journal_mode/synchronous können sich jederzeit von außen geändert
	 * haben (s. zond_dbase_check_journal_settings()) - deshalb hier vor
	 * jeder Dual-Write-Transaktion erneut geprüft, nicht nur beim Öffnen.
	 * Für BEIDE Dateien, obwohl die Transaktion selbst nur noch auf EINER
	 * Connection läuft (s.u.) - work hat trotz ATTACH weiterhin seine
	 * eigene, unabhängige Datei/Verbindung für alle Einzel-DB-Operationen,
	 * deren journal_mode/synchronous unabhängig von außen verstellt worden
	 * sein könnte. */
	rc = zond_dbase_check_journal_settings(dbase_zond->zond_dbase_store, error);
	if (rc)
		return -1;

	rc = zond_dbase_check_journal_settings(dbase_zond->zond_dbase_work, error);
	if (rc)
		return -1;

	/* Nur noch EIN BEGIN auf der store-Connection - work ist per ATTACH
	 * (project_create_dbase_zond()) als zweites Schema mit eingehängt,
	 * die Dual-Write-Funktionen (dbase_zond_update_sections()/_path()/
	 * _gmessage_index()) schreiben schema-qualifiziert auf main+work
	 * innerhalb dieser einen Transaktion (ToDo.c, Architektur-Plan
	 * Atomarität store/work, Punkt 4). */
	return zond_dbase_begin(dbase_zond->zond_dbase_store, error);
}

void dbase_zond_rollback(DBaseZond* dbase_zond, GError** error) {
	GError* error_int = NULL;

	/* Nur noch EIN ROLLBACK (Punkt 4) - die alte Merge-Logik für ZWEI
	 * unabhängige Rollback-Fehler (store- und work-Connection) entfällt
	 * dadurch (Punkt 5). Trotzdem weiterhin über einen lokalen error_int,
	 * NICHT direkt über den übergebenen error-Parameter: der hält an
	 * dieser Stelle i.d.R. schon den Fehler, WEGEN dem gerade
	 * zurückgerollt wird (z.B. den gescheiterten Commit, s.
	 * dbase_zond_commit()) - würde das fehlschlagende ROLLBACK-Statement
	 * selbst *error direkt überschreiben, ginge dieser eigentliche Grund
	 * verloren (und die alte GError würde geleakt). Daher: anhängen statt
	 * überschreiben, wie zuvor. */
	zond_dbase_rollback(dbase_zond->zond_dbase_store, &error_int);
	if (!error_int)
		return;   //Normalfall: Rollback erfolgreich

	if (!error)
		g_error_free(error_int);
	else if (*error) {
		(*error)->message = add_string((*error)->message,
				g_strdup_printf("\n\n%s", error_int->message));
		g_error_free(error_int);
	} else
		*error = error_int;

	return;
}

gint dbase_zond_commit(DBaseZond* dbase_zond, GError** error) {
	gint rc = 0;

	/* Nur noch EIN COMMIT (Punkt 4). Der frühere "Katastrophe"-Fall (zweiter
	 * Commit scheitert, nachdem der erste schon erfolgreich war -
	 * store/work dadurch inkonsistent) kann strukturell nicht mehr
	 * auftreten, weil es nur noch einen einzigen Commit auf einer
	 * einzigen Connection gibt - der schlägt entweder ganz oder gar nicht
	 * fehl (vorausgesetzt journal_mode/synchronous halten, s.
	 * zond_dbase_check_journal_settings() oben). Entfällt daher
	 * ersatzlos (Punkt 4/5). */
	rc = zond_dbase_commit(dbase_zond->zond_dbase_store, error);
	if (rc) {
		dbase_zond_rollback(dbase_zond, error);
		return -1;
	}

	return 0;
}

// ============================================================================
// Database Update Functions
// ============================================================================

/* Alle drei folgenden Funktionen laufen auf der EINEN store-Connection mit
 * angehängtem work-Schema (project_create_dbase_zond(), ATTACH DATABASE
 * ... AS work) - dieselbe schema-qualifizierte SQL wird einmal für "main"
 * (=store) und einmal für "work" ausgeführt, innerhalb derselben, von
 * dbase_zond_begin() geöffneten Transaktion (ToDo.c, Architektur-Plan
 * Atomarität store/work, Punkt 3). work behält daneben unverändert seine
 * eigene Connection für alle anderen (Einzel-DB-)Operationen. */

/* Sections müssen für main und work UNABHÄNGIG gelesen und neu berechnet
 * werden (nicht: einmal berechnen, in beide schreiben) - store und work
 * können für dieselbe Zeile unterschiedliche Ausgangswerte haben, da work
 * laufend fortgeschrieben, store aber nur bei project_save() komplett
 * nachgezogen wird (Diskussion 03.09.2026). */
static gint dbase_zond_update_section_schema(sqlite3 *db, gchar const *schema,
		DisplayedDocument *dd, GError **error) {
	gint rc = 0;
	gchar *sql = NULL;
	sqlite3_stmt *stmt = NULL;
	SondFilePartPDF *sfp_pdf = NULL;
	gchar *filepart = NULL;
	g_autoptr(GArray) arr_sections = NULL;

	sfp_pdf = zond_pdf_document_get_sfp_pdf(dd->zpdfd_part->zond_pdf_document);
	filepart = sond_file_part_get_filepart(SOND_FILE_PART(sfp_pdf));

	sql = g_strdup_printf("SELECT ID, section FROM %s.knoten WHERE file_part=?1;",
			schema);
	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	g_free(sql);
	if (rc != SQLITE_OK) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("SQLITE3"), rc,
					"%s (%s): %s", __func__, schema, sqlite3_errmsg(db));
		g_free(filepart);
		return -1;
	}
	sqlite3_bind_text(stmt, 1, filepart, -1, SQLITE_TRANSIENT);
	g_free(filepart);

	arr_sections = g_array_new(FALSE, FALSE, sizeof(Section));
	g_array_set_clear_func(arr_sections, (GDestroyNotify) section_free);

	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		Section section = { 0 };

		section.ID = sqlite3_column_int(stmt, 0);
		section.section = g_strdup((gchar const*) sqlite3_column_text(stmt, 1));

		//wenn section == NULL, dann brauch es nicht gespeichert zu werden,
		//ist ja die ganze PDF-Datei - keine Anpassung nötig
		if (section.section)
			g_array_append_val(arr_sections, section);
	}
	sqlite3_finalize(stmt);

	if (rc != SQLITE_DONE) {
		if (error)
			*error = g_error_new(g_quark_from_static_string("SQLITE3"), rc,
					"%s (%s): %s", __func__, schema, sqlite3_errmsg(db));
		return -1;
	}

	for (guint i = 0; i < arr_sections->len; i++) {
		Section section = g_array_index(arr_sections, Section, i);
		Anbindung anbindung_int = { 0 };
		gchar *section_new = NULL;
		gchar *sql_update = NULL;
		sqlite3_stmt *stmt_update = NULL;

		anbindung_parse_file_section(section.section, &anbindung_int);
		anbindung_aktualisieren(dd->zpdfd_part->zond_pdf_document, &anbindung_int);
		// Recalculate changes that will be removed during save
		anbindung_korrigieren(dd->zpdfd_part, &anbindung_int);
		anbindung_build_file_section(anbindung_int, &section_new);

		sql_update = g_strdup_printf("UPDATE %s.knoten SET section=?2 WHERE ID=?1;",
				schema);
		rc = sqlite3_prepare_v2(db, sql_update, -1, &stmt_update, NULL);
		g_free(sql_update);
		if (rc != SQLITE_OK) {
			if (error)
				*error = g_error_new(g_quark_from_static_string("SQLITE3"), rc,
						"%s (%s): %s", __func__, schema, sqlite3_errmsg(db));
			g_free(section_new);
			return -1;
		}

		sqlite3_bind_int(stmt_update, 1, section.ID);
		sqlite3_bind_text(stmt_update, 2, section_new, -1, SQLITE_TRANSIENT);
		g_free(section_new);

		rc = sqlite3_step(stmt_update);
		sqlite3_finalize(stmt_update);
		if (rc != SQLITE_DONE) {
			if (error)
				*error = g_error_new(g_quark_from_static_string("SQLITE3"), rc,
						"%s (%s): %s", __func__, schema, sqlite3_errmsg(db));
			return -1;
		}
	}

	return 0;
}

gint dbase_zond_update_sections(DBaseZond* dbase_zond, DisplayedDocument* dd,
		GError** error) {
	sqlite3 *db = zond_dbase_get_dbase(dbase_zond->zond_dbase_store);
	gint rc = 0;

	rc = dbase_zond_update_section_schema(db, "main", dd, error);
	if (rc)
		return -1;

	rc = dbase_zond_update_section_schema(db, "work", dd, error);
	if (rc)
		return -1;

	return 0;
}

gint dbase_zond_update_path(DBaseZond* dbase_zond, gchar const* prefix_old,
		gchar const* prefix_new, GError** error) {
	sqlite3 *db = zond_dbase_get_dbase(dbase_zond->zond_dbase_store);
	static gchar const *schemas[] = { "main", "work" };

	for (gint i = 0; i < 2; i++) {
		gchar *sql = NULL;
		sqlite3_stmt *stmt = NULL;
		gint rc = 0;

		sql = g_strdup_printf("UPDATE %s.knoten SET file_part = "
				"REPLACE( SUBSTR( file_part, 1, LENGTH( ?1 ) ), ?1, ?2 ) || "
				"SUBSTR( file_part, LENGTH( ?1 ) + 1 );", schemas[i]);
		rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
		g_free(sql);
		if (rc != SQLITE_OK) {
			if (error)
				*error = g_error_new(g_quark_from_static_string("SQLITE3"), rc,
						"%s (%s): %s", __func__, schemas[i], sqlite3_errmsg(db));
			return -1;
		}

		sqlite3_bind_text(stmt, 1, prefix_old, -1, SQLITE_STATIC);
		sqlite3_bind_text(stmt, 2, prefix_new, -1, SQLITE_STATIC);

		rc = sqlite3_step(stmt);
		sqlite3_finalize(stmt);
		if (rc != SQLITE_DONE) {
			if (error)
				*error = g_error_new(g_quark_from_static_string("SQLITE3"), rc,
						"%s (%s): %s", __func__, schemas[i], sqlite3_errmsg(db));
			return -1;
		}
	}

	return 0;
}

gint dbase_zond_update_gmessage_index(DBaseZond* dbase_zond,
		gchar const* prefix, gint index, gboolean into, GError** error) {
	sqlite3 *db = zond_dbase_get_dbase(dbase_zond->zond_dbase_store);
	static gchar const *schemas[] = { "main", "work" };

	for (gint i = 0; i < 2; i++) {
		gchar *sql = NULL;
		sqlite3_stmt *stmt = NULL;
		gint rc = 0;

		sql = g_strdup_printf(
				"UPDATE %s.knoten "
				"SET file_part = ?1 || " //?1 ist prefix
				"(CAST(SUBSTR(SUBSTR(file_part, LENGTH(?1) + 1), 1, "
				"INSTR(SUBSTR(file_part, LENGTH(?1) + 1) || '/', '/') - 1) "
				"AS INTEGER) + ?2) || " //?2 ist Zahl, die hinzugesetzt/abgezogen wird
				"SUBSTR(SUBSTR(file_part, LENGTH(?1) + 1), "
				"INSTR(SUBSTR(file_part, LENGTH(?1) + 1) || '/', '/')) "
				"WHERE file_part LIKE ?1 || '%%' "
				"AND CAST(SUBSTR(SUBSTR(file_part, LENGTH(?1) + 1), 1, "
						"INSTR(SUBSTR(file_part, LENGTH(?1) + 1) || '/', '/') - 1) "
						"AS INTEGER) >= ?3; ", //?3 ist Schwellenwert
				schemas[i]);
		rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
		g_free(sql);
		if (rc != SQLITE_OK) {
			if (error)
				*error = g_error_new(g_quark_from_static_string("SQLITE3"), rc,
						"%s (%s): %s", __func__, schemas[i], sqlite3_errmsg(db));
			return -1;
		}

		sqlite3_bind_text(stmt, 1, prefix, -1, SQLITE_STATIC);
		sqlite3_bind_int(stmt, 2, into ? 1 : -1);
		sqlite3_bind_int(stmt, 3, index);

		rc = sqlite3_step(stmt);
		sqlite3_finalize(stmt);
		if (rc != SQLITE_DONE) {
			if (error)
				*error = g_error_new(g_quark_from_static_string("SQLITE3"), rc,
						"%s (%s): %s", __func__, schemas[i], sqlite3_errmsg(db));
			return -1;
		}
	}

	return 0;
}

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
	gtk_widget_set_sensitive(GTK_WIDGET(zond->treeview[BAUM_FS]), active);
	gtk_widget_set_sensitive(GTK_WIDGET(zond->treeview[BAUM_INHALT]), active);
	gtk_widget_set_sensitive(GTK_WIDGET(zond->treeview[BAUM_AUSWERTUNG]), active);

	g_simple_action_set_enabled(zond->menu.schliessen,  active);
	g_simple_action_set_enabled(zond->menu.export_odt,  active);
	g_simple_action_set_enabled(zond->menu.pdf,         active);
	g_simple_action_set_enabled(zond->menu.struktur,    active);
	g_simple_action_set_enabled(zond->menu.ansicht,     active);
	gtk_widget_set_sensitive(zond->fs_button,           active);
	g_simple_action_set_enabled(zond->menu.extras,      active);

	if (!active)
		g_simple_action_set_enabled(zond->menu.speichern, FALSE);

	return;
}

// ============================================================================
// Database Creation and Cleanup
// ============================================================================

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

	path_tmp = g_strconcat(path, ".tmp", NULL);

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

	/* work zusätzlich als zweites Schema an store anhängen (ToDo.c,
	 * Architektur-Plan Atomarität store/work, Punkt 2): work behält
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
			if (save_rc)
				return -1;
		} else if (rc != GTK_RESPONSE_NO) {
			return 1;  // User cancelled
		}
	}

	// Close all viewer windows
	for (gint i = 0; i < zond->arr_pv->len; i++)
		viewer_save_and_close(g_ptr_array_index(zond->arr_pv, i));

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

	gchar *working_copy = g_strconcat(zond->project_dir, "/", zond->project_name, ".tmp", NULL);

	sond_treeviewfm_set_root(SOND_TREEVIEWFM(zond->treeview[BAUM_FS]), NULL, NULL);
	project_clear_dbase_zond(&(zond->dbase_zond));

	// Disable autosave if active
	if (g_settings_get_boolean(zond->settings, "autosave")) {
		if (!g_source_remove_by_user_data(zond))
			display_message(zond->app_window,
					"autosave-Timeout konnte nicht entfernt werdern", NULL);
	}

	// Remove temporary database
	if (!sond_remove(working_copy, &error_remove)) {
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

	// Create or open databases
	rc = project_create_dbase_zond(zond, create, error);
	if (rc) {
		project_open_cleanup(zond);
		return -1;
	}

	// Load tree structures if opening existing project
	if (!create) {
		rc = project_load_trees(zond, error);
		if (rc) {
			project_open_cleanup(zond);
			return -1;
		}
	}

	// Set filesystem root
	rc = sond_treeviewfm_set_root(SOND_TREEVIEWFM(zond->treeview[BAUM_FS]),
			zond->project_dir, error);
	if (rc) {
		project_open_cleanup(zond);
		return -1;
	}

	gchar* datadir = g_build_filename(zond->exe_dir, "../share/tessdata", NULL);
	gchar* embedding_model_path = resolve_model_path(zond, "embedding-model-path",
			"Qwen3-Embedding-0.6B-Q8_0.gguf");
	zond->wctx = sond_process_file_create_wctx(zond->ctx,
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

	project_reset_changed(zond, FALSE);

	// Enable autosave if configured
	if (g_settings_get_boolean(zond->settings, "autosave"))
		g_timeout_add_seconds(AUTOSAVE_INTERVAL_SECONDS, project_timeout_autosave, zond);

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

// ============================================================================
// Menu Callbacks
// ============================================================================

/**
 * Load existing project
 */
gint project_load(Projekt* zond, GError** error) {
	gint rc = 0;

	rc = project_confirm_switch(zond);
	if (rc)
		return 0;

	gchar *abs_path = filename_oeffnen(GTK_WINDOW(zond->app_window));
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
