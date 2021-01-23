#include <stdlib.h>

#include <gtk/gtk.h>
#include <mariadb/mysql.h>

#include "../global_types_sojus.h"
#include "../../misc.h"


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


/****************************************
*   Erstellung einer neuen Datenbank
*   Verbindung zu SQL-Server mit
*   ausreichenden Rechten muß hergestellt sein
**************************************************/
static gint
db_create( Sojus* sojus, MYSQL* con, gchar* db_name, gchar** errmsg )
{
    //prüfen, ob db "db_name" schon existiert
    if ( !mysql_select_db( con, db_name ) ) return 1;//Falls nicht:

    //neue Datenbank erstellen
    gint rc = 0;

    gchar* sql = g_strconcat( "CREATE DATABASE `", db_name,"` "
            "CHARACTER SET = 'utf8' COLLATE = 'utf8_general_ci'", NULL );
    if ( (rc = mysql_query( con, sql )) )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Fehler bei CREATE "
                "DATABASE ", db_name, ":\n", mysql_error( con ),
                NULL );
        g_free( sql );

        return -1;
    }
    g_free( sql );

    //Mit der neuen Datenbank verbinden
    if ( (rc = mysql_select_db( con, db_name )) )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Fehler in cb_dialog_"
                "create_response:\nErzeugte Datenbank konnte nicht "
                "verbunden werden.\nmysql_select_db \"", db_name, "\":\n",
                mysql_error( con ), NULL );

        return -1;
    }

    rc = 0;
    sql = "CREATE TABLE `Sachgebiete` ( "
            "`Bezeichnung` VARCHAR(50) NOT NULL, "
            "PRIMARY KEY (`Bezeichnung`) ) "
            "ENGINE=InnoDB;"
            "INSERT INTO `Sachgebiete` VALUES ('');"
            "INSERT INTO `Sachgebiete` VALUES ('Strafsachen');"
            "INSERT INTO `Sachgebiete` VALUES ('Steuersachen');"
            "INSERT INTO Sachgebiete VALUES ('Steuerstrafsachen');"
            "INSERT INTO Sachgebiete VALUES ('Zivilsachen');"
            "INSERT INTO Sachgebiete VALUES ('Arbeitssachen');"
            "INSERT INTO Sachgebiete VALUES ('Verwaltungssachen');"
            "INSERT INTO Sachgebiete VALUES ('Familiensachen');"
            "INSERT INTO Sachgebiete VALUES ('sonst. Sachen');"

            "CREATE TABLE `Sachbearbeiter` ( "
            "`Sachbearbeiter-ID` VARCHAR(4) PRIMARY KEY NOT NULL, "
            "`Name` VARCHAR(40) NOT NULL ) "
            "ENGINE=InnoDB;"
            "INSERT INTO `Sachbearbeiter` VALUES ('','- ohne -');"
            "INSERT INTO `Sachbearbeiter` VALUES ('V','Peter Krieger');"
            "INSERT INTO `Sachbearbeiter` VALUES ('IV','Carsten Rubarth');"

            "CREATE TABLE `Akten` ( "
            "`RegNr` INT(11) NOT NULL, "
            "`RegJahr` INT(11) NOT NULL, "
            "`Bezeichnung` VARCHAR(50) NOT NULL, "
            "`Gegenstand` VARCHAR(50) NOT NULL, "
            "`Sachgebiet` VARCHAR(50) NOT NULL, "
            "`Sachbearbeiter-ID` VARCHAR(4) NOT NULL, "
            "`Anlagedatum` VARCHAR(40) NOT NULL, "
            "`Ablagenr` VARCHAR(40) NULL, "
            "PRIMARY KEY (`RegNr`, `RegJahr`), "
            "CONSTRAINT `FK_akten_sachgebiete` FOREIGN KEY "
            "(`Sachgebiet`) REFERENCES `Sachgebiete` (`Bezeichnung`) "
            "ON DELETE RESTRICT ON UPDATE RESTRICT, "
            "CONSTRAINT `FK_akten_sachbearbeiter` FOREIGN KEY "
            "(`Sachbearbeiter-ID`) REFERENCES `Sachbearbeiter` "
            "(`Sachbearbeiter-ID`) "
            "ON DELETE RESTRICT ON UPDATE RESTRICT ) "
            "ENGINE=InnoDB;"

            "CREATE TABLE `Adressen` ( "
            "`Adressnr` INT(11) NOT NULL, "
            "`Adresszeile1` VARCHAR(50) NULL DEFAULT NULL, "
            "`Titel` VARCHAR(50) NULL DEFAULT NULL, "
            "`Vorname` VARCHAR(50) NULL DEFAULT NULL, "
            "`Name` VARCHAR(50) NULL DEFAULT NULL, "
            "`Adresszusatz` VARCHAR(50) NULL DEFAULT NULL, "
            "`Strasse` VARCHAR(50) NULL DEFAULT NULL, "
            "`Hausnr` VARCHAR(50) NULL DEFAULT NULL, "
            "`PLZ` VARCHAR(8) NULL DEFAULT NULL, "
            "`Ort` VARCHAR(50) NULL DEFAULT NULL, "
            "`Land` VARCHAR(50) NULL DEFAULT NULL, "
            "`Telefon1` VARCHAR(50) NULL DEFAULT NULL, "
            "`Telefon2` VARCHAR(50) NULL DEFAULT NULL, "
            "`Telefon3` VARCHAR(50) NULL DEFAULT NULL, "
            "`Fax` VARCHAR(50) NULL DEFAULT NULL, "
            "`EMail` VARCHAR(50) NULL DEFAULT NULL, "
            "`Homepage` VARCHAR(50) NULL DEFAULT NULL, "
            "`IBAN` VARCHAR(50) NULL DEFAULT NULL, "
            "`BIC` VARCHAR(50) NULL DEFAULT NULL, "
            "`Anrede` VARCHAR(100) NULL DEFAULT NULL, "
            "`Bemerkungen` VARCHAR(500) NULL DEFAULT NULL, "
            "PRIMARY KEY (`Adressnr`) ) "
            "ENGINE=InnoDB;"

            "CREATE TABLE `Beteiligtenart` ( "
            "`Bezeichnung` VARCHAR(50) NOT NULL, "
            "PRIMARY KEY (`Bezeichnung`) ) "
            "ENGINE=InnoDB;"
            "INSERT INTO Beteiligtenart VALUES ('');"
            "INSERT INTO Beteiligtenart VALUES ('Mandant');"
            "INSERT INTO Beteiligtenart VALUES ('Staatsanwaltschaft');"
            "INSERT INTO Beteiligtenart VALUES ('Gericht I. Instanz');"
            "INSERT INTO Beteiligtenart VALUES ('Berufungsinstanz');"
            "INSERT INTO Beteiligtenart VALUES ('Revisionsinstanz');"
            "INSERT INTO Beteiligtenart VALUES ('Ermittlungsrichter');"
            "INSERT INTO Beteiligtenart VALUES ('Beschwerdeinstanz');"
            "INSERT INTO Beteiligtenart VALUES ('Polizei');"
            "INSERT INTO Beteiligtenart VALUES ('Gegner');"
            "INSERT INTO Beteiligtenart VALUES ('Gegnervertreter');"
            "INSERT INTO Beteiligtenart VALUES ('sonst. Korrespondenz');"

            "CREATE TABLE `Aktenbet` ( "
            "`ID`INT(11) NOT NULL AUTO_INCREMENT PRIMARY KEY, "
            "`RegNr` INT(11) NOT NULL, "
            "`RegJahr` INT(11) NOT NULL, "
            "`Adressnr` INT(11) NOT NULL, "
            "`Beteiligtenart` VARCHAR(50) NULL, "
            "`Betreff1` VARCHAR(70) NULL, "
            "`Betreff2` VARCHAR(70) NULL, "
            "`Betreff3` VARCHAR(70) NULL, "
            "CONSTRAINT `FK_aktenbet_akten` FOREIGN KEY (`RegNr`, "
            "`RegJahr`) REFERENCES `Akten` (`RegNr`, `RegJahr`) "
            "ON UPDATE CASCADE ON DELETE CASCADE, "
            "CONSTRAINT `FK_aktenbet_adressen` FOREIGN KEY "
            "(`Adressnr`) REFERENCES `Adressen` (`Adressnr`) "
            "ON UPDATE CASCADE ON DELETE CASCADE, "
            "CONSTRAINT `FK_aktenbet_beteiligtenart` FOREIGN KEY "
            "(`Beteiligtenart`) "
            "REFERENCES `Beteiligtenart` (`Bezeichnung`) "
            "ON DELETE RESTRICT ON UPDATE RESTRICT ) "
            "ENGINE=InnoDB;"

            "CREATE TABLE `WV` ( "
            "`RegNr` INT(11) NOT NULL, "
            "`RegJahr` INT(11) NOT NULL, "
            "`Datum` DATE NOT NULL, "
            "`Grund` VARCHAR(50) NOT NULL, "
            "`Sachbearbeiter-ID` VARCHAR(4) NOT NULL, "
            "CONSTRAINT `FK_wv_akten` FOREIGN KEY (`RegNr`, "
            "`RegJahr`) REFERENCES `Akten` (`RegNr`, `RegJahr`) "
            "ON UPDATE CASCADE ON DELETE CASCADE, "
            "CONSTRAINT `FK_wv_sachbearbeiter` FOREIGN KEY "
            "(`Sachbearbeiter-ID`) REFERENCES `Sachbearbeiter` "
            "(`Sachbearbeiter-ID`) "
            "ON DELETE RESTRICT ON UPDATE RESTRICT ) "
            "ENGINE=InnoDB;"

            "CREATE TABLE `Fristen` ( "
            "`RegNr` INT(11) NOT NULL, "
            "`RegJahr` INT(11) NOT NULL, "
            "`Datum` DATE NOT NULL, "
            "`Bezeichnung` VARCHAR(50) NOT NULL, "
            "CONSTRAINT `FK_fristen_akten` FOREIGN KEY (`RegNr`, "
            "`RegJahr`) REFERENCES `Akten` (`RegNr`, `RegJahr`) "
            "ON UPDATE CASCADE ON DELETE CASCADE ) "
            "ENGINE=InnoDB;"

            "CREATE TABLE `Gerichtstermine` ( "
            "`RegNr` INT(11) NOT NULL, "
            "`RegJahr` INT(11) NOT NULL, "
            "`Zeit` DATETIME NOT NULL, "
            "`Dauer` TIME NOT NULL, "
            "`Gericht` VARCHAR(50) NOT NULL, "
            "`Saal` VARCHAR(50) NOT NULL, "
            "`Az` VARCHAR(50) NOT NULL, "
            "`Bezeichnung` VARCHAR(50) NOT NULL, "
            "`Bemerkungen` VARCHAR(50) NOT NULL, "
            "`Sachbearbeiter-ID` VARCHAR(4) NOT NULL, "
            "CONSTRAINT `FK_gerichtstermine_akten` FOREIGN KEY (`RegNr`, "
            "`RegJahr`) REFERENCES `Akten` (`RegNr`, `RegJahr`) "
            "ON UPDATE CASCADE ON DELETE CASCADE, "
            "CONSTRAINT `FK_gerichtstermine_sachbearbeiter` FOREIGN KEY "
            "(`Sachbearbeiter-ID`) REFERENCES `Sachbearbeiter` "
            "(`Sachbearbeiter-ID`) "
            "ON DELETE RESTRICT ON UPDATE RESTRICT ) "
            "ENGINE=InnoDB;"

            "CREATE TABLE `Termine` ( "
            "`Zeit` DATETIME NOT NULL, "
            "`Dauer` TIME NULL DEFAULT NULL, "
            "`RegNr` INT(11) NULL DEFAULT NULL, "
            "`RegJahr` INT(11) NULL DEFAULT NULL, "
            "`Ort` VARCHAR(50) NOT NULL, "
            "`Bezeichnung` VARCHAR(50) NOT NULL, "
            "`Bemerkungen` VARCHAR(50) NOT NULL, "
            "`Sachbearbeiter-ID` VARCHAR(4) NOT NULL, "
            "CONSTRAINT `FK_termine_akten` FOREIGN KEY (`RegNr`, "
            "`RegJahr`) REFERENCES `Akten` (`RegNr`, `RegJahr`) "
            "ON UPDATE CASCADE ON DELETE CASCADE, "
            "CONSTRAINT `FK_termine_sachbearbeiter`FOREIGN KEY "
            "(`Sachbearbeiter-ID`) REFERENCES `Sachbearbeiter` "
            "(`Sachbearbeiter-ID`) "
            "ON DELETE RESTRICT ON UPDATE RESTRICT ) "
            "ENGINE=InnoDB;"

            "CREATE TABLE `Sachkonten` ( "
            "`Nr` INT(11) NOT NULL, "
            "`Bezeichnung` VARCHAR(50) NOT NULL ) "
            "ENGINE=InnoDB;"

            "CREATE TABLE `Journal` ( "
            "`ID` INT(11) NOT NULL, "
            "`Sachkontonr` INT(11) NOT NULL, "
            "`Gegenkontonr` INT(11) NOT NULL, "
            "`Betrag` DECIMAL(15,2) NOT NULL, "
            "`USt` INT(11) NOT NULL, "
            "`Buchungstext` VARCHAR(50) NOT NULL, "
            "`RegNr` INT(11) NULL DEFAULT NULL, "
            "`RegJahr` INT(11) NULL DEFAULT NULL, "
            "PRIMARY KEY (`ID`), "
            "CONSTRAINT `FK_journal_akten` FOREIGN KEY (`RegNr`, "
            "`RegJahr`) REFERENCES `Akten` (`RegNr`, `RegJahr`) "
            "ON UPDATE CASCADE ON DELETE CASCADE ) "
            "ENGINE=InnoDB;"

            "CREATE TABLE `log` ( "
            "`Zeit` DATETIME(3) NOT NULL, "
            "`SQL` VARCHAR(2000) NOT NULL, "
            "`Nutzer` VARCHAR(50) NOT NULL ) "
            "ENGINE=InnoDB;";

    rc = mysql_query( con, sql );

    if ( rc )
    {
        gint ret = 0;
        if ( errmsg ) *errmsg = g_strconcat( "Bei Einrichtung db:\n",
                mysql_error( con ), NULL );
        sql = g_strdup_printf( "DROP DATABASE `%s`", db_name );
        ret = mysql_query( con, sql );
        g_free( sql );
        if ( ret && errmsg ) add_string( *errmsg, g_strconcat( "\n\nFehler "
                "bei Löschen der Database ", db_name, ":\n",
                mysql_error( con ), NULL ) );
        else if ( errmsg ) add_string( *errmsg, g_strconcat( "Database ", db_name,
                " wurde gelöscht", NULL ) );

        return -1;
    }

    gint status = 0;

    do
    {
        rc = mysql_affected_rows( con );
        if ( rc < 0 ) break;
        /* more results? -1 = no, >0 = error, 0 = yes (keep looping) */
        status = mysql_next_result( con );
    } while (status == 0);

    if ( rc || (status > 0) )
    {
        gint ret = 0;
        if ( errmsg ) *errmsg = g_strconcat( "Bei Einrichtung db:\n",
                mysql_error( con ), NULL );
        sql = g_strdup_printf( "DROP DATABASE `%s`", db_name );
        ret = mysql_query( con, sql );
        g_free( sql );
        if ( ret && errmsg ) add_string( *errmsg, g_strconcat( "\n\nFehler "
                "bei Löschen der Database ", db_name, ":\n",
                mysql_error( con ), NULL ) );
        else if ( errmsg ) add_string( *errmsg, g_strconcat( "Database ", db_name,
                " wurde gelöscht", NULL ) );

        return -1;
    }

    return 0;
}


gint
db_connect_database( Sojus* sojus, GtkWidget* window, MYSQL* con )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    gchar* db_name = NULL;
    gboolean dont_ask = FALSE;

    dont_ask = (gboolean) (!sojus->db.con && g_strcmp0( db_name, "" ));
    db_name = g_settings_get_string( sojus->settings, "dbname" );

    do
    {
        gint ret = 0;
        gchar* errmsg = NULL;

        if ( dont_ask ) ret = mysql_select_db( con, db_name );
        else ret = 1;

        dont_ask = TRUE;

        if ( ret == 0 ) break;
        else
        {
            gint rc = 0;

            if ( g_strcmp0( mysql_error( con ), "" ) )
                    display_message( window, "Datenbank konnte nicht "
                    "verbunden werden -\n\nBei Aufruf db_select_database:\n",
                    mysql_error( con ), NULL );

            rc = dialog_with_buttons( window, "Datenbank auswählen",
                    "", &db_name, "Bestehende Datenbank", 1, "Datenbank erzeugen", 2,
                    "Abbrechen", GTK_RESPONSE_CANCEL, NULL );
            if ( rc == 1 ) continue;
            else if ( rc == 2 )
            {
                gint ret = 0;

                ret = db_create( sojus, con, db_name, &errmsg );
                if ( ret == -1 )
                {
                    display_message( window, "Datenbank """, db_name,
                            """ konnte nicht erzeugt werden -\nBei Aufruf "
                            "db_create:\n", errmsg, NULL );
                    g_free( errmsg );
                }
                else if ( ret == 1 ) display_message( window, "Datenbank """, db_name,
                            """ existiert bereits", NULL );
                else if ( ret == 0 ) break;
            }

            g_free( db_name );

            return 1;
        }
    } while ( 1 );

    rc = db_activate( sojus, con, db_name, &errmsg );
    if ( rc )
    {
        display_message( window, "Fehler beim Aktivieren der Datenbank -\n\n"
                "Bei Aufruf db_activate:\n", errmsg, NULL );
        g_free( errmsg );

        g_free( db_name );

        return -1;
    }

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
db_get_connection( Sojus* sojus, GtkWidget* window )
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

    if ( !sojus->db.con ) try_settings = TRUE;

    do
    {
        if ( try_settings ) con = db_connect( host, user, password, port, &errmsg );

        //Wenn Verbindung nicht hergestellt werden konnte
        if ( !con )
        {
            gint rc = 0;

            if ( !try_settings ) rc = 2;
            else rc = dialog_with_buttons( window, "Verbindung zu SQL-Server "
                    "konnte nicht hergestellt werden", errmsg, NULL,
                    "Erneut versuchen", 1, "Andere Verbindung", 2, "Abbrechen",
                    GTK_RESPONSE_CANCEL, NULL );

            g_free( errmsg );

            try_settings = TRUE;

            if ( rc == 1 ) continue;
            else if ( rc == 2 )
            {
                rc = db_get_con_params( window, &host, &port, &user, &password );
                if ( rc == GTK_RESPONSE_OK ) continue;
            }

            break;
        }
        else break;
    } while ( 1 );

    if ( con )
    {
        gint rc = 0;

        rc = db_connect_database( sojus, window, con );
        if ( rc == 0 )
        {
            g_settings_set_string( sojus->settings, "host", host );
            g_settings_set_int( sojus->settings, "port", port );
            g_settings_set_string( sojus->settings, "user", user );
            g_settings_set_string( sojus->settings, "password", password );

            mysql_close( sojus->db.con );
            sojus->db.con = con;
        }
        else mysql_close( con );
    }

    g_free( host );
    g_free( user );
    g_free( password );

    if ( sojus->db.con ) return 0;

    return 1;
}

