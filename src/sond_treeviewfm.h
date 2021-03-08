#ifndef FM_H_INCLUDED
#define FM_H_INCLUDED

#include <glib-object.h>
#include <gtk/gtk.h>

#include "sond_treeview.h"


G_BEGIN_DECLS

typedef struct _DBase DBase;


#define SOND_TYPE_TREEVIEWFM sond_treeviewfm_get_type( )
G_DECLARE_DERIVABLE_TYPE (SondTreeviewFM, sond_treeviewfm, SOND, TREEVIEWFM, SondTreeview)


struct _SondTreeviewFMClass
{
    GtkTreeViewClass parent_class;

};


SondTreeviewFM* sond_treeviewfm_new( void );

gint sond_treeviewfm_set_root( SondTreeviewFM*, const gchar*, gchar** );

const gchar* sond_treeviewfm_get_root( SondTreeviewFM* );

void sond_treeviewfm_set_dbase( SondTreeviewFM*, DBase* );

DBase* sond_treeviewfm_get_dbase( SondTreeviewFM* );

void sond_treeviewfm_set_funcs( SondTreeviewFM*,
        gint (*) (SondTreeviewFM*, const GFile*, const GFile*, gpointer, gchar**),
        gint (*) (SondTreeviewFM*, const gint, gpointer, gchar**),
        gint (*) (SondTreeviewFM*, const GFile*, gpointer, gchar**),
        gpointer func_data );

gchar* sond_treeviewfm_get_full_path( SondTreeviewFM*, GtkTreeIter* );

gchar* sond_treeviewfm_get_rel_path( SondTreeviewFM*, GtkTreeIter* );

gint sond_treeviewfm_create_dir( SondTreeviewFM*, gboolean, gchar** );

gint sond_treeviewfm_paste_clipboard( SondTreeviewFM*, gboolean, gchar** );

gint sond_treeviewfm_clipboard_loeschen( SondTreeviewFM*, gchar** );



#endif // FM_H_INCLUDED
