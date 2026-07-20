#ifndef SUCHEN_H_INCLUDED
#define SUCHEN_H_INCLUDED

typedef struct _Projekt Projekt;

typedef char gchar;
typedef struct _GError GError;

gint suchen_treeviews(Projekt*, const gchar*, GError**);

#endif // SUCHEN_H_INCLUDED
