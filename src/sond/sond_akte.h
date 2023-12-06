#ifndef SOND_AKTE_H_INCLUDED
#define SOND_AKTE_H_INCLUDED

#include <glib.h>
#include <mysql.h>
#include "sond_person.h"

#define SOND_AKTE_ERROR sond_akte_error_quark()
G_DEFINE_QUARK(sond-akte-error-quark,sond_akte_error)


enum SondAkteError
{
    SOND_AKTE_ERROR_PARSEJSON,
    NUM_SOND_AKTE_ERROR
};

typedef struct _AkteLebenszeit
{
    GDateTime* von;
    GDateTime* bis;
    gint ablagenr;
} AkteLebenszeit;

typedef struct _AkteBeteiligter
{
    PersonKurz* person_kurz;
    gint code_betart; //property der rel _hat_ zwischen akte und person
    GPtrArray* arr_betreffs; //sind properties der rel akte _hat_ person; vielleicht zur Markieruhg der Reihenfolge Ziffer voranstellen?!
} AkteBeteiligter;

typedef struct _AkteSachbearbeiter
{
    gint id; //
    PersonKurz* person_kurz; //sb _ist_ person
    gchar* sb_kuerzel; //ist property des entity "Sachbearbeiter
    GDateTime* von;
    GDateTime* bis;
} AkteSachbearbeiter;

typedef struct _SondAkte
{
    gint ID_entity;
    gint reg_jahr;
    gint reg_nr;
    gchar* aktenrubrum;
    gchar* aktenkurzbez;
} SondAkte;

/*
typedef struct _Akte
{
    AkteKurz* akte_kurz;
    AkteSachbearbeiter* akte_sachbearbeiter; //akte _hat_ sachbearbeiter
    GPtrArray* arr_beteiligte; //akte _hat_ person
    GPtrArray* arr_lebenszeiten; //von/bis sind properties von akte; ablagenr ist property von bis
} Akte;


void akte_kurz_free( AkteKurz* );

AkteKurz* akte_kurz_new( void );
*/

void sond_akte_free( SondAkte* );

SondAkte* sond_akte_new( );

SondAkte* sond_akte_new_from_json( const gchar*, GError** );

JsonObject* sond_akte_to_json_object( SondAkte* );

gchar* sond_akte_to_json_string( SondAkte* );

#endif //SOND_AKTE_H_INCLUDED
