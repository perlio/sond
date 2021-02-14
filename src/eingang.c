/*
zond (eingang.c) - Akten, Beweisstücke, Unterlagen
Copyright (C) 2020  pelo america

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/


#include <gtk/gtk.h>

#include "misc.h"
#include "eingang.h"
#include "dbase.h"

void
eingang_free( Eingang* eingang )
{
    g_date_free( eingang->absendedatum );
    g_date_free( eingang->eingangsdatum );
    g_date_free( eingang->erfassungsdatum );
    g_free( eingang->absender );
    g_free( eingang->ort );
    g_free( eingang->traeger );
    g_free( eingang->transport );

    g_free( eingang );
}


Eingang*
eingang_new( void )
{
    Eingang* eingang = g_malloc0( sizeof( Eingang ) );

    return eingang;
}


gint
eingang_set_rel_path( DBase* dbase, Eingang* eingang,
        gchar** errmsg )
{



}


gint
eingang_get_for_rel_path( DBase* dbase, const gchar* rel_path,
        Eingang** eingang, gchar** errmsg )
{
    gchar* buf = NULL;
    gint zaehler = 1;

    buf = g_strdup( rel_path );

    do
    {
        gint rc = 0;
        gchar* buf_int = NULL;
        gchar* ptr_buf = NULL;

        rc = dbase_get_eingang_for_rel_path( dbase, buf, eingang, errmsg );
        if ( rc == -1 )
        {
            g_free( buf );
            ERROR( "dbase_get_eingang_for_rel_path" )
        }
        else if ( rc == 0 ) //*eingang wurde gesetzt
        {
            g_free( buf );
            return zaehler;
        }

        ptr_buf = g_strrstr( buf, "/" );
        if ( !ptr_buf ) break;

        buf_int = g_strndup( buf, strlen( buf ) - strlen( ptr_buf ) );
        g_free( buf );
        buf = buf_int;

        zaehler++;
    }
    while ( 1 );

    g_free( buf );

    return 0;
}


