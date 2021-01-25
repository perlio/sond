#include <gtk/gtk.h>

#include "../../treeview.h"
#include "../global_types_sojus.h"

#include "../00misc/auswahl.h"
#include "../../misc.h"
#include "../02Akten/akten.h"

#include "../../fm.h"


static gboolean
cb_fm_delete_event( GtkWidget* window, GdkEvent* event, gpointer data )
{
    gchar* regnr_string = NULL;
    GtkWidget* fm_treeview = NULL;

    Sojus* sojus = (Sojus*) data;

    fm_treeview = g_object_get_data( G_OBJECT(window), "fm_treeview" );
    regnr_string = g_object_get_data( G_OBJECT(window), "regnr-string" );

    g_object_set_data( G_OBJECT(sojus->app_window), regnr_string, NULL );

    g_free( regnr_string );

    //ggf. db schließen

    return FALSE;
}


static void
file_manager_close( GtkWidget* fm_window )
{
    gboolean ret = FALSE;
    g_signal_emit_by_name( fm_window, "delete-event", &ret );

    return;
}


static gint
file_manager_before_move( const GFile* src, const GFile* dest, gpointer data,
        gchar** errmsg )
{

    return 0;
}


static gint
file_manager_after_move( gint res_before, gpointer data, gchar** errmsg )
{

    return 0;
}


static gint
file_manager_test( const GFile* file, gpointer data, gchar** errmsg )
{

    return 0;
}


static ModifyFile*
file_manager_create_modify_file( Sojus* sojus, gchar* path )
{
    static ModifyFile modify_file = { file_manager_before_move,
            file_manager_after_move, file_manager_test, NULL };

    return &modify_file;
}


void
file_manager_entry_activate( GtkWidget* entry, gpointer data )
{
    gchar* regnr_string = NULL;
    GtkWidget* fm_window = NULL;
    Akte* akte = NULL;
    gchar* dokument_dir = NULL;
    gchar* path = NULL;
    gint rc = 0;
    gchar* errmsg = NULL;

    Sojus* sojus = (Sojus*) data;

    if ( !auswahl_get_regnr_akt( sojus, GTK_ENTRY(entry) ) ) return;

    regnr_string = g_strdup_printf( "%i-%i", sojus->regnr_akt, sojus->jahr_akt );
    if ( (fm_window = g_object_get_data( G_OBJECT(sojus->app_window), regnr_string )) )
    {
        gtk_window_present( GTK_WINDOW(fm_window) );
        g_free( regnr_string );

        return;
    }

    fm_window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_default_size( GTK_WINDOW(fm_window), 1200, 700 );

    GtkWidget* swindow = gtk_scrolled_window_new( NULL, NULL );
    gtk_container_add( GTK_CONTAINER(fm_window), swindow );

    GtkWidget* fm_treeview = GTK_WIDGET(fm_create_tree_view( ));
    gtk_container_add( GTK_CONTAINER(swindow), fm_treeview );

    g_object_set_data( G_OBJECT(fm_treeview), "clipboard", sojus->clipboard );

    g_signal_connect( fm_window, "delete-event", G_CALLBACK(cb_fm_delete_event), sojus );

    g_object_set_data( G_OBJECT(sojus->app_window), regnr_string, fm_window );
    g_object_set_data( G_OBJECT(fm_window), "regnr-string", regnr_string );
    g_object_set_data( G_OBJECT(fm_window), "fm_treeview", fm_treeview );

    akte = akte_oeffnen( sojus, sojus->regnr_akt, sojus->jahr_akt );
    if ( !akte ) file_manager_close( fm_window );

    dokument_dir = g_settings_get_string( sojus->settings, "dokument-dir" );
    dokument_dir = add_string( dokument_dir, g_strdup( "/" ) );
    path = g_strdelimit( g_strdup( akte->bezeichnung ), "/\\", '-' );
    akte_free( akte );
    path = add_string( dokument_dir, path );
    path = add_string( path, g_strdup_printf( " %i-%i/", sojus->regnr_akt,
            sojus->jahr_akt % 100 ) );

    g_object_set_data( G_OBJECT(fm_treeview), "modify-file",
            file_manager_create_modify_file( sojus, path ) );

    rc = fm_set_root( GTK_TREE_VIEW(fm_treeview), path, &errmsg );
    gtk_window_set_title( GTK_WINDOW(fm_window), path );
    g_free( path );
    if ( rc )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(fm_treeview) ),
                "Fehler -\n\nBei Aufruf fm_set_root:\n", errmsg, NULL );
        g_free( errmsg );

        file_manager_close( fm_window );

        return;
    }

    gtk_widget_show_all( fm_window );

    return;
}
