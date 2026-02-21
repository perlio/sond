/*
 sond (sond_file_helper.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_file_helper.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#ifdef G_OS_WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <wchar.h>


/* Hilfsfunktion: Konvertiert UTF-8-Pfad zu Wide-String mit \\?\ Prefix */
static wchar_t*
prepare_long_path(const gchar *path, GError **error)
{
    wchar_t *wpath = NULL;
    wchar_t abs_wpath[32768];
    wchar_t *long_path = NULL;

    /* UTF-8 → UTF-16 */
    wpath = g_utf8_to_utf16(path, -1, NULL, NULL, NULL);
    if (!wpath) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME,
                    "Ungültige UTF-8-Kodierung");
        return NULL;
    }

    /* Zu absolutem Pfad machen, falls relativ */
    DWORD len = GetFullPathNameW(wpath, 32768, abs_wpath, NULL);
    g_free(wpath);

    if (len == 0 || len >= 32768) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "GetFullPathName fehlgeschlagen: %lu", GetLastError());
        return NULL;
    }

    /* \\?\ Prefix hinzufügen */
    long_path = g_new(wchar_t, wcslen(abs_wpath) + 5);
    swprintf(long_path, wcslen(abs_wpath) + 5, L"\\\\?\\%ls", abs_wpath);

    return long_path;
}

/* Hilfsfunktion: Windows-Fehler zu GIOErrorEnum */
static GIOErrorEnum
g_io_error_from_win32(DWORD win_error)
{
    switch (win_error) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            return G_IO_ERROR_NOT_FOUND;

        case ERROR_ACCESS_DENIED:
            return G_IO_ERROR_PERMISSION_DENIED;

        case ERROR_ALREADY_EXISTS:
        case ERROR_FILE_EXISTS:
            return G_IO_ERROR_EXISTS;

        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
            return G_IO_ERROR_BUSY;

        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:
            return G_IO_ERROR_NO_SPACE;

        case ERROR_DISK_FULL:
            return G_IO_ERROR_NO_SPACE;

        case ERROR_INVALID_NAME:
        case ERROR_BAD_PATHNAME:
            return G_IO_ERROR_INVALID_FILENAME;

        case ERROR_TOO_MANY_OPEN_FILES:
            return G_IO_ERROR_TOO_MANY_OPEN_FILES;

        case ERROR_DIRECTORY:
            return G_IO_ERROR_IS_DIRECTORY;

        case ERROR_NOT_A_REPARSE_POINT:
        case ERROR_INVALID_REPARSE_DATA:
            return G_IO_ERROR_NOT_SYMBOLIC_LINK;

        default:
            return G_IO_ERROR_FAILED;
    }
}

/* Hilfsfunktion: Windows-Fehler zu GError */
static void
set_error_from_win32(GError **error, DWORD win_error)
{
    gchar *msg;
    GIOErrorEnum io_error;

    msg = g_win32_error_message(win_error);
    io_error = g_io_error_from_win32(win_error);

    g_set_error(error, G_IO_ERROR, io_error, "%s", msg);
    g_free(msg);
}
#endif /* G_OS_WIN32 */

gboolean
sond_mkdir(const gchar *path, GError **error)
{
    g_return_val_if_fail(path != NULL, FALSE);

#ifdef G_OS_WIN32
    wchar_t *long_path;
    BOOL success;

    long_path = prepare_long_path(path, error);
    if (!long_path)
        return FALSE;

    success = CreateDirectoryW(long_path, NULL);

    if (!success) {
        DWORD win_error = GetLastError();
		set_error_from_win32(error, win_error);
		g_free(long_path);
		return FALSE;
    }

    g_free(long_path);
    return TRUE;
#else
    if (g_mkdir(path, 0755) != 0) {
		g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
					"%s", g_strerror(errno));
		return FALSE;
    }
    return TRUE;
#endif
}

gboolean
sond_exists(const gchar *path)
{
    g_return_val_if_fail(path != NULL, FALSE);

#ifdef G_OS_WIN32
    wchar_t *long_path;
    DWORD attrs;

    long_path = prepare_long_path(path, NULL);
    if (!long_path)
        return FALSE;

    attrs = GetFileAttributesW(long_path);
    g_free(long_path);

    return (attrs != INVALID_FILE_ATTRIBUTES);
#else
    return g_file_test(path, G_FILE_TEST_EXISTS);
#endif
}

gboolean
sond_mkdir_with_parents(const gchar *path, GError **error)
{
    gchar *parent;

    g_return_val_if_fail(path != NULL, FALSE);

    /* Prüfe ob bereits existiert */
    if (sond_exists(path))
        return TRUE;

    /* Erstelle Parent rekursiv */
    parent = g_path_get_dirname(path);

    if (g_strcmp0(parent, ".") != 0 && g_strcmp0(parent, path) != 0) {
        if (!sond_mkdir_with_parents(parent, error)) {
            g_free(parent);
            return FALSE;
        }
    }

    g_free(parent);

    /* Erstelle dieses Verzeichnis */
    return sond_mkdir(path, error);
}

gboolean
sond_remove(const gchar *path, GError **error)
{
    g_return_val_if_fail(path != NULL, FALSE);

#ifdef G_OS_WIN32
    wchar_t *long_path;
    BOOL success;

    long_path = prepare_long_path(path, error);
    if (!long_path)
        return FALSE;

    success = DeleteFileW(long_path);

    if (!success) {
        set_error_from_win32(error, GetLastError());
        g_free(long_path);
        return FALSE;
    }

    g_free(long_path);
    return TRUE;
#else
    if (g_remove(path) != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "%s", g_strerror(errno));
        return FALSE;
    }
    return TRUE;
#endif
}

gboolean
sond_rmdir(const gchar *path, GError **error)
{
    g_return_val_if_fail(path != NULL, FALSE);

#ifdef G_OS_WIN32
    wchar_t *long_path;
    BOOL success;

    long_path = prepare_long_path(path, error);
    if (!long_path)
        return FALSE;

    success = RemoveDirectoryW(long_path);

    if (!success) {
        set_error_from_win32(error, GetLastError());
        g_free(long_path);
        return FALSE;
    }

    g_free(long_path);
    return TRUE;
#else
    if (g_rmdir(path) != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "%s", g_strerror(errno));
        return FALSE;
    }
    return TRUE;
#endif
}

gboolean
sond_rmdir_r(const gchar *path, GError **error)
{
    SondDir *dir;
    const gchar *name;
    gboolean success = TRUE;

    g_return_val_if_fail(path != NULL, FALSE);

    /* Öffne Verzeichnis */
    dir = sond_dir_open(path, error);
    if (!dir)
        return FALSE;

    /* Iteriere über alle Einträge */
    while ((name = sond_dir_read_name(dir)) != NULL) {
        gchar *fullpath = g_build_filename(path, name, NULL);
        GStatBuf st;

        /* Hole Datei-Info */
        if (sond_stat(fullpath, &st, error) != 0) {
            success = FALSE;
            g_free(fullpath);
            break;
        }

        /* Rekursiv löschen wenn Verzeichnis */
        if (S_ISDIR(st.st_mode)) {
            if (!sond_rmdir_r(fullpath, error)) {
                success = FALSE;
                g_free(fullpath);
                break;
            }
        } else {
            /* Datei löschen */
            if (!sond_remove(fullpath, error)) {
                success = FALSE;
                g_free(fullpath);
                break;
            }
        }

        g_free(fullpath);
    }

    sond_dir_close(dir);

    /* Lösche das Verzeichnis selbst */
    if (success) {
        if (!sond_rmdir(path, error)) {
            success = FALSE;
        }
    }

    return success;
}

FILE*
sond_fopen(const gchar *path, const gchar *mode, GError **error)
{
    g_return_val_if_fail(path != NULL, NULL);
    g_return_val_if_fail(mode != NULL, NULL);

#ifdef G_OS_WIN32
    wchar_t *long_path;
    wchar_t *wmode;
    FILE *file;

    long_path = prepare_long_path(path, error);
    if (!long_path)
        return NULL;

    /* Mode auch zu Wide-String */
    wmode = g_utf8_to_utf16(mode, -1, NULL, NULL, NULL);
    if (!wmode) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                    "Ungültiger Modus");
        g_free(long_path);
        return NULL;
    }

    file = _wfopen(long_path, wmode);

    if (!file) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "%s", g_strerror(errno));
    }

    g_free(long_path);
    g_free(wmode);

    return file;
#else
    FILE *file = g_fopen(path, mode);
    if (!file) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "%s", g_strerror(errno));
    }
    return file;
#endif
}

gint
sond_stat(const gchar *path, GStatBuf *buf, GError **error)
{
    g_return_val_if_fail(path != NULL, -1);
    g_return_val_if_fail(buf != NULL, -1);

#ifdef G_OS_WIN32
    wchar_t *long_path;
    struct _stat64 st;
    gint result;

    long_path = prepare_long_path(path, error);
    if (!long_path)
        return -1;

    result = _wstat64(long_path, &st);

    if (result != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "%s", g_strerror(errno));
        g_free(long_path);
        return -1;
    }

    /* Kopiere relevante Felder zu GStatBuf */
    buf->st_mode = st.st_mode;
    buf->st_size = st.st_size;
    buf->st_atime = st.st_atime;
    buf->st_mtime = st.st_mtime;
    buf->st_ctime = st.st_ctime;

    g_free(long_path);
    return 0;
#else
    if (g_stat(path, buf) != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "%s", g_strerror(errno));
        return -1;
    }
    return 0;
#endif
}

gboolean
sond_rename(const gchar *oldpath, const gchar *newpath, GError **error)
{
    g_return_val_if_fail(oldpath != NULL, FALSE);
    g_return_val_if_fail(newpath != NULL, FALSE);

#ifdef G_OS_WIN32
    wchar_t *long_oldpath;
    wchar_t *long_newpath;
    BOOL success;

    long_oldpath = prepare_long_path(oldpath, error);
    if (!long_oldpath)
        return FALSE;

    long_newpath = prepare_long_path(newpath, error);
    if (!long_newpath) {
        g_free(long_oldpath);
        return FALSE;
    }

    success = MoveFileW(long_oldpath, long_newpath);

    if (!success) {
        set_error_from_win32(error, GetLastError());
        g_free(long_oldpath);
        g_free(long_newpath);
        return FALSE;
    }

    g_free(long_oldpath);
    g_free(long_newpath);
    return TRUE;
#else
    if (g_rename(oldpath, newpath) != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "%s", g_strerror(errno));
        return FALSE;
    }
    return TRUE;
#endif
}

gboolean
sond_copy(const gchar *source, const gchar *dest, gboolean overwrite, GError **error)
{
    g_return_val_if_fail(source != NULL, FALSE);
    g_return_val_if_fail(dest != NULL, FALSE);

#ifdef G_OS_WIN32
    wchar_t *long_source;
    wchar_t *long_dest;
    BOOL success;

    long_source = prepare_long_path(source, error);
    if (!long_source)
        return FALSE;

    long_dest = prepare_long_path(dest, error);
    if (!long_dest) {
        g_free(long_source);
        return FALSE;
    }

    /* CopyFileW: letzter Parameter FALSE = überschreibe nicht (fail wenn existiert) */
    success = CopyFileW(long_source, long_dest, !overwrite);

    if (!success) {
        set_error_from_win32(error, GetLastError());
        g_free(long_source);
        g_free(long_dest);
        return FALSE;
    }

    g_free(long_source);
    g_free(long_dest);
    return TRUE;
#else
    FILE *src_file = NULL;
    FILE *dst_file = NULL;
    gchar buffer[8192];
    size_t bytes_read;
    gboolean result = FALSE;

    /* Prüfe ob Ziel existiert und overwrite=FALSE */
    if (!overwrite && g_file_test(dest, G_FILE_TEST_EXISTS)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS,
                    "Zieldatei existiert bereits");
        return FALSE;
    }

    src_file = fopen(source, "rb");
    if (!src_file) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Konnte Quelldatei nicht öffnen: %s", g_strerror(errno));
        return FALSE;
    }

    dst_file = fopen(dest, "wb");
    if (!dst_file) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Konnte Zieldatei nicht erstellen: %s", g_strerror(errno));
        fclose(src_file);
        return FALSE;
    }

    /* Kopiere in Blöcken */
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), src_file)) > 0) {
        if (fwrite(buffer, 1, bytes_read, dst_file) != bytes_read) {
            g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                        "Fehler beim Schreiben: %s", g_strerror(errno));
            goto cleanup;
        }
    }

    if (ferror(src_file)) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Fehler beim Lesen: %s", g_strerror(errno));
        goto cleanup;
    }

    result = TRUE;

cleanup:
    if (src_file) fclose(src_file);
    if (dst_file) fclose(dst_file);

    return result;
#endif
}

gboolean
sond_copy_r(const gchar *source, const gchar *dest, gboolean overwrite, GError **error)
{
    SondDir *dir;
    const gchar *name;
    gboolean success = TRUE;
    GStatBuf st;

    g_return_val_if_fail(source != NULL, FALSE);
    g_return_val_if_fail(dest != NULL, FALSE);

    /* Hole Info über Quelle */
    if (sond_stat(source, &st, error) != 0) {
        return FALSE;
    }

    /* Falls Quelle eine Datei ist, einfach kopieren */
    if (!S_ISDIR(st.st_mode)) {
        return sond_copy(source, dest, overwrite, error);
    }

    /* Quelle ist ein Verzeichnis - erstelle Zielverzeichnis */
    if (sond_exists(dest)) {
        if (!overwrite) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS,
                        "Zielverzeichnis existiert bereits");
            return FALSE;
        }
    } else {
        if (!sond_mkdir(dest, error)) {
            return FALSE;
        }
    }

    /* Öffne Quellverzeichnis */
    dir = sond_dir_open(source, error);
    if (!dir)
        return FALSE;

    /* Iteriere über alle Einträge */
    while ((name = sond_dir_read_name(dir)) != NULL) {
        gchar *source_path = g_build_filename(source, name, NULL);
        gchar *dest_path = g_build_filename(dest, name, NULL);
        GStatBuf entry_st;

        /* Hole Info über Eintrag */
        if (sond_stat(source_path, &entry_st, error) != 0) {
            success = FALSE;
            g_free(source_path);
            g_free(dest_path);
            break;
        }

        /* Rekursiv kopieren wenn Verzeichnis */
        if (S_ISDIR(entry_st.st_mode)) {
            if (!sond_copy_r(source_path, dest_path, overwrite, error)) {
                success = FALSE;
                g_free(source_path);
                g_free(dest_path);
                break;
            }
        } else {
            /* Datei kopieren */
            if (!sond_copy(source_path, dest_path, overwrite, error)) {
                success = FALSE;
                g_free(source_path);
                g_free(dest_path);
                break;
            }
        }

        g_free(source_path);
        g_free(dest_path);
    }

    sond_dir_close(dir);

    return success;
}

gint
sond_open_read(const gchar *path, GError **error)
{
    g_return_val_if_fail(path != NULL, -1);

#ifdef G_OS_WIN32
    wchar_t *long_path;
    HANDLE h;
    gint fd;

    long_path = prepare_long_path(path, error);
    if (!long_path)
        return -1;

    h = CreateFileW(long_path,
                    GENERIC_READ,
                    FILE_SHARE_READ,
                    NULL,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL,
                    NULL);

    if (h == INVALID_HANDLE_VALUE) {
        set_error_from_win32(error, GetLastError());
        g_free(long_path);
        return -1;
    }

    /* Windows-Handle zu C-File-Descriptor */
    fd = _open_osfhandle((intptr_t)h, _O_RDONLY);

    if (fd == -1) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "_open_osfhandle fehlgeschlagen");
        CloseHandle(h);
        g_free(long_path);
        return -1;
    }

    g_free(long_path);
    return fd;
#else
    gint fd = g_open(path, O_RDONLY, 0);
    if (fd == -1) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "%s", g_strerror(errno));
    }
    return fd;
#endif
}

/* Verzeichnis-Iteration */
#ifdef G_OS_WIN32
struct _SondDir {
    HANDLE handle;
    WIN32_FIND_DATAW find_data;
    gchar *utf8_name;
    gboolean first;
};
#else
struct _SondDir {
    GDir *gdir;
};
#endif

SondDir*
sond_dir_open(const gchar *path, GError **error)
{
    SondDir *dir;

    g_return_val_if_fail(path != NULL, NULL);

    dir = g_new0(SondDir, 1);

#ifdef G_OS_WIN32
    wchar_t *long_path;
    wchar_t search_path[32768];

    long_path = prepare_long_path(path, error);
    if (!long_path) {
        g_free(dir);
        return NULL;
    }

    /* Entferne trailing Backslash falls vorhanden */
    size_t path_len;
    path_len = wcslen(long_path);
    if (path_len > 0 && long_path[path_len - 1] == L'\\') {
        long_path[path_len - 1] = L'\0';
    }

    /* Füge \* für FindFirstFile hinzu */
    swprintf(search_path, 32768, L"%ls\\*", long_path);
    g_free(long_path);

    dir->handle = FindFirstFileW(search_path, &dir->find_data);

    if (dir->handle == INVALID_HANDLE_VALUE) {
        set_error_from_win32(error, GetLastError());
        g_free(dir);
        return NULL;
    }

    dir->first = TRUE;
    dir->utf8_name = NULL;

    return dir;
#else
    dir->gdir = g_dir_open(path, 0, error);
    if (!dir->gdir) {
        g_free(dir);
        return NULL;
    }
    return dir;
#endif
}

const gchar*
sond_dir_read_name(SondDir *dir)
{
    g_return_val_if_fail(dir != NULL, NULL);

#ifdef G_OS_WIN32
    BOOL success;

    /* Beim ersten Aufruf haben wir schon Daten von FindFirstFile */
    if (dir->first) {
        dir->first = FALSE;
    } else {
        success = FindNextFileW(dir->handle, &dir->find_data);
        if (!success) {
            return NULL;
        }
    }

    /* Überspringe . und .. */
    if (wcscmp(dir->find_data.cFileName, L".") == 0 ||
        wcscmp(dir->find_data.cFileName, L"..") == 0) {
        return sond_dir_read_name(dir);  /* Rekursiv zum nächsten */
    }

    /* Konvertiere Wide-String zu UTF-8 */
    g_free(dir->utf8_name);
    dir->utf8_name = g_utf16_to_utf8((gunichar2*)dir->find_data.cFileName,
                                      -1, NULL, NULL, NULL);

    return dir->utf8_name;
#else
    return g_dir_read_name(dir->gdir);
#endif
}

void
sond_dir_close(SondDir *dir)
{
    if (!dir)
        return;

#ifdef G_OS_WIN32
    if (dir->handle != INVALID_HANDLE_VALUE) {
        FindClose(dir->handle);
    }
    g_free(dir->utf8_name);
#else
    if (dir->gdir) {
        g_dir_close(dir->gdir);
    }
#endif

    g_free(dir);
}

gboolean
sond_open(const gchar *path, gboolean open_with, GError **error)
{
    g_return_val_if_fail(path != NULL, FALSE);

#ifdef G_OS_WIN32
    wchar_t *long_path;
    SHELLEXECUTEINFOW sei = { 0 };
    BOOL ret;

    long_path = prepare_long_path(path, error);
    if (!long_path)
        return FALSE;

    sei.cbSize = sizeof(sei);
    sei.nShow = SW_SHOWNORMAL;
    sei.lpVerb = open_with ? L"openas" : L"open";
    sei.lpFile = long_path;
    sei.fMask = SEE_MASK_INVOKEIDLIST;

    ret = ShellExecuteExW(&sei);
    g_free(long_path);

    if (!ret) {
        set_error_from_win32(error, GetLastError());
        return FALSE;
    }

    return TRUE;
#else
    gchar *argv[3];
    gboolean ret;

    argv[0] = "xdg-open";
    argv[1] = (gchar *)path;
    argv[2] = NULL;

    ret = g_spawn_async(NULL, argv, NULL,
                        G_SPAWN_SEARCH_PATH,
                        NULL, NULL, NULL, error);

    return ret;
#endif
}
