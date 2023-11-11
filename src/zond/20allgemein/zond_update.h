#ifndef ZOND_UPDATE_H_INCLUDED
#define ZOND_UPDATE_H_INCLUDED

typedef int gint;
typedef struct _Projekt Projekt;
typedef struct _GError GError;


gint zond_update( Projekt*, GError** );


#endif // ZOND_UPDATE_H_INCLUDED
