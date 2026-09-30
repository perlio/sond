#ifndef SUCHEN_H_INCLUDED
#define SUCHEN_H_INCLUDED

typedef struct _Projekt Projekt;

typedef char gchar;
typedef struct _GError GError;

gint suchen_treeviews(Projekt*, const gchar*, GError**);

typedef struct _GtkTreeIter GtkTreeIter;

//Sprung zu iter in BAUM_INHALT/BAUM_AUSWERTUNG (baum)
void suchen_springe_zu_iter(Projekt*, gint, GtkTreeIter*);

//Sprung zu Knoten node_id in BAUM_INHALT/BAUM_AUSWERTUNG (baum)
void suchen_springe_zu_knoten(Projekt*, gint, gint);

//Sprung zu file_part/section in BAUM_FS
void suchen_springe_zu_baum_fs(Projekt*, gchar const*, gchar const*);

#endif // SUCHEN_H_INCLUDED
