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
#include "sond_module_akte.h"
#include "../../sond_graph/sond_graph_property.h"
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
    ZEILE_ABGEWAEHLT,    /* vom Nutzer abgewählt, wieder entsperrt */
    ZEILE_ARCHIVIERT,    /* archiviert und markiert */
    ZEILE_UEBERSPRUNGEN  /* beim Lauf nicht mehr Kandidat (z. B. inzwischen reaktiviert) */
} ZeilenZustand;

/* Schritte des Archivierungslaufs für die aktuelle Akte */
typedef enum {
    PH_NEXT,
    PH_PRUEFEN,
    PH_START,
    PH_POLL,
    PH_MARK
} LaufPhase;

typedef struct {
    SondArchivKandidat *k;
    ZeilenZustand zustand;
    gboolean gesperrt;       /* wir halten die Sperre */
    gboolean behalten;       /* Sperre nicht freigeben (Server-Job könnte noch laufen) */
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
    GtkWidget *btn_archivieren;
    GtkWidget *btn_stopp;
    GtkWidget *progress;
    GtkWidget *info_label;
    GtkWidget *detail_label;     // Beispiele für abgelehnte Akten (markierbar)
    GtkWidget *list;
    GPtrArray *zeilen;       /* Zeile* */
    GHashTable *laufende;    /* Library-Name -> Nutzer eines laufenden Jobs */
    guint lock_source;       /* Idle-Quelle des Sperrens, 0 = keine */
    guint lock_index;        /* nächste Zeile, die gesperrt wird */
    guint fehler_folge;      /* aufeinanderfolgende Verbindungsfehler */

    /* Archivierungslauf */
    gboolean lauf_aktiv;
    gboolean lauf_stopp;     /* nach der laufenden Akte anhalten */
    guint lauf_source;
    LaufPhase phase;
    guint lauf_index;        /* nächste Zeile, die geprüft wird */
    guint lauf_nr;           /* wievielte Akte des Laufs */
    guint lauf_gesamt;
    Zeile *aktuell;
    SondGraphNode *aktuell_node;
    gchar *aktuell_archiv;
    gint64 job_start;        /* Monotonic-Zeit des Job-Starts in us */
    guint start_versuche;
    guint poll_fehler;
    gint tage_suche;         /* Überliegedauer der letzten Suche */
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
    guint fertig = 0;
    gchar *text = NULL;

    for (guint i = 0; i < priv->zeilen->len; i++) {
        Zeile *z = g_ptr_array_index(priv->zeilen, i);

        if (z->zustand == ZEILE_BEREIT)
            bereit++;
        else if (z->zustand == ZEILE_FREMD)
            fremd++;
        else if (z->zustand == ZEILE_FEHLER || z->zustand == ZEILE_UEBERSPRUNGEN)
            fehler++;
        else if (z->zustand == ZEILE_ARCHIVIERT)
            fertig++;
    }
    text = g_strdup_printf("%u Kandidaten: %u bereit, %u archiviert, %u gesperrt oder in Arbeit, "
            "%u Fehler oder übersprungen", priv->zeilen->len, bereit, fertig, fremd, fehler);
    gtk_widget_set_sensitive(priv->btn_archivieren, bereit > 0 && !priv->lauf_aktiv &&
            !priv->lock_source);
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
    if (!z->gesperrt || z->behalten)
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
        priv->lock_source = 0;
        zusammenfassung_aktualisieren(priv);

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
    gint tage = 0;
    SondGraphNodeSearchCriteria *kriterien = NULL;
    GPtrArray *nodes = NULL;
    GPtrArray *kandidaten = NULL;
    gchar *text = NULL;

    (void) button;

    //ein eben getippter Wert im Zahlenfeld gilt erst nach dem Übernehmen
    gtk_spin_button_update(GTK_SPIN_BUTTON(priv->spin_tage));
    tage = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(priv->spin_tage));

    //vorherige Suche beenden: laufendes Sperren stoppen, Sperren freigeben
    sperren_anhalten(priv);
    alle_entsperren(priv);
    liste_leeren(priv);
    gtk_label_set_text(GTK_LABEL(priv->info_label), "Suche läuft ...");
    gtk_widget_set_visible(priv->detail_label, FALSE);
    gtk_widget_set_sensitive(priv->btn_archivieren, FALSE);
    priv->tage_suche = tage;

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
    {
        GHashTable *gruende = g_hash_table_new(g_str_hash, g_str_equal);   // statische Texte -> Anzahl
        GString *s = g_string_new(NULL);
        GString *beispiele = g_string_new(NULL);   // je Ablehnungsgrund ein Beispiel (Tooltip)
        GHashTable *gezeigt = g_hash_table_new(g_str_hash, g_str_equal);
        GHashTableIter it;
        gpointer key = NULL;
        gpointer value = NULL;
        gboolean erster = TRUE;

        for (guint i = 0; i < nodes->len; i++) {
            const gchar *grund = NULL;
            SondArchivKandidat *k = sond_archiv_kandidat_pruefen(g_ptr_array_index(nodes, i),
                    tage, NULL, &grund);

            if (k)
                g_ptr_array_add(kandidaten, k);
            else if (grund) {
                if (!g_hash_table_contains(gezeigt, grund)) {
                    SondGraphNode *nd = g_ptr_array_index(nodes, i);
                    gchar *json = sond_graph_property_list_to_json(
                            sond_graph_node_get_properties(nd));

                    g_string_append_printf(beispiele, "%s\nBeispiel Node %" G_GINT64_FORMAT ": %s\n\n",
                            grund, sond_graph_node_get_id(nd), json);
                    g_free(json);
                    g_hash_table_add(gezeigt, (gpointer) grund);
                }
                g_hash_table_replace(gruende, (gpointer) grund, GUINT_TO_POINTER(
                        GPOINTER_TO_UINT(g_hash_table_lookup(gruende, grund)) + 1));
            }
        }
        g_string_printf(s, "%u abgelegte Akten gefunden, davon %u mit abgelaufener "
                "Überliegedauer (%d Tage)", nodes->len, kandidaten->len, tage);
        g_hash_table_iter_init(&it, gruende);
        while (g_hash_table_iter_next(&it, &key, &value)) {
            g_string_append_printf(s, "%s%u x %s", erster ? ". Nicht übernommen: " : ", ",
                    GPOINTER_TO_UINT(value), (const gchar*) key);
            erster = FALSE;
        }
        text = g_string_free(s, FALSE);
        g_hash_table_unref(gruende);
        gtk_label_set_text(GTK_LABEL(priv->detail_label), beispiele->str);
        gtk_widget_set_visible(priv->detail_label, beispiele->len > 0);
        g_string_free(beispiele, TRUE);
        g_hash_table_unref(gezeigt);
    }
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
 * Archivierungslauf
 * ---------------------------------------------------------------------- */

static void lauf_planen(VerwPrivate *priv, guint ms);

static void lauf_oberflaeche(VerwPrivate *priv, gboolean laeuft) {
    gtk_widget_set_sensitive(priv->list, !laeuft);
    gtk_widget_set_sensitive(priv->spin_tage, !laeuft);
    gtk_widget_set_sensitive(priv->btn_suchen, !laeuft);
    gtk_widget_set_visible(priv->btn_stopp, laeuft);
    gtk_widget_set_sensitive(priv->btn_stopp, laeuft);
    gtk_widget_set_visible(priv->progress, laeuft);
}

static void lauf_ende(VerwPrivate *priv) {
    guint fertig = 0;
    guint probleme = 0;
    gchar *text = NULL;

    for (guint i = 0; i < priv->zeilen->len; i++) {
        Zeile *z = g_ptr_array_index(priv->zeilen, i);

        if (z->zustand == ZEILE_ARCHIVIERT)
            fertig++;
        else if (z->zustand == ZEILE_FEHLER || z->zustand == ZEILE_UEBERSPRUNGEN)
            probleme++;
    }

    priv->lauf_aktiv = FALSE;
    priv->aktuell = NULL;
    g_clear_object(&priv->aktuell_node);
    g_clear_pointer(&priv->aktuell_archiv, g_free);
    lauf_oberflaeche(priv, FALSE);
    zusammenfassung_aktualisieren(priv);

    text = g_strdup_printf("%s: %u archiviert, %u mit Fehler oder übersprungen",
            priv->lauf_stopp ? "Lauf angehalten" : "Lauf beendet", fertig, probleme);
    gtk_label_set_text(GTK_LABEL(priv->info_label), text);
    g_free(text);
    priv->lauf_stopp = FALSE;
}

/* Die aktuelle Akte ist fertig (gut oder schlecht): weiter zur nächsten */
static void lauf_weiter(VerwPrivate *priv) {
    g_clear_object(&priv->aktuell_node);
    g_clear_pointer(&priv->aktuell_archiv, g_free);
    priv->aktuell = NULL;
    priv->phase = PH_NEXT;
    lauf_planen(priv, 10);
}

static void lauf_zeile_fehler(VerwPrivate *priv, Zeile *z, const gchar *text, gboolean entsperren) {
    gchar *s = g_strdup_printf("Fehler: %s", text);

    z->zustand = ZEILE_FEHLER;
    status_setzen(z, s);
    g_free(s);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(z->check), FALSE);
    if (entsperren)
        zeile_entsperren(priv, z);
}

static gboolean lauf_schritt(gpointer data) {
    VerwPrivate *priv = data;
    Zeile *z = priv->aktuell;
    GError *error = NULL;

    priv->lauf_source = 0;

    switch (priv->phase) {
    case PH_NEXT: {
        Zeile *naechste = NULL;
        gchar *text = NULL;

        if (priv->lauf_stopp) {
            lauf_ende(priv);

            return G_SOURCE_REMOVE;
        }
        while (priv->lauf_index < priv->zeilen->len) {
            Zeile *c = g_ptr_array_index(priv->zeilen, priv->lauf_index++);

            if (c->zustand == ZEILE_BEREIT &&
                    gtk_check_button_get_active(GTK_CHECK_BUTTON(c->check))) {
                naechste = c;
                break;
            }
        }
        if (!naechste) {
            lauf_ende(priv);

            return G_SOURCE_REMOVE;
        }
        priv->aktuell = naechste;
        priv->lauf_nr++;
        priv->start_versuche = 0;
        priv->poll_fehler = 0;
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(priv->progress),
                (gdouble) (priv->lauf_nr - 1) / (gdouble) priv->lauf_gesamt);
        text = g_strdup_printf("Akte %u von %u: %u/%u", priv->lauf_nr, priv->lauf_gesamt,
                naechste->k->lfd_nr, naechste->k->year);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(priv->progress), text);
        g_free(text);
        status_setzen(naechste, "prüfe ...");
        priv->phase = PH_PRUEFEN;
        lauf_planen(priv, 10);
        break;
    }

    case PH_PRUEFEN: {
        SondArchivKandidat *frisch = NULL;
        const gchar *grund = NULL;

        //frisch vom Server laden: zwischen Suche und Sperre kann sich etwas geändert haben
        priv->aktuell_node = sond_module_akte_find_by_regnr(priv->client,
                z->k->lfd_nr, z->k->year, &error);
        if (!priv->aktuell_node) {
            lauf_zeile_fehler(priv, z, error ? error->message : "Akte nicht geladen", TRUE);
            g_clear_error(&error);
            lauf_weiter(priv);
            break;
        }
        frisch = sond_archiv_kandidat_pruefen(priv->aktuell_node, priv->tage_suche, NULL, &grund);
        if (!frisch) {
            gchar *s = g_strdup_printf("übersprungen: %s", grund ? grund : "kein Kandidat mehr");

            z->zustand = ZEILE_UEBERSPRUNGEN;
            status_setzen(z, s);
            g_free(s);
            gtk_check_button_set_active(GTK_CHECK_BUTTON(z->check), FALSE);
            zeile_entsperren(priv, z);
            lauf_weiter(priv);
            break;
        }
        priv->aktuell_archiv = g_strdup(frisch->archiv_name);
        sond_archiv_kandidat_free(frisch);
        priv->phase = PH_START;
        lauf_planen(priv, 10);
        break;
    }

    case PH_START:
        if (sond_client_archiv_start(priv->client, z->k->library_name,
                priv->aktuell_archiv, FALSE, &error)) {
            priv->job_start = g_get_monotonic_time();
            status_setzen(z, "archiviere ...");
            priv->phase = PH_POLL;
            lauf_planen(priv, 2000);
        } else if (g_error_matches(error, SOND_CLIENT_ERROR, SOND_CLIENT_ERROR_BUSY)) {
            //anderer Job auf dem Server: warten und erneut versuchen
            status_setzen(z, "wartet (Server belegt)");
            if (priv->lauf_stopp) {
                z->zustand = ZEILE_BEREIT;
                status_setzen(z, "bereit");
                lauf_ende(priv);
            } else {
                lauf_planen(priv, 3000);
            }
        } else {
            lauf_zeile_fehler(priv, z, error ? error->message : "Start fehlgeschlagen", TRUE);
            lauf_weiter(priv);
        }
        g_clear_error(&error);
        break;

    case PH_POLL: {
        SondArchivJob *job = sond_client_archiv_status(priv->client, z->k->library_name, &error);

        if (!job) {
            //Verbindung weg: der Job läuft evtl. weiter, die Sperre bleibt deshalb bestehen
            if (++priv->poll_fehler >= 10) {
                z->behalten = TRUE;
                lauf_zeile_fehler(priv, z, "Stand des Jobs unbekannt - er läuft eventuell "
                        "noch; bitte später prüfen", FALSE);
                g_clear_error(&error);
                lauf_weiter(priv);
                break;
            }
            g_clear_error(&error);
            lauf_planen(priv, 3000);
            break;
        }
        priv->poll_fehler = 0;

        if (job->state == SOND_ARCHIV_RUNNING) {
            gchar *s = g_strdup_printf("archiviere seit %d s ...",
                    (gint) ((g_get_monotonic_time() - priv->job_start) / G_USEC_PER_SEC));

            status_setzen(z, s);
            g_free(s);
            lauf_planen(priv, 2000);
        } else if (job->state == SOND_ARCHIV_OK) {
            priv->phase = PH_MARK;
            lauf_planen(priv, 10);
        } else if (job->state == SOND_ARCHIV_ERROR) {
            lauf_zeile_fehler(priv, z, job->message ? job->message : "Archivierung gescheitert", TRUE);
            lauf_weiter(priv);
        } else if (++priv->start_versuche < 3) {
            //Server kennt den Job nicht mehr (Neustart): Start wiederholen, ist unschädlich
            priv->phase = PH_START;
            lauf_planen(priv, 500);
        } else {
            lauf_zeile_fehler(priv, z, "Job auf dem Server unbekannt", TRUE);
            lauf_weiter(priv);
        }
        sond_archiv_job_free(job);
        break;
    }

    case PH_MARK:
        if (sond_archiv_markieren(priv->aktuell_node, priv->aktuell_archiv) &&
                sond_client_save_node(priv->client, priv->aktuell_node, &error)) {
            z->zustand = ZEILE_ARCHIVIERT;
            status_setzen(z, "archiviert");
            gtk_check_button_set_active(GTK_CHECK_BUTTON(z->check), FALSE);
            gtk_widget_set_sensitive(z->check, FALSE);
            zeile_entsperren(priv, z);
        } else {
            //Archiv ist angelegt, die Library gelöscht; beim nächsten Lauf wird nur noch markiert
            gchar *s = g_strdup_printf("archiviert, Markierung nicht gespeichert: %s",
                    error ? error->message : "unbekannt");

            z->zustand = ZEILE_FEHLER;
            status_setzen(z, s);
            g_free(s);
            gtk_check_button_set_active(GTK_CHECK_BUTTON(z->check), FALSE);
            zeile_entsperren(priv, z);
        }
        g_clear_error(&error);
        lauf_weiter(priv);
        break;
    }

    return G_SOURCE_REMOVE;
}

static void lauf_planen(VerwPrivate *priv, guint ms) {
    priv->lauf_source = g_timeout_add(ms, lauf_schritt, priv);
}

static void lauf_starten(VerwPrivate *priv) {
    guint anzahl = 0;

    for (guint i = 0; i < priv->zeilen->len; i++) {
        Zeile *z = g_ptr_array_index(priv->zeilen, i);

        if (z->zustand == ZEILE_BEREIT &&
                gtk_check_button_get_active(GTK_CHECK_BUTTON(z->check)))
            anzahl++;
    }
    if (anzahl == 0)
        return;

    priv->lauf_aktiv = TRUE;
    priv->lauf_stopp = FALSE;
    priv->lauf_index = 0;
    priv->lauf_nr = 0;
    priv->lauf_gesamt = anzahl;
    priv->phase = PH_NEXT;
    lauf_oberflaeche(priv, TRUE);
    gtk_widget_set_sensitive(priv->btn_archivieren, FALSE);
    gtk_label_set_text(GTK_LABEL(priv->info_label), "Archivierung läuft ...");
    lauf_planen(priv, 10);
}

static void on_archivieren_bestaetigt(GObject *source, GAsyncResult *result, gpointer user_data) {
    VerwPrivate *priv = user_data;
    GError *error = NULL;
    gint button = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result, &error);

    if (error) {
        g_error_free(error);

        return;
    }
    if (button == 0)
        lauf_starten(priv);
}

static void on_archivieren_clicked(GtkButton *button, VerwPrivate *priv) {
    GtkAlertDialog *dialog = gtk_alert_dialog_new("Ausgewählte Akten archivieren?");
    const gchar *buttons[] = { "Archivieren", "Abbrechen", NULL };
    guint anzahl = 0;
    gchar *detail = NULL;

    (void) button;
    for (guint i = 0; i < priv->zeilen->len; i++) {
        Zeile *z = g_ptr_array_index(priv->zeilen, i);

        if (z->zustand == ZEILE_BEREIT &&
                gtk_check_button_get_active(GTK_CHECK_BUTTON(z->check)))
            anzahl++;
    }
    detail = g_strdup_printf("%u Akten werden archiviert.\n\nJeder Dokumentenordner wird als ZIP "
            "in die Library \"%s\" abgelegt und danach auf dem Server gelöscht. Zum "
            "Reaktivieren muss er wiederhergestellt werden, was länger dauern kann.",
            anzahl, "abgelegteAkten");
    gtk_alert_dialog_set_detail(dialog, detail);
    g_free(detail);
    gtk_alert_dialog_set_buttons(dialog, buttons);
    gtk_alert_dialog_set_default_button(dialog, 1);
    gtk_alert_dialog_set_cancel_button(dialog, 1);
    gtk_alert_dialog_set_modal(dialog, TRUE);
    gtk_alert_dialog_choose(dialog, GTK_WINDOW(gtk_widget_get_root(priv->main_widget)),
            NULL, on_archivieren_bestaetigt, priv);
    g_object_unref(dialog);
}

static void on_stopp_clicked(GtkButton *button, VerwPrivate *priv) {
    (void) button;
    priv->lauf_stopp = TRUE;
    gtk_widget_set_sensitive(priv->btn_stopp, FALSE);
    gtk_label_set_text(GTK_LABEL(priv->info_label),
            "Stopp angefordert: nach der laufenden Akte wird angehalten ...");
}

/* ------------------------------------------------------------------------
 * Aufbau und Abbau des Moduls
 * ---------------------------------------------------------------------- */

static void on_main_destroy(GtkWidget *widget, VerwPrivate *priv) {
    (void) widget;

    //läuft gerade ein Server-Job, bleibt die Sperre dieser Akte bestehen (sonst könnte sie
    //mitten im Job reaktiviert werden)
    if (priv->lauf_source) {
        g_source_remove(priv->lauf_source);
        priv->lauf_source = 0;
    }
    if (priv->lauf_aktiv && priv->aktuell && (priv->phase == PH_POLL || priv->phase == PH_MARK))
        priv->aktuell->behalten = TRUE;
    g_clear_object(&priv->aktuell_node);
    g_free(priv->aktuell_archiv);
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

    priv->btn_archivieren = gtk_button_new_with_label("Ausgewählte archivieren");
    gtk_widget_set_tooltip_text(priv->btn_archivieren,
            "Archiviert die bereiten, angehakten Akten nacheinander");
    gtk_widget_set_sensitive(priv->btn_archivieren, FALSE);
    g_signal_connect(priv->btn_archivieren, "clicked", G_CALLBACK(on_archivieren_clicked), priv);
    gtk_box_append(GTK_BOX(steuer), priv->btn_archivieren);

    priv->btn_stopp = gtk_button_new_with_label("Anhalten");
    gtk_widget_set_tooltip_text(priv->btn_stopp, "Hält nach der laufenden Akte an");
    gtk_widget_set_visible(priv->btn_stopp, FALSE);
    g_signal_connect(priv->btn_stopp, "clicked", G_CALLBACK(on_stopp_clicked), priv);
    gtk_box_append(GTK_BOX(steuer), priv->btn_stopp);
    gtk_box_append(GTK_BOX(main_box), steuer);

    priv->info_label = gtk_label_new("");
    gtk_widget_set_halign(priv->info_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(main_box), priv->info_label);

    priv->detail_label = gtk_label_new("");
    gtk_label_set_selectable(GTK_LABEL(priv->detail_label), TRUE);
    gtk_label_set_wrap(GTK_LABEL(priv->detail_label), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(priv->detail_label), PANGO_WRAP_CHAR);
    gtk_label_set_xalign(GTK_LABEL(priv->detail_label), 0.0f);
    gtk_widget_set_halign(priv->detail_label, GTK_ALIGN_START);
    gtk_widget_set_visible(priv->detail_label, FALSE);
    gtk_box_append(GTK_BOX(main_box), priv->detail_label);

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
