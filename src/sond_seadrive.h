#ifndef SOND_SEADRIVE_H_INCLUDED
#define SOND_SEADRIVE_H_INCLUDED

#include "sond_treeviewfm.h"
#include "sond_icon_util.h" /* SondSeadriveBadge, SondSeadriveDirStatus */

G_BEGIN_DECLS

/*
 * SeaDrive integration is Windows-only.
 * On Linux, SeaDrive uses a FUSE driver - the pin/offline concept
 * does not map to Windows file attributes and CF-API does not exist.
 * All functions below are no-ops on non-Windows platforms.
 */

/* Pin state values (mirror CF_PIN_STATE_* from cfapi.h) */
#define STVFM_PIN_STATE_UNSPECIFIED  0   /* "Always available" deactivated  */
#define STVFM_PIN_STATE_PINNED       1   /* "Always available" activated    */
#define STVFM_PIN_STATE_UNPINNED     2   /* clear cache                     */
#define STVFM_PIN_STATE_EXCLUDED     3
#define STVFM_PIN_STATE_INHERIT      4

#ifdef _WIN32

/*
 * Returns TRUE if full_path lies inside a CF-API sync root
 * (i.e. is a SeaDrive, OneDrive or similar cloud directory).
 * Use this to decide whether SeaDrive operations make sense.
 */
gboolean sond_seadrive_is_seadrive_path(const gchar *full_path);

/*
 * Get the current PinState of a file/directory.
 * Returns STVFM_PIN_STATE_UNSPECIFIED if cldapi.dll is not available
 * or the file is not a cloud file.
 */
guint sond_seadrive_get_pin_state(const gchar *full_path);

/*
 * Set the PinState on a single file or directory.
 * If recurse is TRUE and full_path is a directory, all children
 * are also set (using CfSetPinState with CF_SET_PIN_FLAG_RECURSE).
 * Returns TRUE on success.
 */
gboolean sond_seadrive_set_pin_state(const gchar *full_path,
                                     guint        pin_state,
                                     gboolean     recurse,
                                     GError     **error);

/*
 * Stößt die Hydrierung (den Download) einer noch nicht lokal vorhandenen
 * Cloud-Datei über die offizielle CfHydratePlaceholder()-API an - Ersatz
 * für den früheren CreateFileW(GENERIC_READ)+ReadFile()-Trick, der nach
 * dem Windows-Update KB5124008 (09/2026) zuverlässig mit
 * ERROR_CLOUD_FILE_ACCESS_DENIED fehlschlägt (s. ausführlichen
 * Doc-Kommentar an der Implementierung, sond_treeviewfm_seadrive.c).
 * TRUE (No-Op), wenn die Datei schon lokal ist. FALSE mit gesetztem error
 * bei echtem Fehlschlag (z.B. CF-API nicht verfügbar).
 */
gboolean sond_seadrive_hydrate(const gchar *full_path, GError **error);

/*
 * Schneller, nicht-blockierender Vorab-Check (nur GetFileAttributesW):
 * TRUE, wenn full_path ein noch nicht lokal vorhandener Cloud-Platzhalter
 * ist (Hydrierung nötig), FALSE wenn die Datei schon lokal ist oder ihre
 * Attribute nicht gelesen werden konnten. Erlaubt es Aufrufern (s.
 * sond_treeviewfm_open()), bei bereits lokalen Dateien direkt normal zu
 * öffnen, statt unnötig einen Hydrier-Hintergrund-Thread zu starten.
 */
gboolean sond_seadrive_needs_hydration(const gchar *full_path);

/*
 * Wie sond_seadrive_hydrate(), aber nicht-blockierend: stößt die
 * Hydrierung in einem Hintergrund-Thread an und kehrt sofort zurück
 * (Fire-and-forget). Ein erneuter Aufruf für denselben full_path,
 * während bereits ein Thread dafür läuft, ist ein No-Op. Fehler landen
 * nur im Log. Hintergrund: bei sehr großen Dateien blockierte der
 * synchrone Aufruf von sond_seadrive_hydrate() im GTK-Hauptthread das
 * gesamte Programm ohne Rückmeldung/Abbrechen-Möglichkeit (Nutzer-Fund
 * 18.09.2026, s. ausführlichen Doc-Kommentar an der Implementierung,
 * sond_treeviewfm_seadrive.c, und ToDo.c).
 */
void sond_seadrive_hydrate_async(const gchar *full_path);

/*
 * TRUE, wenn full_path aktuell hydriert wird (sond_seadrive_hydrate_
 * async() dafür einen Hintergrund-Thread laufen hat).
 */
gboolean sond_seadrive_is_hydrating(const gchar *full_path);

/*
 * Versucht (bestmöglich, keine Erfolgsgarantie), eine laufende
 * Hydrierung von full_path abzubrechen. S. ausführlichen Doc-Kommentar
 * an der Implementierung (sond_treeviewfm_seadrive.c) zu den Grenzen
 * dieses Mechanismus (CancelSynchronousIo, für CfHydratePlaceholder()
 * nicht offiziell dokumentiert).
 */
void sond_seadrive_hydrate_cancel(const gchar *full_path);

/*
 * Konsolidierte Check-und-Reagiere-Sequenz für Doppelklick-artige
 * Öffnen-Aktionen (s. ausführl. Doc-Kommentar an der Implementierung,
 * sond_treeviewfm_seadrive.c) - fasst needs_hydration()/is_hydrating()/
 * hydrate_async()/show_hydrate_progress_dialog_multi() zusammen, damit
 * diese Sequenz nicht an jeder Öffnen-Stelle (BAUM_FS, BAUM_INHALT/
 * AUSWERTUNG, ...) erneut dupliziert werden muss. full_path muss der
 * volle Pfad der realen Datei im Dateisystem sein (Container-Vorfahre
 * mit parent==NULL). Rückgabe: TRUE = Datei ist bereits lokal, Aufrufer
 * soll normal öffnen. FALSE = Hydrierung wurde angestoßen bzw. Dialog
 * gezeigt, Aufrufer soll sofort zurückkehren statt zu öffnen.
 *
 * Nutzer-Fund 19.09.2026: nur noch ein dünner Wrapper um
 * sond_seadrive_ensure_hydrated_multi() (s.u.) mit einem einelementigen
 * Array - der vormals eigenständige Einzeldatei-Fortschrittsdialog
 * (sond_seadrive_show_hydrate_progress_dialog()) war eine reine
 * Dopplung der Multi-Variante und wurde entfernt.
 */
gboolean sond_seadrive_ensure_hydrated(GtkWindow *parent,
        const gchar *full_path);

/*
 * Wie sond_seadrive_show_hydrate_progress_dialog(), aber für mehrere
 * gleichzeitig betroffene Dateien (Auszug-Fall im Auswertungsverzeichnis,
 * s. ausführl. Doc-Kommentar an der Implementierung,
 * sond_treeviewfm_seadrive.c) - eine Fortschrittszeile pro Datei in
 * full_paths, ein gemeinsamer Abbrechen-Button (bricht alle noch
 * laufenden Einträge ab) und ein gemeinsamer Schließen-Button.
 */
void sond_seadrive_show_hydrate_progress_dialog_multi(GtkWindow *parent,
        GPtrArray *full_paths);

/*
 * Wie sond_seadrive_ensure_hydrated(), aber für eine Menge von Pfaden
 * (Auszug-Fall) - stößt für jede noch nicht hydrierte Datei aus
 * full_paths die Hydrierung an (No-Op, falls schon läuft) und zeigt bei
 * bereits laufender Hydrierung (mind. einer davon) den gemeinsamen
 * Fortschrittsdialog (s.o.). Rückgabe: TRUE = alle Dateien bereits
 * lokal, Aufrufer soll normal öffnen. FALSE = mindestens eine Datei
 * musste hydriert werden, Aufrufer soll sofort zurückkehren.
 */
gboolean sond_seadrive_ensure_hydrated_multi(GtkWindow *parent,
        GPtrArray *full_paths);

/*
 * Attach SeaDrive menu items to the context menu of stvfm.
 * Call this after sond_treeviewfm_init_contextmenu().
 */
void sond_treeviewfm_seadrive_init_contextmenu(SondTreeviewFM *stvfm);

/*
 * Setzt den Pin-State rekursiv auf dem gesamten Projektverzeichnis (Root
 * von stvfm) und zeigt bei Fehlern selbst einen Dialog. Ehemals "Gesamtes
 * Verzeichnis" im Kontextmenü von BAUM_FS (stv.sd-*-all) - seit 11.09.2026
 * nur noch über das Hauptmenü ("Extras > SeaDrive", win.sd-*-all in
 * headerbar.c) erreichbar: die Aktion betraf schon immer die Projekt-
 * Wurzel, unabhängig von Rechtsklick-Ziel oder Selektion, und gehörte
 * damit eigentlich nie in ein Kontextmenü (das ja "dieser Punkt"/"diese
 * Auswahl" suggeriert) - Nutzer-Feedback, s. ToDo.c.
 */
void sond_treeviewfm_seadrive_pin_root(SondTreeviewFM *stvfm, guint pin_state);

/*
 * Setzt den Pin-State auf der aktuellen Selektion in stvfm (rekursiv bei
 * ausgewählten Ordnern). Vom Kontextmenü von BAUM_FS selbst genutzt UND
 * seit 11.09.2026 vom Hauptmenü ("Projekt > SeaDrive > .../Auswahl",
 * win.sd-*-sel in headerbar.c), wenn BAUM_FS gerade der Baum mit einer
 * Selektion ist.
 */
void sond_treeviewfm_seadrive_pin_selection(SondTreeviewFM *stvfm, guint pin_state);

/*
 * Graut die "Auswahl"-SeaDrive-Menüpunkte im Kontextmenü von stvfm ein/aus.
 * Aufgerufen, wenn ein Projekt geöffnet/geschlossen wird (project.c,
 * project_set_widgets_sensitive()) - sensitive sollte dabei bereits
 * "aktives Projekt UND sond_treeviewfm_is_seadrive_path(stvfm)" sein.
 */
void sond_treeviewfm_seadrive_set_contextmenu_sensitive(SondTreeviewFM *stvfm,
        gboolean sensitive);

gpointer sond_treeviewfm_seadrive_watcher_thread(gpointer user_data);
void sond_treeviewfm_seadrive_item_dehydrated(SondTreeviewFM *stvfm, const gchar *full_path);

#else /* !_WIN32 */

/* Stub inlines for Linux - compile to nothing */
static inline guint
sond_seadrive_get_pin_state(const gchar *full_path)
{
    (void)full_path;
    return STVFM_PIN_STATE_UNSPECIFIED;
}

static inline gboolean
sond_seadrive_set_pin_state(const gchar *full_path,
                            guint        pin_state,
                            gboolean     recurse,
                            GError     **error)
{
    (void)full_path; (void)pin_state; (void)recurse; (void)error;
    return FALSE;
}

static inline gboolean
sond_seadrive_hydrate(const gchar *full_path, GError **error)
{
    (void)full_path; (void)error;
    return TRUE;
}

static inline void
sond_seadrive_hydrate_async(const gchar *full_path)
{
    (void)full_path;
}

static inline gboolean
sond_seadrive_is_hydrating(const gchar *full_path)
{
    (void)full_path;
    return FALSE;
}

static inline void
sond_seadrive_hydrate_cancel(const gchar *full_path)
{
    (void)full_path;
}

static inline gboolean
sond_seadrive_ensure_hydrated(GtkWindow *parent, const gchar *full_path)
{
    (void)parent; (void)full_path;
    return TRUE;
}

static inline gboolean
sond_seadrive_ensure_hydrated_multi(GtkWindow *parent, GPtrArray *full_paths)
{
    (void)parent; (void)full_paths;
    return TRUE;
}

static inline gboolean
sond_seadrive_is_offline(const gchar *full_path)
{
    (void)full_path;
    return FALSE;
}

static inline void
sond_treeviewfm_seadrive_init_contextmenu(SondTreeviewFM *stvfm)
{
    (void)stvfm;
}

static inline void
sond_treeviewfm_seadrive_pin_root(SondTreeviewFM *stvfm, guint pin_state)
{
    (void)stvfm; (void)pin_state;
}

static inline void
sond_treeviewfm_seadrive_pin_selection(SondTreeviewFM *stvfm, guint pin_state)
{
    (void)stvfm; (void)pin_state;
}

static inline void
sond_treeviewfm_seadrive_set_contextmenu_sensitive(SondTreeviewFM *stvfm,
        gboolean sensitive)
{
    (void)stvfm; (void)sensitive;
}

#endif /* _WIN32 */

#ifdef _WIN32
/* Rekursive Ordner-Statistik für den SeaDrive-Coverage-Badge (Ordner-
 * Ebene), jeweils im GANZEN Teilbaum unter dem Ordner (rekursiv), nicht
 * nur direkte Kinder - Rendering bleibt dadurch O(1) pro Zeile. S.
 * sond_treeviewfm_seadrive_get_dir_status(). Bewusst UNABHÄNGIG von
 * seadrive_pending_down_paths/-pending_down (das bleibt die engere Frage
 * "wie viele gepinnte Dateien werden gerade heruntergeladen" für die
 * Projekt-weite Zähleranzeige) - hier geht es um die tatsächliche lokale
 * Verfügbarkeit (Redesign "SeaDrive-Badges Datei+Ordner", 09/2026,
 * nachdem die vorherige, auf "gepinnt+pending" basierende Definition
 * Ordner ohne jedes Pin fälschlich badge-los erscheinen ließ). */
typedef struct {
	guint not_hydrated;    /* Dateien im Teilbaum, die NICHT lokal vorhanden sind (unabhängig vom Pin-Status) */
	guint hydrated_pinned; /* Dateien im Teilbaum, die lokal vorhanden UND gepinnt sind */
	guint total;           /* Dateien insgesamt im Teilbaum */
} SondSeadriveDirCounts;

/* path_pending_down: Pfad, auf den sich delta_down bezieht (NULL, wenn
 * delta_down==0). delta_down>0: Pfad wird ins interne pending_down-Set
 * aufgenommen (Zähler nur erhöht, wenn er noch nicht drin war);
 * delta_down<0: Pfad wird aus dem Set entfernt (Zähler nur verringert,
 * wenn er tatsächlich drin war) - verhindert Drift bei doppelten/
 * verpassten Events (s. Untersuchung SeaDrive-Coverage, 09/2026). Betrifft
 * NUR den Projekt-weiten "wird gerade heruntergeladen"-Zähler
 * (seadrive_pending_down) - für den Ordner-Coverage-Badge s.
 * sond_treeviewfm_seadrive_update_dir_coverage(). */
void     sond_treeviewfm_seadrive_update_status(SondTreeviewFM*,
             const gchar *path_pending_down, gint delta_down,
             const gchar *path_up, gboolean up_pending);
/* Ersetzt das komplette pending_down-Set (Initialscan oder Resync nach
 * Buffer-Overflow) - transfer full, Ownership geht an stvfm über (String-
 * Set, Werte irrelevant). Zähler wird aus g_hash_table_size() abgeleitet. */
void     sond_treeviewfm_seadrive_set_pending_down_paths(SondTreeviewFM*,
             GHashTable *paths);
/* Ersetzt die komplette Ordner-Statistik (Initialscan/Resync) - transfer
 * full (Pfad -> SondSeadriveDirCounts*), Ownership geht an stvfm über. */
void     sond_treeviewfm_seadrive_set_dir_counts(SondTreeviewFM*,
             GHashTable *dir_counts);
/* Ersetzt die komplette Ground-Truth-Map für die Datei-Badges (Initialscan/
 * Resync) - transfer full, Ownership geht an stvfm über. Pfad ->
 * GINT_TO_POINTER(SondSeadriveBadge); Einträge mit Wert NONE werden nicht
 * gespeichert (ein Lookup-Fehlschlag bedeutet ohnehin NONE) - hält die Map
 * kleiner, da der Normalfall (hydriert, nicht gepinnt) die Mehrheit ist. */
void     sond_treeviewfm_seadrive_set_file_badges(SondTreeviewFM*,
             GHashTable *badges);
/* Liefert den aktuellen, vom Scan/Watcher gepflegten Datei-Badge für
 * file_full_path (voller Pfad), oder NONE, wenn kein Eintrag existiert.
 * Ersetzt einen früheren LIVEN GetFileAttributesW-Aufruf pro Renderzeile
 * durch einen reinen O(1)-Hashtable-Lookup - der Watcher hält die Map
 * ohnehin schon aktuell (Untersuchung "Ordner-Badges", 09/2026: der
 * Ordner-Status nutzte das Muster schon, der Datei-Badge inkonsistenter-
 * weise noch nicht). Auch von anderen Bäumen nutzbar (z.B. ZondTreeview),
 * die auf dieselben Datei-Pfade verweisen - dafür stvfm auf die FS-Baum-
 * Instanz des Projekts (BAUM_FS) beziehen. */
SondSeadriveBadge sond_treeviewfm_seadrive_get_file_badge(SondTreeviewFM*,
             const gchar *file_full_path);
/* Aktualisiert den Datei-Badge für GENAU eine Datei (Ground-Truth-Map
 * seadrive_file_badges verhindert Drift bei doppelten/verpassten Events,
 * analog seadrive_pending_down_paths) UND zieht daraus abgeleitet die
 * Ordner-Coverage-Statistik alle Vorfahren-Verzeichnisse hoch nach (s.
 * sond_treeviewfm_seadrive_update_dir_coverage()) - EIN Aufruf pro Datei-
 * Ereignis genügt, weil sich beides aus demselben new_badge-Wert ableitet
 * (PENDING/OFFLINE -> zählt als "nicht hydriert", PINNED -> zählt als
 * "hydriert+gepinnt", NONE -> keins von beidem). new_badge=NONE für eine
 * gelöschte Datei (existiert nicht mehr). delta_total: +1 (ADDED/RENAMED_
 * NEW_NAME) / -1 (REMOVED/RENAMED_OLD_NAME) / 0 (MODIFIED). */
void     sond_treeviewfm_seadrive_update_file_badge(SondTreeviewFM*,
             const gchar *file_full_path, SondSeadriveBadge new_badge,
             gint delta_total);
/* Passt die Ordner-Statistik für GENAU dir_path an (kein Ancestor-Walk -
 * das macht sond_treeviewfm_seadrive_update_dir_coverage() für eine Datei
 * automatisch). Legt den Eintrag bei Bedarf an; negative Deltas werden bei
 * 0 gekappt (Schutz gegen Drift durch verpasste/doppelte Events). */
void     sond_treeviewfm_seadrive_dir_delta(SondTreeviewFM*,
             const gchar *dir_path, gint delta_not_hydrated,
             gint delta_hydrated_pinned, gint delta_total);
/* Wendet die Deltas einer einzelnen Datei-Zustandsänderung (Hydrierung/
 * Pin/Neuanlage/Löschung) auf ALLE Vorfahren-Verzeichnisse von
 * file_full_path bis root an (Ordner-Badge bleibt dadurch laufend
 * aktuell, nicht nur nach einem Rescan). Bewusst von
 * sond_treeviewfm_seadrive_update_status() getrennt - andere Fragestellung
 * (s. SondSeadriveDirCounts). */
void     sond_treeviewfm_seadrive_update_dir_coverage(SondTreeviewFM*,
             const gchar *file_full_path, gint delta_not_hydrated,
             gint delta_hydrated_pinned, gint delta_total);
/* Liefert den aggregierten Hydrierungsstatus für dir_path (voller Pfad,
 * wie von watcher_count_pending_down() als Key verwendet), oder NONE,
 * wenn kein Eintrag existiert (z.B. Ordner erst nach dem letzten
 * Rescan angelegt), der Teilbaum leer ist, oder alle Dateien hydriert,
 * aber nicht alle gepinnt sind (dieselbe "kein Icon nötig"-Bedeutung wie
 * beim Datei-Badge, s. SondSeadriveDirStatus). */
SondSeadriveDirStatus sond_treeviewfm_seadrive_get_dir_status(SondTreeviewFM*,
             const gchar *dir_path);
void     sond_treeviewfm_seadrive_item_hydrated(SondTreeviewFM*, const gchar *full_path);
gboolean sond_treeviewfm_seadrive_stop_requested(SondTreeviewFM*);
void     sond_treeviewfm_seadrive_start_watcher(SondTreeviewFM*);
void     sond_treeviewfm_seadrive_stop_watcher(SondTreeviewFM*);
/* Nicht-blockierende Variante für sond_treeviewfm_set_root() (Projekt
 * schließen/wechseln) - s. ausführlichen Kommentar in sond_treeviewfm.c.
 * NICHT verwenden, wenn stvfm selbst im Anschluss zerstört wird
 * (finalize() nutzt weiterhin die blockierende Variante). */
void     sond_treeviewfm_seadrive_stop_watcher_async(SondTreeviewFM*);

/* Setzt die vier SeaDrive-Ground-Truth-Hashtables (Badges/Coverage/
 * Pending-Sets) sowie die Pending-Zähler zurück und emittiert das Status-
 * Signal mit (0, 0) - aufgerufen von sond_treeviewfm_set_root() bei
 * Projekt-Wechsel/-Schließen, MUSS dort passieren, sonst bleiben Pfade/
 * Zähler einer vorigen Projekt-Session stehen (Nutzer-Fund 18.09.2026,
 * "Schließen dauert 20 Sek." - die eigentliche Zerstörung der Tabellen
 * lief bis dahin synchron im GTK-Hauptthread, s. ausführlichen Kommentar
 * bei der Implementierung in sond_seadrive.c). Verschoben aus
 * sond_treeviewfm.c (Refactoring 18.09.2026, "in _treeviewfm.c sind auch
 * Funktionen, die in sond_treeviewfm_seadrive gehören"). */
void     sond_seadrive_reset_ground_truth(SondTreeviewFM*);
#endif

G_END_DECLS

#endif /* SOND_SEADRIVE_H_INCLUDED */
