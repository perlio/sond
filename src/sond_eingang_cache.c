/*
 sond (sond_eingang_cache.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_eingang_cache.h"

#include <gio/gio.h>
#include <sqlite3.h>
#include <string.h>

#include "sond_file_helper.h"
#include "sond_log_and_error.h"

#define CACHE_FORMAT   1     /* PRAGMA user_version */
#define MEMO_MAX       512   /* Verzeichniskontexte im Speicher */

struct _SondEingangCache {
    sqlite3 *db;
    gchar *library_dir;
    GHashTable *memo;        /* Schlüssel des Elternverzeichnisses -> DirCtx */
};

/* Alles, was für die Kinder eines Verzeichnisses gebraucht wird */
typedef struct {
    gint inherited;          /* Stempel des Verzeichnisses oder eines Vorfahren */
    GHashTable *own;         /* Schlüssel eines Kindes -> Datum (eigener Stempel) */
} DirCtx;

static void dir_ctx_free(gpointer data) {
    DirCtx *ctx = data;

    g_hash_table_unref(ctx->own);
    g_free(ctx);
}

/* ========================================================================
 * Pfadschlüssel
 * ======================================================================== */

/* Vereinheitlicht einen Pfad: '/' als Trenner, ohne Anfangs- und Endstrich;
 * unter Windows (Dateisystem ohne Beachtung der Groß-/Kleinschreibung) in
 * Kleinschreibung gefaltet. */
static gchar* make_key(const gchar *pfad) {
    gchar *norm = g_strdup(pfad ? pfad : "");
    gchar *start = norm;
    gsize len = 0;
    gchar *key = NULL;

    for (gchar *c = norm; *c; c++)
        if (*c == '\\')
            *c = '/';

    while (*start == '/')
        start++;
    len = strlen(start);
    while (len > 0 && start[len - 1] == '/')
        start[--len] = '\0';

#ifdef G_OS_WIN32
    key = g_utf8_casefold(start, -1);
#else
    key = g_strdup(start);
#endif
    g_free(norm);

    return key;
}

static gchar* parent_of(const gchar *key) {
    const gchar *slash = strrchr(key, '/');

    return slash ? g_strndup(key, (gsize) (slash - key)) : g_strdup("");
}

/* ========================================================================
 * Datenbank
 * ======================================================================== */

static gboolean exec_sql(SondEingangCache *cache, const gchar *sql,
        GError **error) {
    gchar *errmsg = NULL;

    if (sqlite3_exec(cache->db, sql, NULL, NULL, &errmsg) != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "Eingangs-Cache: %s", errmsg ? errmsg : "SQL-Fehler");
        sqlite3_free(errmsg);

        return FALSE;
    }

    return TRUE;
}

static gint get_user_version(sqlite3 *db) {
    sqlite3_stmt *stmt = NULL;
    gint version = 0;

    if (sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &stmt, NULL) ==
            SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            version = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }

    return version;
}

SondEingangCache* sond_eingang_cache_open(const gchar *db_path,
        const gchar *library_dir, GError **error) {
    SondEingangCache *cache = NULL;
    gint rc = 0;

    g_return_val_if_fail(db_path != NULL, NULL);
    g_return_val_if_fail(library_dir != NULL, NULL);

    cache = g_new0(SondEingangCache, 1);
    cache->library_dir = g_strdup(library_dir);
    cache->memo = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
            dir_ctx_free);

    rc = sqlite3_open(db_path, &cache->db);
    if (rc != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "Eingangs-Cache '%s' lässt sich nicht öffnen: %s", db_path,
                sqlite3_errmsg(cache->db));
        sond_eingang_cache_free(cache);

        return NULL;
    }

    //Nur ein Zwischenspeicher, aus den Zetteln jederzeit neu aufzubauen
    if (!exec_sql(cache, "PRAGMA synchronous = OFF;", error))
        goto fail;

    //Andere Version: verwerfen und aus den Zetteln neu aufbauen
    if (get_user_version(cache->db) != CACHE_FORMAT) {
        gchar *sql = g_strdup_printf("DROP TABLE IF EXISTS stempel; "
                "DROP TABLE IF EXISTS zettel; PRAGMA user_version = %d;",
                CACHE_FORMAT);
        gboolean ok = exec_sql(cache, sql, error);

        g_free(sql);
        if (!ok)
            goto fail;
    }

    if (!exec_sql(cache,
            "CREATE TABLE IF NOT EXISTS stempel ("
            "key TEXT PRIMARY KEY, "      /* vereinheitlichter Pfad */
            "pfad TEXT NOT NULL, "        /* Pfad wie im Zettel */
            "eingang INTEGER, "           /* JJJJMMTT, NULL bei entfernt */
            "art TEXT, "
            "ist_ordner INTEGER NOT NULL DEFAULT 0, "
            "zettel TEXT NOT NULL, "      /* Name des bestimmenden Zettels */
            "geloescht INTEGER NOT NULL DEFAULT 0); "
            "CREATE TABLE IF NOT EXISTS zettel (name TEXT PRIMARY KEY);",
            error))
        goto fail;

    return cache;

fail:
    sond_eingang_cache_free(cache);

    return NULL;
}

void sond_eingang_cache_free(SondEingangCache *cache) {
    if (!cache)
        return;

    if (cache->memo)
        g_hash_table_unref(cache->memo);
    if (cache->db)
        sqlite3_close(cache->db);
    g_free(cache->library_dir);
    g_free(cache);
}

/* ========================================================================
 * Zettel anwenden
 * ======================================================================== */

gboolean sond_eingang_cache_apply(SondEingangCache *cache,
        SondEingangZettel *zettel, const gchar *zettel_name, GError **error) {
    sqlite3_stmt *stmt = NULL;
    gboolean ok = TRUE;

    g_return_val_if_fail(cache != NULL, FALSE);
    g_return_val_if_fail(zettel != NULL, FALSE);
    g_return_val_if_fail(zettel_name != NULL, FALSE);

    if (!exec_sql(cache, "BEGIN;", error))
        return FALSE;

    /* Ein Eintrag gilt nur, wenn kein jüngerer Zettel schon etwas zu diesem
     * Pfad sagt (der Dateiname sortiert chronologisch). Ein später
     * eintreffender älterer Zettel kann so nichts mehr überschreiben. */
    if (sqlite3_prepare_v2(cache->db,
            "INSERT INTO stempel (key, pfad, eingang, art, ist_ordner, zettel, "
            "geloescht) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7) "
            "ON CONFLICT(key) DO UPDATE SET pfad = excluded.pfad, "
            "eingang = excluded.eingang, art = excluded.art, "
            "ist_ordner = excluded.ist_ordner, zettel = excluded.zettel, "
            "geloescht = excluded.geloescht "
            "WHERE stempel.zettel <= excluded.zettel;", -1, &stmt, NULL)
            != SQLITE_OK) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Eingangs-Cache: %s",
                sqlite3_errmsg(cache->db));
        exec_sql(cache, "ROLLBACK;", NULL);

        return FALSE;
    }

    for (guint i = 0; i < zettel->eintraege->len && ok; i++) {
        SondEingangEintrag *e = g_ptr_array_index(zettel->eintraege, i);
        gchar *key = make_key(e->pfad);

        if (*key) {
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);
            sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, e->pfad, -1, SQLITE_TRANSIENT);
            if (e->entfernen)
                sqlite3_bind_null(stmt, 3);
            else
                sqlite3_bind_int(stmt, 3, sond_eingang_datum_zu_int(e->eingang));
            if (e->art)
                sqlite3_bind_text(stmt, 4, e->art, -1, SQLITE_TRANSIENT);
            else
                sqlite3_bind_null(stmt, 4);
            sqlite3_bind_int(stmt, 5, e->ist_ordner ? 1 : 0);
            sqlite3_bind_text(stmt, 6, zettel_name, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 7, e->entfernen ? 1 : 0);

            if (sqlite3_step(stmt) != SQLITE_DONE) {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "Eingangs-Cache: %s", sqlite3_errmsg(cache->db));
                ok = FALSE;
            }
        }
        g_free(key);
    }
    sqlite3_finalize(stmt);

    if (ok) {
        sqlite3_stmt *stmt_z = NULL;

        if (sqlite3_prepare_v2(cache->db,
                "INSERT OR IGNORE INTO zettel (name) VALUES (?1);", -1, &stmt_z,
                NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt_z, 1, zettel_name, -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt_z);
            sqlite3_finalize(stmt_z);
        }
    }

    ok = ok && exec_sql(cache, "COMMIT;", error);
    if (!ok)
        exec_sql(cache, "ROLLBACK;", NULL);

    g_hash_table_remove_all(cache->memo);

    return ok;
}

static gboolean zettel_known(SondEingangCache *cache, const gchar *name) {
    sqlite3_stmt *stmt = NULL;
    gboolean known = FALSE;

    if (sqlite3_prepare_v2(cache->db, "SELECT 1 FROM zettel WHERE name = ?1;",
            -1, &stmt, NULL) != SQLITE_OK)
        return FALSE;

    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    known = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);

    return known;
}

gint sond_eingang_cache_reload(SondEingangCache *cache, guint *n_zettel,
        GError **error) {
    gchar *dir_path = NULL;
    SondDir *dir = NULL;
    GPtrArray *names = NULL;
    const gchar *name = NULL;
    guint applied = 0;

    g_return_val_if_fail(cache != NULL, -1);

    if (n_zettel)
        *n_zettel = 0;

    dir_path = g_build_filename(cache->library_dir, ".sond", "eingang", NULL);
    if (!sond_exists(dir_path)) { //noch nie etwas gestempelt
        g_free(dir_path);

        return 0;
    }

    dir = sond_dir_open(dir_path, NULL);
    if (!dir) {
        LOG_WARN("%s: '%s' lässt sich nicht lesen", __func__, dir_path);
        g_free(dir_path);

        return 0;
    }

    names = g_ptr_array_new_with_free_func(g_free);
    while ((name = sond_dir_read_name(dir)))
        if (g_str_has_suffix(name, ".json"))
            g_ptr_array_add(names, g_strdup(name));
    sond_dir_close(dir);

    //Dateinamen sortieren chronologisch (UTC-Zeit am Anfang)
    {
        GPtrArray *sorted = g_ptr_array_new_with_free_func(g_free);
        GList *list = NULL;

        for (guint i = 0; i < names->len; i++)
            list = g_list_prepend(list, g_ptr_array_index(names, i));
        list = g_list_sort(list, (GCompareFunc) g_strcmp0);
        for (GList *l = list; l; l = l->next)
            g_ptr_array_add(sorted, g_strdup(l->data));
        g_list_free(list);
        g_ptr_array_unref(names);
        names = sorted;
    }

    for (guint i = 0; i < names->len; i++) {
        const gchar *zettel_name = g_ptr_array_index(names, i);
        gchar *path = NULL;
        GError *err_read = NULL;
        SondEingangZettel *zettel = NULL;

        if (zettel_known(cache, zettel_name))
            continue;

        path = g_build_filename(dir_path, zettel_name, NULL);
        zettel = sond_eingang_zettel_read(path, &err_read);
        g_free(path);

        if (!zettel) {
            LOG_WARN("%s: Zettel '%s' nicht lesbar: %s", __func__, zettel_name,
                    err_read ? err_read->message : "?");
            g_clear_error(&err_read);

            continue;
        }

        if (!sond_eingang_cache_apply(cache, zettel, zettel_name, error)) {
            sond_eingang_zettel_free(zettel);
            g_ptr_array_unref(names);
            g_free(dir_path);

            return -1;
        }
        sond_eingang_zettel_free(zettel);
        applied++;
    }

    g_ptr_array_unref(names);
    g_free(dir_path);

    if (n_zettel)
        *n_zettel = applied;

    return 0;
}

/* ========================================================================
 * Abfrage
 * ======================================================================== */

/* Stempel des Pfads key oder des nächsten Vorfahren (0 = keiner); art (nullable)
 * bekommt die Art des bestimmenden Stempels */
static gint inherited_for(SondEingangCache *cache, const gchar *key,
        gchar **art) {
    sqlite3_stmt *stmt = NULL;
    gchar *current = g_strdup(key);
    gint result = 0;

    if (art)
        *art = NULL;

    if (sqlite3_prepare_v2(cache->db,
            "SELECT eingang, art FROM stempel WHERE key = ?1 AND geloescht = 0;",
            -1, &stmt, NULL) != SQLITE_OK) {
        g_free(current);

        return 0;
    }

    while (*current && !result) {
        gchar *parent = NULL;

        sqlite3_reset(stmt);
        sqlite3_bind_text(stmt, 1, current, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result = sqlite3_column_int(stmt, 0);
            if (art && sqlite3_column_text(stmt, 1))
                *art = g_strdup((const gchar*) sqlite3_column_text(stmt, 1));
        }

        parent = parent_of(current);
        g_free(current);
        current = parent;
    }
    sqlite3_finalize(stmt);
    g_free(current);

    return result;
}

static DirCtx* dir_ctx_build(SondEingangCache *cache, const gchar *parent_key) {
    DirCtx *ctx = g_new0(DirCtx, 1);
    sqlite3_stmt *stmt = NULL;
    gsize prefix_len = *parent_key ? strlen(parent_key) + 1 : 0;

    ctx->own = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    ctx->inherited = inherited_for(cache, parent_key, NULL);

    /* Eigene Stempel der direkten Kinder. Schlüssel unterhalb von "<Eltern>/"
     * liegen zwischen "<Eltern>/" und "<Eltern>0"; tiefere Einträge (weiterer
     * '/' im Rest) werden schon in SQL ausgeschlossen. */
    if (*parent_key) {
        gchar *lo = g_strconcat(parent_key, "/", NULL);
        gchar *hi = g_strconcat(parent_key, "0", NULL);

        if (sqlite3_prepare_v2(cache->db,
                "SELECT key, eingang FROM stempel WHERE key > ?1 AND key < ?2 "
                "AND instr(substr(key, ?3), '/') = 0 AND geloescht = 0;", -1,
                &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, lo, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, hi, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 3, (gint) prefix_len + 1);
        }
        g_free(lo);
        g_free(hi);
    } else if (sqlite3_prepare_v2(cache->db,
            "SELECT key, eingang FROM stempel WHERE instr(key, '/') = 0 AND "
            "geloescht = 0;", -1, &stmt, NULL) != SQLITE_OK)
        stmt = NULL;

    while (stmt && sqlite3_step(stmt) == SQLITE_ROW) {
        const gchar *key = (const gchar*) sqlite3_column_text(stmt, 0);
        gint datum = sqlite3_column_int(stmt, 1);

        if (!key || datum <= 0)
            continue;

        g_hash_table_insert(ctx->own, g_strdup(key), GINT_TO_POINTER(datum));
    }
    if (stmt)
        sqlite3_finalize(stmt);

    return ctx;
}

gboolean sond_eingang_cache_has_own(SondEingangCache *cache,
        const gchar *rel_path) {
    sqlite3_stmt *stmt = NULL;
    gchar *key = NULL;
    gboolean has = FALSE;

    g_return_val_if_fail(cache != NULL, FALSE);

    key = make_key(rel_path);
    if (*key && sqlite3_prepare_v2(cache->db,
            "SELECT 1 FROM stempel WHERE key = ?1 AND geloescht = 0;", -1,
            &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
        has = (sqlite3_step(stmt) == SQLITE_ROW);
        sqlite3_finalize(stmt);
    }
    g_free(key);

    return has;
}

/* Pfad ohne die ersten n Segmente ('/' oder '\\' als Trenner) */
static const gchar* skip_segments(const gchar *pfad, guint n) {
    const gchar *p = pfad;

    while (*p == '/' || *p == '\\')
        p++;
    while (n > 0 && *p) {
        while (*p && *p != '/' && *p != '\\')
            p++;
        while (*p == '/' || *p == '\\')
            p++;
        n--;
    }

    return p;
}

gint sond_eingang_cache_wirksam(SondEingangCache *cache, const gchar *rel_path,
        gchar **art) {
    gchar *key = NULL;
    gint result = 0;

    g_return_val_if_fail(cache != NULL, 0);

    key = make_key(rel_path);
    result = inherited_for(cache, key, art);
    g_free(key);

    return result;
}

guint sond_eingang_cache_uebertragen_zettel(SondEingangCache *cache,
        SondEingangZettel *zettel, const gchar *old_rel, const gchar *new_rel,
        gboolean alte_entfernen) {
    sqlite3_stmt *stmt = NULL;
    gchar *key = NULL;
    guint n_segments = 1;
    guint count = 0;

    g_return_val_if_fail(cache != NULL, 0);
    g_return_val_if_fail(zettel != NULL, 0);
    g_return_val_if_fail(old_rel != NULL && new_rel != NULL, 0);

    key = make_key(old_rel);
    if (!*key) {
        g_free(key);

        return 0;
    }
    for (const gchar *c = key; *c; c++)
        if (*c == '/')
            n_segments++;

    {
        gchar *lo = g_strconcat(key, "/", NULL);
        gchar *hi = g_strconcat(key, "0", NULL);

        if (sqlite3_prepare_v2(cache->db,
                "SELECT pfad, eingang, art, ist_ordner FROM stempel WHERE "
                "geloescht = 0 AND (key = ?1 OR (key > ?2 AND key < ?3)) "
                "ORDER BY key;", -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, lo, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, hi, -1, SQLITE_TRANSIENT);

            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const gchar *pfad = (const gchar*) sqlite3_column_text(stmt, 0);
                gint datum = sqlite3_column_int(stmt, 1);
                const gchar *art = (const gchar*) sqlite3_column_text(stmt, 2);
                gboolean ist_ordner = sqlite3_column_int(stmt, 3) != 0;
                const gchar *rest = NULL;
                gchar *neu = NULL;
                gchar *iso = NULL;

                if (!pfad || datum <= 0)
                    continue;

                rest = skip_segments(pfad, n_segments);
                neu = *rest ? g_strconcat(new_rel, "/", rest, NULL) :
                        g_strdup(new_rel);
                iso = g_strdup_printf("%04d-%02d-%02d", datum / 10000,
                        (datum / 100) % 100, datum % 100);

                //erst den alten Pfad aufheben, dann den neuen setzen (bei einer
                //Umbenennung nur der Groß-/Kleinschreibung ist es derselbe Schlüssel)
                if (alte_entfernen)
                    sond_eingang_zettel_add_entfernen(zettel, pfad);
                sond_eingang_zettel_add(zettel, neu, ist_ordner, iso, art, NULL, 0);
                count++;

                g_free(neu);
                g_free(iso);
            }
            sqlite3_finalize(stmt);
        }
        g_free(lo);
        g_free(hi);
    }
    g_free(key);

    return count;
}

GPtrArray* sond_eingang_cache_stempel_unter(SondEingangCache *cache,
        const gchar *rel_path) {
    GPtrArray *result = g_ptr_array_new_with_free_func(g_free);
    sqlite3_stmt *stmt = NULL;
    gchar *key = NULL;

    g_return_val_if_fail(cache != NULL, result);

    key = make_key(rel_path);
    if (*key) {
        gchar *lo = g_strconcat(key, "/", NULL);
        gchar *hi = g_strconcat(key, "0", NULL);

        //der Pfad selbst oder alles zwischen "<key>/" und "<key>0"
        if (sqlite3_prepare_v2(cache->db,
                "SELECT pfad FROM stempel WHERE geloescht = 0 AND "
                "(key = ?1 OR (key > ?2 AND key < ?3));", -1, &stmt, NULL)
                == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, lo, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, hi, -1, SQLITE_TRANSIENT);

            while (sqlite3_step(stmt) == SQLITE_ROW)
                g_ptr_array_add(result,
                        g_strdup((const gchar*) sqlite3_column_text(stmt, 0)));
            sqlite3_finalize(stmt);
        }
        g_free(lo);
        g_free(hi);
    }
    g_free(key);

    return result;
}

gint sond_eingang_cache_lookup(SondEingangCache *cache, const gchar *rel_path,
        gboolean is_dir) {
    gchar *key = NULL;
    gchar *parent = NULL;
    DirCtx *ctx = NULL;
    gpointer own = NULL;
    gint result = 0;

    g_return_val_if_fail(cache != NULL, 0);

    key = make_key(rel_path);
    if (!*key) {
        g_free(key);

        return 0;
    }

    parent = parent_of(key);
    ctx = g_hash_table_lookup(cache->memo, parent);
    if (!ctx) {
        if (g_hash_table_size(cache->memo) >= MEMO_MAX)
            g_hash_table_remove_all(cache->memo);
        ctx = dir_ctx_build(cache, parent);
        g_hash_table_insert(cache->memo, g_strdup(parent), ctx);
    }

    own = g_hash_table_lookup(ctx->own, key);
    if (own)
        result = GPOINTER_TO_INT(own);                 //eigener Stempel hat Vorrang
    else
        result = ctx->inherited;                       //sonst vom nächsten Ordner darüber

    g_free(parent);
    g_free(key);

    return result;
}
