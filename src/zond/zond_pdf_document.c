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

#include <mupdf/fitz.h>
#include <glib/gstdio.h>

#include "99conv/mupdf.h"
#include "99conv/pdf.h"

#include "../misc.h"


typedef enum
{
    PROP_PATH = 1,
    N_PROPERTIES
} ZondPdfDocumentProperty;

typedef struct
{
    GMutex mutex_doc;
    fz_context* ctx;
    fz_document* doc;
    gboolean dirty;
    gchar* path;
    GPtrArray* pages; //array von DocumentPage*
    gchar* errmsg;
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
    case PROP_PATH:
      priv->path = g_strdup( g_value_get_string(value) );
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
        case PROP_PATH:
                g_value_set_string( value, priv->path );
                break;

        default:
                /* We don't have any other property... */
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
                break;
    }
}


static void
zond_pdf_document_close_doc_tmp( ZondPdfDocument* self )
{
    gchar* path_tmp = NULL;

    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( ZOND_PDF_DOCUMENT(self) );

    fz_drop_document( priv->ctx, priv->doc );

    path_tmp = g_strconcat( priv->path, ".tmp", NULL );
    if ( g_remove( path_tmp ) ) display_message( NULL, "Datei ", priv->path,
            ".tmp konnte nicht gelöscht werden.\n\nBei Aufruf g_remove:\n",
            strerror( errno ), NULL );
    g_free( path_tmp );

    return;
}


static void
zond_pdf_document_finalize( GObject* self )
{
    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( ZOND_PDF_DOCUMENT(self) );

    g_ptr_array_unref( priv->pages );

    zond_pdf_document_close_doc_tmp( ZOND_PDF_DOCUMENT(self) );
    mupdf_close_context( priv->ctx ); //ToDo: Prüfen, ob drop_context nicht ausreicht...

    g_free( priv->path );
    g_free( priv->errmsg );
    g_mutex_clear( &priv->mutex_doc );

    ZondPdfDocumentClass* klass = ZOND_PDF_DOCUMENT_GET_CLASS(self);

    g_ptr_array_remove_fast( klass->arr_pdf_documents, self );

    G_OBJECT_CLASS(zond_pdf_document_parent_class)->finalize( self );

    return;
}


static void
zond_pdf_document_page_free( PdfDocumentPage* pdf_document_page )
{
    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( pdf_document_page->document );

    fz_drop_page( priv->ctx, pdf_document_page->page );
    fz_drop_stext_page( priv->ctx, pdf_document_page->stext_page );
    fz_drop_display_list( priv->ctx, pdf_document_page->display_list );
    g_ptr_array_unref( pdf_document_page->arr_annots );

    return;
}


static void
zond_pdf_document_page_annot_free( gpointer data )
{
    PdfDocumentPageAnnot* pdf_document_page_annot = (PdfDocumentPageAnnot*) data;

    g_array_unref( pdf_document_page_annot->arr_quads ),

    g_free( pdf_document_page_annot );

    return;
}


static void
zond_pdf_document_page_annot_load( PdfDocumentPage* pdf_document_page,
        pdf_annot* annot, gint idx )
{
    PdfDocumentPageAnnot* pdf_document_page_annot = NULL;

    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( pdf_document_page->document );

    pdf_document_page_annot = g_malloc0( sizeof( PdfDocumentPageAnnot ) );
    pdf_document_page_annot->idx = idx;
    pdf_document_page_annot->type = pdf_annot_type( priv->ctx, annot );
    pdf_document_page_annot->rect = pdf_annot_rect( priv->ctx, annot );
    pdf_document_page_annot->n_quad = pdf_annot_quad_point_count( priv->ctx, annot );

    pdf_document_page_annot->arr_quads = g_array_new( FALSE, FALSE, sizeof( fz_quad ) );

    for ( gint i = 0; i < pdf_document_page_annot->n_quad; i++ )
    {
        fz_quad quad = pdf_annot_quad_point( priv->ctx, annot, i );
        g_array_append_val( pdf_document_page_annot->arr_quads, quad );
    }

    g_ptr_array_add( pdf_document_page->arr_annots, pdf_document_page_annot );

    return;
}


static void
zond_pdf_document_page_load_annots( PdfDocumentPage* pdf_document_page )
{
    pdf_annot* annot = NULL;

    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( pdf_document_page->document );

    annot = pdf_first_annot( priv->ctx, pdf_page_from_fz_page( priv->ctx,
            pdf_document_page->page ) );

    if ( !annot ) return;

    gint idx = 0;
    do
    {
        zond_pdf_document_page_annot_load( pdf_document_page, annot, idx );
        idx++;
    }
    while ( (annot = pdf_next_annot( priv->ctx, annot )) );

    return;
}


static gint
zond_pdf_document_load_stext_page( ZondPdfDocument* self, gint page_doc, gchar** errmsg )
{
    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( self );
    PdfDocumentPage* pdf_document_page = g_ptr_array_index( priv->pages, page_doc );

    fz_try( priv->ctx ) pdf_document_page->stext_page =
            fz_new_stext_page( priv->ctx, pdf_document_page->rect );
    fz_catch( priv->ctx ) ERROR_MUPDF_CTX( "fz_new_stext_page", priv->ctx )

    return 0;
}


static gint
zond_pdf_document_load_page( ZondPdfDocument* self, gint page_doc, gchar** errmsg )
{
    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( self );
    PdfDocumentPage* pdf_document_page = g_ptr_array_index( priv->pages, page_doc );

    fz_try( priv->ctx ) pdf_document_page->page =
            fz_load_page( priv->ctx, priv->doc, page_doc );
    fz_catch( priv->ctx ) ERROR_MUPDF_CTX( "fz_load_page", priv->ctx );

    pdf_document_page->rect = fz_bound_page( priv->ctx, pdf_document_page->page );

    fz_try( priv->ctx ) pdf_document_page->display_list =
            fz_new_display_list( priv->ctx, pdf_document_page->rect );
    fz_catch( priv->ctx )
    {
        fz_drop_page( priv->ctx, pdf_document_page->page );
        ERROR_MUPDF_CTX( "fz_new_display_list", priv->ctx );
    }

    return 0;
}


static gint
zond_pdf_document_page_init( ZondPdfDocument* self, gint index, gchar** errmsg )
{
    gint rc = 0;

    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( ZOND_PDF_DOCUMENT(self) );

    PdfDocumentPage* pdf_document_page = g_malloc0( sizeof( PdfDocumentPage ) );
    ((priv->pages)->pdata)[index] = pdf_document_page;

    pdf_document_page->document = self; //keine ref!
    pdf_document_page->arr_annots = g_ptr_array_new_with_free_func( zond_pdf_document_page_annot_free );

    rc = zond_pdf_document_load_page( self, index, errmsg );
    if ( rc == -1 ) ERROR_SOND( "zond_pdf_document_load_page" )

    rc = zond_pdf_document_load_stext_page( self, index, errmsg );
    if ( rc == -1 ) ERROR_SOND( "zond_pdf_document_load_stext_page" )

    zond_pdf_document_page_load_annots( pdf_document_page );

    return 0;
}


static fz_document*
zond_pdf_document_doc_tmp_open( ZondPdfDocument* self, gchar** errmsg )
{
    fz_document* doc = NULL;
    gchar* path_tmp = NULL;
    gboolean success = FALSE;
    GError* error = NULL;

    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( ZOND_PDF_DOCUMENT(self) );

    //Datei auf *.tmp kopieren
    path_tmp = g_strconcat( priv->path, ".tmp", NULL );
    GFile* orig = g_file_new_for_path( priv->path );
    GFile* tmp = g_file_new_for_path( path_tmp );

    success = g_file_copy( orig, tmp, G_FILE_COPY_NONE, NULL, NULL, NULL, &error );
    g_object_unref( orig );
    g_object_unref( tmp );
    if ( !success )
    {
        g_free( path_tmp );
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_copy:\n",
                error->message, NULL );
        g_error_free( error );

        return NULL;
    }

    doc = mupdf_dokument_oeffnen( priv->ctx, path_tmp, errmsg );

    if ( !doc )
    {
        if ( errmsg ) *errmsg = add_string( g_strdup( "Bei Aufruf mupdf_dokument_oeffnen:\n" ),
                *errmsg );

        return NULL;
    }

    return doc;
}


static void
zond_pdf_document_constructed( GObject* self )
{
    gchar* errmsg = NULL;
    gint number_of_pages = 0;

    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( ZOND_PDF_DOCUMENT(self) );

    priv->doc = zond_pdf_document_doc_tmp_open( ZOND_PDF_DOCUMENT(self), &errmsg );
    if ( !priv->doc )
    {
        priv->errmsg = g_strconcat( "Bei Aufruf mupdf_dokument_oeffnen:\n",
                errmsg, NULL );
        g_free( errmsg );

        return;
    }

    number_of_pages = fz_count_pages( priv->ctx, priv->doc );
    if ( number_of_pages == 0 )
    {
        priv->errmsg = g_strdup( "Dokument enthält keine Seiten" );

        return;
    }

    g_ptr_array_set_size( priv->pages, number_of_pages );

    for ( gint i = 0; i < priv->pages->len; i++ )
    {
        gint rc = 0;

        rc = zond_pdf_document_page_init( ZOND_PDF_DOCUMENT(self), i, &errmsg );
        if ( rc == -1 )
        {
            priv->errmsg = g_strconcat( "bei Aufruf document_load_page:\n",
                    errmsg, NULL );
            g_free( errmsg );

            return;
        }
    }

    return;
}


static void
zond_pdf_document_class_init( ZondPdfDocumentClass* klass )
{
    GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

    klass->arr_pdf_documents = g_ptr_array_new( );

    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->constructed = zond_pdf_document_constructed;
    object_class->finalize = zond_pdf_document_finalize;

    object_class->set_property = zond_pdf_document_set_property;
    object_class->get_property = zond_pdf_document_get_property;

    obj_properties[PROP_PATH] =
            g_param_spec_string( "path",
                                 "gchar*",
                                 "Pfad zur Datei.",
                                 NULL,
                                  G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

    g_object_class_install_properties(object_class,
                                      N_PROPERTIES,
                                      obj_properties);

    return;
}


static void
zond_pdf_document_init( ZondPdfDocument* self )
{
    gchar* errmsg = NULL;

    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( self );

    priv->ctx = mupdf_init( &errmsg );
    if ( !priv->ctx )
    {
        priv->errmsg = g_strconcat( "Bei Aufruf mupdf_init:\n", errmsg, NULL );
        g_free( errmsg );

        return;
    }

    g_mutex_init( &priv->mutex_doc );

    priv->pages = g_ptr_array_new_with_free_func( (GDestroyNotify)
            zond_pdf_document_page_free );

    return;
}


ZondPdfDocument*
zond_pdf_document_open( const gchar* path, gchar** errmsg )
{
    ZondPdfDocument* zond_pdf_document = NULL;
    ZondPdfDocumentPrivate* priv = NULL;

    ZondPdfDocumentClass* klass = g_type_class_peek( ZOND_TYPE_PDF_DOCUMENT );

    if ( klass )
    {
        for ( gint i = 0; i < klass->arr_pdf_documents->len; i++ )
        {
            zond_pdf_document = g_ptr_array_index( klass->arr_pdf_documents, i );
            ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( zond_pdf_document );

            if ( !g_strcmp0( priv->path, path ) ) return g_object_ref( zond_pdf_document );
        }
    }

    zond_pdf_document = g_object_new( ZOND_TYPE_PDF_DOCUMENT, "path", path, NULL );
    priv = zond_pdf_document_get_instance_private( zond_pdf_document );

    if ( priv->errmsg )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf zond_pdf_document_new:\n",
                priv->errmsg, NULL );
        g_object_unref( zond_pdf_document );

        return NULL;
    }

    if ( klass ) g_ptr_array_add( klass->arr_pdf_documents, zond_pdf_document );

    return zond_pdf_document;
}


const ZondPdfDocument*
zond_pdf_document_is_open( const gchar* path )
{
    ZondPdfDocumentClass* klass = g_type_class_peek_static( zond_pdf_document_get_type( ) );

    for ( gint i = 0; i < klass->arr_pdf_documents->len; i++ )
    {
        ZondPdfDocument* zond_pdf_document = g_ptr_array_index( klass->arr_pdf_documents, i );
        ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( zond_pdf_document );

        if ( !g_strcmp0( priv->path, path ) ) return zond_pdf_document;
    }

    return NULL;
}


gint
zond_pdf_document_save( ZondPdfDocument* self, gchar** errmsg )
{
    gint rc = 0;

    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( self );

    if ( priv->dirty )
    {
        rc = mupdf_save_doc( priv->ctx,
                pdf_specifics( priv->ctx, priv->doc ), priv->path, errmsg );
        if ( rc )
        {
            if ( errmsg ) *errmsg = add_string( g_strdup( "Bei Aufruf mupdf_save_doc:\n" ),
                    *errmsg );

            return -1;
        }

        priv->dirty = FALSE;
    }

    return 0;
}


void
zond_pdf_document_close_doc_and_pages( ZondPdfDocument* self )
{
    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( self );

    for ( gint i = 0; i < priv->pages->len; i++ )
    {
        PdfDocumentPage* pdf_document_page = g_ptr_array_index( priv->pages, i );
        fz_drop_page( priv->ctx, pdf_document_page->page );
        pdf_document_page->page = NULL;
    }

    fz_drop_document( priv->ctx, priv->doc );
    priv->doc = NULL;

    return;
}


gint
zond_pdf_document_reopen_doc_and_pages( ZondPdfDocument* self, gchar** errmsg )
{
    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( self );

    priv->doc = mupdf_dokument_oeffnen( priv->ctx, priv->path, errmsg );
    if ( !priv->doc ) ERROR_SOND( "mupdf_dokument_oeffnen" )

    //Seiten wieder laden
    for ( gint i = 0; i < priv->pages->len; i++ )
    {
        fz_try( priv->ctx ) ((PdfDocumentPage*) ((priv->pages)->pdata)[i])->page =
                fz_load_page( priv->ctx, priv->doc, i );
        fz_catch( priv->ctx ) ERROR_MUPDF_CTX( "fz_load_page", priv->ctx )
    }

    return 0;
}


gboolean
zond_pdf_document_is_dirty( ZondPdfDocument* self )
{
    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( self );

    if ( priv->dirty ) return TRUE;

    return FALSE;
}


void
zond_pdf_document_set_dirty( ZondPdfDocument* self, gboolean dirty )
{
    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( self );

    priv->dirty = dirty;

    return;
}


void
zond_pdf_document_close( ZondPdfDocument* zond_pdf_document )
{
    g_object_unref( zond_pdf_document );

    return;
}


fz_document*
zond_pdf_document_get_fz_doc( ZondPdfDocument* self )
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


gchar*
zond_pdf_document_get_path( ZondPdfDocument* self )
{
    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( self );

    return priv->path;
}


gint
zond_pdf_document_get_index( PdfDocumentPage* pdf_document_page )
{
    guint index = 0;

    ZondPdfDocumentPrivate* priv =
            zond_pdf_document_get_instance_private( pdf_document_page->document );

    if ( g_ptr_array_find( priv->pages, pdf_document_page, &index ) ) return index;

    return -1;
}


void
zond_pdf_document_mutex_lock( ZondPdfDocument* self )
{
    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( self );

    g_mutex_lock( &priv->mutex_doc );

    return;
}


void
zond_pdf_document_mutex_unlock( ZondPdfDocument* self )
{
    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( self );

    g_mutex_unlock( &priv->mutex_doc );

    return;
}


gint
zond_pdf_document_page_refresh( ZondPdfDocument* self, gint page_doc,
        gint flags, gchar** errmsg )
{
    gint rc = 0;

    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( self );

    fz_context* ctx = priv->ctx;

    PdfDocumentPage* pdf_document_page = g_ptr_array_index( priv->pages, page_doc );

    //Seite clearen
    fz_drop_page( ctx, pdf_document_page->page );
    fz_drop_display_list( ctx, pdf_document_page->display_list );

    rc = zond_pdf_document_load_page( self, page_doc, errmsg );
    if ( rc == -1 ) ERROR_SOND( "zond_pdf_document_load_page" )

    if ( flags & 1 )
    {
        fz_drop_stext_page( ctx, pdf_document_page->stext_page );
        pdf_document_page->stext_page = NULL;

        zond_pdf_document_load_stext_page( self, page_doc, errmsg );
        if ( rc ) ERROR_SOND( "zond_pdf_document_load_stext_page" )
    }
    if ( flags & 2 )
    {
        g_ptr_array_remove_range( pdf_document_page->arr_annots, 0, pdf_document_page->arr_annots->len );

        zond_pdf_document_page_load_annots( pdf_document_page );
    }

    return 0;
}


gint
zond_pdf_document_insert_pages( ZondPdfDocument* zond_pdf_document, gint pos,
        gint num, fz_context* ctx, pdf_document* pdf_doc, gchar** errmsg )
{
    gint rc = 0;
    gint count = 0;

    ZondPdfDocumentPrivate* priv = zond_pdf_document_get_instance_private( zond_pdf_document );

    count = pdf_count_pages( ctx, pdf_doc );
    if ( count == 0 ) return 0;

    //einfügen in doc
    rc = pdf_copy_page( ctx, pdf_doc, 0, pdf_count_pages( ctx, pdf_doc ) - 1,
            pdf_specifics( zond_pdf_document_get_ctx( zond_pdf_document ),
            zond_pdf_document_get_fz_doc( zond_pdf_document ) ), pos, errmsg );
    if ( rc ) ERROR_SOND( "pdf_copy-page" )

    for ( gint i = 0; i < count; i++ )
    {
        gint rc = 0;

        g_ptr_array_insert( priv->pages, pos + i, NULL );

        rc = zond_pdf_document_page_init( zond_pdf_document, pos + i, errmsg );
        if ( rc == -1 ) ERROR_SOND( "zond_pdf_document_page_init" )
    }

    for ( gint i = pos + count; i < priv->pages->len; i++ )
    {
        gint rc = 0;

        rc = zond_pdf_document_page_refresh( zond_pdf_document, i, 2, errmsg );
        if ( rc == -1 ) ERROR_SOND( "zond_pdf_document_page_refresh" )
    }

    return 0;
}


