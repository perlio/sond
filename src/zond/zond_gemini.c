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
    const gchar* pfad; //muß nicht freed werden; geborgter Zeiger aus zond_pdf_document
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
zond_gemini_get_or_create_person( gpointer database, const gchar* person, gchar** errmsg )
{
    GArray* arr_entities = NULL;
    gint rc = 0;
    gchar** ueai = NULL;
    gchar* name = NULL;
    gchar* vorname = NULL;
    gchar* geb_datum = NULL;
    gchar* geb_datum_formatted = NULL;
    gchar* geschlecht = NULL;
    gint ID_entity_person = 0;

    //ueAI normalisieren
    ueai = g_strsplit( person, ",", -1 );
    if ( ueai[0] )
    {
        name = g_strchug( ueai[0] );
        if ( ueai[1] )
        {
            vorname = g_strchug( ueai[1] );
            if ( ueai[2] )
            {
                geb_datum = g_strchug( ueai[2] );
                if ( ueai[3] ) geschlecht = g_strchug( ueai[3] );
            }
        }
    }

    if ( geb_datum ) geb_datum_formatted = zond_gemini_format_time( geb_datum, NULL, errmsg );

    // if ( Person == ueAI vorhanden ) ID_entity herausfinden
    rc = sond_database_get_entities_for_properties_and( database, &arr_entities,
            errmsg, 10100, name, 11010, vorname, 10030, geb_datum_formatted, -1 );
    if ( rc )
    {
        g_strfreev( ueai );
        g_free( geb_datum_formatted );
        ERROR_S
    }

    if ( arr_entities->len > 1 )
    {
        //mehrere mit gleichem Namen, Vornamen und GebDatum
        //was tun?
        g_array_unref( arr_entities );
        g_strfreev( ueai );
        g_free( geb_datum_formatted );
        ERROR_S_MESSAGE( "Mehrere gleiche Leute?!" )
        //später vielleicht anzeigen und auswählen lassen?!
    }
    else if ( arr_entities->len == 1 )
    {
        ID_entity_person = g_array_index( arr_entities, gint, 0 );
        g_array_unref( arr_entities );
    }
    else if ( arr_entities->len == 0 )
    {
        gint rc = 0;
        gint ID_label_person = 0;

        g_array_unref( arr_entities );

        if ( !geschlecht ) ID_label_person = 310;
        else if ( !g_strcmp0( geschlecht, "männlich" ) ) ID_label_person = 312;
        else if ( !g_strcmp0( geschlecht, "weiblich" ) ) ID_label_person = 313;
        else ID_label_person = 314; //divers!

        rc = sond_database_insert_entity( database, ID_label_person, errmsg );
        if ( rc == -1 )
        {
            g_strfreev( ueai );
            g_free( geb_datum_formatted );
            ERROR_S
        }

        ID_entity_person = rc;

        rc = sond_database_insert_property( database, ID_entity_person, 10100, name, errmsg );
        if ( rc == -1 )
        {
            g_strfreev( ueai );
            g_free( geb_datum_formatted );
            ERROR_S
        }

        //wird abgefragt, ob vorname == NULL - > dann wird nix eingefügt...
        rc = sond_database_insert_property( database, ID_entity_person, 11010, vorname, errmsg );
        if ( rc == -1 )
        {
            g_strfreev( ueai );
            g_free( geb_datum_formatted );
            ERROR_S
        }

        rc = sond_database_insert_property( database, ID_entity_person, 10030, geb_datum_formatted, errmsg );
        if ( rc == -1 )
        {
            g_strfreev( ueai );
            g_free( geb_datum_formatted );
            ERROR_S
        }
    }

    g_strfreev( ueai );
    g_free( geb_datum_formatted );

    return ID_entity_person;
}


static gint
zond_gemini_get_or_create_anschluss( gpointer database, gint ID_label_anschluss, const gchar* anschluss, const gchar* AI, gchar** errmsg )
{
    gint rc = 0;
    GArray* arr_ID_anschluesse = NULL;
    gint ID_entity_anschluss = 0;

    //Name == anschluss
    rc = sond_database_get_entities_for_property( database,
            10100, anschluss, &arr_ID_anschluesse, errmsg );
    if ( rc == -1 ) ERROR_S
    else if ( rc > 1 )
    {
        g_array_unref( arr_ID_anschluesse );
        ERROR_S_MESSAGE( "Mehrere Anschlüsse zur Rufnr./IMSI/IMEI gespeichert" )
    }
    else if ( rc == 0 ) //noch nicht vorhanden -> einfügen
    {
        gint rc = 0;

        g_array_unref( arr_ID_anschluesse );

        //ÜA-entity einfügen
        rc = sond_database_insert_entity( database, ID_label_anschluss, errmsg );
        if ( rc == -1 ) ERROR_S

        ID_entity_anschluss = rc;

        //als "Name" ÜA eintragen
        rc = sond_database_insert_property( database, ID_entity_anschluss, 10100,
                anschluss, errmsg );
        if ( rc == -1 ) ERROR_S

        //Falls AI zum ÜA gelesen werden konnte...
        if ( AI )
        {
            gint rc = 0;
            gint ID_entity_ai = 0;
            rc = zond_gemini_get_or_create_person( database, AI, errmsg );
            if ( rc == -1 ) ERROR_S

            ID_entity_ai = rc;

            //rel ueAI - "Anschluß" einfügen
            rc = sond_database_insert_rel( database, ID_entity_anschluss, 10010,
                    ID_entity_ai, errmsg );
            if ( rc == -1 ) ERROR_S
        }
    }
    else if ( rc == 1 )
    {
        ID_entity_anschluss = g_array_index( arr_ID_anschluesse, gint, 0 );
        g_array_unref( arr_ID_anschluesse );
    }

    return ID_entity_anschluss;
}


static gint
zond_gemini_save_ereignis( gpointer database, Ereignis* ereignis, gchar** errmsg )
{
    GArray* arr_ID_tkue_massnahmen = NULL;
    gint rc = 0;
    gint ID_entity_TKUE_massnahme = 0;
    gint ID_entity_TKUE_ereignis = 0;
    gint ID_label_ereignis = 0;
    gint ID_entity_gemini_urkunde = 0;
    gint ID_entity_fundstelle = 0;

    if ( !ereignis || !ereignis->leitungsnr ) return 0;

    //TKÜ-Maßnahme ermitteln bzw. eintragen
    rc = sond_database_get_entities_for_property( database,
            11059, ereignis->leitungsnr, &arr_ID_tkue_massnahmen, errmsg );
    if ( rc == -1 ) ERROR_S
    else if ( rc > 1 )
    {
        g_array_unref( arr_ID_tkue_massnahmen );
        ERROR_S_MESSAGE( "Mehrere TKÜ-Maßnahmen zur Leitungsnr. gespeichert" )
    }
    else if ( rc == 0 ) //"neue" TKÜ-Maßnahme
    {
        gint rc = 0;
        gint ID_entity_anschluss = 0;
        gint ID_label_anschluss = 0;

        g_array_unref( arr_ID_tkue_massnahmen );

        //TKÜ-Maßnahme = 1000
        rc = sond_database_insert_entity( database, 1000, errmsg );
        if ( rc == -1 ) ERROR_S
        else ID_entity_TKUE_massnahme = rc;

        //Leitungsnummer eintragen
        rc = sond_database_insert_property( database,
                ID_entity_TKUE_massnahme, 11059, ereignis->leitungsnr, errmsg );
        if ( rc == -1 ) ERROR_S

        //Verbindung zu überwachtem Anschluß/Endgerät
        if ( g_str_has_suffix( ereignis->massnahme, "GSM" ) ) ID_label_anschluss = 425;
        else if ( g_str_has_suffix( ereignis->massnahme, "IMSI" ) ) ID_label_anschluss = 435;
        else if ( g_str_has_suffix( ereignis->massnahme, "IMEI" ) ) ID_label_anschluss = 437;
        else ID_label_anschluss = 425; //wenn nix, wohl Telefon

        rc = zond_gemini_get_or_create_anschluss( database,
                ID_label_anschluss, ereignis->ueA, ereignis->ueAI, errmsg );
        if ( rc == -1 ) ERROR_S
        else ID_entity_anschluss = rc;

        //rel zwischen ueA und TKÜ-Maßnahme erzeugen
        rc = sond_database_insert_rel( database,
                ID_entity_TKUE_massnahme, 10010, ID_entity_anschluss, errmsg );
        if ( rc == -1 ) ERROR_S

        if ( ereignis->uePerson )
        {
            gint rc = 0;
            gint ID_entity_ue_person = 0;

            rc = zond_gemini_get_or_create_person( database, ereignis->uePerson, errmsg );
            if ( rc == -1 ) ERROR_S

            ID_entity_ue_person = rc;

            //rel TKÜ-Maßnahme - uePerson einfügen
            rc = sond_database_insert_rel( database,
                    ID_entity_TKUE_massnahme, 10010, ID_entity_ue_person, errmsg );
            if ( rc == -1 ) ERROR_S
        }
    }
    else if ( rc == 1 )
    {
        ID_entity_TKUE_massnahme = g_array_index( arr_ID_tkue_massnahmen, gint, 0 );
        g_array_unref( arr_ID_tkue_massnahmen );

        //ggf. überprüfen, ob alle Angaben (uePerson/AI) vorhanden und ggf. ergänzen
    }

    //TKÜ-Ereignis Art ermitteln
    if ( !g_strcmp0( ereignis->art, "Gespräch (Audio)" ) ) ID_label_ereignis = 1012;
    else if ( !g_strcmp0( ereignis->art, "Textnachrichtenaustausch" ) )
            ID_label_ereignis = 1015;
    else if ( !g_strcmp0( ereignis->art, "Multimedianachrichtenaustausch" ) )
            ID_label_ereignis = 1020;
    else ID_label_ereignis = 1010;

    //Ereignis eintragen
    rc = sond_database_insert_entity( database,
            ID_label_ereignis, errmsg );
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
            gint ID_entity_person = 0;

            rc = zond_gemini_get_or_create_person( database, ereignis->sprecher_uea->data, errmsg );
            if ( rc == -1 ) ERROR_S

            ID_entity_person = rc;

            //rel TKÜ-Maßnahme - uePerson einfügen
            rc = sond_database_insert_rel( database,
                    ID_entity_TKUE_ereignis, 12000, ID_entity_person, errmsg );
            if ( rc == -1 ) ERROR_S
        } while ( (ereignis->sprecher_uea = ereignis->sprecher_uea->next) );
    }

    if ( ereignis->sprecher_pa )
    {
        do
        {
            gint rc = 0;
            gint ID_entity_person = 0;

            rc = zond_gemini_get_or_create_person( database, ereignis->sprecher_pa->data, errmsg );
            if ( rc == -1 ) ERROR_S

            ID_entity_person = rc;

            //rel TKÜ-Maßnahme - uePerson einfügen
            rc = sond_database_insert_rel( database,
                    ID_entity_TKUE_ereignis, 12005, ID_entity_person, errmsg );
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
            gint ID_entity_anschluss = 0;

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

            rc = zond_gemini_get_or_create_anschluss(
                    database, 425, aai.anschluss, aai.AI, errmsg );
            if ( rc == -1 ) ERROR_S

            ID_entity_anschluss = rc;

            //rel zwischen ueA und TKÜ-Maßnahme erzeugen
            rc = sond_database_insert_rel( database,
                    ID_entity_TKUE_ereignis, 10010, ID_entity_anschluss, errmsg );
            if ( rc == -1 ) ERROR_S
        }
    }

    if ( ereignis->verbundene_nr_und_ai )
    {
        gint rc = 0;
        gchar* ptr_fill = NULL;
        gchar* ptr_read = NULL;
        gint ID_entity_anschluss = 0;

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

        rc = zond_gemini_get_or_create_anschluss(
                database, 425,
                ereignis->verbundene_nr_und_ai->anschluss,
                ereignis->verbundene_nr_und_ai->AI, errmsg );
        if ( rc == -1 ) ERROR_S

        ID_entity_anschluss = rc;

        //rel TKÜ-Ereignis und Anschluß verb Nr erzeugen
        rc = sond_database_insert_rel( database,
                ID_entity_TKUE_ereignis, 10010, ID_entity_anschluss, errmsg );
        if ( rc == -1 ) ERROR_S
    }

    //Gemini-Urkunde eintragen
    rc = sond_database_insert_entity( database, 836, errmsg );
    if ( rc == -1 ) ERROR_S

    ID_entity_gemini_urkunde = rc;

    rc = sond_database_insert_rel( database,
            ID_entity_TKUE_ereignis, 10010, ID_entity_gemini_urkunde, errmsg );
    if ( rc == -1 ) ERROR_S

    //Fundstelle zur Gemini-Urkunde
    rc = sond_database_insert_entity( database, 650, errmsg );
    if ( rc == -1 ) ERROR_S

    ID_entity_fundstelle = rc;

    //rel zu Urkunde
    rc = sond_database_insert_rel( database,
            ID_entity_gemini_urkunde, 10010, ID_entity_fundstelle, errmsg );
    if ( rc == -1 ) ERROR_S

    //properties zur Fundstelle
    rc = sond_database_insert_property( database,
            ID_entity_fundstelle, 11051, ereignis->pfad, errmsg );
    if ( rc == -1 ) ERROR_S

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
zond_gemini_close_ereignis( gpointer database, Ereignis* ereignis,
        LineAtPos last_line_at_pos, gchar** errmsg )
{
    gint rc = 0;

    if ( !ereignis ) return 0;

    ereignis->page_num_end = g_strdup_printf( "%i", last_line_at_pos.page_num );
    ereignis->index_end = g_strdup_printf("%i", (gint) last_line_at_pos.line->bbox.y1 );

    rc = zond_gemini_save_ereignis( database, ereignis, errmsg );
    if ( rc )
    {
        printf("page/index (begin): %s/%s  (end): %s/%s\n", ereignis->page_num_begin, ereignis->index_begin, ereignis->page_num_end, ereignis->index_end);
        g_clear_pointer( &ereignis, zond_gemini_free_ereignis );
        ERROR_S
    }
    g_clear_pointer( &ereignis, zond_gemini_free_ereignis );

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
zond_gemini_read_zond_pdf_document( gpointer database, ZondPdfDocument* zond_pdf_document, gchar** errmsg )
{
    gint rc = 0;
    fz_context* ctx = NULL;
    LineAtPos line_at_pos = { 0 };
    LineAtPos last_line_at_pos = { 0 };
    Ereignis* ereignis = NULL;

    ctx = zond_pdf_document_get_ctx( zond_pdf_document );

    while ( (rc = zond_gemini_get_next_line( zond_pdf_document, &line_at_pos, errmsg )) == 0 )
    {
        gchar* line_string = NULL;

        line_string = pdf_get_string_from_line( ctx, line_at_pos.line, errmsg );
        if ( !line_string ) ERROR_S

        //Beginn/Abschluß alt
        if ( !g_strcmp0( line_string, "TKÜ-Leitungsnummer:" ) )
        {
            g_clear_pointer( &line_string, g_free );

            if ( ereignis ) //aktuelles Ereignis abschließen
            {
                gint rc = 0;

                ereignis->pfad = zond_pdf_document_get_path( zond_pdf_document );

                rc = zond_gemini_close_ereignis( database, ereignis,
                        last_line_at_pos, errmsg );
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

            line_string = zond_gemini_get_next_line_string( zond_pdf_document, &line_at_pos, errmsg );
            if ( !line_string )
            {
                zond_gemini_free_ereignis( ereignis );
                ERROR_S
            }

            //Balken, nicht "Anschlußinhaber"
            if ( !g_strcmp0( line_string, "." ) ) g_clear_pointer( &line_string, g_free );
            else
            {
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
            }
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

        rc = zond_gemini_close_ereignis( database, ereignis, last_line_at_pos, errmsg );
        if ( rc ) ERROR_S
    }

    return 0;
}


static gint
zond_gemini_copy_back_memory_database( ZondDBase* zdb_memory, ZondDBase* zdb_work,
        gchar** errmsg )
{
    gint rc = 0;
    sqlite3* db_memory = NULL;
    const gchar* path_work = NULL;
    gchar* sql = NULL;
    sqlite3_stmt* stmt = NULL;

    sqlite3* db_work = NULL;

    db_work = zond_dbase_get_dbase( zdb_work );

    db_memory = zond_dbase_get_dbase( zdb_memory );
    path_work = zond_dbase_get_path( zdb_work );

    while ( (stmt = sqlite3_next_stmt( db_work, NULL )) ) sqlite3_finalize( stmt );

    sql = g_strdup_printf( "ATTACH '%s' AS work; "
            "INSERT OR IGNORE INTO work.entities SELECT * FROM main.entities; "
            "INSERT OR IGNORE INTO work.rels SELECT * FROM main.rels; "
            "INSERT OR IGNORE INTO work.properties SELECT * FROM main.properties; "
            "DETACH work; ", path_work );
    rc = sqlite3_exec( db_memory, sql, NULL, NULL, errmsg );
    g_free( sql );
    if ( rc ) ERROR_S

    return 0;
}


gint
zond_gemini_read_gemini( Projekt* zond, gchar** errmsg )
{
    gint rc = 0;
    ZondPdfDocument* zond_pdf_document = NULL;
    gchar* file = NULL;
    ZondDBase* zond_dbase = NULL;

    file = filename_oeffnen( GTK_WINDOW(zond->app_window) );
    if ( !file ) return 0;

    zond_pdf_document = zond_pdf_document_open( file, 0, -1, errmsg );
    g_free( file );
    if ( !zond_pdf_document ) ERROR_S

    rc = zond_dbase_new( ":memory:", TRUE, TRUE, &zond_dbase, errmsg );
    if ( rc )
    {
        g_object_unref( zond_pdf_document );
        ERROR_S
    }

    rc = sond_database_add_to_database( zond->dbase_zond->zond_dbase_work, errmsg );
    if ( rc )
    {
        zond_dbase_close( zond_dbase );
        g_object_unref( zond_pdf_document );
        ERROR_S
    }

    rc = zond_dbase_backup( zond->dbase_zond->zond_dbase_work, zond_dbase, errmsg );
    if ( rc )
    {
        g_object_unref( zond_pdf_document );
        zond_dbase_close( zond_dbase );
        ERROR_S
    }

    //in memory-database, die inhaltlich mit ...work identisch ist, einfügen
    rc = zond_gemini_read_zond_pdf_document( zond_dbase, zond_pdf_document, errmsg );
    g_object_unref( zond_pdf_document );
    if ( rc )
    {
        zond_dbase_close( zond_dbase );
        ERROR_S
    }

    //zurückkopieren und schließen
    rc = zond_gemini_copy_back_memory_database( zond_dbase, zond->dbase_zond->zond_dbase_work, errmsg );
    zond_dbase_close( zond_dbase );
    if ( rc ) ERROR_S

    return 0;
}


static Fundstelle*
zond_gemini_get_fundstelle( Projekt* zond, gint ID_entity_tkue_ereignis, gchar** errmsg )
{
    gint rc = 0;
    Fundstelle* fundstelle = NULL;
    GArray* arr_objects = NULL;
    gchar* pfad = NULL;
    gchar* page_begin = NULL;
    gchar* index_begin = NULL;
    gchar* page_end = NULL;
    gchar* index_end = NULL;
    gint ID_entity_fundstelle = 0;

    rc = sond_database_get_ID_label_for_entity( zond->dbase_zond->zond_dbase_work, ID_entity_tkue_ereignis, errmsg );
    if ( rc == -1 ) ERROR_S_VAL( NULL )

    rc = sond_database_label_is_equal_or_parent( zond->dbase_zond->zond_dbase_work,
            1010, rc, errmsg );
    if ( rc == -1 ) ERROR_S_VAL( NULL )
    else if ( rc == 1 ) ERROR_S_MESSAGE_VAL( "Keine TKÜ-Maßnahme", NULL )

    rc = sond_database_get_object_for_subject( zond->dbase_zond->zond_dbase_work,
            ID_entity_tkue_ereignis, &arr_objects, errmsg, 10010, 836, 10010, 650, -1 );
    if ( rc == -1 ) ERROR_S_VAL( NULL )

    if ( arr_objects->len == 0 )
    {
        g_array_unref( arr_objects );
        ERROR_S_MESSAGE_VAL( "Kein Objekt gefunden", NULL )
    }

    ID_entity_fundstelle = g_array_index( arr_objects, gint, 0 );

    g_array_unref( arr_objects );

    rc = sond_database_get_first_property_value_for_subject(
            zond->dbase_zond->zond_dbase_work, ID_entity_fundstelle, 11051,
            &pfad, errmsg );
    if ( rc == -1 ) ERROR_S_VAL( NULL )
    else if ( rc == 1 ) ERROR_S_MESSAGE_VAL( "Kein Pfad gefunden", NULL )

    rc = sond_database_get_first_property_value_for_subject(
            zond->dbase_zond->zond_dbase_work, ID_entity_fundstelle, 11052,
            &page_begin, errmsg );
    if ( rc == -1 ) ERROR_S_VAL( NULL )
    else if ( rc == 1 ) ERROR_S_MESSAGE_VAL( "Keine Anfangssseite gefunden", NULL )

    rc = sond_database_get_first_property_value_for_subject(
            zond->dbase_zond->zond_dbase_work, ID_entity_fundstelle, 11054,
            &index_begin, errmsg );
    if ( rc == -1 ) ERROR_S_VAL( NULL )
    else if ( rc == 1 ) ERROR_S_MESSAGE_VAL( "Keinen Anfangsindex gefunden", NULL )

    rc = sond_database_get_first_property_value_for_subject(
            zond->dbase_zond->zond_dbase_work, ID_entity_fundstelle, 11055,
            &page_end, errmsg );
    if ( rc == -1 ) ERROR_S_VAL( NULL )
    else if ( rc == 1 ) ERROR_S_MESSAGE_VAL( "Keine Endseite gefunden", NULL )

    rc = sond_database_get_first_property_value_for_subject(
            zond->dbase_zond->zond_dbase_work, ID_entity_fundstelle, 11057,
            &index_end, errmsg );
    if ( rc == -1 ) ERROR_S_VAL( NULL )
    else if ( rc == 1 ) ERROR_S_MESSAGE_VAL( "Keinen Endindex gefunden", NULL )

    fundstelle = g_new( Fundstelle, 1 );
    fundstelle->dateipfad = pfad;
    fundstelle->anbindung.von.seite = atoi( page_begin );
    fundstelle->anbindung.von.index = atoi( index_begin );
    fundstelle->anbindung.bis.seite = atoi( page_end );
    fundstelle->anbindung.bis.index = atoi( index_end );

    return fundstelle;
}


gint
zond_gemini_select( Projekt* zond, gchar** errmsg )
{
    gint rc = 0;
    GtkWidget* gemini = NULL;
    GtkWidget* box = NULL;
    GtkWidget* grid = NULL;
    GtkWidget* checkbox = NULL;
    GArray* arr_ID_massnahmen = NULL;

    gemini = gtk_dialog_new_with_buttons( "Gemini", GTK_WINDOW(zond->app_window),
            GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL, "Ok",
            GTK_RESPONSE_OK, "Abbrechen", GTK_RESPONSE_CANCEL, NULL );

    box = gtk_dialog_get_content_area( GTK_DIALOG(gemini) );

    grid = gtk_grid_new( );
    checkbox = sond_checkbox_new( );
    gtk_grid_attach( GTK_GRID(grid), checkbox, 0, 0, 1, 1 );
    gtk_box_pack_start( GTK_BOX(box), grid, TRUE, TRUE, 0 );

    //Leitungsnrn. sammeln
    rc = sond_database_get_entities_for_label( zond->dbase_zond->zond_dbase_work,
            1000, &arr_ID_massnahmen, errmsg );
    if ( rc == -1 ) ERROR_S

    for ( gint i = 0; i < arr_ID_massnahmen->len; i++ )
    {
        gint ID_entity_massnahme = 0;
        gchar* leitungs_nr = NULL;

        ID_entity_massnahme = g_array_index( arr_ID_massnahmen, gint, i );

        rc = sond_database_get_first_property_value_for_subject(
                zond->dbase_zond->zond_dbase_work, ID_entity_massnahme, 11059,
                &leitungs_nr, errmsg );
        if ( rc == -1 )
        {
            g_array_unref( arr_ID_massnahmen );
            ERROR_S
        }

        sond_checkbox_add_entry( SOND_CHECKBOX(checkbox), leitungs_nr, ID_entity_massnahme );
    }

    g_array_unref( arr_ID_massnahmen );

    gtk_widget_show_all( gemini );

    rc = gtk_dialog_run( GTK_DIALOG(gemini) );

    return 0;
}
