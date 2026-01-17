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
    AKTE_STATE_READONLY       /* Akte ist gelockt von anderem User */
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
} SondModuleAktePrivate;

/* Forward declarations */
static void set_akte_state(SondModuleAktePrivate *priv, AkteState new_state);
static void update_ui_from_node(SondModuleAktePrivate *priv);
static void clear_akte_fields(SondModuleAktePrivate *priv);

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
    }
}

static void clear_akte_fields(SondModuleAktePrivate *priv) {
    gtk_editable_set_text(GTK_EDITABLE(priv->regnr_entry), "");
    gtk_editable_set_text(GTK_EDITABLE(priv->entry_kurzbezeichnung), "");
    gtk_editable_set_text(GTK_EDITABLE(priv->textview_gegenstand), "");  /* Jetzt Entry */
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

static void update_ui_from_node(SondModuleAktePrivate *priv) {
    if (!priv->current_node) {
        clear_akte_fields(priv);
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
    gchar *criteria_json = sond_graph_node_search_criteria_to_json(criteria);
    sond_graph_node_search_criteria_free(criteria);
    
    /* HTTP-Request */
    gchar *url = g_strdup_printf("%s/node/search", sond_client_get_server_url(priv->client));
    
    SoupSession *session = soup_session_new();
    SoupMessage *msg = soup_message_new("POST", url);
    g_free(url);
    
    soup_message_set_request_body_from_bytes(msg, "application/json",
        g_bytes_new_take(criteria_json, strlen(criteria_json)));
    
    GBytes *response = soup_session_send_and_read(session, msg, NULL, error);
    
    if (!response) {
        g_object_unref(msg);
        g_object_unref(session);
        return NULL;
    }
    
    guint status = soup_message_get_status(msg);
    if (status != 200) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Server gab Status %u zurück", status);
        g_bytes_unref(response);
        g_object_unref(msg);
        g_object_unref(session);
        return NULL;
    }
    
    /* Response parsen */
    gsize size;
    const gchar *data = g_bytes_get_data(response, &size);
    
    JsonParser *parser = json_parser_new();
    SondGraphNode *result_node = NULL;
    
    if (json_parser_load_from_data(parser, data, size, error)) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);
        
        if (json_object_has_member(obj, "data")) {
            JsonArray *nodes_array = json_object_get_array_member(obj, "data");
            
            /* Durch Ergebnisse iterieren */
            for (guint i = 0; i < json_array_get_length(nodes_array); i++) {
                JsonNode *node_json = json_array_get_element(nodes_array, i);
                
                JsonGenerator *gen = json_generator_new();
                json_generator_set_root(gen, node_json);
                gchar *node_str = json_generator_to_data(gen, NULL);
                
                SondGraphNode *node = sond_graph_node_from_json(node_str, NULL);
                g_free(node_str);
                g_object_unref(gen);
                
                if (node) {
                    GPtrArray *regnr_values = sond_graph_node_get_property(node, "regnr");
                    
                    if (regnr_values && regnr_values->len == 2) {
                        guint node_year = (guint)g_ascii_strtoull(g_ptr_array_index(regnr_values, 0), NULL, 10);  /* Index 0 = Jahr */
                        guint node_lfd = (guint)g_ascii_strtoull(g_ptr_array_index(regnr_values, 1), NULL, 10);   /* Index 1 = lfd_nr */
                        
                        if (node_lfd == lfd_nr && node_year == year) {
                            result_node = node;
                            g_ptr_array_unref(regnr_values);
                            break;
                        }
                        
                        g_ptr_array_unref(regnr_values);
                    }
                    
                    if (!result_node) {
                        g_object_unref(node);
                    }
                }
            }
            
            if (!result_node) {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                           "Akte mit RegNr %u/%u nicht gefunden", lfd_nr, year);
            }
        }
    }
    
    g_object_unref(parser);
    g_bytes_unref(response);
    g_object_unref(msg);
    g_object_unref(session);
    
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
    
    gchar *criteria_json = sond_graph_node_search_criteria_to_json(criteria);
    sond_graph_node_search_criteria_free(criteria);
    
    gchar *url = g_strdup_printf("%s/node/search", sond_client_get_server_url(priv->client));
    
    SoupSession *session = soup_session_new();
    SoupMessage *msg = soup_message_new("POST", url);
    g_free(url);
    
    soup_message_set_request_body_from_bytes(msg, "application/json",
        g_bytes_new_take(criteria_json, strlen(criteria_json)));
    
    GBytes *response = soup_session_send_and_read(session, msg, NULL, error);
    
    if (!response) {
        g_object_unref(msg);
        g_object_unref(session);
        return FALSE;
    }
    
    guint status = soup_message_get_status(msg);
    if (status != 200) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Server gab Status %u zurück", status);
        g_bytes_unref(response);
        g_object_unref(msg);
        g_object_unref(session);
        return FALSE;
    }
    
    gsize size;
    const gchar *data = g_bytes_get_data(response, &size);
    
    JsonParser *parser = json_parser_new();
    guint max_lfd_nr = 0;
    
    if (json_parser_load_from_data(parser, data, size, NULL)) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);
        
        if (json_object_has_member(obj, "data")) {
            JsonArray *nodes_array = json_object_get_array_member(obj, "data");
            
            for (guint i = 0; i < json_array_get_length(nodes_array); i++) {
                JsonNode *node_json = json_array_get_element(nodes_array, i);
                
                JsonGenerator *gen = json_generator_new();
                json_generator_set_root(gen, node_json);
                gchar *node_str = json_generator_to_data(gen, NULL);
                
                SondGraphNode *node = sond_graph_node_from_json(node_str, NULL);
                g_free(node_str);
                g_object_unref(gen);
                
                if (node) {
                    GPtrArray *regnr_values = sond_graph_node_get_property(node, "regnr");
                    
                    if (regnr_values && regnr_values->len == 2) {
                        guint node_year = (guint)g_ascii_strtoull(g_ptr_array_index(regnr_values, 0), NULL, 10);  /* Index 0 = Jahr */
                        guint node_lfd = (guint)g_ascii_strtoull(g_ptr_array_index(regnr_values, 1), NULL, 10);   /* Index 1 = lfd_nr */
                        
                        if (node_year == *year && node_lfd > max_lfd_nr) {
                            max_lfd_nr = node_lfd;
                        }
                        
                        g_ptr_array_unref(regnr_values);
                    }
                    
                    g_object_unref(node);
                }
            }
        }
    }
    
    g_object_unref(parser);
    g_bytes_unref(response);
    g_object_unref(msg);
    g_object_unref(session);
    
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
    
    /* JSON Body erstellen */
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "name");
    json_builder_add_string_value(builder, library_name);
    json_builder_set_member_name(builder, "desc");
    gchar *desc = g_strdup_printf("Akte %u/%u", lfd_nr, year % 100);
    json_builder_add_string_value(builder, desc);
    g_free(desc);
    json_builder_end_object(builder);
    
    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, json_builder_get_root(builder));
    gchar *request_json = json_generator_to_data(gen, NULL);
    g_object_unref(gen);
    g_object_unref(builder);
    
    /* HTTP-Request */
    gchar *url = g_strdup_printf("%s/seafile/library", sond_client_get_server_url(priv->client));
    
    SoupSession *session = soup_session_new();
    SoupMessage *msg = soup_message_new("POST", url);
    g_free(url);
    
    soup_message_set_request_body_from_bytes(msg, "application/json",
        g_bytes_new_take(request_json, strlen(request_json)));
    
    GBytes *response = soup_session_send_and_read(session, msg, NULL, error);
    
    if (!response) {
        g_free(library_name);
        g_object_unref(msg);
        g_object_unref(session);
        return NULL;
    }
    
    guint status = soup_message_get_status(msg);
    if (status != 200) {
        gsize size;
        const gchar *body = g_bytes_get_data(response, &size);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Server gab Status %u zurück: %.*s", status, (int)size, body);
        g_free(library_name);
        g_bytes_unref(response);
        g_object_unref(msg);
        g_object_unref(session);
        return NULL;
    }
    
    /* Response parsen */
    gsize size;
    const gchar *data = g_bytes_get_data(response, &size);
    
    JsonParser *parser = json_parser_new();
    gchar *library_id = NULL;
    
    if (json_parser_load_from_data(parser, data, size, error)) {
        JsonNode *root = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root);
        
        if (json_object_has_member(obj, "data")) {
            JsonObject *data_obj = json_object_get_object_member(obj, "data");
            if (json_object_has_member(data_obj, "library_id")) {
                const gchar *lib_id = json_object_get_string_member(data_obj, "library_id");
                library_id = g_strdup(lib_id);
                LOG_INFO("Seafile Library '%s' erstellt (ID: %s)\n", library_name, library_id);
            }
        }
    }
    
    g_object_unref(parser);
    g_bytes_unref(response);
    g_object_unref(msg);
    g_object_unref(session);
    g_free(library_name);
    
    if (!library_id) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Seafile Library konnte nicht erstellt werden");
    }
    
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
    
    gchar *url = g_strdup_printf("%s/seafile/library/%s",
                                 sond_client_get_server_url(priv->client),
                                 library_id);
    
    SoupSession *session = soup_session_new();
    SoupMessage *msg = soup_message_new("DELETE", url);
    g_free(url);
    
    GBytes *response = soup_session_send_and_read(session, msg, NULL, error);
    
    if (!response) {
        g_object_unref(msg);
        g_object_unref(session);
        return FALSE;
    }
    
    guint status = soup_message_get_status(msg);
    gboolean success = (status == 200 || status == 204);
    
    if (!success) {
        gsize size;
        const gchar *body = g_bytes_get_data(response, &size);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Server gab Status %u zurück: %.*s", status, (int)size, body);
    } else {
        LOG_INFO("Seafile Library gelöscht (ID: %s)\n", library_id);
    }
    
    g_bytes_unref(response);
    g_object_unref(msg);
    g_object_unref(session);
    
    return success;
}

static gboolean save_node(SondModuleAktePrivate *priv, GError **error) {
    if (!priv->current_node) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                   "Kein Node zum Speichern vorhanden");
        return FALSE;
    }
    
    sond_graph_node_set_label(priv->current_node, "Akte");
    
    /* Node direkt zu JSON serialisieren - wie der Server es macht */
    gchar *node_json = sond_graph_node_to_json(priv->current_node);
    
    if (!node_json) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Konnte Node nicht serialisieren");
        return FALSE;
    }
    
    LOG_INFO("Sende Node-JSON (%zu bytes):\n%s\n", strlen(node_json), node_json);
    
    /* HTTP-Request */
    gchar *url = g_strdup_printf("%s/node/save", sond_client_get_server_url(priv->client));
    
    SoupSession *session = soup_session_new();
    SoupMessage *msg = soup_message_new("POST", url);
    g_free(url);
    
    soup_message_set_request_body_from_bytes(msg, "application/json",
        g_bytes_new_take(node_json, strlen(node_json)));
    
    GBytes *response = soup_session_send_and_read(session, msg, NULL, error);
    
    if (!response) {
        g_object_unref(msg);
        g_object_unref(session);
        return FALSE;
    }
    
    guint status = soup_message_get_status(msg);
    if (status != 200) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Server gab Status %u zurück", status);
        g_bytes_unref(response);
        g_object_unref(msg);
        g_object_unref(session);
        return FALSE;
    }
    
    /* Gespeicherten Node zurückladen */
    gsize size;
    const gchar *data = g_bytes_get_data(response, &size);
    
    JsonParser *parser = json_parser_new();
    if (json_parser_load_from_data(parser, data, size, NULL)) {
        JsonNode *root_node = json_parser_get_root(parser);
        JsonObject *obj = json_node_get_object(root_node);
        
        if (json_object_has_member(obj, "data")) {
            JsonNode *data_node = json_object_get_member(obj, "data");
            JsonGenerator *data_gen = json_generator_new();
            json_generator_set_root(data_gen, data_node);
            gchar *saved_json = json_generator_to_data(data_gen, NULL);
            
            g_object_unref(priv->current_node);
            priv->current_node = sond_graph_node_from_json(saved_json, NULL);
            
            LOG_INFO("Node mit ID %" G_GINT64_FORMAT " gespeichert\n", 
                    sond_graph_node_get_id(priv->current_node));
            
            g_free(saved_json);
            g_object_unref(data_gen);
        }
    }
    
    g_object_unref(parser);
    g_bytes_unref(response);
    g_object_unref(msg);
    g_object_unref(session);
    
    return TRUE;
}

static gboolean delete_node(SondModuleAktePrivate *priv, gint64 node_id, GError **error) {
    gchar *url = g_strdup_printf("%s/node/delete/%" G_GINT64_FORMAT, 
                                sond_client_get_server_url(priv->client),
                                node_id);
    
    SoupSession *session = soup_session_new();
    SoupMessage *msg = soup_message_new("DELETE", url);
    g_free(url);
    
    GBytes *response = soup_session_send_and_read(session, msg, NULL, error);

    if (!response) {
        g_object_unref(msg);
        g_object_unref(session);
        return FALSE;
    }

    guint status = soup_message_get_status(msg);
    gboolean success = (status == 200);
    
    if (!success) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Server gab Status %u zurück", status);
    }

    g_bytes_unref(response);
    g_object_unref(msg);
    g_object_unref(session);
    
    return success;
}

/* ========================================================================
 * UI Event Handlers
 * ======================================================================== */

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
        
        /* Status = draft setzen */
        sond_graph_node_set_property_string(priv->current_node, "stat", "draft");
        
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
            /* Lock erfolgreich → EDITING */
            LOG_INFO("Akte %u/%u geladen und gelockt\n", lfd_nr, year);
            update_ui_from_node(priv);
            set_akte_state(priv, AKTE_STATE_EDITING);
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
            
            /* Status auf "complete" setzen */
            sond_graph_node_set_property_string(priv->current_node, "stat", "complete");
            
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
    
    /* Status auf "complete" setzen (falls es "draft" war) */
    sond_graph_node_set_property_string(priv->current_node, "stat", "complete");
    
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
