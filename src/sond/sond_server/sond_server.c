#include <libgen.h>         // dirname
#include <unistd.h>         // readlink
#include <gio/gio.h>
#include <stdio.h>
#include <mysql.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

#ifdef __linux__
#include <linux/limits.h>   // PATH_MAX
#endif // __linux__

#include "../../misc.h"

#define G_LOG_DOMAIN "SondServer"

typedef struct _Sond_Server
{
    GMainLoop* loop;
    gchar* password;
    gchar* base_dir;
    gchar* log_file;
    gchar* mysql_host;
    gint mysql_port;
    gchar* mysql_user;
    gchar* mysql_password;
    gchar* mysql_db;
    gchar* seafile_user;
    gchar* seafile_password;
    gchar* seafile_url;
    gchar* auth_token;
    GThreadPool* thread_pool;
    GArray* arr_creds;
    GMutex mutex_arr_creds;
} SondServer;


gchar*
sond_server_seafile_get_auth_token( SondServer* sond_server, const gchar* user, const gchar* password, gchar** errmsg )
{
    SoupSession* soup_session = NULL;
    SoupMessage* soup_message = NULL;
    gchar* url_text = NULL;
    gchar* body_text = NULL;
    GBytes* body = NULL;
    GBytes* response = NULL;
    JsonParser* parser = NULL;
    JsonNode* node = NULL;
    GError* error = NULL;
    gchar* auth_token = NULL;

    url_text = g_strdup_printf( "%s/api2/auth-token/", sond_server->seafile_url );
    soup_session = soup_session_new( );
    soup_message = soup_message_new( SOUP_METHOD_POST, url_text );
    g_free( url_text );

    body_text = g_strdup_printf( "username=%s&password=%s", user, password );
    body = g_bytes_new( body_text, strlen( body_text ) );
    g_free( body_text );
    soup_message_set_request_body_from_bytes( soup_message, "application/x-www-form-urlencoded", body );
    g_bytes_unref( body );

    response = soup_session_send_and_read( soup_session, soup_message, NULL, &error );
    g_object_unref( soup_message );
    g_object_unref( soup_session );
    if ( error )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Keine Antwort vom SeafileServer\n\n"
                "Bei Aufruf soup_session_send_and_read:\n", error->message, NULL );
        g_error_free( error );

        return NULL;
    }

    parser = json_parser_new( );
    if ( !json_parser_load_from_data( parser, g_bytes_get_data( response, NULL ), -1, &error ) )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Antwort vom SeafileServer nicht im json-Format\n\n"
                "Bei Aufruf json_parser_load_from_data:\n", error->message,
                "\n\nEmpfangene Nachricht:\n", g_bytes_get_data( response, NULL ), NULL );
        g_error_free( error );

        return NULL;
    }

    node = json_parser_get_root( parser );
    if ( JSON_NODE_HOLDS_OBJECT(node) )
    {
        JsonObject* object = NULL;

        object = json_node_get_object( node );

        if ( json_object_has_member( object, "token" ) )
                auth_token = g_strdup( json_object_get_string_member( object, "token" ) );
        else
        {
            if ( errmsg ) *errmsg = g_strconcat( "Antwort vom SeafileServer\n\n"
                    "json hat keim member ""token""\n\nEmpfangene Nachricht:\n",
                    g_bytes_get_data( response, NULL ), NULL );

            return NULL;
        }
    }
    else
    {
        if ( errmsg ) *errmsg = g_strconcat( "Antwort vom SeafileServer\n\n"
                "json ist kein object\n\nEmpfangene Nachricht:\n",
                g_bytes_get_data( response, NULL ), NULL );

        return NULL;
    }

    g_object_unref( parser );
    g_bytes_unref( response );

    return auth_token;
}

typedef struct _Cred
{
    gchar* user;
    gchar* password;
} Cred;


static void
free_cred( Cred* cred )
{
    g_free( cred->user );
    g_free( cred->password );

    return;
}


static gint
get_auth_level( SondServer* sond_server, const gchar* auth )
{
    gchar* user = NULL;
    gchar* password = NULL;
    Cred cred = { 0 };
    gchar* errmsg = NULL;
    gchar* auth_token = NULL;
    gint i = 0;

    password = g_strrstr( auth, "&" ) + 1;
    user = g_strndup( auth, strlen( auth ) - strlen( password ) - 1 );

    //mutex user
    g_mutex_lock( &sond_server->mutex_arr_creds );

    for ( i = 0; i < sond_server->arr_creds->len; i++ )
    {
        cred = g_array_index( sond_server->arr_creds, Cred, i );
        if ( !g_strcmp0( cred.user, user ) ) break;
    }

    g_mutex_unlock( &sond_server->mutex_arr_creds );

    if ( i < sond_server->arr_creds->len &&
            !g_strcmp0( cred.password, password ) ) return 1;

    auth_token = sond_server_seafile_get_auth_token( sond_server, user, password, &errmsg );
    if ( !auth_token )
    {
        g_warning( "User konnte nicht legitimiert werden:\n%s", errmsg );
        g_free( errmsg );

        return 0;
    }

    //brauchen wir nicht
    g_free( auth_token );

    g_mutex_lock( &sond_server->mutex_arr_creds );
    if ( i < sond_server->arr_creds->len )
    {
        g_free( (((Cred*) (void*) (sond_server->arr_creds)->data)[i]).password );
        (((Cred*) (void*) (sond_server->arr_creds)->data)[i]).password = g_strdup( password );
    }
    else
    {
        Cred cred = { 0 };

        cred.user = g_strdup( user );
        cred.password = g_strdup( password );

        g_array_append_val( sond_server->arr_creds, cred );
    }
    g_mutex_unlock( &sond_server->mutex_arr_creds );

    return 1;
}


static gint
process_imessage( SondServer* sond_server, const gchar* auth, const gchar* command, const gchar* params,
        gchar** omessage )
{
    gint auth_level = 0;

    auth_level = get_auth_level( sond_server, auth );
    if ( auth_level == 0 )
    {
        *omessage = g_strdup( "ERROR *** AUTHENTICATION FAILED" );
        return 0;
    }

    if ( g_strcmp0( command, "PING" ) ) *omessage = g_strdup( "PONG" );
    else if ( g_strcmp0( command, "SHUTDOWN" ) )
    {
        *omessage = g_strdup( "SONDSERVER_OK" );

        return 1;
    }
    else if ( g_strcmp0( command, "NEUE_AKTE" ) )
    {

    }
    else
    {
        g_warning( "Nachricht enthält keinen bekannten Befehl" );
        *omessage = g_strdup( "ERROR *** NO_KNOWN_COMMAND" );
    }

    return 0;
}


#define MAX_MSG_SIZE 2048

static void
sond_server_process_message( gpointer data, gpointer user_data )
{
    gssize ret = 0;
    GError* error = NULL;
    GInputStream* istream = NULL;
    GOutputStream* ostream = NULL;
    gchar imessage[MAX_MSG_SIZE] = { 0 };
    gchar* omessage = NULL;
    SondServer* sond_server = NULL;
    GSocketConnection* connection = NULL;
    gchar** imessage_strv = NULL;
    gint rc = 0;

    sond_server = data;
    connection = user_data;

    istream = g_io_stream_get_input_stream( G_IO_STREAM(connection) );
    ostream = g_io_stream_get_output_stream( G_IO_STREAM (connection) );

    ret = g_input_stream_read( istream, imessage, MAX_MSG_SIZE, NULL, &error );
    if ( error )
    {
        g_warning( "input-stream konnte nicht gelesen werden: %s", error->message );
        g_error_free( error );

        omessage = g_strdup( "ERROR *** COULD_NOT_READ_MESSAGE" );
    }
    else if ( ret == 0 )
    {
        g_warning( "input-stream hat keinen Inhalt" );
        omessage = g_strdup( "ERROR *** NO_MESSAGE" );
    }
    else if ( ret > MAX_MSG_SIZE )
    {
        g_warning( "Nachricht abgeschnitten" );
        omessage = g_strdup( "ERROR *** MESSAGE_TRUNCATED" );
    }
    else
    {
        imessage_strv = g_strsplit( imessage, ":", 3 );

        if ( imessage_strv[0] == NULL ) //empty vector
        {
            g_warning( "input-stream ist leer" );
            omessage = g_strdup( "ERROR *** EMPTY_MESSAGE" );
        }
        else
        {
            if ( imessage_strv[1] == NULL ) //
            {
                g_warning( "no-command" );
                g_strfreev( imessage_strv );
                omessage = g_strdup( "ERROR *** NO COMMAND" );
            }
            else
            {
                if ( imessage_strv[2] == NULL )
                {
                    g_warning( "no params" );
                    g_strfreev( imessage_strv );
                    omessage = g_strdup( "ERROR *** NO PARAMS" );
                }
                else
                {
                    rc = process_imessage( sond_server, imessage_strv[0], imessage_strv[1],
                            imessage_strv[2], &omessage );
                    g_strfreev( imessage_strv );
                }
            }
        }
    }

    ret = g_output_stream_write( ostream, omessage, strlen( omessage ), NULL, &error );
    g_free( omessage );
    if ( error )
    {
        g_warning( "Antwort '%s' konnte nicht an client geschickt werden: %s",
                omessage, error->message );
        g_error_free( error );
    }

    if ( rc )
    {
        g_message( "Server wird heruntergefahren " );
        g_main_loop_quit( sond_server->loop );
    }

    g_object_unref( connection );

    return;
}


static gboolean
callback_socket_incoming( GSocketService *service,
                        GSocketConnection *connection,
                        GObject *source_object,
                        gpointer data)
{
    GError* error = NULL;
    SondServer* sond_server = NULL;
    GOutputStream* ostream = NULL;

    sond_server = data;

    if ( !g_thread_pool_push( sond_server->thread_pool, g_object_ref( connection ), &error ) )
    {
        const gchar* omessage = "ERROR *** SERVER NO THREAD";

        g_warning( "thread konnte nicht gepusht werden: %s", error->message );
        g_clear_error( &error );

        ostream = g_io_stream_get_output_stream( G_IO_STREAM (connection) );
        g_output_stream_write( ostream, omessage, strlen( omessage ), NULL, &error );
        if ( error )
        {
            g_warning( "Fehlermeldung konnte nicht an client geschickt werden:\n %s",
                    error->message );
            g_error_free( error );
        }
    }


    return FALSE;
}


static void
sond_server_free( SondServer* sond_server )
{
    g_free( sond_server->mysql_host );
    g_free( sond_server->mysql_user );
    g_free( sond_server->mysql_db );

    g_free( sond_server->password );
    g_free( sond_server->base_dir );
    g_free( sond_server->log_file );
    g_free( sond_server->seafile_password );
    g_free( sond_server->seafile_user );
    g_free( sond_server->seafile_url );
    g_free( sond_server->auth_token );

    g_thread_pool_free( sond_server->thread_pool, TRUE, TRUE );

    return;
}


static void
log_init( SondServer* sond_server )
{
    gint ret = 0;
    FILE* file = NULL;
    GDateTime* date_time = NULL;
    gchar* datetime_iso = NULL;

    date_time = g_date_time_new_now_local( );
    datetime_iso = g_date_time_format_iso8601( date_time );
    g_date_time_unref( date_time );
    sond_server->log_file = g_strconcat( sond_server->base_dir, "logs/log_SondServer_", NULL);
            //datetime_iso, ".log", NULL );
    g_free( datetime_iso );

    file = freopen( sond_server->log_file, "a", stdout );
    if ( !file ) g_error( "stout konnte nicht in Datei %s umgeleitet werden: %s", sond_server->log_file, strerror( errno ) );

    ret = dup2( fileno( stdout ), fileno( stderr ) );
    if ( ret == -1 ) g_error( "stderr konnte nicht umgeleitet werden: %s", strerror( errno ) );

    return;
}


static void
init_socket_service( GKeyFile* keyfile, SondServer* sond_server )
{
    GSocketService* socket = NULL;
    GInetAddress* inet_address = NULL;
    GSocketAddress* socket_address = NULL;
    gboolean success = FALSE;
    gchar* ip_address = NULL;
    guint16 port = 0;
    GError* error = NULL;

    ip_address = g_key_file_get_string( keyfile, "SOCKET", "bind", &error );
    if ( error )
    {
        g_message( "bind-address konnte nicht ermittelt werden: %s - bind to localhost", error->message );
        g_clear_error( &error );
        ip_address = g_strdup( "127.0.0.1" );
    }

    port = g_key_file_get_uint64( keyfile, "SOCKET", "port", &error );
    if ( error )
    {
        g_message( "port konnte nicht ermittelt werden: %s "
            "- default port (35002) wird verwendet", error->message );
        g_clear_error( &error );
        port = 35002;
    }

    inet_address = g_inet_address_new_from_string( ip_address );
    g_free( ip_address );
    if ( !inet_address )
    {
        g_message( "bind-address %s konnte nicht geparst werden - verwende localhost", ip_address );
        inet_address = g_inet_address_new_from_string( "127.0.0.1" );
        if ( !inet_address ) g_error( "GInetAddress konte nicht erzeugt werden" );
    }

    socket_address = g_inet_socket_address_new( inet_address, port );
    g_object_unref( inet_address );
    if ( !socket_address ) g_error( "GSocketAddress konnte nicht erzeugt werden" );

    socket = g_socket_service_new ( );

    success = g_socket_listener_add_address( G_SOCKET_LISTENER(socket), socket_address,
            G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP, NULL, NULL, &error );
    g_object_unref( socket_address );
    if ( !success ) g_error( "g_socket_listener_add_address gibt Fehler zurück: %s", error->message );

    g_signal_connect (socket, "incoming", G_CALLBACK (callback_socket_incoming), sond_server );

    return;
}


static void
get_auth_token( GKeyFile* keyfile, SondServer* sond_server )
{
    GError* error = NULL;
    gchar* errmsg = NULL;

    sond_server->seafile_user = g_key_file_get_string( keyfile, "SEAFILE", "user", &error );
    if ( error ) g_error( "Seafile-user konnte nicht ermittelt werden:\n%s",
            error->message );

    sond_server->seafile_url = g_key_file_get_string( keyfile, "SEAFILE", "url", &error );
    if ( error ) g_error( "SEAFILE-host konnte nicht ermittelt werden:\n%s",
            error->message );

    sond_server->auth_token = sond_server_seafile_get_auth_token( sond_server, sond_server->seafile_user, sond_server->seafile_password, &errmsg );
    if ( !sond_server->auth_token ) g_error( "AuthToken konnte nicht ermittelt werden:\n%s", errmsg );

    return;
}


static void
init_con( GKeyFile* key_file, SondServer* sond_server )
{
    GError* error = NULL;
    MYSQL* con = NULL;

    sond_server->mysql_host = g_key_file_get_string( key_file, "MARIADB", "host", &error );
    if ( error ) g_error( "MariaDB-host konnte nicht ermittelt werden:\n%s",
            error->message );

    sond_server->mysql_port = g_key_file_get_integer( key_file, "MARIADB", "port", &error );
    if ( error ) g_error( "MariaDB-port konnte nicht ermittelt werden:\n%s",
            error->message );

    sond_server->mysql_user = g_key_file_get_string( key_file, "MARIADB", "user", &error );
    if ( error ) g_error( "MariaDB-user konnte nicht ermittelt werden:\n%s",
            error->message );

    sond_server->mysql_db = g_key_file_get_string( key_file, "MARIADB", "db", &error );
    if ( error ) g_error( "MariaDB-db-Name konnte nicht ermittelt werden:\n%s",
            error->message );
g_message( "init sql-con..." );
    con = mysql_init( NULL );
    mysql_ssl_set( con, NULL, NULL, "C:\\msys64\\home\\nc-kr\\sond\\rubarth-krieger-crt.pem", NULL, NULL );
    if ( !mysql_real_connect( con, sond_server->mysql_host, sond_server->mysql_user, sond_server->mysql_password,
            sond_server->mysql_db, sond_server->mysql_port, NULL, CLIENT_MULTI_STATEMENTS ) )
            g_error( "Verbindung zur Datenbank konnte nicht hergestellt "
            "werden:\n%s", mysql_error( con ) );
g_message( "con initialised" );
    //wenn klappt, dann schließen
    mysql_close( con );

    return;
}


gint
main( gint argc, gchar** argv )
{
    GError* error = NULL;
    GSocketService* socket = NULL;
    GMainLoop *loop = NULL;
    SondServer sond_server = { 0 };
    GKeyFile* keyfile = NULL;
    gchar* conf_file = NULL;
    gboolean success = FALSE;

    if ( argc != 4 ) g_error( "Usage: SondServer [password SondServer] [password Mariadb-user] [password Seafile-user]" );
    sond_server.password = argv[1];
    sond_server.mysql_password = argv[2];
    sond_server.seafile_password = argv[3];

    //Arbeitserzeichnis ermitteln
    sond_server.base_dir = get_base_dir( );

#ifdef TESTING
//    log_init( &sond_server );
#endif // TESTING

    keyfile = g_key_file_new( );

    conf_file = g_strconcat( sond_server.base_dir, "SondServer.conf", NULL );
    success = g_key_file_load_from_file( keyfile, conf_file,
            G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS, &error );
    g_free( conf_file );
    if ( !success ) g_error( "SondServer.conf konnte nicht gelesen werden:\n%s",
            error->message );

    get_auth_token( keyfile, &sond_server );
    init_con( keyfile, &sond_server );
    init_socket_service( keyfile, &sond_server );

    g_key_file_free( keyfile );

    //thread_pool erzeugen
    sond_server.thread_pool = g_thread_pool_new( sond_server_process_message, &sond_server, -1, FALSE, &error );

    //arr_creds
    sond_server.arr_creds = g_array_new( FALSE, FALSE, sizeof( Cred ) );
    g_array_set_clear_func( sond_server.arr_creds, (GDestroyNotify) free_cred );

    //mutex
    g_mutex_init( &sond_server.mutex_arr_creds );

    sond_server.loop = g_main_loop_new( NULL, FALSE );

    g_main_loop_run( sond_server.loop );

    g_message( "Shutdown" );
    sond_server_free( &sond_server );

    g_socket_listener_close( G_SOCKET_LISTENER(socket) );
    g_object_unref( socket );
    g_main_loop_unref( loop );

    return 0;
}
