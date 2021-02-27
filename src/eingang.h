#ifndef EINGANG_H_INCLUDED
#define EINGANG_H_INCLUDED

typedef int gint;
typedef char gchar;
typedef struct _GDate GDate;
typedef struct _DBase DBase;

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
} EingangDBase;

void eingang_free( Eingang* );

Eingang* eingang_new( void );

gint eingang_for_rel_path( DBase*, const gchar*, gint*, Eingang**, gint*, gchar** );

gint  eingang_set( GtkTreeView*, GtkTreeIter*, gpointer, gchar** );

#endif // EINGANG_H_INCLUDED
