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
/*
CREATE TABLE Person ( "
ID INT(11) NOT NULL, "


                     */
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

    gchar* sql2 =
            "DROP TABLE IF EXISTS labels; "
            "DROP TABLE IF EXISTS entities; "
            "DROP TABLE IF EXISTS rels; "
            "DROP TABLE IF EXISTS properties; "

            "CREATE TABLE IF NOT EXISTS labels ( "
                "ID INTEGER PRIMARY KEY, "
                "label TEXT NOT NULL, "
                "parent INTEGER NOT NULL, "
                "FOREIGN KEY(parent) REFERENCES labels (ID) "
            "); "

            "INSERT INTO labels (ID, label, parent) VALUES "
                "(0, 'root', 0), "
                "(1, 'nodes', 0), "

                "(100, 'Rechtssubjekt', 1), "

                "(110, 'Rechtsperson', 100), "
                "(115, 'natürliche Person', 110), "
                "(120, 'juristische Person', 110), "
                "(130, 'priv jur Person', 120), "
                "(131, 'GmbH', 130), "
                "(132, 'UG', 130), "
                "(133, 'AG', 130), "
                "(134, 'Verein', 130), "
                "(140, 'öffr jur Person', 120), "
                "(150, 'Körperschaft', 130), "
                "(155, 'Gebietskörperschaft', 150), "
                "(156, 'Staat', 155), "
                "(158, 'Bundesland', 155), "
                "(160, 'Kreis', 155), "
                "(162, 'Gemeinde', 155), "

                "(170, 'Personenmehrheit', 100), "

                "(200, 'Organ', 1),
                "(320, 'Behörde', 300), "
                "(330, 'Gericht', 320), "
                "(340, 'Oberlandesgericht', 330), "
                "(350, 'Landgericht', 330), "
                "(360, 'Amtsgericht', 330), "
                "(370, 'Finanzgericht', 330), "
                "(380, 'Verwaltungsgericht', 330), "
                "(390, 'Oberverwaltungsgericht', 330), "
                "(400, 'Arbeitsgericht', 330), "
                "(410, 'Landesarbeitsgericht', 330), "
                "(420, 'Sozialgericht', 330), "
                "(430, 'Landessozialgericht', 330), "
                "(440, 'Staatsanwaltschaft', 320), "
                "(450, 'Generalstaatsanwaltschaft', 320), "
                "(460, 'Staatsanwaltschaft beim Landgericht', 320), "

                "(300, 'Verfahren', 1), "

                "(600, 'Konvolut', 1), "
                "(610, 'Aktenbestandteil', 600),"
                "(620, 'Akte', 610), "
                "(630, 'Aktenband', 610), "

                "(650, 'Fundstelle', 1), "

                "(700, 'angebundenes Objekt', 1), "
                "(750, 'Urkunde', 700), "
                "(760, 'Urkunde ohne Adressat', 750), "
                "(770, 'Vermerk', 760), "
                "(780, 'Verfügung', 760), "
                "(790, 'Entscheidung', 760), "
                "(800, 'Beschluß', 790), "
                "(810, 'Urteil', 790), "
                "(820, 'Protokoll', 760), "
                "(830, 'Vernehmungsprotokoll', 820), "
                "(832, 'Durchs.-/Sicherst.Protokoll', 820), "
                "(834, 'Gesprächsniederschrift', 820), "
                "(850, 'Urkunde mit Adressat', 750), "
                "(860, 'Schreiben', 850), "
                "(870, 'Übersendungsschreiben', 860), "
                "(880, 'Bescheid', 850), "
                "(890, 'Antrag', 850), "
                "(900, 'Stellungnahme', 850), "

                "(1000, 'Ort', 1), "
                "(1010, 'Straße, 1000), "
                "(1020, 'Adresse', 1000), "
                "(1030, 'Geoposition', 1000), "

                "(1100, 'Telefonnetz', 1), "
                "(1110, 'Ländernetz', 1100), "
                "(1120, 'Ortsnetz', 1100), "
                "(1130, 'Mobilfunknetz', 1100), "

                "(1140, 'TK-Anschluß', 1), "
                "(1150, 'Teilnehmernummer', 1140), "
                "(1160, 'IMSI', 1140), "
                "(1170, 'IMEI', 1140), "

                "(9000, 'value', 1), "
                "(9010, 'string', 9000), "
                "(9020, 'int', 9000), "
                "(9030, 'float', 9000), "
                "(9040, 'time', 9000), "

                "(10000, 'Prädikate', 0), "
                "(10005, '_von_, 0), "
                "(10006, '_bis_', 0), "
                "(10010, '_heißt_', 10000), "
                "(10011, '_hat Vornamen, 10000), "



                "(10010, '_hat node_id_', 10000), " //nur property
                "(10020, '_hat Fundstelle_', 10000), " //nur edge
                "(10050, '_gehört zu_', 10000), "

                "(10106, '_hat Vornamen_', 10100), " //E

                "(10108, '_hat Hausnr_', 10000), "
                "(10110, '_ist ansässig_', 10000, '10200,10210', '1020'), "
                "(10150, '_befindet sich_', 10000, '10200,10210', '1000'), "

                "(10250, '_von (Aktenblatt)_', 10000), " //nur property
                "(10260, '_bis (Aktenblatt)_', 10000), " //nur property
                "(10270, '_gehört zu Verfahren_', 10000), " //Akte
                "(10300, '_wird geführt bei_', 10000), " //Verfahren bei Gericht, Polizei, StA
                "(10310, '_hat Aktenzeichen', 10000) " //property von 10300

                "(10400, '_verfaßt von_', 10000), "
                "(10410, '_handelnd_durch_', 10000) " //qualifier, wenn Behörde tätig wurde
                "; "

            "CREATE TABLE IF NOT EXISTS adm_rels ( "
                "from INTEGER NOT NULL, "
                "rel INTEGER NOT NULL, "
                "to INTEGER NOT NULL, "
                "FOREIGN KEY (from) REFERENCES labels (ID), "
                "FOREIGN KEY (to) REFERENCES labels (ID) "
            "); "

            "INSERT INTO adm_rels (from,to) VALUES "
                "(100, 10010, 11), " //Rechtssubjekt _heißt_ (string)
                "(115, 10011, 11), " //natürliche Person _hat Vornamen_ (string)




                "(10010, 10005, 14), " //_heißt_ _von_ (string)
                "(10010, 10006, 14), " //_heißt_ _bis_ (string)

                "(650, 10050, 600), " //Fundstelle gehört zu (Konvolut)
                "(500, 10300), "
                "(620, 10270), "
                "(630, 10290), "
                "(10020, 100), "
                "(10100, 9010), " //_hat Namen_ und Kinder haben property string
                "(10100, 10200), " //_hat Namen_ und Kinder haben Prädikat _von_
                "(10100, 10210), " //_hat Namen_ und Kinder haben Prädikat _bis_
                "(10200, 9040), " //_von_ hat property DATETIME
                "(10210, 9040), " //_bis_ hat property DATETIME
                "(10270, 500), "
                "(10290, 600), "
                "(10300, 320) "
            "; "

            "CREATE TABLE IF NOT EXISTS entities( "
                "ID INTEGER NOT NULL, "
                "label INTEGER NOT NULL, "
                "FOREIGN KEY (label) REFERENCES labels (ID), "
                "PRIMARY KEY(ID) "
            "); "

            "CREATE TABLE IF NOT EXISTS rels ( "
                "entity INTEGER NOT NULL, "
                "subject INTEGER NOT NULL, "
                "object INTEGER NOT NULL, "
                "FOREIGN KEY (entity) REFERENCES entities (ID), "
                "FOREIGN KEY (subject) REFERENCES entities (ID), "
                "FOREIGN KEY (object) REFERENCES entities (ID) "
            "); "

            "CREATE TABLE IF NOT EXISTS strings ( "
                "entity INTEGER NOT NULL, "
                "value TEXT, "
                "FOREIGN KEY (entity) REFERENCES entities (ID), "
            "); "

            "CREATE TABLE IF NOT EXISTS ints ( "
                "entity INTEGER NOT NULL, "
                "value INTEGER, "
                "FOREIGN KEY (entity) REFERENCES entities (ID), "
            "); "

            "CREATE TABLE IF NOT EXISTS reals ( "
                "entity INTEGER NOT NULL, "
                "value REAL, "
                "FOREIGN KEY (entity) REFERENCES entities (ID), "
            "); "

            "CREATE TABLE IF NOT EXISTS times ( "
                "entity INTEGER NOT NULL, "
                "value DATETIME, "
                "FOREIGN KEY (entity) REFERENCES entities (ID), "
            "); "

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

