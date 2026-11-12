/*
 sond (sond_module_verwaltung.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_module_verwaltung.h"
#include "sond_archiv_kandidaten.h"
#include "../../sond_log_and_error.h"
#include "../../sond_graph/sond_graph_node.h"
#include "../../sond_graph/sond_graph_db.h"

#include <string.h>

/* Zustand einer Zeile der Kandidatenliste */
typedef enum {
    ZEILE_WARTET,        /* Sperre noch nicht versucht */
    ZEILE_BEREIT,        /* gesperrt (von uns), kann archiviert werden */
    ZEILE_FREMD,         /* von jemand anderem gesperrt oder Job läuft: außen vor */
    ZEILE_FEHLER,        /* Sperre scheiterte aus anderem Grund */
    ZEILE_ABGEWAEHLT     /* vom Nutzer abgewählt, wieder entsperrt */
} ZeilenZustand;

typedef struct {
    SondArchivKandidat *k;
    ZeilenZustand zustand;
    gboolean gesperrt;       /* wir halten die Sperre */
    GtkWidget *row;
    GtkWidget *check;
    GtkWidget *status;
    gulong toggled_id;
} Zeile;

typedef struct {
    SondClient *client;
    GtkWidget *main_widget;
    GtkWidget *spin_tage;
    GtkWidget *btn_suchen;
    GtkWidget *progress;
    GtkWidget *info_label;
    GtkWidget *list;
    GPtrArray *zeilen;       /* Zeile* */
    GHashTable *laufende;    /* Library-Name -> Nutzer eines laufenden Jobs */
    guint lock_source;       /* Idle-Quelle des Sperrens, 0 = keine */
    guint lock_index;        /* nächste Zeile, die gesperrt wird */
    guint fehler_folge;      /* aufeinanderfolgende Verbindungsfehler */
} VerwPrivate;

static void zeile_free(Zeile *z) {
    sond_archiv_kandidat_free(z->k);
    g_free(z);
}

static void show_error_dialog(GtkWidget *parent, const gchar *title, const gchar *message) {
    GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", title);

    gtk_alert_dialog_set_detail(dialog, message);
    gtk_alert_dialog_set_modal(dialog, TRUE);
    gtk_alert_dialog_show(dialog, GTK_WINDOW(gtk_widget_get_root(parent)));
    g_object_unref(dialog);
}

/* ------------------------------------------------------------------------
 * Anzeige
 * ---------------------------------------------------------------------- */

static void status_setzen(Zeile *z, const gchar *text) {
    gtk_label_set_text(GTK_LABEL(z->status), text);
}

static void zusammenfassung_aktualisieren(VerwPrivate *priv) {
    guint bereit = 0;
    guint fremd = 0;
    guint fehler = 0;
    gchar *text = NULL;

    for (guint i = 0; i < priv->zeilen->len; i++) {
        Zeile *z = g_ptr_array_index(priv->zeilen, i);

        if (z->zustand == ZEILE_BEREIT)
            bereit++;
        else if (z->zustand == ZEILE_FREMD)
            fremd++;
        else if (z->zustand == ZEILE_FEHLER)
            fehler++;
    }
    text = g_strdup_printf("%u Kandidaten: %u bereit, %u gesperrt oder in Arbeit, %u Fehler",
            priv->zeilen->len, bereit, fremd, fehler);
    gtk_label_set_text(GTK_LABEL(priv->info_label), text);
    g_free(text);
}

/* ------------------------------------------------------------------------
 * Sperren
 * ---------------------------------------------------------------------- */

static void zeile_sperren(VerwPrivate *priv, Zeile *z) {
    GError *error = NULL;
    gchar *von = NULL;
    gchar *seit = NULL;
    gchar *grund = NULL;
    gboolean ok = sond_client_lock_node_ex(priv->client, z->k->node_id,
            SOND_LOCK_REASON_ARCHIVIERUNG, &von, &seit, &grund, &error);

    z->gesperrt = FALSE;
    if (ok) {
        priv->fehler_folge = 0;
        z->gesperrt = TRUE;
        z->zustand = ZEILE_BEREIT;
        status_setzen(z, "bereit");
        gtk_widget_set_sensitive(z->check, TRUE);
    } else if (von && *von) {
        const gchar *job_nutzer = g_hash_table_lookup(priv->laufende, z->k->library_name);
        gchar *text = NULL;

        priv->fehler_folge = 0;
        z->zustand = ZEILE_FREMD;
        if (job_nutzer)
            text = g_strdup_printf("läuft (%s)", job_nutzer);
        else if (grund && g_strcmp0(grund, SOND_LOCK_REASON_BEARBEITUNG))
            text = g_strdup_printf("gesperrt durch %s (%s)", von, grund);
        else
            text = g_strdup_printf("gesperrt durch %s", von);
        status_setzen(z, text);
        g_free(text);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(z->check), FALSE);
        gtk_widget_set_sensitive(z->check, FALSE);
    } else {
        gchar *text = g_strdup_printf("Fehler: %s", error ? error->message : "unbekannt");

        priv->fehler_folge++;
        z->zustand = ZEILE_FEHLER;
        status_setzen(z, text);
        g_free(text);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(z->check), FALSE);
        gtk_widget_set_sensitive(z->check, FALSE);
    }

    g_clear_error(&error);
    g_free(von);
    g_free(seit);
    g_free(grund);
}

static void zeile_entsperren(VerwPrivate *priv, Zeile *z) {
    if (!z->gesperrt)
        return;
    if (!sond_client_unlock_node(priv->client, z->k->node_id, NULL))
        LOG_WARN("Sperre von Node %" G_GINT64_FORMAT " konnte nicht freigegeben werden\n",
                z->k->node_id);
    z->gesperrt = FALSE;
}

static void alle_entsperren(VerwPrivate *priv) {
    for (guint i = 0; i < priv->zeilen->len; i++)
        zeile_entsperren(priv, g_ptr_array_index(priv->zeilen, i));
}

static void sperren_anhalten(VerwPrivate *priv) {
    if (priv->lock_source) {
        g_source_remove(priv->lock_source);
        priv->lock_source = 0;
    }
}

/* Eine Zeile pro Idle-Durchlauf, damit die Oberfläche bedienbar bleibt */
static gboolean sperr_schritt(gpointer data) {
    VerwPrivate *priv = data;

    if (priv->lock_index >= priv->zeilen->len || priv->fehler_folge >= 3) {
        gchar *rest = NULL;

        if (priv->lock_index < priv->zeilen->len) {
            rest = g_strdup_printf("Abbruch nach mehreren Verbindungsfehlern; %u Kandidaten "
                    "ohne Sperre", priv->zeilen->len - priv->lock_index);
            for (guint i = priv->lock_index; i < priv->zeilen->len; i++) {
                Zeile *z = g_ptr_array_index(priv->zeilen, i);

                z->zustand = ZEILE_FEHLER;
                status_setzen(z, "nicht gesperrt (Verbindungsfehler)");
                gtk_check_button_set_active(GTK_CHECK_BUTTON(z->check), FALSE);
                gtk_widget_set_sensitive(z->check, FALSE);
            }
            show_error_dialog(priv->main_widget, "Verbindungsprobleme", rest);
            g_free(rest);
        }
        gtk_widget_set_visible(priv->progress, FALSE);
        gtk_widget_set_sensitive(priv->btn_suchen, TRUE);
        zusammenfassung_aktualisieren(priv);
        priv->lock_source = 0;

        return G_SOURCE_REMOVE;
    }

    {
        Zeile *z = g_ptr_array_index(priv->zeilen, priv->lock_index);
        gchar *text = NULL;

        zeile_sperren(priv, z);
        priv->lock_index++;
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(priv->progress),
                (gdouble) priv->lock_index / (gdouble) priv->zeilen->len);
        text = g_strdup_printf("Sperre %u von %u", priv->lock_index, priv->zeilen->len);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(priv->progress), text);
        g_free(text);
    }

    return G_SOURCE_CONTINUE;
}

/* Haken gesetzt oder entfernt: Sperre holen bzw. freigeben */
static void on_zeile_toggled(GtkCheckButton *check, gpointer user_data) {
    Zeile *z = user_data;
    VerwPrivate *priv = g_object_get_data(G_OBJECT(check), "verw-priv");
    gboolean aktiv = gtk_check_button_get_active(check);

    if (!aktiv && z->zustand == ZEILE_BEREIT) {
        zeile_entsperren(priv, z);
        z->zustand = ZEILE_ABGEWAEHLT;
        status_setzen(z, "abgewählt");
    } else if (aktiv && z->zustand == ZEILE_ABGEWAEHLT) {
        //Wiederwahl: Sperre neu holen (kann inzwischen vergeben sein)
        g_signal_handler_block(check, z->toggled_id);
        zeile_sperren(priv, z);
        g_signal_handler_unblock(check, z->toggled_id);
    }
    zusammenfassung_aktualisieren(priv);
}

/* ------------------------------------------------------------------------
 * Liste aufbauen
 * ---------------------------------------------------------------------- */

static GtkWidget* label_neu(const gchar *text, gint breite, gboolean rechts) {
    GtkWidget *l = gtk_label_new(text);

    gtk_label_set_xalign(GTK_LABEL(l), rechts ? 1.0f : 0.0f);
    gtk_label_set_width_chars(GTK_LABEL(l), breite);
    gtk_label_set_max_width_chars(GTK_LABEL(l), breite > 0 ? breite : -1);
    gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);

    return l;
}

static GtkWidget* kopfzeile_neu(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);

    gtk_widget_set_margin_start(box, 6);
    gtk_widget_set_margin_end(box, 6);
    gtk_box_append(GTK_BOX(box), label_neu("", 2, FALSE));
    gtk_box_append(GTK_BOX(box), label_neu("RegNr", 10, FALSE));
    gtk_box_append(GTK_BOX(box), label_neu("Bezeichnung", 36, FALSE));
    gtk_box_append(GTK_BOX(box), label_neu("Ablagenr", 10, FALSE));
    gtk_box_append(GTK_BOX(box), label_neu("abgelegt", 11, FALSE));
    gtk_box_append(GTK_BOX(box), label_neu("Tage", 6, TRUE));
    gtk_box_append(GTK_BOX(box), label_neu("Zustand", 30, FALSE));
    gtk_widget_add_css_class(box, "heading");

    return box;
}

static Zeile* zeile_neu(VerwPrivate *priv, SondArchivKandidat *k) {
    Zeile *z = g_new0(Zeile, 1);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gchar *regnr = g_strdup_printf("%u/%u", k->lfd_nr, k->year);
    gchar *tage = g_strdup_printf("%d", k->alter_tage);

    z->k = k;
    z->zustand = ZEILE_WARTET;
    gtk_widget_set_margin_start(box, 6);
    gtk_widget_set_margin_end(box, 6);

    z->check = gtk_check_button_new();
    gtk_check_button_set_active(GTK_CHECK_BUTTON(z->check), TRUE);
    gtk_widget_set_sensitive(z->check, FALSE);   //erst nach erfolgreicher Sperre
    g_object_set_data(G_OBJECT(z->check), "verw-priv", priv);
    z->toggled_id = g_signal_connect(z->check, "toggled", G_CALLBACK(on_zeile_toggled), z);
    gtk_box_append(GTK_BOX(box), z->check);

    gtk_box_append(GTK_BOX(box), label_neu(regnr, 10, FALSE));
    gtk_box_append(GTK_BOX(box), label_neu(k->bezeichnung ? k->bezeichnung : "", 36, FALSE));
    gtk_box_append(GTK_BOX(box), label_neu(k->ablnr, 10, FALSE));
    gtk_box_append(GTK_BOX(box), label_neu(k->bis, 11, FALSE));
    gtk_box_append(GTK_BOX(box), label_neu(tage, 6, TRUE));
    z->status = label_neu("wartet auf Sperre", 30, FALSE);
    gtk_box_append(GTK_BOX(box), z->status);

    g_free(regnr);
    g_free(tage);

    z->row = gtk_list_box_row_new();
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(z->row), box);
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(z->row), FALSE);
    gtk_list_box_append(GTK_LIST_BOX(priv->list), z->row);

    return z;
}

static gint kandidaten_vergleich(gconstpointer a, gconstpointer b) {
    const SondArchivKandidat *x = *(SondArchivKandidat* const*) a;
    const SondArchivKandidat *y = *(SondArchivKandidat* const*) b;

    if (x->year != y->year)
        return x->year < y->year ? -1 : 1;
    if (x->lfd_nr != y->lfd_nr)
        return x->lfd_nr < y->lfd_nr ? -1 : 1;

    return 0;
}

static void liste_leeren(VerwPrivate *priv) {
    GtkWidget *child = NULL;

    while ((child = gtk_widget_get_first_child(priv->list)) != NULL)
        gtk_list_box_remove(GTK_LIST_BOX(priv->list), child);
    g_ptr_array_set_size(priv->zeilen, 0);
}

/* Laufende Server-Jobs merken, damit "läuft (Nutzer)" angezeigt werden kann */
static void laufende_jobs_laden(VerwPrivate *priv) {
    GError *error = NULL;
    GPtrArray *jobs = sond_client_archiv_jobs(priv->client, &error);

    g_hash_table_remove_all(priv->laufende);
    for (guint i = 0; jobs && i < jobs->len; i++) {
        SondArchivJob *j = g_ptr_array_index(jobs, i);

        if (j->state == SOND_ARCHIV_RUNNING && j->library_name)
            g_hash_table_replace(priv->laufende, g_strdup(j->library_name),
                    g_strdup(j->user ? j->user : "?"));
    }
    if (jobs)
        g_ptr_array_unref(jobs);
    g_clear_error(&error);   //ältere Server kennen den Endpunkt nicht: dann eben ohne
}

static void on_suchen_clicked(GtkButton *button, VerwPrivate *priv) {
    GError *error = NULL;
    gint tage = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(priv->spin_tage));
    SondGraphNodeSearchCriteria *kriterien = NULL;
    GPtrArray *nodes = NULL;
    GPtrArray *kandidaten = NULL;
    gchar *text = NULL;

    (void) button;

    //vorherige Suche beenden: laufendes Sperren stoppen, Sperren freigeben
    sperren_anhalten(priv);
    alle_entsperren(priv);
    liste_leeren(priv);
    gtk_label_set_text(GTK_LABEL(priv->info_label), "Suche läuft ...");

    kriterien = sond_archiv_suchkriterien();
    nodes = sond_client_search_nodes(priv->client, kriterien, &error);
    sond_graph_node_search_criteria_free(kriterien);
    if (!nodes) {
        gtk_label_set_text(GTK_LABEL(priv->info_label), "Suche fehlgeschlagen");
        show_error_dialog(priv->main_widget, "Suche fehlgeschlagen",
                error ? error->message : "Die Akten konnten nicht gelesen werden.");
        g_clear_error(&error);

        return;
    }

    kandidaten = g_ptr_array_new_with_free_func((GDestroyNotify) sond_archiv_kandidat_free);
    for (guint i = 0; i < nodes->len; i++) {
        SondArchivKandidat *k = sond_archiv_kandidat_pruefen(g_ptr_array_index(nodes, i),
                tage, NULL, NULL);

        if (k)
            g_ptr_array_add(kandidaten, k);
    }
    text = g_strdup_printf("%u abgelegte Akten gefunden, davon %u mit abgelaufener "
            "Überliegedauer (%d Tage)", nodes->len, kandidaten->len, tage);
    g_ptr_array_unref(nodes);

    if (kandidaten->len == 0) {
        gtk_label_set_text(GTK_LABEL(priv->info_label), text);
        g_free(text);
        g_ptr_array_unref(kandidaten);

        return;
    }
    g_free(text);

    g_ptr_array_sort(kandidaten, kandidaten_vergleich);
    laufende_jobs_laden(priv);

    //Zeilen übernehmen die Kandidaten
    for (guint i = 0; i < kandidaten->len; i++) {
        SondArchivKandidat *k = g_ptr_array_index(kandidaten, i);

        g_ptr_array_add(priv->zeilen, zeile_neu(priv, k));
        kandidaten->pdata[i] = NULL;
    }
    g_ptr_array_set_size(kandidaten, 0);
    g_ptr_array_unref(kandidaten);

    //Sperren für alle Kandidaten versuchen, eine Zeile pro Idle-Durchlauf
    priv->lock_index = 0;
    priv->fehler_folge = 0;
    gtk_widget_set_sensitive(priv->btn_suchen, FALSE);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(priv->progress), 0.0);
    gtk_widget_set_visible(priv->progress, TRUE);
    gtk_label_set_text(GTK_LABEL(priv->info_label), "Kandidaten werden gesperrt ...");
    priv->lock_source = g_idle_add(sperr_schritt, priv);
}

/* ------------------------------------------------------------------------
 * Aufbau und Abbau des Moduls
 * ---------------------------------------------------------------------- */

static void on_main_destroy(GtkWidget *widget, VerwPrivate *priv) {
    (void) widget;

    sperren_anhalten(priv);
    alle_entsperren(priv);
    g_ptr_array_unref(priv->zeilen);
    g_hash_table_unref(priv->laufende);
    g_object_unref(priv->client);
    g_free(priv);
}

GtkWidget* sond_module_verwaltung_new(SondClient *client) {
    VerwPrivate *priv = g_new0(VerwPrivate, 1);
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    GtkWidget *titel = gtk_label_new("Abgelegte Akten archivieren");
    GtkWidget *steuer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *scroll = gtk_scrolled_window_new();

    priv->client = g_object_ref(client);
    priv->zeilen = g_ptr_array_new_with_free_func((GDestroyNotify) zeile_free);
    priv->laufende = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    priv->main_widget = main_box;

    gtk_widget_set_margin_start(main_box, 20);
    gtk_widget_set_margin_end(main_box, 20);
    gtk_widget_set_margin_top(main_box, 20);
    gtk_widget_set_margin_bottom(main_box, 20);

    gtk_widget_add_css_class(titel, "heading");
    gtk_widget_set_halign(titel, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(main_box), titel);

    gtk_box_append(GTK_BOX(steuer), gtk_label_new("Überliegedauer in Tagen:"));
    priv->spin_tage = gtk_spin_button_new_with_range(0, 3650, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->spin_tage), SOND_ARCHIV_UEBERLIEGE_STANDARD);
    gtk_widget_set_tooltip_text(priv->spin_tage,
            "Eine abgelegte Akte wird Kandidat, wenn ihre letzte Ablage mindestens "
            "so viele Tage zurückliegt.");
    gtk_box_append(GTK_BOX(steuer), priv->spin_tage);

    priv->btn_suchen = gtk_button_new_with_label("Kandidaten suchen");
    gtk_widget_set_tooltip_text(priv->btn_suchen,
            "Sucht abgelegte Akten und sperrt sie für die Archivierung");
    g_signal_connect(priv->btn_suchen, "clicked", G_CALLBACK(on_suchen_clicked), priv);
    gtk_box_append(GTK_BOX(steuer), priv->btn_suchen);
    gtk_box_append(GTK_BOX(main_box), steuer);

    priv->info_label = gtk_label_new("");
    gtk_widget_set_halign(priv->info_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(main_box), priv->info_label);

    priv->progress = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(priv->progress), TRUE);
    gtk_widget_set_visible(priv->progress, FALSE);
    gtk_box_append(GTK_BOX(main_box), priv->progress);

    gtk_box_append(GTK_BOX(main_box), kopfzeile_neu());

    priv->list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(priv->list), GTK_SELECTION_NONE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), priv->list);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(main_box), scroll);

    g_signal_connect(main_box, "destroy", G_CALLBACK(on_main_destroy), priv);

    return main_box;
}
