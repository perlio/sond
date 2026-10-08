#ifndef EXPORT_H_INCLUDED
#define EXPORT_H_INCLUDED

#include <glib.h>

typedef struct _Projekt Projekt;

//Export-Dialog, Auswahl und Schreiben; 0 auch bei Abbruch durch den Nutzer
gint export_activate(Projekt* zond, GError** error);

#endif // EXPORT_H_INCLUDED
