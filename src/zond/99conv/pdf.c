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
    };
} ZondToken;


static void
pdf_zond_free_token_array( gpointer data )
{
    ZondToken* zond_token = (ZondToken*) data;

    if ( zond_token->tok == PDF_TOK_STRING || zond_token->tok == PDF_TOK_NAME ||
            zond_token->tok == PDF_TOK_KEYWORD ) g_free( zond_token->s );

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
        else if ( tok == PDF_TOK_STRING || tok == PDF_TOK_NAME ||
                tok == PDF_TOK_KEYWORD ) zond_token.s = g_strdup( lxb.scratch );

        pdf_lexbuf_fin( ctx, &lxb );

        g_array_append_val( arr_zond_token, zond_token );
    }

    return arr_zond_token;

}


void
pdf_zond_filter_token( GArray* arr_zond_token, gint flags )
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
                if ( text_state.Tf != -1 ) pdf_zond_invalidate_token( arr_zond_token, text_state.Tf, 2 );

                text_state.Tf = i;
            }
            else if ( !g_strcmp0( zond_token.s, "Tr" ) )
            {
                if ( text_state.Tr != -1 ) pdf_zond_invalidate_token( arr_zond_token, text_state.Tr, 2 );

                text_state.Tr = i;
                text_state.Tr_act = zond_token.i;
            }
            else if ( !g_strcmp0( zond_token.s, "Ts" ) )
            {
                if ( text_state.Ts != -1 ) pdf_zond_invalidate_token( arr_zond_token, text_state.Ts, 2 );

                text_state.Ts = i;
            }

            //nur merken, wo ist, ob gelöscht wird bei ET entschieden
            else if ( !g_strcmp0( zond_token.s, "BT" ) ) text_state.BT = i;

            else if ( !g_strcmp0( zond_token.s, "Tj" ) ||
                    !g_strcmp0( zond_token.s, "'" ) ||
                    !g_strcmp0( zond_token.s, """" ) ||
                    !g_strcmp0( zond_token.s, "TJ" ) )
            {
                //soll Text gefiltert werden?
                if ( flags == 3 || (flags == 1 && text_state.Tr_act != 3) ||
                        (flags == 2 && text_state.Tr_act == 3) ) //nicht anzeigen!
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
            if ( text_state.Tf != -1 ) pdf_zond_invalidate_token( arr_zond_token, text_state.Tf, 2 );
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

