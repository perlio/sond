#include <libgen.h>         // dirname
#include <unistd.h>         // readlink
#include <linux/limits.h>   // PATH_MAX
#include <gtk/gtk.h>
#include <stdio.h>
#include <unistd.h>

#define G_LOG_DOMAIN "SondServer"

typedef struct _Sond_Server
{
    gchar* base_dir;
    gchar* log_file;
    GMainLoop* loop;
} SondServer;


#define MAX_MSG_SIZE 2048

static gboolean
callback_socket_incoming( GSocketService *service,
                        GSocketConnection *connection,
                        GObject *source_object,
                        gpointer data)
{
    gssize ret = 0;
    GError* error = NULL;
    GInputStream* istream = NULL;
    GOutputStream* ostream = NULL;
    gchar imessage[MAX_MSG_SIZE] = { 0 };
    gchar* omessage = NULL;
    SondServer* sond_server = NULL;

    sond_server = data;

    istream = g_io_stream_get_input_stream( G_IO_STREAM(connection) );
    ostream = g_io_stream_get_output_stream( G_IO_STREAM (connection) );

    ret = g_input_stream_read( istream, imessage, MAX_MSG_SIZE, NULL, &error );
    if ( error )
    {
        g_warning( "input-stream konnte nicht gelesen werden: %s", error->message );
        g_error_free( error );
        return FALSE;
    }
    else if ( ret == 0 )
    {
        g_warning( "input-stream hat keinen Inhalt" );
        omessage = g_strdup( "NO_MESSAGE" );
    }
    else if ( ret > MAX_MSG_SIZE )
    {
        g_message( "Nachricht abgeschnitten" );
        omessage = g_strdup( "MESSAGE_TRUNCATED" );
    }

    if ( g_str_has_prefix( imessage, "SHUTDOWN" ) )
    {
        g_main_loop_quit( sond_server->loop );
        return FALSE;
    }
    else if ( g_str_has_prefix( imessage, "AKTE_ANLEGEN" ) )
    {

    }
    else
    {
        g_warning( "Nachricht enthält keinen bekannten Befehl" );
        omessage = g_strdup( "NO_KNOWN_COMMAND" );
    }

    ret = g_output_stream_write( ostream, omessage, strlen( omessage ), NULL, &error );
    g_free( omessage );
    if ( error )
    {
        g_warning( "Antwort '%s' konnte nicht an client geschickt werden: %s",
                omessage, error->message );
        g_error_free( error );
    }

    return FALSE;
}


static void
sond_server_free( SondServer* sond_server )
{
    g_free( sond_server->base_dir );
    g_free( sond_server->log_file );

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
    sond_server->log_file = g_strconcat( sond_server->base_dir, "/logs/log_",
            datetime_iso, ".log", NULL );
    g_free( datetime_iso );

    file = freopen( sond_server->log_file, "a", stdout );
    if ( !file ) g_error( "stout konnte nicht in Datei %s umgeleitet werden: %s", sond_server->log_file, strerror( errno ) );

    ret = dup2( fileno( stdout ), fileno( stderr ) );
    if ( ret == -1 ) g_error( "stderr konnte nicht umgeleitet werden: %s", strerror( errno ) );

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
    GInetAddress* inet_address = NULL;
    GSocketAddress* socket_address = NULL;
    gboolean success = FALSE;
    gchar* ip_address = NULL;
    guint16 port = 0;
    gchar* conf_file = NULL;
    gchar result[PATH_MAX] = { 0 };
    ssize_t count = 0;

    //Arbeitserzeichnis ermitteln
    count = readlink("/proc/self/exe", result, PATH_MAX);
    if (count == -1) g_error( "Programmverzeichnis konnte nicht ermittelt werden - %s", strerror( errno ) );
    sond_server.base_dir = g_path_get_dirname( result );

    log_init( &sond_server );

    keyfile = g_key_file_new( );

    conf_file = g_strconcat( sond_server.base_dir, "/SondServer.conf", NULL );
    success = g_key_file_load_from_file( keyfile, conf_file,
            G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS, &error );
    g_free( conf_file );
    if ( success )
    {
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

        g_key_file_free( keyfile );

    }
    else
    {
        g_message( "SondServer.conf konnte nicht gelesen werden: %s "
                "- Default Interface (localhost) und Port (35002) werden verwendet",
                error->message );
        g_clear_error ( &error );
        ip_address = g_strdup( "127.0.0.1" );
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

    g_signal_connect (socket, "incoming", G_CALLBACK (callback_socket_incoming), &sond_server );

    loop = g_main_loop_new(NULL, FALSE);
    sond_server.loop = loop;

    g_main_loop_run(loop);

    g_message( "Shutdown" );
    sond_server_free( &sond_server );

    g_socket_listener_close( G_SOCKET_LISTENER(socket) );
    g_object_unref( socket );
    g_main_loop_unref(loop);

    return 0;
}
