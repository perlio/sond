#ifndef ZOND_DBASE_H_INCLUDED
#define ZOND_DBASE_H_INCLUDED

#include <glib-object.h>
#include "global_types.h"

#define ZOND_DBASE_VERSION "v0.9"

#define ERROR_ZOND_DBASE(x) { if ( errmsg ) *errmsg = add_string( g_strconcat( "Bei Aufruf " x ":\n", \
                       sqlite3_errmsg(zond_dbase_get_dbase(zond_dbase)), NULL ), *errmsg ); \
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

gint zond_dbase_insert_node( ZondDBase*, Baum, gint, gboolean, const gchar*,
        const gchar*, gchar** );

gint zond_dbase_remove_node( ZondDBase*, Baum, gint, gchar** );

gint zond_dbase_kopieren_nach_auswertung( ZondDBase*, Baum, gint, gint, gboolean,
        gchar** );

gint zond_dbase_verschieben_knoten( ZondDBase*, Baum, gint, gint, gint, gchar** );

G_END_DECLS

#endif // ZOND_DBASE_H_INCLUDED



