/*
 sond (sond_eingang_zettel.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_eingang_zettel.h"

#include <errno.h>
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <stdio.h>
#include <string.h>

#include "sond_file_helper.h"

#define ZETTEL_FORMAT 1

static void eintrag_free(gpointer data) {
    SondEingangEintrag *e = data;

    g_free(e->pfad);
    g_free(e->eingang);
    g_free(e->art);
    g_free(e->bemerkung);
    g_free(e);
}

SondEingangZettel* sond_eingang_zettel_new(const gchar *von, const gchar *quelle) {
    SondEingangZettel *zettel = g_new0(SondEingangZettel, 1);
    GDateTime *jetzt = g_date_time_new_now_utc();

    zettel->id = g_uuid_string_random();
    zettel->angelegt = g_date_time_format(jetzt, "%Y-%m-%dT%H:%M:%SZ");
    zettel->von = g_strdup(von ? von : "");
    zettel->rechner = g_strdup(g_get_host_name());
    zettel->quelle = g_strdup(quelle ? quelle : "import");
    zettel->eintraege = g_ptr_array_new_with_free_func(eintrag_free);
    g_date_time_unref(jetzt);

    return zettel;
}

void sond_eingang_zettel_free(SondEingangZettel *zettel) {
    if (!zettel)
        return;

    g_free(zettel->id);
    g_free(zettel->angelegt);
    g_free(zettel->von);
    g_free(zettel->rechner);
    g_free(zettel->quelle);
    if (zettel->eintraege)
        g_ptr_array_unref(zettel->eintraege);
    g_free(zettel);
}

void sond_eingang_zettel_add(SondEingangZettel *zettel, const gchar *pfad,
        gboolean ist_ordner, const gchar *eingang, const gchar *art,
        const gchar *bemerkung, gint64 original) {
    SondEingangEintrag *e = g_new0(SondEingangEintrag, 1);

    e->pfad = g_strdup(pfad);
    e->ist_ordner = ist_ordner;
    e->eingang = g_strdup(eingang);
    e->art = (art && *art) ? g_strdup(art) : NULL;
    e->bemerkung = (bemerkung && *bemerkung) ? g_strdup(bemerkung) : NULL;
    e->original = original;
    g_ptr_array_add(zettel->eintraege, e);
}

void sond_eingang_zettel_add_entfernen(SondEingangZettel *zettel,
        const gchar *pfad) {
    SondEingangEintrag *e = g_new0(SondEingangEintrag, 1);

    e->pfad = g_strdup(pfad);
    e->entfernen = TRUE;
    g_ptr_array_add(zettel->eintraege, e);
}

/* ========================================================================
 * Datum
 * ======================================================================== */

gboolean sond_eingang_datum_parse(const gchar *text, gchar **iso) {
    guint a = 0, b = 0, c = 0;
    gchar sep1 = 0, sep2 = 0;
    gchar *trimmed = NULL;
    gint day = 0, month = 0, year = 0;
    gint n = 0;

    if (iso)
        *iso = NULL;
    if (!text)
        return FALSE;

    trimmed = g_strstrip(g_strdup(text));
    n = sscanf(trimmed, "%u%c%u%c%u", &a, &sep1, &b, &sep2, &c);

    if (n == 5 && sep1 == '.' && sep2 == '.') {        //TT.MM.JJJJ oder T.M.JJ
        day = (gint) a;
        month = (gint) b;
        year = (gint) c;
        if (year < 100)
            year += 2000;
    } else if (n == 5 && sep1 == '-' && sep2 == '-') { //JJJJ-MM-TT
        year = (gint) a;
        month = (gint) b;
        day = (gint) c;
    } else {
        g_free(trimmed);

        return FALSE;
    }
    g_free(trimmed);

    if (year < 1900 || year > 2200 || !g_date_valid_dmy((GDateDay) day,
            (GDateMonth) month, (GDateYear) year))
        return FALSE;

    if (iso)
        *iso = g_strdup_printf("%04d-%02d-%02d", year, month, day);

    return TRUE;
}

gchar* sond_eingang_datum_heute(void) {
    GDateTime *jetzt = g_date_time_new_now_local();
    gchar *text = g_date_time_format(jetzt, "%Y-%m-%d");

    g_date_time_unref(jetzt);

    return text;
}

gchar* sond_eingang_datum_anzeige(const gchar *iso) {
    gchar *parsed = NULL;
    gchar *text = NULL;
    gint year = 0, month = 0, day = 0;

    if (!sond_eingang_datum_parse(iso, &parsed))
        return g_strdup(iso ? iso : "");

    sscanf(parsed, "%d-%d-%d", &year, &month, &day);
    text = g_strdup_printf("%02d.%02d.%04d", day, month, year);
    g_free(parsed);

    return text;
}

/* ========================================================================
 * Schreiben
 * ======================================================================== */

static gchar* unix_to_iso(gint64 sekunden) {
    GDateTime *zeit = g_date_time_new_from_unix_utc(sekunden);
    gchar *text = NULL;

    if (!zeit)
        return NULL;
    text = g_date_time_format(zeit, "%Y-%m-%dT%H:%M:%SZ");
    g_date_time_unref(zeit);

    return text;
}

static gchar* zettel_to_json(SondEingangZettel *zettel) {
    JsonBuilder *builder = json_builder_new();
    JsonGenerator *generator = json_generator_new();
    JsonNode *root = NULL;
    gchar *json = NULL;

    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "format");
    json_builder_add_int_value(builder, ZETTEL_FORMAT);
    json_builder_set_member_name(builder, "id");
    json_builder_add_string_value(builder, zettel->id);
    json_builder_set_member_name(builder, "angelegt");
    json_builder_add_string_value(builder, zettel->angelegt);
    json_builder_set_member_name(builder, "von");
    json_builder_add_string_value(builder, zettel->von);
    json_builder_set_member_name(builder, "rechner");
    json_builder_add_string_value(builder, zettel->rechner);
    json_builder_set_member_name(builder, "quelle");
    json_builder_add_string_value(builder, zettel->quelle);

    json_builder_set_member_name(builder, "eintraege");
    json_builder_begin_array(builder);
    for (guint i = 0; i < zettel->eintraege->len; i++) {
        SondEingangEintrag *e = g_ptr_array_index(zettel->eintraege, i);

        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "aktion");
        json_builder_add_string_value(builder, e->entfernen ? "entfernen" : "setzen");
        json_builder_set_member_name(builder, "pfad");
        json_builder_add_string_value(builder, e->pfad);

        if (!e->entfernen) {
            gchar *original = e->original > 0 ? unix_to_iso(e->original) : NULL;

            json_builder_set_member_name(builder, "typ");
            json_builder_add_string_value(builder, e->ist_ordner ? "ordner" : "datei");
            json_builder_set_member_name(builder, "eingang");
            json_builder_add_string_value(builder, e->eingang);
            if (e->art) {
                json_builder_set_member_name(builder, "art");
                json_builder_add_string_value(builder, e->art);
            }
            if (e->bemerkung) {
                json_builder_set_member_name(builder, "bemerkung");
                json_builder_add_string_value(builder, e->bemerkung);
            }
            if (original) {
                json_builder_set_member_name(builder, "original");
                json_builder_add_string_value(builder, original);
            }
            g_free(original);
        }
        json_builder_end_object(builder);
    }
    json_builder_end_array(builder);
    json_builder_end_object(builder);

    root = json_builder_get_root(builder);
    json_generator_set_root(generator, root);
    json_generator_set_pretty(generator, TRUE);
    json = json_generator_to_data(generator, NULL);

    json_node_unref(root);
    g_object_unref(generator);
    g_object_unref(builder);

    return json;
}

gchar* sond_eingang_zettel_write(SondEingangZettel *zettel,
        const gchar *library_dir, GError **error) {
    gchar *dir = NULL;
    gchar *compact = NULL;
    gchar *user = NULL;
    gchar *name = NULL;
    gchar *path = NULL;
    gchar *path_tmp = NULL;
    gchar *json = NULL;
    FILE *fp = NULL;
    gboolean ok = FALSE;
    gsize len = 0;

    g_return_val_if_fail(zettel != NULL, NULL);
    g_return_val_if_fail(library_dir != NULL, NULL);

    if (zettel->eintraege->len == 0)
        return NULL;

    dir = g_build_filename(library_dir, ".sond", "eingang", NULL);
    if (!sond_mkdir_with_parents(dir, error)) {
        g_free(dir);

        return NULL;
    }

    //"2026-11-11T13:05:22Z" -> "20261111T130522Z"; sortiert chronologisch
    compact = g_strdup(zettel->angelegt);
    for (gchar *src = compact, *dst = compact; ; src++) {
        if (*src != '-' && *src != ':')
            *dst++ = *src;
        if (!*src)
            break;
    }

    user = sond_sanitize_filename(*zettel->von ? zettel->von : "unbekannt");
    {
        gchar *kurz = g_strndup(zettel->id, 8);

        name = g_strdup_printf("%s_%s_%s.json", compact, user, kurz);
        g_free(kurz);
    }
    path = g_build_filename(dir, name, NULL);
    path_tmp = g_strconcat(path, ".tmp", NULL);

    json = zettel_to_json(zettel);
    len = strlen(json);

    fp = sond_fopen(path_tmp, "wb", error);
    if (fp) {
        if (fwrite(json, 1, len, fp) != len)
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "Fehler beim Schreiben des Zettels: %s", g_strerror(errno));
        else
            ok = TRUE;

        if (fclose(fp) != 0 && ok) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "Fehler beim Schreiben des Zettels: %s", g_strerror(errno));
            ok = FALSE;
        }
    }

    if (ok)
        ok = sond_rename(path_tmp, path, error);
    if (!ok && sond_exists(path_tmp))
        sond_remove(path_tmp, NULL);

    g_free(json);
    g_free(path_tmp);
    g_free(path);
    g_free(user);
    g_free(compact);
    g_free(dir);

    if (!ok) {
        g_free(name);

        return NULL;
    }

    return name;
}

/* ========================================================================
 * Lesen
 * ======================================================================== */

static gchar* member_dup(JsonObject *obj, const gchar *name) {
    if (!json_object_has_member(obj, name))
        return NULL;

    return g_strdup(json_object_get_string_member(obj, name));
}

SondEingangZettel* sond_eingang_zettel_read(const gchar *file_path,
        GError **error) {
    gchar *contents = NULL;
    gsize length = 0;
    JsonParser *parser = NULL;
    JsonObject *obj = NULL;
    JsonArray *arr = NULL;
    SondEingangZettel *zettel = NULL;

    g_return_val_if_fail(file_path != NULL, NULL);

    if (!sond_file_get_contents(file_path, &contents, &length, error))
        return NULL;

    parser = json_parser_new();
    if (!json_parser_load_from_data(parser, contents, (gssize) length, error)) {
        g_free(contents);
        g_object_unref(parser);

        return NULL;
    }
    g_free(contents);

    if (!JSON_NODE_HOLDS_OBJECT(json_parser_get_root(parser))) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                "Zettel '%s': kein JSON-Objekt", file_path);
        g_object_unref(parser);

        return NULL;
    }
    obj = json_node_get_object(json_parser_get_root(parser));

    if (!json_object_has_member(obj, "format") ||
            json_object_get_int_member(obj, "format") > ZETTEL_FORMAT) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                "Zettel '%s': unbekanntes Format", file_path);
        g_object_unref(parser);

        return NULL;
    }

    zettel = sond_eingang_zettel_new(NULL, NULL);
    g_free(zettel->id);
    g_free(zettel->angelegt);
    g_free(zettel->von);
    g_free(zettel->rechner);
    g_free(zettel->quelle);
    zettel->id = member_dup(obj, "id");
    zettel->angelegt = member_dup(obj, "angelegt");
    zettel->von = member_dup(obj, "von");
    zettel->rechner = member_dup(obj, "rechner");
    zettel->quelle = member_dup(obj, "quelle");

    if (json_object_has_member(obj, "eintraege"))
        arr = json_object_get_array_member(obj, "eintraege");

    for (guint i = 0; arr && i < json_array_get_length(arr); i++) {
        JsonObject *eo = json_array_get_object_element(arr, i);
        gchar *aktion = NULL;
        gchar *pfad = NULL;

        if (!eo)
            continue;

        aktion = member_dup(eo, "aktion");
        pfad = member_dup(eo, "pfad");
        if (!pfad || !*pfad) {
            g_free(aktion);
            g_free(pfad);
            continue;
        }

        if (g_strcmp0(aktion, "entfernen") == 0)
            sond_eingang_zettel_add_entfernen(zettel, pfad);
        else if (g_strcmp0(aktion, "setzen") == 0) {
            gchar *eingang = member_dup(eo, "eingang");
            gchar *typ = member_dup(eo, "typ");
            gchar *art = member_dup(eo, "art");
            gchar *bemerkung = member_dup(eo, "bemerkung");
            gchar *original_text = member_dup(eo, "original");
            GDateTime *original = NULL;

            if (original_text)
                original = g_date_time_new_from_iso8601(original_text, NULL);

            if (eingang && sond_eingang_datum_parse(eingang, NULL))
                sond_eingang_zettel_add(zettel, pfad,
                        g_strcmp0(typ, "ordner") == 0, eingang, art, bemerkung,
                        original ? g_date_time_to_unix(original) : 0);

            if (original)
                g_date_time_unref(original);
            g_free(original_text);
            g_free(bemerkung);
            g_free(art);
            g_free(typ);
            g_free(eingang);
        }

        g_free(aktion);
        g_free(pfad);
    }

    g_object_unref(parser);

    return zettel;
}
