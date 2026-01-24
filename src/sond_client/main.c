/*
 sond (main.c) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2026 pelo america

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

#include "sond_client.h"
#include "sond_client_window.h"
#include "sond_login_dialog.h"
#include "../sond_log_and_error.h"

#include <gtk/gtk.h>

static gboolean on_login_needed(SondClient *client, gpointer user_data) {
    const gchar *message = (const gchar *)user_data;
    
    LoginResult *login_result = sond_login_dialog_show(NULL,
                                                        sond_client_get_server_url(client),
                                                        message);
    
    if (!login_result || !login_result->success) {
        LOG_INFO("Login cancelled by user\n");
        login_result_free(login_result);
        return FALSE;
    }
    
    sond_client_set_auth(client, login_result->username, login_result->session_token,
    		login_result->seafile_token, login_result->seafile_url);
    
    login_result_free(login_result);
    return TRUE;
}

static void on_auth_failed(SondClient *client, gpointer user_data) {
    LOG_ERROR("Auth failed - session expired\n");
}

static void on_activate(GtkApplication *app, gpointer user_data) {
    SondClient *client = SOND_CLIENT(user_data);
    
    SondClientWindow *window = sond_client_window_new(app, client);
    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char *argv[]) {
    gchar *config_file = "SondClient.conf";
    
    logging_init("sond_client");
    
    gtk_init();
    
    if (argc > 1) {
        config_file = argv[1];
    }
    
    LOG_INFO("Starting SOND Client...\n");
    LOG_INFO("Config: %s\n", config_file);
    
    GError *error = NULL;
    SondClient *client = sond_client_new(config_file, &error);
    
    if (!client) {
        LOG_ERROR("Failed to create client: %s\n", error->message);
        g_error_free(error);
        return 1;
    }
    
    sond_client_set_login_callback(client, on_login_needed, 
                                    "Session abgelaufen. Bitte erneut anmelden.");
    
    sond_client_set_auth_failed_callback(client, on_auth_failed, NULL);
    
    /* KEIN Login beim Start - Login erfolgt erst bei erster Server-Anfrage */
    
    GtkApplication *app = gtk_application_new("de.rubarth-krieger.sond",
                                               G_APPLICATION_DEFAULT_FLAGS);
    
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), client);
    
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    
    g_object_unref(app);
    g_object_unref(client);
    
    return status;
}
