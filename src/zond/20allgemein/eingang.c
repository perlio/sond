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

#include "../global_types.h"

#include "eingang.h"


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
eingang_set_rel_path( Projekt* zond, Eingang* eingang,
        gchar** errmsg )
{



}


gint
eingang_get_for_rel_path( Projekt* zond, const gchar* rel_path,
        Eingang** eingang, gchar** errmsg )
{


}


