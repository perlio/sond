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

#include "sond_database.h"



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

                "(372, 'Geschlecht', 1), "

                "(375, 'Adressbestandteil', 1), "
                "(380, 'Land', 375), "
                "(390, 'Ort', 375), "
                "(400, 'Straße', 375), "

                "(405, 'Telefonnummer', 1), "
                "(410, 'Vorwahl', 405), "
                "(412, 'Ländervorwahl', 410), "
                "(413, 'Teilnetzvorwahl', 410), "
                "(414, 'Ortsvorwahl', 413), "
                "(416, 'Mobilfunknetzvorwahl', 413), "
                "(420, 'Teilnehmernummer', 405), "
                "(430, 'Stammrufnummer', 420), "

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
                "(10030, '_Beginn_', 2), "
                "(10040, '_Ende_', 2), "
                "(10050, '_Bemerkung_', 2), "
                "(10100, '_Name_', 2), "
                "(10110, '_Kürzel_, 2); "

                //Properties für Subjekt
                "(10120, '_E-Mail-Adresse_', 2), "
                "(10130, '_Homepage_', 2), "
                "(10140, '_BIC_', 2), " //für Subjekt wo Bank ist

                //Properties für natürliche Person
                "(11010, '_Vorname_', 2), "
                "(11020, '_Titel_', 2), "
                "(11025, '_Dienstgrad_', 2), "


                //Properties für Kontoverbindung
                "(11027, '_IBAN_', 2), "

                //Für Sitz
                "(11030, '_Hausnr_', 2), "
                "(11035, '_PLZ_', 2), "
                "(11040, '_Adresszusatz_', 2), "

                //für Sitz und Rel Arbeitsplatz
                "(11050, '_Durchwahl_', 2), "

/*                //15000-19999: Prädikate zond
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
                "(20030, '_hat als Aktenbeteiligten', 2), "
                "(20031, '_ist Aktenbeteiligtenart_', 2), "
                "(20032, '_hat als Betreff_', 2), "
*/
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

            "(100, 10010, 420), " //Subjekt _hat_ Teilnehmernummer
            "(100, 10010, 440), " //Subjekt _hat_ Kontoverbindung
            "(100, 10030, 100004), " //Subjekt _Beginn_(date)
            "(100, 10040, 100004), " //Subjekt _Ende_(date)
            "(100, 10050, 100001), " //Subjekt _Bemerkung_(string)
            "(100, 10100, 100001), " //Subjekt Name(string)
            "(100, 10120, 100001), " //Subjekt _E-Mail-Adresse_(string)
            "(100, 10130, 100001), " //Subjekt _Homepage_(string)
            "(100, 10140, 100001), " //Subjekt _BIC_(string)

            "(110, 10010, 460), " //Subjekt des öffentlichen Rechts _hat_ Dienstsitz
            "(300, 10010, 452), " //Subjekt des Privatrechts _hat_Geschäftssitz
            "(310, 10010, 456), " //natürliche Person _hat_ Wohnsitz
            "(310, 10010, 372), " //natürliche Person _hat_ Geschlecht#
            "(310, 10010, 470), " //natürliche Person _hat_ Beruf/Dienstgrad
            "(310, 10020, 450), " //natürliche Person _arbeitet an_ Sitz
            "(310, 11010, 100001), " //natürliche Person Vorname(string)
            "(310, 11020, 100001), " //natürliche Person Titel(string)
            "(310, 11025, 100001), " //natürliche Person Dienstgrad(string)

            "(375, 10100, 100001), " //Adressbestandteil Name(string)
            "(380, 10110, 100001), " //Land Kürzel(string)
            "(390, 10000, 380), " //Ort _gehört zu_ Land
            "(400, 10000, 390), " //Straße gehört zu Ort

            "(405, 10100, 100001), " //Telefonnummer Name(string) - meine Telefonnr heißt "..."
            "(413, 10000, 412), " //Teilnetzvorwahll _gehört zu_ Ländervorwahl
            "(420, 10000, 413), " //Teilnehmernr _gehört zu_ Teilnetzvorwahl

            "(440, 10000, 100), " //Kontoverbindung _gehört zu_ Subjekt (Bank)

            "(450, 10000, 400), " //Sitz _gehört zu_ Straße
            "(450, 10010, 420), " //Sitz _hat_ Teilnehmernr
            "(450, 10120, 100001), " //Sitz _E-Mail-Adresse_(string)
            "(450, 10130, 100001), " //Sitz _Homepage_(string)
            "(450, 11030, 100001), " //Sitz Hausnr(string)
            "(450, 11035, 100001), " //Sitz _PLZ_(string)
            "(450, 11040, 100001), " //Sitz _Adresszusatz_(string)
            "(450, 11050, 100001), " //Sitz _Durchwahl_(string) (nicht nat. Personen zuzuordnende Durchwahlen)

            "(470, 10100, 100001), " //Beruf-Dienstgrad _Name_(string)
            "(470, 10110, 100001), " //Beruf-Dienstgrad _Kürzel_(string)

            "(10010, 10030, 100004), " //_hat_ _Beginn_(date)
            "(10010, 10040, 100004), " //_hat_ _Ende_(date)
            "(10010, 10050, 100001), " //_hat_ _Bemerkung_(date)
            "(10020, 11050, 100001), " //_arbeitet an_ _Durchwahl_(string)
            "(10030, 10050, 100001), " //_Beginn_ _Bemerkung_(string)
            "(10040, 10050, 100001), " //_Ende_ _Bemerkung_(string)

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


            "; ";
}

