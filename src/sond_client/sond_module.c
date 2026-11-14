/*
 sond (sond_module.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_module.h"

#define ABORT_KEY "sond-module-abort"

typedef struct {
    SondModuleAbortFunc func;
    gpointer user_data;
    gboolean done;
} AbortEntry;

void sond_module_set_abort(GtkWidget *module, SondModuleAbortFunc func,
        gpointer user_data) {
    AbortEntry *entry = NULL;

    g_return_if_fail(GTK_IS_WIDGET(module));

    entry = g_new0(AbortEntry, 1);
    entry->func = func;
    entry->user_data = user_data;
    g_object_set_data_full(G_OBJECT(module), ABORT_KEY, entry, g_free);
}

void sond_module_abort(GtkWidget *module) {
    AbortEntry *entry = NULL;

    g_return_if_fail(GTK_IS_WIDGET(module));

    entry = g_object_get_data(G_OBJECT(module), ABORT_KEY);
    if (!entry || !entry->func || entry->done)
        return;

    entry->done = TRUE;
    entry->func(module, entry->user_data);
}

void sond_module_abort_stack(GtkStack *stack) {
    GtkWidget *active = NULL;

    g_return_if_fail(GTK_IS_STACK(stack));

    active = gtk_stack_get_visible_child(stack);
    if (active)
        sond_module_abort(active);

    for (GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(stack)); child;
            child = gtk_widget_get_next_sibling(child))
        sond_module_abort(child);
}
