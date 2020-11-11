#ifndef INIT_H_INCLUDED
#define INIT_H_INCLUDED

typedef struct _Projekt Projekt;
typedef void* gpointer;

typedef struct _GtkApplication GtkApplication;


void open_file( Projekt*, gpointer );

Projekt* init( GtkApplication* );

#endif // INIT_H_INCLUDED
