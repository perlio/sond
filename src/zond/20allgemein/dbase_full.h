#ifndef DBASE_FULL_H_INCLUDED
#define DBASE_FULL_H_INCLUDED

#define ERROR_DBASE_FULL(x) { if ( errmsg ) *errmsg = add_string( g_strconcat( "Bei Aufruf " x ":\n", \
                       sqlite3_errmsg(dbase_full->dbase.db), NULL ), *errmsg ); \
                       return -1; }

#include "../../dbase.h"

typedef struct sqlite3_stmt sqlite3_stmt;

typedef struct _DBase_Full
{
    DBase dbase;
    sqlite3_stmt* insert_node[5];
    sqlite3_stmt* set_node_text[2];
    sqlite3_stmt* stmts[25];

} DBaseFull;

gint dbase_full_insert_node( DBaseFull*, Baum, gint, gboolean, const gchar*,
        const gchar*, gchar** );

gint dbase_full_create( const gchar*, DBaseFull**, gboolean, gboolean, gchar** );

#endif // DBASE_FULL_H_INCLUDED
