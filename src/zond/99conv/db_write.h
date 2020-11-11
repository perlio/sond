#ifndef DB_H_INCLUDED
#define DB_H_INCLUDED

#include "../enums.h"

typedef struct _Projekt Projekt;

typedef int gint;
typedef char gchar;
typedef int gboolean;


gint db_begin( Projekt*, gchar** );

gint db_begin_both( Projekt*, gchar** );

gint db_commit( Projekt*, gchar** );

gint db_commit_both( Projekt*, gchar** );

gint db_rollback( Projekt*, gchar** );

gint db_rollback_both( Projekt*, gchar** );

gint db_remove_node( Projekt*, Baum, gint, gchar** );

gint db_insert_node( Projekt*, Baum, gint, gboolean, const gchar*, const gchar*, gchar** );

gint db_set_datei( Projekt*, gint, gchar*, gchar** );

gint db_verschieben_knoten( Projekt*, Baum, gint, gint, gint, gchar** );

gint db_kopieren_nach_auswertung( Projekt*, Baum, gint, gint, gboolean, gchar** );

gint db_kopieren_nach_auswertung_mit_kindern( Projekt*, gboolean, Baum,
        gint, gint, gboolean, gchar** );

gint db_set_node_text( Projekt*, Baum, gint, const gchar*, gchar** );

gint db_set_icon_id( Projekt*, Baum, gint, const gchar*, gchar** );

gint db_speichern_textview( Projekt*, gint, gchar*, gchar** );

gint db_update_path( Projekt*, const gchar*, const gchar*, gchar** );

#endif // DB_H_INCLUDED
