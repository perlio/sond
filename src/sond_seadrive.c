/*
 * sond_seadrive.c (bis 18.09.2026: sond_treeviewfm_seadrive.c)
 *
 * SeaDrive integration for SondTreeviewFM.
 * Windows-only - on Linux this compiles to an empty translation unit.
 *
 * Uses prepare_long_path() from sond_file_helper for consistent UTF-8
 * handling and long path support (>260 chars) throughout.
 *
 * Refactoring (18.09.2026, Nutzer-Fund "sond_treeviewfm.c und
 * sond_treeviewfm_seadrive.c sind riesen Trümmer! ... in _treeviewfm.c
 * sind auch Funktionen, die in sond_treeviewfm_seadrive gehören"): Modul
 * umbenannt (treeviewfm_seadrive -> seadrive) UND die ~800 Zeilen
 * SeaDrive-Backend-Logik (Ground-Truth-Hashtables für Badges/Coverage,
 * Watcher-Start/Stop), die vorher zwangsläufig in sond_treeviewfm.c
 * stehen mußte (G_DEFINE_TYPE_WITH_PRIVATE() erzeugt nur einen in dieser
 * Übersetzungseinheit sichtbaren statischen Accessor), hierher verschoben.
 * Zugriff auf SondTreeviewFMPrivate/SondTVFMItemPrivate jetzt über die
 * "Freund"-Accessoren sond_treeviewfm_get_priv()/sond_tvfm_item_get_priv()
 * aus sond_treeviewfm_private.h (dort ausführl. Kommentar zur Begründung).
 */

#include "sond_seadrive.h"

#ifdef _WIN32

#include <windows.h>
#include <glib.h>
#include <gtk/gtk.h>

#include "sond_treeview.h"
#include "sond_treeviewfm_private.h"
#include "sond_fileparts.h"
#include "sond_file_helper.h"
#include "sond_log_and_error.h"
#include "sond_mime.h"
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

#define CF_HYDRATE_FLAG_NONE         0x00000000

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

/* S. Doc-Kommentar an sond_seadrive_hydrate() (18.09.2026) - offizieller
 * Ersatz für den früheren, rohen CreateFileW+ReadFile-"Trick" in
 * sond_treeviewfm_open(). */
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

/* Nutzer-Fund 16.09.2026: Nach dem Kopieren mehrerer Dateien aus einem
 * ZIP-Archiv ins Dateisystem (auch schon bei nur ~20 Dateien - ein
 * Puffer-Overflow von ReadDirectoryChangesW als Ursache damit
 * ausgeschlossen) blieb die SeaDrive-Statusanzeige dauerhaft auf "✓"
 * (alles synchron) stehen, obwohl die frisch kopierten Dateien noch
 * hochgeladen werden mussten. Ursache: diese Funktion wertete jede Art
 * von Unsicherheit/Fehlschlag (CfGetPlaceholderInfo fehlt, Datei nicht
 * öffenbar, CfGetPlaceholderInfo() schlägt fehl) als "im Zweifel: in
 * sync" - TRUE. Eine gerade erst per normalem CreateFile()/fwrite() (statt
 * über die Cloud-Files-Platzhalter-APIs) neu angelegte Datei wird von
 * CfGetPlaceholderInfo() vermutlich (noch) nicht als Cloud-Datei erkannt,
 * der Aufruf schlägt fehl - und der optimistische Fallback verschleiert
 * dann dauerhaft (nicht nur kurz nach dem Anlegen), dass die Datei noch
 * hochgeladen werden muss. Für einen Indikator, der vor "Daten sind noch
 * nicht gesichert" warnen soll, ist das die falsche Default-Richtung: ein
 * fälschliches "noch nicht synchron" ist höchstens ein optisches
 * Ärgernis, ein fälschliches "alles synchron" verschleiert echten
 * Datenverlust-Risiko-Zustand. Fallback deshalb auf FALSE (not in sync,
 * Upload ausstehend) gedreht - eine Datei gilt jetzt nur noch dann als
 * synchron, wenn die Prüfung das auch tatsächlich bestätigen konnte. */
static gboolean watcher_check_in_sync(const gchar *utf8_path)
{
    SeaDrivePlaceholderBasicInfo basic = { 0 };
    DWORD returned = 0;
    HRESULT hr;
    HANDLE h;
    wchar_t *lp;

    if (!g_CfGetPlaceholderInfo)
        return FALSE; /* im Zweifel: not in sync */

    lp = prepare_long_path(utf8_path, NULL);
    if (!lp)
        return FALSE;

    h = CreateFileW(lp, FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    g_free(lp);

    if (h == INVALID_HANDLE_VALUE)
        return FALSE;

    hr = g_CfGetPlaceholderInfo(h, CF_PLACEHOLDER_INFO_BASIC,
            &basic, sizeof(basic), &returned);
    CloseHandle(h);

    if (FAILED(hr) && hr != HRESULT_MORE_DATA)
        return FALSE;

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
/*  Public API: sond_seadrive_hydrate                                 */
/* ------------------------------------------------------------------ */

/*
 * sond_seadrive_hydrate:
 *
 * Stößt die Hydrierung (den Download) einer noch nicht lokal vorhandenen
 * Cloud-Datei an - Ersatz für den früheren Mechanismus in
 * sond_treeviewfm_open() (Doppelklick auf einen SeaDrive-Platzhalter), der
 * dafür einfach mit CreateFileW(GENERIC_READ)+ReadFile() ein Byte gelesen
 * hat, um SeaDrive/Windows zum "Recall" zu bewegen.
 *
 * Regressions-Fund 18.09.2026 (s. ToDo.c): nach einem Windows-Update
 * (KB5124008, 08.09.2026 - laut Presseberichten ungewöhnlich umfangreich
 * und mit zahlreichen Kollateralschäden an unzusammenhängenden
 * Systemkomponenten) schlägt dieser rohe CreateFileW(GENERIC_READ)-Aufruf
 * bei SeaDrive-Platzhaltern zuverlässig mit GetLastError()=395
 * (ERROR_CLOUD_FILE_ACCESS_DENIED) fehl - auch nach Installation des
 * Notfall-Nachfolge-Updates (KB5129195) und nach vollständigem Neustart/
 * Neu-Build von zond selbst (also kein zond-Bug, s. Diagnose-Logging-
 * Auswertung). Die Windows-Dokumentation zu ERROR_CLOUD_FILE_ACCESS_DENIED
 * deckt sich damit: der Fehler tritt typischerweise auf, wenn eine
 * Anwendung eine noch nicht hydrierte Cloud-Datei mit einem gewöhnlichen
 * Lesezugriff öffnet, STATT die Hydrierung über die dafür vorgesehene
 * Cloud-Filter-API anzustoßen - genau das tat der alte Code.
 *
 * Diese Funktion verwendet stattdessen die offizielle, für genau diesen
 * Zweck vorgesehene CfHydratePlaceholder()-API (cfapi.h/cldapi.dll,
 * dynamisch geladen wie der Rest dieser Datei - s. Kommentar bei
 * cfapi_init_once(), derselbe Grund: kein cfapi.h im hier verwendeten
 * MinGW-Toolchain). Laut Microsoft-Dokumentation genügt dafür ein Handle
 * mit reinem Attribut-Zugriff (FILE_READ_ATTRIBUTES statt GENERIC_READ) -
 * das dürfte der eigentliche Unterschied sein, den das Windows-Update
 * jetzt strenger prüft. Länge bewusst auf 1 Byte begrenzt (wie beim alten
 * ReadFile(1 Byte)-Trick): reicht, um den Provider (SeaDrive) zum
 * Download der Datei zu bewegen, ohne dass dieser Aufruf selbst
 * (synchron, blockierend) auf die komplette Downloaddauer einer großen
 * Datei warten muss.
 *
 * Bereits lokal vorhandene Dateien (kein FILE_ATTRIBUTE_RECALL_ON_DATA_
 * ACCESS) sind ein No-Op (TRUE, kein Fehler). Gibt FALSE mit gesetztem
 * error zurück, wenn cldapi.dll/CfHydratePlaceholder nicht verfügbar ist
 * oder der Aufruf selbst fehlschlägt - der Aufrufer fällt in diesem Fall
 * auf den normalen Öffnen-Weg zurück (s. sond_treeviewfm_open()).
 *
 * Korrektur 18.09.2026: Die obige Annahme, die 1-Byte-Begrenzung von
 * Length spare das Warten auf die komplette Downloaddauer, hat sich in
 * der Praxis als falsch erwiesen - Nutzer-Fund: bei einer 51-GB-Datei
 * blockierte dieser Aufruf trotzdem minutenlang (vermutlich lädt der
 * SeaDrive-Provider unabhängig von der angeforderten Länge grundsätzlich
 * die ganze Datei, bevor CfHydratePlaceholder zurückkehrt). Diese Funktion
 * bleibt deshalb synchron/blockierend - s. sond_seadrive_hydrate_async()
 * weiter unten, die sie in einem Hintergrund-Thread aufruft, und
 * sond_seadrive_needs_hydration() für einen schnellen, nicht-blockierenden
 * Vorab-Check (nur GetFileAttributesW), mit dem der Aufrufer entscheiden
 * kann, ob eine Hydrierung überhaupt nötig ist, ohne dafür einen Thread
 * zu starten.
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
/*  Public API: sond_seadrive_hydrate_async                            */
/* ------------------------------------------------------------------ */

/* Menge der Pfade, für die aktuell ein Hydrier-Hintergrund-Thread läuft -
 * verhindert, dass ein erneuter Doppelklick auf dieselbe, noch
 * herunterladende Datei einen zweiten, redundanten Thread/
 * CfHydratePlaceholder()-Aufruf auslöst. Nutzer-Fund 18.09.2026: bei
 * sehr großen Dateien (gemeldeter Fall: 51 GB) blockierte der bis dahin
 * SYNCHRONE Aufruf von sond_seadrive_hydrate() im GTK-Hauptthread das
 * gesamte Programm minutenlang ohne jede Rückmeldung oder
 * Abbrechen-Möglichkeit (s. ToDo.c). Nutzer-Entscheidung: kein
 * Info-Fenster beim ERSTEN Doppelklick (der Download läuft ohnehin im
 * Hintergrund weiter, unabhängig davon, ob die UI darauf wartet) -
 * stattdessen sofort in die UI zurückkehren (Fire-and-forget).
 *
 * Ergänzung, ebenfalls 18.09.2026: bei einem erneuten Doppelklick auf
 * dieselbe, noch laufende Datei jetzt statt eines stillen No-Ops ein
 * Fortschritts-/Abbrechen-Dialog (sond_seadrive_show_hydrate_progress_
 * dialog(), weiter unten) - Nutzerwunsch, um bei versehentlichen
 * Großdateien den Download tatsächlich stoppen zu können, statt den
 * SeaDrive-Server unnötig weiter zu belasten.
 *
 * Wert je Pfad: HydratingEntry (unten), enthält u.a. ein
 * THREAD_TERMINATE-Handle auf den Hydrier-Thread für CancelSynchronousIo()
 * (echter Abbruch-Versuch, s. sond_seadrive_hydrate_cancel()). Schlüssel:
 * full_path (g_strdup'd). Von Haupt- UND Hintergrund-Threads genutzt,
 * deshalb per Mutex geschützt (statisch/all-zero-initialisiert - laut
 * GLib-Doku für GMutex zulässig, kein g_mutex_init() nötig). */
static GMutex      g_hydrating_mutex;
static GHashTable *g_hydrating_paths = NULL;

typedef struct {
    HANDLE   thread_handle;    /* NULL, bis der Hydrier-Thread wirklich
                                 * läuft (kurzes Zeitfenster direkt nach
                                 * dem Einfügen in g_hydrating_paths, s.
                                 * sond_seadrive_hydrate_async()) */
    gboolean cancel_requested; /* per sond_seadrive_hydrate_cancel()
                                 * gesetzt - falls thread_handle zu diesem
                                 * Zeitpunkt noch NULL ist (s.o.), wertet
                                 * hydrate_thread_func() dies selbst aus
                                 * und startet den Download erst gar
                                 * nicht. */
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
} HydrateThreadData;

static gpointer hydrate_thread_func(gpointer data)
{
    HydrateThreadData *td = (HydrateThreadData *) data;
    GError *error = NULL;
    HydratingEntry *entry;
    HANDLE thread_handle_dup = NULL;
    gboolean pre_cancelled = FALSE;

    /* Eigenes Thread-Handle (mit THREAD_TERMINATE-Recht, s.
     * CancelSynchronousIo()-Doku) für einen möglichen späteren
     * Abbrechen-Versuch von außen bereitstellen. */
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
        /* Abbrechen wurde schon angefordert, bevor dieser Thread überhaupt
         * so weit kam, sein Handle einzutragen - Download erst gar nicht
         * starten. */
        if (thread_handle_dup)
            CloseHandle(thread_handle_dup);
    } else if (!sond_seadrive_hydrate(td->full_path, &error)) {
        /* Bei per CancelSynchronousIo() abgebrochenen Aufrufen liefert
         * GetLastError() innerhalb von sond_seadrive_hydrate() i.d.R.
         * ERROR_OPERATION_ABORTED - wird hier wie jeder andere Fehler
         * einfach mitgeloggt. */
        LOG_WARN("%s: sond_seadrive_hydrate('%s'): %s", __func__,
                td->full_path, error ? error->message : "?");
        g_clear_error(&error);
    }

    g_mutex_lock(&g_hydrating_mutex);
    g_hash_table_remove(g_hydrating_paths, td->full_path); /* schließt
            thread_handle_dup via hydrating_entry_free(), falls gesetzt */
    g_mutex_unlock(&g_hydrating_mutex);

    g_free(td->full_path);
    g_free(td);
    return NULL;
}

/*
 * sond_seadrive_hydrate_async:
 *
 * Wie sond_seadrive_hydrate(), aber nicht-blockierend: startet die
 * eigentliche Hydrierung in einem eigenen Hintergrund-Thread und kehrt
 * sofort zurück (Fire-and-forget, s. Doc-Kommentar oben, 18.09.2026). Ein
 * erneuter Aufruf für denselben full_path, während bereits ein Thread
 * dafür läuft, ist ein No-Op (Aufrufer sollte in diesem Fall stattdessen
 * sond_seadrive_show_hydrate_progress_dialog() zeigen, s.
 * sond_treeviewfm_open()). Fehler landen nur im Log (LOG_WARN in
 * hydrate_thread_func()) - ein synchroner Rückgabewert wäre ohnehin
 * nicht sinnvoll nutzbar, da der eigentliche Download beim Rücksprung
 * i.d.R. noch läuft.
 */
void sond_seadrive_hydrate_async(const gchar *full_path)
{
    HydrateThreadData *td = NULL;
    GThread *thread = NULL;

    g_mutex_lock(&g_hydrating_mutex);
    if (!g_hydrating_paths)
        g_hydrating_paths = g_hash_table_new_full(g_str_hash, g_str_equal,
                g_free, hydrating_entry_free);

    if (g_hash_table_contains(g_hydrating_paths, full_path)) {
        /* schon ein Hydrier-Thread für diese Datei unterwegs - No-Op */
        g_mutex_unlock(&g_hydrating_mutex);
        return;
    }
    g_hash_table_insert(g_hydrating_paths, g_strdup(full_path),
            g_new0(HydratingEntry, 1));
    g_mutex_unlock(&g_hydrating_mutex);

    td = g_new0(HydrateThreadData, 1);
    td->full_path = g_strdup(full_path);

    thread = g_thread_new("seadrive-hydrate", hydrate_thread_func, td);
    if (!thread) {
        /* Thread-Erzeugung fehlgeschlagen - Eintrag wieder entfernen,
         * sonst bliebe die Datei für immer fälschlich als "läuft schon"
         * markiert. */
        g_mutex_lock(&g_hydrating_mutex);
        g_hash_table_remove(g_hydrating_paths, td->full_path);
        g_mutex_unlock(&g_hydrating_mutex);
        g_free(td->full_path);
        g_free(td);
        return;
    }
    g_thread_unref(thread); /* fire-and-forget, kein g_thread_join() */
}

/*
 * sond_seadrive_is_hydrating:
 *
 * TRUE, wenn für full_path aktuell ein Hydrier-Hintergrund-Thread läuft
 * (s.o.). Vom Aufrufer genutzt, um bei einem erneuten Doppelklick
 * zwischen "neue Hydrierung anstoßen" (sond_seadrive_hydrate_async()) und
 * "Fortschritt/Abbrechen-Dialog zeigen" (sond_seadrive_show_hydrate_
 * progress_dialog()) zu unterscheiden.
 */
gboolean sond_seadrive_is_hydrating(const gchar *full_path)
{
    gboolean result;

    g_mutex_lock(&g_hydrating_mutex);
    result = g_hydrating_paths &&
            g_hash_table_contains(g_hydrating_paths, full_path);
    g_mutex_unlock(&g_hydrating_mutex);

    return result;
}

/*
 * sond_seadrive_hydrate_cancel:
 *
 * Versucht, eine laufende Hydrierung von full_path abzubrechen - über
 * CancelSynchronousIo() auf das Thread-Handle des Hydrier-Threads (dieser
 * steckt synchron/blockierend in CfHydratePlaceholder(), s.
 * sond_seadrive_hydrate()). Ist der Thread noch nicht so weit, sein
 * Handle einzutragen (s. HydratingEntry oben), wird nur cancel_requested
 * gesetzt - der Thread bricht dann selbst vorzeitig ab, bevor er den
 * Download überhaupt beginnt.
 *
 * WICHTIG: CancelSynchronousIo() ist für CfHydratePlaceholder()
 * offiziell nicht dokumentiert (Microsoft dokumentiert es allgemein für
 * synchrone Dateizugriffe, s. cancelsynchronousio-func). Ob der
 * SeaDrive-Minifilter/-Dienst einen so markierten Abbruch tatsächlich
 * zeitnah beachtet und den Download serverseitig stoppt, ist nicht
 * garantiert - es ist aber der einzige als Konsument (nicht Sync-
 * Provider) verfügbare Mechanismus, ohne den eigentlichen CF-API-Aufruf
 * auf OVERLAPPED umzustellen. Kein Rückgabewert: Erfolg zeigt sich
 * indirekt darüber, dass der Eintrag aus g_hydrating_paths verschwindet
 * (von sond_seadrive_show_hydrate_progress_dialog() gepollt).
 */
void sond_seadrive_hydrate_cancel(const gchar *full_path)
{
    HydratingEntry *entry;

    g_mutex_lock(&g_hydrating_mutex);
    entry = g_hydrating_paths ?
            g_hash_table_lookup(g_hydrating_paths, full_path) : NULL;
    if (entry) {
        if (entry->thread_handle)
            CancelSynchronousIo(entry->thread_handle);
        else
            entry->cancel_requested = TRUE;
    }
    g_mutex_unlock(&g_hydrating_mutex);
}

/* ------------------------------------------------------------------ */
/*  Public API: sond_seadrive_show_hydrate_progress_dialog              */
/* ------------------------------------------------------------------ */

/* CF_PLACEHOLDER_INFO_STANDARD liefert u.a. OnDiskDataSize (bereits lokal
 * vorhandene Bytes) - Struktur-Layout 1:1 aus der offiziellen cfapi.h
 * übernommen (Recherche 18.09.2026), hier wie beim Rest der Datei manuell
 * dupliziert (kein cfapi.h im verwendeten MinGW-Toolchain, s. Kommentar
 * bei cfapi_init_once()). PinState/InSyncState hier bewusst als DWORD
 * (nicht als CF_PIN_STATE/CF_IN_SYNC_STATE-Enum) deklariert, analog
 * SeaDrivePlaceholderBasicInfo oben - nur OnDiskDataSize wird tatsächlich
 * ausgewertet.
 *
 * Regressions-Fund 18.09.2026 (Nutzer: Fortschrittsanzeige bleibt
 * durchgehend bei 0%, obwohl laut Windows-Explorer kräftig heruntergeladen
 * wird): das offizielle cfapi.h deklariert FileIdentity[] als BYTE[1] -
 * ein reiner Platzhalter für einen tatsächlich variabel langen Puffer, der
 * vom Aufrufer selbst groß genug angelegt werden muss (s. auch
 * SeaDrivePlaceholderBasicInfo oben, dort schon immer mit 256 Byte statt
 * [1] deklariert). Mit nur 1 Byte Puffer für FileIdentity liefert
 * CfGetPlaceholderInfo() bei einer nicht winzigen Identity (bei SeaDrive
 * offenbar der Normalfall) HRESULT_MORE_DATA zurück - technisch ein
 * FAILURE-HRESULT (Severity-Bit gesetzt trotz des Namens), SUCCEEDED()
 * schlägt also fehl und hydrate_progress_update() brach VOR dem
 * Auswerten von OnDiskDataSize ab, ohne die Progress-Bar je zu
 * aktualisieren - exakt das gemeldete Symptom. Fix: FileIdentity analog
 * SeaDrivePlaceholderBasicInfo auf 256 Byte vergrößert. */
#define CF_PLACEHOLDER_INFO_STANDARD  1

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

/* Nutzer-Fund 19.09.2026 ("sond_seadrive_ensure_hydrated und _multi
 * enthalten viel doppelten Code - kann man _ensure_hydrated nicht als
 * _multi mit arr->len==1 verstehen?"): der komplette Einzeldatei-
 * Fortschrittsdialog, der hier vorher stand (HydrateProgressUi,
 * hydrate_progress_update(), hydrate_progress_tick(),
 * cb_hydrate_progress_dialog_destroy(),
 * cb_hydrate_progress_abbrechen_clicked(),
 * sond_seadrive_show_hydrate_progress_dialog()), war strukturell eine
 * 1:1-Dopplung der weiter unten stehenden Multi-Variante
 * (HydrateProgressEntryMulti/HydrateProgressUiMulti und Umfeld) - nur
 * für genau einen statt beliebig viele Pfade. Ersatzlos entfernt:
 * sond_seadrive_ensure_hydrated() (s.u.) delegiert jetzt an
 * sond_seadrive_ensure_hydrated_multi() mit einem einelementigen
 * GPtrArray, und sond_seadrive_show_hydrate_progress_dialog() (nirgends
 * sonst im Projekt direkt aufgerufen, s. grep) entfällt zugunsten von
 * sond_seadrive_show_hydrate_progress_dialog_multi(). Einzige sichtbare
 * Änderung: der Dialog bei einem erneuten Doppelklick auf eine einzelne,
 * noch hydrierende Datei zeigt jetzt denselben Dialograhmen wie der
 * Auszug-Fall (Titel "Download läuft", darunter EINE Zeile mit
 * Dateiname + Fortschrittsbalken statt des Satzes "Download läuft
 * bereits: <Name>") - inhaltlich identisch, nur ohne den einleitenden
 * Satz. SeaDrivePlaceholderStandardInfo/CF_PLACEHOLDER_INFO_STANDARD
 * oben bleiben unverändert bestehen, da hydrate_progress_entry_update()
 * (Multi-Variante) sie weiterhin braucht. */

/* Nutzer-Hinweis 18.09.2026: "Identischer Code in sond_treeviewfm.c und
 * zond_treeview.c - das ist ungünstig." - beide Stellen (BAUM_FS-
 * Doppelklick in sond_treeviewfm_open() bzw. BAUM_INHALT/AUSWERTUNG-
 * Doppelklick in zond_treeview_open_node()) prüften vor dem eigentlichen
 * Öffnen wortgleich needs_hydration()/is_hydrating()/hydrate_async()/
 * show_hydrate_progress_dialog() und kehrten dann sofort zurück. Diese
 * gemeinsame Sequenz hierher gezogen.
 *
 * Bewusst NICHT als gemeinsame Stelle gewählt: sond_file_part_open()
 * (Öffnen mit externem Programm/ShellExecute) - erreichte PDFs mit
 * internem Viewer ohnehin nicht (die laufen über
 * zond_treeview_open_single_view()/_open_auszug(), nie über
 * sond_file_part_open()). Nutzer-Test 18.09.2026 hat dabei eine
 * ursprüngliche Vermutung widerlegt: Hydrierung bei "Öffnen mit" wird
 * NICHT etwa vom gestarteten externen Programm bzw. Windows-Explorer
 * selbst übernommen, sondern ganz normal von zond ausgelöst - weil
 * beide Aufrufer (s.u.) diesen Check schon VOR der Verzweigung zu
 * open_with/sond_file_part_open() durchlaufen. Funktioniert nachweislich
 * korrekt (inkl. Dialog beim zweiten Doppelklick) - der eigentliche,
 * weiterhin gültige Grund gegen sond_file_part_open() als gemeinsame
 * Stelle ist allein die fehlende Abdeckung des internen-Viewer-Pfads.
 * Die Ermittlung des vollen Pfads der realen Datei (Container-Vorfahre
 * mit parent==NULL) bleibt bewusst beim jeweiligen Aufrufer, da sie je
 * nach Baum ein anderes Datenmodell abläuft (SondTVFMItem bzw.
 * SondFilePart) und sich dafür keine gemeinsame Stelle anbietet. */
/* Nutzer-Fund 19.09.2026: nur noch ein dünner Wrapper um
 * sond_seadrive_ensure_hydrated_multi() mit einem einelementigen
 * GPtrArray - s. ausführl. Kommentar dort sowie den entfallenen
 * Einzeldatei-Dialog weiter oben. full_path wird nur gelesen (die
 * Multi-Variante kopiert bei Bedarf selbst), der einelementige Array
 * trägt also nur full_path als rohen Zeiger und braucht keine eigene
 * free_func. */
gboolean sond_seadrive_ensure_hydrated(GtkWindow *parent,
        const gchar *full_path)
{
    GPtrArray *full_paths;
    gboolean result;

    if (!full_path)
        return TRUE;

    full_paths = g_ptr_array_new();
    g_ptr_array_add(full_paths, (gpointer) full_path);

    result = sond_seadrive_ensure_hydrated_multi(parent, full_paths);

    g_ptr_array_free(full_paths, TRUE);

    return result;
}

/* ------------------------------------------------------------------ */
/*  Public API: sond_seadrive_ensure_hydrated_multi (Auszug-Fall)      */
/* ------------------------------------------------------------------ */

/* Nutzer-Wunsch 18.09.2026: der Auszug-Fall im Auswertungsverzeichnis
 * (mehrere Kind-Anbindungen unter einem Strukturpunkt werden zu einer
 * gemeinsamen Ansicht zusammengefasst, s. zond_treeview_open_auszug(),
 * zond_treeview.c) kann mehrere verschiedene reale PDF-Dateien
 * betreffen, die jede für sich hydriert werden müssen: "Für alle
 * betroffenen PDF muß erforderlichenfalls die Hydrierung angestoßen
 * werden. Erneuter Doppelklick muß dann halt den Download-Status für
 * alle betroffenen - das heißt noch nicht hydrierten - Dateien anzeigen.
 * Schließen und Abbruch wie gehabt."
 *
 * Analog zum Einzeldatei-Fall (sond_seadrive_ensure_hydrated() oben),
 * aber für eine Menge von Pfaden: pro betroffener, noch nicht
 * hydrierter Datei wird die Hydrierung angestoßen (No-Op, falls schon
 * läuft); war beim Aufruf schon mindestens eine davon in Hydrierung
 * (= zweiter Doppelklick), wird EIN gemeinsamer Dialog mit je einer
 * Fortschrittszeile pro noch nicht hydrierter Datei gezeigt (statt N
 * einzelner Dialoge). "Schließen" schließt nur den Dialog (Downloads
 * laufen unbeobachtet weiter, wie beim Einzeldatei-Dialog); "Abbrechen"
 * bricht alle noch laufenden Einträge gleichzeitig ab. Der Dialog
 * schließt sich von selbst, sobald ALLE Einträge fertig sind. */

typedef struct {
    gchar     *full_path;
    GtkWidget *label;
    GtkWidget *progress_bar;
    guint64    file_size;
    gboolean   logged_failure;
    gboolean   done;
} HydrateProgressEntryMulti;

typedef struct {
    GPtrArray *entries; /* HydrateProgressEntryMulti*, eigene Kopien */
    GtkWidget *dialog;
    GtkWidget *abbrechen_button;
    guint      timeout_id;
} HydrateProgressUiMulti;

/* Ermittelt per CfGetPlaceholderInfo() den Hydrierungs-Fortschritt eines
 * einzelnen Eintrags (Datei) und aktualisiert dessen Fortschrittsbalken. */
static void hydrate_progress_entry_update(HydrateProgressEntryMulti *entry)
{
    wchar_t *lp;
    HANDLE   h;

    lp = prepare_long_path(entry->full_path, NULL);
    if (!lp)
        return;

    h = CreateFileW(lp, FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    g_free(lp);
    if (h == INVALID_HANDLE_VALUE)
        return;

    cfapi_init();
    if (g_CfGetPlaceholderInfo) {
        SeaDrivePlaceholderStandardInfo info = { 0 };
        DWORD returned_length = 0;
        HRESULT hr = g_CfGetPlaceholderInfo(h, CF_PLACEHOLDER_INFO_STANDARD,
                &info, sizeof(info), &returned_length);

        if (SUCCEEDED(hr)) {
            guint64 on_disk = (guint64) info.OnDiskDataSize.QuadPart;
            gdouble fraction = 0.0;
            gchar *on_disk_str, *total_str, *text;

            if (entry->file_size > 0) {
                fraction = (gdouble) on_disk / (gdouble) entry->file_size;
                if (fraction > 1.0)
                    fraction = 1.0;
            }
            gtk_progress_bar_set_fraction(
                    GTK_PROGRESS_BAR(entry->progress_bar), fraction);

            on_disk_str = g_format_size(on_disk);
            total_str = g_format_size(entry->file_size);
            text = g_strdup_printf("%s von %s", on_disk_str, total_str);
            gtk_progress_bar_set_text(GTK_PROGRESS_BAR(entry->progress_bar),
                    text);
            g_free(on_disk_str);
            g_free(total_str);
            g_free(text);
        } else if (!entry->logged_failure) {
            LOG_WARN("%s: CfGetPlaceholderInfo('%s'): 0x%08lX "
                    "(returned_length=%lu, sizeof(info)=%zu)", __func__,
                    entry->full_path, (unsigned long) hr,
                    (unsigned long) returned_length, sizeof(info));
            entry->logged_failure = TRUE;
        }
    }

    CloseHandle(h);
}

static void hydrate_progress_entry_free(gpointer data)
{
    HydrateProgressEntryMulti *entry = (HydrateProgressEntryMulti *) data;
    g_free(entry->full_path);
    g_free(entry);
}

/* Aktualisiert alle noch nicht fertigen Einträge (Fortschrittsbalken
 * bzw. "fertig", falls die Hydrierung inzwischen endete) - OHNE den
 * Dialog bei Bedarf zu schließen (das übernimmt separat
 * hydrate_progress_tick_multi(), s.u.). Getrennt gehalten, damit der
 * initiale, synchrone Aufruf direkt nach dem Aufbau des Dialogs (s.
 * sond_seadrive_show_hydrate_progress_dialog_multi()) nicht riskiert,
 * das gerade erst erzeugte ui bei sofort schon abgeschlossener
 * Hydrierung wieder freizugeben, bevor der Aufrufer fertig damit ist
 * (Use-after-free) - anders als hydrate_progress_tick_multi(), das nur
 * als g_timeout_add()-Callback läuft, nachdem der Aufrufer längst
 * zurückgekehrt ist. */
static void hydrate_progress_update_multi(HydrateProgressUiMulti *ui)
{
    guint i;

    for (i = 0; i < ui->entries->len; i++) {
        HydrateProgressEntryMulti *entry = g_ptr_array_index(ui->entries, i);

        if (entry->done)
            continue;

        if (!sond_seadrive_is_hydrating(entry->full_path)) {
            entry->done = TRUE;
            gtk_progress_bar_set_fraction(
                    GTK_PROGRESS_BAR(entry->progress_bar), 1.0);
            gtk_progress_bar_set_text(GTK_PROGRESS_BAR(entry->progress_bar),
                    "fertig");
            continue;
        }

        hydrate_progress_entry_update(entry);
    }
}

static gboolean hydrate_progress_all_done_multi(HydrateProgressUiMulti *ui)
{
    guint i;

    for (i = 0; i < ui->entries->len; i++) {
        HydrateProgressEntryMulti *entry = g_ptr_array_index(ui->entries, i);
        if (!entry->done)
            return FALSE;
    }
    return TRUE;
}

static gboolean hydrate_progress_tick_multi(gpointer data)
{
    HydrateProgressUiMulti *ui = (HydrateProgressUiMulti *) data;

    hydrate_progress_update_multi(ui);

    if (hydrate_progress_all_done_multi(ui)) {
        /* Alle betroffenen Dateien fertig (Erfolg, Fehler oder Abbruch) -
         * Dialog selbstständig schließen. */
        ui->timeout_id = 0;
        gtk_widget_destroy(ui->dialog);
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

static void cb_hydrate_progress_dialog_destroy_multi(GtkWidget *dialog,
        gpointer data)
{
    HydrateProgressUiMulti *ui = (HydrateProgressUiMulti *) data;
    (void) dialog;

    if (ui->timeout_id)
        g_source_remove(ui->timeout_id);
    g_ptr_array_free(ui->entries, TRUE);
    g_free(ui);
}

static void cb_hydrate_progress_abbrechen_clicked_multi(GtkButton *button,
        gpointer data)
{
    HydrateProgressUiMulti *ui = (HydrateProgressUiMulti *) data;
    guint i;

    for (i = 0; i < ui->entries->len; i++) {
        HydrateProgressEntryMulti *entry = g_ptr_array_index(ui->entries, i);
        if (!entry->done) {
            sond_seadrive_hydrate_cancel(entry->full_path);
            gtk_label_set_text(GTK_LABEL(entry->label), "Wird abgebrochen...");
        }
    }
    gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
}

/*
 * sond_seadrive_show_hydrate_progress_dialog_multi:
 * Wie sond_seadrive_show_hydrate_progress_dialog(), aber für mehrere
 * gleichzeitig betroffene Dateien - eine Fortschrittszeile (Dateiname +
 * Balken) pro Eintrag in full_paths, ein gemeinsamer "Abbrechen"-Button
 * (bricht alle noch laufenden Einträge ab) und ein gemeinsamer
 * "Schließen"-Button. Schließt sich automatisch, sobald ALLE Einträge
 * fertig sind.
 */
void sond_seadrive_show_hydrate_progress_dialog_multi(GtkWindow *parent,
        GPtrArray *full_paths)
{
    HydrateProgressUiMulti *ui;
    GtkWidget *content_area;
    GtkWidget *scrolled_window;
    GtkWidget *vbox;
    GtkWidget *button;
    guint i;

    ui = g_new0(HydrateProgressUiMulti, 1);
    ui->entries = g_ptr_array_new_with_free_func(hydrate_progress_entry_free);

    ui->dialog = gtk_dialog_new_with_buttons("Download läuft", parent,
            GTK_DIALOG_DESTROY_WITH_PARENT, NULL, NULL);
    gtk_window_set_default_size(GTK_WINDOW(ui->dialog), 460, -1);

    /* Nutzer-Wunsch 18.09.2026: bei vielen betroffenen Dateien (Auszug
     * mit entsprechend vielen Anbindungen) sollen trotzdem alle
     * Fortschrittszeilen einsehbar bleiben, ohne dass der Dialog selbst
     * über den Bildschirm hinaus wächst - Liste deshalb in ein
     * GtkScrolledWindow mit fester Maximalhöhe gepackt (scrollt ab ca.
     * 5 Zeilen; propagate_natural_height lässt den Dialog bei WENIGER
     * Einträgen trotzdem passend klein bleiben, statt immer die volle
     * Maximalhöhe zu belegen). */
    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
            GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(
            GTK_SCROLLED_WINDOW(scrolled_window), 60);
    gtk_scrolled_window_set_max_content_height(
            GTK_SCROLLED_WINDOW(scrolled_window), 320);
    gtk_scrolled_window_set_propagate_natural_height(
            GTK_SCROLLED_WINDOW(scrolled_window), TRUE);

    content_area = gtk_dialog_get_content_area(GTK_DIALOG(ui->dialog));
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);

    for (i = 0; i < full_paths->len; i++) {
        const gchar *full_path = g_ptr_array_index(full_paths, i);
        HydrateProgressEntryMulti *entry = g_new0(HydrateProgressEntryMulti, 1);
        gchar *basename;
        wchar_t *lp;
        LARGE_INTEGER size = { .QuadPart = 0 };

        entry->full_path = g_strdup(full_path);

        lp = prepare_long_path(full_path, NULL);
        if (lp) {
            HANDLE h = CreateFileW(lp, FILE_READ_ATTRIBUTES,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
            g_free(lp);
            if (h != INVALID_HANDLE_VALUE) {
                GetFileSizeEx(h, &size);
                CloseHandle(h);
            }
        }
        entry->file_size = (guint64) size.QuadPart;

        basename = g_path_get_basename(full_path);
        entry->label = gtk_label_new(basename);
        gtk_label_set_line_wrap(GTK_LABEL(entry->label), TRUE);
        gtk_widget_set_halign(entry->label, GTK_ALIGN_START);
        g_free(basename);
        gtk_box_pack_start(GTK_BOX(vbox), entry->label, FALSE, FALSE, 0);

        entry->progress_bar = gtk_progress_bar_new();
        gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(entry->progress_bar),
                TRUE);
        gtk_box_pack_start(GTK_BOX(vbox), entry->progress_bar, FALSE, FALSE, 0);

        g_ptr_array_add(ui->entries, entry);
    }

    gtk_container_add(GTK_CONTAINER(scrolled_window), vbox);
    gtk_container_add(GTK_CONTAINER(content_area), scrolled_window);

    button = gtk_dialog_add_button(GTK_DIALOG(ui->dialog), "Schließen",
            GTK_RESPONSE_NONE);
    g_signal_connect_swapped(button, "clicked",
            G_CALLBACK(gtk_widget_destroy), ui->dialog);

    ui->abbrechen_button = gtk_dialog_add_button(GTK_DIALOG(ui->dialog),
            "Abbrechen", GTK_RESPONSE_NONE);
    g_signal_connect(ui->abbrechen_button, "clicked",
            G_CALLBACK(cb_hydrate_progress_abbrechen_clicked_multi), ui);

    g_signal_connect(ui->dialog, "destroy",
            G_CALLBACK(cb_hydrate_progress_dialog_destroy_multi), ui);

    gtk_widget_show_all(ui->dialog);

    hydrate_progress_update_multi(ui); /* initiale Anzeige, kein Auto-Destroy */
    ui->timeout_id = g_timeout_add(300, hydrate_progress_tick_multi, ui);
}

/*
 * sond_seadrive_ensure_hydrated_multi:
 * Wie sond_seadrive_ensure_hydrated(), aber für eine Menge von Pfaden
 * (Auszug-Fall) - s. ausführlichen Doc-Kommentar oberhalb der Structs.
 * full_paths wird nur gelesen (full_path-Strings werden bei Bedarf
 * kopiert), Aufrufer bleibt Eigentümer.
 */
gboolean sond_seadrive_ensure_hydrated_multi(GtkWindow *parent,
        GPtrArray *full_paths)
{
    GPtrArray *pending;
    gboolean any_hydrating = FALSE;
    gboolean result = TRUE;
    guint i;

    if (!full_paths || full_paths->len == 0)
        return TRUE;

    pending = g_ptr_array_new();
    for (i = 0; i < full_paths->len; i++) {
        const gchar *path = g_ptr_array_index(full_paths, i);
        if (path && sond_seadrive_needs_hydration(path)) {
            g_ptr_array_add(pending, (gpointer) path);
            if (sond_seadrive_is_hydrating(path))
                any_hydrating = TRUE;
        }
    }

    if (pending->len > 0) {
        for (i = 0; i < pending->len; i++)
            sond_seadrive_hydrate_async(
                    (const gchar *) g_ptr_array_index(pending, i));

        if (any_hydrating)
            sond_seadrive_show_hydrate_progress_dialog_multi(parent, pending);

        result = FALSE;
    }

    g_ptr_array_free(pending, TRUE);
    return result;
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

    /* fs leaf files (auch eingebettete Teile - Mime-Parts, ZIP-Einträge,
     * PDF-Seiten): path_or_section ist NULL, sfp gibt zunächst nur den
     * eigenen (bei eingebetteten Teilen ggf. rein internen/synthetischen)
     * Pfad des jeweiligen Teils her.
     *
     * Nutzer-Fund 18.09.2026: "Anwahl von 'Immer offline verfügbar' wirkt
     * nur bei Message, nicht bei den mimeparts" - bisher wurde genau
     * dieser eigene sfp-Pfad direkt verwendet, was für Top-Level-Dateien
     * (kein Parent, z.B. den Message-Knoten selbst) zufällig stimmt, für
     * einen mit Parent (Mime-Part/ZIP-Eintrag/PDF-Seite) aber einen
     * nicht-existenten Pfad ergibt - sond_seadrive_set_pin_state()
     * schlägt dann für diese Zeilen wirkungslos fehl. Der Pin-/
     * Hydrierungsstatus gehört aber ohnehin zur realen Datei als janzem,
     * nicht zum einzelnen Teil - deshalb jetzt, analog zum selben Fund im
     * SeaDrive-Badge (render_file_icon(), ToDo.c 18.09.2026), konsequent
     * zum obersten Vorfahren hochgelaufen und dessen Pfad verwendet. */
    sfp = sond_tvfm_item_get_sond_file_part(stvfm_item);
    if (sfp) {
        const gchar *sfp_path;

        while (sond_file_part_get_parent(sfp))
            sfp = sond_file_part_get_parent(sfp);

        sfp_path = sond_file_part_get_path(sfp);
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
/*  Public: apply to root directory ("Gesamtes Projekt")               */
/* ------------------------------------------------------------------ */

/* War früher intern und wurde aus dem BAUM_FS-Kontextmenü heraus
 * aufgerufen ("Gesamtes Verzeichnis"). Seit 11.09.2026 öffentlich, da nur
 * noch vom Hauptmenü (win.sd-*-all, headerbar.c) aus erreichbar - die
 * Aktion betraf schon immer die Projekt-Wurzel unabhängig von Selektion/
 * Rechtsklick-Ziel und gehörte damit eigentlich nie in ein Kontextmenü,
 * s. sond_seadrive.h. */
void sond_treeviewfm_seadrive_pin_root(SondTreeviewFM *stvfm, guint pin_state)
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
/*  Public: apply to current selection ("Auswahl")                    */
/* ------------------------------------------------------------------ */

/* Wendet pin_state auf die aktuelle Selektion in stvfm an (rekursiv bei
 * ausgewählten Ordnern, s. seadrive_pin_foreach()/apply_pin_state_to_item()).
 * War früher nur inline in seadrive_action_activate(); seit 11.09.2026
 * öffentlich, da auch vom globalen Hauptmenü aus genutzt ("Projekt >
 * SeaDrive > .../Auswahl", win.sd-*-sel in headerbar.c), wenn BAUM_FS
 * gerade der Baum mit einer Selektion ist (s. zond_baum_mit_auswahl()). */
void sond_treeviewfm_seadrive_pin_selection(SondTreeviewFM *stvfm,
        guint pin_state)
{
    GError *error = NULL;

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
/*  GSimpleAction-Callback (ersetzt seadrive_menu_activate)           */
/* ------------------------------------------------------------------ */

static void seadrive_action_activate(GSimpleAction *action, GVariant *parameter,
        gpointer data)
{
    SondTreeviewFM *stvfm = SOND_TREEVIEWFM(data);
    guint pin_state = (guint) GPOINTER_TO_INT(
            g_object_get_data(G_OBJECT(action), "pin_state"));

    /* Im Kontextmenü gibt es seit 11.09.2026 nur noch "Auswahl" - "Gesamtes
     * Projekt" (sond_treeviewfm_seadrive_pin_root()) ist nur noch über das
     * Hauptmenü erreichbar, s. sond_treeviewfm_seadrive_init_contextmenu(). */
    sond_treeviewfm_seadrive_pin_selection(stvfm, pin_state);
}

/* ------------------------------------------------------------------ */
/*  Public: attach SeaDrive submenu to context menu                   */
/* ------------------------------------------------------------------ */

void sond_treeviewfm_seadrive_init_contextmenu(SondTreeviewFM *stvfm)
{
    /* Nur noch Aktionen registrieren - GMenu-Sections wurden
     * bereits in sond_treeviewfm_class_init aufgebaut. */
    GSimpleActionGroup *ag = sond_treeview_get_action_group(SOND_TREEVIEW(stvfm));

    /* "-all"-Varianten (wirkten schon immer auf die Projekt-Wurzel,
     * unabhängig von Selektion/Rechtsklick-Ziel) gibt es seit 11.09.2026
     * nur noch im Hauptmenü (win.sd-*-all, headerbar.c) - hier im
     * Kontextmenü bewusst nur noch "Auswahl", s. sond_treeviewfm.c
     * (add_base_menu) und sond_seadrive.h. */
    struct { const gchar *name; guint pin_state; } actions[] = {
        { "sd-pin-sel",     STVFM_PIN_STATE_PINNED      },
        { "sd-unspec-sel",  STVFM_PIN_STATE_UNSPECIFIED },
        { "sd-unpin-sel",   STVFM_PIN_STATE_UNPINNED    },
    };

    for (guint i = 0; i < G_N_ELEMENTS(actions); i++) {
        GSimpleAction *act = g_simple_action_new(actions[i].name, NULL);
        g_object_set_data(G_OBJECT(act), "pin_state",
                GINT_TO_POINTER((gint) actions[i].pin_state));
        g_signal_connect(act, "activate",
                G_CALLBACK(seadrive_action_activate), stvfm);
        g_action_map_add_action(G_ACTION_MAP(ag), G_ACTION(act));
        g_object_unref(act);
    }
}

/* Graut die "Auswahl"-SeaDrive-Einträge im Kontextmenü von stvfm ein/aus.
 * Von project_set_widgets_sensitive() (project.c) aufgerufen, wenn ein
 * Projekt geöffnet/geschlossen wird bzw. sich herausstellt, ob dessen
 * Wurzel überhaupt ein SeaDrive-Verzeichnis ist (sond_treeviewfm_is_
 * seadrive_path()) - Nutzer-Feedback 11.09.2026: ohne SeaDrive-Projekt
 * sollen die Menüpunkte nicht anwählbar sein statt wirkungslos. */
void sond_treeviewfm_seadrive_set_contextmenu_sensitive(SondTreeviewFM *stvfm,
        gboolean sensitive)
{
    GSimpleActionGroup *ag = sond_treeview_get_action_group(SOND_TREEVIEW(stvfm));
    const gchar *names[] = { "sd-pin-sel", "sd-unspec-sel", "sd-unpin-sel" };

    for (guint i = 0; i < G_N_ELEMENTS(names); i++) {
        GAction *a = g_action_map_lookup_action(G_ACTION_MAP(ag), names[i]);
        if (a)
            g_simple_action_set_enabled(G_SIMPLE_ACTION(a), sensitive);
    }
}

/* ------------------------------------------------------------------ */
/*  Verschoben aus sond_treeviewfm.c (Refactoring 18.09.2026, "in       */
/*  _treeviewfm.c sind auch Funktionen, die in sond_treeviewfm_seadrive */
/*  gehören") - Zugriff auf SondTreeviewFMPrivate/SondTVFMItemPrivate   */
/*  über die Freund-Accessoren sond_treeviewfm_get_priv()/              */
/*  sond_tvfm_item_get_priv() aus sond_treeviewfm_private.h.            */
/* ------------------------------------------------------------------ */

void
sond_treeviewfm_seadrive_dir_delta(SondTreeviewFM *stvfm,
		const gchar *dir_path, gint delta_not_hydrated,
		gint delta_hydrated_pinned, gint delta_total) {
	SondTreeviewFMPrivate *p = NULL;
	SondSeadriveDirCounts *counts = NULL;

	if (!stvfm || !dir_path || (delta_not_hydrated == 0 &&
			delta_hydrated_pinned == 0 && delta_total == 0))
		return;

	p = sond_treeviewfm_get_priv(stvfm);

	if (!p->seadrive_dir_counts)
		p->seadrive_dir_counts = g_hash_table_new_full(
				g_str_hash, g_str_equal, g_free, g_free);

	counts = g_hash_table_lookup(p->seadrive_dir_counts, dir_path);
	if (!counts) {
		counts = g_new0(SondSeadriveDirCounts, 1);
		g_hash_table_insert(p->seadrive_dir_counts, g_strdup(dir_path), counts);
	}

	/* Negative Deltas bei 0 kappen statt umlaufen zu lassen (guint!) -
	 * Schutz gegen Drift durch verpasste/doppelte Events, analog den
	 * Guards bei seadrive_pending_down/-up. */
	if (delta_not_hydrated < 0 && (guint) -delta_not_hydrated > counts->not_hydrated)
		counts->not_hydrated = 0;
	else
		counts->not_hydrated += delta_not_hydrated;

	if (delta_hydrated_pinned < 0 && (guint) -delta_hydrated_pinned > counts->hydrated_pinned)
		counts->hydrated_pinned = 0;
	else
		counts->hydrated_pinned += delta_hydrated_pinned;

	if (delta_total < 0 && (guint) -delta_total > counts->total)
		counts->total = 0;
	else
		counts->total += delta_total;

	gtk_widget_queue_draw(GTK_WIDGET(stvfm));
}

void
sond_treeviewfm_seadrive_update_dir_coverage(SondTreeviewFM *stvfm,
		const gchar *file_full_path, gint delta_not_hydrated,
		gint delta_hydrated_pinned, gint delta_total) {
	const gchar *root = NULL;
	gchar *dir = NULL;
	gchar *slash = NULL;

	if (!file_full_path || (delta_not_hydrated == 0 &&
			delta_hydrated_pinned == 0 && delta_total == 0))
		return;

	root = sond_treeviewfm_get_root(stvfm);
	if (!root)
		return;

	dir = g_strdup(file_full_path);

	for (;;) {
		slash = strrchr(dir, '/');
		if (!slash)
			break;
		*slash = '\0';

		sond_treeviewfm_seadrive_dir_delta(stvfm, dir, delta_not_hydrated,
				delta_hydrated_pinned, delta_total);

		if (!g_strcmp0(dir, root))
			break; /* root selbst mit erledigt - keine Vorfahren mehr darüber */
	}

	g_free(dir);
}

void
sond_treeviewfm_seadrive_update_status(SondTreeviewFM *stvfm,
		const gchar *path_pending_down, gint delta_down,
		const gchar *path_up, gboolean up_pending) {
	gboolean changed = FALSE;
	SondTreeviewFMPrivate *p = sond_treeviewfm_get_priv(stvfm);

	if (delta_down > 0 && path_pending_down) {
		if (!p->seadrive_pending_down_paths)
			p->seadrive_pending_down_paths = g_hash_table_new_full(
					g_str_hash, g_str_equal, g_free, NULL);
		if (g_hash_table_add(p->seadrive_pending_down_paths,
				g_strdup(path_pending_down))) {
			p->seadrive_pending_down++;
			changed = TRUE;
		}
	} else if (delta_down < 0 && path_pending_down) {
		if (p->seadrive_pending_down_paths &&
				g_hash_table_remove(p->seadrive_pending_down_paths,
						path_pending_down)) {
			p->seadrive_pending_down--;
			changed = TRUE;
		}
	}

	if (path_up) {
		if (!p->seadrive_not_in_sync)
			p->seadrive_not_in_sync = g_hash_table_new_full(
					g_str_hash, g_str_equal, g_free, NULL);
		if (up_pending) {
			if (g_hash_table_add(p->seadrive_not_in_sync, g_strdup(path_up)))
				p->seadrive_pending_up++;
		} else {
			if (g_hash_table_remove(p->seadrive_not_in_sync, path_up))
				p->seadrive_pending_up--;
		}
		changed = TRUE;
	}

	if (changed) {
		gtk_widget_queue_draw(GTK_WIDGET(stvfm));
		g_signal_emit(stvfm,
				SOND_TREEVIEWFM_GET_CLASS(stvfm)->signal_seadrive_status, 0,
				p->seadrive_pending_down,
				p->seadrive_pending_up);
	}
}

void
sond_treeviewfm_seadrive_set_pending_down_paths(SondTreeviewFM *stvfm,
		GHashTable *paths) {
	SondTreeviewFMPrivate *p = sond_treeviewfm_get_priv(stvfm);

	if (p->seadrive_pending_down_paths)
		g_hash_table_destroy(p->seadrive_pending_down_paths);
	p->seadrive_pending_down_paths = paths;
	p->seadrive_pending_down = paths ? g_hash_table_size(paths) : 0;

	gtk_widget_queue_draw(GTK_WIDGET(stvfm));
	g_signal_emit(stvfm,
			SOND_TREEVIEWFM_GET_CLASS(stvfm)->signal_seadrive_status, 0,
			p->seadrive_pending_down,
			p->seadrive_pending_up);
}

void
sond_treeviewfm_seadrive_set_dir_counts(SondTreeviewFM *stvfm,
		GHashTable *dir_counts) {
	SondTreeviewFMPrivate *p = sond_treeviewfm_get_priv(stvfm);

	if (p->seadrive_dir_counts)
		g_hash_table_destroy(p->seadrive_dir_counts);
	p->seadrive_dir_counts = dir_counts;

	gtk_widget_queue_draw(GTK_WIDGET(stvfm));
}

void
sond_treeviewfm_seadrive_set_file_badges(SondTreeviewFM *stvfm,
		GHashTable *badges) {
	SondTreeviewFMPrivate *p = sond_treeviewfm_get_priv(stvfm);

	if (p->seadrive_file_badges)
		g_hash_table_destroy(p->seadrive_file_badges);
	p->seadrive_file_badges = badges;

	gtk_widget_queue_draw(GTK_WIDGET(stvfm));
	g_signal_emit(stvfm,
			SOND_TREEVIEWFM_GET_CLASS(stvfm)->signal_seadrive_status, 0,
			p->seadrive_pending_down,
			p->seadrive_pending_up);
}

SondSeadriveBadge
sond_treeviewfm_seadrive_get_file_badge(SondTreeviewFM *stvfm,
		const gchar *file_full_path) {
	SondTreeviewFMPrivate *p = NULL;
	gpointer val = NULL;

	if (!stvfm || !file_full_path)
		return SOND_SEADRIVE_BADGE_NONE;

	p = sond_treeviewfm_get_priv(stvfm);
	if (!p->seadrive_file_badges)
		return SOND_SEADRIVE_BADGE_NONE;

	if (!g_hash_table_lookup_extended(p->seadrive_file_badges, file_full_path,
			NULL, &val))
		return SOND_SEADRIVE_BADGE_NONE;

	return (SondSeadriveBadge) GPOINTER_TO_INT(val);
}

/* Leitet aus einem SondSeadriveBadge-Wert ab, ob die Datei für die Ordner-
 * Coverage-Statistik als "nicht hydriert" bzw. "hydriert+gepinnt" zählt -
 * einzige Stelle, an der diese Zuordnung getroffen wird (Konsistenz
 * zwischen Scan und Live-Update, s. watcher_count_pending_down() in
 * sond_treeviewfm_seadrive.c, die dieselbe Logik redundant, aber
 * gleichlautend anwendet). */
static void
seadrive_badge_to_coverage(SondSeadriveBadge badge,
		gboolean *out_not_hydrated, gboolean *out_hydrated_pinned) {
	*out_not_hydrated = (badge == SOND_SEADRIVE_BADGE_OFFLINE ||
			badge == SOND_SEADRIVE_BADGE_PENDING);
	*out_hydrated_pinned = (badge == SOND_SEADRIVE_BADGE_PINNED);
}

void
sond_treeviewfm_seadrive_update_file_badge(SondTreeviewFM *stvfm,
		const gchar *file_full_path, SondSeadriveBadge new_badge,
		gint delta_total) {
	SondTreeviewFMPrivate *p = NULL;
	SondSeadriveBadge old_badge = SOND_SEADRIVE_BADGE_NONE;
	gpointer old_val = NULL;
	gboolean old_not_hydrated = FALSE, old_hydrated_pinned = FALSE;
	gboolean new_not_hydrated = FALSE, new_hydrated_pinned = FALSE;
	gint delta_not_hydrated = 0, delta_hydrated_pinned = 0;

	if (!stvfm || !file_full_path)
		return;

	p = sond_treeviewfm_get_priv(stvfm);

	if (p->seadrive_file_badges &&
			g_hash_table_lookup_extended(p->seadrive_file_badges,
					file_full_path, NULL, &old_val))
		old_badge = (SondSeadriveBadge) GPOINTER_TO_INT(old_val);

	if (old_badge != new_badge) {
		seadrive_badge_to_coverage(old_badge, &old_not_hydrated,
				&old_hydrated_pinned);
		seadrive_badge_to_coverage(new_badge, &new_not_hydrated,
				&new_hydrated_pinned);
		delta_not_hydrated = (gint) new_not_hydrated - (gint) old_not_hydrated;
		delta_hydrated_pinned = (gint) new_hydrated_pinned -
				(gint) old_hydrated_pinned;

		if (new_badge == SOND_SEADRIVE_BADGE_NONE) {
			if (p->seadrive_file_badges)
				g_hash_table_remove(p->seadrive_file_badges, file_full_path);
		} else {
			if (!p->seadrive_file_badges)
				p->seadrive_file_badges = g_hash_table_new_full(
						g_str_hash, g_str_equal, g_free, NULL);
			g_hash_table_insert(p->seadrive_file_badges,
					g_strdup(file_full_path), GINT_TO_POINTER(new_badge));
		}

		gtk_widget_queue_draw(GTK_WIDGET(stvfm));
		g_signal_emit(stvfm,
				SOND_TREEVIEWFM_GET_CLASS(stvfm)->signal_seadrive_status, 0,
				p->seadrive_pending_down,
				p->seadrive_pending_up);
	}

	if (delta_not_hydrated != 0 || delta_hydrated_pinned != 0 ||
			delta_total != 0)
		sond_treeviewfm_seadrive_update_dir_coverage(stvfm, file_full_path,
				delta_not_hydrated, delta_hydrated_pinned, delta_total);
}

SondSeadriveDirStatus
sond_treeviewfm_seadrive_get_dir_status(SondTreeviewFM *stvfm,
		const gchar *dir_path) {
	SondTreeviewFMPrivate *p = NULL;
	SondSeadriveDirCounts *counts = NULL;

	if (!stvfm || !dir_path)
		return SOND_SEADRIVE_DIR_STATUS_NONE;

	p = sond_treeviewfm_get_priv(stvfm);
	if (!p->seadrive_dir_counts)
		return SOND_SEADRIVE_DIR_STATUS_NONE;

	counts = g_hash_table_lookup(p->seadrive_dir_counts, dir_path);
	if (!counts || counts->total == 0)
		return SOND_SEADRIVE_DIR_STATUS_NONE;

	if (counts->not_hydrated == counts->total)
		return SOND_SEADRIVE_DIR_STATUS_FULL_OFFLINE;

	if (counts->not_hydrated == 0) {
		/* alle Dateien hydriert */
		if (counts->hydrated_pinned == counts->total)
			return SOND_SEADRIVE_DIR_STATUS_FULL_HYDRATED_PINNED;
		/* hydriert, aber nicht alle gepinnt - dieselbe "kein Icon
		 * nötig"-Bedeutung wie beim Datei-Badge */
		return SOND_SEADRIVE_DIR_STATUS_NONE;
	}

	/* 0 < not_hydrated < total - weder komplett hydriert noch komplett
	 * offline, kein gemeinsamer Nenner */
	return SOND_SEADRIVE_DIR_STATUS_MIXED;
}

void
sond_treeviewfm_seadrive_item_hydrated(SondTreeviewFM *stvfm,
		const gchar *full_path) {
	GtkTreeIter iter = { 0 };
	SondTVFMItem *stvfm_item = NULL;
	SondTVFMItemPrivate *stvfm_item_priv = NULL;
	SondFilePart *sfp_old = NULL;
	SondFilePart *sfp_new = NULL;
	const gchar *rel_path = NULL;
	int rc = 0;
	SondTVFMItemType type = 0;

	const gchar *root = sond_treeviewfm_get_root(stvfm);
	if (!root || !full_path)
		return;

	/* relativen Pfad ermitteln */
	gsize root_len = strlen(root);
	if (!g_str_has_prefix(full_path, root))
		return;
	rel_path = full_path + root_len;
	if (*rel_path == '/' || *rel_path == '\\')
		rel_path++;
	if (!*rel_path)
		return;

	/* Knoten im sichtbaren Baum suchen - nicht expandieren */
	rc = sond_treeviewfm_file_part_visible(stvfm, NULL, rel_path, FALSE,
			&iter, NULL);
	if (rc != 1)
		return; /* nicht sichtbar - wird beim nächsten Expandieren korrekt geladen */

	gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)),
			&iter, 0, &stvfm_item, -1);
	if (!stvfm_item)
		return;

	stvfm_item_priv = sond_tvfm_item_get_priv(stvfm_item);
	sfp_old = stvfm_item_priv->sond_file_part;
	type = stvfm_item_priv->type;
	g_object_unref(stvfm_item);

	/* Nur korrigieren wenn Item als einfaches LEAF geladen wurde
	 * (d.h. es war offline beim Laden und wurde nicht auf Kinder geprüft) */
	if (type != SOND_TVFM_ITEM_TYPE_LEAF ||
			!SOND_IS_FILE_PART_LEAF(sfp_old))
		return;

	/* Altes sfp aus arr_opened_files entfernen damit sond_file_part_create
	 * nicht das alte LEAF zurückgibt anstatt ein neues PDF/ZIP/GMessage zu erstellen */
	GPtrArray *arr = sond_file_part_get_arr_opened_files(
			sond_file_part_get_parent(sfp_old));
	if (arr)
		g_ptr_array_remove_fast(arr, sfp_old);

	/* Neues sfp mit korrektem Typ erstellen */
	GError* error = NULL;
	sfp_new = sond_file_part_create(sond_file_part_get_parent(sfp_old),
			rel_path, &error);

	if (!sfp_new) {
		LOG_WARN("SondFilePart kann nicht geöffnet werden:\n%s",
				error->message);
		g_error_free(error);

		return;
	}

	/* Altes Item im Baum durch neues ersetzen */
	SondTVFMItem *stvfm_item_new = sond_tvfm_item_create(stvfm, sfp_new, NULL);
	g_object_unref(sfp_new);

	gtk_tree_store_set(
			GTK_TREE_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm))),
			&iter, 0, stvfm_item_new, -1);

	SondTVFMItemPrivate *new_priv =
			sond_tvfm_item_get_priv(stvfm_item_new);

	/* Falls jetzt Kinder möglich: Dummy-Kind einfügen */
	if (new_priv->has_children) {
		GtkTreeIter iter_dummy = { 0 };
		gtk_tree_store_insert(GTK_TREE_STORE(
				gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm))),
				&iter_dummy, &iter, -1);
	}

	g_object_unref(stvfm_item_new);

	gtk_widget_queue_draw(GTK_WIDGET(stvfm));
}

void
sond_treeviewfm_seadrive_item_dehydrated(SondTreeviewFM *stvfm,
		const gchar *full_path) {
	GtkTreeIter iter = { 0 };
	SondTVFMItem *stvfm_item = NULL;
	SondTVFMItemPrivate *stvfm_item_priv = NULL;
	SondFilePart *sfp_old = NULL;
	const gchar *rel_path = NULL;
	int rc = 0;

	const gchar *root = sond_treeviewfm_get_root(stvfm);
	if (!root || !full_path)
		return;

	/* relativen Pfad ermitteln */
	gsize root_len = strlen(root);
	if (!g_str_has_prefix(full_path, root))
		return;
	rel_path = full_path + root_len;
	if (*rel_path == '/' || *rel_path == '\\')
		rel_path++;
	if (!*rel_path)
		return;

	/* Knoten im sichtbaren Baum suchen - nicht expandieren */
	rc = sond_treeviewfm_file_part_visible(stvfm, NULL, rel_path, FALSE,
			&iter, NULL);
	if (rc != 1)
		return; /* nicht sichtbar */

	gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)),
			&iter, 0, &stvfm_item, -1);
	if (!stvfm_item)
		return;

	stvfm_item_priv = sond_tvfm_item_get_priv(stvfm_item);
	sfp_old = stvfm_item_priv->sond_file_part;
	g_object_unref(stvfm_item);

	/* Nur korrigieren wenn Item ein durch Hydration entstandenes DIR ist:
	 * GMessage oder ZIP als sfp, kein path_or_section (= Datei selbst, nicht Unterknoten) */
	if (stvfm_item_priv->type != SOND_TVFM_ITEM_TYPE_DIR ||
			!sfp_old ||
			stvfm_item_priv->path_or_section ||
			(!SOND_IS_FILE_PART_GMESSAGE(sfp_old) &&
			 !SOND_IS_FILE_PART_ZIP(sfp_old) &&
			 !SOND_IS_FILE_PART_PDF(sfp_old)))
		return;

	/* Altes sfp zuerst aus arr_opened_files entfernen - VOR create_leaf,
	 * damit nicht das alte sfp zurückgegeben wird */
	{
		GPtrArray *arr = sond_file_part_get_arr_opened_files(
				sond_file_part_get_parent(sfp_old));
		if (arr)
			g_ptr_array_remove_fast(arr, sfp_old);
	}

	/* MIME-Typ aus Extension ermitteln - Datei ist jetzt offline */
	const gchar *mime_type = mime_from_extension(rel_path);
	if (!mime_type)
		mime_type = "application/octet-stream";

	/* Neues LEAF-sfp erstellen */
	SondFilePart *sfp_new = sond_file_part_create_leaf(
			rel_path,
			sond_file_part_get_parent(sfp_old),
			mime_type);
	if (!sfp_new)
		return;

	/* Alle Kinder aus dem Baum entfernen */
	{
		GtkTreeIter iter_child = { 0 };
		gboolean has_child = gtk_tree_model_iter_children(
				gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm)), &iter_child, &iter);
		while (has_child)
			has_child = gtk_tree_store_remove(
					GTK_TREE_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm))),
					&iter_child);
	}

	/* Neues LEAF-Item erstellen und im Baum ersetzen */
	SondTVFMItem *stvfm_item_new = sond_tvfm_item_create(stvfm, sfp_new, NULL);
	g_object_unref(sfp_new);

	gtk_tree_store_set(
			GTK_TREE_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(stvfm))),
			&iter, 0, stvfm_item_new, -1);

	g_object_unref(stvfm_item_new);

	gtk_widget_queue_draw(GTK_WIDGET(stvfm));
}

gboolean
sond_treeviewfm_seadrive_stop_requested(SondTreeviewFM *stvfm) {
	SondTreeviewFMPrivate *p = sond_treeviewfm_get_priv(stvfm);
	return g_atomic_int_get(&p->seadrive_watcher_stop) != 0;
}

void
sond_treeviewfm_seadrive_start_watcher(SondTreeviewFM *stvfm) {
	SondTreeviewFMPrivate *p = sond_treeviewfm_get_priv(stvfm);
	if (p->seadrive_watcher_thread)
		return;
	g_atomic_int_set(&p->seadrive_watcher_stop, 0);
	p->seadrive_watcher_thread = g_thread_new(
			"seadrive-watcher",
			sond_treeviewfm_seadrive_watcher_thread,
			stvfm);
}

/* Nutzer-Fund 18.09.2026: "Schließen des Projekts bei SeaDrive-Projekten
 * dauert sehr lange (20 Sek.)". Das synchrone g_thread_join() in
 * sond_treeviewfm_seadrive_stop_watcher() blockierte den GTK-Hauptthread
 * (project_close() -> sond_treeviewfm_set_root(NULL) -> hier), bis der
 * Watcher-Thread sein CloseHandle() auf das ReadDirectoryChangesW-
 * Verzeichnis-Handle abgeschlossen hatte. Offenbar braucht SeaDrives
 * Cloud-Filtertreiber dafür regelmäßig um die 20 Sekunden (vermutlich ein
 * interner Timeout), um die dort noch ausstehende, per CancelIo() nur
 * ANGESTOSSENE (nicht sofort abgeschlossene) Directory-Change-
 * Notification wirklich abzubrechen - CloseHandle() wartet laut Windows-
 * I/O-Modell auf den Abschluss ausstehender I/O, bevor das Handle
 * wirklich freigegeben wird.
 *
 * Der Watcher-Thread fasst nach dem Setzen des Stop-Flags (s. sond_
 * treeviewfm_seadrive_watcher_thread(), Schleifenende) keinerlei stvfm-
 * Daten mehr an - nur noch CancelIo()/CloseHandle()/g_free() auf seine
 * eigenen, rein lokalen Handles/Kopien (hDir, ov.hEvent, root). Das
 * Warten auf sein Ende kann deshalb GEFAHRLOS in einen eigenen
 * kurzlebigen "Reaper"-Thread verlagert werden, SOLANGE das stvfm-Objekt
 * selbst währenddessen am Leben bleibt - das gilt für den Aufruf aus
 * sond_treeviewfm_set_root() (Projekt schließen/wechseln: das BAUM_FS-
 * Widget bleibt über die Projekt-Lebensdauer hinaus bestehen), NICHT
 * aber für den Aufruf aus sond_treeviewfm_finalize(): dort wird direkt im
 * Anschluss der private Instanz-Speicher freigegeben, ein im Hintergrund
 * noch laufender Watcher-Thread könnte dann via sond_treeviewfm_seadrive_
 * stop_requested(stvfm) auf bereits freigegebenen Speicher zugreifen
 * (Use-after-free). Deshalb zwei Varianten: die synchrone (unverändert,
 * für finalize()) und eine neue asynchrone (für set_root()). */
static gpointer seadrive_watcher_reap(gpointer data) {
	GThread *old_thread = (GThread*) data;

	g_thread_join(old_thread);

	return NULL;
}

void
sond_treeviewfm_seadrive_stop_watcher(SondTreeviewFM *stvfm) {
	SondTreeviewFMPrivate *p = sond_treeviewfm_get_priv(stvfm);
	if (!p->seadrive_watcher_thread)
		return;
	g_atomic_int_set(&p->seadrive_watcher_stop, 1);
	g_thread_join(p->seadrive_watcher_thread);
	p->seadrive_watcher_thread = NULL;
}

/* Wie sond_treeviewfm_seadrive_stop_watcher(), wartet aber NICHT im
 * aufrufenden Thread auf das Thread-Ende, s. ausführlichen Kommentar
 * oben. Nur verwenden, wenn stvfm selbst danach am Leben bleibt (aktuell:
 * sond_treeviewfm_set_root()). */
void
sond_treeviewfm_seadrive_stop_watcher_async(SondTreeviewFM *stvfm) {
	SondTreeviewFMPrivate *p = sond_treeviewfm_get_priv(stvfm);
	GThread *old_thread = NULL;

	if (!p->seadrive_watcher_thread)
		return;

	g_atomic_int_set(&p->seadrive_watcher_stop, 1);

	/* Schon hier (nicht erst nach dem Join) auf NULL setzen, damit
	 * sond_treeviewfm_seadrive_start_watcher() bei einem sofort
	 * folgenden Öffnen eines neuen Projekts nicht fälschlich "läuft
	 * schon" annimmt. */
	old_thread = p->seadrive_watcher_thread;
	p->seadrive_watcher_thread = NULL;

	/* g_thread_unref() statt g_thread_join() auf den Reaper selbst -
	 * dokumentiertes GLib-Muster für "fire and forget"-Threads, deren
	 * Ergebnis niemanden interessiert. */
	g_thread_unref(g_thread_new("seadrive-watcher-reaper",
			seadrive_watcher_reap, old_thread));
}

/* Container für die vier SeaDrive-Ground-Truth-Hashtables, deren
 * Zerstörung von sond_treeviewfm_set_root() (über sond_seadrive_reset_
 * ground_truth()) in den Hintergrund verlagert wird. Alle vier enthalten
 * ausschließlich Strings/Zahlen ohne Rückverweis auf ein stvfm-Objekt,
 * ihre Zerstörung ist deshalb von einem beliebigen Thread aus und zu
 * einem beliebigen späteren Zeitpunkt sicher. */
typedef struct {
	GHashTable *not_in_sync;
	GHashTable *pending_down_paths;
	GHashTable *dir_counts;
	GHashTable *file_badges;
} SeadriveOldTables;

static gpointer seadrive_old_tables_reap(gpointer data) {
	SeadriveOldTables *old = data;

	if (old->not_in_sync)
		g_hash_table_destroy(old->not_in_sync);
	if (old->pending_down_paths)
		g_hash_table_destroy(old->pending_down_paths);
	if (old->dir_counts)
		g_hash_table_destroy(old->dir_counts);
	if (old->file_badges)
		g_hash_table_destroy(old->file_badges);

	g_free(old);

	return NULL;
}

/* Setzt die vier SeaDrive-Ground-Truth-Hashtables auf leer zurück und
 * emittiert das Status-Signal mit (0, 0) - aufgerufen von
 * sond_treeviewfm_set_root() bei Projekt-Wechsel/-Schließen. Verschoben
 * aus sond_treeviewfm.c (Refactoring 18.09.2026, "in _treeviewfm.c sind
 * auch Funktionen, die in sond_treeviewfm_seadrive gehören") - vormals
 * SeadriveOldTables/seadrive_old_tables_reap() plus ein Inline-Block in
 * sond_treeviewfm_set_root() selbst.
 *
 * Nutzer-Fund 18.09.2026 (Folgefund - der erste Verdacht, der Watcher-
 * Thread-Join, war laut Call-Stack-Analyse per Eclipse/gdb-Suspend NICHT
 * die Ursache): der Stack zeigte den Hänger exakt HIER, in
 * g_hash_table_remove_all() auf seadrive_file_badges. Bei einem großen
 * SeaDrive-Projekt hat praktisch jede noch nicht heruntergeladene
 * (OFFLINE-)Datei einen eigenen Eintrag in dieser Tabelle - bei vielen
 * Zehn- oder Hunderttausend Dateien im Projekt entsprechend viele
 * Einträge, die remove_all() einzeln (mit je einem g_free() auf den Key-
 * String) synchron im GTK-Hauptthread abarbeiten musste. Betraf im
 * Prinzip auch die drei anderen SeaDrive-Hashtables hier, nur mit
 * typischerweise deutlich weniger Einträgen.
 *
 * Fix: die alten Tabellen werden hier nur noch aus stvfm_priv
 * "gestohlen" (Felder sofort auf NULL gesetzt, ein nachfolgender Zugriff
 * sieht also sofort "leer") und ihre komplette Zerstörung
 * (g_hash_table_destroy()) an einen kurzlebigen Hintergrund-Thread
 * abgegeben, analog zum Watcher-Reaper bei sond_treeviewfm_seadrive_
 * stop_watcher_async(). Die Tabellen enthalten ausschließlich
 * Strings/Zahlen ohne Rückverweis auf stvfm, ihre Zerstörung ist deshalb
 * unabhängig vom weiteren Leben des stvfm-Objekts sicher. */
void
sond_seadrive_reset_ground_truth(SondTreeviewFM *stvfm) {
	SondTreeviewFMPrivate *stvfm_priv = NULL;

	if (!stvfm)
		return;

	stvfm_priv = sond_treeviewfm_get_priv(stvfm);

	stvfm_priv->seadrive_pending_down = 0;
	stvfm_priv->seadrive_pending_up = 0;
	{
		SeadriveOldTables *old = g_new0(SeadriveOldTables, 1);

		old->not_in_sync = stvfm_priv->seadrive_not_in_sync;
		old->pending_down_paths = stvfm_priv->seadrive_pending_down_paths;
		old->dir_counts = stvfm_priv->seadrive_dir_counts;
		old->file_badges = stvfm_priv->seadrive_file_badges;

		stvfm_priv->seadrive_not_in_sync = NULL;
		stvfm_priv->seadrive_pending_down_paths = NULL;
		stvfm_priv->seadrive_dir_counts = NULL;
		stvfm_priv->seadrive_file_badges = NULL;

		if (old->not_in_sync || old->pending_down_paths || old->dir_counts ||
				old->file_badges)
			g_thread_unref(g_thread_new("seadrive-tables-reaper",
					seadrive_old_tables_reap, old));
		else
			g_free(old);
	}
	g_signal_emit(stvfm,
			SOND_TREEVIEWFM_GET_CLASS(stvfm)->signal_seadrive_status, 0,
			(guint)0, (guint)0);
}

#endif /* _WIN32 */
