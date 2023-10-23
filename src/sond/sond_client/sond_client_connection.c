#include <glib.h>
#include <gio/gio.h>

#include "../../misc.h"

#include "sond_client.h"

gboolean
sond_client_connection_ping( SondClient* sond_client, GError** error )
{
    gchar* rcv_message = NULL;

    rcv_message = sond_client_send_and_read( sond_client, "PING", "", error );
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
