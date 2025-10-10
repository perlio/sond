#ifndef SOND_TREEVIEWFM_H_INCLUDED
#define SOND_TREEVIEWFM_H_INCLUDED

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#include "sond_treeview.h"

typedef struct _SondFilePart SondFilePart;

G_BEGIN_DECLS

typedef enum {
	SOND_TVFM_ITEM_TYPE_DIR, //Verzeichnis
	SOND_TVFM_ITEM_TYPE_LEAF, //SondFilePart
	SOND_TVFM_ITEM_TYPE_LEAF_SECTION //Teil von Datei
} SondTVFMItemType;

//Sond_TVFM_ITEM definieren - lokales GObject-Derivat
#define SOND_TYPE_TVFM_ITEM sond_tvfm_item_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondTVFMItem, sond_tvfm_item, SOND, TVFM_ITEM, GObject)

struct _SondTVFMItemClass {
	GObjectClass parent_class;

	gint (*load_sections)(SondTVFMItem*, GPtrArray**, GError**);
};

#define SOND_TYPE_TREEVIEWFM sond_treeviewfm_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondTreeviewFM, sond_treeviewfm, SOND, TREEVIEWFM, SondTreeview)

struct _SondTreeviewFMClass {
	SondTreeviewClass parent_class;

	gint (*deter_background)(SondTVFMItem*, GError**);
	gint (*text_from_section)(SondTVFMItem*, gchar**, GError**);
	gint (*before_delete)(SondTVFMItem*, GError**);
	gint (*before_move)(SondTreeviewFM*, gchar const*, gchar const*, GError**);
	void (*after_move)(SondTreeviewFM*, gboolean, GError*);
	gint (*text_edited)(SondTreeviewFM*, GtkTreeIter*, SondTVFMItem*, const gchar*,
			GError**);
	void (*results_row_activated)(GtkWidget*, GtkWidget*, gpointer);
	gint (*open_stvfm_item) (SondTreeviewFM*, SondTVFMItem*, gboolean, GError**);
	gint (*load_sections) (SondTVFMItem*, GPtrArray**, GError**);
	gboolean (*has_sections) (SondTVFMItem*);
	gint (*delete_section) (SondTVFMItem*, GError**);
};

SondTVFMItemType sond_tvfm_item_get_item_type(SondTVFMItem*);

gchar const* sond_tvfm_item_get_path_or_section(SondTVFMItem *);

SondFilePart* sond_tvfm_item_get_sond_file_part(SondTVFMItem*);

SondTreeviewFM* sond_tvfm_item_get_stvfm(SondTVFMItem *);

void sond_tvfm_item_set_icon_name(SondTVFMItem*, gchar const*);

gchar const* sond_tvfm_item_get_icon_name(SondTVFMItem*);

SondTVFMItem* sond_tvfm_item_create(SondTreeviewFM*, SondTVFMItemType type,
		SondFilePart *, gchar const*);

gint sond_tvfm_item_load_children(SondTVFMItem*, GPtrArray**, GError**);

gint sond_treeviewfm_file_part_visible(SondTreeviewFM*, GtkTreeIter*,
		gchar const*, gboolean, GtkTreeIter*, GError**);

gint sond_treeviewfm_set_root(SondTreeviewFM*, const gchar*, gchar**);

const gchar* sond_treeviewfm_get_root(SondTreeviewFM*);

G_END_DECLS

#endif // SOND_TREEVIEWFM_H_INCLUDED
