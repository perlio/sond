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
#include <zip.h>
#include <string.h>


#include "../misc.h"


static void
zond_installer_unzip( const gchar* vtag )
{
    gchar* basedir = NULL;
	gchar filename[260] = { 0 } ; // File path
	struct zip *za; // Zip archive
	struct zip_file *zf; // Stores file to be extracted
	struct zip_stat sb; // Stores file info
	char buf[100]; // Buffer to write stuff
	int err; // Stores error code
	int len;
	FILE * fd; // Where file is extracted to
	long long sum; // How much file has been copied so far

	basedir = get_base_dir( );
    strcpy( filename, basedir );
    free( basedir );
    strcat( filename, "zond-x86_64-" );
    strcat( filename, vtag );
    strcat( filename, ".zip" );
printf( "%s\n", filename );
	// Open the zip file
	za = zip_open( filename, 0, &err );
	if ( !za )
    {
        zip_error_t zip_error = { 0 };
        zip_error_init_with_code( &zip_error, err );
        printf( "zip_open Error: %s", zip_error_strerror( &zip_error ) );
        zip_error_fini( &zip_error );

        exit( -1 );
    }

	// Unpack zip
	int num = zip_get_num_entries( za, 0 );
	for ( int i = 0; i < num; i++ )
    { // Iterate through all files in zip
		if (zip_stat_index(za, i, 0, &sb) == 0)
        { // Tries to grab file info
			// Print file info
			printf("==================\n");
			printf("%i  Name: [%s], ", i, sb.name);
			printf("Size: [%llu], ", sb.size);
			printf("mtime: [%u]\n", (unsigned int)sb.mtime);

			//directories überspülen - die gibt's ja alle schon
			len = strlen(sb.name);
			if (sb.name[len - 1] == '/') continue;

			{ // Check if directory
				safe_create_dir(sb.name); // Create directory
			} else {
				zf = zip_fopen_index(za, i, 0); // Open file within zip
				if (!zf) {
					fprintf(stderr, "Unable to open file within zip\n");
					exit(100);
				}

				fd = fopen(sb.name, "wb"); // Create new file
				if (fd == NULL) {
					fprintf(stderr, "Unable to create destination file\n");
					exit(101);
				}

				sum = 0;
				while (sum != sb.size) { // Copy bytes to new file
					len = zip_fread(zf, buf, 100);
					if (len < 0) {
						fprintf(stderr, "Unable to extract file contents\n");
						exit(102);
					}
					fwrite(buf, 1, len, fd);
					sum += len;
				}
				// Finished copying file
				fclose(fd);
				zip_fclose(zf);
			}
        } else {
			printf("File[%s] Line[%d]\n", __FILE__, __LINE__);
*/
		}
		else printf("nix\n");
	}

	if (zip_close(za) == -1)
    {
		fprintf(stderr, "Can't close zip archive '%s'\n", filename );
		return;
	}
sleep(50);
    return;
}


int
main( gint argc, char **argv)
{
    if ( argc != 2 )
    {
        printf( "Usage: intaller.exe [Versions-Tag]" );
        sleep(10);
        return -1;
    }

    zond_installer_unzip( argv[1] );

    return 0;
}
