#include <glib.h>
#include <gio/gio.h>

#include "../../misc.h"

#include "sond_client.h"

#define MAX_MSG_SIZE 2048

gchar*
sond_client_connection_send_and_read( SondClient* sond_client, const gchar*
        command, const gchar* params, GError** error )
{
    GSocketConnection * connection = NULL;
    GSocketClient * client = NULL;
    gchar imessage[MAX_MSG_SIZE] = { 0 };
    gssize ret = 0;
    gchar* omessage = NULL;

    client = g_socket_client_new();
//    g_socket_client_set_tls( client, TRUE );

    /* connect to the host */
    connection = g_socket_client_connect_to_host (client, sond_client->server_host,
            sond_client->server_port, NULL, error );
    if ( *error )
    {
        g_object_unref( client );
        g_prefix_error( error, "%s\n", __func__ );

        return NULL;
    }

    /* use the connection */
    GInputStream * istream = g_io_stream_get_input_stream (G_IO_STREAM (connection));
    GOutputStream * ostream = g_io_stream_get_output_stream (G_IO_STREAM (connection));

    omessage = g_strconcat( sond_client->user, ":", sond_client->password, ":",
            command, ":", params, NULL );

    g_output_stream_write( ostream, omessage, strlen( omessage ), NULL, error );
    g_free( omessage );
    if ( *error )
    {
        g_object_unref( connection );
        g_object_unref( client );
        g_prefix_error( error, "%s\n", __func__ );

        return NULL;
    }

    ret = g_input_stream_read( istream, imessage, MAX_MSG_SIZE, NULL, error );
    g_object_unref( connection );
    g_object_unref( client );
    if ( *error )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return NULL;
    }
    else if ( ret == 0 )
    {
        *error = g_error_new( SOND_CLIENT_ERROR, SOND_CLIENT_ERROR_NOANSWER, "%s\ninput-stream leer", __func__ );

        return NULL;
    }
    else if ( ret > MAX_MSG_SIZE )
    {
        *error = g_error_new( SOND_CLIENT_ERROR, SOND_CLIENT_ERROR_MESSAGETOOLONG,
                "%s\ninput-stream leer", __func__ );

        return NULL;
    }

    return g_strdup( imessage );
}


gboolean
sond_client_connection_ping( SondClient* sond_client, GError** error )
{
    gchar* rcv_message = NULL;

    rcv_message = sond_client_connection_send_and_read( sond_client, "PING", "", error );
    if ( !rcv_message )
    {
        g_prefix_error( error, "%s\n", __func__ );
        return FALSE;
    }
    else if ( !g_strcmp0( rcv_message, "PONG" ) )
    {
        g_free( rcv_message );

        return TRUE;
    }
    else
    {
        *error = g_error_new( SOND_CLIENT_ERROR, SOND_CLIENT_ERROR_INVALRESP,
                "%s\n%s", __func__, rcv_message );
        g_free( rcv_message );

        return FALSE;
    }
}
