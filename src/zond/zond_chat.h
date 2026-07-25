#ifndef ZOND_CHAT_H_INCLUDED
#define ZOND_CHAT_H_INCLUDED

#include <gtk/gtk.h>

typedef struct _Projekt Projekt;

/**
 * zond_chat_activate:
 *
 * Öffnet den Chat-Dialog: Frage eingeben, Antwort wird auf Basis einer
 * semantischen Suche über den Index (sond_index_semantic_search) plus
 * Generierung mit dem lokalen Chat-Modell (sond_chat_answer) erzeugt.
 * Analog zu zond_indexsuche_activate() aufgebaut/aufgerufen.
 */
void zond_chat_activate(GtkMenuItem *item, gpointer data);

#endif /* ZOND_CHAT_H_INCLUDED */
