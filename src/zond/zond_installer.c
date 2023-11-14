/*
zond (zond_installer.c) - Akten, Beweisstücke, Unterlagen
Copyright (C) 2023  pelo america

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

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <stdio.h>
#include <unistd.h>

#include "../misc.h"


gint
main( gint argc, gchar **argv)
{
    gint rc = 0;
    gchar* base_dir = NULL;
    gchar* argv_spawn[3] = { NULL };
    GError* error = NULL;
    GPid pid = 0;

    base_dir = get_base_dir( );
    rc = chdir( base_dir );
    g_free( base_dir );
    if ( rc )
    {
        printf( "Konnte Arbeitsverzeichnis nicht festlegen - %s",
                strerror( errno ) );

    }

    rc = rm_r( "bin" );
    if ( rc )
    {
        printf( "Konnte altes bin-Verzeichnis nicht löschen - %s",
                strerror( errno ) );

        return -1;
    }

    rc = rename( "ain", "bin" );
    if ( rc )
    {
        printf( "Konnte heruntergeladenes bin-Verzeichnis nicht umbenennen - %s",
                strerror( errno ) );

        return -1;
    }

    rc = rm_r( "share" );
    if ( rc )
    {
        printf( "Konnte altes share-Verzeichnis nicht löschen - %s",
                strerror( errno ) );

        return -1;
    }

    rc = rename( "ahare", "share" );
    if ( rc )
    {
        printf( "Konnte heruntergeladenes share-Verzeichnis nicht umbenennen - %s",
                strerror( errno ) );

        return -1;
    }

    rc = rm_r( "lib" );
    if ( rc )
    {
        printf( "Konnte altes lib-Verzeichnis nicht löschen - %s",
                strerror( errno ) );

        return -1;
    }

    rc = rename( "aib", "lib" );
    if ( rc )
    {
        printf( "Konnte heruntergeladenes lib-Verzeichnis nicht umbenennen - %s",
                strerror( errno ) );

        return -1;
    }

#ifdef __WIN32
    argv_spawn[0] = "bin/zond.exe";
#elifdef __linux__
    argv_spawn[0] = "bin/zond";
#endif // __WIN32

    argv_spawn[1] = argv[1];

    if ( !g_spawn_async( NULL, argv_spawn, NULL, G_SPAWN_DO_NOT_REAP_CHILD,
            NULL, NULL, &pid, &error ) )
    {
        printf( "zond konnte nicht gestartet werden - %s",
                error->message );
        g_error_free( error );

        return -1;
    }

    g_child_watch_add( pid, (GChildWatchFunc) g_spawn_close_pid, NULL );

    return 0;
}
