#include <glib.h>
#include <gio/gio.h>

#include "sond_client.h"

#define MAX_MSG_SIZE 2048

gchar*
sond_client_connection_send_and_read( SondClient* sond_client, const gchar*
        message, GError** error )
{
    GError * error_tmp = NULL;
    GSocketConnection * connection = NULL;
    GSocketClient * client = NULL;
    gchar imessage[MAX_MSG_SIZE] = { 0 };
    gssize ret = 0;

    client = g_socket_client_new();
    g_socket_client_set_tls( client, TRUE );

    /* connect to the host */
    connection = g_socket_client_connect_to_host (client, sond_client->server_host,
            sond_client->server_port, NULL, &error_tmp );
    if ( error_tmp )
    {
        g_set_error( error, SOND_CLIENT_ERROR, SOND_CLIENT_ERROR_KEINSERVER,
                "Keine Verbindung zum Server:\n%s",
                error_tmp->message );
        g_error_free( error_tmp );
        g_object_unref( client );

        return NULL;
    }
    /* use the connection */
    GInputStream * istream = g_io_stream_get_input_stream (G_IO_STREAM (connection));
    GOutputStream * ostream = g_io_stream_get_output_stream (G_IO_STREAM (connection));

    g_output_stream_write( ostream, message, strlen( message ), NULL, &error_tmp );
    if ( error_tmp )
    {
        g_set_error( error, SOND_CLIENT_ERROR, SOND_CLIENT_ERROR_OMESSAGE,
                "Senden Nachricht an Server fehlgeschlagen:\n%s",
                error_tmp->message );
        g_error_free( error_tmp );
        g_object_unref( connection );
        g_object_unref( client );

        return NULL;
    }

    ret = g_input_stream_read( istream, imessage, MAX_MSG_SIZE, NULL, &error_tmp );
    if ( error )
    {
        g_set_error( error, SOND_CLIENT_ERROR, SOND_CLIENT_ERROR_IMESSAGE,
                "Empfang Nachricht von Server fehlgeschlagen:\n%s",
                error_tmp->message );
        g_error_free( error_tmp );
        g_object_unref( connection );
        g_object_unref( client );

        return NULL;
    }
    else if ( ret == 0 )
    {
        g_set_error( error, SOND_CLIENT_ERROR, SOND_CLIENT_ERROR_IMESSAGE,
                "%s:\ng_input_stream_read liest 0 Byte", __func__ );
        g_object_unref( connection );
        g_object_unref( client );

        return NULL;
    }
    else if ( ret > MAX_MSG_SIZE )
    {
        g_set_error( error, SOND_CLIENT_ERROR, SOND_CLIENT_ERROR_IMESSAGE,
                "Empfang Nachricht von Server fehlgeschlagen:\n"
                "Nachricht abgeschnitten" );
        g_object_unref( connection );
        g_object_unref( client );

        return NULL;
    }

    g_object_unref( connection );
    g_object_unref( client );

    return g_strdup( imessage );
}


gboolean
sond_client_connection_ping( SondClient* sond_client, GError** error )
{
    GError* error_tmp = NULL;
    gchar* rcv_message = NULL;

    rcv_message = sond_client_connection_send_and_read( sond_client, "PING::", &error_tmp );
    if ( error_tmp )
    {
        if ( error_tmp->code == SOND_CLIENT_ERROR_KEINSERVER )
        {
            g_error_free( error_tmp );

            return FALSE;
        }
        else
        {
            g_set_error( error, SOND_CLIENT_ERROR, error_tmp->code, "%s:\n%s",
                    __func__, error_tmp->message );
            g_error_free( error_tmp );

            return FALSE;
        }
    }

    if ( !g_strcmp0( rcv_message, "PONG" ) )
    {
        g_free( rcv_message );

        return TRUE;
    }
    else
    {
        g_free( rcv_message );
        g_set_error( error, SOND_CLIENT_ERROR, SOND_CLIENT_ERROR_INVRESPONSE,
                "%s:\nUngültige Antwort von Server", __func__ );

        return FALSE;
    }
}
