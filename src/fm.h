#ifndef FM_H_INCLUDED
#define FM_H_INCLUDED

typedef struct _GtkWidget GtkWidget;
typedef struct _GtkTreeView GtkTreeView;
typedef struct _GtkTreeIter GtkTreeIter;
typedef struct _GtkCellRenderer GtkCellRenderer;

typedef void* gpointer;
typedef int gboolean;
typedef char gchar;


void cb_fm_row_text_edited( GtkCellRenderer*, gchar*, gchar*, gpointer );

gint fm_datei_oeffnen( const gchar*, gchar** );

gint fm_load_dir( GtkTreeView*, GtkTreeIter*, gchar** );

GtkTreeView* fm_create_tree_view( GtkWidget*, void (*GCallback) );


#endif // FM_H_INCLUDED
