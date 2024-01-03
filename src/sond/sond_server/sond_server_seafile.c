/*
sond (sond_server_seafile.c) - Akten, Beweisstücke, Unterlagen
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

#include <glib.h>
#include <json-glib/json-glib.h>
#include <curl/curl.h>

#include "../../misc.h"
#include "../../misc_stdlib.h"

#include "sond_server.h"



static gint
sond_server_seafile_do_delete_lib( SondServer* sond_server,
        const gchar* repo_id, GError** error )
{
    CURL *curl = NULL;
    CURLcode res = CURLE_OK;
    gchar *url = NULL;
    struct curl_slist* slist = NULL;
    gchar* header_token = NULL;
    glong http_code = 0;

    curl = curl_easy_init();
    if ( !curl )
    {
        *error = g_error_new( g_quark_from_static_string( "CURL" ), 0,
                "%s\ncurl_easy_init nicht erfolgreich", __func__ );
        return -1;
    }

    url = g_strdup_printf( "%s/api2/repos/%s", sond_server->seafile_url, repo_id );
    curl_easy_setopt( curl, CURLOPT_URL, url );
    g_free( url );

    header_token = g_strdup_printf( "Authorization: Token %s", sond_server->auth_token );
    slist = curl_slist_append( slist, header_token );
    slist = curl_slist_append( slist, "Accept: application/json; indent=4" );
    curl_easy_setopt( curl, CURLOPT_HTTPHEADER, slist );

    curl_easy_setopt( curl, CURLOPT_CUSTOMREQUEST, "DELETE" );

    curl_easy_setopt( curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA );
    curl_easy_setopt( curl, CURLOPT_FOLLOWLOCATION, 1L );

    res = curl_easy_perform(curl);
    curl_easy_getinfo ( curl, CURLINFO_RESPONSE_CODE, &http_code );

    /* always cleanup */
    curl_easy_cleanup(curl);
    curl_slist_free_all( slist );

    if ( res != CURLE_OK )
    {
        *error = g_error_new( g_quark_from_static_string( "CURL" ), 0,
                "%s\ncurl_easy_perform:\n%s", __func__, curl_easy_strerror( res ) );

        return -1;
    }

    if ( http_code != 200 )
    {
        *error = g_error_new( g_quark_from_static_string( "HTTP" ), 0,
                "%s\ncurl_easy_perform:\n%ld", __func__, http_code );

        return -1;
    }

    return 0;
}


static gchar*
parse_resp_lib_search( const gchar* json, GError** error )
{
    JsonParser* parser = NULL;
    gboolean ret = FALSE;
    JsonNode* node = NULL;
    JsonArray* array = NULL;
    JsonObject* object = NULL;
    gchar* repo_id = NULL;

    parser = json_parser_new( );
    ret = json_parser_load_from_data( parser, json, -1, error );
    if ( !ret )
    {
        g_prefix_error( error, "%s\njson_parser_load_from_data\n", __func__ );
        g_object_unref( parser );

        return NULL;
    }

    node = json_parser_get_root( parser );
    if ( !JSON_NODE_HOLDS_ARRAY(node) )
    {
        *error = g_error_new( SOND_SERVER_ERROR, 0,
                "Antwort-json ist kein Array" );
        g_object_unref( parser );

        return NULL;
    }

    array = json_node_get_array( node );

    if ( json_array_get_length( array ) == 0 )
    {
        *error = g_error_new( SOND_SERVER_ERROR, SOND_SERVER_ERROR_NOTFOUND,
                "Keine library gefunden" );
        g_object_unref( parser );

        return NULL;
    }
    else if ( json_array_get_length( array ) != 1 )
    {
        *error = g_error_new( SOND_SERVER_ERROR, 0,
                "%d libraries zu gesuchtem Namen gefunden",
                 json_array_get_length( array ) );
        g_object_unref( parser );

        return NULL;
    }

    object = json_array_get_object_element( array, 0 );

    if ( !json_object_has_member( object, "id" ) )
    {
        *error = g_error_new( SOND_SERVER_ERROR, 0,
                "json enthält kein member ""id""" );
        g_object_unref( parser );

        return NULL;
    }

    repo_id = g_strdup( json_object_get_string_member( object, "id" ) );

    g_object_unref( parser );

    return repo_id;
}


static gchar*
sond_server_seafile_get_repo_id( SondServer* sond_server,
        gint reg_nr, gint reg_jahr, GError** error )
{
    CURL *curl = NULL;
    CURLcode res = CURLE_OK;
    gchar *url = NULL;
    struct curl_slist* slist = NULL;
    gchar* header_token = NULL;
    glong http_code = 0;
    CurlUserData mem = { 0 };
    gchar* repo_id = NULL;

    curl = curl_easy_init();
    if ( !curl )
    {
        *error = g_error_new( g_quark_from_static_string( "CURL" ), 0,
                "%s\ncurl_easy_init nicht erfolgreich", __func__ );
        return NULL;
    }

    url = g_strdup_printf( "%s/api2/repos/?type=repo&nameContains=%d-%d", sond_server->seafile_url, reg_jahr, reg_nr );
    curl_easy_setopt( curl, CURLOPT_URL, url );
    g_free( url );

    header_token = g_strdup_printf( "Authorization: Token %s", sond_server->auth_token );
    slist = curl_slist_append( slist, header_token );
    slist = curl_slist_append( slist, "Accept: application/json; indent=4" );
    curl_easy_setopt( curl, CURLOPT_HTTPHEADER, slist );

    curl_easy_setopt( curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA );
    curl_easy_setopt( curl, CURLOPT_FOLLOWLOCATION, 1L );

    curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, curl_write_cb );
    curl_easy_setopt( curl, CURLOPT_WRITEDATA, &mem);

    res = curl_easy_perform(curl);
    curl_easy_getinfo ( curl, CURLINFO_RESPONSE_CODE, &http_code );

    /* always cleanup */
    curl_easy_cleanup(curl);
    curl_slist_free_all( slist );

    if ( res != CURLE_OK )
    {
        *error = g_error_new( g_quark_from_static_string( "CURL" ), 0,
                "%s\ncurl_easy_perform:\n%s", __func__, curl_easy_strerror( res ) );
        free( mem.response );

        return NULL;
    }

    if ( http_code != 200 )
    {
        *error = g_error_new( g_quark_from_static_string( "HTTP" ), 0,
                "%s\ncurl_easy_perform:\n%ld", __func__, http_code );
        free( mem.response );

        return NULL;
    }
printf("%s\n", mem.response);
    repo_id = parse_resp_lib_search( mem.response, error );
    free( mem.response );
    if ( !repo_id ) g_prefix_error( error, "%s\n", __func__ );

    return repo_id;
}


gint
sond_server_seafile_delete_lib( SondServer* sond_server,
        gint reg_nr, gint reg_jahr, GError** error )
{
    gchar* repo_id = NULL;
    gint rc = 0;

    repo_id = sond_server_seafile_get_repo_id( sond_server, reg_nr, reg_jahr, error );
    if ( !repo_id )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    rc = sond_server_seafile_do_delete_lib( sond_server, repo_id, error );
    g_free( repo_id );
    if ( !rc )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    return 0;
}


static gint
sond_server_seafile_create_dir( SondServer* sond_server,
        const gchar* repo_id, const gchar* dirname, GError** error )
{
    CURL *curl = NULL;
    CURLcode res = CURLE_OK;
    gchar *url = NULL;
    gchar* data = NULL;
    struct curl_slist* slist = NULL;
    gchar* header_token = NULL;
    glong http_code = 0;

    curl = curl_easy_init();
    if ( !curl )
    {
        *error = g_error_new( g_quark_from_static_string( "CURL" ), 0,
                "%s\ncurl_easy_init nicht erfolgreich", __func__ );
        return -1;
    }

    url = g_strdup_printf( "%s/api2/repos/%s/dir/?p=/%s", sond_server->seafile_url,
            repo_id, dirname );
    curl_easy_setopt( curl, CURLOPT_URL, url );
    g_free( url );

    data = "operation=mkdir";
    curl_easy_setopt( curl, CURLOPT_POSTFIELDS, data );

    header_token = g_strdup_printf( "Authorization: Token %s", sond_server->auth_token );
    slist = curl_slist_append( slist, header_token );
    slist = curl_slist_append( slist, "Accept: application/json; indent=4" );
    curl_easy_setopt( curl, CURLOPT_HTTPHEADER, slist );

    curl_easy_setopt( curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA );
    curl_easy_setopt( curl, CURLOPT_FOLLOWLOCATION, 1L );

    res = curl_easy_perform(curl);
    curl_easy_getinfo ( curl, CURLINFO_RESPONSE_CODE, &http_code );

    /* always cleanup */
    curl_easy_cleanup(curl);
    curl_slist_free_all( slist );

    if ( res != CURLE_OK )
    {
        *error = g_error_new( g_quark_from_static_string( "CURL" ), 0,
                "%s\ncurl_easy_perform:\n%s", __func__, curl_easy_strerror( res ) );

        return -1;
    }

    if ( http_code != 201 )
    {
        *error = g_error_new( g_quark_from_static_string( "HTTP" ), 0,
                "%s\ncurl_easy_perform:\n%ld", __func__, http_code );

        return -1;
    }

    return 0;
}


static gint
sond_server_seafile_create_dirs( SondServer* sond_server,
        const gchar* repo_id, GError** error )
{
    gint rc = 0;

    rc = sond_server_seafile_create_dir( sond_server, repo_id,
            "docs", error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    rc = sond_server_seafile_create_dir( sond_server, repo_id,
            "eingang", error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    rc = sond_server_seafile_create_dir( sond_server, repo_id,
            "ausgang", error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    return 0;
}


static gint
sond_server_seafile_share_repo( SondServer* sond_server,
        const gchar* repo_id, GError** error )
{
    CURL *curl = NULL;
    CURLcode res = CURLE_OK;
    gchar *url = NULL;
    gchar* data = NULL;
    struct curl_slist* slist = NULL;
    gchar* header_token = NULL;
    glong http_code = 0;

    curl = curl_easy_init();
    if ( !curl )
    {
        *error = g_error_new( g_quark_from_static_string( "CURL" ), 0,
                "%s\ncurl_easy_init nicht erfolgreich", __func__ );
        return -1;
    }

    url = g_strdup_printf( "%s/api2/repos/%s/dir/shared_items/?p=/", sond_server->seafile_url, repo_id );
    curl_easy_setopt( curl, CURLOPT_URL, url );
    g_free( url );

    curl_easy_setopt( curl, CURLOPT_CUSTOMREQUEST, "PUT" );

    data = g_strdup_printf( "share_type=group&group_id=%d&permission=rw", sond_server->seafile_group_id );
    curl_easy_setopt( curl, CURLOPT_POSTFIELDS, data );

    header_token = g_strdup_printf( "Authorization: Token %s", sond_server->auth_token );
    slist = curl_slist_append( slist, header_token );
    slist = curl_slist_append( slist, "Accept: application/json; indent=4" );
    curl_easy_setopt( curl, CURLOPT_HTTPHEADER, slist );

    curl_easy_setopt( curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA );
    curl_easy_setopt( curl, CURLOPT_FOLLOWLOCATION, 1L );

    res = curl_easy_perform(curl);
    curl_easy_getinfo ( curl, CURLINFO_RESPONSE_CODE, &http_code );

    /* always cleanup */
    curl_easy_cleanup(curl);
    curl_slist_free_all( slist );
    g_free( header_token );
    g_free( data );

    if ( res != CURLE_OK )
    {
        *error = g_error_new( g_quark_from_static_string( "CURL" ), 0,
                "%s\ncurl_easy_perform:\n%s", __func__, curl_easy_strerror( res ) );

        return -1;
    }

    if ( http_code != 200 )
    {
        *error = g_error_new( g_quark_from_static_string( "HTTP" ), 0,
                "%s\ncurl_easy_perform HTTP-Code:\n%ld", __func__, http_code );

        return -1;
    }

    return 0;
}


static gchar*
sond_server_seafile_parse_resp_create_lib( const gchar* json, GError** error )
{
    JsonParser* parser = NULL;
    gboolean ret = FALSE;
    JsonNode* node = NULL;
    gchar* repo_id = NULL;

    parser = json_parser_new( );
    ret = json_parser_load_from_data( parser, json, -1, error );
    if ( !ret )
    {
        g_prefix_error( error, "%s\njson_parser_load_from_data\n", __func__ );
        g_object_unref( parser );

        return NULL;
    }

    node = json_parser_get_root( parser );
    if ( JSON_NODE_HOLDS_OBJECT(node) )
    {
        JsonObject* object = NULL;

        object = json_node_get_object( node );

        if ( json_object_has_member( object, "repo_id" ) )
                repo_id = g_strdup( json_object_get_string_member( object, "repo_id" ) );
        else
        {
            *error = g_error_new( SOND_SERVER_ERROR, 0,
                    "json enthält kein member ""repo_id""" );
            g_object_unref( parser );

            return NULL;
        }
    }
    else
    {
        *error = g_error_new( SOND_SERVER_ERROR, 0,
                "json ist kein object" );
        g_object_unref( parser );

        return NULL;
    }

    g_object_unref( parser );

    return repo_id;
}


static gchar*
sond_server_seafile_do_create_lib( SondServer* sond_server,
        gint reg_nr, gint reg_jahr, GError** error )
{
    CURL *curl = NULL;
    CURLcode res = CURLE_OK;
    gchar *url = NULL;
    gchar* data = NULL;
    struct curl_slist* slist = NULL;
    gchar* header_token = NULL;
    glong http_code = 0;
    CurlUserData mem = { 0 };
    gchar* resp = NULL;

    curl = curl_easy_init();
    if ( !curl )
    {
        *error = g_error_new( g_quark_from_static_string( "CURL" ), 0,
                "%s\ncurl_easy_init nicht erfolgreich", __func__ );
        return NULL;
    }

    url = g_strdup_printf( "%s/api2/repos/", sond_server->seafile_url );
    curl_easy_setopt( curl, CURLOPT_URL, url );
    g_free( url );

    data = g_strdup_printf( "name=%d-%d", reg_jahr, reg_nr );
    curl_easy_setopt( curl, CURLOPT_POSTFIELDS, data );

    header_token = g_strdup_printf( "Authorization: Token %s", sond_server->auth_token );
    slist = curl_slist_append( slist, header_token );
    slist = curl_slist_append( slist, "Accept: application/json; indent=4" );
    curl_easy_setopt( curl, CURLOPT_HTTPHEADER, slist );

    curl_easy_setopt( curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA );
    curl_easy_setopt( curl, CURLOPT_FOLLOWLOCATION, 1L );

    curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, curl_write_cb );
    curl_easy_setopt( curl, CURLOPT_WRITEDATA, &mem);

    res = curl_easy_perform(curl);
    curl_easy_getinfo ( curl, CURLINFO_RESPONSE_CODE, &http_code );

    /* always cleanup */
    curl_easy_cleanup(curl);
    curl_slist_free_all( slist );
    g_free( data );

    if ( res != CURLE_OK )
    {
        *error = g_error_new( g_quark_from_static_string( "CURL" ), 0,
                "%s\ncurl_easy_perform:\n%s", __func__, curl_easy_strerror( res ) );
        free( mem.response );

        return NULL;
    }

    if ( http_code != 200 )
    {
        *error = g_error_new( g_quark_from_static_string( "HTTP" ), 0,
                "%s\ncurl_easy_perform:\n%ld", __func__, http_code );
        free( mem.response );

        return NULL;
    }

    resp = g_strdup( mem.response );
    free( mem.response );

    return resp;
}


static gint
sond_server_seafile_prepare_lib( SondServer* sond_server,
        const gchar* resp, GError** error )
{
    gchar* repo_id = NULL;
    gint rc = 0;

    repo_id = sond_server_seafile_parse_resp_create_lib( resp, error );
    if ( !repo_id )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    rc = sond_server_seafile_share_repo( sond_server, repo_id, error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );
        g_free( repo_id );

        return -1;
    }

    rc = sond_server_seafile_create_dirs( sond_server, repo_id, error );
    g_free( repo_id );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }


    return 0;
}


gint
sond_server_seafile_create_akte( SondServer* sond_server,
        gint reg_nr, gint reg_jahr, GError** error )
{
    gchar* resp = NULL;
    gint rc = 0;

    resp = sond_server_seafile_do_create_lib( sond_server, reg_nr, reg_jahr, error );
    if ( !resp )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    rc = sond_server_seafile_prepare_lib( sond_server, resp, error );
    g_free( resp );
    if ( rc )
    {
        GError* error_tmp = NULL;
        gint res = 0;

        res = sond_server_seafile_delete_lib( sond_server, reg_nr, reg_jahr, &error_tmp );
        if ( res )
        {
            gchar* message = NULL;

            message = g_strdup_printf( "\n\nLibrary ""%d-%d"" konnte "
                    "nicht gelöscht werden\n\n%s",
                    reg_jahr, reg_nr, error_tmp->message );
            g_error_free( error_tmp );

            g_warning( message );

            (*error)->message = add_string( (*error)->message, message );
        }

        return -1;
    }

    return 0;
}
