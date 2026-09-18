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
#include <string.h>

#ifdef G_OS_WIN32
#include <windows.h>
#include <io.h>
#include <wchar.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Minimaler CF-API-Ausschnitt für Cloud-Hydrierung bei Bedarf         */
/* ------------------------------------------------------------------ */

/* Nutzer-Fund 18.09.2026: Nachdem sond_fopen() (s.u.) auf CreateFileW()
 * umgestellt wurde, kam für .sond_index.db-shm (zonds eigene, im
 * Projektverzeichnis liegende SQLite-WAL-Begleitdatei) plötzlich statt
 * der bisherigen (irreführenden) "Invalid argument"-Meldung die
 * spezifischere "Der Zugriff auf die Clouddatei wurde verweigert"
 * (ERROR_CLOUD_FILE_ACCESS_DENIED, 395) - exakt derselbe Fehler, der
 * schon beim SeaDrive-Doppelklick-Hydrieren aufgetreten war (s.
 * sond_seadrive_hydrate(), sond_treeviewfm_seadrive.c, 18.09.2026): die
 * Datei war ein noch nicht hydrierter SeaDrive-Platzhalter, und ein
 * roher Lesezugriff (egal ob über _wfopen() - das mappt diesen Fall nur
 * unspezifisch auf errno=EINVAL statt ihn eigens zu erkennen - oder über
 * CreateFileW(GENERIC_READ)) schlägt dafür fehl; nötig ist stattdessen
 * die offizielle Cloud-Filter-API. D.h. die ursprüngliche "führender
 * Punkt"-Theorie zur .sond_index.db-shm-Regression war vermutlich falsch
 * bzw. unvollständig - es handelte sich von Anfang an um genau dieses
 * Hydrierungsproblem, nur von _wfopen() irreführend als generisches
 * EINVAL gemeldet.
 *
 * Allgemeine Lösung (statt SeaDrive-Sonderfall-Prüfung vor jedem
 * sond_fopen()-Aufruf im ganzen Code): bei genau diesem Fehler einmalig
 * Hydrierung anstoßen und den Öffnen-Versuch wiederholen - unabhängig
 * davon, ob der Pfad überhaupt auf einem SeaDrive-Laufwerk liegt (für
 * gewöhnliche lokale Dateien tritt dieser Fehler nie auf, die Prüfung
 * ist also ein reiner No-Op-Fall dort). Absichtlich hier in
 * sond_file_helper.c dupliziert statt sond_treeviewfm_seadrive.h
 * einzubinden: Letzteres hängt (über sond_treeviewfm.h) von GTK ab,
 * sond_file_helper.c ist bewusst eine GTK-freie, niedrige Utility-Ebene
 * (die umgekehrt schon von sond_treeviewfm_seadrive.c genutzt wird -
 * ein Rückbezug wäre ein Include-Zirkel). Mittelfristig wäre eine
 * gemeinsame, GTK-freie CF-API-Basis (eigene kleine Datei) sauberer als
 * diese Duplizierung - hier aus Zeitgründen zurückgestellt. */
#ifndef ERROR_CLOUD_FILE_ACCESS_DENIED
#define ERROR_CLOUD_FILE_ACCESS_DENIED 395L
#endif

typedef HRESULT (WINAPI *PFN_CfHydratePlaceholder_fh)(
    HANDLE       FileHandle,
    LARGE_INTEGER StartingOffset,
    LARGE_INTEGER Length,
    DWORD        HydrateFlags,
    LPOVERLAPPED Overlapped);

static PFN_CfHydratePlaceholder_fh g_CfHydratePlaceholder_fh = NULL;
static HMODULE                     g_hCldApi_fh              = NULL;
static GOnce                       g_cfapi_once_fh           = G_ONCE_INIT;

static gpointer cfapi_init_once_fh(gpointer data)
{
    (void) data;
    g_hCldApi_fh = LoadLibraryA("cldapi.dll");
    if (g_hCldApi_fh)
        g_CfHydratePlaceholder_fh = (PFN_CfHydratePlaceholder_fh)
                GetProcAddress(g_hCldApi_fh, "CfHydratePlaceholder");
    return g_hCldApi_fh;
}

/* Versucht best-effort, long_path zu hydrieren (Fehler werden bewusst
 * ignoriert - der Aufrufer wiederholt danach ohnehin seinen ursprünglichen
 * CreateFileW()-Versuch und meldet bei erneutem Fehlschlag dessen realen
 * Fehler, s. sond_fopen()). */
static void hydrate_if_cloud_placeholder(const wchar_t *long_path)
{
    HANDLE h;

    g_once(&g_cfapi_once_fh, cfapi_init_once_fh, NULL);
    if (!g_CfHydratePlaceholder_fh)
        return;

    h = CreateFileW(long_path, FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;

    {
        LARGE_INTEGER offset = { .QuadPart = 0 };
        LARGE_INTEGER length = { .QuadPart = 1 };
        g_CfHydratePlaceholder_fh(h, offset, length, 0, NULL);
    }

    CloseHandle(h);
}

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

#ifdef G_OS_WIN32
/* Sharing-Violations sind beim Auto-Update ein bekanntes, rein zeitliches
 * Problem: eine gerade erst beendete zond.exe (bzw. eine ihrer DLLs) wird
 * von Windows/Virenscanner manchmal noch einen kurzen Moment gesperrt
 * gehalten, obwohl der Prozess laut WaitForSingleObject schon beendet ist.
 * Deshalb hier ein paar Mal mit kurzer Pause erneut versuchen, statt sofort
 * aufzugeben (insgesamt ca. 5 Sekunden). */
#define SOND_WIN32_RETRY_TRIES 25
#define SOND_WIN32_RETRY_DELAY_MS 200

static gboolean
win32_is_transient_error(DWORD win_error)
{
    return win_error == ERROR_SHARING_VIOLATION
            || win_error == ERROR_LOCK_VIOLATION
            || win_error == ERROR_ACCESS_DENIED;
}
#endif

gboolean
sond_remove(const gchar *path, GError **error)
{
    g_return_val_if_fail(path != NULL, FALSE);

#ifdef G_OS_WIN32
    wchar_t *long_path = prepare_long_path(path, error);
    if (!long_path)
        return FALSE;

    BOOL success = FALSE;
    DWORD win_error = 0;
    for (int attempt = 0; attempt < SOND_WIN32_RETRY_TRIES; attempt++) {
        success = DeleteFileW(long_path);
        if (success)
            break;
        win_error = GetLastError();
        if (!win32_is_transient_error(win_error))
            break;
        Sleep(SOND_WIN32_RETRY_DELAY_MS);
    }
    g_free(long_path);

    if (!success) {
        set_error_from_win32(error, win_error);
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

    BOOL success = FALSE;
    DWORD win_error = 0;
    for (int attempt = 0; attempt < SOND_WIN32_RETRY_TRIES; attempt++) {
        success = RemoveDirectoryW(long_path);
        if (success)
            break;
        win_error = GetLastError();
        if (!win32_is_transient_error(win_error))
            break;
        Sleep(SOND_WIN32_RETRY_DELAY_MS);
    }
    g_free(long_path);

    if (!success) {
        set_error_from_win32(error, win_error);
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

    /* success wird FALSE, sobald ein Eintrag nicht geloescht werden konnte -
     * wir brechen dann aber NICHT ab, sondern versuchen trotzdem, alle
     * uebrigen Eintraege loszuwerden. Sonst reicht eine einzige gerade noch
     * gesperrte Datei (siehe sond_remove/sond_rmdir oben), um den kompletten
     * Rest eines ansonsten loeschbaren Verzeichnisses zu retten - genau das
     * hat beim Auto-Update dazu gefuehrt, dass das alte bin/-Verzeichnis
     * stehen blieb und die neue Version danach nicht mehr an seine Stelle
     * verschoben werden konnte. */
    gboolean success = TRUE;
    const gchar *name;

    while ((name = sond_dir_read_name(dir)) != NULL) {
        gchar *fullpath = g_build_filename(path, name, NULL);
        GStatBuf st;
        GError *error_entry = NULL;
        gboolean ok;

        if (sond_stat(fullpath, &st, &error_entry) != 0) {
            success = FALSE;
            if (error && !*error)
                *error = error_entry;
            else
                g_clear_error(&error_entry);
            g_free(fullpath);
            continue;
        }

        if (S_ISDIR(st.st_mode))
            ok = sond_rmdir_r(fullpath, &error_entry);
        else
            ok = sond_remove(fullpath, &error_entry);

        if (!ok) {
            success = FALSE;
            if (error && !*error)
                *error = error_entry;
            else
                g_clear_error(&error_entry);
        }

        g_free(fullpath);
    }

    sond_dir_close(dir);

    /* Das Verzeichnis selbst nur loeschen, wenn wirklich alles drin weg ist -
     * sonst wuerde sond_rmdir() ohnehin mit ERROR_DIR_NOT_EMPTY scheitern. */
    if (success && !sond_rmdir(path, error))
        success = FALSE;

    return success;
}

/* Nutzer-Entscheidung 09/2026 (nach zwei Regressionen in Folge - erst
 * "invalid argument" beim Laden durch ZIP-Sonderzeichen im Dateinamen,
 * dann "Datei von anderem Prozeß verwendet" bei praktisch jedem Projekt,
 * weil der CreateFileW-Ersatz einen zu engen Freigabemodus setzte):
 * versuchsweiser Umstieg von _wfopen() auf CreateFileW()/_open_osfhandle()/
 * _fdopen() (s. Versionsgeschichte) wieder VOLLSTÄNDIG zurückgenommen -
 * zurück zu _wfopen(), trotz der bekannten, seltenen Einschränkung bei
 * ZIP-Dateinamen mit Leerzeichen/Punkt am Ende einer Pfadkomponente (löst
 * dort weiterhin errno=EINVAL aus, s. ToDo.c). Stabilität hat Vorrang vor
 * diesem Randfall. */
FILE*
sond_fopen(const gchar *path, const gchar *mode, GError **error)
{
    g_return_val_if_fail(path != NULL, NULL);
    g_return_val_if_fail(mode != NULL, NULL);

#ifdef G_OS_WIN32
    /* Nutzer-Fund 18.09.2026 (s. ausführl. Doc-Kommentar bei sond_stat()
     * oben, ToDo.c): _wfopen() validiert Dateinamen zusätzlich zu dem,
     * was Win32 mit dem \\?\-Langpfad-Präfix verlangt, und lehnt dabei
     * u.a. Namen mit Leerzeichen/Punkt am Ende sowie Namen ab, die nur
     * aus einem führenden Punkt + Text bestehen. Allgemeine Lösung
     * (Nutzer-Entscheidung 18.09.2026: "Das muß man doch allgemein
     * lösen" - statt die konkret betroffene Datei zu verstecken):
     * CreateFileW() statt _wfopen(), zweiter Versuch nach dem am
     * 16.09.2026 wegen zu engem Freigabemodus zurückgenommenen ersten
     * Versuch (s. ToDo.c, Task #105) - diesmal mit demselben großzügigen
     * Freigabemodus (FILE_SHARE_READ|WRITE|DELETE), der bereits in
     * sond_seadrive_hydrate()/hydrate_progress_update()
     * (sond_treeviewfm_seadrive.c) erfolgreich verwendet wird. Das war
     * die eigentliche Ursache der Vorgänger-Regression ("Datei von
     * anderem Prozeß verwendet"), nicht der Wechsel auf CreateFileW an
     * sich. Über _open_osfhandle()/_fdopen() wird daraus wieder ein
     * normales FILE*, mit dem der Rest des Codes unverändert
     * weiterarbeiten kann.
     *
     * Deckt die in dieser Codebasis tatsächlich verwendeten Modi ab
     * ("rb", "wb", "w") sowie generisch "r"/"w"/"a" mit optionalem "+" -
     * bei "a" wird die Schreibposition nur einmalig beim Öffnen ans Ende
     * gesetzt (kein atomares FILE_APPEND_DATA), da kein Aufrufer diesen
     * Modus aktuell nutzt. */
    DWORD desired_access = 0;
    DWORD creation_disposition = 0;
    gboolean binary = (strchr(mode, 'b') != NULL);
    gboolean append = (strchr(mode, 'a') != NULL);
    gboolean plus = (strchr(mode, '+') != NULL);
    wchar_t *long_path = NULL;
    HANDLE h = INVALID_HANDLE_VALUE;
    gint osf_flags = 0;
    gint fd = -1;
    FILE *file = NULL;

    if (strchr(mode, 'r')) {
        desired_access = plus ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ;
        creation_disposition = OPEN_EXISTING;
    } else if (strchr(mode, 'w')) {
        desired_access = plus ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_WRITE;
        creation_disposition = CREATE_ALWAYS;
    } else if (append) {
        desired_access = plus ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_WRITE;
        creation_disposition = OPEN_ALWAYS;
    } else {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                    "sond_fopen('%s'): unbekannter Modus '%s'", path, mode);
        return NULL;
    }

    long_path = prepare_long_path(path, error);
    if (!long_path)
        return NULL;

    h = CreateFileW(long_path, desired_access,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL, creation_disposition, FILE_ATTRIBUTE_NORMAL, NULL);

    if (h == INVALID_HANDLE_VALUE &&
            GetLastError() == (DWORD) ERROR_CLOUD_FILE_ACCESS_DENIED) {
        /* S. ausführlichen Doc-Kommentar bei hydrate_if_cloud_placeholder()
         * oben (18.09.2026) - noch nicht hydrierter SeaDrive-Platzhalter,
         * einmalig Hydrierung anstoßen und erneut versuchen. */
        hydrate_if_cloud_placeholder(long_path);
        h = CreateFileW(long_path, desired_access,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                NULL, creation_disposition, FILE_ATTRIBUTE_NORMAL, NULL);
    }

    g_free(long_path);

    if (h == INVALID_HANDLE_VALUE) {
        DWORD win_err = GetLastError();
        gchar *msg = g_win32_error_message(win_err);
        g_set_error(error, G_IO_ERROR, g_io_error_from_win32(win_err),
                    "sond_fopen('%s'): %s", path, msg);
        g_free(msg);
        return NULL;
    }

    if (append)
        SetFilePointer(h, 0, NULL, FILE_END);

    osf_flags = binary ? _O_BINARY : _O_TEXT;
    if (desired_access == GENERIC_READ)
        osf_flags |= _O_RDONLY;

    fd = _open_osfhandle((intptr_t) h, osf_flags);
    if (fd == -1) {
        gint err = errno;
        CloseHandle(h);
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(err),
                    "sond_fopen('%s'): _open_osfhandle: %s", path,
                    g_strerror(err));
        return NULL;
    }

    file = _fdopen(fd, mode);
    if (!file) {
        gint err = errno;
        _close(fd); /* schließt auch das zugrundeliegende Handle */
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(err),
                    "sond_fopen('%s'): _fdopen: %s", path, g_strerror(err));
        return NULL;
    }

    return file;
#else
    FILE *file = g_fopen(path, mode);
    if (!file) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "sond_fopen('%s'): %s", path, g_strerror(errno));
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
    /* Nutzer-Fund 18.09.2026 ("Fehler beim Laden des Projekts: Invalid
     * argument" bei .sond_index.db-shm, s. ToDo.c): _wstat64() (wie
     * _wfopen(), s. sond_fopen() unten) validiert Dateinamen ZUSÄTZLICH
     * über das hinaus, was Win32 mit dem \\?\-Langpfad-Präfix verlangt,
     * und lehnt dabei u.a. Namen ab, die nur aus einem führenden Punkt +
     * Text bestehen (analog zur schon dokumentierten Ablehnung von
     * Leerzeichen/Punkt am Ende einer Pfadkomponente). Allgemeine Lösung
     * statt Einzelfall-Workaround (Nutzer-Entscheidung 18.09.2026): auf
     * GetFileAttributesExW() umgestellt - eine reine Win32-Metadaten-
     * Abfrage ohne CRT-eigene Namensprüfung UND ohne Handle/Freigabe-
     * Verhandlung (im Gegensatz zu CreateFileW/_wfopen() also strukturell
     * gar nicht erst anfällig für "Datei von anderem Prozeß verwendet").
     *
     * Bekannter Unterschied zu _wstat64(): _wstat64() löst Reparse-Points/
     * Symlinks auf (liefert Infos über das ZIEL, wie POSIX stat()),
     * GetFileAttributesExW() dagegen nicht (wie POSIX lstat() - liefert
     * Infos über den Reparse-Point selbst). Für Verzeichnis-Junctions/
     * -Symlinks bleibt S_ISDIR() trotzdem korrekt (das Verzeichnis-Bit
     * sitzt unter NTFS auch auf dem Link-Eintrag selbst) - nur bei
     * Datei-Symlinks mit abweichendem Zieltyp könnte sich das Verhalten
     * unterscheiden; im bisherigen Code nirgends als relevant erkennbar. */
    wchar_t *long_path = prepare_long_path(path, error);
    if (!long_path)
        return -1;

    WIN32_FILE_ATTRIBUTE_DATA attr_data = { 0 };
    BOOL ok = GetFileAttributesExW(long_path, GetFileExInfoStandard, &attr_data);
    g_free(long_path);

    if (!ok) {
        DWORD win_err = GetLastError();
        gchar *msg = g_win32_error_message(win_err);
        g_set_error(error, G_IO_ERROR, g_io_error_from_win32(win_err),
                    "sond_stat('%s'): %s", path, msg);
        g_free(msg);
        return -1;
    }

    {
        /* FILETIME zaehlt 100-ns-Intervalle seit 1601-01-01, time_t
         * Sekunden seit 1970-01-01 - Epochendifferenz in 100ns-Einheiten. */
        const guint64 filetime_unix_epoch_diff = 116444736000000000ULL;
        gboolean is_dir = (attr_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        gboolean writable = !(attr_data.dwFileAttributes & FILE_ATTRIBUTE_READONLY);
        ULARGE_INTEGER ul;

        memset(buf, 0, sizeof(*buf));

        buf->st_mode = (is_dir ? (S_IFDIR | 0111) : S_IFREG) |
                (writable ? 0666 : 0444);
        buf->st_size = ((gint64) attr_data.nFileSizeHigh << 32) |
                attr_data.nFileSizeLow;

        ul.LowPart = attr_data.ftLastAccessTime.dwLowDateTime;
        ul.HighPart = attr_data.ftLastAccessTime.dwHighDateTime;
        buf->st_atime = (time_t) ((ul.QuadPart - filetime_unix_epoch_diff) / 10000000ULL);

        ul.LowPart = attr_data.ftLastWriteTime.dwLowDateTime;
        ul.HighPart = attr_data.ftLastWriteTime.dwHighDateTime;
        buf->st_mtime = (time_t) ((ul.QuadPart - filetime_unix_epoch_diff) / 10000000ULL);

        ul.LowPart = attr_data.ftCreationTime.dwLowDateTime;
        ul.HighPart = attr_data.ftCreationTime.dwHighDateTime;
        buf->st_ctime = (time_t) ((ul.QuadPart - filetime_unix_epoch_diff) / 10000000ULL);
    }

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

    BOOL success = FALSE;
    DWORD win_error = 0;
    gboolean cleaned_dest = FALSE;
    for (int attempt = 0; attempt < SOND_WIN32_RETRY_TRIES; attempt++) {
        success = MoveFileW(long_oldpath, long_newpath);
        if (success)
            break;
        win_error = GetLastError();

        if ((win_error == ERROR_ALREADY_EXISTS || win_error == ERROR_FILE_EXISTS)
                && !cleaned_dest) {
            /* Ziel existiert noch - typischerweise ein Rest einer vorherigen,
             * nicht ganz vollstaendigen Aufraeumaktion (eine darin enthaltene
             * Datei war zu dem Zeitpunkt noch gesperrt). MoveFileW
             * ueberschreibt anders als ein Unix-rename() nie von selbst -
             * also einmal versuchen, das Ziel wegzuraeumen, und den Move
             * dann wiederholen. */
            GError *error_clean = NULL;
            GStatBuf st;

            cleaned_dest = TRUE; //nur einmal versuchen, nicht endlos loopen
            if (sond_stat(newpath, &st, &error_clean) == 0) {
                if (S_ISDIR(st.st_mode))
                    sond_rmdir_r(newpath, &error_clean);
                else
                    sond_remove(newpath, &error_clean);
            }
            g_clear_error(&error_clean);
            continue; //sofort erneut versuchen, ohne zu warten
        }

        if (!win32_is_transient_error(win_error))
            break;
        Sleep(SOND_WIN32_RETRY_DELAY_MS);
    }
    g_free(long_oldpath);
    g_free(long_newpath);

    if (!success) {
        set_error_from_win32(error, win_error);
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
