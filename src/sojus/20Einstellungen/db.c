#include "../globals.h"

#include "../../misc.h"


MYSQL*
db_connect( GtkWidget* window, const gchar* host, const gchar* user, const gchar*
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


static void
cb_button_con_abbrechen_clicked( GtkButton* button, gpointer user_data )
{
    gtk_widget_destroy( (GtkWidget*) user_data );

    return;
}


static void
cb_button_con_ok_clicked( GtkButton* button, gpointer user_data )
{
    gchar* errmsg = NULL;

    GtkWidget* connection_window = (GtkWidget*) user_data;
    Sojus* sojus = (Sojus*) g_object_get_data( G_OBJECT(connection_window), "sojus" );

    //alte Verbindung löschen
    g_free( sojus->db.host );
    g_free( sojus->db.user );
    g_free( sojus->db.password );
    g_free( sojus->db.db_name );
    sojus->db.db_name = g_strdup( "" );
    GSettings* settings = settings_open( );
    g_settings_set_string( settings, "dbname", "" );
    g_object_unref( settings );
    widgets_desktop_label_db( G_OBJECT(sojus->app_window), "" );

    //entries einlesen
    GtkWidget* entry_host = g_object_get_data( G_OBJECT(connection_window), "entry_host" );
    GtkWidget* entry_port = g_object_get_data( G_OBJECT(connection_window), "entry_port" );
    GtkWidget* entry_user = g_object_get_data( G_OBJECT(connection_window), "entry_user" );
    GtkWidget* entry_password = g_object_get_data( G_OBJECT(connection_window), "entry_password" );

    const gchar* host = gtk_entry_get_text( GTK_ENTRY(entry_host) );
    const gchar* port = gtk_entry_get_text( GTK_ENTRY(entry_port) );
    const gchar* user = gtk_entry_get_text( GTK_ENTRY(entry_user) );
    const gchar* password = gtk_entry_get_text( GTK_ENTRY(entry_password) );

    //port umwandeln
    gint port_nummer = atoi( port );

    //Verbindung versuchen
    mysql_close( sojus->db.con );

    MYSQL* con = db_connect( connection_window, host, user, password,
            port_nummer, &errmsg );
    sojus->db.con = con;

    if ( con )
    {
        sojus->db.host = g_strdup( host );
        sojus->db.port = port_nummer;
        sojus->db.user = g_strdup( user );
        sojus->db.password = g_strdup( password );

    }
    else
    {
        sojus->db.host = g_strdup( "" );
        sojus->db.port = 0;
        sojus->db.user = g_strdup( "" );
        sojus->db.password = g_strdup( "" );
    }

    widgets_desktop_db_con( G_OBJECT(sojus->app_window), con == NULL ? FALSE : TRUE );
    widgets_desktop_label_con( G_OBJECT(sojus->app_window), sojus->db.host,
            sojus->db.port, sojus->db.user );

    if ( !con )
    {
        display_message( connection_window, "Datenbankverbindung konnte "
                "nicht hergestellt werden:\n\nBei Aufruf von sql_connect: \n",
                errmsg, NULL );
        g_free( errmsg );
    }

    settings_con_speichern( sojus->db.host, sojus->db.port, sojus->db.user,
            sojus->db.password );

    gtk_widget_destroy( connection_window );

    return;
}


void
db_connection_window( Sojus* sojus )
{
    GtkWidget* connection_window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_title( GTK_WINDOW(connection_window), "Verbindung SQL-Server" );
    gtk_window_set_default_size( GTK_WINDOW(connection_window), 500, 300 );

    GtkWidget* grid = gtk_grid_new( );
    gtk_container_add( GTK_CONTAINER(connection_window), grid );

    //Host
    GtkWidget* frame_host = gtk_frame_new( "Host" );
    GtkWidget* entry_host = gtk_entry_new( );
    gtk_container_add( GTK_CONTAINER(frame_host), entry_host );
    gtk_grid_attach( GTK_GRID(grid), frame_host, 0, 0, 1, 1 );

    //port
    GtkWidget* frame_port = gtk_frame_new( "Port" );
    GtkWidget* entry_port = gtk_entry_new( );
    gtk_container_add( GTK_CONTAINER(frame_port), entry_port );
    gtk_grid_attach( GTK_GRID(grid), frame_port , 0, 1, 1, 1 );

    //user
    GtkWidget* frame_user = gtk_frame_new( "User" );
    GtkWidget* entry_user = gtk_entry_new( );
    gtk_container_add( GTK_CONTAINER(frame_user), entry_user );
    gtk_grid_attach( GTK_GRID(grid), frame_user , 0, 2, 1, 1 );

    //password
    GtkWidget* frame_password = gtk_frame_new( "Passwort" );
    GtkWidget* entry_password= gtk_entry_new( );
    gtk_container_add( GTK_CONTAINER(frame_password), entry_password );
    gtk_grid_attach( GTK_GRID(grid), frame_password , 0, 3, 1, 1 );

    GtkWidget* button_con_ok = gtk_button_new_with_label( "Ok" );
    gtk_grid_attach( GTK_GRID(grid), button_con_ok , 0, 4, 1, 1 );

    GtkWidget* button_con_abbrechen = gtk_button_new_with_label( "Abbrechen" );
    gtk_grid_attach( GTK_GRID(grid), button_con_abbrechen , 1, 4, 1, 1 );

    g_object_set_data( G_OBJECT(connection_window), "sojus", sojus );
    g_object_set_data( G_OBJECT(connection_window), "entry_host", entry_host );
    g_object_set_data( G_OBJECT(connection_window), "entry_port", entry_port );
    g_object_set_data( G_OBJECT(connection_window), "entry_user", entry_user );
    g_object_set_data( G_OBJECT(connection_window), "entry_password", entry_password );

    g_signal_connect_swapped( entry_host, "activate",
            G_CALLBACK(gtk_widget_grab_focus), entry_port );
    g_signal_connect_swapped( entry_port, "activate",
            G_CALLBACK(gtk_widget_grab_focus), entry_user );
    g_signal_connect_swapped( entry_user, "activate",
            G_CALLBACK(gtk_widget_grab_focus), entry_password );
    g_signal_connect_swapped( entry_password, "activate",
            G_CALLBACK(gtk_widget_grab_focus), entry_host );

    g_signal_connect( button_con_ok, "clicked",
            G_CALLBACK(cb_button_con_ok_clicked), connection_window );
    g_signal_connect( button_con_abbrechen, "clicked",
            G_CALLBACK(cb_button_con_abbrechen_clicked), connection_window );

    gtk_widget_grab_focus( entry_host );

    gtk_widget_show_all( connection_window );

    return;
}


static gint
db_select_database( MYSQL* con, const gchar* db_name, gchar** errmsg )
{
    //Falls entry leer ist
    if ( !g_strcmp0( db_name, "" ) )
    {
        *errmsg = g_strdup( "Bitte geben Sie einen Namen ein" );
        return -1;
    }

    //Verbinden
    if ( mysql_select_db( con, db_name ) )//Falls Fehler bei Verbindung
    {
        *errmsg = g_strconcat( "Fehler bei Auswahl der Datenbank ", db_name,
                ":\n", mysql_error( con ), NULL );
        return -1;
    }

    return 0;
}


gint
db_active( GtkWidget* window, const gchar* db_name, gchar** errmsg )
{
    Sojus* sojus = (Sojus*) g_object_get_data( G_OBJECT(window), "sojus" );

    //Sachgebiete- und BetArt-Array leeren
    g_ptr_array_remove_range( sojus->sachgebiete, 0, sojus->sachgebiete->len );
    g_ptr_array_remove_range( sojus->beteiligtenart, 0, sojus->beteiligtenart->len );
    g_ptr_array_remove_range( sojus->sachbearbeiter, 0, sojus->sachbearbeiter->len );

    GSettings* settings = settings_open( );

    g_free( sojus->db.db_name );

    if ( db_select_database( sojus->db.con, db_name, errmsg ) )
    {
        if ( errmsg ) *errmsg = add_string( g_strdup( "Bei Aufruf db_select_database:\n" ),
                *errmsg );

        sojus->db.db_name = NULL;

        widgets_desktop_db_name( sojus, FALSE );
        widgets_desktop_label_db( G_OBJECT(sojus->app_window), "" );
        widgets_desktop_db_name( sojus, FALSE );

        g_settings_set_string( settings, "dbname", "" );
        g_object_unref( settings );

        return -1;
    }

    sojus->db.db_name = g_strdup( db_name );

    g_settings_set_string( settings, "dbname", db_name );

    widgets_desktop_db_name( sojus, TRUE );
    widgets_desktop_label_db( G_OBJECT(sojus->app_window), db_name );

    //Sachgebiete einlesen
    gchar* sql = NULL;
    sql = "SELECT * FROM Sachgebiete;";
    if ( mysql_query( sojus->db.con, sql ) ) display_message( window,
            "Fehler in db_active:\nnmysql_query \"", sql, "\":\n", mysql_error(
            sojus->db.con ), NULL );
    else
    {
        MYSQL_RES* mysql_res = mysql_store_result( sojus->db.con );
        MYSQL_ROW mysql_row = NULL;
        while ( (mysql_row = mysql_fetch_row( mysql_res )) ) g_ptr_array_add(
                sojus->sachgebiete, g_strdup( mysql_row[0] ) );
        mysql_free_result( mysql_res );
    }

    //Beteiligtenart einlesen
    sql = "SELECT * FROM Beteiligtenart;";
    if ( mysql_query( sojus->db.con, sql ) ) display_message( window,
            "Fehler in db_active:\nmysql_query \"", sql, "\"\n", mysql_error(
            sojus->db.con ), NULL );
    else
    {
        MYSQL_RES* mysql_res = mysql_store_result( sojus->db.con );
        MYSQL_ROW mysql_row = NULL;
        while ( (mysql_row = mysql_fetch_row( mysql_res )) ) g_ptr_array_add(
                sojus->beteiligtenart, g_strdup( mysql_row[0] ) );
        mysql_free_result( mysql_res );
    }
    //Sachbearbeiter einlesen
    sql = "SELECT * FROM Sachbearbeiter ORDER BY `Sachbearbeiter-ID` ASC;";
    if ( mysql_query( sojus->db.con, sql ) ) display_message( window,
            "Fehler in db_active:\nmysql_query \"", sql, "\"\n", mysql_error(
            sojus->db.con ), NULL );
    else
    {
        MYSQL_RES* mysql_res = mysql_store_result( sojus->db.con );
        MYSQL_ROW mysql_row = NULL;
        while ( (mysql_row = mysql_fetch_row( mysql_res )) ) g_ptr_array_add(
                sojus->sachbearbeiter, g_strdup( mysql_row[0] ) );
        mysql_free_result( mysql_res );
    }

    g_object_unref( settings );

    return 0;
}


static void
cb_dialog_select_response( GtkDialog* dialog, gint response_id, gpointer user_data )
{
    if ( response_id == GTK_RESPONSE_ACCEPT )
    {
        GtkWidget* dialog_box = gtk_dialog_get_content_area( dialog );
        GList* list = gtk_container_get_children( GTK_CONTAINER(dialog_box) );

        const gchar* db_name = gtk_entry_get_text( GTK_ENTRY(list->data) );
        g_list_free( list );

        gchar* errmsg = NULL;
        gint rc = 0;
        rc = db_active( GTK_WIDGET(dialog), db_name, &errmsg );
        if ( rc )
        {
            display_message( GTK_WIDGET(dialog), "Fehler in cb_dialog_"
                    "select_response:\n", errmsg, NULL );
            g_free( errmsg );
        }
    }

    gtk_widget_destroy( GTK_WIDGET(dialog) );

    return;
}


void
db_select( Sojus* sojus )
{
    if ( !(sojus->db.con) )
    {
        display_message( sojus->app_window, "Keine Verbindung zum "
                "SQL-Server\nBitte zuerst Verbindung herstellen", NULL );
        return;
    }

    GtkWidget* db_con_dialog = gtk_dialog_new_with_buttons( "Datenbank "
            "verbinden", GTK_WINDOW(sojus->app_window), GTK_DIALOG_MODAL |
            GTK_DIALOG_DESTROY_WITH_PARENT, "Verbinden", GTK_RESPONSE_ACCEPT,
            "Abbrechen", GTK_RESPONSE_REJECT, NULL );

    g_object_set_data( G_OBJECT(db_con_dialog), "sojus", (gpointer) sojus );

    GtkWidget* dialog_box = gtk_dialog_get_content_area(
            GTK_DIALOG(db_con_dialog) );

    GtkWidget* entry_db_name = gtk_entry_new( );
    gtk_box_pack_start( GTK_BOX(dialog_box), entry_db_name, TRUE, TRUE, 1 );

    g_signal_connect( db_con_dialog, "response",
            G_CALLBACK(cb_dialog_select_response), (gpointer) sojus );
    gtk_widget_show_all( db_con_dialog );

    return;
}


/****************************************
*   Erstellung einer neuen Datenbank
*   Verbindung zu SQL-Server mit
*   ausreichenden Rechten muß hergestellt sein
**************************************************/
void
cb_dialog_create_response( GtkDialog* dialog, gint response_id, gpointer user_data )
{
    Sojus* sojus = (Sojus*) user_data;

    if ( response_id == GTK_RESPONSE_ACCEPT )
    {
        GtkWidget* dialog_box = gtk_dialog_get_content_area( dialog );
        GList* list = gtk_container_get_children( GTK_CONTAINER(dialog_box) );

        const gchar* db_name = gtk_entry_get_text( GTK_ENTRY(list->data) );

        g_list_free( list );

        //Falls entry leer ist
        if ( !g_strcmp0( db_name, "" ) )
        {
            display_message( GTK_WIDGET(dialog), "Kein gültiger Name",
                    NULL );
            gtk_widget_destroy( GTK_WIDGET(dialog) );

            return;
        }

        //prüfen, ob db "db_name" schon existiert
        if ( mysql_select_db( sojus->db.con, db_name ) ) //Falls nicht:
        {
            //neue Datenbank erstellen
            gint rc = 0;

            gchar* sql = g_strconcat( "CREATE DATABASE `", db_name,"` "
                    "CHARACTER SET = 'utf8' COLLATE = 'utf8_general_ci'", NULL );
            if ( (rc = mysql_query( sojus->db.con, sql )) )
            {
                display_message( GTK_WIDGET(dialog), "Fehler bei CREATE "
                        "DATABASE ", db_name, ":\n", mysql_error( sojus->db.con ),
                        NULL );
                g_free( sql );
                gtk_widget_destroy( GTK_WIDGET(dialog) );

                return;
            }
            g_free( sql );

            //Mit der neuen Datenbank verbinden
            if ( (rc = mysql_select_db( sojus->db.con, db_name )) )
            {
                display_message( GTK_WIDGET(dialog), "Fehler in cb_dialog_"
                        "create_response:\nErzeugte Datenbank konnte nicht "
                        "verbunden werden.\nmysql_select_db \"", db_name, "\":\n",
                        mysql_error( sojus->db.con ), NULL );
                gtk_widget_destroy( GTK_WIDGET(dialog) );

                return;
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

            mysql_query( sojus->db.con, sql );
            gint status = 0;

            do
            {
                rc = mysql_affected_rows( sojus->db.con );
                if ( rc < 0 ) break;
                /* more results? -1 = no, >0 = error, 0 = yes (keep looping) */
                status = mysql_next_result( sojus->db.con );
            } while (status == 0);

            if ( rc || (status > 0) )
            {
                display_message( GTK_WIDGET(dialog), "Fehler in db_create:\n",
                        mysql_error( sojus->db.con ), NULL );
                sql = g_strdup_printf( "DROP DATABASE `%s`", db_name );
                rc = mysql_query( sojus->db.con, sql );
                g_free( sql );
                if ( rc ) display_message( GTK_WIDGET(dialog), "Fehler in "
                        "db_create:\nDROP DATABASE `", db_name, "`:\n",
                        mysql_error( sojus->db.con ), NULL );
                gtk_widget_destroy( GTK_WIDGET(dialog) );

                return;
            }

            //Datenbank aktivieren
            gchar* errmsg = NULL;
            rc = db_active( GTK_WIDGET(dialog), db_name, &errmsg );
            if ( rc )
            {
                display_message( GTK_WIDGET(dialog), "Fehler in cb_dialog_create_"
                        "response:\ndb_active \"", db_name, "\":\n", errmsg, NULL );
                sql = g_strconcat( "DROP DATABASE ", db_name, ";", NULL );
                mysql_query( sojus->db.con, sql );
                g_free( sql );
                gtk_widget_destroy( GTK_WIDGET(dialog) );

                return;
            }

            //DB-Namen in Settings speichern
            GSettings* settings = settings_open( );
            g_settings_set_string( settings, "dbname", db_name );
            g_object_unref( settings );

            widgets_desktop_db_con( G_OBJECT(sojus->app_window), TRUE );
            widgets_desktop_db_name( sojus, TRUE );
            widgets_desktop_label_db( G_OBJECT(sojus->app_window), db_name );
        }
        else
        {
            display_message( GTK_WIDGET(dialog), "Datenbank ", db_name,
                    " existiert bereits!\nMit Datenbank ", db_name,
                    " verbunden.", NULL );

            widgets_desktop_db_con( G_OBJECT(sojus->app_window), TRUE );
            widgets_desktop_label_db( G_OBJECT(sojus->app_window), db_name );
        }
    }

    gtk_widget_destroy( GTK_WIDGET(dialog) );

    return;
}


void
db_create( Sojus* sojus )
{
    if ( !(sojus->db.con) )
    {
        display_message( sojus->app_window, "Keine Verbindung zum "
                "SQL-Server\nBitte zuerst Verbindung herstellen", NULL );
        return;
    }

    GtkWidget* db_con_dialog = gtk_dialog_new_with_buttons( "Datenbank "
            "erstellen", GTK_WINDOW(sojus->app_window), GTK_DIALOG_MODAL |
            GTK_DIALOG_DESTROY_WITH_PARENT, "Erstellen", GTK_RESPONSE_ACCEPT,
            "Abbrechen", GTK_RESPONSE_REJECT, NULL );

    g_object_set_data( G_OBJECT(db_con_dialog), "sojus", (gpointer) sojus );

    GtkWidget* dialog_box = gtk_dialog_get_content_area(
            GTK_DIALOG(db_con_dialog) );

    GtkWidget* entry_db_name = gtk_entry_new( );
    gtk_box_pack_start( GTK_BOX(dialog_box), entry_db_name, TRUE, TRUE, 1 );

    g_signal_connect( db_con_dialog, "response",
            G_CALLBACK(cb_dialog_create_response), (gpointer) sojus );
    gtk_widget_show_all( db_con_dialog );

    return;
}
