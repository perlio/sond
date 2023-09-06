#include <gtk/gtk.h>
#ifdef __WIN32
#include <windows.h>
#include <wincrypt.h>
#endif // __WIN32

#include "../../misc.h"
#include "../../zond/99conv/general.h"

#include "sond_client.h"
#include "sond_client_connection.h"
#include "sond_client_file_manager.h"
#include "sond_client_misc.h"

//#include "libsearpc/searpc-client.h"
#include "libsearpc/searpc-named-pipe-transport.h"

#define SEAFILE_SOCKET_NAME "seadrive.sock"



static gboolean
sond_client_close( GtkWidget* app_window, GdkEvent* event, gpointer data )
{
    SondClient* sond_client = (SondClient*) data;
    g_free( sond_client->base_dir );
    g_free( sond_client->seadrive_root );
    g_free( sond_client->seafile_root );
    g_free( sond_client->server_host );
    g_free( sond_client->server_user );
    g_free( sond_client->user );
    g_free( sond_client->password );

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

    path = g_strdup_printf("\\\\.\\pipe\\seafile_%s", b64encode(userNameBuf));
#else
    path = g_build_filename ( sond_client->seafile, SEAFILE_SOCKET_NAME, NULL);
#endif

    searpc_named_pipe_client = searpc_create_named_pipe_client( path );
    g_free( path );
    ret = searpc_named_pipe_client_connect( searpc_named_pipe_client );
    if ( ret == -1 )
    {
        //seaf-daemon starten
        g_error( "Verbindung zum RPC-Server kann nicht hergestellt werden" );
    }

    sond_client->searpc_client = searpc_client_with_named_pipe_transport( searpc_named_pipe_client,
            "seafile-rpcserver" );

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


static gint
sond_client_get_creds( SondClient* sond_client )
{
    GtkWidget* dialog = gtk_dialog_new_with_buttons( "Verbindung zu SQL-Server",
            GTK_WINDOW(sond_client->app_window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            "Ok", GTK_RESPONSE_OK, "Abbrechen", GTK_RESPONSE_CANCEL, NULL );

    GtkWidget* content = gtk_dialog_get_content_area( GTK_DIALOG(dialog) );

    //User
    GtkWidget* frame_user = gtk_frame_new( "Benutzername" );
    GtkWidget* entry_user = gtk_entry_new( );
    gtk_container_add( GTK_CONTAINER(frame_user), entry_user );
    gtk_box_pack_start( GTK_BOX(content), frame_user, FALSE, FALSE, 0 );

    //password
    GtkWidget* frame_password = gtk_frame_new( "Passwort" );
    GtkWidget* entry_password = gtk_entry_new( );
    gtk_container_add( GTK_CONTAINER(frame_password), entry_password );
    gtk_box_pack_start( GTK_BOX(content), frame_password, FALSE, FALSE, 0 );

    g_signal_connect_swapped( entry_user, "activate",
            G_CALLBACK(gtk_widget_grab_focus), entry_password );

    g_signal_connect_swapped( entry_password, "activate",
            G_CALLBACK(gtk_widget_grab_focus),
            gtk_dialog_get_widget_for_response( GTK_DIALOG(dialog), GTK_RESPONSE_OK ) );

    gtk_widget_grab_focus( entry_user );
    gtk_widget_show_all( dialog );

    gint res = gtk_dialog_run( GTK_DIALOG(dialog) );

    if ( res == GTK_RESPONSE_OK )
    {
        sond_client->user = g_strdup( gtk_entry_get_text( GTK_ENTRY(entry_user) ) );
        sond_client->password = g_strdup( gtk_entry_get_text( GTK_ENTRY(entry_password) ) );
    }

    gtk_widget_destroy( dialog );

    return res;
}


static void
sond_client_read_conf( SondClient* sond_client )
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

    sond_client->seafile_root = g_key_file_get_string( key_file, "SEAFILE", "root", &error );
    if ( error ) g_error( "Seadrive-Dir konnte nicht ermittelt werden:\n%s", error->message );

    sond_client->seadrive_root = g_key_file_get_string( key_file, "SEADRIVE", "root", &error );
    if ( error ) g_error( "Seadrive-Dir konnte nicht ermittelt werden:\n%s", error->message );

    g_key_file_free( key_file );

    return;
}


static void
sond_client_init( GtkApplication* app, SondClient* sond_client )
{
    SondError* sond_error = NULL;

    sond_client->arr_file_manager = g_ptr_array_new( );
    g_ptr_array_set_free_func( sond_client->arr_file_manager,
            (GDestroyNotify) sond_client_file_manager_free );

    sond_client_init_app_window( app, sond_client );

    sond_client->base_dir = get_base_dir( );

    sond_client_read_conf( sond_client );

    sond_client_get_creds( sond_client );

    sond_client_init_rpc_client( sond_client );

    if ( !sond_client_connection_ping( sond_client, &sond_error ) ) DISPLAY_SOND_ERROR
    else printf( "PONG" );
//    sond_client_seadrive_test_seafile_server( sond_client );

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
