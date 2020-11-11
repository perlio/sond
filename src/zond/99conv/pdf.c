#include "../error.h"

#include "../99conv/mupdf.h"
#include "../99conv/general.h"

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <glib/gstdio.h>

#include <stdio.h>


/** Nicht thread-safe  **/
gint
pdf_document_get_dest( fz_context* ctx, pdf_document* pdf_doc, gint page_doc,
        gpointer* ret, gboolean first_occ, gchar** errmsg )
{
    pdf_obj* obj_dest_tree = NULL;
    pdf_obj* obj_key = NULL;
    pdf_obj* obj_val = NULL;
    pdf_obj* obj_array = NULL;
    pdf_obj* obj_page = NULL;
    gint num = 0;
    const gchar* dest_found = NULL;

    fz_try( ctx ) obj_dest_tree = pdf_load_name_tree( ctx, pdf_doc, PDF_NAME(Dests) );
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

        fz_try( ctx ) num = pdf_lookup_page_number( ctx, pdf_doc, obj_page );
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
pdf_get_page_num_from_dest_doc( fz_context* ctx, fz_document* doc, const gchar* dest, gchar** errmsg )
{
    pdf_obj* obj_dest_string = NULL;
    pdf_obj* obj_dest = NULL;
    pdf_obj* pageobj = NULL;
    gint page_num = 0;

    pdf_document* pdf_doc = pdf_specifics( ctx, doc );

    obj_dest_string = pdf_new_string( ctx, dest, strlen( dest ) );
    fz_try( ctx ) obj_dest = pdf_lookup_dest( ctx, pdf_doc, obj_dest_string);
    fz_always( ctx ) pdf_drop_obj( ctx, obj_dest_string );
    fz_catch( ctx ) ERROR_MUPDF( "pdf_lookup_dest" )

	pageobj = pdf_array_get( ctx, obj_dest, 0 );

	if ( pdf_is_int( ctx, pageobj ) ) page_num = pdf_to_int( ctx, pageobj );
	else
	{
		fz_try( ctx ) page_num = pdf_lookup_page_number( ctx, pdf_doc, pageobj );
		fz_catch( ctx ) ERROR_MUPDF( "pdf_lookup_page_number" )
	}

    return page_num;
}


gint
pdf_get_page_num_from_dest( fz_context* ctx, const gchar* rel_path,
        const gchar* dest, gchar** errmsg )
{
    fz_document* doc = NULL;
    gint page_num = 0;

    fz_try( ctx ) doc = fz_open_document( ctx, rel_path );
    fz_catch( ctx ) ERROR_MUPDF( "fz_open_document" )

    page_num = pdf_get_page_num_from_dest_doc( ctx, doc, dest, errmsg );
	fz_drop_document( ctx, doc );
    if ( page_num < 0 ) ERROR_PAO( "get_page_num_from_dest_doc" )

    return page_num;
}


float
pdf_get_rotate( fz_context* ctx, pdf_obj* page )
{
    pdf_obj* rotate_obj = NULL;

    rotate_obj = pdf_dict_get( ctx, page, PDF_NAME(Rotate) );

    return pdf_to_real( ctx, rotate_obj );
}


gint
pdf_update_content_stream( fz_context* ctx, pdf_obj* page_ref,
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


gint
pdf_print_token( fz_context* ctx, fz_stream* stream, gchar** errmsg )
{
    pdf_token tok = PDF_TOK_NULL;

    while ( tok != PDF_TOK_EOF )
    {
        pdf_lexbuf lxb;
        pdf_lexbuf_init( ctx, &lxb, PDF_LEXBUF_SMALL );

        tok = pdf_lex( ctx, stream, &lxb );

        printf( "%i  %s\n", tok, lxb.scratch );

        pdf_lexbuf_fin( ctx, &lxb );
    }


    return 0;

}

gint
pdf_copy_page( fz_context* ctx, pdf_document* doc_src, gint page_from,
        gint page_to, pdf_document* doc_dest, gint page,
        gchar** errmsg )
{
    //  Seiten in doc_src einfügen
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


/** stream filtern **/

static gint
pdf_append_filtered_TO( fz_context* ctx, fz_buffer* buf_new, gchar* BT,
        gchar* ET, gboolean vis_TO_BT, gint flags, gchar** errmsg )
{
    gboolean vis_act = vis_TO_BT;
    gchar* ptr = BT;

    while ( ptr < ET )
    {

    }

    return 0;
}


static gboolean
text_showing_op( gchar* begin, gchar* end )
{
    gchar* ptr = NULL;

    ptr = begin;

    while ( ptr < end )
    {
        if ( *ptr == '"' || *ptr == 39 ) return TRUE;
        if ( *ptr == 'T' )
        {
            if ( *(ptr + 1) == 'J' || *(ptr + 1) == 'j' ) return TRUE;
        }

        ptr++;
    }

    return FALSE;
}


static gint
find_next_Tr( gchar* buf, size_t size, gchar** end_Tr )
{
    gchar* ptr = NULL;
    gint64 num = 0;
    gchar* endptr = NULL;

    ptr = buf;

    while ( ptr < buf + size )
    {
        //1. Zahl
        num = g_ascii_strtoll( ptr, &endptr, 10 );
        if ( ptr != endptr ) //Zahl gefunden
        { //vorspulen
            ptr = endptr;
            while ( is_white( ptr) ) ptr++;
        }
        else
        {
            ptr++;
            continue;
        }

        //2. Tr
        if ( *ptr == 'T' && *(ptr + 1) == 'r' )
        {
            *end_Tr = ptr + 1;
            return (gint) num;
        }
    }

    //nix gefunden
    *end_Tr = buf + size;

    return -1;
}


static gboolean
text_object_uniform_vis( gchar* BT, gchar* ET, gboolean vis_BT, gboolean* vis_ET,
        gboolean* has_text )
{
    gboolean uniform = TRUE;
    gchar* ptr = NULL;
    gchar* end_ptr = NULL;
    gint Tr_act = -1;
    gint Tr = -1;

    *has_text = FALSE;
    *vis_ET = vis_BT;

    //Sichtbarkeit des ersten angezeigten Textes ermitteln
    ptr = BT;
    while ( ptr < ET )
    {
        Tr_act = find_next_Tr( ptr, ET - ptr, &end_ptr );

        if ( text_showing_op( ptr, end_ptr ) )
        {
            if ( *has_text && Tr_act != -1 )
            {
                if ( (Tr == 3) == (*vis_ET) ) uniform = FALSE;
            }

            *has_text = TRUE;
        }

        if ( Tr_act >= 0 ) Tr = Tr_act;

        if ( Tr >= 0 ) *vis_ET = (Tr == 3) ? FALSE : TRUE;

        ptr = end_ptr + 1;
    }

    return uniform;
}


static gchar*
find_next_ET( gchar* buf, size_t size )
{
    gchar* ptr = NULL;
    gboolean in_string = FALSE;

    ptr = buf;

    while ( ptr < buf + size - 1 )
    {
        if ( *ptr == '(' ) in_string = TRUE;
        if ( *ptr == ')' ) in_string = FALSE;

        if ( !in_string && *ptr == 'E' && *(ptr + 1) == 'T' ) return ptr + 1;

        ptr++;
    }

    //Nix gefunden darf eigentlich nicht sein
    return buf + size - 1;
}


gchar*
find_next_BT( gchar* buf, size_t size, gchar** ET )
{
    gchar* ptr = NULL;
    gchar* BT = NULL;

    ptr = buf;

    while ( ptr < buf + size - 1 )
    {
        if ( *ptr == 'B' && *(ptr + 1) == 'T' )
        {
            BT = ptr;
            if ( ET ) *ET = find_next_ET( BT, buf + size - ptr );

            return BT;
        }
        ptr++;
    }

    //nix gefunden
    if ( ET ) *ET = buf + size - 1;

    return buf + size - 1;
}


fz_buffer*
pdf_get_content_stream_as_buffer( fz_context* ctx, pdf_obj* page_ref,
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
/*
        // Test
        gint rc = pdf_print_token( ctx, stream, errmsg );
        if ( rc ) ERROR_PAO_R( "pdf_print_token", NULL )
*/
        buf = fz_read_all( ctx, stream, 1024 );
    }
    fz_always( ctx ) fz_drop_stream( ctx, stream );
    fz_catch( ctx ) ERROR_MUPDF_R( "open and read stream", NULL )

    return buf;
}


/** Flags:
*** 1<<0:   allg. Stream
*** 1<<1:   TextObjekte sichtbar
*** 1<<2:   TextObjekte unsichtbar  **/
gint
pdf_filter_stream( fz_context* ctx, pdf_obj* page_ref, gint flags, gchar** errmsg )
{
    gint rc = 0;
    fz_buffer* buf = NULL;
    fz_buffer* buf_new = NULL;
    gchar* data = NULL;
    gchar* BT = NULL;
    gchar* pos = NULL;
    gchar* ET = NULL;
    gchar* end_Tr = NULL;
    gint last_Tr = -1;
    size_t size = 0;
    gboolean vis_TO = TRUE;

    if ( flags == 7 ) return 0;

    fz_try( ctx ) buf_new = fz_new_buffer( ctx, 1024 );
    fz_catch( ctx ) ERROR_MUPDF( "fz_new_buffer" )

    buf = pdf_get_content_stream_as_buffer( ctx, page_ref, errmsg );
    if ( !buf )
    {
        fz_drop_buffer( ctx, buf_new );
        ERROR_PAO( "pdf_get_content_stream_as_buffer" )
    }

    size = fz_buffer_storage( ctx, buf, (guchar**) &data );
    pos = data;

    while ( pos < data + size - 1 )
    {
        gboolean uniform = FALSE;
        gboolean has_text = FALSE;
        gchar* ptr = NULL;

        BT = find_next_BT( pos, size - (pos - data) , &ET );

        //wenn "normaler Stream" gewählt, dann alles außer Text-Objecten
        if ( flags & 1 )
        {
            fz_try( ctx ) fz_append_data( ctx, buf_new, pos, BT - pos );
            fz_catch( ctx )
            {
                fz_drop_buffer( ctx, buf );
                fz_drop_buffer( ctx, buf_new );
                ERROR_MUPDF( "fz_append_data" )
            }
        }

        //falls "normaler" stream nicht kopiert werden soll UND erstes TextObject des streams...
        if ( !(flags & 1) && pos == data && BT != ET ) //gucken, ob vorher (innerhalb q/Q-Rahmen) cm vorhanden
        {
            ptr = BT - 1;

            while ( *ptr != 'q' )
            {
                if ( *ptr == 'm' && *(ptr - 1) == 'c' ) //cm gefunden
                {
                    ptr = ptr - 2;

                    //sechs Leerstellen zurückspulen
                    gint zaehler = 6;
                    gdouble cm[6] = { 0.0 };
                    while ( zaehler >= 0 )
                    {
                        if ( is_white( ptr ) )
                        {
                            zaehler--;
                            while ( is_white( ptr ) ) ptr--;
                            while ( !is_white( ptr ) ) ptr--;
                        }
                        cm[zaehler] = g_ascii_strtod( ptr, NULL );
                    }

                    gchar* cm_string = g_strdup_printf( "\n%g %g %g %g %g %g cm\n",
                            cm[0], cm[1], cm[2], cm[3], cm[4], cm[5] );

                    //Komma durch Punkt ersetzen
                    for ( gint i = 0; i < strlen( cm_string ); i++ )
                            if ( *(cm_string + i) == ',' ) *(cm_string + i) = '.';

                    fz_try( ctx ) fz_append_data( ctx, buf_new, cm_string, strlen( cm_string ) );
                    fz_catch( ctx )
                    {
                        fz_drop_buffer( ctx, buf );
                        fz_drop_buffer( ctx, buf_new );
                        ERROR_MUPDF( "fz_append_data" )
                    }
                    break;
                }

                ptr--;
            }
        }

        if ( flags & 6 )
        {
            gboolean last_vis_TO = FALSE;

            //vor dem aktuellen TextObject suchen
            ptr = pos;
            while ( ptr < BT )
            {
                gint search_Tr = find_next_Tr( ptr, BT - ptr, &end_Tr );
                if ( search_Tr >= 0 ) last_Tr = search_Tr;
                ptr = end_Tr + 1;
            }

            if ( last_Tr == 3 ) vis_TO = FALSE;
            else if ( last_Tr >= 0 ) vis_TO = TRUE;

            uniform = text_object_uniform_vis( BT, ET, vis_TO,
                    &last_vis_TO, &has_text );

            vis_TO = last_vis_TO;
        }

       if ( (!uniform && (flags & 6)) || //TextObject ist gemischt-vorläufig
                                            //nur entfernen, wenn beide Sorten
                                            //Text entfernt werden sollen
                (uniform && (flags & 2) && has_text && vis_TO ) ||
                (uniform && (flags & 4) && has_text && (!vis_TO)) ) //nur sichtbarer Text soll
                                                        //entfernt werden und Text ist unsichtbar
        {
            fz_try( ctx )
            {
                fz_append_data( ctx, buf_new, BT, ET - BT + 1 );
                fz_append_data( ctx, buf_new, "\n", 1 );
            }
            fz_catch( ctx )
            {
                fz_drop_buffer( ctx, buf );
                fz_drop_buffer( ctx, buf_new );
                ERROR_MUPDF( "fz_append_data" )
            }
        }

        pos = ET + 1;
    }

    fz_drop_buffer( ctx, buf );

    rc = pdf_update_content_stream( ctx, page_ref, buf_new, errmsg );
    fz_drop_buffer( ctx, buf_new );
    if ( rc ) ERROR_PAO( "pdf_update_content_stream" )

    return 0;
}

static gint
pdf_filter_buf( fz_context* ctx, fz_buffer* buf, fz_buffer* buf_new, gint flags,
        gchar** errmsg )
{
    gchar* data = NULL;
    gchar* BT = NULL;
    gchar* pos = NULL;
    gchar* ET = NULL;
    gchar* end_Tr = NULL;
    gint last_Tr = -1;
    size_t size = 0;
    gboolean vis_TO_BT = TRUE;

    size = fz_buffer_storage( ctx, buf, (guchar**) &data );
    pos = data;

    while ( pos < data + size - 1 )
    {
        gboolean uniform = FALSE;
        gboolean has_text = FALSE;
        gchar* ptr = NULL;

        BT = find_next_BT( pos, size - (pos - data) , &ET );

        //stream vor BT einfügen
        fz_try( ctx ) fz_append_data( ctx, buf_new, pos, BT - pos );
        fz_catch( ctx ) ERROR_MUPDF( "fz_append_data" )

        //vor dem aktuellen TextObject suchen
        ptr = pos;
        while ( ptr < BT )
        {
            gint search_Tr = find_next_Tr( ptr, BT - ptr, &end_Tr );
            if ( search_Tr >= 0 ) last_Tr = search_Tr;
            ptr = end_Tr + 1;
        }

        if ( last_Tr == 3 ) vis_TO_BT = FALSE;
        else if ( last_Tr >= 0 ) vis_TO_BT = TRUE;

        gboolean vis_TO = FALSE;

        uniform = text_object_uniform_vis( BT, ET, vis_TO_BT,
                &vis_TO, &has_text );

        if ( has_text && ((flags & 3) != 3) ) //nicht: kein Text oder aller Text soll weg
        {
            if ( uniform && (((flags & 1 ) && vis_TO) || ((flags & 2) && !vis_TO)) )
            {
                fz_try( ctx )
                {
                    fz_append_data( ctx, buf_new, BT, ET - BT + 1 );
                    fz_append_data( ctx, buf_new, "\n", 1 );
                }
                fz_catch( ctx ) ERROR_MUPDF( "fz_append_data" )
            }
            else if ( !uniform )
            {
                gint rc = 0;
                rc = pdf_append_filtered_TO( ctx, buf_new, BT, ET, vis_TO_BT, flags, errmsg );
                if ( rc ) ERROR_PAO( "pdf_filter_TO" )
            }
        }

        if ( uniform ) vis_TO_BT = vis_TO;

        pos = ET + 1;
    }

    return 0;
}


gint
pdf_remove_text( fz_context* ctx, pdf_obj* page_ref, gint flags, gchar** errmsg )
{
    gint rc = 0;
    fz_buffer* buf = NULL;
    fz_buffer* buf_new = NULL;

    if ( flags == 0 ) return 0;

    buf = pdf_get_content_stream_as_buffer( ctx, page_ref, errmsg );
    if ( !buf ) ERROR_PAO( "pdf_get_content_stream_as_buffer" )

    fz_try( ctx ) buf_new = fz_new_buffer( ctx, 1024 );
    fz_catch( ctx )
    {
        fz_drop_buffer( ctx, buf );
        ERROR_MUPDF( "fz_new_buffer" )
    }

    rc = pdf_filter_buf( ctx, buf, buf_new, flags, errmsg );
    fz_drop_buffer( ctx, buf );
    if ( rc )
    {
        fz_drop_buffer( ctx, buf_new );
        ERROR_PAO( "pdf_filter_buf" )
    }

    rc = pdf_update_content_stream( ctx, page_ref, buf_new, errmsg );
    fz_drop_buffer( ctx, buf_new );
    if ( rc ) ERROR_PAO( "pdf_update_content_stream" )

    return 0;
}


/* (War nötig, um von tesseract erstellte Pdf mit darzustellen)
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

gint
pdf_render_stext_page_direct( DocumentPage* document_page, gchar** errmsg )
{
    //structured text-device
    fz_device* s_t_device = NULL;

    fz_stext_options opts = { FZ_STEXT_DEHYPHENATE };

    fz_context* ctx = document_page->document->ctx;

    fz_try( ctx ) s_t_device = fz_new_stext_device( ctx, document_page->stext_page, &opts );
    fz_catch( ctx ) ERROR_MUPDF( "fz_new_stext_device" )

//Seite durch's device laufen lassen
    fz_try( ctx ) fz_run_page( ctx, document_page->page, s_t_device, fz_identity, NULL );
    fz_always( ctx )
    {
        fz_close_device( ctx, s_t_device );
        fz_drop_device( ctx, s_t_device );
    }
    fz_catch( ctx ) ERROR_MUPDF( "fz_run_page" )

    return 0;
}


gchar*
pdf_get_text_from_stext_page( fz_context* ctx, fz_stext_page* stext_page,
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
