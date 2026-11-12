/*
 sond (sond_server_archiv.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_server_archiv.h"
#include "sond_server_seafile.h"
#include "sond_server_seafile_stream.h"
#include "../sond_log_and_error.h"

#include <json-glib/json-glib.h>
#include <glib/gstdio.h>
#include <zip.h>
#include <string.h>

typedef struct {
    gchar *path;      /* absolut in der Library, z. B. "/a/b.pdf" */
    gboolean is_dir;
    gint64 size;
    gint64 mtime;
} Eintrag;

static void eintrag_free(Eintrag *e) {
    g_free(e->path);
    g_free(e);
}

/* Verzeichnis der Library lesen (JSON-Array), @out um Einträge ergänzen,
 * Unterverzeichnisse rekursiv. Verzeichnisse stehen vor ihrem Inhalt. */
static gboolean list_recursive(SondServer *server, const gchar *library_id,
                               const gchar *path, GPtrArray *out, GError **error) {
    gchar *esc = g_uri_escape_string(path, "/", FALSE);
    gchar *query = g_strdup_printf("/api2/repos/%s/dir/?p=%s", library_id, esc);
    gchar *body = NULL;
    guint status = 0;
    JsonParser *parser = NULL;
    JsonNode *root = NULL;
    JsonArray *arr = NULL;
    gboolean ok = FALSE;

    g_free(esc);
    if (!sond_server_seafile_api_call(server, "GET", query, NULL, &status, &body, error))
        goto out;
    if (status != 200) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "Verzeichnis '%s' nicht lesbar (HTTP %u)", path, status);
        goto out;
    }

    parser = json_parser_new();
    if (!json_parser_load_from_data(parser, body, -1, error))
        goto out;
    root = json_parser_get_root(parser);
    if (!root || !JSON_NODE_HOLDS_ARRAY(root)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "Unerwartete Antwort für Verzeichnis '%s'", path);
        goto out;
    }
    arr = json_node_get_array(root);

    for (guint i = 0; i < json_array_get_length(arr); i++) {
        JsonObject *obj = json_array_get_object_element(arr, i);
        const gchar *type = obj && json_object_has_member(obj, "type") ?
                json_object_get_string_member(obj, "type") : NULL;
        const gchar *name = obj && json_object_has_member(obj, "name") ?
                json_object_get_string_member(obj, "name") : NULL;
        Eintrag *e = NULL;

        if (!type || !name || !name[0])
            continue;
        if (g_strcmp0(type, "file") && g_strcmp0(type, "dir"))
            continue;

        e = g_new0(Eintrag, 1);
        e->path = g_strcmp0(path, "/") ? g_strdup_printf("%s/%s", path, name) :
                g_strdup_printf("/%s", name);
        e->is_dir = !g_strcmp0(type, "dir");
        e->size = (!e->is_dir && json_object_has_member(obj, "size")) ?
                json_object_get_int_member(obj, "size") : 0;
        e->mtime = json_object_has_member(obj, "mtime") ?
                json_object_get_int_member(obj, "mtime") : 0;
        g_ptr_array_add(out, e);

        if (e->is_dir && !list_recursive(server, library_id, e->path, out, error))
            goto out;
    }

    ok = TRUE;

out:
    g_clear_object(&parser);
    g_free(body);
    g_free(query);

    return ok;
}

/* Größe einer Datei im Wurzelverzeichnis der Library; -1 wenn sie fehlt */
static gint64 root_file_size(SondServer *server, const gchar *library_id,
                             const gchar *name, GError **error) {
    gchar *query = g_strdup_printf("/api2/repos/%s/dir/?p=%%2F", library_id);
    gchar *body = NULL;
    guint status = 0;
    gint64 result = -2;
    JsonParser *parser = NULL;

    if (!sond_server_seafile_api_call(server, "GET", query, NULL, &status, &body, error))
        goto out;
    if (status != 200) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "Wurzelverzeichnis nicht lesbar (HTTP %u)", status);
        goto out;
    }

    parser = json_parser_new();
    if (!json_parser_load_from_data(parser, body, -1, error))
        goto out;

    JsonNode *root = json_parser_get_root(parser);

    result = -1;
    if (root && JSON_NODE_HOLDS_ARRAY(root)) {
        JsonArray *arr = json_node_get_array(root);

        for (guint i = 0; i < json_array_get_length(arr); i++) {
            JsonObject *obj = json_array_get_object_element(arr, i);

            if (obj && json_object_has_member(obj, "name") &&
                    !g_strcmp0(json_object_get_string_member(obj, "name"), name)) {
                result = json_object_has_member(obj, "size") ?
                        json_object_get_int_member(obj, "size") : 0;
                break;
            }
        }
    }

out:
    g_clear_object(&parser);
    g_free(body);
    g_free(query);

    return result;
}

static void remove_tmp_dir(const gchar *dir) {
    GDir *d = g_dir_open(dir, 0, NULL);
    const gchar *name = NULL;

    while (d && (name = g_dir_read_name(d))) {
        gchar *p = g_build_filename(dir, name, NULL);

        g_remove(p);
        g_free(p);
    }
    if (d)
        g_dir_close(d);
    g_rmdir(dir);
}

/* Name im ZIP: ohne führenden Schrägstrich, Verzeichnisse mit Schlussstrich */
static gchar* zip_name(const Eintrag *e) {
    return g_strdup_printf("%s%s", e->path + 1, e->is_dir ? "/" : "");
}

/* Dateien laden und das ZIP schreiben */
static gboolean build_zip(SondServer *server, const gchar *library_id,
                          GPtrArray *liste, const gchar *tmpdir,
                          const gchar *zip_path, GError **error) {
    zip_t *z = NULL;
    int zerr = 0;
    guint n_file = 0;

    z = zip_open(zip_path, ZIP_CREATE | ZIP_TRUNCATE, &zerr);
    if (!z) {
        zip_error_t ze;

        zip_error_init_with_code(&ze, zerr);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "ZIP konnte nicht angelegt werden: %s", zip_error_strerror(&ze));
        zip_error_fini(&ze);

        return FALSE;
    }

    for (guint i = 0; i < liste->len; i++) {
        Eintrag *e = g_ptr_array_index(liste, i);
        gchar *name = zip_name(e);
        zip_int64_t idx = -1;

        if (e->is_dir) {
            idx = zip_dir_add(z, name, ZIP_FL_ENC_UTF_8);
        } else {
            gchar *tmp = g_strdup_printf("%s%cf%u", tmpdir, G_DIR_SEPARATOR, n_file++);
            zip_source_t *src = NULL;

            if (!sond_server_seafile_download_file(server, library_id, e->path, tmp, error)) {
                g_free(tmp);
                g_free(name);
                zip_discard(z);

                return FALSE;
            }
            src = zip_source_file(z, tmp, 0, 0);
            g_free(tmp);
            if (src) {
                idx = zip_file_add(z, name, src, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE);
                if (idx < 0)
                    zip_source_free(src);
            }
            if (idx >= 0)
                zip_set_file_compression(z, (zip_uint64_t) idx, ZIP_CM_DEFLATE, 9);
        }

        if (idx < 0) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "'%s' konnte nicht ins ZIP aufgenommen werden: %s", e->path,
                    zip_strerror(z));
            g_free(name);
            zip_discard(z);

            return FALSE;
        }
        if (e->mtime > 0)
            zip_file_set_mtime(z, (zip_uint64_t) idx, (time_t) e->mtime, 0);
        g_free(name);
    }

    if (zip_close(z) < 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "ZIP konnte nicht geschrieben werden: %s", zip_strerror(z));
        zip_discard(z);

        return FALSE;
    }

    return TRUE;
}

/* Fertiges ZIP gegen die Liste prüfen: gleiche Einträge, Größen, CRC */
static gboolean verify_zip(GPtrArray *liste, const gchar *zip_path, GError **error) {
    zip_t *z = NULL;
    int zerr = 0;
    GHashTable *erwartet = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    gboolean ok = FALSE;
    zip_int64_t n = 0;
    guchar *buf = g_malloc(65536);

    for (guint i = 0; i < liste->len; i++) {
        Eintrag *e = g_ptr_array_index(liste, i);
        gint64 *size = g_new(gint64, 1);

        *size = e->size;
        g_hash_table_insert(erwartet, zip_name(e), size);
    }

    z = zip_open(zip_path, ZIP_RDONLY | ZIP_CHECKCONS, &zerr);
    if (!z) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "ZIP lässt sich nicht öffnen (Code %d)", zerr);
        goto out;
    }

    n = zip_get_num_entries(z, 0);
    if ((guint) n != liste->len) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "ZIP enthält %" G_GINT64_FORMAT " statt %u Einträge", (gint64) n, liste->len);
        goto out;
    }

    for (zip_int64_t i = 0; i < n; i++) {
        const gchar *name = zip_get_name(z, (zip_uint64_t) i, ZIP_FL_ENC_UTF_8);
        gint64 *soll = name ? g_hash_table_lookup(erwartet, name) : NULL;
        zip_stat_t st;
        zip_file_t *f = NULL;
        gint64 gelesen = 0;
        zip_int64_t r = 0;

        if (!soll) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "Unerwarteter Eintrag im ZIP: %s", name ? name : "?");
            goto out;
        }
        if (name[strlen(name) - 1] != '/') {
            zip_stat_init(&st);
            if (zip_stat_index(z, (zip_uint64_t) i, 0, &st) < 0 ||
                    !(st.valid & ZIP_STAT_SIZE) || (gint64) st.size != *soll) {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "Größe von '%s' im ZIP stimmt nicht", name);
                goto out;
            }
            f = zip_fopen_index(z, (zip_uint64_t) i, 0);
            if (!f) {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "'%s' im ZIP nicht lesbar: %s", name, zip_strerror(z));
                goto out;
            }
            while ((r = zip_fread(f, buf, 65536)) > 0)
                gelesen += r;
            if (r < 0 || zip_fclose(f) != 0 || gelesen != *soll) {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "Prüfsumme/Länge von '%s' im ZIP fehlerhaft", name);
                goto out;
            }
        }
        g_hash_table_remove(erwartet, name);
    }

    ok = g_hash_table_size(erwartet) == 0;
    if (!ok)
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "ZIP ist unvollständig (%u Einträge fehlen)", g_hash_table_size(erwartet));

out:
    if (z)
        zip_discard(z);
    g_hash_table_unref(erwartet);
    g_free(buf);

    return ok;
}

/* Liegt das Archiv @datei schon in "abgelegteAkten"? */
static gboolean bereits_archiviert(SondServer *server, const gchar *datei) {
    GError *err = NULL;
    gchar *arch_id = sond_server_seafile_get_library_id_by_name(server,
            SOND_SERVER_ARCHIV_LIBRARY, &err);
    gboolean da = FALSE;

    g_clear_error(&err);
    if (arch_id) {
        da = root_file_size(server, arch_id, datei, &err) >= 0;
        g_clear_error(&err);
    }
    g_free(arch_id);

    return da;
}

gboolean sond_server_archiv_library(SondServer *server, const gchar *library_name,
                                    const gchar *archiv_name, gboolean delete_after,
                                    GError **error) {
    g_return_val_if_fail(server && library_name && archiv_name, FALSE);

    gchar *lib_id = NULL;
    gchar *arch_id = NULL;
    gchar *datei = NULL;
    gchar *tmpdir = NULL;
    gchar *zip_path = NULL;
    GPtrArray *liste = g_ptr_array_new_with_free_func((GDestroyNotify) eintrag_free);
    GError *local = NULL;
    gint64 zip_size = 0;
    gint64 remote = 0;
    gboolean ok = FALSE;

    if (!archiv_name[0] || strpbrk(archiv_name, "/\\")) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                "Ungültiger Archivname '%s'", archiv_name);
        goto out;
    }
    datei = g_strdup_printf("%s.zip", archiv_name);

    lib_id = sond_server_seafile_get_library_id_by_name(server, library_name, &local);
    if (!lib_id) {
        //Library fehlt, Archiv ist da: ein früherer Lauf war schon erfolgreich
        if (g_error_matches(local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) &&
                bereits_archiviert(server, datei)) {
            LOG_INFO("'%s' ist bereits als '%s' archiviert\n", library_name, datei);
            ok = TRUE;
        } else {
            g_propagate_error(error, local);
            local = NULL;
        }
        goto out;
    }

    arch_id = sond_server_seafile_get_library_id_by_name(server,
            SOND_SERVER_ARCHIV_LIBRARY, &local);
    if (!arch_id) {
        if (!g_error_matches(local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
            g_propagate_error(error, local);
            local = NULL;
            goto out;
        }
        g_clear_error(&local);
        arch_id = sond_server_seafile_create_library(server, SOND_SERVER_ARCHIV_LIBRARY,
                "Archivierte Akten (ZIP)", error);
        if (!arch_id)
            goto out;
        LOG_INFO("Library '%s' angelegt\n", SOND_SERVER_ARCHIV_LIBRARY);
    }

    remote = root_file_size(server, arch_id, datei, error);
    if (remote == -2)
        goto out;
    if (remote >= 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS,
                "Archiv '%s' existiert bereits", datei);
        goto out;
    }

    if (!list_recursive(server, lib_id, "/", liste, error))
        goto out;
    LOG_INFO("Archiviere '%s': %u Einträge\n", library_name, liste->len);

    tmpdir = g_dir_make_tmp("sond_archiv_XXXXXX", error);
    if (!tmpdir)
        goto out;
    zip_path = g_build_filename(tmpdir, "archiv.zip", NULL);

    if (!build_zip(server, lib_id, liste, tmpdir, zip_path, error))
        goto out;
    if (!verify_zip(liste, zip_path, error))
        goto out;

    {
        GStatBuf st;

        if (g_stat(zip_path, &st) != 0) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "ZIP nicht gefunden");
            goto out;
        }
        zip_size = (gint64) st.st_size;
    }

    if (!sond_server_seafile_upload_file(server, arch_id, "/", datei, zip_path, error))
        goto out;
    remote = root_file_size(server, arch_id, datei, error);
    if (remote != zip_size) {
        if (remote != -2)
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "Archiv '%s' nach dem Hochladen nicht oder mit falscher Größe "
                    "vorhanden (%" G_GINT64_FORMAT " statt %" G_GINT64_FORMAT ")",
                    datei, remote, zip_size);
        goto out;
    }
    LOG_INFO("Archiv '%s' (%" G_GINT64_FORMAT " Bytes) abgelegt\n", datei, zip_size);

    if (delete_after && !sond_server_seafile_delete_library(server, lib_id, error))
        goto out;

    ok = TRUE;

out:
    if (tmpdir)
        remove_tmp_dir(tmpdir);
    g_clear_error(&local);
    g_ptr_array_unref(liste);
    g_free(lib_id);
    g_free(arch_id);
    g_free(datei);
    g_free(tmpdir);
    g_free(zip_path);

    return ok;
}

/* ------------------------------------------------------------------------
 * Wiederherstellen
 * ---------------------------------------------------------------------- */

/* Verzeichnisse von "/" bis @path in der Library anlegen (jedes nur einmal) */
static gboolean ensure_dir_chain(SondServer *server, const gchar *library_id,
                                 const gchar *path, GHashTable *angelegt, GError **error) {
    gchar **teile = g_strsplit(path, "/", -1);
    GString *p = g_string_new(NULL);
    gboolean ok = TRUE;

    for (guint i = 0; teile[i] && ok; i++) {
        if (!teile[i][0])
            continue;
        g_string_append_printf(p, "/%s", teile[i]);
        if (g_hash_table_contains(angelegt, p->str))
            continue;
        ok = sond_server_seafile_ensure_dir(server, library_id, p->str, error);
        if (ok)
            g_hash_table_add(angelegt, g_strdup(p->str));
    }
    g_string_free(p, TRUE);
    g_strfreev(teile);

    return ok;
}

/* Eintrag @idx des ZIP in die Datei @dest schreiben */
static gboolean extract_entry(zip_t *z, zip_uint64_t idx, gint64 soll, const gchar *dest,
                              GError **error) {
    zip_file_t *f = zip_fopen_index(z, idx, 0);
    FILE *out = NULL;
    guchar *buf = NULL;
    zip_int64_t r = 0;
    gint64 gesamt = 0;
    gboolean ok = FALSE;

    if (!f) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "ZIP-Eintrag nicht lesbar: %s", zip_strerror(z));
        return FALSE;
    }
    out = g_fopen(dest, "wb");
    if (!out) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "Datei '%s' nicht schreibbar", dest);
        zip_fclose(f);
        return FALSE;
    }

    buf = g_malloc(65536);
    while ((r = zip_fread(f, buf, 65536)) > 0) {
        if (fwrite(buf, 1, (size_t) r, out) != (size_t) r) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Schreibfehler (Platte voll?)");
            goto out;
        }
        gesamt += r;
    }
    if (r < 0 || gesamt != soll) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "ZIP-Eintrag fehlerhaft (Prüfsumme/Länge)");
        goto out;
    }
    ok = TRUE;

out:
    g_free(buf);
    fclose(out);
    zip_fclose(f);

    return ok;
}

/* Wiederhergestellte Library gegen das ZIP prüfen: gleiche Dateien und Größen */
static gboolean verify_restored(zip_t *z, GPtrArray *liste, GError **error) {
    GHashTable *erwartet = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    zip_int64_t n = zip_get_num_entries(z, 0);
    gboolean ok = FALSE;

    for (zip_int64_t i = 0; i < n; i++) {
        const gchar *name = zip_get_name(z, (zip_uint64_t) i, ZIP_FL_ENC_UTF_8);
        zip_stat_t st;
        gint64 *size = NULL;

        if (!name || name[strlen(name) - 1] == '/')
            continue;
        zip_stat_init(&st);
        if (zip_stat_index(z, (zip_uint64_t) i, 0, &st) < 0)
            goto out;
        size = g_new(gint64, 1);
        *size = (gint64) st.size;
        g_hash_table_insert(erwartet, g_strdup(name), size);
    }

    for (guint i = 0; i < liste->len; i++) {
        Eintrag *e = g_ptr_array_index(liste, i);
        gchar *name = NULL;
        gint64 *soll = NULL;

        if (e->is_dir)
            continue;
        name = zip_name(e);
        soll = g_hash_table_lookup(erwartet, name);
        if (!soll || *soll != e->size) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "Wiederhergestellte Datei '%s' stimmt nicht mit dem Archiv überein", e->path);
            g_free(name);
            goto out;
        }
        g_hash_table_remove(erwartet, name);
        g_free(name);
    }

    ok = g_hash_table_size(erwartet) == 0;
    if (!ok)
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "%u Dateien aus dem Archiv fehlen in der wiederhergestellten Library",
                g_hash_table_size(erwartet));

out:
    g_hash_table_unref(erwartet);

    return ok;
}

gboolean sond_server_archiv_restore(SondServer *server, const gchar *library_name,
                                    const gchar *archiv_name, GError **error) {
    g_return_val_if_fail(server && library_name && archiv_name, FALSE);

    gchar *datei = NULL;
    gchar *arch_id = NULL;
    gchar *lib_id = NULL;
    gchar *tmpdir = NULL;
    gchar *zip_path = NULL;
    gchar *remote_pfad = NULL;
    GError *local = NULL;
    zip_t *z = NULL;
    GHashTable *angelegt = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GPtrArray *liste = g_ptr_array_new_with_free_func((GDestroyNotify) eintrag_free);
    gboolean angelegt_hier = FALSE;
    gboolean ok = FALSE;
    int zerr = 0;

    if (!library_name[0] || strpbrk(library_name, "/\\") || !archiv_name[0] ||
            strpbrk(archiv_name, "/\\")) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                "Ungültiger Library- oder Archivname");
        goto out;
    }
    datei = g_strdup_printf("%s.zip", archiv_name);

    //das Ziel darf noch nicht existieren
    lib_id = sond_server_seafile_get_library_id_by_name(server, library_name, &local);
    if (lib_id) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS,
                "Library '%s' existiert bereits", library_name);
        g_free(lib_id);
        lib_id = NULL;
        goto out;
    }
    if (!g_error_matches(local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
        g_propagate_error(error, local);
        local = NULL;
        goto out;
    }
    g_clear_error(&local);

    arch_id = sond_server_seafile_get_library_id_by_name(server,
            SOND_SERVER_ARCHIV_LIBRARY, error);
    if (!arch_id)
        goto out;
    {
        gint64 groesse = root_file_size(server, arch_id, datei, error);

        if (groesse == -2)
            goto out;
        if (groesse < 0) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                    "Archiv '%s' nicht gefunden", datei);
            goto out;
        }
    }

    tmpdir = g_dir_make_tmp("sond_restore_XXXXXX", error);
    if (!tmpdir)
        goto out;
    zip_path = g_build_filename(tmpdir, "archiv.zip", NULL);
    remote_pfad = g_strdup_printf("/%s", datei);
    if (!sond_server_seafile_download_file(server, arch_id, remote_pfad, zip_path, error))
        goto out;

    z = zip_open(zip_path, ZIP_RDONLY | ZIP_CHECKCONS, &zerr);
    if (!z) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "Archiv '%s' lässt sich nicht öffnen (Code %d)", datei, zerr);
        goto out;
    }

    lib_id = sond_server_seafile_create_library(server, library_name, NULL, error);
    if (!lib_id)
        goto out;
    angelegt_hier = TRUE;
    LOG_INFO("Stelle '%s' aus '%s' wieder her\n", library_name, datei);

    for (zip_int64_t i = 0; i < zip_get_num_entries(z, 0); i++) {
        const gchar *name = zip_get_name(z, (zip_uint64_t) i, ZIP_FL_ENC_UTF_8);
        gchar *pfad = NULL;
        gboolean ist_dir = FALSE;

        if (!name || !name[0] || name[0] == '/' || strstr(name, "../") ||
                g_str_has_prefix(name, "..")) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "Unzulässiger Pfad im Archiv: %s", name ? name : "?");
            goto out;
        }
        ist_dir = name[strlen(name) - 1] == '/';
        pfad = g_strdup_printf("/%s", name);
        if (ist_dir)
            pfad[strlen(pfad) - 1] = '\0';

        if (ist_dir) {
            gboolean r = ensure_dir_chain(server, lib_id, pfad, angelegt, error);

            g_free(pfad);
            if (!r)
                goto out;
        } else {
            gchar *eltern = g_path_get_dirname(pfad);
            gchar *basis = g_path_get_basename(pfad);
            gchar *tmp = g_build_filename(tmpdir, "datei", NULL);
            zip_stat_t st;
            gboolean r = FALSE;

            zip_stat_init(&st);
            r = zip_stat_index(z, (zip_uint64_t) i, 0, &st) == 0 &&
                    ensure_dir_chain(server, lib_id, eltern, angelegt, error) &&
                    extract_entry(z, (zip_uint64_t) i, (gint64) st.size, tmp, error) &&
                    sond_server_seafile_upload_file(server, lib_id, eltern, basis, tmp, error);
            g_remove(tmp);
            g_free(tmp);
            g_free(eltern);
            g_free(basis);
            g_free(pfad);
            if (!r) {
                if (error && !*error)
                    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "'%s' konnte nicht wiederhergestellt werden", name);
                goto out;
            }
        }
    }

    if (!list_recursive(server, lib_id, "/", liste, error) ||
            !verify_restored(z, liste, error))
        goto out;

    LOG_INFO("Library '%s' wiederhergestellt\n", library_name);
    ok = TRUE;

out:
    if (z)
        zip_discard(z);
    //halb wiederhergestellte Library entfernen, damit ein neuer Versuch möglich ist
    if (!ok && angelegt_hier && lib_id) {
        GError *del = NULL;

        if (!sond_server_seafile_delete_library(server, lib_id, &del))
            LOG_WARN("Unvollständige Library '%s' konnte nicht entfernt werden: %s\n",
                    library_name, del ? del->message : "?");
        g_clear_error(&del);
    }
    if (tmpdir)
        remove_tmp_dir(tmpdir);
    g_clear_error(&local);
    g_hash_table_unref(angelegt);
    g_ptr_array_unref(liste);
    g_free(datei);
    g_free(arch_id);
    g_free(lib_id);
    g_free(tmpdir);
    g_free(zip_path);
    g_free(remote_pfad);

    return ok;
}
