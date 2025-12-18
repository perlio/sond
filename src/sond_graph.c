/*
 sond (sond_fileparts.h) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2025  peloamerica

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

#include <glib.h>
#include <mysql/mysql.h>
#include <json-glib/json-glib.h>

#include <stdio.h>

/**
 * sond_graph_db_exists:
 * @mysql: Aktive MySQL-Verbindung
 * @db_name: Name der Datenbank
 *
 * Prüft, ob die Datenbank bereits existiert.
 *
 * Returns: TRUE wenn die Datenbank existiert, FALSE sonst
 */
static gboolean sond_graph_db_exists(MYSQL *mysql, const gchar *db_name) {
    gchar *query = g_strdup_printf(
        "SELECT SCHEMA_NAME FROM INFORMATION_SCHEMA.SCHEMATA WHERE SCHEMA_NAME = '%s'",
        db_name
    );

    if (mysql_query(mysql, query)) {
        g_free(query);
        return FALSE;
    }
    g_free(query);

    MYSQL_RES *result = mysql_store_result(mysql);
    if (!result)
        return FALSE;

    gboolean exists = (mysql_num_rows(result) > 0);
    mysql_free_result(result);

    return exists;
}

/**
 * sond_graph_prompt_overwrite:
 * @db_name: Name der Datenbank
 *
 * Fragt den Benutzer, ob die existierende Datenbank überschrieben werden soll.
 *
 * Returns: TRUE wenn überschreiben, FALSE wenn abbrechen
 */
static gboolean sond_graph_prompt_overwrite(const gchar *db_name) {
    gchar response[10];

    g_print("\n⚠️  Datenbank '%s' existiert bereits!\n", db_name);
    g_print("Möchten Sie sie überschreiben? Alle Daten gehen verloren! (ja/nein): ");

    if (fgets(response, sizeof(response), stdin) == NULL)
        return FALSE;

    // Newline entfernen
    gsize len = strlen(response);
    if (len > 0 && response[len-1] == '\n')
        response[len-1] = '\0';

    return (g_ascii_strcasecmp(response, "ja") == 0 ||
            g_ascii_strcasecmp(response, "j") == 0 ||
            g_ascii_strcasecmp(response, "yes") == 0 ||
            g_ascii_strcasecmp(response, "y") == 0);
}

/**
 * sond_graph_execute_sql:
 * @mysql: Aktive MySQL-Verbindung
 * @sql: SQL-Statement
 * @error: Fehler-Rückgabe
 *
 * Führt ein SQL-Statement aus.
 *
 * Returns: 0 bei Erfolg, -1 bei Fehler
 */
static gint sond_graph_execute_sql(MYSQL *mysql, const gchar *sql, GError **error) {
    if (mysql_query(mysql, sql)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                   "SQL execution failed: %s\nQuery: %s",
                   mysql_error(mysql), sql);
        return -1;
    }
    return 0;
}

/**
 * sond_graph_create:
 * @mysql: Aktive MySQL-Verbindung mit ausreichenden Rechten (CREATE DATABASE)
 * @db_name: Name der zu erstellenden Datenbank (z.B. "graph_db")
 * @prompt_if_exists: TRUE = Benutzer fragen bei existierender DB, FALSE = automatisch überschreiben
 * @error: Fehler-Rückgabe
 *
 * Erstellt eine Graph-Datenbank mit nodes und edges Tabellen.
 * Properties werden als JSON-Arrays im definierten Format gespeichert.
 *
 * Returns: 0 bei Erfolg, -1 bei Fehler
 */
gint sond_graph_create(MYSQL *mysql,
                       const gchar *db_name,
                       gboolean prompt_if_exists,
                       GError **error) {
    g_return_val_if_fail(mysql != NULL, -1);
    g_return_val_if_fail(db_name != NULL, -1);

    // Prüfen ob Datenbank existiert
    if (sond_graph_db_exists(mysql, db_name)) {
        if (prompt_if_exists) {
            if (!sond_graph_prompt_overwrite(db_name)) {
                g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_EXIST,
                           "Operation cancelled by user");
                return -1;
            }
        }

        // Datenbank löschen
        g_print("🗑️  Lösche existierende Datenbank '%s'...\n", db_name);
        gchar *drop_sql = g_strdup_printf("DROP DATABASE IF EXISTS %s", db_name);
        if (sond_graph_execute_sql(mysql, drop_sql, error) != 0) {
            g_free(drop_sql);
            return -1;
        }
        g_free(drop_sql);
    }

    // Datenbank erstellen
    g_print("📦 Erstelle Datenbank '%s'...\n", db_name);
    gchar *create_db_sql = g_strdup_printf(
        "CREATE DATABASE %s CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci",
        db_name
    );
    if (sond_graph_execute_sql(mysql, create_db_sql, error) != 0) {
        g_free(create_db_sql);
        return -1;
    }
    g_free(create_db_sql);

    // Datenbank auswählen
    if (mysql_select_db(mysql, db_name)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                   "Failed to select database: %s", mysql_error(mysql));
        return -1;
    }

    // Nodes Tabelle erstellen
    g_print("📋 Erstelle 'nodes' Tabelle...\n");
    const gchar *create_nodes_sql =
        "CREATE TABLE nodes ("
        "    id INT PRIMARY KEY AUTO_INCREMENT,"
        "    label VARCHAR(255) NOT NULL,"
        "    properties JSON,"
        "    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
        "    CHECK (JSON_VALID(properties)),"
        "    INDEX idx_label (label),"
        "    INDEX idx_created (created_at),"
        "    INDEX idx_updated (updated_at)"
        ") ENGINE=InnoDB";

    if (sond_graph_execute_sql(mysql, create_nodes_sql, error) != 0)
        return -1;

    // Edges Tabelle erstellen
    g_print("🔗 Erstelle 'edges' Tabelle...\n");
    const gchar *create_edges_sql =
        "CREATE TABLE edges ("
        "    id INT PRIMARY KEY AUTO_INCREMENT,"
        "    label VARCHAR(255) NOT NULL,"
        "    from_node INT NOT NULL,"
        "    to_node INT NOT NULL,"
        "    properties JSON,"
        "    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
        "    CHECK (JSON_VALID(properties)),"
        "    FOREIGN KEY (from_node) REFERENCES nodes(id) ON DELETE CASCADE,"
        "    FOREIGN KEY (to_node) REFERENCES nodes(id) ON DELETE CASCADE,"
        "    INDEX idx_from_node (from_node),"
        "    INDEX idx_to_node (to_node),"
        "    INDEX idx_label (label),"
        "    INDEX idx_from_label (from_node, label),"
        "    INDEX idx_to_label (to_node, label),"
        "    INDEX idx_created (created_at),"
        "    INDEX idx_updated (updated_at)"
        ") ENGINE=InnoDB";

    if (sond_graph_execute_sql(mysql, create_edges_sql, error) != 0)
        return -1;

    // Views erstellen
    g_print("👁️  Erstelle Views...\n");
    const gchar *create_view_outgoing =
        "CREATE OR REPLACE VIEW node_outgoing_edges AS "
        "SELECT "
        "    n.id AS node_id,"
        "    n.label AS node_label,"
        "    e.id AS edge_id,"
        "    e.label AS edge_label,"
        "    e.to_node,"
        "    target.label AS target_label,"
        "    e.properties AS edge_properties "
        "FROM nodes n "
        "JOIN edges e ON n.id = e.from_node "
        "JOIN nodes target ON e.to_node = target.id";

    if (sond_graph_execute_sql(mysql, create_view_outgoing, error) != 0)
        return -1;

    const gchar *create_view_incoming =
        "CREATE OR REPLACE VIEW node_incoming_edges AS "
        "SELECT "
        "    n.id AS node_id,"
        "    n.label AS node_label,"
        "    e.id AS edge_id,"
        "    e.label AS edge_label,"
        "    e.from_node,"
        "    source.label AS source_label,"
        "    e.properties AS edge_properties "
        "FROM nodes n "
        "JOIN edges e ON n.id = e.to_node "
        "JOIN nodes source ON e.from_node = source.id";

    if (sond_graph_execute_sql(mysql, create_view_incoming, error) != 0)
        return -1;

    g_print("✅ Datenbank '%s' erfolgreich erstellt!\n", db_name);
    g_print("   - Tabelle 'nodes' mit JSON properties\n");
    g_print("   - Tabelle 'edges' mit JSON properties\n");
    g_print("   - Views für Graph-Traversierung\n");

    return 0;
}

/**
 * sond_graph_insert_node:
 * @mysql: Aktive MySQL-Verbindung (DB muss ausgewählt sein)
 * @label: Label des Nodes (z.B. "Person", "Company")
 * @properties_json: Properties als JSON-String im Property-Format, oder NULL
 * @error: Fehler-Rückgabe
 *
 * Fügt einen neuen Node in die Datenbank ein.
 *
 * Beispiel für properties_json:
 * "[{\"key\":\"name\",\"value\":\"Max\"},{\"key\":\"age\",\"value\":42}]"
 *
 * Returns: ID des eingefügten Nodes (>= 0) bei Erfolg, -1 bei Fehler
 */
gint sond_graph_insert_node(MYSQL *mysql,
                             const gchar *label,
                             const gchar *properties_json,
                             GError **error) {
    g_return_val_if_fail(mysql != NULL, -1);
    g_return_val_if_fail(label != NULL, -1);

    // Escape JSON string für SQL
    gchar *escaped_json = NULL;
    if (properties_json) {
        gsize len = strlen(properties_json);
        escaped_json = g_malloc(len * 2 + 1);
        mysql_real_escape_string(mysql, escaped_json, properties_json, len);
    }

    // SQL erstellen
    gchar *sql;
    if (properties_json && escaped_json) {
        sql = g_strdup_printf(
            "INSERT INTO nodes (label, properties) VALUES ('%s', '%s')",
            label, escaped_json
        );
    } else {
        sql = g_strdup_printf(
            "INSERT INTO nodes (label, properties) VALUES ('%s', '[]')",
            label
        );
    }

    g_free(escaped_json);

    // Query ausführen
    if (mysql_query(mysql, sql)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                   "Failed to insert node: %s", mysql_error(mysql));
        g_free(sql);
        return -1;
    }
    g_free(sql);

    // ID des eingefügten Nodes zurückgeben
    return (gint)mysql_insert_id(mysql);
}

/**
 * sond_graph_insert_edge:
 * @mysql: Aktive MySQL-Verbindung (DB muss ausgewählt sein)
 * @label: Label der Edge (z.B. "KNOWS", "WORKS_AT")
 * @from_node_id: ID des Start-Nodes
 * @to_node_id: ID des Ziel-Nodes
 * @properties_json: Properties als JSON-String im Property-Format, oder NULL
 * @error: Fehler-Rückgabe
 *
 * Fügt eine neue Edge zwischen zwei Nodes ein.
 *
 * Beispiel für properties_json:
 * "[{\"key\":\"since\",\"value\":\"2020-01-15\"},{\"key\":\"type\",\"value\":\"friend\"}]"
 *
 * Returns: ID der eingefügten Edge (>= 0) bei Erfolg, -1 bei Fehler
 */
gint sond_graph_insert_edge(MYSQL *mysql,
                             const gchar *label,
                             gint from_node_id,
                             gint to_node_id,
                             const gchar *properties_json,
                             GError **error) {
    g_return_val_if_fail(mysql != NULL, -1);
    g_return_val_if_fail(label != NULL, -1);
    g_return_val_if_fail(from_node_id > 0, -1);
    g_return_val_if_fail(to_node_id > 0, -1);

    // Escape JSON string für SQL
    gchar *escaped_json = NULL;
    if (properties_json) {
        gsize len = strlen(properties_json);
        escaped_json = g_malloc(len * 2 + 1);
        mysql_real_escape_string(mysql, escaped_json, properties_json, len);
    }

    // SQL erstellen
    gchar *sql;
    if (properties_json && escaped_json) {
        sql = g_strdup_printf(
            "INSERT INTO edges (label, from_node, to_node, properties) "
            "VALUES ('%s', %d, %d, '%s')",
            label, from_node_id, to_node_id, escaped_json
        );
    } else {
        sql = g_strdup_printf(
            "INSERT INTO edges (label, from_node, to_node, properties) "
            "VALUES ('%s', %d, %d, '[]')",
            label, from_node_id, to_node_id
        );
    }

    g_free(escaped_json);

    // Query ausführen
    if (mysql_query(mysql, sql)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                   "Failed to insert edge: %s", mysql_error(mysql));
        g_free(sql);
        return -1;
    }
    g_free(sql);

    // ID der eingefügten Edge zurückgeben
    return (gint)mysql_insert_id(mysql);
}

/**
 * sond_graph_update_node_properties:
 * @mysql: Aktive MySQL-Verbindung
 * @node_id: ID des Nodes
 * @properties_json: Neue Properties als JSON-String
 * @error: Fehler-Rückgabe
 *
 * Aktualisiert die Properties eines Nodes (überschreibt komplett).
 *
 * Returns: 0 bei Erfolg, -1 bei Fehler
 */
gint sond_graph_update_node_properties(MYSQL *mysql,
                                        gint node_id,
                                        const gchar *properties_json,
                                        GError **error) {
    g_return_val_if_fail(mysql != NULL, -1);
    g_return_val_if_fail(node_id > 0, -1);
    g_return_val_if_fail(properties_json != NULL, -1);

    // Escape JSON
    gsize len = strlen(properties_json);
    gchar *escaped_json = g_malloc(len * 2 + 1);
    mysql_real_escape_string(mysql, escaped_json, properties_json, len);

    gchar *sql = g_strdup_printf(
        "UPDATE nodes SET properties = '%s' WHERE id = %d",
        escaped_json, node_id
    );

    g_free(escaped_json);

    gint result = sond_graph_execute_sql(mysql, sql, error);
    g_free(sql);

    return result;
}

/**
 * sond_graph_update_edge_properties:
 * @mysql: Aktive MySQL-Verbindung
 * @edge_id: ID der Edge
 * @properties_json: Neue Properties als JSON-String
 * @error: Fehler-Rückgabe
 *
 * Aktualisiert die Properties einer Edge (überschreibt komplett).
 *
 * Returns: 0 bei Erfolg, -1 bei Fehler
 */
gint sond_graph_update_edge_properties(MYSQL *mysql,
                                        gint edge_id,
                                        const gchar *properties_json,
                                        GError **error) {
    g_return_val_if_fail(mysql != NULL, -1);
    g_return_val_if_fail(edge_id > 0, -1);
    g_return_val_if_fail(properties_json != NULL, -1);

    // Escape JSON
    gsize len = strlen(properties_json);
    gchar *escaped_json = g_malloc(len * 2 + 1);
    mysql_real_escape_string(mysql, escaped_json, properties_json, len);

    gchar *sql = g_strdup_printf(
        "UPDATE edges SET properties = '%s' WHERE id = %d",
        escaped_json, edge_id
    );

    g_free(escaped_json);

    gint result = sond_graph_execute_sql(mysql, sql, error);
    g_free(sql);

    return result;
}

/**
 * sond_graph_delete_node:
 * @mysql: Aktive MySQL-Verbindung
 * @node_id: ID des zu löschenden Nodes
 * @error: Fehler-Rückgabe
 *
 * Löscht einen Node und alle zugehörigen Edges (CASCADE).
 *
 * Returns: 0 bei Erfolg, -1 bei Fehler
 */
gint sond_graph_delete_node(MYSQL *mysql,
                             gint node_id,
                             GError **error) {
    g_return_val_if_fail(mysql != NULL, -1);
    g_return_val_if_fail(node_id > 0, -1);

    gchar *sql = g_strdup_printf("DELETE FROM nodes WHERE id = %d", node_id);
    gint result = sond_graph_execute_sql(mysql, sql, error);
    g_free(sql);

    return result;
}

/**
 * sond_graph_delete_edge:
 * @mysql: Aktive MySQL-Verbindung
 * @edge_id: ID der zu löschenden Edge
 * @error: Fehler-Rückgabe
 *
 * Löscht eine Edge.
 *
 * Returns: 0 bei Erfolg, -1 bei Fehler
 */
gint sond_graph_delete_edge(MYSQL *mysql,
                             gint edge_id,
                             GError **error) {
    g_return_val_if_fail(mysql != NULL, -1);
    g_return_val_if_fail(edge_id > 0, -1);

    gchar *sql = g_strdup_printf("DELETE FROM edges WHERE id = %d", edge_id);
    gint result = sond_graph_execute_sql(mysql, sql, error);
    g_free(sql);

    return result;
}

/**
 * SondProperty:
 * @key: Name der Property
 * @value: Wert als GVariant (kann jeden JSON-Typ enthalten)
 * @properties: (nullable): Hash-Table mit Sub-Properties (key -> GPtrArray of SondProperty*)
 *
 * Struktur für eine Property mit optionalen Sub-Properties.
 * Properties können rekursiv verschachtelt werden.
 */
typedef struct _SondProperty SondProperty;

struct _SondProperty {
    gchar *key;                  // Property-Name
    GVariant *value;             // Wert (string, int, double, bool, null, array, object)
    GHashTable *properties;      // Sub-Properties: key (gchar*) -> GPtrArray of SondProperty*
};

/**
 * sond_property_new:
 * @key: Name der Property
 * @value: (nullable): Wert als GVariant
 *
 * Erstellt eine neue Property.
 *
 * Returns: (transfer full): Neue SondProperty-Instanz
 */
SondProperty* sond_property_new(const gchar *key, GVariant *value) {
    g_return_val_if_fail(key != NULL, NULL);

    SondProperty *prop = g_new0(SondProperty, 1);
    prop->key = g_strdup(key);
    prop->value = value ? g_variant_ref(value) : NULL;
    prop->properties = g_hash_table_new_full(
        g_str_hash,
        g_str_equal,
        g_free,
        (GDestroyNotify)g_ptr_array_unref
    );

    return prop;
}

/**
 * sond_property_free:
 * @prop: Property zum Freigeben
 *
 * Gibt eine Property und alle Sub-Properties frei.
 */
void sond_property_free(SondProperty *prop) {
    if (!prop)
        return;

    g_free(prop->key);
    g_clear_pointer(&prop->value, g_variant_unref);
    g_clear_pointer(&prop->properties, g_hash_table_unref);
    g_free(prop);
}

/**
 * sond_property_add_sub:
 * @prop: Parent-Property
 * @key: Key der Sub-Property
 * @sub: Sub-Property zum Hinzufügen
 *
 * Fügt eine Sub-Property hinzu. Mehrere Properties mit gleichem Key sind möglich.
 */
void sond_property_add_sub(SondProperty *prop, const gchar *key, SondProperty *sub) {
    g_return_if_fail(prop != NULL);
    g_return_if_fail(key != NULL);
    g_return_if_fail(sub != NULL);

    GPtrArray *arr = g_hash_table_lookup(prop->properties, key);
    if (!arr) {
        arr = g_ptr_array_new_with_free_func((GDestroyNotify)sond_property_free);
        g_hash_table_insert(prop->properties, g_strdup(key), arr);
    }
    g_ptr_array_add(arr, sub);
}

/**
 * sond_property_get_subs:
 * @prop: Property
 * @key: Key der gesuchten Sub-Properties
 *
 * Holt alle Sub-Properties mit dem gegebenen Key.
 *
 * Returns: (nullable) (transfer none): Array von Sub-Properties oder NULL
 */
GPtrArray* sond_property_get_subs(SondProperty *prop, const gchar *key) {
    g_return_val_if_fail(prop != NULL, NULL);
    g_return_val_if_fail(key != NULL, NULL);

    return g_hash_table_lookup(prop->properties, key);
}

/**
 * sond_property_get_first_sub:
 * @prop: Property
 * @key: Key der gesuchten Sub-Property
 *
 * Holt die erste Sub-Property mit dem gegebenen Key.
 *
 * Returns: (nullable) (transfer none): Erste Sub-Property oder NULL
 */
SondProperty* sond_property_get_first_sub(SondProperty *prop, const gchar *key) {
    GPtrArray *arr = sond_property_get_subs(prop, key);
    if (!arr || arr->len == 0)
        return NULL;
    return g_ptr_array_index(arr, 0);
}

/**
 * sond_property_has_subs:
 * @prop: Property
 *
 * Prüft, ob die Property Sub-Properties hat.
 *
 * Returns: TRUE wenn Sub-Properties existieren
 */
gboolean sond_property_has_subs(SondProperty *prop) {
    g_return_val_if_fail(prop != NULL, FALSE);
    return g_hash_table_size(prop->properties) > 0;
}

/**
 * sond_property_from_json_object:
 * @key: Key der Property
 * @obj: JSON-Objekt mit {key, value, properties}
 *
 * Erstellt eine SondProperty aus einem JSON-Objekt (rekursiv).
 *
 * Returns: (transfer full): Neue SondProperty oder NULL bei Fehler
 */
static SondProperty* sond_property_from_json_object(const gchar *key, JsonObject *obj) {
    // Value auslesen und zu GVariant konvertieren
    JsonNode *value_node = json_object_get_member(obj, "value");
    GVariant *value = NULL;

    if (value_node && !json_node_is_null(value_node)) {
        switch (json_node_get_value_type(value_node)) {
            case G_TYPE_STRING:
                value = g_variant_new_string(json_node_get_string(value_node));
                break;
            case G_TYPE_INT64:
                value = g_variant_new_int32((gint32)json_node_get_int(value_node));
                break;
            case G_TYPE_DOUBLE:
                value = g_variant_new_double(json_node_get_double(value_node));
                break;
            case G_TYPE_BOOLEAN:
                value = g_variant_new_boolean(json_node_get_boolean(value_node));
                break;
            default:
                // Für Arrays und komplexe Typen
                if (json_node_get_node_type(value_node) == JSON_NODE_ARRAY) {
                    JsonArray *arr = json_node_get_array(value_node);
                    guint len = json_array_get_length(arr);

                    // Prüfe ob es ein Array von Doubles ist
                    gboolean all_doubles = TRUE;
                    for (guint i = 0; i < len; i++) {
                        if (json_array_get_double_element(arr, i) == 0.0 &&
                            !JSON_NODE_HOLDS_VALUE(json_array_get_element(arr, i))) {
                            all_doubles = FALSE;
                            break;
                        }
                    }

                    if (all_doubles && len > 0) {
                        // Double-Array erstellen
                        GVariant **values = g_new(GVariant*, len);
                        for (guint i = 0; i < len; i++) {
                            values[i] = g_variant_new_double(json_array_get_double_element(arr, i));
                        }
                        value = g_variant_new_array(G_VARIANT_TYPE_DOUBLE, values, len);
                        g_free(values);
                    } else {
                        // Als JSON-String speichern
                        JsonGenerator *gen = json_generator_new();
                        json_generator_set_root(gen, value_node);
                        gchar *json_str = json_generator_to_data(gen, NULL);
                        value = g_variant_new_string(json_str);
                        g_free(json_str);
                        g_object_unref(gen);
                    }
                } else {
                    // Andere komplexe Typen als JSON-String
                    JsonGenerator *gen = json_generator_new();
                    json_generator_set_root(gen, value_node);
                    gchar *json_str = json_generator_to_data(gen, NULL);
                    value = g_variant_new_string(json_str);
                    g_free(json_str);
                    g_object_unref(gen);
                }
        }
    }

    SondProperty *prop = sond_property_new(key, value);
    if (value)
        g_variant_unref(value);

    // Sub-properties rekursiv verarbeiten
    if (json_object_has_member(obj, "properties")) {
        JsonArray *sub_props_array = json_object_get_array_member(obj, "properties");
        guint len = json_array_get_length(sub_props_array);

        for (guint i = 0; i < len; i++) {
            JsonObject *sub_obj = json_array_get_object_element(sub_props_array, i);
            const gchar *sub_key = json_object_get_string_member(sub_obj, "key");

            if (sub_key) {
                SondProperty *sub_prop = sond_property_from_json_object(sub_key, sub_obj);
                if (sub_prop) {
                    sond_property_add_sub(prop, sub_key, sub_prop);
                }
            }
        }
    }

    return prop;
}

/**
 * sond_graph_load_node_properties:
 * @mysql: Aktive MySQL-Verbindung
 * @node_id: ID des Nodes
 * @error: (nullable): Rückgabe für Fehlerinformationen
 *
 * Lädt alle Properties eines Nodes aus der Datenbank.
 *
 * Returns: (transfer full) (element-type utf8 GPtrArray): Hash-Table mit key -> GPtrArray of SondProperty*
 */
static GHashTable* sond_graph_parse_properties_json(const gchar *properties_json,
                                                      GError **error) {
    if (!properties_json || strlen(properties_json) == 0) {
        // Leere Hash-Table zurückgeben
        return g_hash_table_new_full(
            g_str_hash,
            g_str_equal,
            g_free,
            (GDestroyNotify)g_ptr_array_unref
        );
    }

    JsonParser *parser = json_parser_new();

    if (!json_parser_load_from_data(parser, properties_json, -1, error)) {
        g_object_unref(parser);
        return NULL;
    }

    GHashTable *properties = g_hash_table_new_full(
        g_str_hash,
        g_str_equal,
        g_free,
        (GDestroyNotify)g_ptr_array_unref
    );

    JsonNode *root = json_parser_get_root(parser);
    if (json_node_get_node_type(root) == JSON_NODE_ARRAY) {
        JsonArray *array = json_node_get_array(root);
        guint len = json_array_get_length(array);

        for (guint i = 0; i < len; i++) {
            JsonObject *obj = json_array_get_object_element(array, i);
            const gchar *key = json_object_get_string_member(obj, "key");

            if (key) {
                SondProperty *prop = sond_property_from_json_object(key, obj);
                if (prop) {
                    GPtrArray *arr = g_hash_table_lookup(properties, key);
                    if (!arr) {
                        arr = g_ptr_array_new_with_free_func((GDestroyNotify)sond_property_free);
                        g_hash_table_insert(properties, g_strdup(key), arr);
                    }
                    g_ptr_array_add(arr, prop);
                }
            }
        }
    }

    g_object_unref(parser);
    return properties;
}

GHashTable* sond_graph_load_node_properties(MYSQL *mysql,
                                              gint node_id,
                                              GError **error) {
    GHashTable *all_properties = NULL;

    g_return_val_if_fail(mysql != NULL, NULL);
    g_return_val_if_fail(node_id > 0, NULL);

    gchar *query = g_strdup_printf(
        "SELECT properties FROM nodes WHERE id = %d",
        node_id
    );

    if (mysql_query(mysql, query)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                   "MySQL query failed: %s", mysql_error(mysql));
        g_free(query);
        return NULL;
    }
    g_free(query);

    MYSQL_RES *result = mysql_store_result(mysql);
    if (!result) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                   "Failed to get result: %s", mysql_error(mysql));
        return NULL;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
	all_properties = sond_graph_parse_properties_json(row ? row[0] : NULL, error);
	if (!all_properties)
		ERROR_Z

    mysql_free_result(result);
    return all_properties;
}

/**
 * sond_graph_load_edge_properties:
 * @mysql: Aktive MySQL-Verbindung
 * @edge_id: ID der Edge
 * @error: (nullable): Rückgabe für Fehlerinformationen
 *
 * Lädt alle Properties einer Edge aus der Datenbank.
 *
 * Returns: (transfer full) (element-type utf8 GPtrArray): Hash-Table mit key -> GPtrArray of SondProperty*
 */
GHashTable* sond_graph_load_edge_properties(MYSQL *mysql,
                                              gint edge_id,
                                              GError **error) {
    GHashTable *all_properties = NULL;

    g_return_val_if_fail(mysql != NULL, NULL);
    g_return_val_if_fail(edge_id > 0, NULL);

    gchar *query = g_strdup_printf(
        "SELECT properties FROM edges WHERE id = %d",
        edge_id
    );

    if (mysql_query(mysql, query)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                   "MySQL query failed: %s", mysql_error(mysql));
        g_free(query);
        return NULL;
    }
    g_free(query);

    MYSQL_RES *result = mysql_store_result(mysql);
    if (!result) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                   "Failed to get result: %s", mysql_error(mysql));
        return NULL;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
	all_properties = sond_graph_parse_properties_json(row ? row[0] : NULL, error);
	if (!all_properties)
		ERROR_Z

    mysql_free_result(result);
    return all_properties;
}

/**
 * sond_graph_get_node_property:
 * @mysql: Aktive MySQL-Verbindung
 * @node_id: ID des Nodes
 * @key: Key der gesuchten Property
 * @error: (nullable): Rückgabe für Fehlerinformationen
 *
 * Lädt eine spezifische Property eines Nodes (alle Vorkommen).
 *
 * Returns: (transfer full) (nullable): Array von Properties oder NULL
 */
GPtrArray* sond_graph_get_node_property(MYSQL *mysql,
                                         gint node_id,
                                         const gchar *key,
                                         GError **error) {
    g_return_val_if_fail(key != NULL, NULL);

    GHashTable *all = sond_graph_load_node_properties(mysql, node_id, error);
    if (!all)
        return NULL;

    GPtrArray *props = g_hash_table_lookup(all, key);
    if (props) {
        // Array referenzieren, damit es nach destroy erhalten bleibt
        g_ptr_array_ref(props);
    }

    g_hash_table_unref(all);
    return props;
}

/**
 * SondNode:
 * @id: Eindeutige ID des Nodes in der Datenbank
 * @label: Label/Typ des Nodes (z.B. "Person", "Company")
 * @properties: Hash-Table mit Properties (key -> GPtrArray of SondProperty*)
 * @created_at: Zeitstempel der Erstellung (optional)
 * @updated_at: Zeitstempel der letzten Änderung (optional)
 *
 * Repräsentiert einen Node in der Graph-Datenbank.
 */
typedef struct _SondNode SondNode;

struct _SondNode {
    gint id;
    gchar *label;
    GHashTable *properties;     // key (gchar*) -> GPtrArray of SondProperty*
    GDateTime *created_at;
    GDateTime *updated_at;
};

/**
 * SondEdge:
 * @id: Eindeutige ID der Edge in der Datenbank
 * @label: Label/Typ der Edge (z.B. "KNOWS", "WORKS_AT")
 * @from_node_id: ID des Start-Nodes
 * @to_node_id: ID des Ziel-Nodes
 * @properties: Hash-Table mit Properties (key -> GPtrArray of SondProperty*)
 * @created_at: Zeitstempel der Erstellung (optional)
 * @updated_at: Zeitstempel der letzten Änderung (optional)
 *
 * Repräsentiert eine Edge in der Graph-Datenbank.
 */
typedef struct _SondEdge SondEdge;

struct _SondEdge {
    gint id;
    gchar *label;
    gint from_node_id;
    gint to_node_id;
    GHashTable *properties;     // key (gchar*) -> GPtrArray of SondProperty*
    GDateTime *created_at;
    GDateTime *updated_at;
};

/**
 * sond_node_new:
 * @id: Node-ID (0 wenn noch nicht in DB)
 * @label: Label des Nodes
 *
 * Erstellt einen neuen Node.
 *
 * Returns: (transfer full): Neuer SondNode
 */
SondNode* sond_node_new(gint id, const gchar *label) {
    g_return_val_if_fail(label != NULL, NULL);

    SondNode *node = g_new0(SondNode, 1);
    node->id = id;
    node->label = g_strdup(label);
    node->properties = g_hash_table_new_full(
        g_str_hash,
        g_str_equal,
        g_free,
        (GDestroyNotify)g_ptr_array_unref
    );
    node->created_at = NULL;
    node->updated_at = NULL;

    return node;
}

/**
 * sond_node_free:
 * @node: Node zum Freigeben
 *
 * Gibt einen Node und alle seine Properties frei.
 */
void sond_node_free(SondNode *node) {
    if (!node)
        return;

    g_free(node->label);
    g_clear_pointer(&node->properties, g_hash_table_unref);
    g_clear_pointer(&node->created_at, g_date_time_unref);
    g_clear_pointer(&node->updated_at, g_date_time_unref);
    g_free(node);
}

/**
 * sond_node_add_property:
 * @node: Node
 * @key: Property-Key
 * @prop: Property zum Hinzufügen
 *
 * Fügt eine Property zum Node hinzu.
 */
void sond_node_add_property(SondNode *node, const gchar *key, SondProperty *prop) {
    g_return_if_fail(node != NULL);
    g_return_if_fail(key != NULL);
    g_return_if_fail(prop != NULL);

    GPtrArray *arr = g_hash_table_lookup(node->properties, key);
    if (!arr) {
        arr = g_ptr_array_new_with_free_func((GDestroyNotify)sond_property_free);
        g_hash_table_insert(node->properties, g_strdup(key), arr);
    }
    g_ptr_array_add(arr, prop);
}

/**
 * sond_node_get_properties:
 * @node: Node
 * @key: Property-Key
 *
 * Holt alle Properties mit dem gegebenen Key.
 *
 * Returns: (nullable) (transfer none): Array von Properties oder NULL
 */
GPtrArray* sond_node_get_properties(SondNode *node, const gchar *key) {
    g_return_val_if_fail(node != NULL, NULL);
    g_return_val_if_fail(key != NULL, NULL);

    return g_hash_table_lookup(node->properties, key);
}

/**
 * sond_node_get_first_property:
 * @node: Node
 * @key: Property-Key
 *
 * Holt die erste Property mit dem gegebenen Key.
 *
 * Returns: (nullable) (transfer none): Property oder NULL
 */
SondProperty* sond_node_get_first_property(SondNode *node, const gchar *key) {
    GPtrArray *arr = sond_node_get_properties(node, key);
    if (!arr || arr->len == 0)
        return NULL;
    return g_ptr_array_index(arr, 0);
}

/**
 * sond_edge_new:
 * @id: Edge-ID (0 wenn noch nicht in DB)
 * @label: Label der Edge
 * @from_node_id: ID des Start-Nodes
 * @to_node_id: ID des Ziel-Nodes
 *
 * Erstellt eine neue Edge.
 *
 * Returns: (transfer full): Neue SondEdge
 */
SondEdge* sond_edge_new(gint id, const gchar *label, gint from_node_id, gint to_node_id) {
    g_return_val_if_fail(label != NULL, NULL);
    g_return_val_if_fail(from_node_id > 0, NULL);
    g_return_val_if_fail(to_node_id > 0, NULL);

    SondEdge *edge = g_new0(SondEdge, 1);
    edge->id = id;
    edge->label = g_strdup(label);
    edge->from_node_id = from_node_id;
    edge->to_node_id = to_node_id;
    edge->properties = g_hash_table_new_full(
        g_str_hash,
        g_str_equal,
        g_free,
        (GDestroyNotify)g_ptr_array_unref
    );
    edge->created_at = NULL;
    edge->updated_at = NULL;

    return edge;
}

/**
 * sond_edge_free:
 * @edge: Edge zum Freigeben
 *
 * Gibt eine Edge und alle ihre Properties frei.
 */
void sond_edge_free(SondEdge *edge) {
    if (!edge)
        return;

    g_free(edge->label);
    g_clear_pointer(&edge->properties, g_hash_table_unref);
    g_clear_pointer(&edge->created_at, g_date_time_unref);
    g_clear_pointer(&edge->updated_at, g_date_time_unref);
    g_free(edge);
}

/**
 * sond_edge_add_property:
 * @edge: Edge
 * @key: Property-Key
 * @prop: Property zum Hinzufügen
 *
 * Fügt eine Property zur Edge hinzu.
 */
void sond_edge_add_property(SondEdge *edge, const gchar *key, SondProperty *prop) {
    g_return_if_fail(edge != NULL);
    g_return_if_fail(key != NULL);
    g_return_if_fail(prop != NULL);

    GPtrArray *arr = g_hash_table_lookup(edge->properties, key);
    if (!arr) {
        arr = g_ptr_array_new_with_free_func((GDestroyNotify)sond_property_free);
        g_hash_table_insert(edge->properties, g_strdup(key), arr);
    }
    g_ptr_array_add(arr, prop);
}

/**
 * sond_edge_get_properties:
 * @edge: Edge
 * @key: Property-Key
 *
 * Holt alle Properties mit dem gegebenen Key.
 *
 * Returns: (nullable) (transfer none): Array von Properties oder NULL
 */
GPtrArray* sond_edge_get_properties(SondEdge *edge, const gchar *key) {
    g_return_val_if_fail(edge != NULL, NULL);
    g_return_val_if_fail(key != NULL, NULL);

    return g_hash_table_lookup(edge->properties, key);
}

/**
 * sond_edge_get_first_property:
 * @edge: Edge
 * @key: Property-Key
 *
 * Holt die erste Property mit dem gegebenen Key.
 *
 * Returns: (nullable) (transfer none): Property oder NULL
 */
SondProperty* sond_edge_get_first_property(SondEdge *edge, const gchar *key) {
    GPtrArray *arr = sond_edge_get_properties(edge, key);
    if (!arr || arr->len == 0)
        return NULL;
    return g_ptr_array_index(arr, 0);
}

/**
 * sond_graph_parse_properties_json:
 * @properties_json: JSON-String mit Properties im Property-Format
 * @error: (nullable): Rückgabe für Fehlerinformationen
 *
 * Parst einen JSON-String und erstellt eine Hash-Table mit Properties.
 *
 * Returns: (transfer full) (nullable): Hash-Table (key -> GPtrArray of SondProperty*) oder NULL bei Fehler
 */
/**
 * sond_graph_load_node:
 * @mysql: Aktive MySQL-Verbindung
 * @node_id: ID des zu ladenden Nodes
 * @error: (nullable): Rückgabe für Fehlerinformationen
 *
 * Lädt einen Node mit allen Properties aus der Datenbank.
 *
 * Returns: (transfer full) (nullable): SondNode oder NULL bei Fehler
 */
SondNode* sond_graph_load_node(MYSQL *mysql,
                                gint node_id,
                                GError **error) {
    g_return_val_if_fail(mysql != NULL, NULL);
    g_return_val_if_fail(node_id > 0, NULL);

    gchar *query = g_strdup_printf(
        "SELECT id, label, properties, created_at, updated_at FROM nodes WHERE id = %d",
        node_id
    );

    if (mysql_query(mysql, query)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                   "MySQL query failed: %s", mysql_error(mysql));
        g_free(query);
        return NULL;
    }
    g_free(query);

    MYSQL_RES *result = mysql_store_result(mysql);
    if (!result) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                   "Failed to get result: %s", mysql_error(mysql));
        return NULL;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    if (!row) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                   "Node with ID %d not found", node_id);
        mysql_free_result(result);
        return NULL;
    }

    // Node erstellen
    gint id = atoi(row[0]);
    const gchar *label = row[1];
    const gchar *properties_json = row[2];

    SondNode *node = sond_node_new(id, label);

    // Timestamps parsen (optional)
    if (row[3]) {
        node->created_at = g_date_time_new_from_iso8601(row[3], NULL);
    }
    if (row[4]) {
        node->updated_at = g_date_time_new_from_iso8601(row[4], NULL);
    }

    // Properties parsen
    GHashTable *properties = sond_graph_parse_properties_json(properties_json, error);
    if (!properties) {
        mysql_free_result(result);
        sond_node_free(node);
        return NULL;
    }

    // Properties übernehmen
    g_hash_table_unref(node->properties);
    node->properties = properties;

    mysql_free_result(result);
    return node;
}

/**
 * sond_graph_load_edge:
 * @mysql: Aktive MySQL-Verbindung
 * @edge_id: ID der zu ladenden Edge
 * @error: (nullable): Rückgabe für Fehlerinformationen
 *
 * Lädt eine Edge mit allen Properties aus der Datenbank.
 *
 * Returns: (transfer full) (nullable): SondEdge oder NULL bei Fehler
 */
SondEdge* sond_graph_load_edge(MYSQL *mysql,
                                gint edge_id,
                                GError **error) {
    g_return_val_if_fail(mysql != NULL, NULL);
    g_return_val_if_fail(edge_id > 0, NULL);

    gchar *query = g_strdup_printf(
        "SELECT id, label, from_node, to_node, properties, created_at, updated_at "
        "FROM edges WHERE id = %d",
        edge_id
    );

    if (mysql_query(mysql, query)) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                   "MySQL query failed: %s", mysql_error(mysql));
        g_free(query);
        return NULL;
    }
    g_free(query);

    MYSQL_RES *result = mysql_store_result(mysql);
    if (!result) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                   "Failed to get result: %s", mysql_error(mysql));
        return NULL;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    if (!row) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                   "Edge with ID %d not found", edge_id);
        mysql_free_result(result);
        return NULL;
    }

    // Edge erstellen
    gint id = atoi(row[0]);
    const gchar *label = row[1];
    gint from_node_id = atoi(row[2]);
    gint to_node_id = atoi(row[3]);
    const gchar *properties_json = row[4];

    SondEdge *edge = sond_edge_new(id, label, from_node_id, to_node_id);

    // Timestamps parsen (optional)
    if (row[5]) {
        edge->created_at = g_date_time_new_from_iso8601(row[5], NULL);
    }
    if (row[6]) {
        edge->updated_at = g_date_time_new_from_iso8601(row[6], NULL);
    }

    // Properties parsen
    GHashTable *properties = sond_graph_parse_properties_json(properties_json, error);
    if (!properties) {
        mysql_free_result(result);
        sond_edge_free(edge);
        return NULL;
    }

    // Properties übernehmen
    g_hash_table_unref(edge->properties);
    edge->properties = properties;

    mysql_free_result(result);
    return edge;
}
