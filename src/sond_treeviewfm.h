#ifndef SOND_TREEVIEWFM_H_INCLUDED
#define SOND_TREEVIEWFM_H_INCLUDED

#include <glib-object.h>
#include <gtk/gtk.h>

#include "sond_treeview.h"


G_BEGIN_DECLS

typedef struct _DBase DBase;


#define SOND_TYPE_TREEVIEWFM sond_treeviewfm_get_type( )
G_DECLARE_DERIVABLE_TYPE (SondTreeviewFM, sond_treeviewfm, SOND, TREEVIEWFM, SondTreeview)


struct _SondTreeviewFMClass
{
    SondTreeviewClass parent_class;
    void (*row_text_edited) (GtkCellRenderer*, gchar*, gchar*, gpointer);
    gint (*dbase_begin) (SondTreeviewFM*, gchar**);
    gint (*dbase_test) (SondTreeviewFM*, const gchar*, gchar**);
    gint (*dbase_update_path) (SondTreeviewFM*, const gchar*, const gchar*, gchar**);
    gint (*dbase_update_eingang) (SondTreeviewFM*, const gchar*, const gchar*, gboolean, gchar**);
    gint (*dbase_end) (SondTreeviewFM*, gboolean, gchar**);
};


SondTreeviewFM* sond_treeviewfm_new( void );

gint sond_treeviewfm_set_root( SondTreeviewFM*, const gchar*, gchar** );

const gchar* sond_treeviewfm_get_root( SondTreeviewFM* );

void sond_treeviewfm_set_dbase( SondTreeviewFM*, DBase* );

DBase* sond_treeviewfm_get_dbase( SondTreeviewFM* );

void sond_treeviewfm_column_eingang_set_visible( SondTreeviewFM*, gboolean );

gchar* sond_treeviewfm_get_full_path( SondTreeviewFM*, GtkTreeIter* );

gchar* sond_treeviewfm_get_rel_path( SondTreeviewFM*, GtkTreeIter* );

gint sond_treeviewfm_create_dir( SondTreeviewFM*, gboolean, gchar** );

gint sond_treeviewfm_paste_clipboard( SondTreeviewFM*, gboolean, gchar** );

gint sond_treeviewfm_selection_loeschen( SondTreeviewFM*, gchar** );



#endif // SOND_TREEVIEWFM_H_INCLUDED
