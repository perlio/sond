#ifndef SOND_TREEVIEW_H_INCLUDED
#define SOND_TREEVIEW_H_INCLUDED

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SOND_TYPE_TREEVIEW sond_treeview_get_type( )
G_DECLARE_DERIVABLE_TYPE (SondTreeview, sond_treeview, SOND, TREEVIEW, GtkTreeView)


struct _SondTreeviewClass
{
    GtkTreeViewClass parent_class;

};

typedef struct _Clipboard Clipboard;

SondTreeview* sond_treeview_new( );

void sond_treeview_set_clipboard( SondTreeview*, Clipboard* );

Clipboard* sond_treeview_get_clipboard( SondTreeview* );

void sond_treeview_set_render_text_cell_func( SondTreeview*, void (*)
        ( SondTreeview*, GtkTreeIter*, gpointer ), gpointer );

GtkTreeViewColumn* sond_treeview_get_column( SondTreeview* );

GtkCellRenderer* sond_treeview_get_cell_renderer_icon( SondTreeview* );

GtkCellRenderer* sond_treeview_get_cell_renderer_text( SondTreeview* );

void sond_treeview_expand_row( SondTreeview*, GtkTreeIter* );

GtkTreeIter* sond_treeview_insert_node( SondTreeview*, GtkTreeIter*, gboolean );

GtkTreeIter* sond_treeview_get_cursor( SondTreeview* );

void sond_treeview_set_cursor( SondTreeview*, GtkTreeIter* );

void sond_treeview_set_cursor_on_text_cell( SondTreeview* stv, GtkTreeIter* iter );

gboolean sond_treeview_test_cursor_descendant( SondTreeview* );

GPtrArray* sond_treeview_selection_get_refs( SondTreeview* );

void sond_treeview_copy_or_cut_selection( SondTreeview*, gboolean );

gint sond_treeview_clipboard_foreach( SondTreeview*, gint (*)
        ( SondTreeview*, GtkTreeIter*, gpointer, gchar** ), gpointer, gchar** );

gint sond_treeview_selection_foreach( SondTreeview*, gint (*)
        ( SondTreeview*, GtkTreeIter*, gpointer, gchar** ), gpointer, gchar** );


G_END_DECLS

#endif // SOND_TREEVIEW_H_INCLUDED
