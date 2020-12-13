#ifndef FS_TREE_H_INCLUDED
#define FS_TREE_H_INCLUDED

typedef struct _GtkTreeIter GtkTreeIter;
typedef struct _GFile GFile;
typedef struct _GFileInfo GFileInfo;

typedef int gint;
typedef char gchar;
typedef int gboolean;

typedef struct _Projekt Projekt;

gint fs_tree_dir_foreach( Projekt*, GFile*, gint (*) (Projekt*, GFile*, GFile*,
        GFileInfo*, gpointer, gchar**), gpointer, gchar**);

gint fs_tree_remove_node( Projekt*, GFile*, GtkTreeIter*, gchar** );

#endif // FS_TREE_H_INCLUDED
