#ifndef ZOND_DBASE_H_INCLUDED
#define ZOND_DBASE_H_INCLUDED

#include <glib-object.h>
#include "global_types.h"

#define ZOND_DBASE_VERSION "v0.10"

#define ERROR_ZOND_DBASE(x) { if ( errmsg ) *errmsg = add_string( g_strconcat( "Bei Aufruf ", __func__, ":\n", \
                       "Bei Aufruf " x ":\n", \
                       sqlite3_errmsg(zond_dbase_get_dbase(zond_dbase)), NULL ), *errmsg ); \
                       return -1; }

#define ERROR_ROLLBACK(zond_dbase) \
          { if ( errmsg ) *errmsg = add_string( \
            g_strconcat( "Bei Aufruf ", __func__, ":\n", NULL ), *errmsg ); \
            \
            gchar* err_rollback = NULL; \
            gint rc_rollback = 0; \
            rc_rollback = zond_dbase_rollback( zond_dbase, &err_rollback ); \
            if ( errmsg ) \
            { \
                if ( !rc_rollback ) *errmsg = add_string( *errmsg, \
                        g_strdup( "\n\nRollback durchgeführt" ) ); \
                else *errmsg = add_string( *errmsg, g_strconcat( "\n\nRollback " \
                        "fehlgeschlagen\n\nBei Aufruf dbase_rollback:\n", \
                        err_rollback, "\n\nDatenbankverbindung trennen", NULL ) ); \
            } \
            g_free( err_rollback ); \
            \
            return -1; }

G_BEGIN_DECLS

#define ZOND_TYPE_DBASE zond_dbase_get_type( )
G_DECLARE_DERIVABLE_TYPE (ZondDBase, zond_dbase, ZOND, DBASE, GObject)

struct _ZondDBaseClass
{
    GObjectClass parent_class;
};


gint zond_dbase_new( const gchar*, gboolean, gboolean, ZondDBase**, gchar** );

void zond_dbase_close( ZondDBase* );

sqlite3* zond_dbase_get_dbase( ZondDBase* );

const gchar* zond_dbase_get_path( ZondDBase* );

gint zond_dbase_backup( ZondDBase*, ZondDBase*, gchar** );

gint zond_dbase_prepare( ZondDBase*, const gchar*, const gchar**,gint,
        sqlite3_stmt***, gchar** );

gint zond_dbase_begin( ZondDBase*, gchar** );

gint zond_dbase_commit( ZondDBase*, gchar** );

gint zond_dbase_rollback( ZondDBase*, gchar** );

gint zond_dbase_insert_node( ZondDBase*, Baum, gint, gboolean, const gchar*,
        const gchar*, gchar** );

gint zond_dbase_remove_node( ZondDBase*, Baum, gint, gchar** );

gint zond_dbase_kopieren_nach_auswertung( ZondDBase*, Baum, gint, gint, gboolean,
        gchar** );

gint zond_dbase_verschieben_knoten( ZondDBase*, Baum, gint, gint, gint, gchar** );

gint zond_dbase_set_icon_name( ZondDBase*, Baum, gint, const gchar*, gchar** );

gint zond_dbase_get_icon_name_and_node_text( ZondDBase*, Baum, gint, gchar**,
        gchar**, gchar** );

gint zond_dbase_set_text( ZondDBase*, gint, gchar*, gchar** );

gint zond_dbase_get_text( ZondDBase*, gint, gchar**, gchar** );

gint zond_dbase_set_ziel( ZondDBase*, Ziel*, gint, gchar** );

gint zond_dbase_set_datei( ZondDBase*, gint, const gchar*, gchar** );

gint zond_dbase_get_parent( ZondDBase*, Baum, gint, gchar** );

gint zond_dbase_get_older_sibling( ZondDBase*, Baum, gint, gchar** );

gint zond_dbase_get_younger_sibling( ZondDBase*, Baum, gint, gchar** );

gint zond_dbase_get_first_child( ZondDBase*, Baum, gint, gchar** );

gint zond_dbase_get_ref_id( ZondDBase*, gint, gchar** );

gint zond_dbase_get_rel_path( ZondDBase*, Baum, gint, gchar**, gchar** );

gint zond_dbase_get_ziel( ZondDBase*, Baum, gint, Ziel*, gchar** );

gint zond_dbase_get_node_id_from_rel_path( ZondDBase*, const gchar*, gchar** );

gint zond_dbase_check_id( ZondDBase*, const gchar*, gchar** );

gint zond_dbase_set_node_text( ZondDBase*, Baum, gint, const gchar*, gchar** );

gint zond_dbase_set_link( ZondDBase*, const gint, const gint, const gchar*,
        const gint, const gint, gchar** );

gint zond_dbase_check_link( ZondDBase*, Baum, gint, gchar** );

gint zond_dbase_get_link( ZondDBase*, gint, Baum*, gint*, gchar**, Baum*, gint*,
        gchar** );

gint zond_dbase_remove_link( ZondDBase*, const gint, const gint, gchar** );

gint zond_dbase_update_path( ZondDBase*, const gchar*, const gchar*, gchar** );

gint zond_dbase_test_path( ZondDBase*, const gchar*, gchar** );

G_END_DECLS

#endif // ZOND_DBASE_H_INCLUDED



