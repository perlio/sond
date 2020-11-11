#ifndef TREEVIEW_H_INCLUDED
#define TREEVIEW_H_INCLUDED

typedef struct _GtkTreeSelection GtkTreeSelection;
typedef struct _GtkTreeModel GtkTreeModel;
typedef struct _GtkTreePath GtkTreePath;
typedef struct _GtkTreeView GtkTreeView;
typedef struct _GPtrArray GPtrArray;
typedef struct _GtkCellRenderer GtkCellRenderer;

typedef int gboolean;
typedef void* gpointer;
typedef int gint;

typedef struct _Clipboard Clipboard;


void treeview_zelle_ausgrauen( GtkTreeView*, GtkTreePath*, GtkCellRenderer*,
        Clipboard* );

void treeview_underline_cursor( GtkTreeView*, GtkTreePath*, GtkCellRenderer* );

gboolean treeview_selection_select_func( GtkTreeSelection*, GtkTreeModel*, GtkTreePath*,
        gboolean, gpointer );

gint treeview_selection_testen_cursor_ist_abkoemmling( GtkTreeView*, GPtrArray* );

GPtrArray* treeview_selection_get_refs( GtkTreeView* );


#endif // TREEVIEW_H_INCLUDED
