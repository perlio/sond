#include <glib.h>
#include <gio/gio.h>

#include "../../misc.h"

#include "sond_client.h"

#define MAX_MSG_SIZE 2048

gchar*
sond_client_connection_send_and_read( SondClient* sond_client, const gchar*
        message, SondError** sond_error )
{
    GError * error = NULL;
    GSocketConnection * connection = NULL;
    GSocketClient * client = NULL;
    gchar imessage[MAX_MSG_SIZE] = { 0 };
    gssize ret = 0;

    client = g_socket_client_new();
//    g_socket_client_set_tls( client, TRUE );

    /* connect to the host */
    connection = g_socket_client_connect_to_host (client, sond_client->server_host,
            sond_client->server_port, NULL, &error );
    if ( error )
    {
        g_object_unref( client );
        *sond_error = sond_error_new( error, "g_socket_client_connect_to_host" );
        SOND_ERROR_VAL(NULL)
    }

    /* use the connection */
    GInputStream * istream = g_io_stream_get_input_stream (G_IO_STREAM (connection));
    GOutputStream * ostream = g_io_stream_get_output_stream (G_IO_STREAM (connection));

    g_output_stream_write( ostream, message, strlen( message ), NULL, &error );
    if ( error )
    {
        g_object_unref( connection );
        g_object_unref( client );
        *sond_error = sond_error_new( error, "g_output_stream_write" );
        SOND_ERROR_VAL(NULL)
    }

    ret = g_input_stream_read( istream, imessage, MAX_MSG_SIZE, NULL, &error );
    g_object_unref( connection );
    g_object_unref( client );
    if ( error )
    {
        *sond_error = sond_error_new( error, "g_input_stream_read" );
        SOND_ERROR_VAL(NULL)
    }
    else if ( ret == 0 )
    {
        *sond_error = sond_error_new_full( SOND_CLIENT_ERROR,
                SOND_CLIENT_ERROR_NOINPUT, "Antwort leer", __func__ );
        SOND_ERROR_VAL(NULL)
    }
    else if ( ret > MAX_MSG_SIZE )
    {
        *sond_error = sond_error_new_full( SOND_CLIENT_ERROR,
                SOND_CLIENT_ERROR_INPTRUNC, "Antwort abgeschnitten", __func__ );
        SOND_ERROR_VAL(NULL)
    }

    return g_strdup( imessage );
}


gboolean
sond_client_connection_ping( SondClient* sond_client, SondError** sond_error )
{
    gchar* rcv_message = NULL;
    gchar* out_message = NULL;

    out_message = g_strdup_printf( "%s&%s:PING:", sond_client->user, sond_client->password );

    rcv_message = sond_client_connection_send_and_read( sond_client, out_message, sond_error );
    g_free( out_message );
    if ( !rcv_message ) SOND_ERROR_VAL(FALSE)

    if ( !g_strcmp0( rcv_message, "PONG" ) )
    {
        g_free( rcv_message );

        return TRUE;
    }
    else
    {
        g_free( rcv_message );
        *sond_error = sond_error_new_full( SOND_CLIENT_ERROR, SOND_CLIENT_ERROR_INVALRESP,
                "Server antwortet nicht mit 'PONG'", __func__ );

        return FALSE;
    }
}
