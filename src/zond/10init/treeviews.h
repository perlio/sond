#ifndef TREEVIEWS_H_INCLUDED
#define TREEVIEWS_H_INCLUDED


typedef int gint;
typedef struct _Projekt Projekt;
typedef enum BAEUME Baum;
typedef struct _Anbindung Anbindung;


void init_treeviews( Projekt* );

gint treeviews_hat_vorfahre_datei( Projekt*, Baum, gint, gboolean, gchar** );

gint treeviews_knoten_verschieben( Projekt*, Baum, gint, gint, gint, gchar** );

gint treeviews_get_baum_and_node_id( Projekt*, GtkTreeIter*, Baum*, gint* );

gint treeviews_get_rel_path_and_anbindung( Projekt*, Baum, gint, gchar**,
        Anbindung**, gchar** );

gint treeviews_entfernen_anbindung( Projekt*, Baum, gchar** );

gint treeviews_selection_loeschen( Projekt*, Baum, gchar** );

gint treeviews_change_icon_id( Projekt*, Baum, const gchar*, gchar** );

gint treeviews_node_text_nach_anbindung( Projekt*, Baum, gchar** );

gint treeviews_insert_node( Projekt*, Baum, gboolean, gchar** );

#endif // TREEVIEWS_H_INCLUDED
