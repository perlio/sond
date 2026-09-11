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
