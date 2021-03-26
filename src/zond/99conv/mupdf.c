#include "../error.h"

#include "../99conv/general.h"

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>


static void
mupdf_unlock( void* user, gint lock )
{
    GMutex* mutex = (GMutex*) user;

    g_mutex_unlock( &mutex[lock] );

    return;
}

static void
mupdf_lock( void* user, gint lock )
{
    GMutex* mutex = (GMutex*) user;

    g_mutex_lock( &mutex[lock] );

    return;
}


/** Wenn NULL, dann Fehler und *errmsg gesetzt **/
fz_context*
mupdf_init( gchar** errmsg )
{
    fz_context* ctx = NULL;
    fz_locks_context locks_context;

    GMutex* mutex = g_malloc0( sizeof( GMutex ) * FZ_LOCK_MAX );

    for ( gint i = 0; i < FZ_LOCK_MAX; i++ ) g_mutex_init( &mutex[i] );

    locks_context.user = mutex;
    locks_context.lock = mupdf_lock;
    locks_context.unlock = mupdf_unlock;

	/* Create a context to hold the exception stack and various caches. */
	ctx = fz_new_context( NULL, &locks_context, FZ_STORE_UNLIMITED );
    if ( !ctx )
    {
        g_mutex_clear( mutex );
        g_free( mutex );
        ERROR_MUPDF_R( "fz_new_context", NULL )
    }

	/* Register the default file types to handle. */
	fz_try(ctx)  fz_register_document_handlers(ctx);
	fz_catch( ctx ) //ERROR_MUPDF( "fz_register_document_handlers" )
    {
        fz_drop_context( ctx );
        g_mutex_clear( mutex );
        g_free( mutex );
        ERROR_MUPDF_R( "fz_register_document_handlers", NULL )
    }

    return ctx;
}


void
mupdf_close_context( fz_context* ctx )
{
    GMutex* mutex = (GMutex*) ctx->locks.user;

    fz_drop_context( ctx );

//    for ( gint i = 0; i < FZ_LOCK_MAX; i++ )
  //          g_mutex_clear( &mutex[i] );

//    g_free( mutex );

    return;
}


/** Wenn NULL, dann Fehler und *errmsg gesetzt **/
fz_document*
mupdf_dokument_oeffnen( fz_context* ctx, const gchar* path, gchar** errmsg )
{
    fz_document* doc = NULL;

	fz_try( ctx ) doc = fz_open_document( ctx, path );
	fz_catch( ctx ) ERROR_MUPDF_R( "fz_open_document", NULL )

	return doc;
}


gint
mupdf_save_doc( fz_context* ctx, fz_document* doc, const gchar* path,
        gchar** errmsg )
{
    pdf_document* pdf_doc = pdf_specifics( ctx, doc );

    pdf_write_options opts = {
            0, // do_incremental
            1, // do_pretty
            0, // do_ascii
            0, // do_compress
            1, // do_compress_images
            1, // do_compress_fonts
            1, // do_decompress
            1, // do_garbage
            0, // do_linear
            1, // do_clean
            1, // do_sanitize
            0, // do_appearance
            0, // do_encrypt
            ~0, // permissions
            "", // opwd_utf8[128]
            "", // upwd_utf8[128]
            };

    fz_try( ctx ) pdf_save_document( ctx, pdf_doc, path, &opts );
    fz_catch( ctx ) ERROR_MUPDF( "pdf_save_document" )

    return 0;
}

