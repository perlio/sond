#ifndef SELECTION_H_INCLUDED
#define SELECTION_H_INCLUDED

#include "../enums.h"

typedef struct _Projekt Projekt;
typedef struct _GList GList;

typedef int gint;
typedef char gchar;
typedef int gboolean;


gboolean selection_anchor_no_link( GtkTreeIter*, gboolean, gint* );

void selection_paste( Projekt*, gboolean, gboolean );

#endif // SELECTION_H_INCLUDED
