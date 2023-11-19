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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <ftw.h>

#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#endif // _WIN32



static int
rm( const char *path, const struct stat *s, int flag, struct FTW *f)
{
    int (*rm_func)(const char *);

    rm_func = (flag == FTW_DP) ? rmdir : unlink;
    if( rm_func(path) ) return -1;

    return 0;
}


int
rm_r( const char* path )
{
    if ( nftw( path, rm, 10, FTW_DEPTH) ) return -1;

    return 0;
}


char*
get_base_dir( void )
{
#ifdef _WIN32
    DWORD ret = 0;
    TCHAR buff[MAX_PATH] = { 0 };
    char base_dir[MAX_PATH] = { 0 };

    ret = GetModuleFileName(NULL, buff, _countof(buff));
    if ( !ret )
    {
        DWORD error_code = 0;

        error_code = GetLastError( );

        return NULL;
    }

    strncpy( base_dir,(const char*) buff, strlen( buff ) -
            strlen( strrchr( (const char*) buff, '\\' ) ) - 3 );

    return strdup( base_dir );
#elif defined( __linux__ )
    char result[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
    if (count == -1) return NULL; //errno is set
    return strdup( dirname( dirname( result ) ) ); //zond/bin/zond.exe
#endif // _WIN32
}


int main()
{
    int rc = 0;
    char* base_dir = NULL;

    base_dir = get_base_dir( );
    if ( !base_dir )
    {
        printf( "Konnte base-dir nicht setzen" );
        goto end;
    }

    rc = chdir( base_dir );
    if ( rc )
    {
        printf( "Konnte Arbeitsverzeichnis nicht festlegen - %s",
                strerror( errno ) );

        goto end;
    }

    rc = rm_r( "share" );
    if ( rc )
    {
        printf( "Konnte altes share-Verzeichnis nicht löschen - %s",
                strerror( errno ) );

        goto end;
    }

    rc = rename( "ahare", "share" );
    if ( rc )
    {
        printf( "Konnte heruntergeladenes share-Verzeichnis nicht umbenennen - %s",
                strerror( errno ) );

        goto end;
    }

    rc = rm_r( "lib" );
    if ( rc )
    {
        printf( "Konnte altes lib-Verzeichnis nicht löschen - %s",
                strerror( errno ) );

        goto end;
    }

    rc = rename( "aib", "lib" );
    if ( rc )
    {
        printf( "Konnte heruntergeladenes lib-Verzeichnis nicht umbenennen - %s",
                strerror( errno ) );

        goto end;
    }

    rc = rm_r( "bin" );
    if ( rc )
    {
        printf( "Konnte altes bin-Verzeichnis nicht löschen - %s",
                strerror( errno ) );

        goto end;
    }

    rc = rename( "ain", "bin" );
    if ( rc )
    {
        printf( "Konnte heruntergeladenes bin-Verzeichnis nicht umbenennen - %s",
                strerror( errno ) );

        goto end;
    }

end:
    free( base_dir );
    printf( "Bitte Fenster schließen" );
    while ( 1 ) { };

    return 0;
}
