#include <gtk/gtk.h>
#ifdef __WIN32
#include <windows.h>
#include <wincrypt.h>
#endif // __WIN32
#include <openssl/evp.h>
#include <openssl/rand.h>

#include "../../misc.h"
#include "../../zond/99conv/general.h"

#include "sond_client.h"
#include "sond_client_akte.h"
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
    g_free( sond_client->user );
    g_free( sond_client->password );
    g_free( sond_client->password_hash );
    g_free( sond_client->password_salt );

    g_ptr_array_unref( sond_client->arr_file_manager );

    if ( sond_client->searpc_client )
            searpc_free_client_with_pipe_transport( sond_client->searpc_client );

    gtk_widget_destroy( sond_client->app_window );
    sond_client->app_window = NULL;

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


static gint
sond_client_init_rpc_client( SondClient* sond_client, GError** error )
{
    SearpcNamedPipeClient* searpc_named_pipe_client = NULL;
    gchar* path = NULL;
    gint ret = 0;

#ifdef __WIN32
    char userNameBuf[32767];
    DWORD bufCharCount = sizeof(userNameBuf);
    if (GetUserNameA(userNameBuf, &bufCharCount) == 0)
    {
        *error = g_error_new( SOND_CLIENT_ERROR, SOND_CLIENT_ERROR_NOUSERNAME,
                "GetUserNameA: Konnte Benutzernamen nicht ermitteln" );
        return -1;
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
        *error = g_error_new( SOND_CLIENT_ERROR, SOND_CLIENT_ERROR_NOSEAFRPC,
                "Verbindung zum RPC-Server kann nicht hergestellt werden" );
        return -1;
    }

    sond_client->searpc_client = searpc_client_with_named_pipe_transport( searpc_named_pipe_client,
            "seafile-rpcserver" );

    return 0;
}

#define SALT_SIZE 16
#define HASH_ALGORITHM EVP_sha256()
#define HASH_SIZE EVP_MD_size(HASH_ALGORITHM)
#define MAX_PASSWORD_SIZE 100
#define HEX_BUFFER_SIZE (HASH_SIZE * 2 + 1)

static gchar*
sond_client_create_hash(const gchar* password, const gchar* salt_b64, GError** error )
{
    EVP_MD_CTX* mdctx = NULL;
    const EVP_MD* md = NULL;
    guchar* salt = NULL;
    gsize salt_len = 0;
    guint hash_len = 0;
    gchar* hash_64 = NULL;
    guchar hash[HASH_SIZE] = { };
    gint rc = 0;

    mdctx = EVP_MD_CTX_new( );
    if ( !mdctx )
    {
        *error = g_error_new( SOND_CLIENT_ERROR, SOND_CLIENT_ERROR_NOSEAFRPC, "EV_MD_CTX_new failed" );
        return NULL;
    }

    md = HASH_ALGORITHM;

    if ( EVP_DigestInit_ex( mdctx, md, NULL ) != 1 )
    {
        *error = g_error_new( SOND_CLIENT_ERROR, SOND_CLIENT_ERROR_NOSEAFRPC, "EV_DigestInit_ex failed" );
        EVP_MD_CTX_free(mdctx);
        return NULL;
    }

    if ( EVP_DigestUpdate( mdctx, password, strlen( password ) ) != 1 )
    {
        *error = g_error_new( SOND_CLIENT_ERROR, SOND_CLIENT_ERROR_NOSEAFRPC, "EV_DigestUpdate failed" );
        EVP_MD_CTX_free(mdctx);
        return NULL;
    }

    salt = g_base64_decode( salt_b64, &salt_len );

    rc = EVP_DigestUpdate( mdctx, salt, salt_len);
    g_free( salt );
    if ( rc != 1 )
    {
        *error = g_error_new( SOND_CLIENT_ERROR, SOND_CLIENT_ERROR_NOSEAFRPC, "EV_DigestUpdate failed" );
        EVP_MD_CTX_free(mdctx);
        return NULL;
    }

    rc = EVP_DigestFinal_ex( mdctx, hash, &hash_len );
    EVP_MD_CTX_free(mdctx);
    if ( rc != 1 )
    {
        *error = g_error_new( SOND_CLIENT_ERROR, SOND_CLIENT_ERROR_NOSEAFRPC, "EV_DigestUpdate failed" );
        return NULL;
    }

    hash_64 = g_base64_encode( hash, hash_len );

    return hash_64;
}


static gint
sond_client_get_creds( SondClient* sond_client, GError** error )
{
    gint ret = 0;
    gchar user[256] = { 0 };

    GtkWidget* dialog = gtk_dialog_new_with_buttons( "Password",
            GTK_WINDOW(sond_client->app_window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            "Ok", GTK_RESPONSE_OK, "Abbrechen", GTK_RESPONSE_CANCEL, NULL );

    GtkWidget* content = gtk_dialog_get_content_area( GTK_DIALOG(dialog) );

    //User
    GtkWidget* frame_user = gtk_frame_new( "Benutzername" );
    GtkWidget* entry_user = gtk_entry_new( );
    gtk_container_add( GTK_CONTAINER(frame_user), entry_user );
    gtk_box_pack_start( GTK_BOX(content), frame_user, FALSE, FALSE, 0 );

    if ( sond_client->user )
            gtk_entry_set_text( GTK_ENTRY(entry_user), sond_client->user );
    gtk_widget_grab_focus( entry_user );

    //password
    GtkWidget* frame_password = gtk_frame_new( "Passwort" );
    GtkWidget* entry_password = gtk_entry_new( );
    gtk_entry_set_visibility( GTK_ENTRY( entry_password), FALSE );
    gtk_container_add( GTK_CONTAINER(frame_password), entry_password );
    gtk_box_pack_start( GTK_BOX(content), frame_password, FALSE, FALSE, 0 );

    g_signal_connect_swapped( entry_user, "activate",
            G_CALLBACK(gtk_widget_grab_focus), entry_password );

    g_signal_connect_swapped( entry_password, "activate",
            G_CALLBACK(gtk_button_clicked),
            gtk_dialog_get_widget_for_response( GTK_DIALOG(dialog), GTK_RESPONSE_OK ) );

    gtk_widget_grab_focus( entry_user );
    gtk_widget_show_all( dialog );

    gint res = gtk_dialog_run( GTK_DIALOG(dialog) );

    g_stpcpy( user, gtk_entry_get_text( GTK_ENTRY(entry_user) ) );
    sond_client->password = g_strdup( gtk_entry_get_text( GTK_ENTRY(entry_password) ) );

    gtk_widget_destroy( dialog );

    if ( res == GTK_RESPONSE_OK )
    {
        if ( g_strcmp0( user, sond_client->user ) ) //anderer user als gespeichert
        {
            guchar salt[SALT_SIZE] = { };
            gchar* salt_b64 = NULL;
            gchar* hash_b64 = NULL;
            GKeyFile* key_file = NULL;
            gchar* conf_path = NULL;
            gboolean success = FALSE;

            //versuchen, online zu authentifizieren
            if ( !sond_client_connection_ping( sond_client, error ) )
            {
                g_prefix_error( error, "%s\n", __func__ );

                return -1;
            }

            //salt erzeugen
            if (! RAND_bytes( (guchar*) salt, SALT_SIZE ) )
            {
                g_prefix_error( error, "%s\n", __func__ );
                return -1;
            }

            salt_b64 = g_base64_encode( salt, SALT_SIZE );

            //hash aus passwort und salt erzeugen
            hash_b64 = sond_client_create_hash( sond_client->password, salt_b64, error );
            if ( !hash_b64 )
            {
                g_free( salt_b64 );
                g_prefix_error( error, "%s\n", __func__ );

                return -1;
            }

            //hash und salt in keyfile abspeichern
            key_file = g_key_file_new( );

            conf_path = g_build_filename( sond_client->base_dir, "SondClient.conf", NULL );
            success = g_key_file_load_from_file( key_file, conf_path,
                    G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS, error );
            g_free( conf_path );
            if ( !success )
            {
                g_prefix_error( error, "%s\n", __func__ );
                g_free( salt_b64 );
                g_free( hash_b64 );

                return -1;
            }

            g_key_file_set_string( key_file, "CREDS", "user", user );
            g_free( sond_client->user );
            sond_client->user = g_strdup( user );

            g_key_file_set_string( key_file, "CREDS", "password_hash", hash_b64 );
            g_free( hash_b64 );

            g_key_file_set_string( key_file, "CREDS", "password_salt", salt_b64 );
            g_free( salt_b64 );

            conf_path = g_build_filename( sond_client->base_dir, "SondClient.conf", NULL );
            success = g_key_file_save_to_file( key_file, conf_path, error );
            g_free( conf_path );
            g_key_file_unref( key_file );
            if ( !success )
            {
                g_prefix_error( error, "%s\n", __func__ );
                return -1;
            }
        }
        else //user ist gepspeicherter user
        {
            gchar* hash_b64 = NULL;

            hash_b64 = sond_client_create_hash( sond_client->password,
                    sond_client->password_salt, error );
            if ( !hash_b64 )
            {
                g_prefix_error( error, "%s\n", __func__ );

                return -1;
            }

            if ( g_strcmp0( hash_b64, sond_client->password_hash ) )
            {
                *error = g_error_new( SOND_CLIENT_ERROR, SOND_CLIENT_ERROR_INVALRESP,
                        "%s\nUsername und/oder Passwort ungültig", __func__ );

                return -1;
            }
        }
    }
    else ret = 1;

    return ret;
}


static gint
sond_client_read_conf( SondClient* sond_client, GError** error )
{
    GKeyFile* key_file = NULL;
    gchar* conf_path = NULL;
    gboolean success = FALSE;
    gchar* hash = NULL;
    gchar* user = NULL;
    gchar* salt = NULL;

    key_file = g_key_file_new( );

    conf_path = g_build_filename( sond_client->base_dir, "SondClient.conf", NULL );
    success = g_key_file_load_from_file( key_file, conf_path,
            G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS, error );
    g_free( conf_path );
    if ( !success )
    {
        g_prefix_error( error, "%s\n", __func__ );
        return -1;
    }

    user = g_key_file_get_string( key_file, "CREDS", "user", NULL );
    if ( user && g_strcmp0( user, "" ) )
    {
        hash = g_key_file_get_string( key_file, "CREDS", "password_hash", NULL );
        if ( hash && g_strcmp0( hash, "" ) )
        {
            salt = g_key_file_get_string( key_file, "CREDS", "password_salt", NULL );
            if ( salt && g_strcmp0( salt, "" ) )
            {
                sond_client->user = user;
                sond_client->password_hash = hash;
                sond_client->password_salt = salt;
            }
        }
    }

    if ( !sond_client->user )
    {
        g_free( user );
        g_free( hash );
        g_free( salt );
    }

    sond_client->server_host = g_key_file_get_string( key_file, "SERVER", "host", error );
    if ( *error )
    {
        g_prefix_error( error, "%s\n", __func__ );
        return -1;
    }

    sond_client->server_port = (guint16) g_key_file_get_uint64( key_file, "SERVER", "port", error );
    if ( *error )
    {
        g_prefix_error( error, "%s\n", __func__ );
        return -1;
    }

    sond_client->seafile_root = g_key_file_get_string( key_file, "SEAFILE", "root", error );
    if ( *error )
    {
        g_prefix_error( error, "%s\n", __func__ );
        return -1;
    }

    sond_client->seadrive_root = g_key_file_get_string( key_file, "SEADRIVE", "root", error );
    if ( *error )
    {
        g_prefix_error( error, "%s\n", __func__ );
        return -1;
    }

    g_key_file_free( key_file );

    return 0;
}


static void
sond_client_init_app_window( GtkApplication* app, SondClient* sond_client )
{
    GtkWidget* grid = NULL;
    GtkWidget* frame_dok = NULL;
    GtkWidget* entry_dok = NULL;
    GtkWidget* button_akte = NULL;

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

    button_akte = gtk_button_new_with_label( "Akte" );
    gtk_grid_attach( GTK_GRID(grid), button_akte, 0, 1, 1, 1 );

    gtk_widget_show_all( sond_client->app_window );

    g_signal_connect( sond_client->app_window, "delete-event",
            G_CALLBACK(sond_client_close), sond_client );

    g_signal_connect( entry_dok, "activate",
            G_CALLBACK(sond_client_file_manager_entry_activate), sond_client );
    g_signal_connect( button_akte, "clicked",
            G_CALLBACK(sond_client_akte_init), sond_client );

    return;
}


static void
sond_client_init( GtkApplication* app, SondClient* sond_client )
{
    gint rc = 0;
    GError* error = NULL;

    sond_client->arr_file_manager = g_ptr_array_new( );
    g_ptr_array_set_free_func( sond_client->arr_file_manager,
            (GDestroyNotify) sond_client_file_manager_free );

    sond_client_init_app_window( app, sond_client );

    sond_client->base_dir = get_base_dir( );

    rc = sond_client_read_conf( sond_client, &error );
    if ( rc )
    {
        display_message( sond_client->app_window,
                "Konfigurationsdatei konnte nicht gelesen werden\n\n",
                error->message, NULL );
        g_error_free( error );

        sond_client_quit( sond_client );

        return;
    }

    while ( (rc = sond_client_get_creds( sond_client, &error )) == -1 )
    {
        if ( rc == -1 )
        {
            display_message( sond_client->app_window,
                    "Nutzer konnte nicht legitimiert werden\n\n",
                    error->message, NULL );
            g_clear_error( &error );
        }
    }

    if ( rc == 1 ) //abbrechen
    {
        sond_client_quit( sond_client );

        return;
    }

    rc = sond_client_init_rpc_client( sond_client, &error );
    if ( rc )
    {
        display_message( sond_client->app_window,
                "Seafile-RPC-Client konnte nicht gestartet werden\n\n",
                error->message, NULL );
        g_error_free( error );

        sond_client_quit( sond_client );

        return;
    }

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
