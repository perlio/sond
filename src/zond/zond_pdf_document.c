/*
zond (zond_pdf_document.c) - Akten, Beweisstücke, Unterlagen
Copyright (C) 2021  pelo america

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

#include "zond_pdf_document.h"

#include <glib/gstdio.h>
#include "99conv/pdf.h"

#include "../misc.h"


typedef enum
{
    PROP_FILE_PART = 1,
    PROP_PASSWORD,
    N_PROPERTIES
} ZondPdfDocumentProperty;

typedef struct
{
    GMutex mutex_doc;
    fz_context* ctx;
    pdf_document* doc;
    gchar* password;
    gint auth;
    gchar* file_part;
    gboolean read_only;
    gchar* working_copy;
    GPtrArray* pages; //array von PdfDocumentPage*
    GArray* arr_journal;
} ZondPdfDocumentPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(ZondPdfDocument, zond_pdf_document, G_TYPE_OBJECT)


static void
zond_pdf_document_set_property (GObject      *object,
                          guint         property_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    ZondPdfDocument* self = ZOND_PDF_DOCUMENT(object);
    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( self );

    switch ((ZondPdfDocumentProperty) property_id)
    {
    case PROP_FILE_PART:
      priv->file_part = g_strdup( g_value_get_string(value) );
      break;

    case PROP_PASSWORD:
      priv->password = g_strdup( g_value_get_string(value) );
      break;

    default:
      /* We don't have any other property... */
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}


static void
zond_pdf_document_get_property (GObject    *object,
                          guint       property_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    ZondPdfDocument* self = ZOND_PDF_DOCUMENT(object);
    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( self );

    switch ((ZondPdfDocumentProperty) property_id)
    {
        case PROP_FILE_PART:
                g_value_set_string( value, priv->file_part );
                break;

        case PROP_PASSWORD:
                g_value_set_string( value, priv->password );
                break;

        default:
                /* We don't have any other property... */
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
                break;
    }
}


static void
zond_pdf_document_close_context( fz_context* ctx )
{
    GMutex* mutex = (GMutex*) ctx->locks.user;

    fz_drop_context( ctx );

    for ( gint i = 0; i < FZ_LOCK_MAX; i++ ) g_mutex_clear( &mutex[i] );

    g_free( mutex );

    return;
}


static void
zond_pdf_document_finalize( GObject* self )
{
    gchar* path = NULL;

    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( ZOND_PDF_DOCUMENT(self) );

    g_ptr_array_unref( priv->pages );

    if ( !priv->read_only ) path = g_strdup( fz_stream_filename( priv->ctx, priv->doc->file ) );
    pdf_drop_document( priv->ctx, priv->doc );

    if ( !priv->read_only )
    {
        gint ret = 0;

        ret = remove( path );
        g_free( path );
        if ( ret )
        {
            gchar* error_text = NULL;

            error_text = g_strdup_printf( "remove('%s'): %s", priv->file_part, strerror( errno ) );
            display_error( NULL, "Arbeitskopie konnte nicht gelöscht werden\n\n", error_text );
            g_free( error_text );
        }
    }
    zond_pdf_document_close_context( priv->ctx ); //drop_context reicht nicht aus!

    g_array_unref( priv->arr_journal );

    g_free( priv->file_part );
    g_free( priv->password );
    g_mutex_clear( &priv->mutex_doc );

    ZondPdfDocumentClass* klass = ZOND_PDF_DOCUMENT_GET_CLASS(self);

    g_ptr_array_remove_fast( klass->arr_pdf_documents, self );

    G_OBJECT_CLASS(zond_pdf_document_parent_class)->finalize( self );

    return;
}


static void
zond_pdf_document_page_free( PdfDocumentPage* pdf_document_page )
{
    if ( !pdf_document_page ) return;

    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( pdf_document_page->document );

    fz_drop_page( priv->ctx, &(pdf_document_page->page->super) );
    fz_drop_stext_page( priv->ctx, pdf_document_page->stext_page );
    fz_drop_display_list( priv->ctx, pdf_document_page->display_list );
    if ( pdf_document_page->arr_annots ) g_ptr_array_unref( pdf_document_page->arr_annots );

    g_free( pdf_document_page );

    return;
}


static void
zond_pdf_document_page_annot_free( gpointer data )
{
    PdfDocumentPageAnnot* pdf_document_page_annot = (PdfDocumentPageAnnot*) data;

    //Text-Markup-annots
    if ( pdf_document_page_annot->type == PDF_ANNOT_HIGHLIGHT ||
            pdf_document_page_annot->type == PDF_ANNOT_UNDERLINE ||
            pdf_document_page_annot->type == PDF_ANNOT_STRIKE_OUT ||
            pdf_document_page_annot->type == PDF_ANNOT_SQUIGGLY )
            g_array_unref( pdf_document_page_annot->annot_text_markup.arr_quads );

    g_free( pdf_document_page_annot );

    return;
}


static void
zond_pdf_document_page_annot_load( PdfDocumentPage* pdf_document_page,
        pdf_annot* annot )
{
    PdfDocumentPageAnnot* pdf_document_page_annot = NULL;

    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( pdf_document_page->document );

    pdf_document_page_annot = g_malloc0( sizeof( PdfDocumentPageAnnot ) );

    pdf_document_page_annot->annot = annot;
    pdf_document_page_annot->pdf_document_page = pdf_document_page;

    fz_try( priv->ctx )
    {
        pdf_document_page_annot->type = pdf_annot_type( priv->ctx, annot );
        pdf_document_page_annot->flags = pdf_annot_flags( priv->ctx, annot );
        pdf_document_page_annot->content = pdf_annot_contents( priv->ctx, annot );
    }
    fz_catch( priv->ctx )
    {
        g_free( pdf_document_page_annot );
        fz_warn( priv->ctx, "Warnung: Funktion pdf_annot_quad_point_count gab "
                "Fehler zurück: %s", fz_caught_message( priv->ctx ) );
        return;
    }

    //Text-Markup-annots
    if ( pdf_document_page_annot->type == PDF_ANNOT_HIGHLIGHT ||
            pdf_document_page_annot->type == PDF_ANNOT_UNDERLINE ||
            pdf_document_page_annot->type == PDF_ANNOT_STRIKE_OUT ||
            pdf_document_page_annot->type == PDF_ANNOT_SQUIGGLY )
    {
        fz_try( priv->ctx ) pdf_document_page_annot->annot_text_markup.n_quad =
                pdf_annot_quad_point_count( priv->ctx, annot );
        fz_catch( priv->ctx )
        {
            g_free( pdf_document_page_annot );
            fz_warn( priv->ctx, "Warnung: Funktion pdf_annot_quad_point_count gab "
                    "Fehler zurück: %s", fz_caught_message( priv->ctx ) );
            return;
        }

        pdf_document_page_annot->annot_text_markup.arr_quads =
                g_array_new( FALSE, FALSE, sizeof( fz_quad ) );

        for ( gint i = 0; i < pdf_document_page_annot->annot_text_markup.n_quad; i++ )
        {
            fz_quad quad = pdf_annot_quad_point( priv->ctx, annot, i );
            g_array_append_val( pdf_document_page_annot->annot_text_markup.arr_quads, quad );
        }

    }
    else if ( pdf_document_page_annot->type == PDF_ANNOT_TEXT )
    {
        pdf_document_page_annot->annot_text.rect = pdf_bound_annot( priv->ctx, annot );
        pdf_document_page_annot->annot_text.open = pdf_annot_is_open( priv->ctx, annot );
        pdf_document_page_annot->annot_text.name = pdf_annot_icon_name( priv->ctx, annot );
    }

    g_ptr_array_add( pdf_document_page->arr_annots, pdf_document_page_annot );

    return;
}


void
zond_pdf_document_page_load_annots( PdfDocumentPage* pdf_document_page )
{
    pdf_annot* annot = NULL;

    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( pdf_document_page->document );

    annot = pdf_first_annot( priv->ctx, pdf_document_page->page );

    if ( !annot ) return;

    do zond_pdf_document_page_annot_load( pdf_document_page, annot );
    while ( (annot = pdf_next_annot( priv->ctx, annot )) );

    return;
}


gint
zond_pdf_document_load_page( PdfDocumentPage* pdf_document_page, gchar** errmsg )
{
    fz_context* ctx = NULL;

    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( pdf_document_page->document );

    ctx = priv->ctx;

    fz_try( ctx ) pdf_document_page->page = pdf_load_page( ctx, priv->doc, pdf_document_page->page_doc );
    fz_catch( ctx ) ERROR_MUPDF( "pdf_load_page" );

    pdf_document_page->arr_annots = g_ptr_array_new_with_free_func( zond_pdf_document_page_annot_free );

    zond_pdf_document_page_load_annots( pdf_document_page );

    return 0;
}


static gint
zond_pdf_document_init_page( ZondPdfDocument* self,
        PdfDocumentPage* pdf_document_page, gint index, gchar** errmsg )
{
    fz_context* ctx = NULL;
    pdf_obj* rotate_obj = NULL;
    fz_rect mediabox = { 0 };
    fz_matrix page_ctm = { 0 };

    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( ZOND_PDF_DOCUMENT(self) );

    pdf_document_page->document = self; //keine ref!

    ctx = priv->ctx;

    fz_try( ctx )
    {
        pdf_document_page->page_doc = index;
        pdf_document_page->obj = pdf_lookup_page_obj( ctx, priv->doc, index );
        pdf_page_obj_transform( ctx, pdf_document_page->obj, &mediabox, &page_ctm);
        pdf_document_page->rect = fz_transform_rect(mediabox, page_ctm);

        rotate_obj = pdf_dict_get( ctx, pdf_document_page->obj, PDF_NAME(Rotate) );
        if ( rotate_obj ) pdf_document_page->rotate = pdf_to_int( ctx, rotate_obj );
        //else: 0
    }
    fz_catch( ctx ) ERROR_MUPDF( "pdf_lookup_page_obj etc." );

    return 0;
}


static gint
zond_pdf_document_init_pages( ZondPdfDocument* self, gint von, gint bis, gchar** errmsg )
{
    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( self );

    if ( bis == -1 ) bis = priv->pages->len - 1;

    if ( von < 0 || von > bis || bis > priv->pages->len - 1 )
    {
        if ( errmsg ) *errmsg = g_strdup( "Seitengrenzen nicht eingehalten" );
        return -1;
    }

    for ( gint i = von; i <= bis; i++ )
    {
        gint rc = 0;
        PdfDocumentPage* pdf_document_page = NULL;

        //wenn schon initialisiert -> weiter
        if ( g_ptr_array_index( priv->pages, i ) ) continue;

        pdf_document_page = g_malloc0( sizeof( PdfDocumentPage ) );
        ((priv->pages)->pdata)[i] = pdf_document_page;

        rc = zond_pdf_document_init_page( self, pdf_document_page, i, errmsg );
        if ( rc )
        {
            g_free( pdf_document_page );
            ((priv->pages)->pdata)[i] = NULL;
            ERROR_S
        }
    }

    return 0;
}


static void
zond_pdf_document_class_init( ZondPdfDocumentClass* klass )
{
    GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

    klass->arr_pdf_documents = g_ptr_array_new( );

    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->finalize = zond_pdf_document_finalize;

    object_class->set_property = zond_pdf_document_set_property;
    object_class->get_property = zond_pdf_document_get_property;

    obj_properties[PROP_FILE_PART] =
            g_param_spec_string( "file-part",
                                 "gchar*",
                                 "Pfad zur Datei.",
                                 NULL,
                                  G_PARAM_CONSTRUCT | G_PARAM_READWRITE);

    obj_properties[PROP_PASSWORD] =
            g_param_spec_string( "password",
                                 "gchar*",
                                 "Passwort.",
                                 NULL,
                                  G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE );

    g_object_class_install_properties(object_class,
                                      N_PROPERTIES,
                                      obj_properties);

    return;
}


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


static void*
mupdf_malloc( void* user, size_t size )
{
    return g_malloc( size );
}


static void*
mupdf_realloc( void* user, void* old, size_t size )
{
    return g_realloc( old, size );
}


static void
mupdf_free( void* user, void* ptr )
{
    g_free( ptr );

    return;
}


/** Wenn NULL, dann Fehler und *errmsg gesetzt **/
static fz_context*
zond_pdf_document_init_context( void )
{
    GMutex* mutex = NULL;
    fz_context* ctx = NULL;
    fz_locks_context locks_context = { 0, };
    fz_alloc_context alloc_context = { 0, };

    //mutex für document
    mutex = g_malloc0( sizeof( GMutex ) * FZ_LOCK_MAX );
    for ( gint i = 0; i < FZ_LOCK_MAX; i++ ) g_mutex_init( &(mutex[i]) );

    locks_context.user = mutex;
    locks_context.lock = mupdf_lock;
    locks_context.unlock = mupdf_unlock;

    alloc_context.user = NULL;
    alloc_context.malloc = mupdf_malloc;
    alloc_context.realloc = mupdf_realloc;
    alloc_context.free = mupdf_free;

	/* Create a context to hold the exception stack and various caches. */
	ctx = fz_new_context( &alloc_context, &locks_context, FZ_STORE_UNLIMITED );
    if ( !ctx )
    {
        for ( gint i = 0; i < FZ_LOCK_MAX; i++ ) g_mutex_clear( &mutex[i] );
        g_free( mutex );
    }

    return ctx;
}


static void
zond_pdf_document_init( ZondPdfDocument* self )
{
    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( self );

    g_mutex_init( &priv->mutex_doc );

    priv->pages = g_ptr_array_new_with_free_func( (GDestroyNotify)
            zond_pdf_document_page_free );
    priv->arr_journal = g_array_new( FALSE, FALSE, sizeof( JournalEntry ) );
    g_array_set_clear_func( priv->arr_journal, (GDestroyNotify) free_journal_entry );

    return;
}


const ZondPdfDocument*
zond_pdf_document_is_open( const gchar* file_part )
{
    ZondPdfDocumentClass* klass = g_type_class_peek_static( zond_pdf_document_get_type( ) );

    if ( !klass ) return NULL;

    for ( gint i = 0; i < klass->arr_pdf_documents->len; i++ )
    {
        ZondPdfDocument* zond_pdf_document = g_ptr_array_index( klass->arr_pdf_documents, i );
        ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( zond_pdf_document );

        if ( !g_strcmp0( priv->file_part, file_part ) ) return zond_pdf_document;
    }

    return NULL;
}


ZondPdfDocument*
zond_pdf_document_open( const gchar* file_part, gint von, gint bis, gchar** errmsg )
{
    gint rc = 0;
    ZondPdfDocument* zond_pdf_document = NULL;
    ZondPdfDocumentPrivate* priv = NULL;
    ZondPdfDocumentClass* klass = NULL;
    GError* error = NULL;
    gint number_of_pages = 0;

    zond_pdf_document = (ZondPdfDocument*) zond_pdf_document_is_open( file_part );
    if ( zond_pdf_document )
    {
        gint rc = 0;

        rc = zond_pdf_document_init_pages( zond_pdf_document, von, bis, errmsg );
        if ( rc ) ERROR_S_VAL( NULL )

        return g_object_ref( zond_pdf_document );
    }

    zond_pdf_document = g_object_new( ZOND_TYPE_PDF_DOCUMENT, "file-part", file_part, NULL );

    priv = zond_pdf_document_get_instance_private( zond_pdf_document );

    priv->ctx = zond_pdf_document_init_context( );
    if ( !priv->ctx )
    {
        if ( errmsg ) *errmsg = g_strdup_printf( "%s\nfz_context konnte nicht erzeugt werden", __func__ );
        g_object_unref( zond_pdf_document );

        return NULL;
    }

    rc = pdf_open_and_authen_document( priv->ctx, TRUE, FALSE, priv->file_part,
            &priv->password, &priv->doc, &priv->auth, &error );
    if ( rc )
    {
        if ( rc == -1 )
        {
            if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
            g_error_free( error );
        }
        else if ( errmsg ) *errmsg = g_strdup_printf( "%s\nDokument verschlüsselt", __func__ );

        g_object_unref( zond_pdf_document );

        return NULL;
    }

    number_of_pages = pdf_count_pages( priv->ctx, priv->doc );
    if ( number_of_pages == 0 )
    {
        if ( errmsg ) *errmsg = g_strdup_printf( "%s\nDokument enthält keine Seiten", __func__ );

        return NULL;
    }

    g_ptr_array_set_size( priv->pages, number_of_pages );

    rc = zond_pdf_document_init_pages( zond_pdf_document, von, bis, errmsg );
    if ( rc )
    {
        g_object_unref( zond_pdf_document );
        ERROR_S_VAL( NULL )
    }

    klass = ZOND_PDF_DOCUMENT_GET_CLASS( zond_pdf_document );
    g_ptr_array_add( klass->arr_pdf_documents, zond_pdf_document );

    return zond_pdf_document;
}


void
zond_pdf_document_unload_page( PdfDocumentPage* pdf_document_page )
{
    if ( !pdf_document_page ) return;

    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( pdf_document_page->document );

    if ( pdf_document_page->arr_annots )
    {
        g_ptr_array_unref( pdf_document_page->arr_annots );
        pdf_document_page->arr_annots = NULL;
    }
    fz_drop_page( priv->ctx, &(pdf_document_page->page->super) );
    pdf_document_page->page = NULL;

    pdf_document_page->thread &= 12; //bit 2 ausschalten

    return;
}


gint
zond_pdf_document_save( ZondPdfDocument* self, gchar** errmsg )
{
    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( self );

    gint rc = 0;
    GError* error = NULL;

    if ( priv->read_only )
    {
        if ( errmsg ) *errmsg = g_strdup_printf( "%s\nDokument wurde schreibgeschützt geöffnet", __func__ );

        return -1;
    }

    rc = pdf_save( priv->ctx, priv->doc, priv->file_part, &error );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strdup_printf( "%s\n%s", __func__, error->message );
        g_error_free( error );

        return -1;
    }

    //Wenn erfolgreich gespeichert, Änderungs-Journal löschen
    g_array_remove_range( zond_pdf_document_get_arr_journal( self ),
            0, zond_pdf_document_get_arr_journal( self )->len );

    return 0;
}


void
zond_pdf_document_close( ZondPdfDocument* zond_pdf_document )
{
    g_object_unref( zond_pdf_document );

    return;
}


pdf_document*
zond_pdf_document_get_pdf_doc( ZondPdfDocument* self )
{
    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( self );

    return priv->doc;
}


GPtrArray*
zond_pdf_document_get_arr_pages( ZondPdfDocument* self )
{
    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( self );

    return priv->pages;
}


GArray*
zond_pdf_document_get_arr_journal( ZondPdfDocument* self )
{
    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( self );

    return priv->arr_journal;
}


PdfDocumentPage*
zond_pdf_document_get_pdf_document_page( ZondPdfDocument* self, gint page_doc )
{
    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( self );

    return g_ptr_array_index( priv->pages, page_doc );
}


gint
zond_pdf_document_get_number_of_pages( ZondPdfDocument* self )
{
    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( self );

    return priv->pages->len;
}


fz_context*
zond_pdf_document_get_ctx( ZondPdfDocument* self )
{
    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( self );

    return priv->ctx;
}


const gchar*
zond_pdf_document_get_file_part( ZondPdfDocument* self )
{
    if ( !ZOND_IS_PDF_DOCUMENT(self) ) return NULL;
    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( self );

    return priv->file_part;
}


void
zond_pdf_document_mutex_lock( const ZondPdfDocument* self )
{
    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( (ZondPdfDocument*) self );

    g_mutex_lock( &priv->mutex_doc );

    return;
}


void
zond_pdf_document_mutex_unlock( const ZondPdfDocument* self )
{
    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( (ZondPdfDocument*) self );

    g_mutex_unlock( &priv->mutex_doc );

    return;
}


//wird nur aufgerufen, wenn alle threadpools aus sind!
gint
zond_pdf_document_insert_pages( ZondPdfDocument* zond_pdf_document, gint pos,
        fz_context* ctx, pdf_document* pdf_doc, gchar** errmsg )
{
    gint rc = 0;
    gint count = 0;

    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( zond_pdf_document );

    count = pdf_count_pages( ctx, pdf_doc );
    if ( count == 0 ) return 0;
/*
    //Seiten, die nach hinten verschoben werden, droppen
    for ( gint i = pos; i < priv->pages->len; i++ )
            zond_pdf_document_unload_page( g_ptr_array_index( priv->pages, i ) );
*/
    zond_pdf_document_mutex_lock( zond_pdf_document );
    //einfügen in doc
    rc = pdf_copy_page( ctx, pdf_doc, 0, pdf_count_pages( ctx, pdf_doc ) - 1,
            priv->doc, pos, errmsg );
    zond_pdf_document_mutex_unlock( zond_pdf_document );
    if ( rc ) ERROR_S

    //eingefügte Seiten als pdf_document_page erzeugen und initiieren
    for ( gint i = pos; i < pos + count; i++ )
    {
        gint rc = 0;
        PdfDocumentPage* pdf_document_page = NULL;

        pdf_document_page = g_malloc0( sizeof( PdfDocumentPage ) );

        g_ptr_array_insert( priv->pages, i, pdf_document_page );

        rc = zond_pdf_document_init_page( zond_pdf_document, pdf_document_page, i, errmsg );
        if ( rc == -1 ) ERROR_S
    }

    //verschobene Seiten haben neuen index
    for ( gint i = pos + count; i < priv->pages->len; i++ )
    {
        PdfDocumentPage* pdf_document_page = g_ptr_array_index( priv->pages, i );
/*      vielleicht nicht erforderlich, wenn page_obj bleibt; nur index ändern
        rc = zond_pdf_document_init_page( zond_pdf_document, pdf_document_page, i, errmsg );
        if ( rc == -1 ) ERROR_S
*/
        pdf_document_page->page_doc = i;
    }

    return 0;
}


