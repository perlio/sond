#ifndef ERROR_H_INCLUDED
#define ERROR_H_INCLUDED

typedef char gchar;

gchar* add_string( gchar*, gchar* );
gchar* prepend_string( gchar*, gchar* );


//Fehlerbehandlung
#define ERROR_PAO_R(x,y) { if ( errmsg ) *errmsg = prepend_string( *errmsg, \
                       g_strdup( "Bei Aufruf " x ":\n" ) ); \
                       return y; }

#define ERROR_SQL_R(x,y) { if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf " x ":\n", \
                       sqlite3_errmsg(zond->db), NULL ); \
                       return y; }

#define ERROR_MUPDF_R(x,y) { if ( errmsg ) *errmsg = prepend_string( *errmsg, g_strconcat( \
                        "Bei Aufruf " x ":\n", fz_caught_message( ctx ), NULL ) ); \
                         return y; }

#define ERROR_PAO(x) { if ( errmsg ) *errmsg = prepend_string( *errmsg, \
                       g_strdup( "Bei Aufruf " x ":\n" ) ); \
                       return -1; }

#define ERROR_THREAD(x) { fprintf( stderr, "Thread error: %s\n", x); \
                          fz_drop_context( ctx ); return; }

#define ERROR_SQL(x) { if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf " x ":\n", \
                       sqlite3_errmsg(zond->db), NULL ); \
                       return -1; }

#define ERROR_MUPDF(x) { if ( errmsg ) *errmsg = add_string( *errmsg, g_strconcat( \
                        "Bei Aufruf " x ":\n", fz_caught_message( ctx ), NULL ) ); \
                         return -1; }

#define ERR_MUPDF(x) { if ( errmsg ) *errmsg = add_string( *errmsg, g_strconcat( \
                        "Bei Aufruf " x ":\n", fz_caught_message( ctx ), NULL ) ); }

#define ERROR_MUPDF_CTX(x,y) { if ( errmsg ) *errmsg = add_string( *errmsg, g_strconcat( \
                        "Bei Aufruf " x ":\n", fz_caught_message( y ), NULL ) ); \
                         return -1; }

#define ROLLBACK \
          { rc = db_rollback( zond, errmsg ); \
            if ( !rc && errmsg ) *errmsg = add_string( *errmsg, \
                    g_strdup( "\n\nRollback durchgeführt" ) ); \
            else if ( errmsg ) *errmsg = add_string( *errmsg, \
                    g_strdup( "\n\nGgf. Datenbankverbindung schließen" ) ); \
        }

#define ERROR_PAO_ROLLBACK(x) \
          { if ( errmsg ) *errmsg = prepend_string( *errmsg, \
            g_strdup( "Bei Aufruf " x ":\n" ) ); \
            ROLLBACK \
            return -1; }

#define ERROR_SQL_ROLLBACK(x) \
          { if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf " x ":\n", \
                    sqlite3_errmsg(zond->db), NULL ); \
            ROLLBACK \
            return -1; }


#endif // ERROR_H_INCLUDED
