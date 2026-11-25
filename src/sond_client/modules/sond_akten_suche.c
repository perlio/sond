/*
 sond (sond_akten_suche.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_akten_suche.h"

#include <string.h>

#include "sond_akten.h"
#include "sond_import.h"
#include "../../sond_graph/sond_graph_db.h"
#include "../../sond_graph/sond_graph_property.h"

/* Zeitgrenze für Suche und Laden einer Akte (Sekunden), damit das Fenster bei
 * toter Verbindung nicht minutenlang steht; andere Serveraufrufe behalten
 * ihre Voreinstellung */
#define SERVER_KURZ_TIMEOUT 5

/* Zustand der Serververbindung für die Suche, einmal je Client (alle Suchfelder teilen ihn) */
typedef struct {
    gint64 server_pause_bis;         /* bis dahin (monotone Zeit) wird der Server nicht erneut versucht */
    gboolean merkliste_abgeglichen;  /* Merkliste in dieser Sitzung auf den Serverstand gekürzt */
} SucheServerState;

/* Daten eines Suchfeldes */
typedef struct {
    SondClient *client;
    GtkWidget *entry;
    SucheServerState *state;
    SondAkteGewaehltFunc gewaehlt;
    SondAkteUnbekanntFunc unbekannt;
    gpointer data;
} SucheCtx;

/* Callback-Daten für Suchergebnis-Dialog */
typedef struct {
    SucheCtx *c;
    GPtrArray *results;
    GtkWidget *dialog;
    GtkWidget *listbox;
    SondSucheServer server;      /* für den Zusatz in der Trefferliste */
} SearchDialogData;

static void on_search_result_selected(GtkListBox *listbox, GtkListBoxRow *row,
        SearchDialogData *data);
static void ergebnisse_zeigen(SucheCtx *c, GPtrArray *results, const gchar *search_term,
        SondSucheServer server);

static SucheServerState* server_state(SondClient *client) {
    SucheServerState *st = g_object_get_data(G_OBJECT(client), "sond-akten-suche-state");

    if (!st) {
        st = g_new0(SucheServerState, 1);
        g_object_set_data_full(G_OBJECT(client), "sond-akten-suche-state", st, g_free);
    }

    return st;
}

static void info_dialog(SucheCtx *c, const gchar *title, const gchar *message) {
    GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", title);
    GtkRoot *root = gtk_widget_get_root(c->entry);

    gtk_alert_dialog_set_detail(dialog, message);
    gtk_alert_dialog_set_modal(dialog, TRUE);
    gtk_alert_dialog_show(dialog, root && GTK_IS_WINDOW(root) ? GTK_WINDOW(root) : NULL);
    g_object_unref(dialog);
}

/* ------------------------------------------------------------------ */
/*  Stil der Eingabefelder                                             */
/* ------------------------------------------------------------------ */

static GtkCssProvider *stil_provider = NULL;
static gint stil_refs = 0;

void sond_akten_suche_stil_ref(void) {
    if (stil_refs++ > 0)
        return;

    stil_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(stil_provider,
            "entry.widget-active {"
            "  background-color: #f0ffd0;"
            "  color: #000000;"
            "}"
            "entry.widget-inactive {"
            "  background-color: #f5f5f5;"
            "  color: #808080;"
            "}");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
            GTK_STYLE_PROVIDER(stil_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
}

void sond_akten_suche_stil_unref(void) {
    if (stil_refs <= 0 || --stil_refs > 0)
        return;

    //der Provider ist global am Display registriert und muss wieder entfernt werden
    gtk_style_context_remove_provider_for_display(gdk_display_get_default(),
            GTK_STYLE_PROVIDER(stil_provider));
    g_clear_object(&stil_provider);
}

void sond_akten_suche_stil_apply(GtkWidget *widget, gboolean active) {
    if (!widget)
        return;

    gtk_widget_remove_css_class(widget, "widget-active");
    gtk_widget_remove_css_class(widget, "widget-inactive");
    gtk_widget_add_css_class(widget, active ? "widget-active" : "widget-inactive");
}

/* ------------------------------------------------------------------ */
/*  Suche                                                              */
/* ------------------------------------------------------------------ */

/**
 * treffer_free:
 * 
 * Gibt Speicher eines Suchergebnisses frei.
 */
static void treffer_free(SondAkteTreffer *result) {
    if (!result) return;
    
    if (result->node) {
        g_object_unref(result->node);
    }
    g_free(result->regnr_display);
    g_free(result->kurzbezeichnung);
    g_free(result->gegenstand);
    g_free(result);
}


/**
 * search_akten_by_property:
 * 
 * Sucht Akten anhand einer Eigenschaft (z. B. "kurzb" oder "ggstd"; serverseitige Suche).
 * Die Datenbank führt die Teilstring-Suche durch.
 * 
 * Returns: (transfer full): Array von SondAkteTreffer oder NULL bei Fehler
 */
static GPtrArray* search_akten_by_property(SucheCtx *c, 
                                            const gchar *key, 
                                            const gchar *search_term, 
                                            GError **error) {
    /* Suche Akten mit Property-Filter auf "kurzb" */
    SondGraphNodeSearchCriteria *criteria = sond_graph_node_search_criteria_new();
    criteria->label = g_strdup("Akte");
    
    /* Wildcard-Pattern für Teilstring-Suche erstellen: *search_term* */
    gchar *pattern = g_strdup_printf("*%s*", search_term);
    
    /* Property-Filter für Kurzbezeichnung (Teilstring-Suche via Wildcards) */
    sond_graph_node_search_criteria_add_property_filter(criteria, key, pattern);
    g_free(pattern);

    criteria->limit = 100;  /* Maximal 100 Ergebnisse */
    
    GPtrArray *nodes = sond_client_search_nodes(c->client, criteria, error);
    sond_graph_node_search_criteria_free(criteria);
    
    if (!nodes) {
        return NULL;
    }
    
    /* Ergebnis-Array erstellen */
    GPtrArray *results = g_ptr_array_new_with_free_func((GDestroyNotify)treffer_free);
    
    /* Alle zurückgegebenen Nodes sind bereits gefiltert */
    for (guint i = 0; i < nodes->len; i++) {
        SondGraphNode *node = g_ptr_array_index(nodes, i);
        
        /* Kurzbezeichnung holen */
        gchar *kurzb = sond_graph_node_get_property_string(node, "kurzb");
        
        /* RegNr extrahieren */
            GPtrArray *regnr_values = sond_graph_node_get_property(node, "regnr");
            if (regnr_values && regnr_values->len == 2) {
                guint year = (guint)g_ascii_strtoull(g_ptr_array_index(regnr_values, 0), NULL, 10);
                guint lfd_nr = (guint)g_ascii_strtoull(g_ptr_array_index(regnr_values, 1), NULL, 10);
                
                /* Suchergebnis erstellen */
                SondAkteTreffer *result = g_new0(SondAkteTreffer, 1);
                result->node = g_object_ref(node);
                result->lfd_nr = lfd_nr;
                result->year = year;
                result->regnr_display = sond_akten_regnr_anzeige(lfd_nr, year);
                result->kurzbezeichnung = kurzb;  /* Ownership übernehmen */
                result->gegenstand = sond_graph_node_get_property_string(node, "ggstd");
                kurzb = NULL;  /* Nicht freigeben */
                
                g_ptr_array_add(results, result);
                
                g_ptr_array_unref(regnr_values);
            }

        g_free(kurzb);
    }
    
    g_ptr_array_unref(nodes);
    
    return results;
}


/* Ergebnis für RegNr suchen oder neu anlegen; bei Treffer gleicher RegNr
 * werden Server- und SeaDrive-Angaben zusammengeführt. */
static SondAkteTreffer* find_or_add_result(GPtrArray *results, guint lfd_nr,
                                             guint year) {
    SondAkteTreffer *r = NULL;

    for (guint i = 0; i < results->len; i++) {
        r = g_ptr_array_index(results, i);
        if (r->lfd_nr == lfd_nr && r->year == year)
            return r;
    }

    r = g_new0(SondAkteTreffer, 1);
    r->lfd_nr = lfd_nr;
    r->year = year;
    r->regnr_display = sond_akten_regnr_anzeige(lfd_nr, year);
    g_ptr_array_add(results, r);

    return r;
}

static gint search_result_compare(gconstpointer a, gconstpointer b) {
    const SondAkteTreffer *ra = *(SondAkteTreffer* const*) a;
    const SondAkteTreffer *rb = *(SondAkteTreffer* const*) b;

    if (ra->year != rb->year)
        return ra->year < rb->year ? 1 : -1;
    if (ra->lfd_nr != rb->lfd_nr)
        return ra->lfd_nr < rb->lfd_nr ? 1 : -1;

    return 0;
}


//Nach einem Fehlschlag wird der Server so lange nicht erneut versucht (Mikrosekunden),
//damit die Suche offline nicht jedes Mal auf die Zeitgrenze wartet
#define SERVER_PAUSE_US (120 * G_USEC_PER_SEC)

/* Verbindung zum Server herstellen (Anmeldung, falls noch keine Sitzung besteht;
 * kurze Zeitgrenze). FALSE: nicht erreichbar oder Anmeldung gescheitert, *fehler
 * (nullable) erhält die Meldung. */
static gboolean server_verbinden(SucheCtx *c, gchar **fehler) {
    GError *error = NULL;
    guint alt = 0;
    gboolean ok = FALSE;

    if (sond_client_is_logged_in(c->client))
        return TRUE;

    if (g_get_monotonic_time() < c->state->server_pause_bis) {
        if (fehler)
            *fehler = g_strdup("Server zuletzt nicht erreichbar");

        return FALSE;
    }

    alt = sond_client_set_io_timeout(c->client, SERVER_KURZ_TIMEOUT);
    ok = sond_client_login(c->client, &error);
    sond_client_set_io_timeout(c->client, alt);

    if (!ok) {
        c->state->server_pause_bis = g_get_monotonic_time() + SERVER_PAUSE_US;
        if (fehler)
            *fehler = g_strdup(error ? error->message : "Anmeldung nicht möglich");
    }
    g_clear_error(&error);

    return ok;
}

/* Kennung der Datenbank des Servers: ein Knoten "Instanz", der beim ersten Bedarf
 * angelegt wird. Neue Volumes bedeuten eine neue Kennung. g_free() oder NULL. */
static gchar* server_instanz_id(SucheCtx *c) {
    SondGraphNodeSearchCriteria *criteria = sond_graph_node_search_criteria_new();
    GPtrArray *nodes = NULL;
    SondGraphNode *best = NULL;
    gchar *id = NULL;
    GError *error = NULL;

    criteria->label = g_strdup("Instanz");
    criteria->limit = 0;
    nodes = sond_client_search_nodes(c->client, criteria, &error);
    sond_graph_node_search_criteria_free(criteria);

    if (!nodes) {
        g_clear_error(&error);

        return NULL;
    }

    //bei Dubletten (gleichzeitig angelegt) gilt der Knoten mit der kleinsten ID
    for (guint i = 0; i < nodes->len; i++) {
        SondGraphNode *node = g_ptr_array_index(nodes, i);

        if (!best || sond_graph_node_get_id(node) < sond_graph_node_get_id(best))
            best = node;
    }
    if (best)
        id = sond_graph_node_get_property_string(best, "uuid");
    g_ptr_array_unref(nodes);

    if (id && *id)
        return id;
    g_free(id);

    {
        SondGraphNode *neu = sond_graph_node_new();

        id = g_uuid_string_random();
        sond_graph_node_set_label(neu, "Instanz");
        sond_graph_node_set_property_string(neu, "uuid", id);
        if (!sond_client_save_node(c->client, neu, &error)) {
            g_free(id);
            id = NULL;
            g_clear_error(&error);
        }
        g_object_unref(neu);
    }

    return id;
}

/* Merkliste auf die Akten kürzen, die der Server kennt (einmal je Sitzung) */
static void merkliste_abgleichen(SucheCtx *c, const gchar *merkliste) {
    SondGraphNodeSearchCriteria *criteria = sond_graph_node_search_criteria_new();
    GPtrArray *nodes = NULL;
    GPtrArray *namen = NULL;
    GError *error = NULL;

    criteria->label = g_strdup("Akte");
    criteria->limit = 0;
    nodes = sond_client_search_nodes(c->client, criteria, &error);
    sond_graph_node_search_criteria_free(criteria);

    if (!nodes) {
        g_clear_error(&error);

        return;
    }

    namen = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; i < nodes->len; i++) {
        GPtrArray *regnr = sond_graph_node_get_property(g_ptr_array_index(nodes, i),
                "regnr");

        if (regnr && regnr->len == 2)
            g_ptr_array_add(namen, sond_akten_library_name(
                    (guint) g_ascii_strtoull(g_ptr_array_index(regnr, 1), NULL, 10),
                    (guint) g_ascii_strtoull(g_ptr_array_index(regnr, 0), NULL, 10)));
        if (regnr)
            g_ptr_array_unref(regnr);
    }

    sond_akten_bereinigen(merkliste, namen);
    c->state->merkliste_abgeglichen = TRUE;

    g_ptr_array_unref(namen);
    g_ptr_array_unref(nodes);
}

/**
 * collect_results:
 *
 * Eine Suche für online und offline. Zuerst wird der Server befragt (mit
 * Anmeldung, falls nötig, und kurzer Zeitgrenze); antwortet er, ist seine
 * Antwort maßgeblich: Libraries im SeaDrive, die er nicht kennt, erscheinen
 * nicht, und die Merkliste wird auf seinen Stand gekürzt. Nur wenn der Server
 * nicht erreichbar ist, gilt die lokale Liste (Libraries im SeaDrive mit der
 * Bezeichnung aus .sond/akte oder der Merkliste). Gleiche Akten erscheinen
 * einmal. @server sagt, ob und wie der Server beteiligt war; bei einem Fehler
 * enthält @fehler (nullable) die Meldung.
 *
 * Returns: (transfer full): Array von SondAkteTreffer, neueste zuerst
 */
static GPtrArray* collect_results(SucheCtx *c, const gchar *text,
                                  SondSucheServer *server, gchar **fehler) {
    GPtrArray *results = g_ptr_array_new_with_free_func(
            (GDestroyNotify)treffer_free);
    gchar *root = sond_client_find_seadrive_root(c->client, NULL);
    gchar *merkliste = sond_akten_merkliste_pfad();
    guint lfd_nr = 0;
    guint year = 0;

    *server = SOND_SUCHE_SERVER_NICHT_BEFRAGT;
    if (fehler)
        *fehler = NULL;

    /* 1. Server zuerst */
    if (server_verbinden(c, fehler)) {
        GError *error = NULL;
        guint alt_timeout = sond_client_set_io_timeout(c->client,
                                                       SERVER_KURZ_TIMEOUT);

        *server = SOND_SUCHE_SERVER_OK;

        if (sond_akten_ist_regnr(text) && sond_akten_parse_regnr(text, &lfd_nr, &year)) {
            SondGraphNode *node = sond_akten_suche_node_by_regnr(c->client, lfd_nr, year, &error);

            if (node) {
                SondAkteTreffer *r = find_or_add_result(results, lfd_nr, year);
                gchar *kurzb = sond_graph_node_get_property_string(node, "kurzb");

                if (r->node)
                    g_object_unref(r->node);
                r->node = node;
                if (kurzb) {
                    g_free(r->kurzbezeichnung);
                    r->kurzbezeichnung = kurzb;
                }
                g_free(r->gegenstand);
                r->gegenstand = sond_graph_node_get_property_string(node, "ggstd");
            } else if (error && error->code != G_IO_ERROR_NOT_FOUND) {
                *server = SOND_SUCHE_SERVER_FEHLER;
                if (fehler)
                    *fehler = g_strdup(error->message);
            }
        } else {
            //Suchtext in Kurzbezeichnung und in Gegenstand
            static const gchar *felder[] = { "kurzb", "ggstd" };

            for (guint k = 0; k < G_N_ELEMENTS(felder) && *server == SOND_SUCHE_SERVER_OK; k++) {
                GPtrArray *srv = search_akten_by_property(c, felder[k], text,
                                                          &error);

                if (srv) {
                    for (guint i = 0; i < srv->len; i++) {
                        SondAkteTreffer *s = g_ptr_array_index(srv, i);
                        SondAkteTreffer *r = find_or_add_result(results, s->lfd_nr,
                                                                 s->year);

                        if (r->node)
                            g_object_unref(r->node);
                        r->node = s->node ? g_object_ref(s->node) : NULL;
                        if (s->kurzbezeichnung) {
                            g_free(r->kurzbezeichnung);
                            r->kurzbezeichnung = g_strdup(s->kurzbezeichnung);
                        }
                        g_free(r->gegenstand);
                        r->gegenstand = g_strdup(s->gegenstand);
                    }
                    g_ptr_array_unref(srv);
                } else if (error) {
                    *server = SOND_SUCHE_SERVER_FEHLER;
                    if (fehler)
                        *fehler = g_strdup(error->message);
                }
                g_clear_error(&error);
            }
        }
        g_clear_error(&error);

        if (*server == SOND_SUCHE_SERVER_OK) {
            //Die Merkliste gehört zu genau diesem Server (Kennung der Datenbank)
            //und enthält nur, was er kennt
            if (!c->state->merkliste_abgeglichen) {
                gchar *instanz = server_instanz_id(c);

                if (instanz) {
                    sond_akten_instanz_pruefen(merkliste, instanz);
                    g_free(instanz);
                }
                merkliste_abgleichen(c, merkliste);
            }

            for (guint i = 0; i < results->len; i++) {
                SondAkteTreffer *r = g_ptr_array_index(results, i);
                gchar *lib = sond_akten_library_name(r->lfd_nr, r->year);

                //Bezeichnungen vom Server auch für die Offline-Suche merken
                if (r->node && r->kurzbezeichnung)
                    sond_akten_merken(merkliste, lib, r->kurzbezeichnung,
                                      r->gegenstand ? r->gegenstand : "");

                //liegt die Library auch im SeaDrive?
                if (root) {
                    gchar *dir = sond_import_find_library_dir(root, lib, NULL);

                    r->lokal = (dir != NULL);
                    g_free(dir);
                }
                g_free(lib);
            }
        } else {
            c->state->server_pause_bis = g_get_monotonic_time() + SERVER_PAUSE_US;
        }

        sond_client_set_io_timeout(c->client, alt_timeout);
    } else {
        *server = SOND_SUCHE_SERVER_FEHLER;
    }

    /* 2. Rückfall: der Server hat nicht geantwortet, dann gilt die lokale Liste */
    if (*server != SOND_SUCHE_SERVER_OK && root) {
        GPtrArray *alle = sond_akten_scan(root, merkliste, NULL);

        if (alle) {
            GPtrArray *treffer = sond_akten_suchen(alle, text);

            for (guint i = 0; i < treffer->len; i++) {
                SondAkteEintrag *e = g_ptr_array_index(treffer, i);
                SondAkteTreffer *r = find_or_add_result(results, e->lfd_nr,
                                                         e->year);

                r->lokal = TRUE;
                if (!r->kurzbezeichnung && e->bezeichnung)
                    r->kurzbezeichnung = g_strdup(e->bezeichnung);
                if (!r->gegenstand && e->gegenstand)
                    r->gegenstand = g_strdup(e->gegenstand);
            }
            g_ptr_array_unref(treffer);
            g_ptr_array_unref(alle);
        }
    }

    g_free(root);
    g_free(merkliste);
    g_ptr_array_sort(results, search_result_compare);

    return results;
}


/**
 * show_akte_search_results:
 * 
 * Zeigt Suchergebnisse in einem Dialog an.
 */
static void ergebnisse_zeigen(SucheCtx *c, GPtrArray *results, const gchar *search_term, SondSucheServer server) {
    if (!results || results->len == 0) {
        gchar *msg = g_strdup_printf(
            "Keine Akten gefunden mit Suchbegriff: \"%s\"",
            search_term);
        info_dialog(c, "Keine Ergebnisse", msg);
        g_free(msg);
        return;
    }
    
    /* Dialog erstellen */
    GtkWidget *dialog = gtk_window_new();
    gchar *title = g_strdup_printf("Aktensuche: \"%s\" (%u Ergebnisse)%s", search_term, results->len, server == SOND_SUCHE_SERVER_FEHLER ? " - Server nicht erreichbar, nur lokale Treffer" : "");
    gtk_window_set_title(GTK_WINDOW(dialog), title);
    g_free(title);
    
    gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 400);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), 
                                 GTK_WINDOW(gtk_widget_get_root(c->entry)));
    
    /* ScrolledWindow für Liste */
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_window_set_child(GTK_WINDOW(dialog), scrolled);
    
    /* ListBox für Ergebnisse */
    GtkWidget *listbox = gtk_list_box_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), listbox);
    
    /* Callback-Daten */
    SearchDialogData *data = g_new0(SearchDialogData, 1);
    data->c = c;
    data->results = g_ptr_array_ref(results);  /* Referenz erhöhen */
    data->dialog = dialog;
    data->listbox = listbox;
    data->server = server;
    
    /* Ergebnisse in ListBox einfügen */
    for (guint i = 0; i < results->len; i++) {
        SondAkteTreffer *result = g_ptr_array_index(results, i);
        
        gchar *label_text = g_strdup_printf("%s - %s%s%s%s", result->regnr_display,
                                           result->kurzbezeichnung ? result->kurzbezeichnung : "",
                                           (result->gegenstand && *result->gegenstand) ? "  |  " : "",
                                           result->gegenstand ? result->gegenstand : "",
                                           result->node ? "" : (server == SOND_SUCHE_SERVER_OK ? "   (nur im SeaDrive)" :
                                               server == SOND_SUCHE_SERVER_FEHLER ? "   (Server nicht erreichbar)" : ""));
        GtkWidget *label = gtk_label_new(label_text);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);  /* Links ausrichten */
        gtk_widget_set_margin_start(label, 10);
        gtk_widget_set_margin_end(label, 10);
        gtk_widget_set_margin_top(label, 5);
        gtk_widget_set_margin_bottom(label, 5);
        g_free(label_text);
        
        gtk_list_box_append(GTK_LIST_BOX(listbox), label);
    }
    
    /* Signal verbinden */
    g_signal_connect(listbox, "row-activated", 
                    G_CALLBACK(on_search_result_selected), data);
    
    /* Cleanup bei Dialog-Schließung */
    g_signal_connect_swapped(dialog, "destroy", 
                             G_CALLBACK(g_ptr_array_unref), data->results);
    g_signal_connect_swapped(dialog, "destroy", 
                             G_CALLBACK(g_free), data);
    
    gtk_window_present(GTK_WINDOW(dialog));
}



/**
 * on_search_result_selected:
 *
 * Callback wenn Benutzer ein Suchergebnis auswählt (Doppelklick oder Enter).
 */
static void on_search_result_selected(GtkListBox *listbox, GtkListBoxRow *row,
        SearchDialogData *data) {
    SondAkteTreffer *result = NULL;
    gint index = 0;

    (void) listbox;

    if (!row)
        return;

    index = gtk_list_box_row_get_index(row);
    if (index < 0 || index >= (gint) data->results->len)
        return;

    result = g_ptr_array_index(data->results, index);
    if (data->c->gewaehlt)
        data->c->gewaehlt(result, data->server, data->c->data);

    /* Dialog schließen */
    gtk_window_close(GTK_WINDOW(data->dialog));
}

/* Nachfrage nach einer Suche ohne Treffer, wenn der Server nicht geantwortet
 * hat: erneut suchen? */
typedef struct {
    SucheCtx *c;
    gchar *such;
} ServerSucheData;

static void such_ausfuehren(SucheCtx *c, const gchar *such);

static void on_server_suche_response(GObject *source, GAsyncResult *result,
        gpointer user_data) {
    ServerSucheData *d = user_data;
    GError *error = NULL;
    int button = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result, &error);

    g_clear_error(&error);

    if (button == 0) {
        d->c->state->server_pause_bis = 0;   //ausdrücklich noch einmal versuchen
        such_ausfuehren(d->c, d->such);
    }

    g_free(d->such);
    g_free(d);
}

static void frage_server_suche(SucheCtx *c, const gchar *such, const gchar *fehler) {
    GtkAlertDialog *dialog = gtk_alert_dialog_new("Server nicht erreichbar");
    ServerSucheData *d = g_new0(ServerSucheData, 1);
    gchar *detail = g_strdup_printf("Im SeaDrive-Laufwerk wurde nichts gefunden, "
            "und der Server hat nicht geantwortet (%s). Erneut suchen?",
            fehler ? fehler : "unbekannter Fehler");
    const char *buttons[] = {"Erneut suchen", "Abbrechen", NULL};
    GtkRoot *root = gtk_widget_get_root(c->entry);

    d->c = c;
    d->such = g_strdup(such);

    gtk_alert_dialog_set_detail(dialog, detail);
    g_free(detail);
    gtk_alert_dialog_set_buttons(dialog, buttons);
    gtk_alert_dialog_set_default_button(dialog, 0);
    gtk_alert_dialog_set_cancel_button(dialog, 1);
    gtk_alert_dialog_set_modal(dialog, TRUE);

    gtk_alert_dialog_choose(dialog, root && GTK_IS_WINDOW(root) ? GTK_WINDOW(root) : NULL,
            NULL, on_server_suche_response, d);
    g_object_unref(dialog);
}

/* Suche ausführen: RegNr oder Suchtext, zuerst auf dem Server (mit Anmeldung),
 * nur bei Nichterreichbarkeit im SeaDrive (s. collect_results). Ein einziger
 * Treffer wird gleich gewählt, bei mehreren erscheint die Trefferliste. Ohne
 * Treffer und ohne Antwort des Servers wird gefragt, ob erneut gesucht werden soll. */
static void such_ausfuehren(SucheCtx *c, const gchar *such) {
    guint lfd_nr = 0;
    guint year = 0;
    gchar *fehler = NULL;
    SondSucheServer server = SOND_SUCHE_SERVER_NICHT_BEFRAGT;
    GPtrArray *results = collect_results(c, such, &server, &fehler);

    if (results->len == 1) {
        SondAkteTreffer *r = g_ptr_array_index(results, 0);

        if (c->gewaehlt)
            c->gewaehlt(r, server, c->data);
    } else if (results->len > 1) {
        ergebnisse_zeigen(c, results, such, server);
    } else if (server != SOND_SUCHE_SERVER_OK) {
        frage_server_suche(c, such, fehler);
    } else if (c->unbekannt && sond_akten_ist_regnr(such) &&
            sond_akten_parse_regnr(such, &lfd_nr, &year)) {
        c->unbekannt(lfd_nr, year, c->data);
    } else {
        gchar *msg = g_strdup_printf("Keine Akten gefunden mit Suchbegriff: \"%s\"", such);

        info_dialog(c, "Keine Ergebnisse", msg);
        g_free(msg);
    }

    g_free(fehler);
    g_ptr_array_unref(results);
}

/* ------------------------------------------------------------------ */
/*  Das Suchfeld                                                       */
/* ------------------------------------------------------------------ */

/* Enter im Suchfeld */
static void on_entry_activate(GtkEntry *entry, gpointer user_data) {
    SucheCtx *c = user_data;
    gchar *such = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(entry))));

    if (*such)
        such_ausfuehren(c, such);
    g_free(such);
}

static void ctx_free(gpointer data) {
    SucheCtx *c = data;

    g_clear_object(&c->client);
    g_free(c);
    sond_akten_suche_stil_unref();
}

GtkWidget* sond_akten_suche_new(SondClient *client) {
    GtkWidget *entry = gtk_entry_new();
    SucheCtx *c = g_new0(SucheCtx, 1);

    c->client = g_object_ref(client);
    c->entry = entry;
    c->state = server_state(client);

    sond_akten_suche_stil_ref();
    sond_akten_suche_stil_apply(entry, TRUE);
    gtk_editable_set_width_chars(GTK_EDITABLE(entry), 30);
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Registernummer oder Suchtext, dann Enter");
    g_signal_connect(entry, "activate", G_CALLBACK(on_entry_activate), c);
    g_object_set_data_full(G_OBJECT(entry), "sond-akten-suche", c, ctx_free);

    return entry;
}

void sond_akten_suche_set_funcs(GtkWidget *suche, SondAkteGewaehltFunc gewaehlt,
        SondAkteUnbekanntFunc unbekannt, gpointer user_data) {
    SucheCtx *c = g_object_get_data(G_OBJECT(suche), "sond-akten-suche");

    g_return_if_fail(c != NULL);

    c->gewaehlt = gewaehlt;
    c->unbekannt = unbekannt;
    c->data = user_data;
}

void sond_akten_suche_set_aktiv(GtkWidget *suche, gboolean aktiv) {
    gtk_widget_set_sensitive(suche, aktiv);
    sond_akten_suche_stil_apply(suche, aktiv);
}

void sond_akten_suche_fokus(GtkWidget *suche) {
    //nur wenn das Feld bedienbar ist; sonst bleibt der Fokus, wo er ist
    if (gtk_widget_get_sensitive(suche))
        gtk_widget_grab_focus(suche);
}

/* ------------------------------------------------------------------ */
/*  Akte zur Registernummer                                            */
/* ------------------------------------------------------------------ */

SondGraphNode* sond_akten_suche_node_by_regnr(SondClient *client, guint lfd_nr, guint year,
        GError **error) {
    SondGraphNodeSearchCriteria *criteria = sond_graph_node_search_criteria_new();
    gchar *year_str = g_strdup_printf("%u", year);
    gchar *lfdnr_str = g_strdup_printf("%u", lfd_nr);
    GPtrArray *nodes = NULL;
    SondGraphNode *result_node = NULL;

    criteria->label = g_strdup("Akte");

    //zwei Filter auf das Array "regnr": Jahr (Index 0) und laufende Nummer (Index 1)
    g_ptr_array_add(criteria->property_filters,
            sond_property_filter_new_with_index("regnr", year_str, 0));
    g_ptr_array_add(criteria->property_filters,
            sond_property_filter_new_with_index("regnr", lfdnr_str, 1));
    g_free(year_str);
    g_free(lfdnr_str);

    criteria->limit = 100;

    //Client-Methode verwenden (mit Auto-Login und Re-Login bei 401)
    nodes = sond_client_search_nodes(client, criteria, error);
    sond_graph_node_search_criteria_free(criteria);

    if (!nodes)
        return NULL;

    //ersten passenden Node finden
    for (guint i = 0; i < nodes->len; i++) {
        SondGraphNode *node = g_ptr_array_index(nodes, i);
        GPtrArray *regnr_values = sond_graph_node_get_property(node, "regnr");

        if (regnr_values && regnr_values->len == 2) {
            guint node_year = (guint) g_ascii_strtoull(g_ptr_array_index(regnr_values, 0), NULL, 10);
            guint node_lfd = (guint) g_ascii_strtoull(g_ptr_array_index(regnr_values, 1), NULL, 10);

            if (node_lfd == lfd_nr && node_year == year) {
                result_node = g_object_ref(node);
                g_ptr_array_unref(regnr_values);
                break;
            }
        }
        if (regnr_values)
            g_ptr_array_unref(regnr_values);
    }

    g_ptr_array_unref(nodes);

    if (!result_node)
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                "Akte mit RegNr %u/%u nicht gefunden", lfd_nr, year);

    return result_node;
}
