#ifndef ADRESSEN_DB_H_INCLUDED
#define ADRESSEN_DB_H_INCLUDED

Adresse* adresse_oeffnen( GtkWidget*, gint );

void adresse_free( Adresse* );

void adresse_speichern( GtkWidget* );

#endif // ADRESSEN_DB_H_INCLUDED
