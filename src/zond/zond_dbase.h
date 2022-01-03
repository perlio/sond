#ifndef ZOND_DBASE_H_INCLUDED
#define ZOND_DBASE_H_INCLUDED

#include <glib-object.h>

#define ZOND_DBASE_VERSION "v0.10"

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




G_END_DECLS

#endif // ZOND_DBASE_H_INCLUDED



