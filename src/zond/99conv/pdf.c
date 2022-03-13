#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <glib/gstdio.h>

#include "pdf.h"
#include "../zond_pdf_document.h"

#include "../../misc.h"



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
            ERROR_MUPDF( "pdf_lookup- and flatten_inheritable_page" )
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
            ERROR_MUPDF( "pdf_new_dict/_put" )
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
                ERROR_MUPDF( "pdf_dict_put_drop" )
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

            ERROR_MUPDF( "pdf_add_object/_insert_page" )
        }
    }

    pdf_drop_graft_map( ctx, graft_map );

    return 0;
}


gint
pdf_clean( fz_context* ctx, const gchar* rel_path, gchar** errmsg )
{
    gchar* path_tmp = NULL;
    pdf_document* doc = NULL;

    pdf_write_options opts = {
            0, // do_incremental
            1, // do_pretty
            1, // do_ascii
            0, // do_compress
            1, // do_compress_images
            1, // do_compress_fonts
            0, // do_decompress
            4, // do_garbage
            0, // do_linear
            1, // do_clean
            0, // do_sanitize
            0, // do_appearance
            0, // do_encrypt
            0, // dont_regenerate_id  Don't regenerate ID if set (used for clean)
            ~0, // permissions
            "", // opwd_utf8[128]
            "", // upwd_utf8[128]
            0 //do snapshot
            };

    //prüfen, ob in Viewer geöffnet
    if ( zond_pdf_document_is_open( rel_path ) ) ERROR_S_MESSAGE( "Dokument ist geöffnet" )

    fz_try( ctx ) doc = pdf_open_document( ctx, rel_path );
    fz_catch( ctx ) ERROR_MUPDF( "pdf_document_open" )

    fz_try( ctx ) pdf_clean_document( ctx, doc );
    fz_catch( ctx )
    {
        pdf_drop_document( ctx, doc );
        ERROR_MUPDF( "pdf_clean_document" )
    }

    path_tmp = g_strconcat( rel_path, ".tmp_clean", NULL );

    fz_try( ctx ) pdf_save_document( ctx, doc, path_tmp, &opts );
    fz_always( ctx ) pdf_drop_document( ctx, doc );
    fz_catch( ctx )
    {
        g_free( path_tmp );
        ERROR_MUPDF( "pdf_save_document" )
    }

    if ( g_remove( rel_path ) )
    {
        g_free( path_tmp );
        ERROR_S_MESSAGE( "g_remove" )
    }
    if ( g_rename( path_tmp, rel_path ) )
    {
        g_free( path_tmp );
        ERROR_S_MESSAGE( "g_rename" )
    }

    g_free( path_tmp );

    return 0;
}
