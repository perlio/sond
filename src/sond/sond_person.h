#ifndef SOND_PERSON_H_INCLUDED
#define SOND_PERSON_H_INCLUDED

#include <glib.h>

typedef struct _Person_Kurz {
	gint id; //id der Person (object der rel!)
	gchar *name; //gemeint: die property "name" der Person
	gchar *vorname;
} PersonKurz;

void person_kurz_free(PersonKurz*);

#endif // SOND_PERSON_H_INCLUDED
