/*
 sond (sond_import_dialog.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_import_dialog.h"
#include "sond_module_akte.h"
#include "sond_import.h"
#include "../../sond_log_and_error.h"
#include "../../sond_eingang_zettel.h"
#include "../../sond_graph/sond_graph_node.h"

#include <string.h>

typedef struct _ImportJob ImportJob;
typedef struct _ImportPriv ImportPriv;

/* Eine gewählte Quelle (Zeile der Liste) mit ihren Angaben */
typedef struct {
    ImportPriv *priv;
    gchar *path;
    gboolean is_dir;
    GtkWidget *box;          /* Zeileninhalt */
    GtkWidget *stamp_box;    /* Name, Eingang, Art */
    GtkWidget *entry_name;   /* nur bei Ordnern */
    GtkWidget *entry_datum;  /* Eingang, TT.MM.JJJJ */
    GtkWidget *calendar;
    GtkWidget *popover;
    GtkWidget *drop_art;
    gint cal_year;           /* zuletzt gezeigter Monat im Kalender */
    gint cal_month;
} SourceRow;

/* Auswahl für "Art" (Eintrag 0 = keine Angabe), gemeinsam mit zond */
#define ART_CHOICES (sond_eingang_arten())

struct _ImportPriv {
    SondClient *client;

    GtkWidget *window;
    GtkWidget *main_box;
    GtkWidget *lbl_akte;
    GtkWidget *btn_folder;
    GtkWidget *btn_files;
    GtkWidget *btn_clear;
    GtkWidget *list;            /* Quellenzeilen */
    GtkWidget *btn_import;
    GtkWidget *btn_cancel;
    GtkWidget *lbl_why;         /* warum "Importieren" gesperrt ist */
    GtkWidget *progress;
    GtkWidget *lbl_progress;
    GtkWidget *log_view;

    GPtrArray *sources;      /* (element-type SourceRow) gewählte Quellen */
    gchar *library_name;     /* Library der Akte, z. B. "2026-23" */
    gboolean gesperrt;       /* Akte abgelegt oder nicht angelegt: kein Import */
    ImportJob *job;          /* laufender Import oder NULL */
};

/* Quelle eines Importlaufs mit ihren Angaben (Kopie, gehört dem Job) */
typedef struct {
    gchar *path;
    gchar *folder_name;      /* (nullable) Zielname eines Ordners */
    gchar *eingang;          /* "JJJJ-MM-TT" */
    gchar *art;              /* (nullable) */
} JobSource;

/* Ein Importlauf. Gehört dem Worker-Thread, bis done_idle() ihn freigibt;
 * das Widget wird nur über die schwache Referenz angesprochen. */
struct _ImportJob {
    GWeakRef box;
    GPtrArray *sources;      /* (element-type JobSource) */
    gchar *target;
    gint cancel;
    gchar *user;
    SondImportResult *result;
    GError *error;
    gchar *zettel_name;      /* Dateiname des geschriebenen Zettels oder NULL */
    GError *zettel_error;
};

typedef struct {
    ImportJob *job;
    guint files_done;
    guint files_total;
    gint64 bytes_done;
    gint64 bytes_total;
    gchar *current;
} ProgressMsg;

static void source_row_free(gpointer data) {
    SourceRow *row = data;

    g_free(row->path);
    g_free(row);
}

static void job_source_free(gpointer data) {
    JobSource *js = data;

    g_free(js->path);
    g_free(js->folder_name);
    g_free(js->eingang);
    g_free(js->art);
    g_free(js);
}

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
    const gchar *why = "";

    gtk_widget_set_sensitive(priv->list, !running);

    gtk_widget_set_sensitive(priv->btn_folder, !running);
    gtk_widget_set_sensitive(priv->btn_files, !running);
    gtk_widget_set_sensitive(priv->btn_clear,
            !running && priv->sources->len > 0);
    gtk_widget_set_sensitive(priv->btn_import,
            !running && !priv->gesperrt && priv->sources->len > 0);
    gtk_widget_set_visible(priv->btn_cancel, running);
    gtk_widget_set_sensitive(priv->btn_cancel, running);

    //Grund nennen, solange "Importieren" gesperrt ist
    if (!running) {
        if (priv->gesperrt)
            why = "Für diese Akte ist kein Import möglich";
        else if (priv->sources->len == 0)
            why = "Quelle wählen";
    }
    gtk_label_set_text(GTK_LABEL(priv->lbl_why), why);
}

/* ---- Kalender ---------------------------------------------------------- */

static void on_calendar_day_selected(GtkCalendar *calendar, gpointer user_data) {
    SourceRow *row = user_data;
    GDateTime *dt = gtk_calendar_get_date(calendar);
    gint year = g_date_time_get_year(dt);
    gint month = g_date_time_get_month(dt);
    gchar *text = g_date_time_format(dt, "%d.%m.%Y");
    gboolean same_month = (year == row->cal_year && month == row->cal_month);

    gtk_editable_set_text(GTK_EDITABLE(row->entry_datum), text);

    row->cal_year = year;
    row->cal_month = month;

    //Monatswechsel im Kalender schließt das Fenster nicht, ein Klick auf einen Tag schon
    if (same_month)
        gtk_popover_popdown(GTK_POPOVER(row->popover));

    g_free(text);
    g_date_time_unref(dt);
}

/* Beim Öffnen: Kalender auf das Datum im Feld stellen (sonst heute) */
static void on_popover_map(GtkWidget *popover, gpointer user_data) {
    SourceRow *row = user_data;
    gchar *iso = NULL;
    GDateTime *dt = NULL;

    if (sond_eingang_datum_parse(
            gtk_editable_get_text(GTK_EDITABLE(row->entry_datum)), &iso)) {
        gint y = 0, m = 0, d = 0;

        sscanf(iso, "%d-%d-%d", &y, &m, &d);
        dt = g_date_time_new_local(y, m, d, 0, 0, 0);
    }
    if (!dt)
        dt = g_date_time_new_now_local();
    g_free(iso);

    row->cal_year = g_date_time_get_year(dt);
    row->cal_month = g_date_time_get_month(dt);

    g_signal_handlers_block_by_func(row->calendar, on_calendar_day_selected, row);
    gtk_calendar_set_date(GTK_CALENDAR(row->calendar), dt);
    g_signal_handlers_unblock_by_func(row->calendar, on_calendar_day_selected, row);

    g_date_time_unref(dt);
}

/* ---- Quellenzeilen ----------------------------------------------------- */

static void on_remove_clicked(GtkButton *button, gpointer user_data) {
    SourceRow *row = user_data;
    ImportPriv *priv = row->priv;

    gtk_list_box_remove(GTK_LIST_BOX(priv->list),
            gtk_widget_get_parent(row->box));
    g_ptr_array_remove(priv->sources, row);   //gibt die Zeile frei
    update_sensitivity(priv);
}

static GtkWidget* labeled(const gchar *text, GtkWidget *child) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

    gtk_box_append(GTK_BOX(box), gtk_label_new(text));
    gtk_box_append(GTK_BOX(box), child);

    return box;
}

/* Neue Zeile; Eingang und Art übernimmt sie von der vorigen Zeile */
static void add_source(ImportPriv *priv, const gchar *path) {
    SourceRow *row = NULL;
    SourceRow *prev = NULL;
    GtkWidget *icon = NULL;
    GtkWidget *label = NULL;
    GtkWidget *btn_cal = NULL;
    GtkWidget *btn_remove = NULL;
    gchar *base = NULL;

    if (!path)
        return;

    for (guint i = 0; i < priv->sources->len; i++)
        if (g_strcmp0(((SourceRow*) g_ptr_array_index(priv->sources, i))->path,
                path) == 0)
            return;

    if (priv->sources->len > 0)
        prev = g_ptr_array_index(priv->sources, priv->sources->len - 1);

    row = g_new0(SourceRow, 1);
    row->priv = priv;
    row->path = g_strdup(path);
    row->is_dir = g_file_test(path, G_FILE_TEST_IS_DIR);

    row->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_top(row->box, 4);
    gtk_widget_set_margin_bottom(row->box, 4);
    gtk_widget_set_margin_start(row->box, 4);
    gtk_widget_set_margin_end(row->box, 4);

    icon = gtk_image_new_from_icon_name(row->is_dir ? "folder-symbolic" :
            "text-x-generic-symbolic");
    gtk_box_append(GTK_BOX(row->box), icon);

    label = gtk_label_new(path);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_width_chars(GTK_LABEL(label), 30);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_widget_set_tooltip_text(label, path);
    gtk_box_append(GTK_BOX(row->box), label);

    row->stamp_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(row->box), row->stamp_box);

    if (row->is_dir) {
        row->entry_name = gtk_entry_new();
        gtk_editable_set_width_chars(GTK_EDITABLE(row->entry_name), 18);
        base = g_path_get_basename(path);
        gtk_editable_set_text(GTK_EDITABLE(row->entry_name), base);
        g_free(base);
        gtk_box_append(GTK_BOX(row->stamp_box), labeled("als Ordner:", row->entry_name));
    }

    row->entry_datum = gtk_entry_new();
    gtk_editable_set_width_chars(GTK_EDITABLE(row->entry_datum), 10);
    if (prev)
        gtk_editable_set_text(GTK_EDITABLE(row->entry_datum),
                gtk_editable_get_text(GTK_EDITABLE(prev->entry_datum)));
    else {
        gchar *heute = sond_eingang_datum_heute();
        gchar *anzeige = sond_eingang_datum_anzeige(heute);

        gtk_editable_set_text(GTK_EDITABLE(row->entry_datum), anzeige);
        g_free(anzeige);
        g_free(heute);
    }

    row->calendar = gtk_calendar_new();
    row->popover = gtk_popover_new();
    gtk_popover_set_child(GTK_POPOVER(row->popover), row->calendar);
    g_signal_connect(row->calendar, "day-selected",
            G_CALLBACK(on_calendar_day_selected), row);
    g_signal_connect(row->popover, "map", G_CALLBACK(on_popover_map), row);

    btn_cal = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(btn_cal),
            "x-office-calendar-symbolic");
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(btn_cal), row->popover);
    gtk_widget_set_tooltip_text(btn_cal, "Datum im Kalender wählen");

    {
        GtkWidget *datum_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);

        gtk_box_append(GTK_BOX(datum_box), row->entry_datum);
        gtk_box_append(GTK_BOX(datum_box), btn_cal);
        gtk_box_append(GTK_BOX(row->stamp_box), labeled("Eingang:", datum_box));
    }

    row->drop_art = gtk_drop_down_new_from_strings(ART_CHOICES);
    if (prev)
        gtk_drop_down_set_selected(GTK_DROP_DOWN(row->drop_art),
                gtk_drop_down_get_selected(GTK_DROP_DOWN(prev->drop_art)));
    gtk_box_append(GTK_BOX(row->stamp_box), labeled("Art:", row->drop_art));

    btn_remove = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_add_css_class(btn_remove, "flat");
    gtk_widget_set_tooltip_text(btn_remove, "Diese Quelle entfernen");
    g_signal_connect(btn_remove, "clicked", G_CALLBACK(on_remove_clicked), row);
    gtk_box_append(GTK_BOX(row->box), btn_remove);

    gtk_list_box_append(GTK_LIST_BOX(priv->list), row->box);
    g_ptr_array_add(priv->sources, row);
}

static void clear_sources(ImportPriv *priv) {
    gtk_list_box_remove_all(GTK_LIST_BOX(priv->list));
    g_ptr_array_set_size(priv->sources, 0);
}

static void set_log(ImportPriv *priv, const gchar *text) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(priv->log_view));

    gtk_text_buffer_set_text(buffer, text, -1);
}

/* ========================================================================
 * Akte
 * ======================================================================== */

/* Anzeige der Akte und, soweit möglich, Prüfung ihres Status. Ist der Client
 * angemeldet, fragt sie den Server; sonst (offline oder nicht angemeldet) wird
 * nur darauf hingewiesen, dass der Status nicht geprüft wurde - der Import
 * selbst braucht den Server nicht. */
static void init_akte(ImportPriv *priv, guint lfd_nr, guint year,
        const gchar *bezeichnung) {
    gchar *titel = g_strdup_printf("Akte %u/%02u%s%s", lfd_nr, year % 100,
            (bezeichnung && *bezeichnung) ? ": " : "",
            (bezeichnung && *bezeichnung) ? bezeichnung : "");
    gchar *msg = NULL;
    gboolean fehler = FALSE;

    priv->library_name = sond_module_akte_library_name(lfd_nr, year);
    priv->gesperrt = FALSE;

    if (sond_client_is_logged_in(priv->client)) {
        GError *error = NULL;
        SondGraphNode *node = sond_module_akte_find_by_regnr(priv->client,
                lfd_nr, year, &error);

        if (node) {
            SondAkteStatus status = sond_module_akte_get_status(node);

            if (status == SOND_AKTE_STATUS_AKTIV)
                msg = g_strdup(titel);
            else {
                msg = g_strdup_printf("%s\n\nDie Akte ist %s.", titel,
                        status == SOND_AKTE_STATUS_ABGELEGT ? "abgelegt" :
                                "noch nicht angelegt");
                priv->gesperrt = TRUE;
                fehler = TRUE;
            }
            g_object_unref(node);
        } else if (error && error->code == G_IO_ERROR_NOT_FOUND) {
            msg = g_strdup_printf("%s\n\nDie Akte gibt es auf dem Server "
                    "nicht.", titel);
            priv->gesperrt = TRUE;
            fehler = TRUE;
        } else
            msg = g_strdup_printf("%s\n\nStatus nicht geprüft (Server nicht "
                    "erreichbar).", titel);
        g_clear_error(&error);
    } else
        msg = g_strdup_printf("%s\n\nStatus nicht geprüft (nicht "
                "angemeldet).", titel);

    set_akte_label(priv, msg, fehler);
    g_free(msg);
    g_free(titel);
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

        update_sensitivity(get_priv(box));
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

        update_sensitivity(get_priv(box));
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

    clear_sources(priv);
    update_sensitivity(priv);
}

/* ========================================================================
 * Import
 * ======================================================================== */

static void job_free(ImportJob *job) {
    g_weak_ref_clear(&job->box);
    g_ptr_array_unref(job->sources);
    g_free(job->target);
    g_free(job->user);
    sond_import_result_free(job->result);
    g_clear_error(&job->error);
    g_free(job->zettel_name);
    g_clear_error(&job->zettel_error);
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

            if (r->entries->len > 0) {
                if (job->zettel_name)
                    g_string_append_printf(s, "Eingang erfasst für %u "
                            "Eintrag/Einträge (Zettel %s).\n",
                            r->entries->len, job->zettel_name);
                else
                    g_string_append_printf(s, "ACHTUNG: Die Eingangsangabe "
                            "konnte nicht gespeichert werden: %s\nDie Dateien "
                            "sind kopiert.\n", job->zettel_error ?
                                    job->zettel_error->message : "unbekannt");
            }

            append_list(s, "Übersprungen", r->skipped);
            append_list(s, "Fehlgeschlagen", r->failed);

            gtk_label_set_text(GTK_LABEL(priv->lbl_progress),
                    r->cancelled ? "Abgebrochen" : "Fertig");
            if (!r->cancelled)
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(priv->progress), 1.0);

            /* Nur bei vollständigem Erfolg ist die Auswahl erledigt */
            if (!r->cancelled && r->failed->len == 0)
                clear_sources(priv);
        }

        set_log(priv, s->str);
        g_string_free(s, TRUE);
        update_sensitivity(priv);
        g_object_unref(box);
    }

    job_free(job);

    return G_SOURCE_REMOVE;
}

/* Eingangsangabe (Zettel) für die obersten Einträge des Laufs schreiben. Ein
 * Fehlschlag macht den Import nicht ungültig, er wird nur gemeldet. */
static void write_zettel(ImportJob *job) {
    SondEingangZettel *zettel = NULL;

    if (!job->result || job->result->entries->len == 0)
        return;

    zettel = sond_eingang_zettel_new(job->user, "import");
    for (guint i = 0; i < job->result->entries->len; i++) {
        SondImportEntry *entry = g_ptr_array_index(job->result->entries, i);
        JobSource *js = NULL;

        if (entry->source >= job->sources->len)
            continue;
        js = g_ptr_array_index(job->sources, entry->source);

        //Eingang und Art der Quelle, aus der der Eintrag stammt
        sond_eingang_zettel_add(zettel, entry->rel, entry->is_dir,
                js->eingang, js->art, NULL, entry->mtime);
    }

    job->zettel_name = sond_eingang_zettel_write(zettel, job->target,
            &job->zettel_error);
    sond_eingang_zettel_free(zettel);
}

static gpointer import_thread(gpointer data) {
    ImportJob *job = data;
    GPtrArray *core_sources = g_ptr_array_new_with_free_func(g_free);

    for (guint i = 0; i < job->sources->len; i++) {
        JobSource *js = g_ptr_array_index(job->sources, i);
        SondImportSource *cs = g_new0(SondImportSource, 1);

        cs->path = js->path;
        cs->folder_name = js->folder_name;
        g_ptr_array_add(core_sources, cs);
    }

    job->result = sond_import_copy(core_sources, job->target, on_progress, job,
            &job->cancel, &job->error);
    g_ptr_array_unref(core_sources);
    write_zettel(job);
    g_idle_add(done_idle, job);

    return NULL;
}

static void on_import_clicked(GtkButton *button, gpointer user_data) {
    ImportPriv *priv = user_data;
    GError *error = NULL;
    gchar *root = NULL;
    gchar *target = NULL;
    ImportJob *job = NULL;
    GPtrArray *job_sources = NULL;

    if (priv->job || priv->gesperrt || !priv->library_name ||
            priv->sources->len == 0)
        return;

    //Angaben je Quelle prüfen und übernehmen, bevor etwas kopiert wird
    job_sources = g_ptr_array_new_with_free_func(job_source_free);
    for (guint i = 0; i < priv->sources->len; i++) {
        SourceRow *row = g_ptr_array_index(priv->sources, i);
        JobSource *js = g_new0(JobSource, 1);

        js->path = g_strdup(row->path);
        g_ptr_array_add(job_sources, js);

        if (!sond_eingang_datum_parse(
                gtk_editable_get_text(GTK_EDITABLE(row->entry_datum)),
                &js->eingang)) {
            gchar *detail = g_strdup_printf("Quelle: %s\n\nBitte das Datum "
                    "als TT.MM.JJJJ eingeben (oder im Kalender wählen).",
                    row->path);

            show_error(priv->main_box, "Eingangsdatum ungültig", detail);
            g_free(detail);
            g_ptr_array_unref(job_sources);
            return;
        }

        if (row->entry_name) {
            const gchar *name = gtk_editable_get_text(GTK_EDITABLE(row->entry_name));

            if (name && *name)
                js->folder_name = g_strdup(name);
        }

        {
            guint art = gtk_drop_down_get_selected(GTK_DROP_DOWN(row->drop_art));

            if (art > 0 && art < g_strv_length((gchar**) ART_CHOICES))
                js->art = g_strdup(ART_CHOICES[art]);
        }
    }

    root = sond_client_find_seadrive_root(priv->client, &error);
    if (!root) {
        show_error(priv->main_box, "SeaDrive nicht gefunden",
                error ? error->message : "unbekannt");
        g_clear_error(&error);
        g_ptr_array_unref(job_sources);
        return;
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
        g_ptr_array_unref(job_sources);
        return;
    }

    job = g_new0(ImportJob, 1);
    g_weak_ref_init(&job->box, priv->main_box);
    job->sources = job_sources;
    job->target = target;
    //ohne Anmeldung (offline) gilt der Systembenutzer
    job->user = g_strdup(sond_client_get_user_id(priv->client) ?
            sond_client_get_user_id(priv->client) : g_get_user_name());

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

/* Solange ein Import läuft, bleibt der Dialog offen (dort ist "Abbrechen") */
static gboolean on_close_request(GtkWindow *window, gpointer user_data) {
    ImportPriv *priv = user_data;

    return priv->job != NULL;
}

void sond_import_dialog_show(SondClient *client, GtkWindow *parent,
        guint lfd_nr, guint year, const gchar *bezeichnung) {
    ImportPriv *priv = g_new0(ImportPriv, 1);
    GtkWidget *row = NULL;
    GtkWidget *label = NULL;
    GtkWidget *scrolled = NULL;

    priv->client = g_object_ref(client);
    priv->sources = g_ptr_array_new_with_free_func(source_row_free);

    priv->window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(priv->window), "Dokumente importieren");
    gtk_window_set_default_size(GTK_WINDOW(priv->window), 900, 700);
    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(priv->window), parent);
        gtk_window_set_modal(GTK_WINDOW(priv->window), TRUE);
    }
    g_signal_connect(priv->window, "close-request",
            G_CALLBACK(on_close_request), priv);

    priv->main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(priv->main_box, 20);
    gtk_widget_set_margin_end(priv->main_box, 20);
    gtk_widget_set_margin_top(priv->main_box, 20);
    gtk_widget_set_margin_bottom(priv->main_box, 20);
    gtk_window_set_child(GTK_WINDOW(priv->window), priv->main_box);
    g_object_set_data_full(G_OBJECT(priv->main_box), "sond-import-priv", priv,
            import_priv_free);

    /* Akte */
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

    priv->list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(priv->list), GTK_SELECTION_NONE);
    gtk_list_box_set_placeholder(GTK_LIST_BOX(priv->list),
            gtk_label_new("Keine Quellen gewählt."));
    scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scrolled), 160);
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), priv->list);
    gtk_box_append(GTK_BOX(priv->main_box), scrolled);

    label = gtk_label_new("Ordner kommen als Ordner in die Akte, lose "
            "Dateien direkt in die Wurzel. Jede Quelle bekommt ihren eigenen "
            "Namen (Ordner), ihr Eingangsdatum und ihre Art; neue Zeilen "
            "übernehmen die Werte der vorigen. Bei gleichem Namen wird ein "
            "Zähler angehängt, Namen mit Punkt am Anfang werden übersprungen.");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_widget_add_css_class(label, "dim-label");
    gtk_box_append(GTK_BOX(priv->main_box), label);

    /* Aktion */
    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(priv->main_box), row);

    priv->btn_import = gtk_button_new_with_label("Importieren");
    g_signal_connect(priv->btn_import, "clicked", G_CALLBACK(on_import_clicked), priv);
    gtk_box_append(GTK_BOX(row), priv->btn_import);

    priv->btn_cancel = gtk_button_new_with_label("Abbrechen");
    g_signal_connect(priv->btn_cancel, "clicked", G_CALLBACK(on_cancel_clicked), priv);
    gtk_box_append(GTK_BOX(row), priv->btn_cancel);

    priv->lbl_why = gtk_label_new("");
    gtk_widget_add_css_class(priv->lbl_why, "dim-label");
    gtk_box_append(GTK_BOX(row), priv->lbl_why);

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

    init_akte(priv, lfd_nr, year, bezeichnung);
    update_sensitivity(priv);

    gtk_window_present(GTK_WINDOW(priv->window));
}
