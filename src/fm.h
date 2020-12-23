#ifndef FM_H_INCLUDED
#define FM_H_INCLUDED

typedef struct _GFile GFile;
typedef struct _GtkWidget GtkWidget;
typedef struct _GtkTreeView GtkTreeView;
typedef struct _GtkTreeIter GtkTreeIter;
typedef struct _GtkCellRenderer GtkCellRenderer;

typedef void* gpointer;
typedef int gboolean;
typedef char gchar;

typedef struct _S_FM_Change_Path
{
    gint (* before) ( const GFile*, const GFile*, gpointer, gchar** );
    gint (* after) ( const gint, gpointer, gchar** );
    gpointer data;
} SFMChangePath;

typedef struct _S_FM_Remove
{
    gint (* fm_test) ( GFile*, gpointer, gchar** );
    gpointer data;
} SFMRemove;



gchar* fm_get_full_path( GtkTreeView*, GtkTreeIter* );

gchar* fm_get_rel_path( GtkTreeView*, GtkTreeIter* );

gint fm_create_dir( GtkTreeView*, gboolean, gchar** );

void cb_fm_row_text_edited( GtkCellRenderer*, gchar*, gchar*, gpointer );

gint fm_datei_oeffnen( const gchar*, gchar** );

gint fm_paste_selection( GtkTreeView*, GPtrArray*, gboolean, gboolean, gpointer,
        gchar** );

gint fm_foreach_loeschen( GtkTreeView*, GtkTreeIter*, gpointer, gchar** );

gint fm_load_dir( GtkTreeView*, GtkTreeIter*, gchar** );

GtkTreeView* fm_create_tree_view( GtkWidget*, SFMChangePath* );


#endif // FM_H_INCLUDED
