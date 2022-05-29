#include <stdlib.h>
#include <gtk/gtk.h>
#include <ctype.h>

#include "general.h"

#include "../../misc.h"



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
info_window_set_progress_bar_fraction( InfoWindow* info_window, gdouble fraction )
{
    if ( !GTK_IS_PROGRESS_BAR(info_window->last_inserted_widget) ) return;

    gtk_progress_bar_set_fraction( GTK_PROGRESS_BAR(info_window->last_inserted_widget), fraction );

    while ( gtk_events_pending( ) ) gtk_main_iteration( );

    return;
}


void
info_window_set_progress_bar( InfoWindow* info_window )
{
    info_window->last_inserted_widget = gtk_progress_bar_new( );
    gtk_widget_show( info_window->last_inserted_widget );

    while ( gtk_events_pending( ) ) gtk_main_iteration( );

    info_window_scroll( info_window );

    return;
}


void
info_window_set_message( InfoWindow* info_window, const gchar* message )
{
    info_window->last_inserted_widget = gtk_label_new( message );
    gtk_widget_set_halign( info_window->last_inserted_widget, GTK_ALIGN_START );
    gtk_box_pack_start( GTK_BOX(info_window->content), info_window->last_inserted_widget, FALSE, FALSE, 0 );
    gtk_widget_show_all( info_window->last_inserted_widget );

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

