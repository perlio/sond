#ifndef ZOND_GEMINI_H_INCLUDED
#define ZOND_GEMINI_H_INCLUDED

#include "global_types.h"

typedef char gchar;
typedef int gint;
typedef struct _Projekt Projekt;

typedef struct _Fundstelle
{
    gchar* dateipfad;
    Anbindung anbindung;
} Fundstelle;

gint zond_gemini_read_gemini( Projekt*, gchar** );

gint zond_gemini_select( Projekt*, gchar** );

#endif //ZOND_GEMINI_H_INCLUDED
