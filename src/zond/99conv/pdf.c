#include "../error.h"

#include "../zond_pdf_document.h"

#include "../99conv/general.h"

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <glib/gstdio.h>

#include <stdio.h>


/** Nicht thread-safe  **/
gint
pdf_document_get_dest( fz_context* ctx, pdf_document* doc, gint page_doc,
        gpointer* ret, gboolean first_occ, gchar** errmsg )
{
    pdf_obj* obj_dest_tree = NULL;
    pdf_obj* obj_key = NULL;
    pdf_obj* obj_val = NULL;
    pdf_obj* obj_array = NULL;
    pdf_obj* obj_page = NULL;
    gint num = 0;
    const gchar* dest_found = NULL;


    fz_try( ctx ) obj_dest_tree = pdf_load_name_tree( ctx, doc, PDF_NAME(Dests) );
    fz_catch( ctx ) ERROR_MUPDF( "pdf_load_name_tree" )

    for ( gint i = 0; i < pdf_dict_len( ctx, obj_dest_tree ); i++ )
    {
        obj_key = pdf_dict_get_key( ctx, obj_dest_tree, i );
        obj_val = pdf_dict_get_val( ctx, obj_dest_tree, i );

        fz_try( ctx ) obj_array = pdf_resolve_indirect( ctx, obj_val );
        fz_catch( ctx )
        {
            pdf_drop_obj( ctx, obj_dest_tree );

            ERROR_MUPDF( "pdf_resolve_indirect" )
        }

        obj_page = pdf_array_get( ctx, obj_array, 0 );

        fz_try( ctx ) num = pdf_lookup_page_number( ctx, doc, obj_page );
        fz_catch( ctx )
        {
            pdf_drop_obj( ctx, obj_dest_tree );

            ERROR_MUPDF( "pdf_lookup_page_number" )
        }

        if ( num == page_doc )
        {
            dest_found = pdf_to_name( ctx, obj_key );
            if ( g_str_has_prefix( dest_found, "ZND-" ) )
            {
                if ( g_uuid_string_is_valid( dest_found + 4 ) )
                {
                    if ( first_occ )
                    {
                        *ret = (gpointer) g_strdup( dest_found );
                        break;
                    }
                    else g_ptr_array_add( (GPtrArray*) *ret, (gpointer) g_strdup( dest_found ) );
                }
            }
        }
    }

    pdf_drop_obj( ctx, obj_dest_tree );

    return 0;
}


gint
pdf_copy_page( fz_context* ctx, pdf_document* doc_src, gint page_from,
        gint page_to, pdf_document* doc_dest, gint page,
        gchar** errmsg )
{
    /* Copy as few key/value pairs as we can. Do not include items that reference other pages. */
    static pdf_obj * const copy_list[] = {
        PDF_NAME(Contents),
        PDF_NAME(Resources),
        PDF_NAME(MediaBox),
        PDF_NAME(CropBox),
        PDF_NAME(BleedBox),
        PDF_NAME(TrimBox),
        PDF_NAME(ArtBox),
        PDF_NAME(Rotate),
        PDF_NAME(UserUnit),
        PDF_NAME(Annots)
    };

    pdf_obj *page_ref = NULL;
    pdf_obj *page_dict = NULL;
    pdf_obj *obj = NULL;
    pdf_obj *ref = NULL;
    pdf_graft_map* graft_map = NULL;

    graft_map = pdf_new_graft_map( ctx, doc_dest ); //keine exception

    for ( gint u = page_from; u <= page_to; u++ )
    {
        fz_try( ctx )
        {
            page_ref = pdf_lookup_page_obj( ctx, doc_src, u );
            pdf_flatten_inheritable_page_items( ctx, page_ref );
        }
        fz_catch( ctx )
        {
            pdf_drop_graft_map( ctx, graft_map );
            ERROR_MUPDF_CTX( "pdf_lookup- and flatten_inheritable_page", ctx )
        }

        fz_try( ctx )
        {
            /* Make a new page object dictionary to hold the items we copy from the source page. */
            page_dict = pdf_new_dict( ctx, doc_dest, 4 );
            pdf_dict_put( ctx, page_dict, PDF_NAME(Type), PDF_NAME(Page) );
        }
        fz_catch( ctx )
        {
            pdf_drop_graft_map( ctx, graft_map );
            pdf_drop_obj( ctx, page_dict );
            ERROR_MUPDF_CTX( "pdf_new_dict/_put", ctx )
        }

        for ( gint i = 0; i < nelem(copy_list); i++ )
        {
            obj = pdf_dict_get( ctx, page_ref, copy_list[i] ); //ctx wird gar nicht gebraucht
            fz_try( ctx ) if (obj != NULL)
                    pdf_dict_put_drop( ctx, page_dict, copy_list[i],
                    pdf_graft_mapped_object( ctx, graft_map, obj ) );
            fz_catch( ctx )
            {
                pdf_drop_graft_map( ctx, graft_map );
                pdf_drop_obj( ctx, page_dict );
                ERROR_MUPDF_CTX( "pdf_dict_put_drop", ctx )
            }
        }

        fz_try( ctx )
        {
            /* Add the page object to the destination document. */
            ref = pdf_add_object( ctx, doc_dest, page_dict );

            /* Insert it into the page tree. */
            pdf_insert_page( ctx, doc_dest, (page == -1) ? -1 : page + u - page_from, ref );
        }
        fz_always( ctx )
        {
            pdf_drop_obj( ctx, page_dict );
            pdf_drop_obj( ctx, ref );
        }
        fz_catch( ctx )
        {
            pdf_drop_graft_map( ctx, graft_map );

            ERROR_MUPDF_CTX( "pdf_add_object/_insert_page", ctx )
        }
    }

    pdf_drop_graft_map( ctx, graft_map );

    return 0;
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


static void
pdf_zond_free_token_array( gpointer data )
{
    ZondToken* zond_token = (ZondToken*) data;

    if ( zond_token->tok == PDF_TOK_KEYWORD || zond_token->tok == PDF_TOK_NAME )
            g_free( zond_token->s );
    else if ( zond_token->tok == PDF_TOK_STRING ) g_byte_array_unref( zond_token->gba );

    return;
}

/** stream filtern **/
GArray*
pdf_zond_get_token_array( fz_context* ctx, fz_stream* stream )
{
    GArray* arr_zond_token = g_array_new( FALSE, FALSE, sizeof( ZondToken ) );
    g_array_set_clear_func( arr_zond_token, (GDestroyNotify) pdf_zond_free_token_array);

    pdf_token tok = PDF_TOK_NULL;

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
    }

    return arr_zond_token;
}


static void
pdf_zond_insert_keyword( GArray* arr_zond_token, const gchar* keyword, gint index )
{
    ZondToken zond_token;

    zond_token.tok = PDF_TOK_KEYWORD;
    zond_token.s = g_strdup( keyword );

    g_array_insert_val( arr_zond_token, index, zond_token );

    return;
}


static void
pdf_zond_invalidate_token( GArray* arr_zond_token, gint index, gint len )
{
    for ( gint u = index; u > index - len; u-- )
    {
        ((ZondToken*) arr_zond_token->data)[u].tok = PDF_NUM_TOKENS;
    }

    return;
}


void
pdf_zond_filter_text( GArray* arr_zond_token, gint flags )
{
    struct TextState
    {
        gint Tc;
        gint Tw;
        gint Tz;
        gint TL;
        gint Tf;
        gint Tr;
        gint Ts;

        gint Tr_act;

        gint BT;

        gint Td;
        gint TD;
        gint Tm;
        gint TAst;
    } text_state = { -1, -1, -1, -1, -1, -1, -1, 0, -1, -1, -1, -1, -1 };

    for ( gint i = 0; i < arr_zond_token->len; i++ )
    {
        ZondToken zond_token = g_array_index( arr_zond_token, ZondToken, i );

        if ( (zond_token.tok == PDF_TOK_KEYWORD) )
        {
            if ( !g_strcmp0( zond_token.s, "Tc" ) )
            {
                if ( text_state.Tc != -1 ) pdf_zond_invalidate_token( arr_zond_token, text_state.Tc, 2 );

                text_state.Tc = i;
            }
            else if ( !g_strcmp0( zond_token.s, "Tw" ) )
            {
                if ( text_state.Tw != -1 ) pdf_zond_invalidate_token( arr_zond_token, text_state.Tw, 2 );

                text_state.Tw = i;
            }
            else if ( !g_strcmp0( zond_token.s, "Tz" ) )
            {
                if ( text_state.Tz != -1 ) pdf_zond_invalidate_token( arr_zond_token, text_state.Tz, 2 );

                text_state.Tz = i;
            }
            else if ( !g_strcmp0( zond_token.s, "TL" ) )
            {
                if ( text_state.TL != -1 ) pdf_zond_invalidate_token( arr_zond_token, text_state.TL, 2 );

                text_state.TL = i;
            }
            else if ( !g_strcmp0( zond_token.s, "Tf" ) )
            {
                if ( text_state.Tf != -1 ) pdf_zond_invalidate_token( arr_zond_token, text_state.Tf, 3 );

                text_state.Tf = i;
            }
            else if ( !g_strcmp0( zond_token.s, "Tr" ) )
            {
                ZondToken zond_token_prev = { 0 };

                if ( text_state.Tr != -1 ) pdf_zond_invalidate_token( arr_zond_token, text_state.Tr, 2 );

                text_state.Tr = i;

                zond_token_prev = g_array_index( arr_zond_token, ZondToken, i - 1 );
                text_state.Tr_act = zond_token_prev.i;
            }
            else if ( !g_strcmp0( zond_token.s, "Ts" ) )
            {
                if ( text_state.Ts != -1 ) pdf_zond_invalidate_token( arr_zond_token, text_state.Ts, 2 );

                text_state.Ts = i;
            }

            //nur merken, wo ist, ob gelöscht wird bei ET entschieden
            else if ( !g_strcmp0( zond_token.s, "BT" ) ) text_state.BT = i;

            else if ( !g_strcmp0( zond_token.s, "Td" ) )
            {
                if ( text_state.Td != -1 ) pdf_zond_invalidate_token( arr_zond_token, text_state.Td, 3 );

                text_state.Td = i;
            }
            else if ( !g_strcmp0( zond_token.s, "TD" ) )
            {
                if ( text_state.TD != -1 ) pdf_zond_invalidate_token( arr_zond_token, text_state.TD, 3 );

                text_state.TD = i;
            }
            else if ( !g_strcmp0( zond_token.s, "Tm" ) )
            {
                if ( text_state.Tm != -1 ) pdf_zond_invalidate_token( arr_zond_token, text_state.Tm, 7 );

                text_state.Tm = i;
            }
            else if ( !g_strcmp0( zond_token.s, "TAst" ) )
            {
                if ( text_state.TAst != -1 ) pdf_zond_invalidate_token( arr_zond_token, text_state.TAst, 1 );

                text_state.TAst = i;
            }


            else if ( !g_strcmp0( zond_token.s, "Tj" ) ||
                    !g_strcmp0( zond_token.s, "'" ) ||
                    !g_strcmp0( zond_token.s, """" ) ||
                    !g_strcmp0( zond_token.s, "TJ" ) )
            {
                //soll Text gefiltert werden?
                if ( flags == 3 || (flags == 1 && text_state.Tr_act != 3) ||
                        (flags == 2 && text_state.Tr_act == 3) ) //löschen!
                {
                    //Löschen bzw. ersetzen
                    if ( !g_strcmp0( zond_token.s, "Tj" ) )
                    {
                        g_array_remove_range( arr_zond_token, i - 1, 2 );
                        i = i - 2;
                    }
                    else if ( !g_strcmp0( zond_token.s, "'" ) )
                    {
                        g_array_remove_range( arr_zond_token, i - 1, 2 );

                        pdf_zond_insert_keyword( arr_zond_token, "T*", i - 1 );

                        text_state.TAst = i - 1;

                        i--; //auf i - 1 gehen; array eins kürzer geworden!
                    }
                    else if ( !g_strcmp0( zond_token.s, """" ) )
                    {
                        pdf_zond_insert_keyword( arr_zond_token, "Tw", i - 2 );
                        g_array_remove_range( arr_zond_token, i, 2 );
                        pdf_zond_insert_keyword( arr_zond_token, "Tc", i );
                        pdf_zond_insert_keyword( arr_zond_token, "T*", i + 1 );

                        text_state.Tw = i - 2;
                        text_state.Tc = i;
                        text_state.TAst = i + 1;

                        i++;
                    }
                    else if ( !g_strcmp0( zond_token.s, "TJ" ) )
                    {
                        //Beginn array finden
                        ZondToken zond_token_begin;
                        gint begin = i;
                        do
                        {
                            begin--;
                            zond_token_begin = g_array_index( arr_zond_token, ZondToken, begin );
                        } while ( zond_token_begin.tok != PDF_TOK_OPEN_ARRAY );

                        g_array_remove_range( arr_zond_token, begin, i - begin + 1 );

                        i = i - begin + 1;
                    }
                }
                else //Text wird angezeigt
                {
                    //etwaig gesetzte text_states resetten
                    text_state.Tc = -1;
                    text_state.Tw = -1;
                    text_state.Tz = -1;
                    text_state.TL = -1;
                    text_state.Tf = -1;
                    text_state.Tr = -1;
                    text_state.Ts = -1;

                    text_state.BT = -1;

                    text_state.Td = -1;
                    text_state.TD = -1;
                    text_state.Tm = -1;
                    text_state.TAst = -1;
                }
            }

            else if (!g_strcmp0( zond_token.s, "ET" ) )
            {
                //BT löschen, wenn nicht resetted
                if ( text_state.BT != -1 )
                {
                    pdf_zond_invalidate_token( arr_zond_token, text_state.BT, 1 );
                    //und ET löschen
                    pdf_zond_invalidate_token( arr_zond_token, i, 1 );
                }

                //Text Pos-Operatoren löschen
                if ( text_state.Td != -1 ) pdf_zond_invalidate_token( arr_zond_token, text_state.Td, 3 );
                if ( text_state.TD != -1 ) pdf_zond_invalidate_token( arr_zond_token, text_state.TD, 3 );
                if ( text_state.Tm != -1 ) pdf_zond_invalidate_token( arr_zond_token, text_state.Tm, 7 );
                if ( text_state.TAst != -1 ) pdf_zond_invalidate_token( arr_zond_token, text_state.TAst, 1 );

                //und resetten
                text_state.BT = -1;

                text_state.Td = -1;
                text_state.TD = -1;
                text_state.Tm = -1;
                text_state.TAst = -1;
            }
        }
        else if ( zond_token.tok == PDF_TOK_EOF )
        {
            //noch gesetzte Text-State-Ops löschen
            if ( text_state.Tc != -1 ) pdf_zond_invalidate_token( arr_zond_token, text_state.Tc, 2 );
            if ( text_state.Tw != -1 ) pdf_zond_invalidate_token( arr_zond_token, text_state.Tw, 2 );
            if ( text_state.Tz != -1 ) pdf_zond_invalidate_token( arr_zond_token, text_state.Tz, 2 );
            if ( text_state.TL != -1 ) pdf_zond_invalidate_token( arr_zond_token, text_state.TL, 2 );
            if ( text_state.Tf != -1 ) pdf_zond_invalidate_token( arr_zond_token, text_state.Tf, 3 );
            if ( text_state.Tr != -1 ) pdf_zond_invalidate_token( arr_zond_token, text_state.Tr, 2 );
            if ( text_state.Ts != -1 ) pdf_zond_invalidate_token( arr_zond_token, text_state.Ts, 2 );

            //etwaig gesetzte text_states resetten
            text_state.Tc = -1;
            text_state.Tw = -1;
            text_state.Tz = -1;
            text_state.TL = -1;
            text_state.Tf = -1;
            text_state.Tr = -1;
            text_state.Ts = -1;
        }
    }

    return;
}


fz_buffer*
pdf_zond_reassemble_buffer( fz_context* ctx, GArray* arr_zond_token, gchar** errmsg )
{
    fz_buffer* fzbuf = NULL;

    fz_try( ctx ) fzbuf = fz_new_buffer( ctx, 1012 );
    fz_catch( ctx ) ERROR_MUPDF_R( "fz_new_buffer", NULL )

    for ( gint i = 0; i < arr_zond_token->len; i++ )
    {
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
                fz_append_data(ctx, fzbuf, zond_token.gba->data, zond_token.gba->len );
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
                fz_append_printf(ctx, fzbuf, "%s", zond_token.s);
                if ( !g_strcmp0( zond_token.s, "ID" ) ) fz_append_byte( ctx, fzbuf, ' ' );
                fz_append_byte( ctx, fzbuf, '\n' );
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


//immer "allgemeine" States (cm)
//1: Text
static void
zond_pdf_reset_gs( GPtrArray* arr_gs, gint flags )
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
zond_pdf_restore_gs( GPtrArray* arr_gs )
{
    g_ptr_array_remove_index( arr_gs, arr_gs->len - 1 );

    struct GraphicsState* GS = g_ptr_array_index( arr_gs, arr_gs->len - 1 );

    return GS;
}


static struct GraphicsState*
zond_pdf_store_gs( GPtrArray* arr_gs, gint i )
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
pdf_zond_free_gs( gpointer data )
{
    struct GraphicsState* GS = (struct GraphicsState*) data;

    g_array_unref( GS->arr_cm );

    g_free( GS );

    return;
}


void
pdf_zond_filter_content_stream( GArray* arr_zond_token, gint flags )
{
    struct BT
    {
        gint BT;

        gint Td;
        gint TD;
        gint Tm;
        gint TAst;
    } BT = { -1, -1, -1, -1, -1 };

    struct BI
    {
        gint BI;
        gint ID;
    } BI = { -1, -1 };

    gint BX = -1;

    GPtrArray* arr_gs = g_ptr_array_new( );
    g_ptr_array_set_free_func( arr_gs, pdf_zond_free_gs );

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

    gint last_token = -1;

    //Tokens durchgehen
    for ( gint i = 0; i < arr_zond_token->len; i++ )
    {
        ZondToken zond_token = g_array_index( arr_zond_token, ZondToken, i );

        if ( (zond_token.tok == PDF_TOK_KEYWORD) )
        {
            if ( !g_strcmp0( zond_token.s, "q" ) ) GS_act = zond_pdf_store_gs( arr_gs, i );
            else if ( !g_strcmp0( zond_token.s, "cm" ) ) g_array_append_val( GS_act->arr_cm, i );
            else if ( !g_strcmp0( zond_token.s, "Tc" ) )
            {
                //nur wenn Operand in gleicher Ebene bereits gesetzt wurde, steht fest, das vorheriger nutzlos ist
                if ( GS_act->Tc >= GS_act->begin )
                        pdf_zond_invalidate_token( arr_zond_token, GS_act->Tc, 2 );

                GS_act->Tc = i;
            }
            else if ( !g_strcmp0( zond_token.s, "Tw" ) )
            {
                if ( GS_act->Tw >= GS_act->begin ) //stammt aus der gleichen Ebene
                        pdf_zond_invalidate_token( arr_zond_token, GS_act->Tw, 2 );

                GS_act->Tw = i;
            }
            else if ( !g_strcmp0( zond_token.s, "Tz" ) )
            {
                if ( GS_act->Tz >= GS_act->begin ) //stammt aus der gleichen Ebene
                        pdf_zond_invalidate_token( arr_zond_token, GS_act->Tz, 2 );

                GS_act->Tz = i;
            }
            else if ( !g_strcmp0( zond_token.s, "TL" ) )
            {
                if ( GS_act->TL >= GS_act->begin ) //stammt aus der gleichen Ebene
                        pdf_zond_invalidate_token( arr_zond_token, GS_act->TL, 2 );

                GS_act->TL = i;
            }
            else if ( !g_strcmp0( zond_token.s, "Tf" ) )
            {
                if ( GS_act->Tf >= GS_act->begin ) //stammt aus der gleichen Ebene
                        pdf_zond_invalidate_token( arr_zond_token, GS_act->Tf, 3 );

                GS_act->Tf = i;
            }
            else if ( !g_strcmp0( zond_token.s, "Tr" ) )
            {
                ZondToken zond_token_prev = { 0 };

                zond_token_prev = g_array_index( arr_zond_token, ZondToken, i - 1 );
                GS_act->Tr_act = zond_token_prev.i;

                if ( GS_act->Tr >= GS_act->begin ) //stammt aus der gleichen Ebene
                        pdf_zond_invalidate_token( arr_zond_token, GS_act->Tr, 2 );

                GS_act->Tr = i;
            }
            else if ( !g_strcmp0( zond_token.s, "Ts" ) )
            {
                if ( GS_act->Ts >= GS_act->begin ) //stammt aus der gleichen Ebene
                        pdf_zond_invalidate_token( arr_zond_token, GS_act->Ts, 2 );

                GS_act->Ts = i;
            }

            //nur merken, wo ist, ob gelöscht wird bei ET entschieden
            else if ( !g_strcmp0( zond_token.s, "BT" ) ) //initialisieren
            {
                BT.BT = i;
                BT.Td = -1;
                BT.TD = -1;
                BT.Tm = -1;
                BT.TAst = -1;
            }

            else if ( !g_strcmp0( zond_token.s, "Td" ) )
            {
                if ( BT.Td != -1 ) pdf_zond_invalidate_token( arr_zond_token, BT.Td, 3 );

                BT.Td = i;
            }
            else if ( !g_strcmp0( zond_token.s, "TD" ) )
            {
                if ( BT.TD != -1 ) pdf_zond_invalidate_token( arr_zond_token, BT.TD, 3 );

                BT.TD = i;
            }
            else if ( !g_strcmp0( zond_token.s, "Tm" ) )
            {
                if ( BT.Tm != -1 ) pdf_zond_invalidate_token( arr_zond_token, BT.Tm, 7 );

                BT.Tm = i;
            }
            else if ( !g_strcmp0( zond_token.s, "TAst" ) )
            {
                if ( BT.TAst != -1 ) pdf_zond_invalidate_token( arr_zond_token, BT.TAst, 1 );

                BT.TAst = i;
            }


            else if ( !g_strcmp0( zond_token.s, "Tj" ) ||
                    !g_strcmp0( zond_token.s, "'" ) ||
                    !g_strcmp0( zond_token.s, """" ) ||
                    !g_strcmp0( zond_token.s, "TJ" ) )
            {
                //soll Text gefiltert werden?
                if ( flags == 3 || (flags == 1 && GS_act->Tr_act != 3) ||
                        (flags == 2 && GS_act->Tr_act == 3) ) //löschen!
                {
                    //Löschen bzw. ersetzen
                    if ( !g_strcmp0( zond_token.s, "Tj" ) )
                    {
                        g_array_remove_range( arr_zond_token, i - 1, 2 );
                        i = i - 2;
                    }
                    else if ( !g_strcmp0( zond_token.s, "'" ) )
                    {
                        g_array_remove_range( arr_zond_token, i - 1, 2 );

                        pdf_zond_insert_keyword( arr_zond_token, "T*", i - 1 );

                        if ( BT.TAst != -1 ) pdf_zond_invalidate_token( arr_zond_token, BT.TAst, 1 );
                        BT.TAst = i - 1;

                        i--; //auf i - 1 gehen; array eins kürzer geworden!
                    }
                    else if ( !g_strcmp0( zond_token.s, """" ) )
                    {
                        pdf_zond_insert_keyword( arr_zond_token, "Tw", i - 2 );
                        g_array_remove_range( arr_zond_token, i, 2 );
                        pdf_zond_insert_keyword( arr_zond_token, "Tc", i );
                        pdf_zond_insert_keyword( arr_zond_token, "T*", i + 1 );

                        if ( GS_act->Tw != -1 && GS_act->Tw >= GS_act->begin ) //stammt aus der gleichen Ebene
                                pdf_zond_invalidate_token( arr_zond_token, GS_act->Tw, 2 );
                        GS_act->Tw = i - 2;

                        if ( GS_act->Tc != -1 && GS_act->Tc >= GS_act->begin ) //stammt aus der gleichen Ebene
                                pdf_zond_invalidate_token( arr_zond_token, GS_act->Tc, 2 );
                        GS_act->Tw = i;

                        if ( BT.TAst != -1 ) pdf_zond_invalidate_token( arr_zond_token, BT.TAst, 1 );
                        BT.TAst = i + 1;

                        i++;
                    }
                    else if ( !g_strcmp0( zond_token.s, "TJ" ) )
                    {
                        //Beginn array finden
                        ZondToken zond_token_begin;
                        gint begin = i;
                        do
                        {
                            begin--;
                            zond_token_begin = g_array_index( arr_zond_token, ZondToken, begin );
                        } while ( zond_token_begin.tok != PDF_TOK_OPEN_ARRAY );

                        g_array_remove_range( arr_zond_token, begin, i - begin + 1 );

                        i = begin - 1;
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

                    //BX resetten
                    BX = -1;

                    //reset graphics_state
                    zond_pdf_reset_gs( arr_gs, 1 );
                }
            }

            else if ( !g_strcmp0( zond_token.s, "ET" ) )
            {
                //BT löschen, wenn nicht resetted
                if ( BT.BT != -1 )
                {
                    pdf_zond_invalidate_token( arr_zond_token, BT.BT, 1 );
                    //und ET löschen
                    pdf_zond_invalidate_token( arr_zond_token, i, 1 );
                }

                //Text Pos-Operatoren löschen
                if ( BT.Td != -1 ) pdf_zond_invalidate_token( arr_zond_token, BT.Td, 3 );
                if ( BT.TD != -1 ) pdf_zond_invalidate_token( arr_zond_token, BT.TD, 3 );
                if ( BT.Tm != -1 ) pdf_zond_invalidate_token( arr_zond_token, BT.Tm, 7 );
                if ( BT.TAst != -1 ) pdf_zond_invalidate_token( arr_zond_token, BT.TAst, 1 );
            }

            else if ( !g_strcmp0( zond_token.s, "BI" ) )
            {
                BI.BI = i;
                BI.ID = -1;
            }
            else if ( !g_strcmp0( zond_token.s, "ID" ) ) BI.ID = i;
            else if ( !g_strcmp0( zond_token.s, "EI" ) )
            {
                if (BI.ID == i - 1 ) //nix dazwischen
                {
                    for ( gint u = BI.BI; u <= i; u++ )
                            pdf_zond_invalidate_token( arr_zond_token, i, i - BI.BI + 1 );
                }
            }

            else if ( !g_strcmp0( zond_token.s, "BX" ) ) BX = i;
            else if ( !g_strcmp0( zond_token.s, "EX" ) )
            {
                //nix dazwischen
                if ( BX == i - 1 ) pdf_zond_invalidate_token( arr_zond_token, i, 2 );
           /*     else
                {
                    gboolean stay = FALSE;

                    for ( gint u = BX; u < EX; u++ )
                    {
                        if ( g_array_index( arr_zond_token, ZondToken, u ).tok != PDF_NUM_TOKENS )
                        {
                            stay = TRUE;
                            break;
                        }
                    }

                }*/
            }
            else if ( !g_strcmp0( zond_token.s, "Q" ) )
            {
                if ( GS_act->q != -1 )
                {
                    pdf_zond_invalidate_token( arr_zond_token, GS_act->q, 1 );
                    pdf_zond_invalidate_token( arr_zond_token, i, 1 );
                }

                for ( gint u = GS_act->arr_cm->len - 1; u >= 0; u-- )
                {
                    gint cm = g_array_index( GS_act->arr_cm, gint, u );
                    if ( cm >= GS_act->begin ) pdf_zond_invalidate_token( arr_zond_token, cm, 7 );
                }
                //noch gesetzte Text-State-Ops löschen (wenn aus aktueller Ebene - dann auch != -1!
                if ( GS_act->Tc >= GS_act->begin ) pdf_zond_invalidate_token( arr_zond_token, GS_act->Tc, 2 );
                if ( GS_act->Tw >= GS_act->begin ) pdf_zond_invalidate_token( arr_zond_token, GS_act->Tw, 2 );
                if ( GS_act->Tz >= GS_act->begin ) pdf_zond_invalidate_token( arr_zond_token, GS_act->Tz, 2 );
                if ( GS_act->TL >= GS_act->begin ) pdf_zond_invalidate_token( arr_zond_token, GS_act->TL, 2 );
                if ( GS_act->Tf >= GS_act->begin ) pdf_zond_invalidate_token( arr_zond_token, GS_act->Tf, 3 );
                if ( GS_act->Tr >= GS_act->begin ) pdf_zond_invalidate_token( arr_zond_token, GS_act->Tr, 2 );
                if ( GS_act->Ts >= GS_act->begin ) pdf_zond_invalidate_token( arr_zond_token, GS_act->Ts, 2 );

                //GS zurücksetzen
                GS_act = zond_pdf_restore_gs( arr_gs );
            }

            //irgendein token
            else
            {
                zond_pdf_reset_gs( arr_gs, 0 ); //Text ist ja schon behandelt; irgendwann auch weitere ausgenommen...
                BX = -1;
            }

            last_token = i;
        }
        else if ( zond_token.tok == PDF_TOK_EOF )
        {
            if ( GS_act->q != -1 ) pdf_zond_invalidate_token( arr_zond_token, GS_act->q, 1 );

            for ( gint u = GS_act->arr_cm->len - 1; u >= 0; u --)
            {
                gint cm = g_array_index( GS_act->arr_cm, gint, u );
                if ( cm >= GS_act->begin ) pdf_zond_invalidate_token( arr_zond_token, cm, 7 );
            }

            //noch gesetzte Text-State-Ops löschen
            if ( GS_act->Tc != -1 ) pdf_zond_invalidate_token( arr_zond_token, GS_act->Tc, 2 );
            if ( GS_act->Tw != -1 ) pdf_zond_invalidate_token( arr_zond_token, GS_act->Tw, 2 );
            if ( GS_act->Tz != -1 ) pdf_zond_invalidate_token( arr_zond_token, GS_act->Tz, 2 );
            if ( GS_act->TL != -1 ) pdf_zond_invalidate_token( arr_zond_token, GS_act->TL, 2 );
            if ( GS_act->Tf != -1 ) pdf_zond_invalidate_token( arr_zond_token, GS_act->Tf, 3 );
            if ( GS_act->Tr != -1 ) pdf_zond_invalidate_token( arr_zond_token, GS_act->Tr, 2 );
            if ( GS_act->Ts != -1 ) pdf_zond_invalidate_token( arr_zond_token, GS_act->Ts, 2 );
        }
    }

    g_ptr_array_unref( arr_gs );

    return;
}


/*
// (War nötig, um von tesseract erstellte Pdf mit darzustellen)
gint
pdf_show_hidden_text( fz_context* ctx, pdf_obj* page_ref, gchar** errmsg )
{
    gint rc = 0;
    fz_buffer* buf = NULL;
    guchar* data = NULL;
    gchar* pos = NULL;
    gchar* end_Tr = NULL;
    gint Tr = 0;
    size_t size = 0;

    //Zunächst content-stream (Tr auf 0 setzen)
    buf = pdf_get_content_stream_as_buffer( ctx, page_ref, errmsg );
    if ( !buf ) ERROR_PAO( "pdf_ocr_get_content_stream_as_buffer" )

    size = fz_buffer_storage( ctx, buf, &data );
    pos = (gchar*) data;
    while ( pos < (gchar*) data + size - 1 )
    {
        Tr = find_next_Tr( pos, (gchar*) data + size - pos - 1, &end_Tr );
        pos = end_Tr + 1;
        if ( Tr == 3 )
        {
            gchar* ptr = pos - 3;
            while ( is_white( ptr ) ) ptr--;
            *ptr = '0';
        }
    }

    rc = pdf_update_content_stream( ctx, page_ref, buf, errmsg );
    fz_drop_buffer( ctx, buf );
    if ( rc ) ERROR_PAO( "pdf_update_content_stream" )

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
    pdf_drop_obj( ctx, font_name );

    return 0;
}
*/

