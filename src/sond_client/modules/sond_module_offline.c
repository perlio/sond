/*
 sond (sond_module_offline.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_module_offline.h"
#include "../sond_client.h"
#include "../sond_offline_manager.h"
#include "../sond_seafile_sync.h"
#include "../../sond_log_and_error.h"
//#include "../../misc_stdlib.h"
#include "../../sond_file_helper.h"

#include <glib/gstdio.h>
#include "../libsearpc/searpc-client.h"
#include "../libsearpc/searpc-named-pipe-transport.h"

typedef struct {
    SondClient *client;
    
    /* UI-Elemente */
    GtkWidget *main_widget;
    GtkWidget *list_box;
    GtkWidget *refresh_button;
    GtkWidget *info_label;
} SondModuleOfflinePrivate;

/* Forward declarations */
static void update_akte_list(SondModuleOfflinePrivate *priv);

/* ========================================================================
 * Helper Functions
 * ======================================================================== */

static void show_error_dialog(GtkWidget *parent, const gchar *title, const gchar *message) {
    GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", title);
    gtk_alert_dialog_set_detail(dialog, message);
    gtk_alert_dialog_set_modal(dialog, TRUE);
    
    gtk_alert_dialog_show(dialog, GTK_WINDOW(gtk_widget_get_root(parent)));
    g_object_unref(dialog);
}

static void show_info_dialog(GtkWidget *parent, const gchar *title, const gchar *message) {
    GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", title);
    gtk_alert_dialog_set_detail(dialog, message);
    gtk_alert_dialog_set_modal(dialog, TRUE);
    
    gtk_alert_dialog_show(dialog, GTK_WINDOW(gtk_widget_get_root(parent)));
    g_object_unref(dialog);
}

static gchar* format_regnr_display(const gchar *regnr_storage) {
    if (!regnr_storage) {
        return g_strdup("???");
    }
    
    gchar **parts = g_strsplit(regnr_storage, "-", 2);
    if (g_strv_length(parts) != 2) {
        g_strfreev(parts);
        return g_strdup(regnr_storage);
    }
    
    guint year = (guint)g_ascii_strtoull(parts[0], NULL, 10);
    guint lfd = (guint)g_ascii_strtoull(parts[1], NULL, 10);
    
    g_strfreev(parts);
    
    return g_strdup_printf("%u/%02u", lfd, year % 100);
}

static gchar* format_datetime(GDateTime *dt) {
    if (!dt) {
        return g_strdup("Nie");
    }
    
    return g_date_time_format(dt, "%d.%m.%Y %H:%M");
}

static gchar* get_sync_status_text(const gchar *library_id) {
    GError *error = NULL;
    gchar *status = sond_seafile_get_sync_status(library_id, &error);
    
    if (error) {
        LOG_WARN("Fehler beim Abrufen des Sync-Status: %s\n", error->message);
        g_error_free(error);
        return g_strdup("Unbekannt");
    }
    
    if (!status) {
        return g_strdup("Nicht synchronisiert");
    }
    
    if (g_strcmp0(status, "synchronized") == 0) {
        g_free(status);
        return g_strdup("✓ Synchronisiert");
    } else if (g_strcmp0(status, "syncing") == 0) {
        g_free(status);
        return g_strdup("⟳ Läuft...");
    } else if (g_strcmp0(status, "error") == 0) {
        g_free(status);
        return g_strdup("⚠ Fehler");
    }
    
    gchar *result = g_strdup_printf("Status: %s", status);
    g_free(status);
    return result;
}

/* ========================================================================
 * Event Handlers
 * ======================================================================== */

typedef struct {
    SondModuleOfflinePrivate *priv;
    gchar *regnr;
    gchar *library_id;
} ActionData;

static void action_data_free(ActionData *data) {
    if (!data) return;
    g_free(data->regnr);
    g_free(data->library_id);
    g_free(data);
}

static void on_sync_stop_clicked(GtkButton *button, ActionData *data) {
    SondModuleOfflinePrivate *priv = data->priv;
    
    /* Prüfe ob Repo vollständig synchronisiert ist */
    GError *status_error = NULL;
    gchar *sync_status = sond_seafile_get_sync_status(data->library_id, &status_error);

    if (status_error) {
        gchar *msg = g_strdup_printf("Fehler beim Prüfen des Sync-Status: %s",
                                    status_error->message);
        show_error_dialog(priv->main_widget, "Fehler", msg);
        g_free(msg);
        g_error_free(status_error);
        return;
    }

    /* Nur pausieren wenn synchronisiert */
    if (!sync_status || (g_strcmp0(sync_status, "synchronized") != 0 &&
                         g_strcmp0(sync_status, "done") != 0)) {
        gchar *current_status = sync_status ? g_strdup(sync_status) : g_strdup("unbekannt");
        gchar *msg = g_strdup_printf(
            "Synchronisation kann nur pausiert werden wenn vollständig synchronisiert.\n\n"
            "Aktueller Status: %s\n\n"
            "Bitte warten Sie bis die Synchronisation abgeschlossen ist.",
            current_status);
        show_error_dialog(priv->main_widget, "Synchronisation läuft noch", msg);
        g_free(msg);
        g_free(current_status);
        if (sync_status) g_free(sync_status);
        return;
    }

    g_free(sync_status);

    /* Seafile Sync stoppen */
    GError *error = NULL;
    if (!sond_seafile_unsync_library(data->library_id, &error)) {
        gchar *msg = g_strdup_printf("Fehler beim Stoppen der Synchronisation: %s",
                                    error ? error->message : "Unbekannt");
        show_error_dialog(priv->main_widget, "Fehler", msg);
        g_free(msg);
        if (error) g_error_free(error);
        return;
    }
    
    /* syncing_enabled auf FALSE setzen (Akte bleibt in Liste!) */
    SondOfflineManager *manager = sond_client_get_offline_manager(priv->client);
    if (manager) {
        GError *set_error = NULL;
        if (!sond_offline_manager_set_sync_enabled(manager, data->regnr, FALSE, &set_error)) {
            LOG_WARN("Konnte syncing_enabled nicht setzen: %s\n",
                    set_error ? set_error->message : "Unbekannt");
            if (set_error) g_error_free(set_error);
        }
    }
    
    /* Liste aktualisieren */
    gtk_widget_activate(priv->refresh_button);
}

static void on_sync_resume_clicked(GtkButton *button, ActionData *data) {
    if (!data || !data->priv) {
        LOG_ERROR("on_sync_resume_clicked: Invalid data\n");
        return;
    }
    
    SondModuleOfflinePrivate *priv = data->priv;
    SondOfflineManager *manager = sond_client_get_offline_manager(priv->client);
    
    if (!manager) {
        show_error_dialog(priv->main_widget, "Fehler", "Offline Manager nicht verfügbar");
        return;
    }
    
    const gchar *sync_dir = sond_offline_manager_get_sync_directory(manager);
    gchar *local_path = g_build_filename(sync_dir, data->regnr, NULL);
    
    /* Prüfe ob Verzeichnis existiert */
    if (!g_file_test(local_path, G_FILE_TEST_IS_DIR)) {
        show_error_dialog(priv->main_widget, "Fehler", 
                         "Lokales Verzeichnis existiert nicht mehr. Bitte Cache löschen und neu synchronisieren.");
        g_free(local_path);
        return;
    }
    
    /* Clone-Token vom Server holen */
    GError *error = NULL;
    gchar *clone_token = sond_client_get_seafile_clone_token(priv->client, data->library_id, &error);
    if (!clone_token) {
        show_error_dialog(priv->main_widget, "Clone-Token konnte nicht abgerufen werden",
        		error ? error->message : "Unbekannt");
        if (error) g_error_free(error);
        g_free(local_path);
        return;
    }
    
    /* Seafile RPC-Client verbinden */
    SearpcClient *rpc_client = sond_seafile_get_rpc_client(&error);
    if (!rpc_client) {
        show_error_dialog(priv->main_widget, "Verbindung mit Seafile-Client "
        		"konnte nicht erstellt werden", error ? error->message : "Unbekannt");
        if (error) g_error_free(error);
        g_free(clone_token);
        g_free(local_path);
        return;
    }
    
    /* RPC-Call: seafile_clone - synchronisiert existierendes Verzeichnis */
    GError *rpc_error = NULL;
    gchar *task_id = NULL;
    gchar *server_url = (gchar*)sond_client_get_seafile_url(priv->client);
    gchar *more_info = g_strdup_printf("{\"server_url\":\"%s\"}", server_url);

    searpc_client_call(
        rpc_client,
        "seafile_clone",
        "string",
        0,
        &task_id,
        &rpc_error,
        11,
        "string", data->library_id,       // repo_id
        "int", (void*)1,                   // repo_version
        "string", data->regnr,             // repo_name
        "string", local_path,              // worktree (existierendes Verzeichnis!)
        "string", clone_token,             // token (vom Server)
        "string", NULL,                    // passwd
        "string", "",                      // magic
        "string", "",                      // email
        "string", "",                      // random_key
        "int", (void*)0,                   // enc_version
        "string", more_info                // more_info
    );

    g_free(more_info);
    g_free(clone_token);
    searpc_free_client_with_pipe_transport(rpc_client);
    g_free(local_path);

    if (rpc_error) {
        show_error_dialog(priv->main_widget, "Fortsetzen der Synchronisation "
        		"fehlgeschlagen", rpc_error->message);
        g_error_free(rpc_error);
        return;
    }

    if (!task_id) {
        show_error_dialog(priv->main_widget, "Fortsetzen der Synchronisation "
        		"fehlgeschlagen", "Keine Task-ID");
        return;
    }
    
    /* syncing_enabled auf TRUE setzen */
    GError *set_error = NULL;
    if (!sond_offline_manager_set_sync_enabled(manager, data->regnr, TRUE, &set_error)) {
        LOG_WARN("Konnte syncing_enabled nicht setzen: %s\n",
                set_error ? set_error->message : "Unbekannt");
        if (set_error) g_error_free(set_error);
    }

    g_free(task_id);
    
    LOG_INFO("Synchronisation für %s fortgesetzt\n", data->regnr);
    show_info_dialog(priv->main_widget, "Erfolg", "Synchronisation wurde fortgesetzt.");
    
    update_akte_list(priv);
}

static void on_cache_delete_response(GObject *source, GAsyncResult *result, gpointer user_data) {
    GtkAlertDialog *dialog = GTK_ALERT_DIALOG(source);
    ActionData *data = (ActionData *)user_data;
    SondModuleOfflinePrivate *priv = data->priv;
    
    GError *error = NULL;
    int button = gtk_alert_dialog_choose_finish(dialog, result, &error);
    
    if (error) {
        LOG_ERROR("Dialog error: %s\n", error->message);
        g_error_free(error);
        action_data_free(data);
        return;
    }
    
    if (button != 0) {
        action_data_free(data);
    	return;
    }
    
    SondOfflineManager *manager = sond_client_get_offline_manager(priv->client);
    const gchar *sync_dir = sond_offline_manager_get_sync_directory(manager);
    gchar *akte_path = g_build_filename(sync_dir, data->regnr, NULL);
    
	if (!g_file_test(akte_path, G_FILE_TEST_IS_DIR)) {
		gchar *msg = g_strdup_printf("Löschen Cache zur Akte %s nicht möglich",
									akte_path);
		show_error_dialog(priv->main_widget, msg, "Ist kein Verzeichnis");
		g_free(msg);
		g_free(akte_path);
	    action_data_free(data);
		return;
	}
	if (sond_rmdir_r(akte_path, &error) != 0) {
		gchar *msg = g_strdup_printf("Fehler beim Löschen des Cache-Verzeichnisses: %s",
									akte_path);
		show_error_dialog(priv->main_widget, msg, error->message);
		g_free(msg);
		g_error_free(error);
		g_free(akte_path);
	    action_data_free(data);
		return;
	}

    if (!sond_offline_manager_remove_akte(manager, data->regnr, &error)) {
        LOG_WARN("Konnte Akte nicht aus Offline-Liste entfernen: %s\n",
                   error ? error->message : "Unbekannt");
        g_error_free(error);
    }
    
    action_data_free(data);
    g_free(akte_path);
    
    show_info_dialog(priv->main_widget, "Erfolg", "Cache wurde gelöscht.");
    
    update_akte_list(priv);
}

static void on_cache_delete_clicked(GtkButton *button, ActionData *data) {
    SondModuleOfflinePrivate *priv = data->priv;
    
    gchar *regnr_display = format_regnr_display(data->regnr);
    gchar *message = g_strdup_printf(
        "Möchten Sie den lokalen Cache für Akte %s wirklich löschen?\n\n"
        "Alle lokal gespeicherten Dateien gehen verloren!",
        regnr_display);
    g_free(regnr_display);
    
    GtkAlertDialog *dialog = gtk_alert_dialog_new("Cache löschen?");
    gtk_alert_dialog_set_detail(dialog, message);
    g_free(message);
    
    const char *buttons[] = {"Ja, löschen", "Abbrechen", NULL};
    gtk_alert_dialog_set_buttons(dialog, buttons);
    gtk_alert_dialog_set_default_button(dialog, 1);
    gtk_alert_dialog_set_cancel_button(dialog, 1);
    gtk_alert_dialog_set_modal(dialog, TRUE);
    
    ActionData *callback_data = g_new0(ActionData, 1);
    callback_data->priv = priv;
    callback_data->regnr = g_strdup(data->regnr);
    callback_data->library_id = g_strdup(data->library_id);
    
    gtk_alert_dialog_choose(dialog,
                           GTK_WINDOW(gtk_widget_get_root(priv->main_widget)),
                           NULL,
                           on_cache_delete_response,
                           callback_data);
    
    g_object_unref(dialog);
}

static void on_open_folder_clicked(GtkButton *button, ActionData *data) {
    SondModuleOfflinePrivate *priv = data->priv;
    SondOfflineManager *manager = sond_client_get_offline_manager(priv->client);
    const gchar *sync_dir = sond_offline_manager_get_sync_directory(manager);
    gchar *akte_path = g_build_filename(sync_dir, data->regnr, NULL);
    
    GError *error = NULL;
#ifdef G_OS_WIN32
    gchar *cmd = g_strdup_printf("explorer \"%s\"", akte_path);
#else
    gchar *cmd = g_strdup_printf("xdg-open \"%s\"", akte_path);
#endif
    
    if (!g_spawn_command_line_async(cmd, &error)) {
        gchar *msg = g_strdup_printf("Fehler beim Öffnen des Ordners: %s",
                                    error ? error->message : "Unbekannt");
        show_error_dialog(priv->main_widget, "Fehler", msg);
        g_free(msg);
        if (error) g_error_free(error);
    }
    
    g_free(cmd);
    g_free(akte_path);
}

static void update_akte_list(SondModuleOfflinePrivate *priv) {
    GtkWidget *child = gtk_widget_get_first_child(priv->list_box);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(GTK_LIST_BOX(priv->list_box), child);
        child = next;
    }
    
    SondOfflineManager *manager = sond_client_get_offline_manager(priv->client);
    GList *akten = sond_offline_manager_get_all_akten(manager);
    
    if (!akten) {
        gtk_label_set_text(GTK_LABEL(priv->info_label), "Keine Offline-Akten vorhanden");
        return;
    }
    
    guint count = g_list_length(akten);
    gchar *info_text = g_strdup_printf("%u Offline-Akte%s", count, count == 1 ? "" : "n");
    gtk_label_set_text(GTK_LABEL(priv->info_label), info_text);
    g_free(info_text);
    
    for (GList *l = akten; l != NULL; l = l->next) {
        SondOfflineAkte *akte = (SondOfflineAkte *)l->data;
        
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_widget_set_margin_start(row, 10);
        gtk_widget_set_margin_end(row, 10);
        gtk_widget_set_margin_top(row, 5);
        gtk_widget_set_margin_bottom(row, 5);
        
        gchar *regnr_display = format_regnr_display(akte->regnr);
        GtkWidget *regnr_label = gtk_label_new(regnr_display);
        gtk_widget_set_size_request(regnr_label, 60, -1);
        gtk_widget_add_css_class(regnr_label, "monospace");
        gtk_box_append(GTK_BOX(row), regnr_label);
        g_free(regnr_display);
        
        GtkWidget *kurzb_label = gtk_label_new(akte->kurzb);
        gtk_widget_set_size_request(kurzb_label, 200, -1);
        gtk_label_set_ellipsize(GTK_LABEL(kurzb_label), PANGO_ELLIPSIZE_END);
        gtk_widget_set_halign(kurzb_label, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(row), kurzb_label);
        
        gchar *status_text = get_sync_status_text(akte->seafile_library_id);
        GtkWidget *status_label = gtk_label_new(status_text);
        gtk_widget_set_size_request(status_label, 150, -1);
        gtk_box_append(GTK_BOX(row), status_label);
        g_free(status_text);
        
        gchar *last_sync = format_datetime(akte->last_synced);
        GtkWidget *sync_label = gtk_label_new(last_sync);
        gtk_widget_set_size_request(sync_label, 120, -1);
        gtk_widget_add_css_class(sync_label, "dim-label");
        gtk_box_append(GTK_BOX(row), sync_label);
        g_free(last_sync);
        
        GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_set_hexpand(spacer, TRUE);
        gtk_box_append(GTK_BOX(row), spacer);
        
        ActionData *action_data = g_new0(ActionData, 1);
        action_data->priv = priv;
        action_data->regnr = g_strdup(akte->regnr);
        action_data->library_id = g_strdup(akte->seafile_library_id);
        
        GtkWidget *open_btn = gtk_button_new_with_label("📁 Ordner öffnen");
        g_signal_connect_data(open_btn, "clicked", G_CALLBACK(on_open_folder_clicked),
                             action_data, (GClosureNotify)action_data_free, 0);
        gtk_box_append(GTK_BOX(row), open_btn);
        
        GError *error = NULL;
        gchar *sync_status = sond_seafile_get_sync_status(akte->seafile_library_id, &error);
        
        if (sync_status) {
            /* Akte wird synchronisiert → "Sync pausieren" Button */
            ActionData *stop_data = g_new0(ActionData, 1);
            stop_data->priv = priv;
            stop_data->regnr = g_strdup(akte->regnr);
            stop_data->library_id = g_strdup(akte->seafile_library_id);
            
            GtkWidget *stop_btn = gtk_button_new_with_label("⏸ Sync pausieren");
            g_signal_connect_data(stop_btn, "clicked", G_CALLBACK(on_sync_stop_clicked),
                                 stop_data, (GClosureNotify)action_data_free, 0);
            gtk_box_append(GTK_BOX(row), stop_btn);
            
            g_free(sync_status);
        } else {
            /* Akte wird nicht synchronisiert → "Sync fortsetzen" + "Cache löschen" */
            
            /* Fortsetzen-Button */
            ActionData *resume_data = g_new0(ActionData, 1);
            resume_data->priv = priv;
            resume_data->regnr = g_strdup(akte->regnr);
            resume_data->library_id = g_strdup(akte->seafile_library_id);
            
            GtkWidget *resume_btn = gtk_button_new_with_label("▶ Sync fortsetzen");
            g_signal_connect_data(resume_btn, "clicked", G_CALLBACK(on_sync_resume_clicked),
                                 resume_data, (GClosureNotify)action_data_free, 0);
            gtk_box_append(GTK_BOX(row), resume_btn);
            
            /* Löschen-Button */
            ActionData *delete_data = g_new0(ActionData, 1);
            delete_data->priv = priv;
            delete_data->regnr = g_strdup(akte->regnr);
            delete_data->library_id = g_strdup(akte->seafile_library_id);
            
            GtkWidget *delete_btn = gtk_button_new_with_label("🗑 Cache löschen");
            gtk_widget_add_css_class(delete_btn, "destructive-action");
            g_signal_connect_data(delete_btn, "clicked", G_CALLBACK(on_cache_delete_clicked),
                                 delete_data, (GClosureNotify)action_data_free, 0);
            gtk_box_append(GTK_BOX(row), delete_btn);
        }
        
        if (error) g_error_free(error);
        
        gtk_list_box_append(GTK_LIST_BOX(priv->list_box), row);
    }
}

static void on_refresh_clicked(GtkButton *button, SondModuleOfflinePrivate *priv) {
    update_akte_list(priv);
}

GtkWidget* sond_module_offline_new(SondClient *client) {
    SondModuleOfflinePrivate *priv = g_new0(SondModuleOfflinePrivate, 1);
    priv->client = g_object_ref(client);
    
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(main_box, 20);
    gtk_widget_set_margin_end(main_box, 20);
    gtk_widget_set_margin_top(main_box, 20);
    gtk_widget_set_margin_bottom(main_box, 20);
    
    priv->main_widget = main_box;
    
    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(main_box), header_box);
    
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span size='large' weight='bold'>Offline-Akten</span>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(header_box), title);
    
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(header_box), spacer);
    
    priv->refresh_button = gtk_button_new_with_label("🔄 Aktualisieren");
    g_signal_connect(priv->refresh_button, "clicked", G_CALLBACK(on_refresh_clicked), priv);
    gtk_box_append(GTK_BOX(header_box), priv->refresh_button);
    
    priv->info_label = gtk_label_new("");
    gtk_widget_set_halign(priv->info_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(priv->info_label, "dim-label");
    gtk_box_append(GTK_BOX(main_box), priv->info_label);
    
    GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(main_box), separator);
    
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                    GTK_POLICY_AUTOMATIC,
                                    GTK_POLICY_AUTOMATIC);
    gtk_box_append(GTK_BOX(main_box), scrolled);
    
    priv->list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(priv->list_box), GTK_SELECTION_NONE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), priv->list_box);
    
    g_object_set_data_full(G_OBJECT(main_box), "priv", priv, g_free);
    
    update_akte_list(priv);
    
    return main_box;
}
