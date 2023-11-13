/*
zond (zond_update.c) - Akten, Beweisstücke, Unterlagen
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

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <curl/curl.h>
#include <zip.h>
#include <sys/stat.h>
#include <errno.h>

#include "../../misc.h"

#include "../global_types.h"


static gint
zond_update_unzip( Projekt* zond, const gchar* vtag, GError** error )
{
	gchar* zipname = NULL; // File path
	struct zip *za; // Zip archive
	int err; // Stores error code
	gchar* dir_update = NULL;
	gint rc = 0;

    //Verzeichnis schaffen, in das entpackt werden soll
    dir_update = g_strconcat( zond->base_dir, vtag, NULL );
    rc = g_mkdir( dir_update, S_IRWXU | S_IRWXG );
    g_free( dir_update );
    if ( rc == -1 && errno != EEXIST )
    {
        *error = g_error_new( ZOND_ERROR, ZOND_ERROR_IO, "%s\ng_mkdir\n%s",
                __func__, strerror( errno ) );

        return -1;
    }

	zipname = g_strconcat( zond->base_dir, "zond-x86_64-", vtag, ".zip", NULL );
	// Open the zip file
	za = zip_open( zipname, 0, &err );
	g_free( zipname );
	if ( !za )
    {
        zip_error_t zip_error = { 0 };
        zip_error_init_with_code( &zip_error, err );
        *error = g_error_new( ZOND_ERROR, ZOND_ERROR_ZIP, "%s\nzip_open\n%s",
        __func__, zip_error_strerror( &zip_error ) );
        zip_error_fini( &zip_error );

        return -1;
    }

	// Unpack zip
	int num = zip_get_num_entries( za, 0 );
	for ( int i = 0; i < num; i++ )
    { // Iterate through all files in zip
        struct zip_stat sb; // Stores file info
        gint rc = 0;
        gint len_name = 0;

		rc = zip_stat_index(za, i, 0, &sb);
		if ( rc )
        {
            zip_error_t* zip_error = NULL;

            zip_error = zip_get_error( za );
            *error = g_error_new( ZOND_ERROR, ZOND_ERROR_ZIP, "%s\nzip_stat_index\n%s",
                    __func__, zip_error_strerror( zip_error ) );
            zip_error_fini( zip_error );
            zip_discard( za );

            return -1;
        }

        len_name = strlen(sb.name);
        if (sb.name[len_name - 1] == '/')// Check if directory
        {
            gchar* dir = NULL;
            gint rc = 0;

            dir = g_strconcat( zond->base_dir, vtag, "/", sb.name, NULL );
            rc = mkdir_p( dir );
            g_free( dir );
            if ( rc && errno != EEXIST )
            {
                *error = g_error_new( ZOND_ERROR, ZOND_ERROR_IO, "%s\ng_mkdir\n%s",
                        __func__, strerror( errno ) );
                zip_discard( za );

                return -1;
            }
        }
        else
        {
            struct zip_file *zf; // Stores file to be extracted
            FILE * fd; // Where file is extracted to
            long long sum; // How much file has been copied so far
            gchar* filename = NULL;

            zf = zip_fopen_index(za, i, 0); // Open file within zip
            if ( !zf )
            {
                zip_error_t* zip_error = NULL;

                zip_error = zip_get_error( za );
                *error = g_error_new( ZOND_ERROR, ZOND_ERROR_ZIP, "%s\nzip_fopen_index\n%s",
                        __func__, zip_error_strerror( zip_error ) );
                zip_error_fini( zip_error );
                zip_discard( za );

                return -1;
            }

            filename = g_strconcat( zond->base_dir, vtag, "/", sb.name, NULL );
            fd = fopen( filename, "wb" ); // Create new file
            g_free( filename );
            if (fd == NULL)
            {
                *error = g_error_new( ZOND_ERROR, ZOND_ERROR_IO, "%s\nfopen\n%s",
                        __func__, strerror( errno ) );
                zip_fclose( zf ); //ToDo: Fehlerabfrage
                zip_discard( za );

                return -1;
            }

            sum = 0;
            while (sum != sb.size)
            { // Copy bytes to new file
                char buf[100]; // Buffer to write stuff
                int len;

                len = zip_fread(zf, buf, 100);
                if (len < 0)
                {
                    *error = g_error_new( ZOND_ERROR, ZOND_ERROR_ZIP,
                            "%s\nzip_fread\nKann Datei nicht lesen", __func__ );
                    fclose( fd ); //ToDo: Fehler...
                    zip_fclose( zf ); //ToDo: Fehler...
                    zip_discard( za );

                    return -1;
                }

                fwrite(buf, 1, len, fd); //ToDo: Fehlerabfrage
                sum += len;
            }
            // Finished copying file
            fclose(fd); //ToDo: Fehler...
            zip_fclose(zf); //ToDo: Fehler...
        }
    }

    zip_discard( za );

    return 0;
}


static gint
zond_update_download_newest( Projekt* zond, const gchar* vtag, GError** error )
{
    CURL *curl = NULL;
    FILE *fp= NULL;
    CURLcode res;
    gchar* filename = NULL;
    gchar *url = NULL;
    gchar* outfilename = NULL;

    curl = curl_easy_init();
    if ( !curl )
    {
        *error = g_error_new( ZOND_ERROR, ZOND_ERROR_CURL,
                "curl_easy_init nicht erfolgreich" );
        return -1;
    }

    filename = g_strconcat( "zond-x86_64-", vtag, ".zip", NULL );

    outfilename = g_strconcat( zond->base_dir, filename, NULL );
    fp = fopen(outfilename,"wb");
    g_free( outfilename );

    if ( !fp )
    {
        *error = g_error_new( ZOND_ERROR, ZOND_ERROR_IO,
                "fopen gibt Fehler zurück: %s", strerror( errno ) );
        g_free( filename );
        return -1;
    }

    url = g_strconcat( "https://github.com/perlio/sond/releases/download/", vtag, "/",
            filename, NULL );
    curl_easy_setopt( curl, CURLOPT_URL, url );
    g_free( url );
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L );
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

    g_free( filename );

    res = curl_easy_perform(curl);
    /* always cleanup */
    curl_easy_cleanup(curl);
    fclose(fp);

    if ( res != CURLE_OK )
    {
        *error = g_error_new( ZOND_ERROR, ZOND_ERROR_CURL,
                "%s\ncurl_easy_perform:\n%s", __func__, curl_easy_strerror( res ) );
        return -1;
    }

    return 0;
}


static gchar*
zond_update_get_vtag( Projekt* zond, GError** error )
{
    SoupSession* soup_session = NULL;
    SoupMessage* soup_message = NULL;
    GBytes* response = NULL;
    gboolean ret = FALSE;
    JsonParser* parser = NULL;
    JsonNode* node = NULL;
    gchar* vtag = NULL;

    soup_session = soup_session_new_with_options( "user-agent", "perlio", NULL );
    soup_message = soup_message_new( SOUP_METHOD_GET, "https://api.github.com/repos/perlio/sond/releases/latest" );

    response = soup_session_send_and_read( soup_session, soup_message, NULL, error );
    g_object_unref( soup_message );
    g_object_unref( soup_session );
    if ( !response )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return NULL;
    }

    parser = json_parser_new( );
    ret = json_parser_load_from_data( parser, g_bytes_get_data( response, NULL ), -1, error );
    g_bytes_unref( response );
    if ( !ret )
    {
        g_prefix_error( error, "%s\n", __func__ );
        g_object_unref( parser );

        return NULL;
    }

    node = json_parser_get_root( parser );
    if ( JSON_NODE_HOLDS_OBJECT(node) )
    {
        JsonObject* object = NULL;

        object = json_node_get_object( node );

        if ( json_object_has_member( object, "tag_name" ) )
                vtag = g_strdup( json_object_get_string_member( object, "tag_name" ) );
        else
        {
            *error = g_error_new( ZOND_ERROR, ZOND_ERROR_VTAG_NOT_FOUND,
                    "json enthält kein member ""tag_name""" );
            g_object_unref( parser );

            return NULL;
        }
    }
    else
    {
        *error = g_error_new( ZOND_ERROR, ZOND_ERROR_JSON_NO_OBJECT,
                "json ist kein object" );
        g_object_unref( parser );

        return NULL;
    }

    g_object_unref( parser );

    return vtag;
}


gint
zond_update( Projekt* zond, GError** error )
{
    gchar** strv_tags = NULL;
    gchar* title = NULL;
    gint rc = 0;
    gchar* argv[3] = { NULL };
    GPid pid = 0;
    gchar* vtag = NULL;
    gboolean ret = FALSE;
    gboolean res = FALSE;

    vtag = zond_update_get_vtag( zond, error );
    if ( !vtag )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }


    //tag mit aktueller version vergleichen
    strv_tags = g_strsplit( vtag + 1, ".", -1 );

    if ( atoi( strv_tags[0] ) <= atoi( MAJOR ) )
    {
        if ( atoi( strv_tags[1] ) <= atoi( MINOR ) )
        {
            if ( atoi( strv_tags[2] ) <= atoi( PATCH ) )
            {
                g_strfreev( strv_tags );
                g_free( vtag );

                return 1;
            }
        }
    }

    g_strfreev( strv_tags );

    title = g_strconcat( "Aktuellere Version vorhanden (", vtag, ")", NULL );

    rc = abfrage_frage( zond->app_window, title, "Herunterladen und installieren?",
            NULL );
    g_free( title );
    if ( rc != GTK_RESPONSE_YES )
    {
        g_free( vtag );
        return 0;
    }
/*
    //herunterladen
    rc = zond_update_download_newest( zond, vtag, error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );
        g_free( vtag );

        return -1;
    }
*/
    //entpacken
    rc = zond_update_unzip( zond, vtag, error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );
        g_free( vtag );

        return -1;
    }

    //installer starten
#ifdef __WIN32
    argv[0] = g_strconcat( zond->base_dir, "bin/zond_installer.exe", NULL );
#elifdef __linux__
    argv[0] = g_strdup( "bin/zond_installer" );
#endif // __linux__

    argv[1] = vtag;

    res = g_spawn_async( NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD,
            NULL, NULL, &pid, error );
    g_free( vtag );
    g_free( argv[0] );
    if ( !res )
    {
        g_prefix_error( error, "%s\n", __func__ );
        return -1;
    }

    g_child_watch_add( pid, (GChildWatchFunc) g_spawn_close_pid, NULL );

    //Projekt schließen und zond beenden
    g_signal_emit_by_name( zond->app_window, "delete-event", NULL, &ret );

    return 0;
}

