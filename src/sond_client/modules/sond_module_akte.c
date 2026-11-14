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
#include "sond_archiv_kandidaten.h"
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
    gchar *lock_reason_other;   /* Grund der fremden Sperre (nullable) */
    GtkWidget *btn_break_lock;
    gboolean last_save_ok;   /* Ergebnis des letzten save_node() - fuer on_ok_clicked */
    gboolean unsaved_conflict; /* Konflikt (gesperrt/geaendert): Eingaben stehen noch in der Maske, wurden aber nicht gespeichert */
    gboolean ablegen_vorgemerkt;      /* "Akte ablegen" gedrückt: bis/Ablagenr. kommen erst beim Speichern dazu */
    gboolean reaktivieren_vorgemerkt; /* "Akte reaktivieren" gedrückt: von kommt erst beim Speichern dazu */
    gboolean ok_laeuft;               /* "Ok" ruft das Speichern auf: dann keine Erfolgsmeldung */
    gboolean wiederherstellung_vorgemerkt; /* archivierte Akte: "Ok" stößt die Wiederherstellung an */
    gchar *wh_library;                /* dazu: Library-Name */
    gchar *wh_archiv;                 /* dazu: Archivname */
    gchar *wh_status;                 /* Anzeige zum Stand einer laufenden Wiederherstellung (nullable) */
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

/* Wie weit der Server an einer Suche beteiligt war */
typedef enum {
    SERVER_NICHT_BEFRAGT,   /* keine Sitzung: es wurde nur lokal gesucht */
    SERVER_OK,              /* hat geantwortet (auch "nicht gefunden") */
    SERVER_FEHLER           /* nicht erreichbar oder Fehler */
} SuchServer;

/* Zeitgrenze für Suche und Laden einer Akte (Sekunden), damit das Fenster bei
 * toter Verbindung nicht minutenlang steht; andere Serveraufrufe behalten
 * ihre Voreinstellung */
#define SERVER_KURZ_TIMEOUT 5

/* Forward declarations */
static void set_akte_state(SondModuleAktePrivate *priv, AkteState new_state);
static void finish_akte(SondModuleAktePrivate *priv);
static void wh_beim_auswaehlen(SondModuleAktePrivate *priv);
static void wiederherstellung_vormerkung_loeschen(SondModuleAktePrivate *priv);
static void update_ui_from_node(SondModuleAktePrivate *priv);
static void clear_akte_fields(SondModuleAktePrivate *priv);
static void update_status_display(SondModuleAktePrivate *priv);
static void suggest_last_regnr(SondModuleAktePrivate *priv);
static void apply_widget_style(GtkWidget *widget, gboolean active);
static gboolean ist_akte_aktiv(SondGraphNode *node);
static void show_akte_search_results(SondModuleAktePrivate *priv, GPtrArray *results, const gchar *search_term, SuchServer server);
static void akte_search_result_free(AkteSearchResult *result);
static GPtrArray* search_akten_by_kurzbezeichnung(SondModuleAktePrivate *priv, const gchar *search_term, GError **error);
static void update_action_widgets(SondModuleAktePrivate *priv);
static SondGraphNode* search_node_by_regnr(SondModuleAktePrivate *priv,
                                           guint lfd_nr, guint year, GError **error);
static void select_akte(SondModuleAktePrivate *priv, guint lfd_nr, guint year,
                        const gchar *bezeichnung, const gchar *gegenstand,
                        SondGraphNode *node, SuchServer server);

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

            /* Keine Bearbeitungssitzung: nichts zu bestätigen oder zu verwerfen */
            gtk_widget_set_sensitive(priv->btn_ok, FALSE);
            gtk_widget_set_sensitive(priv->btn_speichern, FALSE);
            gtk_widget_set_sensitive(priv->btn_abbrechen, FALSE);
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
                gchar *grund = (priv->lock_reason_other &&
                        g_strcmp0(priv->lock_reason_other, SOND_LOCK_REASON_BEARBEITUNG)) ?
                        g_strdup_printf("%s, ", priv->lock_reason_other) : g_strdup("");
                gchar *text = priv->locked_at_other ?
                    g_strdup_printf("Gesperrt von: %s (%sseit %s)",
                                    priv->locked_by_other, grund, priv->locked_at_other) :
                    g_strdup_printf("Gesperrt von: %s", priv->locked_by_other);
                g_free(grund);
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
            gtk_widget_set_sensitive(priv->btn_speichern, priv->ablegen_vorgemerkt || priv->reaktivieren_vorgemerkt);
            gtk_widget_set_sensitive(priv->btn_abbrechen, TRUE);
            //bei vorgemerkter Wiederherstellung bleibt nur Ok oder Abbrechen
            gtk_widget_set_sensitive(priv->btn_reaktivieren, !priv->wiederherstellung_vorgemerkt);
            break;

        case AKTE_STATE_AUSGEWAEHLT:
            /* Nur Ansicht ohne Sperre; ein neues Suchen ist jederzeit möglich.
             * Ablegen/Reaktivieren erst nach "Bearbeiten" (dann gesperrt). */
            gtk_widget_set_sensitive(priv->regnr_entry, TRUE);
            apply_widget_style(priv->regnr_entry, TRUE);

            gtk_widget_set_sensitive(priv->akte_fields_box, FALSE);

            apply_widget_style(priv->entry_kurzbezeichnung, FALSE);
            apply_widget_style(priv->textview_gegenstand, FALSE);

            /* Keine Sperre gehalten: nichts zu bestätigen oder zu verwerfen */
            gtk_widget_set_sensitive(priv->btn_ok, FALSE);
            gtk_widget_set_sensitive(priv->btn_speichern, FALSE);
            gtk_widget_set_sensitive(priv->btn_abbrechen, FALSE);
            gtk_widget_set_visible(priv->btn_ablegen, FALSE);
            gtk_widget_set_visible(priv->btn_reaktivieren, FALSE);
            break;
    }

    update_action_widgets(priv);
}

/* Die geladene Akte ist (gespeichert) abgelegt, auch wenn sie archiviert ist oder
 * gerade wiederhergestellt wird. Eine Akte ohne "von" (noch nicht angelegt) und
 * die Offline-Anzeige ohne Node zählen nicht. */
static gboolean akte_ist_abgelegt(SondModuleAktePrivate *priv) {
    GPtrArray *von_props = NULL;
    gboolean materiell = FALSE;

    if (!priv->current_node || sond_graph_node_get_id(priv->current_node) <= 0)
        return FALSE;

    von_props = sond_graph_node_get_property(priv->current_node, "von");
    if (von_props) {
        materiell = von_props->len > 0;
        g_ptr_array_unref(von_props);
    }

    return materiell && !ist_akte_aktiv(priv->current_node);
}

/* Freischaltung der Knöpfe. Es gibt keinen Online-Zustand: Serverfunktionen
 * (Neue Akte, Bearbeiten, Speichern, Ablegen ...) sind bedienbar, wo sie
 * inhaltlich Sinn haben, und scheitern beim Klick mit einer Meldung, wenn der
 * Server nicht zu erreichen ist. Import und zond brauchen nur die gewählte
 * Akte und das SeaDrive-Laufwerk; bei einer abgelegten Akte sind beide gesperrt
 * (erst reaktivieren). */
static void update_action_widgets(SondModuleAktePrivate *priv) {
    gboolean gewaehlt = sond_client_get_current_akte(priv->client, NULL, NULL);
    gboolean arbeit = gewaehlt && !akte_ist_abgelegt(priv);
    AkteState st = priv->state;

    gtk_widget_set_sensitive(priv->btn_neue_akte,
            st == AKTE_STATE_INITIAL || st == AKTE_STATE_AUSGEWAEHLT);

    gtk_widget_set_sensitive(priv->btn_import,
            arbeit && st != AKTE_STATE_CREATING);
    gtk_widget_set_sensitive(priv->btn_zond,
            arbeit && st != AKTE_STATE_CREATING);

    //"Bearbeiten": solange die Akte gewählt oder nur gesperrt angezeigt wird
    gtk_widget_set_sensitive(priv->btn_break_lock,
            gewaehlt && (st == AKTE_STATE_READONLY ||
                    st == AKTE_STATE_AUSGEWAEHLT));
}

static void clear_akte_fields(SondModuleAktePrivate *priv) {
    priv->unsaved_conflict = FALSE;
    g_clear_pointer(&priv->wh_status, g_free);
    priv->ablegen_vorgemerkt = FALSE;
    priv->reaktivieren_vorgemerkt = FALSE;
    wiederherstellung_vormerkung_loeschen(priv);
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

/* Erfolgsmeldung nach dem Speichern; bei "Ok" entfällt sie (die Maske schließt sich) */
static void speicher_hinweis(SondModuleAktePrivate *priv, const gchar *message) {
    if (priv->ok_laeuft)
        return;
    show_info_dialog(priv->main_widget, "Gespeichert", message);
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
    SuchServer server;      /* für den Zusatz in der Trefferliste */
} SearchDialogData;

/**
 * select_akte:
 *
 * Wählt eine Akte aus (ohne Sperre) und zeigt sie an. @node sind die Daten vom
 * Server, falls die Suche sie lieferte; @server sagt, wie es der Server bei
 * der Suche hielt. Hat er geantwortet, ohne die Akte zu liefern (Treffer nur
 * über den Gegenstand), wird sie einmal gezielt geladen; bei Fehler oder ohne
 * Befragung gibt es keinen weiteren Versuch, die Maske zeigt dann nur Nummer
 * und Bezeichnung. Gearbeitet wird erst nach "Bearbeiten".
 */
static void select_akte(SondModuleAktePrivate *priv, guint lfd_nr, guint year,
                        const gchar *bezeichnung, const gchar *gegenstand,
                        SondGraphNode *node, SuchServer server) {
    SondGraphNode *geladen = NULL;
    const gchar *herkunft = NULL;

    if (!node && server == SERVER_OK) {
        GError *error = NULL;
        guint alt = sond_client_set_io_timeout(priv->client, SERVER_KURZ_TIMEOUT);

        geladen = search_node_by_regnr(priv, lfd_nr, year, &error);
        sond_client_set_io_timeout(priv->client, alt);
        if (!geladen && error && error->code != G_IO_ERROR_NOT_FOUND)
            herkunft = "nur lokal bekannt (Server nicht erreichbar)";
        else if (!geladen)
            herkunft = "auf dem Server nicht gefunden";
        g_clear_error(&error);
        node = geladen;
    } else if (!node && server == SERVER_FEHLER)
        herkunft = "nur lokal bekannt (Server nicht erreichbar)";
    else if (!node)
        herkunft = "nur lokal bekannt (Server nicht befragt)";

    if (priv->current_node) {
        g_object_unref(priv->current_node);
        priv->current_node = NULL;
    }
    priv->is_new_from_entry = FALSE;
    priv->unsaved_conflict = FALSE;
    sond_client_set_last_regnr(priv->client, lfd_nr, year);
    g_clear_pointer(&priv->wh_status, g_free);

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

    /* Wird die Akte gerade wiederhergestellt: Stand abfragen und, wenn der Job fertig
     * ist, die Akte gleich fertigstellen (nur wenn angemeldet) */
    if (priv->current_node && sond_client_is_logged_in(priv->client) &&
            sond_archiv_wiederherstellung(priv->current_node, NULL)) {
        wh_beim_auswaehlen(priv);
        if (priv->current_node && !sond_archiv_wiederherstellung(priv->current_node, NULL))
            update_ui_from_node(priv);   //inzwischen abgeschlossen: Anzeige aktualisieren
    }
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
                result->kurzbezeichnung, result->gegenstand, result->node,
                data->server);

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
 * Server (mit kurzer Zeitgrenze). Gleiche Akten erscheinen einmal. @server
 * sagt, ob und wie der Server beteiligt war; bei einem Fehler enthält @fehler
 * (nullable) die Meldung, die lokalen Treffer bleiben erhalten.
 *
 * Returns: (transfer full): Array von AkteSearchResult, neueste zuerst
 */
static GPtrArray* collect_results(SondModuleAktePrivate *priv, const gchar *text,
                                  SuchServer *server, gchar **fehler) {
    GPtrArray *results = g_ptr_array_new_with_free_func(
            (GDestroyNotify)akte_search_result_free);
    gchar *root = sond_client_find_seadrive_root(priv->client, NULL);
    gchar *merkliste = sond_akten_merkliste_pfad();
    guint lfd_nr = 0;
    guint year = 0;

    *server = SERVER_NICHT_BEFRAGT;
    if (fehler)
        *fehler = NULL;

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
        guint alt_timeout = sond_client_set_io_timeout(priv->client,
                                                       SERVER_KURZ_TIMEOUT);

        *server = SERVER_OK;

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
            } else if (error && error->code != G_IO_ERROR_NOT_FOUND) {
                *server = SERVER_FEHLER;
                if (fehler)
                    *fehler = g_strdup(error->message);
            }
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
            } else if (error) {
                *server = SERVER_FEHLER;
                if (fehler)
                    *fehler = g_strdup(error->message);
            }
        }
        g_clear_error(&error);
        sond_client_set_io_timeout(priv->client, alt_timeout);

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
static void show_akte_search_results(SondModuleAktePrivate *priv, GPtrArray *results, const gchar *search_term, SuchServer server) {
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
    gchar *title = g_strdup_printf("Aktensuche: \"%s\" (%u Ergebnisse)%s", search_term, results->len, server == SERVER_FEHLER ? " - Server nicht erreichbar, nur lokale Treffer" : "");
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
    data->server = server;
    
    /* Ergebnisse in ListBox einfügen */
    for (guint i = 0; i < results->len; i++) {
        AkteSearchResult *result = g_ptr_array_index(results, i);
        
        gchar *label_text = g_strdup_printf("%s - %s%s%s%s", result->regnr_display,
                                           result->kurzbezeichnung ? result->kurzbezeichnung : "",
                                           (result->gegenstand && *result->gegenstand) ? "  |  " : "",
                                           result->gegenstand ? result->gegenstand : "",
                                           result->node ? "" : (server == SERVER_OK ? "   (nur im SeaDrive)" :
                                               server == SERVER_FEHLER ? "   (Server nicht erreichbar)" : ""));
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
    
    /* Alle Akten mit einer Ablage in diesem Jahr (über das bis-Datum; die Unter-Property
     * "ablnr" selbst lässt sich nicht suchen) */
    SondGraphNodeSearchCriteria *criteria = sond_archiv_ablnr_suchkriterien(year);

    /* Client-Methode verwenden */
    GPtrArray *nodes = sond_client_search_nodes(priv->client, criteria, error);
    sond_graph_node_search_criteria_free(criteria);

    if (!nodes) {
        return FALSE;
    }

    guint max_lfd_nr = sond_archiv_hoechste_ablnr(nodes, year);

    g_ptr_array_unref(nodes);
    
    /* Nächste Nummer */
    *ablnr = g_strdup_printf("%u-%u", year, max_lfd_nr + 1);
    LOG_INFO("Nächste Ablagenr: %s\n", *ablnr);
    
    return TRUE;
}

/* Aktiv, wie es nach dem Speichern wäre: die vorgemerkte Aktion (Ablegen oder
 * Reaktivieren) ist wie eine Eingabe in der Maske berücksichtigt, aber noch nicht
 * am Node eingetragen. */
static gboolean effektiv_aktiv(SondModuleAktePrivate *priv) {
    if (priv->ablegen_vorgemerkt)
        return FALSE;
    if (priv->reaktivieren_vorgemerkt)
        return TRUE;

    return ist_akte_aktiv(priv->current_node);
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
    
    /* Wird gerade aus dem Archiv wiederhergestellt: nur Anzeige, keine Aktionen */
    {
        gchar *wdatum = NULL;

        if (!priv->ablegen_vorgemerkt && !priv->reaktivieren_vorgemerkt &&
                !priv->wiederherstellung_vorgemerkt &&
                sond_archiv_wiederherstellung(priv->current_node, &wdatum)) {
            gchar *text = g_strdup_printf("Status: wird wiederhergestellt (seit %s)%s%s", wdatum,
                    priv->wh_status ? " - " : "", priv->wh_status ? priv->wh_status : "");

            gtk_label_set_text(GTK_LABEL(priv->status_label), text);
            gtk_widget_set_visible(priv->btn_ablegen, FALSE);
            gtk_widget_set_visible(priv->btn_reaktivieren, FALSE);
            g_free(text);
            g_free(wdatum);

            return;
        }
        g_free(wdatum);
    }

    gboolean aktiv = effektiv_aktiv(priv);

    if (aktiv) {
        gtk_label_set_text(GTK_LABEL(priv->status_label), priv->reaktivieren_vorgemerkt ?
                "Status: wird beim Speichern reaktiviert" : "Status: Aktiv");
        gtk_widget_set_visible(priv->btn_ablegen, TRUE);
        gtk_widget_set_visible(priv->btn_reaktivieren, FALSE);
    } else if (priv->wiederherstellung_vorgemerkt) {
        gtk_label_set_text(GTK_LABEL(priv->status_label), "Status: wird beim Ok wiederhergestellt");
        gtk_widget_set_visible(priv->btn_ablegen, FALSE);
        gtk_widget_set_visible(priv->btn_reaktivieren, TRUE);
        gtk_widget_set_sensitive(priv->btn_reaktivieren, FALSE);   //nur noch Ok oder Abbrechen
    } else if (priv->ablegen_vorgemerkt) {
        gtk_label_set_text(GTK_LABEL(priv->status_label), "Status: wird beim Speichern abgelegt");
        gtk_widget_set_visible(priv->btn_ablegen, FALSE);
        gtk_widget_set_visible(priv->btn_reaktivieren, TRUE);
        gtk_widget_set_sensitive(priv->btn_reaktivieren, TRUE);
    } else {
        gchar *ablnr = get_letzte_ablage(priv->current_node);
        if (ablnr) {
            gchar *text = g_strdup_printf("Status: Abgelegt (%s)%s", ablnr,
                    sond_archiv_ist_archiviert(priv->current_node, NULL) ? ", archiviert" : "");
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
    update_action_widgets(priv);

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
    g_free(priv->lock_reason_other);
    priv->lock_reason_other = NULL;
    if (code == SOND_CLIENT_ERROR_LOCK_NOT_HELD && locked_by && *locked_by) {
        priv->locked_by_other = g_strdup(locked_by);
        priv->locked_at_other = (locked_at && *locked_at) ? g_strdup(locked_at) : NULL;
    }

    priv->ablegen_vorgemerkt = FALSE;
    priv->reaktivieren_vorgemerkt = FALSE;
    wiederherstellung_vormerkung_loeschen(priv);
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

/* Nach einer Änderung der vorgemerkten Aktion: Anzeige und Zustand der Maske
 * aus dem Zustand "wie nach dem Speichern" ableiten. */
static void aktion_neu_darstellen(SondModuleAktePrivate *priv) {
    update_status_display(priv);
    set_akte_state(priv, effektiv_aktiv(priv) ? AKTE_STATE_EDITING : AKTE_STATE_ABGELEGT);
}

/* Beim Speichern am Node eingetragene Properties der vorgemerkten Aktion (für den
 * Rückbau, falls das Speichern scheitert) */
typedef struct {
    SondGraphProperty *bis;
    SondGraphProperty *von;
} AngewendeteAktion;

/* Die vorgemerkte Aktion jetzt am Node eintragen: bei "Ablegen" ein "bis" mit der
 * dann nächsten Ablagenummer (die Nummer wird also erst beim Speichern vergeben),
 * bei "Reaktivieren" ein "von"; jeweils mit dem Datum des Speicherns. */
static gboolean aktion_anwenden(SondModuleAktePrivate *priv, AngewendeteAktion *a, GError **error) {
    GPtrArray *properties = NULL;
    GDateTime *now = NULL;
    gchar *datum = NULL;

    a->bis = NULL;
    a->von = NULL;
    if (!priv->ablegen_vorgemerkt && !priv->reaktivieren_vorgemerkt)
        return TRUE;

    properties = sond_graph_node_get_properties(priv->current_node);
    now = g_date_time_new_now_local();
    datum = g_date_time_format(now, "%Y-%m-%d");
    g_date_time_unref(now);

    if (priv->ablegen_vorgemerkt) {
        gchar *ablnr = NULL;

        if (!get_next_ablnr(priv, &ablnr, error)) {
            g_free(datum);

            return FALSE;
        }
        {
            const gchar *bis_value[] = {datum};
            const gchar *ablnr_value[] = {ablnr};
            SondGraphProperty *bis_prop = sond_graph_property_new("bis", bis_value, 1);

            sond_graph_property_add_subproperty(bis_prop,
                    sond_graph_property_new("ablnr", ablnr_value, 1));
            g_ptr_array_add(properties, bis_prop);
            a->bis = bis_prop;
        }
        g_free(ablnr);
    } else {
        const gchar *von_value[] = {datum};
        SondGraphProperty *von_prop = sond_graph_property_new("von", von_value, 1);

        g_ptr_array_add(properties, von_prop);
        a->von = von_prop;
    }
    g_free(datum);

    return TRUE;
}

/* Wieder austragen, falls das Speichern gescheitert ist */
static void aktion_zurueckbauen(SondModuleAktePrivate *priv, AngewendeteAktion *a) {
    GPtrArray *properties = priv->current_node ?
            sond_graph_node_get_properties(priv->current_node) : NULL;

    if (properties && a->bis)
        g_ptr_array_remove(properties, a->bis);
    if (properties && a->von)
        g_ptr_array_remove(properties, a->von);
    a->bis = NULL;
    a->von = NULL;
}

/* Event Handler für Ablage/Reaktivierung: "Akte ablegen" merkt die Ablage nur vor
 * (wie eine Eingabe in der Maske); Ablagenummer und bis-Datum entstehen beim
 * Speichern ("Speichern" oder "Ok"). Ein vorgemerktes Reaktivieren nimmt es zurück. */
static void on_ablegen_clicked(GtkButton *button, SondModuleAktePrivate *priv) {
    (void) button;
    if (!priv->current_node) {
        return;
    }

    if (!effektiv_aktiv(priv)) {
        show_error_dialog(priv->main_widget, "Fehler", "Akte ist bereits abgelegt.");
        return;
    }

    if (priv->reaktivieren_vorgemerkt)
        priv->reaktivieren_vorgemerkt = FALSE;
    else
        priv->ablegen_vorgemerkt = TRUE;
    aktion_neu_darstellen(priv);
}

/* Library-Name ("Jahr-LfdNr") eines Akten-Nodes; NULL ohne gültige RegNr */
static gchar* node_library_name(SondGraphNode *node) {
    GPtrArray *regnr = node ? sond_graph_node_get_property(node, "regnr") : NULL;
    gchar *lib = NULL;

    if (regnr && regnr->len == 2)
        lib = g_strdup_printf("%s-%s", (const gchar*) g_ptr_array_index(regnr, 0),
                (const gchar*) g_ptr_array_index(regnr, 1));
    if (regnr)
        g_ptr_array_unref(regnr);

    return lib;
}

/* Heutiges Datum als "JJJJ-MM-TT" */
static gchar* datum_heute(void) {
    GDateTime *now = g_date_time_new_now_local();
    gchar *datum = g_date_time_format(now, "%Y-%m-%d");

    g_date_time_unref(now);

    return datum;
}

static void wiederherstellung_vormerkung_loeschen(SondModuleAktePrivate *priv) {
    priv->wiederherstellung_vorgemerkt = FALSE;
    g_clear_pointer(&priv->wh_library, g_free);
    g_clear_pointer(&priv->wh_archiv, g_free);
}

/* "Ok" bei vorgemerkter Wiederherstellung eines archivierten Dokumentenordners: den
 * Server-Job anstoßen, die Akte als "wird wiederhergestellt" markieren und
 * speichern. Das Fertigstellen (von setzen, Markierung entfernen) geschieht später,
 * sobald der Job beendet ist (beim Auswählen oder Bearbeiten der Akte). */
static void wiederherstellung_anstossen(SondModuleAktePrivate *priv) {
    GError *error = NULL;
    gchar *datum = NULL;

    if (!sond_client_archiv_start(priv->client, priv->wh_library, priv->wh_archiv, TRUE, &error)) {
        gchar *text = g_strdup_printf("Die Wiederherstellung konnte nicht gestartet werden:\n\n%s\n\n"
                "Die Akte bleibt abgelegt; Sie können es noch einmal versuchen oder abbrechen.",
                error ? error->message : "unbekannter Fehler");

        show_error_dialog(priv->main_widget, "Wiederherstellung nicht gestartet", text);
        g_free(text);
        g_clear_error(&error);

        return;   //bleibt vorgemerkt, die Maske bleibt offen
    }

    datum = datum_heute();
    sond_archiv_wiederherstellung_markieren(priv->current_node, datum);
    if (!save_node(priv, &error)) {
        gchar *text = g_strdup_printf("Die Wiederherstellung läuft auf dem Server, aber die "
                "Markierung der Akte konnte nicht gespeichert werden:\n\n%s\n\n"
                "Öffnen Sie die Akte später erneut; sobald der Dokumentenordner da ist, kann sie "
                "reaktiviert werden.", error ? error->message : "unbekannter Fehler");

        sond_archiv_wiederherstellung_entfernen(priv->current_node);
        show_error_dialog(priv->main_widget, "Markierung nicht gespeichert", text);
        g_free(text);
        g_clear_error(&error);
    } else {
        show_info_dialog(priv->main_widget, "Wiederherstellung gestartet",
                "Der Dokumentenordner wird auf dem Server wiederhergestellt. Die Akte ist bis "
                "dahin als \"wird wiederhergestellt\" markiert und wird beim nächsten Öffnen "
                "fertiggestellt.");
    }
    g_free(datum);

    wiederherstellung_vormerkung_loeschen(priv);
    finish_akte(priv);
}

/* ------------------------------------------------------------------------
 * Wiederherstellung fertigstellen (Akte ist als "wird wiederhergestellt" markiert)
 * ---------------------------------------------------------------------- */

typedef enum {
    WH_LAEUFT,           /* der Wiederherstellungsjob läuft */
    WH_FERTIG,           /* der Job ist erfolgreich beendet */
    WH_FEHLER,           /* der Job ist gescheitert */
    WH_UNKLAR_DA,        /* Job unbekannt (Serverneustart?), Dokumentenordner ist aber da */
    WH_UNKLAR_FEHLT,     /* Job unbekannt, Dokumentenordner fehlt: nie fertig geworden */
    WH_NICHT_ABRUFBAR    /* Stand ist nicht abrufbar (Server) */
} WhStand;

/* Stand der Wiederherstellung der aktuellen Akte beim Server; @text bekommt je nach
 * Stand eine Angabe (Beginn, Fehlermeldung) */
static WhStand wh_stand(SondModuleAktePrivate *priv, gchar **text) {
    gchar *lib = node_library_name(priv->current_node);
    GError *error = NULL;
    SondArchivJob *job = lib ? sond_client_archiv_status(priv->client, lib, &error) : NULL;
    WhStand stand = WH_NICHT_ABRUFBAR;

    *text = NULL;
    if (!job) {
        *text = g_strdup(error ? error->message : "nicht abrufbar");
        g_clear_error(&error);
        g_free(lib);

        return WH_NICHT_ABRUFBAR;
    }

    if (job->restore && job->state == SOND_ARCHIV_RUNNING) {
        GDateTime *dt = job->start_time > 0 ? g_date_time_new_from_unix_local(job->start_time) : NULL;

        stand = WH_LAEUFT;
        if (dt) {
            *text = g_date_time_format(dt, "%d.%m.%Y %H:%M");
            g_date_time_unref(dt);
        }
    } else if (job->restore && job->state == SOND_ARCHIV_OK) {
        stand = WH_FERTIG;
    } else if (job->restore && job->state == SOND_ARCHIV_ERROR) {
        stand = WH_FEHLER;
        *text = g_strdup(job->message ? job->message : "unbekannter Fehler");
    } else {
        //kein Wiederherstellungsjob bekannt: Ordner prüfen
        gchar *id = sond_client_get_seafile_library_id(priv->client, lib, &error);

        if (id) {
            stand = WH_UNKLAR_DA;
            g_free(id);
        } else if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
            stand = WH_UNKLAR_FEHLT;
        } else {
            stand = WH_NICHT_ABRUFBAR;
            *text = g_strdup(error ? error->message : "nicht abrufbar");
        }
        g_clear_error(&error);
    }
    sond_archiv_job_free(job);
    g_free(lib);

    return stand;
}

/* Akte (Sperre gehalten, Node frisch geladen) fertigstellen: Markierung entfernen,
 * bei @erfolg das "von" setzen, speichern. Scheitert das Speichern, ist alles wie
 * vorher. */
static gboolean wh_abschliessen(SondModuleAktePrivate *priv, gboolean erfolg, GError **error) {
    GPtrArray *properties = sond_graph_node_get_properties(priv->current_node);
    SondGraphProperty *von_prop = NULL;
    gchar *wdatum = NULL;

    sond_archiv_wiederherstellung(priv->current_node, &wdatum);
    sond_archiv_wiederherstellung_entfernen(priv->current_node);
    if (erfolg) {
        gchar *datum = datum_heute();
        const gchar *von_value[] = {datum};

        von_prop = sond_graph_property_new("von", von_value, 1);
        g_ptr_array_add(properties, von_prop);
        g_free(datum);
    }

    if (!save_node(priv, error)) {
        if (von_prop)
            g_ptr_array_remove(properties, von_prop);
        if (wdatum)
            sond_archiv_wiederherstellung_markieren(priv->current_node, wdatum);
        g_free(wdatum);

        return FALSE;
    }
    g_free(wdatum);

    return TRUE;
}

static void wh_status_setzen(SondModuleAktePrivate *priv, const gchar *text) {
    g_free(priv->wh_status);
    priv->wh_status = text ? g_strdup(text) : NULL;
}

/* Beim Auswählen (ohne Sperre): Stand abfragen; ist die Wiederherstellung fertig oder
 * gescheitert, versuchen, die Akte gleich fertigzustellen (sperren, frisch laden,
 * eintragen, entsperren). Gelingt das nicht, bleibt es bei der Anzeige. Nur wenn
 * angemeldet; mit kurzer Zeitgrenze. */
static void wh_beim_auswaehlen(SondModuleAktePrivate *priv) {
    guint alt = sond_client_set_io_timeout(priv->client, SERVER_KURZ_TIMEOUT);
    gchar *text = NULL;
    WhStand stand = wh_stand(priv, &text);
    gint64 node_id = sond_graph_node_get_id(priv->current_node);
    gboolean abschliessen = stand == WH_FERTIG || stand == WH_FEHLER || stand == WH_UNKLAR_FEHLT;

    switch (stand) {
    case WH_LAEUFT:
        {
            gchar *s = text ? g_strdup_printf("Wiederherstellung läuft seit %s", text) :
                    g_strdup("Wiederherstellung läuft");

            wh_status_setzen(priv, s);
            g_free(s);
        }
        break;
    case WH_UNKLAR_DA:
        wh_status_setzen(priv, "Stand unklar (Dokumentenordner vorhanden, Job unbekannt); "
                "wird beim Bearbeiten geklärt");
        break;
    case WH_NICHT_ABRUFBAR:
        wh_status_setzen(priv, "Stand der Wiederherstellung nicht abrufbar");
        break;
    default:
        wh_status_setzen(priv, stand == WH_FERTIG ?
                "fertig; wird beim Bearbeiten abgeschlossen" : "wird beim Bearbeiten abgeschlossen");
        break;
    }

    if (abschliessen && node_id > 0 &&
            sond_client_lock_node(priv->client, node_id, SOND_LOCK_REASON_BEARBEITUNG, NULL)) {
        GError *error = NULL;
        guint lfd = 0;
        guint year = 0;
        GPtrArray *regnr = sond_graph_node_get_property(priv->current_node, "regnr");
        SondGraphNode *frisch = NULL;

        if (regnr && regnr->len == 2) {
            year = (guint) g_ascii_strtoull(g_ptr_array_index(regnr, 0), NULL, 10);
            lfd = (guint) g_ascii_strtoull(g_ptr_array_index(regnr, 1), NULL, 10);
            frisch = search_node_by_regnr(priv, lfd, year, NULL);
        }
        if (regnr)
            g_ptr_array_unref(regnr);

        if (frisch) {
            g_object_unref(priv->current_node);
            priv->current_node = frisch;
            if (sond_archiv_wiederherstellung(priv->current_node, NULL)) {
                if (wh_abschliessen(priv, stand == WH_FERTIG, &error)) {
                    wh_status_setzen(priv, NULL);
                    if (stand == WH_FEHLER || stand == WH_UNKLAR_FEHLT) {
                        gchar *m = g_strdup_printf("Die Wiederherstellung des Dokumentenordners ist "
                                "nicht gelungen%s%s.\n\nDie Akte bleibt abgelegt; die "
                                "Wiederherstellung kann beim Reaktivieren erneut angestoßen werden.",
                                text ? ": " : "", text ? text : "");

                        show_error_dialog(priv->main_widget, "Wiederherstellung fehlgeschlagen", m);
                        g_free(m);
                    }
                } else {
                    wh_status_setzen(priv, "Abschluss nicht gespeichert; wird beim Bearbeiten wiederholt");
                    g_clear_error(&error);
                }
            } else {
                wh_status_setzen(priv, NULL);   //inzwischen von jemand anderem abgeschlossen
            }
        }
        sond_client_unlock_node(priv->client, node_id, NULL);
    }

    sond_client_set_io_timeout(priv->client, alt);
    g_free(text);
    update_status_display(priv);
    update_action_widgets(priv);
}

typedef struct {
    SondModuleAktePrivate *priv;
} WhUnklarFrage;

static void on_wh_unklar_response(GObject *source, GAsyncResult *result, gpointer user_data) {
    WhUnklarFrage *f = user_data;
    SondModuleAktePrivate *priv = f->priv;
    GError *error = NULL;
    gint button = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result, &error);
    gint64 node_id = priv->current_node ? sond_graph_node_get_id(priv->current_node) : 0;

    if (error) {
        g_error_free(error);
        button = 1;
    }
    g_free(f);

    if (!priv->current_node || node_id <= 0)
        return;

    if (button == 0) {
        GError *save_error = NULL;

        if (wh_abschliessen(priv, TRUE, &save_error)) {
            wh_status_setzen(priv, NULL);
            update_ui_from_node(priv);
            set_akte_state(priv, AKTE_STATE_EDITING);
            return;
        }
        show_error_dialog(priv->main_widget, "Speichern fehlgeschlagen",
                save_error ? save_error->message : "Unbekannter Fehler");
        g_clear_error(&save_error);
    }
    //nicht übernommen oder Fehler: Sperre abgeben, Akte nur ansehen
    sond_client_unlock_node(priv->client, node_id, NULL);
    update_ui_from_node(priv);
    set_akte_state(priv, AKTE_STATE_AUSGEWAEHLT);
}

/* Beim Bearbeiten (Sperre gehalten, Node frisch): FALSE, wenn die Akte nicht zum
 * Bearbeiten freigegeben wird (Sperre ist dann wieder abgegeben oder eine Rückfrage
 * läuft); TRUE, wenn die Wiederherstellung abgeschlossen ist und normal weitergeht. */
static gboolean wh_beim_bearbeiten(SondModuleAktePrivate *priv, gint64 node_id) {
    gchar *text = NULL;
    WhStand stand = wh_stand(priv, &text);
    GError *error = NULL;
    gboolean weiter = FALSE;

    switch (stand) {
    case WH_FERTIG:
    case WH_FEHLER:
    case WH_UNKLAR_FEHLT:
        if (wh_abschliessen(priv, stand == WH_FERTIG, &error)) {
            wh_status_setzen(priv, NULL);
            if (stand != WH_FERTIG) {
                gchar *m = g_strdup_printf("Die Wiederherstellung des Dokumentenordners ist "
                        "nicht gelungen%s%s.\n\nDie Akte bleibt abgelegt; die Wiederherstellung "
                        "kann beim Reaktivieren erneut angestoßen werden.",
                        text ? ": " : "", text ? text : "");

                show_error_dialog(priv->main_widget, "Wiederherstellung fehlgeschlagen", m);
                g_free(m);
            }
            weiter = TRUE;
        } else {
            show_error_dialog(priv->main_widget, "Speichern fehlgeschlagen",
                    error ? error->message : "Unbekannter Fehler");
            g_clear_error(&error);
        }
        break;

    case WH_UNKLAR_DA:
        {
            WhUnklarFrage *f = g_new0(WhUnklarFrage, 1);
            GtkAlertDialog *dialog = gtk_alert_dialog_new("Wiederherstellung unklar");
            const gchar *buttons[] = { "Als wiederhergestellt übernehmen", "Nicht übernehmen", NULL };

            f->priv = priv;
            gtk_alert_dialog_set_detail(dialog, "Der Dokumentenordner dieser Akte ist vorhanden, "
                    "aber der Server kennt den Wiederherstellungsauftrag nicht mehr (vermutlich "
                    "wurde er neu gestartet). Der Ordner könnte unvollständig sein.\n\nNur "
                    "übernehmen, wenn Sie sicher sind, dass er vollständig ist.");
            gtk_alert_dialog_set_buttons(dialog, buttons);
            gtk_alert_dialog_set_default_button(dialog, 1);
            gtk_alert_dialog_set_cancel_button(dialog, 1);
            gtk_alert_dialog_set_modal(dialog, TRUE);
            gtk_alert_dialog_choose(dialog, GTK_WINDOW(gtk_widget_get_root(priv->main_widget)),
                    NULL, on_wh_unklar_response, f);
            g_object_unref(dialog);
        }
        break;

    case WH_LAEUFT:
    default:
        {
            gchar *m = NULL;

            if (stand == WH_LAEUFT)
                m = text ? g_strdup_printf("Der Dokumentenordner dieser Akte wird noch "
                        "wiederhergestellt (seit %s). Bitte später erneut versuchen.", text) :
                        g_strdup("Der Dokumentenordner dieser Akte wird noch wiederhergestellt. "
                        "Bitte später erneut versuchen.");
            else
                m = g_strdup_printf("Der Stand der Wiederherstellung ist nicht abrufbar:\n\n%s",
                        text ? text : "unbekannt");
            show_info_dialog(priv->main_widget, "Wiederherstellung läuft", m);
            g_free(m);
        }
        break;
    }

    g_free(text);
    if (!weiter && stand != WH_UNKLAR_DA) {
        //Akte nur ansehen: Sperre abgeben
        sond_client_unlock_node(priv->client, node_id, NULL);
        update_ui_from_node(priv);
        set_akte_state(priv, AKTE_STATE_AUSGEWAEHLT);
    } else if (!weiter) {
        update_ui_from_node(priv);
        set_akte_state(priv, AKTE_STATE_AUSGEWAEHLT);
    }

    return weiter;
}

typedef struct {
    SondModuleAktePrivate *priv;
    gchar *library;
    gchar *archiv;
} RestoreFrage;

static void on_restore_frage_response(GObject *source, GAsyncResult *result, gpointer user_data) {
    RestoreFrage *f = user_data;
    GError *error = NULL;
    gint button = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result, &error);

    if (error) {
        g_error_free(error);
    } else if (button == 0 && f->priv->current_node) {
        //nur vormerken: die Wiederherstellung startet erst mit "Ok"
        wiederherstellung_vormerkung_loeschen(f->priv);
        f->priv->wiederherstellung_vorgemerkt = TRUE;
        f->priv->wh_library = g_strdup(f->library);
        f->priv->wh_archiv = g_strdup(f->archiv);
        aktion_neu_darstellen(f->priv);
    }

    g_free(f->library);
    g_free(f->archiv);
    g_free(f);
}

static void on_reaktivieren_clicked(GtkButton *button, SondModuleAktePrivate *priv) {
    (void) button;
    if (!priv->current_node) {
        return;
    }

    //eine vorgemerkte Wiederherstellung lässt sich nur mit "Abbrechen" zurücknehmen
    if (priv->wiederherstellung_vorgemerkt)
        return;

    /* Ein noch nicht gespeichertes Ablegen wird damit einfach zurückgenommen */
    if (priv->ablegen_vorgemerkt) {
        priv->ablegen_vorgemerkt = FALSE;
        aktion_neu_darstellen(priv);
        return;
    }

    if (ist_akte_aktiv(priv->current_node)) {
        show_error_dialog(priv->main_widget, "Fehler", "Akte ist bereits aktiv.");
        return;
    }

    gchar *lib = node_library_name(priv->current_node);
    if (!lib) {
        show_error_dialog(priv->main_widget, "Fehler", "Akte hat keine gültige RegNr");
        return;
    }

    /* Läuft für die Library gerade ein Archivierungs- oder Wiederherstellungsjob, darf
     * nicht reaktiviert werden (der Job würde den Dokumentenordner gleich löschen bzw.
     * neu anlegen). Ist der Stand nicht abrufbar, wird vorsichtshalber ebenfalls abgelehnt. */
    GError *error = NULL;
    SondArchivJob *job = sond_client_archiv_status(priv->client, lib, &error);
    if (!job) {
        gchar *msg = g_strdup_printf("Der Stand der Archivierung ist nicht abrufbar:\n\n%s",
                error ? error->message : "unbekannt");

        show_error_dialog(priv->main_widget, "Reaktivieren nicht möglich", msg);
        g_free(msg);
        g_clear_error(&error);
        g_free(lib);
        return;
    }
    if (job->state == SOND_ARCHIV_RUNNING) {
        show_error_dialog(priv->main_widget, "Reaktivieren nicht möglich",
                job->restore ? "Der Dokumentenordner dieser Akte wird gerade wiederhergestellt." :
                "Der Dokumentenordner dieser Akte wird gerade archiviert. Bitte später erneut "
                "versuchen.");
        sond_archiv_job_free(job);
        g_free(lib);
        return;
    }
    sond_archiv_job_free(job);

    /* Ist der Dokumentenordner da, wird normal reaktiviert (auch bei Markierung "archiv",
     * etwa nach einer früheren Wiederherstellung). Fehlt er, ist die Akte archiviert: der
     * Archivname steht in der Markierung, sonst wird er aus Ablage und Datum gebildet (Lauf
     * nach dem Archivieren abgebrochen, bevor die Markierung gespeichert war). */
    gchar *archiv = NULL;
    gchar *lib_id = NULL;

    sond_archiv_ist_archiviert(priv->current_node, &archiv);
    lib_id = sond_client_get_seafile_library_id(priv->client, lib, &error);
    if (lib_id) {
        g_free(lib_id);
        g_free(archiv);
        archiv = NULL;
    } else if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
        if (!archiv) {
            const gchar *grund = NULL;
            SondArchivKandidat *k = sond_archiv_kandidat_pruefen(priv->current_node, 0, NULL, &grund);

            if (k) {
                archiv = g_strdup(k->archiv_name);
                sond_archiv_kandidat_free(k);
            }
        }
        if (!archiv) {
            show_error_dialog(priv->main_widget, "Reaktivieren nicht möglich",
                    "Die Library dieser Akte fehlt, und der Archivname lässt sich nicht "
                    "bestimmen. Bitte den Administrator fragen.");
            g_clear_error(&error);
            g_free(lib);
            return;
        }
    } else {
        gchar *msg = g_strdup_printf("Ob der Dokumentenordner vorhanden ist, lässt sich nicht "
                "prüfen:\n\n%s", error ? error->message : "unbekannt");

        show_error_dialog(priv->main_widget, "Reaktivieren nicht möglich", msg);
        g_free(msg);
        g_clear_error(&error);
        g_free(lib);
        g_free(archiv);
        return;
    }
    g_clear_error(&error);

    if (!archiv) {
        /* Dokumentenordner ist da: das Reaktivieren wird nur vorgemerkt, das "von"
         * entsteht beim Speichern ("Speichern" oder "Ok") */
        g_free(lib);
        priv->reaktivieren_vorgemerkt = TRUE;
        aktion_neu_darstellen(priv);
        return;
    }

    /* Archiviert: vorher Warnung und Rückfrage */
    {
        RestoreFrage *f = g_new0(RestoreFrage, 1);
        GtkAlertDialog *dialog = gtk_alert_dialog_new("Dokumentenordner wiederherstellen?");
        const gchar *buttons[] = { "Vormerken", "Abbrechen", NULL };
        gchar *detail = g_strdup_printf("Der Dokumentenordner dieser Akte ist archiviert (%s). "
                "Beim Ok wird er auf dem Server wiederhergestellt; das kann bei großen Akten "
                "länger dauern. Die Akte ist bis dahin als \"wird wiederhergestellt\" markiert "
                "und noch nicht aktiv.\n\nWiederherstellung vormerken?", archiv);

        f->priv = priv;
        f->library = lib;
        f->archiv = archiv;
        gtk_alert_dialog_set_detail(dialog, detail);
        g_free(detail);
        gtk_alert_dialog_set_buttons(dialog, buttons);
        gtk_alert_dialog_set_default_button(dialog, 1);
        gtk_alert_dialog_set_cancel_button(dialog, 1);
        gtk_alert_dialog_set_modal(dialog, TRUE);
        gtk_alert_dialog_choose(dialog, GTK_WINDOW(gtk_widget_get_root(priv->main_widget)),
                NULL, on_restore_frage_response, f);
        g_object_unref(dialog);
    }
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
    gchar *locked_by = NULL, *locked_at = NULL, *lock_reason = NULL;

    if (!sond_client_lock_node_ex(priv->client, node_id, SOND_LOCK_REASON_BEARBEITUNG,
                                  &locked_by, &locked_at, &lock_reason, &lock_error)) {
        /* Weiterhin gesperrt: aktuellen Sperrer anzeigen, READONLY bleibt */
        g_free(priv->locked_by_other);
        priv->locked_by_other = locked_by;
        g_free(priv->locked_at_other);
        priv->locked_at_other = locked_at;
        g_free(priv->lock_reason_other);
        priv->lock_reason_other = lock_reason;
        if (lock_error) g_error_free(lock_error);

        /* Auch die Leseansicht auf den aktuellen Stand bringen (kein Lock noetig) */
        GError *load_error = NULL;
        SondGraphNode *fresh = search_node_by_regnr(priv, lfd_nr, year, &load_error);
        if (fresh) {
            g_object_unref(priv->current_node);
            priv->current_node = fresh;
        }
        priv->ablegen_vorgemerkt = FALSE;
        priv->reaktivieren_vorgemerkt = FALSE;
        wiederherstellung_vormerkung_loeschen(priv);
        if (load_error) g_error_free(load_error);

        priv->unsaved_conflict = FALSE;
        update_ui_from_node(priv);
        set_akte_state(priv, AKTE_STATE_READONLY);
        return TRUE;
    }
    g_free(locked_by);
    g_free(locked_at);
    g_free(lock_reason);

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
    priv->ablegen_vorgemerkt = FALSE;
    priv->reaktivieren_vorgemerkt = FALSE;
    wiederherstellung_vormerkung_loeschen(priv);

    g_free(priv->locked_by_other);
    priv->locked_by_other = NULL;
    g_free(priv->locked_at_other);
    priv->locked_at_other = NULL;
    g_free(priv->lock_reason_other);
    priv->lock_reason_other = NULL;

    //wird die Akte gerade wiederhergestellt: Stand klären, ggf. abschließen
    if (sond_archiv_wiederherstellung(priv->current_node, NULL) &&
            !wh_beim_bearbeiten(priv, node_id))
        return FALSE;

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

    //Gesperrt für die Archivierung: solange der Server-Job läuft (oder sein Stand nicht
    //abrufbar ist), wird das Lock nicht gebrochen, weil der Job danach die Library der
    //Akte löscht. Läuft kein Job, ist die Sperre verwaist (z. B. Client abgestürzt) und
    //lässt sich wie jede andere brechen.
    if (!g_strcmp0(priv->lock_reason_other, SOND_LOCK_REASON_ARCHIVIERUNG)) {
        GPtrArray *regnr = sond_graph_node_get_property(priv->current_node, "regnr");
        gchar *lib = (regnr && regnr->len == 2) ?
                g_strdup_printf("%s-%s", (const gchar*) g_ptr_array_index(regnr, 0),
                        (const gchar*) g_ptr_array_index(regnr, 1)) : NULL;
        SondArchivJob *job = lib ? sond_client_archiv_status(priv->client, lib, NULL) : NULL;

        if (regnr)
            g_ptr_array_unref(regnr);
        g_free(lib);
        if (!job || job->state == SOND_ARCHIV_RUNNING) {
            gchar *seit = priv->locked_at_other ?
                    g_strdup_printf(" (gesperrt seit: %s)", priv->locked_at_other) : g_strdup("");
            gchar *text = g_strdup_printf("Diese Akte wird gerade von '%s' archiviert%s.\n\n"
                    "Die Sperre kann nicht übernommen werden. Bitte später erneut versuchen.",
                    priv->locked_by_other ? priv->locked_by_other : "einem anderen Benutzer", seit);

            show_error_dialog(priv->main_widget, "Akte wird archiviert", text);
            g_free(text);
            g_free(seit);
            sond_archiv_job_free(job);

            return;
        }
        sond_archiv_job_free(job);
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
    guint alt_timeout = 0;
    GError *error = NULL;
    SondGraphNode *node = NULL;

    if (!sond_client_get_current_akte(priv->client, &lfd_nr, &year))
        return FALSE;

    alt_timeout = sond_client_set_io_timeout(priv->client, SERVER_KURZ_TIMEOUT);

    if (!sond_client_login(priv->client, &error)) {
        sond_client_set_io_timeout(priv->client, alt_timeout);
        show_error_dialog(priv->main_widget, "Server nicht erreichbar",
                "Die Akte kann nicht bearbeitet werden, weil keine Verbindung "
                "zum Server besteht. Es wurde nichts geändert.");
        g_clear_error(&error);
        return FALSE;
    }

    node = search_node_by_regnr(priv, lfd_nr, year, &error);
    sond_client_set_io_timeout(priv->client, alt_timeout);
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
                                              SOND_LOCK_REASON_BEARBEITUNG, &save_error)) {
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

/* Nachfrage nach einer Suche ohne Treffer: auf dem Server suchen (mit
 * Anmeldung, falls noch keine Sitzung besteht) bzw. nach einem Fehler erneut */
typedef struct {
    SondModuleAktePrivate *priv;
    gchar *such;
    gboolean anmelden;
} ServerSucheData;

static void such_ausfuehren(SondModuleAktePrivate *priv, const gchar *such);

static void on_server_suche_response(GObject *source, GAsyncResult *result,
                                     gpointer user_data) {
    ServerSucheData *d = user_data;
    GError *error = NULL;
    int button = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result,
                                                &error);

    g_clear_error(&error);

    if (button == 0) {
        gboolean weiter = TRUE;

        if (d->anmelden) {
            guint alt = sond_client_set_io_timeout(d->priv->client,
                                                   SERVER_KURZ_TIMEOUT);

            weiter = sond_client_login(d->priv->client, &error);
            sond_client_set_io_timeout(d->priv->client, alt);
            g_clear_error(&error);

            if (!weiter)
                show_info_dialog(d->priv->main_widget, "Keine Anmeldung",
                        "Die Anmeldung am Server war nicht möglich oder wurde "
                        "abgebrochen. Gesucht wurde nur im SeaDrive-Laufwerk.");
        }

        if (weiter)
            such_ausfuehren(d->priv, d->such);
    }

    g_free(d->such);
    g_free(d);
}

static void frage_server_suche(SondModuleAktePrivate *priv, const gchar *such,
                               gboolean anmelden, const gchar *fehler) {
    GtkAlertDialog *dialog = NULL;
    ServerSucheData *d = g_new0(ServerSucheData, 1);
    gchar *detail = NULL;
    const char *buttons_ja[] = {"Ja", "Nein", NULL};
    const char *buttons_erneut[] = {"Erneut suchen", "Abbrechen", NULL};

    d->priv = priv;
    d->such = g_strdup(such);
    d->anmelden = anmelden;

    if (anmelden) {
        dialog = gtk_alert_dialog_new("Auf dem Server suchen?");
        detail = g_strdup("Im SeaDrive-Laufwerk wurde nichts gefunden. Soll auf "
                "dem Server gesucht werden? Dafür ist eine Anmeldung nötig.");
        gtk_alert_dialog_set_buttons(dialog, buttons_ja);
    } else {
        dialog = gtk_alert_dialog_new("Server nicht erreichbar");
        detail = g_strdup_printf("Im SeaDrive-Laufwerk wurde nichts gefunden, und "
                "der Server hat nicht geantwortet (%s). Erneut suchen?",
                fehler ? fehler : "unbekannter Fehler");
        gtk_alert_dialog_set_buttons(dialog, buttons_erneut);
    }
    gtk_alert_dialog_set_detail(dialog, detail);
    g_free(detail);
    gtk_alert_dialog_set_default_button(dialog, 0);
    gtk_alert_dialog_set_cancel_button(dialog, 1);
    gtk_alert_dialog_set_modal(dialog, TRUE);

    gtk_alert_dialog_choose(dialog,
                            GTK_WINDOW(gtk_widget_get_root(priv->main_widget)),
                            NULL, on_server_suche_response, d);
    g_object_unref(dialog);
}

/* Suche ausführen: RegNr oder Suchtext, im SeaDrive und, wenn angemeldet, auf
 * dem Server. Lokale Treffer gelten (ein einziger wird gleich gewählt, bei
 * mehreren erscheint die Trefferliste). Ohne lokalen Treffer sagt das nichts,
 * deshalb wird dann gefragt, ob der Server befragt werden soll. */
static void such_ausfuehren(SondModuleAktePrivate *priv, const gchar *such) {
    guint lfd_nr = 0;
    guint year = 0;
    gchar *fehler = NULL;
    SuchServer server = SERVER_NICHT_BEFRAGT;
    GPtrArray *results = collect_results(priv, such, &server, &fehler);

    if (results->len == 1) {
        AkteSearchResult *r = g_ptr_array_index(results, 0);

        select_akte(priv, r->lfd_nr, r->year, r->kurzbezeichnung, r->gegenstand,
                    r->node, server);
    } else if (results->len > 1) {
        show_akte_search_results(priv, results, such, server);
    } else if (server == SERVER_NICHT_BEFRAGT) {
        frage_server_suche(priv, such, TRUE, NULL);
    } else if (server == SERVER_FEHLER) {
        frage_server_suche(priv, such, FALSE, fehler);
    } else if (is_valid_regnr_format(such) && parse_regnr(such, &lfd_nr, &year)) {
        sond_client_set_last_regnr(priv->client, lfd_nr, year);
        ask_new_akte(priv, lfd_nr, year);
    } else {
        gchar *msg = g_strdup_printf("Keine Akten gefunden mit Suchbegriff: \"%s\"",
                                     such);

        show_info_dialog(priv->main_widget, "Keine Ergebnisse", msg);
        g_free(msg);
    }

    g_free(fehler);
    g_ptr_array_unref(results);
}

/* Enter im Suchfeld */
static void on_regnr_entry_activate(GtkEntry *entry, SondModuleAktePrivate *priv) {
    gchar *such = g_strstrip(g_strdup(gtk_editable_get_text(GTK_EDITABLE(entry))));

    if (*such)
        such_ausfuehren(priv, such);
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
			speicher_hinweis(priv, msg);
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
            speicher_hinweis(priv, "Akte wurde erfolgreich gespeichert.");

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
    
    /* Vorgemerktes Ablegen/Reaktivieren jetzt am Node eintragen (Ablagenummer und
     * Datum entstehen erst hier) */
    AngewendeteAktion aktion = { NULL, NULL };
    GError *error = NULL;
    if (!aktion_anwenden(priv, &aktion, &error)) {
        gchar *msg = g_strdup_printf("Fehler bei Ablagenr-Vergabe: %s",
                error ? error->message : "Unbekannt");

        show_error_dialog(priv->main_widget, "Speichern fehlgeschlagen", msg);
        g_free(msg);
        g_clear_error(&error);
        g_free(kurzb_alt);
        g_free(ggstd_alt);
        return;
    }

    /* Speichern */
    if (save_node(priv, &error)) {
        gboolean aktion_war_vorgemerkt = priv->ablegen_vorgemerkt || priv->reaktivieren_vorgemerkt;

        priv->ablegen_vorgemerkt = FALSE;
        priv->reaktivieren_vorgemerkt = FALSE;
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
        if (aktion_war_vorgemerkt)
            aktion_neu_darstellen(priv);   /* Ablegen/Reaktivieren ist jetzt gespeichert */

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

        speicher_hinweis(priv, "Akte wurde erfolgreich gespeichert.");
    } else {
        gchar *msg = g_strdup_printf("Fehler: %s", error ? error->message : "Unbekannt");

        /* Der Node bleibt, wie er war; die Aktion bleibt vorgemerkt */
        aktion_zurueckbauen(priv, &aktion);
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

    /* Letzte RegNr vorschlagen; der Fokus zeigt, dass die nächste Akte erwartet wird */
    suggest_last_regnr(priv);
    gtk_widget_grab_focus(priv->regnr_entry);
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

    /* Vorgemerkte Wiederherstellung eines archivierten Dokumentenordners: "Ok" stößt sie
     * auf dem Server an (der Job läuft im Hintergrund) und beendet die Sitzung. */
    if (priv->wiederherstellung_vorgemerkt) {
        wiederherstellung_anstossen(priv);
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
        priv->ok_laeuft = TRUE;   //bei "Ok" kein Hinweis: das Schließen der Maske ist die Rückmeldung
        on_speichern_clicked(button, priv);
        priv->ok_laeuft = FALSE;

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

    /* Letzte RegNr vorschlagen; der Fokus zeigt, dass die nächste Akte erwartet wird */
    suggest_last_regnr(priv);
    gtk_widget_grab_focus(priv->regnr_entry);
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
    g_free(priv->lock_reason_other);
    priv->lock_reason_other = NULL;
    wiederherstellung_vormerkung_loeschen(priv);
    g_free(priv->wh_status);
    priv->wh_status = NULL;

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
