/*
 * sond_treeviewfm_seadrive.c
 *
 * SeaDrive integration for SondTreeviewFM.
 * Windows-only - on Linux this compiles to an empty translation unit.
 *
 * Uses prepare_long_path() from sond_file_helper for consistent UTF-8
 * handling and long path support (>260 chars) throughout.
 */

#include "sond_treeviewfm_seadrive.h"

#ifdef _WIN32

#include <windows.h>
#include <glib.h>
#include <gtk/gtk.h>

#include "sond_treeview.h"
#include "sond_fileparts.h"
#include "sond_file_helper.h"
#include "sond_log_and_error.h"
#include "misc.h"

/* ------------------------------------------------------------------ */
/*  FILE_ATTRIBUTE_* not always defined in older MinGW headers        */
/* ------------------------------------------------------------------ */

#ifndef FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS
#define FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS  0x00400000
#endif
#ifndef FILE_ATTRIBUTE_PINNED
#define FILE_ATTRIBUTE_PINNED                 0x00080000
#endif
#ifndef FILE_ATTRIBUTE_UNPINNED
#define FILE_ATTRIBUTE_UNPINNED               0x00100000
#endif

/* ------------------------------------------------------------------ */
/*  CF-API constants (from cfapi.h, duplicated to avoid the header)   */
/* ------------------------------------------------------------------ */

#define CF_PIN_STATE_UNSPECIFIED     0
#define CF_PIN_STATE_PINNED          1
#define CF_PIN_STATE_UNPINNED        2
#define CF_PIN_STATE_EXCLUDED        3
#define CF_PIN_STATE_INHERIT         4

#define CF_SET_PIN_FLAG_NONE         0x00000000
#define CF_SET_PIN_FLAG_RECURSE      0x00000001

#define HRESULT_MORE_DATA      ((HRESULT)0x800700EAL)

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

/* ------------------------------------------------------------------ */
/*  Runtime-loaded CF-API pointers                                     */
/* ------------------------------------------------------------------ */

static PFN_CfSetPinState           g_CfSetPinState           = NULL;
static PFN_CfGetPlaceholderInfo    g_CfGetPlaceholderInfo    = NULL;
static PFN_CfGetSyncRootInfoByPath g_CfGetSyncRootInfoByPath = NULL;
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
    return g_hCldApi;
}

static void cfapi_init(void)
{
    g_once(&g_cfapi_once, cfapi_init_once, NULL);
}

/* ------------------------------------------------------------------ */
/*  CF-API Konstanten für Watcher                                      */
/* ------------------------------------------------------------------ */

#define CF_PLACEHOLDER_INFO_BASIC      0
#define CF_IN_SYNC_STATE_NOT_IN_SYNC   0
#define CF_IN_SYNC_STATE_IN_SYNC       1

typedef struct {
    DWORD         PinState;
    DWORD         InSyncState;
    LARGE_INTEGER FileId;
    LARGE_INTEGER SyncRootFileId;
    ULONG         FileIdentityLength;
    BYTE          FileIdentity[256];
} SeaDrivePlaceholderBasicInfo;

#define HRESULT_MORE_DATA  ((HRESULT)0x800700EAL)

/* ------------------------------------------------------------------ */
/*  Watcher: idle-Callback-Daten                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    SondTreeviewFM *stvfm;
    gint            delta_down;  /* +1 oder -1, 0 = keine Änderung */
    gchar          *path_down;       /* Pfad der hydrierten Datei, NULL sonst */
    gchar          *path_dehydrated; /* Pfad der dehydrierten Datei, NULL sonst */
    gchar          *path_up;     /* NULL oder Pfad für pending_up-Änderung */
    gboolean        up_pending;  /* TRUE=NOT_IN_SYNC, FALSE=IN_SYNC */
} WatcherIdleData;

static gboolean watcher_idle_cb(gpointer user_data)
{
    WatcherIdleData *d = user_data;

    sond_treeviewfm_seadrive_update_status(d->stvfm,
            d->delta_down,
            d->path_up, d->up_pending);

    /* Wenn Datei hydrated: Knoten im Baum korrigieren */
    if (d->path_down)
        sond_treeviewfm_seadrive_item_hydrated(d->stvfm, d->path_down);

    /* Wenn Datei dehydrated: Knoten im Baum zurückbauen */
    if (d->path_dehydrated)
        sond_treeviewfm_seadrive_item_dehydrated(d->stvfm, d->path_dehydrated);

    g_free(d->path_down);
    g_free(d->path_dehydrated);
    g_free(d->path_up);
    g_free(d);
    return G_SOURCE_REMOVE;
}

/* ------------------------------------------------------------------ */
/*  Watcher: Initialscan pending_down                                  */
/* ------------------------------------------------------------------ */

static guint watcher_count_pending_down(const gchar *dir_utf8,
        SondTreeviewFM *stvfm)
{
    guint count = 0;
    gchar *pattern = g_strconcat(dir_utf8, "/*", NULL);
    wchar_t *lp = prepare_long_path(pattern, NULL);
    g_free(pattern);
    if (!lp)
        return 0;

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(lp, &fd);
    g_free(lp);

    if (h == INVALID_HANDLE_VALUE)
        return 0;

    do {
        if (sond_treeviewfm_seadrive_stop_requested(stvfm))
            break;

        if (wcscmp(fd.cFileName, L".") == 0 ||
                wcscmp(fd.cFileName, L"..") == 0)
            continue;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            gchar *name = g_utf16_to_utf8(
                    (gunichar2*) fd.cFileName, -1, NULL, NULL, NULL);
            if (name) {
                gchar *sub = g_strconcat(dir_utf8, "/", name, NULL);
                g_free(name);
                count += watcher_count_pending_down(sub, stvfm);
                g_free(sub);
            }
        } else {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_PINNED) &&
                    (fd.dwFileAttributes & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS))
                count++;
        }
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    return count;
}

typedef struct {
    SondTreeviewFM *stvfm;
    guint           count;
} WatcherInitData;

static gboolean watcher_init_idle_cb(gpointer user_data)
{
    WatcherInitData *d = user_data;
    /* Zähler absolut setzen via update mit delta = count.
     * Da pending_down zu diesem Zeitpunkt noch 0 ist (nur der
     * Watcher-Thread hat ihn noch nicht verändert), ist
     * delta = count korrekt. Watcher-Events die während des
     * Scans gepuffert wurden, kommen danach und korrigieren. */
    sond_treeviewfm_seadrive_set_pending_down(d->stvfm, d->count);
    g_free(d);
    return G_SOURCE_REMOVE;
}

/* ------------------------------------------------------------------ */
/*  Watcher: Hilfsfunktionen für Thread                               */
/* ------------------------------------------------------------------ */

static gboolean watcher_check_in_sync(const gchar *utf8_path)
{
    SeaDrivePlaceholderBasicInfo basic = { 0 };
    DWORD returned = 0;
    HRESULT hr;
    HANDLE h;
    wchar_t *lp;

    if (!g_CfGetPlaceholderInfo)
        return TRUE; /* im Zweifel: in sync */

    lp = prepare_long_path(utf8_path, NULL);
    if (!lp)
        return TRUE;

    h = CreateFileW(lp, FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    g_free(lp);

    if (h == INVALID_HANDLE_VALUE)
        return TRUE;

    hr = g_CfGetPlaceholderInfo(h, CF_PLACEHOLDER_INFO_BASIC,
            &basic, sizeof(basic), &returned);
    CloseHandle(h);

    if (FAILED(hr) && hr != HRESULT_MORE_DATA)
        return TRUE;

    return basic.InSyncState == CF_IN_SYNC_STATE_IN_SYNC;
}

/* ------------------------------------------------------------------ */
/*  Watcher-Thread                                                      */
/* ------------------------------------------------------------------ */

gpointer sond_treeviewfm_seadrive_watcher_thread(gpointer user_data)
{
    SondTreeviewFM *stvfm = SOND_TREEVIEWFM(user_data);

    /* CF-API initialisieren - muss vor dem Thread-Start erfolgen */
    cfapi_init();

    const gchar *root = sond_treeviewfm_get_root(stvfm);
    if (!root)
        return NULL;

    /* Verzeichnis-Handle für ReadDirectoryChangesW */
    wchar_t *root_w = prepare_long_path(root, NULL);
    if (!root_w)
        return NULL;

    HANDLE hDir = CreateFileW(root_w,
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            NULL);
    g_free(root_w);

    if (hDir == INVALID_HANDLE_VALUE)
        return NULL;

    /* OVERLAPPED + Event für asynchrones ReadDirectoryChangesW */
    OVERLAPPED ov = { 0 };
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent) {
        CloseHandle(hDir);
        return NULL;
    }

    BYTE buf[32768];
    DWORD bytes_returned = 0;

    /* Ersten ReadDirectoryChangesW-Aufruf starten - VOR dem Scan,
     * damit während des Scans entstehende Events nicht verloren gehen */
    ReadDirectoryChangesW(hDir, buf, sizeof(buf), TRUE,
            FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_LAST_WRITE,
            NULL, &ov, NULL);

    /* Initialscan: zählt bereits vorhandene PINNED+offline Dateien.
     * ReadDirectoryChangesW läuft bereits - Events während des Scans
     * werden gepuffert und danach verarbeitet (selbstkorrigierend). */
    if (!sond_treeviewfm_seadrive_stop_requested(stvfm)) {
        guint initial = watcher_count_pending_down(root, stvfm);
        WatcherInitData *d = g_new0(WatcherInitData, 1);
        d->stvfm = stvfm;
        d->count = initial;
        g_idle_add(watcher_init_idle_cb, d);
    }

    while (!sond_treeviewfm_seadrive_stop_requested(stvfm)) {

        /* 500ms-Timeout für reaktionsschnelles Stoppen */
        DWORD wait = WaitForSingleObject(ov.hEvent, 500);

        if (wait == WAIT_OBJECT_0) {
            /* Ereignisse verarbeiten */
            if (GetOverlappedResult(hDir, &ov, &bytes_returned, FALSE)
                    && bytes_returned > 0) {

                FILE_NOTIFY_INFORMATION *fni =
                        (FILE_NOTIFY_INFORMATION*) buf;

                do {
                    /* Dateiname von UTF-16 nach UTF-8 */
                    GError *conv_error = NULL;
                    gchar *filename = g_utf16_to_utf8(
                            (gunichar2*) fni->FileName,
                            fni->FileNameLength / sizeof(WCHAR),
                            NULL, NULL, &conv_error);
                    if (!filename) {
                        LOG_WARN("UTF-16->UTF-8 Konvertierung fehlgeschlagen: %s",
                                conv_error ? conv_error->message : "?");
                        g_error_free(conv_error);
                    }

                    if (filename) {
                        /* Schrägstriche vereinheitlichen */
                        for (gchar *c = filename; *c; c++)
                            if (*c == '\\')
                                *c = '/';

                        gchar *full = g_strconcat(root, "/", filename, NULL);
                        g_free(filename);

                        wchar_t *lp = prepare_long_path(full, NULL);
                        if (lp) {
                            DWORD attrs = GetFileAttributesW(lp);
                            g_free(lp);

                            if (attrs != INVALID_FILE_ATTRIBUTES &&
                                    !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {

                                WatcherIdleData *d = g_new0(WatcherIdleData, 1);
                                d->stvfm = stvfm;

                                /* ATTRIBUTES: pending_down aktualisieren */
                                {
                                    gboolean pinned   = (attrs & FILE_ATTRIBUTE_PINNED) != 0;
                                    gboolean offline  = (attrs & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS) != 0;
                                    gboolean unpinned = (attrs & FILE_ATTRIBUTE_UNPINNED) != 0;

                                    if (pinned && offline)
                                        d->delta_down = +1; /* gepinnt, noch nicht lokal */
                                    else if (pinned && !offline) {
                                        d->delta_down = -1; /* hydrated via pin */
                                        d->path_down  = g_strdup(full);
                                    } else if (unpinned && offline) {
                                        /* Datei dehydriert - war nicht mehr PINNED+offline,
                                         * also nicht im Zähler - delta_down nicht setzen */
                                        d->path_dehydrated = g_strdup(full);
                                    } else if (!pinned && offline)
                                        d->delta_down = -1; /* unpinned während laufendem Download */
                                    else if (!unpinned) {
                                        /* !pinned && !offline && !unpinned:
                                         * Hydration via Doppelklick-Recall -
                                         * Knoten im Baum korrigieren */
                                        d->path_down = g_strdup(full);
                                    }
                                    /* !pinned && !offline && unpinned:
                                     * Dehydration abgeschlossen - nichts tun */
                                }

                                /* LAST_WRITE: In-Sync-Status prüfen */
                                {
                                    gboolean in_sync = watcher_check_in_sync(full);
                                    d->path_up = g_strdup(full);
                                    d->up_pending = !in_sync;
                                }

								g_idle_add(watcher_idle_cb, d);
                            }
                        }
                        g_free(full);
                    }

                    if (fni->NextEntryOffset == 0)
                        break;
                    fni = (FILE_NOTIFY_INFORMATION*)
                            ((BYTE*) fni + fni->NextEntryOffset);
                } while (1);
            }

            /* Nächsten ReadDirectoryChangesW-Aufruf starten */
            ResetEvent(ov.hEvent);
            ReadDirectoryChangesW(hDir, buf, sizeof(buf), TRUE,
                    FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_LAST_WRITE,
                    NULL, &ov, NULL);
        }
    }

    /* Aufräumen */
    CancelIo(hDir);
    CloseHandle(ov.hEvent);
    CloseHandle(hDir);

    return NULL;
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

/* ------------------------------------------------------------------ */
/*  Internal: build full UTF-8 path from stvfm_item                   */
/* ------------------------------------------------------------------ */

static gchar *stvfm_item_get_full_path(SondTVFMItem *stvfm_item)
{
    const gchar *root;
    const gchar *rel;
    SondFilePart *sfp;

    root = sond_treeviewfm_get_root(sond_tvfm_item_get_stvfm(stvfm_item));
    if (!root)
        return NULL;

    /* fs dirs: path_or_section holds relative path */
    rel = sond_tvfm_item_get_path_or_section(stvfm_item);
    if (rel && *rel)
        return g_strconcat(root, "/", rel, NULL);

    /* fs leaf files: path_or_section is NULL, sfp holds relative path */
    sfp = sond_tvfm_item_get_sond_file_part(stvfm_item);
    if (sfp) {
        const gchar *sfp_path = sond_file_part_get_path(sfp);
        if (sfp_path && *sfp_path)
            return g_strconcat(root, "/", sfp_path, NULL);
    }

    /* root directory itself */
    return g_strdup(root);
}

/* ------------------------------------------------------------------ */
/*  Internal: PINNED- oder UNPINNED-Attribut bei Elternverzeichnissen löschen */
/* ------------------------------------------------------------------ */

static void clear_attr_on_parents(const gchar *full_path, const gchar *root,
        DWORD attr_to_check)
{
    gchar *dir = g_path_get_dirname(full_path);

    while (dir && g_strcmp0(dir, root) != 0 && g_strcmp0(dir, ".") != 0) {
        wchar_t *lp = prepare_long_path(dir, NULL);
        if (lp) {
            DWORD attrs = GetFileAttributesW(lp);
            g_free(lp);
            if (attrs != INVALID_FILE_ATTRIBUTES &&
                    (attrs & attr_to_check)) {
                GError *error = NULL;
                if (!sond_seadrive_set_pin_state(dir,
                        STVFM_PIN_STATE_UNSPECIFIED, FALSE, &error))
                    g_clear_error(&error);
            } else
                break; /* Attribut nicht gesetzt - weiter aufsteigen unnötig */
        }
        gchar *parent = g_path_get_dirname(dir);
        g_free(dir);
        dir = parent;
    }
    g_free(dir);
}

/* ------------------------------------------------------------------ */
/*  Internal: apply pin state to one item                             */
/* ------------------------------------------------------------------ */

static void apply_pin_state_to_item(SondTVFMItem *stvfm_item, guint pin_state)
{
    SondFilePart *sfp;
    gchar *full_path;
    GError *error = NULL;
    gboolean is_dir;

    sfp = sond_tvfm_item_get_sond_file_part(stvfm_item);

    /*
     * Skip embedded entries (section inside PDF/ZIP/GMessage).
     * These have sfp != NULL AND path_or_section != NULL.
     * Act on:
     *   sfp == NULL              -> fs directory
     *   sfp != NULL && path_or_section == NULL -> the file itself on the filesystem
     */
    if (sfp != NULL && sond_tvfm_item_get_path_or_section(stvfm_item) != NULL)
        return;

    full_path = stvfm_item_get_full_path(stvfm_item);
    if (!full_path)
        return;

    is_dir = (sond_tvfm_item_get_item_type(stvfm_item) == SOND_TVFM_ITEM_TYPE_DIR);

    if (!sond_seadrive_set_pin_state(full_path, pin_state, is_dir, &error)) {
        LOG_WARN("SeaDrive set_pin_state('%s'): %s",
                 full_path, error ? error->message : "?");
        g_clear_error(&error);
    }
    else if (pin_state != STVFM_PIN_STATE_PINNED) {
        // PINNED bei allen Elternverzeichnissen löschen
        const gchar *root = sond_treeviewfm_get_root(
                sond_tvfm_item_get_stvfm(stvfm_item));
        if (root)
            clear_attr_on_parents(full_path, root, FILE_ATTRIBUTE_PINNED);
    } else {
        //PINNED gesetzt - UNPINNED bei allen Elternverzeichnissen löschen
        const gchar *root = sond_treeviewfm_get_root(
                sond_tvfm_item_get_stvfm(stvfm_item));
        if (root)
            clear_attr_on_parents(full_path, root, FILE_ATTRIBUTE_UNPINNED);
    }

    g_free(full_path);
}

/* ------------------------------------------------------------------ */
/*  Internal: foreach callback                                        */
/* ------------------------------------------------------------------ */

static gint seadrive_pin_foreach(SondTreeview *stv, GtkTreeIter *iter,
                                 gpointer data, GError **error)
{
    SondTVFMItem *item = NULL;
    (void)error;
    gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(stv)),
                       iter, 0, &item, -1);
    if (item) {
        apply_pin_state_to_item(item, GPOINTER_TO_UINT(data));
        g_object_unref(item);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Internal: apply to root directory                                 */
/* ------------------------------------------------------------------ */

static void apply_pin_state_to_root(SondTreeviewFM *stvfm, guint pin_state)
{
    const gchar *root = sond_treeviewfm_get_root(stvfm);
    GError *error = NULL;

    if (!root)
        return;

    if (!sond_seadrive_set_pin_state(root, pin_state, TRUE, &error)) {
        display_message(gtk_widget_get_toplevel(GTK_WIDGET(stvfm)),
                        "SeaDrive: Fehler\n\n",
                        error ? error->message : "Unbekannter Fehler", NULL);
        g_clear_error(&error);
    }
}

/* ------------------------------------------------------------------ */
/*  Context menu callback                                             */
/* ------------------------------------------------------------------ */

static void seadrive_menu_activate(GtkMenuItem *item, gpointer data)
{
    SondTreeviewFM *stvfm = SOND_TREEVIEWFM(data);
    guint    pin_state = (guint)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "pin_state"));
    gboolean sel_only  = (gboolean)GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "sel"));
    GError  *error = NULL;

    if (sel_only) {
        if (!gtk_tree_selection_count_selected_rows(
                gtk_tree_view_get_selection(GTK_TREE_VIEW(stvfm)))) {
            display_message(gtk_widget_get_toplevel(GTK_WIDGET(stvfm)),
                            "Keine Punkte ausgewaehlt", NULL);
            return;
        }
        gint rc = sond_treeview_selection_foreach(SOND_TREEVIEW(stvfm),
                      seadrive_pin_foreach, GUINT_TO_POINTER(pin_state), &error);
        if (rc == -1) {
            display_message(gtk_widget_get_toplevel(GTK_WIDGET(stvfm)),
                            "SeaDrive: Fehler\n\n",
                            error ? error->message : "", NULL);
            g_clear_error(&error);
        }
    } else {
        apply_pin_state_to_root(stvfm, pin_state);
    }

    /* Neuzeichnen nur bei UNSPECIFIED/UNPINNED nötig - bei PINNED
     * übernimmt der Watcher das Neuzeichnen via Attribut-Events */
    if (pin_state != STVFM_PIN_STATE_PINNED)
        gtk_widget_queue_draw(GTK_WIDGET(stvfm));
}

/* ------------------------------------------------------------------ */
/*  Helper: create one menu item                                      */
/* ------------------------------------------------------------------ */

static GtkWidget *make_pin_item(const gchar *label, guint pin_state,
                                gboolean sel_only, SondTreeviewFM *stvfm)
{
    GtkWidget *item = gtk_menu_item_new_with_label(label);
    g_object_set_data(G_OBJECT(item), "pin_state", GINT_TO_POINTER((gint)pin_state));
    g_object_set_data(G_OBJECT(item), "sel",       GINT_TO_POINTER((gint)sel_only));
    g_signal_connect(item, "activate", G_CALLBACK(seadrive_menu_activate), stvfm);
    return item;
}

/* ------------------------------------------------------------------ */
/*  Public: attach SeaDrive submenu to context menu                   */
/* ------------------------------------------------------------------ */

void sond_treeviewfm_seadrive_init_contextmenu(SondTreeviewFM *stvfm)
{
    GtkWidget *contextmenu = sond_treeview_get_contextmenu(SOND_TREEVIEW(stvfm));
    GtkWidget *item_sep    = gtk_separator_menu_item_new();
    GtkWidget *item_top    = gtk_menu_item_new_with_label("SeaDrive");
    GtkWidget *menu_sub    = gtk_menu_new();

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_top), menu_sub);

    /* --- Immer offline verfügbar --- */
    GtkWidget *item_pin    = gtk_menu_item_new_with_label("Immer offline verf\u00fcgbar");
    GtkWidget *menu_pin    = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_pin), menu_pin);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_pin),
        make_pin_item("Gesamtes Verzeichnis", STVFM_PIN_STATE_PINNED, FALSE, stvfm));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_pin),
        make_pin_item("Auswahl", STVFM_PIN_STATE_PINNED, TRUE, stvfm));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_sub), item_pin);

    /* --- Offline verfügbar aufheben --- */
    GtkWidget *item_unspec    = gtk_menu_item_new_with_label("Offline verf\u00fcgbar aufheben");
    GtkWidget *menu_unspec    = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_unspec), menu_unspec);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_unspec),
        make_pin_item("Gesamtes Verzeichnis", STVFM_PIN_STATE_UNSPECIFIED, FALSE, stvfm));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_unspec),
        make_pin_item("Auswahl", STVFM_PIN_STATE_UNSPECIFIED, TRUE, stvfm));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_sub), item_unspec);

    /* --- Cache leeren --- */
    GtkWidget *item_unpin    = gtk_menu_item_new_with_label("Cache leeren");
    GtkWidget *menu_unpin    = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_unpin), menu_unpin);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_unpin),
        make_pin_item("Gesamtes Verzeichnis", STVFM_PIN_STATE_UNPINNED, FALSE, stvfm));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_unpin),
        make_pin_item("Auswahl", STVFM_PIN_STATE_UNPINNED, TRUE, stvfm));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_sub), item_unpin);

    gtk_menu_shell_append(GTK_MENU_SHELL(contextmenu), item_sep);
    gtk_menu_shell_append(GTK_MENU_SHELL(contextmenu), item_top);
    gtk_widget_show_all(item_top);
    gtk_widget_show(item_sep);
    /* initial deaktiviert - wird in set_root aktiviert wenn SeaDrive-Pfad */
    gtk_widget_set_sensitive(item_top, FALSE);
    g_object_set_data(G_OBJECT(stvfm), "seadrive-menu-item", item_top);
}

#endif /* _WIN32 */
