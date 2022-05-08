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

#include <mupdf/fitz.h>

#include "global_types.h"
#include "../misc.h"
#include "../sond_database.h"
#include "20allgemein/pdf_text.h"
#include "99conv/pdf.h"

#include "zond_pdf_document.h"


typedef struct _Line_At_Pos
{
    fz_stext_line* line;
    fz_stext_block* block;
    fz_stext_page* page;
    gint page_num;
} LineAtPos;


typedef struct _Ereignis
{
    gint page_num_begin;
    gint index_begin;

    gchar* leitungsnr;
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

    gint page_num_end;
    gint index_end;
} Ereignis;


static void
zond_gemini_free_ereignis( Ereignis* ereignis )
{
    g_free( ereignis->leitungsnr ); //
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
        time[11] = zeit[6];
        time[12] = zeit[7];
        time[13] = ':';
        time[14] = zeit[3];
        time[15] = zeit[4];
        time[16] = ':';
        time[17] = zeit[0];
        time[18] = zeit[1];
    }

    return g_strdup( time );
}


static gint
zond_gemini_save_ereignis( Projekt* zond, Ereignis* ereignis, gchar** errmsg )
{
    GArray* arr_ID_TKUE_ereignisse = NULL;
    gint rc = 0;
    gint ID_entity_TKUE_massnahme = 0;
    gint ID_entity_TKUE_ereignis = 0;
    gint ID_label_ereignis = 0;

    if ( !ereignis || !ereignis->leitungsnr ) return 0;

    //TKÜ-Maßnahme ermitteln bzw. eintragen
    rc = sond_database_get_entities_for_property( zond->dbase_zond->zond_dbase_work,
            11059, ereignis->leitungsnr, &arr_ID_TKUE_ereignisse, errmsg );
    if ( rc == -1 ) ERROR_S
    else if ( rc > 1 )
    {
        g_array_unref( arr_ID_TKUE_ereignisse );
        ERROR_S_MESSAGE( "Mehrere TKÜ-Maßnahmen zur Leitungsnr. gespeichert" )
    }
    else if ( rc == 0 ) //"neue" TKÜ-Maßnahme
    {
        gint rc = 0;

        g_array_unref( arr_ID_TKUE_ereignisse );

        rc = sond_database_insert_entity( zond->dbase_zond->zond_dbase_work, 1000, errmsg );
        if ( rc == -1 ) ERROR_S
        else ID_entity_TKUE_massnahme = rc;

        rc = sond_database_insert_property( zond->dbase_zond->zond_dbase_work,
                ID_entity_TKUE_massnahme, 11059, ereignis->leitungsnr, errmsg );
        if ( rc == -1 ) ERROR_S

        if ( ereignis->ueA )
        {
            gint rc = 0;
            rc = sond_database_insert_property(
                    zond->dbase_zond->zond_dbase_work, ID_entity_TKUE_massnahme,
                    11060, ereignis->ueA, errmsg );
            if ( rc == -1 ) ERROR_S
        }

        if ( ereignis->ueAI )
        {
            gint rc = 0;
            rc = sond_database_insert_property(
                    zond->dbase_zond->zond_dbase_work, ID_entity_TKUE_massnahme,
                    11061, ereignis->ueAI, errmsg );
            if ( rc == -1 ) ERROR_S
        }

        if ( ereignis->uePerson )
        {
            gint rc = 0;
            rc = sond_database_insert_property(
                    zond->dbase_zond->zond_dbase_work, ID_entity_TKUE_massnahme,
                    11062, ereignis->uePerson, errmsg );
            if ( rc == -1 ) ERROR_S
        }
    }
    else if ( rc == 1 )
    {
        ID_entity_TKUE_massnahme = g_array_index( arr_ID_TKUE_ereignisse, gint, 0 );
        g_array_unref( arr_ID_TKUE_ereignisse );
    }

    //TKÜ-Ereignis Art ermitteln
    if ( !g_strcmp0( ereignis->art, "Gespräch (Audio)" ) ) ID_label_ereignis = 1012;
    else if ( !g_strcmp0( ereignis->art, "Textnachrichtenaustausch" ) )
            ID_label_ereignis = 1015;
    else if ( !g_strcmp0( ereignis->art, "Multimedianachrichtenaustausch" ) )
            ID_label_ereignis = 1020;
    else ID_label_ereignis = 1010;

    //Ereignis eintragen
    rc = sond_database_insert_entity( zond->dbase_zond->zond_dbase_work,
            ID_label_ereignis, errmsg );
    if ( rc == -1 ) ERROR_S
    else ID_entity_TKUE_ereignis = rc;

    //Zuordnung zu Maßnahme: Gespräch _gehört zu_ Maßnahme
    rc = sond_database_insert_rel( zond->dbase_zond->zond_dbase_work,
            ID_entity_TKUE_ereignis, 10000, ID_entity_TKUE_massnahme, errmsg );
    if ( rc == -1 ) ERROR_S

    //Properties eintragen
    if ( ereignis->korrelations_nr )
    {
        gint rc = 0;

        rc = sond_database_insert_property( zond->dbase_zond->zond_dbase_work,
                ID_entity_TKUE_ereignis, 11065, ereignis->korrelations_nr, errmsg );
        if ( rc == -1 ) ERROR_S
    }

    if ( ereignis->art )
    {
        gint rc = 0;

        rc = sond_database_insert_property( zond->dbase_zond->zond_dbase_work,
                ID_entity_TKUE_ereignis, 11067, ereignis->art, errmsg );
        if ( rc == -1 ) ERROR_S
    }

    if ( ereignis->gis_link )
    {
        gint rc = 0;

        rc = sond_database_insert_property( zond->dbase_zond->zond_dbase_work,
                ID_entity_TKUE_ereignis, 11070, ereignis->gis_link, errmsg );
        if ( rc == -1 ) ERROR_S
    }

    if ( ereignis->standort_funkmast_uea )
    {
        gint rc = 0;

        rc = sond_database_insert_property( zond->dbase_zond->zond_dbase_work,
                ID_entity_TKUE_ereignis, 11072, ereignis->standort_funkmast_uea, errmsg );
        if ( rc == -1 ) ERROR_S
    }

    if ( ereignis->beginndatum )
    {
        gint rc = 0;
        gchar* time = NULL;

        time = zond_gemini_format_time( ereignis->beginndatum, ereignis->beginnzeit, errmsg );
        if ( !time ) ERROR_S

        rc = sond_database_insert_property( zond->dbase_zond->zond_dbase_work,
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

        rc = sond_database_insert_property( zond->dbase_zond->zond_dbase_work,
                ID_entity_TKUE_ereignis, 10040, time, errmsg );
        if ( rc == -1 ) ERROR_S

        g_free( time );
    }

    if ( ereignis->richtung )
    {
        gint rc = 0;

        rc = sond_database_insert_property( zond->dbase_zond->zond_dbase_work,
                ID_entity_TKUE_ereignis, 11075, ereignis->richtung, errmsg );
        if ( rc == -1 ) ERROR_S
    }


    return 0;
}


static gint
zond_gemini_close_ereignis( Projekt* zond, Ereignis* ereignis,
        LineAtPos last_line_at_pos, gchar** errmsg )
{
    gint rc = 0;

    if ( !ereignis ) return 0;

    ereignis->page_num_end = last_line_at_pos.page_num;
    ereignis->index_end = (gint) last_line_at_pos.line->bbox.y1;

    rc = zond_gemini_save_ereignis( zond, ereignis, errmsg );
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
zond_gemini_read_zond_pdf_document( Projekt* zond, ZondPdfDocument* zond_pdf_document, gchar** errmsg )
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

                rc = zond_gemini_close_ereignis( zond, ereignis,
                        last_line_at_pos, errmsg );
                if ( rc ) ERROR_S
            }

            ereignis = g_malloc0( sizeof( Ereignis ) );

            ereignis->page_num_begin = line_at_pos.page_num;
            ereignis->index_begin = (gint) line_at_pos.line->bbox.y0;

            line_string = zond_gemini_get_next_line_string( zond_pdf_document, &line_at_pos, errmsg );
            if ( !line_string )
            {
                zond_gemini_free_ereignis( ereignis );
                ERROR_S
            }

            ereignis->leitungsnr = line_string;

            //4mal vorspulen zum überwachten Anschluß
            for ( gint i = 0; i < 4; i++ )
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
            else //"Anschlußinhaber"
            {
                line_string = zond_gemini_get_next_line_string( zond_pdf_document, &line_at_pos, errmsg );
                if ( !line_string )
                {
                    zond_gemini_free_ereignis( ereignis );
                    ERROR_S
                }

                ereignis->ueAI = line_string;

                //zwei weiter, zu "überwachter Person"
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

                ereignis->uePerson = line_string;


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
        else if ( !g_strcmp0( line_string, "") )
        {

        }
        else g_clear_pointer( &line_string, g_free );

        last_line_at_pos = line_at_pos;
    }
    if ( rc == -1 ) ERROR_S
    else if ( rc == 1 )
    {
        gint rc = 0;

        rc = zond_gemini_close_ereignis( zond, ereignis, last_line_at_pos, errmsg );
        if ( rc ) ERROR_S
    }

    return 0;
}


gint
zond_gemini_read_gemini( Projekt* zond, gchar** errmsg )
{
    gint rc = 0;
    ZondPdfDocument* zond_pdf_document = NULL;
    gchar* file = NULL;

    file = filename_oeffnen( GTK_WINDOW(zond->app_window) );
    if ( !file ) return 0;

    zond_pdf_document = zond_pdf_document_open( file, 0, -1, errmsg );
    g_free( file );
    if ( !zond_pdf_document ) ERROR_S

    rc = sond_database_add_to_database( zond->dbase_zond->zond_dbase_work, errmsg );
    if ( rc ) ERROR_S

    rc = zond_gemini_read_zond_pdf_document( zond, zond_pdf_document, errmsg );
    if ( rc )
    {
        g_object_unref( zond_pdf_document );
        ERROR_S
    }

    return 0;
}


