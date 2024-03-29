#ifndef TREEVIEWS_H_INCLUDED
#define TREEVIEWS_H_INCLUDED


typedef int gint;
typedef char gchar;
typedef struct _Projekt Projekt;
typedef enum BAEUME Baum;
typedef struct _Anbindung Anbindung;
typedef struct _GtkTreeIter GtkTreeIter;
typedef int gboolean;

gint treeviews_hat_vorfahre_datei( Projekt*, Baum, gint, gboolean, gchar** );

Baum treeviews_get_baum_iter( Projekt*, GtkTreeIter* );

gint treeviews_get_baum_and_node_id( Projekt*, GtkTreeIter*, Baum*, gint* );

gint treeviews_get_rel_path_and_anbindung( Projekt*, Baum, gint, gchar**,
        Anbindung**, gchar** );

gboolean treeviews_get_anchor( Projekt*, gboolean, GtkTreeIter*, GtkTreeIter*, Baum*, gint* );

gint treeviews_selection_entfernen_anbindung( Projekt*, Baum, gchar** );

gint treeviews_selection_set_node_text( Projekt*, Baum, gchar** );

gint treeviews_db_to_baum( Projekt*, Baum, gint, GtkTreeIter*, gboolean,
        GtkTreeIter*, gchar** );

gint treeviews_load_node( Projekt*, gboolean, Baum, gint,
        GtkTreeIter*, gboolean, GtkTreeIter*, gchar** );

gint treeviews_reload_baeume( Projekt*, gchar** );

gint treeviews_knoten_verschieben( Projekt*, Baum, gint, gint, gint, gchar** );

gint treeviews_paste_clipboard_as_link( Projekt*, gboolean, GtkTreeIter*, GtkTreeIter*,
        Baum, gint, gchar** );

gint treeviews_clipboard_kopieren( Projekt*, gboolean, GtkTreeIter*, GtkTreeIter*,
        Baum, gint, gchar** );

gint treeviews_clipboard_verschieben( Projekt*, gboolean, GtkTreeIter*, GtkTreeIter*,
        Baum, gint, gchar** );

#endif // TREEVIEWS_H_INCLUDED
