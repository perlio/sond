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

            "CREATE TABLE IF NOT EXISTS strings ( "
                "entity INTEGER NOT NULL, "
                "value TEXT, "
                "FOREIGN KEY (entity) REFERENCES entities (ID) "
            "); "

            "CREATE TABLE IF NOT EXISTS ints ( "
                "entity INTEGER NOT NULL, "
                "value INTEGER, "
                "FOREIGN KEY (entity) REFERENCES entities (ID) "
            "); "

            "CREATE TABLE IF NOT EXISTS reals ( "
                "entity INTEGER NOT NULL, "
                "value REAL, "
                "FOREIGN KEY (entity) REFERENCES entities (ID) "
            "); "

            "CREATE TABLE IF NOT EXISTS times ( "
                "entity INTEGER NOT NULL, "
                "value DATETIME, "
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

                "(105, 'Rechtssubjekt', 100),"
                "(110, 'Rechtsperson', 105), "
                "(115, 'natürliche Person', 110), "
                "(120, 'juristische Person', 110), "
                "(130, 'priv jur Person', 120), "
                "(131, 'GmbH', 130), "
                "(132, 'UG', 130), "
                "(133, 'AG', 130), "
                "(134, 'Verein', 130), "
                "(140, 'öffr jur Person', 120), "
                "(150, 'Körperschaft', 140), "
                "(155, 'Gebietskörperschaft', 150), "
                "(156, 'Staat', 155), "
                "(158, 'Bundesland', 155), "
                "(160, 'Kreis', 155), "
                "(162, 'Gemeinde', 155), "

                "(170, 'Personenmehrheit', 105), "
                "(175, 'Eheleute', 170), "
                "(178, 'GbR', 170), "
                "(180, 'OHG', 170), "
                "(182, 'Partnerschaft', 170), "

                "(200, 'Behörde', 100), "
                "(230, 'Gericht', 200), "
                "(235, 'Oberlandesgericht', 230), "
                "(236, 'Landgericht', 230), "
                "(237, 'Amtsgericht', 230), "
                "(238, 'Finanzgericht', 230), "
                "(240, 'Verwaltungsgericht', 230), "
                "(292, 'Oberverwaltungsgericht', 230), "
                "(294, 'Arbeitsgericht', 230), "
                "(295, 'Landesarbeitsgericht', 230), "
                "(297, 'Sozialgericht', 230), "
                "(298, 'Landessozialgericht', 230), "
                "(300, 'Staatsanwaltschaft', 200), "
                "(302, 'Generalstaatsanwaltschaft', 300), "
                "(303, 'Staatsanwaltschaft beim Landgericht', 300), "
                "(310, 'Polizeibehörde', 200), "
                "(320, 'Landeskriminalamt', 310), "
                "(325, 'Polizeipräsident', 310), "
                "(330, 'Kreispolizeibehörde', 310), "

                "(400, 'Straße', 1), "
                "(402, 'Adresse', 1), "

                "(405, 'Telefonnummer', 1), "
                "(410, 'Vorwahl', 405), "
                "(412, 'Ländervorwahl', 410), "
                "(413, 'Teilnetzvorwahl', 410), "
                "(414, 'Ortsvorwahl', 410), "
                "(416, 'Mobilfunknetzvorwahl', 410), "
                "(420, 'Teilnehmernummer', 405), "

                "(430, 'Homepage', 1), "
                "(435, 'E-Mail-Adresse', 1), "

                "(440, 'Kontoverbindung', 1), "


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
                "(10005, '_von_', 2), "
                "(10006, '_bis_', 2), "
                "(10010, '_heißt_', 2), "
                "(10011, '_hat Vornamen_', 2), "
                "(10012, '_hat Titel_', 2), "
                "(10020, '_gehört zu_', 2), "
                "(10030, '_ist_', 2), "
                "(10040, '_hat Sitz_', 2), "
                "(10042, '_hat Kanzleisitz_', 2), " //für private Personen
                "(10045, '_hat Hausnr_', 2), " //für 402 Adresse!
                "(10046, '_hat Zusatz (1. Zeile)_', 2), "

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
            "(400, 10020, 162), " //Straße _gehört zu_ Gemeinde

            "(402, 10020, 400), " //Adresse _gehört zu_ Straße
            "(402, 10045, 100001), " //Adresse _hat Hausnummer_ (string)
            "(402, 10046, 100001), " //Adresse _hat Zusatz (1. Zeile)_ (string)

            "(405, 10030, 100001), " //Telefonnummer _ist_ (string)
            "(413, 10020, 412), " //Teilnetzvorwahl _gehört zu_ Ländervorahl
            "(420, 10020, 413), " //Teilnehmernummer _gehört zu_ Teilnetzvorwahl

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

