#ifndef FM_H_INCLUDED
#define FM_H_INCLUDED


typedef struct _GFile GFile;
typedef struct _GtkWidget GtkWidget;
typedef struct _GtkTreeView GtkTreeView;
typedef struct _GtkTreeViewColumn GtkTreeViewColumn;
typedef struct _GtkCellRenderer GtkCellRenderer;
typedef struct _GtkTreeModel GtkTreeModel;
typedef struct _GtkTreeIter GtkTreeIter;
typedef struct _GPtrArray GPtrArray;

typedef void* gpointer;
typedef int gboolean;
typedef char gchar;
typedef int gint;
typedef struct _Clipboard Clipboard;
typedef struct _DBase DBase;

typedef struct _FM FM;

typedef struct _Modify_File
{
    gint (* before_move) ( const gchar*, const GFile*, const GFile*, gpointer, gchar** );
    gint (* after_move) ( const gchar*, const gint, gpointer, gchar** );
    gint (* test) ( const gchar*, const GFile*, gpointer, gchar** );
    gpointer data;
} ModifyFile;

struct _FM
{
    GtkTreeView* fm_treeview;
    GtkCellRenderer* cell_filename;
    GtkTreeViewColumn* column_eingang;
    gchar* root;
    Clipboard* clipboard;
    gpointer app_context;
    ModifyFile* modify_file;
};

void fm_destroy( FM* );

gchar* fm_get_rel_path_from_file( const gchar*, const GFile* );

gchar* fm_get_rel_path( GtkTreeModel*, GtkTreeIter* );

gchar* fm_get_full_path( FM*, GtkTreeIter* );

gint fm_set_eingang( FM*, DBase*, gchar** );

gint fm_create_dir( FM*, gboolean, gchar** );

gint fm_paste_selection( FM*, GtkTreeView*, GPtrArray*, gboolean, gboolean, gchar** );

gint fm_foreach_loeschen( GtkTreeView*, GtkTreeIter*, gpointer, gchar** );

void fm_remove_column_eingang( FM* );

void fm_add_column_eingang( FM*, DBase* );

FM* fm_create( void );

void fm_unset_root( FM* );

gint fm_set_root( FM*, const gchar*, gchar** );

#endif // FM_H_INCLUDED
