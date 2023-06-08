#ifndef OEFFNEN_H_INCLUDED
#define OEFFNEN_H_INCLUDED

#include "../global_types.h"

typedef int gint;
typedef char gchar;


gint oeffnen_datei( Projekt*, const gchar*, Anbindung*, PdfPos*, gchar** );

gint oeffnen_node( Projekt*, GtkTreeIter*, gchar** );


#endif // OEFFNEN_H_INCLUDED
