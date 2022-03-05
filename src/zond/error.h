#ifndef ERROR_H_INCLUDED
#define ERROR_H_INCLUDED

typedef char gchar;

gchar* add_string( gchar*, gchar* );


//Fehlerbehandlung


#define ERROR_MUPDF(x) { if ( errmsg ) *errmsg = add_string( *errmsg, g_strconcat( \
                        "Bei Aufruf " x ":\n", fz_caught_message( ctx ), NULL ) ); \
                         return -1; }

#define ERROR_MUPDF_R(x,y) { if ( errmsg ) *errmsg = add_string( g_strconcat( \
                        "Bei Aufruf " x ":\n", fz_caught_message( ctx ), NULL ), *errmsg ); \
                         return y; }

#endif // ERROR_H_INCLUDED
