#ifndef DB_H_INCLUDED
#define DB_H_INCLUDED

#include "../enums.h"

typedef struct _Projekt Projekt;

typedef int gint;
typedef char gchar;
typedef int gboolean;


gint db_remove_node( Projekt*, Baum, gint, gchar** );

gint db_verschieben_knoten( Projekt*, Baum, gint, gint, gint, gchar** );

gint db_kopieren_nach_auswertung( Projekt*, Baum, gint, gint, gboolean, gchar** );

#endif // DB_H_INCLUDED
