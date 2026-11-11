/*
 sond (sond_module_import.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_module_import.h"
#include "sond_module_akte.h"
#include "sond_import.h"
#include "../../sond_log_and_error.h"
#include "../../sond_graph/sond_graph_node.h"

#include <string.h>

typedef struct _ImportJob ImportJob;

typedef struct {
    SondClient *client;

    GtkWidget *main_box;
    GtkWidget *entry_regnr;
    GtkWidget *lbl_akte;
    GtkWidget *btn_folder;
    GtkWidget *btn_files;
    GtkWidget *btn_clear;
    GtkWidget *lbl_sources;
    GtkWidget *btn_import;
    GtkWidget *btn_cancel;
    GtkWidget *progress;
    GtkWidget *lbl_progress;
    GtkWidget *log_view;

    GPtrArray *sources;      /* gewählte Quellen (Pfade) */
    gchar *library_name;     /* geprüfte, aktive Akte; NULL = keine */
    ImportJob *job;          /* laufender Import oder NULL */
} ImportPriv;

/* Ein Importlauf. Gehört dem Worker-Thread, bis done_idle() ihn freigibt;
 * das Widget wird nur über die schwache Referenz angesprochen. */
struct _ImportJob {
    GWeakRef box;
    GPtrArray *sources;
    gchar *target;
    gint cancel;
    SondImportResult *result;
    GError *error;
};

typedef struct {
    ImportJob *job;
    guint files_done;
    guint files_total;
    gint64 bytes_done;
    gint64 bytes_total;
    gchar *current;
} ProgressMsg;

static void import_priv_free(gpointer data) {
    ImportPriv *priv = data;

    if (priv->job)
        g_atomic_int_set(&priv->job->cancel, 1);

    g_object_unref(priv->client);
    g_ptr_array_unref(priv->sources);
    g_free(priv->library_name);
    g_free(priv);
}

static ImportPriv* get_priv(GtkWidget *box) {
    return g_object_get_data(G_OBJECT(box), "sond-import-priv");
}

/* ========================================================================
 * Hilfsfunktionen
 * ======================================================================== */

static void show_error(GtkWidget *parent, const gchar *title,
        const gchar *message) {
    GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", title);

    gtk_alert_dialog_set_detail(dialog, message);
    gtk_alert_dialog_set_modal(dialog, TRUE);
    gtk_alert_dialog_show(dialog, GTK_WINDOW(gtk_widget_get_root(parent)));
    g_object_unref(dialog);
}

static void set_akte_label(ImportPriv *priv, const gchar *text, gboolean is_error) {
    gtk_label_set_text(GTK_LABEL(priv->lbl_akte), text);
    if (is_error)
        gtk_widget_add_css_class(priv->lbl_akte, "error");
    else
        gtk_widget_remove_css_class(priv->lbl_akte, "error");
}

static void update_sensitivity(ImportPriv *priv) {
    gboolean running = (priv->job != NULL);

    gtk_widget_set_sensitive(priv->entry_regnr, !running);
    gtk_widget_set_sensitive(priv->btn_folder, !running);
    gtk_widget_set_sensitive(priv->btn_files, !running);
    gtk_widget_set_sensitive(priv->btn_clear,
            !running && priv->sources->len > 0);
    gtk_widget_set_sensitive(priv->btn_import,
            !running && priv->library_name && priv->sources->len > 0);
    gtk_widget_set_visible(priv->btn_cancel, running);
    gtk_widget_set_sensitive(priv->btn_cancel, running);
}

static void update_sources_label(ImportPriv *priv) {
    GString *s = g_string_new(NULL);

    if (priv->sources->len == 0)
        g_string_append(s, "Keine Quellen gewählt.");
    else {
        g_string_append_printf(s, "%u Quelle(n):", priv->sources->len);
        for (guint i = 0; i < priv->sources->len; i++)
            g_string_append_printf(s, "\n%s",
                    (const gchar*) g_ptr_array_index(priv->sources, i));
    }

    gtk_label_set_text(GTK_LABEL(priv->lbl_sources), s->str);
    g_string_free(s, TRUE);
    update_sensitivity(priv);
}

static void add_source(ImportPriv *priv, const gchar *path) {
    if (!path)
        return;

    for (guint i = 0; i < priv->sources->len; i++)
        if (g_strcmp0(g_ptr_array_index(priv->sources, i), path) == 0)
            return;

    g_ptr_array_add(priv->sources, g_strdup(path));
}

static void set_log(ImportPriv *priv, const gchar *text) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(priv->log_view));

    gtk_text_buffer_set_text(buffer, text, -1);
}

/* ========================================================================
 * Akte prüfen
 * ======================================================================== */

static void check_akte(ImportPriv *priv) {
    const gchar *text = gtk_editable_get_text(GTK_EDITABLE(priv->entry_regnr));
    guint lfd_nr = 0;
    guint year = 0;
    GError *error = NULL;
    SondGraphNode *node = NULL;
    SondAkteStatus status = SOND_AKTE_STATUS_NICHT_ANGELEGT;

    g_clear_pointer(&priv->library_name, g_free);

    if (!sond_module_akte_parse_regnr(text, &lfd_nr, &year)) {
        set_akte_label(priv, "Bitte eine Registernummer eingeben (z. B. 23/26).",
                TRUE);
        update_sensitivity(priv);
        return;
    }

    node = sond_module_akte_find_by_regnr(priv->client, lfd_nr, year, &error);
    if (!node) {
        gchar *msg = NULL;

        if (error && error->code == G_IO_ERROR_NOT_FOUND)
            msg = g_strdup_printf("Akte %u/%02u nicht gefunden.", lfd_nr,
                    year % 100);
        else
            msg = g_strdup_printf("Fehler bei der Suche: %s",
                    error ? error->message : "unbekannt");

        set_akte_label(priv, msg, TRUE);
        g_free(msg);
        g_clear_error(&error);
        update_sensitivity(priv);
        return;
    }

    status = sond_module_akte_get_status(node);

    if (status != SOND_AKTE_STATUS_AKTIV) {
        gchar *msg = g_strdup_printf("Akte %u/%02u ist %s.", lfd_nr,
                year % 100, (status == SOND_AKTE_STATUS_ABGELEGT) ?
                        "abgelegt" : "noch nicht angelegt");

        set_akte_label(priv, msg, TRUE);
        g_free(msg);
    } else {
        gchar *kurzb = sond_graph_node_get_property_string(node, "kurzb");
        gchar *msg = g_strdup_printf("Akte %u/%02u: %s", lfd_nr, year % 100,
                kurzb ? kurzb : "");

        priv->library_name = sond_module_akte_library_name(lfd_nr, year);
        set_akte_label(priv, msg, FALSE);
        g_free(msg);
        g_free(kurzb);
    }

    g_object_unref(node);
    update_sensitivity(priv);
}

static void on_entry_activate(GtkEntry *entry, gpointer user_data) {
    check_akte(user_data);
}

/* Eingabe geändert: bisherige Prüfung gilt nicht mehr */
static void on_entry_changed(GtkEditable *editable, gpointer user_data) {
    ImportPriv *priv = user_data;

    g_clear_pointer(&priv->library_name, g_free);
    set_akte_label(priv, "", FALSE);
    update_sensitivity(priv);
}

/* Modul wird sichtbar: die im Aktenmodul angezeigte Akte nur als Vorschlag in
 * das Eingabefeld übernehmen; geprüft wird erst auf Enter oder "Akte prüfen". */
static void on_map(GtkWidget *widget, gpointer user_data) {
    ImportPriv *priv = user_data;
    guint lfd_nr = 0;
    guint year = 0;
    gchar *suggestion = NULL;

    if (priv->job)
        return;
    if (!sond_client_get_current_akte(priv->client, &lfd_nr, &year))
        return;

    suggestion = g_strdup_printf("%u/%02u", lfd_nr, year % 100);

    if (g_strcmp0(gtk_editable_get_text(GTK_EDITABLE(priv->entry_regnr)),
            suggestion) != 0)
        gtk_editable_set_text(GTK_EDITABLE(priv->entry_regnr), suggestion);

    g_free(suggestion);
}

/* ========================================================================
 * Quellen wählen
 * ======================================================================== */

static void on_folder_chosen(GObject *source, GAsyncResult *result,
        gpointer user_data) {
    GtkWidget *box = user_data;
    GListModel *folders = gtk_file_dialog_select_multiple_folders_finish(
            GTK_FILE_DIALOG(source), result, NULL);

    if (folders) {
        guint n = g_list_model_get_n_items(folders);

        for (guint i = 0; i < n; i++) {
            GFile *file = g_list_model_get_item(folders, i);
            gchar *path = g_file_get_path(file);

            add_source(get_priv(box), path);
            g_free(path);
            g_object_unref(file);
        }

        update_sources_label(get_priv(box));
        g_object_unref(folders);
    }

    g_object_unref(box);
}

static void on_files_chosen(GObject *source, GAsyncResult *result,
        gpointer user_data) {
    GtkWidget *box = user_data;
    GListModel *files = gtk_file_dialog_open_multiple_finish(
            GTK_FILE_DIALOG(source), result, NULL);

    if (files) {
        guint n = g_list_model_get_n_items(files);

        for (guint i = 0; i < n; i++) {
            GFile *file = g_list_model_get_item(files, i);
            gchar *path = g_file_get_path(file);

            add_source(get_priv(box), path);
            g_free(path);
            g_object_unref(file);
        }

        update_sources_label(get_priv(box));
        g_object_unref(files);
    }

    g_object_unref(box);
}

static void on_folder_clicked(GtkButton *button, gpointer user_data) {
    ImportPriv *priv = user_data;
    GtkFileDialog *dialog = gtk_file_dialog_new();

    gtk_file_dialog_set_title(dialog, "Ordner auswählen");
    gtk_file_dialog_select_multiple_folders(dialog,
            GTK_WINDOW(gtk_widget_get_root(priv->main_box)), NULL,
            on_folder_chosen, g_object_ref(priv->main_box));
    g_object_unref(dialog);
}

static void on_files_clicked(GtkButton *button, gpointer user_data) {
    ImportPriv *priv = user_data;
    GtkFileDialog *dialog = gtk_file_dialog_new();

    gtk_file_dialog_set_title(dialog, "Dateien auswählen");
    gtk_file_dialog_open_multiple(dialog,
            GTK_WINDOW(gtk_widget_get_root(priv->main_box)), NULL,
            on_files_chosen, g_object_ref(priv->main_box));
    g_object_unref(dialog);
}

static void on_clear_clicked(GtkButton *button, gpointer user_data) {
    ImportPriv *priv = user_data;

    g_ptr_array_set_size(priv->sources, 0);
    update_sources_label(priv);
}

/* ========================================================================
 * Import
 * ======================================================================== */

static void job_free(ImportJob *job) {
    g_weak_ref_clear(&job->box);
    g_ptr_array_unref(job->sources);
    g_free(job->target);
    sond_import_result_free(job->result);
    g_clear_error(&job->error);
    g_free(job);
}

static gboolean progress_idle(gpointer data) {
    ProgressMsg *msg = data;
    GtkWidget *box = g_weak_ref_get(&msg->job->box);

    if (box) {
        ImportPriv *priv = get_priv(box);
        gchar *text = NULL;
        gdouble fraction = 0.0;

        if (msg->bytes_total > 0)
            fraction = (gdouble) msg->bytes_done / (gdouble) msg->bytes_total;
        else if (msg->files_total > 0)
            fraction = (gdouble) msg->files_done / (gdouble) msg->files_total;
        if (fraction > 1.0)
            fraction = 1.0;

        text = g_strdup_printf("%u von %u Dateien%s%s", msg->files_done,
                msg->files_total, msg->current ? " - " : "",
                msg->current ? msg->current : "");
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(priv->progress), fraction);
        gtk_label_set_text(GTK_LABEL(priv->lbl_progress), text);

        g_free(text);
        g_object_unref(box);
    }

    g_free(msg->current);
    g_free(msg);

    return G_SOURCE_REMOVE;
}

/* Aus dem Worker-Thread: nur Werte kopieren und an den GTK-Thread geben */
static void on_progress(const SondImportProgress *progress, gpointer user_data) {
    ProgressMsg *msg = g_new0(ProgressMsg, 1);

    msg->job = user_data;
    msg->files_done = progress->files_done;
    msg->files_total = progress->files_total;
    msg->bytes_done = progress->bytes_done;
    msg->bytes_total = progress->bytes_total;
    msg->current = g_strdup(progress->current);

    g_idle_add(progress_idle, msg);
}

static void append_list(GString *s, const gchar *title, GPtrArray *list) {
    if (!list || list->len == 0)
        return;

    g_string_append_printf(s, "\n%s (%u):\n", title, list->len);
    for (guint i = 0; i < list->len; i++)
        g_string_append_printf(s, "  %s\n", (const gchar*) g_ptr_array_index(list, i));
}

static gboolean done_idle(gpointer data) {
    ImportJob *job = data;
    GtkWidget *box = g_weak_ref_get(&job->box);

    if (box) {
        ImportPriv *priv = get_priv(box);
        GString *s = g_string_new(NULL);

        priv->job = NULL;

        if (!job->result) {
            g_string_append_printf(s, "Import nicht möglich: %s\n",
                    job->error ? job->error->message : "unbekannter Fehler");
            gtk_label_set_text(GTK_LABEL(priv->lbl_progress), "Fehler");
        } else {
            SondImportResult *r = job->result;

            if (r->cancelled)
                g_string_append(s, "Abgebrochen.\n");
            g_string_append_printf(s, "%u Datei(en) kopiert", r->copied);
            if (r->renamed > 0)
                g_string_append_printf(s, ", davon %u wegen gleicher Namen "
                        "mit Zähler versehen", r->renamed);
            g_string_append(s, ".\n");
            if (r->copied > 0)
                g_string_append(s, "Das Hochladen übernimmt SeaDrive im "
                        "Hintergrund.\n");

            append_list(s, "Übersprungen", r->skipped);
            append_list(s, "Fehlgeschlagen", r->failed);

            gtk_label_set_text(GTK_LABEL(priv->lbl_progress),
                    r->cancelled ? "Abgebrochen" : "Fertig");
            if (!r->cancelled)
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(priv->progress), 1.0);

            /* Nur bei vollständigem Erfolg ist die Auswahl erledigt */
            if (!r->cancelled && r->failed->len == 0)
                g_ptr_array_set_size(priv->sources, 0);
        }

        set_log(priv, s->str);
        g_string_free(s, TRUE);
        update_sources_label(priv);
        g_object_unref(box);
    }

    job_free(job);

    return G_SOURCE_REMOVE;
}

static gpointer import_thread(gpointer data) {
    ImportJob *job = data;

    job->result = sond_import_copy(job->sources, job->target, on_progress, job,
            &job->cancel, &job->error);
    g_idle_add(done_idle, job);

    return NULL;
}

static void on_import_clicked(GtkButton *button, gpointer user_data) {
    ImportPriv *priv = user_data;
    GError *error = NULL;
    const gchar *configured = NULL;
    gchar *root = NULL;
    gchar *target = NULL;
    ImportJob *job = NULL;

    if (priv->job || !priv->library_name || priv->sources->len == 0)
        return;

    configured = sond_client_get_seadrive_root(priv->client);
    if (configured)
        root = g_strdup(configured);
    else {
        root = sond_import_detect_seadrive_root(
                sond_client_get_seafile_url(priv->client), &error);
        if (!root) {
            show_error(priv->main_box, "SeaDrive nicht gefunden",
                    error ? error->message : "unbekannt");
            g_clear_error(&error);
            return;
        }
    }

    target = sond_import_find_library_dir(root, priv->library_name, &error);
    g_free(root);
    if (!target) {
        gchar *detail = g_strdup_printf("%s%s", error ? error->message : "unbekannt",
                (error && error->code == G_IO_ERROR_NOT_FOUND) ?
                        "\n\nSeaDrive zeigt neu angelegte Bibliotheken erst "
                        "nach einiger Zeit an. Bitte später erneut "
                        "versuchen." : "");

        show_error(priv->main_box, "Akte im SeaDrive-Laufwerk nicht erreichbar",
                detail);
        g_free(detail);
        g_clear_error(&error);
        return;
    }

    job = g_new0(ImportJob, 1);
    g_weak_ref_init(&job->box, priv->main_box);
    job->sources = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; i < priv->sources->len; i++)
        g_ptr_array_add(job->sources,
                g_strdup(g_ptr_array_index(priv->sources, i)));
    job->target = target;

    priv->job = job;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(priv->progress), 0.0);
    gtk_label_set_text(GTK_LABEL(priv->lbl_progress), "Import läuft ...");
    set_log(priv, "");
    update_sensitivity(priv);

    g_thread_unref(g_thread_new("sond-import", import_thread, job));
}

static void on_cancel_clicked(GtkButton *button, gpointer user_data) {
    ImportPriv *priv = user_data;

    if (priv->job)
        g_atomic_int_set(&priv->job->cancel, 1);
    gtk_widget_set_sensitive(priv->btn_cancel, FALSE);
}

/* ========================================================================
 * Aufbau
 * ======================================================================== */

GtkWidget* sond_module_import_new(SondClient *client) {
    ImportPriv *priv = g_new0(ImportPriv, 1);
    GtkWidget *row = NULL;
    GtkWidget *label = NULL;
    GtkWidget *scrolled = NULL;

    priv->client = g_object_ref(client);
    priv->sources = g_ptr_array_new_with_free_func(g_free);

    priv->main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(priv->main_box, 20);
    gtk_widget_set_margin_end(priv->main_box, 20);
    gtk_widget_set_margin_top(priv->main_box, 20);
    gtk_widget_set_margin_bottom(priv->main_box, 20);
    g_object_set_data_full(G_OBJECT(priv->main_box), "sond-import-priv", priv,
            import_priv_free);

    /* Akte */
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(priv->main_box), row);

    label = gtk_label_new("Registernummer (Enter zum Prüfen):");
    gtk_box_append(GTK_BOX(row), label);

    priv->entry_regnr = gtk_entry_new();
    gtk_editable_set_width_chars(GTK_EDITABLE(priv->entry_regnr), 7);
    g_signal_connect(priv->entry_regnr, "activate",
            G_CALLBACK(on_entry_activate), priv);
    g_signal_connect(priv->entry_regnr, "changed",
            G_CALLBACK(on_entry_changed), priv);
    gtk_box_append(GTK_BOX(row), priv->entry_regnr);

    priv->lbl_akte = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(priv->lbl_akte), 0.0);
    gtk_label_set_wrap(GTK_LABEL(priv->lbl_akte), TRUE);
    gtk_box_append(GTK_BOX(priv->main_box), priv->lbl_akte);

    gtk_box_append(GTK_BOX(priv->main_box),
            gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* Quellen */
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(priv->main_box), row);

    priv->btn_folder = gtk_button_new_with_label("Ordner wählen ...");
    g_signal_connect(priv->btn_folder, "clicked", G_CALLBACK(on_folder_clicked), priv);
    gtk_box_append(GTK_BOX(row), priv->btn_folder);

    priv->btn_files = gtk_button_new_with_label("Dateien wählen ...");
    g_signal_connect(priv->btn_files, "clicked", G_CALLBACK(on_files_clicked), priv);
    gtk_box_append(GTK_BOX(row), priv->btn_files);

    priv->btn_clear = gtk_button_new_with_label("Auswahl leeren");
    g_signal_connect(priv->btn_clear, "clicked", G_CALLBACK(on_clear_clicked), priv);
    gtk_box_append(GTK_BOX(row), priv->btn_clear);

    priv->lbl_sources = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(priv->lbl_sources), 0.0);
    gtk_label_set_yalign(GTK_LABEL(priv->lbl_sources), 0.0);
    gtk_label_set_selectable(GTK_LABEL(priv->lbl_sources), TRUE);
    scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scrolled), 90);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), priv->lbl_sources);
    gtk_box_append(GTK_BOX(priv->main_box), scrolled);

    label = gtk_label_new("Der Inhalt eines gewählten Ordners wird in die "
            "Wurzel der Akte kopiert, der Aufbau bleibt erhalten. Bei "
            "gleichem Namen wird ein Zähler angehängt; Namen mit Punkt am "
            "Anfang werden übersprungen.");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_widget_add_css_class(label, "dim-label");
    gtk_box_append(GTK_BOX(priv->main_box), label);

    /* Aktion */
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(priv->main_box), row);

    priv->btn_import = gtk_button_new_with_label("Importieren");
    g_signal_connect(priv->btn_import, "clicked", G_CALLBACK(on_import_clicked), priv);
    gtk_box_append(GTK_BOX(row), priv->btn_import);

    priv->btn_cancel = gtk_button_new_with_label("Abbrechen");
    g_signal_connect(priv->btn_cancel, "clicked", G_CALLBACK(on_cancel_clicked), priv);
    gtk_box_append(GTK_BOX(row), priv->btn_cancel);

    priv->progress = gtk_progress_bar_new();
    gtk_box_append(GTK_BOX(priv->main_box), priv->progress);

    priv->lbl_progress = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(priv->lbl_progress), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(priv->lbl_progress), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_append(GTK_BOX(priv->main_box), priv->lbl_progress);

    /* Ergebnis */
    priv->log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(priv->log_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(priv->log_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(priv->log_view), TRUE);
    scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), priv->log_view);
    gtk_box_append(GTK_BOX(priv->main_box), scrolled);

    g_signal_connect(priv->main_box, "map", G_CALLBACK(on_map), priv);

    update_sources_label(priv);

    return priv->main_box;
}
