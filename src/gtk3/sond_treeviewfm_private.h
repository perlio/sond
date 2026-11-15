#ifndef SOND_TREEVIEWFM_PRIVATE_H_INCLUDED
#define SOND_TREEVIEWFM_PRIVATE_H_INCLUDED

/*
 * "Freund"-Header: G_DEFINE_TYPE_WITH_PRIVATE() erzeugt einen STATISCHEN
 * inline-Accessor, der nur innerhalb derselben Übersetzungseinheit
 * sichtbar ist. Dieser Header macht die beiden privaten Structs UND je
 * einen normalen (nicht-statischen) "Freund"-Accessor bekannt, damit
 * sond_seadrive.c direkt auf SondTreeviewFMPrivate/SondTVFMItemPrivate
 * zugreifen kann (Ground-Truth-Hashtables für Badges/Coverage, Watcher),
 * ohne die öffentliche API von sond_treeviewfm.h zu vergrößern - nicht
 * für Aufrufer außerhalb von sond_treeviewfm.c/sond_seadrive.c gedacht
 * (deshalb kein Include in sond_treeviewfm.h). Die
 * G_DEFINE_TYPE_WITH_PRIVATE()-Makroaufrufe und die GObject-
 * Typregistrierung bleiben in sond_treeviewfm.c - hier nur die Struct-
 * Definitionen und die Freund-Funktionsdeklarationen, deren
 * Implementierung den jeweiligen Makro-Accessor 1:1 durchreicht.
 */

#include "sond_treeviewfm.h"
#ifdef _WIN32
#include "../seadrive/sond_seadrive_watcher.h"
#endif

G_BEGIN_DECLS

//SOND_TREEVIEWFM
typedef struct {
	gchar *root;
	GtkTreeViewColumn *column_eingang;
	gboolean is_seadrive_path;
	SondTreeviewFMIndexCtxFunc index_ctx_func;
	gpointer index_ctx_func_data;
	GFileMonitor *root_monitor; //Zugänge von außen auf Root-Ebene
	guint root_monitor_timer;   //Entprellung
	gchar *search_text;         //Einstellungen der letzten Dateisuche
	SondTVFMSearchOpts search_opts;
	gboolean search_settings_valid;
#ifdef _WIN32
	SondSeadriveWatcher *seadrive_watcher; //nur bei einem Projekt im SeaDrive-Laufwerk
#endif
} SondTreeviewFMPrivate;

/* Freund-Accessor für sond_seadrive.c - reicht 1:1 an den von
 * G_DEFINE_TYPE_WITH_PRIVATE() erzeugten (statischen, nur in
 * sond_treeviewfm.c sichtbaren) sond_treeviewfm_get_instance_private()
 * durch. Definiert in sond_treeviewfm.c. */
SondTreeviewFMPrivate *sond_treeviewfm_get_priv(SondTreeviewFM *stvfm);

#include "../tvfm/sond_tvfm_item_private.h"

G_END_DECLS

#endif // SOND_TREEVIEWFM_PRIVATE_H_INCLUDED
