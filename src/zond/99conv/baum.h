#ifndef BAUM_H_INCLUDED
#define BAUM_H_INCLUDED

#include "../enums.h"

typedef struct _GtkTreeIter GtkTreeIter;
typedef struct _SondTreeview SondTreeview;
typedef struct _Projekt Projekt;
typedef struct _GtkTreePath GtkTreePath;
typedef struct _GtkTreeModel GtkTreeModel;

typedef int gboolean;
typedef int gint;
typedef char gchar;


gint baum_abfragen_aktuelle_node_id( SondTreeview* );

Baum baum_abfragen_aktiver_treeview( Projekt* );

GtkTreePath* baum_abfragen_path( SondTreeview*, gint );

GtkTreeIter* baum_abfragen_iter( SondTreeview*, gint );

#endif // BAUM_H_INCLUDED
