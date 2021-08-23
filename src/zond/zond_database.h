#ifndef ZOND_DATABASE_H_INCLUDED
#define ZOND_DATABASE_H_INCLUDED

typedef struct _Projekt Projekt;
typedef int gint;
typedef char gchar;

gint zond_database_insert_anbindung( Projekt*, gint, gchar** );


#endif //ZOND_DATABASE_H_INCLUDED
