#ifndef BAUM_H_INCLUDED
#define BAUM_H_INCLUDED

#include "../enums.h"

typedef struct _GtkTreeIter GtkTreeIter;
typedef struct _GtkTreeView GtkTreeView;
typedef struct _Projekt Projekt;
typedef struct _GtkTreePath GtkTreePath;

typedef int gboolean;
typedef int gint;
typedef char gchar;


GtkTreeIter* baum_einfuegen_knoten( GtkTreeView*, GtkTreeIter*, gboolean );

gint baum_abfragen_parent_id( Projekt*, Baum, GtkTreeIter* );

gint baum_abfragen_older_sibling_id( Projekt*, Baum, GtkTreeIter* );

GtkTreeIter* baum_abfragen_aktuellen_cursor( GtkTreeView* );

gint baum_abfragen_aktuelle_node_id( GtkTreeView* );

Baum baum_abfragen_aktiver_treeview( Projekt* );

void baum_setzen_cursor( Projekt*, Baum, GtkTreeIter* );

void expand_row( Projekt*, Baum, GtkTreeIter* );

void expand_to_row( Projekt*, Baum, GtkTreeIter* );

gint baum_abfragen_node_id( GtkTreeView*, GtkTreePath*, gchar** );

GtkTreePath* baum_abfragen_path( GtkTreeView*, gint );

GtkTreeIter* baum_abfragen_iter( GtkTreeView*, gint );

Baum baum_get_baum_from_treeview( Projekt*, GtkWidget* );

#endif // BAUM_H_INCLUDED
