#include "sond_client_misc.h"
#include <glib.h>

#define JAHRHUNDERT_GRENZE 1960

void
sond_client_misc_parse_regnr( const gchar* entry, gint* regnr, gint* jahr )
{
    gint strlen_vor_slash = 0;

    gchar* regnr_str = NULL;
    gchar* year_str = NULL;

    strlen_vor_slash = strlen( entry ) - strlen( g_strstr_len( entry, -1,
            "/" ) );

    year_str = g_strstr_len( entry, -1, "/" ) + 1;
    regnr_str = g_strndup( entry, strlen_vor_slash );

    *jahr = (gint) g_ascii_strtoll( year_str, NULL, 10 );
    *regnr = (gint) g_ascii_strtoll( regnr_str, NULL, 10 );
    g_free( regnr_str );

    if ( *jahr < JAHRHUNDERT_GRENZE ) *jahr += 2000;
    else *jahr += 1900;

    return;
}




gboolean
sond_client_misc_regnr_wohlgeformt( const gchar* entry )
{
    if ( (*entry < 48) || (*entry > 57) ) return FALSE; //erstes Zeichen muß Ziffer sein

    gint slashes = 0;
    gint pos = 1;
    while ( *(entry + pos) != 0 )
    {
        if ( *(entry + pos) == 47 ) slashes++;
        else if ( (*entry < 48) || (*entry > 57) ) return FALSE;
        if ( slashes > 1 ) return FALSE;
        pos++;
    }

    if ( slashes == 0 ) return FALSE;

    if ( strlen( g_strrstr( entry, "/" ) ) != 3 ) return FALSE;

    return TRUE;
}


