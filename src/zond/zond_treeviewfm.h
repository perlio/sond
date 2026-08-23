#ifndef ZOND_TREEVIEWFM_H_INCLUDED
#define ZOND_TREEVIEWFM_H_INCLUDED

#include <glib-object.h>
#include <gtk/gtk.h>

#include "../sond_treeviewfm.h"

#include "zond_init.h"

G_BEGIN_DECLS

//ZOND_TYPE_TREEVIEWFM definieren
#define ZOND_TYPE_TREEVIEWFM zond_treeviewfm_get_type( )
G_DECLARE_DERIVABLE_TYPE(ZondTreeviewFM, zond_treeviewfm, ZOND, TREEVIEWFM, SondTreeviewFM)

struct _ZondTreeviewFMClass {
	SondTreeviewFMClass parent_class;
};

gint zond_treeviewfm_insert_section(ZondTreeviewFM*, gint, GtkTreeIter*,
		gboolean, GtkTreeIter*, GError**);

ZondTreeviewFM* zond_treeviewfm_new(Projekt* zond);

gint zond_treeviewfm_section_visible(ZondTreeviewFM*, gchar const*,
		gchar const*, gboolean, gboolean*, GtkTreeIter*, gboolean*, gboolean*,
		GError**);

gint zond_treeviewfm_set_cursor_on_section(ZondTreeviewFM*, gchar const*,
		gchar const*, GError**);

void zond_treeviewfm_kill_parent(ZondTreeviewFM*, GtkTreeIter*);

/* Wie sond_treeviewfm_get_fileparts() (sond_treeviewfm.h), aber
 * Anbindungs-bewusst: ein markierter SOND_TVFM_ITEM_TYPE_LEAF_SECTION-
 * Knoten in BAUM_FS (= eine Anbindung, s. ziele.c) liefert den
 * tatsächlichen Seitenbereich statt (wie in der Basisklasse) immer NULL
 * (= ganze Datei). */
GHashTable* zond_treeviewfm_get_fileparts(ZondTreeviewFM*, gboolean,
		GError**);

G_END_DECLS

#endif // ZOND_TREEVIEWFM_H_INCLUDED
