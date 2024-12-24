#ifndef AKTENBET_H_INCLUDED
#define AKTENBET_H_INCLUDED

typedef struct _GtkWidget GtkWidget;
typedef struct _GPtrArray GPtrArray;

typedef struct _Aktenbet Aktenbet;

typedef void *gpointer;
typedef int gint;

GPtrArray* aktenbet_oeffnen(GtkWidget*, gint, gint);

void aktenbet_free(gpointer);

void aktenbet_speichern(GtkWidget*);

Aktenbet* aktenbet_einlesen(GtkWidget*);

Aktenbet* aktenbet_neu(void);

#endif // AKTENBET_H_INCLUDED
