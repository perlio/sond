/*
sond (sond_database.c) - Akten, Beweisstücke, Unterlagen
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


#include <gtk/gtk.h>
#include <sqlite3.h>
#include <mysql/mysql.h>

#include "misc.h"
#include "sond_database.h"
#include "zond/zond_dbase.h"


/*
gboolean
sond_database_is_valid_time( const gchar* time )
{
    gchar* ptr = NULL;
    gboolean res = TRUE;

    if ( !time ) return FALSE;
    if ( !(strlen( time ) == 4 ||
           strlen( time ) == 7 ||
           strlen( time ) == 10 ||
           strlen( time ) == 13 ||
           strlen( time ) == 16 ||
           strlen( time ) == 19) ) return FALSE;

    ptr = time;

    for (gint i = 0; i < 4; i++ )
    {
        if ( time[i] < '0' || time[i] > '9' ) return FALSE;
    }

    if ( time[4] = NULL )
    {
        if ( precision ) *precision = 1;
        return TRUE;
    }
    else if ( time[4] != '-' ) return FALSE;

    if ( !(time[5] == '0' && time[6] >= '1' && time[6] <= '9') &&
            !(time[5] == '1' && time[6] >= '0' && time[6] <= 2) ) return FALSE;

    if ( time[7] == NULL )
    {
        if ( precision ) *precision = 2;
        return TRUE;
    }
    else if ( time[7] != '-' ) return FALSE;
}*/

gint
sond_database_add_to_database( gpointer database, gchar** errmsg )
{
    const gchar* sql[] = {
            "CREATE TABLE IF NOT EXISTS labels ( "
                "ID INTEGER, "
                "label TEXT NOT NULL, "
                "parent INTEGER NOT NULL, "
                "FOREIGN KEY (parent) REFERENCES labels (ID), "
                "PRIMARY KEY (ID) "
            "); ",

            "CREATE TABLE IF NOT EXISTS adm_rels ( "
                "subject INTEGER NOT NULL, "
                "rel INTEGER NOT NULL, "
                "object INTEGER NOT NULL, "
                "FOREIGN KEY (subject) REFERENCES labels (ID), "
                "FOREIGN KEY (rel) REFERENCES labels (ID), "
                "FOREIGN KEY (object) REFERENCES labels (ID), "
                "UNIQUE (subject,rel,object) "
            "); ",

            "CREATE TABLE IF NOT EXISTS entities( "
                "ID INTEGER PRIMARY KEY AUTOINCREMENT, "
                "ID_label INTEGER NOT NULL, "
                "FOREIGN KEY (ID_label) REFERENCES labels (ID) "
            "); ",

            "CREATE TABLE IF NOT EXISTS rels ( "
                "entity_subject INTEGER NOT NULL, "
                "entity_rel INTEGER NOT NULL, "
                "entity_object INTEGER NOT NULL, "
                "FOREIGN KEY (entity_subject) REFERENCES entities (ID), "
                "FOREIGN KEY (entity_rel) REFERENCES entities (ID), "
                "FOREIGN KEY (entity_object) REFERENCES entities (ID) "
            "); ",

            "CREATE TABLE IF NOT EXISTS properties ( "
                "entity_subject INTEGER NOT NULL, "
                "entity_property INTEGER NOT NULL, "
                "value TEXT, "
                "FOREIGN KEY (entity_subject) REFERENCES entities (ID), "
                "FOREIGN KEY (entity_property) REFERENCES entities (ID) "
            "); ",

            "INSERT OR IGNORE INTO labels (ID, label, parent) VALUES "
                "(0, 'root', 0), "
                "(1, 'node', 0), "
                "(2, 'edge', 0), "
                "(3, 'property', 0), "
                "(4, 'value', 0), "

                //100 - 999: Knoten, allgemein
                "(100, 'Subjekt', 1), "

                "(110, 'Subjekt des öffentlichen Rechts', 100), "

                "(115, 'öffr jur Person', 110), "
                "(120, 'Körperschaft', 115), "
                "(135, 'Universität', 120), "
                "(140, 'Kammer', 120), "
                "(142, 'Rechtsanwaltskammer', 140), "
                "(145, 'Anstalt', 115), "

                "(160, 'Behörde', 110), "
                "(170, 'Gericht', 160), "
                "(175, 'Oberlandesgericht', 170), "
                "(180, 'Landgericht', 170), "
                "(185, 'Amtsgericht', 170), "
                "(190, 'Finanzgericht', 170), "
                "(195, 'Verwaltungsgericht', 170), "
                "(200, 'Oberverwaltungsgericht', 170), "
                "(205, 'Arbeitsgericht', 170), "
                "(210, 'Landesarbeitsgericht', 170), "
                "(215, 'Sozialgericht', 170), "
                "(220, 'Landessozialgericht', 170), "
                "(230, 'Staatsanwaltschaft', 160), "
                "(235, 'Generalstaatsanwaltschaft', 230), "
                "(240, 'Staatsanwaltschaft beim Landgericht', 230), "
                "(250, 'Polizeibehörde', 160), "
                "(255, 'Landeskriminalamt', 250), "
                "(260, 'Polizeipräsident', 250), "
                "(265, 'Kreispolizeibehörde', 250), "

                "(300, 'Subjekt des Privatrechts', 100), "

                "(310, 'natürliche Person', 300), "
                "(312, 'Mann', 310), "
                "(313, 'Frau', 310), "
                "(314, 'diverse natürliche Person', 310), "
                "(320, 'juristische Person', 300), "
                "(325, 'GmbH', 320), "
                "(330, 'UG', 320), "
                "(335, 'AG', 320), "
                "(340, 'Verein', 320), "

                "(350, 'Personenmehrheit', 300), "
                "(354, 'Gesellschaft', 350), "
                "(355, 'GbR', 354), "
                "(360, 'OHG', 354), "
                "(365, 'KG', 354), "
                "(370, 'Partnerschaft', 354), "

                "(375, 'Adressbestandteil', 1), "
                "(380, 'Land', 375), "
                "(390, 'Ort', 375), "
                "(400, 'Straße', 375), "

                "(425, 'Rufnummer', 1), "
                "(430, 'Stammrufnummer', 425), "

                "(435, 'IMSI', 1), "
                "(437, 'IMEI', 1), "

                "(440, 'Kontoverbindung', 1), "

                "(450, 'Sitz', 1), "
                "(452, 'Geschäftssitz', 450), "
                "(454, 'Kanzleisitz', 452), "
                "(456, 'Wohnsitz', 450), "
                "(460, 'Dienstsitz', 450), "

                "(470, 'Beruf-Dienstgrad', 1), "

                // 1000-5000: Knoten zond
                "(500, 'Verfahren', 1), "

                "(600, 'Konvolut', 1), "
                "(610, 'Aktenbestandteil', 600),"
                "(620, 'Aktenteil', 610), " //z.B. Hauptakte, BMO o.ä.
                "(630, 'Aktenband', 610), "

                "(650, 'Fundstelle', 1), "
                "(660, 'Datei', 1), "

                "(750, 'Urkunde', 1), "
                "(760, 'Urkunde ohne Adressat', 750), "
                "(770, 'Vermerk', 760), "
                "(780, 'Verfügung', 760), "
                "(790, 'Entscheidung', 760), "
                "(800, 'Beschluß', 790), "
                "(810, 'Urteil', 790), "
                "(820, 'Protokoll', 760), "
                "(830, 'Vernehmungsprotokoll', 820), "
                "(832, 'Durchs.-/Sicherst.Protokoll', 820), "
                "(834, 'TKÜ-Protokoll', 820), "
                "(836, 'Gemini-Protokoll', 834), "
                "(838, 'Case-Protokoll', 834), "
                "(850, 'Urkunde mit Adressat', 750), "
                "(860, 'Schreiben', 850), "
                "(870, 'Übersendungsschreiben', 860), "
                "(880, 'Bescheid', 850), "
                "(890, 'Antrag', 850), "
                "(900, 'Stellungnahme', 850), "

                "(1000, 'TKÜ-Maßnahme', 1), "

                "(1010, 'TKÜ-Ereignis', 1), "
                "(1012, 'Telefonat', 1010), "
                "(1015, 'SMS', 1010), "
                "(1020, 'Multimedianachricht', 1010), "

                //5000 - 8999: Knoten sojus
                "(5000, 'Akte', 1), "
                "(5005, 'Sachbearbeiter', 1), "
                "(5010, 'Sachgebiet', 1), "
                "(5020, 'Aktenbeteiligtenart', 1), "

                "(5040, 'Termin', 1), "

                //10000-15000: Prädikate, allgemein
                "(10000, '_gehört zu_', 2), "
                "(10010, '_hat_', 2), "
                "(10020, '_arbeitet an_', 2), "

                //Properties allgemein
                "(10025, '_Zeitpunkt_', 3), "
                "(10030, '_Beginn_', 10025), "
                "(10040, '_Ende_', 10025), "
                "(10050, '_Bemerkung_', 3), "
                "(10100, '_Name_', 3), "
                "(10110, '_Kürzel_', 3), "

                //Properties für Subjekt
                "(10120, '_E-Mail-Adresse_', 3), "
                "(10130, '_Homepage_', 3), "

                //Properties für natürliche Person
                "(11010, '_Vorname_', 3), "
                "(11020, '_Titel_', 3), "

                //Properties für Kontoverbindung
                "(11025, '_BIC_', 3), "
                "(11027, '_IBAN_', 3), "

                //Für Sitz
                "(11030, '_Hausnr_', 3), "
                "(11035, '_PLZ_', 3), "
                "(11040, '_Adresszusatz_', 3), "

                //für Sitz und _arbeitet an_
                "(11050, '_Durchwahl_', 3), "

                //prop für Fundstelle
                "(11051, '_Dateipfad_', 3), "
                "(11052, '_Seite von_', 3), "
                //"(11053, '_nameddest von_',2 ), "
                "(11054, '_index von_', 3), "
                "(11055, '_Seite bis_', 3), "
                //"(11056, '_nameddest bis_',2 ), "
                "(11057, '_index bis_', 3), "
                //"(11058, '_node id_', 2), "


                //Property für TKÜ-Maßnahme
                "(11059, '_Leitungsnummer_', 3), "

                //p für TKÜ-Ereignis
                "(11065, '_Korrelationsnummer_', 3), "
                "(11067, '_Art_', 3), "
                "(11070, '_GIS-Link_', 3), "
                "(11072, '_Standort Funkmast ÜA_', 3), "
                "(11075, '_Richtung_', 3), "
                "(11080, '_Text_', 3), "

                //Prädikate Gemini
                "(12000, '_hat als Sprecher am ÜA_', 2), "
                "(12005, '_hat als Sprecher am PA_', 2), "
/*                //15000-19999: Prädikate zond
                "(15150, '_befindet sich_', 2), "

                "(15250, '_von (Aktenblatt)_', 2), " //nur property
                "(15260, '_bis (Aktenblatt)_', 2), " //nur property
                "(15300, '_wird geführt bei_', 2), " //Verfahren bei Gericht, Polizei, StA
                "(15310, '_hat Aktenzeichen_', 2), " //property von 10300

                "(15400, '_verfaßt von_', 2), "
                "(15410, '_handelnd_durch_', 2), " //qualifier, wenn Behörde tätig wurde

                //20000-24999: Prädikate sojus
                "(20000, '_hat Registernummer_', 2), "
                "(20001, '_hat Registerjahr_', 2), "
                "(20030, '_hat als Aktenbeteiligten', 2), "
                "(20031, '_ist Aktenbeteiligtenart_', 2), "
                "(20032, '_hat als Betreff_', 2), "
*/
                //Wert
                "(100001, 'string', 4), "
                "(100002, 'int', 4), "
                "(100003, 'real', 4), "
                "(100004, 'time', 4) "
                "; ",

            "INSERT OR IGNORE INTO adm_rels (subject,rel,object) VALUES "

                "(100, 10010, 425), " //Subjekt _hat_ Teilnehmernummer
                "(100, 10010, 440), " //Subjekt _hat_ Kontoverbindung
                "(100, 10030, 100004), " //Subjekt _Beginn_(date)
                "(100, 10040, 100004), " //Subjekt _Ende_(date)
                "(100, 10050, 100001), " //Subjekt _Bemerkung_(string)
                "(100, 10100, 100001), " //Subjekt Name(string)
                "(100, 10120, 100001), " //Subjekt _E-Mail-Adresse_(string)
                "(100, 10130, 100001), " //Subjekt _Homepage_(string)

                "(110, 10010, 460), " //Subjekt des öffentlichen Rechts _hat_ Dienstsitz
                "(300, 10010, 452), " //Subjekt des Privatrechts _hat_Geschäftssitz
                "(310, 10010, 456), " //natürliche Person _hat_ Wohnsitz
                "(310, 10010, 470), " //natürliche Person _hat_ Beruf/Dienstgrad
                "(310, 10020, 450), " //natürliche Person _arbeitet an_ Sitz
                "(310, 11010, 100001), " //natürliche Person Vorname(string)
                "(310, 11020, 100001), " //natürliche Person Titel(string)

                "(375, 10100, 100001), " //Adressbestandteil Name(string)
                "(380, 10110, 100001), " //Land Kürzel(string)
                "(390, 10000, 380), " //Ort _gehört zu_ Land
                "(400, 10000, 390), " //Straße gehört zu Ort

                "(425, 10100, 100001), " //Telefonnummer Name(string) - meint: Telefonnr lautet "..."

                "(435, 10100, 100001), " //IMSI _hat_ Rufnummer
                "(437, 10100, 100001), " //IMEI _hat_ IMSI

                "(440, 10000, 100), " //Kontoverbindung _gehört zu_ Subjekt (Bank)
                "(440, 11025, 100001), " //Kontoverbindung _BIC_ (string)
                "(440, 11027, 100001), " //Kontoverbidnung _IBAN_ (string)

                "(450, 10000, 400), " //Sitz _gehört zu_ Straße
                "(450, 10010, 425), " //Sitz _hat_ Teilnehmernr
                "(450, 10120, 100001), " //Sitz _E-Mail-Adresse_(string)
                "(450, 10130, 100001), " //Sitz _Homepage_(string)
                "(450, 11030, 100001), " //Sitz Hausnr(string)
                "(450, 11035, 100001), " //Sitz _PLZ_(string)
                "(450, 11040, 100001), " //Sitz _Adresszusatz_(string)
                "(450, 11050, 100001), " //Sitz _Durchwahl_(string) (nicht nat. Personen zuzuordnende Durchwahlen, z.B. -0, -10,)

                "(470, 10100, 100001), " //Beruf-Dienstgrad _Name_(string)
                "(470, 10110, 100001), " //Beruf-Dienstgrad _Kürzel_(string)

                "(650, 11051, 100001), " //Fundstelle _Dateipfad_ (string)
                "(650, 11052, 100002), " //Fundstelle _Seite von_ (int)
                "(650, 11054, 100002), " //Fundstelle _index von_ (int)
                "(650, 11055, 100002), " //Fundstelle _Seite bis_ (int)
                "(650, 11057, 100002), " //Fundstelle _index bis_ (int)

                "(750, 10010, 650), " //Urkunde _hat_ Fundstelle

                "(1000, 10010, 310), " //TKÜ-Maßnahme _hat_ natürliche Person (= Überwachte Person)
                "(1000, 10010, 425), " //TKÜ-Maßnahme _hat_ Rufnummer
                "(1000, 10010, 435), " //TKÜ-Maßnahme _hat_ IMSI
                "(1000, 10010, 437), " //TKÜ-Maßnahme _hat_ IMEI
                "(1000, 10030, 100004), " //TKÜ-Maßnahme _Beginn_ (date)
                "(1000, 10030, 100004), " //TKÜ-Maßnahme _Ende_ (date)
                "(1000, 11059, 100001), " //TKÜ-Maßnahme _Leitungsnummer_ (string)

                "(1010, 10000, 1000), " //TKÜ-Ereignis _gehört zu_ TKÜ-Maßnahme
                "(1010, 10010, 425), " //TKÜ-Ereignis _hat_ Rufnummer [Partnernummer]
                "(1010, 10010, 834), " //TKÜ-Ereignis _hat_ TKÜ-Protokoll
                "(1010, 10025, 100004), " //TKÜ-Ereignis _Zeitpunkt_ (date)
                "(1010, 11065, 100001), " //TKÜ-Ereignis _Korrelationsnummer_ (string)
                "(1010, 11067, 100001), " //TKÜ-Ereignis _Art_ (string)
                "(1010, 11070, 100001), " //TKÜ-Ereignis _GIS-Link_ (string)
                "(1010, 11072, 100001), " //TKÜ-Ereignis _Funkmast_ (string)
                "(1010, 11075, 100001), " //TKÜ-Ereignis _Richtung_ (string)
                "(1010, 12000, 310), " //TKÜ-Ereignis _hat Sprecher am ÜA_ natürliche Person
                "(1010, 12005, 310), " //TKÜ-Ereignis _hat Sprecher am PA_ natürliche Person

                "(1015, 11080, 100001), " //SMS _Text_ (string)

                "(10010, 10025, 100004), " //_hat_ _Zeitpunkt_(date)
                "(10010, 10050, 100001), " //_hat_ _Bemerkung_(string)
                "(10020, 10025, 100004), " //_arbeitet an_ _Zeitpunkt_(date)
                "(10020, 10050, 100001), " //_arbeitet an_ _Bemerkung_(string)
                "(10020, 11050, 100001), " //_arbeitet an_ _Durchwahl_(string)
                "(10025, 10050, 100001), " //_Zeitpunkt_ _Bemerkung_(string)

                "(10100, 10030, 100004), " //_Name_ _Beginn_(date)
                "(10100, 10040, 100004), " //_Name_ _Ende_(date)
                "(10100, 10050, 100001), " //_Name_ _Bemerkung_(date)

                "(10120, 10030, 100004), " //_E-Mail-Adresse_ _Beginn_(date)
                "(10120, 10040, 100004), " //_E-Mail-Adresse_ _Ende_(date)
                "(10120, 10050, 100001), " //_E-Mail-Adresse_ _Bemerkung_(date)

                "(10130, 10030, 100004), " //_Homepage_ _Beginn_(date)
                "(10130, 10040, 100004), " //_Homepage_ _Ende_(date)
                "(10130, 10050, 100001), " //_Homepage_ _Bemerkung_(date)

                "(11010, 10030, 100004), " //_Vorname_ _Beginn_(date)
                "(11010, 10040, 100004), " //_Vorname_ _Ende_(date)
                "(11010, 10050, 100001), " //_Vorname_ _Bemerkung_(date)

                "(11020, 10030, 100004), " //_Titel_ _Beginn_(date)
                "(11020, 10040, 100004), " //_Titel_ _Ende_(date)
                "(11020, 10050, 100001), " //_Titel_ _Bemerkung_(date)

                "(11050, 10030, 100004), " //_Durchwahl_ _Beginn_(date)
                "(11050, 10040, 100004), " //_Durchwahl_ _Ende_(date)
                "(11050, 10050, 100001) " //_Durchwahl_ _Bemerkung_(date)

            "; " };

    if ( ZOND_IS_DBASE(database) )
    {
        ZondDBase* zond_dbase = ZOND_DBASE(database);

        for ( gint i = 0; i < nelem( sql ); i++ )
        {
            gint rc = 0;

            rc = sqlite3_exec( zond_dbase_get_dbase( zond_dbase ), sql[i], NULL, NULL, errmsg );
            if ( rc != SQLITE_OK ) ERROR_S
        }
    }
    else //mysql
    {
        gint rc = 0;
        gchar* sql_mysql = NULL;
        MYSQL* con = (MYSQL*) database;
        sql[2] = "CREATE TABLE IF NOT EXISTS entities( "
                "ID INTEGER PRIMARY KEY AUTO_INCREMENT, "
                "ID_label INTEGER NOT NULL, "
                "FOREIGN KEY (ID_label) REFERENCES labels (ID) "
            "); ";

        for ( gint i = 0; i < nelem( sql ) - 2; i++ )
        {
            gint rc = 0;

            rc = mysql_query( con, sql[i] );
            while ( rc ) //>0 = error, -1 = weitere res ?!?
            {
                if ( rc > 0 )
                {
                    if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf ", __func__,
                            ":\n", mysql_error( con ), NULL );
                    return -1;
                }

                rc = mysql_next_result( con );
            }
        }

        sql_mysql = g_strdup( sql[5] );

        //OR ausradieren
        sql_mysql[7] = ' ';
        sql_mysql[8] = ' ';

        rc = mysql_query( con, sql_mysql );
        g_free( sql_mysql );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf ", __func__, ":\n",
                    mysql_error( con ), NULL );
            return -1;
        }

        sql_mysql = g_strdup( sql[6] );

        //OR ausradieren
        sql_mysql[7] = ' ';
        sql_mysql[8] = ' ';

        rc = mysql_query( con, sql_mysql );
        g_free( sql_mysql );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf ", __func__, ":\n",
                    mysql_error( con ), NULL );
            return -1;
        }
    }

    return 0;
}


gint
sond_database_insert_entity( gpointer database, gint ID_label, gchar** errmsg )
{
    gint rc = 0;
    gint new_node_id = 0;

    const gchar* sql[] = {
            "INSERT INTO entities (ID_label) VALUES (?1);",

            "SELECT (last_insert_rowid()); " };

    if ( ZOND_IS_DBASE(database) )
    {
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_label );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label)" )

        rc = sqlite3_step( stmt[0] );
        if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step (insert)" )

        rc = sqlite3_step( stmt[1] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step (get last inserted rowid)" )

        if ( rc == SQLITE_ROW )new_node_id = sqlite3_column_int( stmt[1], 0 );
        else ERROR_S_MESSAGE( "entity konnte nicht eingefügt werden" )
    }
    else //maySQL-con wird übergeben
    {
        sql[2] = "SELECT (LAST_INSERT_ID()); ";

    }

    return new_node_id;
}


gint
sond_database_is_admitted_edge( gpointer database, gint ID_label_subject,
        gint ID_label_rel, gint ID_label_object, gchar** errmsg )
{
    const gchar* sql[] = {
            "SELECT COUNT(adm_rels.subject) FROM adm_rels AS adm_rels_subject JOIN "
            "(WITH RECURSIVE cte_labels (ID) AS ( "
                "VALUES (?1) "
                "UNION ALL "
                "SELECT labels.ID "
                    "FROM labels JOIN cte_labels WHERE "
                    "labels.parent = cte_labels.ID "
                ") SELECT ID AS ID_CTE FROM cte_labels) "
                "JOIN adm_rels AS adm_rels_object"
                "ON adm_rels_subject.rel = ID_CTE "
                "ON adm_rels_object.rel = ID_CTE "
                "WHERE adm_rels.subject=?2 AND adm_rels.object=?3; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_label_rel );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label_rel)" )

        rc = sqlite3_bind_int( stmt[0], 2, ID_label_subject );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label_subject)" )

        rc = sqlite3_bind_int( stmt[0], 3, ID_label_object );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label_object)" )

        rc = sqlite3_step( stmt[0] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )
        else if ( rc == SQLITE_DONE ) ERROR_S_MESSAGE( "Kein Ergebnis" )

        return (gboolean) sqlite3_column_int( stmt[0], 0 );
    }
    else //mysql
    {

    }

    return 0;
}


gint
sond_database_is_admitted_rel( gpointer database, gint ID_label_subject,
        gint ID_label_rel, gchar** errmsg )
{
    const gchar* sql[] = {
            "SELECT COUNT(adm_rels.subject) FROM adm_rels JOIN "
            "(WITH RECURSIVE cte_labels (ID) AS ( "
                "VALUES (?1) "
                "UNION ALL "
                "SELECT labels.ID "
                    "FROM labels JOIN cte_labels WHERE "
                    "labels.parent = cte_labels.ID "
                ") SELECT ID AS ID_CTE FROM cte_labels) "
                "ON adm_rels.rel = ID_CTE "
                "WHERE adm_rels.subject=?2; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_label_rel );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label_rel)" )

        rc = sqlite3_bind_int( stmt[0], 2, ID_label_subject );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label_subject)" )

        rc = sqlite3_step( stmt[0] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )
        else if ( rc == SQLITE_DONE ) ERROR_S_MESSAGE( "Kein Ergebnis" )

        return (gboolean) sqlite3_column_int( stmt[0], 0 );
    }
    else //mysql
    {

    }

    return 0;
}


gint
sond_database_label_is_equal_or_parent( gpointer database, gint ID_label_parent, gint ID_label_test, gchar** errmsg )
{
    const gchar* sql[] = {
            "(WITH RECURSIVE cte_labels (ID) AS ( "
                "VALUES (@1) "
                "UNION ALL "
                "SELECT labels.ID "
                    "FROM labels JOIN cte_labels WHERE "
                    "labels.parent = cte_labels.ID "
                ") SELECT ID AS ID_CTE FROM cte_labels) "
                "WHERE ID=@2; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_label_parent );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label_parent)" )

        rc = sqlite3_bind_int( stmt[0], 2, ID_label_test );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label_test)" )

        rc = sqlite3_step( stmt[0] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )

        if ( rc == SQLITE_DONE ) return 1;
    }
    else //mysql
    {

    }

    return 0;
}


gint
sond_database_get_ID_label_for_entity( gpointer database, gint ID_entity, gchar** errmsg )
{
    const gchar* sql[] = {
            "SELECT ID_label FROM entities WHERE ID=?1; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_entity );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_entity)" )

        rc = sqlite3_step( stmt[0] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )
        else if ( rc == SQLITE_DONE ) ERROR_S_MESSAGE( "ID_entity existiert nicht" )

        return sqlite3_column_int( stmt[0], 0 );
    }
    else //mysql
    {

    }

    return 0;
}


gint
sond_database_insert_rel( gpointer database, gint ID_entity_subject, gint ID_label_rel,
        gint ID_entity_object, gchar** errmsg )
{
    gint ID_new_entity_rel = 0;

    const gchar* sql[] = {
            "INSERT INTO rels (entity_subject, entity_rel, entity_object) "
            "VALUES (?1,?2,?3); "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = sond_database_insert_entity( database, ID_label_rel, errmsg );
        if ( rc == -1 ) ERROR_S
        else ID_new_entity_rel = rc;

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_entity_subject );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_entity_subject)" )

        rc = sqlite3_bind_int( stmt[0], 2, ID_new_entity_rel );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_new_entity_rel)" )

        rc = sqlite3_bind_int( stmt[0], 3, ID_entity_object );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_entity_object)" )

        rc = sqlite3_step( stmt[0] );
        if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step [0]" )
    }
    else //MariaDB
    {

    }

    return 0;
}


gint
sond_database_insert_property( gpointer database, gint ID_entity_subject, gint ID_label_property,
        const gchar* value_property, gchar** errmsg )
{
    gint rc = 0;
    gint ID_new_entity_property = 0;

    const gchar* sql[] = {
            "INSERT INTO properties (entity_subject, entity_property, value) "
            "VALUES (?1,?2,?3); "
    };

    if ( !value_property ) return 0;

    rc = sond_database_insert_entity( database, ID_label_property, errmsg );
    if ( rc == -1 ) ERROR_S
    else ID_new_entity_property = rc;

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_entity_subject );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_entity_subject)" )

        rc = sqlite3_bind_int( stmt[0], 2, ID_new_entity_property );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_entity_subject)" )

        rc = sqlite3_bind_text( stmt[0], 3, value_property, -1, NULL );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_text (value_property)" )

        rc = sqlite3_step( stmt[0] );
        if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step [0]" )
    }
    else //MariaDB
    {

    }

    return 0;
}


gint
sond_database_get_entities_for_label( gpointer database, gint ID_label,
        GArray** arr_entities, gchar** errmsg )
{
    const gchar* sql[] = {
            "SELECT entities.ID FROM entities JOIN "
            "(WITH RECURSIVE cte_labels (ID) AS ( "
                "VALUES (@1) "
                "UNION ALL "
                "SELECT labels.ID "
                    "FROM labels JOIN cte_labels WHERE "
                    "labels.parent = cte_labels.ID "
                ") SELECT ID AS ID_CTE FROM cte_labels) "
                "ON entities.ID_label = ID_CTE; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_label );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label)" )

        *arr_entities = g_array_new( FALSE, FALSE, sizeof( gint ) );
        do
        {
            rc = sqlite3_step( stmt[0] );
            if ( rc != SQLITE_ROW && rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )
            else if ( rc == SQLITE_ROW )
            {
                gint ID_entity = 0;

                ID_entity = sqlite3_column_int( stmt[0], 0 );
                g_array_append_val( *arr_entities, ID_entity );
            }
        } while ( rc == SQLITE_ROW );
    }
    else //mysql
    {

    }

    return 0;
}


/** findet zu subject alle obejcts mit
    - ID_label_rel und
    - ID_label_object (einschließlich dessen Kinder) heraus **/
static gint
sond_database_get_object_for_subject_one_step( gpointer database, gint ID_entity_subject,
        gint ID_label_rel, gint ID_label_object, GArray** arr_objects, gchar** errmsg )
{
    gint cnt = 0;
    const gchar* sql[] = {
            "SELECT rels.entity_object FROM "
                "rels JOIN "
                "entities AS e1 JOIN "
                "entities AS e2 JOIN "
                "(WITH RECURSIVE cte_labels (ID) AS ( "
                    "VALUES (@1) "
                    "UNION ALL "
                    "SELECT labels.ID "
                        "FROM labels JOIN cte_labels WHERE "
                        "labels.parent = cte_labels.ID "
                    ") SELECT ID AS ID_CTE FROM cte_labels) "
                "WHERE "
                    "rels.entity_subject=@2 AND "
                    "rels.entity_rel=e1.ID AND "
                    "e1.ID_label=@3 AND "
                    "rels.entity_object=e2.ID AND "
                    "e2.ID_label=ID_CTE; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_label_object );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label_object)" )

        rc = sqlite3_bind_int( stmt[0], 2, ID_entity_subject );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_entity_subject)" )

        rc = sqlite3_bind_int( stmt[0], 3, ID_label_rel );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label_rel)" )

        do
        {
            rc = sqlite3_step( stmt[0] );
            if ( rc != SQLITE_ROW && rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )
            else if ( rc == SQLITE_ROW )
            {
                gint ID_entity = 0;

                ID_entity = sqlite3_column_int( stmt[0], 0 );
                g_array_append_val( *arr_objects, ID_entity );
                cnt++;
            }
        } while ( rc == SQLITE_ROW );
    }
    else //mysql
    {

    }

    return cnt;

}


/** findet zu subject alle objects mit
    - ID_label_rel und
    - ID_label_object (einschließlich dessen Kinder) heraus

    Sofern mehrere Parameter-Paare angegeben sind, werden Ketten gebildet **/
gint
sond_database_get_object_for_subject( gpointer database, gint ID_entity_subject,
        GArray** arr_objects, gchar** errmsg, ... )
{
    va_list ap;
    gint ID_label_rel = 0;
    GArray* arr_objects_tmp = NULL;

    arr_objects_tmp = g_array_new( FALSE, FALSE, sizeof( gint ) );
    g_array_append_val( arr_objects_tmp, ID_entity_subject );

    va_start( ap, errmsg );

    while ( (ID_label_rel = va_arg( ap, gint )) != -1 )
    {
        gint ID_label_object = 0;
        GArray* arr_results = NULL;

        arr_results = g_array_new( FALSE, FALSE, sizeof( gint ) );

        ID_label_object = va_arg( ap, gint );

        for ( gint i = 0; i < arr_objects_tmp->len; i++ )
        {
            gint rc = 0;
            gint ID_entity_subject_tmp = 0;

            ID_entity_subject_tmp = g_array_index( arr_objects_tmp, gint, i );

            rc = sond_database_get_object_for_subject_one_step( database,
                    ID_entity_subject_tmp, ID_label_rel, ID_label_object, &arr_results, errmsg );
            if ( rc == -1 )
            {
                g_array_unref( arr_results );
                g_array_unref( arr_objects_tmp );
                va_end( ap );
                ERROR_S
            }
        }
        g_array_unref( arr_objects_tmp );
        arr_objects_tmp = arr_results;
    }

    va_end( ap );

    *arr_objects = arr_objects_tmp;

    return 0;
}


gint
sond_database_get_entities_for_property( gpointer database,
        gint ID_label_property, const gchar* value, GArray** arr_res, gchar** errmsg )
{
    gint cnt = 0;
    const gchar* sql[] = {
            "SELECT properties.entity_subject FROM entities JOIN properties "
            "ON properties.entity_property=entities.ID "
            "AND entities.ID_label=?1 AND properties.value=?2 "
            "ORDER BY properties.entity_subject ASC; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_label_property );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label_property)" )

        rc = sqlite3_bind_text( stmt[0], 2, value, -1, NULL );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_text (value)" )

        if ( arr_res ) *arr_res = g_array_new( FALSE, FALSE, sizeof( gint ) );

        do
        {
            rc = sqlite3_step( stmt[0] );
            if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
            {
                if ( arr_res )
                {
                    g_array_unref( *arr_res );
                    *arr_res = NULL;
                }
                ERROR_ZOND_DBASE( "sqlite3_step" )
            }
            else if ( rc == SQLITE_ROW )
            {
                gint ID_entity = 0;

                ID_entity = sqlite3_column_int( stmt[0], 0 );
                if ( arr_res ) g_array_append_val( *arr_res, ID_entity );
                cnt++;
            }
        } while ( rc == SQLITE_ROW );
    }
    else //mysql
    {

    }

    return cnt;
}


static gint
sond_database_get_entities_for_no_property( gpointer database,
        gint ID_label_property, GArray** arr_res, gchar** errmsg )
{
    gint cnt = 0;
    const gchar* sql[] = {
            "SELECT properties.entity_subject FROM entities JOIN properties "
            "ON properties.entity_property=entities.ID "
            "AND entities.ID_label IS NOT ?1; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_label_property );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label_property)" )

        if ( arr_res ) *arr_res = g_array_new( FALSE, FALSE, sizeof( gint ) );

        do
        {
            rc = sqlite3_step( stmt[0] );
            if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
            {
                if ( arr_res )
                {
                    g_array_unref( *arr_res );
                    *arr_res = NULL;
                }
                ERROR_ZOND_DBASE( "sqlite3_step" )
            }
            else if ( rc == SQLITE_ROW )
            {
                gint ID_entity = 0;

                ID_entity = sqlite3_column_int( stmt[0], 0 );
                if ( arr_res ) g_array_append_val( *arr_res, ID_entity );
                cnt++;
            }
        } while ( rc == SQLITE_ROW );
    }
    else //mysql
    {

    }

    return cnt;
}


//Value einer property darf nicht NULL sein, macht ja auch keinen Sinn
//wenn value == NULL übergeben wird dann soll das heißen: entity must not eine solche property haben!
gint
sond_database_get_entities_for_properties_and( gpointer database,
        GArray** arr_res, gchar** errmsg, ... )
{
    va_list arg_pointer;
    GArray* arr_first = NULL;
    gint ID_label = 0;
    GPtrArray* arr_arrays = NULL;

    arr_arrays = g_ptr_array_new_full( 0, (GDestroyNotify) g_ptr_array_unref );

    va_start( arg_pointer, errmsg );

    while ( (ID_label = va_arg( arg_pointer, gint )) != -1 )
    {
        const gchar* value = NULL;
        gint rc = 0;
        GArray* arr_prop = NULL;

        value = va_arg( arg_pointer, const gchar* );

        if ( value ) rc = sond_database_get_entities_for_property( database, ID_label, value,
                &arr_prop, errmsg );
        else rc = sond_database_get_entities_for_no_property( database, ID_label,
                &arr_prop, errmsg );
        if ( rc == -1 )
        {
            va_end( arg_pointer );
            g_ptr_array_unref( arr_arrays );
            ERROR_S
        }

        g_ptr_array_add( arr_arrays, arr_prop );
    }

    va_end( arg_pointer );

    if ( arr_arrays->len == 0 ) return 0;

    arr_first = g_ptr_array_index( arr_arrays, 0 );
    for ( gint i = 0; i < arr_first->len; i++ )
    {
        gint ID_first_array = 0;

        ID_first_array = g_array_index( arr_first, gint, i );

        for ( gint a = 1; a < arr_arrays->len; a++ )
        {
            GArray* arr_n = NULL;
            gboolean found = FALSE;

            arr_n = g_ptr_array_index( arr_arrays, a );

            for ( gint u = 0; u < arr_n->len; u++ )
            {
                gint ID = 0;

                ID = g_array_index( arr_n, gint, u );
                if ( ID == ID_first_array )
                {
                    found = TRUE;
                    break;
                }
            }

            if ( found == FALSE )
            {
                g_array_remove_index( arr_first, i );
                break;
            }
        }
    }

    if ( arr_res )
    {
        *arr_res = g_array_new( FALSE, FALSE, sizeof ( gint ) );
        *arr_res = g_array_ref( arr_first );
    }

    g_ptr_array_unref( arr_arrays ); //arr_first wird nicht gelöscht, da ref == 2 (hoffe ich)

    return 0;
}


gint
sond_database_get_label_for_ID_label( gpointer database, gint ID_label, gchar** label, gchar** errmsg )
{
    const gchar* sql[] = {
            "SELECT label FROM labels WHERE ID=@1; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_label );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label)" )

        rc = sqlite3_step( stmt[0] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )
        else if ( rc == SQLITE_DONE ) ERROR_S_MESSAGE( "ID_label existiert nicht" )

        if ( label ) *label = g_strdup( (const gchar*) sqlite3_column_text( stmt[0], 0 ) );
    }
    else //mysql
    {
        gint rc = 0;
        MYSQL* con = NULL;
        MYSQL_RES* mysql_res = NULL;
        MYSQL_ROW row = NULL;
        gchar* sql_mariadb = NULL;

        con = (MYSQL*) database;

        sql_mariadb = g_strdup_printf( "SET @1=%i; ", ID_label );
        sql_mariadb = add_string( sql_mariadb, g_strdup( sql[0] ) );

        rc = mysql_query( con, sql_mariadb );
        g_free( sql_mariadb );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf ", __func__, ":\n",
                    mysql_error( con ), NULL );
            return -1;
        }

        mysql_res = mysql_store_result( con );
        if ( !mysql_res )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf mysql_store_result:\n",
                    mysql_error( con ), NULL );

            return -1;
        }

        row = mysql_fetch_row( mysql_res );
        if ( !row )
        {
            mysql_free_result( mysql_res );
            ERROR_S_MESSAGE( "ID_label existiert nicht" )
        }

        if ( label ) *label = g_strdup( row[0] );

        mysql_free_result( mysql_res );
    }

    return 0;
}


gint
sond_database_get_property_value( gpointer database, gint ID_property, gchar** value, gchar** errmsg )
{
    const gchar* sql[] = {
            "SELECT value FROM properties WHERE entity_property=@1; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_property );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_property)" )

        rc = sqlite3_step( stmt[0] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )
        else if ( rc == SQLITE_DONE ) ERROR_S_MESSAGE( "ID_property existiert nicht" )

        if ( value ) *value = g_strdup( (const gchar*) sqlite3_column_text( stmt[0], 0 ) );
    }
    else //mysql
    {
        gint rc = 0;
        MYSQL* con = NULL;
        MYSQL_RES* mysql_res = NULL;
        MYSQL_ROW row = NULL;
        gchar* sql_mariadb = NULL;

        con = (MYSQL*) database;

        sql_mariadb = g_strdup_printf( "SET @1=%i; ", ID_property );
        sql_mariadb = add_string( sql_mariadb, g_strdup( sql[0] ) );

        rc = mysql_query( con, sql_mariadb );
        g_free( sql_mariadb );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf ", __func__, ":\n",
                    mysql_error( con ), NULL );
            return -1;
        }

        mysql_res = mysql_store_result( con );
        if ( !mysql_res )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf mysql_store_result:\n",
                    mysql_error( con ), NULL );

            return -1;
        }

        row = mysql_fetch_row( mysql_res );
        if ( !row )
        {
            mysql_free_result( mysql_res );
            ERROR_S_MESSAGE( "ID_property existiert nicht" )
        }

        if ( value ) *value = g_strdup( row[0] );

        mysql_free_result( mysql_res );
    }

    return 0;
}


gint
sond_database_get_properties( gpointer database, gint ID_entity, GArray** arr_properties, gchar** errmsg )
{
    const gchar* sql[] = {
            "SELECT entity_property FROM properties WHERE entity_subject=@1; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_entity );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_entity)" )

        *arr_properties = g_array_new( FALSE, FALSE, sizeof( gint ) );
        do
        {
            gint ID_property = 0;

            rc = sqlite3_step( stmt[0] );
            if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
            {
                g_array_unref( *arr_properties );
                ERROR_ZOND_DBASE( "sqlite3_step" )
            }
            else if ( rc == SQLITE_DONE ) break;

            ID_property = sqlite3_column_int( stmt[0], 0 );
            g_array_append_val( *arr_properties, ID_property );
        } while ( rc == SQLITE_ROW );
    }
    else //mysql
    {
        gint rc = 0;
        MYSQL* con = NULL;
        MYSQL_RES* mysql_res = NULL;
        MYSQL_ROW row = NULL;
        gchar* sql_mariadb = NULL;

        con = (MYSQL*) database;

        sql_mariadb = g_strdup_printf( "SET @1=%i; ", ID_entity );
        sql_mariadb = add_string( sql_mariadb, g_strdup( sql[0] ) );

        rc = mysql_query( con, sql_mariadb );
        g_free( sql_mariadb );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf ", __func__, ":\n",
                    "Bei Aufruf mysql_query:\n", mysql_error( con ), NULL );
            return -1;
        }

        mysql_res = mysql_store_result( con );
        if ( !mysql_res )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf ", __func__, ":\n",
                    "Bei Aufruf mysql_store_result:\n",
                    mysql_error( con ), NULL );

            return -1;
        }

        while ( (row = mysql_fetch_row( mysql_res )) )
        {
            gint ID_property = 0;

            ID_property = atoi( row[0] );
            g_array_append_val( *arr_properties, ID_property );
        }

        mysql_free_result( mysql_res );
    }

    return 0;
}


gint
sond_database_get_first_property_value_for_subject( gpointer database,
        gint ID_entity_subject, gint ID_label_property, gchar** value, gchar** errmsg )
{
    const gchar* sql[] = {
            "SELECT properties.value FROM properties JOIN entities  "
            "ON properties.entity_property=entities.ID "
            "AND properties.entity_subject=@1 "
            "AND entities.ID_label=@2; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_entity_subject );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_entity_subject)" )

        rc = sqlite3_bind_int( stmt[0], 2, ID_label_property );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label_property)" )

        rc = sqlite3_step( stmt[0] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )
        else if ( rc == SQLITE_DONE ) return 1;

        *value = g_strdup( (const gchar*) sqlite3_column_text( stmt[0], 0 ) );
    }
    else //mysql
    {
        gint rc = 0;
        MYSQL* con = NULL;
        MYSQL_RES* mysql_res = NULL;
        MYSQL_ROW row = NULL;
        gchar* sql_mariadb = NULL;

        con = (MYSQL*) database;

        sql_mariadb = g_strdup_printf( "SET @1=%i; ", ID_entity_subject );
        sql_mariadb = add_string( sql_mariadb, g_strdup_printf( "SET @2=%i; ", ID_label_property ) );
        sql_mariadb = add_string( sql_mariadb, g_strdup( sql[0] ) );

        rc = mysql_query( con, sql_mariadb );
        g_free( sql_mariadb );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf ", __func__, ":\n",
                    mysql_error( con ), NULL );
            return -1;
        }

        mysql_res = mysql_store_result( con );
        if ( !mysql_res )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf mysql_store_result:\n",
                    mysql_error( con ), NULL );

            return -1;
        }

        row = mysql_fetch_row( mysql_res );
        if ( !row )
        {
            mysql_free_result( mysql_res );
            return 1;
        }

        *value = g_strdup( row[0] );

        mysql_free_result( mysql_res );
    }

    return 0;
}


gint
sond_database_get_subject_and_first_property_value_for_labels( gpointer database,
        gint ID_label_subject, gint ID_label_property, GArray** arr_IDs, GPtrArray** arr_values, gchar** errmsg )
{
    const gchar* sql[] = {
            "SELECT e1.ID, properties.value "
            "FROM properties JOIN entities AS e1 JOIN entities AS e2 WHERE "
            "e1.ID_label=@1 AND "
            "e1.ID=properties.entity_subject AND "
            "properties.entity_property=e2.ID AND "
            "e2.ID_label=@2 "
            "ORDER BY properties.value ASC; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_label_subject );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label_subject)" )

        rc = sqlite3_bind_int( stmt[0], 2, ID_label_property );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label_property)" )

        *arr_IDs = g_array_new( FALSE, FALSE, sizeof( gint ) );
        *arr_values = g_ptr_array_new_full( 0, g_free );

        do
        {
            rc = sqlite3_step( stmt[0] );
            if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
            {
                g_array_unref( *arr_IDs );
                g_ptr_array_unref( *arr_values );
                ERROR_ZOND_DBASE( "sqlite3_step" )
            }

            if ( rc == SQLITE_ROW )
            {
                gchar* value = NULL;
                gint ID = 0;

                ID = sqlite3_column_int( stmt[0], 0 );
                g_array_append_val( *arr_IDs, ID );

                value = g_strdup( (const gchar*) sqlite3_column_text( stmt[0], 1 ) );
                g_ptr_array_add( *arr_values, value );
            }
        } while ( rc == SQLITE_ROW );
    }
    else //mysql
    {
        gint rc = 0;
        MYSQL* con = NULL;
        MYSQL_RES* mysql_res = NULL;
        MYSQL_ROW row = NULL;
        gchar* sql_mariadb = NULL;

        con = (MYSQL*) database;

    }

    return 0;
}


gint
sond_database_get_outgoing_rels( gpointer database, gint ID_entity, GArray** arr_o_rels, gchar** errmsg )
{
    const gchar* sql[] = {
            "SELECT entity_rel FROM rels WHERE entity_subject=@1; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_entity );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_entity)" )

        *arr_o_rels = g_array_new( FALSE, FALSE, sizeof( gint ) );
        do
        {
            gint ID_entity_rel = 0;

            rc = sqlite3_step( stmt[0] );
            if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
            {
                g_array_unref( *arr_o_rels );
                ERROR_ZOND_DBASE( "sqlite3_step" )
            }
            else if ( rc == SQLITE_DONE ) break;

            ID_entity_rel = sqlite3_column_int( stmt[0], 0 );
            g_array_append_val( *arr_o_rels, ID_entity_rel );
        } while ( rc == SQLITE_ROW );
    }
    else //mysql
    {
        gint rc = 0;
        MYSQL* con = NULL;
        MYSQL_RES* mysql_res = NULL;
        MYSQL_ROW row = NULL;
        gchar* sql_mariadb = NULL;

        con = (MYSQL*) database;

        sql_mariadb = g_strdup_printf( "SET @1=%i; ", ID_entity );
        sql_mariadb = add_string( sql_mariadb, g_strdup( sql[0] ) );

        rc = mysql_query( con, sql_mariadb );
        g_free( sql_mariadb );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf ", __func__, ":\n",
                    "Bei Aufruf mysql_query:\n", mysql_error( con ), NULL );
            return -1;
        }

        mysql_res = mysql_store_result( con );
        if ( !mysql_res )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf ", __func__, ":\n"
                    "Bei Aufruf mysql_store_result:\n",
                    mysql_error( con ), NULL );

            return -1;
        }

        *arr_o_rels = g_array_new( FALSE, FALSE, sizeof( gint ) );
        while ( (row = mysql_fetch_row( mysql_res )) )
        {
            gint ID_entity_rel = 0;

            ID_entity_rel = atoi( row[0] );
            g_array_append_val( *arr_o_rels, ID_entity_rel );
        }

        mysql_free_result( mysql_res );
    }

    return 0;
}


gint
sond_database_get_object_from_rel( gpointer database, gint ID_entity_rel, gchar** errmsg )
{
    gint ID_entity_object = 0;
    const gchar* sql[] = {
            "SELECT entity_object FROM rels WHERE entity_rel=@1; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_entity_rel );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_entity_rel)" )

        rc = sqlite3_step( stmt[0] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )
        else if ( rc == SQLITE_DONE ) ERROR_S_MESSAGE( "Rel nicht vorhanden" );

        ID_entity_object = sqlite3_column_int( stmt[0], 0 );
    }
    else //mysql
    {

    }

    return ID_entity_object;
}


/*
gint
dbase_full_get_label_text_for_entity( DBaseFull* dbase_full, gint ID_entity, gchar** label, gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( dbase_full->stmts[44] );

    rc = sqlite3_bind_int( dbase_full->stmts[44], 1, ID_entity );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (ID_entity)" )

    rc = sqlite3_step( dbase_full->stmts[44] );
    if ( rc != SQLITE_ROW ) ERROR_DBASE_FULL( "sqlite3_step" )

    if ( label ) *label =
            g_strdup( (const gchar*) sqlite3_column_text( dbase_full->stmts[44], 0 ) );

    return 0;
}


static void
dbase_full_clear_property( gpointer data )
{
    Property* property = (Property*) data;

    g_free( property->label );
    g_free( property->value );
    g_array_unref( property->arr_properties );

    return;
}


gint
dbase_full_get_properties( DBaseFull* dbase_full, gint ID_entity,
        GArray** arr_properties, gchar** errmsg )
{
    gint rc = 0;

    if ( !arr_properties ) return 0;

    sqlite3_reset( dbase_full->stmts[45] );

    rc = sqlite3_bind_int( dbase_full->stmts[45], 1, ID_entity );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (ID_entity)" )

    *arr_properties = g_array_new( FALSE, FALSE, sizeof( Property ) );
    g_array_set_clear_func( *arr_properties, dbase_full_clear_property );

    do
    {
        Property property = { 0 };

        rc = sqlite3_step( dbase_full->stmts[45] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
        {
            g_array_unref( *arr_properties );
            ERROR_DBASE_FULL( "sqlite3_step" )
        }
        else if ( rc == SQLITE_ROW )
        {
            gint ret = 0;

            property.ID = sqlite3_column_int( dbase_full->stmts[45], 0 );
            property.label = g_strdup( (const gchar*) sqlite3_column_text( dbase_full->stmts[45], 1 ) );
            property.value = g_strdup( (const gchar*) sqlite3_column_text( dbase_full->stmts[45], 2 ) );

            ret = dbase_full_get_properties( dbase_full, property.ID, &(property.arr_properties), errmsg );
            if ( ret )
            {
                g_array_unref( *arr_properties );
                ERROR_PAO( "dbase_full_get_properties" );
            }

            g_array_append_val( *arr_properties, property );
        }
    } while ( rc == SQLITE_ROW );

    return 0;
}


gint
dbase_full_get_outgoing_edges( DBaseFull* dbase_full, gint ID_entity, GArray** arr_edges,
        gchar** errmsg )
{
    gint rc = 0;

    if ( !arr_edges ) return 0;

    sqlite3_reset( dbase_full->stmts[46] );

    rc = sqlite3_bind_int( dbase_full->stmts[46], 1, ID_entity );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (ID_entity)" )

    *arr_edges = g_array_new( FALSE, FALSE, sizeof( Edge ) );

    do
    {
        rc = sqlite3_step( dbase_full->stmts[46] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
        {
            g_array_unref( *arr_edges );
            ERROR_DBASE_FULL( "sqlite3_step" )
        }
        else if ( rc == SQLITE_ROW )
        {
            Edge edge = { 0 };

            edge.ID = sqlite3_column_int( dbase_full->stmts[46], 0 );
            edge.subject = sqlite3_column_int( dbase_full->stmts[46], 1 );
            edge.object = sqlite3_column_int( dbase_full->stmts[46], 2 );

            g_array_append_val( *arr_edges, edge );
        }
    } while ( rc == SQLITE_ROW );

    return 0;
}


gint
dbase_full_get_label_text( DBaseFull* dbase_full, gint ID_label, gchar** label_text, gchar** errmsg )
{
    gint rc = 0;

    if ( !label_text ) return 0;

    sqlite3_reset( dbase_full->stmts[47] );

    rc = sqlite3_bind_int( dbase_full->stmts[47], 1, ID_label );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (ID_label)" )

    rc = sqlite3_step( dbase_full->stmts[47] );
    if ( rc != SQLITE_ROW ) ERROR_DBASE_FULL( "sqlite3_step" )

    *label_text = g_strdup( (const gchar*) sqlite3_column_text( dbase_full->stmts[47], 0 ) );

    return 0;
}


//Array von Kindern von label; nur ID (gint)
gint
dbase_full_get_array_children_label( DBaseFull* dbase_full, gint label,
        GArray** arr_children, gchar** errmsg )
{
    gint rc = 0;

    if ( !arr_children ) return 0;

    sqlite3_reset( dbase_full->stmts[48] );

    rc = sqlite3_bind_int( dbase_full->stmts[48], 1, label );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (label)" )

    *arr_children = g_array_new( FALSE, FALSE, sizeof( gint ) );

    do
    {
        gint child = 0;

        rc = sqlite3_step( dbase_full->stmts[48] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
        {
            g_array_unref( *arr_children );
            ERROR_DBASE_FULL( "sqlite3_step" )
        }
        else if ( rc == SQLITE_ROW )
        {
            child = sqlite3_column_int( dbase_full->stmts[48], 0 );
            g_array_append_val( *arr_children, child );
        }
    } while ( rc == SQLITE_ROW );

    return 0;
}


//Array von nodes mit dem label "nomen" oder eines Kindes von "nomen"; nur ID (gint)
gint
dbase_full_get_array_nodes( DBaseFull* dbase_full, gint nomen, GArray** arr_nodes,
        gchar** errmsg )
{
    gint rc = 0;

    if ( !arr_nodes ) return 0;

    sqlite3_reset( dbase_full->stmts[49] );

    rc = sqlite3_bind_int( dbase_full->stmts[49], 1, nomen );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (nomen)" )

    *arr_nodes = g_array_new( FALSE, FALSE, sizeof( gint ) );

    do
    {
        gint ID_entity = 0;

        rc = sqlite3_step( dbase_full->stmts[49] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
        {
            g_array_unref( *arr_nodes );
            ERROR_DBASE_FULL( "sqlite3_step" )
        }
        else if ( rc == SQLITE_ROW )
        {
            ID_entity = sqlite3_column_int( dbase_full->stmts[49], 0 );
            g_array_append_val( *arr_nodes, ID_entity );
        }
    } while ( rc == SQLITE_ROW );

    return 0;
}


gint
dbase_full_get_incoming_edges( DBaseFull* dbase_full, gint ID_entity, GArray** arr_edges,
        gchar** errmsg )
{
    gint rc = 0;

    if ( !arr_edges ) return 0;

    sqlite3_reset( dbase_full->stmts[50] );

    rc = sqlite3_bind_int( dbase_full->stmts[50], 1, ID_entity );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (ID_entity)" )

    *arr_edges = g_array_new( FALSE, FALSE, sizeof( Edge ) );

    do
    {
        rc = sqlite3_step( dbase_full->stmts[50] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
        {
            g_array_unref( *arr_edges );
            ERROR_DBASE_FULL( "sqlite3_step" )
        }
        else if ( rc == SQLITE_ROW )
        {
            Edge edge = { 0 };

            edge.ID = sqlite3_column_int( dbase_full->stmts[50], 0 );
            edge.subject = sqlite3_column_int( dbase_full->stmts[50], 1 );
            edge.object = sqlite3_column_int( dbase_full->stmts[50], 2 );

            g_array_append_val( *arr_edges, edge );
        }
    } while ( rc == SQLITE_ROW );

    return 0;
}


gint
dbase_full_get_adm_entities( DBaseFull* dbase_full, gint label, GArray** arr_adm_entities, gchar** errmsg )
{
    gint rc = 0;

    if ( !arr_adm_entities ) return 0;

    sqlite3_reset( dbase_full->stmts[51] );

    rc = sqlite3_bind_int( dbase_full->stmts[51], 1, label );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (label)" )

    *arr_adm_entities = g_array_new( FALSE, FALSE, sizeof( gint ) );

    do
    {
        rc = sqlite3_step( dbase_full->stmts[51] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
        {
            g_array_unref( *arr_adm_entities );
            ERROR_DBASE_FULL( "sqlite3_step" )
        }
        else if ( rc == SQLITE_ROW )
        {
            gint adm_entity = 0;

            adm_entity = sqlite3_column_int( dbase_full->stmts[51], 0 );
            g_array_append_val( *arr_adm_entities, adm_entity );
        }
    } while ( rc == SQLITE_ROW );

    return 0;
}


gint
dbase_full_get_label_for_entity( DBaseFull* dbase_full, gint ID_entity, gchar** errmsg )
{
    gint rc = 0;
    gint label = 0;

    sqlite3_reset( dbase_full->stmts[52] );

    rc = sqlite3_bind_int( dbase_full->stmts[52], 1, ID_entity );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (ID_entity)" )

    rc = sqlite3_step( dbase_full->stmts[52] );
    if ( rc != SQLITE_ROW ) ERROR_DBASE_FULL( "sqlite3_step" )

    label = sqlite3_column_int( dbase_full->stmts[52], 0 );

    return label;
}


gint
dbase_full_insert_edge( DBaseFull* dbase_full, gint entity, gint subject,
        gint object, gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( dbase_full->stmts[53] );

    rc = sqlite3_bind_int( dbase_full->stmts[53], 1, entity );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (entity)" )

    rc = sqlite3_bind_int( dbase_full->stmts[53], 2, subject );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (subject)" )

    rc = sqlite3_bind_int( dbase_full->stmts[53], 3, object );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (value)" )

    rc = sqlite3_step( dbase_full->stmts[53] );
    if ( rc != SQLITE_DONE ) ERROR_DBASE_FULL( "sqlite3_step" )

    return 0;
}


gint
dbase_full_get_entity( DBaseFull* dbase_full, gint ID_entity, Entity** entity, gchar** errmsg )
{
    gchar* label = NULL;
    GArray* arr_properties = NULL;
    gint rc = 0;

    if ( !entity ) return 0;

    rc = dbase_full_get_label_text_for_entity( dbase_full, ID_entity, &label, errmsg );
    if ( rc ) ERROR_PAO( "dbase_full_get_label_text_for_entity" )

    rc = dbase_full_get_properties( dbase_full, ID_entity, &arr_properties, errmsg );
    if ( rc )
    {
        g_free( label );
        ERROR_PAO( "dbase_full_get_properties" )
    }

    *entity = g_malloc0( sizeof( Entity ) );
    (*entity)->ID = ID_entity;
    (*entity)->label = label;
    (*entity)->arr_properties = arr_properties;

    return 0;
}



//  get_label_entity (44)
            "SELECT labels.label FROM labels JOIN entities "
                "ON entities.label = labels.ID WHERE entities.ID = ?1;",

//  get_properties (45)
            "SELECT ID_entity, label_text, properties.value FROM "
                "(SELECT entities.ID AS ID_entity,labels.label AS label_text "
                    "FROM entities JOIN labels ON entities.label = labels.ID) "
                    "JOIN properties ON ID_entity = properties.entity "
                    "WHERE properties.subject = ?1; ",

//  get_outgoint_edges (46)
            "SELECT ID_subject, ID_edge, labels.label, ID_object "
                "FROM labels JOIN "
                "(SELECT edges.subject AS ID_subject, edges.entity AS ID_edge, entities.label AS ID_label_edge, edges.object AS ID_object "
                "FROM edges JOIN entities ON edges.entity = entities.ID WHERE edges.subject = ?1) "
                "ON ID_label_edge = labels.ID; ",

//  get_label_text (47)
            "SELECT labels.label FROM labels WHERE labels.ID = ?1; ",

//  get_array_children (48)
            "SELECT labels.ID FROM labels WHERE labels.parent = ?1; ",

//  get_array_nodes (49)
            "SELECT entities.ID FROM entities JOIN "
            "(WITH RECURSIVE cte_labels (ID) AS ( "
                "VALUES (?1) "
                "UNION ALL "
                "SELECT labels.ID "
                    "FROM labels JOIN cte_labels WHERE "
                    "labels.parent = cte_labels.ID "
                ") SELECT ID AS ID_CTE FROM cte_labels) "
                "ON entities.label = ID_CTE; ",

//  get_incoming_edges (50)
            "SELECT edges.ID, edges.subject, edges.object"
                "FROM edges JOIN entities ON edges.entity = entities.ID WHERE edges.object = ?1; "

//  get_adm_entities (51)
            "SELECT adm_entities.rentity FROM adm_entities JOIN "
            "(WITH RECURSIVE cte_labels (ID) AS ( "
                "VALUES (?1) "
                "UNION ALL "
                "SELECT labels.ID "
                    "FROM labels JOIN cte_labels WHERE "
                    "labels.parent = cte_labels.ID "
                ") SELECT ID AS ID_CTE FROM cte_labels) "
                "ON adm_entities.entity = ID_CTE; ",

//  get_label_for_entity (52)
            "SELECT entities.label FROM entities WHERE entities.ID = ?1; ",

//  insert_edge (53)
            "INSERT INTO edges (entity,subject,object) VALUES (?1,?2,?3); ",


gint
sojus_database_update_property( MYSQL* con, gint ID_property, const gchar* value, gchar** errmsg )
{
    gchar* sql = NULL;
    gint rc = 0;

    sql = g_strdup_printf( "UPDATE properties SET value=%s WHERE entity=%i;",
            value, ID_property );

    rc = mysql_query( con, sql );
    g_free( sql );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf mysql_query:\n",
                mysql_error( con ), NULL );

        return -1;
    }

    return 0;
}


gint
sojus_database_delete_property( MYSQL* con, gint ID_property, gchar** errmsg )
{
    gchar* sql = NULL;
    gint rc = 0;
    MYSQL_RES* mysql_res = NULL;
    MYSQL_ROW row = NULL;

    sql = g_strdup_printf( "DELETE FROM properties WHERE entity=%i; "
            "DELETE vom rels WHERE object=%i;", ID_property, ID_property );

    rc = mysql_query( con, sql );
    g_free( sql );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf mysql_query:\n",
                mysql_error( con ), NULL );

        return -1;
    }

    sql = g_strdup_printf( "SELECT object FROM rels WHERE subject=%i;", ID_property );

    rc = mysql_query( con, sql );
    g_free( sql );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf mysql_query:\n",
                mysql_error( con ), NULL );

        return -1;
    }

    mysql_res = mysql_store_result( con );
    if ( !mysql_res )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf mysql_store_result:\n",
                mysql_error( con ), NULL );

        return -1;
    }

    while ( (row = mysql_fetch_row( mysql_res )) )
    {
        gint rc = 0;

        rc = sojus_database_delete_property( con, atoi( row[0] ), errmsg );
        if ( rc ) ERROR_SOND( "sojus_database_detete_property" )
    }

    mysql_free_result( mysql_res );

    return 0;
}

*/




