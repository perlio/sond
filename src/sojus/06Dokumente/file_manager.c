#include <gtk/gtk.h>

#include "../../treeview.h"
#include "../global_types_sojus.h"

#include "../00misc/auswahl.h"
#include "../../misc.h"
#include "../02Akten/akten.h"

#include "../../fm.h"


gboolean
cb_fm_delete_event( GtkWidget* window, GdkEvent* event, gpointer user_data )
{

    return FALSE;
}


void
cb_fm_entry( GtkEntry* entry, gpointer data )
{
    gint rc = 0;
    Akte* akte = NULL;
    gchar* path = NULL;
    gchar* errmsg = NULL;

    GtkTreeView* fm = (GtkTreeView*) data;

    Sojus* sojus = g_object_get_data( G_OBJECT(fm), "sojus" );

    if ( !auswahl_get_regnr_akt( sojus, entry ) ) return;

    akte = akte_oeffnen( sojus, sojus->regnr_akt, sojus->jahr_akt );
    if ( !akte ) return;

    path = g_strdelimit( g_strdup( akte->bezeichnung ), "/\\", '-' );
    akte_free( akte );

    path = add_string( path, g_strdup_printf( " %i-%i", sojus->regnr_akt, sojus->jahr_akt % 100 ) );

    rc = fm_set_root( fm, path, &errmsg );
    g_free( path );
    if ( rc )
    {
        display_message( sojus->app_window, "Fehler -\n\nBei Aufruf fm_set_root:\n",
                errmsg, NULL );
        g_free( errmsg );
    }

    //bu_dokument_erzeugen anschalten

    return;
}


void
file_manager_create( GtkWidget* button, gpointer data )
{
    Sojus* sojus = (Sojus*) data;

    GtkWidget* fm_window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
//    gtk_window_set_title( GTK_WINDOW(adressen_window), "Adresse" );
    gtk_window_set_default_size( GTK_WINDOW(fm_window), 1200, 700 );

    GtkWidget* headerbar = gtk_header_bar_new( );
    gtk_header_bar_set_show_close_button( GTK_HEADER_BAR(headerbar), TRUE );
//    gtk_header_bar_set_title( GTK_HEADER_BAR(adressen_headerbar), "Adressen" );
    gtk_window_set_titlebar( GTK_WINDOW(fm_window), headerbar );

    GtkWidget* entry = gtk_entry_new( );
    gtk_header_bar_pack_start( GTK_HEADER_BAR(headerbar), entry );

    GtkWidget* swindow = gtk_scrolled_window_new( NULL, NULL );
    gtk_container_add( GTK_CONTAINER(fm_window), swindow );

    GtkWidget* fm_treeview = GTK_WIDGET(fm_create_tree_view( ));
    gtk_container_add( GTK_CONTAINER(swindow), fm_treeview );

    g_object_set_data( G_OBJECT(fm_treeview), "clipboard", sojus->clipboard );
    g_object_set_data( G_OBJECT(fm_treeview), "sojus", sojus );

    g_signal_connect( entry, "activate", G_CALLBACK(cb_fm_entry), fm_treeview );

    gtk_widget_show_all( fm_window );

    return;
}
