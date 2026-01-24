/*
 sond (sond_offline_manager.c) - Akten, Beweisstücke, Unterlagen
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

/**
 * @file sond_offline_manager.c
 * @brief Implementation der Offline-Akten-Verwaltung
 */

#include "sond_offline_manager.h"
#include "../sond_log_and_error.h"

#include <json-glib/json-glib.h>

#define OFFLINE_CONFIG_FILENAME ".sond-offline.json"
#define CONFIG_VERSION 1

struct _SondOfflineManager {
    GObject parent_instance;

    gchar *sync_directory;      /* Basisverzeichnis */
    gchar *config_file_path;    /* Voller Pfad zur JSON-Datei */
    GList *akten;               /* Liste von SondOfflineAkte* */
};

G_DEFINE_TYPE(SondOfflineManager, sond_offline_manager, G_TYPE_OBJECT)

/* ========================================================================
 * SondOfflineAkte Implementation
 * ======================================================================== */

SondOfflineAkte* sond_offline_akte_new(const gchar *regnr,
                                        const gchar *kurzb,
                                        const gchar *ggstd,
                                        const gchar *seafile_library_id) {
    SondOfflineAkte *akte = g_new0(SondOfflineAkte, 1);

    akte->regnr = g_strdup(regnr);
    akte->kurzb = g_strdup(kurzb);
    akte->ggstd = g_strdup(ggstd);
    akte->seafile_library_id = g_strdup(seafile_library_id);
    akte->last_synced = g_date_time_new_now_local();

    return akte;
}

void sond_offline_akte_free(SondOfflineAkte *akte) {
    if (!akte) return;

    g_free(akte->regnr);
    g_free(akte->kurzb);
    g_free(akte->ggstd);
    g_free(akte->seafile_library_id);
    
    if (akte->last_synced) {
        g_date_time_unref(akte->last_synced);
    }

    g_free(akte);
}

/* ========================================================================
 * JSON Serialisierung
 * ======================================================================== */

/**
 * akte_to_json:
 * 
 * Konvertiert SondOfflineAkte zu JSON
 */
static JsonNode* akte_to_json(SondOfflineAkte *akte) {
    JsonBuilder *builder = json_builder_new();

    json_builder_begin_object(builder);

    json_builder_set_member_name(builder, "regnr");
    json_builder_add_string_value(builder, akte->regnr);

    json_builder_set_member_name(builder, "kurzb");
    json_builder_add_string_value(builder, akte->kurzb);

    json_builder_set_member_name(builder, "ggstd");
    json_builder_add_string_value(builder, akte->ggstd);

    json_builder_set_member_name(builder, "seafile_library_id");
    json_builder_add_string_value(builder, akte->seafile_library_id);

    if (akte->last_synced) {
        json_builder_set_member_name(builder, "last_synced");
        gchar *iso8601 = g_date_time_format_iso8601(akte->last_synced);
        json_builder_add_string_value(builder, iso8601);
        g_free(iso8601);
    }

    json_builder_end_object(builder);

    JsonNode *node = json_builder_get_root(builder);
    g_object_unref(builder);

    return node;
}

/**
 * akte_from_json:
 * 
 * Erstellt SondOfflineAkte aus JSON
 */
static SondOfflineAkte* akte_from_json(JsonObject *obj) {
    SondOfflineAkte *akte = g_new0(SondOfflineAkte, 1);

    akte->regnr = g_strdup(json_object_get_string_member(obj, "regnr"));
    akte->kurzb = g_strdup(json_object_get_string_member(obj, "kurzb"));
    akte->ggstd = g_strdup(json_object_get_string_member(obj, "ggstd"));
    akte->seafile_library_id = g_strdup(json_object_get_string_member(obj, "seafile_library_id"));

    /* last_synced parsen */
    if (json_object_has_member(obj, "last_synced")) {
        const gchar *iso8601 = json_object_get_string_member(obj, "last_synced");
        akte->last_synced = g_date_time_new_from_iso8601(iso8601, NULL);
    } else {
        akte->last_synced = NULL;
    }

    return akte;
}

/* ========================================================================
 * Datei-Operationen
 * ======================================================================== */

/**
 * save_config:
 * 
 * Speichert aktuelle Akten-Liste in JSON-Datei
 */
static gboolean save_config(SondOfflineManager *manager, GError **error) {
    JsonBuilder *builder = json_builder_new();

    json_builder_begin_object(builder);

    /* Version */
    json_builder_set_member_name(builder, "version");
    json_builder_add_int_value(builder, CONFIG_VERSION);

    /* Akten Array */
    json_builder_set_member_name(builder, "akten");
    json_builder_begin_array(builder);

    for (GList *l = manager->akten; l != NULL; l = l->next) {
        SondOfflineAkte *akte = l->data;
        JsonNode *node = akte_to_json(akte);
        json_builder_add_value(builder, node);
    }

    json_builder_end_array(builder);
    json_builder_end_object(builder);

    /* Zu Datei schreiben */
    JsonGenerator *gen = json_generator_new();
    json_generator_set_pretty(gen, TRUE);
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(gen, root);

    gboolean success = json_generator_to_file(gen, manager->config_file_path, error);

    json_node_free(root);
    g_object_unref(gen);
    g_object_unref(builder);

    return success;
}

/**
 * load_config:
 * 
 * Lädt Akten-Liste aus JSON-Datei
 */
static gboolean load_config(SondOfflineManager *manager, GError **error) {
    /* Prüfen ob Datei existiert */
    if (!g_file_test(manager->config_file_path, G_FILE_TEST_EXISTS)) {
        LOG_INFO("Keine Offline-Konfiguration gefunden, starte mit leerer Liste\n");
        return TRUE;  /* Kein Fehler, nur noch keine Datei */
    }

    JsonParser *parser = json_parser_new();

    if (!json_parser_load_from_file(parser, manager->config_file_path, error)) {
        g_object_unref(parser);
        return FALSE;
    }

    JsonNode *root = json_parser_get_root(parser);
    JsonObject *root_obj = json_node_get_object(root);

    /* Version prüfen */
    gint version = json_object_get_int_member(root_obj, "version");
    if (version != CONFIG_VERSION) {
        LOG_WARN("Offline-Konfig hat andere Version (%d vs %d), versuche trotzdem zu laden\n",
                 version, CONFIG_VERSION);
    }

    /* Akten laden */
    JsonArray *akten_array = json_object_get_array_member(root_obj, "akten");
    guint len = json_array_get_length(akten_array);

    for (guint i = 0; i < len; i++) {
        JsonObject *akte_obj = json_array_get_object_element(akten_array, i);
        SondOfflineAkte *akte = akte_from_json(akte_obj);
        manager->akten = g_list_append(manager->akten, akte);
    }

    g_object_unref(parser);

    LOG_INFO("Offline-Akten geladen: %u Akten\n", len);

    return TRUE;
}

/* ========================================================================
 * GObject Implementation
 * ======================================================================== */

static void sond_offline_manager_finalize(GObject *object) {
    SondOfflineManager *self = SOND_OFFLINE_MANAGER(object);

    g_free(self->sync_directory);
    g_free(self->config_file_path);

    g_list_free_full(self->akten, (GDestroyNotify)sond_offline_akte_free);

    G_OBJECT_CLASS(sond_offline_manager_parent_class)->finalize(object);
}

static void sond_offline_manager_class_init(SondOfflineManagerClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = sond_offline_manager_finalize;
}

static void sond_offline_manager_init(SondOfflineManager *self) {
    self->sync_directory = NULL;
    self->config_file_path = NULL;
    self->akten = NULL;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

SondOfflineManager* sond_offline_manager_new(const gchar *sync_directory) {
    g_return_val_if_fail(sync_directory != NULL, NULL);

    SondOfflineManager *manager = g_object_new(SOND_TYPE_OFFLINE_MANAGER, NULL);

    manager->sync_directory = g_strdup(sync_directory);
    manager->config_file_path = g_build_filename(sync_directory, 
                                                  OFFLINE_CONFIG_FILENAME, 
                                                  NULL);

    /* Sync-Verzeichnis erstellen falls nicht vorhanden */
    if (!g_file_test(sync_directory, G_FILE_TEST_EXISTS)) {
        if (g_mkdir_with_parents(sync_directory, 0755) != 0) {
            LOG_ERROR("Konnte Sync-Verzeichnis nicht erstellen: %s\n", sync_directory);
            g_object_unref(manager);
            return NULL;
        }
        LOG_INFO("Sync-Verzeichnis erstellt: %s\n", sync_directory);
    }

    /* Config laden */
    GError *error = NULL;
    if (!load_config(manager, &error)) {
        LOG_ERROR("Fehler beim Laden der Offline-Konfiguration: %s\n", error->message);
        g_error_free(error);
        /* Weiter machen mit leerer Liste */
    }

    return manager;
}

gboolean sond_offline_manager_add_akte(SondOfflineManager *manager,
                                        SondOfflineAkte *akte,
                                        GError **error) {
    g_return_val_if_fail(SOND_IS_OFFLINE_MANAGER(manager), FALSE);
    g_return_val_if_fail(akte != NULL, FALSE);

    /* Prüfen ob bereits vorhanden */
    if (sond_offline_manager_get_akte(manager, akte->regnr)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS,
                   "Akte %s ist bereits in Offline-Liste", akte->regnr);
        return FALSE;
    }

    /* Hinzufügen */
    manager->akten = g_list_append(manager->akten, akte);

    /* Speichern */
    return save_config(manager, error);
}

gboolean sond_offline_manager_remove_akte(SondOfflineManager *manager,
                                           const gchar *regnr,
                                           GError **error) {
    g_return_val_if_fail(SOND_IS_OFFLINE_MANAGER(manager), FALSE);
    g_return_val_if_fail(regnr != NULL, FALSE);

    /* Suchen und entfernen */
    for (GList *l = manager->akten; l != NULL; l = l->next) {
        SondOfflineAkte *akte = l->data;
        if (g_strcmp0(akte->regnr, regnr) == 0) {
            manager->akten = g_list_remove(manager->akten, akte);
            sond_offline_akte_free(akte);

            /* Speichern */
            return save_config(manager, error);
        }
    }

    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
               "Akte %s nicht in Offline-Liste", regnr);
    return FALSE;
}

SondOfflineAkte* sond_offline_manager_get_akte(SondOfflineManager *manager,
                                                const gchar *regnr) {
    g_return_val_if_fail(SOND_IS_OFFLINE_MANAGER(manager), NULL);
    g_return_val_if_fail(regnr != NULL, NULL);

    for (GList *l = manager->akten; l != NULL; l = l->next) {
        SondOfflineAkte *akte = l->data;
        if (g_strcmp0(akte->regnr, regnr) == 0) {
            return akte;
        }
    }

    return NULL;
}

GList* sond_offline_manager_get_all_akten(SondOfflineManager *manager) {
    g_return_val_if_fail(SOND_IS_OFFLINE_MANAGER(manager), NULL);
    return manager->akten;
}

gboolean sond_offline_manager_update_last_synced(SondOfflineManager *manager,
                                                  const gchar *regnr,
                                                  GError **error) {
    g_return_val_if_fail(SOND_IS_OFFLINE_MANAGER(manager), FALSE);
    g_return_val_if_fail(regnr != NULL, FALSE);

    SondOfflineAkte *akte = sond_offline_manager_get_akte(manager, regnr);
    if (!akte) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "Akte %s nicht in Offline-Liste", regnr);
        return FALSE;
    }

    if (akte->last_synced) {
        g_date_time_unref(akte->last_synced);
    }
    akte->last_synced = g_date_time_new_now_local();

    return save_config(manager, error);
}

gboolean sond_offline_manager_is_offline(SondOfflineManager *manager,
                                          const gchar *regnr) {
    g_return_val_if_fail(SOND_IS_OFFLINE_MANAGER(manager), FALSE);
    g_return_val_if_fail(regnr != NULL, FALSE);

    return sond_offline_manager_get_akte(manager, regnr) != NULL;
}

const gchar* sond_offline_manager_get_sync_directory(SondOfflineManager *manager) {
    g_return_val_if_fail(manager != NULL, NULL);
    return manager->sync_directory;
}
