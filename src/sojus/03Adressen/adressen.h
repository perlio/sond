#ifndef ADRESSEN_DB_H_INCLUDED
#define ADRESSEN_DB_H_INCLUDED

typedef struct _GtkWidget GtkWidget;

typedef struct _Adresse Adresse;

typedef int gint;

Adresse* adresse_oeffnen(GtkWidget*, gint);

void adresse_free(Adresse*);

void adresse_speichern(GtkWidget*);

#endif // ADRESSEN_DB_H_INCLUDED
