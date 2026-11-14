/*
 sond (sond_seadrive_sync.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_seadrive_sync.h"

#ifdef G_OS_WIN32

#include <windows.h>

#include "sond_file_helper.h"

//aus cfapi.h (nicht eingebunden, die Funktion wird zur Laufzeit geladen)
#define CF_PLACEHOLDER_INFO_BASIC 0
#define CF_IN_SYNC_STATE_IN_SYNC 1
#define HRESULT_MORE_DATA_SYNC ((HRESULT)0x800700EAL)

//Toleranz für Zeitunterschiede zwischen Änderung und Beginn der Berechtigung
#define SINCE_SLACK 60

//Obergrenze der durchsuchten Einträge, danach "unbekannt"
#define MAX_ENTRIES 200000

typedef HRESULT (WINAPI *PFN_CfGetPlaceholderInfo)(HANDLE FileHandle,
        DWORD InfoClass, PVOID InfoBuffer, DWORD InfoBufferLength,
        PDWORD ReturnedLength);

//Aufbau wie SeaDrivePlaceholderBasicInfo in sond_seadrive.c
typedef struct {
    DWORD PinState;
    DWORD InSyncState;
    LARGE_INTEGER FileId;
    LARGE_INTEGER SyncRootFileId;
    ULONG FileIdentityLength;
    BYTE FileIdentity[256];
} BasicInfo;

static GOnce g_once = G_ONCE_INIT;

static gpointer load_cfapi(gpointer data) {
    HMODULE module = LoadLibraryA("cldapi.dll");

    (void) data;

    if (!module)
        return NULL;

    return (gpointer) GetProcAddress(module, "CfGetPlaceholderInfo");
}

static gboolean file_in_sync(PFN_CfGetPlaceholderInfo get_info,
        const gchar *path) {
    BasicInfo basic = { 0 };
    DWORD returned = 0;
    wchar_t *lp = prepare_long_path(path, NULL);
    HANDLE h = INVALID_HANDLE_VALUE;
    HRESULT hr = S_OK;

    if (!lp)
        return FALSE;

    h = CreateFileW(lp, FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    g_free(lp);
    if (h == INVALID_HANDLE_VALUE)
        return FALSE;

    hr = get_info(h, CF_PLACEHOLDER_INFO_BASIC, &basic, sizeof(basic),
            &returned);
    CloseHandle(h);

    if (FAILED(hr) && hr != HRESULT_MORE_DATA_SYNC)
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
static gboolean scan_dir(PFN_CfGetPlaceholderInfo get_info, const gchar *dir,
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
            ok = scan_dir(get_info, path, threshold, visited, unsynced);
        } else if (filetime_to_unix(&fd.ftLastWriteTime) >= threshold) {
            if (!file_in_sync(get_info, path))
                (*unsynced)++;
        }
        g_free(path);
    } while (ok && FindNextFileW(h, &fd));

    FindClose(h);

    return ok;
}

gint sond_seadrive_count_unsynced(const gchar *dir, gint64 since_unix) {
    PFN_CfGetPlaceholderInfo get_info = NULL;
    guint visited = 0;
    guint unsynced = 0;

    g_return_val_if_fail(dir != NULL, -1);

    get_info = (PFN_CfGetPlaceholderInfo) g_once(&g_once, load_cfapi, NULL);
    if (!get_info)
        return -1;

    if (!scan_dir(get_info, dir, since_unix - SINCE_SLACK, &visited, &unsynced))
        return -1;

    return (gint) unsynced;
}

#else /* kein Windows: keine Cloud-Files-API */

gint sond_seadrive_count_unsynced(const gchar *dir, gint64 since_unix) {
    (void) dir;
    (void) since_unix;

    return -1;
}

#endif
