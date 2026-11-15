/*
 sond (sond_dokumente.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_dokumente.h"

#include "sond_import.h"
#include "sond_module_akte.h"
#include "sond_zond_start.h"
#include "../../gtk4/sond_treeviewfm.h"
#include "../../sond_seadrive_core.h"

typedef struct {
    gchar *lib_dir;
    gchar *lib_name;
    GtkWidget *window;
    GtkWidget *tree;
    GtkWidget *schalter;
    gboolean setze_schalter;   /* Schalter wird gerade programmgesteuert gesetzt */
} DokData;

static void dok_free(gpointer data) {
    DokData *d = data;

    g_free(d->lib_dir);
    g_free(d->lib_name);
    g_free(d);
}

static void meldung(GtkWindow *parent, const gchar *titel, const gchar *text) {
    GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", titel);

    gtk_alert_dialog_set_detail(dialog, text);
    gtk_alert_dialog_set_modal(dialog, TRUE);
    gtk_alert_dialog_show(dialog, parent);
    g_object_unref(dialog);
}

#ifdef G_OS_WIN32

/* Die Dateien, die zond zum Arbeiten braucht: Projektdatei, Index (mit den
 * SQLite-Begleitdateien) und das Verzeichnis .sond. Nur vorhandene. */
static GPtrArray* projekt_pfade(DokData *d) {
    GPtrArray *pfade = g_ptr_array_new_with_free_func(g_free);
    static const gchar *index_namen[] = { ".sond_index.db", ".sond_index.db-wal",
            ".sond_index.db-shm", ".sond" };
    gchar *projekt = sond_zond_project_file(d->lib_dir, d->lib_name);

    if (g_file_test(projekt, G_FILE_TEST_EXISTS))
        g_ptr_array_add(pfade, projekt);
    else
        g_free(projekt);

    for (guint i = 0; i < G_N_ELEMENTS(index_namen); i++) {
        gchar *pfad = g_build_filename(d->lib_dir, index_namen[i], NULL);

        if (g_file_test(pfad, G_FILE_TEST_EXISTS))
            g_ptr_array_add(pfade, pfad);
        else
            g_free(pfad);
    }

    return pfade;
}

/* Projektdateien angeheftet? Maßgeblich ist die Projektdatei (die Datei, ohne die
 * zond gar nicht startet). */
static gboolean projekt_angeheftet(DokData *d) {
    GPtrArray *pfade = projekt_pfade(d);
    gboolean angeheftet = FALSE;

    if (pfade->len > 0)
        angeheftet = (sond_seadrive_get_pin_state(g_ptr_array_index(pfade, 0)) ==
                STVFM_PIN_STATE_PINNED);
    g_ptr_array_unref(pfade);

    return angeheftet;
}

static void on_schalter_aktiv(GObject *schalter, GParamSpec *pspec, gpointer user_data) {
    DokData *d = user_data;
    gboolean an = gtk_switch_get_active(GTK_SWITCH(schalter));
    GPtrArray *pfade = NULL;
    GPtrArray *fehler = NULL;

    (void) pspec;

    if (d->setze_schalter)
        return;

    pfade = projekt_pfade(d);
    fehler = g_ptr_array_new_with_free_func(g_free);

    for (guint i = 0; i < pfade->len; i++) {
        const gchar *pfad = g_ptr_array_index(pfade, i);
        GError *error = NULL;
        //das Verzeichnis .sond rekursiv, Dateien einzeln
        gboolean verzeichnis = g_file_test(pfad, G_FILE_TEST_IS_DIR);

        if (!sond_seadrive_set_pin_state(pfad, an ? STVFM_PIN_STATE_PINNED :
                STVFM_PIN_STATE_UNSPECIFIED, verzeichnis, &error))
            sond_seadrive_pin_note_failure(fehler, pfad, error);
        g_clear_error(&error);
    }

    if (fehler->len > 0) {
        GString *text = g_string_new("Nicht alle Projektdateien ließen sich ändern:\n");

        for (guint i = 0; i < MIN(fehler->len, 5); i++)
            g_string_append_printf(text, "\n%s",
                    (const gchar*) g_ptr_array_index(fehler, i));
        meldung(GTK_WINDOW(d->window), "SeaDrive", text->str);
        g_string_free(text, TRUE);

        //Schalter dem tatsächlichen Zustand angleichen
        d->setze_schalter = TRUE;
        gtk_switch_set_active(GTK_SWITCH(schalter), projekt_angeheftet(d));
        d->setze_schalter = FALSE;
    }

    g_ptr_array_unref(fehler);
    g_ptr_array_unref(pfade);
}

#endif /* G_OS_WIN32 */

void sond_dokumente_show(SondClient *client, GtkWindow *parent, guint lfd_nr,
        guint year, const gchar *bezeichnung) {
    GError *error = NULL;
    gchar *root = NULL;
    DokData *d = NULL;
    gchar *titel = NULL;
    GtkWidget *box = NULL;
    GtkWidget *kopf = NULL;
    GtkWidget *label = NULL;

    root = sond_client_find_seadrive_root(client, &error);
    if (!root) {
        meldung(parent, "SeaDrive nicht gefunden", error ? error->message : "unbekannt");
        g_clear_error(&error);

        return;
    }

    d = g_new0(DokData, 1);
    d->lib_name = sond_module_akte_library_name(lfd_nr, year);
    d->lib_dir = sond_import_find_library_dir(root, d->lib_name, &error);
    g_free(root);

    if (!d->lib_dir) {
        meldung(parent, "Akte im SeaDrive-Laufwerk nicht erreichbar",
                error ? error->message : "unbekannt");
        g_clear_error(&error);
        dok_free(d);

        return;
    }

    titel = g_strdup_printf("Dokumentenverzeichnis %s%s%s", d->lib_name,
            (bezeichnung && *bezeichnung) ? " - " : "",
            (bezeichnung && *bezeichnung) ? bezeichnung : "");
    d->window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(d->window), titel);
    gtk_window_set_default_size(GTK_WINDOW(d->window), 900, 600);
    if (parent)
        gtk_window_set_transient_for(GTK_WINDOW(d->window), parent);
    g_free(titel);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);
    gtk_window_set_child(GTK_WINDOW(d->window), box);

    //Kopfzeile: Projektdateien offline halten
    kopf = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(box), kopf);

    label = gtk_label_new("Projektdateien (Projektdatei, Index) immer offline verfügbar:");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_box_append(GTK_BOX(kopf), label);

    d->schalter = gtk_switch_new();
    gtk_widget_set_valign(d->schalter, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(d->schalter,
            "Hält die Projektdatei, den Index und das Verzeichnis .sond dauerhaft "
            "auf diesem Rechner, damit zond auch ohne Verbindung arbeitet");
    gtk_box_append(GTK_BOX(kopf), d->schalter);

    //Baum
    d->tree = sond_treeviewfm_new();
    gtk_widget_set_vexpand(d->tree, TRUE);
    gtk_box_append(GTK_BOX(box), d->tree);

    if (sond_treeviewfm_set_root(SOND_TREEVIEWFM(d->tree), d->lib_dir, &error)) {
        meldung(parent, "Verzeichnis nicht lesbar", error ? error->message : "unbekannt");
        g_clear_error(&error);
    }

#ifdef G_OS_WIN32
    if (sond_treeviewfm_is_seadrive_path(SOND_TREEVIEWFM(d->tree))) {
        d->setze_schalter = TRUE;
        gtk_switch_set_active(GTK_SWITCH(d->schalter), projekt_angeheftet(d));
        d->setze_schalter = FALSE;
        g_signal_connect(d->schalter, "notify::active",
                G_CALLBACK(on_schalter_aktiv), d);
    } else {
        gtk_widget_set_sensitive(d->schalter, FALSE);
    }
#else
    gtk_widget_set_sensitive(d->schalter, FALSE);
#endif

    //DokData lebt mit dem Fenster
    g_object_set_data_full(G_OBJECT(d->window), "sond-dok", d, dok_free);
    gtk_window_present(GTK_WINDOW(d->window));
}
