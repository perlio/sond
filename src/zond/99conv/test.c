#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <gtk/gtk.h>
#include <sqlite3.h>

#include "../global_types.h"
#include "../pdf_ocr.h"
#include "../zond_pdf_document.h"
#include "../zond_tree_store.h"
#include "../zond_gemini.h"
#include "../../sond_treeview.h"
#include "../../sond_treeviewfm.h"
#include "../../misc.h"
#include "../../sond_database_node.h"
#include "../../sond_database_entity.h"
#include "../zond_gemini.h"

#include "general.h"
#include "pdf.h"
#include "../20allgemein/pdf_text.h"



/** rc == -1: Fähler
    rc == 0: alles ausgeführt, sämtliche Callbacks haben 0 zurückgegeben
    rc == 1: alles ausgeführt, mindestens ein Callback hat 1 zurückgegeben
    rc == 2: nicht alles ausgeführt, Callback hat 2 zurückgegeben -> sofortiger Abbruch
    **/
static gint
dir_foreach( GFile* file, gboolean rec, gint (*foreach) ( GFile*, GFileInfo*, gpointer, gchar** ),
        gpointer data, gchar** errmsg )
{
    GError* error = NULL;
    gboolean flag = FALSE;
    GFileEnumerator* enumer = NULL;

    enumer = g_file_enumerate_children( file, "*", G_FILE_QUERY_INFO_NONE, NULL, &error );
    if ( !enumer )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerate_children:\n",
                error->message, NULL );
        g_error_free( error );

        return -1;
    }

    while ( 1 )
    {
        GFile* file_child = NULL;
        GFileInfo* info_child = NULL;

        if ( !g_file_enumerator_iterate( enumer, &info_child, &file_child, NULL, &error ) )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerator_iterate:\n",
                    error->message, NULL );
            g_error_free( error );
            g_object_unref( enumer );

            return -1;
        }

        if ( file_child ) //es gibt noch Datei in Verzeichnis
        {
            gint rc = 0;

            rc = foreach( file_child, info_child, data, errmsg );
            if ( rc == -1 )
            {
                g_object_unref( enumer );
                if ( errmsg ) *errmsg = add_string( g_strdup( "Bei Aufruf foreach:\n" ),
                        *errmsg );

                return -1;
            }
            else if ( rc == 1 ) flag = TRUE;
            else if ( rc == 2 ) //Abbruch gewählt
            {
                g_object_unref( enumer );
                return 2;
            }

            if ( rec && g_file_info_get_file_type( info_child ) == G_FILE_TYPE_DIRECTORY )
            {
                gint rc = 0;

                rc = dir_foreach( file_child, TRUE, foreach, data, errmsg );
                if ( rc == -1 )
                {
                    g_object_unref( enumer );
                    if ( errmsg ) *errmsg = add_string( g_strdup( "Bei Aufruf foreach:\n" ),
                            *errmsg );

                    return -1;
                }
                else if ( rc == 1 ) flag = TRUE;//Abbruch gewählt
                else if ( rc == 2 )
                {
                    g_object_unref( enumer );
                    return 2;
                }
            }
        } //ende if ( file_child )
        else break;
    }

    g_object_unref( enumer );

    return (flag) ? 1 : 0;
}


gint
test_II( Projekt* zond, gchar** errmsg )
{
    GFile* file_root = NULL;
    const gchar* root = "C:/Users/nc-kr/laufende Akten";
    InfoWindow* info_window = NULL;
    gint rc = 0;

    file_root = g_file_new_for_path( root );
    info_window = info_window_open( zond->app_window, "Untersuchung auf InlineImages" );
 //   rc = dir_foreach( file_root, TRUE, test_pdf, info_window, errmsg );
    info_window_close( info_window );
    if ( rc == -1 ) ERROR_S

    return 0;
}


gint
test( Projekt* zond, gchar** errmsg )
{
    GList* list_app_info = NULL;

    list_app_info = g_app_info_get_all( );

    while ( list_app_info )
    {
        gchar** list_type = NULL;

        printf( "%s   %s\n", g_app_info_get_commandline( G_APP_INFO(list_app_info->data) ),
                g_app_info_get_executable( G_APP_INFO(list_app_info->data) ) );

        list_type = g_app_info_get_supported_types( G_APP_INFO(list_app_info->data) );
        while ( *list_type )
        {
            printf("%s\n", *list_type );
            list_type++;
        }

        list_app_info = list_app_info->next;
    }

    g_list_free_full( list_app_info, g_object_unref );

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
*/


void
pdf_print_buffer( fz_context* ctx, fz_buffer* buf )
{
    gchar* data = NULL;
    gchar* pos = NULL;
    size_t size = 0;

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

    return;
}


fz_buffer*
pdf_ocr_get_content_stream_as_buffer( fz_context* ctx, pdf_obj* page_ref,
        gchar** errmsg );

gint
pdf_print_content_stream( fz_context* ctx, pdf_obj* page_ref, gchar** errmsg )
{
    fz_buffer* buf = NULL;
    buf = pdf_ocr_get_content_stream_as_buffer( ctx, page_ref, errmsg );
    if ( !buf ) ERROR_SOND( "pdf_ocr_get_content_stream_as_buffer" )

    pdf_print_buffer( ctx, buf );

    return 0;
}


