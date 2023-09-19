#ifndef SOND_CLIENT_H_INCLUDED
#define SOND_CLIENT_H_INCLUDED

#define SOND_CLIENT_ERROR sond_client_error_quark()
G_DEFINE_QUARK(sond-client-error-quark,sond_client_error)

#define DISPLAY_SOND_ERROR {display_message( sond_client->app_window, \
        "function stack:\n", \
        (sond_error->function_stack) ? (sond_error->function_stack) : "", \
        sond_error->origin, "\nerror-message:\n", \
        sond_error->error->message, NULL ); \
        sond_error_free( sond_error ); \
}

enum SondClientError
{
    SOND_CLIENT_ERROR_INVALRESP,
    SOND_CLIENT_ERROR_NOINPUT,
    SOND_CLIENT_ERROR_INPTRUNC,
    SOND_CLIENT_ERROR_NOANSWER,
    SOND_CLIENT_ERROR_MESSAGETOOLONG,
    NUM_SOND_CLIENT_ERROR
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
    gchar* seafile_root;
    gchar* seadrive_root;

    gchar* user;
    gchar* password;

    gchar* server_host;
    guint16 server_port;
    gchar* server_user;

    gint reg_jahr_akt;
    gint reg_nr_akt;
} SondClient;

void sond_client_quit( SondClient* );

#endif // SOND_CLIENT_H_INCLUDED
