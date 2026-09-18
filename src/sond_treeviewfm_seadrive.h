#ifndef SOND_TREEVIEWFM_SEADRIVE_H_INCLUDED
#define SOND_TREEVIEWFM_SEADRIVE_H_INCLUDED

#include "sond_treeviewfm.h"

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
 * Zeigt einen kleinen Dialog mit Fortschritt (bereits heruntergeladene/
 * gesamte Bytes) und einem Abbrechen-Button für eine bereits laufende
 * Hydrierung von full_path. Gedacht für einen erneuten Doppelklick auf
 * eine Datei, die schon hydriert wird (s. sond_treeviewfm_open()) -
 * Nutzer-Wunsch 18.09.2026, um bei versehentlich angeklickten
 * Großdateien den SeaDrive-Server nicht unnötig weiter zu belasten.
 * Schließen des Dialogs (per "Schließen"-Button oder X) bricht die
 * Hydrierung NICHT ab; der Dialog schließt sich außerdem von selbst,
 * sobald die Hydrierung endet.
 */
void sond_seadrive_show_hydrate_progress_dialog(GtkWindow *parent,
        const gchar *full_path);

/*
 * Konsolidierte Check-und-Reagiere-Sequenz für Doppelklick-artige
 * Öffnen-Aktionen (s. ausführl. Doc-Kommentar an der Implementierung,
 * sond_treeviewfm_seadrive.c) - fasst needs_hydration()/is_hydrating()/
 * hydrate_async()/show_hydrate_progress_dialog() zusammen, damit diese
 * Sequenz nicht an jeder Öffnen-Stelle (BAUM_FS, BAUM_INHALT/
 * AUSWERTUNG, ...) erneut dupliziert werden muss. full_path muss der
 * volle Pfad der realen Datei im Dateisystem sein (Container-Vorfahre
 * mit parent==NULL). Rückgabe: TRUE = Datei ist bereits lokal, Aufrufer
 * soll normal öffnen. FALSE = Hydrierung wurde angestoßen bzw. Dialog
 * gezeigt, Aufrufer soll sofort zurückkehren statt zu öffnen.
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

static inline void
sond_seadrive_show_hydrate_progress_dialog(GtkWindow *parent,
        const gchar *full_path)
{
    (void)parent; (void)full_path;
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

G_END_DECLS

#endif /* SOND_TREEVIEWFM_SEADRIVE_H_INCLUDED */
