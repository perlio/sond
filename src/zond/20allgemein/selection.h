#ifndef SELECTION_H_INCLUDED
#define SELECTION_H_INCLUDED

#include "../enums.h"

typedef struct _Projekt Projekt;
typedef struct _GList GList;

typedef int gint;
typedef char gchar;
typedef int gboolean;


gint three_treeviews_paste_clipboard( Projekt*, gboolean, gboolean, gchar** );

#endif // SELECTION_H_INCLUDED
