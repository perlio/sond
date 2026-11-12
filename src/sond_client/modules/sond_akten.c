/*
 sond (sond_akten.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_akten.h"

#include <string.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>

#include "../../sond_akte_name.h"
#include "../../sond_file_helper.h"

void sond_akte_eintrag_free(SondAkteEintrag *eintrag) {
    if (!eintrag)
        return;

    g_free(eintrag->library_name);
    g_free(eintrag->library_dir);
    g_free(eintrag->bezeichnung);
    g_free(eintrag);
}

/* ========================================================================
 * Merkliste: JSON-Objekt { "<Library>": "<Bezeichnung>", ... }
 * ======================================================================== */

gchar* sond_akten_merkliste_pfad(void) {
    return g_build_filename(g_get_user_config_dir(), "sond", "akten.json", NULL);
}

/* Library-Name -> Bezeichnung (beide g_free()); leer, wenn es die Datei nicht gibt */
static GHashTable* merkliste_laden(const gchar *pfad) {
    GHashTable *map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
            g_free);
    JsonParser *parser = NULL;

    if (!pfad)
        return map;

    parser = json_parser_new();
    if (json_parser_load_from_file(parser, pfad, NULL)) {
        JsonNode *root = json_parser_get_root(parser);

        if (root && JSON_NODE_HOLDS_OBJECT(root)) {
            JsonObject *obj = json_node_get_object(root);
            GList *members = json_object_get_members(obj);

            for (GList *l = members; l; l = l->next) {
                JsonNode *value = json_object_get_member(obj, l->data);

                if (value && JSON_NODE_HOLDS_VALUE(value) &&
                        json_node_get_value_type(value) == G_TYPE_STRING)
                    g_hash_table_replace(map, g_strdup(l->data),
                            g_strdup(json_node_get_string(value)));
            }
            g_list_free(members);
        }
    }
    g_object_unref(parser);

    return map;
}

static gboolean merkliste_speichern(const gchar *pfad, GHashTable *map) {
    JsonBuilder *builder = json_builder_new();
    JsonGenerator *gen = json_generator_new();
    JsonNode *root = NULL;
    GHashTableIter iter;
    gpointer key = NULL;
    gpointer value = NULL;
    gchar *dir = g_path_get_dirname(pfad);
    gchar *tmp = g_strconcat(pfad, ".tmp", NULL);
    gboolean ok = FALSE;

    json_builder_begin_object(builder);
    g_hash_table_iter_init(&iter, map);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        json_builder_set_member_name(builder, key);
        json_builder_add_string_value(builder, value);
    }
    json_builder_end_object(builder);

    root = json_builder_get_root(builder);
    json_generator_set_root(gen, root);
    json_generator_set_pretty(gen, TRUE);

    //über eine Hilfsdatei, damit ein Abbruch die bisherige Merkliste nicht zerstört
    if (g_mkdir_with_parents(dir, 0700) == 0 &&
            json_generator_to_file(gen, tmp, NULL)) {
        if (g_file_test(pfad, G_FILE_TEST_EXISTS))
            g_remove(pfad);
        ok = (g_rename(tmp, pfad) == 0);
    }

    json_node_unref(root);
    g_object_unref(gen);
    g_object_unref(builder);
    g_free(dir);
    g_free(tmp);

    return ok;
}

void sond_akten_merken(const gchar *merkliste, const gchar *library_name,
        const gchar *bezeichnung) {
    GHashTable *map = NULL;
    const gchar *alt = NULL;

    if (!merkliste || !library_name || !bezeichnung || !*bezeichnung)
        return;

    map = merkliste_laden(merkliste);
    alt = g_hash_table_lookup(map, library_name);
    if (g_strcmp0(alt, bezeichnung) != 0) {
        g_hash_table_replace(map, g_strdup(library_name), g_strdup(bezeichnung));
        merkliste_speichern(merkliste, map);
    }
    g_hash_table_unref(map);
}

/* ========================================================================
 * Lesen aus dem SeaDrive
 * ======================================================================== */

gchar* sond_akten_lese_bezeichnung(const gchar *library_dir) {
    gchar *dir_path = NULL;
    SondDir *dir = NULL;
    const gchar *name = NULL;
    gchar *first = NULL;

    g_return_val_if_fail(library_dir != NULL, NULL);

    dir_path = g_build_filename(library_dir, ".sond", "akte", NULL);
    dir = sond_dir_open(dir_path, NULL);
    g_free(dir_path);
    if (!dir)
        return NULL;

    while ((name = sond_dir_read_name(dir))) {
        if (!strcmp(name, ".") || !strcmp(name, ".."))
            continue;
        if (!first || g_strcmp0(name, first) < 0) {
            g_free(first);
            first = g_strdup(name);
        }
    }
    sond_dir_close(dir);

    if (first) {
        gchar *bezeichnung = sond_akte_name_unescape(first);

        g_free(first);

        return bezeichnung;
    }

    return NULL;
}

/* "2026-23" -> Jahr 2026, Nummer 23 */
static gboolean parse_library_name(const gchar *name, guint *year, guint *lfd) {
    const gchar *p = name;
    guint64 y = 0;
    guint64 n = 0;

    for (gint i = 0; i < 4; i++, p++) {
        if (!g_ascii_isdigit(*p))
            return FALSE;
        y = y * 10 + (guint64) (*p - '0');
    }
    if (*p++ != '-' || !g_ascii_isdigit(*p))
        return FALSE;
    for (; *p; p++) {
        if (!g_ascii_isdigit(*p) || n > 100000000)
            return FALSE;
        n = n * 10 + (guint64) (*p - '0');
    }

    *year = (guint) y;
    *lfd = (guint) n;

    return TRUE;
}

static gint eintrag_vergleich(gconstpointer a, gconstpointer b) {
    const SondAkteEintrag *ea = *(SondAkteEintrag* const*) a;
    const SondAkteEintrag *eb = *(SondAkteEintrag* const*) b;

    if (ea->year != eb->year)
        return ea->year < eb->year ? 1 : -1;
    if (ea->lfd_nr != eb->lfd_nr)
        return ea->lfd_nr < eb->lfd_nr ? 1 : -1;

    return 0;
}

GPtrArray* sond_akten_scan(const gchar *seadrive_root, const gchar *merkliste,
        GError **error) {
    SondDir *root_dir = NULL;
    const gchar *cat = NULL;
    GPtrArray *result = NULL;
    GHashTable *gesehen = NULL;
    GHashTable *gemerkt = NULL;
    gboolean geaendert = FALSE;

    g_return_val_if_fail(seadrive_root != NULL, NULL);

    root_dir = sond_dir_open(seadrive_root, error);
    if (!root_dir)
        return NULL;

    result = g_ptr_array_new_with_free_func(
            (GDestroyNotify) sond_akte_eintrag_free);
    gesehen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    gemerkt = merkliste_laden(merkliste);

    //Kategorien ("Meine Bibliotheken", "Für meine Gruppen", ...): lokalisiert,
    //deshalb alle durchsehen; es werden nur Verzeichnisnamen gelesen
    while ((cat = sond_dir_read_name(root_dir))) {
        gchar *cat_path = NULL;
        SondDir *cat_dir = NULL;
        const gchar *lib = NULL;

        if (!strcmp(cat, ".") || !strcmp(cat, ".."))
            continue;

        cat_path = g_build_filename(seadrive_root, cat, NULL);
        cat_dir = g_file_test(cat_path, G_FILE_TEST_IS_DIR) ?
                sond_dir_open(cat_path, NULL) : NULL;

        while (cat_dir && (lib = sond_dir_read_name(cat_dir))) {
            SondAkteEintrag *eintrag = NULL;
            guint year = 0;
            guint lfd = 0;
            gchar *lib_dir = NULL;

            if (!parse_library_name(lib, &year, &lfd) ||
                    g_hash_table_contains(gesehen, lib))
                continue;

            lib_dir = g_build_filename(cat_path, lib, NULL);
            if (!g_file_test(lib_dir, G_FILE_TEST_IS_DIR)) {
                g_free(lib_dir);
                continue;
            }

            eintrag = g_new0(SondAkteEintrag, 1);
            eintrag->library_name = g_strdup(lib);
            eintrag->year = year;
            eintrag->lfd_nr = lfd;
            eintrag->library_dir = lib_dir;
            eintrag->bezeichnung = sond_akten_lese_bezeichnung(lib_dir);

            if (eintrag->bezeichnung) {
                if (g_strcmp0(g_hash_table_lookup(gemerkt, lib),
                        eintrag->bezeichnung) != 0) {
                    g_hash_table_replace(gemerkt, g_strdup(lib),
                            g_strdup(eintrag->bezeichnung));
                    geaendert = TRUE;
                }
            } else {
                const gchar *alt = g_hash_table_lookup(gemerkt, lib);

                if (alt) {
                    eintrag->bezeichnung = g_strdup(alt);
                    eintrag->bezeichnung_gemerkt = TRUE;
                }
            }

            g_hash_table_add(gesehen, g_strdup(lib));
            g_ptr_array_add(result, eintrag);
        }

        if (cat_dir)
            sond_dir_close(cat_dir);
        g_free(cat_path);
    }
    sond_dir_close(root_dir);

    if (geaendert && merkliste)
        merkliste_speichern(merkliste, gemerkt);

    g_ptr_array_sort(result, eintrag_vergleich);
    g_hash_table_unref(gesehen);
    g_hash_table_unref(gemerkt);

    return result;
}

/* ========================================================================
 * Suche
 * ======================================================================== */

/* "23", "23/26", "23/2026", "2026-23": erkannte RegNr; year 0 = jedes Jahr */
static gboolean parse_regnr_suche(const gchar *text, guint *lfd, guint *year) {
    gchar **teile = NULL;
    gboolean ok = FALSE;
    gboolean alle_ziffern = TRUE;

    for (const gchar *p = text; *p; p++)
        if (!g_ascii_isdigit(*p) && *p != '/' && *p != '-')
            return FALSE;
    if (!*text)
        return FALSE;

    for (const gchar *p = text; *p; p++)
        if (!g_ascii_isdigit(*p))
            alle_ziffern = FALSE;

    if (alle_ziffern) {
        *lfd = (guint) g_ascii_strtoull(text, NULL, 10);
        *year = 0;

        return *lfd > 0;
    }

    if (strchr(text, '/')) {
        teile = g_strsplit(text, "/", 2);
        if (teile[0] && teile[1] && *teile[0] && *teile[1] &&
                !strchr(teile[1], '/') && !strchr(teile[0], '-') &&
                !strchr(teile[1], '-')) {
            guint y = (guint) g_ascii_strtoull(teile[1], NULL, 10);

            *lfd = (guint) g_ascii_strtoull(teile[0], NULL, 10);
            if (strlen(teile[1]) == 2)
                y += 2000;
            if (strlen(teile[1]) == 2 || strlen(teile[1]) == 4) {
                *year = y;
                ok = TRUE;
            }
        }
    } else if (strchr(text, '-')) {
        teile = g_strsplit(text, "-", 2);
        if (teile[0] && teile[1] && strlen(teile[0]) == 4 && *teile[1] &&
                !strchr(teile[1], '-')) {
            *year = (guint) g_ascii_strtoull(teile[0], NULL, 10);
            *lfd = (guint) g_ascii_strtoull(teile[1], NULL, 10);
            ok = TRUE;
        }
    }
    g_strfreev(teile);

    return ok && *lfd > 0;
}

GPtrArray* sond_akten_suchen(GPtrArray *alle, const gchar *text) {
    GPtrArray *treffer = g_ptr_array_new();
    gchar *such = NULL;
    gchar **woerter = NULL;
    guint lfd = 0;
    guint year = 0;
    gboolean ist_regnr = FALSE;

    g_return_val_if_fail(alle != NULL, treffer);

    such = g_strstrip(g_strdup(text ? text : ""));
    ist_regnr = parse_regnr_suche(such, &lfd, &year);

    if (!ist_regnr && *such) {
        gchar *fold = g_utf8_casefold(such, -1);

        woerter = g_strsplit_set(fold, " \t", -1);
        g_free(fold);
    }

    for (guint i = 0; i < alle->len; i++) {
        SondAkteEintrag *e = g_ptr_array_index(alle, i);
        gboolean passt = TRUE;

        if (ist_regnr)
            passt = (e->lfd_nr == lfd) && (year == 0 || e->year == year);
        else if (woerter) {
            gchar *hay_raw = g_strconcat(e->library_name, " ",
                    e->bezeichnung ? e->bezeichnung : "", NULL);
            gchar *hay = g_utf8_casefold(hay_raw, -1);

            for (gchar **w = woerter; *w; w++)
                if (**w && !strstr(hay, *w)) {
                    passt = FALSE;
                    break;
                }
            g_free(hay);
            g_free(hay_raw);
        }

        if (passt)
            g_ptr_array_add(treffer, e);
    }

    g_strfreev(woerter);
    g_free(such);

    return treffer;
}
