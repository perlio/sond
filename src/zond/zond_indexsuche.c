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
#include "zond_pdf_document.h"

#include "10init/app_window.h"
#include "10init/headerbar.h"
#include "20allgemein/project.h"

#include "40viewer/viewer.h"
#include "40viewer/viewer_search.h"
#include "40viewer/document.h"

#include "99conv/general.h"


/* -------------------------------------------------------------------------
 * row-activated: Treffer anspringen und im Viewer markieren
 * ---------------------------------------------------------------------- */

void
zond_indexsuche_row_activated(GtkTreeView *treeview, GtkTreePath *tree_path,
        GtkTreeViewColumn *col, gpointer data) {
    Projekt      *zond         = (Projekt *) data;
    GtkTreeModel *model        = NULL;
    GtkTreeIter   iter         = { 0 };
    gchar        *filename     = NULL;
    gchar        *char_pos_str = NULL;
    gchar        *page_nr_raw_str = NULL;
    gint          page_nr      = -1; /* roh, Stand: letzte Indizierung */
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
            3, &char_pos_str,
            4, &page_nr_raw_str,
            -1);

    /* Absichtlich die rohe (Spalte 4), nicht die beim Aufbau der Liste
     * bereits angezeigte Seite (Spalte 1) verwenden: seitdem können in
     * einem offenen Viewer weitere Seiten eingefügt/gelöscht worden sein.
     * Die Live-Übersetzung erfolgt daher unten, unmittelbar vor der
     * Navigation, immer neu aus der rohen Seite. */
    if (page_nr_raw_str && *page_nr_raw_str)
        page_nr = atoi(page_nr_raw_str);
    g_free(page_nr_raw_str);

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
            	DisplayedDocument* dd = NULL;
            	ZondPdfDocument* zpdfd_open = NULL;
            	gint page_nr_akt = page_nr;

            	/* Erneut (unmittelbar vor der Navigation) gegen den JETZT
            	 * aktuellen Live-Stand übersetzen - nicht gegen den Stand
            	 * beim Aufbau der Ergebnisliste. Falls die Datei offen ist
            	 * und dort inzwischen (auch erst nach Öffnen der Liste)
            	 * Seiten eingefügt/gelöscht wurden, ändert sich sonst die
            	 * Zielseite zwischen Anzeige und Klick. */
            	zpdfd_open = zond_pdf_document_is_open(SOND_FILE_PART_PDF(sfp));
            	if (zpdfd_open) {
            		Anbindung anbindung = { { page_nr, 0 }, { page_nr, EOP } };

            		anbindung_aktualisieren(zpdfd_open, &anbindung);
            		page_nr_akt = anbindung.von.seite;
            	}

            	/* PDF → interner PDF-Viewer */
                PdfPos pos_pdf = { page_nr_akt, 0 };
                dd = document_new_displayed_document(SOND_FILE_PART_PDF(sfp), NULL, NULL,
                		FALSE, NULL, &error);
                if (!dd) {
                	display_message(zond->app_window, "DisplayedDocument "
                			"konnte nicht erstellt werden:\n", error->message, NULL);
                	g_error_free(error);
                	g_free(filename);
                	return;
                }

                rc = zond_treeview_oeffnen_internal_viewer(zond,
                        dd, &pos_pdf, &error);
                if (rc) {
                    display_message(zond->app_window,
                            "Fehler beim Öffnen\n\n",
                            error ? error->message : "?", NULL);
                    g_clear_error(&error);
                } else if (term && zond->arr_pv->len > 0) {
                    PdfViewer *pv = g_ptr_array_index(zond->arr_pv,
                            zond->arr_pv->len - 1);
                    viewer_highlight_at_char_pos(pv, page_nr_akt,
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
 * Vollständigkeits-Prüfung vor der Suche über eine Auswahl:
 *
 * "Ausgewählte Punkte" filtert Suchtreffer auf den Index-Stand, wie er
 * zuletzt indiziert wurde - fehlt für einen ausgewählten Punkt (Teile
 * der) Indizierung, sieht das Ergebnis genauso aus wie "kommt dort nicht
 * vor", ohne daß der Nutzer das unterscheiden könnte. Deshalb wird die
 * Auswahl VOR der eigentlichen Suche auf Vollständigkeit geprüft (nicht
 * erst hinterher an einem leeren Ergebnis herumgerätselt) - fehlt etwas,
 * wird nachgefragt, ob jetzt nachindiziert werden soll.
 * ---------------------------------------------------------------------- */

typedef struct {
    SondFilePart  *sfp;          /* (transfer none) */
    SondPageRange *range;        /* (transfer none), kann NULL sein (ganze Datei) */
    gchar         *display_name;
    gint           missing;
    gint           total;
} SondIndexCoverageGap;

static void
sond_index_coverage_gap_free(gpointer p) {
    SondIndexCoverageGap *gap = p;
    if (!gap) return;
    g_free(gap->display_name);
    g_free(gap);
}

/* Ermittelt für einen einzelnen ausgewählten Punkt (sfp, range), wie viele
 * der erwarteten Seiten noch nicht indiziert sind. Bei PDFs ohne
 * eingeschränkten Seitenbereich (range == NULL, "ganze Datei") wird dazu
 * die tatsächliche Seitenzahl ermittelt - ein reiner Metadaten-Zugriff
 * (pdf_count_pages), kein Volltext/OCR, sollte also auch bei vielen/
 * großen PDFs schnell gehen. Nicht-PDF-Fileparts gelten als ein einziger
 * "virtueller" Eintrag (page_nr = -1 in der Konvention von sond_index.c).
 *
 * Returns: TRUE bei Erfolg (auch wenn missing == 0), FALSE bei Fehler
 *          (z.B. Datei nicht lesbar) - error gesetzt.
 */
static gboolean
check_coverage_one(Projekt *zond, SondFilePart *sfp, SondPageRange *range,
        gint *out_missing, gint *out_total, GError **error) {
    SondIndexCtx *index_ctx = zond->wctx->index_ctx;
    gchar        *fp        = sond_file_part_get_filepart(sfp);
    gint          missing   = 0;
    gint          total     = 1;

    if (!fp) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "Dateipfad nicht ermittelbar");
        return FALSE;
    }

    if (SOND_IS_FILE_PART_PDF(sfp)) {
        gint von = 0, bis = 0;

        if (range && range->von >= 0) {
            von = range->von;
            bis = range->bis;
        } else {
            pdf_document *doc = sond_file_part_pdf_open_document(zond->ctx,
                    SOND_FILE_PART_PDF(sfp), FALSE, FALSE, error);
            if (!doc) {
                g_free(fp);
                return FALSE;
            }
            bis = pdf_count_pages(zond->ctx, doc) - 1;
            pdf_drop_document(zond->ctx, doc);
        }
        total = bis - von + 1;

        GArray     *indexed     = sond_index_ctx_get_pages_for_file(index_ctx, fp);
        GHashTable *indexed_set = g_hash_table_new(NULL, NULL);
        for (guint i = 0; i < indexed->len; i++)
            g_hash_table_add(indexed_set,
                    GINT_TO_POINTER(g_array_index(indexed, gint, i)));

        for (gint p = von; p <= bis; p++)
            if (!g_hash_table_contains(indexed_set, GINT_TO_POINTER(p)))
                missing++;

        g_hash_table_destroy(indexed_set);
        g_array_free(indexed, TRUE);
    } else {
        GArray *indexed = sond_index_ctx_get_pages_for_file(index_ctx, fp);
        total   = 1;
        missing = (indexed->len == 0) ? 1 : 0;
        g_array_free(indexed, TRUE);
    }

    g_free(fp);
    *out_missing = missing;
    *out_total   = total;
    return TRUE;
}

/* Prüft alle Punkte in ht_fileparts. Fehler beim Prüfen einzelner Punkte
 * (z.B. Datei nicht lesbar) werden nur geloggt, nicht als Gesamtfehler
 * propagiert - lieber eine unvollständige Diagnose als die Suche deshalb
 * ganz zu blockieren.
 *
 * Returns: (transfer full) GPtrArray von SondIndexCoverageGap* für alle
 *          Punkte mit mindestens einer fehlenden Seite (leer, wenn alles
 *          vollständig indiziert ist).
 */
static GPtrArray*
check_coverage(Projekt *zond, GHashTable *ht_fileparts) {
    GPtrArray *gaps = g_ptr_array_new_with_free_func(sond_index_coverage_gap_free);
    GHashTableIter iter_sel;
    gpointer key = NULL, value = NULL;

    g_hash_table_iter_init(&iter_sel, ht_fileparts);
    while (g_hash_table_iter_next(&iter_sel, &key, &value)) {
        SondFilePart  *sfp   = (SondFilePart*) key;
        SondPageRange *range = (SondPageRange*) value;
        gint           missing = 0, total = 0;
        GError        *error   = NULL;

        if (!check_coverage_one(zond, sfp, range, &missing, &total, &error)) {
            g_warning("check_coverage: %s", error ? error->message : "?");
            g_clear_error(&error);
            continue;
        }

        if (missing > 0) {
            SondIndexCoverageGap *gap = g_new0(SondIndexCoverageGap, 1);
            gap->sfp          = sfp;
            gap->range        = range;
            gap->display_name = sond_file_part_get_filepart(sfp);
            gap->missing      = missing;
            gap->total        = total;
            g_ptr_array_add(gaps, gap);
        }
    }

    return gaps;
}

static gchar*
format_gap_line(SondIndexCoverageGap *gap) {
    return (gap->total == 1)
            ? g_strdup_printf("%s (nicht indiziert)", gap->display_name)
            : g_strdup_printf("%s (%d/%d Seiten fehlen)",
                    gap->display_name, gap->missing, gap->total);
}

#define SOND_INDEXSUCHE_COVERAGE_SHOW_MAX 10

/* Zeigt die Lücken-Übersicht (mit "Aufklappen" für mehr als
 * SOND_INDEXSUCHE_COVERAGE_SHOW_MAX Einträge) und fragt nach, wie weiter
 * verfahren werden soll.
 *
 * Returns: GTK_RESPONSE_YES (jetzt nachindizieren), GTK_RESPONSE_NO
 *          (trotzdem suchen) oder GTK_RESPONSE_CANCEL/DELETE_EVENT (ganz
 *          abbrechen).
 */
static gint
ask_coverage_gaps(Projekt *zond, GPtrArray *gaps, guint n_total) {
    GtkWidget *dialog  = NULL;
    GtkWidget *content = NULL;
    GtkWidget *box     = NULL;
    GtkWidget *label   = NULL;
    gchar     *summary = NULL;
    gint       response = 0;
    guint      shown    = MIN(gaps->len, SOND_INDEXSUCHE_COVERAGE_SHOW_MAX);

    dialog = gtk_dialog_new_with_buttons(
            "Auswahl nicht vollständig indiziert",
            GTK_WINDOW(zond->app_window),
            GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
            "Jetzt nachindizieren", GTK_RESPONSE_YES,
            "Trotzdem suchen",      GTK_RESPONSE_NO,
            "Abbrechen",            GTK_RESPONSE_CANCEL,
            NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_YES);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 480, -1);

    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_container_add(GTK_CONTAINER(content), box);

    summary = g_strdup_printf(
            "%u von %u ausgewählten Punkten sind nicht vollständig indiziert:",
            gaps->len, n_total);
    label = gtk_label_new(summary);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    g_free(summary);

    for (guint i = 0; i < shown; i++) {
        SondIndexCoverageGap *gap = g_ptr_array_index(gaps, i);
        gchar *line = format_gap_line(gap);
        GtkWidget *l = gtk_label_new(line);
        gtk_widget_set_halign(l, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(box), l, FALSE, FALSE, 0);
        g_free(line);
    }

    if (gaps->len > shown) {
        gchar *more_label = g_strdup_printf("... und %u weitere anzeigen",
                gaps->len - shown);
        GtkWidget *expander = gtk_expander_new(more_label);
        g_free(more_label);

        GtkWidget *inner_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
        gtk_container_set_border_width(GTK_CONTAINER(inner_box), 6);
        for (guint i = shown; i < gaps->len; i++) {
            SondIndexCoverageGap *gap = g_ptr_array_index(gaps, i);
            gchar *line = format_gap_line(gap);
            GtkWidget *l = gtk_label_new(line);
            gtk_widget_set_halign(l, GTK_ALIGN_START);
            gtk_box_pack_start(GTK_BOX(inner_box), l, FALSE, FALSE, 0);
            g_free(line);
        }
        gtk_container_add(GTK_CONTAINER(expander), inner_box);
        gtk_box_pack_start(GTK_BOX(box), expander, FALSE, FALSE, 0);
    }

    gtk_widget_show_all(dialog);
    response = my_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    return response;
}

/* -------------------------------------------------------------------------
 * Öffentliche Funktion: Dialog + Ergebnisanzeige
 * ---------------------------------------------------------------------- */

/* ht_filter: schränkt die Treffer auf diese Punkte ein (NULL = keine
 * Einschränkung, ganzer Index). ht_coverage: worüber der Abdeckungs-Check
 * (schon indiziert? fehlt etwas?) läuft - für "Ausgewählte Punkte" dieselbe
 * Map wie ht_filter, für "Gesamtes Projektverzeichnis" eine eigene, vom
 * Aufrufer erzeugte Map über alle Dateien (ht_filter bleibt dort NULL,
 * sonst würde jeder Treffer unnötig gegen die komplette Dateiliste
 * geprüft). NULL = kein Abdeckungs-Check. */
static void
zond_indexsuche_do(Projekt *zond, GHashTable* ht_filter, GHashTable *ht_coverage) {
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

    if (ht_coverage) {
        GPtrArray *gaps = check_coverage(zond, ht_coverage);
        if (gaps->len > 0) {
            gint resp = ask_coverage_gaps(zond, gaps, g_hash_table_size(ht_coverage));
            if (resp == GTK_RESPONSE_CANCEL) {
                g_ptr_array_unref(gaps);
                return;
            }
            if (resp == GTK_RESPONSE_YES) {
                GHashTable *ht_reindex = g_hash_table_new_full(NULL, NULL,
                        g_object_unref, sond_page_range_free);
                for (guint i = 0; i < gaps->len; i++) {
                    SondIndexCoverageGap *gap = g_ptr_array_index(gaps, i);
                    SondPageRange *range_copy = gap->range
                            ? sond_page_range_new(gap->range->von, gap->range->bis)
                            : NULL;
                    g_hash_table_insert(ht_reindex, g_object_ref(gap->sfp), range_copy);
                }
                zond_index_erstellen_ht(zond, ht_reindex);
            }
            /* GTK_RESPONSE_NO: einfach weiter mit der Suche, ohne nachzuindizieren. */
        }
        g_ptr_array_unref(gaps);
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

    response = my_dialog_run(GTK_DIALOG(dialog));

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

            /* Treffer auf Auswahl filtern wenn gewünscht.
             * ht_filter ist eine Map SondFilePart* -> SondPageRange*
             * (siehe sond_treeviewfm_get_fileparts()/zond_treeview_get_
             * selected_fileparts()) - direkt vom Aufrufer übergeben, kein
             * erneutes Auslesen einer Treeview-Selektion nötig (das war
             * vorher kaputt: stvfm_filter war nie zugewiesen/immer NULL,
             * daher lief dieser Zweig faktisch nie).
             * Ein NULL-Wert bedeutet "ganze Datei"; ein SondPageRange*
             * beschränkt den Treffer zusätzlich auf dessen Seitenbereich
             * (die Anbindung des ausgewählten Punkts). */
            if (ht_filter && hits->len > 0) {
                GPtrArray *filtered = g_ptr_array_new_with_free_func(
                        sond_index_hit_free);

                for (guint i = 0; i < hits->len; i++) {
                    SondIndexHit *hit = g_ptr_array_index(hits, i);
                    gboolean keep = FALSE;

                    GHashTableIter iter_sel;
                    gpointer key = NULL;
                    gpointer value = NULL;
                    g_hash_table_iter_init(&iter_sel, ht_filter);
                    while (g_hash_table_iter_next(&iter_sel, &key, &value)) {
                        SondFilePart *sfp_sel = (SondFilePart*) key;
                        SondPageRange *range = (SondPageRange*) value;
                        gchar *fp = sond_file_part_get_filepart(sfp_sel);

                        /* Treffer gehört zu fp selbst oder zu einem darin
                         * enthaltenen Unterpfad (Konvention '//' als
                         * Trenner, wie in sond_index_ctx_clear_file). Ein
                         * reiner Prefix-Vergleich würde z.B. "a/b" auch
                         * für "a/bc" fälschlich matchen. */
                        if (fp) {
                            gsize fp_len = strlen(fp);
                            if (g_str_has_prefix(hit->filename, fp) &&
                                    (hit->filename[fp_len] == '\0' ||
                                     (hit->filename[fp_len] == '/' &&
                                      hit->filename[fp_len + 1] == '/'))) {
                                /* Datei passt - bei Anbindung (range) auf
                                 * deren Seitenbereich einschränken (nur
                                 * ganze Seiten, s. Absprache). Ohne range:
                                 * ganze Datei, jede Seite zählt. */
                                if (!range || (hit->page_nr >= range->von &&
                                        hit->page_nr <= range->bis))
                                    keep = TRUE;
                            }
                        }
                        g_free(fp);
                        if (keep) break;
                    }

                    if (keep) {
                        g_ptr_array_add(filtered, hit);
                        /* Eigentumsübertragung: aus hits entfernen ohne free */
                        g_ptr_array_index(hits, i) = NULL;
                    }
                }

                g_ptr_array_unref(hits);
                hits = filtered;
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
             * Spalte 1: Seite (1-basiert, zum Anzeigezeitpunkt live umgerechnet)
             * Spalte 2: Fundstelle (snippet)
             * Spalte 3: char_pos_in_page (versteckt)
             * Spalte 4: rohe, unübersetzte Seite aus dem Index (versteckt) -
             *           wird beim Klick erneut gegen den dann aktuellen
             *           Live-Stand übersetzt (siehe zond_indexsuche_row_
             *           activated), falls zwischen Aufbau der Liste und
             *           Klick weitere Seiten eingefügt/gelöscht wurden.
             */
            gchar const *cols[] = { "Datei", "Seite", "Fundstelle", "", "", NULL };
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
                gint page_nr_akt = hit->page_nr;

                /* Ist die Datei schon offen und liegen dort anhängige
                 * (noch nicht gespeicherte) Seiten einfügen/löschen vor,
                 * dann bezieht sich hit->page_nr (Stand: letzte Indizierung)
                 * ggf. auf eine andere Seite als im aktuellen Live-Zustand.
                 * Über eine Ganze-Seite-Anbindung {page_nr,0}-{page_nr,EOP}
                 * schon hier - vor dem Speichern - auf den aktuellen Stand
                 * übersetzen, damit die angezeigte Seitenzahl stimmt. */
                if (page_nr_akt >= 0) {
                    ZondPdfDocument *zpdfd_open =
                            zond_pdf_document_find_by_filename(hit->filename);

                    if (zpdfd_open) {
                        Anbindung anbindung = { { page_nr_akt, 0 },
                                { page_nr_akt, EOP } };

                        anbindung_aktualisieren(zpdfd_open, &anbindung);
                        page_nr_akt = anbindung.von.seite;
                    }
                }

                gchar *page_str = (page_nr_akt >= 0)
                        ? g_strdup_printf("%d", page_nr_akt + 1)
                        : g_strdup("");
                gchar *char_pos_str = g_strdup_printf("%d",
                        hit->char_pos_in_page);
                gchar *page_raw_str = g_strdup_printf("%d", hit->page_nr);
                gchar const *row[] = {
                        hit->filename,
                        page_str,
                        hit->snippet ? hit->snippet : "",
                        char_pos_str,
                        page_raw_str,
                        NULL
                };
                sond_result_view_append(rv, row);
                g_free(page_str);
                g_free(char_pos_str);
                g_free(page_raw_str);
            }

            g_ptr_array_unref(hits);
            gtk_widget_show_all(rv);
        }
    }

    gtk_widget_destroy(dialog);
}

void
zond_indexsuche_activate(GtkMenuItem *item, gpointer data) {
    Projekt *zond = (Projekt*) data;
    GHashTable *ht_coverage = NULL;
    GError *error = NULL;

    /* "Gesamtes Projektverzeichnis": keine Auswahl zum Filtern (ht_filter
     * bleibt NULL, wie bisher), aber der Abdeckungs-Check (schon
     * indiziert?) soll trotzdem laufen - dafür eine eigene Map über alle
     * Dateien im Projekt aufbauen, unabhängig vom (NULL) Filter. Schlägt
     * das Aufbauen fehl, wird die Suche trotzdem ausgeführt, nur eben ohne
     * Abdeckungs-Check (kein Grund, die Suche deswegen zu blockieren). */
    ht_coverage = zond_treeviewfm_get_fileparts(
            ZOND_TREEVIEWFM(zond->treeview[BAUM_FS]), FALSE, &error);
    if (!ht_coverage)
        g_clear_error(&error);

    zond_indexsuche_do(zond, NULL, ht_coverage);

    if (ht_coverage)
        g_hash_table_destroy(ht_coverage);
}

void
zond_indexsuche_activate_with_selection(GtkMenuItem *item,
		GHashTable* ht_fileparts, gpointer data) {
    zond_indexsuche_do((Projekt*) data, ht_fileparts, ht_fileparts);
}

/* Gemeinsame Logik fuer "Indexsuche in Auswahl", aufgerufen aus den
 * Kontextmenues aller drei Baeume (dort ist "baum" instanzgebunden bekannt,
 * ueber zond->baum_active - zuverlaessig, da bei Rechtsklick synchron per
 * focus-in gesetzt) sowie aus dem globalen Fenstermenue (dort wird "baum"
 * vorher per zond_baum_mit_auswahl() ermittelt, weil dort kein fester
 * Baum-Kontext existiert). baum == KEIN_BAUM zeigt "Keine Punkte
 * ausgewaehlt" an. */
void
zond_indexsuche_activate_fuer_baum(Projekt *zond, Baum baum) {
    GError *error = NULL;
    GHashTable *ht_fileparts = NULL;

    if (baum == KEIN_BAUM) {
        display_message(zond->app_window, "Keine Punkte ausgewählt", NULL);
        return;
    }

    if (baum == BAUM_FS)
        ht_fileparts = zond_treeviewfm_get_fileparts(
                ZOND_TREEVIEWFM(zond->treeview[BAUM_FS]), TRUE, &error);
    else
        ht_fileparts = zond_treeview_get_selected_fileparts(
                ZOND_TREEVIEW(zond->treeview[baum]), &error);

    if (!ht_fileparts) {
        display_message(zond->app_window, "Fehler beim Ermitteln der Auswahl:\n",
                error ? error->message : "?", NULL);
        g_clear_error(&error);
        return;
    }
    if (g_hash_table_size(ht_fileparts) == 0) {
        display_message(zond->app_window, "Keine Punkte ausgewählt", NULL);
        g_hash_table_destroy(ht_fileparts);
        return;
    }

    zond_indexsuche_activate_with_selection(NULL, ht_fileparts, zond);
    g_hash_table_destroy(ht_fileparts);
}
