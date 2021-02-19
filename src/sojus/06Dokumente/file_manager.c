#include <gtk/gtk.h>
#include <sqlite3.h>

#include "../../treeview.h"
#include "../global_types_sojus.h"

#include "../00misc/auswahl.h"
#include "../../misc.h"
#include "../02Akten/akten.h"

#include "../../fm.h"
#include "../../dbase.h"
#include "../../eingang.h"


static gboolean
cb_file_manager_delete_event( GtkWidget* window, GdkEvent* event, gpointer data )
{
    gchar* regnr_string = NULL;
    GtkWidget* fm_treeview = NULL;
    ModifyFile* modify_file = NULL;

    Sojus* sojus = (Sojus*) data;

    regnr_string = g_object_get_data( G_OBJECT(window), "regnr-string" );
    g_object_set_data( G_OBJECT(sojus->app_window), regnr_string, NULL );
    g_free( regnr_string );

    fm_treeview = g_object_get_data( G_OBJECT(window), "fm-treeview" );

    fm_unset_root( GTK_TREE_VIEW(fm_treeview) );

    dbase_destroy( (DBase*) g_object_get_data( G_OBJECT(fm_treeview), "dbase" ) );
    modify_file = g_object_get_data( G_OBJECT(fm_treeview), "modify-file" );
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


static void
file_manager_cb_dir_einfuegen_p( GtkWidget* item, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    GtkWidget* fm_window = (GtkWidget*) data;

    GtkWidget* fm_treeview = g_object_get_data( G_OBJECT(fm_window), "fm-treeview" );

    rc = fm_create_dir( GTK_TREE_VIEW(fm_treeview), FALSE, &errmsg );
    if ( rc )
    {
        display_message( fm_window, "Fehler bei Erzeugen neues Verzeichnis -\n\n"
                "Bei Aufruf fm_create_dir:\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


static void
file_manager_cb_dir_einfuegen_up( GtkWidget* item, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    GtkWidget* fm_window = (GtkWidget*) data;

    GtkWidget* fm_treeview = g_object_get_data( G_OBJECT(fm_window), "fm-treeview" );

    rc = fm_create_dir( GTK_TREE_VIEW(fm_treeview), TRUE, &errmsg );
    if ( rc )
    {
        display_message( fm_window, "Fehler bei Erzeugen neues Verzeichnis -\n\n"
                "Bei Aufruf fm_create_dir:\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


static void
file_manager_cb_kopieren( GtkWidget* item, gpointer data )
{
    GtkWidget* fm_window = (GtkWidget*) data;

    GtkWidget* fm_treeview = g_object_get_data( G_OBJECT(fm_window), "fm-treeview" );
    Clipboard* clipboard = g_object_get_data( G_OBJECT(fm_treeview), "clipboard" );

    treeview_copy_or_cut_selection( GTK_TREE_VIEW(fm_treeview), clipboard, FALSE );

    return;
}


static void
file_manager_cb_ausschneiden( GtkWidget* item, gpointer data )
{
    GtkWidget* fm_window = (GtkWidget*) data;

    GtkWidget* fm_treeview = g_object_get_data( G_OBJECT(fm_window), "fm-treeview" );
    Clipboard* clipboard = g_object_get_data( G_OBJECT(fm_treeview), "clipboard" );

    treeview_copy_or_cut_selection( GTK_TREE_VIEW(fm_treeview), clipboard, TRUE );

    return;
}


static void
file_manager_cb_einfuegen_p( GtkWidget* item, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    GtkWidget* fm_window = (GtkWidget*) data;

    GtkWidget* fm_treeview = g_object_get_data( G_OBJECT(fm_window), "fm-treeview" );
    Clipboard* clipboard = g_object_get_data( G_OBJECT(fm_treeview), "clipboard" );

    rc = fm_paste_selection( GTK_TREE_VIEW(fm_treeview), clipboard->tree_view,
            clipboard->arr_ref, clipboard->ausschneiden, FALSE, &errmsg );
    if ( rc )
    {
        display_message( fm_window, "Fehler bei Einfügen -\n\nBei Aufruf fm_"
                "paste_selection:\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


static void
file_manager_cb_einfuegen_up( GtkWidget* item, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    GtkWidget* fm_window = (GtkWidget*) data;

    GtkWidget* fm_treeview = g_object_get_data( G_OBJECT(fm_window), "fm-treeview" );
    Clipboard* clipboard = g_object_get_data( G_OBJECT(fm_treeview), "clipboard" );

    rc = fm_paste_selection( GTK_TREE_VIEW(fm_treeview), clipboard->tree_view,
            clipboard->arr_ref, clipboard->ausschneiden, TRUE, &errmsg );
    if ( rc )
    {
        display_message( fm_window, "Fehler bei Einfügen -\n\nBei Aufruf fm_"
                "paste_selection:\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


static void
file_manager_cb_loeschen( GtkWidget* item, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    GtkWidget* fm_window = (GtkWidget*) data;

    GtkWidget* fm_treeview = g_object_get_data( G_OBJECT(fm_window), "fm-treeview" );

    GPtrArray* refs = treeview_selection_get_refs( GTK_TREE_VIEW(fm_treeview) );
    if ( !refs ) return;

    rc = treeview_selection_foreach( GTK_TREE_VIEW(fm_treeview), refs,
            fm_foreach_loeschen, NULL, &errmsg );
    g_ptr_array_unref( refs );
    if ( rc == -1 )
    {
        display_message( fm_window, "Fehler bei Löschen -\n\nBei Aufruf "
                "treeview_selection_foreach:\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


static void
file_manager_cb_eingang( GtkWidget* item, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;
    Eingang* eingang = NULL;

    GtkWidget* fm_window = (GtkWidget*) data;

    GtkWidget* fm_treeview = g_object_get_data( G_OBJECT(fm_window), "fm-treeview" );

    //selektierte Dateien holen
    GPtrArray* refs = treeview_selection_get_refs( GTK_TREE_VIEW(fm_treeview) );
    if ( !refs ) return;

    rc = treeview_selection_foreach( GTK_TREE_VIEW(fm_treeview), refs,
            eingang_set, &eingang, &errmsg );
    g_ptr_array_unref( refs );
    eingang_free( eingang );
    if ( rc == -1 )
    {
        display_message( fm_window, "Fehler bei Eingang -\n\nBei Aufruf "
                "treeview_selection_foreach:\n", errmsg, NULL );
        g_free( errmsg );
    }

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

    GtkWidget* fm_treeview = (GtkWidget*) data;
    const gchar* root = g_object_get_data( G_OBJECT(fm_treeview), "root" );
    DBase* dbase = g_object_get_data( G_OBJECT(fm_treeview), "dbase" );

    rel_path = fm_get_rel_path_from_file( root, file );

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

    GtkWidget* fm_treeview = (GtkWidget*) data;
    const gchar* root = g_object_get_data( G_OBJECT(fm_treeview), "root" );
    DBase* dbase = g_object_get_data( G_OBJECT(fm_treeview), "dbase" );

    if ( !file_manager_same_project( root, dest ) ) //Verschieben in anderes Projekt
    {
        gint rc = 0;

        rc = file_manager_test( src, data, errmsg ); //Datei in Ursprungsprojekt angebunden?
        if ( rc == -1 ) ERROR( "file_manager_test" )
        else if ( rc == 1 ) return 1; //Wenn ja: überspringen
    }

    rc = dbase_begin( dbase, errmsg );
    if ( rc ) ERROR( "dbase_begin" )

    gchar* rel_path_source = fm_get_rel_path_from_file( root, src );
    gchar* rel_path_dest = fm_get_rel_path_from_file( root, dest );

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

    GtkWidget* fm_treeview = (GtkWidget*) data;
    DBase* dbase = g_object_get_data( G_OBJECT(fm_treeview), "dbase" );

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
file_manager_open_dbase( GtkWidget* fm_treeview, DBase** dbase,
        gchar** errmsg )
{
    gint rc = 0;
    gchar* db_name = NULL;

    db_name = g_strconcat( g_object_get_data( G_OBJECT(fm_treeview), "root" ),
            "/doc_db.ZND", NULL );

    rc = dbase_create_with_stmts( db_name, dbase, FALSE, FALSE, errmsg );
    g_free( db_name );
    if ( rc ) // da FALSE, kann nur -1 oder 0 zurückgegeben werden
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf file_manager_create_dbase:\n",
                *errmsg, NULL );

        return -1;
    }

    return 0;
}


static void
file_manager_set_headerbar( GtkWidget* fm_window, const gchar* path )
{
    GtkWidget* headerbar = gtk_header_bar_new( );
    GtkWidget* menu_fm = gtk_menu_new( );
    GtkWidget* menu_button = gtk_menu_button_new( );

    GtkAccelGroup* accel_group = gtk_accel_group_new( );
    gtk_window_add_accel_group( GTK_WINDOW(fm_window), accel_group );

    GtkWidget* dir_einfuegen = gtk_menu_item_new_with_label( "Neues Verzeichnis" );
    GtkWidget* dir_einfuegen_menu = gtk_menu_new( );
    GtkWidget* dir_einfuegen_p = gtk_menu_item_new_with_label( "Gleiche Ebene" );
    GtkWidget* dir_einfuegen_up = gtk_menu_item_new_with_label( "Unterpunkt" );
    gtk_menu_shell_append( GTK_MENU_SHELL(dir_einfuegen_menu), dir_einfuegen_p );
    gtk_menu_shell_append( GTK_MENU_SHELL(dir_einfuegen_menu), dir_einfuegen_up );
    gtk_menu_item_set_submenu( GTK_MENU_ITEM(dir_einfuegen), dir_einfuegen_menu );

    GtkWidget* kopieren = gtk_menu_item_new_with_label( "Kopieren" );
    GtkWidget* ausschneiden = gtk_menu_item_new_with_label( "Ausschneiden" );

    GtkWidget* einfuegen = gtk_menu_item_new_with_label( "Einfügen" );
    GtkWidget* einfuegen_menu = gtk_menu_new( );
    GtkWidget* einfuegen_p = gtk_menu_item_new_with_label( "Gleiche Ebene" );
    GtkWidget* einfuegen_up = gtk_menu_item_new_with_label( "Unterpunkt" );
    gtk_menu_shell_append( GTK_MENU_SHELL(einfuegen_menu), einfuegen_p );
    gtk_menu_shell_append( GTK_MENU_SHELL(einfuegen_menu), einfuegen_up );
    gtk_menu_item_set_submenu( GTK_MENU_ITEM(einfuegen), einfuegen_menu );

    GtkWidget* loeschen = gtk_menu_item_new_with_label( "Löschen" );

    GtkWidget* eingang = gtk_menu_item_new_with_label( "Eingang" );

    g_signal_connect( dir_einfuegen_p, "activate", G_CALLBACK(file_manager_cb_dir_einfuegen_p),
            fm_window );
    g_signal_connect( dir_einfuegen_up, "activate", G_CALLBACK(file_manager_cb_dir_einfuegen_up),
            fm_window );
    g_signal_connect( kopieren, "activate", G_CALLBACK(file_manager_cb_kopieren),
            fm_window );
    g_signal_connect( ausschneiden, "activate", G_CALLBACK(file_manager_cb_ausschneiden),
            fm_window );
    g_signal_connect( einfuegen_p, "activate", G_CALLBACK(file_manager_cb_einfuegen_p),
            fm_window );
    g_signal_connect( einfuegen_up, "activate", G_CALLBACK(file_manager_cb_einfuegen_up),
            fm_window );
    g_signal_connect( loeschen, "activate", G_CALLBACK(file_manager_cb_loeschen),
            fm_window );
    g_signal_connect( eingang, "activate", G_CALLBACK(file_manager_cb_eingang),
            fm_window );

    gtk_widget_add_accelerator( dir_einfuegen_p, "activate", accel_group,
            GDK_KEY_p, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator( dir_einfuegen_up, "activate", accel_group,
            GDK_KEY_p, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator( kopieren, "activate", accel_group,
            GDK_KEY_c, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator( ausschneiden, "activate", accel_group,
            GDK_KEY_x, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator( einfuegen_p, "activate", accel_group,
            GDK_KEY_v, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator( einfuegen_up, "activate", accel_group,
            GDK_KEY_v, GDK_CONTROL_MASK | GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator( loeschen, "activate", accel_group,
            GDK_KEY_Delete, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator( eingang, "activate", accel_group,
            GDK_KEY_e, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

    gtk_menu_shell_append( GTK_MENU_SHELL(menu_fm), dir_einfuegen );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_fm), kopieren );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_fm), ausschneiden );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_fm), einfuegen );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_fm), loeschen );
    gtk_menu_shell_append( GTK_MENU_SHELL(menu_fm), eingang );

    gtk_widget_show_all( menu_fm );

    gtk_menu_button_set_popup( GTK_MENU_BUTTON(menu_button), menu_fm );

    gtk_header_bar_pack_start( GTK_HEADER_BAR(headerbar), menu_button );
    gtk_header_bar_set_title( GTK_HEADER_BAR(headerbar), path );
    gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(headerbar), ":minimize,maximize,close");
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);

    gtk_window_set_titlebar( GTK_WINDOW(fm_window), headerbar );

    return;
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
    DBase* dbase = NULL;

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
    path = add_string( path, g_strdup_printf( " %i-%i", sojus->regnr_akt,
            sojus->jahr_akt % 100 ) );

    modify_file = g_malloc0( sizeof( ModifyFile ) );

    GtkWidget* fm_treeview = fm_create_tree_view( sojus->clipboard, modify_file );
    gtk_container_add( GTK_CONTAINER(swindow), fm_treeview );

    g_object_set_data( G_OBJECT(fm_window), "fm-treeview", fm_treeview );

    rc = fm_set_root( GTK_TREE_VIEW(fm_treeview), path, &errmsg );
    file_manager_set_headerbar( fm_window, path );
    g_free( path );
    if ( rc )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(fm_treeview) ),
                "Fehler -\n\nBei Aufruf fm_set_root:\n", errmsg, NULL );
        g_free( errmsg );
        g_object_set_data( G_OBJECT(sojus->app_window), regnr_string, NULL );
        g_free( regnr_string );
        g_free( modify_file );
        gtk_widget_destroy( fm_window );

        return;
    }

    modify_file->before_move = file_manager_before_move;
    modify_file->after_move = file_manager_after_move;
    modify_file->test = file_manager_test;
    modify_file->data = (gpointer) fm_treeview;

    rc = file_manager_open_dbase( fm_treeview, &dbase, &errmsg );
    if ( rc )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(fm_window) ),
                "Fehler FileManager -\n\nBei Aufruf file_manager_create_modify:\n",
                errmsg, NULL );
        g_free( errmsg );
        g_object_set_data( G_OBJECT(sojus->app_window), regnr_string, NULL );
        g_free( regnr_string );
        fm_unset_root( GTK_TREE_VIEW(fm_treeview) );
        g_free( modify_file );
        gtk_widget_destroy( fm_window );

        return;
    }

    fm_add_column_eingang( GTK_TREE_VIEW(fm_treeview), dbase, &errmsg );

    g_object_set_data( G_OBJECT(fm_treeview), "dbase", dbase );

    gtk_widget_show_all( fm_window );

    return;
}
