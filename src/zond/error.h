#ifndef ERROR_H_INCLUDED
#define ERROR_H_INCLUDED

typedef char gchar;

gchar* add_string( gchar*, gchar* );
gchar* prepend_string( gchar*, gchar* );


//Fehlerbehandlung
#define ERROR_PAO(x) { if ( errmsg ) *errmsg = add_string( \
                       g_strdup( "Bei Aufruf " x ":\n" ), *errmsg ); \
                       return -1; }

#define ERROR_PAO_R(x,y) { if ( errmsg ) *errmsg = add_string( \
                       g_strdup( "Bei Aufruf " x ":\n" ), *errmsg ); \
                       return y; }

#define ERROR_SQL(x) { if ( errmsg ) *errmsg = add_string( g_strconcat( "Bei Aufruf " x ":\n", \
                       sqlite3_errmsg(zond->dbase_zond->dbase_work->dbase.db), NULL ), *errmsg ); \
                       return -1; }

#define ERROR_SQL_R(x,y) { if ( errmsg ) *errmsg = add_string( g_strconcat( "Bei Aufruf " x ":\n", \
                       sqlite3_errmsg(zond->dbase_zond->dbase_work->dbase.db), NULL ), *errmsg ); \
                       return y; }

#define ERROR_MUPDF(x) { if ( errmsg ) *errmsg = add_string( *errmsg, g_strconcat( \
                        "Bei Aufruf " x ":\n", fz_caught_message( ctx ), NULL ) ); \
                         return -1; }

#define ERROR_MUPDF_R(x,y) { if ( errmsg ) *errmsg = add_string( g_strconcat( \
                        "Bei Aufruf " x ":\n", fz_caught_message( ctx ), NULL ), *errmsg ); \
                         return y; }

#define ERROR_THREAD(x) { fprintf( stderr, "Thread error: %s\n", x); \
                          fz_drop_context( ctx ); return; }

#define ERROR_MUPDF_CTX(x,y) { if ( errmsg ) *errmsg = add_string( *errmsg, g_strconcat( \
                        "Bei Aufruf " x ":\n", fz_caught_message( y ), NULL ) ); \
                         return -1; }

#endif // ERROR_H_INCLUDED
