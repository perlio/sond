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
    gchar          *path_pending_down; /* Pfad, auf den sich delta_down bezieht
                                         * (immer gesetzt, außer bei einer
                                         * Dehydration ohne Zähler-Änderung -
                                         * dann bleibt delta_down einfach 0
                                         * und update_status ignoriert ihn).
                                         * Wird - da ohnehin immer gesetzt -
                                         * auch als Pfad für die Ordner-
                                         * Coverage-Aktualisierung
                                         * wiederverwendet (s. coverage_*
                                         * unten). */
    gint            delta_down;  /* +1 oder -1, 0 = keine Änderung - NUR für
                                   * den Projekt-weiten "wird gerade
                                   * heruntergeladen"-Zähler
                                   * (seadrive_pending_down), s.
                                   * sond_treeviewfm_seadrive_update_status(). */
    gint            delta_total; /* +1 (ADDED/RENAMED_NEW_NAME) / -1 (REMOVED/
                                   * RENAMED_OLD_NAME) / 0 (MODIFIED - Datei
                                   * existierte schon) - für den Ordner-
                                   * Coverage-Badge (Gesamtzahl Dateien im
                                   * Teilbaum). */
    SondSeadriveBadge coverage_badge; /* aktueller Badge-Wert der Datei
                                        * (NONE bei REMOVED - Datei zählt
                                        * dann gar nicht mehr mit, s.
                                        * delta_total). Wird sowohl für den
                                        * Datei-eigenen Badge (Ground-Truth-
                                        * Map) als auch zur Herleitung der
                                        * Ordner-Coverage-Deltas verwendet,
                                        * s. sond_treeviewfm_seadrive_update_
                                        * file_badge(). */
    gchar          *path_down;       /* Pfad der hydrierten Datei, NULL sonst */
    gchar          *path_dehydrated; /* Pfad der dehydrierten Datei, NULL sonst */
    gchar          *path_up;     /* NULL oder Pfad für pending_up-Änderung */
    gboolean        up_pending;  /* TRUE=NOT_IN_SYNC, FALSE=IN_SYNC */
} WatcherIdleData;

static gboolean watcher_idle_cb(gpointer user_data)
{
    WatcherIdleData *d = user_data;

    sond_treeviewfm_seadrive_update_status(d->stvfm,
            d->path_pending_down, d->delta_down,
            d->path_up, d->up_pending);

    /* Datei-eigenen Badge (Ground-Truth-Map) UND davon abgeleitet die
     * Ordner-Coverage aktualisieren - path_pending_down ist in beiden
     * Aufrufstellen unten immer gesetzt. */
    if (d->path_pending_down)
        sond_treeviewfm_seadrive_update_file_badge(d->stvfm,
                d->path_pending_down, d->coverage_badge, d->delta_total);

    /* Wenn Datei hydrated: Knoten im Baum korrigieren */
    if (d->path_down)
        sond_treeviewfm_seadrive_item_hydrated(d->stvfm, d->path_down);

    /* Wenn Datei dehydrated: Knoten im Baum zurückbauen */
    if (d->path_dehydrated)
        sond_treeviewfm_seadrive_item_dehydrated(d->stvfm, d->path_dehydrated);

    g_free(d->path_pending_down);
    g_free(d->path_down);
    g_free(d->path_dehydrated);
    g_free(d->path_up);
    g_free(d);
    return G_SOURCE_REMOVE;
}

/* ------------------------------------------------------------------ */
/*  Watcher: Initialscan pending_down                                  */
/* ------------------------------------------------------------------ */

/* Trägt die vollen Pfade aller PINNED+offline Dateien unter dir_utf8
 * (rekursiv) in out_paths ein (Set, Keys = g_strdup'te Pfade - für den
 * Projekt-weiten "wird gerade heruntergeladen"-Zähler, seadrive_pending_
 * down), sowie für JEDE Datei mit einem Badge != NONE dessen Wert in
 * out_file_badges (Pfad -> GINT_TO_POINTER(SondSeadriveBadge) - Ground-
 * Truth-Map für den Datei-eigenen Badge UND Grundlage der Ordner-Coverage-
 * Statistik, s. sond_treeviewfm_seadrive_update_file_badge()) UND für JEDES
 * durchlaufene Verzeichnis dessen rekursive {not_hydrated,hydrated_pinned,
 * total}-Statistik in out_dir_counts (Pfad -> SondSeadriveDirCounts*). Wird
 * sowohl für den Initialscan als auch für einen Resync nach Buffer-
 * Overflow verwendet (s. WatcherRescanData) - beide Fälle brauchen die
 * kompletten Pfad-Sets/die komplette Ordner-Statistik, nicht nur eine
 * Anzahl, damit spätere Einzel-Events (Add/Remove/Hydration) korrekt gegen
 * ein Set/eine Statistik abgeglichen statt blind auf einen Zähler
 * angewandt werden können.
 *
 * out_not_hydrated/out_hydrated_pinned/out_total (können NULL sein):
 * liefern die für dir_utf8 selbst ermittelte rekursive Summe an den
 * Aufrufer zurück, damit dieser (bei einem Verzeichnis-Kind) seine eigene
 * Summe hochrechnen kann, ohne aus out_dir_counts nachschlagen zu
 * müssen. */
static void watcher_count_pending_down(const gchar *dir_utf8,
        SondTreeviewFM *stvfm, GHashTable *out_paths,
        GHashTable *out_file_badges,
        GHashTable *out_dir_counts, guint *out_not_hydrated,
        guint *out_hydrated_pinned, guint *out_total)
{
    guint dir_not_hydrated = 0;
    guint dir_hydrated_pinned = 0;
    guint dir_total = 0;

    gchar *pattern = g_strconcat(dir_utf8, "/*", NULL);
    wchar_t *lp = prepare_long_path(pattern, NULL);
    g_free(pattern);
    if (!lp)
        goto done;

    {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(lp, &fd);
    g_free(lp);

    if (h == INVALID_HANDLE_VALUE)
        goto done;

    do {
        if (sond_treeviewfm_seadrive_stop_requested(stvfm))
            break;

        if (wcscmp(fd.cFileName, L".") == 0 ||
                wcscmp(fd.cFileName, L"..") == 0)
            continue;

        /* Reparse-Points (Symlinks/Junctions) überspringen - sonst könnte
         * eine Verzeichnisschleife diese Rekursion endlos laufen lassen. */
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            continue;

        gchar *name = g_utf16_to_utf8(
                (gunichar2*) fd.cFileName, -1, NULL, NULL, NULL);
        if (!name)
            continue;
        gchar *sub = g_strconcat(dir_utf8, "/", name, NULL);
        g_free(name);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            guint sub_not_hydrated = 0, sub_hydrated_pinned = 0, sub_total = 0;
            watcher_count_pending_down(sub, stvfm, out_paths,
                    out_file_badges,
                    out_dir_counts, &sub_not_hydrated, &sub_hydrated_pinned,
                    &sub_total);
            dir_not_hydrated += sub_not_hydrated;
            dir_hydrated_pinned += sub_hydrated_pinned;
            dir_total += sub_total;
            g_free(sub);
        } else {
            gboolean pinned  = (fd.dwFileAttributes & FILE_ATTRIBUTE_PINNED) != 0;
            gboolean offline = (fd.dwFileAttributes & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS) != 0;
            SondSeadriveBadge badge = SOND_SEADRIVE_BADGE_NONE;

            /* Dieselbe Prioritätsreihenfolge wie in
             * sond_treeviewfm_render_file_icon()/sond_icon_util.h. */
            if (offline && pinned)
                badge = SOND_SEADRIVE_BADGE_PENDING;
            else if (offline)
                badge = SOND_SEADRIVE_BADGE_OFFLINE;
            else if (pinned)
                badge = SOND_SEADRIVE_BADGE_PINNED;

            dir_total++;
            if (badge == SOND_SEADRIVE_BADGE_PENDING ||
                    badge == SOND_SEADRIVE_BADGE_OFFLINE)
                dir_not_hydrated++;
            else if (badge == SOND_SEADRIVE_BADGE_PINNED)
                dir_hydrated_pinned++;

            if (badge != SOND_SEADRIVE_BADGE_NONE)
                g_hash_table_insert(out_file_badges, g_strdup(sub),
                        GINT_TO_POINTER(badge));

            /* Projekt-weiter "wird gerade heruntergeladen"-Zähler
             * (seadrive_pending_down) bleibt bei der engeren
             * PINNED+offline-Definition - andere Fragestellung als
             * die Ordner-Coverage-Statistik oben. */
            if (badge == SOND_SEADRIVE_BADGE_PENDING)
                g_hash_table_add(out_paths, g_strdup(sub));

            g_free(sub);
        }
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    }

done:
    if (out_dir_counts) {
        SondSeadriveDirCounts *counts = g_new(SondSeadriveDirCounts, 1);
        counts->not_hydrated = dir_not_hydrated;
        counts->hydrated_pinned = dir_hydrated_pinned;
        counts->total = dir_total;
        g_hash_table_insert(out_dir_counts, g_strdup(dir_utf8), counts);
    }
    if (out_not_hydrated) *out_not_hydrated = dir_not_hydrated;
    if (out_hydrated_pinned) *out_hydrated_pinned = dir_hydrated_pinned;
    if (out_total) *out_total = dir_total;
    return;
}

typedef struct {
    SondTreeviewFM *stvfm;
    GHashTable     *paths;        /* transfer full - s. sond_treeviewfm_seadrive_set_pending_down_paths() */
    GHashTable     *file_badges;  /* transfer full - s. sond_treeviewfm_seadrive_set_file_badges() */
    GHashTable     *dir_counts;   /* transfer full - s. sond_treeviewfm_seadrive_set_dir_counts() */
} WatcherRescanData;

static gboolean watcher_rescan_idle_cb(gpointer user_data)
{
    WatcherRescanData *d = user_data;
    /* Ersetzt die komplette Ground-Truth-Map/die Ordner-Statistik. Beim
     * allerersten Aufruf (Initialscan) sind sie noch leer; bei einem
     * späteren Resync (Buffer-Overflow) wird der alte, ggf. inzwischen
     * falsche Stand komplett verworfen - Watcher-Events, die während des
     * Scans gepuffert wurden, kommen danach und korrigieren ggf. noch
     * einmal nach. */
    sond_treeviewfm_seadrive_set_pending_down_paths(d->stvfm, d->paths);
    sond_treeviewfm_seadrive_set_file_badges(d->stvfm, d->file_badges);
    sond_treeviewfm_seadrive_set_dir_counts(d->stvfm, d->dir_counts);
    g_free(d);
    return G_SOURCE_REMOVE;
}

/* Stößt einen kompletten Rescan von root an und ersetzt anschließend (per
 * g_idle_add, UI-Thread) die komplette Ground-Truth-Map und die Ordner-
 * Statistik. Gemeinsam von Initialscan und Buffer-Overflow-Resync genutzt. */
static void watcher_rescan(SondTreeviewFM *stvfm, const gchar *root)
{
    GHashTable *paths = g_hash_table_new_full(
            g_str_hash, g_str_equal, g_free, NULL);
    GHashTable *file_badges = g_hash_table_new_full(
            g_str_hash, g_str_equal, g_free, NULL);
    GHashTable *dir_counts = g_hash_table_new_full(
            g_str_hash, g_str_equal, g_free, g_free);
    watcher_count_pending_down(root, stvfm, paths, file_badges,
            dir_counts, NULL, NULL, NULL);

    WatcherRescanData *d = g_new0(WatcherRescanData, 1);
    d->dir_counts = dir_counts;
    d->stvfm = stvfm;
    d->paths = paths;
    d->file_badges = file_badges;
    g_idle_add(watcher_rescan_idle_cb, d);
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

    /* Eigene Kopie statt geliehenem Zeiger auf stvfm_priv->root: der
     * Aufrufer (sond_treeviewfm_set_root()) joint diesen Thread zwar immer
     * synchron VOR einer Root-Änderung, aber der Thread soll nicht von
     * dieser Aufrufreihenfolge abhängen, falls die mal umgebaut wird. */
    gchar *root = g_strdup(sond_treeviewfm_get_root(stvfm));
    if (!root)
        return NULL;

    /* Verzeichnis-Handle für ReadDirectoryChangesW */
    wchar_t *root_w = prepare_long_path(root, NULL);
    if (!root_w) {
        g_free(root);
        return NULL;
    }

    HANDLE hDir = CreateFileW(root_w,
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            NULL);
    g_free(root_w);

    if (hDir == INVALID_HANDLE_VALUE) {
        g_free(root);
        return NULL;
    }

    /* OVERLAPPED + Event für asynchrones ReadDirectoryChangesW */
    OVERLAPPED ov = { 0 };
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent) {
        CloseHandle(hDir);
        g_free(root);
        return NULL;
    }

    BYTE buf[32768];
    DWORD bytes_returned = 0;

    /* Ersten ReadDirectoryChangesW-Aufruf starten - VOR dem Scan,
     * damit während des Scans entstehende Events nicht verloren gehen.
     * FILE_NOTIFY_CHANGE_FILE_NAME zusätzlich zu ATTRIBUTES/LAST_WRITE:
     * ohne diesen Filter werden neu angelegte oder gelöschte Dateien vom
     * Watcher gar nicht bemerkt - der pending_down/pending_up-Zähler lief
     * dadurch mit der Zeit auseinander (Untersuchung SeaDrive-Coverage,
     * 09/2026). */
    if (!ReadDirectoryChangesW(hDir, buf, sizeof(buf), TRUE,
            FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_LAST_WRITE |
                    FILE_NOTIFY_CHANGE_FILE_NAME,
            NULL, &ov, NULL)) {
        LOG_WARN("SeaDrive-Watcher: ReadDirectoryChangesW('%s') fehlgeschlagen "
                "(Fehler %lu) - Watcher wird nicht gestartet",
                root, (unsigned long) GetLastError());
        CloseHandle(ov.hEvent);
        CloseHandle(hDir);
        g_free(root);
        return NULL;
    }

    /* Initialscan: ermittelt bereits vorhandene PINNED+offline Dateien.
     * ReadDirectoryChangesW läuft bereits - Events während des Scans
     * werden gepuffert und danach verarbeitet (selbstkorrigierend). */
    if (!sond_treeviewfm_seadrive_stop_requested(stvfm))
        watcher_rescan(stvfm, root);

    while (!sond_treeviewfm_seadrive_stop_requested(stvfm)) {

        /* 500ms-Timeout für reaktionsschnelles Stoppen */
        DWORD wait = WaitForSingleObject(ov.hEvent, 500);

        if (wait == WAIT_OBJECT_0) {
            /* Ereignisse verarbeiten */
            if (GetOverlappedResult(hDir, &ov, &bytes_returned, FALSE)) {
              if (bytes_returned == 0) {
                /* Buffer-Overflow: ReadDirectoryChangesW meldet Erfolg, aber
                 * 0 Bytes - der interne Puffer ist übergelaufen, ALLE
                 * Ereignisse seit dem letzten erfolgreichen Read sind
                 * verloren (nicht nur die im Puffer nicht mehr unter-
                 * gebrachten). Einzige sichere Reaktion: kompletter Resync
                 * (jetzt unproblematisch, s. Untersuchung SeaDrive-Coverage
                 * 09/2026 - ~1,5s auch bei ~70.000 Dateien). */
                LOG_WARN("SeaDrive-Watcher('%s'): ReadDirectoryChangesW-Puffer "
                        "übergelaufen - Events verloren, erzwinge Resync",
                        root);
                watcher_rescan(stvfm, root);
              } else {

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

                        if (fni->Action == FILE_ACTION_REMOVED ||
                                fni->Action == FILE_ACTION_RENAMED_OLD_NAME) {
                            /* Datei/Verzeichnis ist weg - Attribute nicht
                             * mehr abfragbar. Pfad bedingungslos aus beiden
                             * Tracking-Sets entfernen (No-Op, falls nicht
                             * enthalten - z.B. weil es ein Verzeichnis war
                             * oder die Datei nie PINNED+offline bzw. NOT_IN_
                             * SYNC war). Ohne das würden gelöschte Dateien,
                             * die gerade noch heruntergeladen wurden, ewig
                             * mitgezählt (Untersuchung SeaDrive-Coverage,
                             * 09/2026). delta_total=-1 zieht die Datei aus
                             * der Ordner-Coverage-Gesamtzahl ab (auch ein
                             * No-Op über die 0-Kappung, falls es ein
                             * Verzeichnis war - dessen eigener Eintrag in
                             * seadrive_dir_counts bleibt dann zwar stehen,
                             * wird aber beim nächsten Rescan bereinigt). */
                            WatcherIdleData *d = g_new0(WatcherIdleData, 1);
                            d->stvfm = stvfm;
                            d->path_pending_down = g_strdup(full);
                            d->delta_down = -1;
                            d->delta_total = -1;
                            d->path_up = g_strdup(full);
                            d->up_pending = FALSE;

                            g_idle_add(watcher_idle_cb, d);
                        } else {
                            /* ADDED, RENAMED_NEW_NAME, MODIFIED (Attribute/
                             * Last-Write) - Datei existiert (noch), aktuelle
                             * Attribute abfragen. */
                            wchar_t *lp = prepare_long_path(full, NULL);
                            if (lp) {
                                DWORD attrs = GetFileAttributesW(lp);
                                g_free(lp);

                                if (attrs != INVALID_FILE_ATTRIBUTES &&
                                        !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {

                                    WatcherIdleData *d = g_new0(WatcherIdleData, 1);
                                    d->stvfm = stvfm;
                                    d->path_pending_down = g_strdup(full);
                                    /* Nur bei echter Neuanlage zählt die Datei
                                     * zusätzlich zur Ordner-Gesamtzahl - bei
                                     * MODIFIED existierte sie schon. */
                                    if (fni->Action == FILE_ACTION_ADDED ||
                                            fni->Action == FILE_ACTION_RENAMED_NEW_NAME)
                                        d->delta_total = +1;

                                    /* pending_down aktualisieren */
                                    {
                                        gboolean pinned   = (attrs & FILE_ATTRIBUTE_PINNED) != 0;
                                        gboolean offline  = (attrs & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS) != 0;
                                        gboolean unpinned = (attrs & FILE_ATTRIBUTE_UNPINNED) != 0;

                                        /* Datei-Badge: dieselbe Prioritäts-
                                         * reihenfolge wie im Scan
                                         * (watcher_count_pending_down()) /
                                         * render_file_icon(). */
                                        if (offline && pinned)
                                            d->coverage_badge = SOND_SEADRIVE_BADGE_PENDING;
                                        else if (offline)
                                            d->coverage_badge = SOND_SEADRIVE_BADGE_OFFLINE;
                                        else if (pinned)
                                            d->coverage_badge = SOND_SEADRIVE_BADGE_PINNED;
                                        else
                                            d->coverage_badge = SOND_SEADRIVE_BADGE_NONE;

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
                        }
                        g_free(full);
                    }

                    if (fni->NextEntryOffset == 0)
                        break;
                    fni = (FILE_NOTIFY_INFORMATION*)
                            ((BYTE*) fni + fni->NextEntryOffset);
                } while (1);
              }
            }

            /* Nächsten ReadDirectoryChangesW-Aufruf starten */
            ResetEvent(ov.hEvent);
            if (!ReadDirectoryChangesW(hDir, buf, sizeof(buf), TRUE,
                    FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_LAST_WRITE |
                            FILE_NOTIFY_CHANGE_FILE_NAME,
                    NULL, &ov, NULL)) {
                LOG_WARN("SeaDrive-Watcher: ReadDirectoryChangesW('%s') beim "
                        "Neu-Anstoßen fehlgeschlagen (Fehler %lu) - Watcher "
                        "wird beendet", root, (unsigned long) GetLastError());
                break;
            }
        }
    }

    /* Aufräumen */
    CancelIo(hDir);
    CloseHandle(ov.hEvent);
    CloseHandle(hDir);
    g_free(root);

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
/*  GSimpleAction-Callback (ersetzt seadrive_menu_activate)           */
/* ------------------------------------------------------------------ */

static void seadrive_action_activate(GSimpleAction *action, GVariant *parameter,
        gpointer data)
{
    SondTreeviewFM *stvfm = SOND_TREEVIEWFM(data);
    guint    pin_state = (guint) GPOINTER_TO_INT(
            g_object_get_data(G_OBJECT(action), "pin_state"));
    gboolean sel_only  = (gboolean) GPOINTER_TO_INT(
            g_object_get_data(G_OBJECT(action), "sel"));
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

    /* Bei UNSPECIFIED/UNPINNED ändert sich das PINNED-Attribut sofort
     * (synchron), das Overlay-Icon muss also sofort neu gezeichnet werden.
     * Bei PINNED dagegen bleibt die Datei i.d.R. zunächst weiter "offline"
     * (RECALL_ON_DATA_ACCESS) - SeaDrive lädt sie erst im Hintergrund
     * herunter. Das Icon würde also ohnehin noch "nicht lokal" zeigen; der
     * spätere Wechsel auf "gepinnt" kommt automatisch über den Watcher-
     * Thread (FILE_NOTIFY_CHANGE_ATTRIBUTES -> sond_treeviewfm_seadrive_
     * item_hydrated() -> queue_draw), ein sofortiges Neuzeichnen hier wäre
     * also nur ein unnötiger Zwischenschritt. */
    if (pin_state != STVFM_PIN_STATE_PINNED)
        gtk_widget_queue_draw(GTK_WIDGET(stvfm));
}

/* ------------------------------------------------------------------ */
/*  Public: attach SeaDrive submenu to context menu                   */
/* ------------------------------------------------------------------ */

void sond_treeviewfm_seadrive_init_contextmenu(SondTreeviewFM *stvfm)
{
    /* Nur noch Aktionen registrieren - GMenu-Sections wurden
     * bereits in sond_treeviewfm_class_init aufgebaut. */
    GSimpleActionGroup *ag = sond_treeview_get_action_group(SOND_TREEVIEW(stvfm));

    struct { const gchar *name; guint pin_state; gboolean sel; } actions[] = {
        { "sd-pin-all",     STVFM_PIN_STATE_PINNED,      FALSE },
        { "sd-pin-sel",     STVFM_PIN_STATE_PINNED,      TRUE  },
        { "sd-unspec-all",  STVFM_PIN_STATE_UNSPECIFIED, FALSE },
        { "sd-unspec-sel",  STVFM_PIN_STATE_UNSPECIFIED, TRUE  },
        { "sd-unpin-all",   STVFM_PIN_STATE_UNPINNED,    FALSE },
        { "sd-unpin-sel",   STVFM_PIN_STATE_UNPINNED,    TRUE  },
    };

    for (guint i = 0; i < G_N_ELEMENTS(actions); i++) {
        GSimpleAction *act = g_simple_action_new(actions[i].name, NULL);
        g_object_set_data(G_OBJECT(act), "pin_state",
                GINT_TO_POINTER((gint) actions[i].pin_state));
        g_object_set_data(G_OBJECT(act), "sel",
                GINT_TO_POINTER((gint) actions[i].sel));
        g_signal_connect(act, "activate",
                G_CALLBACK(seadrive_action_activate), stvfm);
        g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(act));
        g_object_unref(act);
    }
}

#endif /* _WIN32 */
