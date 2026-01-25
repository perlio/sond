/*
 sond (sond_client_window.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_client_window.h"
#include "modules/sond_module_akte.h"
#include "modules/sond_module_offline.h"
#include "../sond_log_and_error.h"

struct _SondClientWindow {
    GtkApplicationWindow parent_instance;
    
    SondClient *client;
    
    /* UI-Elemente */
    GtkWidget *main_stack;
    GtkWidget *module_selector;
    GtkWidget *status_bar;
    
    /* Module */
    GtkWidget *module_akte;
    GtkWidget *module_offline;
};

G_DEFINE_TYPE(SondClientWindow, sond_client_window, GTK_TYPE_APPLICATION_WINDOW)

static void on_module_button_clicked(GtkButton *button, SondClientWindow *window) {
    const gchar *module_name = (const gchar *)g_object_get_data(G_OBJECT(button), "module-name");
    
    if (module_name) {
        gtk_stack_set_visible_child_name(GTK_STACK(window->main_stack), module_name);
    }
}

static void sond_client_window_init(SondClientWindow *self) {
    /* Fenster-Eigenschaften */
    gtk_window_set_title(GTK_WINDOW(self), "SOND Client");
    gtk_window_set_default_size(GTK_WINDOW(self), 1024, 768);
    
    /* Haupt-Layout: Vertical Box */
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(self), main_box);
    
    /* Header Bar */
    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(header), TRUE);
    gtk_window_set_titlebar(GTK_WINDOW(self), header);
    
    /* Modul-Auswahlleiste */
    GtkWidget *module_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_margin_start(module_box, 10);
    gtk_widget_set_margin_end(module_box, 10);
    gtk_widget_set_margin_top(module_box, 10);
    gtk_widget_set_margin_bottom(module_box, 10);
    gtk_box_append(GTK_BOX(main_box), module_box);
    
    GtkWidget *label = gtk_label_new("Module:");
    gtk_box_append(GTK_BOX(module_box), label);
    
    /* Button für "Akte"-Modul */
    GtkWidget *btn_akte = gtk_button_new_with_label("Akte");
    g_object_set_data(G_OBJECT(btn_akte), "module-name", (gpointer)"akte");
    g_signal_connect(btn_akte, "clicked", G_CALLBACK(on_module_button_clicked), self);
    gtk_box_append(GTK_BOX(module_box), btn_akte);
    
    /* Button für "Offline"-Modul */
    GtkWidget *btn_offline = gtk_button_new_with_label("Offline-Akten");
    g_object_set_data(G_OBJECT(btn_offline), "module-name", (gpointer)"offline");
    g_signal_connect(btn_offline, "clicked", G_CALLBACK(on_module_button_clicked), self);
    gtk_box_append(GTK_BOX(module_box), btn_offline);
    
    /* Separator */
    GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(main_box), separator);
    
    /* Stack für Module */
    self->main_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(self->main_stack), 
                                   GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_widget_set_vexpand(self->main_stack, TRUE);
    gtk_box_append(GTK_BOX(main_box), self->main_stack);
    
    /* Platzhalter für leere Ansicht */
    GtkWidget *welcome = gtk_label_new("Bitte wählen Sie ein Modul aus.");
    gtk_stack_add_named(GTK_STACK(self->main_stack), welcome, "welcome");
    
    /* Status-Leiste */
    self->status_bar = gtk_label_new("Nicht verbunden");
    gtk_widget_set_margin_start(self->status_bar, 10);
    gtk_widget_set_margin_end(self->status_bar, 10);
    gtk_widget_set_margin_top(self->status_bar, 5);
    gtk_widget_set_margin_bottom(self->status_bar, 5);
    gtk_widget_add_css_class(self->status_bar, "dim-label");
    gtk_box_append(GTK_BOX(main_box), self->status_bar);
}

static void sond_client_window_class_init(SondClientWindowClass *klass) {
}

SondClientWindow* sond_client_window_new(GtkApplication *app, SondClient *client) {
    SondClientWindow *window = g_object_new(SOND_TYPE_CLIENT_WINDOW,
                                             "application", app,
                                             NULL);
    
    window->client = g_object_ref(client);
    
    /* Akte-Modul erstellen */
    window->module_akte = sond_module_akte_new(client);
    gtk_stack_add_named(GTK_STACK(window->main_stack), window->module_akte, "akte");
    
    /* Offline-Modul erstellen */
    window->module_offline = sond_module_offline_new(client);
    gtk_stack_add_named(GTK_STACK(window->main_stack), window->module_offline, "offline");

    return window;
}
