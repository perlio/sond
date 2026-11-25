/*
 sond (sond_module_projekt.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_module_projekt.h"

#include <string.h>

#include "../sond_akten.h"
#include "../sond_akten_suche.h"
#include "sond_dokumente.h"
#include "sond_zond_lease.h"
#include "../../sond_zond_prozess.h"

#define SERVER_KURZ_TIMEOUT 5

typedef struct {
    SondClient *client;
    SondAkteLease *lease;      /* Schreibberechtigung der Akten (Besitz) */
    GtkWidget *entry;
    GtkWidget *info_kurz;      /* Kurzbezeichnung der gewählten Akte */
    GtkWidget *info_ggs;       /* Gegenstand, kleiner darunter */
    GtkWidget *btn_ok;         /* "Schließen": Akte schließen und eine andere wählen */
    GtkWidget *btn_zond;       /* zond mit der Projektdatei der gewählten Akte starten */
    GtkWidget *lease_label;    /* Stand der Schreibberechtigung der gewählten Akte */
    GtkWidget *btn_release;    /* Berechtigung zurückgeben */
    SondZondStarter *starter;
    guint timer;               /* prüft, ob zond läuft */
    GtkWidget *inhalt;         /* Platz für das Dokumentenverzeichnis */
    GtkWidget *doku;           /* Dokumentenverzeichnis der gewählten Akte; NULL = keins */
    gchar *doku_lib;           /* Library-Name dazu */
    guint doku_lfd_nr;
    guint doku_year;
    gint64 doku_akte_id;
} ProjektPriv;

static void priv_free(gpointer data) {
    ProjektPriv *p = data;

    if (p->lease) {
        //die Berechtigung bleibt bestehen (lokaler Zustand in der Datei)
        sond_akte_lease_set_event_handler(p->lease, NULL, NULL);
        sond_akte_lease_free(p->lease);
    }
    g_clear_object(&p->client);
    g_free(p->doku_lib);
    g_free(p);
}

static void meldung(ProjektPriv *p, const gchar *titel, const gchar *text) {
    GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", titel);
    GtkRoot *root = gtk_widget_get_root(p->entry);

    gtk_alert_dialog_set_detail(dialog, text);
    gtk_alert_dialog_set_modal(dialog, TRUE);
    gtk_alert_dialog_show(dialog, root && GTK_IS_WINDOW(root) ? GTK_WINDOW(root) : NULL);
    g_object_unref(dialog);
}

/* Node-ID der Akte: vom Server, sonst laut lokalem Lease-Zustand (offline); 0, wenn beides fehlt */
static gint64 akte_id_ermitteln(ProjektPriv *p, guint lfd_nr, guint year, const gchar *lib) {
    gint64 id = 0;

    if (sond_client_is_logged_in(p->client)) {
        GError *error = NULL;
        guint alt = sond_client_set_io_timeout(p->client, SERVER_KURZ_TIMEOUT);
        SondGraphNode *node = sond_akten_suche_node_by_regnr(p->client, lfd_nr, year, &error);

        if (node) {
            id = sond_graph_node_get_id(node);
            g_object_unref(node);
        }
        g_clear_error(&error);
        sond_client_set_io_timeout(p->client, alt);
    }

    if (id <= 0)
        id = sond_akte_lease_find_by_name(p->lease, lib);

    return id;
}

//Bezeichnung und Gegenstand der gezeigten Akte (NULL oder leer: nichts); der Gegenstand steht kleiner darunter
static void info_setzen(ProjektPriv *p, const gchar *kurz, const gchar *gegenstand) {
    gboolean hat_kurz = kurz && *kurz;
    gboolean hat_ggs = gegenstand && *gegenstand;

    gtk_label_set_text(GTK_LABEL(p->info_kurz), hat_kurz ? kurz : "");
    gtk_widget_set_tooltip_text(p->info_kurz, hat_kurz ? kurz : NULL);
    gtk_widget_set_visible(p->info_kurz, hat_kurz);
    gtk_label_set_text(GTK_LABEL(p->info_ggs), hat_ggs ? gegenstand : "");
    gtk_widget_set_tooltip_text(p->info_ggs, hat_ggs ? gegenstand : NULL);
    gtk_widget_set_visible(p->info_ggs, hat_ggs);
}

//Zeile zur Schreibberechtigung: nur sichtbar, wenn es etwas zu sagen gibt
static void lease_text_setzen(ProjektPriv *p, const gchar *text) {
    gtk_label_set_text(GTK_LABEL(p->lease_label), text);
    gtk_widget_set_visible(p->lease_label, text && *text);
}

static void entry_zustand(ProjektPriv *p);

/* Läuft zond gerade mit der gezeigten Akte? */
static gboolean zond_laeuft_fuer_akte(ProjektPriv *p) {
    return p->doku_lib && sond_zond_prozess_laeuft(p->doku_lib);
}

/* Schaltfläche und Anzeige der Schreibberechtigung nachführen. Der Server wird kurz befragt;
 * ohne Antwort bleibt der lokale Stand. */
static void anzeige_aktualisieren(ProjektPriv *p) {
    gchar *text = NULL;
    gboolean wartet = sond_zond_starter_waiting(p->starter);

    gtk_widget_set_sensitive(p->btn_zond, p->doku && !wartet && !zond_laeuft_fuer_akte(p));
    entry_zustand(p);
    if (p->doku)
        sond_dokumente_sperre_pruefen(p->doku);
    gtk_widget_set_tooltip_text(p->btn_zond, p->doku && zond_laeuft_fuer_akte(p) ?
            "zond hat diese Akte schon geöffnet." :
            "Die Projektdatei der gewählten Akte in zond öffnen");

    if (!p->doku) {
        lease_text_setzen(p, "");
        gtk_widget_set_visible(p->btn_release, FALSE);

        return;
    }

    gtk_widget_set_visible(p->btn_release,
            p->doku_akte_id > 0 && sond_akte_lease_may_write(p->lease, p->doku_akte_id));

    if (sond_zond_starter_message(p->starter, p->doku_akte_id)) {
        text = g_strdup(sond_zond_starter_message(p->starter, p->doku_akte_id));
    } else if (wartet) {
        text = g_strdup("Projekt: warte auf die Freigabe durch den anderen Rechner ...");
    } else if (p->doku_akte_id > 0 && sond_client_is_logged_in(p->client)) {
        SondLeaseInfo info = { 0 };
        GError *error = NULL;
        guint alt = sond_client_set_io_timeout(p->client, SERVER_KURZ_TIMEOUT);

        if (sond_akte_lease_get_info(p->lease, p->doku_akte_id, &info, &error))
            text = sond_lease_info_text(&info);
        sond_client_set_io_timeout(p->client, alt);
        sond_akte_lease_info_clear(&info);
        g_clear_error(&error);
    }

    if (!text)
        text = g_strdup(p->doku_akte_id > 0 && sond_akte_lease_may_write(p->lease, p->doku_akte_id) ?
                "Projekt: auf diesem Rechner ausgecheckt (Stand ohne Server)" :
                "Projekt: Stand unbekannt (Server nicht befragt)");
    lease_text_setzen(p, text);
    g_free(text);
}

/* Die gewählte Akte "rastet ein": Der Entry ist nur ohne gezeigte Akte bedienbar, "Schließen" gibt ihn
 * wieder frei (nicht während eines Index-Jobs). */
static void entry_zustand(ProjektPriv *p) {
    gboolean busy = p->doku && sond_dokumente_busy(p->doku);

    sond_akten_suche_set_aktiv(p->entry, p->doku == NULL);
    gtk_widget_set_sensitive(p->btn_ok, p->doku && !busy);
    gtk_widget_set_tooltip_text(p->btn_ok, busy ?
            "Für die Akte läuft ein Index-Job. Bitte warten, bis er beendet ist." :
            "Akte schließen und eine andere wählen");
}

static void on_starter_changed(gpointer user_data) {
    anzeige_aktualisieren(user_data);
}

static void on_release_clicked(GtkButton *button, gpointer user_data) {
    ProjektPriv *p = user_data;

    (void) button;

    if (p->doku && p->doku_akte_id > 0)
        sond_zond_starter_confirm_release(p->starter, p->doku_akte_id, p->doku_lib);
}

/* "Schließen": die gezeigte Akte schließen, den Entry wieder freigeben und die letzte RegNr vorschlagen */
static void on_ok_clicked(GtkButton *button, gpointer user_data) {
    ProjektPriv *p = user_data;

    (void) button;

    if (!p->doku || sond_dokumente_busy(p->doku))
        return;

    sond_zond_starter_cancel_wait(p->starter);
    gtk_box_remove(GTK_BOX(p->inhalt), p->doku);
    p->doku = NULL;
    g_clear_pointer(&p->doku_lib, g_free);
    p->doku_akte_id = 0;
    info_setzen(p, NULL, NULL);
    gtk_editable_set_text(GTK_EDITABLE(p->entry), "");
    anzeige_aktualisieren(p);
    sond_akten_suche_vorschlagen(p->entry);
    sond_akten_suche_fokus(p->entry);
}

static void on_zond_clicked(GtkButton *button, gpointer user_data) {
    ProjektPriv *p = user_data;

    (void) button;

    if (p->doku)
        sond_zond_starter_start(p->starter, p->doku_lfd_nr, p->doku_year, p->doku_akte_id);
}

static gboolean on_timer(gpointer user_data) {
    ProjektPriv *p = user_data;

    if (p->doku)
        gtk_widget_set_sensitive(p->btn_zond, !sond_zond_starter_waiting(p->starter) &&
                !zond_laeuft_fuer_akte(p));
    entry_zustand(p);

    return G_SOURCE_CONTINUE;
}

/* Zeigt das Dokumentenverzeichnis der Akte; das bisherige wird dafür entfernt. */
static void akte_waehlen(ProjektPriv *p, guint lfd_nr, guint year, const gchar *bezeichnung,
        const gchar *gegenstand, gint64 akte_id) {
    g_autofree gchar *lib = sond_akten_library_name(lfd_nr, year);
    GError *error = NULL;
    GtkWidget *neu = NULL;

    if (p->doku && !g_strcmp0(p->doku_lib, lib))
        return; //wird schon gezeigt

    if (p->doku && sond_dokumente_busy(p->doku)) {
        meldung(p, "Akte kann nicht gewechselt werden",
                "Für die gezeigte Akte läuft ein Index-Job. Bitte warten, bis er beendet ist.");

        return;
    }

    sond_zond_starter_cancel_wait(p->starter);
    if (p->doku) {
        gtk_box_remove(GTK_BOX(p->inhalt), p->doku);
        p->doku = NULL;
        g_clear_pointer(&p->doku_lib, g_free);
        info_setzen(p, NULL, NULL);
        anzeige_aktualisieren(p);
    }

    if (akte_id <= 0)
        akte_id = akte_id_ermitteln(p, lfd_nr, year, lib);
    neu = sond_dokumente_widget_new(p->client, p->lease, lfd_nr, year, akte_id, &error);
    if (!neu) {
        meldung(p, "Dokumentenverzeichnis nicht verfügbar", error ? error->message : "unbekannt");
        g_clear_error(&error);

        return;
    }

    gtk_box_append(GTK_BOX(p->inhalt), neu);
    p->doku = neu;
    p->doku_lib = g_strdup(lib);
    p->doku_lfd_nr = lfd_nr;
    p->doku_year = year;
    p->doku_akte_id = akte_id;

    info_setzen(p, bezeichnung, gegenstand);
    anzeige_aktualisieren(p);
}

/* Eine Akte wurde im Suchfeld gewählt (einziger Treffer oder Wahl in der Liste) */
static void on_akte_gewaehlt(const SondAkteTreffer *t, SondSucheServer server, gpointer user_data) {
    ProjektPriv *p = user_data;

    (void) server;

    gtk_editable_set_text(GTK_EDITABLE(p->entry), t->regnr_display);
    sond_client_set_last_regnr(p->client, t->lfd_nr, t->year);
    akte_waehlen(p, t->lfd_nr, t->year, t->kurzbezeichnung, t->gegenstand,
            t->node ? sond_graph_node_get_id(t->node) : 0);
}

/* Beim Anzeigen der Seite, ohne gezeigte Akte: die zuletzt verwendete RegNr vorschlagen (sie kann im
 * Aktenmodul gewählt worden sein) */
static void on_map(GtkWidget *widget, gpointer user_data) {
    ProjektPriv *p = user_data;

    (void) widget;

    if (!p->doku)
        sond_akten_suche_vorschlagen(p->entry);
}

static void on_destroy(GtkWidget *widget, gpointer user_data) {
    ProjektPriv *p = user_data;

    (void) widget;

    if (p->timer) {
        g_source_remove(p->timer);
        p->timer = 0;
    }
    sond_zond_starter_free(p->starter);
    p->starter = NULL;
}

GtkWidget* sond_module_projekt_new(SondClient *client) {
    ProjektPriv *p = g_new0(ProjektPriv, 1);
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *zeile = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *frage = gtk_label_new("Welche Akte?");

    p->client = g_object_ref(client);
    p->lease = sond_akte_lease_new(client);

    gtk_widget_set_margin_start(main_box, 8);
    gtk_widget_set_margin_end(main_box, 8);
    gtk_widget_set_margin_top(main_box, 8);
    gtk_widget_set_margin_bottom(main_box, 8);

    gtk_widget_add_css_class(frage, "heading");
    gtk_box_append(GTK_BOX(zeile), frage);

    p->entry = sond_akten_suche_new(client);
    sond_akten_suche_set_funcs(p->entry, on_akte_gewaehlt, NULL, p);
    gtk_box_append(GTK_BOX(zeile), p->entry);

    p->btn_ok = gtk_button_new_with_label("Schließen");
    gtk_widget_set_sensitive(p->btn_ok, FALSE);
    gtk_widget_set_tooltip_text(p->btn_ok, "Akte schließen und eine andere wählen");
    g_signal_connect(p->btn_ok, "clicked", G_CALLBACK(on_ok_clicked), p);
    gtk_box_append(GTK_BOX(zeile), p->btn_ok);

    //Kurzbezeichnung, darunter der Gegenstand kleiner und abgesetzt
    {
        GtkWidget *info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

        gtk_widget_set_hexpand(info, TRUE);
        gtk_widget_set_valign(info, GTK_ALIGN_CENTER);
        p->info_kurz = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(p->info_kurz), 0.0);
        gtk_label_set_ellipsize(GTK_LABEL(p->info_kurz), PANGO_ELLIPSIZE_END);
        gtk_widget_set_visible(p->info_kurz, FALSE);
        gtk_box_append(GTK_BOX(info), p->info_kurz);
        p->info_ggs = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(p->info_ggs), 0.0);
        gtk_label_set_ellipsize(GTK_LABEL(p->info_ggs), PANGO_ELLIPSIZE_END);
        gtk_widget_add_css_class(p->info_ggs, "caption");
        gtk_widget_add_css_class(p->info_ggs, "dim-label");
        gtk_widget_set_visible(p->info_ggs, FALSE);
        gtk_box_append(GTK_BOX(info), p->info_ggs);
        gtk_box_append(GTK_BOX(zeile), info);
    }

    p->btn_zond = gtk_button_new_with_label("zond starten");
    gtk_widget_set_sensitive(p->btn_zond, FALSE);
    g_signal_connect(p->btn_zond, "clicked", G_CALLBACK(on_zond_clicked), p);
    gtk_box_append(GTK_BOX(zeile), p->btn_zond);

    p->btn_release = gtk_button_new_with_label("Freigeben");
    gtk_widget_set_tooltip_text(p->btn_release,
            "Die Schreibberechtigung zurückgeben, damit ein anderer Rechner die Akte "
            "bearbeiten kann");
    g_signal_connect(p->btn_release, "clicked", G_CALLBACK(on_release_clicked), p);
    gtk_widget_set_visible(p->btn_release, FALSE);
    gtk_box_append(GTK_BOX(zeile), p->btn_release);
    gtk_box_append(GTK_BOX(main_box), zeile);

    p->lease_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(p->lease_label), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(p->lease_label), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(p->lease_label, "caption");
    gtk_widget_add_css_class(p->lease_label, "dim-label");
    gtk_widget_set_visible(p->lease_label, FALSE);
    gtk_box_append(GTK_BOX(main_box), p->lease_label);

    p->inhalt = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(p->inhalt, TRUE);
    gtk_box_append(GTK_BOX(main_box), p->inhalt);

    p->starter = sond_zond_starter_new(client, p->lease, main_box, on_starter_changed, p);
    sond_akte_lease_set_event_handler(p->lease, sond_zond_starter_lease_event, p->starter);
    sond_akte_lease_start_heartbeat(p->lease);
    p->timer = g_timeout_add_seconds(5, on_timer, p);

    g_object_set_data_full(G_OBJECT(main_box), "priv", p, priv_free);
    g_signal_connect(main_box, "destroy", G_CALLBACK(on_destroy), p);
    g_signal_connect(main_box, "map", G_CALLBACK(on_map), p);

    return main_box;
}

void sond_module_projekt_focus_entry(GtkWidget *module) {
    ProjektPriv *p = g_object_get_data(G_OBJECT(module), "priv");

    if (p)
        sond_akten_suche_fokus(p->entry);
}
