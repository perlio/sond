/*
 zond (zond_indexsuche.c) - Akten, Beweisstücke, Unterlagen
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

#include "zond_indexsuche.h"

#include <gtk/gtk.h>
#include <glib.h>

#include "../misc.h"
#include "../sond_index.h"
#include "../sond_result_view.h"
#include "../sond_fileparts.h"
#include "../sond_process_file.h"
#include "../sond_renderer.h"

#include "zond_init.h"
#include "zond_treeviewfm.h"
#include "zond_treeview.h"

#include "10init/app_window.h"
#include "20allgemein/project.h"

#include "40viewer/viewer.h"


/* -------------------------------------------------------------------------
 * row-activated: Treffer anspringen und im Viewer markieren
 * ---------------------------------------------------------------------- */

static void
zond_indexsuche_row_activated(GtkTreeView *treeview, GtkTreePath *tree_path,
        GtkTreeViewColumn *col, gpointer data) {
    Projekt      *zond         = (Projekt *) data;
    GtkTreeModel *model        = NULL;
    GtkTreeIter   iter         = { 0 };
    gchar        *filename     = NULL;
    gchar        *page_nr_str  = NULL;
    gchar        *char_pos_str = NULL;
    gint          page_nr      = -1;
    gint          char_pos_in_page = 0;
    gint          rc           = 0;
    GError       *error        = NULL;

    /* term wurde beim Öffnen des Ergebnisfensters als widget-data gespeichert */
    const gchar *term = g_object_get_data(
            G_OBJECT(gtk_widget_get_toplevel(GTK_WIDGET(treeview))),
            "index-search-term");

    model = gtk_tree_view_get_model(treeview);
    gtk_tree_model_get_iter(model, &iter, tree_path);
    gtk_tree_model_get(model, &iter,
            0, &filename,
            1, &page_nr_str,
            3, &char_pos_str,
            -1);

    if (page_nr_str && *page_nr_str)
        page_nr = atoi(page_nr_str) - 1;
    g_free(page_nr_str);

    if (char_pos_str && *char_pos_str)
        char_pos_in_page = atoi(char_pos_str);
    g_free(char_pos_str);

    /* FS-Ansicht einschalten falls nötig */
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(zond->fs_button)))
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(zond->fs_button), TRUE);

    {
        GtkTreeIter iter_fm = { 0 };

        rc = sond_treeviewfm_file_part_visible(
                SOND_TREEVIEWFM(zond->treeview[BAUM_FS]),
                NULL, filename, TRUE, &iter_fm, &error);
        if (rc == -1) {
            display_message(zond->app_window,
                    "Fehler\n\n",
                    error ? error->message : "?", NULL);
            g_clear_error(&error);
        } else if (rc == 1) {
            SondTVFMItem *stvfm_item = NULL;
            SondFilePart *sfp        = NULL;

            gtk_tree_model_get(
                    gtk_tree_view_get_model(GTK_TREE_VIEW(zond->treeview[BAUM_FS])),
                    &iter_fm, 0, &stvfm_item, -1);
            sfp = sond_tvfm_item_get_sond_file_part(stvfm_item);
            g_object_unref(stvfm_item);

            if (sfp && SOND_IS_FILE_PART_PDF(sfp) && page_nr >= 0) {
                /* PDF → interner PDF-Viewer */
                PdfPos pos_pdf = { page_nr, 0 };

                rc = zond_treeview_oeffnen_internal_viewer(zond,
                        SOND_FILE_PART_PDF(sfp), NULL, &pos_pdf, &error);
                if (rc) {
                    display_message(zond->app_window,
                            "Fehler beim Öffnen\n\n",
                            error ? error->message : "?", NULL);
                    g_clear_error(&error);
                } else if (term && zond->arr_pv->len > 0) {
                    PdfViewer *pv = g_ptr_array_index(zond->arr_pv,
                            zond->arr_pv->len - 1);
                    viewer_highlight_at_char_pos(pv, page_nr,
                            char_pos_in_page, term);
                }
            } else if (sfp) {
                /* Nicht-PDF → sond_renderer mit Highlighting */
                GBytes *bytes = sond_file_part_get_bytes(sfp, &error);
                if (!bytes) {
                    display_message(zond->app_window,
                            "Fehler beim Lesen der Datei\n\n",
                            error ? error->message : "?", NULL);
                    g_clear_error(&error);
                } else {
                    rc = sond_render_with_term(bytes, sfp,
                            NULL, term, char_pos_in_page, &error);
                    g_bytes_unref(bytes);
                    if (rc) {
                        display_message(zond->app_window,
                                "Fehler beim Öffnen\n\n",
                                error ? error->message : "?", NULL);
                        g_clear_error(&error);
                    }
                }
            }
        }
    }

    g_free(filename);
}


/* -------------------------------------------------------------------------
 * Öffentliche Funktion: Dialog + Ergebnisanzeige
 * ---------------------------------------------------------------------- */

void
zond_indexsuche_activate(Projekt *zond) {
    GtkWidget *dialog     = NULL;
    GtkWidget *content    = NULL;
    GtkWidget *grid       = NULL;
    GtkWidget *label_term = NULL;
    GtkWidget *entry_term = NULL;
    GtkWidget *label_ctx  = NULL;
    GtkWidget *entry_ctx  = NULL;
    gint       response   = 0;

    if (!zond->wctx || !zond->wctx->index_ctx) {
        display_message(zond->app_window,
                "Kein Index vorhanden.\n"
                "Bitte zuerst Dateien indizieren.", NULL);
        return;
    }

    /* --- Eingabe-Dialog --- */
    dialog = gtk_dialog_new_with_buttons(
            "Index durchsuchen",
            GTK_WINDOW(zond->app_window),
            GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
            "Suchen",    GTK_RESPONSE_OK,
            "Abbrechen", GTK_RESPONSE_CANCEL,
            NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_widget_set_margin_start(grid, 12);
    gtk_widget_set_margin_end(grid, 12);
    gtk_widget_set_margin_top(grid, 12);
    gtk_widget_set_margin_bottom(grid, 12);

    label_term = gtk_label_new("Suchbegriff:");
    gtk_widget_set_halign(label_term, GTK_ALIGN_END);
    entry_term = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(entry_term), TRUE);
    gtk_widget_set_hexpand(entry_term, TRUE);

    label_ctx = gtk_label_new("Im Kontext von:");
    gtk_widget_set_halign(label_ctx, GTK_ALIGN_END);
    entry_ctx = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(entry_ctx), TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry_ctx), "optional");
    gtk_widget_set_hexpand(entry_ctx, TRUE);

    gtk_grid_attach(GTK_GRID(grid), label_term, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry_term, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), label_ctx,  0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry_ctx,  1, 1, 1, 1);

    gtk_container_add(GTK_CONTAINER(content), grid);
    gtk_widget_show_all(dialog);

    response = gtk_dialog_run(GTK_DIALOG(dialog));

    if (response == GTK_RESPONSE_OK) {
        const gchar *term = gtk_entry_get_text(GTK_ENTRY(entry_term));
        const gchar *ctx  = gtk_entry_get_text(GTK_ENTRY(entry_ctx));

        if (term && *term) {
            GPtrArray *hits  = NULL;
            GError    *error = NULL;

            hits = sond_index_search(
                    zond->wctx->index_ctx,
                    term,
                    (ctx && *ctx) ? ctx : NULL,
                    &error);

            if (!hits) {
                gtk_widget_destroy(dialog);
                display_message(zond->app_window,
                        "Fehler bei der Indexsuche:\n\n",
                        error ? error->message : "?", NULL);
                g_clear_error(&error);
                return;
            }

            if (hits->len == 0) {
                gtk_widget_destroy(dialog);
                g_ptr_array_unref(hits);
                display_message(zond->app_window,
                        "Keine Treffer gefunden.", NULL);
                return;
            }

            /* Ergebnisfenster:
             * Spalte 0: Dateiname
             * Spalte 1: Seite (1-basiert)
             * Spalte 2: Fundstelle (snippet)
             * Spalte 3: char_pos_in_page (versteckt)
             */
            gchar const *cols[] = { "Datei", "Seite", "Fundstelle", "", NULL };
            gchar *titel = g_strdup_printf(
                    "Index-Suche: \u201e%s\u201c", term);
            GtkWidget *rv = sond_result_view_new(
                    GTK_WINDOW(zond->app_window),
                    titel,
                    cols,
                    G_CALLBACK(zond_indexsuche_row_activated),
                    zond);
            g_free(titel);

            g_object_set_data_full(G_OBJECT(rv), "index-search-term",
                    g_strdup(term), g_free);

            for (guint i = 0; i < hits->len; i++) {
                SondIndexHit *hit = g_ptr_array_index(hits, i);
                gchar *page_str = (hit->page_nr >= 0)
                        ? g_strdup_printf("%d", hit->page_nr + 1)
                        : g_strdup("");
                gchar *char_pos_str = g_strdup_printf("%d",
                        hit->char_pos_in_page);
                gchar const *row[] = {
                        hit->filename,
                        page_str,
                        hit->snippet ? hit->snippet : "",
                        char_pos_str,
                        NULL
                };
                sond_result_view_append(rv, row);
                g_free(page_str);
                g_free(char_pos_str);
            }

            g_ptr_array_unref(hits);
            gtk_widget_show_all(rv);
        }
    }

    gtk_widget_destroy(dialog);
}
