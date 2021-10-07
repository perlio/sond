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

typedef struct _Entity
{
    gint ID;
    gint label;
} Entity;


typedef struct _Property
{
    Entity entity;
    union
    {
        gchar* string;
        gint integer;
        float real;
        time_t time;
    } value;

} Property;

typedef struct _Node
{
    Entity entity;
    GArray* outgoing_rels;
} Node;

typedef struct _Rel
{
    Entity rel;
    union
    {
        Node object;
        gchar* string;
        gint integer;
        float real;
        time_t time;
    } object;
    GArray* outgoing_rel;
} Rel;



const gchar*
sond_database_sql_create_database( void )
{
    return "DROP TABLE IF EXISTS labels; "
            "DROP TABLE IF EXISTS entities; "
            "DROP TABLE IF EXISTS rels; "
            "DROP TABLE IF EXISTS properties; "

            "CREATE TABLE IF NOT EXISTS labels ( "
                "ID INTEGER PRIMARY KEY, "
                "label TEXT NOT NULL, "
                "parent INTEGER NOT NULL, "
                "FOREIGN KEY (parent) REFERENCES labels (ID) "
            "); "

            "CREATE TABLE IF NOT EXISTS adm_rels ( "
                "subject INTEGER NOT NULL, "
                "rel INTEGER NOT NULL, "
                "object INTEGER NOT NULL, "
                "FOREIGN KEY (subject) REFERENCES labels (ID), "
                "FOREIGN KEY (rel) REFERENCES labels (ID), "
                "FOREIGN KEY (object) REFERENCES labels (ID) "
            "); "

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

            "CREATE TABLE IF NOT EXISTS properties ( "
                "entity INTEGER NOT NULL, "
                "value TEXT, "
                "FOREIGN KEY (entity) REFERENCES entities (ID) "
            "); ";
}


const gchar*
sond_database_sql_insert_labels( void )
{
    return "INSERT INTO labels (ID, label, parent) VALUES "
                "(0, 'root', 0), "
                "(1, 'Knoten', 0), "
                "(2, 'Prädikat', 0), "
                "(3, 'Wert', 0), "

                //100 - 999: Knoten, allgemein
                "(100, 'Subjekt', 1), "

                "(110, 'Subjekt des öffentlichen Rechts', 105), "

                "(115, 'Gebietskörperschaft', 110), "
                "(117, 'Staat', 115), "
                "(119, 'Bundesland', 115), "
                "(121, 'Kreis', 115), "
                "(123, 'Gemeinde', 115), "

                "(130, 'sonstiges Subjekt des öffentlichen Rechts', 110), "

                "(135, 'öffr jur Person', 130), "
                "(137, 'Körperschaft', 120), "
                "(139, 'Universität', 137), "
                "(141, 'Kammer', 137), "
                "(143, 'Rechtsanwaltskammer', 141), "
                "(145, 'Anstalt', 135), "

                "(160, 'Behörde', 130), "
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
                "(320, 'juristische Person', 300), "
                "(325, 'GmbH', 320), "
                "(330, 'UG', 320), "
                "(335, 'AG', 320), "
                "(340, 'Verein', 320), "

                "(350, 'Personenmehrheit', 300), "
                "(352, 'Eheleute', 350), "
                "(355, 'GbR', 350), "
                "(360, 'OHG', 350), "
                "(365, 'KG', 350), "
                "(370, 'Partnerschaft', 350), "

                "(400, 'Straße', 1), "

                "(405, 'Telefonnummer', 1), "
                "(410, 'Vorwahl', 405), "
                "(412, 'Ländervorwahl', 410), "
                "(413, 'Teilnetzvorwahl', 410), "
                "(414, 'Ortsvorwahl', 413), "
                "(416, 'Mobilfunknetzvorwahl', 413), "
                "(420, 'Teilnehmernummer', 405), "
                "(430, 'Stammrufnummer', 405), "

                "(430, 'Homepage', 1), "
                "(435, 'E-Mail-Adresse', 1), "

                "(440, 'Kontoverbindung', 1), "

                "(450, 'Sitz', 1), "
                "(452, 'Geschäftssitz', 450), "
                "(454, 'Kanzleisitz', 452), "
                "(456, 'Wohnsitz', 450), "
                "(460, 'Dienstsitz', 450), "
                "(462, 'erster Dienstsitz', 460), "
                "(464, 'weiterer Dienstsitz', 460), "

                // 1000-5000: Knoten zond
                "(500, 'Verfahren', 1), "

                "(600, 'Konvolut', 1), "
                "(610, 'Aktenbestandteil', 600),"
                "(620, 'Aktenteil', 610), " //z.B. Hauptakte, BMO o.ä.
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


                "(1160, 'IMSI', 1), "
                "(1170, 'IMEI', 1), "

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
                "(10100, '_Name_', 2), "
                "(11010, '_Vorname_', 2), "
                "(11020, '_Titel_', 2), "

                "(11030, '_Hausnr_', 2), "
                "(11040, '_Adresszusatz_', 2), "

                "(11050, '_Anrede_', 2), "
                "(11035, '_Hinweis auf Sitz_', 2), "
                "(11080, '_Durchwahl_', 2), "
                "(11090, '_Faxdurchwahl_', 2), "

                //qualifier allgemein
                "(12000, '_von_', 2), "
                "(12010, '_bis_', 2), "
                "(12020, '_Bemerkung_', 2), "

                //15000-19999: Prädikate zond
                "(15010, '_hat node_id_', 2), " //nur property
                "(15020, '_hat Fundstelle_', 2), " //nur edge

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
                "(20025, '_hat Kürzel_', 2), "
                "(20030, '_hat als Aktenbeteiligten', 2), "
                "(20031, '_ist Aktenbeteiligtenart_', 2), "
                "(20032, '_hat als Betreff_', 2), "


                //Wert
                "(100001, 'string', 3), "
                "(100002, 'int', 3), "
                "(100003, 'real', 3), "
                "(100004, 'time', 3) "
                "; ";
}


const gchar*
sond_database_sql_insert_adm_rels( void )
{
    return "INSERT INTO adm_rels (subject,rel,object) VALUES "

            "(130, 10010, 460), " //sonst Subjekt des öff Rechts (keine Gebietskörperschaften) _hat_ Dienstsitz
            "(300, 10010, 452), " //Subjekt des Privatrechts _hat_ Geschäftssitz
            "(310, 10010, 456), " //natürliche Person _hat_ Wohnsitz

            "(400, 10000, 162), " //Straße _gehört zu_ Gemeinde
            "(402, 10000, 400), " //Adresse _gehört zu_ Straße

            "(413, 10000, 412), " //Teilnetzvorwahl _gehört zu_ Ländervorahl
            "(420, 10000, 413), " //Teilnehmernummer _gehört zu_ Teilnetzvorwahl

            "(100, 10005, 100004), " //Subjekt _von_ (time) (Geburtstag bei nat Person, sonst Gründungsdatum etc.)
            "(100, 10006, 100004), " //Subjekt _bis_ (time) (Todestag/Auflösungsdatum)
            "(100, 10010, 100001), " //Subjekt _heißt_ (string)
            "(100, 10040, 402), " //Subjekt _ist ansässig_ Adresse
            "(115, 10011, 100001), " //natürliche Person _hat Vornamen_ (string)
            "(115, 10012, 100001), " //natürliche Person _hat Titel_ (string)
            "(115, 10042, 402), " //natürliche Person _hat Kanzleisitz_ Adresse
            "(130, 10042, 402), " //priv jur Person _hat Kanzleisitz_ Adresse
            "(170, 10042, 402), " //Personenmehrheit _hat Kanzleisitz_ Adresse

            "(400, 10010, 100001), " //Straße _heißt_ (string)
            "(402, 10045, 100001), " //Adresse _hat Hausnummer_ (string)
            "(402, 10046, 100001), " //Adresse _hat Zusatz (1. Zeile)_ (string)

            "(405, 10030, 100001), " //Telefonnummer _ist_ (string)

            "(5000, 10005, 100004), " //Akte _von_ (time) (Anlage der Akte bzw.Reaktivierung
            "(5000, 10006, 100004), " //Akte _bis_ (time) - Ablage bzw. erneute Ablage
            "(5000, 10010, 100001), " //Akte _heißt_ (string)
            "(5000, 10020, 5005), " //Akte _gehört zu_ Sachbearbeiter
            "(5000, 10020, 5010), " //Akte _gehört zu_ Sachgebiet
            "(5000, 20000, 100002), " //Akten _hat Registernummer_ (int)
            "(5000, 20001, 100002), " //Akte _hat Registerjahr_ (int)
            "(5000, 20030, 100), " //Akte _hat als Beteiligten_ Subjekt
            "(5005, 10030, 115), " //Sachbearbeiter _ist_ natürliche Person
            "(5005, 20025, 100001), " //Sachbearbeiter _hat_ Kürzel (string)
            "(5040, 10005, 100004), " //Termin _von_ (time)
            "(5040, 10006, 100004), " //Termin _bis_ (time)


            "(10010, 10005, 100004), " //_heißt_ _von_ (time)
            "(10010, 10006, 100004), " //_heißt_ _bis_ (time)

            "(10040, 10005, 100004), " //_ist ansässig_ _von_ (time)
            "(10040, 10006, 100004), " //_ist ansässig_ _bis_ (time)

            "(20030, 20031, 5020), " //_hat als Beteiligten_ _ist Beteiligtenart_ Aktenbeteiligtenart
            "(20030, 20032, 100001) " //_hat als Aktenbeteiligten_ _hat Betreff_ (string)


            "; ";
}

