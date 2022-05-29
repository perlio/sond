#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <gtk/gtk.h>
#include <sqlite3.h>

#include "../global_types.h"
#include "../zond_pdf_document.h"
#include "../zond_database.h"
#include "../zond_tree_store.h"
#include "../../sond_treeview.h"
#include "../../misc.h"
#include "../../sond_database_node.h"
#include "../../sond_database_entity.h"
#include "../zond_gemini.h"

#include "general.h"
#include "pdf.h"
#include "../20allgemein/pdf_text.h"


gint
test( Projekt* zond, gchar** errmsg )
{
    gint rc = 0;
    gchar* text = NULL;
    gint ID_entity = 0;
    GtkWidget* window = NULL;
    GtkWidget* swindow = NULL;
    GtkWidget* sdn = NULL;

    rc = abfrage_frage( zond->app_window, "ID_entity eingeben", NULL, &text );
    if ( rc != GTK_RESPONSE_YES ) return 0;

    ID_entity = atoi( text );

    window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    swindow = gtk_scrolled_window_new( NULL, NULL );

    sdn = sond_database_node_load_new( zond->dbase_zond->zond_dbase_work, ID_entity, errmsg );
    if ( !sdn ) ERROR_S

    gtk_container_add( GTK_CONTAINER(swindow), sdn );
    gtk_container_add( GTK_CONTAINER(window), swindow );

    gtk_widget_show_all( window );

    return 0;
}


/*
static gint
datei_schreiben_guuid( const gchar* full_path, gchar** guuid_ret, gchar** errmsg )
{
    gchar* filename = NULL;
    GFile* file = NULL;
    GFileOutputStream* stream = NULL;
    GError* error = NULL;

    if (  filesystem == 1 ) //NTFS oder SMB
    {
        filename = g_strconcat( full_path, ":guuid", NULL );
        file = g_file_new_for_path( filename );
        g_free( filename );

        stream = g_file_append_to( file, G_FILE_CREATE_NONE, NULL, &error );
        if ( !stream )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_read:\n",
                    error->message, NULL );
            g_error_free( error );
            g_object_unref( file );

            return -1;
        }

        gchar* guuid = g_uuid_string_random( );

        gssize size = g_output_stream_write( G_OUTPUT_STREAM(stream), guuid,
                strlen( guuid ) + 1, NULL, &error );
        if ( size == -1 )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_input_stream_read:\n",
                    error->message, NULL );
            g_error_free( error );
            g_object_unref( stream );
            g_object_unref( file );
            g_free( guuid );

            return -1;
        }
        if ( !g_output_stream_close( G_OUTPUT_STREAM(stream), NULL, &error ) )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_output_stream_close:\n",
                    error->message, NULL );
            g_error_free( error );
            g_object_unref( stream );
            g_object_unref( file );
            g_free( guuid );

            return -1;
        }
        g_object_unref( stream );
        g_object_unref( file );

        *guuid_ret = guuid;
    }

    return 0;
}
*/

/** Fehler: -1
    guuid wird zurückgegeben: 0
    keine guuid gefunden: 1  **/
/*
static gint
datei_lesen_guuid( const gchar* full_path, gchar**guuid, gchar** errmsg )
{
    GFile* file = NULL;
    GFileInputStream* stream = NULL;
    GError* error = NULL;

    gchar* filename = g_strconcat( full_path, ":guuid", NULL );
    file = g_file_new_for_path( filename );

    stream = g_file_read( file, NULL, &error );
    if ( !stream )
    {
        gint ret = 0;
        if ( error->code != G_IO_ERROR_NOT_FOUND )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_read:\n",
                    error->message, NULL );
            ret = -1;
        }
        else ret = 1;
        g_error_free( error );
        g_object_unref( file );

        return ret;
    }

    gchar buffer[100] = { 0 };

    gssize size = g_input_stream_read( G_INPUT_STREAM(stream), buffer,
            100, NULL, &error );
    if ( size == -1 )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_input_stream_read:\n",
                error->message, NULL );
        g_error_free( error );
        g_object_unref( stream );
        g_object_unref( file );

        return -1;
    }
    g_object_unref( stream );
    g_object_unref( file );
    g_free( filename );

    *guuid = g_strdup( buffer );

    //überprüfen, ob gültige guuid
    if ( !g_uuid_string_is_valid( *guuid ) )
    {
        g_free( *guuid );
        if (errmsg ) *errmsg = g_strconcat( "Datei '", full_path, "' enthält "
                "keine gültige guuid", NULL );

        return -1;
    }

    return 0;
}


static gint
datei_query_filesystem( const gchar* filename, gchar** errmsg )
{

    GFile* file = NULL;
    GError* error = NULL;
    GFileInfo* info = NULL;
    gchar* type = NULL;
    gint ret = 0;

    file = g_file_new_for_path( filename );
    info = g_file_query_info( file, "*", 0, NULL, &error );
    if ( !info )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_read:\n",
                error->message, NULL );
        g_error_free( error );
        g_object_unref( file );

        return -1;
    }
    gchar** list = g_file_info_list_attributes( info, NULL );
    while ( *list != NULL )
    {
        gchar* text = *list;
        type = g_file_info_get_attribute_as_string( info, text );
        printf( "%s: %s\n", text, type);
        g_free( type );
        list++;
    }

    g_object_unref( info );
    printf("\n\n");

    info = g_file_query_filesystem_info( file, "*", NULL, &error );
    if ( !info )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_read:\n",
                error->message, NULL );
        g_error_free( error );
        g_object_unref( file );

        return -1;
    }
    list = g_file_info_list_attributes( info, NULL );
    while ( *list != NULL )
    {
        gchar* text = *list;
        type = g_file_info_get_attribute_as_string( info, text );
        printf( "%s: %s\n", text, type);
        g_free( type );
        list++;
    }

    g_object_unref( info );
    g_object_unref( file );

    return ret;
}


gint
pdf_print_content_stream( fz_context* ctx, pdf_obj* page_ref, gchar** errmsg )
{
    fz_buffer* buf = NULL;
    gchar* data = NULL;
    gchar* pos = NULL;
    size_t size = 0;

    buf = pdf_ocr_get_content_stream_as_buffer( ctx, page_ref, errmsg );
    if ( !buf ) ERROR_SOND( "pdf_ocr_get_content_stream_as_buffer" )

    size = fz_buffer_storage( ctx, buf, (guchar**) &data );
    pos = data;
    for ( gint i = 0; i < size; i++ )
    {
        if ( !(*pos == 0 ||
                *pos == 9 ||
                *pos == 10 ||
                *pos == 12 ||
                *pos == 13 ||
                *pos == 31) ) printf("%c", *pos );
        else if ( *pos == 10 || *pos == 13 ) printf("\n");
        else if ( *pos == 0 ) printf("(null)\n");
        else printf(" ");
        pos++;
    }

    return 0;
}

*/
