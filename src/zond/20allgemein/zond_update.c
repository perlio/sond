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
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <curl/curl.h>

#include "../../misc.h"

#include "../global_types.h"

/*
size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t written = fwrite(ptr, size, nmemb, stream);
    return written;
}
*/

static gint
zond_update_download_newest( Projekt* zond, const gchar* tag, GError** error )
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

    filename = g_strconcat( "zond-x86_64-", tag, ".zip", NULL );

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

//    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);

    url = g_strconcat( "https://github.com/perlio/sond/releases/latest/downloads/",
            filename, NULL );
    g_free( filename );
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    g_free( url );

    res = curl_easy_perform(curl);
    /* always cleanup */
    curl_easy_cleanup(curl);
    fclose(fp);

    if ( res != CURLE_OK )
    {
        *error = g_error_new( ZOND_ERROR, ZOND_ERROR_CURL,
                "%s\ncurl_easy_perform gibt Error-Code %i zurück", __func__, res );
        return -1;
    }

    return 0;
}



gint
zond_update( Projekt* zond, GError** error )
{
    SoupSession* soup_session = NULL;
    SoupMessage* soup_message = NULL;
    GBytes* response = NULL;
    gboolean ret = FALSE;
    JsonParser* parser = NULL;
    JsonNode* node = NULL;
    gchar* tag = NULL;
    gchar** strv_tags = NULL;
    gchar* title = NULL;
    gint rc = 0;
    gchar* argv[2] = { NULL };
    GPid pid = 0;

    soup_session = soup_session_new_with_options( "user-agent", "perlio", NULL );
    soup_message = soup_message_new( SOUP_METHOD_GET, "https://api.github.com/repos/perlio/sond/releases/latest" );

    response = soup_session_send_and_read( soup_session, soup_message, NULL, error );
    g_object_unref( soup_message );
    g_object_unref( soup_session );
    if ( !response )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    parser = json_parser_new( );
    ret = json_parser_load_from_data( parser, g_bytes_get_data( response, NULL ), -1, error );
    g_bytes_unref( response );
    if ( !ret )
    {
        g_prefix_error( error, "%s\n", __func__ );
        g_object_unref( parser );

        return -1;
    }

    node = json_parser_get_root( parser );
    if ( JSON_NODE_HOLDS_OBJECT(node) )
    {
        JsonObject* object = NULL;

        object = json_node_get_object( node );

        if ( json_object_has_member( object, "tag_name" ) )
                tag = g_strdup( json_object_get_string_member( object, "tag_name" ) );
        else
        {
            *error = g_error_new( ZOND_ERROR, ZOND_ERROR_VTAG_NOT_FOUND,
                    "json enthält kein member ""tag_name""" );
            g_object_unref( parser );

            return -1;
        }
    }
    else
    {
        *error = g_error_new( ZOND_ERROR, ZOND_ERROR_JSON_NO_OBJECT,
                "json ist kein object" );
        g_object_unref( parser );

        return -1;
    }

    g_object_unref( parser );

    //tag mit aktueller version vergleichen
    strv_tags = g_strsplit( tag + 1, ".", -1 );

    if ( atoi( strv_tags[0] ) <= atoi( MAJOR ) )
    {
        if ( atoi( strv_tags[1] ) <= atoi( MINOR ) )
        {
            if ( atoi( strv_tags[2] ) <= atoi( PATCH ) )
            {
                g_strfreev( strv_tags );

                return 1;
            }
        }
    }

    g_strfreev( strv_tags );

    title = g_strconcat( "Aktuellere Version vorhanden (", tag, ")", NULL );

    rc = abfrage_frage( zond->app_window, title, "Herunterladen und installieren?",
            NULL );
    g_free( title );
    if ( rc != GTK_RESPONSE_YES )
    {
        g_free( tag );
        return 0;
    }

    //herunterladen
    rc = zond_update_download_newest( zond, tag, error );
    g_free( tag );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    //Projekt schließen und zond beenden
    g_signal_emit_by_name( zond->app_window, "delete-event", NULL, &ret );

    //installer starten
#ifdef __WIN32
    argv[0] = "bin/installer.exe";
#elifdef __linux__
    argv[0] = "bin/installer";
#endif // __linux__

    ret = g_spawn_async( NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD,
            NULL, NULL, &pid, error );
    if ( !ret )
    {
        g_prefix_error( error, "%s\n", __func__ );
        return -1;
    }

    g_child_watch_add( pid, (GChildWatchFunc) g_spawn_close_pid, NULL );

    return 0;
}

