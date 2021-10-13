#ifndef SOJUS_ADRESSEN_H_INCLUDED
#define SOJUS_ADRESSEN_H_INCLUDED

#include <time.h>

typedef struct _Hist
{
    gchar* von;
    gchar* bemerkung_von;
    gchar* bis;
    gchar* bemerkung_bis;
    gboolean act;
} Hist;

typedef struct _Value
{
    gchar* value;
    Hist hist;
} Value;

typedef struct _Adresse
{
    gchar* land;
    gchar* stadt;
    gchar* PLZ;
    gchar* strasse;
    gchar* hausnr;
    Hist hist;
} Adresse;

typedef struct _Sitz
{
    gint typ;
    GArray* telefonnummern; //Array von values
    gchar* durchwahl_zentrale;
    gchar* faxdurchwahl_zentrale;
    Hist hist;
}

typedef struct _Person
{
    gint typ;
    GArray* namen; //Array von Values
    GArray* vornamen;
    GArray* telefonnrn; //insb. Handynummern
}

typedef struct _Adresse
{
    Person person;
    GArray sitze;
    GArray arbeitsplaetze;


} Adresse;

void sojus_adressen_cb_fenster( GtkButton* , gpointer );


#endif // SOJUS_ADRESSEN_H_INCLUDED
