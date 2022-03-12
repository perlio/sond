#include <stdlib.h>

#include <gtk/gtk.h>
#include <ctype.h>

#include "../zond_pdf_document.h"
#include "../zond_tree_store.h"
#include "../zond_dbase.h"

#include "../global_types.h"
#include "../error.h"

#include "../../misc.h"
#include "../../dbase.h"

#include "../99conv/baum.h"
#include "../99conv/db_zu_baum.h"
#include "../99conv/pdf.h"
#include "../20allgemein/project.h"


#include "../40viewer/document.h"



/** String Utilities **/
gchar*
utf8_to_local_filename( const gchar* utf8 )
{
    //utf8 in filename konvertieren
    gsize written;
    gchar* charset = g_get_codeset();
    gchar* local_filename = g_convert( utf8, -1, charset, "UTF-8", NULL, &written,
            NULL );
    g_free( charset );

    return local_filename; //muß g_freed werden!
}


gint
string_to_guint( const gchar* string, guint* zahl )
{
    gboolean is_guint = TRUE;
    if ( !strlen( string ) ) is_guint = FALSE;
    gint i = 0;
    while ( i < (gint) strlen( string ) && is_guint )
    {
        if ( !isdigit( (int) *(string + i) ) ) is_guint = FALSE;
        i++;
    }

    if ( is_guint )
    {
        *zahl = atoi( string );
        return 0;
    }
    else return -1;
}


/** Andere Sachen **/
gchar*
filename_speichern( GtkWindow* window, const gchar* titel )
{
    GSList* list = choose_files( GTK_WIDGET(window), NULL, titel, "Speichern",
            GTK_FILE_CHOOSER_ACTION_SAVE, FALSE );

    if ( !list ) return NULL;

    gchar* uri_unescaped = g_uri_unescape_string( list->data, NULL );
    g_free( list->data );
    g_slist_free( list );

    gchar* abs_path = g_strdup( uri_unescaped + 8 );
    g_free( uri_unescaped );

    return abs_path; //muß g_freed werden
}


gchar*
filename_oeffnen( GtkWindow* window )
{
    GSList* list = choose_files( GTK_WIDGET(window), NULL, "Datei auswählen", "Öffnen",
            GTK_FILE_CHOOSER_ACTION_OPEN, FALSE );

    if ( !list ) return NULL;

    gchar* uri_unescaped = g_uri_unescape_string( list->data, NULL );
    g_free( list->data );
    g_slist_free( list );

    gchar* abs_path = g_strdup( uri_unescaped + 8);
    g_free( uri_unescaped );

    return abs_path; //muß g_freed werden
}


gboolean
is_pdf( const gchar* path )
{
    gchar* content_type = NULL;
    gboolean res = FALSE;

    content_type = g_content_type_guess( path, NULL, 0, NULL );

    //Sonderbehandung, falls pdf-Datei
    if ( (!g_strcmp0( content_type, ".pdf" ) || !g_strcmp0( content_type,
            "application/pdf" )) ) res = TRUE;
    g_free( content_type );

    return res;
}


void
info_window_close( InfoWindow* info_window )
{
    GtkWidget* button =
            gtk_dialog_get_widget_for_response( GTK_DIALOG(info_window->dialog),
            GTK_RESPONSE_CANCEL );
    gtk_button_set_label( GTK_BUTTON(button), "Schließen" );
    gtk_widget_grab_focus( button );

    gtk_dialog_run( GTK_DIALOG(info_window->dialog) );

    gtk_widget_destroy( info_window->dialog );

    g_free( info_window );

    return;
}


void
info_window_scroll( InfoWindow* info_window )
{
    GtkWidget* viewport = NULL;
    GtkWidget* swindow = NULL;
    GtkAdjustment* adj = NULL;

    viewport = gtk_widget_get_parent( info_window->content);
    swindow = gtk_widget_get_parent( viewport );
    adj = gtk_scrolled_window_get_vadjustment( GTK_SCROLLED_WINDOW(swindow) );
    gtk_adjustment_set_value( adj, gtk_adjustment_get_upper( adj ) );

    return;
}


void
info_window_set_message( InfoWindow* info_window, const gchar* message )
{
    GtkWidget* label = NULL;

    label = gtk_label_new( message );
    gtk_widget_set_halign( label, GTK_ALIGN_START );
    gtk_box_pack_start( GTK_BOX(info_window->content), label, FALSE, FALSE, 0 );
    gtk_widget_show_all( label );

    while ( gtk_events_pending( ) ) gtk_main_iteration( );

    info_window_scroll( info_window );

    return;
}


static void
cb_info_window_response( GtkDialog* dialog, gint id, gpointer data )
{
    InfoWindow* info_window = (InfoWindow*) data;

    if ( info_window->cancel ) return;

    info_window_set_message( info_window, "...abbrechen" );
    info_window->cancel = TRUE;

    return;
}


InfoWindow*
info_window_open( GtkWidget* window, const gchar* title )
{
    GtkWidget* content = NULL;
    GtkWidget* swindow = NULL;

    InfoWindow* info_window = g_malloc0( sizeof( InfoWindow ) );

    info_window->dialog = gtk_dialog_new_with_buttons( title, GTK_WINDOW(window),
            GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL, "Abbrechen",
            GTK_RESPONSE_CANCEL, NULL );

    gtk_window_set_default_size( GTK_WINDOW(info_window->dialog), 450, 110 );

    content = gtk_dialog_get_content_area( GTK_DIALOG(info_window->dialog) );
    swindow = gtk_scrolled_window_new( NULL, NULL );
    gtk_box_pack_start( GTK_BOX(content), swindow, TRUE, TRUE, 0 );

    info_window->content = gtk_box_new( GTK_ORIENTATION_VERTICAL, 0 );
    gtk_container_add( GTK_CONTAINER(swindow), info_window->content );

    gtk_widget_show_all( info_window->dialog );

    g_signal_connect( GTK_DIALOG(info_window->dialog), "response",
            G_CALLBACK(cb_info_window_response), info_window );

    return info_window;
}

