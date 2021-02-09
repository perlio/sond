#ifndef DB_STRUKTUR_H_INCLUDED
#define DB_STRUKTUR_H_INCLUDED

#include "../enums.h"

typedef struct _Projekt Projekt;
typedef struct _Ziel Ziel;

typedef int gint;
typedef char gchar;


gint db_get_parent( Projekt*, Baum, gint, gchar** );

gint db_get_older_sibling( Projekt*, Baum, gint, gchar** );

gint db_get_younger_sibling( Projekt*, Baum, gint, gchar** );

gint db_get_first_child( Projekt*, Baum, gint, gchar** );

gint db_get_icon_name_and_node_text( Projekt*, Baum, gint, gchar**, gchar**, gchar** );

gint db_get_ref_id( Projekt*, gint, gchar** );

gint db_get_rel_path( Projekt*, Baum, gint, gchar**, gchar** );

gint db_get_ziel( Projekt*, Baum, gint, Ziel**, gchar** );

gint db_knotentyp_abfragen( Projekt*, Baum, gint, gchar** );

gint db_get_text( Projekt*, gint, gchar**, gchar** );

gint db_get_node_id_from_rel_path( Projekt*, const gchar*, gchar** );

gint db_check_id( Projekt*, const gchar*, gchar** );

#endif // DB_STRUKTUR_H_INCLUDED
