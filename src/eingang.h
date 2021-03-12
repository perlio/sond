#ifndef EINGANG_H_INCLUDED
#define EINGANG_H_INCLUDED

typedef int gint;
typedef char gchar;
typedef struct _GDate GDate;
typedef struct _DBase DBase;
typedef struct _SondTreeviewFM SondTreeviewFM;

typedef struct _Eingang
{
    gchar* eingangsdatum;
    gchar* transport;
    gchar* traeger;
    gchar* ort;
    gchar* absender;
    gchar* absendedatum;
    gchar* erfassungsdatum;
} Eingang;

typedef struct _Eingang_DBase
{
    Eingang** eingang;
    DBase* dbase;
    gint* last_inserted_ID;
} EingangDBase;

void eingang_free( Eingang* );

gint eingang_for_rel_path( DBase*, const gchar*, gint*, Eingang**, gint*, gchar** );

gint eingang_set( SondTreeviewFM*, gchar** );

#endif // EINGANG_H_INCLUDED
