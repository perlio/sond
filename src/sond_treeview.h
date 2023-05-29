#ifndef SOND_TREEVIEW_H_INCLUDED
#define SOND_TREEVIEW_H_INCLUDED

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SOND_TYPE_TREEVIEW sond_treeview_get_type( )
G_DECLARE_DERIVABLE_TYPE (SondTreeview, sond_treeview, SOND, TREEVIEW, GtkTreeView)


typedef struct _Clipboard {
    SondTreeview* tree_view;
    gboolean ausschneiden;
    GPtrArray* arr_ref;
} Clipboard;

struct _SondTreeviewClass
{
    GtkTreeViewClass parent_class;
    Clipboard* clipboard;
    void (*render_text_cell) ( GtkTreeViewColumn*, GtkCellRenderer*, GtkTreeModel*,
            GtkTreeIter*, gpointer );
};


SondTreeview* sond_treeview_new( gint );

void sond_treeview_set_render_text_cell_func( SondTreeview*, void (*render_text_cell)
        ( GtkTreeViewColumn*, GtkCellRenderer*, GtkTreeModel*, GtkTreeIter*, gpointer ) );

void sond_treeview_set_id( SondTreeview*, gint );

gint sond_treeview_get_id( SondTreeview* );

GtkCellRenderer* sond_treeview_get_cell_renderer_icon( SondTreeview* );

GtkCellRenderer* sond_treeview_get_cell_renderer_text( SondTreeview* );

void sond_treeview_expand_row( SondTreeview*, GtkTreeIter* );

gboolean sond_treeview_get_cursor( SondTreeview*, GtkTreeIter* );

void sond_treeview_set_cursor( SondTreeview*, GtkTreeIter* );

void sond_treeview_set_cursor_on_text_cell( SondTreeview* stv, GtkTreeIter* iter );

gboolean sond_treeview_test_cursor_descendant( SondTreeview* );

void sond_treeview_copy_or_cut_selection( SondTreeview*, gboolean );

gint sond_treeview_clipboard_foreach( gint (*)
        ( SondTreeview*, GtkTreeIter*, gpointer, gchar** ), gpointer, gchar** );

gint sond_treeview_selection_foreach( SondTreeview*, gint (*)
        ( SondTreeview*, GtkTreeIter*, gpointer, gchar** ), gpointer, gchar** );


G_END_DECLS

#endif // SOND_TREEVIEW_H_INCLUDED
