#include "../global_types.h"
#include "../error.h"
#include "../zond_pdf_document.h"
#include "../zond_database.h"

#include "../99conv/general.h"
#include "pdf.h"
#include "pdf_ocr.h"

#include "../20allgemein/project.h"

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
#include <gtk/gtk.h>
#include <sqlite3.h>



static gint
datei_schreiben_guuid( const gchar* full_path, gchar** guuid_ret, gchar** errmsg )
{
    gchar* filename = NULL;
    GFile* file = NULL;
    GFileOutputStream* stream = NULL;
    GError* error = NULL;

    if (  /*filesystem ==*/ 1 ) //NTFS oder SMB
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


/** Fehler: -1
    guuid wird zurückgegeben: 0
    keine guuid gefunden: 1  **/
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
/*
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

    return ret;  */

    return 1;
}


gint
pdf_print_content_stream( fz_context* ctx, pdf_obj* page_ref, gchar** errmsg )
{
    fz_buffer* buf = NULL;
    gchar* data = NULL;
    gchar* pos = NULL;
    size_t size = 0;

    buf = pdf_ocr_get_content_stream_as_buffer( ctx, page_ref, errmsg );
    if ( !buf ) ERROR_PAO( "pdf_ocr_get_content_stream_as_buffer" )

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


#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#endif // _WIN32

#include "../40viewer/viewer_pixbuf.h"
#include <mupdf/fitz.h>


static void
begin_print( GtkPrintOperation* op, GtkPrintContext* context )
{
    printf( "begin\n" );

    return;
}


static void
draw_page( GtkPrintOperation* op, GtkPrintContext* context, gint page_nr, gpointer user_data )
{
    gdouble dpi_x = 0;
    gdouble dpi_y = 0;
    gdouble width = 0;
    gdouble height = 0;

    dpi_x = gtk_print_context_get_dpi_x( context );
    dpi_y = gtk_print_context_get_dpi_y( context );
    width = gtk_print_context_get_width( context );
    height = gtk_print_context_get_height( context );


  //  gdk_cairo_set_source_pixbuf( gtk_print_context_get_cairo_context( context ), pixbuf, 0, 0 );

    return;
}


gint
test( Projekt* zond, gchar** errmsg )
{
    GtkPrintOperation* print = NULL;
    GtkPrintOperationResult res;

    print = gtk_print_operation_new( );

    gtk_print_operation_set_n_pages( print, 12 );

    g_signal_connect (print, "begin_print", G_CALLBACK (begin_print), NULL);
    g_signal_connect (print, "draw_page", G_CALLBACK (draw_page), NULL);

    res = gtk_print_operation_run (print, GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,
                                 GTK_WINDOW (zond->app_window), NULL);

    printf("res: %i\n", res);

    return 0;
}


/*

    gint rc = 0;
    sqlite3* db = NULL;

    rc = sqlite3_open_v2( "kl.txt", &db, SQLITE_OPEN_READWRITE, NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_open_v2:\n",
                sqlite3_errstr( rc ), NULL );
        sqlite3_close( db );

        return -1;
    }

    return 0;
}



    //Pfad LibreOffice herausfinden
    gchar soffice_exe[270] = { 0 };
    GError* error = NULL;

#ifdef _WIN32
    HRESULT rc = 0;

    DWORD bufferlen = 270;

    rc = AssocQueryString( 0, ASSOCSTR_EXECUTABLE, ".odt", "open", soffice_exe,
            &bufferlen );
    if ( rc != S_OK )
    {
        if ( errmsg ) *errmsg = g_strdup( "Export nicht möglich:\n\nFehler bei Aufruf "
                "AssocQueryString" );

        return -1;
    }
#else
    //für Linux etc: Pfad von soffice suchen
#endif // _WIN32

    //htm-Datei umwandeln
    gboolean ret = FALSE;

    gchar* argv[6] = { NULL };
    argv[0] = soffice_exe;
 //   argv[1] = "-o";
    argv[1] = "C:/Users/pkrieger/AppData/Roaming/LibreOffice/4/user/template/vorlagen/Briefkopf.ott";
//    argv[2] = "vnd.sun.star.script:Standard.Module.Main?language=Basic&location=application";
    argv[2] = "macro:///Standard.Module.Dokument_erzeugen(2,2020,3,0,0)";
//  127.0.0.1:3306", "root", "ttttt", 2, 2020, 3, 0, 0
    ret = g_spawn_async( NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL, NULL,
            &error );
    if ( !ret )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Export nicht möglich:\n\nFehler bei Aufruf "
                "g_spawn_sync:\n", error->message, NULL );
        g_error_free( error );

        return -1;
    }

    return 0;
}
*/

