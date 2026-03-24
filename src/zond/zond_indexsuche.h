#ifndef ZOND_INDEXSUCHE_H_INCLUDED
#define ZOND_INDEXSUCHE_H_INCLUDED

#include <gtk/gtk.h>

typedef struct _Projekt Projekt;
typedef struct _SondTreeviewFM SondTreeviewFM;

void zond_indexsuche_activate(GtkMenuItem *item, gpointer data);

void zond_indexsuche_activate_with_selection(GtkMenuItem *item,
		GHashTable* ht_fileparts, gpointer data);

#endif /* ZOND_INDEXSUCHE_H_INCLUDED */
