#ifndef OEFFNEN_H_INCLUDED
#define OEFFNEN_H_INCLUDED

#include "../global_types.h"

typedef int gint;
typedef char gchar;


gint oeffnen_datei( Projekt*, const gchar*, Anbindung*, PdfPos*, gchar** );

gint oeffnen_node( Projekt*, GtkTreeIter*, gchar** );

gint oeffnen_actual_node( Projekt*, gchar** );

#endif // OEFFNEN_H_INCLUDED
