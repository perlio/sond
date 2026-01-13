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

/* Globale Variablen für Re-Login */
static SondClient *g_client = NULL;

/**
 * on_auth_failed:
 *
 * Callback bei 401-Fehler. Zeigt Login-Dialog erneut.
 */
static void on_auth_failed(SondClient *client, gpointer user_data) {
    LOG_ERROR("Auth failed - showing login dialog again\n");
    
    /* Login-Dialog zeigen */
    LoginResult *login_result = sond_login_dialog_show(NULL,
                                                        sond_client_get_server_url(client),
                                                        "Session abgelaufen. Bitte erneut anmelden.");
    
    if (login_result && login_result->success) {
        /* Neue Auth setzen */
        sond_client_set_auth(client, login_result->username, login_result->session_token);
        LOG_INFO("Re-login successful for user '%s'\n", login_result->username);
        login_result_free(login_result);
    } else {
        /* User hat abgebrochen → Beenden */
        LOG_INFO("Re-login cancelled by user - exiting\n");
        login_result_free(login_result);
        exit(0);
    }
}

static void on_activate(GtkApplication *app, gpointer user_data) {
    SondClient *client = SOND_CLIENT(user_data);
    
    /* Hauptfenster erstellen */
    SondClientWindow *window = sond_client_window_new(app, client);
    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char *argv[]) {
    gchar *config_file = "SondClient.conf";
    
    logging_init("sond_client");
    
    /* GTK initialisieren (brauchen wir für Login-Dialog) */
    gtk_init();
    
    /* Config-Datei aus Kommandozeile */
    if (argc > 1) {
        config_file = argv[1];
    }
    
    LOG_INFO("Starting SOND Client...\n");
    LOG_INFO("Config: %s\n", config_file);
    
    /* Client erstellen */
    GError *error = NULL;
    SondClient *client = sond_client_new(config_file, &error);
    
    if (!client) {
        LOG_ERROR("Failed to create client: %s\n", error->message);
        g_error_free(error);
        return 1;
    }
    
    /* Login-Dialog zeigen (modal, blocking) */
    LOG_INFO("Showing login dialog...\n");
    LoginResult *login_result = sond_login_dialog_show(NULL,
                                                        sond_client_get_server_url(client),
                                                        NULL);
    
    if (!login_result || !login_result->success) {
        LOG_INFO("Login cancelled by user\n");
        login_result_free(login_result);
        g_object_unref(client);
        return 0;  /* Normaler Exit, User hat abgebrochen */
    }
    
    /* Auth-Informationen im Client setzen */
    sond_client_set_auth(client, login_result->username, login_result->session_token);
    LOG_INFO("Login successful for user '%s'\n", login_result->username);
    
    /* Auth-Failed-Callback setzen */
    sond_client_set_auth_failed_callback(client, on_auth_failed, NULL);
    
    login_result_free(login_result);
    
    /* Globale Referenz für Re-Login */
    g_client = client;
    
    /* Verbindung zum Server herstellen */
    if (!sond_client_connect(client, &error)) {
        LOG_ERROR("Failed to connect to server: %s\n", error->message);
        g_error_free(error);
        g_object_unref(client);
        return 1;
    }
    
    /* GTK Application */
    GtkApplication *app = gtk_application_new("de.rubarth-krieger.sond",
                                               G_APPLICATION_DEFAULT_FLAGS);
    
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), client);
    
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    
    g_object_unref(app);
    g_object_unref(client);
    
    return status;
}
