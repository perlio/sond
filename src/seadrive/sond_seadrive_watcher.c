/*
 sond (sond_seadrive_watcher.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_seadrive_watcher.h"

#ifdef _WIN32

#include <windows.h>
#include <string.h>

#include "sond_seadrive_core.h"
#include "sond_seadrive_win.h"
#include "../sond_file_helper.h"
#include "../sond_log_and_error.h"

/* ------------------------------------------------------------------ */
/*  Objekt                                                             */
/* ------------------------------------------------------------------ */

struct _SondSeadriveWatcher {
    GObject  parent_instance;
    gchar   *root;
    GThread *thread;
    gint     stop;                /* atomares Flag: 0=laufen, 1=stoppen */
    guint    pending_down;        /* PINNED + RECALL_ON_DATA_ACCESS */
    guint    pending_up;          /* NOT_IN_SYNC */
    /* Pfade, die als "nicht synchronisiert" gezählt sind (dedupliziert die
     * pending_up-Erhöhungen bei mehrfachen LAST_WRITE-Ereignissen) */
    GHashTable *not_in_sync;
    /* Pfade, die als "pending_down" (PINNED+offline) gezählt sind: Grundlage
     * des Zählers; ohne sie ließe sich bei REMOVED nicht sagen, ob die Datei
     * mitgezählt war. Wird beim Initialscan und beim Resync ersetzt. */
    GHashTable *pending_down_paths;
    /* Rekursive Ordner-Statistik: voller Pfad -> SondSeadriveDirCounts* */
    GHashTable *dir_counts;
    /* Datei-Badge: voller Pfad -> GINT_TO_POINTER(SondSeadriveBadge); Einträge
     * mit NONE werden nicht gespeichert. Auch Grundlage der Ordner-Zähler. */
    GHashTable *file_badges;
};

G_DEFINE_TYPE(SondSeadriveWatcher, sond_seadrive_watcher, G_TYPE_OBJECT)

enum {
    SIGNAL_CHANGED,
    SIGNAL_STATUS_CHANGED,
    SIGNAL_FILE_CHANGED,
    SIGNAL_HYDRATED,
    SIGNAL_DEHYDRATED,
    N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

static void sond_seadrive_watcher_finalize(GObject *object)
{
    SondSeadriveWatcher *w = SOND_SEADRIVE_WATCHER(object);

    //Die Tabellen können sehr groß sein; wer die letzte Referenz hält (meist der
    //Reaper-Thread beim Schließen), trägt die Kosten
    if (w->not_in_sync)
        g_hash_table_destroy(w->not_in_sync);
    if (w->pending_down_paths)
        g_hash_table_destroy(w->pending_down_paths);
    if (w->dir_counts)
        g_hash_table_destroy(w->dir_counts);
    if (w->file_badges)
        g_hash_table_destroy(w->file_badges);
    g_free(w->root);

    G_OBJECT_CLASS(sond_seadrive_watcher_parent_class)->finalize(object);
}

static void sond_seadrive_watcher_class_init(SondSeadriveWatcherClass *klass)
{
    G_OBJECT_CLASS(klass)->finalize = sond_seadrive_watcher_finalize;

    //irgendein Zustand hat sich geändert (Anzeige neu zeichnen)
    signals[SIGNAL_CHANGED] = g_signal_new("changed",
            G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 0);
    //die Zähler "wird heruntergeladen" und "nicht synchron" haben sich geändert
    signals[SIGNAL_STATUS_CHANGED] = g_signal_new("status-changed",
            G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);
    //der Badge einer Datei hat sich geändert
    signals[SIGNAL_FILE_CHANGED] = g_signal_new("file-changed",
            G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 1, G_TYPE_STRING);
    //eine Datei wurde geladen bzw. wieder aus dem lokalen Speicher genommen
    signals[SIGNAL_HYDRATED] = g_signal_new("hydrated",
            G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 1, G_TYPE_STRING);
    signals[SIGNAL_DEHYDRATED] = g_signal_new("dehydrated",
            G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void sond_seadrive_watcher_init(SondSeadriveWatcher *w)
{
    (void) w;
}

SondSeadriveWatcher* sond_seadrive_watcher_new(const gchar *root)
{
    SondSeadriveWatcher *w = g_object_new(SOND_TYPE_SEADRIVE_WATCHER, NULL);

    w->root = g_strdup(root);

    return w;
}

static gboolean watcher_stop_requested(SondSeadriveWatcher *w)
{
    return g_atomic_int_get(&w->stop) != 0;
}

static void emit_status(SondSeadriveWatcher *w)
{
    g_signal_emit(w, signals[SIGNAL_STATUS_CHANGED], 0, w->pending_down,
            w->pending_up);
}

/* ------------------------------------------------------------------ */
/*  Zustand (nur im Hauptthread)                                       */
/* ------------------------------------------------------------------ */

//Ordner-Statistik für genau dir_path anpassen; negative Deltas bei 0 kappen
//(Schutz gegen Drift durch verpasste oder doppelte Ereignisse)
static void dir_delta(SondSeadriveWatcher *w, const gchar *dir_path,
        gint delta_not_hydrated, gint delta_hydrated_pinned, gint delta_total)
{
    SondSeadriveDirCounts *counts = NULL;

    if (!dir_path || (delta_not_hydrated == 0 && delta_hydrated_pinned == 0 &&
            delta_total == 0))
        return;

    if (!w->dir_counts)
        w->dir_counts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                g_free);

    counts = g_hash_table_lookup(w->dir_counts, dir_path);
    if (!counts) {
        counts = g_new0(SondSeadriveDirCounts, 1);
        g_hash_table_insert(w->dir_counts, g_strdup(dir_path), counts);
    }

    if (delta_not_hydrated < 0 && (guint) -delta_not_hydrated > counts->not_hydrated)
        counts->not_hydrated = 0;
    else
        counts->not_hydrated += delta_not_hydrated;

    if (delta_hydrated_pinned < 0 &&
            (guint) -delta_hydrated_pinned > counts->hydrated_pinned)
        counts->hydrated_pinned = 0;
    else
        counts->hydrated_pinned += delta_hydrated_pinned;

    if (delta_total < 0 && (guint) -delta_total > counts->total)
        counts->total = 0;
    else
        counts->total += delta_total;
}

//Deltas einer Datei auf alle Vorfahren-Verzeichnisse bis zur Wurzel anwenden
static void update_dir_coverage(SondSeadriveWatcher *w, const gchar *file_full_path,
        gint delta_not_hydrated, gint delta_hydrated_pinned, gint delta_total)
{
    gchar *dir = NULL;

    if (!file_full_path || !w->root || (delta_not_hydrated == 0 &&
            delta_hydrated_pinned == 0 && delta_total == 0))
        return;

    dir = g_strdup(file_full_path);

    for (;;) {
        gchar *slash = strrchr(dir, '/');

        if (!slash)
            break;
        *slash = '\0';

        dir_delta(w, dir, delta_not_hydrated, delta_hydrated_pinned, delta_total);

        if (!g_strcmp0(dir, w->root))
            break; //Wurzel selbst mit erledigt
    }

    g_free(dir);
}

static void update_status(SondSeadriveWatcher *w, const gchar *path_pending_down,
        gint delta_down, const gchar *path_up, gboolean up_pending)
{
    gboolean changed = FALSE;

    if (delta_down > 0 && path_pending_down) {
        if (!w->pending_down_paths)
            w->pending_down_paths = g_hash_table_new_full(g_str_hash, g_str_equal,
                    g_free, NULL);
        if (g_hash_table_add(w->pending_down_paths, g_strdup(path_pending_down))) {
            w->pending_down++;
            changed = TRUE;
        }
    } else if (delta_down < 0 && path_pending_down) {
        if (w->pending_down_paths &&
                g_hash_table_remove(w->pending_down_paths, path_pending_down)) {
            w->pending_down--;
            changed = TRUE;
        }
    }

    if (path_up) {
        if (!w->not_in_sync)
            w->not_in_sync = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                    NULL);
        if (up_pending) {
            if (g_hash_table_add(w->not_in_sync, g_strdup(path_up)))
                w->pending_up++;
        } else {
            if (g_hash_table_remove(w->not_in_sync, path_up))
                w->pending_up--;
        }
        changed = TRUE;
    }

    if (changed)
        emit_status(w);
}

//Zählt der Badge als "nicht hydriert" bzw. "hydriert und gepinnt"? Einzige Stelle
//dieser Zuordnung (Scan und Live-Aktualisierung stimmen so überein)
static void badge_to_coverage(SondSeadriveBadge badge, gboolean *out_not_hydrated,
        gboolean *out_hydrated_pinned)
{
    *out_not_hydrated = (badge == SOND_SEADRIVE_BADGE_OFFLINE ||
            badge == SOND_SEADRIVE_BADGE_PENDING);
    *out_hydrated_pinned = (badge == SOND_SEADRIVE_BADGE_PINNED);
}

//Badge genau einer Datei aktualisieren und davon abgeleitet die Ordner-Zähler.
//new_badge NONE für eine gelöschte Datei; delta_total +1 (neu), -1 (weg), 0 (geändert).
static void update_file_badge(SondSeadriveWatcher *w, const gchar *file_full_path,
        SondSeadriveBadge new_badge, gint delta_total)
{
    SondSeadriveBadge old_badge = SOND_SEADRIVE_BADGE_NONE;
    gpointer old_val = NULL;
    gboolean old_not_hydrated = FALSE, old_hydrated_pinned = FALSE;
    gboolean new_not_hydrated = FALSE, new_hydrated_pinned = FALSE;
    gint delta_not_hydrated = 0, delta_hydrated_pinned = 0;

    if (!file_full_path)
        return;

    if (w->file_badges &&
            g_hash_table_lookup_extended(w->file_badges, file_full_path, NULL,
                    &old_val))
        old_badge = (SondSeadriveBadge) GPOINTER_TO_INT(old_val);

    if (old_badge != new_badge) {
        badge_to_coverage(old_badge, &old_not_hydrated, &old_hydrated_pinned);
        badge_to_coverage(new_badge, &new_not_hydrated, &new_hydrated_pinned);
        delta_not_hydrated = (gint) new_not_hydrated - (gint) old_not_hydrated;
        delta_hydrated_pinned = (gint) new_hydrated_pinned -
                (gint) old_hydrated_pinned;

        if (new_badge == SOND_SEADRIVE_BADGE_NONE) {
            if (w->file_badges)
                g_hash_table_remove(w->file_badges, file_full_path);
        } else {
            if (!w->file_badges)
                w->file_badges = g_hash_table_new_full(g_str_hash, g_str_equal,
                        g_free, NULL);
            g_hash_table_insert(w->file_badges, g_strdup(file_full_path),
                    GINT_TO_POINTER(new_badge));
        }

        emit_status(w);
        g_signal_emit(w, signals[SIGNAL_FILE_CHANGED], 0, file_full_path);
    }

    if (delta_not_hydrated != 0 || delta_hydrated_pinned != 0 || delta_total != 0)
        update_dir_coverage(w, file_full_path, delta_not_hydrated,
                delta_hydrated_pinned, delta_total);
}

SondSeadriveBadge sond_seadrive_watcher_get_file_badge(SondSeadriveWatcher *w,
        const gchar *file_full_path)
{
    gpointer val = NULL;

    if (!w || !file_full_path || !w->file_badges)
        return SOND_SEADRIVE_BADGE_NONE;

    if (!g_hash_table_lookup_extended(w->file_badges, file_full_path, NULL, &val))
        return SOND_SEADRIVE_BADGE_NONE;

    return (SondSeadriveBadge) GPOINTER_TO_INT(val);
}

SondSeadriveDirStatus sond_seadrive_watcher_get_dir_status(SondSeadriveWatcher *w,
        const gchar *dir_path)
{
    SondSeadriveDirCounts *counts = NULL;

    if (!w || !dir_path || !w->dir_counts)
        return SOND_SEADRIVE_DIR_STATUS_NONE;

    counts = g_hash_table_lookup(w->dir_counts, dir_path);
    if (!counts || counts->total == 0)
        return SOND_SEADRIVE_DIR_STATUS_NONE;

    if (counts->not_hydrated == counts->total)
        return SOND_SEADRIVE_DIR_STATUS_FULL_OFFLINE;

    if (counts->not_hydrated == 0) {
        if (counts->hydrated_pinned == counts->total)
            return SOND_SEADRIVE_DIR_STATUS_FULL_HYDRATED_PINNED;
        //hydriert, aber nicht alle gepinnt: wie beim Datei-Badge kein Symbol nötig
        return SOND_SEADRIVE_DIR_STATUS_NONE;
    }

    return SOND_SEADRIVE_DIR_STATUS_MIXED;
}

guint sond_seadrive_watcher_get_pending_down(SondSeadriveWatcher *w)
{
    return w ? w->pending_down : 0;
}

guint sond_seadrive_watcher_get_pending_up(SondSeadriveWatcher *w)
{
    return w ? w->pending_up : 0;
}

/* ------------------------------------------------------------------ */
/*  Watcher: Aufträge an den Hauptthread                               */
/* ------------------------------------------------------------------ */

typedef struct {
    SondSeadriveWatcher *watcher; /* Referenz */
    gchar          *path_pending_down; /* Pfad, auf den sich delta_down bezieht
                                        * (immer gesetzt, auch als Pfad für die
                                        * Datei- und Ordner-Aktualisierung) */
    gint            delta_down;  /* +1 oder -1, 0 = keine Änderung - nur für den
                                  * Zähler "wird heruntergeladen" */
    gint            delta_total; /* +1 (ADDED/RENAMED_NEW_NAME) / -1 (REMOVED/
                                  * RENAMED_OLD_NAME) / 0 (MODIFIED) - für die
                                  * Gesamtzahl der Dateien im Ordner */
    SondSeadriveBadge coverage_badge; /* aktueller Badge der Datei (NONE bei REMOVED) */
    gchar          *path_down;       /* Pfad der hydrierten Datei, NULL sonst */
    gchar          *path_dehydrated; /* Pfad der dehydrierten Datei, NULL sonst */
    gchar          *path_up;     /* NULL oder Pfad für die pending_up-Änderung */
    gboolean        up_pending;  /* TRUE=NOT_IN_SYNC, FALSE=IN_SYNC */
} WatcherIdleData;

static gboolean watcher_idle_cb(gpointer user_data)
{
    WatcherIdleData *d = user_data;
    SondSeadriveWatcher *w = d->watcher;

    //nach dem Stoppen betrifft das Ereignis ein Projekt, das nicht mehr gezeigt wird
    if (!watcher_stop_requested(w)) {
        update_status(w, d->path_pending_down, d->delta_down, d->path_up,
                d->up_pending);

        if (d->path_pending_down)
            update_file_badge(w, d->path_pending_down, d->coverage_badge,
                    d->delta_total);

        if (d->path_down)
            g_signal_emit(w, signals[SIGNAL_HYDRATED], 0, d->path_down);
        if (d->path_dehydrated)
            g_signal_emit(w, signals[SIGNAL_DEHYDRATED], 0, d->path_dehydrated);

        g_signal_emit(w, signals[SIGNAL_CHANGED], 0);
    }

    g_free(d->path_pending_down);
    g_free(d->path_down);
    g_free(d->path_dehydrated);
    g_free(d->path_up);
    g_object_unref(w);
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
 * Statistik, s. update_file_badge()) UND für JEDES
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
        SondSeadriveWatcher *w, GHashTable *out_paths,
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
        if (watcher_stop_requested(w))
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
            watcher_count_pending_down(sub, w, out_paths,
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
             * sond_tvfm_status.c. */
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
    SondSeadriveWatcher *watcher; /* Referenz */
    GHashTable     *paths;        /* transfer full */
    GHashTable     *file_badges;  /* transfer full */
    GHashTable     *dir_counts;   /* transfer full */
} WatcherRescanData;

static gboolean watcher_rescan_idle_cb(gpointer user_data)
{
    WatcherRescanData *d = user_data;
    SondSeadriveWatcher *w = d->watcher;

    if (watcher_stop_requested(w)) {
        g_hash_table_destroy(d->paths);
        g_hash_table_destroy(d->file_badges);
        g_hash_table_destroy(d->dir_counts);
    } else {
        /* Ersetzt die Tabellen vollständig. Beim Initialscan sind sie noch leer;
         * bei einem Resync (Pufferüberlauf) wird der alte, womöglich falsche
         * Stand verworfen - Ereignisse, die während des Scans gepuffert wurden,
         * kommen danach und korrigieren ggf. noch einmal nach. */
        if (w->pending_down_paths)
            g_hash_table_destroy(w->pending_down_paths);
        w->pending_down_paths = d->paths;
        w->pending_down = g_hash_table_size(d->paths);

        if (w->file_badges)
            g_hash_table_destroy(w->file_badges);
        w->file_badges = d->file_badges;

        if (w->dir_counts)
            g_hash_table_destroy(w->dir_counts);
        w->dir_counts = d->dir_counts;

        emit_status(w);
        g_signal_emit(w, signals[SIGNAL_CHANGED], 0);
    }

    g_object_unref(w);
    g_free(d);

    return G_SOURCE_REMOVE;
}

/* Scannt root komplett und ersetzt danach (Hauptthread) die Tabellen. Gemeinsam
 * von Initialscan und Resync nach Pufferüberlauf genutzt. */
static void watcher_rescan(SondSeadriveWatcher *w, const gchar *root)
{
    GHashTable *paths = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GHashTable *file_badges = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
            NULL);
    GHashTable *dir_counts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
            g_free);
    WatcherRescanData *d = NULL;

    watcher_count_pending_down(root, w, paths, file_badges, dir_counts, NULL, NULL,
            NULL);

    d = g_new0(WatcherRescanData, 1);
    d->watcher = g_object_ref(w);
    d->paths = paths;
    d->file_badges = file_badges;
    d->dir_counts = dir_counts;
    g_idle_add(watcher_rescan_idle_cb, d);
}

/* ------------------------------------------------------------------ */
/*  Watcher: Hilfsfunktionen für Thread                               */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/*  Watcher-Thread                                                      */
/* ------------------------------------------------------------------ */

static gpointer watcher_thread(gpointer user_data)
{
    SondSeadriveWatcher *w = SOND_SEADRIVE_WATCHER(user_data);

    /* Eigene Kopie der Wurzel: der Thread liest nur sie, nicht das Objekt. */
    gchar *root = g_strdup(w->root);
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
     * dadurch mit der Zeit auseinander. */
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
    if (!watcher_stop_requested(w))
        watcher_rescan(w, root);

    while (!watcher_stop_requested(w)) {

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
                 * (unproblematisch: ~1,5s auch bei ~70.000 Dateien). */
                LOG_WARN("SeaDrive-Watcher('%s'): ReadDirectoryChangesW-Puffer "
                        "übergelaufen - Events verloren, erzwinge Resync",
                        root);
                watcher_rescan(w, root);
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
                             * mitgezählt. delta_total=-1 zieht die Datei aus
                             * der Ordner-Coverage-Gesamtzahl ab (auch ein
                             * No-Op über die 0-Kappung, falls es ein
                             * Verzeichnis war - dessen eigener Eintrag in
                             * seadrive_dir_counts bleibt dann zwar stehen,
                             * wird aber beim nächsten Rescan bereinigt). */
                            WatcherIdleData *d = g_new0(WatcherIdleData, 1);
                            d->watcher = g_object_ref(w);
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
                                    d->watcher = g_object_ref(w);
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
                                        gboolean in_sync = sond_seadrive_file_in_sync(full);
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
/*  Start und Ende                                                     */
/* ------------------------------------------------------------------ */

void sond_seadrive_watcher_start(SondSeadriveWatcher *w)
{
    if (!w || w->thread)
        return;

    g_atomic_int_set(&w->stop, 0);
    w->thread = g_thread_new("seadrive-watcher", watcher_thread, w);
}

void sond_seadrive_watcher_stop(SondSeadriveWatcher *w)
{
    if (!w || !w->thread)
        return;

    g_atomic_int_set(&w->stop, 1);
    g_thread_join(w->thread);
    w->thread = NULL;
}

typedef struct {
    GThread *thread;
    SondSeadriveWatcher *watcher; /* Referenz */
} Reaper;

static gpointer watcher_reap(gpointer data)
{
    Reaper *r = data;

    g_thread_join(r->thread);
    g_object_unref(r->watcher); //zerstört bei letzter Referenz auch die Tabellen
    g_free(r);

    return NULL;
}

/* Wie sond_seadrive_watcher_stop(), wartet aber nicht auf das Thread-Ende. Das
 * synchrone g_thread_join() würde den Hauptthread beim Schließen des Projekts
 * blockieren, bis der Watcher-Thread sein CloseHandle() auf das
 * ReadDirectoryChangesW-Verzeichnis-Handle abgeschlossen hat: SeaDrives
 * Cloud-Filtertreiber braucht dafür regelmäßig um die 20 Sekunden, weil die
 * ausstehende Benachrichtigung per CancelIo() nur angestoßen, nicht sofort
 * abgeschlossen wird.
 * Der Thread fasst nach dem Stop-Flag nur noch seine eigenen Handles an, das
 * Warten geht daher gefahrlos in einen kurzlebigen Reaper-Thread, der auch die
 * Referenz auf das Objekt (und damit dessen große Tabellen) hält. */
void sond_seadrive_watcher_stop_async(SondSeadriveWatcher *w)
{
    Reaper *r = NULL;

    if (!w || !w->thread)
        return;

    g_atomic_int_set(&w->stop, 1);

    r = g_new0(Reaper, 1);
    r->thread = w->thread;
    r->watcher = g_object_ref(w);
    w->thread = NULL;

    g_thread_unref(g_thread_new("seadrive-watcher-reaper", watcher_reap, r));
}

#endif /* _WIN32 */
