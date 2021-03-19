/*
zond (pdf_datei.c) - Akten, Beweisstücke, Unterlagen
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

#include "pdf_datei.h"

#include "../../misc.h"

/*
typedef struct
{
    GMutex mutex_doc;
    fz_context* ctx;
    fz_document* doc;
    gboolean dirty;
    gchar* path;
    GPtrArray* pages; //array von DocumentPage*
} PdfDocumentPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(PdfDocument, pdf_document, G_TYPE_OBJECT)


static void
pdf_document_class_init( PdfDocumentClass* klass )
{
    klass->open_docs = g_ptr_array_new( );

    return;
}


static void
pdf_document_init( PdfDocument* self )
{

    return;
}


Document*
document_new_from_path( const gchar* rel_path, gchar** errmsg )
{
    PdfDocument*
    PdfDocumentClass* klass = pdf_document
    gint rc = 0;
    Document* document = 0;
    gint number_of_pages = 0;

    document = g_malloc0( sizeof( Document ) );

    document->ctx = mupdf_init( );
    if ( !document->ctx ) ERROR_PAO_R( "mupdf_init", NULL )

    document->doc = mupdf_dokument_oeffnen( document->ctx, rel_path, errmsg );
    if ( !document->doc )
    {
        mupdf_close_context( document->ctx );
        g_free( document );
        ERROR_PAO_R( "mupdf_dokument_oeffnen", NULL )
    }
    number_of_pages = fz_count_pages( document->ctx, document->doc );
    if ( number_of_pages == 0 )
    {
        fz_drop_document( document->ctx, document->doc );
        mupdf_close_context( document->ctx );
        g_free( document );
        if ( errmsg ) *errmsg = g_strdup( "Dokument enthält keine Seiten" );

        return NULL;
    }

    g_mutex_init( &document->mutex_doc );

    document->path = g_strdup( rel_path );
    document->ref_count = 1;

    document->pages = g_ptr_array_sized_new( number_of_pages );
    g_ptr_array_set_free_func( document->pages, (GDestroyNotify) document_free_page );

    for ( gint i = 0; i < number_of_pages; i++ )
            g_ptr_array_add( document->pages, NULL );

    for ( gint i = 0; i < document->pages->len; i++ )
    {
        rc = document_load_page( document, i, errmsg );
        if ( rc == -1 )
        {
            g_ptr_array_unref( document->pages );
            g_free( document->path );
            g_mutex_clear( &document->mutex_doc );
            fz_drop_document( document->ctx, document->doc );
            mupdf_close_context( document->ctx );
            g_free( document );

            ERROR_PAO_R( "document_load_page", NULL )
        }
        //Wenn 1 einfach weiter; Seite wurde halt schon geladen
    }

    return document;
}

*/
