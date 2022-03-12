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

ZondTreeview* zond_treeview_new( Projekt*, gint );

GtkTreeIter* zond_treeview_abfragen_iter( ZondTreeview*, gint );

GtkTreePath* zond_treeview_get_path( SondTreeview*, gint );

G_END_DECLS

#endif // SOND_TREEVIEW_H_INCLUDED
