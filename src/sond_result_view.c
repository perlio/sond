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
#include <stdlib.h>
#include <string.h>

/* Schlüssel für g_object_set_data */
#define KEY_TREEVIEW     "sond-rv-treeview"
#define KEY_LISTSTORE    "sond-rv-liststore"
#define KEY_SORTMODEL    "sond-rv-sortmodel"
#define KEY_NUM_COLS     "sond-rv-num-cols"
#define KEY_FILTER_DATA  "sond-rv-filter-data"
#define KEY_COUNT_LABEL  "sond-rv-count-label"
#define KEY_DETAIL_VIEW  "sond-rv-detail-view"
#define KEY_DETAIL_COL   "sond-rv-detail-col"

typedef struct {
    gchar    *needle;      /* aktueller Filtertext, kleingeschrieben, ggf. NULL/leer */
    gint      num_cols;
    gboolean *searchable;  /* pro Spalte: wird beim Filtern durchsucht? */
} FilterData;

static void filter_data_free(gpointer p) {
    FilterData *fd = (FilterData*) p;

    if (!fd)
        return;

    g_free(fd->needle);
    g_free(fd->searchable);
    g_free(fd);
}

/* ==========================================================================
 * Filtern
 * ======================================================================== */

static void update_count_label(GtkWidget *result_view) {
    GtkWidget *label = NULL;
    GtkListStore *store = NULL;
    GtkTreeModel *sort_model = NULL;
    gint total = 0;
    gint shown = 0;
    gchar *text = NULL;

    label = GTK_WIDGET(g_object_get_data(G_OBJECT(result_view), KEY_COUNT_LABEL));
    store = GTK_LIST_STORE(g_object_get_data(G_OBJECT(result_view), KEY_LISTSTORE));
    sort_model = GTK_TREE_MODEL(g_object_get_data(G_OBJECT(result_view), KEY_SORTMODEL));

    if (!label || !store || !sort_model)
        return;

    total = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(store), NULL);
    shown = gtk_tree_model_iter_n_children(sort_model, NULL);

    if (shown == total)
        text = g_strdup_printf(total == 1 ? "%d Treffer" : "%d Treffer", total);
    else
        text = g_strdup_printf("%d von %d Treffern", shown, total);

    gtk_label_set_text(GTK_LABEL(label), text);
    g_free(text);
}

static gboolean row_visible_func(GtkTreeModel *model, GtkTreeIter *iter,
        gpointer data) {
    FilterData *fd = (FilterData*) data;

    if (!fd->needle || !*fd->needle)
        return TRUE;

    for (gint i = 0; i < fd->num_cols; i++) {
        gchar *val = NULL;
        gchar *val_lower = NULL;
        gboolean match = FALSE;

        if (!fd->searchable[i])
            continue;

        gtk_tree_model_get(model, iter, i, &val, -1);
        if (!val)
            continue;

        val_lower = g_utf8_strdown(val, -1);
        match = (strstr(val_lower, fd->needle) != NULL);
        g_free(val_lower);
        g_free(val);

        if (match)
            return TRUE;
    }

    return FALSE;
}

static void cb_search_changed(GtkEditable *editable, gpointer user_data) {
    GtkWidget *result_view = GTK_WIDGET(user_data);
    FilterData *fd = NULL;
    GtkTreeModelFilter *filter = NULL;
    gchar *text = NULL;

    fd = (FilterData*) g_object_get_data(G_OBJECT(result_view), KEY_FILTER_DATA);
    if (!fd)
        return;

    text = gtk_editable_get_chars(editable, 0, -1);
    g_free(fd->needle);
    fd->needle = g_utf8_strdown(text ? text : "", -1);
    g_free(text);

    /* Filter hängt am GtkTreeModelSort - dessen Kindmodell ist der Filter */
    filter = GTK_TREE_MODEL_FILTER(
            gtk_tree_model_sort_get_model(
                    GTK_TREE_MODEL_SORT(g_object_get_data(
                            G_OBJECT(result_view), KEY_SORTMODEL))));
    gtk_tree_model_filter_refilter(filter);

    update_count_label(result_view);
}

/* ==========================================================================
 * Sortieren - Zahlen als Zahlen vergleichen ("Seite" 2 < 10, nicht "10" < "2")
 * ======================================================================== */

static gint compare_rows(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b,
        gpointer data) {
    gint col = GPOINTER_TO_INT(data);
    gchar *sa = NULL;
    gchar *sb = NULL;
    gint result = 0;

    gtk_tree_model_get(model, a, col, &sa, -1);
    gtk_tree_model_get(model, b, col, &sb, -1);

    if (sa && sb && *sa && *sb) {
        gchar *enda = NULL;
        gchar *endb = NULL;
        glong na = strtol(sa, &enda, 10);
        glong nb = strtol(sb, &endb, 10);

        if (*enda == '\0' && *endb == '\0')
            result = (na < nb) ? -1 : (na > nb) ? 1 : 0;
        else
            result = g_utf8_collate(sa, sb);
    } else
        result = g_strcmp0(sa, sb);

    g_free(sa);
    g_free(sb);

    return result;
}

/* ==========================================================================
 * Zeile kopieren (Kontextmenü / Strg+C)
 * ======================================================================== */

static void copy_selected_rows(GtkWidget *result_view) {
    GtkWidget *treeview = NULL;
    GtkTreeSelection *sel = NULL;
    GtkTreeModel *model = NULL;
    GList *rows = NULL;
    gint num_cols = 0;
    GString *text = NULL;

    treeview = sond_result_view_get_treeview(result_view);
    if (!treeview)
        return;

    num_cols = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(result_view), KEY_NUM_COLS));
    sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    rows = gtk_tree_selection_get_selected_rows(sel, &model);
    if (!rows)
        return;

    text = g_string_new(NULL);

    for (GList *l = rows; l; l = l->next) {
        GtkTreeIter iter = { 0 };

        if (gtk_tree_model_get_iter(model, &iter, (GtkTreePath*) l->data)) {
            for (gint i = 0; i < num_cols; i++) {
                gchar *val = NULL;

                gtk_tree_model_get(model, &iter, i, &val, -1);
                if (i > 0)
                    g_string_append_c(text, '\t');
                g_string_append(text, val ? val : "");
                g_free(val);
            }
            g_string_append_c(text, '\n');
        }
    }

    g_list_free_full(rows, (GDestroyNotify) gtk_tree_path_free);

    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
            text->str, (gint) text->len);
    g_string_free(text, TRUE);
}

static void cb_menu_copy_row(GtkMenuItem *item, gpointer user_data) {
    copy_selected_rows(GTK_WIDGET(user_data));
}

static gboolean cb_treeview_button_press(GtkWidget *treeview,
        GdkEventButton *event, gpointer user_data) {
    GtkWidget *result_view = GTK_WIDGET(user_data);
    GtkWidget *menu = NULL;
    GtkWidget *item_copy = NULL;
    GtkTreePath *path = NULL;

    if (event->type != GDK_BUTTON_PRESS || event->button != GDK_BUTTON_SECONDARY)
        return FALSE;

    /* Rechtsklick auf eine (noch) nicht selektierte Zeile: diese selektieren */
    if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(treeview),
            (gint) event->x, (gint) event->y, &path, NULL, NULL, NULL)) {
        GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
        if (!gtk_tree_selection_path_is_selected(sel, path)) {
            gtk_tree_selection_unselect_all(sel);
            gtk_tree_selection_select_path(sel, path);
        }
        gtk_tree_path_free(path);
    } else
        return FALSE; /* Klick außerhalb aller Zeilen */

    menu = gtk_menu_new();
    item_copy = gtk_menu_item_new_with_label("Zeile(n) kopieren");
    g_signal_connect(item_copy, "activate", G_CALLBACK(cb_menu_copy_row), result_view);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_copy);
    gtk_widget_show_all(menu);

#if GTK_CHECK_VERSION(3, 22, 0)
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*) event);
#else
    gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
            event->button, event->time);
#endif

    return TRUE;
}

static gboolean cb_treeview_key_press(GtkWidget *treeview, GdkEventKey *event,
        gpointer user_data) {
    if ((event->state & GDK_CONTROL_MASK)
            && (event->keyval == GDK_KEY_c || event->keyval == GDK_KEY_C)) {
        copy_selected_rows(GTK_WIDGET(user_data));
        return TRUE;
    }

    return FALSE;
}

/* ==========================================================================
 * Fundstelle-Kürzung: per Hand geschnitten und umgebrochen statt über
 * Zellrenderer-Eigenschaften (wrap-mode/lines/height) - die hatten sich in
 * der Praxis als unzuverlässig erwiesen. Der eingebettete Zeilenumbruch im
 * "text" wird von Pango zuverlässig als echter Zeilenumbruch gerendert.
 * ======================================================================== */

/* Fundstelle-Vorschau: fest auf RV_SNIPPET_PREVIEW_MAXCHARS Zeichen
 * gekürzt und auf genau RV_SNIPPET_PREVIEW_LINES Zeilen aufgeteilt (Bruch
 * am nächsten Leerzeichen zur Mitte) - für alle Treffer gleich hoch. Der
 * volle Text steht im Detailbereich (Master-Detail, s.u.). */
#define RV_SNIPPET_PREVIEW_MAXCHARS 200
#define RV_SNIPPET_PREVIEW_LINES    2
#define RV_SNIPPET_PREVIEW_LEAD     40  /* Zeichen vor dem Treffer, s.u. */

#define KEY_SEARCH_TERM  "index-search-term" /* von außen gesetzt, s.
                                                 zond_indexsuche.c/zond_chat.c */

/* Case-insensitive Vergleich ab genau dieser Position (UTF-8-bewusst,
 * ohne den Text komplett kleinzuschreiben - vermeidet Byte-/Zeichenlängen-
 * Verschiebungen durch g_utf8_strdown() bei manchen Zeichen). */
static gboolean utf8_ci_match_at(gchar const *haystack, gchar const *needle) {
    gchar const *h = haystack;
    gchar const *n = needle;

    while (*n) {
        if (!*h)
            return FALSE;
        if (g_unichar_tolower(g_utf8_get_char(h))
                != g_unichar_tolower(g_utf8_get_char(n)))
            return FALSE;
        h = g_utf8_next_char(h);
        n = g_utf8_next_char(n);
    }

    return TRUE;
}

static gchar* rv_split_lines(gchar const *s, gint n_lines) {
    GString *out = NULL;
    glong    len = g_utf8_strlen(s, -1);

    if (n_lines < 2 || len == 0)
        return g_strdup(s);

    out = g_string_new(NULL);

    gchar const *rest      = s;
    glong        rest_len  = len;
    for (gint line = 0; line < n_lines - 1 && rest_len > 0; line++) {
        glong        target   = rest_len / (n_lines - line);
        gchar const *mid_ptr  = g_utf8_offset_to_pointer(rest, target);
        gchar const *brk      = NULL;

        for (gchar const *p = mid_ptr; *p; p = g_utf8_next_char(p))
            if (*p == ' ') { brk = p; break; }
        if (!brk)
            for (gchar const *p = mid_ptr; p > rest; p = g_utf8_prev_char(p))
                if (*p == ' ') { brk = p; break; }

        if (!brk)
            break; /* kein Leerzeichen mehr gefunden - Rest in einer Zeile */

        g_string_append_len(out, rest, brk - rest);
        g_string_append_c(out, '\n');
        rest     = brk + 1;
        rest_len = g_utf8_strlen(rest, -1);
    }
    g_string_append(out, rest);

    return g_string_free(out, FALSE);
}

static void cb_snippet_cell_data(GtkTreeViewColumn *col, GtkCellRenderer *renderer,
        GtkTreeModel *model, GtkTreeIter *iter, gpointer user_data) {
    gint         col_idx  = GPOINTER_TO_INT(user_data);
    gchar       *text     = NULL;
    gchar       *cut      = NULL;
    gchar       *split    = NULL;
    GtkWidget   *treeview = NULL;
    GtkWidget   *toplevel = NULL;
    gchar const *term     = NULL;
    gchar const *start_ptr = NULL;
    gboolean     lead_ellipsis  = FALSE;
    gboolean     trail_ellipsis = FALSE;

    gtk_tree_model_get(model, iter, col_idx, &text, -1);

    if (!text) {
        g_object_set(renderer, "text", "", NULL);
        return;
    }

    /* Vorschau am tatsächlichen Treffer ausrichten statt immer am
     * Textanfang: seit der Kontext vor dem Treffer (Suche über die
     * chunks-Tabelle) großzügiger bemessen ist, würde eine feste
     * Kürzung ab Zeichen 0 sonst oft nur Text VOR dem Treffer zeigen,
     * ohne das Suchwort selbst - der eigentliche Treffer stünde dann
     * erst im (weiter unten markierten) Detailbereich. */
    treeview = gtk_tree_view_column_get_tree_view(col);
    toplevel = treeview ? gtk_widget_get_toplevel(treeview) : NULL;
    term = (toplevel && GTK_IS_WINDOW(toplevel))
            ? (gchar const*) g_object_get_data(G_OBJECT(toplevel), KEY_SEARCH_TERM)
            : NULL;

    start_ptr = text;

    if (term && *term) {
        gchar const *match = NULL;

        for (gchar const *p = text; *p; p = g_utf8_next_char(p))
            if (utf8_ci_match_at(p, term)) { match = p; break; }

        if (match) {
            gchar const *lead = match;
            for (gint k = 0; k < RV_SNIPPET_PREVIEW_LEAD && lead > text; k++)
                lead = g_utf8_prev_char(lead);
            start_ptr = lead;
        }
    }

    lead_ellipsis = (start_ptr > text);

    {
        gchar const *end_ptr = start_ptr;
        for (gint k = 0; k < RV_SNIPPET_PREVIEW_MAXCHARS && *end_ptr; k++)
            end_ptr = g_utf8_next_char(end_ptr);
        trail_ellipsis = (*end_ptr != '\0');

        cut = g_strdup_printf("%s%.*s%s",
                lead_ellipsis  ? "…" : "",
                (gint) (end_ptr - start_ptr), start_ptr,
                trail_ellipsis ? "…" : "");
    }

    split = rv_split_lines(cut, RV_SNIPPET_PREVIEW_LINES);

    g_object_set(renderer, "text", split, NULL);

    g_free(split);
    g_free(cut);
    g_free(text);
}

/* ==========================================================================
 * Master-Detail: volle Zeile der Auswahl im Detailbereich anzeigen
 * ======================================================================== */

#define RV_HIGHLIGHT_TAG "sond-rv-highlight"

/* Alle Vorkommen von term im aktuellen Buffer-Text farblich markieren -
 * sonst findet man den eigentlichen Treffer im (jetzt oft längeren)
 * Detailtext kaum wieder. Kein Treffer bei semantischer Suche (Chat:
 * KEY_SEARCH_TERM bleibt dort ungesetzt) - dann einfach nichts zu tun. */
static void rv_highlight_term(GtkTextBuffer *buffer, gchar const *term) {
    GtkTextTagTable *tag_table  = NULL;
    GtkTextTag      *tag        = NULL;
    GtkTextIter      start_iter = { 0 };
    GtkTextIter      end_iter   = { 0 };
    gchar           *text       = NULL;
    glong            term_len   = 0;
    gint             char_pos   = 0;

    if (!term || !*term)
        return;

    term_len = g_utf8_strlen(term, -1);
    if (term_len == 0)
        return;

    tag_table = gtk_text_buffer_get_tag_table(buffer);
    tag = gtk_text_tag_table_lookup(tag_table, RV_HIGHLIGHT_TAG);
    if (!tag)
        tag = gtk_text_buffer_create_tag(buffer, RV_HIGHLIGHT_TAG,
                "background", "#fff59d", NULL); /* helles Gelb */

    gtk_text_buffer_get_start_iter(buffer, &start_iter);
    gtk_text_buffer_get_end_iter(buffer, &end_iter);
    text = gtk_text_buffer_get_text(buffer, &start_iter, &end_iter, FALSE);
    if (!text)
        return;

    for (gchar const *p = text; *p; p = g_utf8_next_char(p), char_pos++) {
        if (utf8_ci_match_at(p, term)) {
            GtkTextIter it_start = { 0 };
            GtkTextIter it_end   = { 0 };

            gtk_text_buffer_get_iter_at_offset(buffer, &it_start, char_pos);
            gtk_text_buffer_get_iter_at_offset(buffer, &it_end,
                    char_pos + (gint) term_len);
            gtk_text_buffer_apply_tag(buffer, tag, &it_start, &it_end);
        }
    }

    g_free(text);
}

static void cb_selection_changed(GtkTreeSelection *sel, gpointer user_data) {
    GtkWidget     *result_view = GTK_WIDGET(user_data);
    GtkWidget     *detail_view = NULL;
    GtkTextBuffer *buffer      = NULL;
    GtkTreeModel  *model       = NULL;
    GList         *rows        = NULL;
    gint           detail_col  = 0;
    gchar         *text        = NULL;

    detail_view = GTK_WIDGET(g_object_get_data(G_OBJECT(result_view), KEY_DETAIL_VIEW));
    if (!detail_view)
        return;

    detail_col = GPOINTER_TO_INT(
            g_object_get_data(G_OBJECT(result_view), KEY_DETAIL_COL));
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(detail_view));

    rows = gtk_tree_selection_get_selected_rows(sel, &model);
    if (rows) {
        GtkTreeIter iter = { 0 };

        if (gtk_tree_model_get_iter(model, &iter, (GtkTreePath*) rows->data))
            gtk_tree_model_get(model, &iter, detail_col, &text, -1);

        g_list_free_full(rows, (GDestroyNotify) gtk_tree_path_free);
    }

    gtk_text_buffer_set_text(buffer, text ? text : "", -1);
    g_free(text);

    rv_highlight_term(buffer,
            (gchar const*) g_object_get_data(G_OBJECT(result_view), KEY_SEARCH_TERM));
}

/* ==========================================================================
 * Fenstergeometrie (Größe, Position, Aufteilung Liste/Detail) über
 * Sitzungen hinweg merken - in einer eigenen kleinen Konfigurationsdatei,
 * unabhängig vom GSettings-Schema der Anwendung, damit dieses generische,
 * mehrfach genutzte Widget keine Projekt-/Settings-Referenz benötigt.
 * ======================================================================== */

#define RV_CONFIG_GROUP   "window"
#define RV_DEFAULT_WIDTH  1400
#define RV_DEFAULT_HEIGHT 800

static gchar* rv_config_path(void) {
    return g_build_filename(g_get_user_config_dir(), "zond", "result_view.conf", NULL);
}

static void rv_load_geometry(gint *width, gint *height, gint *x, gint *y,
        gint *paned_pos) {
    GKeyFile *kf   = NULL;
    gchar    *path = NULL;
    gint      v    = 0;
    GError   *err  = NULL;

    *width = RV_DEFAULT_WIDTH;
    *height = RV_DEFAULT_HEIGHT;
    *x = -1;
    *y = -1;
    *paned_pos = -1;

    path = rv_config_path();
    kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        v = g_key_file_get_integer(kf, RV_CONFIG_GROUP, "width", NULL);
        if (v > 100)
            *width = v;

        v = g_key_file_get_integer(kf, RV_CONFIG_GROUP, "height", NULL);
        if (v > 100)
            *height = v;

        v = g_key_file_get_integer(kf, RV_CONFIG_GROUP, "x", &err);
        if (!err)
            *x = v;
        g_clear_error(&err);

        v = g_key_file_get_integer(kf, RV_CONFIG_GROUP, "y", &err);
        if (!err)
            *y = v;
        g_clear_error(&err);

        v = g_key_file_get_integer(kf, RV_CONFIG_GROUP, "paned-position", NULL);
        if (v > 0)
            *paned_pos = v;
    }

    g_key_file_free(kf);
    g_free(path);
}

static void rv_save_geometry(gint width, gint height, gint x, gint y,
        gint paned_pos) {
    GKeyFile *kf   = NULL;
    gchar    *path = NULL;
    gchar    *dir  = NULL;

    path = rv_config_path();
    dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);

    kf = g_key_file_new();
    /* bestehende Datei erst laden, damit wir nichts Unbekanntes verwerfen */
    g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL);

    g_key_file_set_integer(kf, RV_CONFIG_GROUP, "width", width);
    g_key_file_set_integer(kf, RV_CONFIG_GROUP, "height", height);
    g_key_file_set_integer(kf, RV_CONFIG_GROUP, "x", x);
    g_key_file_set_integer(kf, RV_CONFIG_GROUP, "y", y);
    g_key_file_set_integer(kf, RV_CONFIG_GROUP, "paned-position", paned_pos);

    g_key_file_save_to_file(kf, path, NULL);

    g_key_file_free(kf);
    g_free(path);
}

static gboolean cb_window_delete(GtkWidget *window, GdkEvent *event,
        gpointer user_data) {
    GtkWidget *paned  = GTK_WIDGET(user_data);
    gint       width  = 0;
    gint       height = 0;
    gint       x      = 0;
    gint       y      = 0;

    gtk_window_get_size(GTK_WINDOW(window), &width, &height);
    gtk_window_get_position(GTK_WINDOW(window), &x, &y);
    rv_save_geometry(width, height, x, y, gtk_paned_get_position(GTK_PANED(paned)));

    return gtk_widget_hide_on_delete(window);
}

/* ==========================================================================
 * Öffentliche API
 * ======================================================================== */

GtkWidget* sond_result_view_new(GtkWindow       *parent,
                                 gchar const     *title,
                                 gchar const    **column_titles,
                                 GCallback        row_activated_cb,
                                 gpointer         cb_data) {
    GtkWidget    *window     = NULL;
    GtkWidget    *vbox       = NULL;
    GtkWidget    *toolbar    = NULL;
    GtkWidget    *footer     = NULL;
    GtkWidget    *search     = NULL;
    GtkWidget    *count_label = NULL;
    GtkWidget    *hint_label = NULL;
    GtkWidget    *paned      = NULL;
    GtkWidget    *swindow    = NULL;
    GtkWidget    *treeview   = NULL;
    GtkWidget    *detail_swindow = NULL;
    GtkWidget    *detail_view    = NULL;
    GtkListStore *store      = NULL;
    GtkTreeModel *filter     = NULL;
    GtkTreeModel *sort_model = NULL;
    GType        *col_types  = NULL;
    gint          num_cols   = 0;
    FilterData   *fd         = NULL;
    gint          geo_width  = 0;
    gint          geo_height = 0;
    gint          geo_x      = 0;
    gint          geo_y      = 0;
    gint          paned_pos  = 0;

    g_return_val_if_fail(column_titles != NULL, NULL);

    /* Anzahl Spalten zählen */
    while (column_titles[num_cols])
        num_cols++;

    g_return_val_if_fail(num_cols > 0, NULL);

    rv_load_geometry(&geo_width, &geo_height, &geo_x, &geo_y, &paned_pos);

    /* --- Fenster --- */
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), title ? title : "Ergebnisse");
    gtk_window_set_default_size(GTK_WINDOW(window), geo_width, geo_height);
    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(window), parent);
        gtk_window_set_destroy_with_parent(GTK_WINDOW(window), TRUE);
    }
    if (geo_x >= 0 && geo_y >= 0)
        gtk_window_move(GTK_WINDOW(window), geo_x, geo_y);
    else if (parent)
        gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER_ON_PARENT);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    /* --- Werkzeugleiste: Suchfeld + Trefferzähler --- */
    toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(toolbar), 6);

    search = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(search), "Ergebnisse filtern …");
    gtk_widget_set_hexpand(search, TRUE);
    gtk_box_pack_start(GTK_BOX(toolbar), search, TRUE, TRUE, 0);

    count_label = gtk_label_new("0 Treffer");
    gtk_style_context_add_class(gtk_widget_get_style_context(count_label), "dim-label");
    gtk_box_pack_start(GTK_BOX(toolbar), count_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);

    /* --- ListStore: alle Spalten G_TYPE_STRING --- */
    col_types = g_new(GType, num_cols);
    for (gint i = 0; i < num_cols; i++)
        col_types[i] = G_TYPE_STRING;

    store = gtk_list_store_newv(num_cols, col_types);
    g_free(col_types);

    /* --- Filter- und Sort-Modell darüberlegen --- */
    filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(store), NULL);

    fd = g_new0(FilterData, 1);
    fd->num_cols = num_cols;
    fd->searchable = g_new0(gboolean, num_cols);
    for (gint i = 0; i < num_cols; i++)
        fd->searchable[i] = (column_titles[i][0] != '\0');

    gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(filter),
            row_visible_func, fd, NULL);

    sort_model = gtk_tree_model_sort_new_with_model(filter);
    g_object_unref(filter);

    for (gint i = 0; i < num_cols; i++)
        gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(sort_model), i,
                compare_rows, GINT_TO_POINTER(i), NULL);

    /* --- TreeView --- */
    treeview = gtk_tree_view_new_with_model(sort_model);
    g_object_unref(store);      /* TreeModelFilter hält Referenz auf store */
    g_object_unref(sort_model); /* TreeView hält Referenz auf sort_model   */

    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeview), TRUE);
#if GTK_MAJOR_VERSION < 4
    /* seit 3.14 deprecated (Theme macht das i.d.R. selbst), aber weiterhin
     * die einfachste Möglichkeit für Zebrastreifen unabhängig vom Theme */
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(treeview), TRUE);
    G_GNUC_END_IGNORE_DEPRECATIONS
#endif
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
        g_object_set(renderer, "ypad", 4, "yalign", 0.0, NULL);
        col      = gtk_tree_view_column_new_with_attributes(
                       column_titles[i], renderer, "text", i, NULL);

        if (i == last_visible) {
            /* letzte sichtbare Spalte: schmaler, dafür zweizeilige
             * Vorschau (fest gekürzt und umgebrochen, s.o.) - über alle
             * Treffer hinweg gleich hoch. Voller Text steht im
             * Detailbereich unterhalb der Liste (Master-Detail, s.u.). */
            gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_FIXED);
            gtk_tree_view_column_set_fixed_width(col, 380);
            gtk_tree_view_column_set_resizable(col, TRUE);
            g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
            gtk_tree_view_column_set_cell_data_func(col, renderer,
                    cb_snippet_cell_data, GINT_TO_POINTER(i), NULL);
        } else {
            gtk_tree_view_column_set_resizable(col, TRUE);
            gtk_tree_view_column_set_min_width(col, 80);
        }

        gtk_tree_view_column_set_sort_column_id(col, i);
        gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), col);
    }

    if (row_activated_cb)
        g_signal_connect(treeview, "row-activated", row_activated_cb, cb_data);

    g_signal_connect(treeview, "button-press-event",
            G_CALLBACK(cb_treeview_button_press), window);
    g_signal_connect(treeview, "key-press-event",
            G_CALLBACK(cb_treeview_key_press), window);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview)),
            "changed", G_CALLBACK(cb_selection_changed), window);

    /* --- ScrolledWindow (Liste) --- */
    swindow = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(swindow),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(swindow), treeview);

    /* --- Detailbereich: voller Text der ausgewählten Zeile (Master-
     * Detail) - dadurch bleibt die Liste gleichmäßig hoch (einzeilig,
     * ellipsiert), und beliebig lange Treffer sind trotzdem vollständig
     * lesbar, sobald man eine Zeile anklickt. --- */
    detail_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(detail_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(detail_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(detail_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(detail_view), 8);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(detail_view), 8);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(detail_view), 6);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(detail_view), 6);

    detail_swindow = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(detail_swindow),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(detail_swindow),
            GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(detail_swindow), detail_view);

    paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_paned_pack1(GTK_PANED(paned), swindow, TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(paned), detail_swindow, TRUE, FALSE);

    if (paned_pos <= 0)
        paned_pos = (gint) (geo_height * 0.7);
    gtk_paned_set_position(GTK_PANED(paned), paned_pos);

    gtk_box_pack_start(GTK_BOX(vbox), paned, TRUE, TRUE, 0);

    g_signal_connect(window, "delete-event",
                     G_CALLBACK(cb_window_delete), paned);

    /* --- Fußzeile: Bedienhinweis --- */
    footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(footer), 4);
    hint_label = gtk_label_new(
            "Doppelklick oder Enter öffnet die Fundstelle · Rechtsklick zum Kopieren");
    gtk_style_context_add_class(gtk_widget_get_style_context(hint_label), "dim-label");
    gtk_label_set_xalign(GTK_LABEL(hint_label), 0.0);
    gtk_box_pack_start(GTK_BOX(footer), hint_label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), footer, FALSE, FALSE, 0);

    /* Metadaten am Fenster speichern */
    g_object_set_data(G_OBJECT(window), KEY_TREEVIEW,  treeview);
    g_object_set_data(G_OBJECT(window), KEY_LISTSTORE, store);
    g_object_set_data(G_OBJECT(window), KEY_SORTMODEL, sort_model);
    g_object_set_data(G_OBJECT(window), KEY_NUM_COLS,  GINT_TO_POINTER(num_cols));
    g_object_set_data(G_OBJECT(window), KEY_COUNT_LABEL, count_label);
    g_object_set_data(G_OBJECT(window), KEY_DETAIL_VIEW, detail_view);
    g_object_set_data(G_OBJECT(window), KEY_DETAIL_COL, GINT_TO_POINTER(last_visible));
    g_object_set_data_full(G_OBJECT(window), KEY_FILTER_DATA, fd, filter_data_free);

    /* "changed" (statt "search-changed") für sofortiges Filtern ohne Delay */
    g_signal_connect(search, "changed", G_CALLBACK(cb_search_changed), window);

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

    update_count_label(result_view);
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
