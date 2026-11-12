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
#include "../../sond_log_and_error.h"
#include "../../sond_graph/sond_graph_node.h"
#include "../../sond_graph/sond_graph_property.h"
#include "../../sond_graph/sond_graph_db.h"
#include "sond_akten.h"
#include "sond_import.h"
#include "sond_import_dialog.h"
#include "sond_zond_start.h"
#include "../../sond_akte_name.h"

#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

typedef enum {
    AKTE_STATE_INITIAL,       /* Nur "Neue Akte" + RegNr Entry aktiv */
    AKTE_STATE_CREATING,      /* Neue Akte wird erstellt (Entry insensitiv) */
    AKTE_STATE_EDITING,       /* Bestehende/Entry-Akte wird bearbeitet */
    AKTE_STATE_READONLY,      /* Akte ist gelockt von anderem User */
    AKTE_STATE_ABGELEGT,      /* Akte ist abgelegt (read-only mit Reaktivierungs-Option) */
    AKTE_STATE_AUSGEWAEHLT    /* Akte ausgewählt, nicht gesperrt: Ansicht (online mit Daten
                                 vom Server, sonst nur Nummer und Bezeichnung); Import und
                                 zond gehen, Änderungen erst nach "Bearbeiten" (online) */
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
    GtkWidget *btn_import;         /* Import-Dialog für die gewählte Akte */
    GtkWidget *btn_zond;           /* zond mit der Projektdatei starten */
    GtkWidget *btn_ok;
    GtkWidget *btn_speichern;
    GtkWidget *btn_abbrechen;
    
    GtkWidget *akte_fields_box;    /* Container für Akte-Felder */
    
    /* Ablage/Reaktivierung */
    GtkWidget *status_label;       /* Zeigt "Aktiv" oder "Abgelegt" */
    GtkWidget *btn_ablegen;        /* "Akte ablegen" */
    GtkWidget *btn_reaktivieren;   /* "Akte reaktivieren" */

    /* OCR */                           // NEU
    GtkWidget *btn_ocr;                 // NEU
    gchar *library_name;                // NEU - wird bei update_ui_from_node gesetzt
    GtkCssProvider *css_provider;

    /* Lock-Konflikt: wer haelt den Lock, wenn wir ihn nicht bekommen haben
     * (AKTE_STATE_READONLY), und seit wann - fuer Anzeige + "Lock brechen" */
    gchar *locked_by_other;
    gchar *locked_at_other;
    GtkWidget *btn_break_lock;
    gboolean last_save_ok;   /* Ergebnis des letzten save_node() - fuer on_ok_clicked */
    gboolean unsaved_conflict; /* Konflikt (gesperrt/geaendert): Eingaben stehen noch in der Maske, wurden aber nicht gespeichert */
} SondModuleAktePrivate;

/* Struktur für Suchergebnis */
typedef struct {
    SondGraphNode *node;    /* (nullable) nur, wenn der Server die Akte kennt */
    guint lfd_nr;
    guint year;
    gchar *regnr_display;   /* Format: "23/26" */
    gchar *kurzbezeichnung; /* (nullable) */
    gchar *gegenstand;      /* (nullable) unterscheidet gleiche Bezeichnungen */
    gboolean lokal;         /* Library im SeaDrive vorhanden */
} AkteSearchResult;

/* Forward declarations */
static void set_akte_state(SondModuleAktePrivate *priv, AkteState new_state);
static void update_ui_from_node(SondModuleAktePrivate *priv);
static void clear_akte_fields(SondModuleAktePrivate *priv);
static void update_status_display(SondModuleAktePrivate *priv);
static void suggest_last_regnr(SondModuleAktePrivate *priv);
static void apply_widget_style(GtkWidget *widget, gboolean active);
static gboolean ist_akte_aktiv(SondGraphNode *node);
static void show_akte_search_results(SondModuleAktePrivate *priv, GPtrArray *results, const gchar *search_term);
static void akte_search_result_free(AkteSearchResult *result);
static GPtrArray* search_akten_by_kurzbezeichnung(SondModuleAktePrivate *priv, const gchar *search_term, GError **error);
static void update_action_widgets(SondModuleAktePrivate *priv);
static SondGraphNode* search_node_by_regnr(SondModuleAktePrivate *priv,
                                           guint lfd_nr, guint year, GError **error);
static void select_akte(SondModuleAktePrivate *priv, guint lfd_nr, guint year,
                        const gchar *bezeichnung, const gchar *gegenstand,
                        SondGraphNode *node);

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

    /* Fokus zuerst explizit wegnehmen: Wenn ein GtkText/GtkTextView (oder
     * ein Vorfahr davon, z.B. akte_fields_box) gerade den Fokus hält und
     * per gtk_widget_set_sensitive(..., FALSE) deaktiviert wird, liefert
     * GTK4 dafür kein korrektes focus-out-Event -> "GtkText - did not
     * receive a focus-out event"-Warnung. Fix: Fokus im Fenster zuerst auf
     * nichts setzen (gtk_window_set_focus(.., NULL)), solange der/die
     * betroffenen Widgets noch sensitiv sind - das löst das normale
     * Fokus-Verlassen (inkl. focus-out) aus, bevor wir irgendetwas
     * deaktivieren. */
    GtkRoot *root = gtk_widget_get_root(priv->main_widget);
    if (GTK_IS_WINDOW(root)) {
        gtk_window_set_focus(GTK_WINDOW(root), NULL);
    }

    switch (new_state) {
        case AKTE_STATE_INITIAL:
            /* Nur "Neue Akte" + Entry aktiv */
            gtk_widget_set_sensitive(priv->regnr_entry, TRUE);
            apply_widget_style(priv->regnr_entry, TRUE);

            gtk_widget_set_sensitive(priv->btn_neue_akte, TRUE);
            gtk_widget_set_sensitive(priv->akte_fields_box, FALSE);

            apply_widget_style(priv->entry_kurzbezeichnung, FALSE);
            apply_widget_style(priv->textview_gegenstand, FALSE);

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

            gtk_widget_set_sensitive(priv->btn_ok, TRUE);
            gtk_widget_set_sensitive(priv->btn_speichern, FALSE);
            gtk_widget_set_sensitive(priv->btn_abbrechen, TRUE);

            if (priv->locked_by_other) {
                gchar *text = priv->locked_at_other ?
                    g_strdup_printf("Gesperrt von: %s (seit %s)",
                                    priv->locked_by_other, priv->locked_at_other) :
                    g_strdup_printf("Gesperrt von: %s", priv->locked_by_other);
                gtk_label_set_text(GTK_LABEL(priv->status_label), text);
                g_free(text);
            }
            break;

        case AKTE_STATE_ABGELEGT:
            /* Abgelegte Akte: Felder sichtbar aber nicht editierbar, Reaktivierung möglich */
            gtk_widget_set_sensitive(priv->regnr_entry, FALSE);
            apply_widget_style(priv->regnr_entry, FALSE);

            gtk_widget_set_sensitive(priv->btn_neue_akte, FALSE);
            gtk_widget_set_sensitive(priv->akte_fields_box, FALSE);

            apply_widget_style(priv->entry_kurzbezeichnung, FALSE);
            apply_widget_style(priv->textview_gegenstand, FALSE);

            gtk_widget_set_sensitive(priv->btn_ok, TRUE);
            gtk_widget_set_sensitive(priv->btn_speichern, FALSE);
            gtk_widget_set_sensitive(priv->btn_abbrechen, TRUE);
            gtk_widget_set_sensitive(priv->btn_reaktivieren, TRUE);
            break;

        case AKTE_STATE_AUSGEWAEHLT:
            /* Nur Ansicht ohne Sperre; ein neues Suchen ist jederzeit möglich.
             * Ablegen/Reaktivieren erst nach "Bearbeiten" (dann gesperrt). */
            gtk_widget_set_sensitive(priv->regnr_entry, TRUE);
            apply_widget_style(priv->regnr_entry, TRUE);

            gtk_widget_set_sensitive(priv->akte_fields_box, FALSE);

            apply_widget_style(priv->entry_kurzbezeichnung, FALSE);
            apply_widget_style(priv->textview_gegenstand, FALSE);

            gtk_widget_set_sensitive(priv->btn_ok, TRUE);
            gtk_widget_set_sensitive(priv->btn_speichern, FALSE);
            gtk_widget_set_sensitive(priv->btn_abbrechen, TRUE);
            gtk_widget_set_visible(priv->btn_ablegen, FALSE);
            gtk_widget_set_visible(priv->btn_reaktivieren, FALSE);
            break;
    }

    update_action_widgets(priv);
}

/* Freischaltung der Knöpfe. Es gibt keinen Online-Zustand: Serverfunktionen
 * (Neue Akte, Bearbeiten, Speichern, Ablegen ...) sind bedienbar, wo sie
 * inhaltlich Sinn haben, und scheitern beim Klick mit einer Meldung, wenn der
 * Server nicht zu erreichen ist. Import und zond brauchen nur die gewählte
 * Akte und das SeaDrive-Laufwerk. */
static void update_action_widgets(SondModuleAktePrivate *priv) {
    gboolean gewaehlt = sond_client_get_current_akte(priv->client, NULL, NULL);
    AkteState st = priv->state;

    gtk_widget_set_sensitive(priv->btn_neue_akte,
            st == AKTE_STATE_INITIAL || st == AKTE_STATE_AUSGEWAEHLT);

    gtk_widget_set_sensitive(priv->btn_import,
            gewaehlt && st != AKTE_STATE_CREATING);
    gtk_widget_set_sensitive(priv->btn_zond,
            gewaehlt && st != AKTE_STATE_CREATING);

    //"Bearbeiten": solange die Akte gewählt oder nur gesperrt angezeigt wird
    gtk_widget_set_sensitive(priv->btn_break_lock,
            gewaehlt && (st == AKTE_STATE_READONLY ||
                    st == AKTE_STATE_AUSGEWAEHLT));
}

static void clear_akte_fields(SondModuleAktePrivate *priv) {
    priv->unsaved_conflict = FALSE;
    gtk_editable_set_text(GTK_EDITABLE(priv->regnr_entry), "");
    gtk_editable_set_text(GTK_EDITABLE(priv->entry_kurzbezeichnung), "");
    gtk_editable_set_text(GTK_EDITABLE(priv->textview_gegenstand), "");  /* Jetzt Entry */
    
    /* Status zurücksetzen */
	gtk_label_set_text(GTK_LABEL(priv->status_label), "Status: -");
	gtk_widget_set_visible(priv->btn_ablegen, FALSE);
	gtk_widget_set_visible(priv->btn_reaktivieren, FALSE);

    /* Library-Name löschen */
    g_free(priv->library_name);
    priv->library_name = NULL;

    /* Andere Module sollen keine Akte mehr vorgeschlagen bekommen */
    sond_client_clear_current_akte(priv->client);

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
    g_free(result->gegenstand);
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
                result->lfd_nr = lfd_nr;
                result->year = year;
                result->regnr_display = format_regnr(lfd_nr, year);
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

/* Callback-Daten für Suchergebnis-Dialog */
typedef struct {
    SondModuleAktePrivate *priv;
    GPtrArray *results;
    GtkWidget *dialog;
    GtkWidget *listbox;
} SearchDialogData;

/**
 * select_akte:
 *
 * Wählt eine Akte aus (ohne Sperre) und zeigt sie an. Mit Anmeldung werden die
 * Daten vom Server geholt (@node oder per RegNr), ohne Anmeldung zeigt die
 * Maske nur Nummer und Bezeichnung. Gearbeitet wird erst nach "Bearbeiten".
 */
static void select_akte(SondModuleAktePrivate *priv, guint lfd_nr, guint year,
                        const gchar *bezeichnung, const gchar *gegenstand,
                        SondGraphNode *node) {
    SondGraphNode *geladen = NULL;
    const gchar *herkunft = NULL;

    //Der Server wird nur befragt, wenn schon eine Sitzung besteht (kein Login
    //von selbst); scheitert er, bleibt die lokale Ansicht
    if (!node && sond_client_is_logged_in(priv->client)) {
        GError *error = NULL;

        geladen = search_node_by_regnr(priv, lfd_nr, year, &error);
        if (!geladen && error && error->code != G_IO_ERROR_NOT_FOUND)
            herkunft = "nur lokal bekannt (Server nicht erreichbar)";
        else if (!geladen)
            herkunft = "auf dem Server nicht gefunden";
        g_clear_error(&error);
        node = geladen;
    } else if (!node)
        herkunft = "nur lokal bekannt (Server nicht befragt)";

    if (priv->current_node) {
        g_object_unref(priv->current_node);
        priv->current_node = NULL;
    }
    priv->is_new_from_entry = FALSE;
    priv->unsaved_conflict = FALSE;
    sond_client_set_last_regnr(priv->client, lfd_nr, year);

    if (node) {
        gchar *status = NULL;

        priv->current_node = g_object_ref(node);
        update_ui_from_node(priv);

        status = g_strdup_printf("%s (Daten vom Server)",
                gtk_label_get_text(GTK_LABEL(priv->status_label)));
        gtk_label_set_text(GTK_LABEL(priv->status_label), status);
        g_free(status);
    } else {
        gchar *regnr_display = format_regnr(lfd_nr, year);
        gchar *status = g_strdup_printf("Status: %s", herkunft);

        clear_akte_fields(priv);
        gtk_editable_set_text(GTK_EDITABLE(priv->regnr_entry), regnr_display);
        gtk_editable_set_text(GTK_EDITABLE(priv->entry_kurzbezeichnung),
                              bezeichnung ? bezeichnung : "");
        gtk_editable_set_text(GTK_EDITABLE(priv->textview_gegenstand),
                              gegenstand ? gegenstand : "");
        gtk_label_set_text(GTK_LABEL(priv->status_label), status);
        sond_client_set_current_akte(priv->client, lfd_nr, year);
        g_free(status);
        g_free(regnr_display);
    }

    if (geladen)
        g_object_unref(geladen);

    set_akte_state(priv, AKTE_STATE_AUSGEWAEHLT);
}

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

    select_akte(data->priv, result->lfd_nr, result->year,
                result->kurzbezeichnung, result->gegenstand, result->node);

    /* Dialog schließen */
    gtk_window_close(GTK_WINDOW(data->dialog));
}

/* Ergebnis für RegNr suchen oder neu anlegen; bei Treffer gleicher RegNr
 * werden Server- und SeaDrive-Angaben zusammengeführt. */
static AkteSearchResult* find_or_add_result(GPtrArray *results, guint lfd_nr,
                                             guint year) {
    AkteSearchResult *r = NULL;

    for (guint i = 0; i < results->len; i++) {
        r = g_ptr_array_index(results, i);
        if (r->lfd_nr == lfd_nr && r->year == year)
            return r;
    }

    r = g_new0(AkteSearchResult, 1);
    r->lfd_nr = lfd_nr;
    r->year = year;
    r->regnr_display = format_regnr(lfd_nr, year);
    g_ptr_array_add(results, r);

    return r;
}

static gint search_result_compare(gconstpointer a, gconstpointer b) {
    const AkteSearchResult *ra = *(AkteSearchResult* const*) a;
    const AkteSearchResult *rb = *(AkteSearchResult* const*) b;

    if (ra->year != rb->year)
        return ra->year < rb->year ? 1 : -1;
    if (ra->lfd_nr != rb->lfd_nr)
        return ra->lfd_nr < rb->lfd_nr ? 1 : -1;

    return 0;
}

/**
 * collect_results:
 *
 * Eine Suche für online und offline: die Libraries im SeaDrive (mit der
 * Bezeichnung aus .sond/akte oder der Merkliste) und, wenn angemeldet, der
 * Server. Gleiche Akten erscheinen einmal. Ein Serverfehler wird in
 * @server_error gemeldet, die lokalen Treffer bleiben erhalten.
 *
 * Returns: (transfer full): Array von AkteSearchResult, neueste zuerst
 */
static GPtrArray* collect_results(SondModuleAktePrivate *priv, const gchar *text,
                                  gchar **server_error) {
    GPtrArray *results = g_ptr_array_new_with_free_func(
            (GDestroyNotify)akte_search_result_free);
    gchar *root = sond_client_find_seadrive_root(priv->client, NULL);
    gchar *merkliste = sond_akten_merkliste_pfad();
    guint lfd_nr = 0;
    guint year = 0;

    /* SeaDrive */
    if (root) {
        GPtrArray *alle = sond_akten_scan(root, merkliste, NULL);

        if (alle) {
            GPtrArray *treffer = sond_akten_suchen(alle, text);

            for (guint i = 0; i < treffer->len; i++) {
                SondAkteEintrag *e = g_ptr_array_index(treffer, i);
                AkteSearchResult *r = find_or_add_result(results, e->lfd_nr,
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
        g_free(root);
    }

    /* Server, nur mit Anmeldung (sonst würde der Login-Dialog aufspringen) */
    if (sond_client_is_logged_in(priv->client)) {
        GError *error = NULL;

        if (is_valid_regnr_format(text) && parse_regnr(text, &lfd_nr, &year)) {
            SondGraphNode *node = search_node_by_regnr(priv, lfd_nr, year, &error);

            if (node) {
                AkteSearchResult *r = find_or_add_result(results, lfd_nr, year);
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
            } else if (error && error->code != G_IO_ERROR_NOT_FOUND)
                *server_error = g_strdup(error->message);
        } else {
            GPtrArray *srv = search_akten_by_kurzbezeichnung(priv, text, &error);

            if (srv) {
                for (guint i = 0; i < srv->len; i++) {
                    AkteSearchResult *s = g_ptr_array_index(srv, i);
                    AkteSearchResult *r = find_or_add_result(results, s->lfd_nr,
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
            } else if (error)
                *server_error = g_strdup(error->message);
        }
        g_clear_error(&error);

        //Bezeichnungen vom Server auch für die Offline-Suche merken
        for (guint i = 0; i < results->len; i++) {
            AkteSearchResult *r = g_ptr_array_index(results, i);

            if (r->node && r->kurzbezeichnung) {
                gchar *lib = format_regnr_storage(r->lfd_nr, r->year);

                sond_akten_merken(merkliste, lib, r->kurzbezeichnung,
                                  r->gegenstand ? r->gegenstand : "");
                g_free(lib);
            }
        }
    }

    g_free(merkliste);
    g_ptr_array_sort(results, search_result_compare);

    return results;
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
        
        gchar *label_text = g_strdup_printf("%s - %s%s%s%s", result->regnr_display,
                                           result->kurzbezeichnung ? result->kurzbezeichnung : "",
                                           (result->gegenstand && *result->gegenstand) ? "  |  " : "",
                                           result->gegenstand ? result->gegenstand : "",
                                           (result->node || !sond_client_is_logged_in(priv->client)) ? "" : "   (nur im SeaDrive)");
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
static gboolean count_von_bis(SondGraphNode *node, guint *von_count,
        guint *bis_count) {
    *von_count = 0;
    *bis_count = 0;

    if (!node) {
        return FALSE;
    }

    GPtrArray *properties = sond_graph_node_get_properties(node);
    if (!properties) {
        return FALSE;
    }

    /* Zähle "von" und "bis" Properties */
    for (guint i = 0; i < properties->len; i++) {
        SondGraphProperty *prop = g_ptr_array_index(properties, i);
        const gchar *key = sond_graph_property_get_key(prop);

        if (g_strcmp0(key, "von") == 0) {
            (*von_count)++;
        } else if (g_strcmp0(key, "bis") == 0) {
            (*bis_count)++;
        }
    }

    return TRUE;
}

static gboolean ist_akte_aktiv(SondGraphNode *node) {
    guint von_count = 0;
    guint bis_count = 0;

    if (!count_von_bis(node, &von_count, &bis_count)) {
        return FALSE;
    }

    /* Wenn mehr "von" als "bis" -> aktiv */
    return von_count > bis_count;
}

SondAkteStatus sond_module_akte_get_status(SondGraphNode *node) {
    guint von_count = 0;
    guint bis_count = 0;

    /* Ohne "von" ist die Akte nie gespeichert worden: der Node reserviert nur
     * die RegNr, Library und Projektdatei gibt es noch nicht */
    if (!count_von_bis(node, &von_count, &bis_count) || von_count == 0) {
        return SOND_AKTE_STATUS_NICHT_ANGELEGT;
    }

    return (von_count > bis_count) ? SOND_AKTE_STATUS_AKTIV :
            SOND_AKTE_STATUS_ABGELEGT;
}

gboolean sond_module_akte_parse_regnr(const gchar *regnr_str, guint *lfd_nr,
        guint *year) {
    return parse_regnr(regnr_str, lfd_nr, year);
}

gchar* sond_module_akte_library_name(guint lfd_nr, guint year) {
    return format_regnr_storage(lfd_nr, year);
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
        sond_client_set_current_akte(priv->client, lfd_nr, year);
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

    /* Library-Name (heißt wie die RegNr) für OCR merken */
    g_free(priv->library_name);
    priv->library_name = NULL;
    if (sond_graph_node_get_id(priv->current_node) > 0) {
        priv->library_name = format_regnr_storage(lfd_nr, year);
    }

    /* OCR-Button sichtbar machen wenn Library vorhanden */
	gtk_widget_set_visible(priv->btn_ocr, priv->library_name != NULL);
}

/* ========================================================================
 * Server Communication
 * ======================================================================== */
SondGraphNode* sond_module_akte_find_by_regnr(SondClient *client,
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
	GPtrArray *nodes = sond_client_search_nodes(client, criteria, error);
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

static SondGraphNode* search_node_by_regnr(SondModuleAktePrivate *priv,
		guint lfd_nr, guint year, GError **error) {
	return sond_module_akte_find_by_regnr(priv->client, lfd_nr, year, error);
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
                                      const gchar *bezeichnung,
                                      const gchar *gegenstand,
                                      GError **error) {
    /* Library-Name: Jahr-LfdNr */
    gchar *library_name = g_strdup_printf("%u-%u", year, lfd_nr);
    
    /* Beschreibung */
    gchar *desc = g_strdup_printf("Akte %u/%u", lfd_nr, year % 100);
    
    /* Client-Methode verwenden (mit Auto-Login und Re-Login bei 401) */
    gchar *library_id = sond_client_create_seafile_library(priv->client,
                                                            library_name,
                                                            desc,
                                                            bezeichnung,
                                                            gegenstand,
                                                            error);
    
    g_free(desc);
    g_free(library_name);
    
    return library_id;
}

/**
 * on_client_node_conflict:
 *
 * Reaktion auf das generische Client-Signal "node-conflict" (LOCK_NOT_HELD /
 * GONE beim Speichern oder Loeschen) fuer
 * die gerade angezeigte Akte. Die Eingaben bleiben in der Maske stehen
 * (zum Kopieren) - neu geladen wird erst auf ausdruecklichen Wunsch ueber
 * "Bearbeiten" (mit Rueckfrage). Gibt es die Akte nicht mehr, wird die Maske
 * zurueckgesetzt.
 */
static void on_client_node_conflict(SondClient *client, gint64 node_id, guint code,
                                    const gchar *locked_by, const gchar *locked_at,
                                    SondModuleAktePrivate *priv) {
    if (!priv->current_node || sond_graph_node_get_id(priv->current_node) != node_id) {
        return;
    }

    if (code == SOND_CLIENT_ERROR_GONE) {
        /* Akte selbst ist weg: Maske zuruecksetzen */
        g_object_unref(priv->current_node);
        priv->current_node = NULL;
        priv->is_new_from_entry = FALSE;
        clear_akte_fields(priv);
        set_akte_state(priv, AKTE_STATE_INITIAL);
        suggest_last_regnr(priv);
        return;
    }

    /* LOCK_NOT_HELD: Lock-Inhaber merken (falls jemand anderes ihn haelt) */
    g_free(priv->locked_by_other);
    priv->locked_by_other = NULL;
    g_free(priv->locked_at_other);
    priv->locked_at_other = NULL;
    if (code == SOND_CLIENT_ERROR_LOCK_NOT_HELD && locked_by && *locked_by) {
        priv->locked_by_other = g_strdup(locked_by);
        priv->locked_at_other = (locked_at && *locked_at) ? g_strdup(locked_at) : NULL;
    }

    set_akte_state(priv, AKTE_STATE_READONLY);
    priv->unsaved_conflict = TRUE;

    gchar *text;
    if (priv->locked_by_other) {
        text = g_strdup_printf("Nicht gespeichert - gesperrt von: %s", priv->locked_by_other);
    } else {
        text = g_strdup("Nicht gespeichert - Sperre nicht mehr gehalten");
    }
    gtk_label_set_text(GTK_LABEL(priv->status_label), text);
    g_free(text);
}

static gboolean save_node(SondModuleAktePrivate *priv, GError **error) {
    if (!priv->current_node) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Kein Node zum Speichern vorhanden");
        return FALSE;
    }
    
    sond_graph_node_set_label(priv->current_node, "Akte");
    
    /* Client-Methode verwenden (mit Auto-Login und Re-Login bei 401).
     * Konflikte meldet der Client zusaetzlich per Signal "node-conflict"
     * (s. on_client_node_conflict) - das kann current_node ersetzen, daher
     * eine eigene Referenz halten. Die Meldung zeigt der Aufrufer
     * (error->message ist benutzerlesbar). */
    SondGraphNode *node = g_object_ref(priv->current_node);
    gboolean ok = sond_client_save_node(priv->client, node, error);
    g_object_unref(node);

    priv->last_save_ok = ok;
    return ok;
}

static gboolean delete_node(SondModuleAktePrivate *priv, gint64 node_id, GError **error) {
    if (!priv->current_node || sond_graph_node_get_id(priv->current_node) != node_id) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Node %" G_GINT64_FORMAT " ist nicht der aktuelle Node", node_id);
        return FALSE;
    }

    /* Client-Methode verwenden (mit Auto-Login und Re-Login bei 401); eigene
     * Referenz halten, falls ein Signal-Handler current_node ersetzt */
    SondGraphNode *node = g_object_ref(priv->current_node);
    gboolean ok = sond_client_delete_node(priv->client, node, error);
    g_object_unref(node);
    return ok;
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

static gboolean reload_and_try_lock(SondModuleAktePrivate *priv);

/**
 * on_break_lock_dialog_response:
 *
 * Callback fuer die "Lock brechen?"-Bestaetigung. Bei "Ja": Lock des anderen
 * Users force-entsperren, dann sofort selbst sperren und in den EDITING-
 * Zustand wechseln.
 */
static void on_break_lock_dialog_response(GObject *source, GAsyncResult *result,
                                           gpointer user_data) {
    GtkAlertDialog *dialog = GTK_ALERT_DIALOG(source);
    SondModuleAktePrivate *priv = user_data;

    GError *error = NULL;
    int button = gtk_alert_dialog_choose_finish(dialog, result, &error);

    if (error) {
        LOG_ERROR("Dialog error: %s\n", error->message);
        g_error_free(error);
        return;
    }

    if (button != 0 || !priv->current_node) {  /* button == 0 -> "Ja" */
        return;
    }

    gint64 node_id = sond_graph_node_get_id(priv->current_node);
    if (node_id <= 0) {
        return;
    }

    GError *unlock_error = NULL;
    if (!sond_client_force_unlock_node(priv->client, node_id, &unlock_error)) {
        show_error_dialog(priv->main_widget, "Lock brechen fehlgeschlagen",
                          unlock_error ? unlock_error->message : "Unbekannt");
        if (unlock_error) g_error_free(unlock_error);
        return;
    }

    /* Lock neu setzen und DANACH den aktuellen Stand laden: der andere
     * Bearbeiter kann inzwischen gespeichert haben, unsere Maske zeigt noch
     * den alten Stand (und das Speichern scheiterte am fehlenden Lock). */
    if (reload_and_try_lock(priv)) {
        /* Sollte kaum vorkommen (wir haben ja gerade entsperrt), aber falls
         * doch (z.B. ein dritter User war schneller): Zustand bleibt READONLY. */
        show_error_dialog(priv->main_widget, "Übernahme fehlgeschlagen",
                          "Die Akte wurde inzwischen von jemand anderem gesperrt.");
    }
}

/* Wert so, wie er nach dem Kodieren als Dateiname und Zurücklesen aussieht
 * (getrimmt, bei Überlänge gekürzt); NULL bei leerem Wert */
static gchar* name_normalisiert(const gchar *wert) {
    gchar *kodiert = sond_akte_name_escape(wert);
    gchar *norm = NULL;

    if (!kodiert)
        return NULL;
    norm = sond_akte_name_unescape(kodiert);
    g_free(kodiert);

    return norm;
}

/* Stimmen die Dateien .sond/akte/B~... und G~... der Library mit Bezeichnung und
 * Gegenstand des (vom Server geladenen) Akten-Knotens überein? Nur wenn nicht,
 * wird der Server einmal gebeten, sie zu setzen; stimmt alles, geht nichts über
 * das Netz. So werden Akten nachgezogen, die vor dieser Funktion angelegt
 * wurden oder bei denen das Ablegen der Dateien einmal fehlgeschlagen ist.
 * Ohne erreichbare Library im SeaDrive lässt sich nichts vergleichen; dann
 * geschieht nichts. Ein Fehlschlag wird nur protokolliert. */
static void namensdateien_abgleichen(SondModuleAktePrivate *priv) {
    GPtrArray *rn = NULL;
    gchar *kurzb = NULL;
    gchar *ggstd = NULL;
    gchar *lib_name = NULL;
    gchar *root = NULL;
    gchar *lib_dir = NULL;
    gchar *bez_lokal = NULL;
    gchar *geg_lokal = NULL;
    gchar *kurzb_norm = NULL;
    gchar *ggstd_norm = NULL;
    const gchar *send_b = NULL;
    const gchar *send_g = NULL;

    if (!priv->current_node)
        return;

    rn = sond_graph_node_get_property(priv->current_node, "regnr");
    if (!rn || rn->len != 2) {
        if (rn)
            g_ptr_array_unref(rn);
        return;
    }
    lib_name = sond_module_akte_library_name(
            (guint)g_ascii_strtoull(g_ptr_array_index(rn, 1), NULL, 10),
            (guint)g_ascii_strtoull(g_ptr_array_index(rn, 0), NULL, 10));
    g_ptr_array_unref(rn);

    root = sond_client_find_seadrive_root(priv->client, NULL);
    if (!root)
        goto out;
    lib_dir = sond_import_find_library_dir(root, lib_name, NULL);
    if (!lib_dir)
        goto out;

    kurzb = sond_graph_node_get_property_string(priv->current_node, "kurzb");
    ggstd = sond_graph_node_get_property_string(priv->current_node, "ggstd");
    kurzb_norm = name_normalisiert(kurzb);
    ggstd_norm = name_normalisiert(ggstd);

    bez_lokal = sond_akten_lese_bezeichnung(lib_dir);
    geg_lokal = sond_akten_lese_gegenstand(lib_dir);

    //Bezeichnung: nur setzen, nicht entfernen; Gegenstand: auch entfernen
    if (kurzb_norm && g_strcmp0(bez_lokal, kurzb_norm) != 0)
        send_b = kurzb;
    if (g_strcmp0(geg_lokal, ggstd_norm) != 0)
        send_g = ggstd ? ggstd : "";

    if (send_b || send_g) {
        GError *error = NULL;

        LOG_INFO("Namensdateien der Library '%s' weichen von den Serverdaten "
                 "ab, werden gesetzt\n", lib_name);
        if (!sond_client_set_library_bezeichnung(priv->client, lib_name,
                send_b, send_g, &error))
            LOG_ERROR("Namensdateien der Library '%s' nicht abgeglichen: %s\n",
                      lib_name, error ? error->message : "Unbekannt");
        g_clear_error(&error);
    }

out:
    g_free(geg_lokal);
    g_free(bez_lokal);
    g_free(ggstd_norm);
    g_free(kurzb_norm);
    g_free(ggstd);
    g_free(kurzb);
    g_free(lib_dir);
    g_free(root);
    g_free(lib_name);
}

/* Lock versuchen und - erst NACH erfolgreichem Lock - die Akte vom Server neu
 * laden (sonst koennte zwischen Laden und Sperren ein anderer speichern und wir
 * wuerden veraltete Daten bearbeiten). Die Lock-Logik liegt im generischen
 * Client. Rueckgabe TRUE nur, wenn die Akte weiterhin von jemand anderem
 * gesperrt ist; bei Erfolg oder Fehler (Dialog bereits angezeigt) FALSE. */
static gboolean reload_and_try_lock(SondModuleAktePrivate *priv) {
    GPtrArray *regnr_values = sond_graph_node_get_property(priv->current_node, "regnr");
    if (!regnr_values || regnr_values->len != 2) {
        show_error_dialog(priv->main_widget, "Fehler", "Akte hat keine gültige RegNr");
        if (regnr_values) g_ptr_array_unref(regnr_values);
        return FALSE;
    }
    guint year = (guint)g_ascii_strtoull(g_ptr_array_index(regnr_values, 0), NULL, 10);
    guint lfd_nr = (guint)g_ascii_strtoull(g_ptr_array_index(regnr_values, 1), NULL, 10);
    g_ptr_array_unref(regnr_values);

    gint64 node_id = sond_graph_node_get_id(priv->current_node);
    GError *lock_error = NULL;
    gchar *locked_by = NULL, *locked_at = NULL;

    if (!sond_client_lock_node_ex(priv->client, node_id, "Bearbeitung",
                                  &locked_by, &locked_at, &lock_error)) {
        /* Weiterhin gesperrt: aktuellen Sperrer anzeigen, READONLY bleibt */
        g_free(priv->locked_by_other);
        priv->locked_by_other = locked_by;
        g_free(priv->locked_at_other);
        priv->locked_at_other = locked_at;
        if (lock_error) g_error_free(lock_error);

        /* Auch die Leseansicht auf den aktuellen Stand bringen (kein Lock noetig) */
        GError *load_error = NULL;
        SondGraphNode *fresh = search_node_by_regnr(priv, lfd_nr, year, &load_error);
        if (fresh) {
            g_object_unref(priv->current_node);
            priv->current_node = fresh;
        }
        if (load_error) g_error_free(load_error);

        priv->unsaved_conflict = FALSE;
        update_ui_from_node(priv);
        set_akte_state(priv, AKTE_STATE_READONLY);
        return TRUE;
    }
    g_free(locked_by);
    g_free(locked_at);

    /* Lock gehalten -> jetzt erst aktuellen Stand laden */
    GError *error = NULL;
    SondGraphNode *node = search_node_by_regnr(priv, lfd_nr, year, &error);
    if (!node) {
        show_error_dialog(priv->main_widget, "Fehler beim Neuladen",
                          error ? error->message : "Akte nicht gefunden");
        if (error) g_error_free(error);
        sond_client_unlock_node(priv->client, node_id, NULL);  /* Lock wieder abgeben */
        return FALSE;
    }

    g_object_unref(priv->current_node);
    priv->current_node = node;

    g_free(priv->locked_by_other);
    priv->locked_by_other = NULL;
    g_free(priv->locked_at_other);
    priv->locked_at_other = NULL;

    //Akte ist gesperrt und frisch geladen: Namensdateien in der Library prüfen
    namensdateien_abgleichen(priv);

    priv->unsaved_conflict = FALSE;
    update_ui_from_node(priv);
    set_akte_state(priv, ist_akte_aktiv(priv->current_node) ?
                   AKTE_STATE_EDITING : AKTE_STATE_ABGELEGT);
    return FALSE;
}

static void start_edit_attempt(SondModuleAktePrivate *priv) {
    if (!priv->current_node) {
        return;
    }

    /* Erst pruefen, ob der Lock inzwischen frei ist; nur wenn nicht, nachfragen. */
    if (!reload_and_try_lock(priv)) {
        return;
    }

    gchar *detail = priv->locked_by_other ?
        g_strdup_printf("Diese Akte wird gerade von '%s' bearbeitet%s%s.\n\n"
                        "Lock trotzdem übernehmen? Der andere Bearbeiter verliert "
                        "dadurch ohne Vorwarnung seinen Zugriff.",
                        priv->locked_by_other,
                        priv->locked_at_other ? " (gesperrt seit: " : "",
                        priv->locked_at_other ? priv->locked_at_other : "") :
        g_strdup("Diese Akte wird gerade von einem anderen Benutzer bearbeitet.\n\n"
                "Lock trotzdem übernehmen? Der andere Bearbeiter verliert dadurch "
                "ohne Vorwarnung seinen Zugriff.");

    GtkAlertDialog *dialog = gtk_alert_dialog_new("Lock brechen?");
    gtk_alert_dialog_set_detail(dialog, detail);
    g_free(detail);

    const char *buttons[] = {"Ja", "Nein", NULL};
    gtk_alert_dialog_set_buttons(dialog, buttons);
    gtk_alert_dialog_set_default_button(dialog, 1);  /* "Nein" als Standard - bewusste Entscheidung */
    gtk_alert_dialog_set_cancel_button(dialog, 1);
    gtk_alert_dialog_set_modal(dialog, TRUE);

    gtk_alert_dialog_choose(dialog,
                           GTK_WINDOW(gtk_widget_get_root(priv->main_widget)),
                           NULL,
                           on_break_lock_dialog_response,
                           priv);

    g_object_unref(dialog);
}

static void on_discard_confirm_response(GObject *source, GAsyncResult *result,
                                         gpointer user_data) {
    GtkAlertDialog *dialog = GTK_ALERT_DIALOG(source);
    SondModuleAktePrivate *priv = user_data;

    GError *error = NULL;
    int button = gtk_alert_dialog_choose_finish(dialog, result, &error);
    if (error) {
        LOG_ERROR("Dialog error: %s\n", error->message);
        g_error_free(error);
        return;
    }

    if (button != 0) {   /* 0 = "Verwerfen und neu laden" */
        return;
    }

    priv->unsaved_conflict = FALSE;
    start_edit_attempt(priv);
}

/* Button "Bearbeiten" (nur im Lesemodus sichtbar) */
/* Eine nur lokal gewählte Akte vom Server laden (mit Login, falls nötig), um sie
 * bearbeiten zu können. Scheitert das, bleibt die lokale Ansicht unverändert. */
static gboolean load_node_from_server(SondModuleAktePrivate *priv) {
    guint lfd_nr = 0;
    guint year = 0;
    GError *error = NULL;
    SondGraphNode *node = NULL;

    if (!sond_client_get_current_akte(priv->client, &lfd_nr, &year))
        return FALSE;

    if (!sond_client_login(priv->client, &error)) {
        show_error_dialog(priv->main_widget, "Server nicht erreichbar",
                "Die Akte kann nicht bearbeitet werden, weil keine Verbindung "
                "zum Server besteht. Es wurde nichts geändert.");
        g_clear_error(&error);
        return FALSE;
    }

    node = search_node_by_regnr(priv, lfd_nr, year, &error);
    if (!node) {
        show_error_dialog(priv->main_widget, "Akte nicht geladen",
                error ? error->message : "Die Akte wurde auf dem Server nicht gefunden.");
        g_clear_error(&error);
        return FALSE;
    }

    priv->current_node = node;
    update_ui_from_node(priv);
    set_akte_state(priv, AKTE_STATE_AUSGEWAEHLT);   //Ablegen usw. bleiben bis zur Sperre verborgen

    return TRUE;
}

static void on_break_lock_clicked(GtkButton *button, SondModuleAktePrivate *priv) {
    if (!priv->current_node && !load_node_from_server(priv)) {
        return;
    }

    /* Stehen noch nicht gespeicherte Eingaben in der Maske (nach einem
     * Konflikt), wuerde das Neuladen sie verwerfen -> vorher fragen. */
    if (priv->unsaved_conflict) {
        GtkAlertDialog *dialog = gtk_alert_dialog_new("Eingaben verwerfen?");
        gtk_alert_dialog_set_detail(dialog,
            "Ihre zuletzt eingegebenen Aenderungen wurden nicht gespeichert. "
            "Sie gehen verloren, wenn die Akte neu geladen wird.\n\n"
            "Tipp: Kopieren Sie den Text vorher aus der Maske.");

        const char *buttons[] = {"Verwerfen und neu laden", "Abbrechen", NULL};
        gtk_alert_dialog_set_buttons(dialog, buttons);
        gtk_alert_dialog_set_default_button(dialog, 1);
        gtk_alert_dialog_set_cancel_button(dialog, 1);
        gtk_alert_dialog_set_modal(dialog, TRUE);

        gtk_alert_dialog_choose(dialog,
                               GTK_WINDOW(gtk_widget_get_root(priv->main_widget)),
                               NULL,
                               on_discard_confirm_response,
                               priv);
        g_object_unref(dialog);
        return;
    }

    start_edit_attempt(priv);
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

/* Nachfrage, ob eine noch nicht vorhandene Akte angelegt werden soll (online) */
static void ask_new_akte(SondModuleAktePrivate *priv, guint lfd_nr, guint year) {
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

/* Enter im Suchfeld: RegNr oder Suchtext. Gesucht wird im SeaDrive und, wenn
 * angemeldet, auf dem Server. Ein einziger Treffer wird gleich gewählt, bei
 * mehreren erscheint die Trefferliste. Die Akte wird dabei nicht gesperrt. */
static void on_regnr_entry_activate(GtkEntry *entry, SondModuleAktePrivate *priv) {
    guint lfd_nr = 0;
    guint year = 0;
    gchar *server_error = NULL;
    gchar *such = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(entry))));
    GPtrArray *results = NULL;
    gboolean online = sond_client_is_logged_in(priv->client);

    if (!*such) {
        g_free(such);
        return;
    }

    results = collect_results(priv, such, &server_error);

    if (results->len == 1) {
        AkteSearchResult *r = g_ptr_array_index(results, 0);

        select_akte(priv, r->lfd_nr, r->year, r->kurzbezeichnung, r->gegenstand,
                    r->node);
    } else if (results->len > 1) {
        show_akte_search_results(priv, results, such);
    } else if (server_error) {
        gchar *msg = g_strdup_printf("Fehler bei der Suche: %s", server_error);

        show_error_dialog(priv->main_widget, "Suchfehler", msg);
        g_free(msg);
    } else if (online && is_valid_regnr_format(such) &&
               parse_regnr(such, &lfd_nr, &year)) {
        sond_client_set_last_regnr(priv->client, lfd_nr, year);
        ask_new_akte(priv, lfd_nr, year);
    } else {
        gchar *msg = g_strdup_printf("Keine Akten gefunden mit Suchbegriff: \"%s\"%s",
                such, online ? "" :
                "\n\nDer Server wurde nicht befragt (keine Sitzung): gesucht wurde nur im SeaDrive-Laufwerk.");

        show_info_dialog(priv->main_widget, "Keine Ergebnisse", msg);
        g_free(msg);
    }

    g_free(server_error);
    g_ptr_array_unref(results);
    g_free(such);
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
            library_id = create_seafile_library(priv, year, lfd_nr,
                    kurzbezeichnung, gegenstand, &lib_error);

            if (!library_id) {
                /* Library-Erstellung fehlgeschlagen - Node wieder löschen (Rollback) */
                LOG_ERROR("Library-Erstellung fehlgeschlagen, lösche Node (Rollback)...\n");

                gint64 node_id = sond_graph_node_get_id(priv->current_node);
                GError *del_error = NULL;

                /* Delete (nur mit eigenem Lock moeglich; die Lock-Zeile verschwindet
                 * serverseitig mit dem Knoten). Nur bei Fehlschlag Lock abgeben. */
                if (!delete_node(priv, node_id, &del_error)) {
                    LOG_ERROR("Rollback fehlgeschlagen: %s\n",
                             del_error ? del_error->message : "Unbekannt");
                    if (del_error) g_error_free(del_error);
                    sond_client_unlock_node(priv->client, node_id, NULL);
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
            gchar *library_id = create_seafile_library(priv, year, lfd,
                    kurzbezeichnung, gegenstand, &error);

            if (!library_id) {
                /* Library-Erstellung fehlgeschlagen - Node wieder löschen (Rollback).
                 * Einheitlich mit dem "Neue Akte"-Button-Pfad weiter oben: bei
                 * fehlgeschlagener Library-Erstellung bleibt keine Akte ohne
                 * Library bestehen, unabhängig vom Erstellungsweg. */
                LOG_ERROR("Library-Erstellung fehlgeschlagen, lösche Node (Rollback)...\n");

                gint64 node_id = sond_graph_node_get_id(priv->current_node);
                GError *del_error = NULL;

                /* Delete (nur mit eigenem Lock moeglich; die Lock-Zeile verschwindet
                 * serverseitig mit dem Knoten). Nur bei Fehlschlag Lock abgeben. */
                if (!delete_node(priv, node_id, &del_error)) {
                    LOG_ERROR("Rollback fehlgeschlagen: %s\n",
                             del_error ? del_error->message : "Unbekannt");
                    if (del_error) g_error_free(del_error);
                    sond_client_unlock_node(priv->client, node_id, NULL);
                }

                /* Fehler anzeigen */
                gchar *msg = g_strdup_printf("Fehler beim Erstellen der Seafile Library: %s\n\n"
                                            "Node wurde zurückgerollt.",
                                            error ? error->message : "Unbekannt");
                show_error_dialog(priv->main_widget, "Library-Erstellung fehlgeschlagen", msg);
                g_free(msg);
                if (error) g_error_free(error);

                /* current_node ungültig machen */
                g_object_unref(priv->current_node);
                priv->current_node = NULL;

                return;
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
    gchar *kurzb_alt = sond_graph_node_get_property_string(priv->current_node, "kurzb");
    gchar *ggstd_alt = sond_graph_node_get_property_string(priv->current_node, "ggstd");
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

        /* Geänderte Bezeichnung oder geänderter Gegenstand: auch die Dateien in
         * der Library nachführen (nur Zusatzangaben für die Suche ohne Server;
         * Fehler nur melden) */
        gboolean kurzb_neu = kurzbezeichnung && *kurzbezeichnung &&
                g_strcmp0(kurzb_alt, kurzbezeichnung) != 0;
        gboolean ggstd_neu = gegenstand && *gegenstand &&
                g_strcmp0(ggstd_alt, gegenstand) != 0;

        if (kurzb_neu || ggstd_neu) {
            GPtrArray *rn = sond_graph_node_get_property(priv->current_node, "regnr");

            if (rn && rn->len == 2) {
                guint year = (guint)g_ascii_strtoull(g_ptr_array_index(rn, 0), NULL, 10);
                guint lfd = (guint)g_ascii_strtoull(g_ptr_array_index(rn, 1), NULL, 10);
                gchar *lib_name = sond_module_akte_library_name(lfd, year);
                GError *name_error = NULL;

                if (!sond_client_set_library_bezeichnung(priv->client, lib_name,
                        kurzb_neu ? kurzbezeichnung : NULL,
                        ggstd_neu ? gegenstand : NULL, &name_error))
                    LOG_ERROR("Aktenbezeichnung in Library '%s' nicht "
                            "nachgeführt: %s\n", lib_name, name_error ?
                            name_error->message : "Unbekannt");
                g_clear_error(&name_error);
                g_free(lib_name);
            }
            if (rn)
                g_ptr_array_unref(rn);
        }

        show_info_dialog(priv->main_widget, "Gespeichert", "Akte wurde erfolgreich gespeichert.");
    } else {
        gchar *msg = g_strdup_printf("Fehler: %s", error ? error->message : "Unbekannt");
        show_error_dialog(priv->main_widget, "Speichern fehlgeschlagen", msg);
        g_free(msg);
        if (error) g_error_free(error);
    }
    g_free(kurzb_alt);
    g_free(ggstd_alt);
}

/* Akte freigeben (Lock abgeben) und Maske zuruecksetzen - gemeinsamer
 * Abschluss von "Ok", ggf. nach der Rueckfrage zum Verwerfen. */
static void finish_akte(SondModuleAktePrivate *priv) {
    /* Lock freigeben falls Node in DB. Unabhaengig von is_new_from_entry -
     * das Flag wird beim vorangehenden on_speichern_clicked() bereits auf
     * FALSE zurueckgesetzt (normaler Speichervorgang) und ist daher an
     * dieser Stelle kein brauchbares Unterscheidungsmerkmal mehr: "Ok"
     * heisst in jedem Fall "fertig bearbeitet", egal ob die Akte neu war
     * oder schon existierte - entsperren wie bei on_abbrechen_clicked(). */
    gint64 node_id = priv->current_node ? sond_graph_node_get_id(priv->current_node) : 0;
    if (node_id > 0) {
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

static void on_ok_discard_response(GObject *source, GAsyncResult *result,
                                    gpointer user_data) {
    GtkAlertDialog *dialog = GTK_ALERT_DIALOG(source);
    SondModuleAktePrivate *priv = user_data;

    GError *error = NULL;
    int button = gtk_alert_dialog_choose_finish(dialog, result, &error);
    if (error) {
        LOG_ERROR("Dialog error: %s\n", error->message);
        g_error_free(error);
        return;
    }

    if (button == 0) {   /* 0 = "Verwerfen" */
        finish_akte(priv);
    }
}

static void on_ok_clicked(GtkButton *button, SondModuleAktePrivate *priv) {
    /* Nur ausgewählt (nichts gesperrt, nichts zu speichern): Auswahl beenden */
    if (priv->state == AKTE_STATE_AUSGEWAEHLT) {
        finish_akte(priv);
        return;
    }

    if (!priv->current_node) {
        show_error_dialog(priv->main_widget, "Nichts zu speichern",
                         "Bitte erst eine Akte laden oder neu erstellen.");
        return;
    }

    /* Lesemodus nach einem Konflikt: die Eingaben in der Maske wurden nicht
     * gespeichert - "Ok" wuerde sie verwerfen, daher vorher fragen. */
    if (priv->state == AKTE_STATE_READONLY && priv->unsaved_conflict) {
        GtkAlertDialog *dialog = gtk_alert_dialog_new("Eingaben verwerfen?");
        gtk_alert_dialog_set_detail(dialog,
            "Ihre zuletzt eingegebenen Aenderungen wurden nicht gespeichert und "
            "gehen verloren, wenn Sie die Akte jetzt schliessen.\n\n"
            "Tipp: Kopieren Sie den Text vorher aus der Maske.");

        const char *buttons[] = {"Verwerfen", "Abbrechen", NULL};
        gtk_alert_dialog_set_buttons(dialog, buttons);
        gtk_alert_dialog_set_default_button(dialog, 1);
        gtk_alert_dialog_set_cancel_button(dialog, 1);
        gtk_alert_dialog_set_modal(dialog, TRUE);

        gtk_alert_dialog_choose(dialog,
                               GTK_WINDOW(gtk_widget_get_root(priv->main_widget)),
                               NULL,
                               on_ok_discard_response,
                               priv);
        g_object_unref(dialog);
        return;
    }

    /* Erst speichern. Schlaegt das Speichern fehl (Dialog wurde bereits
     * gezeigt), bleibt die Akte offen - sonst gingen die Eingaben verloren. */
    if (priv->state != AKTE_STATE_READONLY) {   /* Lesemodus: nichts zu speichern */
        priv->last_save_ok = FALSE;
        on_speichern_clicked(button, priv);

        if (priv->current_node && !priv->last_save_ok) {
            return;
        }
    }

    finish_akte(priv);
}

static void on_abbrechen_clicked(GtkButton *button, SondModuleAktePrivate *priv) {
    /* Wenn über Entry neu angelegt und noch nicht gespeichert: Löschen */
    if (priv->is_new_from_entry && priv->current_node && sond_graph_node_get_id(priv->current_node) > 0) {
        gint64 node_id = sond_graph_node_get_id(priv->current_node);
        LOG_INFO("Lösche nicht gespeicherte Entry-Akte (ID %" G_GINT64_FORMAT ")\n", node_id);
        
        /* Delete nur mit eigenem Lock moeglich: erst loeschen (Lock-Zeile
         * verschwindet mit dem Knoten), nur bei Fehlschlag Lock abgeben */
        GError *error = NULL;
        if (!delete_node(priv, node_id, &error)) {
            LOG_ERROR("Fehler beim Löschen der Akte: %s\n", error ? error->message : "Unbekannt");
            if (error) g_error_free(error);
            sond_client_unlock_node(priv->client, node_id, NULL);
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

/**
 * on_main_box_destroy:
 *
 * "destroy"-Handler fuer main_box. Bisher wurde beim Zerstoeren des Widgets
 * (z.B. Tab-Wechsel/Fenster schliessen, ohne vorher "Ok" oder "Abbrechen" zu
 * klicken) nichts aufgeraeumt - der g_object_set_data_full()-Destroy-Callback
 * fuer "priv" ruft nur g_free() auf priv selbst auf, ohne Lock, current_node,
 * client oder css_provider freizugeben. Das fuehrt dazu, dass eine noch
 * gesperrte Akte fuer immer gesperrt bleibt. Unlock-Versuch unbedingt bei
 * node_id > 0 (wie in on_abbrechen_clicked()/on_ok_clicked(); ein Unlock fuer
 * eine nicht von uns gehaltene Sperre (AKTE_STATE_READONLY) ist serverseitig
 * ein harmloser No-Op, s. handle_node_unlock() in sond_server.c).
 */
static void on_main_box_destroy(GtkWidget *widget, gpointer user_data) {
    SondModuleAktePrivate *priv = (SondModuleAktePrivate *) user_data;

    if (priv->current_node) {
        gint64 node_id = sond_graph_node_get_id(priv->current_node);
        if (node_id > 0) {
            /* sond_client_unlock_node() ist jetzt generisch selbst ein
             * No-Op, wenn wir den Lock laut held_locks gar nicht halten
             * (z.B. READONLY-Anzeige einer von jemand anderem gelockten
             * Akte) - keine Akte-spezifische Sonderbehandlung hier noetig. */
            GError *error = NULL;
            if (!sond_client_unlock_node(priv->client, node_id, &error)) {
                LOG_ERROR("Unlock beim Zerstoeren fehlgeschlagen: %s\n",
                          error ? error->message : "Unbekannt");
                if (error) g_error_free(error);
            }
        }
        g_object_unref(priv->current_node);
        priv->current_node = NULL;
    }

    if (priv->css_provider) {
        /* setup_css_provider() registriert den Provider global am Display
         * (gtk_style_context_add_provider_for_display) - muss daher beim
         * Zerstoeren des Moduls explizit wieder entfernt werden, sonst bleibt
         * er dauerhaft (und bei jedem erneuten Oeffnen des Moduls ein
         * weiteres Mal) am Display registriert. */
        gtk_style_context_remove_provider_for_display(
            gdk_display_get_default(),
            GTK_STYLE_PROVIDER(priv->css_provider));
        g_object_unref(priv->css_provider);
        priv->css_provider = NULL;
    }

    g_free(priv->library_name);
    priv->library_name = NULL;

    g_free(priv->locked_by_other);
    priv->locked_by_other = NULL;
    g_free(priv->locked_at_other);
    priv->locked_at_other = NULL;

    g_signal_handlers_disconnect_by_data(priv->client, priv);
    g_object_unref(priv->client);
    priv->client = NULL;
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

/* =======================================================================
 * Anmelden, Import, zond
 * ======================================================================= */

/* Import-Dialog für die gewählte Akte (geht auch offline) */
static void on_import_clicked(GtkButton *button, SondModuleAktePrivate *priv) {
    guint lfd_nr = 0;
    guint year = 0;

    if (!sond_client_get_current_akte(priv->client, &lfd_nr, &year))
        return;

    sond_import_dialog_show(priv->client,
            GTK_WINDOW(gtk_widget_get_root(priv->main_widget)), lfd_nr, year,
            gtk_editable_get_text(GTK_EDITABLE(priv->entry_kurzbezeichnung)));
}

/* zond mit der Projektdatei der gewählten Akte starten (geht auch offline,
 * sofern SeaDrive die Datei im Cache hat) */
static void on_zond_clicked(GtkButton *button, SondModuleAktePrivate *priv) {
    guint lfd_nr = 0;
    guint year = 0;
    gchar *root = NULL;
    gchar *lib_name = NULL;
    gchar *lib_dir = NULL;
    gchar *exe = NULL;
    gchar *project = NULL;
    GError *error = NULL;

    if (!sond_client_get_current_akte(priv->client, &lfd_nr, &year))
        return;

    lib_name = sond_module_akte_library_name(lfd_nr, year);

    root = sond_client_find_seadrive_root(priv->client, &error);
    if (!root) {
        show_error_dialog(priv->main_widget, "SeaDrive nicht gefunden",
                error ? error->message : "unbekannt");
        goto out;
    }

    lib_dir = sond_import_find_library_dir(root, lib_name, &error);
    if (!lib_dir) {
        show_error_dialog(priv->main_widget,
                "Akte im SeaDrive-Laufwerk nicht erreichbar",
                error ? error->message : "unbekannt");
        goto out;
    }

    exe = sond_zond_find_exe(sond_client_get_zond_exe(priv->client), &error);
    if (!exe) {
        show_error_dialog(priv->main_widget, "zond nicht gefunden",
                error ? error->message : "unbekannt");
        goto out;
    }

    project = sond_zond_project_file(lib_dir, lib_name);
    if (!sond_zond_start(exe, project, &error))
        show_error_dialog(priv->main_widget, "zond konnte nicht gestartet werden",
                error ? error->message : "unbekannt");

out:
    g_clear_error(&error);
    g_free(project);
    g_free(exe);
    g_free(lib_dir);
    g_free(root);
    g_free(lib_name);
}

/* ========================================================================
 * Public API
 * ======================================================================== */

GtkWidget* sond_module_akte_new(SondClient *client) {
    SondModuleAktePrivate *priv = g_new0(SondModuleAktePrivate, 1);
    priv->client = g_object_ref(client);

    /* Generische Konflikt-Meldungen des Clients (gesperrt/geaendert/geloescht) */
    g_signal_connect(priv->client, "node-conflict", G_CALLBACK(on_client_node_conflict), priv);
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

    /* Zwei Fragen untereinander: Welche Akte? Was soll mit ihr geschehen? */
    GtkWidget *top_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(top_grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(top_grid), 12);
    gtk_box_append(GTK_BOX(main_box), top_grid);

    GtkWidget *frage_akte = gtk_label_new("Welche Akte?");
    gtk_widget_add_css_class(frage_akte, "heading");
    gtk_widget_set_halign(frage_akte, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(top_grid), frage_akte, 0, 0, 1, 1);

    GtkWidget *wahl_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_grid_attach(GTK_GRID(top_grid), wahl_box, 1, 0, 1, 1);

    priv->regnr_entry = gtk_entry_new();
    gtk_editable_set_width_chars(GTK_EDITABLE(priv->regnr_entry), 30);
    gtk_entry_set_placeholder_text(GTK_ENTRY(priv->regnr_entry),
            "Registernummer oder Suchtext, dann Enter");
    g_signal_connect(priv->regnr_entry, "activate", G_CALLBACK(on_regnr_entry_activate), priv);
    gtk_box_append(GTK_BOX(wahl_box), priv->regnr_entry);

    priv->btn_neue_akte = gtk_button_new_with_label("Neue Akte");
    g_signal_connect(priv->btn_neue_akte, "clicked", G_CALLBACK(on_neue_akte_clicked), priv);
    gtk_box_append(GTK_BOX(wahl_box), priv->btn_neue_akte);

    GtkWidget *frage_tun = gtk_label_new("Was soll geschehen?");
    gtk_widget_add_css_class(frage_tun, "heading");
    gtk_widget_set_halign(frage_tun, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(top_grid), frage_tun, 0, 1, 1, 1);

    GtkWidget *tun_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_grid_attach(GTK_GRID(top_grid), tun_box, 1, 1, 1, 1);

    priv->btn_import = gtk_button_new_with_label("Import ...");
    gtk_widget_set_tooltip_text(priv->btn_import,
            "Dokumente in die gewählte Akte importieren");
    g_signal_connect(priv->btn_import, "clicked", G_CALLBACK(on_import_clicked), priv);
    gtk_box_append(GTK_BOX(tun_box), priv->btn_import);

    priv->btn_zond = gtk_button_new_with_label("zond starten");
    gtk_widget_set_tooltip_text(priv->btn_zond,
            "Die Projektdatei der gewählten Akte in zond öffnen");
    g_signal_connect(priv->btn_zond, "clicked", G_CALLBACK(on_zond_clicked), priv);
    gtk_box_append(GTK_BOX(tun_box), priv->btn_zond);

    priv->btn_break_lock = gtk_button_new_with_label("Bearbeiten");
    gtk_widget_set_tooltip_text(priv->btn_break_lock,
            "Aktendaten bearbeiten (die Akte wird dafür gesperrt)");
    g_signal_connect(priv->btn_break_lock, "clicked", G_CALLBACK(on_break_lock_clicked), priv);
    gtk_box_append(GTK_BOX(tun_box), priv->btn_break_lock);

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

    /* Aufraeumen (Unlock, current_node/client unref, css_provider entfernen)
     * beim Zerstoeren des Widgets - s. on_main_box_destroy(). */
    g_signal_connect(main_box, "destroy", G_CALLBACK(on_main_box_destroy), priv);

    /* Initial State setzen */
    set_akte_state(priv, AKTE_STATE_INITIAL);

    return main_box;
}
