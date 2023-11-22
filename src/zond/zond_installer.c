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


static int
rename_files( const char* filename, const struct stat* stat_new_file, int flag,
        struct FTW* ftwbuf )
{
    if ( flag == FTW_F )
    {
        struct stat stat_old_file = { 0 };
        int rc = 0;
        char* ptr = NULL;


        ptr = strchr( filename, '/' ) + 1;
        rc = stat( ptr, &stat_old_file );
        if ( rc ) printf( "Konnte Eigenschaften von Datei %s nicht lesen - %s\n",
                ptr, strerror( errno ) );

        if ( stat_new_file->st_mtime > stat_old_file.st_mtime + 1 ) //+1 wg. Rundungsfehlern
        {//Datei aus zip ist neuer -> verschieben
            int rc = 0;

            rc = remove( ptr );
            if ( rc ) printf( "Konnte Datei %s nicht löschen - %s\n", ptr, strerror( errno ) );
            else
            {
                int rc = 0;

                rc = rename( filename, ptr );
                if ( rc ) printf( "Konnte downgeloadetes File %s nicht verschieben - %s\n",
                        filename, strerror( errno ) );
            }
        }
        else //downgeloadetes File nicht neuer
        {
            int rc = 0;

            rc = remove( filename );
            if ( rc ) printf( "Konnte downgeloadetes File %s, das nicht neuer ist, nicht löschen - %s\n",
                    filename, strerror( errno ) );
        }
    }
    else if ( flag == FTW_DP )
    {
        int rc = 0;

        rc = rmdir( filename );
        if ( rc && errno != ENOTEMPTY ) printf( "Konnte directory %s nicht löschen - %s\n",
                filename, strerror( errno ) );
    }
    else printf( "Flag %i zurückgegeben\n", flag );

    return 0;
}


int main( int argc, char** argv )
{
    int rc = 0;
    char* base_dir = NULL;
    char base[PATH_MAX] = { 0 };

    base_dir = get_base_dir( );
    if ( !base_dir )
    {
        printf( "Konnte base-dir nicht setzen\n" );
        goto end;
    }

    strncpy( base, (const char*) base_dir, strlen( base_dir ) - 1 -
            strlen( argv[1] ) );
    free( base_dir );

    rc = chdir( base );
    if ( rc )
    {
        printf( "Konnte Arbeitsverzeichnis nicht festlegen - %s\n",
                strerror( errno ) );

        goto end;
    }

    rc = nftw( argv[1], rename_files, 10, FTW_DEPTH );
    if ( rc )
    {
        printf( "Fehler beim Verschieben der heruntergeladenen Dateien - %s\n\n",
                strerror( errno ) );

        goto end;
    }

end:
    rc = rmdir( argv[1] );
    if ( rc )
    {
        if ( errno == ENOTEMPTY ) printf( "Dateien im Verzeichnis %s "
                "nach Beendigung des Programms von Hand umkopieren!\n\n",
                argv[1] );
        else if ( errno != ENOENT ) printf( "Fehler beim Löschen des Verzeichnisses %s - %s\n\n",
                argv[1], strerror( errno ) );
    }

    printf( "Bitte Fenster schließen" );
    while ( 1 ) { };

    return 0;
}
