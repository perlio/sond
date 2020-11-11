#ifndef SELECTION_H_INCLUDED
#define SELECTION_H_INCLUDED

#include "../enums.h"

typedef struct _Projekt Projekt;
typedef struct _GList GList;

typedef int gint;
typedef char gchar;
typedef int gboolean;


void selection_paste( Projekt*, gboolean );

gint selection_entfernen_anbindung( Projekt*, gchar** );

void selection_loeschen( Projekt* );

gint selection_change_icon_id( Projekt*, const gchar* );

void selection_copy_or_cut( Projekt*, gboolean );

#endif // SELECTION_H_INCLUDED
