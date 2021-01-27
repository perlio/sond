#include <gtk/gtk.h>
#include <sqlite3.h>

#include "../../treeview.h"
#include "../global_types_sojus.h"

#include "../00misc/auswahl.h"
#include "../../misc.h"
#include "../02Akten/akten.h"

#include "../../fm.h"


typedef struct _D_Base
{
    gchar* path;
    sqlite3* db;
    sqlite3_stmt* update_path;
    sqlite3_stmt* test_path;
    sqlite3_stmt* begin;
    sqlite3_stmt* commit;
    sqlite3_stmt* rollback;
} DBase;


static void
file_manager_finish_modify_file( ModifyFile* modify_file )
{
    if ( !modify_file ) return;

    if ( modify_file->data )
    {
        DBase* dbase = (DBase*) modify_file->data;

        sqlite3_finalize( dbase->update_path );
        sqlite3_finalize( dbase->test_path );
        sqlite3_finalize( dbase->begin );
        sqlite3_finalize( dbase->commit );
        sqlite3_finalize( dbase->rollback );
        sqlite3_close( dbase->db );

        g_free( dbase );
    }

    g_free( modify_file );

    return;
}


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

    file_manager_finish_modify_file( modify_file );

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
file_manager_transaction( sqlite3* db, sqlite3_stmt* stmt, gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( stmt );

    rc = sqlite3_step( stmt );
    if ( rc != SQLITE_DONE )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_step:\n",
                sqlite3_errmsg( db ), NULL );

        return -1;
    }

    return 0;
}


static gint
file_manager_update_path( sqlite3* db, sqlite3_stmt* stmt,
        const gchar* rel_path_source, const gchar* rel_path_dest, gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( stmt );

    rc = sqlite3_bind_text( stmt, 1, rel_path_source, -1, NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_bind_text "
                "(rel_path_source):\n", sqlite3_errmsg( db ), NULL );

        return -1;
    }

    rc = sqlite3_bind_text( stmt, 2, rel_path_source, -1, NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_bind_text "
                "(rel_path_dest):\n", sqlite3_errmsg( db ), NULL );

        return -1;
    }

    rc = sqlite3_step( stmt );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_step:\n",
                sqlite3_errmsg( db ), NULL );

        return -1;
    }

    return 0;
}


static gint
file_manager_test( const GFile* file, gpointer data, gchar** errmsg )
{
    gint rc = 0;
    gchar* rel_path = NULL;

    DBase* dbase = (DBase*) data;

    rel_path = fm_get_rel_path_from_file( dbase->path, file );

    sqlite3_reset( dbase->test_path );

    rc = sqlite3_bind_text( dbase->test_path, 1, rel_path, -1, NULL );
    g_free( rel_path );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_bind_text (rel_path):\n",
                sqlite3_errmsg( dbase->db ), NULL );

        return -1;
    }

    rc = sqlite3_step( dbase->test_path );
    if ( (rc != SQLITE_ROW) && rc != SQLITE_DONE )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_step:\n",
                sqlite3_errmsg( dbase->db ), NULL );
        g_free( rel_path );

        return -1;
    }
    else if ( rc == SQLITE_ROW ) return 1;

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
        if ( rc == -1 )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf file_manager_test:\n",
                    errmsg, NULL );

            return -1;
        }
        else if ( rc == 1 ) return 1; //Wenn ja: überspringen
    }

    rc = file_manager_transaction( dbase->db, dbase->begin, errmsg );
    if ( rc == -1 )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf file_manager_transaction "
                "(begin):\n", errmsg, NULL );

        return -1;
    }

    gchar* rel_path_source = fm_get_rel_path_from_file( dbase->path, src );
    gchar* rel_path_dest = fm_get_rel_path_from_file( dbase->path, dest );

    rc = file_manager_update_path( dbase->db, dbase->update_path,
            rel_path_source, rel_path_dest, errmsg );

    g_free( rel_path_source );
    g_free( rel_path_dest );

    if ( rc )
    {
        gint rc = 0;
        gchar* err_rollback = NULL;

        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf file_manager_update_path:\n",
                errmsg, NULL );

        rc = file_manager_transaction( dbase->db, dbase->rollback, &err_rollback );
        if ( rc )
        {
            if ( errmsg ) *errmsg = add_string( *errmsg, g_strconcat( "\n\nBei Aufruf "
                    "file_manager_transaction (rollback):\n", err_rollback,
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
        rc = file_manager_transaction( dbase->db, dbase->rollback, errmsg );
        if ( rc )
        {
            if ( errmsg ) *errmsg = add_string( *errmsg, g_strconcat( "Bei Aufruf "
                    "file_manager_transaction (rollback):\n", errmsg, NULL ) );

            return -1;
        }

    }
    else
    {
        rc = file_manager_transaction( dbase->db, dbase->commit, errmsg );
        if ( rc )
        {
            gint rc = 0;
            gchar* err_rollback = NULL;

            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf file_manager_"
                    "transaction (commit):\n", errmsg, NULL );
            rc = file_manager_transaction( dbase->db, dbase->rollback, &err_rollback );
            if ( rc )
            {
                if ( errmsg ) *errmsg = add_string( *errmsg, g_strconcat( "\n\n"
                        "Bei Aufruf file_manager_transaction (rollback):\n",
                        err_rollback, NULL ) );
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
file_manager_create_dbase( const gchar* path, DBase** dbase, gchar** errmsg )
{
    gint rc = 0;
    gchar* path_db = NULL;
    sqlite3* db = NULL;
    sqlite3_stmt* stmt_update = NULL;
    sqlite3_stmt* stmt_test = NULL;
    sqlite3_stmt* stmt_begin = NULL;
    sqlite3_stmt* stmt_commit = NULL;
    sqlite3_stmt* stmt_rollback = NULL;

    path_db = g_strconcat( path, ".ZND", NULL );
    rc = sqlite3_open_v2( path_db, &db, SQLITE_OPEN_READWRITE, NULL );
    g_free( path_db );
    if ( rc != SQLITE_OK && rc != SQLITE_CANTOPEN )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_open:\n",
                sqlite3_errstr( rc ), NULL );

        return -1;
    }
    else if ( rc == SQLITE_CANTOPEN ) return 1;

/*  update  */
    rc = sqlite3_prepare_v2( db,
            "UPDATE dateien SET rel_path = REPLACE( SUBSTR( rel_path, 1, "
            "LENGTH( ?1 ) ), ?1, ?2 ) || SUBSTR( rel_path, LENGTH( ?1 ) + 1 );",
            -1, &stmt_update, NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare (update):\n",
                sqlite3_errstr( rc ), NULL );
        sqlite3_close( db );

        return -1;
    }

/*  test  */
    rc = sqlite3_prepare_v2( db,
            "SELECT node_id FROM dateien WHERE rel_path=?1;",
            -1, &stmt_test, NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare (test):\n",
                sqlite3_errstr( rc ), NULL );
        sqlite3_finalize( stmt_update );
        sqlite3_close( db );

        return -1;
    }

/*  begin  */
    rc = sqlite3_prepare_v2( db, "BEGIN;", -1, &stmt_begin, NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare (begin):\n",
                sqlite3_errstr( rc ), NULL );
        sqlite3_finalize( stmt_update );
        sqlite3_finalize( stmt_test );
        sqlite3_close( db );

        return -1;
    }

    rc = sqlite3_prepare_v2( db, "COMMIT;", -1, &stmt_commit, NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare (commit):\n",
                sqlite3_errstr( rc ), NULL );
        sqlite3_finalize( stmt_update );
        sqlite3_finalize( stmt_test );
        sqlite3_finalize( stmt_begin );
        sqlite3_close( db );

        return -1;
    }

    rc = sqlite3_prepare_v2( db, "ROLLBACK;", -1, &stmt_rollback, NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare (rollback):\n",
                sqlite3_errstr( rc ), NULL );
        sqlite3_finalize( stmt_update );
        sqlite3_finalize( stmt_test );
        sqlite3_finalize( stmt_begin );
        sqlite3_finalize( stmt_commit );
        sqlite3_close( db );

        return -1;
    }

    *dbase = g_malloc0( sizeof( DBase ) );

    (*dbase)->path = g_strdup( path );
    (*dbase)->db = db;
    (*dbase)->update_path = stmt_update;
    (*dbase)->test_path = stmt_test;
    (*dbase)->begin = stmt_begin;
    (*dbase)->commit = stmt_commit;
    (*dbase)->rollback = stmt_rollback;

    return 0;
}


static gint
file_manager_create_modify_file( const gchar* path, ModifyFile** modify_file, gchar** errmsg )
{
    gint rc = 0;
    DBase* dbase = NULL;

    rc = file_manager_create_dbase( path, &dbase, errmsg );
    if ( rc == -1 )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf file_manager_create_dbase:\n",
                errmsg, NULL );
        g_free( errmsg );

        return -1;
    }
    else if ( rc == 1 ) return 1;

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

    GtkWidget* fm_treeview = GTK_WIDGET(fm_create_tree_view( ));
    gtk_container_add( GTK_CONTAINER(swindow), fm_treeview );

    g_object_set_data( G_OBJECT(fm_treeview), "clipboard", sojus->clipboard );

    g_signal_connect( fm_window, "delete-event", G_CALLBACK(cb_file_manager_delete_event), sojus );

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

    rc = file_manager_create_modify_file( path, &modify_file, &errmsg );
    if ( rc == -1 )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(fm_treeview) ),
                "Fehler FileManager -\n\nBei Aufruf file_manager_create_modify:\n",
                errmsg, NULL );
        g_free( errmsg );
        file_manager_close( fm_window );

        return;
    }

    g_object_set_data( G_OBJECT(fm_treeview), "modify-file", modify_file );

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
