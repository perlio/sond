/*
 sond (gtk4/sond_treeviewfm_unlock.c) - Akten, Beweisstücke, Unterlagen
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

/* Dauerhaft entsperren (Teil von SondTreeviewFM, s. sond_treeviewfm_private.h). Der Ablauf liegt
 * in tvfm/sond_tvfm_unlock.c (GTK-frei); hier sind Menüpunkt, Bestätigung, Meldungen und die
 * Rückrufe (SeaDrive laden, Zeile auffrischen). */

#include "sond_treeviewfm_private.h"

#include <string.h>

#include "sond_seadrive_load_dialog.h"
#include "../tvfm/sond_tvfm_load.h"

#define UNLOCK_TITLE "Dauerhaft entsperren"

/* ------------------------------------------------------------------------
 * Rückrufe
 * --------------------------------------------------------------------- */

//Datei lokal verfügbar machen (nicht geladene SeaDrive-Platzhalter laden)
static gboolean hook_ensure_local(SondFilePart *sfp, gpointer data) {
	SondTreeviewFM *self = SOND_TREEVIEWFM(data);
	gchar *path = sond_tvfm_load_root_file_path(SOND_TVFM_OWNER(self), sfp);
	gboolean ok = TRUE;

	if (path && sond_tvfm_load_not_loaded(SOND_TVFM_OWNER(self), sfp))
		ok = sond_seadrive_load_file_sync(fm_parent_window(self), path);
	g_free(path);

	return ok;
}

static void hook_refresh(SondFilePart *sfp, gpointer data) {
	sond_treeviewfm_refresh_file_part(SOND_TREEVIEWFM(data), sfp);
}

static void hook_report(gchar const *message, gpointer data) {
	fm_show_error(SOND_TREEVIEWFM(data), UNLOCK_TITLE, message);
}

static gboolean hook_busy(SondFilePart *sfp, gpointer data) {
	SondTreeviewFMClass *klass = SOND_TREEVIEWFM_GET_CLASS(SOND_TREEVIEWFM(data));

	return klass->file_part_busy && klass->file_part_busy(sfp);
}

void sond_treeviewfm_unlock_hooks_init(SondTreeviewFM *self, SondUnlockHooks *hooks) {
	g_return_if_fail(SOND_IS_TREEVIEWFM(self));

	memset(hooks, 0, sizeof(SondUnlockHooks));
	hooks->ensure_local = hook_ensure_local;
	hooks->refresh = hook_refresh;
	hooks->report = hook_report;
	hooks->busy = hook_busy;
	hooks->data = self;
}

void sond_treeviewfm_set_write_check(SondTreeviewFM *self,
		SondTreeviewFMWriteCheckFunc func, gpointer user_data) {
	SondTreeviewFMPrivate *priv = NULL;

	g_return_if_fail(SOND_IS_TREEVIEWFM(self));
	priv = fm_get_priv(self);

	priv->write_check = func;
	priv->write_check_data = user_data;
}

/* ------------------------------------------------------------------------
 * Aktion
 * --------------------------------------------------------------------- */

typedef struct {
	SondTreeviewFM *self; //eigene Referenz
	SondUnlockJobs jobs;
} UnlockCtx;

static void unlock_ctx_free(UnlockCtx *ctx) {
	sond_tvfm_unlock_jobs_clear(&ctx->jobs);
	g_object_unref(ctx->self);
	g_free(ctx);
}

static void on_confirmed(GObject *source, GAsyncResult *result, gpointer user_data) {
	UnlockCtx *ctx = user_data;
	SondTreeviewFM *self = ctx->self;
	GError *error = NULL;
	gint button = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result, &error);

	g_clear_error(&error);

	if (button == 0) {
		SondUnlockHooks hooks;
		SondUnlockResult res;
		GString *text = g_string_new(NULL);

		sond_treeviewfm_unlock_hooks_init(self, &hooks);
		sond_tvfm_unlock_run(&ctx->jobs, &hooks, &res);

		g_string_append_printf(text, "%u Datei(en) entsperrt", res.done);
		if (res.skipped)
			g_string_append_printf(text, ", %u übersprungen", res.skipped);
		if (res.aborted)
			g_string_append(text, "\n\nAbgebrochen.");
		fm_show_error(self, UNLOCK_TITLE, text->str);
		g_string_free(text, TRUE);
		sond_tvfm_unlock_result_clear(&res);

		sond_treeviewfm_refresh_status(self);
	}

	unlock_ctx_free(ctx);
}

void sond_treeviewfm_unlock_selection(SondTreeviewFM *self) {
	SondTreeviewFMPrivate *priv = NULL;
	GError *error = NULL;
	GPtrArray *items = NULL;
	UnlockCtx *ctx = NULL;
	SondUnlockHooks hooks;
	guint n_total = 0;
	gchar *busy_text = NULL;
	SondUnlockPrepare ready = SOND_UNLOCK_NOTHING;

	g_return_if_fail(SOND_IS_TREEVIEWFM(self));
	priv = fm_get_priv(self);

	//die Dateien werden verändert: dafür muss die Schreibberechtigung vorliegen
	if (priv->write_check && !priv->write_check(priv->write_check_data, &error)) {
		fm_show_error(self, UNLOCK_TITLE, error ? error->message : "nicht erlaubt");
		g_clear_error(&error);

		return;
	}

	ctx = g_new0(UnlockCtx, 1);
	ctx->self = g_object_ref(self);
	sond_tvfm_unlock_jobs_init(&ctx->jobs);

	items = sond_treeview_get_selected_items(SOND_TREEVIEW(self));
	for (guint i = 0; i < items->len; i++)
		sond_tvfm_unlock_collect_item(&ctx->jobs, g_ptr_array_index(items, i));
	g_ptr_array_unref(items);

	sond_treeviewfm_unlock_hooks_init(self, &hooks);
	ready = sond_tvfm_unlock_prepare(&ctx->jobs, &hooks, &n_total, &busy_text);

	if (ready == SOND_UNLOCK_READY) {
		GtkAlertDialog *question = gtk_alert_dialog_new(
				"Der Passwortschutz von %u Datei(en) wird dauerhaft entfernt.", n_total);
		const char *buttons[] = { "Fortfahren", "Abbrechen", NULL };

		gtk_alert_dialog_set_detail(question, "Die Dateien werden dabei verändert, eine "
				"Sicherung gibt es nicht.");
		gtk_alert_dialog_set_buttons(question, buttons);
		gtk_alert_dialog_set_default_button(question, 1);
		gtk_alert_dialog_set_cancel_button(question, 1);
		gtk_alert_dialog_set_modal(question, TRUE);
		gtk_alert_dialog_choose(question, fm_parent_window(self), NULL, on_confirmed, ctx);
		g_object_unref(question);

		return; //ctx gehört nun dem Dialog
	}

	fm_show_error(self, UNLOCK_TITLE,
			ready == SOND_UNLOCK_NOTHING ? "Die Auswahl enthält keine passwortgeschützten "
					"Dateien." : ready == SOND_UNLOCK_BUSY ? busy_text :
					"Die Datei wird noch geladen. Bitte danach erneut versuchen.");
	g_free(busy_text);
	unlock_ctx_free(ctx);
}

void fm_action_unlock(GSimpleAction *action, GVariant *parameter, gpointer data) {
	(void) action;
	(void) parameter;

	sond_treeviewfm_unlock_selection(SOND_TREEVIEWFM(data));
}
