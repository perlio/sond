#ifndef TREEVIEW_H_INCLUDED
#define TREEVIEW_H_INCLUDED

typedef struct _GtkTreeIter GtkTreeIter;
typedef struct _GtkTreeSelection GtkTreeSelection;
typedef struct _GtkTreeModel GtkTreeModel;
typedef struct _GtkTreePath GtkTreePath;
typedef struct _GtkTreeView GtkTreeView;
typedef struct _GPtrArray GPtrArray;
typedef struct _GtkCellRenderer GtkCellRenderer;

typedef int gboolean;
typedef void* gpointer;
typedef int gint;

typedef struct _Clipboard {
    GtkTreeView* tree_view;
    gboolean ausschneiden;
    GPtrArray* arr_ref;
} Clipboard;


Clipboard* treeview_init_clipboard( void );

void treeview_expand_row( GtkTreeView*, GtkTreeIter* );

GtkTreeIter* treeview_insert_node( GtkTreeView*, GtkTreeIter*, gboolean );

GtkTreeIter* treeview_get_cursor( GtkTreeView* );

void  treeview_set_cursor( GtkTreeView*, GtkTreeIter* );

void treeview_zelle_ausgrauen( GtkTreeView*, GtkTreePath*, GtkCellRenderer*,
        Clipboard* );

void treeview_underline_cursor( GtkTreeView*, GtkTreePath*, GtkCellRenderer* );

gboolean treeview_selection_select_func( GtkTreeSelection*, GtkTreeModel*, GtkTreePath*,
        gboolean, gpointer );

gint treeview_selection_foreach( GtkTreeView*, GPtrArray*, gint (*)
        ( GtkTreeView*, GtkTreeIter*, gpointer, gchar** ), gpointer, gchar** );

gint treeview_selection_testen_cursor_ist_abkoemmling( GtkTreeView*, GPtrArray* );

GPtrArray* treeview_selection_get_refs( GtkTreeView* );

void treeview_copy_or_cut_selection( GtkTreeView*, Clipboard*, gboolean );

#endif // TREEVIEW_H_INCLUDED
