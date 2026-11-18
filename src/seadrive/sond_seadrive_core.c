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


/* ------------------------------------------------------------------ */
/*  Hydrierung im Hintergrund (Threads, Pool, Abbruch)                */
/* ------------------------------------------------------------------ */

/* Menge der Pfade, für die aktuell ein Hydrier-Hintergrund-Thread läuft; verhindert
 * einen zweiten, redundanten CfHydratePlaceholder()-Aufruf für dieselbe Datei. Der
 * synchrone Aufruf von sond_seadrive_hydrate() blockierte bei sehr großen Dateien
 * (z.B. 51 GB) das ganze Programm minutenlang ohne Rückmeldung oder Abbrechen. Wert je
 * Pfad: HydratingEntry. Von Haupt- und Hintergrund-Threads genutzt, deshalb per
 * Mutex geschützt (statisch/all-zero-initialisiert, für GMutex zulässig). */
static GMutex      g_hydrating_mutex;
static GHashTable *g_hydrating_paths = NULL;

typedef struct {
    HANDLE   thread_handle;    /* NULL, bis der Hydrier-Thread wirklich läuft */
    gboolean cancel_requested; /* per sond_seadrive_hydrate_cancel() gesetzt; ist
                                * thread_handle noch NULL, startet der Thread den
                                * Download gar nicht erst */
} HydratingEntry;

static void hydrating_entry_free(gpointer data)
{
    HydratingEntry *entry = (HydratingEntry *) data;

    if (entry->thread_handle)
        CloseHandle(entry->thread_handle);
    g_free(entry);
}

typedef struct {
    gchar *full_path;
    SondSeadriveHydrateFailedFunc failed;
    gpointer user_data;
    GDestroyNotify destroy;
} HydrateThreadData;

typedef struct {
    gchar *full_path;
    gchar *message; /* NULL: kein Fehler (nur aufräumen) */
    SondSeadriveHydrateFailedFunc failed;
    gpointer user_data;
    GDestroyNotify destroy;
} HydrateReport;

//im Hauptthread: Fehler melden, danach user_data freigeben
static gboolean hydrate_report_idle(gpointer data)
{
    HydrateReport *report = (HydrateReport *) data;

    if (report->message && report->failed)
        report->failed(report->full_path, report->message, report->user_data);
    if (report->destroy)
        report->destroy(report->user_data);

    g_free(report->full_path);
    g_free(report->message);
    g_free(report);

    return G_SOURCE_REMOVE;
}

static void hydrate_run(HydrateThreadData *td)
{
    GError *error = NULL;
    HydratingEntry *entry;
    HANDLE thread_handle_dup = NULL;
    gboolean pre_cancelled = FALSE;
    gchar *message = NULL;

    /* Eigenes Thread-Handle (mit THREAD_TERMINATE-Recht, s. CancelSynchronousIo()) für
     * einen möglichen späteren Abbruch von außen bereitstellen. */
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
            GetCurrentProcess(), &thread_handle_dup, THREAD_TERMINATE,
            FALSE, 0);

    g_mutex_lock(&g_hydrating_mutex);
    entry = g_hash_table_lookup(g_hydrating_paths, td->full_path);
    if (entry) {
        if (entry->cancel_requested)
            pre_cancelled = TRUE;
        else
            entry->thread_handle = thread_handle_dup;
    }
    g_mutex_unlock(&g_hydrating_mutex);

    if (pre_cancelled) {
        //Abbruch kam schon vor dem Eintragen des Handles: Download nicht starten
        if (thread_handle_dup)
            CloseHandle(thread_handle_dup);
    } else if (!sond_seadrive_hydrate(td->full_path, &error)) {
        /* Bei per CancelSynchronousIo() abgebrochenen Aufrufen liefert
         * GetLastError() i.d.R. ERROR_OPERATION_ABORTED; das wird wie jeder
         * andere Fehler geloggt, aber nicht gemeldet, wenn der Nutzer abbrach. */
        g_warning("sond_seadrive_hydrate('%s'): %s", td->full_path,
                error ? error->message : "?");

        g_mutex_lock(&g_hydrating_mutex);
        entry = g_hash_table_lookup(g_hydrating_paths, td->full_path);
        pre_cancelled = entry && entry->cancel_requested;
        g_mutex_unlock(&g_hydrating_mutex);

        if (td->failed && !pre_cancelled)
            message = g_strdup(error ? error->message : "unbekannter Fehler");
        g_clear_error(&error);
    }

    g_mutex_lock(&g_hydrating_mutex);
    g_hash_table_remove(g_hydrating_paths, td->full_path); /* schließt
            thread_handle_dup über hydrating_entry_free(), falls gesetzt */
    g_mutex_unlock(&g_hydrating_mutex);

    if (td->failed || td->destroy) {
        HydrateReport *report = g_new0(HydrateReport, 1);

        report->full_path = g_strdup(td->full_path);
        report->message = message;
        report->failed = td->failed;
        report->user_data = td->user_data;
        report->destroy = td->destroy;
        g_idle_add(hydrate_report_idle, report);
    } else
        g_free(message);

    g_free(td->full_path);
    g_free(td);
}

static gpointer hydrate_thread_func(gpointer data)
{
    hydrate_run((HydrateThreadData *) data);

    return NULL;
}

/* Bei vielen Dateien (Kopieren, Auszug) nicht ein Thread je Datei, sondern ein Pool mit
 * wenigen Arbeitern: SeaDrive lädt ohnehin nicht beliebig parallel, und Hunderte
 * gleichzeitige CfHydratePlaceholder()-Aufrufe belasten Rechner und Server. Die
 * Dateien gelten ab dem Einreihen als "wird geladen". */
#define HYDRATE_POOL_THREADS 4

static void hydrate_pool_func(gpointer data, gpointer user_data)
{
    (void) user_data;
    hydrate_run((HydrateThreadData *) data);
}

static GThreadPool *g_hydrate_pool = NULL;

static GThreadPool* hydrate_pool_get(void)
{
    g_mutex_lock(&g_hydrating_mutex);
    if (!g_hydrate_pool)
        g_hydrate_pool = g_thread_pool_new(hydrate_pool_func, NULL,
                HYDRATE_POOL_THREADS, FALSE, NULL);
    g_mutex_unlock(&g_hydrating_mutex);

    return g_hydrate_pool;
}

gboolean sond_seadrive_hydrate_start(const gchar *full_path, gboolean use_pool,
        SondSeadriveHydrateFailedFunc failed, gpointer user_data,
        GDestroyNotify destroy)
{
    HydrateThreadData *td = NULL;

    g_mutex_lock(&g_hydrating_mutex);
    if (!g_hydrating_paths)
        g_hydrating_paths = g_hash_table_new_full(g_str_hash, g_str_equal,
                g_free, hydrating_entry_free);

    if (g_hash_table_contains(g_hydrating_paths, full_path)) {
        //schon ein Hydrier-Thread für diese Datei unterwegs
        g_mutex_unlock(&g_hydrating_mutex);
        if (destroy)
            destroy(user_data);

        return FALSE;
    }
    g_hash_table_insert(g_hydrating_paths, g_strdup(full_path),
            g_new0(HydratingEntry, 1));
    g_mutex_unlock(&g_hydrating_mutex);

    td = g_new0(HydrateThreadData, 1);
    td->full_path = g_strdup(full_path);
    td->failed = failed;
    td->user_data = user_data;
    td->destroy = destroy;

    if (use_pool) {
        g_thread_pool_push(hydrate_pool_get(), td, NULL);

        return TRUE;
    }

    {
        GThread *thread = g_thread_new("seadrive-hydrate", hydrate_thread_func, td);

        if (!thread) {
            //sonst bliebe die Datei für immer fälschlich als "läuft schon" markiert
            g_mutex_lock(&g_hydrating_mutex);
            g_hash_table_remove(g_hydrating_paths, td->full_path);
            g_mutex_unlock(&g_hydrating_mutex);
            if (destroy)
                destroy(user_data);
            g_free(td->full_path);
            g_free(td);

            return FALSE;
        }
        g_thread_unref(thread); //fire-and-forget
    }

    return TRUE;
}

gboolean sond_seadrive_is_hydrating(const gchar *full_path)
{
    gboolean result;

    g_mutex_lock(&g_hydrating_mutex);
    result = g_hydrating_paths &&
            g_hash_table_contains(g_hydrating_paths, full_path);
    g_mutex_unlock(&g_hydrating_mutex);

    return result;
}

/* Versucht, eine laufende Hydrierung abzubrechen: CancelSynchronousIo() auf das Handle
 * des Hydrier-Threads (er steckt blockierend in CfHydratePlaceholder()). Für diese
 * Funktion ist das offiziell nicht dokumentiert; ob SeaDrive den Abbruch zeitnah
 * beachtet, ist nicht garantiert. Erfolg zeigt sich daran, dass der Eintrag aus
 * g_hydrating_paths verschwindet. */
void sond_seadrive_hydrate_cancel(const gchar *full_path)
{
    HydratingEntry *entry;

    g_mutex_lock(&g_hydrating_mutex);
    entry = g_hydrating_paths ?
            g_hash_table_lookup(g_hydrating_paths, full_path) : NULL;
    if (entry) {
        //immer merken: abgebrochene Dateien gelten nicht als gescheitert
        entry->cancel_requested = TRUE;
        if (entry->thread_handle)
            CancelSynchronousIo(entry->thread_handle);
    }
    g_mutex_unlock(&g_hydrating_mutex);
}

/* ------------------------------------------------------------------ */
/*  Lade-Auftrag                                                       */
/* ------------------------------------------------------------------ */

/* CF_PLACEHOLDER_INFO_STANDARD liefert u.a. OnDiskDataSize (bereits lokal vorhandene
 * Bytes); Layout 1:1 aus cfapi.h (kein cfapi.h im MinGW-Toolchain). FileIdentity ist dort
 * BYTE[1], muss aber groß genug sein, sonst liefert CfGetPlaceholderInfo()
 * HRESULT_MORE_DATA (ein FAILURE-HRESULT) und OnDiskDataSize würde nie ausgewertet. */
#define CF_PLACEHOLDER_INFO_STANDARD 1

typedef struct {
    LARGE_INTEGER OnDiskDataSize;
    LARGE_INTEGER ValidatedDataSize;
    LARGE_INTEGER ModifiedDataSize;
    LARGE_INTEGER PropertiesSize;
    DWORD         PinState;
    DWORD         InSyncState;
    LARGE_INTEGER FileId;
    LARGE_INTEGER SyncRootFileId;
    ULONG         FileIdentityLength;
    BYTE          FileIdentity[256];
} SeaDrivePlaceholderStandardInfo;

//Ab so vielen Dateien Sammelanzeige; ab HYDRATE_POOL_MIN_FILES der Pool
#define HYDRATE_POOL_MIN_FILES        8
#define LOAD_AGGREGATE_MIN            20
//Sammelanzeige: je Abfrage höchstens so viele laufende Dateien nach dem Fortschritt fragen
#define LOAD_QUERY_MAX                8

struct _SondSeadriveLoadJob {
    GPtrArray *entries; /* SondSeadriveLoadEntry*, eigene Kopien */
    guint64    total_bytes;
    gboolean   aggregate;
    gboolean   cancelled;
    gboolean   logged_failure;
};

static guint64 load_file_size(const gchar *full_path)
{
    wchar_t *lp = prepare_long_path(full_path, NULL);
    WIN32_FILE_ATTRIBUTE_DATA data;
    guint64 size = 0;

    if (!lp)
        return 0;

    if (GetFileAttributesExW(lp, GetFileExInfoStandard, &data))
        size = ((guint64) data.nFileSizeHigh << 32) | data.nFileSizeLow;
    g_free(lp);

    return size;
}

//lokal vorhandene Bytes eines Eintrags per CfGetPlaceholderInfo()
static void load_entry_query(SondSeadriveLoadJob *job, SondSeadriveLoadEntry *entry)
{
    PFN_CfGetPlaceholderInfo get_info = (PFN_CfGetPlaceholderInfo)
            sond_seadrive_core_placeholder_info();
    wchar_t *lp;
    HANDLE   h;

    if (!get_info)
        return;

    lp = prepare_long_path(entry->full_path, NULL);
    if (!lp)
        return;

    h = CreateFileW(lp, FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    g_free(lp);
    if (h == INVALID_HANDLE_VALUE)
        return;

    {
        SeaDrivePlaceholderStandardInfo info = { 0 };
        DWORD returned_length = 0;
        HRESULT hr = get_info(h, CF_PLACEHOLDER_INFO_STANDARD, &info,
                sizeof(info), &returned_length);

        if (SUCCEEDED(hr))
            entry->on_disk = (guint64) info.OnDiskDataSize.QuadPart;
        else if (!job->logged_failure) {
            g_warning("CfGetPlaceholderInfo('%s'): 0x%08lX", entry->full_path,
                    (unsigned long) hr);
            job->logged_failure = TRUE;
        }
    }

    CloseHandle(h);
}

static void load_entry_free(gpointer data)
{
    SondSeadriveLoadEntry *entry = (SondSeadriveLoadEntry *) data;

    g_free(entry->full_path);
    g_free(entry);
}

SondSeadriveLoadJob* sond_seadrive_load_job_new(GPtrArray *full_paths)
{
    SondSeadriveLoadJob *job = NULL;
    GPtrArray *pending = NULL;
    gboolean use_pool = FALSE;

    if (!full_paths || full_paths->len == 0)
        return NULL;

    pending = g_ptr_array_new();
    for (guint i = 0; i < full_paths->len; i++) {
        const gchar *path = g_ptr_array_index(full_paths, i);

        if (path && sond_seadrive_needs_hydration(path))
            g_ptr_array_add(pending, (gpointer) path);
    }

    if (pending->len == 0) {
        g_ptr_array_free(pending, TRUE);

        return NULL;
    }

    use_pool = (pending->len >= HYDRATE_POOL_MIN_FILES);
    job = g_new0(SondSeadriveLoadJob, 1);
    job->entries = g_ptr_array_new_with_free_func(load_entry_free);
    job->aggregate = (pending->len > LOAD_AGGREGATE_MIN);

    for (guint i = 0; i < pending->len; i++) {
        const gchar *path = g_ptr_array_index(pending, i);
        SondSeadriveLoadEntry *entry = g_new0(SondSeadriveLoadEntry, 1);

        sond_seadrive_hydrate_start(path, use_pool, NULL, NULL, NULL);

        entry->full_path = g_strdup(path);
        entry->file_size = load_file_size(path);
        job->total_bytes += entry->file_size;
        g_ptr_array_add(job->entries, entry);
    }

    g_ptr_array_free(pending, TRUE);
    sond_seadrive_load_job_poll(job);

    return job;
}

void sond_seadrive_load_job_poll(SondSeadriveLoadJob *job)
{
    guint queried = 0;

    g_return_if_fail(job);

    for (guint i = 0; i < job->entries->len; i++) {
        SondSeadriveLoadEntry *entry = g_ptr_array_index(job->entries, i);

        if (!entry->done && !sond_seadrive_is_hydrating(entry->full_path)) {
            entry->done = TRUE;
            entry->ok = !sond_seadrive_needs_hydration(entry->full_path);
            if (entry->ok)
                entry->on_disk = entry->file_size;
        }

        if (!entry->done && (!job->aggregate || queried < LOAD_QUERY_MAX)) {
            load_entry_query(job, entry);
            queried++;
        }
    }
}

gboolean sond_seadrive_load_job_all_done(SondSeadriveLoadJob *job)
{
    g_return_val_if_fail(job, TRUE);

    for (guint i = 0; i < job->entries->len; i++)
        if (!((SondSeadriveLoadEntry *) g_ptr_array_index(job->entries, i))->done)
            return FALSE;

    return TRUE;
}

guint sond_seadrive_load_job_n_entries(SondSeadriveLoadJob *job)
{
    g_return_val_if_fail(job, 0);

    return job->entries->len;
}

SondSeadriveLoadEntry const* sond_seadrive_load_job_entry(SondSeadriveLoadJob *job,
        guint index)
{
    g_return_val_if_fail(job && index < job->entries->len, NULL);

    return g_ptr_array_index(job->entries, index);
}

guint sond_seadrive_load_job_n_done(SondSeadriveLoadJob *job)
{
    guint done = 0;

    g_return_val_if_fail(job, 0);

    for (guint i = 0; i < job->entries->len; i++)
        if (((SondSeadriveLoadEntry *) g_ptr_array_index(job->entries, i))->done)
            done++;

    return done;
}

guint64 sond_seadrive_load_job_total_bytes(SondSeadriveLoadJob *job)
{
    g_return_val_if_fail(job, 0);

    return job->total_bytes;
}

guint64 sond_seadrive_load_job_loaded_bytes(SondSeadriveLoadJob *job)
{
    guint64 bytes = 0;

    g_return_val_if_fail(job, 0);

    for (guint i = 0; i < job->entries->len; i++)
        bytes += ((SondSeadriveLoadEntry *) g_ptr_array_index(job->entries, i))->on_disk;

    return MIN(bytes, job->total_bytes);
}

gboolean sond_seadrive_load_job_is_aggregate(SondSeadriveLoadJob *job)
{
    g_return_val_if_fail(job, FALSE);

    return job->aggregate;
}

void sond_seadrive_load_job_cancel(SondSeadriveLoadJob *job)
{
    g_return_if_fail(job);

    if (job->cancelled)
        return;
    job->cancelled = TRUE;

    for (guint i = 0; i < job->entries->len; i++) {
        SondSeadriveLoadEntry *entry = g_ptr_array_index(job->entries, i);

        if (!entry->done)
            sond_seadrive_hydrate_cancel(entry->full_path);
    }
}

gboolean sond_seadrive_load_job_cancelled(SondSeadriveLoadJob *job)
{
    g_return_val_if_fail(job, FALSE);

    return job->cancelled;
}

GPtrArray* sond_seadrive_load_job_take_failed(SondSeadriveLoadJob *job)
{
    GPtrArray *failed = g_ptr_array_new_with_free_func(g_free);

    g_return_val_if_fail(job, failed);

    for (guint i = 0; i < job->entries->len; i++) {
        SondSeadriveLoadEntry *entry = g_ptr_array_index(job->entries, i);

        if (!entry->ok)
            g_ptr_array_add(failed, g_path_get_basename(entry->full_path));
    }

    return failed;
}

void sond_seadrive_load_job_free(SondSeadriveLoadJob *job)
{
    if (!job)
        return;

    g_ptr_array_free(job->entries, TRUE);
    g_free(job);
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
