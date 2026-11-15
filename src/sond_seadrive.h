#ifndef SOND_SEADRIVE_H_INCLUDED
#define SOND_SEADRIVE_H_INCLUDED

#include "sond_seadrive_core.h"
#include "sond_treeviewfm.h"
#include "sond_icon_util.h" /* SondSeadriveBadge, SondSeadriveDirStatus */

G_BEGIN_DECLS

/*
 * SeaDrive integration is Windows-only.
 * On Linux, SeaDrive uses a FUSE driver - the pin/offline concept
 * does not map to Windows file attributes and CF-API does not exist.
 * All functions below are no-ops on non-Windows platforms.
 */

/* Pin state values: s. sond_seadrive_core.h */

/* Der Viewer linkt sond_seadrive.c nicht (haengt am TreeviewFM): Stubs. */
#if defined(_WIN32) && !defined(VIEWER)

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
 * Sammelmeldung für gescheiterte sond_seadrive_set_pin_state()-Aufrufe
 * ("Cache leeren", "Immer verfügbar" auf mehrere Einträge): note_failure
 * merkt einen Fehlschlag in failed (GPtrArray mit g_free), report_failures
 * zeigt am Ende einen einzigen Dialog (Namen und Ursache, die ersten fünf).
 */
void sond_seadrive_pin_note_failure(GPtrArray *failed, const gchar *full_path,
                                    GError *error);
void sond_seadrive_pin_report_failures(GtkWidget *parent, GPtrArray *failed);

/*
 * Stößt die Hydrierung (den Download) einer noch nicht lokal vorhandenen
 * Cloud-Datei über die offizielle CfHydratePlaceholder()-API an. Der Trick
 * CreateFileW(GENERIC_READ)+ReadFile() schlägt unter aktuellem Windows
 * zuverlässig mit ERROR_CLOUD_FILE_ACCESS_DENIED fehl (s. ausführlichen
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
 * nur im Log. Hintergrund: der synchrone Aufruf von sond_seadrive_hydrate()
 * blockierte bei sehr großen Dateien den GTK-Hauptthread ohne
 * Rückmeldung/Abbrechen-Möglichkeit (s. ausführlichen Doc-Kommentar an der
 * Implementierung, sond_treeviewfm_seadrive.c).
 */
void sond_seadrive_hydrate_async(const gchar *full_path);

/*
 * Wie sond_seadrive_hydrate_async(), meldet aber einen Fehlschlag (z.B. SeaDrive
 * oder Server nicht erreichbar) in einem Dialog über parent. Keine Meldung bei
 * vom Nutzer abgebrochener Hydrierung oder wenn parent inzwischen zerstört ist.
 * parent darf NULL sein (dann nur Log).
 */
void sond_seadrive_hydrate_async_notify(const gchar *full_path,
        GtkWindow *parent);

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
 * Lädt alle full_paths, die als nicht geladene SeaDrive-Platzhalter vorliegen: Downloads
 * anstoßen, ein modales Fenster zeigt Fortschritt (Dateien und Volumen) mit
 * "Abbrechen" und wartet in einer verschachtelten Hauptschleife, bis alle Dateien
 * geladen sind. TRUE: alles
 * lokal, der Aufrufer macht weiter. FALSE: Abbruch durch den Nutzer oder
 * gescheiterte Downloads (schon gemeldet), der Aufrufer bricht still ab.
 * Die _file-Variante prüft einen einzelnen absoluten Pfad (beliebige Datei,
 * auch außerhalb des Projekts).
 */
gboolean sond_seadrive_load_sync(GtkWindow *parent, GPtrArray *full_paths);
gboolean sond_seadrive_load_file_sync(GtkWindow *parent, const gchar *full_path);

/*
 * Attach SeaDrive menu items to the context menu of stvfm.
 * Call this after sond_treeviewfm_init_contextmenu().
 */
void sond_treeviewfm_seadrive_init_contextmenu(SondTreeviewFM *stvfm);

/*
 * Setzt den Pin-State rekursiv auf dem gesamten Projektverzeichnis (Root
 * von stvfm) und zeigt bei Fehlern selbst einen Dialog. Nur über das
 * Hauptmenü ("Extras > SeaDrive", win.sd-*-all in headerbar.c) erreichbar,
 * nicht im Kontextmenü von BAUM_FS: die Aktion betrifft immer die
 * Projekt-Wurzel, unabhängig von Rechtsklick-Ziel oder Selektion, und
 * gehört damit nicht in ein Kontextmenü (das "dieser Punkt"/"diese
 * Auswahl" suggeriert).
 */
void sond_treeviewfm_seadrive_pin_root(SondTreeviewFM *stvfm, guint pin_state);

/*
 * Setzt den Pin-State auf der aktuellen Selektion in stvfm (rekursiv bei
 * ausgewählten Ordnern). Vom Kontextmenü von BAUM_FS selbst genutzt UND
 * vom Hauptmenü ("Projekt > SeaDrive > .../Auswahl", win.sd-*-sel in
 * headerbar.c), wenn BAUM_FS gerade der Baum mit einer
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

void sond_treeviewfm_seadrive_item_dehydrated(SondTreeviewFM *stvfm, const gchar *full_path);

#else /* !_WIN32 || VIEWER */

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

static inline void
sond_seadrive_pin_note_failure(GPtrArray *failed, const gchar *full_path,
                               GError *error)
{
    (void)failed; (void)full_path; (void)error;
}

static inline void
sond_seadrive_pin_report_failures(GtkWidget *parent, GPtrArray *failed)
{
    (void)parent; (void)failed;
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

static inline void
sond_seadrive_hydrate_async_notify(const gchar *full_path, GtkWindow *parent)
{
    (void)full_path; (void)parent;
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
sond_seadrive_load_sync(GtkWindow *parent, GPtrArray *full_paths)
{
    (void)parent; (void)full_paths;
    return TRUE;
}

static inline gboolean
sond_seadrive_load_file_sync(GtkWindow *parent, const gchar *full_path)
{
    (void)parent; (void)full_path;
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

#endif /* _WIN32 && !VIEWER */

#if defined(_WIN32) && !defined(VIEWER)
/* Liefert den aktuellen, vom Scan/Watcher gepflegten Datei-Badge für
 * file_full_path (voller Pfad), oder NONE, wenn kein Eintrag existiert.
 * Ersetzt einen früheren LIVEN GetFileAttributesW-Aufruf pro Renderzeile
 * durch einen reinen O(1)-Hashtable-Lookup - der Watcher hält die Map
 * ohnehin schon aktuell (der Ordner-Status nutzt das Muster schon,
 * dieser Datei-Badge soll konsistent dazu sein). Auch von anderen Bäumen nutzbar (z.B. ZondTreeview),
 * die auf dieselben Datei-Pfade verweisen - dafür stvfm auf die FS-Baum-
 * Instanz des Projekts (BAUM_FS) beziehen. */
SondSeadriveBadge sond_treeviewfm_seadrive_get_file_badge(SondTreeviewFM*,
             const gchar *file_full_path);
/* Liefert den aggregierten Hydrierungsstatus für dir_path (voller Pfad,
 * wie von watcher_count_pending_down() als Key verwendet), oder NONE,
 * wenn kein Eintrag existiert (z.B. Ordner erst nach dem letzten
 * Rescan angelegt), der Teilbaum leer ist, oder alle Dateien hydriert,
 * aber nicht alle gepinnt sind (dieselbe "kein Icon nötig"-Bedeutung wie
 * beim Datei-Badge, s. SondSeadriveDirStatus). */
SondSeadriveDirStatus sond_treeviewfm_seadrive_get_dir_status(SondTreeviewFM*,
             const gchar *dir_path);
void     sond_treeviewfm_seadrive_item_hydrated(SondTreeviewFM*, const gchar *full_path);
void     sond_treeviewfm_seadrive_start_watcher(SondTreeviewFM*);
void     sond_treeviewfm_seadrive_stop_watcher(SondTreeviewFM*);
/* Nicht-blockierende Variante für sond_treeviewfm_set_root() (Projekt
 * schließen/wechseln) - s. ausführlichen Kommentar in sond_treeviewfm.c.
 * NICHT verwenden, wenn stvfm selbst im Anschluss zerstört wird
 * (finalize() nutzt weiterhin die blockierende Variante). */
void     sond_treeviewfm_seadrive_stop_watcher_async(SondTreeviewFM*);

/* Setzt die Anzeige des Statussignals auf (0, 0) - aufgerufen von
 * sond_treeviewfm_set_root() bei Projekt-Wechsel oder -Schließen. Der Zustand
 * selbst liegt im Watcher und verschwindet mit ihm. */
void     sond_seadrive_reset_ground_truth(SondTreeviewFM*);
#endif

G_END_DECLS

#endif /* SOND_SEADRIVE_H_INCLUDED */
