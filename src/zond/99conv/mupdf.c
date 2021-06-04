#include "../error.h"

#include "../99conv/general.h"

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <gio/gio.h>
#include <glib/gstdio.h>


static void
mupdf_unlock( void* user, gint lock )
{
    GMutex* mutex = (GMutex*) user;

    g_mutex_unlock( &(mutex[lock]) );

    return;
}

static void
mupdf_lock( void* user, gint lock )
{
    GMutex* mutex = (GMutex*) user;

    g_mutex_lock( &(mutex[lock]) );

    return;
}


/** Wenn NULL, dann Fehler und *errmsg gesetzt **/
fz_context*
mupdf_init( gchar** errmsg )
{
    fz_context* ctx = NULL;
    fz_locks_context locks_context;

    GMutex* mutex = g_malloc0( sizeof( GMutex ) * FZ_LOCK_MAX );

//    static GMutex mutex[FZ_LOCK_MAX];
    for ( gint i = 0; i < FZ_LOCK_MAX; i++ ) g_mutex_init( &(mutex[i]) );

    locks_context.user = mutex;
    locks_context.lock = mupdf_lock;
    locks_context.unlock = mupdf_unlock;

	/* Create a context to hold the exception stack and various caches. */
	ctx = fz_new_context( NULL, &locks_context, FZ_STORE_UNLIMITED );
    if ( !ctx )
    {
        for ( gint i = 0; i < FZ_LOCK_MAX; i++ ) g_mutex_clear( &mutex[i] );
        g_free( mutex );
        ERROR_MUPDF_R( "fz_new_context", NULL )
    }
/*
	// Register the default file types to handle.
	fz_try(ctx)  fz_register_document_handlers(ctx);
	fz_catch( ctx ) //ERROR_MUPDF( "fz_register_document_handlers" )
    {
        fz_drop_context( ctx );
        for ( gint i = 0; i < FZ_LOCK_MAX; i++ ) g_mutex_clear( &mutex[i] );
        ERROR_MUPDF_R( "fz_register_document_handlers", NULL )
    }
*/
    return ctx;
}


void
mupdf_close_context( fz_context* ctx )
{
    GMutex* mutex = (GMutex*) ctx->locks.user;

    fz_drop_context( ctx );

    for ( gint i = 0; i < FZ_LOCK_MAX; i++ ) g_mutex_clear( &mutex[i] );

//    g_free( mutex );

    return;
}

