#ifndef SOND_TREEVIEWFM_H_INCLUDED
#define SOND_TREEVIEWFM_H_INCLUDED

#include <glib-object.h>
#include <gtk/gtk.h>

#include "sond_treeview.h"

typedef struct _ZondDBase ZondDBase;

G_BEGIN_DECLS

//Sond_TVFM_ITEM definieren - lokales GObject-Derivat
#define SOND_TYPE_TVFM_ITEM sond_tvfm_item_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondTVFMItem, sond_tvfm_item, SOND, TVFM_ITEM, GObject)

struct _SondTVFMItemClass {
	GObjectClass parent_class;
};

//Sond_TVFM_ITEM_FILE definieren
#define SOND_TYPE_TVFM_ITEM_FILE sond_tvfm_item_file_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondTVFMItemFile, sond_tvfm_item_file, SOND,
		TVFM_ITEM_FILE, SondTVFMItem)

struct _SondTVFMItemFileClass {
	SondTVFMItemClass parent_class;
};

#define SOND_TYPE_TREEVIEWFM sond_treeviewfm_get_type( )
G_DECLARE_DERIVABLE_TYPE(SondTreeviewFM, sond_treeviewfm, SOND, TREEVIEWFM,
		SondTreeview)

struct _SondTreeviewFMClass {
	SondTreeviewClass parent_class;
	gint (*dbase_begin)(SondTreeviewFM*, GError**);
	gint (*dbase_test)(SondTreeviewFM*, const gchar*, GError**);
	gint (*dbase_update_path)(SondTreeviewFM*, const gchar*, const gchar*,
			GError**);
	gint (*dbase_end)(SondTreeviewFM*, gboolean, GError**);
	gint (*text_edited)(SondTreeviewFM*, GtkTreeIter*, GObject*, const gchar*,
			GError**);
	void (*results_row_activated)(GtkWidget*, GtkWidget*, gpointer);
	gint (*insert_dummy)(SondTreeviewFM*, GtkTreeIter*, GObject*, GError**);
	gint (*expand_dummy)(SondTreeviewFM*, GtkTreeIter*, GObject*, GError**);
	void (*render_icon)(SondTreeviewFM*, GtkCellRenderer*, GtkTreeIter*,
			GObject*);
	gint (*render_text)(SondTreeviewFM*, GtkTreeIter*, GObject*, gchar const**,
			gboolean*, GError**);
	gint (*open_row)(SondTreeviewFM*, GtkTreeIter*, GObject*, gboolean,
			GError**);
};

gint sond_treeviewfm_rel_path_visible(SondTreeviewFM*, gchar const*, gboolean,
		gboolean*, GtkTreeIter*, GError**);

gint sond_treeviewfm_set_root(SondTreeviewFM*, const gchar*, gchar**);

const gchar* sond_treeviewfm_get_root(SondTreeviewFM*);

void sond_treeviewfm_set_dbase(SondTreeviewFM*, ZondDBase*);

ZondDBase* sond_treeviewfm_get_dbase(SondTreeviewFM*);

void sond_treeviewfm_column_eingang_set_visible(SondTreeviewFM*, gboolean);

gchar* sond_treeviewfm_get_full_path(SondTreeviewFM*, GtkTreeIter*);

gchar* sond_treeviewfm_get_rel_path(SondTreeviewFM*, GtkTreeIter*);

gint sond_treeviewfm_paste_clipboard(SondTreeviewFM*, gboolean, gchar**);

#endif // SOND_TREEVIEWFM_H_INCLUDED