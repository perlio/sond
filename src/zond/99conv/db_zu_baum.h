#ifndef KOPIEREN_H_INCLUDED
#define KOPIEREN_H_INCLUDED

#include "../enums.h"

typedef struct _GtkTreeIter GtkTreeIter;
typedef struct _Projekt Projekt;

typedef int gint;
typedef int gboolean;
typedef char gchar;


gint db_baum_knoten( Projekt*, Baum, gint, GtkTreeIter*, gboolean,
        GtkTreeIter*, gchar** );

gint db_baum_knoten_mit_kindern( Projekt*, gboolean, Baum, gint,
        GtkTreeIter*, gboolean, GtkTreeIter*, gchar** );

gint db_baum_refresh( Projekt*, gchar** );

#endif // KOPIEREN_H_INCLUDED
