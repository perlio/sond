#include <gtk/gtk.h>
#include <sqlite3.h>

#include "../../treeview.h"
#include "../global_types_sojus.h"

#include "../00misc/auswahl.h"
#include "../../misc.h"
#include "../02Akten/akten.h"

#include "../../fm.h"
#include "../../dbase.h"


static gboolean
cb_file_manager_delete_event( GtkWidget* window, GdkEvent* event, gpointer data )
{
    gchar* regnr_string = NULL;
    GtkWidget* fm_treeview = NULL;
    ModifyFile* modify_file = NULL;

    Sojus* sojus = (Sojus*) data;

    fm_treeview = g_object_get_data( G_OBJECT(window), "fm_treeview" );
    regnr_string = g_object_get_data( G_OBJECT(fm_treeview), "regnr-string" );

    g_object_set_data( G_OBJECT(sojus->app_window), regnr_string, NULL );

    g_free( regnr_string );

    modify_file = g_object_get_data( G_OBJECT(fm_treeview), "modify-file" );
    dbase_destroy( (DBase*) modify_file->data );
    g_free( modify_file );

    return FALSE;
}


static void
file_manager_close( GtkWidget* fm_window )
{
    gboolean ret = FALSE;
    g_signal_emit_by_name( fm_window, "delete-event", &ret );

    return;
}


static gboolean
file_manager_same_project( const gchar* path, const GFile* dest )
{
    gboolean same = FALSE;

    gchar* path_dest = g_file_get_path( (GFile*) dest );

    same = g_str_has_prefix( path_dest, path );

    g_free( path_dest );

    return same;
}


static gint
file_manager_test( const GFile* file, gpointer data, gchar** errmsg )
{
    gint rc = 0;
    gchar* rel_path = NULL;

    DBase* dbase = (DBase*) data;

    rel_path = fm_get_rel_path_from_file( dbase->path, file );

    rc = dbase_test_path( dbase, rel_path, errmsg );
    g_free( rel_path );
    if ( rc == -1 ) ERROR( "dbase_test_path" )
    else if ( rc == 1 ) return 1; //Datei existiert

    return 0;
}


static gint
file_manager_before_move( const GFile* src, const GFile* dest, gpointer data,
        gchar** errmsg )
{
    gint rc = 0;

    DBase* dbase = (DBase*) data;

    if ( !file_manager_same_project( dbase->path, dest ) ) //Verschieben in anderes Projekt
    {
        gint rc = 0;

        rc = file_manager_test( src, data, errmsg ); //Datei in Ursprungsprojekt angebunden?
        if ( rc == -1 ) ERROR( "file_manager_test" )
        else if ( rc == 1 ) return 1; //Wenn ja: überspringen
    }

    rc = dbase_begin( dbase, errmsg );
    if ( rc ) ERROR( "dbase_begin" )

    gchar* rel_path_source = fm_get_rel_path_from_file( dbase->path, src );
    gchar* rel_path_dest = fm_get_rel_path_from_file( dbase->path, dest );

    rc = dbase_update_path( dbase, rel_path_source, rel_path_dest, errmsg );

    g_free( rel_path_source );
    g_free( rel_path_dest );

    if ( rc )
    {
        gint rc = 0;
        gchar* err_rollback = NULL;

        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf dbase_update_path:\n",
                errmsg, NULL );

        rc = dbase_rollback( dbase, &err_rollback );
        if ( rc )
        {
            if ( errmsg ) *errmsg = add_string( *errmsg, g_strconcat( "\n\nBei Aufruf "
                    "dbase_rollback:\n", err_rollback,
                    "\n\nDatenbank inkonsistent", NULL ) );
            g_free( err_rollback );
        }
        else if ( errmsg ) *errmsg = add_string( *errmsg, g_strdup( "\n\n"
                    "Rollback durchgeführt" ) );

        return -1;
    }

    return 0;
}


static gint
file_manager_after_move( gint rc_update, gpointer data, gchar** errmsg )
{
    gint rc = 0;

    DBase* dbase = (DBase*) data;

    if ( rc_update == 1 )
    {
        rc = dbase_rollback( dbase, errmsg );
        if ( rc ) ERROR( "dbase_rollback" )
    }
    else
    {
        rc = dbase_commit( dbase, errmsg );
        if ( rc )
        {
            gint rc = 0;
            gchar* err_rollback = NULL;

            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf dbase_commit:\n",
                    errmsg, NULL );
            rc = dbase_rollback( dbase, &err_rollback );
            if ( rc )
            {
                if ( errmsg ) *errmsg = add_string( *errmsg, g_strconcat( "\n\n"
                        "Bei Aufruf dbase_rollback:\n", err_rollback, NULL ) );
                g_free( err_rollback );
            }
            else if ( errmsg ) *errmsg = add_string( *errmsg, g_strdup( "\n\n"
                    "Rollback durchgeführt" ) );

            return -1;
        }
    }

    return 0;
}


static gint
file_manager_create_modify_file( const gchar* path, ModifyFile** modify_file, gchar** errmsg )
{
    gint rc = 0;
    DBase* dbase = NULL;
    gchar* db_name = NULL;

    db_name = g_strconcat( path, ".ZND", NULL );

    rc = dbase_create_with_stmts( db_name, &dbase, FALSE, errmsg );
    g_free( db_name );
    if ( rc ) // da FALSE, kann nur -1 oder 0 zurückgegeben werden
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf file_manager_create_dbase:\n",
                errmsg, NULL );
        g_free( errmsg );

        return -1;
    }

    *modify_file = g_malloc0( sizeof( ModifyFile ) );

    (*modify_file)->before_move = file_manager_before_move;
    (*modify_file)->after_move = file_manager_after_move;
    (*modify_file)->test = file_manager_test;
    (*modify_file)->data = dbase;

    return 0;
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
    ModifyFile* modify_file = NULL;

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

    g_signal_connect( fm_window, "delete-event", G_CALLBACK(cb_file_manager_delete_event), sojus );

    g_object_set_data( G_OBJECT(sojus->app_window), regnr_string, fm_window );
    g_object_set_data( G_OBJECT(fm_window), "regnr-string", regnr_string );

    akte = akte_oeffnen( sojus, sojus->regnr_akt, sojus->jahr_akt );
    if ( !akte ) file_manager_close( fm_window );

    dokument_dir = g_settings_get_string( sojus->settings, "dokument-dir" );
    dokument_dir = add_string( dokument_dir, g_strdup( "/" ) );
    path = g_strdelimit( g_strdup( akte->bezeichnung ), "/\\", '-' );
    akte_free( akte );
    path = add_string( dokument_dir, path );
    path = add_string( path, g_strdup_printf( " %i-%i/", sojus->regnr_akt,
            sojus->jahr_akt % 100 ) );

    rc = file_manager_create_modify_file( path, &modify_file, &errmsg );
    if ( rc )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(fm_window) ),
                "Fehler FileManager -\n\nBei Aufruf file_manager_create_modify:\n",
                errmsg, NULL );
        g_free( errmsg );
        file_manager_close( fm_window );

        return;
    }

    GtkWidget* fm_treeview = GTK_WIDGET(fm_create_tree_view( sojus->clipboard, modify_file ));
    gtk_container_add( GTK_CONTAINER(swindow), fm_treeview );

    g_object_set_data( G_OBJECT(fm_window), "fm_treeview", fm_treeview );

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
