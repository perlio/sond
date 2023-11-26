#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "misc_stdlib.h"


int
main( int argc, char** argv )
{
    int rc = 0;
    char* exe_dir = NULL;
    char path[260] = { 0 };

    exe_dir = get_exe_dir( );
    strcat( path, exe_dir );
    strcat( path, "/Test.exe" );
printf("%s\n", path);
    rc = rmdir( "sdfg/" );
    if ( rc )
    {
        printf( "%i  %s\n", errno, strerror( errno ) );
    }

    printf( "\nBitte Fenster schließen" );

    while( 1 );

    return 0;
}

/*
rename:
- 17 file exists wenn in existierendes File umbenannt werden soll
- funktioniert, auch wenn geöffnet
- 2 No such... wenn oldpath nicht existent oder dir-Bestandteil von newpath nicht existent

remove:
- wenn geöffnet: 13 Permission denied
- wenn nicht existent: 2 No such file or directory

rmdir:
- 41 Directory not empty wenn File drin
*/
