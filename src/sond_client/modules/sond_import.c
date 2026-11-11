/*
 sond (sond_import.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_import.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifdef G_OS_WIN32
#include <windows.h>
#include <sddl.h>
#endif

#include "../../sond_file_helper.h"
#include "../../sond_log_and_error.h"

#define IMPORT_CHUNK_SIZE      (256 * 1024)
#define IMPORT_MAX_DEPTH       64
#define IMPORT_PROGRESS_USEC   (100 * 1000)

/* ========================================================================
 * SeaDrive-Verzeichnis
 * ======================================================================== */

#ifdef G_OS_WIN32

#define SYNCROOT_KEY L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\SyncRootManager"

/* SID des angemeldeten Benutzers als Text; die Sync-Wurzeln sind je SID
 * registriert (mehrere Benutzer desselben Rechners). */
static wchar_t* current_user_sid(void) {
    HANDLE token = NULL;
    DWORD len = 0;
    TOKEN_USER *tu = NULL;
    LPWSTR sid_str = NULL;
    wchar_t *result = NULL;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return NULL;

    GetTokenInformation(token, TokenUser, NULL, 0, &len);
    if (len == 0) {
        CloseHandle(token);
        return NULL;
    }

    tu = g_malloc(len);
    if (GetTokenInformation(token, TokenUser, tu, len, &len) &&
            ConvertSidToStringSidW(tu->User.Sid, &sid_str)) {
        result = g_memdup2(sid_str, (wcslen(sid_str) + 1) * sizeof(wchar_t));
        LocalFree(sid_str);
    }

    g_free(tu);
    CloseHandle(token);

    return result;
}

/* Liest den Pfad der Sync-Wurzel des Benutzers (Wert = SID) unter
 * <SyncRootManager>\<name>\UserSyncRoots. */
static gchar* read_user_sync_root(HKEY syncroots, const wchar_t *name,
        const wchar_t *sid) {
    HKEY key = NULL;
    wchar_t subpath[1024];
    DWORD type = 0;
    DWORD size = 0;
    wchar_t *value = NULL;
    gchar *utf8 = NULL;

    _snwprintf(subpath, G_N_ELEMENTS(subpath), L"%ls\\UserSyncRoots", name);
    subpath[G_N_ELEMENTS(subpath) - 1] = L'\0';
    if (RegOpenKeyExW(syncroots, subpath, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return NULL;

    if (RegQueryValueExW(key, sid, NULL, &type, NULL, &size) == ERROR_SUCCESS &&
            type == REG_SZ && size >= sizeof(wchar_t)) {
        value = g_malloc0(size + sizeof(wchar_t));
        if (RegQueryValueExW(key, sid, NULL, NULL, (LPBYTE) value, &size) ==
                ERROR_SUCCESS)
            utf8 = g_utf16_to_utf8((gunichar2*) value, -1, NULL, NULL, NULL);
        g_free(value);
    }

    RegCloseKey(key);

    return utf8;
}

#endif /* G_OS_WIN32 */

gchar* sond_import_detect_seadrive_root(const gchar *server_hint,
        GError **error) {
#ifdef G_OS_WIN32
    HKEY syncroots = NULL;
    wchar_t *sid = NULL;
    gchar *first = NULL;
    gchar *preferred = NULL;
    gchar *hint = NULL;
    DWORD index = 0;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, SYNCROOT_KEY, 0,
            KEY_READ | KEY_WOW64_64KEY, &syncroots) != ERROR_SUCCESS) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                "SeaDrive nicht gefunden (keine Cloud-Sync-Wurzeln "
                "registriert). Ist SeaDrive installiert und angemeldet?");
        return NULL;
    }

    sid = current_user_sid();
    if (!sid) {
        RegCloseKey(syncroots);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "Benutzerkennung konnte nicht ermittelt werden");
        return NULL;
    }

    if (server_hint && *server_hint) {
        hint = g_strdup(server_hint);
        while (*hint && hint[strlen(hint) - 1] == '/')
            hint[strlen(hint) - 1] = '\0';
    }

    for (;;) {
        wchar_t name[512];
        DWORD len = G_N_ELEMENTS(name);
        gchar *name_utf8 = NULL;
        gchar *path = NULL;

        if (RegEnumKeyExW(syncroots, index++, name, &len, NULL, NULL, NULL,
                NULL) != ERROR_SUCCESS)
            break;

        if (wcsncmp(name, L"seadrive!", 9) != 0)
            continue;

        path = read_user_sync_root(syncroots, name, sid);
        if (!path)
            continue;

        if (!g_file_test(path, G_FILE_TEST_IS_DIR)) {
            g_free(path);
            continue;
        }

        name_utf8 = g_utf16_to_utf8((gunichar2*) name, -1, NULL, NULL, NULL);
        if (!preferred && hint && *hint && name_utf8 && strstr(name_utf8, hint))
            preferred = g_strdup(path);
        if (!first)
            first = g_strdup(path);

        g_free(name_utf8);
        g_free(path);
    }

    RegCloseKey(syncroots);
    g_free(sid);
    g_free(hint);

    if (preferred) {
        g_free(first);
        return preferred;
    }
    if (first)
        return first;

    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
            "SeaDrive-Konto nicht gefunden. Ist SeaDrive angemeldet? "
            "Andernfalls den Pfad in der Konfiguration ([seadrive] root) "
            "angeben.");
    return NULL;
#else
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
            "Automatische SeaDrive-Erkennung gibt es nur unter Windows");
    return NULL;
#endif
}

gchar* sond_import_find_library_dir(const gchar *seadrive_root,
        const gchar *library_name, GError **error) {
    SondDir *dir = NULL;
    const gchar *cat = NULL;
    gchar *found = NULL;

    g_return_val_if_fail(seadrive_root != NULL, NULL);
    g_return_val_if_fail(library_name != NULL && *library_name, NULL);

    if (!g_file_test(seadrive_root, G_FILE_TEST_IS_DIR)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                "SeaDrive-Verzeichnis '%s' nicht vorhanden", seadrive_root);
        return NULL;
    }

    dir = sond_dir_open(seadrive_root, error);
    if (!dir)
        return NULL;

    /* Kategorien ("Meine Bibliotheken", "Für meine Gruppen", ...): die Namen
     * sind lokalisiert, deshalb werden alle durchsucht. Nur die Existenz von
     * <Kategorie>/<Library> wird geprüft, nichts wird aufgelistet. */
    while ((cat = sond_dir_read_name(dir))) {
        gchar *cat_path = g_build_filename(seadrive_root, cat, NULL);
        gchar *cand = g_build_filename(cat_path, library_name, NULL);

        g_free(cat_path);

        if (g_file_test(cand, G_FILE_TEST_IS_DIR)) {
            if (found) {
                g_free(cand);
                g_free(found);
                sond_dir_close(dir);
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS,
                        "Library '%s' ist im SeaDrive mehrfach vorhanden",
                        library_name);
                return NULL;
            }
            found = cand;
        } else
            g_free(cand);
    }

    sond_dir_close(dir);

    if (!found)
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                "Library '%s' ist im SeaDrive-Laufwerk (noch) nicht sichtbar",
                library_name);

    return found;
}

/* ========================================================================
 * Import
 * ======================================================================== */

typedef struct {
    gchar   *src;
    gchar   *rel;     /* relativ zum Ziel, '/' als Trenner, Namen bereinigt */
    gint64   size;
    gint64   mtime;   /* Änderungszeit der Quelle, wird auf die Kopie übertragen */
    gboolean is_dir;
    gboolean top;     /* Eintrag der obersten Ebene, wird in entries gemeldet */
    guint    source;  /* Index der Quelle, aus der der Eintrag stammt */
} ImportItem;

static void import_item_free(gpointer data) {
    ImportItem *item = data;

    g_free(item->src);
    g_free(item->rel);
    g_free(item);
}

static void import_entry_free(gpointer data) {
    SondImportEntry *entry = data;

    g_free(entry->rel);
    g_free(entry);
}

void sond_import_result_free(SondImportResult *result) {
    if (!result)
        return;

    if (result->skipped)
        g_ptr_array_unref(result->skipped);
    if (result->failed)
        g_ptr_array_unref(result->failed);
    if (result->entries)
        g_ptr_array_unref(result->entries);
    g_free(result);
}

static void add_entry(SondImportResult *result, const gchar *rel, guint source,
        gboolean is_dir, gint64 mtime, gint64 size) {
    SondImportEntry *entry = g_new0(SondImportEntry, 1);

    entry->rel = g_strdup(rel);
    entry->source = source;
    entry->is_dir = is_dir;
    entry->mtime = mtime;
    entry->size = size;
    g_ptr_array_add(result->entries, entry);
}

static void note(GPtrArray *arr, const gchar *rel, const gchar *reason) {
    g_ptr_array_add(arr, g_strdup_printf("%s - %s", rel, reason));
}

static void note_error(GPtrArray *arr, const gchar *rel, GError **error) {
    note(arr, rel, (error && *error) ? (*error)->message : "unbekannter Fehler");
    g_clear_error(error);
}

static gchar* rel_join(const gchar *prefix, const gchar *name) {
    return (prefix && *prefix) ? g_strconcat(prefix, "/", name, NULL) :
            g_strdup(name);
}

static void add_item(GPtrArray *items, const gchar *src, const gchar *rel,
        gint64 size, gint64 mtime, gboolean is_dir) {
    ImportItem *item = g_new0(ImportItem, 1);

    item->src = g_strdup(src);
    item->rel = g_strdup(rel);
    item->size = size;
    item->mtime = mtime;
    item->is_dir = is_dir;
    g_ptr_array_add(items, item);
}

/* Fügt einen Eintrag (Datei oder Verzeichnis) hinzu bzw. vermerkt, warum
 * nicht. Verzeichnisse werden rekursiv eingelesen. */
static void scan_entry(const gchar *src, const gchar *name,
        const gchar *rel_prefix, gint depth, GPtrArray *items,
        SondImportResult *result);

static void scan_dir(const gchar *src_dir, const gchar *rel_prefix, gint depth,
        GPtrArray *items, SondImportResult *result) {
    GError *error = NULL;
    SondDir *dir = NULL;
    GPtrArray *names = NULL;
    const gchar *name = NULL;

    if (depth > IMPORT_MAX_DEPTH) {
        note(result->failed, rel_prefix, "Verzeichnisse zu tief verschachtelt");
        return;
    }

    dir = sond_dir_open(src_dir, &error);
    if (!dir) {
        note_error(result->failed, (rel_prefix && *rel_prefix) ? rel_prefix : src_dir,
                &error);
        return;
    }

    /* Namen zuerst sammeln: der von sond_dir_read_name() gelieferte String
     * gehört dem Verzeichnis-Handle, und Rekursion hält sonst viele offen. */
    names = g_ptr_array_new_with_free_func(g_free);
    while ((name = sond_dir_read_name(dir)))
        g_ptr_array_add(names, g_strdup(name));
    sond_dir_close(dir);

    for (guint i = 0; i < names->len; i++)
        scan_entry(src_dir, g_ptr_array_index(names, i), rel_prefix, depth,
                items, result);

    g_ptr_array_unref(names);
}

static void scan_entry(const gchar *src_dir, const gchar *name,
        const gchar *rel_prefix, gint depth, GPtrArray *items,
        SondImportResult *result) {
    GError *error = NULL;
    GStatBuf st;
    gchar *path = NULL;
    gchar *clean = NULL;
    gchar *rel = NULL;
    gchar *rel_orig = rel_join(rel_prefix, name);

    if (name[0] == '.') {
        note(result->skipped, rel_orig, "Name beginnt mit einem Punkt");
        g_free(rel_orig);
        return;
    }

    path = g_build_filename(src_dir, name, NULL);

    if (sond_stat(path, &st, &error) != 0) {
        note_error(result->failed, rel_orig, &error);
        g_free(path);
        g_free(rel_orig);
        return;
    }

    clean = sond_sanitize_filename(name);
    rel = rel_join(rel_prefix, clean);

    if (S_ISDIR(st.st_mode)) {
        add_item(items, path, rel, 0, 0, TRUE);
        scan_dir(path, rel, depth + 1, items, result);
    } else if (S_ISREG(st.st_mode))
        add_item(items, path, rel, (gint64) st.st_size, (gint64) st.st_mtime, FALSE);
    else
        note(result->skipped, rel_orig, "keine reguläre Datei");

    g_free(clean);
    g_free(rel);
    g_free(path);
    g_free(rel_orig);
}

typedef struct {
    SondImportProgress progress;
    SondImportProgressFunc func;
    gpointer user_data;
    gint64 last_report;
} ProgressState;

static void report(ProgressState *ps, gboolean force) {
    gint64 now = g_get_monotonic_time();

    if (!ps->func)
        return;
    if (!force && now - ps->last_report < IMPORT_PROGRESS_USEC)
        return;

    ps->last_report = now;
    ps->func(&ps->progress, ps->user_data);
}

/* Kopiert eine Datei blockweise (Abbruch zwischen den Blöcken). Bei Fehler
 * oder Abbruch wird die angefangene Zieldatei wieder entfernt.
 * Rückgabe: 0 Erfolg, 1 abgebrochen, -1 Fehler. */
static gint copy_file(const gchar *src, const gchar *dest, gint64 mtime,
        ProgressState *ps, const gint *cancel, GError **error) {
    FILE *in = NULL;
    FILE *out = NULL;
    guchar *buf = NULL;
    gint rc = 0;
    size_t n = 0;

    in = sond_fopen(src, "rb", error);
    if (!in)
        return -1;

    out = sond_fopen(dest, "wb", error);
    if (!out) {
        fclose(in);
        return -1;
    }

    buf = g_malloc(IMPORT_CHUNK_SIZE);

    while ((n = fread(buf, 1, IMPORT_CHUNK_SIZE, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "Fehler beim Schreiben: %s", g_strerror(errno));
            rc = -1;
            break;
        }

        ps->progress.bytes_done += (gint64) n;
        report(ps, FALSE);

        if (cancel && g_atomic_int_get(cancel)) {
            rc = 1;
            break;
        }
    }

    if (rc == 0 && ferror(in)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "Fehler beim Lesen: %s", g_strerror(errno));
        rc = -1;
    }

    /* Änderungszeit der Quelle übernehmen, vor dem Schließen (eine einzige
     * Änderung für SeaDrive); ein Fehlschlag ist nicht kritisch */
    if (rc == 0) {
        GError *err_time = NULL;

        if (!sond_set_mtime(out, mtime, &err_time))
            LOG_WARN("Änderungszeit von '%s' nicht gesetzt: %s\n", dest,
                    err_time ? err_time->message : "?");
        g_clear_error(&err_time);
    }

    fclose(in);
    if (fclose(out) != 0 && rc == 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                "Fehler beim Schreiben: %s", g_strerror(errno));
        rc = -1;
    }
    g_free(buf);

    if (rc != 0) {
        GError *err_rm = NULL;

        if (!sond_remove(dest, &err_rm))
            LOG_WARN("Angefangene Datei '%s' ließ sich nicht entfernen: %s\n",
                    dest, err_rm ? err_rm->message : "?");
        g_clear_error(&err_rm);
    }

    return rc;
}

/* Freien Zielnamen im Verzeichnis wählen: bei Namensgleichheit " (n)" vor der
 * Endung. *counter liefert n (0 = Name unverändert). */
static gchar* unique_dest(const gchar *dest_dir, const gchar *base,
        guint *counter) {
    const gchar *dot = strrchr(base, '.');
    gboolean has_ext = dot && dot != base;
    guint n = 0;

    for (;;) {
        gchar *name = (n == 0) ? g_strdup(base) :
                sond_filename_add_counter(base, n, has_ext);
        gchar *full = g_build_filename(dest_dir, name, NULL);

        g_free(name);
        if (!sond_exists(full)) {
            *counter = n;
            return full;
        }

        g_free(full);
        n++;
    }
}

/* Freier Name für einen Ordner in der Wurzel: nicht schon in target_dir und
 * nicht schon in diesem Lauf vergeben; bei Gleichheit " (n)". Die Namen
 * gehören used. */
static gchar* unique_top_name(const gchar *target_dir, const gchar *name,
        GHashTable *used) {
    guint n = 0;

    for (;;) {
        gchar *trial = (n == 0) ? g_strdup(name) :
                sond_filename_add_counter(name, n, FALSE);
        gchar *full = g_build_filename(target_dir, trial, NULL);
        gboolean taken = sond_exists(full) || g_hash_table_contains(used, trial);

        g_free(full);
        if (!taken) {
            g_hash_table_add(used, g_strdup(trial));

            return trial;
        }

        g_free(trial);
        n++;
    }
}

SondImportResult* sond_import_copy(GPtrArray *sources, const gchar *target_dir,
        SondImportProgressFunc progress_func, gpointer user_data,
        const gint *cancel, GError **error) {
    SondImportResult *result = NULL;
    GPtrArray *items = NULL;
    GHashTable *used_top = NULL;
    ProgressState ps = { 0 };

    g_return_val_if_fail(sources != NULL, NULL);
    g_return_val_if_fail(target_dir != NULL, NULL);

    if (!g_file_test(target_dir, G_FILE_TEST_IS_DIR)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                "Zielverzeichnis '%s' nicht vorhanden", target_dir);
        return NULL;
    }

    ps.func = progress_func;
    ps.user_data = user_data;

    result = g_new0(SondImportResult, 1);
    result->skipped = g_ptr_array_new_with_free_func(g_free);
    result->failed = g_ptr_array_new_with_free_func(g_free);
    result->entries = g_ptr_array_new_with_free_func(import_entry_free);

    used_top = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    /* 1. Quellen einlesen */
    items = g_ptr_array_new_with_free_func(import_item_free);
    for (guint i = 0; i < sources->len; i++) {
        const SondImportSource *source = g_ptr_array_index(sources, i);
        const gchar *src = source->path;
        GError *err = NULL;
        GStatBuf st;

        if (sond_stat(src, &st, &err) != 0) {
            note_error(result->failed, src, &err);
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            //Ordner als Ordner: eigener Eintrag der obersten Ebene
            gchar *base = g_path_get_basename(src);
            gboolean custom = source->folder_name && *source->folder_name;
            gchar *clean = NULL;
            gchar *top = NULL;

            if (!custom && base[0] == '.') {
                note(result->skipped, base, "Name beginnt mit einem Punkt");
                g_free(base);
                continue;
            }

            clean = sond_sanitize_filename(custom ? source->folder_name : base);
            top = unique_top_name(target_dir, clean, used_top);
            add_item(items, src, top, 0, 0, TRUE);
            ((ImportItem*) g_ptr_array_index(items, items->len - 1))->top = TRUE;
            ((ImportItem*) g_ptr_array_index(items, items->len - 1))->source = i;
            scan_dir(src, top, 0, items, result);

            g_free(top);
            g_free(clean);
            g_free(base);
        } else {
            gchar *base = g_path_get_basename(src);
            gchar *dir = g_path_get_dirname(src);
            guint before = items->len;

            scan_entry(dir, base, "", 0, items, result);
            //lose Datei: Eintrag der obersten Ebene (Name steht erst beim Kopieren fest)
            if (items->len > before) {
                ((ImportItem*) g_ptr_array_index(items, before))->top = TRUE;
                ((ImportItem*) g_ptr_array_index(items, before))->source = i;
            }
            g_free(base);
            g_free(dir);
        }
    }

    for (guint i = 0; i < items->len; i++) {
        ImportItem *item = g_ptr_array_index(items, i);

        if (!item->is_dir) {
            ps.progress.files_total++;
            ps.progress.bytes_total += item->size;
        }
    }

    /* 2. Kopieren */
    for (guint i = 0; i < items->len; i++) {
        ImportItem *item = g_ptr_array_index(items, i);
        GError *err = NULL;
        gchar *dest = NULL;
        gchar *dest_dir = NULL;
        gchar *base = NULL;
        guint counter = 0;
        gint rc = 0;

        if (cancel && g_atomic_int_get(cancel)) {
            result->cancelled = TRUE;
            break;
        }

        dest = g_build_filename(target_dir, item->rel, NULL);

        if (item->is_dir) {
            gboolean ok = FALSE;

            if (sond_exists(dest)) {
                if (!g_file_test(dest, G_FILE_TEST_IS_DIR))
                    note(result->failed, item->rel,
                            "Ziel existiert bereits als Datei");
                else
                    ok = TRUE;
            } else if (!sond_mkdir_with_parents(dest, &err))
                note_error(result->failed, item->rel, &err);
            else
                ok = TRUE;

            if (ok && item->top)
                add_entry(result, item->rel, item->source, TRUE, 0, 0);

            g_free(dest);
            continue;
        }

        ps.progress.current = item->rel;
        report(&ps, TRUE);

        dest_dir = g_path_get_dirname(dest);
        base = g_path_get_basename(dest);
        g_free(dest);

        if (!sond_mkdir_with_parents(dest_dir, &err)) {
            note_error(result->failed, item->rel, &err);
            ps.progress.files_done++;
            ps.progress.bytes_done += item->size;
            g_free(dest_dir);
            g_free(base);
            continue;
        }

        dest = unique_dest(dest_dir, base, &counter);
        g_free(dest_dir);
        g_free(base);

        {
            gint64 before = ps.progress.bytes_done;

            rc = copy_file(item->src, dest, item->mtime, &ps, cancel, &err);
            if (rc == 0) {
                result->copied++;
                if (counter > 0)
                    result->renamed++;
                if (item->top) { //lose Datei: endgültiger Name in der Wurzel
                    gchar *final_name = g_path_get_basename(dest);

                    add_entry(result, final_name, item->source, FALSE,
                            item->mtime, item->size);
                    g_free(final_name);
                }
            } else if (rc > 0) {
                result->cancelled = TRUE;
            } else
                note_error(result->failed, item->rel, &err);

            //Bytezähler auch bei Fehlern/Auslassungen auf den Sollwert bringen
            ps.progress.bytes_done = before + item->size;
        }

        ps.progress.files_done++;
        g_free(dest);

        if (rc > 0)
            break;
    }

    ps.progress.current = NULL;
    report(&ps, TRUE);

    g_ptr_array_unref(items);
    g_hash_table_unref(used_top);

    return result;
}
