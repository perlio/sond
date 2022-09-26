/*
zond (pdf_ocr.c) - Akten, Beweisstücke, Unterlagen
Copyright (C) 2020  pelo america

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
#include <mupdf/pdf.h>
#include <gtk/gtk.h>
#include <tesseract/capi.h>
#include <allheaders.h>
#include <glib/gstdio.h>

#include "zond_pdf_document.h"

#include "../misc.h"

//#include "40viewer/document.h"

#include "99conv/pdf.h"
#include "99conv/general.h"

#ifdef _WIN32
#include <errhandlingapi.h>
#include <libloaderapi.h>
#elif defined( __linux__ )
#include <string.h>
#include <errno.h>
#endif // _WIN32

#define TESS_SCALE 5

static gint
pdf_ocr_update_content_stream( fz_context* ctx, pdf_obj* page_ref,
        fz_buffer* buf, gchar** errmsg )
{
    pdf_document* doc = NULL;
    pdf_obj* contents_dict = NULL;
    pdf_obj* ind = NULL;

    doc = pdf_get_bound_document( ctx, page_ref );

    fz_var( contents_dict );
    fz_var( ind );
    fz_try( ctx )
    {
        pdf_dict_del( ctx, page_ref, PDF_NAME(Contents) );
        contents_dict = pdf_new_dict( ctx, doc, 2 );
        gint num = pdf_create_object( ctx, doc );
        pdf_update_object( ctx, doc, num, contents_dict );
        ind = pdf_new_indirect( ctx, doc, num, 0 );
        pdf_dict_put( ctx, page_ref, PDF_NAME(Contents), ind );
        pdf_update_stream( ctx, doc, ind, buf, 0 );
    }
    fz_always( ctx )
    {
        pdf_drop_obj( ctx, contents_dict );
        pdf_drop_obj( ctx, ind );
    }
    fz_catch( ctx ) ERROR_MUPDF( "update stream" )

    return 0;
}


static fz_buffer*
pdf_ocr_get_content_stream_as_buffer( fz_context* ctx, pdf_obj* page_ref,
        gchar** errmsg )
{
    pdf_obj* obj_contents = NULL;
    fz_stream* stream = NULL;
    fz_buffer* buf = NULL;

    //Stream doc_text
    obj_contents = pdf_dict_get( ctx, page_ref, PDF_NAME(Contents) );

    fz_try( ctx )
    {
        stream = pdf_open_contents_stream( ctx, pdf_get_bound_document( ctx, page_ref ), obj_contents );
        buf = fz_read_all( ctx, stream, 1024 );
    }
    fz_always( ctx ) fz_drop_stream( ctx, stream );
    fz_catch( ctx ) ERROR_MUPDF_R( "open and read stream", NULL )

    return buf;
}


typedef struct _Zond_Token
{
    pdf_token tok;
    union
    {
        gint i;
        gfloat f;
        gchar* s;
        GByteArray* gba;
    };
} ZondToken;

#define PDF_TOK_INLINE_STREAM 99

static void
pdf_ocr_free_zond_token( gpointer data )
{
    ZondToken* zond_token = (ZondToken*) data;

    if ( zond_token->tok == PDF_TOK_KEYWORD || zond_token->tok == PDF_TOK_NAME )
            g_free( zond_token->s );
    else if ( zond_token->tok == (pdf_token) PDF_TOK_INLINE_STREAM || zond_token->tok == PDF_TOK_STRING ) g_byte_array_unref( zond_token->gba );

    return;
}

static void
pdf_ocr_insert_keyword_in_stream( GArray* arr_zond_token, const gchar* keyword, gint index )
{
    ZondToken zond_token;

    zond_token.tok = PDF_TOK_KEYWORD;
    zond_token.s = g_strdup( keyword );

    g_array_insert_val( arr_zond_token, index, zond_token );

    return;
}


static void
pdf_ocr_invalidate_token( GArray* arr_zond_token, gint index, gint len )
{
    for ( gint u = index; u > index - len; u-- )
    {
        g_array_index(arr_zond_token, ZondToken, u ).tok = PDF_NUM_TOKENS;
    }

    return;
}


static fz_buffer*
pdf_ocr_reassemble_buffer( fz_context* ctx, GArray* arr_zond_token, gchar** errmsg )
{
    fz_buffer* fzbuf = NULL;

    fz_try( ctx ) fzbuf = fz_new_buffer( ctx, 1012 );
    fz_catch( ctx ) ERROR_MUPDF_R( "fz_new_buffer", NULL )

    for ( gint i = 0; i < arr_zond_token->len; i++ )
    {
        guint8* s;
        gchar oct[4];

        ZondToken zond_token = g_array_index( arr_zond_token, ZondToken, i );

        switch (zond_token.tok)
        {
            case PDF_NUM_TOKENS:
                break;
            case PDF_TOK_EOF:
             //   fz_append_byte( ctx, fzbuf, 0 );
                break;
            case PDF_TOK_NAME:
                fz_append_printf(ctx, fzbuf, "/%s ", zond_token.s);
                break;
            case PDF_TOK_STRING:

                fz_append_byte( ctx, fzbuf, '(' );
                s = zond_token.gba->data;

                while ( s < (zond_token.gba->data + zond_token.gba->len - 1 ) )
                {
                    if ( *s < 32 )
                    {
                        switch (*s)
                        {
                            case '\n':
                                fz_append_byte( ctx, fzbuf, '\\' );
                                fz_append_byte( ctx, fzbuf, 'n' );
                                break;
                            case '\r':
                                fz_append_byte( ctx, fzbuf, '\\' );
                                fz_append_byte( ctx, fzbuf, 'r' );
                                break;
                            case '\t':
                                fz_append_byte( ctx, fzbuf, '\\' );
                                fz_append_byte( ctx, fzbuf, 't' );
                                break;
                            case '\b':
                                fz_append_byte( ctx, fzbuf, '\\' );
                                fz_append_byte( ctx, fzbuf, 'b' );
                                break;
                            case '\f':
                                fz_append_byte( ctx, fzbuf, '\\' );
                                fz_append_byte( ctx, fzbuf, 'f' );
                                break;
                            default:
                                fz_append_byte( ctx, fzbuf, '\\' );

                                g_snprintf( oct, 4, "%03o", *s );
                                fz_append_data( ctx, fzbuf, (void*) oct, 3 );
                        }
                    }
                    else
                    {
                        switch( *s )
                        {
                            case '(':
                                fz_append_byte( ctx, fzbuf, '\\' );
                                fz_append_byte( ctx, fzbuf, '(' );
                                break;
                            case ')':
                                fz_append_byte( ctx, fzbuf, '\\' );
                                fz_append_byte( ctx, fzbuf, ')' );
                                break;
                            case '\\':
                                fz_append_byte( ctx, fzbuf, '\\' );
                                fz_append_byte( ctx, fzbuf, '\\' );
                                break;
                            default:
                                fz_append_byte( ctx, fzbuf, *s );
                        }
                    }

                    s++;
                }
                fz_append_byte( ctx, fzbuf, ')' );
                break;
            case PDF_TOK_OPEN_DICT:
                fz_append_string(ctx, fzbuf, "<<");
                break;
            case PDF_TOK_CLOSE_DICT:
                fz_append_string(ctx, fzbuf, ">>");
                break;
            case PDF_TOK_OPEN_ARRAY:
                fz_append_byte(ctx, fzbuf, '[');
                break;
            case PDF_TOK_CLOSE_ARRAY:
                fz_append_byte(ctx, fzbuf, ']');
                break;
            case PDF_TOK_OPEN_BRACE:
                fz_append_byte(ctx, fzbuf, '{');
                break;
            case PDF_TOK_CLOSE_BRACE:
                fz_append_byte(ctx, fzbuf, '}');
                break;
            case PDF_TOK_INT:
                fz_append_printf(ctx, fzbuf, "%ld ", zond_token.i);
                break;
            case PDF_TOK_REAL:
                fz_append_printf(ctx, fzbuf, "%g ", zond_token.f);
                break;
            case PDF_TOK_KEYWORD:
                fz_append_printf(ctx, fzbuf, "%s \n", zond_token.s);
                break;
            case PDF_TOK_INLINE_STREAM:
                fz_append_byte( ctx, fzbuf, 0 );
                fz_append_data(ctx, fzbuf, zond_token.gba->data, zond_token.gba->len );
                fz_append_byte( ctx, fzbuf, '>' );
                fz_append_byte( ctx, fzbuf, ' ' );
                break;
            case PDF_TOK_TRUE:
                fz_append_printf(ctx, fzbuf, "true " );
                break;
            case PDF_TOK_FALSE:
                fz_append_printf(ctx, fzbuf, "false " );
                break;
            case PDF_TOK_NULL:
                fz_append_printf(ctx, fzbuf, "null " );
                break;
            default: /* isregular: !isdelim && !iswhite && c != EOF */
                break;
        }
    }

    return fzbuf;
}


struct GraphicsState
{
    gint begin;
    gint Tr_act;
    gint q;
    GArray* arr_cm;
    struct
    {
        gint Tc;
        gint Tw;
        gint Tz;
        gint TL;
        gint Tf;
        gint Tr;
        gint Ts;
    };
};


//immer "spezielle" states (cm, q)
//1: Text
static void
pdf_ocr_reset_gs( GPtrArray* arr_gs, gint flags )
{
    gint idx = arr_gs->len - 1;

    do
    {
        struct GraphicsState* GS_act = g_ptr_array_index( arr_gs, idx );

        if ( GS_act->q >= GS_act->begin ) GS_act->q = -1; //ist natürlich nur beim letzten der Fall
        for ( gint i = GS_act->arr_cm->len - 1; i >= 0; i-- )
        {
            if ( g_array_index( GS_act->arr_cm, gint, i ) >= GS_act->begin ) g_array_index( GS_act->arr_cm, gint, i ) = -1;
        }

        //TextState
        if ( flags & 1 )
        {
            if ( GS_act->Tc >= GS_act->begin ) GS_act->Tc = -1;
            if ( GS_act->Tw >= GS_act->begin ) GS_act->Tw = -1;
            if ( GS_act->Tz >= GS_act->begin ) GS_act->Tz = -1;
            if ( GS_act->TL >= GS_act->begin ) GS_act->TL = -1;
            if ( GS_act->Tf >= GS_act->begin ) GS_act->Tf = -1;
            if ( GS_act->Tr >= GS_act->begin ) GS_act->Tr = -1;
            if ( GS_act->Ts >= GS_act->begin ) GS_act->Ts = -1;
        }

        idx--;
    } while ( idx >= 0 );

    return;
}


static struct GraphicsState*
pdf_ocr_restore_gs( GPtrArray* arr_gs )
{
    g_ptr_array_remove_index( arr_gs, arr_gs->len - 1 );

    struct GraphicsState* GS = g_ptr_array_index( arr_gs, arr_gs->len - 1 );

    return GS;
}


static struct GraphicsState*
pdf_ocr_store_gs( GPtrArray* arr_gs, gint i )
{
    struct GraphicsState* GS_act = g_ptr_array_index( arr_gs, arr_gs->len - 1 );

    //alte Werte übernehmen
    struct GraphicsState* GS_new = g_memdup2( GS_act, sizeof( struct GraphicsState ) );

    //nur Beginn (q) aktuell
    GS_new->begin = i;
    GS_new->q = GS_new->begin;

    GS_new->arr_cm = g_array_copy( GS_act->arr_cm );

    g_ptr_array_add( arr_gs, GS_new );

    return GS_new;
}


static void
pdf_ocr_free_gs( gpointer data )
{
    struct GraphicsState* GS = (struct GraphicsState*) data;

    g_array_unref( GS->arr_cm );

    g_free( GS );

    return;
}



static GArray*
pdf_ocr_get_cleaned_tokens( fz_context* ctx, fz_stream* stream, gint flags, gchar** errmsg )
{
    GArray* arr_zond_token = NULL;
    gint idx = -1; //damit idx immer aktueller Stand des arrays ist

    GPtrArray* arr_gs = g_ptr_array_new( );
    g_ptr_array_set_free_func( arr_gs, pdf_ocr_free_gs );

    //Ausgangs-GS
    struct GraphicsState* GS_act = g_malloc0( sizeof ( struct GraphicsState ) );
    GS_act->q = -1;
    GS_act->arr_cm = g_array_new( FALSE, FALSE, sizeof( gint ) );
    GS_act->Tc = -1;
    GS_act->Tw = -1;
    GS_act->Tz = -1;
    GS_act->TL = -1;
    GS_act->Tf = -1;
    GS_act->Tr = -1;
    GS_act->Ts = -1;
    g_ptr_array_add( arr_gs, GS_act );

    struct BT
    {
        gint BT;

        gint Td;
        gint TD;
        gint Tm;
        gint TAst;
    } BT = { -1, -1, -1, -1, -1 };

    gint BI = -1;

    GArray* arr_marked_content = g_array_new( FALSE, FALSE, sizeof( gint ) );

    arr_zond_token = g_array_new( FALSE, FALSE, sizeof( ZondToken ) );
    g_array_set_clear_func( arr_zond_token, pdf_ocr_free_zond_token );

    pdf_token tok = PDF_TOK_NULL;

    //1. Durchgang: einlesen und prüfen
    while ( tok != PDF_TOK_EOF )
    {
        ZondToken zond_token = { 0, };
        pdf_lexbuf lxb;

        pdf_lexbuf_init( ctx, &lxb, PDF_LEXBUF_SMALL );

        tok = pdf_lex( ctx, stream, &lxb );
        zond_token.tok = tok;

        if ( tok == PDF_TOK_REAL ) zond_token.f = lxb.f;
        else if ( tok == PDF_TOK_INT ) zond_token.i = lxb.i;
        else if ( tok == PDF_TOK_NAME || tok == PDF_TOK_KEYWORD )
                zond_token.s = g_strdup( lxb.scratch );
        else if ( tok == PDF_TOK_STRING )
        {
            guint8 byte = 0;

            zond_token.gba = g_byte_array_new( );
            g_byte_array_append( zond_token.gba, (guint8*) lxb.scratch, lxb.len );
            g_byte_array_append( zond_token.gba, &byte, 1);
        }

        pdf_lexbuf_fin( ctx, &lxb );

        g_array_append_val( arr_zond_token, zond_token );
        idx++;

        if ( tok == PDF_TOK_KEYWORD )
        {
            if ( !g_strcmp0( zond_token.s, "q" ) ) GS_act = pdf_ocr_store_gs( arr_gs, idx );
            else if ( !g_strcmp0( zond_token.s, "cm" ) ) g_array_append_val( GS_act->arr_cm, idx );
            else if ( !g_strcmp0( zond_token.s, "Tc" ) )
            {
                //nur wenn Operand in gleicher Ebene bereits gesetzt wurde, steht fest, das vorheriger nutzlos ist
                if ( GS_act->Tc >= GS_act->begin )
                        pdf_ocr_invalidate_token( arr_zond_token, GS_act->Tc, 2 );

                GS_act->Tc = idx;
            }
            else if ( !g_strcmp0( zond_token.s, "Tw" ) )
            {
                if ( GS_act->Tw >= GS_act->begin ) //stammt aus der gleichen Ebene
                        pdf_ocr_invalidate_token( arr_zond_token, GS_act->Tw, 2 );

                GS_act->Tw = idx;
            }
            else if ( !g_strcmp0( zond_token.s, "Tz" ) )
            {
                if ( GS_act->Tz >= GS_act->begin ) //stammt aus der gleichen Ebene
                        pdf_ocr_invalidate_token( arr_zond_token, GS_act->Tz, 2 );

                GS_act->Tz = idx;
            }
            else if ( !g_strcmp0( zond_token.s, "TL" ) )
            {
                if ( GS_act->TL >= GS_act->begin ) //stammt aus der gleichen Ebene
                        pdf_ocr_invalidate_token( arr_zond_token, GS_act->TL, 2 );

                GS_act->TL = idx;
            }
            else if ( !g_strcmp0( zond_token.s, "Tf" ) )
            {
                if ( GS_act->Tf >= GS_act->begin ) //stammt aus der gleichen Ebene
                        pdf_ocr_invalidate_token( arr_zond_token, GS_act->Tf, 3 );

                GS_act->Tf = idx;
            }
            else if ( !g_strcmp0( zond_token.s, "Tr" ) )
            {
                ZondToken zond_token_prev = { 0 };

                zond_token_prev = g_array_index( arr_zond_token, ZondToken, idx - 1 );
                GS_act->Tr_act = zond_token_prev.i;

                if ( GS_act->Tr >= GS_act->begin ) //stammt aus der gleichen Ebene
                        pdf_ocr_invalidate_token( arr_zond_token, GS_act->Tr, 2 );

                GS_act->Tr = idx;
            }
            else if ( !g_strcmp0( zond_token.s, "Ts" ) )
            {
                if ( GS_act->Ts >= GS_act->begin ) //stammt aus der gleichen Ebene
                        pdf_ocr_invalidate_token( arr_zond_token, GS_act->Ts, 2 );

                GS_act->Ts = idx;
            }

            //nur merken, wo ist, ob gelöscht wird bei ET entschieden
            else if ( !g_strcmp0( zond_token.s, "BT" ) ) //initialisieren
            {
                BT.BT = idx;
                BT.Td = -1;
                BT.TD = -1;
                BT.Tm = -1;
                BT.TAst = -1;
            }

            else if ( !g_strcmp0( zond_token.s, "Td" ) )
            {
                if ( BT.Td != -1 ) pdf_ocr_invalidate_token( arr_zond_token, BT.Td, 3 );

                BT.Td = idx;
            }
            else if ( !g_strcmp0( zond_token.s, "TD" ) )
            {
                if ( BT.TD != -1 ) pdf_ocr_invalidate_token( arr_zond_token, BT.TD, 3 );

                BT.TD = idx;
            }
            else if ( !g_strcmp0( zond_token.s, "Tm" ) )
            {
                if ( BT.Tm != -1 ) pdf_ocr_invalidate_token( arr_zond_token, BT.Tm, 7 );

                BT.Tm = idx;
            }
            else if ( !g_strcmp0( zond_token.s, "TAst" ) )
            {
                if ( BT.TAst != -1 ) pdf_ocr_invalidate_token( arr_zond_token, BT.TAst, 1 );

                BT.TAst = idx;
            }

            //TextShowingOps
            else if ( !g_strcmp0( zond_token.s, "Tj" ) ||
                    !g_strcmp0( zond_token.s, "'" ) ||
                    !g_strcmp0( zond_token.s, """" ) ||
                    !g_strcmp0( zond_token.s, "TJ" ) )
            {
                //soll Text gefiltert werden?
                if ( (flags & 1 && GS_act->Tr_act != 3) ||
                        (flags & 2 && GS_act->Tr_act == 3) ) //löschen!
                {
                    //Löschen bzw. ersetzen
                    if ( !g_strcmp0( zond_token.s, "Tj" ) ) pdf_ocr_invalidate_token( arr_zond_token, idx, 2 );
                    else if ( !g_strcmp0( zond_token.s, "'" ) )
                    {
                        g_array_remove_range( arr_zond_token, idx - 1, 2 );

                        pdf_ocr_insert_keyword_in_stream( arr_zond_token, "T*", idx - 1 );

                        if ( BT.TAst != -1 ) pdf_ocr_invalidate_token( arr_zond_token, BT.TAst, 1 );
                        BT.TAst = idx - 1;

                        idx--; //auf i - 1 gehen; array eins kürzer geworden!
                    }
                    else if ( !g_strcmp0( zond_token.s, """" ) )
                    {
                        pdf_ocr_insert_keyword_in_stream( arr_zond_token, "Tw", idx - 2 );
                        g_array_remove_range( arr_zond_token, idx, 2 );
                        pdf_ocr_insert_keyword_in_stream( arr_zond_token, "Tc", idx );
                        pdf_ocr_insert_keyword_in_stream( arr_zond_token, "T*", idx + 1 );

                        if ( GS_act->Tw != -1 && GS_act->Tw >= GS_act->begin ) //stammt aus der gleichen Ebene
                                pdf_ocr_invalidate_token( arr_zond_token, GS_act->Tw, 2 );
                        GS_act->Tw = idx - 2;

                        if ( GS_act->Tc != -1 && GS_act->Tc >= GS_act->begin ) //stammt aus der gleichen Ebene
                                pdf_ocr_invalidate_token( arr_zond_token, GS_act->Tc, 2 );
                        GS_act->Tc = idx;

                        if ( BT.TAst != -1 ) pdf_ocr_invalidate_token( arr_zond_token, BT.TAst, 1 );
                        BT.TAst = idx + 1;

                        idx++;
                    }
                    else if ( !g_strcmp0( zond_token.s, "TJ" ) )
                    {
                        //Beginn array finden
                        ZondToken zond_token_begin;
                        gint begin = idx;
                        do
                        {
                            begin--;
                            zond_token_begin = g_array_index( arr_zond_token, ZondToken, begin );
                        } while ( zond_token_begin.tok != PDF_TOK_OPEN_ARRAY );

                        pdf_ocr_invalidate_token( arr_zond_token, idx, idx - begin + 1 );
                    }
                }
                else //Text wird angezeigt
                {
                    //BT resetten
                    BT.BT = -1;

                    BT.Td = -1;
                    BT.TD = -1;
                    BT.Tm = -1;
                    BT.TAst = -1;

                    //reset graphics_state - flag == 1: mit text_state
                    pdf_ocr_reset_gs( arr_gs, 1 );
                    // Marked Content bleibt!
                    for ( gint i = 0; i < arr_marked_content->len; i++ )
                            g_array_index( arr_marked_content, gint, i ) = -1;
                }
            }

            else if ( !g_strcmp0( zond_token.s, "ET" ) )
            {
                //BT löschen, wenn nicht resetted
                if ( BT.BT != -1 )
                {
                    pdf_ocr_invalidate_token( arr_zond_token, BT.BT, 1 );
                    //und ET löschen
                    pdf_ocr_invalidate_token( arr_zond_token, idx, 1 );
                }

                //Text Pos-Operatoren löschen
                if ( BT.Td != -1 ) pdf_ocr_invalidate_token( arr_zond_token, BT.Td, 3 );
                if ( BT.TD != -1 ) pdf_ocr_invalidate_token( arr_zond_token, BT.TD, 3 );
                if ( BT.Tm != -1 ) pdf_ocr_invalidate_token( arr_zond_token, BT.Tm, 7 );
                if ( BT.TAst != -1 ) pdf_ocr_invalidate_token( arr_zond_token, BT.TAst, 1 );
            }

            //InlineImages
            else if ( !g_strcmp0( zond_token.s, "BI" ) ) BI = idx;
            else if ( !g_strcmp0( zond_token.s, "ID" ) )
            {
                //jetzt den inline-stream
                zond_token.tok = PDF_TOK_INLINE_STREAM; //Wert für inline-image-stream
                gint ch = 0;

                //white nach ID
                ch = fz_read_byte(ctx, stream);
                if (ch == '\r')
                    if (fz_peek_byte(ctx, stream) == '\n')
                        fz_read_byte(ctx, stream);

                zond_token.gba = g_byte_array_new( );
                while ( (ch = fz_read_byte( ctx, stream )) != '>' )
                        g_byte_array_append( zond_token.gba, (guint8*) &ch, 1 );

                //Zond Token mit stream abspeichern
                g_array_append_val( arr_zond_token, zond_token );
                idx++;

                //EI suchen
                do
                {
                    ch = fz_read_byte( ctx, stream );
                    if ( ch == EOF )
                    {
                        g_ptr_array_unref( arr_gs );
                        g_array_unref( arr_marked_content );
                        g_array_unref( arr_zond_token );
                        if ( errmsg ) *errmsg = g_strdup( "Syntax Error in ContentStream:\n"
                                "EI nicht gefunden" );
                        return NULL;
                    }
                    else if ( ch == 'E' && fz_peek_byte( ctx, stream ) == 'I' ) break;

                } while ( 1 );
                fz_read_byte( ctx, stream ); //auf das I vorgehen

                if ( flags & 4 ) //inline-images werden herausgefiltert
                {
                    pdf_ocr_invalidate_token( arr_zond_token, idx, idx - BI + 1 );
                    BI = -1;
                }
                else //bleibt drin
                {
                    zond_token.tok = PDF_TOK_KEYWORD;
                    zond_token.s = g_strdup( "\nEI" );

                    g_array_append_val( arr_zond_token, zond_token );
                    idx++;

                    //"vorgespannte" Operatoren resetten
                    pdf_ocr_reset_gs( arr_gs, 0 );
                    for ( gint i = 0; i < arr_marked_content->len; i++ )
                            g_array_index( arr_marked_content, gint, i ) = -1;
                }
            }

            //XObjects
            else if ( !g_strcmp0( zond_token.s, "Do" ) )
            {
                //XObjects löschen
                if ( flags & 8 ) pdf_ocr_invalidate_token( arr_zond_token, idx, 2 );
                else
                {
                    pdf_ocr_reset_gs( arr_gs, 0 );
                    for ( gint i = 0; i < arr_marked_content->len; i++ )
                            g_array_index( arr_marked_content, gint, i ) = -1;
                }
            }

            else if ( !g_strcmp0( zond_token.s, "BMC" ) || !g_strcmp0( zond_token.s, "BDC" ) )
                    g_array_append_val( arr_marked_content, idx );
            else if ( !g_strcmp0( zond_token.s, "EMC" ) )
            {
                gint idx_B_C = g_array_index( arr_marked_content, gint, arr_marked_content->len - 1 );
                if ( idx_B_C != -1 )
                {
                    if ( !g_strcmp0( g_array_index( arr_zond_token, ZondToken, idx_B_C ).s, "BMC" ) )
                            pdf_ocr_invalidate_token( arr_zond_token, idx_B_C, 2 );
                    else
                    {
                        //Beginn dict finden
                        ZondToken zond_token_begin;
                        gint begin = idx_B_C;
                        do
                        {
                            begin--;
                            zond_token_begin = g_array_index( arr_zond_token, ZondToken, begin );
                        } while ( zond_token_begin.tok != PDF_TOK_OPEN_DICT );

                        pdf_ocr_invalidate_token( arr_zond_token, idx_B_C, idx_B_C - begin + 2 );
                        pdf_ocr_invalidate_token( arr_zond_token, idx, 1 ); //EMC selbert
                    }
                }
                g_array_remove_index_fast( arr_marked_content, arr_marked_content->len - 1 );
            }

            else if ( !g_strcmp0( zond_token.s, "Q" ) )
            {
                if ( GS_act->q >= GS_act->begin )
                {
                    pdf_ocr_invalidate_token( arr_zond_token, GS_act->q, 1 );
                    pdf_ocr_invalidate_token( arr_zond_token, idx, 1 );
                }

                for ( gint u = GS_act->arr_cm->len - 1; u >= 0; u-- )
                {
                    gint cm = g_array_index( GS_act->arr_cm, gint, u );
                    if ( cm >= GS_act->begin ) pdf_ocr_invalidate_token( arr_zond_token, cm, 7 );
                }
                //noch gesetzte Text-State-Ops löschen (wenn aus aktueller Ebene - dann auch != -1!
                if ( GS_act->Tc >= GS_act->begin ) pdf_ocr_invalidate_token( arr_zond_token, GS_act->Tc, 2 );
                if ( GS_act->Tw >= GS_act->begin ) pdf_ocr_invalidate_token( arr_zond_token, GS_act->Tw, 2 );
                if ( GS_act->Tz >= GS_act->begin ) pdf_ocr_invalidate_token( arr_zond_token, GS_act->Tz, 2 );
                if ( GS_act->TL >= GS_act->begin ) pdf_ocr_invalidate_token( arr_zond_token, GS_act->TL, 2 );
                if ( GS_act->Tf >= GS_act->begin ) pdf_ocr_invalidate_token( arr_zond_token, GS_act->Tf, 3 );
                if ( GS_act->Tr >= GS_act->begin ) pdf_ocr_invalidate_token( arr_zond_token, GS_act->Tr, 2 );
                if ( GS_act->Ts >= GS_act->begin ) pdf_ocr_invalidate_token( arr_zond_token, GS_act->Ts, 2 );

                //GS zurücksetzen
                GS_act = pdf_ocr_restore_gs( arr_gs );
            }

            //irgendein token, das etwas macht
            else if ( !g_strcmp0( zond_token.s, "S" ) ||
                    !g_strcmp0( zond_token.s, "s" ) ||
                    !g_strcmp0( zond_token.s, "f" ) ||
                    !g_strcmp0( zond_token.s, "F" ) ||
                    !g_strcmp0( zond_token.s, "f*" ) ||
                    !g_strcmp0( zond_token.s, "B" ) ||
                    !g_strcmp0( zond_token.s, "B*" ) ||
                    !g_strcmp0( zond_token.s, "b" ) ||
                    !g_strcmp0( zond_token.s, "b*" ) ||
                    !g_strcmp0( zond_token.s, "n" ) ||
                    !g_strcmp0( zond_token.s, "sh" ) ) //macht sh etwas?
            {
                pdf_ocr_reset_gs( arr_gs, 0 ); //Text ist ja schon behandelt; irgendwann auch weitere ausgenommen...
                for ( gint i = 0; i < arr_marked_content->len; i++ )
                        g_array_index( arr_marked_content, gint, i ) = -1;
            }
        } //endif tok == KEYWORD
    } //endwhile tok != PDF_TOK_EOF

    //tok == PDF_TOK_EOF
    if ( GS_act->q != -1 ) pdf_ocr_invalidate_token( arr_zond_token, GS_act->q, 1 );

    for ( gint u = GS_act->arr_cm->len - 1; u >= 0; u --)
    {
        gint cm = g_array_index( GS_act->arr_cm, gint, u );
        if ( cm >= GS_act->begin ) pdf_ocr_invalidate_token( arr_zond_token, cm, 7 );
    }

    //noch gesetzte Text-State-Ops löschen
    if ( GS_act->Tc != -1 ) pdf_ocr_invalidate_token( arr_zond_token, GS_act->Tc, 2 );
    if ( GS_act->Tw != -1 ) pdf_ocr_invalidate_token( arr_zond_token, GS_act->Tw, 2 );
    if ( GS_act->Tz != -1 ) pdf_ocr_invalidate_token( arr_zond_token, GS_act->Tz, 2 );
    if ( GS_act->TL != -1 ) pdf_ocr_invalidate_token( arr_zond_token, GS_act->TL, 2 );
    if ( GS_act->Tf != -1 ) pdf_ocr_invalidate_token( arr_zond_token, GS_act->Tf, 3 );
    if ( GS_act->Tr != -1 ) pdf_ocr_invalidate_token( arr_zond_token, GS_act->Tr, 2 );
    if ( GS_act->Ts != -1 ) pdf_ocr_invalidate_token( arr_zond_token, GS_act->Ts, 2 );

    g_ptr_array_unref( arr_gs );
    g_array_unref( arr_marked_content );

    //auf BX-EX prüfen
    for ( gint idx = 0; idx < arr_zond_token->len; idx++ )
    {
        gint BX = -1;
        ZondToken zond_token = g_array_index( arr_zond_token, ZondToken, idx );

        if ( zond_token.tok == PDF_TOK_KEYWORD && !g_strcmp0( zond_token.s, "BX" ) ) BX = idx;
        else if ( zond_token.tok != PDF_NUM_TOKENS && BX > -1 ) BX = -1;
        else if ( zond_token.tok == PDF_TOK_KEYWORD && !g_strcmp0( zond_token.s, "EX" )
                && BX > -1 )
        {
            pdf_ocr_invalidate_token( arr_zond_token, BX, 1 );
            pdf_ocr_invalidate_token( arr_zond_token, idx, 1 );
            BX = -1;
        }
    }

    return arr_zond_token;
}


static gint
pdf_ocr_prepare_content_stream( fz_context* ctx, pdf_page* page, gchar** errmsg )
{
    //zunächst mit mupdf alles schön machen, d.h. AsciiHex-Filter für (Inline-)Images
	pdf_annot* annot = NULL;
	pdf_filter_options filter = { 0 };

	filter.recurse = 1;
	filter.sanitize = 0;
	filter.ascii = 1;

    fz_try(ctx) pdf_filter_page_contents(ctx, page->doc, page, &filter);
    fz_catch( ctx ) ERROR_MUPDF( "pdf_filter_page_contents" )

    for (annot = pdf_first_annot(ctx, page); annot != NULL; annot = pdf_next_annot(ctx, annot))
    {
        fz_try( ctx ) pdf_filter_annot_contents(ctx, page->doc, annot, &filter);
        fz_catch( ctx ) ERROR_MUPDF( "pdf_filter_anoot_contents" )
    }

    return 0;
}


static gint
pdf_ocr_filter_content_stream( fz_context* ctx, pdf_page* page, gint flags, gchar** errmsg )
{
    gint rc = 0;
    fz_buffer* buf = NULL;
    fz_stream* stream = NULL;
    GArray* arr_zond_token = NULL;

    //erst den vorhandenen stream schön machen, insbesondere für inline-images!!!
    rc = pdf_ocr_prepare_content_stream( ctx, page, errmsg );
    if ( rc ) ERROR_SOND( "pdf_zond_prepare_content_stream" )

    //Stream doc_text
    fz_try( ctx ) stream = pdf_open_contents_stream( ctx, page->doc, pdf_dict_get( ctx, page->obj, PDF_NAME(Contents) ) );
    fz_catch( ctx ) ERROR_MUPDF( "pdf_open_contents_stream" )

    arr_zond_token = pdf_ocr_get_cleaned_tokens( ctx, stream, flags, errmsg );
    fz_drop_stream( ctx, stream );
    if ( !arr_zond_token ) ERROR_SOND( "pdf_ocr_get_cleaned_tokens" )

    buf = pdf_ocr_reassemble_buffer( ctx, arr_zond_token, errmsg );
    g_array_unref( arr_zond_token );
    if ( !buf ) ERROR_SOND( "pdf_zond_reassemble_buffer" )

    rc = pdf_ocr_update_content_stream( ctx, page->obj, buf, errmsg );
    fz_drop_buffer( ctx, buf );
    if ( rc ) ERROR_SOND( "pdf_ocr_update_content_stream" )

    return 0;
}


static gchar*
pdf_ocr_find_BT( gchar* buf, size_t size )
{
    gchar* ptr = NULL;

    ptr = buf;
    while ( ptr < buf + size - 1 )
    {
        if ( *ptr == 'B' && *(ptr + 1) == 'T' ) return ptr;
        ptr++;
    }

    return NULL;
}


static gint
pdf_ocr_process_tess_tmp( fz_context* ctx, pdf_obj* page_ref,
        fz_matrix ctm, gchar** errmsg )
{
    gint rc = 0;
    fz_buffer* buf = NULL;
    fz_buffer* buf_new = NULL;
    size_t size = 0;
    gchar* data = NULL;
    gchar* cm = NULL;
    gchar* BT = NULL;

    cm = g_strdup_printf( "\nq\n%g %g %g %g %g %g cm\nBT",
            ctm.a, ctm.b, ctm.c, ctm.d, ctm.e, ctm.f );

    //Komma durch Punkt ersetzen
    for ( gint i = 0; i < strlen( cm ); i++ ) if ( *(cm + i) == ',' )
            *(cm + i) = '.';

    buf = pdf_ocr_get_content_stream_as_buffer( ctx, page_ref, errmsg );
    if ( !buf )
    {
        g_free( cm );
        ERROR_SOND( "pdf_ocr_get_content_stream_as_buffer" )
    }

    size = fz_buffer_storage( ctx, buf, (guchar**) &data );

    BT = pdf_ocr_find_BT( data, size );
    if ( !BT )
    {
        fz_drop_buffer( ctx, buf );
        g_free( cm );
        if ( errmsg ) *errmsg = g_strdup( "Bei Aufruf pdf_ocr_find_BT:\nKein BT-Token gefunden" );

        return -1;
    }

    fz_try( ctx ) buf_new = fz_new_buffer( ctx, size + strlen( cm ) + 10 );
    fz_catch( ctx )
    {
        fz_drop_buffer( ctx, buf );
        g_free( cm );
        ERROR_MUPDF( "fz_new_buffer" );
    }

    fz_try( ctx )
    {
        fz_append_data( ctx, buf_new, cm, strlen( cm ) );
        fz_append_data( ctx, buf_new, BT + 2, size - (BT + 2 - data) );
        fz_append_data( ctx, buf_new, "\nQ", 2 );
    }
    fz_always( ctx )
    {
        g_free( cm );
        fz_drop_buffer( ctx, buf );
    }
    fz_catch( ctx )
    {
        fz_drop_buffer( ctx, buf_new );
        ERROR_MUPDF( "append buffer" )
    }
    rc = pdf_ocr_update_content_stream( ctx, page_ref, buf_new, errmsg );
    fz_drop_buffer( ctx, buf_new );
    if ( rc ) ERROR_SOND( "pdf_ocr_update_content_stream" )

    return 0;
}


static fz_matrix
pdf_ocr_create_matrix( fz_context* ctx, fz_rect rect, gfloat scale, gint rotate )
{
    gfloat shift_x = 0;
    gfloat shift_y = 0;
    gfloat width = 0;
    gfloat height = 0;

    width = rect.x1 - rect.x0;
    height = rect.y1 - rect.y0;

    fz_matrix ctm1 = fz_scale( scale, scale );
    fz_matrix ctm2 = fz_rotate( (float) rotate );

    if ( rotate == 90 ) shift_x = height;
    else if ( rotate == 180 )
    {
        shift_x = height;
        shift_y = width;
    }
    else if ( rotate == 270 ) shift_y = width;

    fz_matrix ctm = fz_concat( ctm1, ctm2 );

    ctm.e = shift_x;
    ctm.f = shift_y;

    return ctm;
}


static gint
pdf_ocr_sandwich_page( PdfDocumentPage* pdf_document_page,
        pdf_document* doc_text, gint page_text, gchar** errmsg )
{
    gint rc = 0;
    pdf_obj* page_ref_text = NULL;
    pdf_graft_map *graft_map = NULL;
    pdf_obj *obj = NULL;
    pdf_obj* contents_arr = NULL;
    gint zaehler = 0;

    pdf_obj* resources = NULL;
    pdf_obj* resources_text = NULL;
    pdf_obj* font_dict = NULL;
    pdf_obj* font_dict_text = NULL;
    pdf_obj* font_dict_key = NULL;
    pdf_obj* font_dict_val = NULL;

    fz_context* ctx = zond_pdf_document_get_ctx( pdf_document_page->document );

    fz_try( ctx ) pdf_flatten_inheritable_page_items( ctx, pdf_document_page->page->obj );
    fz_catch( ctx ) ERROR_MUPDF_R( "pdf_flatten_inheritable_page_items (orig)", -2 )

    fz_try( ctx ) page_ref_text = pdf_lookup_page_obj( ctx, doc_text, page_text );
    fz_catch( ctx ) ERROR_MUPDF_R( "pdf_lookup_page_obj", -2 )

    fz_try( ctx ) pdf_flatten_inheritable_page_items( ctx, page_ref_text );
    fz_catch( ctx ) ERROR_MUPDF_R( "pdf_flatten_inheritable_page_items (text)", -2 );

    rc = pdf_ocr_filter_content_stream( ctx, pdf_document_page->page, 3, errmsg );
    if ( rc ) ERROR_SOND( "pdf_zond_filter_content_stream" )

//    fz_rect rect = pdf_ocr_get_mediabox( ctx, pdf_document_page->page->obj );
    float scale = 1./TESS_SCALE/72.*70.;

    fz_matrix ctm = pdf_ocr_create_matrix( ctx, pdf_document_page->rect, scale, pdf_document_page->rotate );

    rc = pdf_ocr_process_tess_tmp( ctx, page_ref_text, ctm, errmsg );
    if ( rc ) ERROR_SOND_VAL( "pdf_ocr_process_tess_tmp", -2 )

    fz_try( ctx ) contents_arr = pdf_new_array( ctx, pdf_get_bound_document( ctx, pdf_document_page->page->obj ), 1 );
    fz_catch( ctx ) ERROR_MUPDF_R( "pdf_new_array", -2 )

    fz_try( ctx )
    {
        //Contents aus Ursrpungs-Pdf in neues Array umkopieren
        //graft nicht erforderlich, da selbes Dokument - Referenzen bleiben
        obj = pdf_dict_get( ctx, pdf_document_page->page->obj, PDF_NAME(Contents) ); //keine exception
        if ( pdf_is_array( ctx, obj ) )
        {
            for ( gint i = 0; i < pdf_array_len( ctx, obj ); i++ )
            {
                pdf_obj* content_stream = pdf_array_get( ctx, obj, i );
                if ( content_stream != NULL )
                {
                    pdf_array_put( ctx, contents_arr, zaehler, content_stream );
                    zaehler++;
                }
            }
        }
        else if ( pdf_is_stream( ctx, obj ) )
        {
            pdf_array_put( ctx, contents_arr, zaehler, obj );
            zaehler++;
        }

        //Jetzt aus Text-PDF - graft map erforderlich
        graft_map = pdf_new_graft_map( ctx, pdf_get_bound_document( ctx, pdf_document_page->page->obj ) ); //keine exception

        obj = pdf_dict_get( ctx, page_ref_text, PDF_NAME(Contents) );
        if ( pdf_is_array( ctx, obj ) )
        {
            for ( gint i = 0; i < pdf_array_len( ctx, obj ); i++ )
            {
                pdf_obj* content_stream = pdf_array_get( ctx, obj, i );
                if ( content_stream != NULL )
                {
                    pdf_array_put_drop( ctx, contents_arr, zaehler,
                            pdf_graft_mapped_object( ctx, graft_map,
                            content_stream ) );
                    zaehler++;
                }
            }
        }
        else if ( pdf_is_stream( ctx, obj ) )
        {
            pdf_array_put_drop( ctx, contents_arr, zaehler,
                    pdf_graft_mapped_object( ctx, graft_map, obj ) );
            zaehler++;
        }

        //alte Contents raus, neue rein
        pdf_dict_del( ctx, pdf_document_page->page->obj, PDF_NAME(Contents) );
        pdf_dict_put( ctx, pdf_document_page->page->obj, PDF_NAME(Contents), contents_arr );

        //Resources aus pdf_text hizukopieren
        resources = pdf_dict_get( ctx, pdf_document_page->page->obj, PDF_NAME(Resources) );
        //Zunächst testen, ob Page-Object Font enthält
        font_dict = pdf_dict_get( ctx, resources, PDF_NAME(Font) );
        if ( !font_dict )
        {
            font_dict = pdf_new_dict( ctx, pdf_get_bound_document( ctx, pdf_document_page->page->obj ), 1 );
            pdf_dict_put_drop( ctx, resources, PDF_NAME(Font), font_dict );
        }

        //Nun Text-Pdf
        resources_text = pdf_dict_get( ctx, page_ref_text, PDF_NAME(Resources) );

        font_dict_text = pdf_dict_get( ctx, resources_text, PDF_NAME(Font) );
        for ( gint i = 0; i < pdf_dict_len( ctx, font_dict_text ); i++ )
        {
            font_dict_key = pdf_dict_get_key( ctx, font_dict_text, i );
            font_dict_val = pdf_dict_get_val( ctx, font_dict_text, i );

            pdf_dict_put_drop( ctx, font_dict, font_dict_key,
                    pdf_graft_mapped_object( ctx, graft_map, font_dict_val ) );
        }
    }
    fz_always( ctx )
    {
        pdf_drop_obj( ctx, contents_arr );
        pdf_drop_graft_map( ctx, graft_map );
    }
    fz_catch( ctx ) ERROR_MUPDF( "fz_try (page_sandwich)" )

    return 0;
}


//thread-safe
static gint
pdf_ocr_sandwich_doc( GPtrArray* arr_document_pages, pdf_document* doc_text,
        InfoWindow* info_window, gchar** errmsg )
{
    if ( arr_document_pages->len == 0 ) return 0;

    gint rc = 0;
    gint zaehler = 0;
    gchar* message = NULL;

    for ( gint i = 0; i < arr_document_pages->len; i++ )
    {
        PdfDocumentPage* pdf_document_page =
                g_ptr_array_index( arr_document_pages, i );

        zond_pdf_document_mutex_lock( pdf_document_page->document );
        rc = pdf_ocr_sandwich_page( pdf_document_page, doc_text, zaehler, errmsg );
        zond_pdf_document_mutex_unlock( pdf_document_page->document );
        zaehler++;
        if ( rc == -1 ) ERROR_SOND( "pdf_ocr_sandwich" )
        else if ( rc == -2 )
        {
            message = g_strdup_printf( "Seite konnte nicht eingelesen werden -\n%s",
                    *errmsg );
            g_free( *errmsg );
            info_window_set_message( info_window, message );
            g_free( message );

            g_ptr_array_remove_index( arr_document_pages, i );
            i--;

            continue;
        }
    }

    return 0;
}


typedef struct _Tess_Recog
{
    TessBaseAPI* handle;
    ETEXT_DESC* monitor;
} TessRecog;


static gpointer
pdf_ocr_tess_recog( gpointer data )
{
    gint rc = 0;

    TessRecog* tess_recog = (TessRecog*) data;

    rc = TessBaseAPIRecognize( tess_recog->handle, tess_recog->monitor );

    if ( rc ) return GINT_TO_POINTER(1);

    return NULL;
}


static gboolean
pdf_ocr_cancel( void* cancel_this )
{
    volatile gboolean *cancelFlag = (volatile gboolean*) cancel_this;
    return *cancelFlag;
}


static gint
pdf_ocr_tess_page( InfoWindow* info_window, TessBaseAPI* handle,
        fz_pixmap* pixmap, gchar** errmsg )
{
    gint rc = 0;
    ETEXT_DESC* monitor = NULL;
    gint progress = 0;

    TessBaseAPISetImage( handle, pixmap->samples, pixmap->w, pixmap->h, pixmap->n, pixmap->stride );

    monitor = TessMonitorCreate( );
    TessMonitorSetCancelThis( monitor, &(info_window->cancel) );
    TessMonitorSetCancelFunc( monitor, (TessCancelFunc) pdf_ocr_cancel );

    TessRecog tess_recog = { handle, monitor };
    GThread* thread_recog = g_thread_new( "recog", pdf_ocr_tess_recog, &tess_recog );

    info_window_set_progress_bar( info_window );

    while ( progress < 100 && !(info_window->cancel) )
    {
        progress = TessMonitorGetProgress( monitor );
        info_window_set_progress_bar_fraction( info_window, ((gdouble) progress) / 100 );
    }

    rc = GPOINTER_TO_INT(g_thread_join( thread_recog ));
    TessMonitorDelete( monitor );

    if ( rc && !(info_window->cancel) ) ERROR_SOND( "TessAPIPRecognize:\nFehler!" )

    return 0;
}


static fz_pixmap*
pdf_ocr_render_pixmap( fz_context* ctx, pdf_document* doc, float scale,
        gchar** errmsg )
{
    pdf_page* page = NULL;
    fz_pixmap* pixmap = NULL;

    page = pdf_load_page( ctx, doc, 0 );

    fz_rect rect = pdf_bound_page( ctx, page );
    fz_matrix ctm = pdf_ocr_create_matrix( ctx, rect, scale, 0 );

    rect = fz_transform_rect( rect, ctm );

//per draw-device to pixmap
    fz_try( ctx ) pixmap = fz_new_pixmap_with_bbox( ctx, fz_device_rgb( ctx ),
            fz_irect_from_rect( rect ), NULL, 0 );
    fz_catch( ctx )
    {
        fz_drop_page( ctx, &page->super );
        ERROR_MUPDF_R( "fz_new_pixmap_with_bbox", NULL )
    }

    fz_try( ctx) fz_clear_pixmap_with_value( ctx, pixmap, 255 );
    fz_catch( ctx )
    {
        fz_drop_page( ctx, &page->super );
        fz_drop_pixmap( ctx, pixmap );

        ERROR_MUPDF_R( "fz_clear_pixmap", NULL )
    }

    fz_device* draw_device = NULL;
    fz_try( ctx ) draw_device = fz_new_draw_device( ctx, ctm, pixmap );
    fz_catch( ctx )
    {
        fz_drop_page( ctx, &page->super );
        fz_drop_pixmap( ctx, pixmap );

        ERROR_MUPDF_R( "fz_new_draw_device", NULL )
    }

    fz_try( ctx ) pdf_run_page( ctx, page, draw_device, fz_identity, NULL );
    fz_always( ctx )
    {
        fz_close_device( ctx, draw_device );
        fz_drop_device( ctx, draw_device );
        fz_drop_page( ctx, &page->super );
    }
    fz_catch( ctx )
    {
        fz_drop_pixmap( ctx, pixmap );

        ERROR_MUPDF_R( "fz_new_draw_device", NULL )
    }

    return pixmap;
}


//thread-safe
static pdf_document*
pdf_ocr_create_doc_from_page( PdfDocumentPage* pdf_document_page, gint flag, gchar** errmsg )
{
    gint rc = 0;
    pdf_document* doc_new = NULL;
    gint page_doc = 0;
    pdf_page* page = NULL;

    fz_context* ctx = zond_pdf_document_get_ctx( pdf_document_page->document );
    pdf_document* doc = zond_pdf_document_get_pdf_doc( pdf_document_page->document );

    fz_try( ctx ) doc_new = pdf_create_document( ctx );
    fz_catch( ctx ) ERROR_MUPDF_R( "pdf_create_document", NULL )

    page_doc = zond_pdf_document_get_index( pdf_document_page );

    zond_pdf_document_mutex_lock( pdf_document_page->document );
    rc = pdf_copy_page( ctx, doc, page_doc, page_doc, doc_new, 0, errmsg );
    zond_pdf_document_mutex_unlock( pdf_document_page->document );
    if ( rc )
    {
        pdf_drop_document( ctx, doc_new );
        ERROR_SOND_VAL( "pdf_copy_page", NULL )
    }

    fz_try( ctx ) page = pdf_load_page( ctx, doc_new, 0 );
    fz_catch( ctx )
    {
        pdf_drop_document( ctx, doc_new );
        ERROR_MUPDF_R( "pdf_lookup_page_obj", NULL )
    }

    //neues dokument mit einer Seite filtern
    rc = pdf_ocr_filter_content_stream( ctx, page, flag, errmsg );
    fz_drop_page( ctx, &page->super );
    if ( rc )
    {
        pdf_drop_document( ctx, doc_new );
        ERROR_MUPDF_R( "pdf_zond_filter_content_stream", NULL );
    }

    return doc_new;
}


//thread-safe
static gint
pdf_ocr_page( PdfDocumentPage* pdf_document_page, InfoWindow* info_window,
        TessBaseAPI* handle, TessResultRenderer* renderer, gchar** errmsg )
{
    gint rc = 0;
    fz_pixmap* pixmap = NULL;
    pdf_document* doc_new = NULL;

    doc_new = pdf_ocr_create_doc_from_page( pdf_document_page, 3, errmsg ); //thread-safe
    if ( !doc_new ) ERROR_S

    fz_context* ctx = zond_pdf_document_get_ctx( pdf_document_page->document );

    pixmap = pdf_ocr_render_pixmap( ctx, doc_new, TESS_SCALE, errmsg );
    pdf_drop_document( ctx, doc_new );
    if ( !pixmap ) ERROR_S

    rc = pdf_ocr_tess_page( info_window, handle, pixmap, errmsg );
    fz_drop_pixmap( ctx, pixmap );
    if ( rc ) ERROR_S

    return 0;
}


static GtkWidget*
pdf_ocr_create_dialog( InfoWindow* info_window, gint page )
{
    gchar* titel = g_strdup_printf( "Seite %i enthält bereits "
            "versteckten Text - Text löschen?", page );
    GtkWidget* dialog = gtk_dialog_new_with_buttons( titel,
            GTK_WINDOW(info_window->dialog), GTK_DIALOG_MODAL,
            "Ja", 1, "Ja für alle", 2, "Nein", 3, "Nein für alle", 4,
            "Anzeigen", 5,
            "Abbrechen", GTK_RESPONSE_CANCEL, NULL );
    g_free( titel );

    return dialog;
}


//thread-safe
static fz_pixmap*
pdf_ocr_render_images( PdfDocumentPage* pdf_document_page, gchar** errmsg )
{
    pdf_document* doc_tmp_orig = NULL;
    fz_pixmap* pixmap = NULL;

    doc_tmp_orig = pdf_ocr_create_doc_from_page( pdf_document_page, 3, errmsg ); //thread-safe
    if ( !doc_tmp_orig ) ERROR_S_VAL( NULL )

    fz_context* ctx = zond_pdf_document_get_ctx( pdf_document_page->document );

    pixmap = pdf_ocr_render_pixmap( ctx, doc_tmp_orig, 1.2, errmsg );
    pdf_drop_document( ctx, doc_tmp_orig );
    if ( !pixmap ) ERROR_S_VAL( NULL )

    return pixmap;
}


static gchar*
pdf_ocr_get_text_from_stext_page( fz_context* ctx, fz_stext_page* stext_page,
        gchar** errmsg )
{
    gchar* text = "";
    guchar* text_tmp = NULL;
    fz_buffer* buf = NULL;
    fz_output* out = NULL;

    fz_try( ctx ) buf = fz_new_buffer( ctx, 1024 );
    fz_catch( ctx ) ERROR_MUPDF_R( "fz_new_buffer", NULL );

    fz_try( ctx ) out = fz_new_output_with_buffer( ctx, buf );
    fz_catch( ctx )
    {
        fz_drop_buffer( ctx, buf );
        ERROR_MUPDF_R( "fz_new_output_with_buffer", NULL );
    }

    fz_try( ctx ) fz_print_stext_page_as_text( ctx, out, stext_page );
    fz_always( ctx )
    {
        fz_close_output( ctx, out );
        fz_drop_output( ctx, out );
    }
    fz_catch( ctx )
    {
        fz_drop_buffer( ctx, buf );
        ERROR_MUPDF_R( "fz_print_stext_page_as_text", NULL )
    }

    fz_try( ctx ) fz_terminate_buffer( ctx, buf );
    fz_catch( ctx )
    {
        fz_drop_buffer( ctx, buf );
        ERROR_MUPDF_R( "fz_terminate_buffer", NULL );
    }

    fz_buffer_storage( ctx, buf, &text_tmp );
    text = g_strdup( (gchar*) text_tmp );
    fz_drop_buffer( ctx, buf );

    return text;
}


//thread-safe
static gchar*
pdf_ocr_get_hidden_text( PdfDocumentPage* pdf_document_page, gchar** errmsg )
{
    pdf_document* doc_tmp_alt = NULL;
    pdf_page* page = NULL;
    fz_stext_page* stext_page = NULL;
    fz_device* s_t_device = NULL;
    gchar* text = NULL;

    //flag == 1: nur sichtbaren Text entfernen
    doc_tmp_alt = pdf_ocr_create_doc_from_page( pdf_document_page, 1, errmsg ); //thread-safe
    if ( !doc_tmp_alt ) ERROR_SOND_VAL( "pdf_create_doc_with_page", NULL )

    fz_context* ctx = zond_pdf_document_get_ctx( pdf_document_page->document );

    fz_try( ctx ) page = pdf_load_page( ctx, doc_tmp_alt, 0 );
    fz_catch( ctx )
    {
        pdf_drop_document( ctx, doc_tmp_alt );
        ERROR_MUPDF_R( "fz_load_page", NULL );

        return NULL;
    }

    //structured text-device
    fz_try( ctx ) stext_page = fz_new_stext_page( ctx, pdf_bound_page( ctx, page ) );
    fz_catch( ctx )
    {
        fz_drop_page( ctx, &page->super );
        pdf_drop_document( ctx, doc_tmp_alt );
        ERROR_MUPDF_R( "fz_new_stext_page", NULL )
    }

    fz_try( ctx ) s_t_device = fz_new_stext_device( ctx, stext_page, NULL );
    fz_catch( ctx )
    {
        fz_drop_stext_page( ctx, stext_page );
        fz_drop_page( ctx, &page->super );
        pdf_drop_document( ctx, doc_tmp_alt );
        ERROR_MUPDF_R( "fz_new_stext_device", NULL )

        return NULL;
    }

//Seite durch's device laufen lassen
    fz_try( ctx ) pdf_run_page( ctx, page, s_t_device, fz_identity, NULL );
    fz_always( ctx )
    {
        fz_close_device( ctx, s_t_device );
        fz_drop_device( ctx, s_t_device );
        fz_drop_page( ctx, &page->super );
        pdf_drop_document( ctx, doc_tmp_alt );
    }
    fz_catch( ctx )
    {
        fz_drop_stext_page( ctx, stext_page );
        ERROR_MUPDF_R( "fz_run_page", NULL )
    }

    //bisheriger versteckter Text
    text = pdf_ocr_get_text_from_stext_page( ctx, stext_page, errmsg );
    fz_drop_stext_page( ctx, stext_page );
    if ( !text ) ERROR_SOND_VAL( "pdf_get_text_from_stext_page", NULL )

    return text;
}


//thread-safe
static gint
pdf_ocr_show_text( InfoWindow* info_window, PdfDocumentPage* pdf_document_page,
        gchar* text_alt, TessBaseAPI* handle, TessResultRenderer* renderer, gchar** errmsg )
{
    gint rc = 0;
    fz_pixmap* pixmap_orig = NULL;
    gchar* text_neu = NULL;

    fz_context* ctx = zond_pdf_document_get_ctx( pdf_document_page->document );

    //Bisherigen versteckten Text
    //gerenderte Seite ohne sichtbaren Text
    pixmap_orig = pdf_ocr_render_images( pdf_document_page, errmsg ); //thread-safe
    if ( !pixmap_orig ) ERROR_S

    //Eigene OCR
    //Wenn angezeigt werden soll, dann muß Seite erstmal OCRed werden
    //Um Vergleich zu haben
    rc = pdf_ocr_page( pdf_document_page, info_window, handle, renderer, errmsg ); //thread-safe
    if ( rc )
    {
        fz_drop_pixmap( ctx, pixmap_orig );
        ERROR_S
    }
    text_neu = TessBaseAPIGetUTF8Text( handle );

    //dialog erzeugen und erweitern
    GtkWidget* label_alt = gtk_label_new( "Gespeicherter Text" );
    GtkWidget* label_neu = gtk_label_new( "Neuer Text" );
    GtkWidget* hbox = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 0 );
    gtk_box_pack_start( GTK_BOX(hbox), label_alt, FALSE, FALSE, 0 );
    gtk_box_pack_end( GTK_BOX(hbox), label_neu, FALSE, FALSE, 0 );

    GtkWidget* text_view_alt = gtk_text_view_new( );
    gtk_text_view_set_editable( GTK_TEXT_VIEW(text_view_alt), FALSE );
    gtk_text_buffer_set_text( gtk_text_view_get_buffer(
            GTK_TEXT_VIEW(text_view_alt) ), text_alt, -1 );

    GtkWidget* text_view_neu = gtk_text_view_new( );
    gtk_text_view_set_editable( GTK_TEXT_VIEW(text_view_neu), FALSE );
    gtk_text_buffer_set_text( gtk_text_view_get_buffer(
            GTK_TEXT_VIEW(text_view_neu) ), text_neu, -1 );
    TessDeleteText( text_neu );

    GtkWidget* swindow_alt = gtk_scrolled_window_new( NULL, NULL );

    GtkWidget* image_orig = gtk_image_new( );
    GdkPixbuf* pixbuf_orig =
            gdk_pixbuf_new_from_data( pixmap_orig->samples,
            GDK_COLORSPACE_RGB, FALSE, 8, pixmap_orig->w,
            pixmap_orig->h, pixmap_orig->stride, NULL, NULL );
    gtk_image_set_from_pixbuf( GTK_IMAGE(image_orig), pixbuf_orig );

    GtkWidget* swindow_neu = gtk_scrolled_window_new( NULL, NULL );

    gtk_container_add( GTK_CONTAINER(swindow_alt), text_view_alt );
    gtk_container_add( GTK_CONTAINER(swindow_neu), text_view_neu );

    GtkWidget* hbox2 = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 0 );
    gtk_box_pack_start( GTK_BOX(hbox2), swindow_alt, TRUE, TRUE, 0 );
    gtk_box_pack_start( GTK_BOX(hbox2), image_orig, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(hbox2), swindow_neu, TRUE, TRUE, 0 );

    GtkWidget* swindow = gtk_scrolled_window_new( NULL, NULL );
    gtk_scrolled_window_set_propagate_natural_height( GTK_SCROLLED_WINDOW(swindow), TRUE );
    gtk_container_add( GTK_CONTAINER(swindow), hbox2 );

    GtkWidget* vbox = gtk_box_new( GTK_ORIENTATION_VERTICAL, 0 );
    gtk_box_pack_start( GTK_BOX(vbox), hbox, FALSE, FALSE, 0 );
    gtk_box_pack_start( GTK_BOX(vbox), swindow, FALSE, FALSE, 0 );

    gint page = zond_pdf_document_get_index( pdf_document_page );
    GtkWidget* dialog = pdf_ocr_create_dialog( info_window, page + 1 );

    GtkWidget* content_area =
            gtk_dialog_get_content_area( GTK_DIALOG(dialog) );
    gtk_box_pack_start( GTK_BOX(content_area), vbox, FALSE, FALSE, 0 );

    gtk_dialog_set_response_sensitive( GTK_DIALOG(dialog), 5, FALSE );

    //anzeigen
    gtk_window_maximize( GTK_WINDOW(dialog) );
    gtk_widget_show_all( dialog );

    //neue Abfrage
    rc = gtk_dialog_run( GTK_DIALOG(dialog) );

    gtk_widget_destroy( dialog );
    fz_drop_pixmap( ctx, pixmap_orig ); //wird nicht (!) mit widget zerstört

    return rc;
}


static gint
pdf_ocr_create_pdf_only_text( InfoWindow* info_window,
        GPtrArray* arr_document_pages, TessBaseAPI* handle,
        TessResultRenderer* renderer, gchar** errmsg )
{
    gint zaehler = 0;
    gint i = 0;
    gint alle = 0;

    for ( i = 0; i < arr_document_pages->len; i++ )
    {
        gboolean rendered = FALSE;
        gchar* page_text = NULL;
        gint rc = 0;

        zaehler++;

        PdfDocumentPage* pdf_document_page = g_ptr_array_index( arr_document_pages, i );
        gint index = zond_pdf_document_get_index( pdf_document_page );

        gchar* info_text = g_strdup_printf( "(%i/%i) %s, Seite %i",
                zaehler, arr_document_pages->len, zond_pdf_document_get_path( pdf_document_page->document ),
                index + 1 );
        info_window_set_message( info_window, info_text );
        g_free( info_text );

        page_text = pdf_ocr_get_hidden_text( pdf_document_page, errmsg );
        if ( !page_text ) ERROR_SOND( "pdf_ocr_get_hidden_text" )

        if ( g_strcmp0( page_text, "" ) && alle == 0 )
        {
            GtkWidget* dialog = pdf_ocr_create_dialog( info_window, index + 1 );
            //braucht nicht thread_safe zu sein
            rc = 0;
            rc = gtk_dialog_run( GTK_DIALOG(dialog) );

            gtk_widget_destroy( dialog );

            //Wenn Anzeigen gewählt wird, dialog in Unterfunktion neu starten
            if ( rc == 5 )
            {
                rc = pdf_ocr_show_text( info_window, pdf_document_page, page_text, handle,
                        renderer, errmsg ); //thread-safe
                g_free( page_text );
                if ( rc == -1 ) ERROR_SOND( "pdf_ocr_show_text" )
                rendered = TRUE;
            }
            else g_free( page_text );

            if ( rc == GTK_RESPONSE_CANCEL || rc == GTK_RESPONSE_DELETE_EVENT )
                    break;
            if ( rc == 1 ) rc = 0; //damit, falls bereits rendered, keine Fehlermeldung
            if ( rc == 2 )
            {
                rc = 0; //s.o. unter rc == 1
                alle = 1;
            }
            if ( rc == 3 ) //Nein
            {
                //Seite an Stelle in Array "setzen"
                g_ptr_array_remove_index( arr_document_pages, i );
                i--;
                continue;
            }
            if ( rc == 4 ) break;
        }
        else g_free( page_text );

        if ( !rendered ) rc = pdf_ocr_page( pdf_document_page, info_window, handle,
                renderer, errmsg ); //thread-safe
        if ( rc ) ERROR_SOND( "pdf_ocr_page" )

        if ( info_window->cancel ) break;

        //PDF rendern
        TessResultRendererAddImage( renderer, handle );
    }

    if ( i < arr_document_pages->len )
            g_ptr_array_remove_range( arr_document_pages, i, arr_document_pages->len - i );

    return 0;
}


static gint
init_tesseract( TessBaseAPI** handle, TessResultRenderer** renderer, gchar* path_tmp, gchar** errmsg )
{
    gint rc = 0;
    gchar* tessdata_dir = NULL;

    //TessBaseAPI
    *handle = TessBaseAPICreate( );
    if ( !(*handle) ) ERROR_SOND( "TessBaseApiCreate" )

    tessdata_dir = get_path_from_base( "share/tessdata", errmsg );
    if ( !tessdata_dir )
    {
        TessBaseAPIDelete( *handle );
        ERROR_S
    }

    rc = TessBaseAPIInit3( *handle, tessdata_dir, "deu" );
    g_free( tessdata_dir );
    if ( rc )
    {
        TessBaseAPIEnd( *handle );
        TessBaseAPIDelete( *handle );
        ERROR_S_MESSAGE( "TessBaseAPIInit3:\nFehler bei Initialisierung" )
    }

    //TessPdfRenderer
    *renderer = TessPDFRendererCreate( path_tmp, TessBaseAPIGetDatapath( *handle ), 1 );
    if ( !(*renderer) )
    {
        TessBaseAPIEnd( *handle );
        TessBaseAPIDelete( *handle );

        ERROR_S_MESSAGE( "TessPdfRenderer konnte nicht initialisiert werden" )
    }
    TessResultRendererBeginDocument( *renderer, "title" );

    return 0;
}


/** Rückgabewert:
*** Bei Fehler: -1; *errmsg wird gesetzt
*** Bei Abbruch: 0 **/
gint
pdf_ocr_pages( Projekt* zond, InfoWindow* info_window, GPtrArray* arr_document_pages, gchar** errmsg )
{
    gint rc = 0;
    fz_context* ctx = NULL;

    TessBaseAPI* handle = NULL;
    TessResultRenderer* renderer = NULL;
    gchar* path_tmp = NULL;

    path_tmp = g_strconcat( g_get_tmp_dir(), "\\tess_tmp", NULL );
    if ( !path_tmp ) ERROR_S

    rc = init_tesseract( &handle, &renderer, path_tmp, errmsg );
    g_free( path_tmp );
    if ( rc ) ERROR_S

    rc = pdf_ocr_create_pdf_only_text( info_window, arr_document_pages, handle,
            renderer, errmsg );

    TessResultRendererEndDocument( renderer );
    TessDeleteResultRenderer( renderer );
    TessBaseAPIEnd( handle );
    TessBaseAPIDelete( handle );

    if ( rc ) ERROR_S

    if ( !arr_document_pages->len ) return 0;

    //erzeugtes PDF mit nur Text mit muPDF öffnen
    pdf_document* doc_text = NULL;

    path_tmp = g_strconcat( g_get_tmp_dir(), "\\tess_tmp.pdf", NULL );

    ctx = zond->ctx;

    //doc mit text öffnen
    fz_try( ctx ) doc_text = pdf_open_document( ctx, path_tmp );//keine Passwortabfrage
    fz_always( ctx ) g_free( path_tmp );
    fz_catch( ctx ) ERROR_MUPDF( "pdf_open_document" )

    //Text in PDF übertragen
    rc = pdf_ocr_sandwich_doc( arr_document_pages, doc_text, info_window, errmsg ); //thread-safe
    pdf_drop_document( ctx, doc_text );
    if ( rc ) ERROR_SOND( "pdf_ocr_sandwich_doc" )

    return 0;
}


// EXPERIMENTELL!!!
gint
pdf_change_hidden_text( fz_context* ctx, pdf_obj* page_ref, gchar** errmsg )
{
    gint rc = 0;
    pdf_obj* obj_contents = NULL;
    fz_stream* stream = NULL;
    pdf_token tok = PDF_TOK_NULL;
    gint idx = -1;
    GArray* arr_zond_token = NULL;
    fz_buffer* buf = NULL;

    //Stream doc_text
    obj_contents = pdf_dict_get( ctx, page_ref, PDF_NAME(Contents) );

    fz_try( ctx ) stream = pdf_open_contents_stream( ctx, pdf_get_bound_document( ctx, page_ref ), obj_contents );
    fz_catch( ctx ) ERROR_MUPDF( "pdf_open_contents_stream" )

    arr_zond_token = g_array_new( FALSE, FALSE, sizeof( ZondToken ) );
    g_array_set_clear_func( arr_zond_token, pdf_ocr_free_zond_token );

    while ( tok != PDF_TOK_EOF )
    {
        ZondToken zond_token = { 0, };
        pdf_lexbuf lxb;

        pdf_lexbuf_init( ctx, &lxb, PDF_LEXBUF_SMALL );

        tok = pdf_lex( ctx, stream, &lxb );
        zond_token.tok = tok;

        if ( tok == PDF_TOK_REAL ) zond_token.f = lxb.f;
        else if ( tok == PDF_TOK_INT ) zond_token.i = lxb.i;
        else if ( tok == PDF_TOK_NAME || tok == PDF_TOK_KEYWORD )
                zond_token.s = g_strdup( lxb.scratch );
        else if ( tok == PDF_TOK_STRING )
        {
            zond_token.gba = g_byte_array_new( );
            g_byte_array_append( zond_token.gba, (guint8*) lxb.scratch, lxb.len );
        }

        pdf_lexbuf_fin( ctx, &lxb );

        g_array_append_val( arr_zond_token, zond_token );
        idx++;

        if ( tok == PDF_TOK_KEYWORD && !g_strcmp0( zond_token.s, "Tr" ) )
        {
            if ( g_array_index( arr_zond_token, ZondToken, idx - 1 ).i == 3 )
                    g_array_index( arr_zond_token, ZondToken, idx - 1 ).i = 0;
        }
    }

    buf = pdf_ocr_reassemble_buffer( ctx, arr_zond_token, errmsg );
    g_array_unref( arr_zond_token );
    if ( !buf ) ERROR_SOND( "pdf_zond_reassemble_buffer" )

    rc = pdf_ocr_update_content_stream( ctx, page_ref, buf, errmsg );
    fz_drop_buffer( ctx, buf );
    if ( rc ) ERROR_SOND( "pdf_update_content_stream" )

    //Dann Font-Dict
    pdf_obj* f_0_0 = NULL;

    fz_try( ctx ) pdf_flatten_inheritable_page_items( ctx, page_ref );
    fz_catch( ctx ) ERROR_MUPDF( "get page_ref" )

    pdf_obj* resources = pdf_dict_get( ctx, page_ref, PDF_NAME(Resources) );
    pdf_obj* font = pdf_dict_get( ctx, resources, PDF_NAME(Font) );

    pdf_obj* f_0_0_name = pdf_new_name( ctx, "f-0-0" );
    f_0_0 = pdf_dict_get( ctx, font, f_0_0_name );
    pdf_drop_obj( ctx, f_0_0_name );

    if ( !f_0_0 ) return 0; //Font nicht vorhanden - nix mehr zu tun

    //Einträge löschen
    gint len = pdf_dict_len( ctx, f_0_0 );
    for ( gint i = 0; i < len; i++ )
    {
        pdf_dict_del( ctx, f_0_0, pdf_dict_get_key( ctx, f_0_0, 0 ) );
    }

    pdf_dict_put( ctx, f_0_0, PDF_NAME(Type), PDF_NAME(Font) );
    pdf_dict_put( ctx, f_0_0, PDF_NAME(Subtype), PDF_NAME(Type1) );
    pdf_obj* font_name = pdf_new_name( ctx, "Times-Roman" );
    pdf_dict_put( ctx, f_0_0, PDF_NAME(BaseFont), font_name );
//    pdf_dict_put( ctx, f_0_0, PDF_NAME(Encoding), PDF_NAME(WinAnsiEndcoding) );
    pdf_drop_obj( ctx, font_name );

    return 0;
}




