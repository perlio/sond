#ifndef ZOND_TREEVIEW_H_INCLUDED
#define ZOND_TREEVIEW_H_INCLUDED

#include <glib-object.h>
#include <gtk/gtk.h>

#include "../sond_treeview.h"

typedef struct _Projekt Projekt;
typedef int gint;

G_BEGIN_DECLS

#define ZOND_TYPE_TREEVIEW zond_treeview_get_type( )
G_DECLARE_DERIVABLE_TYPE (ZondTreeview, zond_treeview, ZOND, TREEVIEW, SondTreeview)


struct _ZondTreeviewClass
{
    SondTreeviewClass parent_class;
};

void zond_treeview_cursor_changed( ZondTreeview*, gpointer );

gboolean zond_treeview_get_anchor( Projekt*, gboolean, GtkTreeIter*,
        GtkTreeIter*, gint* );

gint zond_treeview_walk_tree( ZondTreeview*, gboolean, gint,
        GtkTreeIter*, gboolean, GtkTreeIter*, gint, gint*,
        gint (*) (ZondTreeview*, gint, GtkTreeIter*, gboolean,
        GtkTreeIter*, gint, gint*, GError**), GError** );

gint zond_treeview_copy_node_to_baum_auswertung( ZondTreeview*,
        gint, GtkTreeIter*, gboolean, GtkTreeIter*, gint, gint*, GError** );

ZondTreeview* zond_treeview_new( Projekt*, gint );

GtkTreeIter* zond_treeview_abfragen_iter( ZondTreeview*, gint );

GtkTreePath* zond_treeview_get_path( SondTreeview*, gint );

gint zond_treeview_load_baum( ZondTreeview*, GError** );

G_END_DECLS

#endif // SOND_TREEVIEW_H_INCLUDED
