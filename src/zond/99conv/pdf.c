#include "../error.h"

#include "../zond_pdf_document.h"

#include "../99conv/general.h"
#include "pdf_ocr.h"

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

