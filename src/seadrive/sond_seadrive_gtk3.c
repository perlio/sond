/*
 sond (sond_seadrive.c) - Akten, Beweisstücke, Unterlagen
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

#include "sond_seadrive_gtk3.h"

#ifdef _WIN32

#include <windows.h>
#include <glib.h>
#include <gtk/gtk.h>

#include "sond_seadrive_core.h"
#include "sond_seadrive_win.h"
#include "../gtk3/sond_treeview.h"
#include "../gtk3/sond_treeviewfm_private.h"
#include "../sond_fileparts.h"
#include "../sond_file_helper.h"
#include "../sond_log_and_error.h"
#include "../sond_mime.h"
#include "../misc.h"

//CfGetPlaceholderInfo (geladen in sond_seadrive_core.c)
typedef HRESULT (WINAPI *PFN_CfGetPlaceholderInfo)(
    HANDLE  FileHandle,
    DWORD   InfoClass,
    PVOID   InfoBuffer,
    DWORD   InfoBufferLength,
    PDWORD  ReturnedLength);

static PFN_CfGetPlaceholderInfo cf_get_placeholder_info(void)
{
    return (PFN_CfGetPlaceholderInfo) sond_seadrive_core_placeholder_info();
}

void sond_seadrive_pin_report_failures(GtkWidget *parent, GPtrArray *failed)
{
    GString *text = NULL;

    if (!failed || failed->len == 0)
        return;

    text = g_string_new(NULL);
    if (failed->len == 1)
        g_string_append(text, "1 Eintrag konnte nicht geändert werden:\n");
    else
        g_string_append_printf(text,
                "%u Einträge konnten nicht geändert werden:\n", failed->len);
    for (guint i = 0; i < MIN(failed->len, 5); i++)
        g_string_append_printf(text, "\n%s",
                (const gchar*) g_ptr_array_index(failed, i));
    if (failed->len > 5)
        g_string_append_printf(text, "\n... und %u weitere", failed->len - 5);

    display_message(parent, "SeaDrive\n\n", text->str, NULL);
    g_string_free(text, TRUE);
}

/* ------------------------------------------------------------------ */
/*  Public API: sond_seadrive_hydrate_async                            */
/* ------------------------------------------------------------------ */

/* Menge der Pfade, für die aktuell ein Hydrier-Hintergrund-Thread läuft -
 * verhindert, dass ein erneuter Doppelklick auf dieselbe, noch
 * herunterladende Datei einen zweiten, redundanten Thread/
 * CfHydratePlaceholder()-Aufruf auslöst. Bei sehr großen Dateien (z.B.
 * 51 GB) würde ein SYNCHRONER Aufruf von sond_seadrive_hydrate() im
 * GTK-Hauptthread das gesamte Programm minutenlang ohne Rückmeldung oder
 * Abbrechen-Möglichkeit blockieren. Deshalb: kein Info-Fenster beim ERSTEN
 * Doppelklick (der Download läuft ohnehin im Hintergrund weiter,
 * unabhängig davon, ob die UI darauf wartet) - stattdessen sofort in die UI
 * zurückkehren (Fire-and-forget).
 *
 * Aktionen, die Dateien lesen, laden vorab über sond_seadrive_load_sync()
 * (weiter unten): modales Fortschrittsfenster mit Abbrechen, danach läuft die
 * Aktion automatisch weiter.
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
    GWeakRef parent;   /* Fenster für die Fehlermeldung, kann leer sein */
    gboolean notify;
} HydrateThreadData;

typedef struct {
    gchar *full_path;
    gchar *message;
    GWeakRef parent;
} HydrateReport;

static gboolean hydrate_report_idle(gpointer data)
{
    HydrateReport *report = (HydrateReport *) data;
    GtkWidget *parent = g_weak_ref_get(&report->parent);

    if (parent) {
        gchar *base = g_path_get_basename(report->full_path);
        gchar *text = g_strdup_printf("%s - Download gescheitert", base);

        display_message(parent, text, NULL);
        g_free(text);
        g_free(base);
        g_object_unref(parent);
    }

    g_weak_ref_clear(&report->parent);
    g_free(report->full_path);
    g_free(report->message);
    g_free(report);

    return G_SOURCE_REMOVE;
}

//Fehlschlag im Hintergrundthread an den Hauptthread melden (Dialog)
static void hydrate_report_error(HydrateThreadData *td, gchar const *message)
{
    HydrateReport *report = g_new0(HydrateReport, 1);
    gpointer parent = g_weak_ref_get(&td->parent);

    report->full_path = g_strdup(td->full_path);
    report->message = g_strdup(message ? message : "unbekannter Fehler");
    g_weak_ref_init(&report->parent, parent);
    if (parent)
        g_object_unref(parent);

    g_idle_add(hydrate_report_idle, report);
}

static void hydrate_run(HydrateThreadData *td)
{
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

        /* vom Nutzer abgebrochen? Dann keine Fehlermeldung */
        g_mutex_lock(&g_hydrating_mutex);
        entry = g_hash_table_lookup(g_hydrating_paths, td->full_path);
        pre_cancelled = entry && entry->cancel_requested;
        g_mutex_unlock(&g_hydrating_mutex);

        if (td->notify && !pre_cancelled)
            hydrate_report_error(td, error ? error->message : NULL);
        g_clear_error(&error);
    }

    g_mutex_lock(&g_hydrating_mutex);
    g_hash_table_remove(g_hydrating_paths, td->full_path); /* schließt
            thread_handle_dup via hydrating_entry_free(), falls gesetzt */
    g_mutex_unlock(&g_hydrating_mutex);

    g_weak_ref_clear(&td->parent);
    g_free(td->full_path);
    g_free(td);
}

static gpointer hydrate_thread_func(gpointer data)
{
    hydrate_run((HydrateThreadData *) data);

    return NULL;
}

/* Bei vielen Dateien (Kopieren, Auszug) nicht ein Thread je Datei, sondern ein
 * Pool mit wenigen Arbeitern: SeaDrive lädt ohnehin nicht beliebig parallel,
 * und Hunderte gleichzeitige CfHydratePlaceholder()-Aufrufe belasten Rechner
 * und Server unnötig. Die Dateien gelten ab dem Einreihen als "wird geladen"
 * (g_hydrating_paths), Fortschritt und Abbrechen funktionieren unverändert. */
#define HYDRATE_POOL_THREADS   4
#define HYDRATE_POOL_MIN_FILES 8

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

/*
 * sond_seadrive_hydrate_async:
 *
 * Wie sond_seadrive_hydrate(), aber nicht-blockierend: startet die
 * eigentliche Hydrierung in einem eigenen Hintergrund-Thread und kehrt
 * sofort zurück (Fire-and-forget, s. Doc-Kommentar oben). Ein
 * erneuter Aufruf für denselben full_path, während bereits ein Thread
 * dafür läuft, ist ein No-Op (Aufrufer sollte in diesem Fall stattdessen
 * den Fortschrittsdialog zeigen, s. sond_seadrive_load_sync()). Fehler landen nur im Log (LOG_WARN in
 * hydrate_thread_func()) - ein synchroner Rückgabewert wäre ohnehin
 * nicht sinnvoll nutzbar, da der eigentliche Download beim Rücksprung
 * i.d.R. noch läuft.
 */
/* Reiht die Hydrierung von full_path ein. TRUE, wenn sie neu eingereiht wurde,
 * FALSE, wenn schon eine läuft oder das Einreihen scheiterte. Mit parent wird
 * ein Fehlschlag in einem Dialog gemeldet; use_pool: Pool statt eigenem
 * Thread. */
static gboolean hydrate_start(const gchar *full_path, GtkWindow *parent,
        gboolean use_pool)
{
    HydrateThreadData *td = NULL;

    g_mutex_lock(&g_hydrating_mutex);
    if (!g_hydrating_paths)
        g_hydrating_paths = g_hash_table_new_full(g_str_hash, g_str_equal,
                g_free, hydrating_entry_free);

    if (g_hash_table_contains(g_hydrating_paths, full_path)) {
        /* schon ein Hydrier-Thread für diese Datei unterwegs - No-Op */
        g_mutex_unlock(&g_hydrating_mutex);
        return FALSE;
    }
    g_hash_table_insert(g_hydrating_paths, g_strdup(full_path),
            g_new0(HydratingEntry, 1));
    g_mutex_unlock(&g_hydrating_mutex);

    td = g_new0(HydrateThreadData, 1);
    td->full_path = g_strdup(full_path);
    td->notify = (parent != NULL);
    g_weak_ref_init(&td->parent, parent);

    if (use_pool) {
        g_thread_pool_push(hydrate_pool_get(), td, NULL);
        return TRUE;
    }

    {
        GThread *thread = g_thread_new("seadrive-hydrate", hydrate_thread_func,
                td);

        if (!thread) {
            /* Thread-Erzeugung fehlgeschlagen - Eintrag wieder entfernen,
             * sonst bliebe die Datei für immer fälschlich als "läuft schon"
             * markiert. */
            g_mutex_lock(&g_hydrating_mutex);
            g_hash_table_remove(g_hydrating_paths, td->full_path);
            g_mutex_unlock(&g_hydrating_mutex);
            g_weak_ref_clear(&td->parent);
            g_free(td->full_path);
            g_free(td);
            return FALSE;
        }
        g_thread_unref(thread); /* fire-and-forget, kein g_thread_join() */
    }

    return TRUE;
}

void sond_seadrive_hydrate_async_notify(const gchar *full_path,
        GtkWindow *parent)
{
    hydrate_start(full_path, parent, FALSE);
}

void sond_seadrive_hydrate_async(const gchar *full_path)
{
    sond_seadrive_hydrate_async_notify(full_path, NULL);
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
        //immer merken: abgebrochene Dateien gelten nicht als gescheitert
        entry->cancel_requested = TRUE;
        if (entry->thread_handle)
            CancelSynchronousIo(entry->thread_handle);
    }
    g_mutex_unlock(&g_hydrating_mutex);
}

/* ------------------------------------------------------------------ */
/*  Public API: sond_seadrive_show_hydrate_progress_dialog              */
/* ------------------------------------------------------------------ */

/* CF_PLACEHOLDER_INFO_STANDARD liefert u.a. OnDiskDataSize (bereits lokal
 * vorhandene Bytes) - Struktur-Layout 1:1 aus der offiziellen cfapi.h
 * übernommen, hier wie beim Rest der Datei manuell dupliziert (kein cfapi.h
 * im verwendeten MinGW-Toolchain, s. sond_seadrive_core.c).
 * PinState/InSyncState hier bewusst als DWORD (nicht als CF_PIN_STATE/
 * CF_IN_SYNC_STATE-Enum) deklariert, analog SeaDrivePlaceholderBasicInfo
 * oben - nur OnDiskDataSize wird tatsächlich ausgewertet.
 *
 * Das offizielle cfapi.h deklariert FileIdentity[] als BYTE[1] - ein reiner
 * Platzhalter für einen tatsächlich variabel langen Puffer, der vom
 * Aufrufer selbst groß genug angelegt werden muss (s. auch
 * SeaDrivePlaceholderBasicInfo oben, ebenfalls mit 256 Byte statt [1]).
 * Mit nur 1 Byte Puffer liefert CfGetPlaceholderInfo() bei einer nicht
 * winzigen Identity (bei SeaDrive offenbar der Normalfall)
 * HRESULT_MORE_DATA - technisch ein FAILURE-HRESULT (Severity-Bit gesetzt
 * trotz des Namens), SUCCEEDED() schlüge also fehl und
 * hydrate_progress_update() bräche VOR dem Auswerten von OnDiskDataSize ab,
 * ohne die Progress-Bar je zu aktualisieren (Anzeige bliebe bei 0%). */
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

/* ------------------------------------------------------------------ */
/*  Laden nicht geladener Dateien vor einer Aktion                     */
/* ------------------------------------------------------------------ */

/* Laden nicht geladener Dateien vor einer Aktion (Öffnen, Kopieren, Auszug,
 * Export, Indizieren ...): ensure_loaded() stößt die Downloads
 * an und zeigt EIN modales Fenster mit Fortschritt (Dateien und Volumen in
 * der Titelzeile) und "Abbrechen". Es schließt sich selbst, sobald alle
 * Downloads beendet sind; waren alle erfolgreich, wird die Fertig-Funktion
 * aufgerufen (sie führt die Aktion erneut aus, dann ist alles lokal). Bei
 * Abbruch geschieht nichts weiter, scheiterten Downloads, werden sie gesammelt
 * gemeldet. Bereits geladene Dateien bleiben geladen. Das Fenster zu
 * schließen (X, Esc) wirkt wie "Abbrechen". */

//Fortsetzung nach erfolgreichem Laden (Hauptthread)
typedef void (*SondSeadriveLoadedFunc)(gpointer user_data);

typedef struct {
    gchar     *full_path;
    GtkWidget *label;
    GtkWidget *progress_bar;
    guint64    file_size;
    guint64    on_disk;        /* zuletzt ermittelte, lokal vorhandene Bytes */
    gboolean   logged_failure;
    gboolean   done;           /* Hydrierung beendet (Erfolg oder nicht) */
    gboolean   ok;             /* Datei ist danach vollständig lokal */
} LoadEntry;

/* Ab so vielen Dateien statt einer Zeile je Datei nur eine Sammelanzeige:
 * je Datei zwei Widgets und je Aktualisierung ein Dateizugriff wären bei
 * Hunderten Dateien zu langsam. */
#define HYDRATE_PROGRESS_AGGREGATE_MIN 20

/* Sammelanzeige: je Aktualisierung höchstens so viele laufende Dateien nach
 * dem Fortschritt fragen (der Pool lädt ohnehin nur wenige gleichzeitig; die
 * Reihenfolge der Einträge entspricht der des Einreihens). */
#define HYDRATE_PROGRESS_QUERY_MAX 8

typedef struct {
    GPtrArray *entries; /* LoadEntry*, eigene Kopien */
    GtkWidget *dialog;
    GtkWidget *abbrechen_button;
    guint      timeout_id;
    gboolean   aggregate;    /* Sammelanzeige: entries ohne Widgets */
    gboolean   cancelled;
    GtkWidget *agg_label;
    GtkWidget *agg_bar;
    guint64    total_bytes;
    GWeakRef   parent;
    SondSeadriveLoadedFunc done_func;
    gpointer   user_data;
    GDestroyNotify destroy;
} LoadJob;

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

/* Ermittelt per CfGetPlaceholderInfo() die lokal vorhandenen Bytes eines
 * Eintrags und aktualisiert dessen Fortschrittsbalken (falls vorhanden). */
static void load_entry_query(LoadEntry *entry)
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

    if (cf_get_placeholder_info()) {
        SeaDrivePlaceholderStandardInfo info = { 0 };
        DWORD returned_length = 0;
        HRESULT hr = cf_get_placeholder_info()(h, CF_PLACEHOLDER_INFO_STANDARD,
                &info, sizeof(info), &returned_length);

        if (SUCCEEDED(hr)) {
            entry->on_disk = (guint64) info.OnDiskDataSize.QuadPart;

            if (entry->progress_bar) {
                gdouble fraction = 0.0;
                gchar *on_disk_str, *total_str, *text;

                if (entry->file_size > 0) {
                    fraction = (gdouble) entry->on_disk
                            / (gdouble) entry->file_size;
                    if (fraction > 1.0)
                        fraction = 1.0;
                }
                gtk_progress_bar_set_fraction(
                        GTK_PROGRESS_BAR(entry->progress_bar), fraction);

                on_disk_str = g_format_size(entry->on_disk);
                total_str = g_format_size(entry->file_size);
                text = g_strdup_printf("%s von %s", on_disk_str, total_str);
                gtk_progress_bar_set_text(
                        GTK_PROGRESS_BAR(entry->progress_bar), text);
                g_free(on_disk_str);
                g_free(total_str);
                g_free(text);
            }
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

static void load_entry_free(gpointer data)
{
    LoadEntry *entry = (LoadEntry *) data;
    g_free(entry->full_path);
    g_free(entry);
}

/* Aktualisiert Einträge, Balken und Titelzeile - OHNE den Dialog zu
 * schließen (das übernimmt load_job_tick()). Getrennt gehalten, damit der
 * initiale, synchrone Aufruf nach dem Aufbau des Dialogs das gerade erst
 * erzeugte job nicht bei sofort beendeter Hydrierung freigibt, bevor der
 * Aufrufer fertig damit ist. */
static void load_job_update(LoadJob *job)
{
    guint i;
    guint done = 0;
    guint queried = 0;
    guint64 bytes = 0;
    gchar *bytes_str = NULL;
    gchar *total_str = NULL;
    gchar *title = NULL;

    for (i = 0; i < job->entries->len; i++) {
        LoadEntry *entry = g_ptr_array_index(job->entries, i);

        if (!entry->done && !sond_seadrive_is_hydrating(entry->full_path)) {
            entry->done = TRUE;
            entry->ok = !sond_seadrive_needs_hydration(entry->full_path);
            if (entry->ok)
                entry->on_disk = entry->file_size;
            if (entry->progress_bar) {
                if (entry->ok) {
                    gtk_progress_bar_set_fraction(
                            GTK_PROGRESS_BAR(entry->progress_bar), 1.0);
                    gtk_progress_bar_set_text(
                            GTK_PROGRESS_BAR(entry->progress_bar), "fertig");
                } else
                    gtk_progress_bar_set_text(
                            GTK_PROGRESS_BAR(entry->progress_bar),
                            "nicht geladen");
            }
        }

        if (!entry->done && (!job->aggregate
                || queried < HYDRATE_PROGRESS_QUERY_MAX)) {
            load_entry_query(entry);
            queried++;
        }

        if (entry->done)
            done++;
        bytes += entry->on_disk;
    }

    if (bytes > job->total_bytes)
        bytes = job->total_bytes;
    bytes_str = g_format_size(bytes);
    total_str = g_format_size(job->total_bytes);

    if (job->entries->len == 1)
        title = g_strdup_printf("Download läuft – %s von %s", bytes_str,
                total_str);
    else
        title = g_strdup_printf("Download läuft – %s von %s (%u von %u Dateien)",
                bytes_str, total_str, done, job->entries->len);
    gtk_window_set_title(GTK_WINDOW(job->dialog), title);
    g_free(title);
    g_free(bytes_str);
    g_free(total_str);

    if (job->aggregate) {
        gchar *text = g_strdup_printf("%u von %u Dateien", done,
                job->entries->len);
        gdouble fraction = job->total_bytes ?
                (gdouble) bytes / (gdouble) job->total_bytes :
                (job->entries->len ? (gdouble) done / job->entries->len : 1.0);

        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(job->agg_bar), fraction);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(job->agg_bar), text);
        g_free(text);
    }
}

static gboolean load_job_all_done(LoadJob *job)
{
    guint i;

    for (i = 0; i < job->entries->len; i++) {
        LoadEntry *entry = g_ptr_array_index(job->entries, i);
        if (!entry->done)
            return FALSE;
    }
    return TRUE;
}

//Alle beendet: Fenster schließen, Fehler melden bzw. Aktion fortsetzen
static void load_job_finish(LoadJob *job)
{
    GPtrArray *failed = g_ptr_array_new_with_free_func(g_free);
    GtkWidget *parent = NULL;
    guint i;

    for (i = 0; i < job->entries->len; i++) {
        LoadEntry *entry = g_ptr_array_index(job->entries, i);

        if (!entry->ok)
            g_ptr_array_add(failed, g_path_get_basename(entry->full_path));
    }

    gtk_widget_destroy(job->dialog);

    parent = g_weak_ref_get(&job->parent);
    if (!job->cancelled && failed->len > 0 && parent) {
        GString *text = g_string_new(NULL);

        if (failed->len == 1)
            g_string_append_printf(text, "%s - Download gescheitert",
                    (const gchar *) g_ptr_array_index(failed, 0));
        else {
            g_string_append_printf(text,
                    "%u Dateien - Download gescheitert:\n", failed->len);
            for (i = 0; i < MIN(failed->len, 5); i++)
                g_string_append_printf(text, "\n%s",
                        (const gchar *) g_ptr_array_index(failed, i));
            if (failed->len > 5)
                g_string_append_printf(text, "\n... und %u weitere",
                        failed->len - 5);
        }
        display_message(parent, text->str, NULL);
        g_string_free(text, TRUE);
    }
    if (parent)
        g_object_unref(parent);

    if (!job->cancelled && failed->len == 0 && job->done_func)
        job->done_func(job->user_data);
    if (job->destroy)
        job->destroy(job->user_data);

    g_ptr_array_unref(failed);
    g_ptr_array_free(job->entries, TRUE);
    g_weak_ref_clear(&job->parent);
    g_free(job);
}

static gboolean load_job_tick(gpointer data)
{
    LoadJob *job = (LoadJob *) data;

    load_job_update(job);

    if (load_job_all_done(job)) {
        job->timeout_id = 0;
        load_job_finish(job);
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

static void load_job_cancel(LoadJob *job)
{
    guint i;

    if (job->cancelled)
        return;
    job->cancelled = TRUE;

    for (i = 0; i < job->entries->len; i++) {
        LoadEntry *entry = g_ptr_array_index(job->entries, i);

        if (!entry->done) {
            sond_seadrive_hydrate_cancel(entry->full_path);
            if (entry->label)
                gtk_label_set_text(GTK_LABEL(entry->label),
                        "Wird abgebrochen...");
        }
    }
    if (job->agg_label)
        gtk_label_set_text(GTK_LABEL(job->agg_label), "Wird abgebrochen...");
    gtk_widget_set_sensitive(job->abbrechen_button, FALSE);
}

//Abbrechen-Knopf, X und Esc
static void cb_load_job_response(GtkDialog *dialog, gint response,
        gpointer data)
{
    (void) dialog;
    (void) response;
    load_job_cancel((LoadJob *) data);
}

/*
 * ensure_loaded:
 * Sind alle full_paths lokal, wird TRUE geliefert (Aufrufer macht weiter,
 * done_func/destroy werden nicht aufgerufen, der Aufrufer behält user_data).
 * Sonst werden die Downloads der fehlenden Dateien angestoßen (ab
 * HYDRATE_POOL_MIN_FILES über den Pool), das Fortschrittsfenster gezeigt und
 * FALSE geliefert; der Aufrufer kehrt zurück. Nach erfolgreichem Laden ruft
 * der Hauptthread done_func(user_data) auf; destroy(user_data) folgt in jedem
 * Fall (auch bei Abbruch und Fehlern). full_paths wird nur gelesen.
 */
static gboolean ensure_loaded(GtkWindow *parent, GPtrArray *full_paths,
        SondSeadriveLoadedFunc done_func, gpointer user_data,
        GDestroyNotify destroy)
{
    GPtrArray *pending;
    LoadJob *job;
    GtkWidget *content_area;
    GtkWidget *scrolled_window;
    GtkWidget *vbox;
    guint i;

    if (!full_paths || full_paths->len == 0)
        return TRUE;

    pending = g_ptr_array_new();
    for (i = 0; i < full_paths->len; i++) {
        const gchar *path = g_ptr_array_index(full_paths, i);

        if (path && sond_seadrive_needs_hydration(path))
            g_ptr_array_add(pending, (gpointer) path);
    }

    if (pending->len == 0) {
        g_ptr_array_free(pending, TRUE);

        return TRUE;
    }

    {
        gboolean use_pool = (pending->len >= HYDRATE_POOL_MIN_FILES);

        for (i = 0; i < pending->len; i++)
            hydrate_start((const gchar *) g_ptr_array_index(pending, i), NULL,
                    use_pool);
    }

    job = g_new0(LoadJob, 1);
    job->entries = g_ptr_array_new_with_free_func(load_entry_free);
    g_weak_ref_init(&job->parent, parent);
    job->done_func = done_func;
    job->user_data = user_data;
    job->destroy = destroy;

    job->dialog = gtk_dialog_new_with_buttons("Download läuft", parent,
            GTK_DIALOG_MODAL, NULL, NULL);
    gtk_window_set_default_size(GTK_WINDOW(job->dialog), 460, -1);

    /* Bei vielen Dateien sollen trotzdem alle Fortschrittszeilen einsehbar
     * bleiben, ohne dass der Dialog über den Bildschirm hinaus wächst: Liste
     * in einem GtkScrolledWindow mit fester Maximalhöhe. */
    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
            GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(
            GTK_SCROLLED_WINDOW(scrolled_window), 60);
    gtk_scrolled_window_set_max_content_height(
            GTK_SCROLLED_WINDOW(scrolled_window), 320);
    gtk_scrolled_window_set_propagate_natural_height(
            GTK_SCROLLED_WINDOW(scrolled_window), TRUE);

    content_area = gtk_dialog_get_content_area(GTK_DIALOG(job->dialog));
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);

    job->aggregate = (pending->len > HYDRATE_PROGRESS_AGGREGATE_MIN);
    if (job->aggregate) {
        gchar *text = g_strdup_printf("%u Dateien werden geladen",
                pending->len);

        job->agg_label = gtk_label_new(text);
        g_free(text);
        gtk_widget_set_halign(job->agg_label, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(vbox), job->agg_label, FALSE, FALSE, 0);

        job->agg_bar = gtk_progress_bar_new();
        gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(job->agg_bar), TRUE);
        gtk_box_pack_start(GTK_BOX(vbox), job->agg_bar, FALSE, FALSE, 0);
    }

    for (i = 0; i < pending->len; i++) {
        const gchar *full_path = g_ptr_array_index(pending, i);
        LoadEntry *entry = g_new0(LoadEntry, 1);

        entry->full_path = g_strdup(full_path);
        entry->file_size = load_file_size(full_path);
        job->total_bytes += entry->file_size;

        if (!job->aggregate) {
            gchar *basename = g_path_get_basename(full_path);

            entry->label = gtk_label_new(basename);
            gtk_label_set_line_wrap(GTK_LABEL(entry->label), TRUE);
            gtk_widget_set_halign(entry->label, GTK_ALIGN_START);
            g_free(basename);
            gtk_box_pack_start(GTK_BOX(vbox), entry->label, FALSE, FALSE, 0);

            entry->progress_bar = gtk_progress_bar_new();
            gtk_progress_bar_set_show_text(
                    GTK_PROGRESS_BAR(entry->progress_bar), TRUE);
            gtk_box_pack_start(GTK_BOX(vbox), entry->progress_bar, FALSE,
                    FALSE, 0);
        }

        g_ptr_array_add(job->entries, entry);
    }

    gtk_container_add(GTK_CONTAINER(scrolled_window), vbox);
    gtk_container_add(GTK_CONTAINER(content_area), scrolled_window);

    job->abbrechen_button = gtk_dialog_add_button(GTK_DIALOG(job->dialog),
            "Abbrechen", GTK_RESPONSE_CANCEL);
    g_signal_connect(job->dialog, "response",
            G_CALLBACK(cb_load_job_response), job);

    gtk_widget_show_all(job->dialog);

    load_job_update(job); /* initiale Anzeige, kein Auto-Destroy */
    job->timeout_id = g_timeout_add(300, load_job_tick, job);

    g_ptr_array_free(pending, TRUE);

    return FALSE;
}

typedef struct {
    GMainLoop *loop;
    gboolean   ok;
} LoadSyncData;

static void load_sync_done(gpointer data)
{
    ((LoadSyncData *) data)->ok = TRUE;
}

static void load_sync_destroy(gpointer data)
{
    g_main_loop_quit(((LoadSyncData *) data)->loop);
}

/*
 * sond_seadrive_load_sync:
 * Wie ensure_loaded(), wartet aber (verschachtelte Hauptschleife, das modale
 * Fenster bleibt bedienbar) bis alle Dateien geladen sind. TRUE: alle lokal,
 * der Aufrufer macht weiter. FALSE: der Nutzer hat abgebrochen oder Downloads
 * sind gescheitert (schon gemeldet), der Aufrufer bricht ab. Für Abläufe mit
 * Zwischenschritten (Dialoge, Dateiauswahl), die sich nicht einfach erneut
 * starten lassen.
 */
gboolean sond_seadrive_load_sync(GtkWindow *parent, GPtrArray *full_paths)
{
    LoadSyncData sd = { NULL, FALSE };

    if (ensure_loaded(parent, full_paths, load_sync_done, &sd,
            load_sync_destroy))
        return TRUE;

    sd.loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(sd.loop);
    g_main_loop_unref(sd.loop);

    return sd.ok;
}

gboolean sond_seadrive_load_file_sync(GtkWindow *parent, const gchar *full_path)
{
    GPtrArray *arr;
    gboolean ok;

    if (!full_path)
        return TRUE;

    arr = g_ptr_array_new();
    g_ptr_array_add(arr, (gpointer) full_path);
    ok = sond_seadrive_load_sync(parent, arr);
    g_ptr_array_free(arr, TRUE);

    return ok;
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
     * Pin-/Hydrierungsstatus gehören zur realen Datei als Ganzem, nicht zum
     * einzelnen Teil: für einen Teil mit Parent (Mime-Part/ZIP-Eintrag/
     * PDF-Seite) ergäbe der eigene sfp-Pfad einen nicht-existenten Pfad, und
     * sond_seadrive_set_pin_state() schlüge wirkungslos fehl. Deshalb wie
     * beim SeaDrive-Badge (render_file_icon()) zum obersten Vorfahren
     * hochlaufen und dessen Pfad verwenden. */
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

static void apply_pin_state_to_item(SondTVFMItem *stvfm_item, guint pin_state,
        GPtrArray *failed)
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
        sond_seadrive_pin_note_failure(failed, full_path, error);
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

typedef struct {
    guint      pin_state;
    GPtrArray *failed; /* Meldungen gescheiterter Einträge (g_free) */
} PinForeachData;

static gint seadrive_pin_foreach(SondTreeview *stv, GtkTreeIter *iter,
                                 gpointer data, GError **error)
{
    SondTVFMItem *item = NULL;
    PinForeachData *pd = (PinForeachData*) data;
    (void)error;
    gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(stv)),
                       iter, 0, &item, -1);
    if (item) {
        apply_pin_state_to_item(item, pd->pin_state, pd->failed);
        g_object_unref(item);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Public: apply to root directory ("Gesamtes Projekt")               */
/* ------------------------------------------------------------------ */

/* Öffentlich, da nur vom Hauptmenü (win.sd-*-all, headerbar.c) aus
 * erreichbar - die Aktion betrifft immer die Projekt-Wurzel unabhängig von
 * Selektion/Rechtsklick-Ziel und gehört damit nicht in ein Kontextmenü,
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
 * Öffentlich, da auch vom globalen Hauptmenü aus genutzt ("Projekt >
 * SeaDrive > .../Auswahl", win.sd-*-sel in headerbar.c), wenn BAUM_FS
 * der aktuelle Baum ist (s. zond_baum_aktuell(), app_window.c). */
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

    PinForeachData pd = { pin_state, g_ptr_array_new_with_free_func(g_free) };

    gint rc = sond_treeview_selection_foreach(SOND_TREEVIEW(stvfm),
                  seadrive_pin_foreach, &pd, &error);
    if (rc == -1) {
        display_message(gtk_widget_get_toplevel(GTK_WIDGET(stvfm)),
                        "SeaDrive: Fehler\n\n",
                        error ? error->message : "", NULL);
        g_clear_error(&error);
    }

    //gescheiterte Einträge gesammelt melden
    sond_seadrive_pin_report_failures(gtk_widget_get_toplevel(GTK_WIDGET(stvfm)),
            pd.failed);
    g_ptr_array_unref(pd.failed);

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

    /* Im Kontextmenü gibt es nur "Auswahl" - "Gesamtes Projekt"
     * (sond_treeviewfm_seadrive_pin_root()) ist nur über das Hauptmenü
     * erreichbar, s. sond_treeviewfm_seadrive_init_contextmenu(). */
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

    /* Die "-all"-Varianten (wirken auf die Projekt-Wurzel, unabhängig von
     * Selektion/Rechtsklick-Ziel) gibt es nur im Hauptmenü (win.sd-*-all,
     * headerbar.c) - hier im Kontextmenü bewusst nur "Auswahl", s.
     * sond_treeviewfm.c (add_base_menu) und sond_seadrive.h. */
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
 * seadrive_path()) - ohne SeaDrive-Projekt sollen die Menüpunkte nicht
 * anwählbar sein statt wirkungslos. */
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
	 * PDF als sfp, kein path_or_section (= Datei selbst, nicht Unterknoten).
	 * ZIP und E-Mail bleiben Container (auch nicht geladen mit Pfeil, s.
	 * sond_file_part_create_unread()); das Aufklappen lädt sie bei Bedarf neu. */
	if (stvfm_item_priv->type != SOND_TVFM_ITEM_TYPE_DIR ||
			!sfp_old ||
			stvfm_item_priv->path_or_section ||
			!SOND_IS_FILE_PART_PDF(sfp_old))
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

/* ------------------------------------------------------------------ */
/*  Anbindung des Watchers (sond_seadrive_watcher.c) an das Widget     */
/* ------------------------------------------------------------------ */

static void watcher_changed_cb(SondSeadriveWatcher *w, gpointer user_data) {
	(void) w;

	gtk_widget_queue_draw(GTK_WIDGET(user_data));
}

static void watcher_status_cb(SondSeadriveWatcher *w, guint pending_down,
		guint pending_up, gpointer user_data) {
	SondTreeviewFM *stvfm = SOND_TREEVIEWFM(user_data);

	(void) w;

	g_signal_emit(stvfm, SOND_TREEVIEWFM_GET_CLASS(stvfm)->signal_seadrive_status, 0,
			pending_down, pending_up);
}

static void watcher_hydrated_cb(SondSeadriveWatcher *w, const gchar *path,
		gpointer user_data) {
	(void) w;

	sond_treeviewfm_seadrive_item_hydrated(SOND_TREEVIEWFM(user_data), path);
}

static void watcher_dehydrated_cb(SondSeadriveWatcher *w, const gchar *path,
		gpointer user_data) {
	(void) w;

	sond_treeviewfm_seadrive_item_dehydrated(SOND_TREEVIEWFM(user_data), path);
}

SondSeadriveBadge
sond_treeviewfm_seadrive_get_file_badge(SondTreeviewFM *stvfm,
		const gchar *file_full_path) {
	if (!stvfm)
		return SOND_SEADRIVE_BADGE_NONE;

	return sond_seadrive_watcher_get_file_badge(
			sond_treeviewfm_get_priv(stvfm)->seadrive_watcher, file_full_path);
}

SondSeadriveDirStatus
sond_treeviewfm_seadrive_get_dir_status(SondTreeviewFM *stvfm,
		const gchar *dir_path) {
	if (!stvfm)
		return SOND_SEADRIVE_DIR_STATUS_NONE;

	return sond_seadrive_watcher_get_dir_status(
			sond_treeviewfm_get_priv(stvfm)->seadrive_watcher, dir_path);
}

void
sond_treeviewfm_seadrive_start_watcher(SondTreeviewFM *stvfm) {
	SondTreeviewFMPrivate *p = sond_treeviewfm_get_priv(stvfm);
	SondSeadriveWatcher *w = NULL;

	if (p->seadrive_watcher || !p->root)
		return;

	w = sond_seadrive_watcher_new(p->root);
	g_signal_connect_object(w, "changed", G_CALLBACK(watcher_changed_cb), stvfm, 0);
	g_signal_connect_object(w, "status-changed", G_CALLBACK(watcher_status_cb), stvfm,
			0);
	g_signal_connect_object(w, "hydrated", G_CALLBACK(watcher_hydrated_cb), stvfm, 0);
	g_signal_connect_object(w, "dehydrated", G_CALLBACK(watcher_dehydrated_cb), stvfm,
			0);
	p->seadrive_watcher = w;
	sond_seadrive_watcher_start(w);
}

//Löst den Watcher vom Widget; danach kommen keine Signale mehr
static SondSeadriveWatcher*
seadrive_watcher_detach(SondTreeviewFM *stvfm) {
	SondTreeviewFMPrivate *p = sond_treeviewfm_get_priv(stvfm);
	SondSeadriveWatcher *w = p->seadrive_watcher;

	p->seadrive_watcher = NULL;
	if (w)
		g_signal_handlers_disconnect_by_data(w, stvfm);

	return w;
}

void
sond_treeviewfm_seadrive_stop_watcher(SondTreeviewFM *stvfm) {
	SondSeadriveWatcher *w = seadrive_watcher_detach(stvfm);

	if (!w)
		return;

	sond_seadrive_watcher_stop(w);
	g_object_unref(w);
}

/* Nicht-blockierende Variante für sond_treeviewfm_set_root() (Projekt
 * schließen oder wechseln); das Warten und die Freigabe der großen Tabellen
 * übernimmt der Reaper des Watchers. finalize() nutzt die blockierende Variante. */
void
sond_treeviewfm_seadrive_stop_watcher_async(SondTreeviewFM *stvfm) {
	SondSeadriveWatcher *w = seadrive_watcher_detach(stvfm);

	if (!w)
		return;

	sond_seadrive_watcher_stop_async(w);
	g_object_unref(w);
}

/* Der Zustand liegt im Watcher und ist mit ihm weg; hier nur noch die Anzeige
 * auf (0, 0) setzen. Aufgerufen von sond_treeviewfm_set_root(). */
void
sond_seadrive_reset_ground_truth(SondTreeviewFM *stvfm) {
	if (!stvfm)
		return;

	g_signal_emit(stvfm, SOND_TREEVIEWFM_GET_CLASS(stvfm)->signal_seadrive_status, 0,
			(guint) 0, (guint) 0);
}

#endif /* _WIN32 */
