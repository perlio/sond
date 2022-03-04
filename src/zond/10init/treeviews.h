#ifndef TREEVIEWS_H_INCLUDED
#define TREEVIEWS_H_INCLUDED


typedef int gint;
typedef struct _Projekt Projekt;
typedef enum BAEUME Baum;

gint treeviews_get_baum_and_node_id( Projekt*, GtkTreeIter*, Baum*, gint* );

gint treeviews_entfernen_anbindung( Projekt*, Baum, gchar** );

gint treeviews_selection_loeschen( Projekt*, Baum, gchar** );

gint treeviews_change_icon_id( Projekt*, Baum, const gchar*, gchar** );

gint treeviews_node_text_nach_anbindung( Projekt*, Baum, gchar** );

gint treeviews_insert_node( Projekt*, Baum, gboolean, gchar** );

void treeviews_cb_editing_canceled( GtkCellRenderer*, gpointer );

void treeviews_cb_editing_started( GtkCellRenderer*, GtkEditable*, const gchar*, gpointer );

void init_treeviews( Projekt* );

void treeviews_init_fs_tree( Projekt* );

#endif // TREEVIEWS_H_INCLUDED
