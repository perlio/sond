#ifndef FM_H_INCLUDED
#define FM_H_INCLUDED

typedef struct _GFile GFile;
typedef struct _GtkWidget GtkWidget;
typedef struct _GtkTreeView GtkTreeView;
typedef struct _GtkTreeIter GtkTreeIter;
typedef struct _GtkCellRenderer GtkCellRenderer;
typedef struct _GPtrArray GPtrArray;

typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;

typedef void* gpointer;
typedef int gboolean;
typedef char gchar;
typedef int gint;
typedef struct _Clipboard Clipboard;

typedef struct _Modify_File
{
    gint (* before_move) ( const GFile*, const GFile*, gpointer, gchar** );
    gint (* after_move) ( const gint, gpointer, gchar** );
    gint (* test) ( const GFile*, gpointer, gchar** );
    gpointer data;
} ModifyFile;

gchar* fm_get_rel_path_from_file( const gchar*, const GFile* );

gchar* fm_get_full_path( GtkTreeView*, GtkTreeIter* );

gchar* fm_get_rel_path( GtkTreeView*, GtkTreeIter* );

gint fm_create_dir( GtkTreeView*, gboolean, gchar** );

gint fm_paste_selection( GtkTreeView*, GtkTreeView*, GPtrArray*, gboolean, gboolean, gchar** );

gint fm_foreach_loeschen( GtkTreeView*, GtkTreeIter*, gpointer, gchar** );

GtkWidget* fm_create_tree_view( Clipboard*, ModifyFile* );

void fm_unset_root( GtkTreeView* );

gint fm_set_root( GtkTreeView*, const gchar*, gchar** );

#endif // FM_H_INCLUDED
