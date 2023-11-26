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
#elifdef __linux__
#include <limits.h>
#endif // _WIN32

#include "../misc_stdlib.h"

static int
rename_files( const char* filename, const struct stat* stat_new_file, int flag,
        struct FTW* ftwbuf )
{
    if ( flag == FTW_F )
    {
        struct stat stat_old_file = { 0 };
        int rc = 0;
        char* ptr = NULL;
        char garbage_path[PATH_MAX] = { 'g', 'a', 'r', 'b', 'a', 'g', 'e', '/', 0 };

        ptr = strchr( filename, '/' ) + 1; //vtag abschneiden
        strcat( garbage_path, ptr );

        rc = stat( ptr, &stat_old_file );
        if ( rc && errno != ENOENT ) printf( "Konnte Eigenschaften von Datei %s nicht lesen - %s\n",
                ptr, strerror( errno ) );

        if ( stat_new_file->st_mtime > stat_old_file.st_mtime + 1 ) //+1 wg. Rundungsfehlern
        {//Datei aus zip ist neuer -> verschieben
            int rc = 0;

            rc = remove( ptr );
            if ( rc && errno != ENOENT )
            {
                int rc = 0;
                char garbage_dir[PATH_MAX] = { 0 };

                strncpy( garbage_dir, garbage_path, strlen( garbage_path ) -
                        strlen( strrchr( garbage_path, '/' ) ) );

                rc = mkdir_p( garbage_dir );
                if ( rc ) printf( "Konnte Unterverzeichnis ""%s"" in garbage-Dir nicht erzeugen -%s\n",
                        garbage_dir, strerror( errno ) );

                rc = rename ( ptr, garbage_path ); //verschieben in garbage
                if ( rc )
                {
                    printf( "Konnte Datei %s nicht in Verzeichnis garbage verschieben - %s\n",
                            ptr, strerror( errno ) );

                    return 0; //da kann man nix machen
                }
            }

            rc = rename( filename, ptr );
            if ( rc ) printf( "Konnte downgeloadetes File %s nicht verschieben - %s\n",
                    filename, strerror( errno ) );
            else printf( "Datei ""%s"" upgedated\n", ptr );
        }
        else //downgeloadetes File nicht neuer
        {
            int rc = 0;

            rc = remove( filename );
            if ( rc ) //Abfrage auf ENOENT nicht nötig - nftw würde nicht ausgelöst
            {
                int rc = 0;
                char garbage_dir[PATH_MAX] = { 0 };

                strncpy( garbage_dir, garbage_path, strlen( garbage_path ) -
                        strlen( strrchr( garbage_path, '/' ) ) );

                rc = mkdir_p( garbage_dir );
                if ( rc ) printf( "Konnte Unterverzeichnis ""%s"" in garbage-Dir nicht erzeugen -%s\n",
                        garbage_dir, strerror( errno ) );

                rc = rename ( filename, garbage_path ); //verschieben in garbage
                if ( rc )
                {
                    printf( "Konnte Datei %s nicht in Verzeichnis garbage verschieben - %s\n",
                            ptr, strerror( errno ) );

                    return 0; //da kann man nix machen
                }
            }
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
    char* vtag_dir = NULL;
    char vtag_dir_tmp[MAX_PATH] = { 0 };
    char base_dir[PATH_MAX] = { 0 };
    char* vtag = NULL;

    vtag_dir = get_base_dir( ); // mit / am Ende
    if ( !vtag_dir )
    {
        printf( "Konnte base-dir nicht ermitteln\n" );
        goto end;
    }

    strncpy( vtag_dir_tmp, vtag_dir, strlen( vtag_dir ) - 1 ); // letztes Zeichen (/) abschneiden
    free( vtag_dir );

    vtag = strrchr( vtag_dir_tmp, '\\' ) + 1;

    strncpy( base_dir, vtag_dir_tmp, strlen( vtag_dir_tmp ) - strlen( vtag ) );

    rc = chdir( base_dir );
    if ( rc )
    {
        printf( "Konnte Arbeitsverzeichnis nicht festlegen - %s\n",
                strerror( errno ) );

        goto end;
    }

    rc = mkdir( "garbage" );
    if ( rc )
    {
        printf( "Konnte Verzeichnis ""garbage"" nicht erzeugen - %s", strerror( errno ) );

        goto end;
    }

    nftw( vtag, rename_files, 10, FTW_DEPTH );

end:
    rc = rmdir( "garbage" );
    if ( rc )
    {
        if ( errno == ENOTEMPTY ) printf( "Verzeichnis ""garbage"" "
                "nach Beendigung des Programms von Hand löschen!\n\n" );
        else printf( "Fehler beim Löschen des Verzeichnisses ""garbage"" - %s\n\n",
                strerror( errno ) );
    }

    printf( "Bitte Fenster schließen" );

    while ( 1 );

    return 0;
}
