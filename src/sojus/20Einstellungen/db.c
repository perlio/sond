/*
sojus (db.c) - softkanzlei
Copyright (C) 2021  pelo america

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdlib.h>

#include <gtk/gtk.h>
#include <mariadb/mysql.h>

#include "../../sond_database.h"
#include "../sojus_init.h"
#include "../../misc.h"


/****************************************
*   Erstellung einer neuen Datenbank
*   Verbindung zu SQL-Server mit
*   ausreichenden Rechten muß hergestellt sein
**************************************************/
static gint
db_create( MYSQL* con, gchar* db_name, gchar** errmsg )
{
    gint rc = 0;
    gchar* sql = NULL;

    //prüfen, ob db "db_name" schon existiert
    if ( !mysql_select_db( con, db_name ) ) return 1;//Falls nicht:

    //neue Datenbank erstellen
    sql = g_strconcat( "CREATE DATABASE `", db_name,"` "
            "CHARACTER SET = 'utf8' COLLATE = 'utf8_general_ci'", NULL );
    rc = mysql_query( con, sql );
    g_free( sql );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Fehler bei CREATE "
                "DATABASE ", db_name, ":\n", mysql_error( con ),
                NULL );

        return -1;
    }

    //Mit der neuen Datenbank verbinden
    if ( (rc = mysql_select_db( con, db_name )) )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Fehler in cb_dialog_"
                "create_response:\nErzeugte Datenbank konnte nicht "
                "verbunden werden.\nmysql_select_db \"", db_name, "\":\n",
                mysql_error( con ), NULL );

        return -1;
    }

    rc = sond_database_add_to_database( con, errmsg );
    if ( rc )
    {
        gchar* sql_drop = NULL;
        gint ret = 0;

        sql_drop = g_strdup_printf( "DROP DATABASE `%s`", db_name );
        ret = mysql_query( con, sql_drop );
        g_free( sql_drop );
        if ( ret && errmsg ) *errmsg = add_string( *errmsg, g_strconcat( "\n\nFehler "
                "bei Löschen der Database ", db_name, ":\n",
                mysql_error( con ), NULL ) );
        else if ( errmsg ) *errmsg = add_string( *errmsg, g_strconcat( "\n\nDatabase ", db_name,
                " wurde gelöscht", NULL ) );

        return -1;
    }



    return 0;
}


gint
db_real_connect_database( GtkWidget* app_window, MYSQL* con, gchar** dbname )
{
    do
    {
        gint rc = 0;

        if ( g_strcmp0( *dbname, "" ) && (*dbname) )
        {
            gint ret = 0;

            ret = mysql_select_db( con, *dbname );
            if ( ret ) display_message( app_window, "Datenbank konnte nicht "
                    "verbunden werden -\n\nBei Aufruf mysql_select_db:\n",
                    mysql_error( con ), NULL );
            else break;
        }

        rc = dialog_with_buttons( app_window, "Datenbank auswählen",
                "", dbname, "Bestehende Datenbank", 1, "Datenbank erzeugen", 2,
                "Abbrechen", GTK_RESPONSE_CANCEL, NULL );
        if ( rc == 1 ) continue;
        else if ( rc == 2 )
        {
            gint ret = 0;
            gchar* errmsg = NULL;

            ret = db_create( con, *dbname, &errmsg );
            if ( ret == -1 )
            {
                display_message( app_window, "Datenbank """, *dbname,
                        """ konnte nicht erzeugt werden -\nBei Aufruf "
                        "db_create:\n", errmsg, NULL );
                g_free( errmsg );

                return 1;
            }
            else if ( ret == 1 ) display_message( app_window, "Datenbank """, *dbname,
                        """ existiert bereits", NULL );
            else if ( ret == 0 ) break;
        }
        else if ( rc == GTK_RESPONSE_CANCEL ) return 1;
    } while ( 1 );

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
    gtk_entry_set_visibility( GTK_ENTRY(entry_password), FALSE );
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


void
db_connect_database( Sojus* sojus )
{
    MYSQL* con = NULL;
    gboolean try_settings = FALSE;
    gchar* host = NULL;
    gint port = 0;
    gchar* user = NULL;
    gchar* password = NULL;

    host = g_settings_get_string( sojus->settings, "host" );
    port = g_settings_get_int( sojus->settings, "port" );
    user = g_settings_get_string( sojus->settings, "user" );
    password = g_settings_get_string( sojus->settings, "password" );

    if ( !sojus->con ) try_settings = TRUE;

    do
    {
        gint rc = 0;
        gchar* errmsg = NULL;

        if ( try_settings ) try_settings = FALSE;
        else if ( rc == 0 )
        {
            rc = db_get_con_params( sojus->app_window, &host, &port, &user, &password );
            if ( rc == GTK_RESPONSE_CANCEL ) break;
        }

        con = db_connect( host, user, password, port, &errmsg );

        if ( con ) break;
        else
        {
            gint rc = 0;

            rc = dialog_with_buttons( sojus->app_window, "Verbindung zu SQL-Server "
                    "konnte nicht hergestellt werden", errmsg, NULL,
                    "Erneut versuchen", 1, "Andere Verbindung", 0, "Abbrechen",
                    GTK_RESPONSE_CANCEL, NULL );

            g_free( errmsg );

            if ( rc == GTK_RESPONSE_CANCEL ) break;
            //else: continue
        }
    } while ( 1 );

    //Jetzt Datenbank verbinden
    if ( con )
    {
        gint rc = 0;
        gchar* dbname = NULL;

        dbname = g_settings_get_string( sojus->settings, "dbname" );

        rc = db_real_connect_database( sojus->app_window, con, &dbname );
        if ( rc == 0 )
        {
            g_settings_set_string( sojus->settings, "host", host );
            g_settings_set_int( sojus->settings, "port", port );
            g_settings_set_string( sojus->settings, "user", user );
            g_settings_set_string( sojus->settings, "password", password );
            g_settings_set_string( sojus->settings, "dbname", dbname );

            mysql_close( sojus->con );
            sojus->con = con;
        }
        else mysql_close( con );

        g_free( dbname );
    }

    g_free( host );
    g_free( user );
    g_free( password );

    return;
}

