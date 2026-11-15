/*
 sond (sond_seadrive_core.c) - Akten, Beweisstücke, Unterlagen
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

/* GTK-freier Kern der SeaDrive-Anbindung, s. sond_seadrive_core.h. Aus
 * sond_seadrive.c herausgelöst; die Kommentare stammen von dort. */

#include "sond_seadrive_core.h"

#if defined(_WIN32)

#include <windows.h>
#include <glib.h>
#include <gio/gio.h>

#include "../sond_file_helper.h"
#include "sond_seadrive_win.h"

/* ------------------------------------------------------------------ */
/*  CF-API constants (from cfapi.h, duplicated to avoid the header)   */
/* ------------------------------------------------------------------ */

#define CF_SET_PIN_FLAG_NONE         0x00000000
#define CF_SET_PIN_FLAG_RECURSE      0x00000001

#define CF_HYDRATE_FLAG_NONE         0x00000000

/* ------------------------------------------------------------------ */
/*  CF-API function pointer types                                      */
/* ------------------------------------------------------------------ */

typedef HRESULT (WINAPI *PFN_CfSetPinState)(
    HANDLE       FileHandle,
    DWORD        PinState,
    DWORD        PinFlags,
    LPOVERLAPPED Overlapped);

typedef HRESULT (WINAPI *PFN_CfGetPlaceholderInfo)(
    HANDLE  FileHandle,
    DWORD   InfoClass,
    PVOID   InfoBuffer,
    DWORD   InfoBufferLength,
    PDWORD  ReturnedLength);

typedef HRESULT (WINAPI *PFN_CfGetSyncRootInfoByPath)(
    LPCWSTR SyncRootPath,
    DWORD   InfoClass,
    PVOID   InfoBuffer,
    DWORD   InfoBufferLength,
    PDWORD  ReturnedLength);

typedef HRESULT (WINAPI *PFN_CfHydratePlaceholder)(
    HANDLE       FileHandle,
    LARGE_INTEGER StartingOffset,
    LARGE_INTEGER Length,
    DWORD        HydrateFlags,
    LPOVERLAPPED Overlapped);

/* ------------------------------------------------------------------ */
/*  Runtime-loaded CF-API pointers                                     */
/* ------------------------------------------------------------------ */

static PFN_CfSetPinState           g_CfSetPinState           = NULL;
static PFN_CfGetPlaceholderInfo    g_CfGetPlaceholderInfo    = NULL;
static PFN_CfGetSyncRootInfoByPath g_CfGetSyncRootInfoByPath = NULL;
static PFN_CfHydratePlaceholder    g_CfHydratePlaceholder    = NULL;
static HMODULE                     g_hCldApi                 = NULL;
static GOnce                       g_cfapi_once              = G_ONCE_INIT;

static gpointer cfapi_init_once(gpointer data)
{
    (void)data;
    g_hCldApi = LoadLibraryA("cldapi.dll");
    if (!g_hCldApi)
        return NULL;

    g_CfSetPinState = (PFN_CfSetPinState)
        GetProcAddress(g_hCldApi, "CfSetPinState");
    g_CfGetPlaceholderInfo = (PFN_CfGetPlaceholderInfo)
        GetProcAddress(g_hCldApi, "CfGetPlaceholderInfo");
    g_CfGetSyncRootInfoByPath = (PFN_CfGetSyncRootInfoByPath)
        GetProcAddress(g_hCldApi, "CfGetSyncRootInfoByPath");
    g_CfHydratePlaceholder = (PFN_CfHydratePlaceholder)
        GetProcAddress(g_hCldApi, "CfHydratePlaceholder");
    return g_hCldApi;
}

static void cfapi_init(void)
{
    g_once(&g_cfapi_once, cfapi_init_once, NULL);
}

gpointer sond_seadrive_core_placeholder_info(void)
{
    cfapi_init();

    return (gpointer) g_CfGetPlaceholderInfo;
}

/* ------------------------------------------------------------------ */
/*  Public API: sond_seadrive_is_seadrive_path                        */
/* ------------------------------------------------------------------ */

gboolean sond_seadrive_is_seadrive_path(const gchar *full_path)
{
    wchar_t *lp;
    BYTE     buf[8];
    DWORD    returned = 0;
    HRESULT  hr;

    cfapi_init();
    if (!g_CfGetSyncRootInfoByPath)
        return FALSE;

    lp = prepare_long_path(full_path, NULL);
    if (!lp)
        return FALSE;

    hr = g_CfGetSyncRootInfoByPath(lp, 0, buf, sizeof(buf), &returned);
    g_free(lp);

    return SUCCEEDED(hr) || hr == HRESULT_MORE_DATA;
}

/* ------------------------------------------------------------------ */
/*  Public API: sond_seadrive_get_pin_state                           */
/* ------------------------------------------------------------------ */

guint sond_seadrive_get_pin_state(const gchar *full_path)
{
    wchar_t *lp;
    DWORD    attrs;

    lp = prepare_long_path(full_path, NULL);
    if (!lp)
        return STVFM_PIN_STATE_UNSPECIFIED;

    attrs = GetFileAttributesW(lp);
    g_free(lp);

    if (attrs == INVALID_FILE_ATTRIBUTES)
        return STVFM_PIN_STATE_UNSPECIFIED;

    if (attrs & FILE_ATTRIBUTE_PINNED)
        return STVFM_PIN_STATE_PINNED;
    if (attrs & FILE_ATTRIBUTE_UNPINNED)
        return STVFM_PIN_STATE_UNPINNED;
    return STVFM_PIN_STATE_UNSPECIFIED;
}

/* ------------------------------------------------------------------ */
/*  Public API: sond_seadrive_set_pin_state                           */
/* ------------------------------------------------------------------ */

gboolean sond_seadrive_set_pin_state(const gchar *full_path,
                                     guint        pin_state,
                                     gboolean     recurse,
                                     GError     **error)
{
    wchar_t *lp;
    HANDLE   h;
    HRESULT  hr;

    cfapi_init();
    if (!g_CfSetPinState) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                "CfSetPinState nicht verfügbar");
        return FALSE;
    }

    lp = prepare_long_path(full_path, error);
    if (!lp)
        return FALSE;

    h = CreateFileW(lp,
                    FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    g_free(lp);

    if (h == INVALID_HANDLE_VALUE) {
        if (error) {
            gchar *msg = g_win32_error_message(GetLastError());
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "Cannot open '%s': %s", full_path, msg);
            g_free(msg);
        }
        return FALSE;
    }

    hr = g_CfSetPinState(h, (DWORD)pin_state,
                         recurse ? CF_SET_PIN_FLAG_RECURSE : CF_SET_PIN_FLAG_NONE,
                         NULL);
    CloseHandle(h);

    if (FAILED(hr)) {
        if (error) {
            gchar *msg = g_win32_error_message(hr & 0xFFFF);
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "CfSetPinState('%s'): %s (0x%08lX)",
                    full_path, msg, (unsigned long)hr);
            g_free(msg);
        }
        return FALSE;
    }
    return TRUE;
}

void sond_seadrive_pin_note_failure(GPtrArray *failed, const gchar *full_path,
        GError *error)
{
    gchar *base = NULL;
    const gchar *reason = error ? error->message : "unbekannter Fehler";
    const gchar *sep = strstr(reason, "): ");

    if (!failed)
        return;

    //die Meldung von sond_seadrive_set_pin_state() beginnt mit dem Pfad
    if (sep)
        reason = sep + 3;

    base = g_path_get_basename(full_path);
    g_ptr_array_add(failed, g_strdup_printf("%s - %s", base, reason));
    g_free(base);
}

/* ------------------------------------------------------------------ */
/*  Public API: sond_seadrive_hydrate                                 */
/* ------------------------------------------------------------------ */

/*
 * sond_seadrive_hydrate:
 *
 * Stößt die Hydrierung (den Download) einer noch nicht lokal vorhandenen
 * Cloud-Datei an, über die offizielle, für genau diesen Zweck vorgesehene
 * CfHydratePlaceholder()-API (cfapi.h/cldapi.dll, dynamisch geladen wie der
 * Rest dieser Datei - s. Kommentar bei cfapi_init_once(), derselbe Grund:
 * kein cfapi.h im hier verwendeten MinGW-Toolchain). Der Trick
 * CreateFileW(GENERIC_READ)+ReadFile() (ein Byte lesen, um SeaDrive/Windows
 * zum "Recall" zu bewegen) schlägt unter aktuellem Windows zuverlässig mit
 * ERROR_CLOUD_FILE_ACCESS_DENIED fehl. Laut Microsoft-Dokumentation genügt
 * für CfHydratePlaceholder ein Handle mit reinem Attribut-Zugriff
 * (FILE_READ_ATTRIBUTES statt GENERIC_READ), das ist vermutlich der
 * Unterschied, den Windows jetzt strenger prüft. Length ist auf 1 Byte
 * begrenzt.
 *
 * Bereits lokal vorhandene Dateien (kein FILE_ATTRIBUTE_RECALL_ON_DATA_
 * ACCESS) sind ein No-Op (TRUE, kein Fehler). Gibt FALSE mit gesetztem
 * error zurück, wenn cldapi.dll/CfHydratePlaceholder nicht verfügbar ist
 * oder der Aufruf selbst fehlschlägt - der Aufrufer fällt in diesem Fall
 * auf den normalen Öffnen-Weg zurück (s. sond_treeviewfm_open()).
 *
 * Die Begrenzung auf 1 Byte spart das Warten auf die Downloaddauer nicht:
 * bei einer 51-GB-Datei blockierte der Aufruf trotzdem minutenlang
 * (vermutlich lädt der SeaDrive-Provider unabhängig von der angeforderten
 * Länge grundsätzlich die ganze Datei, bevor CfHydratePlaceholder
 * zurückkehrt). Diese Funktion ist deshalb synchron/blockierend - s.
 * sond_seadrive_hydrate_async() weiter unten, die sie in einem
 * Hintergrund-Thread aufruft, und sond_seadrive_needs_hydration() für einen
 * schnellen, nicht-blockierenden Vorab-Check (nur GetFileAttributesW), mit
 * dem der Aufrufer entscheiden kann, ob eine Hydrierung überhaupt nötig ist,
 * ohne dafür einen Thread zu starten.
 */
gboolean sond_seadrive_needs_hydration(const gchar *full_path)
{
    wchar_t *lp;
    DWORD    attrs;

    lp = prepare_long_path(full_path, NULL);
    if (!lp)
        return FALSE;

    attrs = GetFileAttributesW(lp);
    g_free(lp);

    if (attrs == INVALID_FILE_ATTRIBUTES)
        return FALSE;

    return (attrs & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS) != 0;
}
gboolean sond_seadrive_hydrate(const gchar *full_path, GError **error)
{
    wchar_t *lp;
    DWORD    attrs;
    HANDLE   h;
    HRESULT  hr;
    LARGE_INTEGER offset = { .QuadPart = 0 };
    LARGE_INTEGER length = { .QuadPart = 1 };

    lp = prepare_long_path(full_path, error);
    if (!lp)
        return FALSE;

    attrs = GetFileAttributesW(lp);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        if (error) {
            gchar *msg = g_win32_error_message(GetLastError());
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "GetFileAttributesW('%s'): %s", full_path, msg);
            g_free(msg);
        }
        g_free(lp);
        return FALSE;
    }

    if (!(attrs & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS)) {
        /* schon lokal - nichts zu tun */
        g_free(lp);
        return TRUE;
    }

    cfapi_init();
    if (!g_CfHydratePlaceholder) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                "CfHydratePlaceholder nicht verfügbar (cldapi.dll)");
        g_free(lp);
        return FALSE;
    }

    h = CreateFileW(lp, FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    g_free(lp);

    if (h == INVALID_HANDLE_VALUE) {
        if (error) {
            gchar *msg = g_win32_error_message(GetLastError());
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "CreateFileW('%s'): %s", full_path, msg);
            g_free(msg);
        }
        return FALSE;
    }

    hr = g_CfHydratePlaceholder(h, offset, length, CF_HYDRATE_FLAG_NONE, NULL);
    CloseHandle(h);

    if (FAILED(hr)) {
        if (error) {
            gchar *msg = g_win32_error_message(hr & 0xFFFF);
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "CfHydratePlaceholder('%s'): %s (0x%08lX)",
                    full_path, msg, (unsigned long) hr);
            g_free(msg);
        }
        return FALSE;
    }

    return TRUE;
}



/* ------------------------------------------------------------------ */
/*  Sync-Zustand (hochgeladen?)                                        */
/* ------------------------------------------------------------------ */

//Toleranz für Zeitunterschiede zwischen Änderung und Beginn der Berechtigung
#define SINCE_SLACK 60

//Obergrenze der durchsuchten Einträge, danach "unbekannt"
#define MAX_ENTRIES 200000

//Aufbau von CF_PLACEHOLDER_BASIC_INFO (cfapi.h)
typedef struct {
    DWORD PinState;
    DWORD InSyncState;
    LARGE_INTEGER FileId;
    LARGE_INTEGER SyncRootFileId;
    ULONG FileIdentityLength;
    BYTE FileIdentity[256];
} BasicInfo;

#define CF_PLACEHOLDER_INFO_BASIC 0
#define CF_IN_SYNC_STATE_IN_SYNC 1

/* Gilt die Datei als zum Server synchron (hochgeladen)? Jede Unsicherheit (API fehlt,
 * Datei nicht zu öffnen, Abfrage scheitert) zählt als "nicht synchron": eine gerade
 * neu angelegte Datei erkennt CfGetPlaceholderInfo womöglich noch nicht als Cloud-Datei,
 * und ein fälschliches "alles synchron" würde ein Datenverlust-Risiko verschleiern. */
gboolean sond_seadrive_file_in_sync(const gchar *path) {
    PFN_CfGetPlaceholderInfo get_info = NULL;
    BasicInfo basic = { 0 };
    DWORD returned = 0;
    wchar_t *lp = prepare_long_path(path, NULL);
    HANDLE h = INVALID_HANDLE_VALUE;
    HRESULT hr = S_OK;

    cfapi_init();
    get_info = g_CfGetPlaceholderInfo;
    if (!get_info || !lp) {
        g_free(lp);

        return FALSE;
    }

    h = CreateFileW(lp, FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    g_free(lp);
    if (h == INVALID_HANDLE_VALUE)
        return FALSE;

    hr = get_info(h, CF_PLACEHOLDER_INFO_BASIC, &basic, sizeof(basic),
            &returned);
    CloseHandle(h);

    if (FAILED(hr) && hr != HRESULT_MORE_DATA)
        return FALSE;

    return basic.InSyncState == CF_IN_SYNC_STATE_IN_SYNC;
}

static gint64 filetime_to_unix(const FILETIME *ft) {
    ULARGE_INTEGER ui;

    ui.LowPart = ft->dwLowDateTime;
    ui.HighPart = ft->dwHighDateTime;

    return (gint64) (ui.QuadPart / 10000000ULL) - 11644473600LL;
}

//Rückgabe: FALSE bei Abbruch (zu viele Einträge, Verzeichnis nicht lesbar)
static gboolean scan_dir(const gchar *dir,
        gint64 threshold, guint *visited, guint *unsynced) {
    gchar *pattern = g_strconcat(dir, "/*", NULL);
    wchar_t *lp = prepare_long_path(pattern, NULL);
    WIN32_FIND_DATAW fd;
    HANDLE h = INVALID_HANDLE_VALUE;
    gboolean ok = TRUE;

    g_free(pattern);
    if (!lp)
        return FALSE;

    h = FindFirstFileW(lp, &fd);
    g_free(lp);
    if (h == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_NOT_FOUND; //leeres Verzeichnis

    do {
        gchar *name = NULL;
        gchar *path = NULL;

        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;

        if (++(*visited) > MAX_ENTRIES) {
            ok = FALSE;
            break;
        }

        name = g_utf16_to_utf8((const gunichar2*) fd.cFileName, -1, NULL, NULL,
                NULL);
        if (!name) {
            ok = FALSE;
            break;
        }
        path = g_strdup_printf("%s/%s", dir, name);
        g_free(name);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            ok = scan_dir(path, threshold, visited, unsynced);
        } else if (filetime_to_unix(&fd.ftLastWriteTime) >= threshold) {
            if (!sond_seadrive_file_in_sync(path))
                (*unsynced)++;
        }
        g_free(path);
    } while (ok && FindNextFileW(h, &fd));

    FindClose(h);

    return ok;
}

gint sond_seadrive_count_unsynced(const gchar *dir, gint64 since_unix) {
    guint visited = 0;
    guint unsynced = 0;

    g_return_val_if_fail(dir != NULL, -1);

    cfapi_init();
    if (!g_CfGetPlaceholderInfo)
        return -1;

    if (!scan_dir(dir, since_unix - SINCE_SLACK, &visited, &unsynced))
        return -1;

    return (gint) unsynced;
}

#endif /* _WIN32 */

#if !defined(_WIN32)

gint sond_seadrive_count_unsynced(const gchar *dir, gint64 since_unix)
{
    (void) dir;
    (void) since_unix;

    return -1; //keine Cloud-Files-API
}

#endif
