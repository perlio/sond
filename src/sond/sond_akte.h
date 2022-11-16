#ifndef SOND_AKTE_H_INCLUDED
#define SOND_AKTE_H_INCLUDED

#include <glib.h>
#include <mysql.h>
#include "sond_person.h"


typedef struct _Akte_Lebenszeit
{
    GDateTime* von;
    GDateTime* bis;
    gint ablagenr;
} AkteLebenszeit;

typedef struct _Akte_Beteiligter
{
    PersonKurz* person_kurz;
    gint code_betart; //property der rel _hat_ zwischen akte und person
    GPtrArray* arr_betreffs; //sind properties der rel akte _hat_ person; vielleicht zur Markieruhg der Reihenfolge Ziffer voranstellen?!
} AkteBeteiligter;

typedef struct _Akte_Sachbearbeiter
{
    gint id; //
    PersonKurz* person_kurz; //sb _ist_ person
    gchar* sb_kuerzel; //ist property des entity "Sachbearbeiter
    GDateTime* von;
    GDateTime* bis;
} AkteSachbearbeiter;

typedef struct _Akte_Kurz
{
    gint id;
    gint regjahr;
    gint regnr;
    gchar* bezeichnung;
    gchar* gegenstand;
} AkteKurz;

typedef struct _Akte
{
    AkteKurz* akte_kurz;
    AkteSachbearbeiter* akte_sachbearbeiter; //akte _hat_ sachbearbeiter
    GPtrArray* arr_beteiligte; //akte _hat_ person
    GPtrArray* arr_lebenszeiten; //von/bis sind properties von akte; ablagenr ist property von bis
} Akte;


void akte_free( Akte* );

Akte* akte_new( );

Akte* akte_load( MYSQL*, gint, gint );

gint akte_save( MYSQL*, gint, gint );

JsonNode* akte_serialize( Akte* );

Akte* akte_deserialize( JsonNode* );

GPtrArray* akte_liste_load( MYSQL*, const gchar* );

JsonNode* akte_liste_serialize( GPtrArray* );

#endif //SOND_AKTE_H_INCLUDED
