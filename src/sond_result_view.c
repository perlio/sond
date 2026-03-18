/*
 * sond_result_view.c — Allgemeines Ergebnisfenster mit konfigurierbaren Spalten
 * Copyright (C) 2026  pelo america
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 */

#include "sond_result_view.h"

#include <gtk/gtk.h>
#include <glib.h>

/* Schlüssel für g_object_set_data */
#define KEY_TREEVIEW  "sond-rv-treeview"
#define KEY_LISTSTORE "sond-rv-liststore"
#define KEY_NUM_COLS  "sond-rv-num-cols"

/* ==========================================================================
 * Öffentliche API
 * ======================================================================== */

GtkWidget* sond_result_view_new(GtkWindow       *parent,
                                 gchar const     *title,
                                 gchar const    **column_titles,
                                 GCallback        row_activated_cb,
                                 gpointer         cb_data) {
    GtkWidget    *window   = NULL;
    GtkWidget    *swindow  = NULL;
    GtkWidget    *treeview = NULL;
    GtkListStore *store    = NULL;
    GType        *col_types = NULL;
    gint          num_cols  = 0;

    g_return_val_if_fail(column_titles != NULL, NULL);

    /* Anzahl Spalten zählen */
    while (column_titles[num_cols])
        num_cols++;

    g_return_val_if_fail(num_cols > 0, NULL);

    /* --- Fenster --- */
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), title ? title : "Ergebnisse");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 400);
    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(window), parent);
        gtk_window_set_destroy_with_parent(GTK_WINDOW(window), TRUE);
    }
    g_signal_connect(window, "delete-event",
                     G_CALLBACK(gtk_widget_hide_on_delete), NULL);

    /* --- ListStore: alle Spalten G_TYPE_STRING --- */
    col_types = g_new(GType, num_cols);
    for (gint i = 0; i < num_cols; i++)
        col_types[i] = G_TYPE_STRING;

    store = gtk_list_store_newv(num_cols, col_types);
    g_free(col_types);

    /* --- TreeView --- */
    treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store); /* TreeView hält die Referenz */

    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeview), TRUE);
    gtk_tree_selection_set_mode(
            gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview)),
            GTK_SELECTION_MULTIPLE);

    /* Spalten anlegen — leerer Titel ("") = nur im Store, nicht im TreeView */
    gint last_visible = -1;
    for (gint i = 0; i < num_cols; i++)
        if (column_titles[i][0] != '\0')
            last_visible = i;

    for (gint i = 0; i < num_cols; i++) {
        GtkCellRenderer   *renderer = NULL;
        GtkTreeViewColumn *col      = NULL;

        if (column_titles[i][0] == '\0')
            continue; /* versteckte Spalte: nur im Store */

        renderer = gtk_cell_renderer_text_new();
        col      = gtk_tree_view_column_new_with_attributes(
                       column_titles[i], renderer, "text", i, NULL);

        if (i == last_visible) {
            /* letzte sichtbare Spalte: verbleibenden Platz, Zeilenumbruch */
            gtk_tree_view_column_set_expand(col, TRUE);
            g_object_set(renderer,
                         "wrap-mode", PANGO_WRAP_WORD_CHAR,
                         "wrap-width", 400,
                         NULL);
        } else {
            gtk_tree_view_column_set_resizable(col, TRUE);
            gtk_tree_view_column_set_min_width(col, 80);
        }

        gtk_tree_view_column_set_sort_column_id(col, i);
        gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), col);
    }

    if (row_activated_cb)
        g_signal_connect(treeview, "row-activated", row_activated_cb, cb_data);

    /* --- ScrolledWindow --- */
    swindow = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(swindow),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(swindow), treeview);
    gtk_container_add(GTK_CONTAINER(window), swindow);

    /* Metadaten am Fenster speichern */
    g_object_set_data(G_OBJECT(window), KEY_TREEVIEW,  treeview);
    g_object_set_data(G_OBJECT(window), KEY_LISTSTORE, store);
    g_object_set_data(G_OBJECT(window), KEY_NUM_COLS,  GINT_TO_POINTER(num_cols));

    return window;
}

void sond_result_view_append(GtkWidget    *result_view,
                              gchar const **values) {
    GtkListStore *store    = NULL;
    GtkTreeIter   iter     = { 0 };
    gint          num_cols = 0;

    g_return_if_fail(GTK_IS_WIDGET(result_view));
    g_return_if_fail(values != NULL);

    store    = GTK_LIST_STORE(g_object_get_data(G_OBJECT(result_view),
                                                 KEY_LISTSTORE));
    num_cols = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(result_view),
                                                  KEY_NUM_COLS));
    g_return_if_fail(store != NULL);

    gtk_list_store_append(store, &iter);

    for (gint i = 0; i < num_cols; i++) {
        gchar const *val = (values[i] != NULL) ? values[i] : "";
        gtk_list_store_set(store, &iter, i, val, -1);
    }
}

GtkWidget* sond_result_view_get_treeview(GtkWidget *result_view) {
    g_return_val_if_fail(GTK_IS_WIDGET(result_view), NULL);

    return GTK_WIDGET(g_object_get_data(G_OBJECT(result_view), KEY_TREEVIEW));
}

gchar* sond_result_view_get_selected_value(GtkWidget *result_view,
                                            gint       column) {
    GtkWidget        *treeview = NULL;
    GtkTreeSelection *sel      = NULL;
    GtkTreeModel     *model    = NULL;
    GtkTreeIter       iter     = { 0 };
    gchar            *value    = NULL;

    g_return_val_if_fail(GTK_IS_WIDGET(result_view), NULL);

    treeview = sond_result_view_get_treeview(result_view);
    g_return_val_if_fail(treeview != NULL, NULL);

    sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));

    if (!gtk_tree_selection_get_selected(sel, &model, &iter))
        return NULL;

    gtk_tree_model_get(model, &iter, column, &value, -1);

    return value; /* transfer full */
}
