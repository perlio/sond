/*
 sond (sond_module_akte.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_module_akte.h"
#include "../sond_client.h"
#include "../sond_offline_manager.h"
#include "../sond_seafile_sync.h"
#include "../../sond_log_and_error.h"
#include "../../sond_graph/sond_graph_node.h"
#include "../../sond_graph/sond_graph_property.h"
#include "../../sond_graph/sond_graph_db.h"

#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

typedef enum {
    AKTE_STATE_INITIAL,       /* Nur "Neue Akte" + RegNr Entry aktiv */
    AKTE_STATE_CREATING,      /* Neue Akte wird erstellt (Entry insensitiv) */
    AKTE_STATE_EDITING,       /* Bestehende/Entry-Akte wird bearbeitet */
    AKTE_STATE_READONLY,      /* Akte ist gelockt von anderem User */
    AKTE_STATE_ABGELEGT       /* Akte ist abgelegt (read-only mit Reaktivierungs-Option) */
} AkteState;

typedef struct {
    SondClient *client;
    SondGraphNode *current_node;
    AkteState state;
    gboolean is_new_from_entry;    /* TRUE wenn über Entry angelegt */
    
    /* UI-Elemente */
    GtkWidget *main_widget;
    GtkWidget *regnr_entry;        /* Format: "23/26" */
    GtkWidget *entry_kurzbezeichnung;
    GtkWidget *textview_gegenstand;
    
    GtkWidget *btn_neue_akte;
    GtkWidget *btn_ok;
    GtkWidget *btn_speichern;
    GtkWidget *btn_abbrechen;
    
    GtkWidget *akte_fields_box;    /* Container für Akte-Felder */
    
    /* Ablage/Reaktivierung */
    GtkWidget *status_label;       /* Zeigt "Aktiv" oder "Abgelegt" */
    GtkWidget *btn_ablegen;        /* "Akte ablegen" */
    GtkWidget *btn_reaktivieren;   /* "Akte reaktivieren" */
    
    /* Offline-Verfügbarkeit */
    GtkWidget *offline_toggle;     /* Toggle Button für Offline */

    /* OCR */                           // NEU
    GtkWidget *btn_ocr;                 // NEU
    gchar *library_name;                // NEU - wird bei update_ui_from_node gesetzt
    GtkCssProvider *css_provider;
} SondModuleAktePrivate;

/* Struktur für Suchergebnis */
typedef struct {
    SondGraphNode *node;
    gchar *regnr_display;   /* Format: "23/26" */
    gchar *kurzbezeichnung;
} AkteSearchResult;

/* Forward declarations */
static void set_akte_state(SondModuleAktePrivate *priv, AkteState new_state);
static void update_ui_from_node(SondModuleAktePrivate *priv);
static void clear_akte_fields(SondModuleAktePrivate *priv);
static void update_status_display(SondModuleAktePrivate *priv);
static void on_offline_toggle_toggled(GtkCheckButton *toggle, SondModuleAktePrivate *priv);
static void suggest_last_regnr(SondModuleAktePrivate *priv);
static void apply_widget_style(GtkWidget *widget, gboolean active);
static gboolean ist_akte_aktiv(SondGraphNode *node);
static void show_akte_search_results(SondModuleAktePrivate *priv, GPtrArray *results, const gchar *search_term);
static void akte_search_result_free(AkteSearchResult *result);
static GPtrArray* search_akten_by_kurzbezeichnung(SondModuleAktePrivate *priv, const gchar *search_term, GError **error);

extern const gchar* sond_client_get_server_url(SondClient *client);
extern SoupSession* sond_client_get_session(SondClient *client);

/* ========================================================================
 * CSS Styling für aktive/inaktive Widgets
 * ======================================================================== */

/**
 * setup_css_provider:
 *
 * Erstellt und registriert den CSS Provider für Widget-Styling
 */
static GtkCssProvider* setup_css_provider(void) {
    GtkCssProvider *provider = gtk_css_provider_new();

    const gchar *css_data =
        "entry.widget-active {"
        "  background-color: #f0ffd0;"
        "  color: #000000;"  // ← WICHTIG
        "}"
        "entry.widget-inactive {"
        "  background-color: #f5f5f5;"
        "  color: #808080;"  // ← WICHTIG
        "}";

    gtk_css_provider_load_from_string(provider, css_data);

    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_USER  // ← WICHTIG
    );

    return provider;
}

/**
 * apply_widget_style:
 *
 * Wendet CSS-Klasse auf Widget an je nach aktivem/inaktivem Zustand
 */
static void apply_widget_style(GtkWidget *widget, gboolean active) {
    if (!widget) {
        return;
    }

    /* Alte Klassen entfernen */
    gtk_widget_remove_css_class(widget, "widget-active");
    gtk_widget_remove_css_class(widget, "widget-inactive");

    /* Neue Klasse hinzufügen */
    if (active) {
        gtk_widget_add_css_class(widget, "widget-active");
    } else {
        gtk_widget_add_css_class(widget, "widget-inactive");
    }
}

/* ========================================================================
 * UI State Management
 * ======================================================================== */

static void set_akte_state(SondModuleAktePrivate *priv, AkteState new_state) {
    priv->state = new_state;

    switch (new_state) {
        case AKTE_STATE_INITIAL:
            /* Nur "Neue Akte" + Entry aktiv */
            gtk_widget_set_sensitive(priv->regnr_entry, TRUE);
            apply_widget_style(priv->regnr_entry, TRUE);

            gtk_widget_set_sensitive(priv->btn_neue_akte, TRUE);
            gtk_widget_set_sensitive(priv->akte_fields_box, FALSE);

            apply_widget_style(priv->entry_kurzbezeichnung, FALSE);
            apply_widget_style(priv->textview_gegenstand, FALSE);
            apply_widget_style(priv->offline_toggle, FALSE);

            gtk_widget_set_sensitive(priv->btn_ok, FALSE);
            gtk_widget_set_sensitive(priv->btn_speichern, FALSE);
            gtk_widget_set_sensitive(priv->btn_abbrechen, TRUE);
            break;

        case AKTE_STATE_CREATING:
            /* Entry insensitiv, Felder editierbar */
            gtk_widget_set_sensitive(priv->regnr_entry, FALSE);
            apply_widget_style(priv->regnr_entry, FALSE);

            gtk_widget_set_sensitive(priv->btn_neue_akte, FALSE);
            gtk_widget_set_sensitive(priv->akte_fields_box, TRUE);

            apply_widget_style(priv->entry_kurzbezeichnung, TRUE);
            apply_widget_style(priv->textview_gegenstand, TRUE);
            apply_widget_style(priv->offline_toggle, TRUE);

            gtk_widget_set_sensitive(priv->btn_ok, TRUE);
            gtk_widget_set_sensitive(priv->btn_speichern, TRUE);
            gtk_widget_set_sensitive(priv->btn_abbrechen, TRUE);
            break;

        case AKTE_STATE_EDITING:
            /* Alles editierbar außer Entry */
            gtk_widget_set_sensitive(priv->regnr_entry, FALSE);
            apply_widget_style(priv->regnr_entry, FALSE);

            gtk_widget_set_sensitive(priv->btn_neue_akte, FALSE);
            gtk_widget_set_sensitive(priv->akte_fields_box, TRUE);

            apply_widget_style(priv->entry_kurzbezeichnung, TRUE);
            apply_widget_style(priv->textview_gegenstand, TRUE);
            apply_widget_style(priv->offline_toggle, TRUE);

            gtk_widget_set_sensitive(priv->btn_ok, TRUE);
            gtk_widget_set_sensitive(priv->btn_speichern, TRUE);
            gtk_widget_set_sensitive(priv->btn_abbrechen, TRUE);
            break;

        case AKTE_STATE_READONLY:
            /* Nur Ansicht */
            gtk_widget_set_sensitive(priv->regnr_entry, FALSE);
            apply_widget_style(priv->regnr_entry, FALSE);

            gtk_widget_set_sensitive(priv->btn_neue_akte, FALSE);
            gtk_widget_set_sensitive(priv->akte_fields_box, FALSE);

            apply_widget_style(priv->entry_kurzbezeichnung, FALSE);
            apply_widget_style(priv->textview_gegenstand, FALSE);
            apply_widget_style(priv->offline_toggle, FALSE);

            gtk_widget_set_sensitive(priv->btn_ok, TRUE);
            gtk_widget_set_sensitive(priv->btn_speichern, FALSE);
            gtk_widget_set_sensitive(priv->btn_abbrechen, TRUE);
            break;

        case AKTE_STATE_ABGELEGT:
            /* Abgelegte Akte: Felder sichtbar aber nicht editierbar, Reaktivierung möglich */
            gtk_widget_set_sensitive(priv->regnr_entry, FALSE);
            apply_widget_style(priv->regnr_entry, FALSE);

            gtk_widget_set_sensitive(priv->btn_neue_akte, FALSE);
            gtk_widget_set_sensitive(priv->akte_fields_box, FALSE);

            apply_widget_style(priv->entry_kurzbezeichnung, FALSE);
            apply_widget_style(priv->textview_gegenstand, FALSE);
            apply_widget_style(priv->offline_toggle, FALSE);

            gtk_widget_set_sensitive(priv->btn_ok, TRUE);
            gtk_widget_set_sensitive(priv->btn_speichern, FALSE);
            gtk_widget_set_sensitive(priv->btn_abbrechen, TRUE);
            gtk_widget_set_sensitive(priv->btn_reaktivieren, TRUE);
            break;
    }
}

static void clear_akte_fields(SondModuleAktePrivate *priv) {
    gtk_editable_set_text(GTK_EDITABLE(priv->regnr_entry), "");
    gtk_editable_set_text(GTK_EDITABLE(priv->entry_kurzbezeichnung), "");
    gtk_editable_set_text(GTK_EDITABLE(priv->textview_gegenstand), "");  /* Jetzt Entry */
    
    /* Status zurücksetzen */
	gtk_label_set_text(GTK_LABEL(priv->status_label), "Status: -");
	gtk_widget_set_visible(priv->btn_ablegen, FALSE);
	gtk_widget_set_visible(priv->btn_reaktivieren, FALSE);

    /* Offline-Toggle zurücksetzen */
	if (priv->offline_toggle) {
		g_signal_handlers_block_by_func(priv->offline_toggle, on_offline_toggle_toggled, priv);
		gtk_check_button_set_active(GTK_CHECK_BUTTON(priv->offline_toggle), FALSE);
		g_signal_handlers_unblock_by_func(priv->offline_toggle, on_offline_toggle_toggled, priv);
		gtk_widget_set_visible(priv->offline_toggle, FALSE);
	}

    /* Library-Name löschen */
    g_free(priv->library_name);
    priv->library_name = NULL;

    /* OCR-Button verstecken */
	gtk_widget_set_visible(priv->btn_ocr, FALSE);
}

/* ========================================================================
 * Helper Functions
 * ======================================================================== */

static void show_error_dialog(GtkWidget *parent, const gchar *title, const gchar *message) {
    GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", title);
    gtk_alert_dialog_set_detail(dialog, message);
    gtk_alert_dialog_set_modal(dialog, TRUE);
    
    gtk_alert_dialog_show(dialog, GTK_WINDOW(gtk_widget_get_root(parent)));
    g_object_unref(dialog);
}

static void show_info_dialog(GtkWidget *parent, const gchar *title, const gchar *message) {
    GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", title);
    gtk_alert_dialog_set_detail(dialog, message);
    gtk_alert_dialog_set_modal(dialog, TRUE);
    
    gtk_alert_dialog_show(dialog, GTK_WINDOW(gtk_widget_get_root(parent)));
    g_object_unref(dialog);
}

static gboolean parse_regnr(const gchar *regnr_str, guint *lfd_nr, guint *year) {
    if (!regnr_str || strlen(regnr_str) == 0) {
        return FALSE;
    }
    
    /* Fall 1: Format mit "/" (z.B. "12/26") */
    if (strchr(regnr_str, '/')) {
        gchar **parts = g_strsplit(regnr_str, "/", 2);
        if (g_strv_length(parts) != 2) {
            g_strfreev(parts);
            return FALSE;
        }
        
        *lfd_nr = (guint)g_ascii_strtoull(parts[0], NULL, 10);
        *year = (guint)g_ascii_strtoull(parts[1], NULL, 10);
        
        if (*year < 100) {
            *year += 2000;
        }
        
        g_strfreev(parts);
        return (*lfd_nr > 0 && *year >= 2000);
    }
    
    /* Fall 2: Nur Ziffern (z.B. "1226", "126", "12326") */
    /* Mindestens 3 Ziffern, letzte 2 = Jahr, Rest = lfd_nr */
    gsize len = strlen(regnr_str);
    if (len < 3) {
        return FALSE;
    }
    
    /* Prüfen ob alle Zeichen Ziffern sind */
    for (gsize i = 0; i < len; i++) {
        if (!g_ascii_isdigit(regnr_str[i])) {
            return FALSE;
        }
    }
    
    /* Letzte 2 Ziffern = Jahr */
    gchar *year_str = g_strndup(regnr_str + (len - 2), 2);
    *year = (guint)g_ascii_strtoull(year_str, NULL, 10);
    g_free(year_str);
    
    if (*year < 100) {
        *year += 2000;
    }
    
    /* Rest = lfd. Nr. */
    gchar *lfd_str = g_strndup(regnr_str, len - 2);
    *lfd_nr = (guint)g_ascii_strtoull(lfd_str, NULL, 10);
    g_free(lfd_str);
    
    return (*lfd_nr > 0 && *year >= 2000);
}

static gchar* format_regnr(guint lfd_nr, guint year) {
    guint short_year = year % 100;
    return g_strdup_printf("%u/%02u", lfd_nr, short_year);
}

/**
 * format_regnr_storage:
 *
 * Formatiert RegNr für Speicherung/Pfade: "2026-1"
 * Verwendet NICHT "/" sondern "-" um Pfadprobleme zu vermeiden.
 */
static gchar* format_regnr_storage(guint lfd_nr, guint year) {
    return g_strdup_printf("%u-%u", year, lfd_nr);
}

/**
 * akte_search_result_free:
 * 
 * Gibt Speicher eines Suchergebnisses frei.
 */
static void akte_search_result_free(AkteSearchResult *result) {
    if (!result) return;
    
    if (result->node) {
        g_object_unref(result->node);
    }
    g_free(result->regnr_display);
    g_free(result->kurzbezeichnung);
    g_free(result);
}

/**
 * is_valid_regnr_format:
 * 
 * Prüft ob ein String ein gültiges RegNr-Format hat.
 * Gültig sind:
 * - Nur Ziffern (mindestens 3): "126", "1226", "12326"
 * - Ziffern mit "/": "12/26", "123/26"
 * 
 * Returns: TRUE wenn gültiges Format
 */
static gboolean is_valid_regnr_format(const gchar *str) {
    if (!str || strlen(str) == 0) {
        return FALSE;
    }
    
    gsize len = strlen(str);
    gboolean has_slash = FALSE;
    
    for (gsize i = 0; i < len; i++) {
        gchar c = str[i];
        
        if (c == '/') {
            if (has_slash) {
                return FALSE;  /* Mehr als ein "/" */
            }
            has_slash = TRUE;
        } else if (!g_ascii_isdigit(c)) {
            return FALSE;  /* Nicht-Ziffer und nicht "/" */
        }
    }
    
    /* Format mit "/" muss mindestens ein Zeichen vor und nach "/" haben */
    if (has_slash) {
        gchar **parts = g_strsplit(str, "/", 2);
        gboolean valid = (g_strv_length(parts) == 2 && 
                         strlen(parts[0]) > 0 && 
                         strlen(parts[1]) > 0);
        g_strfreev(parts);
        return valid;
    }
    
    /* Format ohne "/" muss mindestens 3 Ziffern haben */
    return len >= 3;
}

/**
 * search_akten_by_kurzbezeichnung:
 * 
 * Sucht Akten anhand der Kurzbezeichnung (serverseitige Suche).
 * Die Datenbank führt die Teilstring-Suche durch.
 * 
 * Returns: (transfer full): Array von AkteSearchResult oder NULL bei Fehler
 */
static GPtrArray* search_akten_by_kurzbezeichnung(SondModuleAktePrivate *priv, 
                                                   const gchar *search_term, 
                                                   GError **error) {
    /* Suche Akten mit Property-Filter auf "kurzb" */
    SondGraphNodeSearchCriteria *criteria = sond_graph_node_search_criteria_new();
    criteria->label = g_strdup("Akte");
    
    /* Wildcard-Pattern für Teilstring-Suche erstellen: *search_term* */
    gchar *pattern = g_strdup_printf("*%s*", search_term);
    
    /* Property-Filter für Kurzbezeichnung (Teilstring-Suche via Wildcards) */
    sond_graph_node_search_criteria_add_property_filter(criteria, "kurzb", pattern);
    g_free(pattern);

    criteria->limit = 100;  /* Maximal 100 Ergebnisse */
    
    GPtrArray *nodes = sond_client_search_nodes(priv->client, criteria, error);
    sond_graph_node_search_criteria_free(criteria);
    
    if (!nodes) {
        return NULL;
    }
    
    /* Ergebnis-Array erstellen */
    GPtrArray *results = g_ptr_array_new_with_free_func((GDestroyNotify)akte_search_result_free);
    
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
                AkteSearchResult *result = g_new0(AkteSearchResult, 1);
                result->node = g_object_ref(node);
                result->regnr_display = format_regnr(lfd_nr, year);
                result->kurzbezeichnung = kurzb;  /* Ownership übernehmen */
                kurzb = NULL;  /* Nicht freigeben */
                
                g_ptr_array_add(results, result);
                
                g_ptr_array_unref(regnr_values);
            }

        g_free(kurzb);
    }
    
    g_ptr_array_unref(nodes);
    
    return results;
}

/* Callback-Daten für Suchergebnis-Dialog */
typedef struct {
    SondModuleAktePrivate *priv;
    GPtrArray *results;
    GtkWidget *dialog;
    GtkWidget *listbox;
} SearchDialogData;

/**
 * on_search_result_selected:
 * 
 * Callback wenn Benutzer ein Suchergebnis auswählt (Doppelklick oder Enter).
 */
static void on_search_result_selected(GtkListBox *listbox, 
                                      GtkListBoxRow *row,
                                      SearchDialogData *data) {
    if (!row) return;
    
    gint index = gtk_list_box_row_get_index(row);
    if (index < 0 || index >= (gint)data->results->len) {
        return;
    }
    
    AkteSearchResult *result = g_ptr_array_index(data->results, index);
    
    /* Akte laden */
    if (data->priv->current_node) {
        g_object_unref(data->priv->current_node);
    }
    
    data->priv->current_node = g_object_ref(result->node);
    data->priv->is_new_from_entry = FALSE;
    
    /* Lock versuchen zu setzen */
    gint64 node_id = sond_graph_node_get_id(result->node);
    GError *lock_error = NULL;
    
    if (sond_client_lock_node(data->priv->client, node_id, "Bearbeitung", &lock_error)) {
        /* Lock erfolgreich */
        update_ui_from_node(data->priv);
        
        /* Prüfe ob Akte abgelegt ist */
        if (ist_akte_aktiv(data->priv->current_node)) {
            set_akte_state(data->priv, AKTE_STATE_EDITING);
        } else {
            set_akte_state(data->priv, AKTE_STATE_ABGELEGT);
        }
    } else {
        /* Lock fehlgeschlagen (bereits gelockt) → READONLY */
        gchar *msg = g_strdup_printf(
            "Akte %s wird gerade von einem anderen Benutzer bearbeitet.\n\n"
            "Sie können die Akte nur im Lesemodus öffnen.",
            result->regnr_display);
        show_info_dialog(data->priv->main_widget, "Akte gesperrt", msg);
        g_free(msg);
        
        update_ui_from_node(data->priv);
        set_akte_state(data->priv, AKTE_STATE_READONLY);
        
        if (lock_error) g_error_free(lock_error);
    }
    
    /* Dialog schließen */
    gtk_window_close(GTK_WINDOW(data->dialog));
}

/**
 * show_akte_search_results:
 * 
 * Zeigt Suchergebnisse in einem Dialog an.
 */
static void show_akte_search_results(SondModuleAktePrivate *priv, 
                                     GPtrArray *results, 
                                     const gchar *search_term) {
    if (!results || results->len == 0) {
        gchar *msg = g_strdup_printf(
            "Keine Akten gefunden mit Suchbegriff: \"%s\"",
            search_term);
        show_info_dialog(priv->main_widget, "Keine Ergebnisse", msg);
        g_free(msg);
        return;
    }
    
    /* Dialog erstellen */
    GtkWidget *dialog = gtk_window_new();
    gchar *title = g_strdup_printf("Aktensuche: \"%s\" (%u Ergebnisse)", 
                                   search_term, results->len);
    gtk_window_set_title(GTK_WINDOW(dialog), title);
    g_free(title);
    
    gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 400);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), 
                                 GTK_WINDOW(gtk_widget_get_root(priv->main_widget)));
    
    /* ScrolledWindow für Liste */
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_window_set_child(GTK_WINDOW(dialog), scrolled);
    
    /* ListBox für Ergebnisse */
    GtkWidget *listbox = gtk_list_box_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), listbox);
    
    /* Callback-Daten */
    SearchDialogData *data = g_new0(SearchDialogData, 1);
    data->priv = priv;
    data->results = g_ptr_array_ref(results);  /* Referenz erhöhen */
    data->dialog = dialog;
    data->listbox = listbox;
    
    /* Ergebnisse in ListBox einfügen */
    for (guint i = 0; i < results->len; i++) {
        AkteSearchResult *result = g_ptr_array_index(results, i);
        
        gchar *label_text = g_strdup_printf("%s - %s", 
                                           result->regnr_display, 
                                           result->kurzbezeichnung);
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
 * suggest_last_regnr:
 * 
 * Schlägt die zuletzt verwendete RegNr vom Client im Entry-Feld vor.
 */
static void suggest_last_regnr(SondModuleAktePrivate *priv) {
    guint lfd_nr, year;
    
    if (!sond_client_get_last_regnr(priv->client, &lfd_nr, &year)) {
        return;  /* Keine RegNr gespeichert */
    }
    
    gchar *regnr_display = format_regnr(lfd_nr, year);
    gtk_editable_set_text(GTK_EDITABLE(priv->regnr_entry), regnr_display);
    g_free(regnr_display);
    
    /* Cursor ans Ende setzen */
    gtk_editable_set_position(GTK_EDITABLE(priv->regnr_entry), -1);
}

/* ========================================================================
 * Ablage/Reaktivierungs-Funktionen
 * ======================================================================== */

/**
 * ist_akte_aktiv:
 * 
 * Prüft ob eine Akte aktuell aktiv ist.
 * Eine Akte ist aktiv, wenn es mehr "von"-Properties als "bis"-Properties gibt.
 */
static gboolean ist_akte_aktiv(SondGraphNode *node) {
    if (!node) {
        return FALSE;
    }
    
    GPtrArray *properties = sond_graph_node_get_properties(node);
    if (!properties) {
        return FALSE;
    }
    
    /* Zähle "von" und "bis" Properties */
    guint von_count = 0;
    guint bis_count = 0;
    
    for (guint i = 0; i < properties->len; i++) {
        SondGraphProperty *prop = g_ptr_array_index(properties, i);
        const gchar *key = sond_graph_property_get_key(prop);
        
        if (g_strcmp0(key, "von") == 0) {
            von_count++;
        } else if (g_strcmp0(key, "bis") == 0) {
            bis_count++;
        }
    }
    
    /* Wenn mehr "von" als "bis" -> aktiv */
    return von_count > bis_count;
}

/**
 * get_letzte_ablage:
 * 
 * Gibt die letzte Ablagenummer zurück (für Anzeige).
 * Format: "2026-1" (Jahr-lfd_nr)
 * 
 * Returns: (transfer full) (nullable): Ablagenummer oder NULL
 */
static gchar* get_letzte_ablage(SondGraphNode *node) {
    if (!node) {
        return NULL;
    }
    
    GPtrArray *properties = sond_graph_node_get_properties(node);
    if (!properties) {
        return NULL;
    }
    
    /* Suche letzte "bis"-Property */
    SondGraphProperty *letzte_bis = NULL;
    
    for (guint i = 0; i < properties->len; i++) {
        SondGraphProperty *prop = g_ptr_array_index(properties, i);
        const gchar *key = sond_graph_property_get_key(prop);
        
        if (g_strcmp0(key, "bis") == 0) {
            letzte_bis = prop;  /* Letzte "bis" merken */
        }
    }
    
    if (!letzte_bis) {
        return NULL;
    }
    
    /* Hole Sub-Property "ablnr" */
    GPtrArray *sub_props = sond_graph_property_get_properties(letzte_bis);
    if (!sub_props) {
        return NULL;
    }
    
    SondGraphProperty *ablnr_prop = sond_graph_property_find(sub_props, "ablnr");
    if (!ablnr_prop) {
        return NULL;
    }
    
    const gchar *ablnr = sond_graph_property_get_first_value(ablnr_prop);
    return ablnr ? g_strdup(ablnr) : NULL;
}

/**
 * get_next_ablnr:
 * 
 * Ermittelt die nächste Ablagenummer für das aktuelle Jahr.
 * Format: "Jahr-lfd_nr" (z.B. "2026-1")
 */
static gboolean get_next_ablnr(SondModuleAktePrivate *priv, gchar **ablnr, GError **error) {
    GDateTime *now = g_date_time_new_now_local();
    guint year = g_date_time_get_year(now);
    g_date_time_unref(now);
    
    /* Suche nach allen Ablagen in diesem Jahr */
    SondGraphNodeSearchCriteria *criteria = sond_graph_node_search_criteria_new();
    criteria->label = g_strdup("Akte");
    
    /* Filter: "bis" Property mit Sub-Property "ablnr" die mit "Jahr-" beginnt */
    gchar *year_prefix = g_strdup_printf("%u-", year);
    sond_graph_node_search_criteria_add_property_filter(criteria, "ablnr", year_prefix);
    g_free(year_prefix);
    
    criteria->limit = 0;  /* Alle holen */
    
    /* Client-Methode verwenden */
    GPtrArray *nodes = sond_client_search_nodes(priv->client, criteria, error);
    sond_graph_node_search_criteria_free(criteria);

    if (!nodes) {
        return FALSE;
    }

    guint max_lfd_nr = 0;

    for (guint i = 0; i < nodes->len; i++) {
        SondGraphNode *node = g_ptr_array_index(nodes, i);
        GPtrArray *node_props = sond_graph_node_get_properties(node);
        
        /* Durchlaufe alle "bis" Properties */
        for (guint j = 0; j < node_props->len; j++) {
            SondGraphProperty *prop = g_ptr_array_index(node_props, j);
            
            if (g_strcmp0(sond_graph_property_get_key(prop), "bis") == 0) {
                GPtrArray *sub_props = sond_graph_property_get_properties(prop);
                if (sub_props) {
                    SondGraphProperty *ablnr_prop = sond_graph_property_find(sub_props, "ablnr");
                    if (ablnr_prop) {
                        const gchar *ablnr_str = sond_graph_property_get_first_value(ablnr_prop);
                        
                        /* Parse "2026-5" -> lfd_nr = 5 */
                        if (ablnr_str) {
                            gchar **parts = g_strsplit(ablnr_str, "-", 2);
                            if (g_strv_length(parts) == 2) {
                                guint lfd = (guint)g_ascii_strtoull(parts[1], NULL, 10);
                                if (lfd > max_lfd_nr) {
                                    max_lfd_nr = lfd;
                                }
                            }
                            g_strfreev(parts);
                        }
                    }
                }
            }
        }
    }

    g_ptr_array_unref(nodes);
    
    /* Nächste Nummer */
    *ablnr = g_strdup_printf("%u-%u", year, max_lfd_nr + 1);
    LOG_INFO("Nächste Ablagenr: %s\n", *ablnr);
    
    return TRUE;
}

static void update_status_display(SondModuleAktePrivate *priv) {
    if (!priv->current_node) {
        return;
    }
    
    /* Nur anzeigen wenn Node bereits in DB gespeichert ist (ID > 0) */
    if (sond_graph_node_get_id(priv->current_node) <= 0) {
        gtk_label_set_text(GTK_LABEL(priv->status_label), "Status: -");
        gtk_widget_set_visible(priv->btn_ablegen, FALSE);
        gtk_widget_set_visible(priv->btn_reaktivieren, FALSE);
        return;
    }
    
    /* Prüfe ob "von"-Property existiert - nur dann ist Akte "materiell" */
    GPtrArray *von_props = sond_graph_node_get_property(priv->current_node, "von");
    if (!von_props || von_props->len == 0) {
        gtk_label_set_text(GTK_LABEL(priv->status_label), "Status: -");
        gtk_widget_set_visible(priv->btn_ablegen, FALSE);
        gtk_widget_set_visible(priv->btn_reaktivieren, FALSE);
        if (von_props) g_ptr_array_unref(von_props);
        return;
    }
    g_ptr_array_unref(von_props);
    
    gboolean aktiv = ist_akte_aktiv(priv->current_node);
    
    if (aktiv) {
        gtk_label_set_text(GTK_LABEL(priv->status_label), "Status: Aktiv");
        gtk_widget_set_visible(priv->btn_ablegen, TRUE);
        gtk_widget_set_visible(priv->btn_reaktivieren, FALSE);
    } else {
        gchar *ablnr = get_letzte_ablage(priv->current_node);
        if (ablnr) {
            gchar *text = g_strdup_printf("Status: Abgelegt (%s)", ablnr);
            gtk_label_set_text(GTK_LABEL(priv->status_label), text);
            g_free(text);
            g_free(ablnr);
        } else {
            gtk_label_set_text(GTK_LABEL(priv->status_label), "Status: Abgelegt");
        }
        gtk_widget_set_visible(priv->btn_ablegen, FALSE);
        gtk_widget_set_visible(priv->btn_reaktivieren, TRUE);
        /* Wichtig: Button auch sensitiv machen (nicht nur sichtbar) */
        gtk_widget_set_sensitive(priv->btn_reaktivieren, TRUE);
    }
}

static void update_ocr_button_sensitivity(SondModuleAktePrivate *priv) {
    /* OCR-Button ist nur sensitiv wenn NICHT offline */
    gboolean is_offline = gtk_check_button_get_active(GTK_CHECK_BUTTON(priv->offline_toggle));
    gtk_widget_set_sensitive(priv->btn_ocr, !is_offline);
}

static void update_ui_from_node(SondModuleAktePrivate *priv) {
	guint year = 0;
	guint lfd_nr = 0;

    if (!priv->current_node) {
        clear_akte_fields(priv);

        gtk_label_set_text(GTK_LABEL(priv->status_label), "Status: -");
		gtk_widget_set_visible(priv->btn_ablegen, FALSE);
		gtk_widget_set_visible(priv->btn_reaktivieren, FALSE);

		return;
    }
    
    /* RegNr extrahieren */
    GPtrArray *regnr_values = sond_graph_node_get_property(priv->current_node, "regnr");
    if (regnr_values && regnr_values->len == 2) {
        year = (guint)g_ascii_strtoull(g_ptr_array_index(regnr_values, 0), NULL, 10);  /* Index 0 = Jahr */
        lfd_nr = (guint)g_ascii_strtoull(g_ptr_array_index(regnr_values, 1), NULL, 10);   /* Index 1 = lfd_nr */
        gchar *regnr_display = format_regnr(lfd_nr, year);
        gtk_editable_set_text(GTK_EDITABLE(priv->regnr_entry), regnr_display);
        g_free(regnr_display);
        g_ptr_array_unref(regnr_values);
    }
    
    /* Kurzbezeichnung */
    gchar *kurzbezeichnung = sond_graph_node_get_property_string(priv->current_node, "kurzb");
    if (kurzbezeichnung) {
        gtk_editable_set_text(GTK_EDITABLE(priv->entry_kurzbezeichnung), kurzbezeichnung);
        g_free(kurzbezeichnung);
    }
    
    /* Gegenstand */
    gchar *gegenstand = sond_graph_node_get_property_string(priv->current_node, "ggstd");
    if (gegenstand) {
        gtk_editable_set_text(GTK_EDITABLE(priv->textview_gegenstand), gegenstand);  /* Jetzt Entry */
        g_free(gegenstand);
    }
    
    /* Status-Anzeige aktualisieren */
    update_status_display(priv);

    /* Offline-Toggle Status aktualisieren */
	if (priv->offline_toggle) {
		gint64 node_id = sond_graph_node_get_id(priv->current_node);

		if (node_id > 0) {
			SondOfflineManager *manager = sond_client_get_offline_manager(priv->client);
			gchar *regnr = format_regnr_storage(lfd_nr, year);

			SondOfflineAkte *akte = sond_offline_manager_get_akte(manager, regnr);

			g_signal_handlers_block_by_func(priv->offline_toggle, on_offline_toggle_toggled, priv);
			if (akte && akte->syncing_enabled) {
				/* Akte in Liste UND Sync aktiv → Toggle AN */
				gtk_check_button_set_active(GTK_CHECK_BUTTON(priv->offline_toggle), TRUE);
			}
			else {
				/* Nicht in Liste ODER pausiert → Toggle AUS */
				gtk_check_button_set_active(GTK_CHECK_BUTTON(priv->offline_toggle), FALSE);
			}
			g_signal_handlers_unblock_by_func(priv->offline_toggle, on_offline_toggle_toggled, priv);

			gtk_widget_set_visible(priv->offline_toggle, TRUE);

			/* Library-Name speichern für OCR */
			g_free(priv->library_name);
			priv->library_name = regnr;  /* ownership übernehmen*/
		}
		else {
			g_free(priv->library_name);
			priv->library_name = NULL;
		}
	}

    /* OCR-Button sichtbar machen wenn Library vorhanden */
	gtk_widget_set_visible(priv->btn_ocr, priv->library_name != NULL);
	update_ocr_button_sensitivity(priv);
}

/* ========================================================================
 * Server Communication
 * ======================================================================== */
static gboolean wait_for_sync_changed(const gchar *library_id, gboolean clone,
		int timeout_ms, GError **error) {
    int waited = 0;
    while (waited < timeout_ms) {

        gchar *status = sond_seafile_get_sync_status(library_id, error);
        if (error && *error) {
			return FALSE;  // Fehler beim Abrufen des Status
		}
        if ((clone && status != NULL) || (!clone && status == NULL)) {
            g_free(status);
            return TRUE;  // Befehl ist angekommen!
        }
        g_usleep(100000);  // 100ms
        waited += 100;
    }

    if (error && *error == NULL) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
					"Timeout beim Warten auf Sync-Status-Änderung für Library %s", library_id);
	}

    return FALSE;  // Timeout
}

static SondGraphNode* search_node_by_regnr(SondModuleAktePrivate *priv,
		guint lfd_nr, guint year, GError **error) {
	/* Search Criteria erstellen */
	SondGraphNodeSearchCriteria *criteria = sond_graph_node_search_criteria_new();
	criteria->label = g_strdup("Akte");

	/* Zahlen zu Strings konvertieren */
	gchar *year_str = g_strdup_printf("%u", year);
	gchar *lfdnr_str = g_strdup_printf("%u", lfd_nr);

	/* NEU: Zwei separate Filter für Jahr und lfd. Nummer mit array_index */
	SondPropertyFilter *jahr_filter = sond_property_filter_new_with_index(
		"regnr",           // key
		year_str,          // value = "2026"
		0                  // array_index = 0 (erstes Element im Array)
	);
	g_ptr_array_add(criteria->property_filters, jahr_filter);

	SondPropertyFilter *lfdnr_filter = sond_property_filter_new_with_index(
		"regnr",           // key
		lfdnr_str,         // value = "123"
		1                  // array_index = 1 (zweites Element im Array)
	);
	g_ptr_array_add(criteria->property_filters, lfdnr_filter);

	/* Strings freigeben */
	g_free(year_str);
	g_free(lfdnr_str);

	criteria->limit = 100;

	/* Client-Methode verwenden (mit Auto-Login und Re-Login bei 401) */
	GPtrArray *nodes = sond_client_search_nodes(priv->client, criteria, error);
	sond_graph_node_search_criteria_free(criteria);

	if (!nodes) {
	    return NULL;
	}

	/* Ersten passenden Node finden */
	SondGraphNode *result_node = NULL;

	for (guint i = 0; i < nodes->len; i++) {
	    SondGraphNode *node = g_ptr_array_index(nodes, i);
	    GPtrArray *regnr_values = sond_graph_node_get_property(node, "regnr");

	    if (regnr_values && regnr_values->len == 2) {
	        guint node_year = (guint)g_ascii_strtoull(g_ptr_array_index(regnr_values, 0), NULL, 10);
	        guint node_lfd = (guint)g_ascii_strtoull(g_ptr_array_index(regnr_values, 1), NULL, 10);

	        if (node_lfd == lfd_nr && node_year == year) {
	            result_node = g_object_ref(node);  /* Referenz erhöhen */
	            g_ptr_array_unref(regnr_values);
	            break;
	        }

	        g_ptr_array_unref(regnr_values);
	    }
	}

	g_ptr_array_unref(nodes);

	if (!result_node) {
	    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
	               "Akte mit RegNr %u/%u nicht gefunden", lfd_nr, year);
	}

	return result_node;
}

static gboolean get_next_regnr(SondModuleAktePrivate *priv, guint *lfd_nr, guint *year, GError **error) {
    GDateTime *now = g_date_time_new_now_local();
    *year = g_date_time_get_year(now);
    g_date_time_unref(now);
    
    SondGraphNodeSearchCriteria *criteria = sond_graph_node_search_criteria_new();
    criteria->label = g_strdup("Akte");
    
    gchar *year_str = g_strdup_printf("%u", *year);
    sond_graph_node_search_criteria_add_property_filter(criteria, "regnr", year_str);
    g_free(year_str);
    
    criteria->limit = 0;
    
    /* Client-Methode verwenden */
    GPtrArray *nodes = sond_client_search_nodes(priv->client, criteria, error);
    sond_graph_node_search_criteria_free(criteria);

    if (!nodes) {
        return FALSE;
    }

    guint max_lfd_nr = 0;

    for (guint i = 0; i < nodes->len; i++) {
        SondGraphNode *node = g_ptr_array_index(nodes, i);
        GPtrArray *regnr_values = sond_graph_node_get_property(node, "regnr");
        
        if (regnr_values && regnr_values->len == 2) {
            guint node_year = (guint)g_ascii_strtoull(g_ptr_array_index(regnr_values, 0), NULL, 10);
            guint node_lfd = (guint)g_ascii_strtoull(g_ptr_array_index(regnr_values, 1), NULL, 10);
            
            if (node_year == *year && node_lfd > max_lfd_nr) {
                max_lfd_nr = node_lfd;
            }

            g_ptr_array_unref(regnr_values);
        }
    }

    g_ptr_array_unref(nodes);
    
    *lfd_nr = max_lfd_nr + 1;
    
    return TRUE;
}

/**
 * create_seafile_library:
 * 
 * Erstellt eine Seafile Library für eine Akte.
 * Library-Name = "Jahr-LfdNr" (z.B. "2026-23")
 */
static gchar* create_seafile_library(SondModuleAktePrivate *priv,
                                      guint year, guint lfd_nr,
                                      GError **error) {
    /* Library-Name: Jahr-LfdNr */
    gchar *library_name = g_strdup_printf("%u-%u", year, lfd_nr);
    
    /* Beschreibung */
    gchar *desc = g_strdup_printf("Akte %u/%u", lfd_nr, year % 100);
    
    /* Client-Methode verwenden (mit Auto-Login und Re-Login bei 401) */
    gchar *library_id = sond_client_create_seafile_library(priv->client,
                                                            library_name,
                                                            desc,
                                                            error);
    
    g_free(desc);
    g_free(library_name);
    
    return library_id;
}

/**
 * delete_seafile_library:
 * 
 * Löscht eine Seafile Library (Rollback bei Fehler)
 */
static gboolean delete_seafile_library(SondModuleAktePrivate *priv,
                                        const gchar *library_id,
                                        GError **error) {
    LOG_INFO("Lösche Seafile Library (ID: %s)...\n", library_id);
    
    /* Client-Methode verwenden (mit Auto-Login und Re-Login bei 401) */
    return sond_client_delete_seafile_library(priv->client, library_id, error);
}

static gboolean save_node(SondModuleAktePrivate *priv, GError **error) {
    if (!priv->current_node) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Kein Node zum Speichern vorhanden");
        return FALSE;
    }
    
    sond_graph_node_set_label(priv->current_node, "Akte");
    
    /* Client-Methode verwenden (mit Auto-Login und Re-Login bei 401) */
    if (!sond_client_save_node(priv->client, priv->current_node, error)) {
        return FALSE;
    }
    
    return TRUE;
}

static gboolean delete_node(SondModuleAktePrivate *priv, gint64 node_id, GError **error) {
    /* Client-Methode verwenden (mit Auto-Login und Re-Login bei 401) */
    return sond_client_delete_node(priv->client, node_id, error);
}

/* ========================================================================
 * UI Event Handlers
 * ======================================================================== */

/* Event Handler für Ablage/Reaktivierung */
static void on_ablegen_clicked(GtkButton *button, SondModuleAktePrivate *priv) {
    if (!priv->current_node) {
        return;
    }
    
    if (!ist_akte_aktiv(priv->current_node)) {
        show_error_dialog(priv->main_widget, "Fehler", "Akte ist bereits abgelegt.");
        return;
    }
    
    /* Nächste Ablagenummer ermitteln */
    gchar *ablnr = NULL;
    GError *error = NULL;
    
    if (!get_next_ablnr(priv, &ablnr, &error)) {
        gchar *msg = g_strdup_printf("Fehler bei Ablagenr-Vergabe: %s",
                                    error ? error->message : "Unbekannt");
        show_error_dialog(priv->main_widget, "Fehler", msg);
        g_free(msg);
        if (error) g_error_free(error);
        return;
    }
    
    /* Aktuelles Datum als ISO-String */
    GDateTime *now = g_date_time_new_now_local();
    gchar *datum = g_date_time_format(now, "%Y-%m-%d");
    g_date_time_unref(now);
    
    /* "bis"-Property mit Sub-Property "ablnr" hinzufügen */
    GPtrArray *properties = sond_graph_node_get_properties(priv->current_node);
    
    /* Neue "bis"-Property erstellen */
    const gchar *bis_value[] = {datum};
    SondGraphProperty *bis_prop = sond_graph_property_new("bis", bis_value, 1);
    
    /* Sub-Property "ablnr" hinzufügen */
    const gchar *ablnr_value[] = {ablnr};
    SondGraphProperty *ablnr_prop = sond_graph_property_new("ablnr", ablnr_value, 1);
    sond_graph_property_add_subproperty(bis_prop, ablnr_prop);
    
    /* Zur Property-Liste hinzufügen */
    g_ptr_array_add(properties, bis_prop);
    
    g_free(datum);
    g_free(ablnr);
    
    /* Speichern */
    if (!save_node(priv, &error)) {
        gchar *msg = g_strdup_printf("Fehler beim Speichern: %s",
                                    error ? error->message : "Unbekannt");
        show_error_dialog(priv->main_widget, "Fehler", msg);
        g_free(msg);
        if (error) g_error_free(error);
        return;
    }
    
    update_status_display(priv);
    set_akte_state(priv, AKTE_STATE_ABGELEGT);  /* Nach Ablage nicht mehr editierbar */
    show_info_dialog(priv->main_widget, "Erfolgreich", "Akte wurde abgelegt.");
}

static void on_reaktivieren_clicked(GtkButton *button, SondModuleAktePrivate *priv) {
    if (!priv->current_node) {
        return;
    }
    
    if (ist_akte_aktiv(priv->current_node)) {
        show_error_dialog(priv->main_widget, "Fehler", "Akte ist bereits aktiv.");
        return;
    }
    
    /* Aktuelles Datum als ISO-String */
    GDateTime *now = g_date_time_new_now_local();
    gchar *datum = g_date_time_format(now, "%Y-%m-%d");
    g_date_time_unref(now);
    
    /* "von"-Property hinzufügen */
    GPtrArray *properties = sond_graph_node_get_properties(priv->current_node);
    
    const gchar *von_value[] = {datum};
    SondGraphProperty *von_prop = sond_graph_property_new("von", von_value, 1);
    g_ptr_array_add(properties, von_prop);
    
    g_free(datum);
    
    /* Speichern */
    GError *error = NULL;
    if (!save_node(priv, &error)) {
        gchar *msg = g_strdup_printf("Fehler beim Speichern: %s",
                                    error ? error->message : "Unbekannt");
        show_error_dialog(priv->main_widget, "Fehler", msg);
        g_free(msg);
        if (error) g_error_free(error);
        return;
    }
    
    update_status_display(priv);
    set_akte_state(priv, AKTE_STATE_EDITING);  /* Nach Reaktivierung wieder editierbar */
    show_info_dialog(priv->main_widget, "Erfolgreich", "Akte wurde reaktiviert.");
}

/* Helper: Toggle zurücksetzen und Button wieder aktivieren */
static void reset_toggle_and_enable(GtkCheckButton *toggle, gboolean new_state,
                                    SondModuleAktePrivate *priv) {
    g_signal_handlers_block_by_func(toggle, on_offline_toggle_toggled, priv);
    gtk_check_button_set_active(toggle, new_state);
    g_signal_handlers_unblock_by_func(toggle, on_offline_toggle_toggled, priv);
    gtk_widget_set_sensitive(GTK_WIDGET(toggle), TRUE);
}

/* Helper: Sync aktivieren (neue Akte oder pausierte fortsetzen) */
static gboolean activate_sync(SondModuleAktePrivate *priv,
                               SondOfflineManager *manager,
                               const gchar *library_name,
                               const gchar *library_id,
                               GtkCheckButton *toggle,
							   GError** error) {
    SondOfflineAkte *akte = sond_offline_manager_get_akte(manager, library_name);

    /* Akte in Manager registrieren falls nötig */
    if (!akte) {
        gchar *kurzb = sond_graph_node_get_property_string(priv->current_node, "kurzb");
        gchar *ggstd = sond_graph_node_get_property_string(priv->current_node, "ggstd");

        SondOfflineAkte *neue_akte = sond_offline_akte_new(
            library_name, kurzb ? kurzb : "", ggstd ? ggstd : "", library_id);

        g_free(kurzb);
        g_free(ggstd);

        if (!sond_offline_manager_add_akte(manager, neue_akte, error))
            return FALSE;
    } else {
        if (!sond_offline_manager_set_sync_enabled(manager, library_name, TRUE, error))
            return FALSE;
    }

    /* Seafile Sync starten */
    const gchar *sync_dir = sond_offline_manager_get_sync_directory(manager);
    gchar *local_path = g_build_filename(sync_dir, library_name, NULL);

    gboolean success = sond_seafile_sync_library(priv->client, library_id,
                                                  library_name, local_path, error);
    g_free(local_path);

    if (!success)
        return FALSE;

    return TRUE;
}

/* Helper: Sync deaktivieren (pausieren) */
static gboolean deactivate_sync(SondModuleAktePrivate *priv,
                                 SondOfflineManager *manager,
                                 const gchar *library_name,
                                 const gchar *library_id,
								 GError** error) {
    if (!sond_offline_manager_set_sync_enabled(manager, library_name, FALSE, error))
        return FALSE;

    if (!sond_seafile_unsync_library(library_id, error))
        return FALSE;

    return TRUE;
}

/* Event Handler für Offline-Toggle */
static void on_offline_toggle_toggled(GtkCheckButton *toggle, SondModuleAktePrivate *priv) {
    if (!priv->current_node) {
        return;
    }

    gtk_widget_set_sensitive(GTK_WIDGET(toggle), FALSE);

    /* RegNr extrahieren */
    GPtrArray *regnr_values = sond_graph_node_get_property(priv->current_node, "regnr");
    if (!regnr_values || regnr_values->len != 2) {
        show_error_dialog(priv->main_widget, "Fehler", "Akte hat keine gültige RegNr");
        if (regnr_values) g_ptr_array_unref(regnr_values);
        gtk_widget_set_sensitive(GTK_WIDGET(toggle), TRUE);
        return;
    }

    guint year = (guint)g_ascii_strtoull(g_ptr_array_index(regnr_values, 0), NULL, 10);
    guint lfd = (guint)g_ascii_strtoull(g_ptr_array_index(regnr_values, 1), NULL, 10);
    gchar *library_name = format_regnr_storage(lfd, year);
    g_ptr_array_unref(regnr_values);

    SondOfflineManager *manager = sond_client_get_offline_manager(priv->client);
    if (!manager) {
        show_error_dialog(priv->main_widget, "Fehler", "Offline Manager nicht verfügbar");
        g_free(library_name);
        gtk_widget_set_sensitive(GTK_WIDGET(toggle), TRUE);
        return;
    }

    gboolean is_active = gtk_check_button_get_active(toggle);
    SondOfflineAkte *akte = sond_offline_manager_get_akte(manager, library_name);
    gboolean syncing_enabled = akte ? akte->syncing_enabled : FALSE;

    /* Konsistenzprüfung */
    if ((is_active && syncing_enabled) || (!is_active && !syncing_enabled)) {
        gchar *msg = g_strdup_printf("Akte '%s' bereits als %s markiert.",
                                    library_name, is_active ? "synchronisiert" : "nicht synchronisiert");
        show_error_dialog(priv->main_widget, "Toggle-Zustand inkonsistent", msg);
        g_free(msg);
        g_free(library_name);
        reset_toggle_and_enable(toggle, !is_active, priv);
        return;
    }

    /* Library ID ermitteln */
    GError *error = NULL;
    gchar *library_id = NULL;

    if (is_active) {
        library_id = sond_client_get_seafile_library_id(priv->client, library_name, &error);
    } else {
        library_id = g_strdup(akte->seafile_library_id);
    }

    if (!library_id) {
        show_error_dialog(priv->main_widget, "Library-ID nicht gefunden",
        		error ? error->message : "Unbekannt");
        g_error_free(error);
        g_free(library_name);
        reset_toggle_and_enable(toggle, FALSE, priv);
        return;
    }

    /* Sync aktivieren oder deaktivieren */
    gboolean success = FALSE;
    if (is_active) {
        success = activate_sync(priv, manager, library_name, library_id, toggle, &error);
    } else {
        success = deactivate_sync(priv, manager, library_name, library_id, &error);
    }

    if (!success) {
		gchar *msg = g_strdup_printf("Fehler beim %s der Synchronisation",
									is_active ? "Aktivieren" : "Deaktivieren");
		show_error_dialog(priv->main_widget, msg, error ? error->message : "Unbekannt");
		g_free(msg);
		g_error_free(error);
		reset_toggle_and_enable(toggle, !is_active, priv);
		g_free(library_name);
		g_free(library_id);
		return;
	}

    /* Warten bis Sync läuft */
    if (!wait_for_sync_changed(library_id, TRUE, 5000, &error)) {
        show_error_dialog(priv->main_widget, "Synchronisation wurde nicht gestartet",
                         error ? error->message : "Unbekannt");
        g_error_free(error);
        reset_toggle_and_enable(toggle, !is_active, priv);
        return;
    }

    g_free(library_name);
    g_free(library_id);

	gtk_widget_set_sensitive(GTK_WIDGET(toggle), TRUE);

    /* OCR-Button aktualisieren */
    update_ocr_button_sensitivity(priv);

    /* Wenn aktiviert wird: Prüfe ob OCR läuft */
    if (gtk_check_button_get_active(toggle) && priv->library_name) {
        GError *error = NULL;
        OcrStatus *status = sond_client_ocr_get_status(priv->client, priv->library_name, &error);

        if (status && status->running) {
            /* OCR läuft - Offline nicht erlauben */
            gtk_check_button_set_active(toggle, FALSE);

            GtkAlertDialog *dialog = gtk_alert_dialog_new(
                "Offline-Verfügbarkeit nicht möglich!\n\n"
                "Für diese Akte läuft gerade ein OCR-Job.\n"
                "Bitte warten Sie bis der Job abgeschlossen ist.");

            gtk_alert_dialog_show(dialog, GTK_WINDOW(gtk_widget_get_root(priv->main_widget)));
            g_object_unref(dialog);
        }

        if (status) {
            sond_client_ocr_status_free(status);
        }
        g_clear_error(&error);
    }
}

/* Callback-Daten für neuen Akte Dialog */
typedef struct {
    SondModuleAktePrivate *priv;
    guint lfd_nr;
    guint year;
} NewAkteDialogData;

static void on_new_akte_dialog_response(GObject *source, GAsyncResult *result, gpointer user_data) {
    GtkAlertDialog *dialog = GTK_ALERT_DIALOG(source);
    NewAkteDialogData *data = (NewAkteDialogData *)user_data;
    SondModuleAktePrivate *priv = data->priv;
    guint lfd_nr = data->lfd_nr;
    guint year = data->year;
    
    GError *error = NULL;
    int button = gtk_alert_dialog_choose_finish(dialog, result, &error);
    
    if (error) {
        LOG_ERROR("Dialog error: %s\n", error->message);
        g_error_free(error);
        g_free(data);
        return;
    }
    
    /* button == 0 → "Ja", button == 1 → "Nein" */
    if (button == 0) {
        /* Benutzer hat "Ja" geklickt - neue Akte SOFORT anlegen und speichern! */
        if (priv->current_node) {
            g_object_unref(priv->current_node);
        }
        
        priv->current_node = sond_graph_node_new();
        sond_graph_node_set_label(priv->current_node, "Akte");
        
        /* RegNr setzen */
        gchar *lfd_str = g_strdup_printf("%u", lfd_nr);
        gchar *year_str = g_strdup_printf("%u", year);
        const gchar *values[] = {year_str, lfd_str};  /* Jahr ZUERST, dann lfd_nr */
        sond_graph_node_set_property(priv->current_node, "regnr", values, 2);
        g_free(lfd_str);
        g_free(year_str);
        
        /* KEINE "von"-Property setzen - erst beim Speichern! */
        /* Node ist damit noch nicht "materiell" existent */
        
        /* SOFORT in DB speichern UND locken (atomar)! */
        GError *save_error = NULL;
        if (!sond_client_create_and_lock_node(priv->client, priv->current_node, 
                                              "Bearbeitung", &save_error)) {
            gchar *msg = g_strdup_printf("Fehler beim Anlegen der Akte: %s",
                                        save_error ? save_error->message : "Unbekannt");
            show_error_dialog(priv->main_widget, "Fehler", msg);
            g_free(msg);
            if (save_error) g_error_free(save_error);
            g_object_unref(priv->current_node);
            priv->current_node = NULL;
            g_free(data);
            return;
        }
        
        /* Lock ist jetzt gesetzt */
        
        priv->is_new_from_entry = TRUE;
        
        update_ui_from_node(priv);
        set_akte_state(priv, AKTE_STATE_EDITING);
    }
    
    g_free(data);
}

static void on_regnr_entry_activate(GtkEntry *entry, SondModuleAktePrivate *priv) {
    guint lfd_nr, year;

    const gchar *regnr_text = gtk_editable_get_text(GTK_EDITABLE(entry));
    
    if (!regnr_text || strlen(regnr_text) == 0)
    	return NULL;

    /* Prüfen ob gültiges RegNr-Format */
    if (!is_valid_regnr_format(regnr_text)) {
        /* Kein gültiges RegNr-Format -> Suche nach Kurzbezeichnung */
        GError *error = NULL;
        GPtrArray *results = search_akten_by_kurzbezeichnung(priv, regnr_text, &error);
        
        if (error) {
            gchar *msg = g_strdup_printf("Fehler bei der Suche: %s", error->message);
            show_error_dialog(priv->main_widget, "Suchfehler", msg);
            g_free(msg);
            g_error_free(error);
            return;
        }
        
        if (results) {
            show_akte_search_results(priv, results, regnr_text);
            g_ptr_array_unref(results);
        }
        
        return;
    }
    
    /* Gültiges Format -> Parsen */
    if (!parse_regnr(regnr_text, &lfd_nr, &year)) {
        show_error_dialog(priv->main_widget, "Fehler beim Parsen",
                         "RegNr konnte nicht interpretiert werden.");
        return;
    }
    
    /* RegNr merken */
    sond_client_set_last_regnr(priv->client, lfd_nr, year);
    
    GError *error = NULL;
    SondGraphNode *node = search_node_by_regnr(priv, lfd_nr, year, &error);

    /* Fehler außer Akte existiert nicht */
    if (error && error->code != G_IO_ERROR_NOT_FOUND) {
        show_error_dialog(priv->main_widget, "Fehler", error->message);
        g_error_free(error);
        return;
    }
    else if (error) {//kein richtiger Fehler; Akte einfach noch nicht angelegt
    	g_error_free(error);
    }
    
    if (node) {
        /* Akte existiert - versuche Lock zu setzen */
        if (priv->current_node) {
            g_object_unref(priv->current_node);
        }
        priv->current_node = node;
        priv->is_new_from_entry = FALSE;
        
        gint64 node_id = sond_graph_node_get_id(node);
        GError *lock_error = NULL;
        
        if (sond_client_lock_node(priv->client, node_id, "Bearbeitung", &lock_error)) {
            /* Lock erfolgreich */
            update_ui_from_node(priv);
            
            /* Prüfe ob Akte abgelegt ist */
            if (ist_akte_aktiv(priv->current_node)) {
                set_akte_state(priv, AKTE_STATE_EDITING);  /* Aktiv -> editierbar */
            } else {
                set_akte_state(priv, AKTE_STATE_ABGELEGT);  /* Abgelegt -> read-only mit Reaktivierung */
            }
        } else {
            /* Lock fehlgeschlagen (bereits gelockt) → READONLY */
            gchar *msg = g_strdup_printf(
                "Akte %u/%u wird gerade von einem anderen Benutzer bearbeitet.\n\n"
                "Sie können die Akte nur im Lesemodus öffnen.",
                lfd_nr, year);
            show_info_dialog(priv->main_widget, "Akte gesperrt", msg);
            g_free(msg);
            
            update_ui_from_node(priv);
            set_akte_state(priv, AKTE_STATE_READONLY);
            
            if (lock_error) g_error_free(lock_error);
        }
        
        return;
    }
    
    /* Bestätigung für neue Akte */
    gchar *regnr_display = format_regnr(lfd_nr, year);
    gchar *message = g_strdup_printf(
        "Akte %s existiert nicht.\n\nNeue Akte mit dieser Registernummer anlegen?",
        regnr_display);
    g_free(regnr_display);
    
    GtkAlertDialog *dialog = gtk_alert_dialog_new("Akte nicht gefunden");
    gtk_alert_dialog_set_detail(dialog, message);
    g_free(message);
    
    const char *buttons[] = {"Ja", "Nein", NULL};
    gtk_alert_dialog_set_buttons(dialog, buttons);
    gtk_alert_dialog_set_default_button(dialog, 0);  /* "Ja" als Standard */
    gtk_alert_dialog_set_cancel_button(dialog, 1);   /* "Nein" als Abbruch */
    gtk_alert_dialog_set_modal(dialog, TRUE);
    
    /* Callback-Daten vorbereiten */
    NewAkteDialogData *data = g_new0(NewAkteDialogData, 1);
    data->priv = priv;
    data->lfd_nr = lfd_nr;
    data->year = year;
    
    gtk_alert_dialog_choose(dialog,
                           GTK_WINDOW(gtk_widget_get_root(priv->main_widget)),
                           NULL,  /* cancellable */
                           on_new_akte_dialog_response,
                           data);
    
    g_object_unref(dialog);
}

static void on_neue_akte_clicked(GtkButton *button, SondModuleAktePrivate *priv) {
    if (priv->current_node) {
        g_object_unref(priv->current_node);
    }
    
    /* Leeren Node anlegen (nur lokal, NICHT in DB!) */
    priv->current_node = sond_graph_node_new();
    sond_graph_node_set_label(priv->current_node, "Akte");
    /* KEINE RegNr setzen - wird erst beim Speichern vergeben! */
    /* KEINE "von"-Property setzen - erst beim Speichern! */
    
    priv->is_new_from_entry = FALSE;
    
    /* Entry insensitiv machen + Placeholder */
    gtk_widget_set_sensitive(priv->regnr_entry, FALSE);
    apply_widget_style(priv->regnr_entry, FALSE);
    gtk_editable_set_text(GTK_EDITABLE(priv->regnr_entry), "Neue Akte");
    
    /* Felder leer lassen */
    gtk_editable_set_text(GTK_EDITABLE(priv->entry_kurzbezeichnung), "");
    gtk_editable_set_text(GTK_EDITABLE(priv->textview_gegenstand), "");  /* Jetzt Entry */
    
    set_akte_state(priv, AKTE_STATE_CREATING);
}

static void on_speichern_clicked(GtkButton *button, SondModuleAktePrivate *priv) {
	GPtrArray* von_props = NULL;

    if (!priv->current_node) {
        show_error_dialog(priv->main_widget, "Nichts zu speichern",
                         "Bitte erst eine Akte laden oder neu erstellen.");
        return;
    }
    
    /* Prüfen ob Node bereits "materiell" ist (hat "von"-Property) */
	von_props = sond_graph_node_get_property(priv->current_node, "von");
	gboolean is_new = (!von_props || von_props->len == 0);
	if (von_props) {
		g_ptr_array_unref(von_props);
		von_props = NULL;
	}

	/* Prüfen ob Node eine RegNr hat (von "Neue Akte"-Button könnte sie fehlen) */
	GPtrArray *regnr_values = sond_graph_node_get_property(priv->current_node, "regnr");
	gboolean has_regnr = (regnr_values != NULL && regnr_values->len == 2);
	if (regnr_values) {
		g_ptr_array_unref(regnr_values);
	}

    if (!has_regnr) {
        /* Keine RegNr vorhanden - jetzt erst vergeben! */
        /* RETRY-SCHLEIFE: Falls RegNr-Konflikt, nochmal probieren */
        gboolean success = FALSE;
        const gint MAX_RETRIES = 10;
        
        for (gint attempt = 0; attempt < MAX_RETRIES && !success; attempt++) {
            guint lfd_nr, year;
            GError *error = NULL;
            
            if (!get_next_regnr(priv, &lfd_nr, &year, &error)) {
                gchar *msg = g_strdup_printf("Fehler bei RegNr-Vergabe (Versuch %d/%d): %s",
                                            attempt + 1, MAX_RETRIES,
                                            error ? error->message : "Unbekannt");
                show_error_dialog(priv->main_widget, "RegNr-Vergabe fehlgeschlagen", msg);
                g_free(msg);
                if (error) g_error_free(error);
                return;
            }
            
            /* RegNr setzen */
            gchar *lfd_str = g_strdup_printf("%u", lfd_nr);
            gchar *year_str = g_strdup_printf("%u", year);
            const gchar *values[] = {year_str, lfd_str};
            sond_graph_node_set_property(priv->current_node, "regnr", values, 2);
            g_free(lfd_str);
            g_free(year_str);
            
            /* Felder in Node übernehmen (vor dem Speichern!) */
            const gchar *kurzbezeichnung = gtk_editable_get_text(GTK_EDITABLE(priv->entry_kurzbezeichnung));
            if (kurzbezeichnung && strlen(kurzbezeichnung) > 0) {
                sond_graph_node_set_property_string(priv->current_node, "kurzb", kurzbezeichnung);
            }
            
            const gchar *gegenstand = gtk_editable_get_text(GTK_EDITABLE(priv->textview_gegenstand));  /* Jetzt Entry */
            if (gegenstand && strlen(gegenstand) > 0) {
                sond_graph_node_set_property_string(priv->current_node, "ggstd", gegenstand);
            }

            /* "von"-Property setzen - Akte wird damit "materiell" */
            GDateTime *now = g_date_time_new_now_local();
            gchar *datum = g_date_time_format(now, "%Y-%m-%d");
            sond_graph_node_set_property_string(priv->current_node, "von", datum);
            g_date_time_unref(now);
            g_free(datum);
            
            /* ZUERST: Node speichern */
            error = NULL;
            if (!save_node(priv, &error)) {
                /* Node-Speichern fehlgeschlagen */
                if (error && (error->code == 409 || 
                             (error->message && strstr(error->message, "Duplicate")) ||
                             (error->message && strstr(error->message, "already exists")))) {
                    /* RegNr-Konflikt → Retry */
                    LOG_INFO("RegNr-Konflikt bei %u/%u, versuche mit nächster RegNr...\n", lfd_nr, year);
                    if (error) g_error_free(error);
                    /* Schleife läuft weiter */
                    continue;
                } else {
                    /* Anderer Fehler → Abbruch */
                    gchar *msg = g_strdup_printf("Fehler beim Speichern: %s", 
                                                error ? error->message : "Unbekannt");
                    show_error_dialog(priv->main_widget, "Speichern fehlgeschlagen", msg);
                    g_free(msg);
                    if (error) g_error_free(error);
                    return;
                }
            }

            /* Node erfolgreich gespeichert - JETZT Library erstellen */
            gchar *library_id = NULL;
            GError *lib_error = NULL;
            library_id = create_seafile_library(priv, year, lfd_nr, &lib_error);

            if (!library_id) {
                /* Library-Erstellung fehlgeschlagen - Node wieder löschen (Rollback) */
                LOG_ERROR("Library-Erstellung fehlgeschlagen, lösche Node (Rollback)...\n");

                gint64 node_id = sond_graph_node_get_id(priv->current_node);
                GError *del_error = NULL;

                /* Unlock und Delete */
                sond_client_unlock_node(priv->client, node_id, NULL);

                if (!delete_node(priv, node_id, &del_error)) {
                    LOG_ERROR("Rollback fehlgeschlagen: %s\n",
                             del_error ? del_error->message : "Unbekannt");
                    if (del_error) g_error_free(del_error);
                }

                /* Fehler anzeigen */
                gchar *msg = g_strdup_printf("Fehler beim Erstellen der Seafile Library: %s\n\n"
                                            "Node wurde zurückgerollt.",
                                            lib_error ? lib_error->message : "Unbekannt");
                show_error_dialog(priv->main_widget, "Library-Erstellung fehlgeschlagen", msg);
                g_free(msg);
                if (lib_error) g_error_free(lib_error);

                /* current_node ungültig machen */
                g_object_unref(priv->current_node);
                priv->current_node = NULL;

                return;
            }

            /* Erfolg! */
            success = TRUE;
			priv->is_new_from_entry = FALSE;
			
			/* RegNr merken */
			sond_client_set_last_regnr(priv->client, lfd_nr, year);
			
			update_ui_from_node(priv);

			gchar *regnr_display = format_regnr(lfd_nr, year);
			gchar *msg = g_strdup_printf("Akte %s wurde erfolgreich gespeichert.", regnr_display);
			show_info_dialog(priv->main_widget, "Gespeichert", msg);
			g_free(msg);
			g_free(regnr_display);
			g_free(library_id);
        }
        
        if (!success) {
            show_error_dialog(priv->main_widget, "Speichern fehlgeschlagen",
                            "Nach 10 Versuchen konnte keine freie RegNr vergeben werden. "
                            "Bitte später erneut versuchen.");
        }
        
        return;  /* Fertig (Erfolg oder max retries erreicht) */
    }
    
    /* Wenn Node neu ist (keine "von"-Property), Library erstellen */
    if (is_new && has_regnr) {
        /* Manuelle RegNr-Eingabe - Node ist in DB aber noch nicht "materiell" */

        /* RegNr extrahieren */
        GPtrArray *regnr_vals = sond_graph_node_get_property(priv->current_node, "regnr");
        if (regnr_vals && regnr_vals->len == 2) {
            guint year = (guint)g_ascii_strtoull(g_ptr_array_index(regnr_vals, 0), NULL, 10);
            guint lfd = (guint)g_ascii_strtoull(g_ptr_array_index(regnr_vals, 1), NULL, 10);
            g_ptr_array_unref(regnr_vals);

            /* Felder übernehmen */
            const gchar *kurzbezeichnung = gtk_editable_get_text(GTK_EDITABLE(priv->entry_kurzbezeichnung));
            if (kurzbezeichnung && strlen(kurzbezeichnung) > 0) {
                sond_graph_node_set_property_string(priv->current_node, "kurzb", kurzbezeichnung);
            }

            const gchar *gegenstand = gtk_editable_get_text(GTK_EDITABLE(priv->textview_gegenstand));
            if (gegenstand && strlen(gegenstand) > 0) {
                sond_graph_node_set_property_string(priv->current_node, "ggstd", gegenstand);
            }

            /* "von"-Property setzen */
            GDateTime *now = g_date_time_new_now_local();
            gchar *datum = g_date_time_format(now, "%Y-%m-%d");
            sond_graph_node_set_property_string(priv->current_node, "von", datum);
            g_date_time_unref(now);
            g_free(datum);

            /* ZUERST: Node speichern */
            GError *error = NULL;
            if (!save_node(priv, &error)) {
                gchar *msg = g_strdup_printf("Fehler beim Speichern: %s",
                                            error ? error->message : "Unbekannt");
                show_error_dialog(priv->main_widget, "Speichern fehlgeschlagen", msg);
                g_free(msg);
                if (error) g_error_free(error);
                return;
            }

            /* Node gespeichert - JETZT Library erstellen */
            gchar *library_id = create_seafile_library(priv, year, lfd, &error);

            if (!library_id) {
                /* Library-Fehler - nur warnen, Node bleibt */
                LOG_ERROR("Library-Erstellung fehlgeschlagen: %s\n",
                         error ? error->message : "Unbekannt");

                gchar *msg = g_strdup_printf(
                    "Akte wurde gespeichert, aber Seafile Library konnte nicht erstellt werden:\n%s\n\n"
                    "Sie können die Akte trotzdem nutzen, aber keine Dateien hochladen.",
                    error ? error->message : "Unbekannt");
                show_error_dialog(priv->main_widget, "Warnung", msg);
                if (error) g_error_free(error);
            }

            g_free(library_id);

            /* UI aktualisieren */
            priv->is_new_from_entry = FALSE;
            
            /* RegNr merken */
            sond_client_set_last_regnr(priv->client, lfd, year);
            
            update_ui_from_node(priv);
            show_info_dialog(priv->main_widget, "Gespeichert", "Akte wurde erfolgreich gespeichert.");

            return;  /* Fertig! */
        }
    }

    /* RegNr vorhanden (manuelle Eingabe oder bereits gespeichert) */
    const gchar *kurzbezeichnung = gtk_editable_get_text(GTK_EDITABLE(priv->entry_kurzbezeichnung));
    if (kurzbezeichnung && strlen(kurzbezeichnung) > 0) {
        sond_graph_node_set_property_string(priv->current_node, "kurzb", kurzbezeichnung);
    }
    
    const gchar *gegenstand = gtk_editable_get_text(GTK_EDITABLE(priv->textview_gegenstand));  /* Jetzt Entry */
    if (gegenstand && strlen(gegenstand) > 0) {
        sond_graph_node_set_property_string(priv->current_node, "ggstd", gegenstand);
    }
    
    /* "von"-Property setzen falls noch nicht vorhanden (bei erster Speicherung) */
    von_props = sond_graph_node_get_property(priv->current_node, "von");
    if (!von_props || von_props->len == 0) {
        GDateTime *now = g_date_time_new_now_local();
        gchar *datum = g_date_time_format(now, "%Y-%m-%d");
        sond_graph_node_set_property_string(priv->current_node, "von", datum);
        g_date_time_unref(now);
        g_free(datum);
    }
    if (von_props) {
        g_ptr_array_unref(von_props);
        von_props = NULL;
    }
    
    /* Speichern */
    GError *error = NULL;
    if (save_node(priv, &error)) {
        priv->is_new_from_entry = FALSE; /* Nach Speichern nicht mehr löschen */
        
        /* RegNr merken */
        GPtrArray *regnr_vals = sond_graph_node_get_property(priv->current_node, "regnr");
        if (regnr_vals && regnr_vals->len == 2) {
            guint year = (guint)g_ascii_strtoull(g_ptr_array_index(regnr_vals, 0), NULL, 10);
            guint lfd = (guint)g_ascii_strtoull(g_ptr_array_index(regnr_vals, 1), NULL, 10);
            sond_client_set_last_regnr(priv->client, lfd, year);
            g_ptr_array_unref(regnr_vals);
        }
        
        /* UI mit RegNr updaten (falls sie gerade erst vergeben wurde) */
        update_ui_from_node(priv);
        
        show_info_dialog(priv->main_widget, "Gespeichert", "Akte wurde erfolgreich gespeichert.");
    } else {
        gchar *msg = g_strdup_printf("Fehler: %s", error ? error->message : "Unbekannt");
        show_error_dialog(priv->main_widget, "Speichern fehlgeschlagen", msg);
        g_free(msg);
        if (error) g_error_free(error);
    }
}

static void on_ok_clicked(GtkButton *button, SondModuleAktePrivate *priv) {
    if (!priv->current_node) {
        show_error_dialog(priv->main_widget, "Nichts zu speichern",
                         "Bitte erst eine Akte laden oder neu erstellen.");
        return;
    }

    /* Erst speichern */
    on_speichern_clicked(button, priv);
    
    /* Lock freigeben falls Node in DB */
    gint64 node_id = sond_graph_node_get_id(priv->current_node);
    if (priv->is_new_from_entry && node_id > 0) {
        GError *error = NULL;
        if (!sond_client_unlock_node(priv->client, node_id, &error)) {
            LOG_ERROR("Unlock fehlgeschlagen: %s\n", error ? error->message : "Unbekannt");
            if (error) g_error_free(error);
        }
    }
    
    /* Dann zurücksetzen */
    if (priv->current_node) {
        g_object_unref(priv->current_node);
        priv->current_node = NULL;
    }
    
    priv->is_new_from_entry = FALSE;
    
    clear_akte_fields(priv);
    set_akte_state(priv, AKTE_STATE_INITIAL);
    
    /* Letzte RegNr vorschlagen */
    suggest_last_regnr(priv);
}

static void on_abbrechen_clicked(GtkButton *button, SondModuleAktePrivate *priv) {
    /* Wenn über Entry neu angelegt und noch nicht gespeichert: Löschen */
    if (priv->is_new_from_entry && priv->current_node && sond_graph_node_get_id(priv->current_node) > 0) {
        gint64 node_id = sond_graph_node_get_id(priv->current_node);
        LOG_INFO("Lösche nicht gespeicherte Entry-Akte (ID %" G_GINT64_FORMAT ")\n", node_id);
        
        /* Erst Unlock, dann Delete */
        GError *error = NULL;
        if (!sond_client_unlock_node(priv->client, node_id, &error)) {
            LOG_ERROR("Unlock fehlgeschlagen: %s\n", error ? error->message : "Unbekannt");
            if (error) g_error_free(error);
        }
        
        error = NULL;
        if (!delete_node(priv, node_id, &error)) {
            LOG_ERROR("Fehler beim Löschen der Akte: %s\n", error ? error->message : "Unbekannt");
            if (error) g_error_free(error);
        }
    } else if (priv->current_node && sond_graph_node_get_id(priv->current_node) > 0) {
        /* Bestehende Akte: Nur Unlock */
        gint64 node_id = sond_graph_node_get_id(priv->current_node);
        GError *error = NULL;
        if (!sond_client_unlock_node(priv->client, node_id, &error)) {
            LOG_ERROR("Unlock fehlgeschlagen: %s\n", error ? error->message : "Unbekannt");
            if (error) g_error_free(error);
        }
    }
    
    /* Zurücksetzen */
    if (priv->current_node) {
        g_object_unref(priv->current_node);
        priv->current_node = NULL;
    }
    
    priv->is_new_from_entry = FALSE;
    
    clear_akte_fields(priv);
    set_akte_state(priv, AKTE_STATE_INITIAL);
    
    /* Letzte RegNr vorschlagen */
    suggest_last_regnr(priv);
}

/* =======================================================================
 * OCR Callbacks
 * ======================================================================= */

/**
 * ocr_start_job_with_force:
 *
 * Startet OCR-Job (Helper-Funktion)
 */
static void ocr_start_job_with_force(SondModuleAktePrivate *priv,
                                      gboolean force_reprocess) {
    GError *error = NULL;
    gboolean success = sond_client_ocr_start_job(priv->client,
                                          priv->library_name,
                                          force_reprocess,
                                          &error);

    GtkAlertDialog *dialog;

    if (success) {
        dialog = gtk_alert_dialog_new(
            "OCR-Job wurde gestartet.\n\n"
            "Die Verarbeitung läuft im Hintergrund auf dem Server.");
    } else {
        gchar *msg = g_strdup_printf(
            "OCR-Job konnte nicht gestartet werden:\n%s",
            error ? error->message : "Unbekannter Fehler");
        dialog = gtk_alert_dialog_new("%s", msg);
        g_free(msg);
        g_clear_error(&error);
    }

    gtk_alert_dialog_show(dialog, GTK_WINDOW(gtk_widget_get_root(priv->main_widget)));
    g_object_unref(dialog);
}

/**
 * on_ocr_update_choice_cb:
 *
 * Callback für "Update oder Alles neu" Dialog
 */
static void on_ocr_update_choice_cb(GObject *source, GAsyncResult *result, gpointer user_data) {
    SondModuleAktePrivate *priv = user_data;
    GtkAlertDialog *dialog = GTK_ALERT_DIALOG(source);

    gint response = gtk_alert_dialog_choose_finish(dialog, result, NULL);

    if (response == 0) {
        /* "Nur Update" */
        ocr_start_job_with_force(priv, FALSE);
    } else if (response == 1) {
        /* "Alle neu verarbeiten" */
        ocr_start_job_with_force(priv, TRUE);
    }
    /* response == 2 oder -1 = Abbrechen */
}

/**
 * on_ocr_first_run_cb:
 *
 * Callback für ersten OCR-Lauf Dialog
 */
static void on_ocr_first_run_cb(GObject *source, GAsyncResult *result, gpointer user_data) {
    SondModuleAktePrivate *priv = user_data;
    GtkAlertDialog *dialog = GTK_ALERT_DIALOG(source);

    gint response = gtk_alert_dialog_choose_finish(dialog, result, NULL);

    if (response == 0) {
        /* "Ja" */
        ocr_start_job_with_force(priv, FALSE);
    }
    /* response == 1 oder -1 = Nein/Abbrechen */
}

/**
 * on_ocr_clicked:
 *
 * Handler für OCR-Button
 */
static void on_ocr_clicked(GtkButton *button, SondModuleAktePrivate *priv) {
    if (!priv->library_name) {
        LOG_ERROR("OCR clicked but no library_name set");
        return;
    }

    /* 1. Status prüfen */
    GError *error = NULL;
    OcrStatus *status = sond_client_ocr_get_status(priv->client, priv->library_name, &error);

    if (error) {
        /* Fehler beim Status-Abruf */
        gchar *msg = g_strdup_printf(
            "Fehler beim Abrufen des OCR-Status:\n%s",
            error->message);

        GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", msg);
        gtk_alert_dialog_show(dialog, GTK_WINDOW(gtk_widget_get_root(priv->main_widget)));
        g_object_unref(dialog);

        g_free(msg);
        g_error_free(error);
        return;
    }

    /* 2. Läuft bereits ein Job? */
    if (status && status->running) {
        /* Job läuft bereits */
        gchar *msg = g_strdup_printf(
            "OCR-Job läuft bereits!\n\n"
            "Fortschritt: %d/%d Dateien\n%d PDFs verarbeitet",
            status->processed_files,
            status->total_files,
            status->processed_pdfs);

        GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", msg);
        gtk_alert_dialog_show(dialog, GTK_WINDOW(gtk_widget_get_root(priv->main_widget)));
        g_object_unref(dialog);

        g_free(msg);
        sond_client_ocr_status_free(status);
        return;
    }

    /* 3. War schon mal ein Job? */
    gboolean had_previous_job = (status != NULL && status->end_time != NULL);

    if (had_previous_job) {
        /* Zeige wann letzter Job war und frage ob Update */
        gchar *end_str = g_date_time_format(status->end_time, "%d.%m.%Y %H:%M");
        gchar *msg = g_strdup_printf(
            "OCR wurde zuletzt am %s durchgeführt.\n\n"
            "Sollen nur geänderte und neue Dateien verarbeitet werden?",
            end_str);

        const char *buttons[] = {
            "Nur Update",
            "Alle neu verarbeiten",
            "Abbrechen",
            NULL
        };

        GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", msg);
        gtk_alert_dialog_set_buttons(dialog, buttons);
        gtk_alert_dialog_set_cancel_button(dialog, 2);
        gtk_alert_dialog_set_default_button(dialog, 0);

        gtk_alert_dialog_choose(dialog,
                               GTK_WINDOW(gtk_widget_get_root(priv->main_widget)),
                               NULL,
                               on_ocr_update_choice_cb,
                               priv);

        g_object_unref(dialog);
        g_free(msg);
        g_free(end_str);
        sond_client_ocr_status_free(status);

    } else {
        /* Erster Job - direkt starten */
        if (status) {
            sond_client_ocr_status_free(status);
        }

        const char *buttons[] = {
            "Ja",
            "Nein",
            NULL
        };

        GtkAlertDialog *dialog = gtk_alert_dialog_new(
            "OCR für diese Akte starten?\n\n"
            "Dies kann je nach Anzahl der Dateien mehrere Stunden dauern.");

        gtk_alert_dialog_set_buttons(dialog, buttons);
        gtk_alert_dialog_set_cancel_button(dialog, 1);
        gtk_alert_dialog_set_default_button(dialog, 0);

        gtk_alert_dialog_choose(dialog,
                               GTK_WINDOW(gtk_widget_get_root(priv->main_widget)),
                               NULL,
                               on_ocr_first_run_cb,
                               priv);

        g_object_unref(dialog);
    }
}

/* ========================================================================
 * Public API
 * ======================================================================== */

GtkWidget* sond_module_akte_new(SondClient *client) {
    SondModuleAktePrivate *priv = g_new0(SondModuleAktePrivate, 1);
    priv->client = g_object_ref(client);
    priv->current_node = NULL;
    priv->state = AKTE_STATE_INITIAL;
    priv->is_new_from_entry = FALSE;

    /* CSS Provider einrichten */
    priv->css_provider = setup_css_provider();

    /* Haupt-Container */
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(main_box, 20);
    gtk_widget_set_margin_end(main_box, 20);
    gtk_widget_set_margin_top(main_box, 20);
    gtk_widget_set_margin_bottom(main_box, 20);
    
    priv->main_widget = main_box;

    /* Top-Zeile: RegNr + Neue Akte */
    GtkWidget *top_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(main_box), top_box);

    GtkWidget *regnr_label = gtk_label_new("Registernummer:");
    gtk_box_append(GTK_BOX(top_box), regnr_label);

    priv->regnr_entry = gtk_entry_new();
    gtk_editable_set_max_width_chars(GTK_EDITABLE(priv->regnr_entry), 7);  /* Max "123/456" */
    gtk_editable_set_width_chars(GTK_EDITABLE(priv->regnr_entry), 7);
    g_signal_connect(priv->regnr_entry, "activate", G_CALLBACK(on_regnr_entry_activate), priv);
    gtk_box_append(GTK_BOX(top_box), priv->regnr_entry);

    priv->btn_neue_akte = gtk_button_new_with_label("Neue Akte");
    g_signal_connect(priv->btn_neue_akte, "clicked", G_CALLBACK(on_neue_akte_clicked), priv);
    gtk_box_append(GTK_BOX(top_box), priv->btn_neue_akte);

    /* Separator */
    GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(main_box), separator);

    /* Akte-Felder Container */
    priv->akte_fields_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_append(GTK_BOX(main_box), priv->akte_fields_box);

    /* Grid */
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_box_append(GTK_BOX(priv->akte_fields_box), grid);

    /* Kurzbezeichnung */
    GtkWidget *label_kurz = gtk_label_new("Kurzbezeichnung:");
    gtk_widget_set_halign(label_kurz, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), label_kurz, 0, 0, 1, 1);

    priv->entry_kurzbezeichnung = gtk_entry_new();
    gtk_editable_set_width_chars(GTK_EDITABLE(priv->entry_kurzbezeichnung), 40);  /* 1/3 kürzer als vorher */
    gtk_grid_attach(GTK_GRID(grid), priv->entry_kurzbezeichnung, 1, 0, 1, 1);

    /* Gegenstand (jetzt als Entry, nicht TextView) */
    GtkWidget *label_gegenstand = gtk_label_new("Gegenstand:");
    gtk_widget_set_halign(label_gegenstand, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), label_gegenstand, 0, 1, 1, 1);

    priv->textview_gegenstand = gtk_entry_new();  /* Jetzt Entry statt TextView! */
    gtk_widget_set_hexpand(priv->textview_gegenstand, TRUE);
    gtk_grid_attach(GTK_GRID(grid), priv->textview_gegenstand, 1, 1, 1, 1);

    /* Status-Anzeige und Ablage/Reaktivierungs-Buttons (NICHT in akte_fields_box!) */
    GtkWidget *status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(main_box), status_box);  /* Direkt in main_box! */
    
    priv->status_label = gtk_label_new("Status: -");
    gtk_widget_set_halign(priv->status_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(status_box), priv->status_label);
    
    /* Offline-Toggle Button */
	priv->offline_toggle = gtk_check_button_new_with_label("Offline verfügbar");
	gtk_widget_set_margin_start(priv->offline_toggle, 20);
	g_signal_connect(priv->offline_toggle, "toggled", G_CALLBACK(on_offline_toggle_toggled), priv);
	gtk_widget_set_visible(priv->offline_toggle, FALSE);
	gtk_box_append(GTK_BOX(status_box), priv->offline_toggle);

	/* Offline-Toggle Button */
	priv->offline_toggle = gtk_check_button_new_with_label("Offline verfügbar");
	gtk_widget_set_margin_start(priv->offline_toggle, 20);
	g_signal_connect(priv->offline_toggle, "toggled", G_CALLBACK(on_offline_toggle_toggled), priv);
	gtk_widget_set_visible(priv->offline_toggle, FALSE);
	gtk_box_append(GTK_BOX(status_box), priv->offline_toggle);

	/* OCR Button */                                         // NEU
	priv->btn_ocr = gtk_button_new_with_label("OCR");       // NEU
	gtk_widget_set_margin_start(priv->btn_ocr, 10);         // NEU
	g_signal_connect(priv->btn_ocr, "clicked",              // NEU
					G_CALLBACK(on_ocr_clicked), priv);      // NEU
	gtk_widget_set_visible(priv->btn_ocr, FALSE);           // NEU
	gtk_box_append(GTK_BOX(status_box), priv->btn_ocr);     // NEU

	/* Spacer */
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(status_box), spacer);
    
    priv->btn_ablegen = gtk_button_new_with_label("Akte ablegen");
    g_signal_connect(priv->btn_ablegen, "clicked", G_CALLBACK(on_ablegen_clicked), priv);
    gtk_widget_set_visible(priv->btn_ablegen, FALSE);  /* Initial unsichtbar */
    gtk_box_append(GTK_BOX(status_box), priv->btn_ablegen);
    
    priv->btn_reaktivieren = gtk_button_new_with_label("Akte reaktivieren");
    g_signal_connect(priv->btn_reaktivieren, "clicked", G_CALLBACK(on_reaktivieren_clicked), priv);
    gtk_widget_set_visible(priv->btn_reaktivieren, FALSE);  /* Initial unsichtbar */
    gtk_box_append(GTK_BOX(status_box), priv->btn_reaktivieren);

    /* Separator */
    GtkWidget *separator2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(main_box), separator2);

    /* Button-Zeile */
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_append(GTK_BOX(main_box), button_box);

    priv->btn_ok = gtk_button_new_with_label("Ok");
    g_signal_connect(priv->btn_ok, "clicked", G_CALLBACK(on_ok_clicked), priv);
    gtk_box_append(GTK_BOX(button_box), priv->btn_ok);

    priv->btn_speichern = gtk_button_new_with_label("Speichern");
    g_signal_connect(priv->btn_speichern, "clicked", G_CALLBACK(on_speichern_clicked), priv);
    gtk_box_append(GTK_BOX(button_box), priv->btn_speichern);

    priv->btn_abbrechen = gtk_button_new_with_label("Abbrechen");
    g_signal_connect(priv->btn_abbrechen, "clicked", G_CALLBACK(on_abbrechen_clicked), priv);
    gtk_box_append(GTK_BOX(button_box), priv->btn_abbrechen);

    /* Private Daten am Widget speichern */
    g_object_set_data_full(G_OBJECT(main_box), "priv", priv, g_free);

    /* Initial State setzen */
    set_akte_state(priv, AKTE_STATE_INITIAL);

    return main_box;
}
