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


Baum baum_abfragen_aktiver_treeview( Projekt* );

#endif // BAUM_H_INCLUDED
