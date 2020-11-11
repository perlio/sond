#ifndef FS_TREE_H_INCLUDED
#define FS_TREE_H_INCLUDED

typedef struct _GtkTreeIter GtkTreeIter;
typedef struct _GFile GFile;
typedef struct _GFileInfo GFileInfo;

typedef int gint;
typedef char gchar;
typedef int gboolean;

typedef struct _Projekt Projekt;

gchar* fs_tree_get_basename( Projekt*, GtkTreeIter* );

gchar* fs_tree_get_rel_path( Projekt*, GtkTreeIter* );

gchar* fs_tree_get_full_path( Projekt*, GtkTreeIter* );

gint fs_tree_dir_foreach( Projekt*, GFile*, gint (*) (Projekt*, GFile*, GFile*,
        GFileInfo*, gpointer, gchar**), gpointer, gchar**);

gint fs_tree_load_dir( Projekt*, GtkTreeIter*, gchar** );

GFile* fs_insert_dir( GFile*, gboolean, gchar** );

gint fs_tree_insert_dir( Projekt*, gboolean, gchar** );

gint fs_tree_create_sojus_zentral( Projekt*, gchar** );

gint fs_tree_remove_node( Projekt*, GFile*, GtkTreeIter*, gchar** );

#endif // FS_TREE_H_INCLUDED
