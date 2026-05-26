/*
 zond (headerbar.c) - Akten, Beweisstücke, Unterlagen
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
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <sqlite3.h>
#include <glib/gstdio.h>
#include <tesseract/capi.h>

#include "../../misc.h"
#include "../../sond_ocr.h"
#include "../../sond_fileparts.h"
#include "../../sond_treeview.h"
#include "../../sond_treeviewfm.h"
#include "../../sond_log_and_error.h"
#include "../../sond_file_helper.h"
#include "../../sond_process_file.h"
#include "../../sond_treeviewfm_seadrive.h"

#include "../zond_pdf_document.h"
#include "../zond_tree_store.h"
#include "../zond_treeview.h"
#include "../zond_dbase.h"
#include "../zond_indexsuche.h"
#include "../zond_treeviewfm.h"
#include "../zond_init.h"

#include "../20allgemein/ziele.h"
#include "../20allgemein/suchen.h"
#include "../20allgemein/project.h"
#include "../20allgemein/export.h"
#include "../20allgemein/zond_update.h"
#include "../40viewer/viewer.h"
#include "../40viewer/document.h"
#include "../99conv/test.h"


/* ============================================================================
 * HILFSFUNKTIONEN
 * ========================================================================== */
static void activate_baum_action(Projekt *zond, const gchar *action_name) {
	if (zond->baum_active == KEIN_BAUM)
		return;
	GActionGroup *ag = gtk_widget_get_action_group(
			GTK_WIDGET(zond->treeview[zond->baum_active]), "stv");
	if (ag && g_action_group_has_action(ag, action_name))
		g_action_group_activate_action(ag, action_name, NULL);
}

/* ============================================================================
 * CALLBACKS - PROJEKT
 * ========================================================================== */

static void cb_app_projekt_neu(GSimpleAction *a, GVariant *p, gpointer d) {
	gchar *errmsg = NULL;
	Projekt *zond = (Projekt*) d;
	if (project_new(zond, &errmsg)) {
		display_message(zond->app_window, "Fehler beim Anlegen des Projekts\n", errmsg, NULL);
		g_free(errmsg);
	}
}

static void cb_app_projekt_oeffnen(GSimpleAction *a, GVariant *p, gpointer d) {
	gchar *errmsg = NULL;
	Projekt *zond = (Projekt*) d;
	if (project_load(zond, &errmsg)) {
		display_error(zond->app_window, "Fehler beim Laden des Projekts", errmsg);
		g_free(errmsg);
	}
}

static void cb_app_projekt_schliessen(GSimpleAction *a, GVariant *p, gpointer d) {
	gchar *errmsg = NULL;
	Projekt *zond = (Projekt*) d;
	if (project_close(zond, &errmsg) == -1) {
		display_message(zond->app_window, "Fehler beim Schließen des Projekts\n", errmsg, NULL);
		g_free(errmsg);
	}
}

static void cb_app_projekt_speichern(GSimpleAction *a, GVariant *p, gpointer d) {
	gchar *errmsg = NULL;
	Projekt *zond = (Projekt*) d;
	if (project_save(zond, &errmsg)) {
		display_message(zond->app_window, "Fehler beim Speichern des Projekts\n", errmsg, NULL);
		g_free(errmsg);
	}
}

static void cb_app_beenden(GSimpleAction *a, GVariant *p, gpointer d) {
	gboolean ret = FALSE;
	Projekt *zond = (Projekt*) d;
	g_signal_emit_by_name(zond->app_window, "delete-event", NULL, &ret);
}

static void cb_win_export(GSimpleAction *a, GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d;
	gint umfang = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(a), "umfang"));
	GError* error = NULL;
	if (export_activate(zond, umfang, &error)) {
		display_message(zond->app_window, "Export fehlgeschlagen\n", error->message, NULL);
		g_error_free(error);
	}
}

/* ============================================================================
 * INDEX
 * ========================================================================== */

struct _ThreadDataIndex {
	SondTreeviewFM *treeviewfm;
	SondProcessFileCtx *wctx;
	GHashTable *ht_index;
	gint done;
};

static gpointer do_index_thread(gpointer data) {
	struct _ThreadDataIndex *td = (struct _ThreadDataIndex*) data;
	sond_process_fileparts(td->wctx, td->ht_index);
	g_atomic_int_set(&td->done, 1);
	return NULL;
}

static void do_index_erstellen(Projekt *zond, gboolean sel_only) {
	GError *error = NULL;
	GHashTable *ht_index = NULL;
	InfoWindow *info_window = NULL;

	if (zond->baum_active == BAUM_FS || !sel_only)
		ht_index = sond_treeviewfm_get_fileparts(
				SOND_TREEVIEWFM(zond->treeview[BAUM_FS]), sel_only, &error);
	else
		ht_index = zond_treeview_get_selected_fileparts(
				ZOND_TREEVIEW(zond->treeview[zond->baum_active]), &error);
	if (!ht_index) {
		display_message(zond->app_window, "Fehler beim Erstellen des Index:\n",
				error->message, NULL);
		g_error_free(error);
		return;
	}
	info_window = info_window_open(zond->app_window, &zond->wctx->cancel,
			"Index erstellen");
	zond->wctx->log_func_data = (gpointer) info_window;
	zond->wctx->cancel = 0;
	struct _ThreadDataIndex *td = g_new0(struct _ThreadDataIndex, 1);
	td->treeviewfm = SOND_TREEVIEWFM(zond->treeview[BAUM_FS]);
	td->ht_index = ht_index;
	td->wctx = zond->wctx;
	info_window_set_message(info_window, "Indizierung wird gestartet");
	GThread *thread = g_thread_new("ocr-doc", do_index_thread, td);
	if (!thread) {
		info_window_set_message(info_window, "Thread konnte nicht erzeugt werden");
		g_free(td);
		return;
	}
	while (!g_atomic_int_get(&td->done))
		gtk_main_iteration_do(FALSE);
	g_thread_join(thread);
	g_free(td);
	info_window_close(info_window);
}

static void cb_app_index_erstellen(GSimpleAction *a, GVariant *p, gpointer d) {
	do_index_erstellen((Projekt*) d, FALSE);
}

static void cb_app_indexsuche(GSimpleAction *a, GVariant *p, gpointer d) {
	zond_indexsuche_activate(NULL, d);
}

static void cb_win_index_erstellen_sel(GSimpleAction *a, GVariant *p, gpointer d) {
	do_index_erstellen((Projekt*) d, TRUE);
}

static void cb_win_indexsuche_auswahl(GSimpleAction *a, GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d;
	if (zond->baum_active == BAUM_FS)
		zond_indexsuche_activate_with_selection(NULL, NULL, d);
	else
		zond_indexsuche_activate(NULL, d);
}

/* ============================================================================
 * CALLBACKS - BEARBEITEN (Weiterleitungs-Actions)
 * ========================================================================== */

static void cb_win_einf_ge(GSimpleAction *a, GVariant *p, gpointer d) {
	activate_baum_action((Projekt*) d, "einf-ge"); }
static void cb_win_einf_up(GSimpleAction *a, GVariant *p, gpointer d) {
	activate_baum_action((Projekt*) d, "einf-up"); }
static void cb_win_kopieren(GSimpleAction *a, GVariant *p, gpointer d) {
	activate_baum_action((Projekt*) d, "kopieren"); }
static void cb_win_ausschneiden(GSimpleAction *a, GVariant *p, gpointer d) {
	activate_baum_action((Projekt*) d, "ausschneiden"); }
static void cb_win_paste_ge(GSimpleAction *a, GVariant *p, gpointer d) {
	activate_baum_action((Projekt*) d, "paste-ge"); }
static void cb_win_paste_up(GSimpleAction *a, GVariant *p, gpointer d) {
	activate_baum_action((Projekt*) d, "paste-up"); }
static void cb_win_paste_link_ge(GSimpleAction *a, GVariant *p, gpointer d) {
	activate_baum_action((Projekt*) d, "paste-link-ge"); }
static void cb_win_paste_link_up(GSimpleAction *a, GVariant *p, gpointer d) {
	activate_baum_action((Projekt*) d, "paste-link-up"); }
static void cb_win_loeschen(GSimpleAction *a, GVariant *p, gpointer d) {
	activate_baum_action((Projekt*) d, "loeschen"); }
static void cb_win_anb_entf(GSimpleAction *a, GVariant *p, gpointer d) {
	activate_baum_action((Projekt*) d, "anb-entf"); }
static void cb_win_jump(GSimpleAction *a, GVariant *p, gpointer d) {
	activate_baum_action((Projekt*) d, "jump"); }
static void cb_win_oeffnen(GSimpleAction *a, GVariant *p, gpointer d) {
	activate_baum_action((Projekt*) d, "oeffnen"); }
static void cb_win_oeffnen_mit(GSimpleAction *a, GVariant *p, gpointer d) {
	activate_baum_action((Projekt*) d, "oeffnen-mit"); }

static void cb_win_suchen(GSimpleAction *a, GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d;
	gtk_popover_popup(GTK_POPOVER(zond->popover));
}

static void cb_win_icon(GSimpleAction *a, GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d;
	if (zond->baum_active == KEIN_BAUM || zond->baum_active == BAUM_FS)
		return;
	gint icon_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(a), "icon-id"));
	gchar *name = g_strdup_printf("icon-%d", icon_id);
	activate_baum_action(zond, name);
	g_free(name);
}

/* ============================================================================
 * CALLBACKS - PDF
 * ========================================================================== */

static GPtrArray* selection_abfragen_pdf(Projekt *zond, gchar **errmsg) {
	GPtrArray *arr_sfp = g_ptr_array_new_with_free_func(
			(GDestroyNotify) g_object_unref);
	if (zond->baum_active == KEIN_BAUM)
		return NULL;
	GList *selected = gtk_tree_selection_get_selected_rows(
			zond->selection[zond->baum_active], NULL);
	if (!selected)
		return NULL;
	for (GList *l = selected; l; l = l->next) {
		GtkTreeIter iter = { 0 };
		gint node_id = 0;
		gchar *file_part = NULL;
		GError *error = NULL;
		SondFilePart *sfp = NULL;
		if (!gtk_tree_model_get_iter(
				gtk_tree_view_get_model(GTK_TREE_VIEW(
						zond->treeview[zond->baum_active])),
				&iter, l->data)) {
			g_list_free_full(selected, (GDestroyNotify) gtk_tree_path_free);
			g_ptr_array_unref(arr_sfp);
			if (errmsg)
				*errmsg = g_strdup("Iter konnte nicht ermittelt werden");
			return NULL;
		}
		gtk_tree_model_get(gtk_tree_view_get_model(
				GTK_TREE_VIEW(zond->treeview[zond->baum_active])),
				&iter, 2, &node_id, -1);
		if (zond_dbase_get_node(zond->dbase_zond->zond_dbase_work, node_id,
				NULL, NULL, &file_part, NULL, NULL, NULL, NULL, &error)) {
			if (errmsg) *errmsg = g_strdup(error->message);
			g_error_free(error);
			g_list_free_full(selected, (GDestroyNotify) gtk_tree_path_free);
			g_ptr_array_unref(arr_sfp);
			return NULL;
		}
		if (!file_part)
			continue;
		sfp = sond_file_part_from_filepart(file_part, &error);
		g_free(file_part);
		if (!sfp) {
			if (errmsg) *errmsg = g_strdup(error->message);
			g_error_free(error);
			g_list_free_full(selected, (GDestroyNotify) gtk_tree_path_free);
			g_ptr_array_unref(arr_sfp);
			return NULL;
		}
		if (SOND_IS_FILE_PART_PDF(sfp) && !g_ptr_array_find(arr_sfp, sfp, NULL))
			g_ptr_array_add(arr_sfp, sfp);
		else
			g_object_unref(sfp);
	}
	g_list_free_full(selected, (GDestroyNotify) gtk_tree_path_free);
	return arr_sfp;
}

static gint clean_pdf(fz_context *ctx, SondFilePartPDF *sfp_pdf, GError **error) {
	pdf_document *doc = NULL;
	gint *pages = NULL;
	gint count = 0;
	gchar *path_tmp = NULL;
	GError *error_rem = NULL;

	if (zond_pdf_document_is_open(sfp_pdf)) {
		if (error) *error = g_error_new(g_quark_from_static_string("sond"), 0,
				"Datei '%s' ist geöffnet",
				sond_file_part_get_filepart(SOND_FILE_PART(sfp_pdf)));
		return -1;
	}
	doc = sond_file_part_pdf_open_document(ctx, sfp_pdf, TRUE, TRUE, error);
	if (!doc) {
		if (g_error_matches(*error, g_quark_from_static_string("sond"), 1)) {
			g_clear_error(error);
			return 1;
		}
		ERROR_Z
	}
	count = pdf_count_pages(ctx, doc);
	pages = g_malloc(sizeof(gint) * count);
	for (gint i = 0; i < count; i++) pages[i] = i;
	fz_try(ctx)
#ifdef __WIN32__
		pdf_rearrange_pages(ctx, doc, count, pages, PDF_CLEAN_STRUCTURE_KEEP);
#elif defined(__linux__)
		pdf_rearrange_pages(ctx, doc, count, pages);
#endif
	fz_always(ctx)
		g_free(pages);
	fz_catch(ctx) {
		pdf_drop_document(ctx, doc);
		g_object_unref(sfp_pdf);
		if (!sond_remove(path_tmp, &error_rem)) {
			LOG_WARN("Arbeitskopie konnte nicht gelöscht werden: %s",
					error_rem->message);
			g_error_free(error_rem);
		}
		if (error) *error = g_error_new(g_quark_from_static_string("sond"), 0,
				"%s\npdf_rearrange_pages\n%s", __func__, fz_caught_message(ctx));
		g_free(path_tmp);
		return -1;
	}
	gint rc = sond_file_part_pdf_save_and_close(ctx, doc, sfp_pdf, error);
	g_object_unref(sfp_pdf);
	if (!sond_remove(path_tmp, &error_rem)) {
		LOG_WARN("Arbeitskopie konnte nicht gelöscht werden: %s", error_rem->message);
		g_free(error_rem);
	}
	g_free(path_tmp);
	if (rc) ERROR_Z
	return 0;
}

static void cb_win_pdf_reparieren(GSimpleAction *a, GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d;
	gchar *errmsg = NULL;
	GPtrArray *arr_sfp = selection_abfragen_pdf(zond, &errmsg);
	if (!arr_sfp) {
		if (errmsg) {
			display_message(zond->app_window,
					"PDF kann nicht gereinigt werden\n\n", errmsg, NULL);
			g_free(errmsg);
		}
		return;
	}
	if (arr_sfp->len == 0) {
		display_message(zond->app_window, "Keine PDF-Datei ausgewählt", NULL);
		g_ptr_array_unref(arr_sfp);
		return;
	}
	for (gint i = 0; i < arr_sfp->len; i++) {
		GError *error = NULL;
		if (clean_pdf(zond->ctx, g_ptr_array_index(arr_sfp, i), &error) == -1) {
			display_message(zond->app_window, "PDF säubern nicht möglich\n\n",
					error->message, NULL);
			g_error_free(error);
		}
	}
	g_ptr_array_unref(arr_sfp);
}

/* ============================================================================
 * CALLBACKS - ANSICHT
 * ========================================================================== */

static void cb_win_alle_erweitern(GSimpleAction *a, GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d;
	if (zond->baum_active == KEIN_BAUM) return;
	gtk_tree_view_expand_all(GTK_TREE_VIEW(zond->treeview[zond->baum_active]));
}

static void cb_win_zweig_erweitern(GSimpleAction *a, GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d;
	GtkTreePath *path = NULL;
	if (zond->baum_active == KEIN_BAUM) return;
	gtk_tree_view_get_cursor(GTK_TREE_VIEW(zond->treeview[zond->baum_active]),
			&path, NULL);
	if (path) {
		gtk_tree_view_expand_row(
				GTK_TREE_VIEW(zond->treeview[zond->baum_active]), path, TRUE);
		gtk_tree_path_free(path);
	}
}

static void cb_win_reduzieren(GSimpleAction *a, GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d;
	if (zond->baum_active == KEIN_BAUM) return;
	gtk_tree_view_collapse_all(GTK_TREE_VIEW(zond->treeview[zond->baum_active]));
}

static void cb_win_refresh(GSimpleAction *a, GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d;
	GError *error = NULL;
	if (project_load_trees(zond, &error) == -1) {
		display_message(zond->app_window, "Fehler refresh\n\n",
				error->message, NULL);
		g_error_free(error);
	}
}

/* ============================================================================
 * CALLBACKS - EXTRAS
 * ========================================================================== */

static void cb_win_test(GSimpleAction *a, GVariant *p, gpointer d) {
	gchar *errmsg = NULL;
	Projekt *zond = (Projekt*) d;
	if (test(zond, &errmsg)) {
		display_message(zond->app_window, "Test:\n\n", errmsg, NULL);
		g_free(errmsg);
	}
}

/* ============================================================================
 * CALLBACKS - EINSTELLUNGEN
 * ========================================================================== */

static void cb_app_zoom(GSimpleAction *a, GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d;
	gchar *text = g_strdup_printf("%.0f",
			g_settings_get_double(zond->settings, "zoom"));
	abfrage_frage(zond->app_window, "Zoom:", "Faktor eingeben", &text);
	if (!g_strcmp0(text, "")) { g_free(text); return; }
	guint zoom = 0;
	if (string_to_guint(text, &zoom) == 0
			&& zoom >= ZOOM_MIN && zoom <= ZOOM_MAX)
		g_settings_set_double(zond->settings, "zoom", (gdouble) zoom);
	else
		display_message(zond->app_window, "Eingabe nicht gültig", NULL);
	g_free(text);
}

static void cb_app_autosave(GSimpleAction *a, GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d;
	GVariant *state = g_action_get_state(G_ACTION(a));
	gboolean active = !g_variant_get_boolean(state);
	g_variant_unref(state);
	g_simple_action_set_state(a, g_variant_new_boolean(active));
	g_settings_set_boolean(zond->settings, "autosave", active);
	if (!zond->dbase_zond) return;
	if (active)
		g_timeout_add_seconds(10 * 60, project_timeout_autosave, zond);
	else if (!g_source_remove_by_user_data(zond))
		display_message(zond->app_window,
				"autosave-Timeout konnte nicht entfernt werden", NULL);
}

/* ============================================================================
 * CALLBACKS - HILFE
 * ========================================================================== */

static void cb_app_ueber(GSimpleAction *a, GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d;
	display_message(zond->app_window,
			"Version: " MAJOR "." MINOR "." PATCH, NULL);
}

static void cb_app_update(GSimpleAction *a, GVariant *p, gpointer d) {
	Projekt *zond = (Projekt*) d;
	GError *error = NULL;
	gint cancel = 0;
	InfoWindow *info_window = info_window_open(zond->app_window, &cancel,
			"Zond Updater");
	gint rc = zond_update(zond, info_window, &error);
	info_window_kill(info_window);
	if (rc == -1) {
		display_message(zond->app_window, "Update fehlgeschlagen\n\n",
				error->message, NULL);
		g_error_free(error);
	} else if (rc == 1)
		display_message(zond->app_window, "Aktuelle Version installiert", NULL);
}

/* ============================================================================
 * HEADERBAR TOGGLE-BUTTON
 * ========================================================================== */

static void cb_button_mode_toggled(GtkToggleButton *button, gpointer data) {
	Projekt *zond = (Projekt*) data;
	GtkWidget *swindow_fs   = g_object_get_data(G_OBJECT(zond->hpaned), "swindow-fs");
	GtkWidget *swindow_ausw = g_object_get_data(G_OBJECT(zond->hpaned), "swindow-auswertung");

	if (gtk_toggle_button_get_active(button)) {
		/* FS-Modus: FS-Tree zeigen, Auswertungs-Tree verstecken */
		gtk_widget_show(swindow_fs);
		gtk_widget_hide(swindow_ausw);
	} else {
		/* Normal-Modus: FS-Tree verstecken, Auswertungs-Tree zeigen */
		gtk_widget_hide(swindow_fs);
		gtk_widget_show(swindow_ausw);
	}
}

/* ============================================================================
 * APP-ACTIONS AUFBAUEN (an GtkApplication registriert, Präfix "app.")
 * ========================================================================== */

static void init_app_actions(Projekt *zond) {
	GtkApplication *app = zond->app;

#define APP_ACT(name, cb) \
	do { \
		GSimpleAction *_a = g_simple_action_new((name), NULL); \
		g_signal_connect(_a, "activate", G_CALLBACK(cb), zond); \
		g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(_a)); \
		g_object_unref(_a); \
	} while (0)

	APP_ACT("projekt-neu",        cb_app_projekt_neu);
	APP_ACT("projekt-oeffnen",    cb_app_projekt_oeffnen);
	APP_ACT("projekt-speichern",  cb_app_projekt_speichern);
	APP_ACT("projekt-schliessen", cb_app_projekt_schliessen);
	APP_ACT("index-erstellen",    cb_app_index_erstellen);
	APP_ACT("indexsuche",         cb_app_indexsuche);
	APP_ACT("beenden",            cb_app_beenden);
	APP_ACT("ueber",              cb_app_ueber);
	APP_ACT("update",             cb_app_update);
	APP_ACT("zoom",               cb_app_zoom);

	{ gboolean init = g_settings_get_boolean(zond->settings, "autosave");
	  GSimpleAction *a = g_simple_action_new_stateful("autosave", NULL,
			g_variant_new_boolean(init));
	  g_signal_connect(a, "activate", G_CALLBACK(cb_app_autosave), zond);
	  g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(a));
	  g_object_unref(a); }

#undef APP_ACT

	const gchar *accels_beenden[] = { "<Control>q", NULL };
	gtk_application_set_accels_for_action(app, "app.beenden", accels_beenden);
}

/* ============================================================================
 * WINDOW-ACTIONS AUFBAUEN (an Fenster registriert, Präfix "win.")
 * ========================================================================== */

static void init_win_actions(Projekt *zond) {
	GtkApplication *app = zond->app;
	GSimpleActionGroup *ag = g_simple_action_group_new();

#define WIN_ACT(name, cb) \
	do { \
		GSimpleAction *_a = g_simple_action_new((name), NULL); \
		g_signal_connect(_a, "activate", G_CALLBACK(cb), zond); \
		g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(_a)); \
		g_object_unref(_a); \
	} while (0)

	WIN_ACT("index-erstellen-sel", cb_win_index_erstellen_sel);
	WIN_ACT("indexsuche-auswahl",  cb_win_indexsuche_auswahl);

	WIN_ACT("projekt-neu",     cb_app_projekt_neu);
	WIN_ACT("projekt-oeffnen", cb_app_projekt_oeffnen);
	WIN_ACT("index-erstellen", cb_app_index_erstellen);
	WIN_ACT("indexsuche",      cb_app_indexsuche);
	WIN_ACT("beenden",         cb_app_beenden);
	WIN_ACT("ueber",           cb_app_ueber);
	WIN_ACT("update",          cb_app_update);
	WIN_ACT("zoom",            cb_app_zoom);

	{ GSimpleAction *a = g_simple_action_new("projekt-speichern", NULL);
	  g_signal_connect(a, "activate", G_CALLBACK(cb_app_projekt_speichern), zond);
	  g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(a));
	  zond->menu.speichern = a; }

	{ GSimpleAction *a = g_simple_action_new("projekt-schliessen", NULL);
	  g_signal_connect(a, "activate", G_CALLBACK(cb_app_projekt_schliessen), zond);
	  g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(a));
	  zond->menu.schliessen = a; }

	{ gboolean init = g_settings_get_boolean(zond->settings, "autosave");
	  GSimpleAction *a = g_simple_action_new_stateful("autosave", NULL,
			g_variant_new_boolean(init));
	  g_signal_connect(a, "activate", G_CALLBACK(cb_app_autosave), zond);
	  g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(a));
	  g_object_unref(a); }

	{ GSimpleAction *a = g_simple_action_new("export-odt", NULL);
	  g_signal_connect(a, "activate", G_CALLBACK(cb_win_export), zond);
	  g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(a));
	  zond->menu.export_odt = a; }

	WIN_ACT("einf-ge",       cb_win_einf_ge);
	WIN_ACT("einf-up",       cb_win_einf_up);
	WIN_ACT("kopieren",      cb_win_kopieren);
	WIN_ACT("ausschneiden",  cb_win_ausschneiden);
	WIN_ACT("paste-ge",      cb_win_paste_ge);
	WIN_ACT("paste-up",      cb_win_paste_up);
	WIN_ACT("paste-link-ge", cb_win_paste_link_ge);
	WIN_ACT("paste-link-up", cb_win_paste_link_up);
	WIN_ACT("loeschen",      cb_win_loeschen);
	WIN_ACT("anb-entf",      cb_win_anb_entf);
	WIN_ACT("jump",          cb_win_jump);
	WIN_ACT("oeffnen",       cb_win_oeffnen);
	WIN_ACT("oeffnen-mit",   cb_win_oeffnen_mit);
	WIN_ACT("suchen",        cb_win_suchen);

	for (gint i = 0; i < NUMBER_OF_ICONS; i++) {
		gchar *name = g_strdup_printf("icon-%d", i);
		GSimpleAction *a = g_simple_action_new(name, NULL);
		g_object_set_data(G_OBJECT(a), "icon-id", GINT_TO_POINTER(i));
		g_signal_connect(a, "activate", G_CALLBACK(cb_win_icon), zond);
		g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(a));
		g_object_unref(a);
		g_free(name);
	}

	{ GSimpleAction *a = g_simple_action_new("pdf-reparieren", NULL);
	  g_signal_connect(a, "activate", G_CALLBACK(cb_win_pdf_reparieren), zond);
	  g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(a));
	  zond->menu.pdf = a; }

	WIN_ACT("alle-erweitern",  cb_win_alle_erweitern);
	WIN_ACT("zweig-erweitern", cb_win_zweig_erweitern);
	WIN_ACT("reduzieren",      cb_win_reduzieren);
	WIN_ACT("refresh",         cb_win_refresh);

	{ GSimpleAction *a = g_simple_action_new("test", NULL);
	  g_signal_connect(a, "activate", G_CALLBACK(cb_win_test), zond);
	  g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(a));
	  zond->menu.extras = a; }

	zond->menu.struktur = G_SIMPLE_ACTION(
			g_action_map_lookup_action(G_ACTION_MAP(ag), "einf-ge"));
	zond->menu.ansicht = G_SIMPLE_ACTION(
			g_action_map_lookup_action(G_ACTION_MAP(ag), "alle-erweitern"));

#undef WIN_ACT

	gtk_widget_insert_action_group(zond->app_window, "win",
			G_ACTION_GROUP(ag));
	g_object_unref(ag);

	struct { const gchar *action; const gchar *accel; } accels[] = {
		{ "win.einf-ge",        "<Control>p"         },
		{ "win.einf-up",        "<Control><Shift>p"  },
		{ "win.kopieren",       "<Control>c"         },
		{ "win.ausschneiden",   "<Control>x"         },
		{ "win.paste-ge",       "<Control>v"         },
		{ "win.paste-up",       "<Control><Shift>v"  },
		{ "win.paste-link-ge",  "<Control>l"         },
		{ "win.paste-link-up",  "<Control><Shift>l"  },
		{ "win.loeschen",       "<Control>Delete"    },
		{ "win.jump",           "<Control>j"         },
		{ "win.oeffnen",        "<Control>o"         },
	};
	for (guint i = 0; i < G_N_ELEMENTS(accels); i++) {
		const gchar *av[] = { accels[i].accel, NULL };
		gtk_application_set_accels_for_action(app, accels[i].action, av);
	}
}

/* ============================================================================
 * GMENU AUFBAUEN
 * ========================================================================== */

static GMenuModel* build_menu(Projekt *zond) {
	GMenu *menubar = g_menu_new();

	/* ---- Projekt ---- */
	GMenu *m_proj = g_menu_new();
	g_menu_append(m_proj, "Neu",        "win.projekt-neu");
	g_menu_append(m_proj, "Öffnen",     "win.projekt-oeffnen");
	g_menu_append(m_proj, "Speichern",  "win.projekt-speichern");
	g_menu_append(m_proj, "Schliessen", "win.projekt-schliessen");

	GMenu *sec_export = g_menu_new();
	g_menu_append(sec_export, "Export als odt-Dokument", "win.export-odt");
	g_menu_append_section(m_proj, NULL, G_MENU_MODEL(sec_export));
	g_object_unref(sec_export);

	GMenu *sec_index = g_menu_new();
	GMenu *sub_idx_erst = g_menu_new();
	g_menu_append(sub_idx_erst, "Gesamtes Projektverzeichnis", "win.index-erstellen");
	g_menu_append(sub_idx_erst, "Ausgewählte Punkte",          "win.index-erstellen-sel");
	g_menu_append_submenu(sec_index, "Index erstellen",
			G_MENU_MODEL(sub_idx_erst));
	g_object_unref(sub_idx_erst);
	GMenu *sub_idx_such = g_menu_new();
	g_menu_append(sub_idx_such, "Gesamtes Projektverzeichnis", "win.indexsuche");
	g_menu_append(sub_idx_such, "Ausgewählte Punkte",          "win.indexsuche-auswahl");
	g_menu_append_submenu(sec_index, "Index durchsuchen",
			G_MENU_MODEL(sub_idx_such));
	g_object_unref(sub_idx_such);
	g_menu_append_section(m_proj, NULL, G_MENU_MODEL(sec_index));
	g_object_unref(sec_index);

	GMenu *sec_beenden = g_menu_new();
	g_menu_append(sec_beenden, "Beenden", "win.beenden");
	g_menu_append_section(m_proj, NULL, G_MENU_MODEL(sec_beenden));
	g_object_unref(sec_beenden);

	g_menu_append_submenu(menubar, "Projekt", G_MENU_MODEL(m_proj));
	g_object_unref(m_proj);

	/* ---- Bearbeiten ---- */
	GMenu *m_bear = g_menu_new();

	GMenu *sec_einf = g_menu_new();
	GMenu *sub_einf = g_menu_new();
	g_menu_append(sub_einf, "Gleiche Ebene", "win.einf-ge");
	g_menu_append(sub_einf, "Unterebene",    "win.einf-up");
	g_menu_append_submenu(sec_einf, "Punkt einfügen",
			G_MENU_MODEL(sub_einf));
	g_object_unref(sub_einf);
	g_menu_append_section(m_bear, NULL, G_MENU_MODEL(sec_einf));
	g_object_unref(sec_einf);

	GMenu *sec_clip = g_menu_new();
	g_menu_append(sec_clip, "Kopieren",     "win.kopieren");
	g_menu_append(sec_clip, "Ausschneiden", "win.ausschneiden");
	GMenu *sub_paste = g_menu_new();
	g_menu_append(sub_paste, "Gleiche Ebene", "win.paste-ge");
	g_menu_append(sub_paste, "Unterebene",    "win.paste-up");
	g_menu_append_submenu(sec_clip, "Einfügen", G_MENU_MODEL(sub_paste));
	g_object_unref(sub_paste);
	GMenu *sub_paste_link = g_menu_new();
	g_menu_append(sub_paste_link, "Gleiche Ebene", "win.paste-link-ge");
	g_menu_append(sub_paste_link, "Unterebene",    "win.paste-link-up");
	g_menu_append_submenu(sec_clip, "Als Link einfügen",
			G_MENU_MODEL(sub_paste_link));
	g_object_unref(sub_paste_link);
	g_menu_append_section(m_bear, NULL, G_MENU_MODEL(sec_clip));
	g_object_unref(sec_clip);

	GMenu *sec_edit = g_menu_new();
	g_menu_append(sec_edit, "Löschen",              "win.loeschen");
	g_menu_append(sec_edit, "Anbindung entfernen",  "win.anb-entf");
	g_menu_append(sec_edit, "Zu Ursprung springen", "win.jump");
	g_menu_append_section(m_bear, NULL, G_MENU_MODEL(sec_edit));
	g_object_unref(sec_edit);

	GMenu *sec_suche = g_menu_new();
	g_menu_append(sec_suche, "Suchen",     "win.suchen");
	g_menu_append(sec_suche, "Öffnen",     "win.oeffnen");
	g_menu_append(sec_suche, "Öffnen mit", "win.oeffnen-mit");
	g_menu_append_section(m_bear, NULL, G_MENU_MODEL(sec_suche));
	g_object_unref(sec_suche);

	GMenu *sec_icon = g_menu_new();
	GMenu *sub_icon = g_menu_new();
	for (gint i = 0; i < NUMBER_OF_ICONS; i++) {
		gchar *action = g_strdup_printf("win.icon-%d", i);
		GMenuItem *item = g_menu_item_new(zond->icon[i].display_name, action);
		GIcon *icon = g_themed_icon_new(zond->icon[i].icon_name);
		g_menu_item_set_icon(item, icon);
		g_object_unref(icon);
		g_menu_append_item(sub_icon, item);
		g_object_unref(item);
		g_free(action);
	}
	g_menu_append_submenu(sec_icon, "Icon ändern", G_MENU_MODEL(sub_icon));
	g_object_unref(sub_icon);
	g_menu_append_section(m_bear, NULL, G_MENU_MODEL(sec_icon));
	g_object_unref(sec_icon);

	g_menu_append_submenu(menubar, "Bearbeiten", G_MENU_MODEL(m_bear));
	g_object_unref(m_bear);

	/* ---- PDF-Dateien ---- */
	GMenu *m_pdf = g_menu_new();
	g_menu_append(m_pdf, "PDF reparieren", "win.pdf-reparieren");
	g_menu_append_submenu(menubar, "PDF-Dateien", G_MENU_MODEL(m_pdf));
	g_object_unref(m_pdf);

	/* ---- Ansicht ---- */
	GMenu *m_ans = g_menu_new();
	GMenu *sub_erw = g_menu_new();
	g_menu_append(sub_erw, "Ganze Struktur",  "win.alle-erweitern");
	g_menu_append(sub_erw, "Aktueller Zweig", "win.zweig-erweitern");
	g_menu_append_submenu(m_ans, "Erweitern", G_MENU_MODEL(sub_erw));
	g_object_unref(sub_erw);
	g_menu_append(m_ans, "Alle reduzieren", "win.reduzieren");
	GMenu *sec_ref = g_menu_new();
	g_menu_append(sec_ref, "Refresh",     "win.refresh");
	g_menu_append(sec_ref, "Textfenster", "win.textview-extra");
	g_menu_append_section(m_ans, NULL, G_MENU_MODEL(sec_ref));
	g_object_unref(sec_ref);
	g_menu_append_submenu(menubar, "Ansicht", G_MENU_MODEL(m_ans));
	g_object_unref(m_ans);

	/* ---- Extras ---- */
	GMenu *m_ext = g_menu_new();
	g_menu_append(m_ext, "Test", "win.test");
	g_menu_append_submenu(menubar, "Extras", G_MENU_MODEL(m_ext));
	g_object_unref(m_ext);

	/* ---- Einstellungen ---- */
	GMenu *m_eins = g_menu_new();
	g_menu_append(m_eins, "Zoom Interner Viewer",    "win.zoom");
	g_menu_append(m_eins, "Automatisches Speichern", "win.autosave");
	g_menu_append_submenu(menubar, "Einstellungen", G_MENU_MODEL(m_eins));
	g_object_unref(m_eins);

	/* ---- Hilfe ---- */
	GMenu *m_hilfe = g_menu_new();
	g_menu_append(m_hilfe, "Über",   "win.ueber");
	g_menu_append(m_hilfe, "Update", "win.update");
	g_menu_append_submenu(menubar, "Hilfe", G_MENU_MODEL(m_hilfe));
	g_object_unref(m_hilfe);

	return G_MENU_MODEL(menubar);
}

/* ============================================================================
 * ÖFFENTLICHE FUNKTIONEN
 * ========================================================================== */

void init_headerbar(Projekt *zond) {
	GtkWidget *headerbar = gtk_header_bar_new();
	gtk_header_bar_set_has_subtitle(GTK_HEADER_BAR(headerbar), FALSE);
	gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(headerbar),
			":minimize,maximize,close");
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);

	gtk_window_set_titlebar(GTK_WINDOW(zond->app_window), headerbar);

	zond->fs_button = gtk_toggle_button_new_with_label("FS");
	g_signal_connect(zond->fs_button, "toggled",
			G_CALLBACK(cb_button_mode_toggled), zond);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(headerbar), zond->fs_button);

	init_app_actions(zond);
	init_win_actions(zond);

	GMenuModel *model = build_menu(zond);
#if GTK_MAJOR_VERSION >= 4
	GtkWidget *menubar = gtk_popover_menu_bar_new_from_model(model);
#else
	GtkWidget *menubar = gtk_menu_bar_new_from_model(model);
#endif
	g_object_unref(model);
	gtk_header_bar_pack_start(GTK_HEADER_BAR(headerbar), menubar);

	g_simple_action_set_enabled(zond->menu.speichern,      FALSE);
	g_simple_action_set_enabled(zond->menu.schliessen,     FALSE);
	g_simple_action_set_enabled(zond->menu.export_odt,     FALSE);
	g_simple_action_set_enabled(zond->menu.pdf,            FALSE);

#ifdef _WIN32
	GtkWidget *label_seadrive = gtk_label_new("");
	gtk_widget_show(label_seadrive);
	gtk_header_bar_pack_end(GTK_HEADER_BAR(headerbar), label_seadrive);
	g_object_set_data(G_OBJECT(zond->app_window), "seadrive-label",
			label_seadrive);
#endif
}
