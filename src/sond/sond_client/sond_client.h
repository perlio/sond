#ifndef SOND_CLIENT_H_INCLUDED
#define SOND_CLIENT_H_INCLUDED


#define SOND_CLIENT_ERROR sond_client_error_quark()
G_DEFINE_QUARK(sond-client-error-quark, sond_client_error)

enum SondClientError
{
    SOND_CLIENT_ERROR_KEINSERVER,
    SOND_CLIENT_ERROR_OMESSAGE,
    SOND_CLIENT_ERROR_IMESSAGE,
    SOND_CLIENT_ERROR_INVRESPONSE,
    SOND_CLIENT_ERROR_KEINEAKTE
};

typedef struct _GtkWidget GtkWidget;
typedef char gchar;
typedef struct _SearpcClient SearpcClient;
typedef struct _GList GList;

typedef struct _Sond_Client
{
    GtkWidget* app_window;
    GPtrArray* arr_file_manager;
    gchar* base_dir;
    SearpcClient* searpc_client;
    gchar* seadrive_root;
    gchar* seadrive;

    gchar* server_host;
    guint16 server_port;
    gchar* server_user;
} SondClient;

void sond_client_quit( SondClient* );

#endif // SOND_CLIENT_H_INCLUDED