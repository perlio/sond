#ifndef APP_WINDOW_H_INCLUDED
#define APP_WINDOW_H_INCLUDED

#include "../zond_init.h" /* Baum */

typedef struct _Projekt Projekt;

void init_app_window(Projekt*);

/* Baum, für den eine Aktion gilt: der Baum mit dem Fokus im App-Fenster
 * (auch wenn gerade ein anderes Fenster aktiv ist), bei geöffnetem
 * Menü-Popover der zuletzt fokussierte Baum, sonst KEIN_BAUM (z.B. Fokus im
 * Textfeld oder beim Umbenennen im Baum). */
Baum zond_baum_aktuell(Projekt*);

#endif // APP_WINDOW_H_INCLUDED
