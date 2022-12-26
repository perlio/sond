#include <gtk/gtk.h>
#ifdef __WIN32
#include <windows.h>
#include <wincrypt.h>
#endif // __WIN32

#include "../../misc.h"

#include "sond_client.h"
#include "sond_client_file_manager.h"

//#include "libsearpc/searpc-client.h"
#include "libsearpc/searpc-named-pipe-transport.h"

#define SEAFILE_SOCKET_NAME "seadrive.sock"



static gboolean
sond_client_close( GtkWidget* app_window, GdkEvent* event, gpointer data )
{
    SondClient* sond_client = (SondClient*) data;
    g_free( sond_client->base_dir );
    g_free( sond_client->seadrive_root );
    g_free( sond_client->seadrive );
    g_free( sond_client->server_host );
    g_free( sond_client->server_user );

    g_ptr_array_unref( sond_client->arr_file_manager );

    if ( sond_client->searpc_client )
            searpc_free_client_with_pipe_transport( sond_client->searpc_client );

    gtk_widget_destroy( sond_client->app_window );

    return TRUE;
}


void
sond_client_quit( SondClient* sond_client )
{
    sond_client_close( NULL, NULL, sond_client );

    return;
}


#ifdef __WIN32
static char *b64encode(const char *input)
{
    char buf[32767] = {0};
    DWORD retlen = 32767;
    CryptBinaryToStringA((BYTE*) input, strlen(input), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, buf, &retlen);
    return strdup(buf);
}
#endif //__WIN32


static void
sond_client_init_rpc_client( SondClient* sond_client )
{
    SearpcNamedPipeClient* searpc_named_pipe_client = NULL;
    gchar* path = NULL;
    gint ret = 0;

#ifdef __WIN32
    char userNameBuf[32767];
    DWORD bufCharCount = sizeof(userNameBuf);
    if (GetUserNameA(userNameBuf, &bufCharCount) == 0)
    {
        gchar* display_text = g_strdup_printf( "Failed to get user name, "
                "GLE=%lu, required size is %lu\n", GetLastError(), bufCharCount );
        display_message( sond_client->app_window, display_text, NULL );
        g_free( display_text );

        sond_client_quit( sond_client );
        return;
    }

    path = g_strdup_printf("\\\\.\\pipe\\seadrive_%s", b64encode(userNameBuf));
#else
    path = g_build_filename ( sond_client->seadrive, SEAFILE_SOCKET_NAME, NULL);
#endif

    searpc_named_pipe_client = searpc_create_named_pipe_client( path );
    g_free( path );
    ret = searpc_named_pipe_client_connect( searpc_named_pipe_client );
    if ( ret == -1 )
    {
        display_message( sond_client->app_window, "Fehler - RPC-Client konnte "
                "nicht mit Server verbunden werden", NULL );
        g_free( searpc_named_pipe_client->path );
        g_free( searpc_named_pipe_client );

        sond_client_quit( sond_client );
    }
    sond_client->searpc_client = searpc_client_with_named_pipe_transport( searpc_named_pipe_client,
            "seadrive-rpcserver" );

    return;
}


static void
sond_client_init_app_window( GtkApplication* app, SondClient* sond_client )
{
    GtkWidget* grid = NULL;
    GtkWidget* frame_dok = NULL;
    GtkWidget* entry_dok = NULL;

    sond_client->app_window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_title( GTK_WINDOW(sond_client->app_window), "SondClient" );
    gtk_widget_set_size_request( sond_client->app_window, 250, 40 );
    gtk_application_add_window( app, GTK_WINDOW(sond_client->app_window) );

    grid = gtk_grid_new( );
    gtk_container_add( GTK_CONTAINER(sond_client->app_window), grid );

    frame_dok = gtk_frame_new( "Dokumentenverzeichnis öffnen" );
    entry_dok = gtk_entry_new( );
    gtk_container_add( GTK_CONTAINER(frame_dok), entry_dok );
    gtk_grid_attach( GTK_GRID(grid), frame_dok, 0, 0, 1, 1 );

    gtk_widget_show_all( sond_client->app_window );

    g_signal_connect( sond_client->app_window, "delete-event",
            G_CALLBACK(sond_client_close), sond_client );
    g_signal_connect( entry_dok, "activate",
            G_CALLBACK(sond_client_file_manager_entry_activate), sond_client );

    return;
}


static void
sond_client_get_conf( SondClient* sond_client )
{
    GKeyFile* key_file = NULL;
    gchar* conf_path = NULL;
    gboolean success = FALSE;
    GError* error = NULL;

    key_file = g_key_file_new( );

    conf_path = g_build_filename( sond_client->base_dir, "SondClient.conf", NULL );
    success = g_key_file_load_from_file( key_file, conf_path,
            G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS, &error );
    g_free( conf_path );
    if ( !success ) g_error( "SondClient.conf konnte nicht gelesen werden:\n%s",
            error->message );

    sond_client->server_host = g_key_file_get_string( key_file, "SERVER", "host", &error );
    if ( error ) g_error( "Server Host konnte nicht ermittelt werden:\n%s", error->message );

    sond_client->server_port = (guint16) g_key_file_get_uint64( key_file, "SERVER", "port", &error );
    if ( error ) g_error( "Server Port konnte nicht ermittelt werden:\n%s", error->message );

    sond_client->server_user = g_key_file_get_string( key_file, "SERVER", "user", &error );
    if ( error ) g_error( "Server User konnte nicht ermittelt werden:\n%s", error->message );

    sond_client->seadrive_root = g_key_file_get_string( key_file, "SEADRIVE", "root", &error );
    if ( error ) g_error( "Seadrive-Root-Dir konnte nicht ermittelt werden:\n%s", error->message );

    sond_client->seadrive = g_key_file_get_string( key_file, "SEADRIVE", "seadrive", &error );
    if ( error ) g_error( "Seadrive-Dir konnte nicht ermittelt werden:\n%s", error->message );

    g_key_file_free( key_file );

    return;
}


static void
sond_client_init( GtkApplication* app, SondClient* sond_client )
{
    gchar* errmsg = NULL;

    sond_client->arr_file_manager = g_ptr_array_new( );
    g_ptr_array_set_free_func( sond_client->arr_file_manager,
            (GDestroyNotify) sond_client_file_manager_free );

    sond_client_init_app_window( app, sond_client );

    sond_client->base_dir = get_base_dir( &errmsg );
    if ( !sond_client->base_dir )
    {
        display_message( sond_client->app_window, "Fehler - base_dir konnte "
                "nicht ermittelt werden:\n", errmsg, NULL );
        g_free( errmsg );

        sond_client_quit( sond_client );
    }

    sond_client_get_conf( sond_client );

    sond_client_init_rpc_client( sond_client );

    sond_client_seadrive_test_seadrive_server( sond_client );

    return;
}


static void
activate_app( GtkApplication* app, gpointer data )
{
    SondClient* sond_client = (SondClient*) data;

    gtk_window_present( GTK_WINDOW(sond_client->app_window) );

    return;
}


static void
startup_app( GtkApplication* app, gpointer data )
{
    SondClient* sond_client = (SondClient*) data;

    sond_client_init( app, sond_client );

    return;
}


gint
main( int argc, char **argv)
{
    GtkApplication* app = NULL;
    SondClient sond_client = { 0 };
    gint status = 0;

    //ApplicationApp erzeugen
    app = gtk_application_new ( "de.rubarth-krieger.sond_client", G_APPLICATION_DEFAULT_FLAGS );

    //und starten
    g_signal_connect( app, "startup", G_CALLBACK(startup_app), &sond_client );
    g_signal_connect( app, "activate", G_CALLBACK (activate_app), &sond_client );

    status = g_application_run( G_APPLICATION (app), argc, argv );

    g_object_unref( app );

    return status;
}
