/*
zond (zond_gemini.c) - Akten, Beweisstücke, Unterlagen
Copyright (C) 2022  pelo america

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

#include "zond_gemini.h"

#include <mupdf/fitz.h>
#include <sqlite3.h>

#include "global_types.h"
#include "../misc.h"
#include "../sond_database.h"
#include "../sond_checkbox.h"

#include "20allgemein/pdf_text.h"
#include "20allgemein/project.h"
#include "40viewer/document.h"
#include "40viewer/viewer.h"
#include "99conv/general.h"
#include "99conv/pdf.h"

#include "zond_pdf_document.h"
#include "zond_dbase.h"



typedef struct _Anschluss_Und_AI
{
    gchar* anschluss;
    gchar* AI;
} AnschlussUndAI;


typedef struct _Ereignis
{
    gchar* page_num_begin;
    gchar* index_begin;

    gchar* leitungsnr;
    gchar* massnahme;
    gchar* ueA;
    gchar* ueAI;
    gchar* uePerson;
    gchar* korrelations_nr;
    gchar* art;
    gchar* gis_link;
    gchar* standort_funkmast_uea;
    gchar* beginndatum;
    gchar* beginnzeit;
    gchar* endedatum;
    gchar* endezeit;
    gchar* richtung;
    GSList* sprecher_uea;
    GSList* sprecher_pa;
    GArray* partnernr_und_ai;
    AnschlussUndAI* verbundene_nr_und_ai;

    gchar* page_num_end;
    gchar* index_end;
} Ereignis;


typedef struct _Arrays
{
    GArray* arr_massnahmen;
    GArray* arr_anschluesse;
    GArray* arr_personen;
} Arrays;


typedef struct _Person
{
    gint ID_entity;
    gint ID_label_person;
    gchar* name;
    gchar* vorname;
    gchar* geb_datum_formatted;
} Person;


static void
zond_gemini_free_person( Person* person )
{
    g_free( person->name );
    g_free( person->vorname );
    g_free( person->geb_datum_formatted );

    return;
}


typedef struct _Anschluss
{
    gint ID_entity;
    gint ID_label_anschluss;
    gchar* anschluss;
    gint ID_entity_AI;
} Anschluss;


static void
zond_gemini_free_anschluss( Anschluss* anschluss )
{
    g_free( anschluss->anschluss );

    return;
}


typedef struct _Massnahme
{
    gint ID_entity;
    gchar* leitungs_nr;
    gint ID_entity_anschluss;
    gint ID_entity_uePerson;
} Massnahme;


static void
zond_gemini_free_massnahme( Massnahme* massnahme )
{
    g_free( massnahme->leitungs_nr );

    return;
}


static void
zond_gemini_free_anschluss_und_ai( AnschlussUndAI* aai )
{
    g_free( aai->anschluss );
    g_free( aai->AI );

    return;
}


static void
zond_gemini_free_ereignis( Ereignis* ereignis )
{
    if ( !ereignis ) return;

    g_free( ereignis->page_num_begin );
    g_free( ereignis->index_begin );
    g_free( ereignis->leitungsnr ); //
    g_free( ereignis->massnahme );
    g_free( ereignis->ueA ); //
    g_free( ereignis->ueAI ); //
    g_free( ereignis->uePerson ); //
    g_free( ereignis->korrelations_nr );
    g_free( ereignis->art );
    g_free( ereignis->gis_link );
    g_free( ereignis->standort_funkmast_uea );
    g_free( ereignis->beginndatum );
    g_free( ereignis->beginnzeit );
    g_free( ereignis->endedatum ),
    g_free( ereignis->endezeit );
    g_free( ereignis->richtung );
    g_slist_free_full( ereignis->sprecher_uea, g_free );
    g_slist_free_full( ereignis->sprecher_pa, g_free );
    if ( ereignis->partnernr_und_ai ) g_array_unref( ereignis->partnernr_und_ai );

    if ( ereignis->verbundene_nr_und_ai )
    {
        zond_gemini_free_anschluss_und_ai( ereignis->verbundene_nr_und_ai );
        g_free( ereignis->verbundene_nr_und_ai );
    }

    g_free( ereignis->page_num_end );
    g_free( ereignis->index_end );

    g_free( ereignis );

    return;
}


static gchar*
zond_gemini_format_time( const gchar* datum, const gchar* zeit, gchar** errmsg )
{
    gchar time[20] = { 0 };

    if ( strlen( datum ) != 10 ) ERROR_S_MESSAGE_VAL( "Datum irregulär", NULL )

    time[0] = datum[6];
    time[1] = datum[7];
    time[2] = datum[8];
    time[3] = datum[9];
    time[4] = '-';
    time[5] = datum[3];
    time[6] = datum[4];
    time[7] = '-';
    time[8] = datum[0];
    time[9] = datum[1];

    if ( zeit )
    {
        if ( strlen( zeit ) != 8 ) ERROR_S_MESSAGE_VAL( "Zeit irregulär", NULL )

        time[10] = ' ';
        memcpy( &time[11], zeit, 8 );
    }

    return g_strdup( time );
}


static gint
zond_gemini_get_or_create_datei( gpointer database, const gchar* pfad, gchar** errmsg )
{
    gint rc = 0;
    GArray* arr_ID_dateien = NULL;
    gint ID_entity_datei = 0;

    //Name == Pfad
    rc = sond_database_get_entities_for_property( database,
            10100, pfad, &arr_ID_dateien, errmsg );
    if ( rc == -1 ) ERROR_S
    else if ( rc > 1 )
    {
        g_array_unref( arr_ID_dateien );
        ERROR_S_MESSAGE( "Mehrere Dateien zu Pfad gespeichert" )
    }
    else if ( rc == 0 ) //noch nicht vorhanden -> einfügen
    {
        gint rc = 0;

        g_array_unref( arr_ID_dateien );

        //Datei-entity einfügen
        rc = sond_database_insert_entity( database, 660, errmsg );
        if ( rc == -1 ) ERROR_S

        ID_entity_datei = rc;

        //als "Name" ÜA eintragen
        rc = sond_database_insert_property( database, ID_entity_datei, 10100,
                pfad, errmsg );
        if ( rc == -1 ) ERROR_S
    }
    else if ( rc == 1 )
    {
        ID_entity_datei = g_array_index( arr_ID_dateien, gint, 0 );
        g_array_unref( arr_ID_dateien );
    }

    return ID_entity_datei;
}


static gint
zond_gemini_get_or_create_person( gpointer database, Arrays* arrays,
        const gchar* person_text, gint* ID_label_person, gchar** errmsg )
{
    gint rc = 0;
    gchar** ueai = NULL;
    gchar* name = NULL;
    gchar* vorname = NULL;
    gchar* geb_datum = NULL;
    gchar* geb_datum_formatted = NULL;
    gchar* geschlecht = NULL;
    GArray* arr_persons = NULL;
    Person person = { 0 };

    //ueAI normalisieren
    ueai = g_strsplit( person_text, ",", -1 );
    if ( ueai[0] && ueai[0][0] != '\0' )
    {
        name = g_strchug( ueai[0] );
        if ( ueai[1] && ueai[1][0] != '\0' )
        {
            vorname = g_strchug( ueai[1] );
            if ( ueai[2] && ueai[2][0] != '\0')
            {
                geb_datum = g_strchug( ueai[2] );
                geb_datum_formatted = zond_gemini_format_time( geb_datum, NULL, NULL );
                if ( !geb_datum_formatted ) geschlecht = geb_datum;
                else if ( ueai[3] && ueai[3][0] != '\0' ) geschlecht = g_strchug( ueai[3] );
            }
        }
    }
    //Geschlecht ermitteln
    if ( !geschlecht || !g_strcmp0( geschlecht, "unbekannt" ) ) *ID_label_person = 310;
    else if ( !g_strcmp0( geschlecht, "männlich" ) ) *ID_label_person = 312;
    else if ( !g_strcmp0( geschlecht, "weiblich" ) ) *ID_label_person = 313;
    else *ID_label_person = 314; //divers!

    //prüfen, ob schon in diesem Dokument vorhanden
    arr_persons = g_array_new( FALSE, FALSE, sizeof( gint ) );

    for ( gint i = 0; i < arrays->arr_personen->len; i++ )
    {
        Person person_loop = { 0 };

        person_loop = g_array_index( arrays->arr_personen, Person, i );

        if ( g_strcmp0( person_loop.name, name ) ) continue;

        if ( person_loop.vorname && vorname &&
                g_strcmp0( person_loop.vorname, vorname ) ) continue;

        if ( person_loop.geb_datum_formatted && geb_datum_formatted &&
                g_strcmp0( person_loop.geb_datum_formatted, geb_datum_formatted ) )
                continue;

        if ( person_loop.ID_label_person != 310 && *ID_label_person != 310 &&
                person_loop.ID_label_person != *ID_label_person ) continue;

        g_array_append_val( arr_persons, i );
    }

    if ( arr_persons->len == 1 )
    {
        Person person_best_match = { 0 };
        gint index = 0;

        //index zu arrays->arr_personen gespeichert
        index = g_array_index( arr_persons, gint, 0 );
        g_array_unref( arr_persons );
        person_best_match = g_array_index( arrays->arr_personen, Person, index );

        if ( !person_best_match.name && name )
        {
            gint rc = 0;

            g_array_index( arrays->arr_personen, Person, index ).name = g_strdup( name );

            //in database speichern
            //vielleicht schon drin!!!
            rc = sond_database_insert_property( database, person_best_match.ID_entity,
                    _NAME_, name, errmsg );
            if ( rc )
            {
                g_strfreev( ueai );
                g_free( geb_datum_formatted );
                ERROR_S
            }
        }

        if ( !person_best_match.vorname && vorname )
        {
            gint rc = 0;

            g_array_index( arrays->arr_personen, Person, index ).vorname = g_strdup( vorname );

            //in database speichern
            rc = sond_database_insert_property( database, person_best_match.ID_entity,
                    _VORNAME_, vorname, errmsg );
            if ( rc )
            {
                g_strfreev( ueai );
                g_free( geb_datum_formatted );
                ERROR_S
            }
        }

        if ( !person_best_match.geb_datum_formatted && geb_datum_formatted )
        {
            gint rc = 0;

            g_array_index( arrays->arr_personen, Person, index ).geb_datum_formatted =
                    g_strdup( geb_datum_formatted );

            //in database speichern
            rc = sond_database_insert_property( database, person_best_match.ID_entity,
                    _BEGINN_, geb_datum_formatted, errmsg );
            if ( rc )
            {
                g_strfreev( ueai );
                g_free( geb_datum_formatted );
                ERROR_S
            }
        }

        if ( person_best_match.ID_label_person == 310 && *ID_label_person != 310 )
        {
            gint rc = 0;

            g_array_index( arrays->arr_personen, Person, index ).ID_label_person =
                    *ID_label_person;

            //in database speichern
            rc = sond_database_update_label( database, person_best_match.ID_entity,
                    *ID_label_person, errmsg );
            if ( rc )
            {
                g_strfreev( ueai );
                g_free( geb_datum_formatted );
                ERROR_S
            }
        }

        g_strfreev( ueai );
        g_free( geb_datum_formatted );

        return person_best_match.ID_entity;
    }
    else if ( arr_persons->len > 1 )
    {
        //auswählen lassen
        for ( gint i = 0; i < arr_persons->len; i++ )
        {
            Person person = { 0 };
            gint index = 0;

            index = g_array_index( arr_persons, gint, i );
            person = g_array_index( arrays->arr_personen, Person, index );

            printf( "%i %i %s %s %s\n", person.ID_entity, person.ID_label_person, person.name, person.vorname, person.geb_datum_formatted );
        }
        printf( "%i %s %s %s\n", *ID_label_person, name, vorname, geb_datum_formatted );

        //einstweilen Fehlermeldung
        g_array_unref( arr_persons );
        g_strfreev( ueai );
        g_free( geb_datum_formatted );
        ERROR_S_MESSAGE( "Mehrere in Frage kommende Personen eingelesen" )
    }
    else g_array_unref( arr_persons ); //len == 0

    //DATABASE?
    arr_persons = NULL;
    //wir haben nur name
    if ( name && !vorname && !geb_datum_formatted )
    {
        rc = sond_database_get_entities_for_property( database, _NAME_, name,
            &arr_persons, errmsg );
        if ( rc == -1 )
        {
            g_strfreev( ueai );
            g_free( geb_datum_formatted );
            ERROR_S
        }
    }
    else if ( !name && vorname && !geb_datum_formatted )
    {
        gint rc = 0;

        rc = sond_database_get_entities_for_property( database, _VORNAME_,
                vorname, &arr_persons, errmsg );
        if ( rc == -1 )
        {
            g_strfreev( ueai );
            g_free( geb_datum_formatted );
            ERROR_S
        }
    }
    else if ( name && vorname && !geb_datum_formatted )
    {
        gint rc = 0;

        rc = sond_database_get_entities_for_properties_and( database,
                &arr_persons, errmsg, 10100, name, 11010, vorname, -1 );
        if ( rc == -1 )
        {
            g_strfreev( ueai );
            g_free( geb_datum_formatted );
            ERROR_S
        }
    }
    else if ( name && !vorname && geb_datum_formatted )
    {
        gint rc = 0;

        rc = sond_database_get_entities_for_properties_and( database,
                &arr_persons, errmsg, 10100, name, 10030, geb_datum_formatted, -1 );
        if ( rc == -1 )
        {
            g_strfreev( ueai );
            g_free( geb_datum_formatted );
            ERROR_S
        }
    }
    else if ( name && vorname && geb_datum_formatted )
    {
        gint rc = 0;

        rc = sond_database_get_entities_for_properties_and( database,
                &arr_persons, errmsg, 10100, name, 11010, vorname, 10030, geb_datum_formatted, -1 );
        if ( rc == -1 )
        {
            g_strfreev( ueai );
            g_free( geb_datum_formatted );
            ERROR_S
        }
    }

    if ( arr_persons )
    {
        for ( gint i = 0; i < arr_persons->len; i++ )
        {
            gint rc = 0;
            gint ID_entity_person = 0;
            gint ID_label_person_loop = 0;

            ID_entity_person = g_array_index( arr_persons, gint, i );

            rc = sond_database_get_ID_label_for_entity( database, ID_entity_person, errmsg );
            if ( rc == -1 )
            {
                g_strfreev( ueai );
                g_free( geb_datum_formatted );
                g_array_unref( arr_persons );
                ERROR_S
            }
            else ID_label_person_loop = rc;

            if ( ID_label_person_loop != 310 && *ID_label_person != 310 &&
                    ID_label_person_loop != *ID_label_person )
            {
                //rausschmeißen
                g_array_remove_index( arr_persons, i );
                i--;
                continue;
            }
        }

        if ( arr_persons->len > 1 )
        {
            for ( gint i = 0; i < arr_persons->len; i++ )
            {
                printf( "ID_entity: %i\n", g_array_index( arr_persons, gint, i ) );
            }
            //auswählen lassen
            printf( "%i %s %s %s\n", *ID_label_person, name, vorname, geb_datum_formatted );
        }
        else if ( arr_persons->len == 1 ) //genau einer paßt!
        {
            person.ID_entity = g_array_index( arr_persons, gint, 0 );
            person.ID_label_person = *ID_label_person;
            person.name = g_strdup( name );
            person.vorname = g_strdup( vorname );
            person.geb_datum_formatted = g_strdup( geb_datum_formatted );

            g_array_append_val( arrays->arr_personen, person );

            g_strfreev( ueai );
            g_free( geb_datum_formatted );
            g_array_unref( arr_persons );

            //ggf. database ergänzen
            if ( person.name )
            {
                gint rc = 0;

                //prüfen, ob zur ID property "Name" vorliegt
                rc = sond_database_get_first_property_value_for_subject( database,
                        person.ID_entity, _NAME_, NULL, errmsg );
                if ( rc == -1 ) ERROR_S
                else if ( rc == 0 ) //noch keine property gespeichert
                {
                    gint rc = 0;

                    rc = sond_database_insert_property( database, person.ID_entity,
                            _NAME_, person.name, errmsg );
                    if ( rc ) ERROR_S
                }
                //else rc == 1 -> schon was gespeichert, hier keine Kontrolle, ob gleich
            }

            if ( person.vorname )
            {
                gint rc = 0;

                //prüfen, ob zur ID property "Name" vorliegt
                rc = sond_database_get_first_property_value_for_subject( database,
                        person.ID_entity, _VORNAME_, NULL, errmsg );
                if ( rc == -1 ) ERROR_S
                else if ( rc == 0 ) //noch keine property gespeichert
                {
                    gint rc = 0;

                    rc = sond_database_insert_property( database, person.ID_entity,
                            _VORNAME_, person.vorname, errmsg );
                    if ( rc ) ERROR_S
                }
            }

            if ( person.geb_datum_formatted )
            {
                gint rc = 0;

                //prüfen, ob zur ID property "Name" vorliegt
                rc = sond_database_get_first_property_value_for_subject( database,
                        person.ID_entity, _BEGINN_, NULL, errmsg );
                if ( rc == -1 ) ERROR_S
                else if ( rc == 0 ) //noch keine property gespeichert
                {
                    gint rc = 0;

                    rc = sond_database_insert_property( database, person.ID_entity,
                            _BEGINN_, person.geb_datum_formatted, errmsg );
                    if ( rc ) ERROR_S
                }
            }

            if ( person.ID_label_person != 310 )
            {
                gint rc = 0;

                rc = sond_database_get_ID_label_for_entity( database, person.ID_entity, errmsg );
                if ( rc == -1 ) ERROR_S
                else if ( rc == 310 )
                {
                    gint rc = 0;

                    rc = sond_database_update_label( database, person.ID_entity,
                            person.ID_label_person, errmsg );
                    if ( rc == -1 ) ERROR_S
                }
                //else: Prüfung Paranoia, haben wir oben schon getestet!
            }

            return person.ID_entity;
        }
        else g_array_unref( arr_persons );
    }

    //Person einfügen
    rc = sond_database_insert_entity( database, *ID_label_person, errmsg );
    if ( rc == -1 )
    {
        g_strfreev( ueai );
        g_free( geb_datum_formatted );
        ERROR_S
    }
    else person.ID_entity = rc;

    person.ID_label_person = *ID_label_person;

    if ( name )
    {
        rc = sond_database_insert_property( database, person.ID_entity, 10100, name, errmsg );
        if ( rc == -1 )
        {
            g_strfreev( ueai );
            g_free( geb_datum_formatted );
            ERROR_S
        }

        person.name = g_strdup( name );
    }

    if ( vorname )
    {
        rc = sond_database_insert_property( database, person.ID_entity, 11010, vorname, errmsg );
        if ( rc == -1 )
        {
            g_strfreev( ueai );
            g_free( geb_datum_formatted );
            ERROR_S
        }

        person.vorname = g_strdup( vorname );
    }

    if ( geb_datum_formatted )
    {
        rc = sond_database_insert_property( database, person.ID_entity, 10030, geb_datum_formatted, errmsg );
        if ( rc == -1 )
        {
            g_strfreev( ueai );
            g_free( geb_datum_formatted );
            ERROR_S
        }

        person.geb_datum_formatted = g_strdup( geb_datum_formatted );
    }

    g_strfreev( ueai );
    g_free( geb_datum_formatted );

    g_array_append_val( arrays->arr_personen, person );

    return person.ID_entity;
}


static gint
zond_gemini_get_or_create_anschluss( gpointer database, Arrays* arrays,
        gint ID_label_anschluss, const gchar* anschluss_text,
        const gchar* AI_text, gchar** errmsg )
{
    gint rc = 0;
    gint ID_entity_AI = 0;
    gint ID_label_AI = 0;
    GArray* arr_anschluesse = NULL;
    Anschluss anschluss = { 0 };

    if ( AI_text )
    {
        rc = zond_gemini_get_or_create_person( database, arrays, AI_text,
                &ID_label_AI, errmsg );
        if ( rc == -1 ) ERROR_S
        else ID_entity_AI = rc;
    }

    for ( gint i = 0; i < arrays->arr_anschluesse->len; i++ )
    {
        Anschluss anschluss_loop = { 0 };

        anschluss_loop = g_array_index( arrays->arr_anschluesse, Anschluss, i );

        if ( g_strcmp0( anschluss_loop.anschluss, anschluss_text ) ) continue;
        else if ( anschluss_loop.ID_label_anschluss != ID_label_anschluss ) continue;
        else if ( (anschluss_loop.ID_entity_AI != ID_entity_AI) && ID_entity_AI &&
                anschluss_loop.ID_entity_AI ) continue;
        else if ( !anschluss_loop.ID_entity_AI && ID_entity_AI )
        {
            gint rc = 0;

            //kein AI gespeichert, aber aktuell hat -> komisch
            //aber vielleicht ergänzen?
            //verantwortbar, weil ja im selben Dokument
            g_array_index( arrays->arr_anschluesse, Anschluss, i ).ID_entity_AI = ID_entity_AI;

            rc = sond_database_insert_rel( database, anschluss_loop.ID_entity, 10010, ID_entity_AI, errmsg );
            if ( rc == -1 ) ERROR_S
        }

        return anschluss_loop.ID_entity;
   }

    //wenn nicht gefunden: in db suchen
    //Name == anschluss
    rc = sond_database_get_entities_for_property( database,
            10100, anschluss_text, &arr_anschluesse, errmsg );
    if ( rc == -1 ) ERROR_S

    //alle durchgehen
    for ( gint i = 0; i < arr_anschluesse->len; i++ )
    {
        gint rc = 0;
        gint ID_entity_anschluss = 0;
        gint ID_label_anschluss_loop = 0;

        ID_entity_anschluss = g_array_index( arr_anschluesse, gint, i );

        //ID_label_anschluss
        rc = sond_database_get_ID_label_for_entity( database,
                ID_entity_anschluss, errmsg );
        if ( rc == -1 )
        {
            g_array_unref( arr_anschluesse );
            ERROR_S
        }
        else ID_label_anschluss_loop = rc;

        if ( ID_label_anschluss_loop != ID_label_anschluss )
        {
            g_array_remove_index( arr_anschluesse, i );
            i--;
            continue;
        }

        //richtiger AI?
        if ( ID_entity_AI )
        {
            gint rc = 0;
            GArray* arr_AIs = NULL;

            rc = sond_database_get_object_for_subject( database,
                    ID_entity_anschluss, &arr_AIs, errmsg, 10010, ID_label_AI, -1 );
            if ( rc == -1 )
            {
                g_array_unref( arr_anschluesse );
                ERROR_S
            }

            if ( arr_AIs->len )
            {
                for ( gint u = 0; u < arr_AIs->len; u++ )
                {
                    gint ID_entity_AI_loop = 0;

                    ID_entity_AI_loop = g_array_index( arr_AIs, gint, u );

                    if ( ID_entity_AI_loop != ID_entity_AI ) continue;

                    g_array_unref( arr_AIs );
                    g_array_unref( arr_anschluesse );

                    anschluss.ID_entity = ID_entity_anschluss;
                    anschluss.ID_entity_AI = ID_entity_AI;
                    anschluss.anschluss = g_strdup( anschluss_text );
                    anschluss.ID_label_anschluss = ID_label_anschluss;

                    g_array_append_val( arrays->arr_anschluesse, anschluss );

                    return ID_entity_anschluss;
                }

                g_array_unref( arr_AIs );

                g_array_remove_index( arr_anschluesse, i );
                i--;
                continue;
            }

            g_array_unref( arr_AIs );
        }
    }

    if ( arr_anschluesse->len == 1 )
    {
        //nur eins übriggeblieben, wo pascht
        //ergänzen:
        anschluss.ID_entity = g_array_index( arr_anschluesse, gint, 0 );

        g_array_unref( arr_anschluesse );

        if ( ID_entity_AI )
        {
            gint rc = 0;

            anschluss.ID_entity_AI = ID_entity_AI;
            rc = sond_database_insert_rel( database,
                    anschluss.ID_entity, 10010, ID_entity_AI, errmsg );
            if ( rc == -1 ) ERROR_S
        }

        anschluss_text = g_strdup( anschluss_text );
        anschluss.ID_label_anschluss = ID_label_anschluss;

        g_array_append_val( arrays->arr_anschluesse, anschluss );

        return anschluss.ID_entity;
    }
    else if ( arr_anschluesse->len > 1 )
    {
        g_array_unref( arr_anschluesse );
        ERROR_S_MESSAGE( "Mehr als ein Anschluß würden passen - oh je" )
    }
    g_array_unref( arr_anschluesse );

    //ÜA-entity einfügen
    rc = sond_database_insert_entity( database, ID_label_anschluss, errmsg );
    if ( rc == -1 ) ERROR_S
    else anschluss.ID_entity = rc;

    anschluss.ID_label_anschluss = ID_label_anschluss;

    //als "Name" ÜA eintragen
    rc = sond_database_insert_property( database, anschluss.ID_entity, 10100,
            anschluss_text, errmsg );
    if ( rc == -1 ) ERROR_S

    anschluss.anschluss = g_strdup( anschluss_text );

    //Falls AI zum ÜA gelesen werden konnte...
    if ( ID_entity_AI )
    {
        gint rc = 0;

        //rel ueAI - "Anschluß" einfügen
        rc = sond_database_insert_rel( database, anschluss.ID_entity, 10010,
                ID_entity_AI, errmsg );
        if ( rc == -1 ) ERROR_S

        anschluss.ID_entity_AI = ID_entity_AI;
    }

    g_array_append_val( arrays->arr_anschluesse, anschluss );

    return anschluss.ID_entity;
}


static gint
zond_gemini_get_or_create_tkue_massnahme( gpointer database, Arrays* arrays,
        Ereignis* ereignis, gchar** errmsg )
{
    gint rc = 0;
    gint ID_label_anschluss = 0;
    gint ID_entity_anschluss = 0;
    gint ID_label_uePerson = 0;
    gint ID_entity_uePerson = 0;
    GArray* arr_ID_entities_tkue_massnahmen = NULL;
    Massnahme massnahme = { 0 };

    //Verbindung zu überwachtem Anschluß/Endgerät
    if ( g_str_has_suffix( ereignis->massnahme, "GSM" ) ) ID_label_anschluss = 425;
    else if ( g_str_has_suffix( ereignis->massnahme, "IMSI" ) ) ID_label_anschluss = 435;
    else if ( g_str_has_suffix( ereignis->massnahme, "IMEI" ) ) ID_label_anschluss = 437;
    else ID_label_anschluss = 425; //wenn nix, wohl Telefon

    rc = zond_gemini_get_or_create_anschluss( database, arrays, ID_label_anschluss,
            ereignis->ueA, ereignis->ueAI, errmsg );
    if ( rc == -1 ) ERROR_S
    else ID_entity_anschluss = rc;

    if ( ereignis->uePerson )
    {
        gint rc = 0;

        rc = zond_gemini_get_or_create_person( database, arrays,
                ereignis->uePerson, &ID_label_uePerson, errmsg );
        if( rc == -1 ) ERROR_S
        else ID_entity_uePerson = rc;
    }

    //schon gespeichert?
    for ( gint i = 0; i < arrays->arr_massnahmen->len; i++ )
    {
        Massnahme massnahme_loop = { 0 };

        massnahme_loop = g_array_index( arrays->arr_massnahmen, Massnahme, i );

        //andere Leitungsnr -> weiter
        if ( g_strcmp0( massnahme_loop.leitungs_nr, ereignis->leitungsnr ) ) continue;

        //Maßnahme hat anderen Anschluß -> weiter
        if ( massnahme_loop.ID_entity_anschluss != ID_entity_anschluss ) continue;

        //Maßnahme hat uePerson, aber andere -> weiter
        if ( (massnahme_loop.ID_entity_uePerson != ID_entity_uePerson) &&
                massnahme_loop.ID_entity_uePerson > 0 && ID_entity_uePerson > 0 ) continue;

        //Maßnahme ohne uePerson gespeichert, aber hier uePerson eingelesen
        //komisch, darf eigentlich nicht sein
        //Ist aber z.B. bei EG Karl so
        //es wird sehr kompliziert, wenn man sich nicht dafür entscheidet,
        //die gespeicherte entity zu "ergänzen"
        if ( !massnahme_loop.ID_entity_uePerson && ID_entity_uePerson )
        {
            //gespeicherte Maßnahme ändern
            g_array_index( arrays->arr_massnahmen, Massnahme, i ).ID_entity_uePerson = ID_entity_uePerson;

            //in database verknüpfen
            rc = sond_database_insert_rel( database, massnahme_loop.ID_entity,
                    10010, ID_entity_uePerson, errmsg );
            if ( rc == -1 ) ERROR_S
        }
        //ebenso soll nicht nachgefragt werden, ob, wenn nix aktuell und nix gespeichert ist
        //- if ( !massnahme_loop.ID_entity_uePerson && !ID_entity_uePerson ) -
        //etwas unternommen werden soll

        return massnahme_loop.ID_entity;
    }

    //ansonsten prüfen, ob in db gespeichert
    rc = sond_database_get_entities_for_property( database,
            11059, ereignis->leitungsnr, &arr_ID_entities_tkue_massnahmen, errmsg );
    if ( rc == -1 ) ERROR_S
    else if ( rc ) // >=1
    {
        //alle mit gleicher Leitungsnummer durchprobieren, ob auchRest gleich
        for ( gint i = 0; i < arr_ID_entities_tkue_massnahmen->len; i++ )
        {
            gint rc = 0;
            gint ID_entity_tkue_massnahme = 0;
            GArray* arr_anschluesse = NULL;
            gboolean found = FALSE;

            ID_entity_tkue_massnahme = g_array_index( arr_ID_entities_tkue_massnahmen, gint, i );

            //_hat_anschluß einlesen
            rc = sond_database_get_object_for_subject( database,
                    ID_entity_tkue_massnahme, &arr_anschluesse, errmsg, 10010,
                    ID_label_anschluss, -1 );
            if ( rc == -1 )
            {
                g_array_unref( arr_ID_entities_tkue_massnahmen );
                ERROR_S
            }

            //jede Maßnahme muß einen Anschluß haben.
            for ( gint u = 0; u < arr_anschluesse->len; u++ )
            {
                gint ID_entity_anschluss_loop = 0;

                ID_entity_anschluss_loop = g_array_index( arr_anschluesse, gint, u );

                //anschluss nicht gleich: rausschmeißen!
                if ( ID_entity_anschluss_loop != ID_entity_anschluss ) continue;

                found = TRUE;
                break;
            }

            g_array_unref( arr_anschluesse );

            //rauskegeln, falls keiner paßt - gibt wahrscheinlich nur einen
            if ( !found )
            {
                g_array_remove_index( arr_ID_entities_tkue_massnahmen, i );
                i--;
                continue;
            }

            //jetzt gucken, ob uePerson paßt
            if ( ID_entity_uePerson )
            {
                gint rc = 0;
                GArray* arr_ue_personen = NULL;

                //hat uePerson einlesen
                rc = sond_database_get_object_for_subject( database,
                        ID_entity_tkue_massnahme, &arr_ue_personen, errmsg,
                        10010, 310, -1 ); //füralle Menschen
                if ( rc == -1 )
                {
                    g_array_unref( arr_ID_entities_tkue_massnahmen );
                    g_array_unref( arr_anschluesse );

                    ERROR_S
                }

                if ( arr_ue_personen->len )
                {
                    for ( gint u = 0; u < arr_ue_personen->len; u++ )
                    {
                        gint ID_entity_uePerson_loop = 0;

                        ID_entity_uePerson_loop = g_array_index( arr_ue_personen, gint, u );

                        if ( ID_entity_uePerson_loop != ID_entity_uePerson ) continue;

                        //sonst: identische Maßnahme gepeichert
                        g_array_unref( arr_ue_personen );
                        g_array_unref( arr_ID_entities_tkue_massnahmen );
                        g_array_unref( arr_anschluesse );

                        massnahme.ID_entity_uePerson = ID_entity_uePerson;
                        massnahme.ID_entity = ID_entity_tkue_massnahme;
                        massnahme.ID_entity_anschluss = ID_entity_anschluss;
                        massnahme.leitungs_nr = g_strdup( ereignis->leitungsnr );

                        //in array ablegen, für's nächste Mal
                        g_array_append_val( arrays->arr_massnahmen, massnahme );

                        return ID_entity_tkue_massnahme;
                    }

                    g_array_remove_index( arr_ID_entities_tkue_massnahmen, i );
                    i--;
                    continue;
                }

                g_array_unref( arr_ue_personen );
            }
        }

        //entweder uePersonen zu massnahme gelesen aber keine paßt -> rausgeschmissen
        //oder keine gelesen ->ist noch drinnen
        //das sind jetzt die, die passen...
        //...weil entweder Person eingelesen aber nicht gespeichert
        // oder Person gespeichert aber nicht eingelesen
        //...oder keine Person eingelesen und keine gespeichert

        //müßte man anzeigen und dann auswählen
        //aber: sehr kompliziert!
        //vielleicht, wenn nur eine Möglichkeit übrigbleibt, einfach ergänzen
        if ( arr_ID_entities_tkue_massnahmen->len == 1 )
        {
            massnahme.ID_entity = g_array_index( arr_ID_entities_tkue_massnahmen, gint, 0 );
            g_array_unref( arr_ID_entities_tkue_massnahmen );

            if ( ID_entity_uePerson )
            {
                gint rc = 0;

                //in zu speichernde Maßnahme einfügen
                massnahme.ID_entity_uePerson = ID_entity_uePerson;

                //rel in database
                rc = sond_database_insert_rel( database, massnahme.ID_entity, 10010, ID_entity_uePerson, errmsg );
                if ( rc == -1 ) ERROR_S
            }

            massnahme.ID_entity_anschluss = ID_entity_anschluss;
            massnahme.leitungs_nr = g_strdup( ereignis->leitungsnr );

            //in array ablegen, für's nächste Mal
            g_array_append_val( arrays->arr_massnahmen, massnahme );

            return massnahme.ID_entity;
        }
        else if ( arr_ID_entities_tkue_massnahmen->len > 1 )
        {
            g_array_unref( arr_ID_entities_tkue_massnahmen );
            ERROR_S_MESSAGE( "Hilfe! Mehrere TKÜ-Maßnahmen mit gleicher "
                    "Leitungs-Nr. und gleichem Anschluß gespeichert, die passen" )
        }
    }
    g_array_unref( arr_ID_entities_tkue_massnahmen );

    //sonst neu erzeugen
    //TKÜ-Maßnahme = 1000
    rc = sond_database_insert_entity( database, 1000, errmsg );
    if ( rc == -1 ) ERROR_S
    else massnahme.ID_entity = rc;

    //Leitungsnummer eintragen
    massnahme.leitungs_nr = g_strdup( ereignis->leitungsnr );

    rc = sond_database_insert_property( database,
            massnahme.ID_entity, 11059, ereignis->leitungsnr, errmsg );
    if ( rc == -1 ) ERROR_S

    if ( ereignis->ueA )
    {
        gint rc = 0;

        massnahme.ID_entity_anschluss = ID_entity_anschluss;

        //rel zwischen ueA und TKÜ-Maßnahme erzeugen
        rc = sond_database_insert_rel( database,
                massnahme.ID_entity, 10010, massnahme.ID_entity_anschluss, errmsg );
        if ( rc == -1 ) ERROR_S
    }

    if ( ID_entity_uePerson )
    {
        gint rc = 0;

        massnahme.ID_entity_uePerson = ID_entity_uePerson;

        //rel TKÜ-Maßnahme - uePerson einfügen
        rc = sond_database_insert_rel( database,
                massnahme.ID_entity, _HAT_, massnahme.ID_entity_uePerson, errmsg );
        if ( rc == -1 ) ERROR_S
    }

    g_array_append_val( arrays->arr_massnahmen, massnahme );

    return massnahme.ID_entity;
}


static gint
zond_gemini_save_ereignis( gpointer database, Arrays* arrays, Ereignis* ereignis,
        gint ID_entity_datei, gchar** errmsg )
{
    gint rc = 0;
    gint ID_entity_TKUE_ereignis = 0;
    gint ID_label_ereignis = 0;
    gint ID_entity_gemini_urkunde = 0;
    gint ID_entity_fundstelle = 0;
    gint ID_entity_TKUE_massnahme = 0;

    if ( !ereignis || !ereignis->leitungsnr ) return 0; //Hinweis in info_window oder log mit Fundstelle

    rc = zond_gemini_get_or_create_tkue_massnahme( database, arrays, ereignis,
            errmsg );
    if ( rc == -1 ) ERROR_S
    else ID_entity_TKUE_massnahme = rc;

    //TKÜ-Ereignis
    //TKÜ-Ereignis Art ermitteln
    if ( !g_strcmp0( ereignis->art, "Gespräch (Audio)" ) ) ID_label_ereignis = 1012;
    else if ( !g_strcmp0( ereignis->art, "Textnachrichtenaustausch" ) )
            ID_label_ereignis = 1015;
    else if ( !g_strcmp0( ereignis->art, "Multimedianachrichtenaustausch" ) )
            ID_label_ereignis = 1020;
    else ID_label_ereignis = 1010;

    //Ereignis eintragen
    rc = sond_database_insert_entity( database, ID_label_ereignis, errmsg );
    if ( rc == -1 ) ERROR_S
    else ID_entity_TKUE_ereignis = rc;

    //Zuordnung zu Maßnahme: Gespräch _gehört zu_ Maßnahme
    rc = sond_database_insert_rel( database,
            ID_entity_TKUE_ereignis, 10000, ID_entity_TKUE_massnahme, errmsg );
    if ( rc == -1 ) ERROR_S

    //Properties zu TKÜ-Ereignis speichern
    if ( ereignis->korrelations_nr )
    {
        gint rc = 0;

        rc = sond_database_insert_property( database,
                ID_entity_TKUE_ereignis, 11065, ereignis->korrelations_nr, errmsg );
        if ( rc == -1 ) ERROR_S
    }

    if ( ereignis->art )
    {
        gint rc = 0;

        rc = sond_database_insert_property( database,
                ID_entity_TKUE_ereignis, 11067, ereignis->art, errmsg );
        if ( rc == -1 ) ERROR_S
    }

    if ( ereignis->gis_link )
    {
        gint rc = 0;

        rc = sond_database_insert_property( database,
                ID_entity_TKUE_ereignis, 11070, ereignis->gis_link, errmsg );
        if ( rc == -1 ) ERROR_S
    }

    if ( ereignis->standort_funkmast_uea )
    {
        gint rc = 0;

        rc = sond_database_insert_property( database,
                ID_entity_TKUE_ereignis, 11072, ereignis->standort_funkmast_uea, errmsg );
        if ( rc == -1 ) ERROR_S
    }

    if ( ereignis->beginndatum )
    {
        gint rc = 0;
        gchar* time = NULL;

        time = zond_gemini_format_time( ereignis->beginndatum, ereignis->beginnzeit, errmsg );
        if ( !time ) ERROR_S

        rc = sond_database_insert_property( database,
                ID_entity_TKUE_ereignis, 10030, time, errmsg );
        if ( rc == -1 ) ERROR_S

        g_free( time );
    }

    if ( ereignis->endedatum )
    {
        gint rc = 0;
        gchar* time = NULL;

        time = zond_gemini_format_time( ereignis->endedatum, ereignis->endezeit, errmsg );
        if ( !time ) ERROR_S

        rc = sond_database_insert_property( database,
                ID_entity_TKUE_ereignis, 10040, time, errmsg );
        if ( rc == -1 ) ERROR_S

        g_free( time );
    }

    if ( ereignis->richtung )
    {
        gint rc = 0;

        rc = sond_database_insert_property( database,
                ID_entity_TKUE_ereignis, 11075, ereignis->richtung, errmsg );
        if ( rc == -1 ) ERROR_S
    }

    if ( ereignis->sprecher_uea )
    {
        do
        {
            gint rc = 0;
            gint ID_entity_sprecher_ua = 0;
            gint ID_label_person = 0;

            rc = zond_gemini_get_or_create_person( database, arrays,
                    ereignis->sprecher_uea->data, &ID_label_person, errmsg );
            if ( rc == -1 ) ERROR_S
            else ID_entity_sprecher_ua = rc;

            //rel TKÜ-Maßnahme - uePerson einfügen
            rc = sond_database_insert_rel( database,
                    ID_entity_TKUE_ereignis, 12000, ID_entity_sprecher_ua, errmsg );
            if ( rc == -1 ) ERROR_S
        } while ( (ereignis->sprecher_uea = ereignis->sprecher_uea->next) );
    }

    if ( ereignis->sprecher_pa )
    {
        do
        {
            gint rc = 0;
            gint ID_entity_sprecher_pa = 0;
            gint ID_label_person = 0;

            rc = zond_gemini_get_or_create_person( database, arrays,
                    ereignis->sprecher_pa->data, &ID_label_person, errmsg );
            if ( rc == -1 ) ERROR_S
            else ID_entity_sprecher_pa = rc;

            //rel TKÜ-Maßnahme - uePerson einfügen
            rc = sond_database_insert_rel( database, ID_entity_TKUE_ereignis,
                    12005, ID_entity_sprecher_pa, errmsg );
            if ( rc == -1 ) ERROR_S
        } while ( (ereignis->sprecher_pa = ereignis->sprecher_pa->next) );
    }

    if ( ereignis->partnernr_und_ai )
    {
        for ( gint i = 0; i < ereignis->partnernr_und_ai->len; i++ )
        {
            gint rc = 0;
            gchar* ptr_fill = NULL;
            gchar* ptr_read = NULL;
            gint ID_entity_PA = 0;

            AnschlussUndAI aai =
                    g_array_index( ereignis->partnernr_und_ai, AnschlussUndAI, i );

            ptr_read = ptr_fill = aai.anschluss;

            //Leerzeichen entfernen
            while ( *ptr_read )
            {
                if ( *ptr_read != ' ' )
                {
                    *ptr_fill = *ptr_read;
                    ptr_fill++;
                }
                ptr_read++;
            }
            *ptr_fill = 0;

            rc = zond_gemini_get_or_create_anschluss( database, arrays,
                    425, aai.anschluss, aai.AI, errmsg );
            if ( rc == -1 ) ERROR_S
            else ID_entity_PA = rc;

            //rel zwischen ueA und TKÜ-Maßnahme erzeugen
            rc = sond_database_insert_rel( database, ID_entity_TKUE_ereignis,
                    10010, ID_entity_PA, errmsg );
            if ( rc == -1 ) ERROR_S
        }
    }

    if ( ereignis->verbundene_nr_und_ai )
    {
        gint rc = 0;
        gchar* ptr_fill = NULL;
        gchar* ptr_read = NULL;
        gint ID_entity_verb_anschluss = 0;

        ptr_read = ptr_fill = ereignis->verbundene_nr_und_ai->anschluss;

        //Leerzeichen entfernen
        while ( *ptr_read )
        {
            if ( *ptr_read != ' ' )
            {
                *ptr_fill = *ptr_read;
                ptr_fill++;
            }
            ptr_read++;
        }
        *ptr_fill = 0;

        rc = zond_gemini_get_or_create_anschluss( database, arrays,
                425, ereignis->verbundene_nr_und_ai->anschluss,
                ereignis->verbundene_nr_und_ai->AI, errmsg );
        if ( rc == -1 ) ERROR_S
        else ID_entity_verb_anschluss = rc;

        //rel TKÜ-Ereignis und Anschluß verb Nr erzeugen
        rc = sond_database_insert_rel( database, ID_entity_TKUE_ereignis, 10010,
                ID_entity_verb_anschluss, errmsg );
        if ( rc == -1 ) ERROR_S
    }

    //Gemini-Urkunde eintragen
    rc = sond_database_insert_entity( database, 836, errmsg );
    if ( rc == -1 ) ERROR_S
    else ID_entity_gemini_urkunde = rc;

    rc = sond_database_insert_rel( database,
            ID_entity_TKUE_ereignis, 10010, ID_entity_gemini_urkunde, errmsg );
    if ( rc == -1 ) ERROR_S

    //Fundstelle zur Gemini-Urkunde
    rc = sond_database_insert_entity( database, 650, errmsg );
    if ( rc == -1 ) ERROR_S
    else ID_entity_fundstelle = rc;

    //rel zu Urkunde
    rc = sond_database_insert_rel( database, ID_entity_gemini_urkunde, 10010,
            ID_entity_fundstelle, errmsg );
    if ( rc == -1 ) ERROR_S

    //rel zwischen Fundstelle und Datei
    rc = sond_database_insert_rel( database, ID_entity_fundstelle, 10000, ID_entity_datei, errmsg );
    if ( rc == -1 ) ERROR_S

    //properties zur Fundstelle
    rc = sond_database_insert_property( database,
            ID_entity_fundstelle, 11052, ereignis->page_num_begin, errmsg );
    if ( rc == -1 ) ERROR_S

    rc = sond_database_insert_property( database,
            ID_entity_fundstelle, 11054, ereignis->index_begin, errmsg );
    if ( rc == -1 ) ERROR_S

    rc = sond_database_insert_property( database,
            ID_entity_fundstelle, 11055, ereignis->page_num_end, errmsg );
    if ( rc == -1 ) ERROR_S

    rc = sond_database_insert_property( database,
            ID_entity_fundstelle, 11057, ereignis->index_end, errmsg );
    if ( rc == -1 ) ERROR_S

    return 0;
}


typedef struct _Line_At_Pos
{
    fz_stext_line* line;
    fz_stext_block* block;
    fz_stext_page* page;
    gint page_num;
} LineAtPos;


static gint
zond_gemini_close_ereignis( gpointer database, Arrays* arrays, Ereignis* ereignis, gint ID_entity_datei,
        LineAtPos last_line_at_pos, gchar** errmsg )
{
    gint rc = 0;

    if ( !ereignis ) return 0;

    ereignis->page_num_end = g_strdup_printf( "%i", last_line_at_pos.page_num );
    ereignis->index_end = g_strdup_printf("%i", (gint) last_line_at_pos.line->bbox.y1 );

    rc = zond_gemini_save_ereignis( database, arrays, ereignis, ID_entity_datei, errmsg );
    g_clear_pointer( &ereignis, zond_gemini_free_ereignis );
    if ( rc ) ERROR_S

    return 0;
}


static gint
zond_gemini_get_next_line( ZondPdfDocument* zond_pdf_document,
        LineAtPos* line_at_pos, gchar** errmsg )
{
    if ( !line_at_pos->line || !(line_at_pos->line = line_at_pos->line->next) )
    {
        if ( !line_at_pos->block || !(line_at_pos->block = line_at_pos->block->next)
                || !(line_at_pos->line = line_at_pos->block->u.t.first_line) )
        {
            gint rc = 0;
            PdfDocumentPage* pdf_document_page = NULL;

            if ( line_at_pos->page )
            {
                if ( line_at_pos->page_num ==
                        zond_pdf_document_get_number_of_pages( zond_pdf_document ) - 1 ) return 1;
                else line_at_pos->page_num++;
            }

            pdf_document_page = zond_pdf_document_get_pdf_document_page(
                    zond_pdf_document, line_at_pos->page_num );

            rc = pdf_text_render_stext_page_direct(
                    zond_pdf_document_get_ctx( zond_pdf_document ),
                    pdf_document_page, errmsg );
            if ( rc ) ERROR_S

            line_at_pos->page = pdf_document_page->stext_page;
            line_at_pos->block = line_at_pos->page->first_block;
            line_at_pos->line = line_at_pos->block->u.t.first_line;
        }
    }

    return 0;
}


static gchar*
zond_gemini_get_next_line_string( ZondPdfDocument* zond_pdf_document,
        LineAtPos* line_at_pos, gchar** errmsg )
{
    gint rc = 0;
    gchar* line_string = NULL;

    rc = zond_gemini_get_next_line( zond_pdf_document, line_at_pos, errmsg );
    if ( rc == -1 ) ERROR_S_VAL( NULL )
    else if ( rc == 1 ) ERROR_S_MESSAGE_VAL( "Gemini malformed", NULL )

    line_string = pdf_get_string_from_line( zond_pdf_document_get_ctx( zond_pdf_document ), line_at_pos->line, errmsg );
    if ( !line_string ) ERROR_S_VAL( NULL )

    return line_string;
}


static gint
zond_gemini_read_zond_pdf_document( Projekt* zond, InfoWindow* info_window,
        ZondDBase* zond_dbase, ZondPdfDocument* zond_pdf_document, Arrays* arrays,
        gchar** errmsg )
{
    gint rc = 0;
    fz_context* ctx = NULL;
    LineAtPos line_at_pos = { 0 };
    LineAtPos last_line_at_pos = { 0 };
    Ereignis* ereignis = NULL;
    const gchar* rel_path = NULL;
    gint ID_entity_datei = 0;

    ctx = zond_pdf_document_get_ctx( zond_pdf_document );

    //Datei erzeugen oder herausfinden
    rel_path = zond_pdf_document_get_path( zond_pdf_document ) + strlen( zond->dbase_zond->project_dir ) + 1;

    rc = zond_gemini_get_or_create_datei( zond_dbase, rel_path, errmsg );
    if ( rc == -1 ) ERROR_S
    else ID_entity_datei = rc;

    while ( (rc = zond_gemini_get_next_line( zond_pdf_document, &line_at_pos, errmsg )) == 0 )
    {
        gchar* line_string = NULL;

        info_window_set_progress_bar_fraction( info_window, (gdouble)
                line_at_pos.page_num /
                zond_pdf_document_get_arr_pages( zond_pdf_document )->len );

        if ( info_window->cancel )
        {
            zond_gemini_free_ereignis( ereignis );
            return 1;
        }

        line_string = pdf_get_string_from_line( ctx, line_at_pos.line, errmsg );
        if ( !line_string ) ERROR_S

        //Beginn/Abschluß alt
        //"Kopfblock beginnt"
        if ( !g_strcmp0( line_string, "TKÜ-Leitungsnummer:" ) )
        {
            g_clear_pointer( &line_string, g_free );

            if ( ereignis ) //aktuelles Ereignis abschließen
            {
                gint rc = 0;

                rc = zond_gemini_close_ereignis( zond_dbase, arrays, ereignis,
                        ID_entity_datei, last_line_at_pos, errmsg );
                if ( rc ) ERROR_S
            }

            ereignis = g_malloc0( sizeof( Ereignis ) );

            ereignis->page_num_begin = g_strdup_printf( "%i", line_at_pos.page_num );
            ereignis->index_begin = g_strdup_printf( "%i", (gint) line_at_pos.line->bbox.y0 );

            line_string = zond_gemini_get_next_line_string( zond_pdf_document, &line_at_pos, errmsg );
            if ( !line_string )
            {
                zond_gemini_free_ereignis( ereignis );
                ERROR_S
            }

            ereignis->leitungsnr = line_string;

            //2mal vorspulen zur TKÜ-Maßnahme
            for ( gint i = 0; i < 2; i++ )
            {
                gint rc = 0;

                rc = zond_gemini_get_next_line( zond_pdf_document, &line_at_pos, errmsg );
                if ( rc )
                {
                    zond_gemini_free_ereignis( ereignis );
                    if ( rc == 1 ) ERROR_S_MESSAGE( "Gemini malformed" )
                    else ERROR_S
                }
            }

            line_string = pdf_get_string_from_line( ctx, line_at_pos.line, errmsg );
            if ( !line_string )
            {
                zond_gemini_free_ereignis( ereignis );
                ERROR_S
            }

            ereignis->massnahme = line_string;

             //2mal vorspulen zum ÜA
            for ( gint i = 0; i < 2; i++ )
            {
                gint rc = 0;

                rc = zond_gemini_get_next_line( zond_pdf_document, &line_at_pos, errmsg );
                if ( rc )
                {
                    zond_gemini_free_ereignis( ereignis );
                    if ( rc == 1 ) ERROR_S_MESSAGE( "Gemini malformed" )
                    else ERROR_S
                }
            }

            line_string = pdf_get_string_from_line( ctx, line_at_pos.line, errmsg );
            if ( !line_string )
            {
                zond_gemini_free_ereignis( ereignis );
                ERROR_S
            }

            ereignis->ueA = line_string;

            do
            {
                line_string = zond_gemini_get_next_line_string( zond_pdf_document, &line_at_pos, errmsg );
                if ( !line_string )
                {
                    zond_gemini_free_ereignis( ereignis );
                    ERROR_S
                }

                //Balken, nicht "Anschlußinhaber"
                if ( !g_strcmp0( line_string, "." ) )
                {
                    g_clear_pointer( &line_string, g_free );
                    break; //Kopf zu Ende!
                }

                if ( !g_strcmp0( line_string, "Anschlussinhaber:" ) )
                {
                    line_string = zond_gemini_get_next_line_string( zond_pdf_document, &line_at_pos, errmsg );
                    if ( !line_string )
                    {
                        zond_gemini_free_ereignis( ereignis );
                        ERROR_S
                    }

                    ereignis->ueAI = line_string;

                    line_string = zond_gemini_get_next_line_string( zond_pdf_document, &line_at_pos, errmsg );
                    if ( !line_string )
                    {
                        zond_gemini_free_ereignis( ereignis );
                        ERROR_S
                    }
                }

                if ( !g_strcmp0( line_string, "Überwachte Person:" ) )
                {
                    line_string = zond_gemini_get_next_line_string( zond_pdf_document, &line_at_pos, errmsg );
                    if ( !line_string )
                    {
                        zond_gemini_free_ereignis( ereignis );
                        ERROR_S
                    }

                    ereignis->uePerson = line_string;
                }
            } while ( 1 );
        }
        else if ( !g_strcmp0( line_string, "Korrelations-Nr.:" ) )
        {
            g_clear_pointer( &line_string, g_free );

            if ( !ereignis ) ERROR_S_MESSAGE( "Gemini malformed" )

            line_string = zond_gemini_get_next_line_string( zond_pdf_document, &line_at_pos, errmsg );
            if ( !line_string )
            {
                zond_gemini_free_ereignis( ereignis );
                ERROR_S
            }

            ereignis->korrelations_nr = line_string;
        }
        else if ( !g_strcmp0( line_string, "Art:" ) )
        {
            g_clear_pointer( &line_string, g_free );

            if ( !ereignis ) ERROR_S_MESSAGE( "Gemini malformed" )

            line_string = zond_gemini_get_next_line_string( zond_pdf_document, &line_at_pos, errmsg );
            if ( !line_string )
            {
                zond_gemini_free_ereignis( ereignis );
                ERROR_S
            }

            ereignis->art = line_string;
        }
        else if ( !g_strcmp0( line_string, "GIS-Link:" ) )
        {
            g_clear_pointer( &line_string, g_free );

            if ( !ereignis ) ERROR_S_MESSAGE( "Gemini malformed" )

            line_string = zond_gemini_get_next_line_string( zond_pdf_document, &line_at_pos, errmsg );
            if ( !line_string )
            {
                zond_gemini_free_ereignis( ereignis );
                ERROR_S
            }

            ereignis->gis_link = line_string;
        }
        else if ( !g_strcmp0( line_string, "Standort Funkmast ÜA" ) )
        {
            g_clear_pointer( &line_string, g_free );

            if ( !ereignis ) ERROR_S_MESSAGE( "Gemini malformed" )

            do
            {
                line_string = zond_gemini_get_next_line_string( zond_pdf_document, &line_at_pos, errmsg );
                if ( !line_string )
                {
                    zond_gemini_free_ereignis( ereignis );
                    ERROR_S
                }

                if ( g_strcmp0( line_string, "." ) )
                        ereignis->standort_funkmast_uea =
                        add_string( ereignis->standort_funkmast_uea, line_string );
                else break;
            } while ( 1 );

            g_clear_pointer( &line_string, g_free );
        }
        else if ( !g_strcmp0( line_string, "Richtung" ) )
        {
            gfloat y_richtung = 0;

            g_clear_pointer( &line_string, g_free );

            if ( !ereignis ) ERROR_S_MESSAGE( "Gemini malformed" )

            y_richtung = line_at_pos.line->bbox.y1;

            do
            {
                gfloat y = 0.0;
                gfloat x = 0.0;
                LineAtPos line_at_pos_prev = { 0 };

                line_at_pos_prev = line_at_pos; //zwischenspeichern

                line_string = zond_gemini_get_next_line_string( zond_pdf_document, &line_at_pos, errmsg );
                if ( !line_string )
                {
                    zond_gemini_free_ereignis( ereignis );
                    ERROR_S
                }

                y = line_at_pos.line->bbox.y0;
                x = line_at_pos.line->bbox.x0;

                if ( y < y_richtung || y > y_richtung + 8 )
                {
                    //zurückspringen, da schon zu weit...
                    line_at_pos = line_at_pos_prev;
                    break; //und raus
                }

                if ( x > 70 && x < 77 ) ereignis->beginndatum = line_string;
                else if ( x > 168 && x < 175 ) ereignis->beginnzeit = line_string;
                else if ( x > 268 && x < 276 ) ereignis->endedatum = line_string;
                else if ( x > 365 && x < 375 ) ereignis->endezeit = line_string;
                else if ( x > 465 && x < 475 ) ereignis->richtung = line_string;
                else
                {
                    g_clear_pointer( &line_string, g_free );
                    zond_gemini_free_ereignis( ereignis );
                    ERROR_S_MESSAGE( "Gemini malformed" )
                }
            } while ( 1 );
        }
        else if ( !g_strcmp0( line_string, "Partnernummer" ) )
        {
            AnschlussUndAI aai = { 0 };
            LineAtPos line_at_pos_prev = { 0 };

            g_clear_pointer( &line_string, g_free );

            if ( !ereignis ) ERROR_S_MESSAGE( "Gemini malformed" )

            line_string = zond_gemini_get_next_line_string( zond_pdf_document, &line_at_pos, errmsg );
            if ( !line_string )
            {
                zond_gemini_free_ereignis( ereignis );
                ERROR_S
            }

            aai.anschluss = line_string;

            //prüfen, ob Anschlußinhaber direkt hinterher
            //manchmal mehrere AIs zu Partnernummer erfaßt - der letzte scheint der richtige zu sein
            do
            {
                line_at_pos_prev = line_at_pos;

                line_string = zond_gemini_get_next_line_string( zond_pdf_document, &line_at_pos, errmsg );
                if ( !line_string )
                {
                    zond_gemini_free_ereignis( ereignis );
                    ERROR_S
                }

                if ( !g_strcmp0( line_string, "Anschlussinhaber:" ) )
                {
                    g_clear_pointer( &line_string, g_free );

                    //falls schon aus vorheriger Runde AI gespeichert war...
                    g_clear_pointer( &(aai.AI), g_free );

                    line_string = zond_gemini_get_next_line_string( zond_pdf_document, &line_at_pos, errmsg );
                    if ( !line_string )
                    {
                        zond_gemini_free_ereignis( ereignis );
                        ERROR_S
                    }

                    aai.AI = line_string;
                }
                else
                {
                    g_clear_pointer( &line_string, g_free );
                    line_at_pos = line_at_pos_prev;
                    break;
                }
            } while ( 1 );



            //aai ablegen
            //Falls array noch nicht besteht: erzeugen
            if ( !ereignis->partnernr_und_ai )
            {
                ereignis->partnernr_und_ai = g_array_new( FALSE, FALSE, sizeof( AnschlussUndAI ) );
                g_array_set_clear_func( ereignis->partnernr_und_ai, (void (*) (void*)) zond_gemini_free_anschluss_und_ai );
            }

            g_array_append_val( ereignis->partnernr_und_ai, aai );
        }
        else if ( !g_strcmp0( line_string, "Sprecher ÜA") )
        {
            g_clear_pointer( &line_string, g_free );

            if ( !ereignis ) ERROR_S_MESSAGE( "Gemini malformed" )

            line_string = zond_gemini_get_next_line_string( zond_pdf_document, &line_at_pos, errmsg );
            if ( !line_string )
            {
                zond_gemini_free_ereignis( ereignis );
                ERROR_S
            }

            ereignis->sprecher_uea = g_slist_append( ereignis->sprecher_uea, line_string );
        }
        else if ( !g_strcmp0( line_string, "Sprecher PA") )
        {
            g_clear_pointer( &line_string, g_free );

            if ( !ereignis ) ERROR_S_MESSAGE( "Gemini malformed" )

            line_string = zond_gemini_get_next_line_string( zond_pdf_document, &line_at_pos, errmsg );
            if ( !line_string )
            {
                zond_gemini_free_ereignis( ereignis );
                ERROR_S
            }

            ereignis->sprecher_uea = g_slist_append( ereignis->sprecher_pa, line_string );
        }
        else if ( !g_strcmp0( line_string, "verbundene Nummer ÜA" ) )
        {
            AnschlussUndAI aai = { 0 };
            LineAtPos line_at_pos_prev = { 0 };

            g_clear_pointer( &line_string, g_free );

            if ( !ereignis ) ERROR_S_MESSAGE( "Gemini malformed" )

            line_string = zond_gemini_get_next_line_string( zond_pdf_document, &line_at_pos, errmsg );
            if ( !line_string )
            {
                zond_gemini_free_ereignis( ereignis );
                ERROR_S
            }

            aai.anschluss = line_string;

            //prüfen, ob Anschlußinhaber direkt hinterher
            line_at_pos_prev = line_at_pos;
            line_string = zond_gemini_get_next_line_string( zond_pdf_document, &line_at_pos, errmsg );
            if ( !line_string )
            {
                zond_gemini_free_ereignis( ereignis );
                ERROR_S
            }

            if ( !g_strcmp0( line_string, "Anschlussinhaber:" ) )
            {
                g_clear_pointer( &line_string, g_free );

                line_string = zond_gemini_get_next_line_string( zond_pdf_document, &line_at_pos, errmsg );
                if ( !line_string )
                {
                    zond_gemini_free_ereignis( ereignis );
                    ERROR_S
                }

                aai.AI = line_string;
            }
            else line_at_pos = line_at_pos_prev;

            ereignis->verbundene_nr_und_ai = g_new( AnschlussUndAI, 1 );
            *(ereignis->verbundene_nr_und_ai) = aai;
        }
        else g_clear_pointer( &line_string, g_free );

        last_line_at_pos = line_at_pos;
    }
    if ( rc == -1 ) ERROR_S
    else if ( rc == 1 )
    {
        gint rc = 0;

        rc = zond_gemini_close_ereignis( zond_dbase, arrays, ereignis,
                ID_entity_datei, last_line_at_pos, errmsg );
        if ( rc ) ERROR_S
    }

    return 0;
}


static gint
zond_gemini_create_tkue_table( Projekt* zond, gchar** errmsg )
{
    gint rc = 0;
    const gchar* sql =
            "CREATE TABLE IF NOT EXISTS tkue AS "

            "SELECT datei_name, page_begin, index_begin, page_end, index_end, beginn, ID_massnahme, ID_uePerson "
                "FROM "
                "(SELECT tkue_ereignis, beginn "
                    "FROM "
                    "(SELECT entities.ID AS tkue_ereignis "
                        "FROM entities "
                        "WHERE entities.ID_label=1010 OR entities.ID_label=1012 OR entities.ID_label=1015 OR entities.ID_label=1020) "
                    "JOIN "
                    "(SELECT properties.entity_subject AS subject_property, properties.value AS beginn "
                        "FROM "
                        "properties "
                        "JOIN "
                        "(SELECT entities.ID AS property FROM entities WHERE entities.ID_label=10030) "
                        "ON properties.entity_property=property) "
                    "ON tkue_ereignis=subject_property) "
                "JOIN "
                "(SELECT subject_tkue_ereignis, datei_name, page_begin, index_begin, page_end, index_end "
                    "FROM "
                    "(SELECT rels.entity_subject AS subject_tkue_ereignis, rels.entity_object AS object_gemini "
                        "FROM "
                        "rels "
                        "JOIN "
                        "(SELECT entities.ID AS rel FROM entities WHERE entities.ID_label=10010) "
                        "ON rels.entity_rel=rel) /*alle rels _hat_ */ "
                    "JOIN "
                    "(SELECT gemini, datei_name, page_begin, index_begin, page_end, index_end "
                        "FROM "
                        "(SELECT gemini, object_fundstelle "
                            "FROM "
                            "(SELECT entities.ID AS gemini FROM entities WHERE entities.ID_label=836) "
                            "JOIN "
                            "(SELECT rels.entity_subject AS subject_gemini, rels.entity_object AS object_fundstelle "
                                "FROM "
                                "rels "
                                "JOIN "
                                "(SELECT entities.ID AS rel FROM entities WHERE entities.ID_label=10010) "
                                "ON rels.entity_rel=rel) /*alle rels _hat_ */ "
                            "ON gemini=subject_gemini) "
                        "JOIN "
                        "(SELECT fundstelle, datei_name, page_begin, index_begin, page_end, index_end  "
                            "FROM "
                            "(SELECT entities.ID AS fundstelle FROM entities WHERE entities.ID_label=650) "
                            "JOIN "
                            "(SELECT properties.entity_subject AS subject_property_1, properties.value AS page_begin "
                                "FROM "
                                "properties "
                                "JOIN "
                                "(SELECT entities.ID AS property FROM entities WHERE entities.ID_label=11052) "
                                "ON properties.entity_property=property) "
                            "ON fundstelle=subject_property_1 "
                            "JOIN "
                            "(SELECT properties.entity_subject AS subject_property_2, properties.value AS index_begin "
                                "FROM "
                                "properties "
                                "JOIN "
                                "(SELECT entities.ID AS property FROM entities WHERE entities.ID_label=11054) "
                                "ON properties.entity_property=property) "
                            "ON fundstelle=subject_property_2 "
                            "JOIN "
                            "(SELECT properties.entity_subject AS subject_property_3, properties.value AS page_end "
                                "FROM "
                                "properties "
                                "JOIN "
                                "(SELECT entities.ID AS property FROM entities WHERE entities.ID_label=11055) "
                                "ON properties.entity_property=property) "
                            "ON fundstelle=subject_property_3 "
                            "JOIN "
                            "(SELECT properties.entity_subject AS subject_property_4, properties.value AS index_end "
                                "FROM "
                                "properties "
                                "JOIN "
                                "(SELECT entities.ID AS property FROM entities WHERE entities.ID_label=11057) "
                                "ON properties.entity_property=property) "
                            "ON fundstelle=subject_property_4 "
                            "JOIN "
                            "(SELECT rels.entity_subject AS subject_fundstelle, rels.entity_object AS object_datei "
                                "FROM "
                                "rels "
                                "JOIN "
                                "(SELECT entities.ID AS rel FROM entities WHERE entities.ID_label=10000) "
                                "ON rels.entity_rel=rel) "
                            "ON fundstelle=subject_fundstelle "
                            "JOIN "
                            "(SELECT entities.ID AS datei FROM entities WHERE entities.ID_label=660) "
                            "ON datei=object_datei "
                            "JOIN "
                            "(SELECT properties.entity_subject AS subject_property, properties.value AS datei_name "
                                "FROM "
                                "properties "
                                "JOIN "
                                "(SELECT entities.ID AS property FROM entities WHERE entities.ID_label=10100) "
                                "ON properties.entity_property=property) "
                            "ON datei=subject_property) "
                        "ON fundstelle=object_fundstelle) "
                    "ON gemini=object_gemini) "
                "ON tkue_ereignis=subject_tkue_ereignis "
                "JOIN /* mit massnahme!*/ "
                "(SELECT subject_tkue_ereignis AS subject_tkue_ereignis_ii, ID_massnahme, ID_uePerson "
                    "FROM "
                    "(SELECT rels.entity_subject AS subject_tkue_ereignis, rels.entity_object AS object_massnahme "
                        "FROM "
                        "rels "
                        "JOIN "
                        "(SELECT entities.ID AS rel FROM entities WHERE entities.ID_label=10000) "
                        "ON rels.entity_rel=rel) "
                    "JOIN "
                    "(SELECT entities.ID AS ID_massnahme FROM entities WHERE entities.ID_label=1000) "
                    "ON ID_massnahme=object_massnahme "
                    "LEFT JOIN "
                    "(SELECT ID_uePerson, subject_massnahme "
                        "FROM "
                        "(SELECT rels.entity_subject AS subject_massnahme, rels.entity_object AS object_uePerson "
                            "FROM "
                            "rels "
                            "JOIN "
                            "(SELECT entities.ID AS rel FROM entities WHERE entities.ID_label=10010) "
                            "ON rels.entity_rel=rel) "
                        "JOIN "
                        "(SELECT entities.ID AS ID_uePerson "
                            "FROM entities "
                            " WHERE entities.ID_label=310 OR entities.ID_label=312 OR entities.ID_label=313 OR entities.ID_label=314) "
                        "ON ID_ueperson=object_uePerson) "
                    "ON ID_massnahme=subject_massnahme) "
                "ON tkue_ereignis=subject_tkue_ereignis_ii "
                "ORDER BY beginn ASC "
                ";";

    rc = sqlite3_exec( zond_dbase_get_dbase( zond->dbase_zond->zond_dbase_work ),
            "DROP TABLE IF EXISTS tkue;", NULL, NULL, errmsg );
    if ( rc != SQLITE_OK )
    {
        gchar* text = NULL;

        if ( errmsg )
        {
            text = g_strconcat( "Bei Aufruf ", __func__, ":\nBei Aufruf sqlite3_exec:\n", NULL );
            *errmsg = add_string( text, *errmsg );
        }

        return -1;
    }

    rc = sqlite3_exec( zond_dbase_get_dbase( zond->dbase_zond->zond_dbase_work ), sql, NULL, NULL, errmsg );
    if ( rc != SQLITE_OK )
    {
        gchar* text = NULL;

        if ( errmsg )
        {
            text = g_strconcat( "Bei Aufruf ", __func__, ":\nBei Aufruf sqlite3_exec:\n", NULL );
            *errmsg = add_string( text, *errmsg );
        }

        return -1;
    }

    return 0;
}


gint
zond_gemini_read_gemini( Projekt* zond, gchar** errmsg )
{
    gint rc = 0;
    ZondPdfDocument* zond_pdf_document = NULL;
    ZondDBase* zond_dbase = NULL;
    InfoWindow* info_window = NULL;
    Arrays arrays = { 0 };
    GSList* list = NULL;
    GSList* list_ptr = NULL;

    list = choose_files( zond->app_window, NULL,
            "Gemini-Protokoll auswählen", "Einlesen",
            GTK_FILE_CHOOSER_ACTION_OPEN, NULL, TRUE );
    if ( !list ) return 0;

    list_ptr = list;

    rc = zond_dbase_new( ":memory:", TRUE, TRUE, &zond_dbase, errmsg );
    if ( rc )
    {
        g_slist_free_full( list, g_free );
        ERROR_S
    }

    rc = zond_dbase_backup( zond->dbase_zond->zond_dbase_work, zond_dbase, errmsg );
    if ( rc )
    {
        zond_dbase_close( zond_dbase );
        g_slist_free_full( list, g_free );
        ERROR_S
    }

    rc = sond_database_add_to_database( zond_dbase, errmsg );
    if ( rc )
    {
        zond_dbase_close( zond_dbase );
        g_slist_free_full( list, g_free );
        ERROR_S
    }

    info_window = zond_info_window_open( zond, "Einlesen Gemnini-Datei" );

    arrays.arr_anschluesse = g_array_new( FALSE, FALSE, sizeof( Anschluss ) );
    g_array_set_clear_func( arrays.arr_anschluesse, (GDestroyNotify) zond_gemini_free_anschluss );

    arrays.arr_personen = g_array_new( FALSE, FALSE, sizeof( Person ) );
    g_array_set_clear_func( arrays.arr_personen, (GDestroyNotify) zond_gemini_free_person );

    arrays.arr_massnahmen = g_array_new( FALSE, FALSE, sizeof( Massnahme ) );
    g_array_set_clear_func( arrays.arr_massnahmen, (GDestroyNotify) zond_gemini_free_massnahme );

    do
    {
        gchar* uri = NULL;

        uri = g_uri_unescape_string( list_ptr->data, NULL );

        info_window_set_message( info_window, uri + 8 );
        info_window_set_progress_bar( info_window );

        zond_pdf_document = zond_pdf_document_open( uri + 8, 0,
                -1, errmsg );
        if ( !zond_pdf_document )
        {
            info_window_set_message( info_window, "Öffnen nicht möglich:" );
            info_window_set_message( info_window, *errmsg );

            g_free( uri );
            g_clear_pointer( errmsg, g_free );
            continue;
        }

        //in memory-database, die inhaltlich mit ...work identisch ist, einfügen
        rc = zond_gemini_read_zond_pdf_document( zond, info_window, zond_dbase,
                zond_pdf_document, &arrays, errmsg );
        g_free( uri );
        g_object_unref( zond_pdf_document );
        if ( rc == -1 )
        {
            info_window_set_message( info_window, "Vollständiges Einlesen nicht möglich:" );
            info_window_set_message( info_window, *errmsg );

            g_clear_pointer( errmsg, g_free );
            continue;
        }
        else if ( rc == 1 ) break;
    } while ( (list_ptr = list_ptr->next) );

    g_slist_free_full( list, g_free );
    g_array_unref( arrays.arr_massnahmen );
    g_array_unref( arrays.arr_anschluesse );
    g_array_unref( arrays.arr_personen );
    zond_info_window_close( zond, info_window );
    sqlite3_stmt* stmt = NULL;
    while ( (stmt = sqlite3_next_stmt( zond_dbase_get_dbase( zond->dbase_zond->zond_dbase_work ), stmt )) ) sqlite3_reset( stmt );

    rc = zond_dbase_backup( zond_dbase, zond->dbase_zond->zond_dbase_work, errmsg );
    zond_dbase_close( zond_dbase );
    if ( rc ) ERROR_S

    rc = zond_gemini_create_tkue_table( zond, errmsg );
    if ( rc ) ERROR_S

    //da backup, wird Änderung nicht verzeichnet
    if ( !zond->dbase_zond->changed ) project_set_changed( zond );

    return 0;
}


static gint
zond_gemini_select_fill_checkbox_ue_person( Projekt* zond, GtkWidget* scb,
        gchar** errmsg )
{
    gint rc = 0;
    GArray* arr_ID_entity_ue_person = NULL;
    gint ID_label_ue_person = 0;

    //sammeln
    rc = sond_database_get_objects_from_labels( zond->dbase_zond->zond_dbase_work,
            1000, _HAT_, 310, &arr_ID_entity_ue_person, errmsg );
    if ( rc ) ERROR_S

    for ( gint i = 0; i < arr_ID_entity_ue_person->len; i++ )
    {
        gint rc = 0;
        gint ID_entity_ue_person = 0;
        gchar* name = NULL;
        gchar* vorname = NULL;
        gchar* geb_datum = NULL;
        gchar* text = NULL;

        ID_entity_ue_person = g_array_index( arr_ID_entity_ue_person, gint, i );

        rc = sond_database_get_first_property_value_for_subject( zond->dbase_zond->zond_dbase_work,
                ID_entity_ue_person, _NAME_, &name, errmsg );
        if ( rc )
        {
            g_array_unref( arr_ID_entity_ue_person );
            if ( rc == -1 ) ERROR_S
            else if ( rc == 1 ) ERROR_S_MESSAGE( "Keinen Namen gefunden" )
        }

        rc = sond_database_get_first_property_value_for_subject( zond->dbase_zond->zond_dbase_work,
                ID_entity_ue_person, _VORNAME_, &vorname, errmsg );
        if ( rc == -1 )
        {
            g_array_unref( arr_ID_entity_ue_person );
            g_free( name );
            ERROR_S
        }

        rc = sond_database_get_first_property_value_for_subject( zond->dbase_zond->zond_dbase_work,
                ID_entity_ue_person, _BEGINN_, &geb_datum, errmsg );
        if ( rc == -1 )
        {
            g_array_unref( arr_ID_entity_ue_person );
            g_free( name );
            g_free( vorname );
            ERROR_S
        }

        text = g_strconcat( name,", ", vorname, ", ", geb_datum, NULL );
        g_free( name );
        g_free( vorname );
        g_free( geb_datum );

        rc = sond_database_get_ID_label_for_entity( zond->dbase_zond->zond_dbase_work, ID_entity_ue_person,
                errmsg );
        if ( rc == -1 )
        {
            g_array_unref( arr_ID_entity_ue_person );
            ERROR_S
        }
        else ID_label_ue_person = rc;

        if ( ID_label_ue_person == MANN ) text = add_string( text, g_strdup( " (männlich)" ) );
        if ( ID_label_ue_person == FRAU ) text = add_string( text, g_strdup( " (weiblich)" ) );
        if ( ID_label_ue_person == DIVERS ) text = add_string( text, g_strdup( " (divers)" ) );

        sond_checkbox_add_entry( SOND_CHECKBOX(scb), text, ID_entity_ue_person );
        g_free( text );
    }
    g_array_unref( arr_ID_entity_ue_person );

//    sond_checkbox_sort_entries( SOND_CHECKBOX(scb), 0 );

    return 0;
}


gint
zond_gemini_select( Projekt* zond, gchar** errmsg )
{
    gint rc = 0;
    GtkWidget* gemini = NULL;
    GtkWidget* box = NULL;
    GtkWidget* grid = NULL;
    GtkWidget* checkbox_uePerson = NULL;
    GtkWidget* checkbox_leitungsnr = NULL;
    GPtrArray* arr_leitungs_nrn = NULL;
    GArray* arr_ID_entity_massnahmen = NULL;
    GtkWidget* box_entries = NULL;
    GtkWidget* entry_von = NULL;
    GtkWidget* entry_bis = NULL;

    gemini = gtk_dialog_new_with_buttons( "Gemini", GTK_WINDOW(zond->app_window),
            GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL, "Ok",
            GTK_RESPONSE_OK, "Abbrechen", GTK_RESPONSE_CANCEL, NULL );
    gtk_widget_set_vexpand( gemini, FALSE );

    box = gtk_dialog_get_content_area( GTK_DIALOG(gemini) );

    grid = gtk_grid_new( );
    gtk_box_pack_start( GTK_BOX(box), grid, FALSE, FALSE, 0 );

    //überwachte Personen
    checkbox_uePerson = sond_checkbox_new( "Überwachte Person" );
    gtk_grid_attach( GTK_GRID(grid), checkbox_uePerson, 0, 0, 1, 1 );

    rc = zond_gemini_select_fill_checkbox_ue_person( zond, checkbox_uePerson,
            errmsg );
    if ( rc )
    {
        gtk_widget_destroy( gemini );
        ERROR_S
    }

    //Leitungsnrn.
    //checkbox erzeugen und ins grid
    checkbox_leitungsnr = sond_checkbox_new( "Leitungs-Nr." );
    gtk_grid_attach( GTK_GRID(grid), checkbox_leitungsnr, 1, 0, 1, 1 );

    box_entries = gtk_box_new( GTK_ORIENTATION_VERTICAL, 0 );
    gtk_widget_set_valign( box_entries, GTK_ALIGN_START );
    gtk_grid_attach( GTK_GRID(grid), box_entries, 2, 0, 1, 1 );

    entry_von = gtk_entry_new( );
    gtk_entry_set_placeholder_text( GTK_ENTRY(entry_von), "Beginn" );
    gtk_box_pack_start( GTK_BOX(box_entries), entry_von, FALSE, FALSE, 0 );

    entry_bis = gtk_entry_new( );
    gtk_entry_set_placeholder_text( GTK_ENTRY(entry_bis), "Ende" );
    gtk_box_pack_start( GTK_BOX(box_entries), entry_bis, FALSE, FALSE, 0 );

    //Leitungsnrn. sammeln
    rc = sond_database_get_subject_and_first_property_value_for_labels( zond->dbase_zond->zond_dbase_work,
            1000, 11059, &arr_ID_entity_massnahmen, &arr_leitungs_nrn, errmsg );
    if ( rc == -1 ) ERROR_S

    //einfügen in checkbox
    for ( gint i = 0; i < arr_leitungs_nrn->len; i++ )
    {
        gchar* leitungs_nr = NULL;
        gint ID_entity_massnahme = 0;

        leitungs_nr = g_ptr_array_index( arr_leitungs_nrn, i );
        ID_entity_massnahme = g_array_index( arr_ID_entity_massnahmen, gint, i );

        sond_checkbox_add_entry( SOND_CHECKBOX(checkbox_leitungsnr), leitungs_nr, ID_entity_massnahme );
    }

    g_array_unref( arr_ID_entity_massnahmen );
    g_ptr_array_unref( arr_leitungs_nrn );
/*
    g_signal_connect( checkbox_leitungsnr, "alle-toggled",
                     G_CALLBACK(zond_gemini_leitungsnr_alle_toggled), gemini );

    g_signal_connect( checkbox_leitungsnr, "entry-toggled",
                     G_CALLBACK(zond_gemini_leitungsnr_entry_toggled), gemini );
*/

    gtk_widget_grab_focus( gtk_dialog_get_widget_for_response( GTK_DIALOG(gemini), GTK_RESPONSE_CANCEL ) );

    gtk_widget_show_all( gemini );

    rc = gtk_dialog_run( GTK_DIALOG(gemini) );

    if ( rc == GTK_RESPONSE_OK )
    {
        gint rc = 0;
        gchar* sql = NULL;
        GArray* arr_leitungsnrn = NULL;
        GArray* arr_ue_personen = NULL;
        sqlite3_stmt* stmt = NULL;
        DisplayedDocument* dd = NULL;
        DisplayedDocument* dd_act = NULL;
        PdfViewer* pdfv = NULL;
        const gchar* beginn = NULL;
        const gchar* ende = NULL;

        //Grund-SQL-String
        sql = g_strdup( "SELECT datei_name, page_begin, index_begin, page_end, index_end, beginn FROM "
                "tkue WHERE "
                "ID_massnahme=-1 " ); //alles erstmal ausschließen...

        //leitungs_nr auslesen
        arr_leitungsnrn = sond_checkbox_get_active_IDs( SOND_CHECKBOX(checkbox_leitungsnr) );
        if ( arr_leitungsnrn )
        {
            for ( gint i = 0; i < arr_leitungsnrn->len; i++ )
            {
                gint ID_entity_massnahme = 0;
                gchar* text = NULL;

                ID_entity_massnahme = g_array_index( arr_leitungsnrn, gint, i );
                text = g_strdup_printf( "OR ID_massnahme=%i ", ID_entity_massnahme );
                sql = add_string( sql, text );
            }
            g_array_unref( arr_leitungsnrn );
        }

        //uePersonen
        arr_ue_personen = sond_checkbox_get_active_IDs( SOND_CHECKBOX(checkbox_uePerson ) );
        if ( arr_ue_personen )
        {
            for ( gint i = 0; i < arr_ue_personen->len; i++ )
            {
                gint ID_entity_ue_person = 0;
                gchar* text = NULL;

                ID_entity_ue_person = g_array_index( arr_ue_personen, gint, i );
                text = g_strdup_printf( "OR ID_uePerson=%i ", ID_entity_ue_person );
                sql = add_string( sql, text );
            }
            g_array_unref( arr_ue_personen );
        }

        //Anfang und Ende
        beginn = gtk_entry_get_text( GTK_ENTRY(entry_von) );
        ende = gtk_entry_get_text( GTK_ENTRY(entry_bis) );

        if ( g_strcmp0( beginn, "" ) )
        {
            gchar* text = NULL;

            text = g_strdup_printf( "AND beginn>='%s' ", beginn );
            sql = add_string( sql, text );
        }

        if ( g_strcmp0( ende, "" ) )
        {
            gchar* text = NULL;

            text = g_strdup_printf( "AND beginn<='%s' ", ende );
            sql = add_string( sql, text );
        }

        gtk_widget_destroy( gemini );

        sql = add_string( sql, g_strdup( "ORDER BY beginn ASC;" ) );

        //Abfrage in temporärer Tabelle
        rc = sqlite3_prepare_v2( zond_dbase_get_dbase( zond->dbase_zond->zond_dbase_work ),
                sql, -1, &stmt, NULL );
        g_free( sql );
        if ( rc != SQLITE_OK )
        {
            sqlite3_finalize( stmt );
            if ( errmsg ) *errmsg = add_string( g_strconcat( "Bei Aufruf ",
                        __func__, ":\nBei Aufruf sqlite3_prepare_v2:\n",
                       sqlite3_errmsg( zond_dbase_get_dbase( zond->dbase_zond->zond_dbase_work ) ),
                        NULL ), *errmsg );

            return -1;
        }

        do
        {
            const gchar* datei = NULL;
            gint page_begin = 0;
            gint index_begin = 0;
            gint page_end = 0;
            gint index_end = 0;
            DisplayedDocument* dd_new = 0;

            rc = sqlite3_step( stmt );
            if ( rc != SQLITE_DONE && rc != SQLITE_ROW )
            {
                sqlite3_finalize( stmt );
                if ( errmsg ) *errmsg = add_string( g_strconcat( "Bei Aufruf ",
                            __func__, ":\nBei Aufruf sqlite3_prepare_v2:\n",
                           sqlite3_errmsg( zond_dbase_get_dbase( zond->dbase_zond->zond_dbase_work ) ),
                            NULL ), *errmsg );

                return -1;
            }

            if ( rc == SQLITE_DONE ) continue;

            datei = (const gchar*) sqlite3_column_text( stmt, 0 );
            page_begin = atoi( (const gchar*) sqlite3_column_text( stmt, 1 ) );
            index_begin = atoi( (const gchar*) sqlite3_column_text( stmt, 2 ) );
            page_end = atoi( (const gchar*) sqlite3_column_text( stmt, 3 ) );
            index_end = atoi( (const gchar*) sqlite3_column_text( stmt, 4 ) );

            if ( dd_act && !g_strcmp0( zond_pdf_document_get_path(
                    dd_act->zond_pdf_document ), datei ) &&
                    ((dd_act->anbindung->bis.seite == page_begin &&
                    dd_act->anbindung->bis.index < index_begin &&
                    dd_act->anbindung->bis.index + 100 > index_begin) ||
                    (dd_act->anbindung->bis.seite + 1 == page_begin &&
                    index_begin < 100)) ) //schließt sich unmittelbar an
            {
                if ( page_end > dd_act->anbindung->bis.seite )
                {
                    ZondPdfDocument* zpd = NULL;

                    zpd = zond_pdf_document_open( zond_pdf_document_get_path(
                            dd_act->zond_pdf_document ),
                            dd_act->anbindung->bis.seite + 1, page_end, errmsg );
                    if ( !zpd ) ERROR_S
                    g_object_unref( zpd ); //reference schon in dd_act!
                }
                dd_act->anbindung->bis.seite = page_end;
                dd_act->anbindung->bis.index = index_end;
            }
            else
            {
                Anbindung anbindung = { 0 };

                anbindung.von.seite = page_begin;
                anbindung.von.index = index_begin;
                anbindung.bis.seite = page_end;
                anbindung.bis.index = index_end;

                dd_new = document_new_displayed_document( datei, &anbindung, errmsg );
                if ( !dd_new )
                {
                    document_free_displayed_documents( dd );
                    sqlite3_finalize( stmt );
                    ERROR_S
                }

                if ( !dd )
                {
                    dd = dd_new;
                    dd_act = dd_new;
                }
                else
                {
                    dd_act->next = dd_new;
                    dd_act = dd_act->next;
                }
            }
        } while ( rc == SQLITE_ROW );

        sqlite3_finalize( stmt );

        if ( dd )
        {
            pdfv = viewer_start_pv( zond );
            viewer_display_document( pdfv, dd, 0, 0 );
        }
        else display_message( zond->app_window, "Keine Treffer", NULL );
    }
    else gtk_widget_destroy( gemini );

    return 0;
}
