#ifndef EINGANG_H_INCLUDED
#define EINGANG_H_INCLUDED

typedef int gint;
typedef char gchar;
typedef struct _GDate GDate;

typedef struct _Eingang
{
    gchar* rel_path;
    GDate* eingangsdatum;
    gchar* transport;
    gchar* traeger;
    gchar* ort;
    gchar* absender;
    GDate* absendedatum;
    GDate* erfassungsdatum;
} Eingang;


void eingang_free( Eingang* );

Eingang* eingang_new( void );

#endif // EINGANG_H_INCLUDED
