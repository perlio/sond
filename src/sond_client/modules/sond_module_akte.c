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
} SondModuleAktePrivate;

/* Forward declarations */
static void set_akte_state(SondModuleAktePrivate *priv, AkteState new_state);
static void update_ui_from_node(SondModuleAktePrivate *priv);
static void clear_akte_fields(SondModuleAktePrivate *priv);
static void update_status_display(SondModuleAktePrivate *priv);
static void on_offline_toggle_toggled(GtkCheckButton *toggle, SondModuleAktePrivate *priv);

/* ========================================================================
 * UI State Management
 * ======================================================================== */

static void set_akte_state(SondModuleAktePrivate *priv, AkteState new_state) {
    priv->state = new_state;

    switch (new_state) {
        case AKTE_STATE_INITIAL:
            /* Nur "Neue Akte" + Entry aktiv */
            gtk_widget_set_sensitive(priv->regnr_entry, TRUE);
            gtk_widget_set_sensitive(priv->btn_neue_akte, TRUE);
            gtk_widget_set_sensitive(priv->akte_fields_box, FALSE);
            gtk_widget_set_sensitive(priv->btn_ok, FALSE);
            gtk_widget_set_sensitive(priv->btn_speichern, FALSE);
            gtk_widget_set_sensitive(priv->btn_abbrechen, TRUE);
            break;

        case AKTE_STATE_CREATING:
            /* Entry insensitiv, Felder editierbar */
            gtk_widget_set_sensitive(priv->regnr_entry, FALSE);
            gtk_widget_set_sensitive(priv->btn_neue_akte, FALSE);
            gtk_widget_set_sensitive(priv->akte_fields_box, TRUE);
            gtk_widget_set_sensitive(priv->btn_ok, TRUE);
            gtk_widget_set_sensitive(priv->btn_speichern, TRUE);
            gtk_widget_set_sensitive(priv->btn_abbrechen, TRUE);
            break;

        case AKTE_STATE_EDITING:
            /* Alles editierbar außer Entry */
            gtk_widget_set_sensitive(priv->regnr_entry, FALSE);
            gtk_widget_set_sensitive(priv->btn_neue_akte, FALSE);
            gtk_widget_set_sensitive(priv->akte_fields_box, TRUE);
            gtk_widget_set_sensitive(priv->btn_ok, TRUE);
            gtk_widget_set_sensitive(priv->btn_speichern, TRUE);
            gtk_widget_set_sensitive(priv->btn_abbrechen, TRUE);
            break;

        case AKTE_STATE_READONLY:
            /* Nur Ansicht */
            gtk_widget_set_sensitive(priv->regnr_entry, FALSE);
            gtk_widget_set_sensitive(priv->btn_neue_akte, FALSE);
            gtk_widget_set_sensitive(priv->akte_fields_box, FALSE);
            gtk_widget_set_sensitive(priv->btn_ok, TRUE);
            gtk_widget_set_sensitive(priv->btn_speichern, FALSE);
            gtk_widget_set_sensitive(priv->btn_abbrechen, TRUE);
            break;

        case AKTE_STATE_ABGELEGT:
            /* Abgelegte Akte: Felder sichtbar aber nicht editierbar, Reaktivierung möglich */
            gtk_widget_set_sensitive(priv->regnr_entry, FALSE);
            gtk_widget_set_sensitive(priv->btn_neue_akte, FALSE);
            gtk_widget_set_sensitive(priv->akte_fields_box, FALSE);  /* Felder nicht editierbar */
            gtk_widget_set_sensitive(priv->btn_ok, TRUE);
            gtk_widget_set_sensitive(priv->btn_speichern, FALSE);
            gtk_widget_set_sensitive(priv->btn_abbrechen, TRUE);
            /* Reaktivierungs-Button bleibt aktiv (via update_status_display) */
            gtk_widget_set_sensitive(priv->btn_reaktivieren, TRUE);
            break;
    }
}

static void clear_akte_fields(SondModuleAktePrivate *priv) {
    gtk_editable_set_text(GTK_EDITABLE(priv->regnr_entry), "");
    gtk_editable_set_text(GTK_EDITABLE(priv->entry_kurzbezeichnung), "");
    gtk_editable_set_text(GTK_EDITABLE(priv->textview_gegenstand), "");  /* Jetzt Entry */
    
    /* Status zurücksetzen */
    if (priv->status_label) {
        gtk_label_set_text(GTK_LABEL(priv->status_label), "Status: -");
        gtk_widget_set_visible(priv->btn_ablegen, FALSE);
        gtk_widget_set_visible(priv->btn_reaktivieren, FALSE);
    }

    /* Offline-Toggle zurücksetzen */
	if (priv->offline_toggle) {
		g_signal_handlers_block_by_func(priv->offline_toggle, on_offline_toggle_toggled, priv);
		gtk_check_button_set_active(GTK_CHECK_BUTTON(priv->offline_toggle), FALSE);
		g_signal_handlers_unblock_by_func(priv->offline_toggle, on_offline_toggle_toggled, priv);
		gtk_widget_set_visible(priv->offline_toggle, FALSE);
	}
}

/* ========================================================================
 * Helper Functions
 * ======================================================================== */

static void show_error_dialog(GtkWidget *parent, const gchar *title, const gchar *message) {
    LOG_ERROR("%s: %s\n", title, message);
    
    GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", title);
    gtk_alert_dialog_set_detail(dialog, message);
    gtk_alert_dialog_set_modal(dialog, TRUE);
    
    gtk_alert_dialog_show(dialog, GTK_WINDOW(gtk_widget_get_root(parent)));
    g_object_unref(dialog);
}

static void show_info_dialog(GtkWidget *parent, const gchar *title, const gchar *message) {
    LOG_INFO("%s: %s\n", title, message);
    
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
    if (!priv->current_node || !priv->status_label) {
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
    if (!priv->current_node) {
        clear_akte_fields(priv);
        if (priv->status_label) {
            gtk_label_set_text(GTK_LABEL(priv->status_label), "Status: -");
            gtk_widget_set_visible(priv->btn_ablegen, FALSE);
            gtk_widget_set_visible(priv->btn_reaktivieren, FALSE);
        }
        return;
    }
    
    /* RegNr extrahieren */
    GPtrArray *regnr_values = sond_graph_node_get_property(priv->current_node, "regnr");
    if (regnr_values && regnr_values->len == 2) {
        guint year = (guint)g_ascii_strtoull(g_ptr_array_index(regnr_values, 0), NULL, 10);  /* Index 0 = Jahr */
        guint lfd = (guint)g_ascii_strtoull(g_ptr_array_index(regnr_values, 1), NULL, 10);   /* Index 1 = lfd_nr */
        gchar *regnr_display = format_regnr(lfd, year);
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
	if (priv->offline_toggle && priv->current_node) {
		gint64 node_id = sond_graph_node_get_id(priv->current_node);

		if (node_id > 0) {
			GPtrArray *regnr_values = sond_graph_node_get_property(priv->current_node, "regnr");
			if (regnr_values && regnr_values->len == 2) {
				guint year = (guint)g_ascii_strtoull(g_ptr_array_index(regnr_values, 0), NULL, 10);
				guint lfd = (guint)g_ascii_strtoull(g_ptr_array_index(regnr_values, 1), NULL, 10);
				gchar *regnr = format_regnr(lfd, year);
				g_ptr_array_unref(regnr_values);

				SondOfflineManager *manager = sond_client_get_offline_manager(priv->client);
				if (manager) {
					gboolean is_offline = sond_offline_manager_is_offline(manager, regnr);

					g_signal_handlers_block_by_func(priv->offline_toggle, on_offline_toggle_toggled, priv);
					gtk_check_button_set_active(GTK_CHECK_BUTTON(priv->offline_toggle), is_offline);
					g_signal_handlers_unblock_by_func(priv->offline_toggle, on_offline_toggle_toggled, priv);

					gtk_widget_set_visible(priv->offline_toggle, TRUE);
				} else {
					gtk_widget_set_visible(priv->offline_toggle, FALSE);
				}

				g_free(regnr);
			} else {
				gtk_widget_set_visible(priv->offline_toggle, FALSE);
				if (regnr_values) g_ptr_array_unref(regnr_values);
			}
		} else {
			gtk_widget_set_visible(priv->offline_toggle, FALSE);
		}
	}
}

/* ========================================================================
 * Server Communication
 * ======================================================================== */

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
    LOG_INFO("Nächste RegNr: %u/%u\n", *lfd_nr, *year);
    
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
    
    LOG_INFO("Erstelle Seafile Library '%s'...\n", library_name);
    
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
    
    LOG_INFO("Node mit ID %" G_GINT64_FORMAT " gespeichert\n",
            sond_graph_node_get_id(priv->current_node));
    
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

/* Event Handler für Offline-Toggle */
static void on_offline_toggle_toggled(GtkCheckButton *toggle, SondModuleAktePrivate *priv) {
    if (!priv->current_node) {
        return;
    }

    /* Hole Offline Manager */
    SondOfflineManager *manager = sond_client_get_offline_manager(priv->client);
    if (!manager) {
        show_error_dialog(priv->main_widget, "Fehler", "Offline Manager nicht verfügbar");
        return;
    }

    /* RegNr extrahieren */
    GPtrArray *regnr_values = sond_graph_node_get_property(priv->current_node, "regnr");
    if (!regnr_values || regnr_values->len != 2) {
        show_error_dialog(priv->main_widget, "Fehler", "Akte hat keine gültige RegNr");
        if (regnr_values) g_ptr_array_unref(regnr_values);
        return;
    }

    guint year = (guint)g_ascii_strtoull(g_ptr_array_index(regnr_values, 0), NULL, 10);
    guint lfd = (guint)g_ascii_strtoull(g_ptr_array_index(regnr_values, 1), NULL, 10);
    gchar *regnr = format_regnr(lfd, year);
    g_ptr_array_unref(regnr_values);

    gboolean is_active = gtk_check_button_get_active(toggle);

    /* Library ID ermitteln */
     gchar *library_name = g_strdup_printf("%u-%u", year, lfd);
     GError *error = NULL;
     gchar *library_id = NULL;

     if (is_active) {
         /* Toggle AN: Library noch nicht synchronisiert - vom Server holen */
         library_id = sond_seafile_get_library_id_from_server(priv->client, library_name, &error);
     } else {
         /* Toggle AUS: Library ist synchronisiert - lokal finden */
         library_id = sond_seafile_find_library_by_name(library_name, &error);
     }

     g_free(library_name);

     if (!library_id) {
        /* Library nicht gefunden - Fehler anzeigen */
    	GtkAlertDialog *dialog = gtk_alert_dialog_new("Seafile Library nicht gefunden");

    	if (error) {
    	    gtk_alert_dialog_set_detail(dialog, error->message);
    	    g_error_free(error);
    	}

    	gtk_alert_dialog_show(dialog, GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(toggle))));
    	g_object_unref(dialog);

        /* Toggle zurücksetzen */
        g_signal_handlers_block_by_func(toggle, on_offline_toggle_toggled, NULL);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(toggle), FALSE);
        g_signal_handlers_unblock_by_func(toggle, on_offline_toggle_toggled, NULL);

        return;
    }

	if (is_active) {
        /* Offline aktivieren */
        LOG_INFO("Aktiviere Offline für Akte %s\n", regnr);

        /* Prüfen ob bereits offline */
        if (sond_offline_manager_is_offline(manager, regnr)) {
            LOG_INFO("Akte %s ist bereits offline\n", regnr);
            g_free(regnr);
            return;
        }

        /* Hole Kurzbezeichnung und Gegenstand */
        gchar *kurzb = sond_graph_node_get_property_string(priv->current_node, "kurzb");
        gchar *ggstd = sond_graph_node_get_property_string(priv->current_node, "ggstd");

        /* sync_directory vom Offline Manager holen */
		const gchar *sync_dir = sond_offline_manager_get_sync_directory(manager);
		gchar *local_path = g_build_filename(sync_dir, regnr, NULL);

        /* Akte zur Offline-Liste hinzufügen */
        SondOfflineAkte *akte = sond_offline_akte_new(
            regnr,
            kurzb ? kurzb : "",
            ggstd ? ggstd : "",
            library_id,
            local_path
        );

        if (!sond_offline_manager_add_akte(manager, akte, &error)) {
            gchar *msg = g_strdup_printf("Fehler beim Aktivieren: %s",
                                        error ? error->message : "Unbekannt");
            show_error_dialog(priv->main_widget, "Fehler", msg);
            g_free(msg);
            if (error) g_error_free(error);

            sond_offline_akte_free(akte);
            g_free(kurzb);
            g_free(ggstd);
            g_free(library_id);
            g_free(local_path);
            g_free(regnr);

            /* Toggle zurücksetzen */
            g_signal_handlers_block_by_func(toggle, on_offline_toggle_toggled, priv);
            gtk_check_button_set_active(toggle, FALSE);
            g_signal_handlers_unblock_by_func(toggle, on_offline_toggle_toggled, priv);
            return;
        }

        /* Seafile Sync starten */
		if (!sond_seafile_sync_library(library_id, local_path, &error)) {
			/* Sync-Start fehlgeschlagen */
			GtkAlertDialog *dialog = gtk_alert_dialog_new("Seafile Sync konnte nicht gestartet werden");

			if (error) {
			    gtk_alert_dialog_set_detail(dialog, error->message);
			    g_error_free(error);
			}

			gtk_alert_dialog_show(dialog, GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(toggle))));
			g_object_unref(dialog);

			/* Toggle zurücksetzen */
			g_signal_handlers_block_by_func(toggle, on_offline_toggle_toggled, NULL);
			gtk_check_button_set_active(GTK_CHECK_BUTTON(toggle), FALSE);
			g_signal_handlers_unblock_by_func(toggle, on_offline_toggle_toggled, NULL);

			g_free(library_id);
			return;
		}

		LOG_INFO("Seafile Sync gestartet: %s -> %s\n", library_id, local_path);

        show_info_dialog(priv->main_widget, "Offline aktiviert",
                        "Akte ist jetzt für Offline-Nutzung verfügbar.");

        g_free(kurzb);
        g_free(ggstd);
        g_free(library_id);
        g_free(local_path);

    } else {
        /* Offline deaktivieren */
        LOG_INFO("Deaktiviere Offline für Akte %s\n", regnr);

        if (!sond_offline_manager_is_offline(manager, regnr)) {
            LOG_INFO("Akte %s ist nicht offline\n", regnr);
            g_free(regnr);
            return;
        }

        /* Sync deaktivieren (Dateien bleiben lokal) */
        if (!sond_offline_manager_set_sync_enabled(manager, regnr, FALSE, &error)) {
            gchar *msg = g_strdup_printf("Fehler beim Deaktivieren: %s",
                                        error ? error->message : "Unbekannt");
            show_error_dialog(priv->main_widget, "Fehler", msg);
            g_free(msg);
            if (error) g_error_free(error);
            g_free(regnr);

            /* Toggle zurücksetzen */
            g_signal_handlers_block_by_func(toggle, on_offline_toggle_toggled, priv);
            gtk_check_button_set_active(toggle, TRUE);
            g_signal_handlers_unblock_by_func(toggle, on_offline_toggle_toggled, priv);
            return;
        }

        /* Seafile Sync stoppen */
		if (!sond_seafile_unsync_library(library_id, &error)) {
			/* Sync-Stop fehlgeschlagen - nur warnen, nicht kritisch */
			LOG_WARN("Seafile Sync konnte nicht gestoppt werden: %s\n",
					   error ? error->message : "(unbekannter Fehler)");
			if (error) {
				g_error_free(error);
			}
		} else {
			LOG_INFO("Seafile Sync gestoppt: %s\n", library_id);
		}

        show_info_dialog(priv->main_widget, "Offline deaktiviert",
                        "Synchronisation wurde pausiert. Dateien bleiben lokal verfügbar.");
    }

    g_free(regnr);
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
        
        g_print("[AKTE] Calling sond_client_create_and_lock_node now...\n");
        
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
        
        LOG_INFO("Neue Akte mit RegNr %u/%u angelegt und gespeichert (ID: %" G_GINT64_FORMAT ")\n",
                lfd_nr, year, sond_graph_node_get_id(priv->current_node));
    } else {
        /* Benutzer hat "Nein" geklickt - Abbruch */
        LOG_INFO("Anlegen neue Akte %u/%u abgebrochen\n", lfd_nr, year);
    }
    
    g_free(data);
}

static void on_regnr_entry_activate(GtkEntry *entry, SondModuleAktePrivate *priv) {
    const gchar *regnr_text = gtk_editable_get_text(GTK_EDITABLE(entry));
    
    guint lfd_nr, year;
    if (!parse_regnr(regnr_text, &lfd_nr, &year)) {
        show_error_dialog(priv->main_widget, "Ungültiges Format",
                         "Bitte RegNr im Format '12/26' eingeben.");
        return;
    }
    
    GError *error = NULL;
    SondGraphNode *node = search_node_by_regnr(priv, lfd_nr, year, &error);
    
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
            LOG_INFO("Akte %u/%u geladen und gelockt\n", lfd_nr, year);
            update_ui_from_node(priv);
            
            /* Prüfe ob Akte abgelegt ist */
            if (ist_akte_aktiv(priv->current_node)) {
                set_akte_state(priv, AKTE_STATE_EDITING);  /* Aktiv -> editierbar */
            } else {
                set_akte_state(priv, AKTE_STATE_ABGELEGT);  /* Abgelegt -> read-only mit Reaktivierung */
            }
        } else {
            /* Lock fehlgeschlagen (bereits gelockt) → READONLY */
            LOG_INFO("Akte %u/%u gelockt von anderem User, öffne read-only\n", lfd_nr, year);
            
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
    
    /* Akte existiert nicht */
    if (error && error->code != G_IO_ERROR_NOT_FOUND) {
        show_error_dialog(priv->main_widget, "Fehler", error->message);
        g_error_free(error);
        return;
    }
    
    if (error) {
        g_error_free(error);
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
    gtk_editable_set_text(GTK_EDITABLE(priv->regnr_entry), "Neue Akte");
    
    /* Felder leer lassen */
    gtk_editable_set_text(GTK_EDITABLE(priv->entry_kurzbezeichnung), "");
    gtk_editable_set_text(GTK_EDITABLE(priv->textview_gegenstand), "");  /* Jetzt Entry */
    
    set_akte_state(priv, AKTE_STATE_CREATING);
}

static void on_speichern_clicked(GtkButton *button, SondModuleAktePrivate *priv) {
    if (!priv->current_node) {
        show_error_dialog(priv->main_widget, "Nichts zu speichern",
                         "Bitte erst eine Akte laden oder neu erstellen.");
        return;
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
            
            LOG_INFO("Versuch %d/%d: RegNr %u/%u vergeben\n", attempt + 1, MAX_RETRIES, lfd_nr, year);
            
            /* ZUERST: Seafile Library erstellen! */
            gchar *library_id = NULL;
            GError *lib_error = NULL;
            library_id = create_seafile_library(priv, year, lfd_nr, &lib_error);
            
            if (!library_id) {
                /* Library-Erstellung fehlgeschlagen */
                gchar *msg = g_strdup_printf("Fehler beim Erstellen der Seafile Library: %s",
                                            lib_error ? lib_error->message : "Unbekannt");
                show_error_dialog(priv->main_widget, "Library-Erstellung fehlgeschlagen", msg);
                g_free(msg);
                if (lib_error) g_error_free(lib_error);
                return;
            }
            
            LOG_INFO("Seafile Library erstellt (ID: %s), speichere jetzt Node...\n", library_id);
            
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
            
            /* Versuch zu speichern */
            error = NULL;
            if (save_node(priv, &error)) {
                /* Erfolg! */
                success = TRUE;
                priv->is_new_from_entry = FALSE;
                update_ui_from_node(priv);
                
                gchar *regnr_display = format_regnr(lfd_nr, year);
                gchar *msg = g_strdup_printf("Akte %s wurde erfolgreich gespeichert.", regnr_display);
                show_info_dialog(priv->main_widget, "Gespeichert", msg);
                g_free(msg);
                g_free(regnr_display);
                g_free(library_id);
                
                LOG_INFO("Akte erfolgreich gespeichert mit RegNr %u/%u (nach %d Versuch(en))\n",
                        lfd_nr, year, attempt + 1);
            } else {
                /* Fehler beim Speichern - Library zurückrollen! */
                LOG_ERROR("Node-Speichern fehlgeschlagen, rolle Seafile Library zurück...\n");
                
                GError *del_error = NULL;
                if (!delete_seafile_library(priv, library_id, &del_error)) {
                    LOG_ERROR("Rollback der Library fehlgeschlagen: %s\n",
                             del_error ? del_error->message : "Unbekannt");
                    if (del_error) g_error_free(del_error);
                }
                g_free(library_id);
                
                if (error && (error->code == 409 || 
                             (error->message && strstr(error->message, "Duplicate")) ||
                             (error->message && strstr(error->message, "already exists")))) {
                    /* RegNr-Konflikt → Retry */
                    LOG_INFO("RegNr-Konflikt bei %u/%u, versuche mit nächster RegNr...\n", lfd_nr, year);
                    if (error) g_error_free(error);
                    /* Schleife läuft weiter */
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
        }
        
        if (!success) {
            show_error_dialog(priv->main_widget, "Speichern fehlgeschlagen",
                            "Nach 10 Versuchen konnte keine freie RegNr vergeben werden. "
                            "Bitte später erneut versuchen.");
        }
        
        return;  /* Fertig (Erfolg oder max retries erreicht) */
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
    GPtrArray *von_props = sond_graph_node_get_property(priv->current_node, "von");
    if (!von_props || von_props->len == 0) {
        GDateTime *now = g_date_time_new_now_local();
        gchar *datum = g_date_time_format(now, "%Y-%m-%d");
        sond_graph_node_set_property_string(priv->current_node, "von", datum);
        g_date_time_unref(now);
        g_free(datum);
    }
    if (von_props) {
        g_ptr_array_unref(von_props);
    }
    
    /* Speichern */
    GError *error = NULL;
    if (save_node(priv, &error)) {
        priv->is_new_from_entry = FALSE; /* Nach Speichern nicht mehr löschen */
        
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
    /* Erst speichern */
    on_speichern_clicked(button, priv);
    
    /* Lock freigeben falls Node in DB */
    if (priv->current_node && sond_graph_node_get_id(priv->current_node) > 0) {
        gint64 node_id = sond_graph_node_get_id(priv->current_node);
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
