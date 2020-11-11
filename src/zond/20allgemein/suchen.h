#ifndef SUCHEN_H_INCLUDED
#define SUCHEN_H_INCLUDED

typedef struct _Projekt Projekt;

typedef char gchar;


gint suchen_path( Projekt*, const gchar*, gchar** );

gint suchen_text( Projekt*, const gchar*, gchar** );

gint suchen_node_text( Projekt*, const gchar*, gchar** );

#endif // SUCHEN_H_INCLUDED
