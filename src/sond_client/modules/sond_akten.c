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
    g_free(eintrag->gegenstand);
    g_free(eintrag);
}

/* ========================================================================
 * Merkliste: JSON-Objekt { "<Library>": { "bezeichnung": "...",
 * "gegenstand": "..." }, ... }; ein bloßer String als Wert (ältere Fassung)
 * gilt als Bezeichnung.
 * ======================================================================== */

typedef struct {
    gchar *bezeichnung;     /* nullable */
    gchar *gegenstand;      /* nullable */
} Merk;

static void merk_free(gpointer data) {
    Merk *m = data;

    g_free(m->bezeichnung);
    g_free(m->gegenstand);
    g_free(m);
}

gchar* sond_akten_merkliste_pfad(void) {
    return g_build_filename(g_get_user_config_dir(), "sond", "akten.json", NULL);
}

static gchar* member_string(JsonObject *obj, const gchar *name) {
    JsonNode *value = json_object_has_member(obj, name) ?
            json_object_get_member(obj, name) : NULL;

    if (value && JSON_NODE_HOLDS_VALUE(value) &&
            json_node_get_value_type(value) == G_TYPE_STRING)
        return g_strdup(json_node_get_string(value));

    return NULL;
}

/* Library-Name -> Merk; leer, wenn es die Datei nicht gibt */
static GHashTable* merkliste_laden(const gchar *pfad) {
    GHashTable *map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
            merk_free);
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
                Merk *m = g_new0(Merk, 1);

                if (value && JSON_NODE_HOLDS_VALUE(value) &&
                        json_node_get_value_type(value) == G_TYPE_STRING)
                    m->bezeichnung = g_strdup(json_node_get_string(value));
                else if (value && JSON_NODE_HOLDS_OBJECT(value)) {
                    JsonObject *o = json_node_get_object(value);

                    m->bezeichnung = member_string(o, "bezeichnung");
                    m->gegenstand = member_string(o, "gegenstand");
                }
                g_hash_table_replace(map, g_strdup(l->data), m);
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
        Merk *m = value;

        json_builder_set_member_name(builder, key);
        json_builder_begin_object(builder);
        if (m->bezeichnung) {
            json_builder_set_member_name(builder, "bezeichnung");
            json_builder_add_string_value(builder, m->bezeichnung);
        }
        if (m->gegenstand) {
            json_builder_set_member_name(builder, "gegenstand");
            json_builder_add_string_value(builder, m->gegenstand);
        }
        json_builder_end_object(builder);
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

/* Wert setzen (leer = löschen); TRUE, wenn sich etwas geändert hat */
static gboolean merk_setzen(gchar **feld, const gchar *wert) {
    const gchar *neu = (wert && *wert) ? wert : NULL;

    if (g_strcmp0(*feld, neu) == 0)
        return FALSE;

    g_free(*feld);
    *feld = g_strdup(neu);

    return TRUE;
}

void sond_akten_merken(const gchar *merkliste, const gchar *library_name,
        const gchar *bezeichnung, const gchar *gegenstand) {
    GHashTable *map = NULL;
    Merk *m = NULL;
    gboolean geaendert = FALSE;

    if (!merkliste || !library_name || (!bezeichnung && !gegenstand))
        return;

    map = merkliste_laden(merkliste);
    m = g_hash_table_lookup(map, library_name);
    if (!m) {
        m = g_new0(Merk, 1);
        g_hash_table_replace(map, g_strdup(library_name), m);
    }

    if (bezeichnung && *bezeichnung)
        geaendert |= merk_setzen(&m->bezeichnung, bezeichnung);
    if (gegenstand)
        geaendert |= merk_setzen(&m->gegenstand, gegenstand);

    if (geaendert)
        merkliste_speichern(merkliste, map);
    g_hash_table_unref(map);
}

void sond_akten_bereinigen(const gchar *merkliste, GPtrArray *bekannt) {
    GHashTable *map = NULL;
    GHashTable *set = NULL;
    GHashTableIter iter;
    gpointer key = NULL;
    gboolean geaendert = FALSE;

    if (!merkliste || !bekannt)
        return;

    map = merkliste_laden(merkliste);
    set = g_hash_table_new(g_str_hash, g_str_equal);
    for (guint i = 0; i < bekannt->len; i++)
        g_hash_table_add(set, g_ptr_array_index(bekannt, i));

    g_hash_table_iter_init(&iter, map);
    while (g_hash_table_iter_next(&iter, &key, NULL)) {
        if (!g_hash_table_contains(set, key)) {
            g_hash_table_iter_remove(&iter);
            geaendert = TRUE;
        }
    }

    if (geaendert)
        merkliste_speichern(merkliste, map);

    g_hash_table_unref(set);
    g_hash_table_unref(map);
}

void sond_akten_instanz_pruefen(const gchar *merkliste, const gchar *instanz_id) {
    gchar *pfad = NULL;
    gchar *alt = NULL;
    gchar *dir = NULL;

    if (!merkliste || !instanz_id || !*instanz_id)
        return;

    pfad = g_strconcat(merkliste, ".instanz", NULL);
    if (g_file_get_contents(pfad, &alt, NULL, NULL)) {
        g_strstrip(alt);
        if (g_strcmp0(alt, instanz_id) == 0) {
            g_free(alt);
            g_free(pfad);

            return;
        }
        //anderer Server (z. B. nach dem Löschen der Volumes): Merkliste verwerfen
        g_remove(merkliste);
    }
    g_free(alt);

    dir = g_path_get_dirname(pfad);
    g_mkdir_with_parents(dir, 0700);
    g_file_set_contents(pfad, instanz_id, -1, NULL);

    g_free(dir);
    g_free(pfad);
}

/* ========================================================================
 * Lesen aus dem SeaDrive
 * ======================================================================== */

/* Liest .sond/akte einer Library in einem Zug (ein Verzeichnis für beide
 * Angaben). Bei mehreren Einträgen einer Art gilt der in der Sortierung erste.
 * *vorhanden: das Verzeichnis ließ sich öffnen. *autoritativ: es enthält
 * mindestens einen Eintrag mit Vorsilbe, stammt also von der aktuellen
 * Serverfassung - dann heißt ein fehlender Gegenstand wirklich "keiner". */
static void lese_akte_dir(const gchar *library_dir, gchar **bezeichnung,
        gchar **gegenstand, gboolean *vorhanden, gboolean *autoritativ) {
    gchar *dir_path = g_build_filename(library_dir, SOND_AKTE_NAME_DIR, NULL);
    SondDir *dir = sond_dir_open(dir_path, NULL);
    const gchar *name = NULL;
    gchar *first_b = NULL;
    gchar *first_g = NULL;

    g_free(dir_path);
    if (bezeichnung)
        *bezeichnung = NULL;
    if (gegenstand)
        *gegenstand = NULL;
    if (vorhanden)
        *vorhanden = (dir != NULL);
    if (autoritativ)
        *autoritativ = FALSE;
    if (!dir)
        return;

    while ((name = sond_dir_read_name(dir))) {
        gboolean neu = FALSE;
        gchar art = 0;
        gchar **ziel = NULL;

        if (!strcmp(name, ".") || !strcmp(name, ".."))
            continue;

        art = sond_akte_name_parse(name, NULL, &neu);
        if (neu && autoritativ)
            *autoritativ = TRUE;

        ziel = (art == SOND_AKTE_NAME_GEGENSTAND) ? &first_g : &first_b;
        if (!*ziel || g_strcmp0(name, *ziel) < 0) {
            g_free(*ziel);
            *ziel = g_strdup(name);
        }
    }
    sond_dir_close(dir);

    if (first_b) {
        if (bezeichnung)
            sond_akte_name_parse(first_b, bezeichnung, NULL);
        g_free(first_b);
    }
    if (first_g) {
        if (gegenstand)
            sond_akte_name_parse(first_g, gegenstand, NULL);
        g_free(first_g);
    }
}

gchar* sond_akten_lese_bezeichnung(const gchar *library_dir) {
    gchar *bezeichnung = NULL;

    g_return_val_if_fail(library_dir != NULL, NULL);
    lese_akte_dir(library_dir, &bezeichnung, NULL, NULL, NULL);

    return bezeichnung;
}

gchar* sond_akten_lese_gegenstand(const gchar *library_dir) {
    gchar *gegenstand = NULL;

    g_return_val_if_fail(library_dir != NULL, NULL);
    lese_akte_dir(library_dir, NULL, &gegenstand, NULL, NULL);

    return gegenstand;
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
            Merk *m = NULL;
            gboolean autoritativ = FALSE;

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
            lese_akte_dir(lib_dir, &eintrag->bezeichnung, &eintrag->gegenstand,
                    NULL, &autoritativ);

            m = g_hash_table_lookup(gemerkt, lib);
            if (!m) {
                m = g_new0(Merk, 1);
                g_hash_table_replace(gemerkt, g_strdup(lib), m);
            }

            //Bezeichnung: gelesen -> merken; sonst die gemerkte nehmen
            if (eintrag->bezeichnung)
                geaendert |= merk_setzen(&m->bezeichnung, eintrag->bezeichnung);
            else if (m->bezeichnung) {
                eintrag->bezeichnung = g_strdup(m->bezeichnung);
                eintrag->bezeichnung_gemerkt = TRUE;
            }

            //Gegenstand: ein von der aktuellen Serverfassung geschriebenes
            //Verzeichnis ist maßgeblich (ohne G~-Eintrag gibt es keinen); fehlt
            //es, ist es nicht lesbar oder stammt es nur aus der älteren Fassung
            //ohne Gegenstand, gilt die gemerkte Angabe
            if (eintrag->gegenstand)
                geaendert |= merk_setzen(&m->gegenstand, eintrag->gegenstand);
            else if (autoritativ)
                geaendert |= merk_setzen(&m->gegenstand, NULL);
            else if (m->gegenstand)
                eintrag->gegenstand = g_strdup(m->gegenstand);

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
                    e->bezeichnung ? e->bezeichnung : "", " ",
                    e->gegenstand ? e->gegenstand : "", NULL);
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
