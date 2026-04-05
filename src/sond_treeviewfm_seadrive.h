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

gpointer sond_treeviewfm_seadrive_watcher_thread(gpointer user_data);

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

#endif /* _WIN32 */

G_END_DECLS

#endif /* SOND_TREEVIEWFM_SEADRIVE_H_INCLUDED */
