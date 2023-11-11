#include "pdf.h"

#include <glib/gstdio.h>

#include "test.h"

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
    pdf_obj* obj_val_resolved = NULL;
    pdf_obj* obj_page = NULL;
    gint num = 0;
    const gchar* dest_found = NULL;


    fz_try( ctx ) obj_dest_tree = pdf_load_name_tree( ctx, doc, PDF_NAME(Dests) );
    fz_catch( ctx ) ERROR_MUPDF( "pdf_load_name_tree" )

    for ( gint i = 0; i < pdf_dict_len( ctx, obj_dest_tree ); i++ )
    {
        fz_try( ctx ) obj_key = pdf_dict_get_key( ctx, obj_dest_tree, i );
        fz_catch( ctx ) ERROR_MUPDF( "pdf_dict_get_key" )

        fz_try( ctx ) obj_val = pdf_dict_get_val( ctx, obj_dest_tree, i );
        fz_catch( ctx ) ERROR_MUPDF( "pdf_dict_get_val" )

        fz_try( ctx ) obj_val_resolved = pdf_resolve_indirect( ctx, obj_val );
        fz_catch( ctx )
        {
            pdf_drop_obj( ctx, obj_dest_tree );

            ERROR_MUPDF( "pdf_resolve_indirect" )
        }

        //Altmodische PDF verweisen im NameTree auf ein Dict mit dem Schlüssel /D
        if ( pdf_is_array( ctx, obj_val_resolved ) ) obj_page =
                pdf_array_get( ctx, obj_val_resolved, 0 );
        else if ( pdf_is_dict( ctx, obj_val_resolved ) )
        {
            pdf_obj* obj_array = NULL;

            obj_array = pdf_dict_get( ctx, obj_val_resolved, PDF_NAME(D) );
            obj_page = pdf_array_get( ctx, obj_array, 0 );
        }
        else //Name-Tree-Val ist weder array noch dict - es widerspricht der obersten Direktive
        {
            pdf_drop_obj( ctx, obj_dest_tree );
            ERROR_S_MESSAGE( "NamedTree für NamedDests irregulär" )
        }

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
            if (obj != NULL)
            {
                fz_try( ctx )
                {
                    pdf_obj* grafted_obj = NULL;

                    grafted_obj = pdf_graft_mapped_object( ctx, graft_map, obj );
                    pdf_dict_put_drop( ctx, page_dict, copy_list[i], grafted_obj );
                }
                fz_catch( ctx )
                {
                    pdf_drop_graft_map( ctx, graft_map );
                    pdf_drop_obj( ctx, page_dict );
                    ERROR_MUPDF( "pdf_dict_put_drop" )
                }
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
pdf_open_and_authen_document( fz_context* ctx, gboolean prompt,
        const gchar* path, gchar** password, pdf_document** doc, gint* auth,
        gchar** errmsg )
{
    gchar* password_try = NULL;

    fz_try( ctx ) *doc = pdf_open_document( ctx, path );
    fz_catch( ctx ) ERROR_MUPDF( "pdf_open_document" )

    if ( password ) password_try = *password;

    do
    {
        gint res_auth = 0;
        gint res_dialog = 0;

        res_auth = pdf_authenticate_password( ctx, *doc, password_try );
        if ( res_auth ) //erfolgreich!
        {
            if ( auth ) *auth = res_auth;
            if ( password ) *password = password_try;
            break;
        }
        else if ( !prompt ) return 1;

        res_dialog = dialog_with_buttons( NULL, path, "Passwort eingeben:",
                &password_try, "Ok", GTK_RESPONSE_OK, "Abbrechen",
                GTK_RESPONSE_CANCEL, NULL );
        if ( res_dialog != GTK_RESPONSE_OK ) return 1; //Abbruch
    } while ( 1 );

    return 0;
}


gint
pdf_save( fz_context* ctx, pdf_document* pdf_doc, const gchar* path,
        void (*drop_func) (gpointer data1, gpointer data2), gpointer data1,
        gpointer data2, gchar** errmsg )
{
    gchar* path_tmp = NULL;
    pdf_write_options opts =
#ifdef __WIN32
            { 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, ~0, "", "", 0 };
#elif defined(__linux__)
            { 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 1, 0, 0, ~0, "", "" };
#endif // __win32
    if ( pdf_count_pages( ctx, pdf_doc ) < BIG_PDF && !pdf_doc->crypt ) opts.do_garbage = 4;

    path_tmp = g_strconcat( path, ".tmp_clean", NULL );

    fz_try( ctx ) pdf_save_document( ctx, pdf_doc, path_tmp, &opts );
    fz_always( ctx ) drop_func( data1, data2 );
    fz_catch( ctx )
    {
        g_free( path_tmp );
        ERROR_MUPDF( "pdf_write_document" )
    }

    if ( g_remove( path ) )
    {
        g_free( path_tmp );
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_remove:\n", strerror( errno ), NULL );
        return -1;
    }
    if ( g_rename( path_tmp, path ) )
    {
        g_free( path_tmp );
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_rename:\n", strerror( errno ), NULL );
        return -1;
    }

    g_free( path_tmp );

    return 0;
}


gint
pdf_clean( fz_context* ctx, const gchar* rel_path, gchar** errmsg )
{
    pdf_document* doc = NULL;
    gint rc = 0;

    //prüfen, ob in Viewer geöffnet
    if ( zond_pdf_document_is_open( rel_path ) ) ERROR_S_MESSAGE( "Dokument ist geöffnet" )

    rc = pdf_open_and_authen_document( ctx, TRUE, rel_path, NULL, &doc, NULL, errmsg );
    if ( rc == -1 ) ERROR_S
    else if ( rc == 1 ) return 1;

    fz_try( ctx ) retainpages( ctx, doc );
    fz_catch( ctx ) ERROR_MUPDF( "retainpages" )

    rc = pdf_save( ctx, doc, rel_path, (void (*) (gpointer, gpointer)) pdf_drop_document, ctx, doc, errmsg );
    if ( rc ) ERROR_S

    return 0;
}


gchar*
pdf_get_string_from_line( fz_context* ctx, fz_stext_line* line, gchar** errmsg )
{
    gchar* line_string = NULL;
    fz_buffer* buf = NULL;

    fz_try( ctx ) buf = fz_new_buffer( ctx, 128 );
    fz_catch( ctx ) ERROR_MUPDF_R( "fz_new_buffer", NULL )

    //string aus Zeile bilden
    for ( fz_stext_char* stext_char = line->first_char; stext_char; stext_char = stext_char->next )
    {
        fz_try( ctx ) fz_append_rune( ctx, buf, stext_char->c );
        fz_catch( ctx ) ERROR_MUPDF_R( "fz_append_rune", NULL )
    }

    line_string = g_strdup( fz_string_from_buffer( ctx, buf ) );

    fz_drop_buffer( ctx, buf );

    return line_string;
}


/* PDF-Text-Filter */
typedef struct resources_stack
{
	struct resources_stack *next;
	pdf_obj *res;
} resources_stack;

typedef struct
{
	pdf_processor super;
	fz_output *out;
	int ahxencode;
	int extgstate;
	pdf_obj *res;
	pdf_obj *last_res;
	resources_stack *rstack;
} pdf_output_processor;

typedef struct
{
	pdf_output_processor super;
	void (* pdf_drop_buffer_processor) (fz_context* ctx, pdf_processor* proc );

	void (* pdf_buffer_processor_op_q) (fz_context* ctx, pdf_processor* proc );
	void (* pdf_buffer_processor_op_Q) (fz_context* ctx, pdf_processor* proc );

	void (* pdf_buffer_processor_op_Tr) (fz_context* ctx, pdf_processor* proc, gint );

	void (* pdf_buffer_processor_op_TJ) (fz_context*, pdf_processor*, pdf_obj* );
	void (* pdf_buffer_processor_op_Tj) (fz_context*, pdf_processor*, gchar*, size_t );
	void (* pdf_buffer_processor_op_squote) (fz_context*, pdf_processor*,
            gchar*, size_t );
	void (* pdf_buffer_processor_op_dquote) (fz_context*, pdf_processor*,
            float, float, gchar*, size_t );

	GArray* arr_Tr;
	gint flags;
} pdf_text_filter_processor;


static void
pdf_drop_text_filter_processor(fz_context *ctx, pdf_processor *proc)
{
	pdf_text_filter_processor* p = (pdf_text_filter_processor*) proc;

	g_array_unref( p->arr_Tr );

	p->pdf_drop_buffer_processor( ctx, proc );

//	fz_free( ctx, proc );

    return;
}


static void
pdf_text_filter_op_q(fz_context *ctx, pdf_processor *proc)
{
    gint Tr = 0;

	pdf_text_filter_processor* p = (pdf_text_filter_processor*) proc;

    Tr = g_array_index( p->arr_Tr, gint, p->arr_Tr->len - 1 );

    g_array_append_val( p->arr_Tr, Tr );

    //chain-up
	p->pdf_buffer_processor_op_q( ctx, proc );

    return;
}


static void
pdf_text_filter_op_Q( fz_context* ctx, pdf_processor* proc )
{
    pdf_text_filter_processor* p = (pdf_text_filter_processor*) proc;

    if ( p->arr_Tr->len ) //wenn mehr Q als q, dann braucht man auch nicht weiterleiten...
    {
        g_array_remove_index( p->arr_Tr, p->arr_Tr->len - 1 );

        p->pdf_buffer_processor_op_Q( ctx, proc );
    }

    return;
}


static void
pdf_text_filter_op_Tr( fz_context* ctx, pdf_processor* proc, gint render )
{
    pdf_text_filter_processor* p = (pdf_text_filter_processor*) proc;

    (((gint*) (void *) (p->arr_Tr)->data) [(p->arr_Tr->len - 1)]) = render;

    p->pdf_buffer_processor_op_Tr( ctx, proc, render );

    return;
}


static void
pdf_text_filter_op_TJ( fz_context* ctx, pdf_processor* proc, pdf_obj* array )
{
    gint Tr = 0;

    pdf_text_filter_processor* p = (pdf_text_filter_processor*) proc;

    Tr = g_array_index( p->arr_Tr, gint, p->arr_Tr->len - 1 );

    if ( p->flags & 1 && Tr != 3 ) return;
    if ( p->flags & 2 && Tr == 3 ) return;

    p->pdf_buffer_processor_op_TJ( ctx, proc, array );

    return;
}


static void
pdf_text_filter_op_Tj( fz_context* ctx, pdf_processor* proc, gchar* str, size_t len )
{
    gint Tr = 0;

    pdf_text_filter_processor* p = (pdf_text_filter_processor*) proc;

    Tr = g_array_index( p->arr_Tr, gint, p->arr_Tr->len - 1 );

    if ( p->flags & 1 && Tr != 3 ) return;
    if ( p->flags & 2 && Tr == 3 ) return;

    p->pdf_buffer_processor_op_Tj( ctx, proc, str, len );

    return;
}


static void
pdf_text_filter_op_squote( fz_context* ctx, pdf_processor* proc, gchar* str, size_t len )
{
    gint Tr = 0;

    pdf_text_filter_processor* p = (pdf_text_filter_processor*) proc;

    Tr = g_array_index( p->arr_Tr, gint, p->arr_Tr->len - 1 );

    if ( p->flags & 1 && Tr != 3 ) return;
    if ( p->flags & 2 && Tr == 3 ) return;

    p->pdf_buffer_processor_op_squote( ctx, proc, str, len );

    return;
}


static void
pdf_text_filter_op_dquote( fz_context* ctx, pdf_processor* proc, float aw, float ac,
        gchar* str, size_t len )
{
    gint Tr = 0;

    pdf_text_filter_processor* p = (pdf_text_filter_processor*) proc;

    Tr = g_array_index( p->arr_Tr, gint, p->arr_Tr->len - 1 );

    if ( p->flags & 1 && Tr != 3 ) return;
    if ( p->flags & 2 && Tr == 3 ) return;

    p->pdf_buffer_processor_op_dquote( ctx, proc, aw, ac, str, len );

    return;
}

pdf_processor *
pdf_new_text_filter_processor( fz_context *ctx, fz_buffer** buf, gint flags,
        gchar** errmsg )
{
    *buf = NULL;
    gint zero = 0; //wg g_array_append_val
    pdf_text_filter_processor* proc = NULL;
    pdf_output_processor* proc_output = NULL;

	fz_try( ctx ) *buf = fz_new_buffer( ctx, 1024 );
	fz_catch( ctx ) ERROR_MUPDF_R( "fz_new_buffer", NULL )

	fz_try( ctx ) proc_output = (pdf_output_processor*) pdf_new_buffer_processor( ctx, *buf, 0 );
	fz_catch( ctx )
	{
	    fz_drop_buffer( ctx, *buf );
	    ERROR_MUPDF_R( "pdf_new_output_processor", NULL )
	}

	proc = 	Memento_label(fz_calloc(ctx, 1, sizeof( pdf_text_filter_processor ) ), "pdf_processor");

	//output-processor in super-Struktur kopieren
	proc->super = *proc_output;

	//Hülle kann freigegeben werden
	fz_free( ctx, proc_output );

	proc->arr_Tr = g_array_new( FALSE, FALSE, sizeof( gint ) );
	g_array_append_val( proc->arr_Tr, zero );

	proc->flags = flags;

	//Funktionen "umleiten"
	proc->pdf_drop_buffer_processor = proc->super.super.drop_processor;
	proc->super.super.drop_processor = pdf_drop_text_filter_processor;

	/* special graphics state */
	proc->pdf_buffer_processor_op_q = proc->super.super.op_q;
	proc->super.super.op_q = pdf_text_filter_op_q;

	proc->pdf_buffer_processor_op_Q = proc->super.super.op_Q;
	proc->super.super.op_Q = pdf_text_filter_op_Q;

	proc->pdf_buffer_processor_op_Tr = proc->super.super.op_Tr;
	proc->super.super.op_Tr = pdf_text_filter_op_Tr;

	proc->pdf_buffer_processor_op_TJ = proc->super.super.op_TJ;
	proc->super.super.op_TJ = pdf_text_filter_op_TJ;
	proc->pdf_buffer_processor_op_Tj = proc->super.super.op_Tj;
	proc->super.super.op_Tj = pdf_text_filter_op_Tj;
	proc->pdf_buffer_processor_op_squote = proc->super.super.op_squote;
	proc->super.super.op_squote = pdf_text_filter_op_squote;
	proc->pdf_buffer_processor_op_dquote = proc->super.super.op_dquote;
	proc->super.super.op_dquote = pdf_text_filter_op_dquote;

	return (pdf_processor*) proc;
}


fz_buffer*
pdf_text_filter_page( fz_context* ctx, pdf_obj* obj, gint flags, gchar** errmsg )
{
    pdf_obj* contents = NULL;
    pdf_obj* res = NULL;
    pdf_processor* proc = NULL;
    fz_buffer* buf = NULL;
    pdf_document* doc = NULL;

	fz_try( ctx ) contents = pdf_dict_get( ctx, obj, PDF_NAME(Contents) );
	fz_catch( ctx ) ERROR_MUPDF_R( "pdf_dict_get (Contents)", NULL )
	if ( !contents ) ERROR_S_MESSAGE_VAL( "Kein Contents-Dict", NULL )

	fz_try( ctx ) res = pdf_dict_get_inheritable( ctx, obj, PDF_NAME(Resources) );
    fz_catch( ctx ) ERROR_MUPDF_R( "pdf_dict_get_inheritable (Ressources)", NULL )
    if ( !res ) ERROR_S_MESSAGE_VAL( "Kein Ressources-Dict", NULL )

	proc = pdf_new_text_filter_processor( ctx, &buf, flags, errmsg );
	if ( !proc ) ERROR_S_VAL( NULL )

	doc = pdf_pin_document( ctx, obj );
    fz_try( ctx ) pdf_process_contents( ctx, proc, doc, res, contents, NULL, NULL );
    fz_always( ctx )
    {
        pdf_drop_document( ctx, doc );
        pdf_close_processor( ctx, proc );
        pdf_drop_processor( ctx, proc );
    }
    fz_catch( ctx )
    {
        fz_drop_buffer( ctx, buf );
        ERROR_S_VAL( NULL )
    }

    return buf;
}


