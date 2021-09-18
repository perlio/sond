#include <stdlib.h>

#include <gtk/gtk.h>
#include <mariadb/mysql.h>

#include "../../sond_database.h"
#include "../sojus_init.h"
#include "../../misc.h"

/*
static gint
db_activate( Sojus* sojus, MYSQL* con, const gchar* db_name, gchar** errmsg )
{
    GPtrArray* arr_sachgebiete = g_ptr_array_new_with_free_func( (GDestroyNotify) g_free );
    GPtrArray* arr_beteiligtenart = g_ptr_array_new_with_free_func( (GDestroyNotify) g_free );
    GPtrArray* arr_sachbearbeiter = g_ptr_array_new_with_free_func( (GDestroyNotify) g_free );

    //Sachgebiete einlesen
    gchar* sql = NULL;
    sql = "SELECT * FROM Sachgebiete";
    if ( mysql_query( con, sql ) )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf mysql_query (", sql, "):\n",
                mysql_error( con ), NULL );
        g_ptr_array_unref( arr_sachgebiete );
        g_ptr_array_unref( arr_beteiligtenart);
        g_ptr_array_unref( arr_sachbearbeiter );

        return -1;
    }
    else
    {
        MYSQL_RES* mysql_res = mysql_store_result( con );
        MYSQL_ROW mysql_row = NULL;
        while ( (mysql_row = mysql_fetch_row( mysql_res )) ) g_ptr_array_add(
                arr_sachgebiete, g_strdup( mysql_row[0] ) );
        mysql_free_result( mysql_res );
    }

    //Beteiligtenart einlesen
    sql = "SELECT * FROM Beteiligtenart;";
    if ( mysql_query( con, sql ) )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf mysql_query (", sql, "):\n",
                mysql_error( con ), NULL );
        g_ptr_array_unref( arr_sachgebiete );
        g_ptr_array_unref( arr_beteiligtenart);
        g_ptr_array_unref( arr_sachbearbeiter );

        return -1;
    }
    else
    {
        MYSQL_RES* mysql_res = mysql_store_result( con );
        MYSQL_ROW mysql_row = NULL;
        while ( (mysql_row = mysql_fetch_row( mysql_res )) ) g_ptr_array_add(
                arr_beteiligtenart, g_strdup( mysql_row[0] ) );
        mysql_free_result( mysql_res );
    }
    //Sachbearbeiter einlesen
    sql = "SELECT * FROM Sachbearbeiter ORDER BY `Sachbearbeiter-ID` ASC;";
    if ( mysql_query( con, sql ) )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf mysql_query (", sql, "):\n",
                mysql_error( con ), NULL );
        g_ptr_array_unref( arr_sachgebiete );
        g_ptr_array_unref( arr_beteiligtenart);
        g_ptr_array_unref( arr_sachbearbeiter );

        return -1;
    }
    else
    {
        MYSQL_RES* mysql_res = mysql_store_result( con );
        MYSQL_ROW mysql_row = NULL;
        while ( (mysql_row = mysql_fetch_row( mysql_res )) ) g_ptr_array_add(
                arr_sachbearbeiter, g_strdup( mysql_row[0] ) );
        mysql_free_result( mysql_res );
    }

    g_ptr_array_unref( sojus->sachgebiete );
    g_ptr_array_unref( sojus->beteiligtenart );
    g_ptr_array_unref( sojus->sachbearbeiter );

    sojus->sachgebiete = arr_sachgebiete;
    sojus->beteiligtenart = arr_beteiligtenart;
    sojus->sachbearbeiter = arr_sachbearbeiter;

    gtk_window_set_title( GTK_WINDOW(sojus->app_window), db_name );

    return 0;
}
*/

/****************************************
*   Erstellung einer neuen Datenbank
*   Verbindung zu SQL-Server mit
*   ausreichenden Rechten muß hergestellt sein
**************************************************/
static gint
db_create( Sojus* sojus, gchar* db_name, gchar** errmsg )
{
    gint rc = 0;

    //prüfen, ob db "db_name" schon existiert
    if ( !mysql_select_db( sojus->con, db_name ) ) return 1;//Falls nicht:

    //neue Datenbank erstellen
    gchar* sql = g_strconcat( "CREATE DATABASE `", db_name,"` "
            "CHARACTER SET = 'utf8' COLLATE = 'utf8_general_ci'", NULL );
    rc = mysql_query( sojus->con, sql );
    g_free( sql );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Fehler bei CREATE "
                "DATABASE ", db_name, ":\n", mysql_error( sojus->con ),
                NULL );

        return -1;
    }

    //Mit der neuen Datenbank verbinden
    if ( (rc = mysql_select_db( sojus->con, db_name )) )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Fehler in cb_dialog_"
                "create_response:\nErzeugte Datenbank konnte nicht "
                "verbunden werden.\nmysql_select_db \"", db_name, "\":\n",
                mysql_error( sojus->con ), NULL );

        return -1;
    }

    rc = mysql_query( sojus->con, sond_database_sql_create_database( ) );
    if ( rc )
    {
        gint ret = 0;
        gchar* sql_drop = NULL;

        if ( errmsg ) *errmsg = g_strconcat( "Bei Einrichtung db:\n",
                mysql_error( sojus->con ), NULL );
        sql_drop = g_strdup_printf( "DROP DATABASE `%s`", db_name );
        ret = mysql_query( sojus->con, sql_drop );
        g_free( sql_drop );
        if ( ret && errmsg ) add_string( *errmsg, g_strconcat( "\n\nFehler "
                "bei Löschen der Database ", db_name, ":\n",
                mysql_error( sojus->con ), NULL ) );
        else if ( errmsg ) add_string( *errmsg, g_strconcat( "Database ", db_name,
                " wurde gelöscht", NULL ) );

        return -1;
    }

    gint status = 0;

    do
    {
        rc = mysql_affected_rows( sojus->con );
        if ( rc < 0 ) break;
        /* more results? -1 = no, >0 = error, 0 = yes (keep looping) */
        status = mysql_next_result( sojus->con );
    } while (status == 0);

    if ( rc || (status > 0) )
    {
        gint ret = 0;
        if ( errmsg ) *errmsg = g_strconcat( "Bei Einrichtung db:\n",
                mysql_error( sojus->con ), NULL );
        sql = g_strdup_printf( "DROP DATABASE `%s`", db_name );
        ret = mysql_query( sojus->con, sql );
        g_free( sql );
        if ( ret && errmsg ) add_string( *errmsg, g_strconcat( "\n\nFehler "
                "bei Löschen der Database ", db_name, ":\n",
                mysql_error( sojus->con ), NULL ) );
        else if ( errmsg ) add_string( *errmsg, g_strconcat( "Database ", db_name,
                " wurde gelöscht", NULL ) );

        return -1;
    }

    return 0;
}


gint
db_connect_database( Sojus* sojus )
{
    gchar* db_name = NULL;
    gboolean dont_ask = FALSE;

    db_name = g_settings_get_string( sojus->settings, "dbname" );
    dont_ask = (gboolean) (sojus->con && g_strcmp0( db_name, "" ));

    do
    {
        gint ret = 0;
        gchar* errmsg = NULL;

        if ( dont_ask ) ret = mysql_select_db( sojus->con, db_name );
        else ret = 1;

        dont_ask = TRUE;

        if ( ret == 0 ) break;
        else
        {
            gint rc = 0;

            if ( g_strcmp0( mysql_error( sojus->con ), "" ) )
                    display_message( sojus->app_window, "Datenbank konnte nicht "
                    "verbunden werden -\n\nBei Aufruf db_select_database:\n",
                    mysql_error( sojus->con ), NULL );

            rc = dialog_with_buttons( sojus->app_window, "Datenbank auswählen",
                    "", &db_name, "Bestehende Datenbank", 1, "Datenbank erzeugen", 2,
                    "Abbrechen", GTK_RESPONSE_CANCEL, NULL );
            if ( rc == 1 ) continue;
            else if ( rc == 2 )
            {
                gint ret = 0;

                ret = db_create( sojus, db_name, &errmsg );
                if ( ret == -1 )
                {
                    display_message( sojus->app_window, "Datenbank """, db_name,
                            """ konnte nicht erzeugt werden -\nBei Aufruf "
                            "db_create:\n", errmsg, NULL );
                    g_free( errmsg );
                }
                else if ( ret == 1 ) display_message( sojus->app_window, "Datenbank """, db_name,
                            """ existiert bereits", NULL );
                else if ( ret == 0 ) break;
            }

            g_free( db_name );

            return 1;
        }
    } while ( 1 );
/*
    rc = db_activate( sojus, con, db_name, &errmsg );
    if ( rc )
    {
        display_message( sojus->app_window, "Fehler beim Aktivieren der Datenbank -\n\n"
                "Bei Aufruf db_activate:\n", errmsg, NULL );
        g_free( errmsg );

        g_free( db_name );

        return -1;
    }
*/
    g_settings_set_string( sojus->settings, "dbname", db_name );

    g_free( db_name );

    return 0;
}


static gint
db_get_con_params( GtkWidget* window, gchar** host, gint* port, gchar** user, gchar** password )
{
    GtkWidget* dialog = gtk_dialog_new_with_buttons( "Verbindung zu SQL-Server",
            GTK_WINDOW(window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            "Ok", GTK_RESPONSE_OK, "Abbrechen", GTK_RESPONSE_CANCEL, NULL );

    GtkWidget* content = gtk_dialog_get_content_area( GTK_DIALOG(dialog) );

    //Host
    GtkWidget* frame_host = gtk_frame_new( "Host" );
    GtkWidget* entry_host = gtk_entry_new( );
    gtk_entry_set_text( GTK_ENTRY(entry_host), *host );
    gtk_container_add( GTK_CONTAINER(frame_host), entry_host );
    gtk_box_pack_start( GTK_BOX(content), frame_host, FALSE, FALSE, 0 );

    //port
    GtkWidget* frame_port = gtk_frame_new( "Port" );
    GtkWidget* entry_port = gtk_entry_new( );
    gchar* text_port = g_strdup_printf( "%i", *port );
    gtk_entry_set_text( GTK_ENTRY(entry_port), text_port );
    g_free( text_port );
    gtk_container_add( GTK_CONTAINER(frame_port), entry_port );
    gtk_box_pack_start( GTK_BOX(content), frame_port, FALSE, FALSE, 0 );

    //user
    GtkWidget* frame_user = gtk_frame_new( "User" );
    GtkWidget* entry_user = gtk_entry_new( );
    gtk_entry_set_text( GTK_ENTRY(entry_user), *user );
    gtk_container_add( GTK_CONTAINER(frame_user), entry_user );
    gtk_box_pack_start( GTK_BOX(content), frame_user, FALSE, FALSE, 0 );

    //password
    GtkWidget* frame_password = gtk_frame_new( "Passwort" );
    GtkWidget* entry_password= gtk_entry_new( );
    gtk_entry_set_text( GTK_ENTRY(entry_password), *password );
    gtk_container_add( GTK_CONTAINER(frame_password), entry_password );
    gtk_box_pack_start( GTK_BOX(content), frame_password, FALSE, FALSE, 0 );

    g_signal_connect_swapped( entry_host, "activate",
            G_CALLBACK(gtk_widget_grab_focus), entry_port );
    g_signal_connect_swapped( entry_port, "activate",
            G_CALLBACK(gtk_widget_grab_focus), entry_user );
    g_signal_connect_swapped( entry_user, "activate",
            G_CALLBACK(gtk_widget_grab_focus), entry_password );

    g_signal_connect_swapped( entry_password, "activate",
            G_CALLBACK(gtk_widget_grab_focus),
            gtk_dialog_get_widget_for_response( GTK_DIALOG(dialog), GTK_RESPONSE_OK ) );

    gtk_widget_grab_focus( entry_host );
    gtk_widget_show_all( dialog );

    gint res = gtk_dialog_run( GTK_DIALOG(dialog) );

    if ( res == GTK_RESPONSE_OK )
    {
        g_free( *host );
        g_free( *user );
        g_free( *password );

        *host = g_strdup( gtk_entry_get_text( GTK_ENTRY(entry_host) ) );
        *port = atoi( gtk_entry_get_text( GTK_ENTRY(entry_port) ) );
        *user = g_strdup( gtk_entry_get_text( GTK_ENTRY(entry_user) ) );
        *password = g_strdup( gtk_entry_get_text( GTK_ENTRY(entry_password) ) );
    }

    gtk_widget_destroy( dialog );

    return res;
}


static MYSQL*
db_connect( const gchar* host, const gchar* user, const gchar*
        password, gint port, gchar** errmsg )
{
    MYSQL* con = mysql_init( NULL );
    if ( !mysql_real_connect( con, host, user, password, NULL, port, NULL,
            CLIENT_MULTI_STATEMENTS ) )
    {
        *errmsg = g_strconcat( "Verbindung von User \"", user, "\" zu Host \"",
                host, "\" nicht erfolgreich:\n", mysql_error( con ), NULL );
        mysql_close( con );

        return NULL;
    }

    return con;
}


gint
db_get_connection( Sojus* sojus )
{
    MYSQL* con = NULL;
    gchar* errmsg = NULL;
    gchar* host = NULL;
    gint port = 0;
    gchar* user = NULL;
    gchar* password = NULL;
    gboolean try_settings = FALSE;

    host = g_settings_get_string( sojus->settings, "host" );
    port = g_settings_get_int( sojus->settings, "port" );
    user = g_settings_get_string( sojus->settings, "user" );
    password = g_settings_get_string( sojus->settings, "password" );

    if ( !sojus->con ) try_settings = TRUE;

    do
    {
        if ( try_settings ) con = db_connect( host, user, password, port, &errmsg );

        //Wenn Verbindung nicht hergestellt werden konnte
        if ( !con )
        {
            gint rc = 0;

            if ( !try_settings ) rc = 2;
            else rc = dialog_with_buttons( sojus->app_window, "Verbindung zu SQL-Server "
                    "konnte nicht hergestellt werden", errmsg, NULL,
                    "Erneut versuchen", 1, "Andere Verbindung", 2, "Abbrechen",
                    GTK_RESPONSE_CANCEL, NULL );

            g_free( errmsg );

            try_settings = TRUE;

            if ( rc == 1 ) continue;
            else if ( rc == 2 )
            {
                rc = db_get_con_params( sojus->app_window, &host, &port, &user, &password );
                if ( rc == GTK_RESPONSE_OK ) continue;
            }

            break;
        }
        else break;
    } while ( 1 );

    if ( con )
    {
        gint rc = 0;

        mysql_close( sojus->con );
        sojus->con = con;

        rc = db_connect_database( sojus );
        if ( rc == 0 )
        {
            g_settings_set_string( sojus->settings, "host", host );
            g_settings_set_int( sojus->settings, "port", port );
            g_settings_set_string( sojus->settings, "user", user );
            g_settings_set_string( sojus->settings, "password", password );
        }
        else mysql_close( sojus->con );
    }

    g_free( host );
    g_free( user );
    g_free( password );

    if ( sojus->con ) return 0;

    return 1;
}

