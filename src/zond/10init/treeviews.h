#ifndef TREEVIEWS_H_INCLUDED
#define TREEVIEWS_H_INCLUDED


typedef struct _Projekt Projekt;

void treeviews_cb_editing_canceled( GtkCellRenderer*, gpointer );

void treeviews_cb_editing_started( GtkCellRenderer*, GtkEditable*, const gchar*, gpointer );

void init_treeviews( Projekt* );

void treeviews_init_fs_tree( Projekt* );

#endif // TREEVIEWS_H_INCLUDED
