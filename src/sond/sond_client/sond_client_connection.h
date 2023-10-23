#ifndef SOND_CLIENT_CONNECTION_H
#define SOND_CLIENT_CONNECTION_H

typedef struct _Sond_Client SondClient;

gboolean sond_client_connection_ping( SondClient*, GError** );

#endif //SOND_CLIENT_CONNECTION_H
