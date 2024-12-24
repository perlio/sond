#ifndef AKTE_AUSWAHL_NEU_H_INCLUDED
#define AKTE_AUSWAHL_NEU_H_INCLUDED

typedef struct _GtkWidget GtkWidget;

typedef struct _Akte Akte;

typedef int gint;

void akte_free(Akte*);

Akte* akte_oeffnen(Sojus*, gint, gint);

void akte_speichern(GtkWidget*);

#endif // AKTE_AUSWAHL_NEU_H_INCLUDED
