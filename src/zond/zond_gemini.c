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
    g_free( ereignis->leitungsnr );
    g_free( ereignis->ueA );
    g_free( ereignis->ueAI );
    g_free( ereignis->uePerson );
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


static gint
zond_gemini_save_ereignis( Projekt* zond, Ereignis* ereignis, gchar** errmsg )
{
    GArray* arr_ID_TKUE_ereignisse = NULL;
    gint rc = 0;
    gint ID_entity_TKUE_massnahme = 0;
    gint ID_entity_TKUE_ereignis = 0;
    gint ID_label_ereignis = 0;

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
            g_clear_pointer( &line_string, g_free );

            if ( !ereignis ) ERROR_S_MESSAGE( "Gemini malformed" )

            line_string = zond_gemini_get_next_line_string( zond_pdf_document, &line_at_pos, errmsg );
            if ( !line_string )
            {
                zond_gemini_free_ereignis( ereignis );
                ERROR_S
            }

            ereignis->beginndatum = line_string;

            line_string = zond_gemini_get_next_line_string( zond_pdf_document, &line_at_pos, errmsg );
            if ( !line_string )
            {
                zond_gemini_free_ereignis( ereignis );
                ERROR_S
            }

            ereignis->beginnzeit= line_string;

            line_string = zond_gemini_get_next_line_string( zond_pdf_document, &line_at_pos, errmsg );
            if ( !line_string )
            {
                zond_gemini_free_ereignis( ereignis );
                ERROR_S
            }

            ereignis->endedatum = line_string;

            line_string = zond_gemini_get_next_line_string( zond_pdf_document, &line_at_pos, errmsg );
            if ( !line_string )
            {
                zond_gemini_free_ereignis( ereignis );
                ERROR_S
            }

            ereignis->endezeit = line_string;

            line_string = zond_gemini_get_next_line_string( zond_pdf_document, &line_at_pos, errmsg );
            if ( !line_string )
            {
                zond_gemini_free_ereignis( ereignis );
                ERROR_S
            }

            ereignis->richtung = line_string;
        }
        else if ( !g_strcmp0( line_string, "Anschlußinhaber:" ) )
        {
            g_clear_pointer( &line_string, g_free );

            if ( !ereignis ) ERROR_S_MESSAGE( "Gemini malformed" )

            line_string = zond_gemini_get_next_line_string( zond_pdf_document, &line_at_pos, errmsg );
            if ( !line_string )
            {
                zond_gemini_free_ereignis( ereignis );
                ERROR_S
            }

            ereignis->ueAI = line_string;
        }
        else if ( !g_strcmp0( line_string, "Überwachte Person:" ) )
        {
            g_clear_pointer( &line_string, g_free );

            if ( !ereignis ) ERROR_S_MESSAGE( "Gemini malformed" )

            line_string = zond_gemini_get_next_line_string( zond_pdf_document, &line_at_pos, errmsg );
            if ( !line_string )
            {
                zond_gemini_free_ereignis( ereignis );
                ERROR_S
            }

            ereignis->uePerson = line_string;
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


