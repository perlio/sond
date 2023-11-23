/*
sond (misc.c) - Akten, Beweisstücke, Unterlagen
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


/* Make a directory; already existing dir okay */
static int maybe_mkdir(const char* path, mode_t mode)
{
    struct stat st;
    errno = 0;

    /* Try to make the directory */
    if (mkdir( path ) == 0)
        return 0;

    /* If it fails for any reason but EEXIST, fail */
    if (errno != EEXIST)
        return -1;

    /* Check if the existing path is a directory */
    if (stat(path, &st) != 0)
        return -1;

    /* If not, fail with ENOTDIR */
    if (!S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }

    errno = 0;
    return 0;
}

int
mkdir_p(const char *path)
{
    /* Adapted from http://stackoverflow.com/a/2336245/119527 */
    char *_path = NULL;
    char *p;
    int result = -1;
    mode_t mode = 0777;

    errno = 0;

    /* Copy string so it's mutable */
    _path = strdup(path);
    if (_path == NULL)
        goto out;

    /* Iterate the string */
    for (p = _path + 1; *p; p++) {
        if (*p == '/') {
            /* Temporarily truncate */
            *p = '\0';

            if (maybe_mkdir(_path, mode) != 0)
                goto out;

            *p = '/';
        }
    }

    if (maybe_mkdir(_path, mode) != 0)
        goto out;

    result = 0;

out:
    free(_path);
    return result;
}


char*
get_base_dir( void )
{
    char buff[PATH_MAX] = { 0 };
    char base_dir[MAX_PATH] = { 0 };

#ifdef _WIN32
    DWORD ret = 0;

    ret = GetModuleFileName(NULL, buff, _countof(buff));
    if ( !ret )
    {
//        DWORD error_code = 0;
//        error_code = GetLastError( );
//errno nicht set!
        return NULL;
    }
#elif defined( __linux__ )
    ssize_t count = readlink("/proc/self/exe", buff, PATH_MAX);
    if (count == -1) return NULL; //errno is set
#endif // _WIN32
    strncpy( base_dir,(const char*) buff, strlen( buff ) -
            strlen( strrchr( (const char*) buff, '\\' ) ) - 3 );

    return strdup( base_dir );
}


