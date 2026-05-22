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
#include <fcntl.h>
#include <sys/stat.h>

#ifdef G_OS_WIN32
#include <windows.h>
#include <io.h>
#include <wchar.h>

/* Konvertiert UTF-8-Pfad zu Wide-String mit \\?\ Prefix */
wchar_t*
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
        case ERROR_PATH_NOT_FOUND:  return G_IO_ERROR_NOT_FOUND;
        case ERROR_ACCESS_DENIED:   return G_IO_ERROR_PERMISSION_DENIED;
        case ERROR_ALREADY_EXISTS:
        case ERROR_FILE_EXISTS:     return G_IO_ERROR_EXISTS;
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:  return G_IO_ERROR_BUSY;
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:
        case ERROR_DISK_FULL:       return G_IO_ERROR_NO_SPACE;
        case ERROR_INVALID_NAME:
        case ERROR_BAD_PATHNAME:    return G_IO_ERROR_INVALID_FILENAME;
        case ERROR_TOO_MANY_OPEN_FILES: return G_IO_ERROR_TOO_MANY_OPEN_FILES;
        case ERROR_DIRECTORY:       return G_IO_ERROR_IS_DIRECTORY;
        case ERROR_NOT_A_REPARSE_POINT:
        case ERROR_INVALID_REPARSE_DATA: return G_IO_ERROR_NOT_SYMBOLIC_LINK;
        default:                    return G_IO_ERROR_FAILED;
    }
}

/* Hilfsfunktion: Windows-Fehler zu GError */
static void
set_error_from_win32(GError **error, DWORD win_error)
{
    gchar *msg = g_win32_error_message(win_error);
    g_set_error(error, G_IO_ERROR, g_io_error_from_win32(win_error), "%s", msg);
    g_free(msg);
}
#endif /* G_OS_WIN32 */

gboolean
sond_mkdir(const gchar *path, GError **error)
{
    g_return_val_if_fail(path != NULL, FALSE);

#ifdef G_OS_WIN32
    wchar_t *long_path = prepare_long_path(path, error);
    if (!long_path)
        return FALSE;

    BOOL success = CreateDirectoryW(long_path, NULL);
    g_free(long_path);

    if (!success) {
        set_error_from_win32(error, GetLastError());
        return FALSE;
    }
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
    wchar_t *long_path = prepare_long_path(path, NULL);
    if (!long_path)
        return FALSE;

    DWORD attrs = GetFileAttributesW(long_path);
    g_free(long_path);
    return (attrs != INVALID_FILE_ATTRIBUTES);
#else
    return g_file_test(path, G_FILE_TEST_EXISTS);
#endif
}

gboolean
sond_mkdir_with_parents(const gchar *path, GError **error)
{
    g_return_val_if_fail(path != NULL, FALSE);

    if (sond_exists(path))
        return TRUE;

    gchar *parent = g_path_get_dirname(path);
    if (g_strcmp0(parent, ".") != 0 && g_strcmp0(parent, path) != 0) {
        if (!sond_mkdir_with_parents(parent, error)) {
            g_free(parent);
            return FALSE;
        }
    }
    g_free(parent);

    return sond_mkdir(path, error);
}

gboolean
sond_remove(const gchar *path, GError **error)
{
    g_return_val_if_fail(path != NULL, FALSE);

#ifdef G_OS_WIN32
    wchar_t *long_path = prepare_long_path(path, error);
    if (!long_path)
        return FALSE;

    BOOL success = DeleteFileW(long_path);
    g_free(long_path);

    if (!success) {
        set_error_from_win32(error, GetLastError());
        return FALSE;
    }
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
    wchar_t *long_path = prepare_long_path(path, error);
    if (!long_path)
        return FALSE;

    BOOL success = RemoveDirectoryW(long_path);
    g_free(long_path);

    if (!success) {
        set_error_from_win32(error, GetLastError());
        return FALSE;
    }
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
    g_return_val_if_fail(path != NULL, FALSE);

    SondDir *dir = sond_dir_open(path, error);
    if (!dir)
        return FALSE;

    gboolean success = TRUE;
    const gchar *name;

    while ((name = sond_dir_read_name(dir)) != NULL) {
        gchar *fullpath = g_build_filename(path, name, NULL);
        GStatBuf st;

        if (sond_stat(fullpath, &st, error) != 0) {
            success = FALSE;
            g_free(fullpath);
            break;
        }

        if (S_ISDIR(st.st_mode))
            success = sond_rmdir_r(fullpath, error);
        else
            success = sond_remove(fullpath, error);

        g_free(fullpath);
        if (!success)
            break;
    }

    sond_dir_close(dir);

    if (success && !sond_rmdir(path, error))
        success = FALSE;

    return success;
}

FILE*
sond_fopen(const gchar *path, const gchar *mode, GError **error)
{
    g_return_val_if_fail(path != NULL, NULL);
    g_return_val_if_fail(mode != NULL, NULL);

#ifdef G_OS_WIN32
    wchar_t *long_path = prepare_long_path(path, error);
    if (!long_path)
        return NULL;

    wchar_t *wmode = g_utf8_to_utf16(mode, -1, NULL, NULL, NULL);
    if (!wmode) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                    "Ungültiger Modus");
        g_free(long_path);
        return NULL;
    }

    FILE *file = _wfopen(long_path, wmode);
    g_free(long_path);
    g_free(wmode);

    if (!file) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "%s", g_strerror(errno));
    }
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
    wchar_t *long_path = prepare_long_path(path, error);
    if (!long_path)
        return -1;

    struct _stat64 st;
    gint result = _wstat64(long_path, &st);
    g_free(long_path);

    if (result != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "%s", g_strerror(errno));
        return -1;
    }

    buf->st_mode  = st.st_mode;
    buf->st_size  = st.st_size;
    buf->st_atime = st.st_atime;
    buf->st_mtime = st.st_mtime;
    buf->st_ctime = st.st_ctime;
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
    wchar_t *long_oldpath = prepare_long_path(oldpath, error);
    if (!long_oldpath)
        return FALSE;

    wchar_t *long_newpath = prepare_long_path(newpath, error);
    if (!long_newpath) {
        g_free(long_oldpath);
        return FALSE;
    }

    BOOL success = MoveFileW(long_oldpath, long_newpath);
    g_free(long_oldpath);
    g_free(long_newpath);

    if (!success) {
        set_error_from_win32(error, GetLastError());
        return FALSE;
    }
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
    wchar_t *long_source = prepare_long_path(source, error);
    if (!long_source)
        return FALSE;

    wchar_t *long_dest = prepare_long_path(dest, error);
    if (!long_dest) {
        g_free(long_source);
        return FALSE;
    }

    BOOL success = CopyFileW(long_source, long_dest, !overwrite);
    g_free(long_source);
    g_free(long_dest);

    if (!success) {
        set_error_from_win32(error, GetLastError());
        return FALSE;
    }
    return TRUE;
#else
    if (!overwrite && g_file_test(dest, G_FILE_TEST_EXISTS)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS,
                    "Zieldatei existiert bereits");
        return FALSE;
    }

    FILE *src_file = fopen(source, "rb");
    if (!src_file) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Konnte Quelldatei nicht öffnen: %s", g_strerror(errno));
        return FALSE;
    }

    FILE *dst_file = fopen(dest, "wb");
    if (!dst_file) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Konnte Zieldatei nicht erstellen: %s", g_strerror(errno));
        fclose(src_file);
        return FALSE;
    }

    gchar buffer[8192];
    size_t bytes_read;
    gboolean result = TRUE;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), src_file)) > 0) {
        if (fwrite(buffer, 1, bytes_read, dst_file) != bytes_read) {
            g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                        "Fehler beim Schreiben: %s", g_strerror(errno));
            result = FALSE;
            break;
        }
    }

    if (result && ferror(src_file)) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "Fehler beim Lesen: %s", g_strerror(errno));
        result = FALSE;
    }

    fclose(src_file);
    fclose(dst_file);
    return result;
#endif
}

gboolean
sond_copy_r(const gchar *source, const gchar *dest, gboolean overwrite, GError **error)
{
    g_return_val_if_fail(source != NULL, FALSE);
    g_return_val_if_fail(dest != NULL, FALSE);

    GStatBuf st;
    if (sond_stat(source, &st, error) != 0)
        return FALSE;

    if (!S_ISDIR(st.st_mode))
        return sond_copy(source, dest, overwrite, error);

    if (sond_exists(dest)) {
        if (!overwrite) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS,
                        "Zielverzeichnis existiert bereits");
            return FALSE;
        }
    } else {
        if (!sond_mkdir(dest, error))
            return FALSE;
    }

    SondDir *dir = sond_dir_open(source, error);
    if (!dir)
        return FALSE;

    gboolean success = TRUE;
    const gchar *name;

    while ((name = sond_dir_read_name(dir)) != NULL) {
        gchar *source_path = g_build_filename(source, name, NULL);
        gchar *dest_path   = g_build_filename(dest,   name, NULL);
        GStatBuf entry_st;

        if (sond_stat(source_path, &entry_st, error) != 0) {
            success = FALSE;
        } else if (S_ISDIR(entry_st.st_mode)) {
            success = sond_copy_r(source_path, dest_path, overwrite, error);
        } else {
            success = sond_copy(source_path, dest_path, overwrite, error);
        }

        g_free(source_path);
        g_free(dest_path);
        if (!success)
            break;
    }

    sond_dir_close(dir);
    return success;
}

gint
sond_open_fd(const gchar *path, gint flags, gint mode, GError **error)
{
    g_return_val_if_fail(path != NULL, -1);

#ifdef G_OS_WIN32
    wchar_t *long_path;
    HANDLE h;
    DWORD access = 0;
    DWORD creation = 0;
    DWORD share = FILE_SHARE_READ;
    gint osfhandle_flags = 0;

    /* Access-Flags */
    if ((flags & O_RDONLY) || (flags & O_RDWR)) access |= GENERIC_READ;
    if ((flags & O_WRONLY) || (flags & O_RDWR)) access |= GENERIC_WRITE;

    /* Creation-Disposition */
    if      ((flags & O_CREAT) && (flags & O_EXCL))  creation = CREATE_NEW;
    else if ((flags & O_CREAT) && (flags & O_TRUNC)) creation = CREATE_ALWAYS;
    else if  (flags & O_CREAT)                       creation = OPEN_ALWAYS;
    else if  (flags & O_TRUNC)                       creation = TRUNCATE_EXISTING;
    else                                             creation = OPEN_EXISTING;

    /* osfhandle-Flags */
    if (flags & O_RDONLY) osfhandle_flags |= _O_RDONLY;
    if (flags & O_WRONLY) osfhandle_flags |= _O_WRONLY;
    if (flags & O_RDWR)   osfhandle_flags |= _O_RDWR;
    if (flags & O_BINARY) osfhandle_flags |= _O_BINARY;
    else                  osfhandle_flags |= _O_TEXT;

    long_path = prepare_long_path(path, error);
    if (!long_path)
        return -1;

    h = CreateFileW(long_path, access, share, NULL, creation,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    g_free(long_path);

    if (h == INVALID_HANDLE_VALUE) {
        set_error_from_win32(error, GetLastError());
        return -1;
    }

    gint fd = _open_osfhandle((intptr_t)h, osfhandle_flags);
    if (fd == -1) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "_open_osfhandle fehlgeschlagen");
        CloseHandle(h);
        return -1;
    }

    return fd;
#else
    gint fd = g_open(path, flags, mode);
    if (fd == -1) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "%s", g_strerror(errno));
    }
    return fd;
#endif
}

gint
sond_open_read(const gchar *path, GError **error)
{
    return sond_open_fd(path, O_RDONLY | O_BINARY, 0, error);
}

/* ============================================================
 * Verzeichnis-Iteration
 * ============================================================ */
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
    g_return_val_if_fail(path != NULL, NULL);

    SondDir *dir = g_new0(SondDir, 1);

#ifdef G_OS_WIN32
    wchar_t search_path[32768];

    wchar_t *long_path = prepare_long_path(path, error);
    if (!long_path) {
        g_free(dir);
        return NULL;
    }

    size_t path_len = wcslen(long_path);
    if (path_len > 0 && long_path[path_len - 1] == L'\\')
        long_path[path_len - 1] = L'\0';

    swprintf(search_path, 32768, L"%ls\\*", long_path);
    g_free(long_path);

    dir->handle = FindFirstFileW(search_path, &dir->find_data);
    if (dir->handle == INVALID_HANDLE_VALUE) {
        set_error_from_win32(error, GetLastError());
        g_free(dir);
        return NULL;
    }

    dir->first = TRUE;
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
    if (dir->first) {
        dir->first = FALSE;
    } else {
        if (!FindNextFileW(dir->handle, &dir->find_data))
            return NULL;
    }

    /* . und .. überspringen */
    if (wcscmp(dir->find_data.cFileName, L".") == 0 ||
        wcscmp(dir->find_data.cFileName, L"..") == 0)
        return sond_dir_read_name(dir);

    g_free(dir->utf8_name);
    dir->utf8_name = g_utf16_to_utf8(
            (gunichar2*) dir->find_data.cFileName, -1, NULL, NULL, NULL);
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
    if (dir->handle != INVALID_HANDLE_VALUE)
        FindClose(dir->handle);
    g_free(dir->utf8_name);
#else
    if (dir->gdir)
        g_dir_close(dir->gdir);
#endif

    g_free(dir);
}

gboolean
sond_file_get_contents(const gchar *path, gchar **contents, gsize *length, GError **error)
{
    g_return_val_if_fail(path != NULL, FALSE);
    g_return_val_if_fail(contents != NULL, FALSE);

    GStatBuf st;
    if (sond_stat(path, &st, error) != 0)
        return FALSE;

    gsize file_size = (gsize) st.st_size;

    FILE *f = sond_fopen(path, "rb", error);
    if (!f)
        return FALSE;

    gchar *buf = g_malloc(file_size + 1);
    gsize bytes_read = fread(buf, 1, file_size, f);
    fclose(f);

    if (bytes_read != file_size) {
        g_free(buf);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "%s\nfread: %zu von %zu Bytes gelesen",
                    __func__, bytes_read, file_size);
        return FALSE;
    }

    buf[file_size] = '\0';
    *contents = buf;
    if (length)
        *length = file_size;

    return TRUE;
}

gboolean
sond_open(const gchar *path, gboolean open_with, GError **error)
{
    g_return_val_if_fail(path != NULL, FALSE);

#ifdef G_OS_WIN32
    wchar_t *long_path = prepare_long_path(path, error);
    if (!long_path)
        return FALSE;

    SHELLEXECUTEINFOW sei = { 0 };
    sei.cbSize  = sizeof(sei);
    sei.nShow   = SW_SHOWNORMAL;
    sei.lpVerb  = open_with ? L"openas" : L"open";
    sei.lpFile  = long_path;
    sei.fMask   = SEE_MASK_INVOKEIDLIST;

    BOOL ret = ShellExecuteExW(&sei);
    g_free(long_path);

    if (!ret) {
        set_error_from_win32(error, GetLastError());
        return FALSE;
    }
    return TRUE;
#else
    gchar *argv[] = { "xdg-open", (gchar*) path, NULL };
    return g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                         NULL, NULL, NULL, error);
#endif
}
