#ifndef OEFFNEN_H_INCLUDED
#define OEFFNEN_H_INCLUDED

#include "../global_types.h"

typedef int gint;
typedef char gchar;


gint oeffnen_internal_viewer( Projekt*, const gchar*, Anbindung*, const PdfPos*, gchar** );

gint oeffnen_node( Projekt*, GtkTreeIter*, gboolean, gchar** );


#endif // OEFFNEN_H_INCLUDED
