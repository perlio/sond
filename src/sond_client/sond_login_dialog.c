/*
 sond (sond_login_dialog.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_login_dialog.h"
#include "../sond_log_and_error.h"

#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

void login_result_free(LoginResult *result) {
    if (!result) return;
    
    g_free(result->username);
    g_free(result->session_token);
    g_free(result);
}

typedef struct {
    GtkWidget *username_entry;
    GtkWidget *password_entry;
    GtkWidget *error_label;
    const gchar *server_url;
    LoginResult *result;
    GMainLoop *loop;
} LoginDialogData;

static void on_login_clicked(GtkButton *button, LoginDialogData *data) {
    const gchar *username = gtk_editable_get_text(GTK_EDITABLE(data->username_entry));
    const gchar *password = gtk_editable_get_text(GTK_EDITABLE(data->password_entry));
    
    if (!username || strlen(username) == 0) {
        gtk_label_set_text(GTK_LABEL(data->error_label), "Bitte Username eingeben");
        return;
    }
    
    if (!password || strlen(password) == 0) {
        gtk_label_set_text(GTK_LABEL(data->error_label), "Bitte Passwort eingeben");
        return;
    }
    
    gtk_label_set_text(GTK_LABEL(data->error_label), "Authentifiziere...");
    
    /* POST /auth/login */
    gchar *url = g_strdup_printf("%s/auth/login", data->server_url);
    
    SoupSession *session = soup_session_new();
    SoupMessage *msg = soup_message_new("POST", url);
    g_free(url);
    
    /* JSON Body erstellen */
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "username");
    json_builder_add_string_value(builder, username);
    json_builder_set_member_name(builder, "password");
    json_builder_add_string_value(builder, password);
    json_builder_end_object(builder);
    
    JsonGenerator *generator = json_generator_new();
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(generator, root);
    gchar *json_body = json_generator_to_data(generator, NULL);
    
    soup_message_set_request_body_from_bytes(msg, "application/json",
        g_bytes_new_take(json_body, strlen(json_body)));
    
    GError *error = NULL;
    GBytes *response = soup_session_send_and_read(session, msg, NULL, &error);
    
    if (!response) {
        gchar *err_msg = g_strdup_printf("Verbindungsfehler: %s",
                                        error ? error->message : "Unbekannt");
        gtk_label_set_text(GTK_LABEL(data->error_label), err_msg);
        g_free(err_msg);
        if (error) g_error_free(error);
        json_node_free(root);
        g_object_unref(generator);
        g_object_unref(builder);
        g_object_unref(msg);
        g_object_unref(session);
        return;
    }
    
    guint status = soup_message_get_status(msg);
    
    if (status != 200) {
        gsize size;
        const gchar *body = g_bytes_get_data(response, &size);
        
        /* Versuche Fehlermeldung zu parsen */
        JsonParser *parser = json_parser_new();
        gchar *error_msg = "Login fehlgeschlagen";
        
        if (json_parser_load_from_data(parser, body, size, NULL)) {
            JsonNode *root_node = json_parser_get_root(parser);
            JsonObject *obj = json_node_get_object(root_node);
            
            if (json_object_has_member(obj, "error")) {
                error_msg = g_strdup(json_object_get_string_member(obj, "error"));
            }
        }
        
        gtk_label_set_text(GTK_LABEL(data->error_label), error_msg);
        
        g_object_unref(parser);
        g_bytes_unref(response);
        json_node_free(root);
        g_object_unref(generator);
        g_object_unref(builder);
        g_object_unref(msg);
        g_object_unref(session);
        return;
    }
    
    /* Erfolg! Response parsen */
    gsize size;
    const gchar *response_data = g_bytes_get_data(response, &size);
    
    JsonParser *parser = json_parser_new();
    
    if (json_parser_load_from_data(parser, response_data, size, NULL)) {
        JsonNode *root_node = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root_node);
        
        if (json_object_has_member(obj, "data")) {
            JsonObject *data_obj = json_object_get_object_member(obj, "data");
            
            const gchar *session_token = json_object_get_string_member(data_obj, "session_token");
            const gchar *returned_username = json_object_get_string_member(data_obj, "username");
            
            /* LoginResult erstellen */
            data->result = g_new0(LoginResult, 1);
            data->result->success = TRUE;
            data->result->username = g_strdup(returned_username);
            data->result->session_token = g_strdup(session_token);
            
            LOG_INFO("Login successful for user '%s'\n", returned_username);
        }
    }
    
    g_object_unref(parser);
    g_bytes_unref(response);
    json_node_free(root);
    g_object_unref(generator);
    g_object_unref(builder);
    g_object_unref(msg);
    g_object_unref(session);
    
    /* Dialog schließen */
    if (data->loop) {
        g_main_loop_quit(data->loop);
    }
}

static void on_cancel_clicked(GtkButton *button, LoginDialogData *data) {
    /* Abbruch → Ergebnis bleibt NULL */
    if (data->loop) {
        g_main_loop_quit(data->loop);
    }
}

LoginResult* sond_login_dialog_show(GtkWindow *parent,
                                     const gchar *server_url,
                                     const gchar *error_message) {
    LoginDialogData data = {0};
    data.server_url = server_url;
    data.result = NULL;
    
    /* Dialog erstellen */
    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "SOND - Login");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 250);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    
    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
    }
    
    /* Content Box */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 15);
    gtk_widget_set_margin_start(vbox, 30);
    gtk_widget_set_margin_end(vbox, 30);
    gtk_widget_set_margin_top(vbox, 30);
    gtk_widget_set_margin_bottom(vbox, 30);
    gtk_window_set_child(GTK_WINDOW(dialog), vbox);
    
    /* Titel */
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "<span size='large' weight='bold'>Anmelden</span>");
    gtk_box_append(GTK_BOX(vbox), title);
    
    /* Fehlermeldung (falls vorhanden) */
    data.error_label = gtk_label_new(error_message ? error_message : "");
    gtk_label_set_wrap(GTK_LABEL(data.error_label), TRUE);
    gtk_widget_add_css_class(data.error_label, "error");
    gtk_box_append(GTK_BOX(vbox), data.error_label);
    
    /* Grid für Felder */
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_box_append(GTK_BOX(vbox), grid);
    
    /* Username */
    GtkWidget *username_label = gtk_label_new("Benutzername:");
    gtk_widget_set_halign(username_label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), username_label, 0, 0, 1, 1);
    
    data.username_entry = gtk_entry_new();
    gtk_widget_set_hexpand(data.username_entry, TRUE);
    gtk_grid_attach(GTK_GRID(grid), data.username_entry, 1, 0, 1, 1);
    
    /* Password */
    GtkWidget *password_label = gtk_label_new("Passwort:");
    gtk_widget_set_halign(password_label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), password_label, 0, 1, 1, 1);
    
    data.password_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(data.password_entry), FALSE);
    gtk_widget_set_hexpand(data.password_entry, TRUE);
    gtk_grid_attach(GTK_GRID(grid), data.password_entry, 1, 1, 1, 1);
    
    /* Button Box */
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(button_box, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(vbox), button_box);
    
    GtkWidget *cancel_button = gtk_button_new_with_label("Abbrechen");
    g_signal_connect(cancel_button, "clicked", G_CALLBACK(on_cancel_clicked), &data);
    gtk_box_append(GTK_BOX(button_box), cancel_button);
    
    GtkWidget *login_button = gtk_button_new_with_label("Anmelden");
    gtk_widget_add_css_class(login_button, "suggested-action");
    g_signal_connect(login_button, "clicked", G_CALLBACK(on_login_clicked), &data);
    gtk_box_append(GTK_BOX(button_box), login_button);
    
    /* Enter-Taste im Password-Feld löst Login aus */
    GtkEventController *controller = gtk_event_controller_key_new();
    g_signal_connect_swapped(controller, "key-pressed",
                             G_CALLBACK(gtk_widget_activate), login_button);
    gtk_widget_add_controller(data.password_entry, controller);
    
    /* Dialog zeigen */
    gtk_window_present(GTK_WINDOW(dialog));
    
    /* Modal Event Loop */
    data.loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(data.loop);
    g_main_loop_unref(data.loop);
    
    /* Dialog schließen */
    gtk_window_destroy(GTK_WINDOW(dialog));
    
    /* Verarbeite Events damit Dialog wirklich verschwindet */
    while (g_main_context_pending(NULL)) {
        g_main_context_iteration(NULL, FALSE);
    }
    
    return data.result;
}
