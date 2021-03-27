#include <gtk/gtk.h>
#include <sqlite3.h>

#include "../../sond_treeviewfm.h"

#include "../../dbase.h"
#include "../../eingang.h"

#include "../global_types_sojus.h"

#include "../00misc/auswahl.h"
#include "../../misc.h"
#include "../02Akten/akten.h"

#include "file_manager.h"


typedef struct _Open_FM
{
    GtkWidget* window;
    SondTreeviewFM* stvfm;
    gint regnr;
    gint jahr;
}OpenFM;


static gboolean
cb_file_manager_delete_event( GtkWidget* window, GdkEvent* event, gpointer data )
{
    Sojus* sojus = (Sojus*) data;

    for ( gint i = 0; i < sojus->arr_open_fm->len; i++ )
    {
        OpenFM* open_fm = g_ptr_array_index( sojus->arr_open_fm, i );
        if ( open_fm->window == window )
        {
            dbase_destroy( sond_treeviewfm_get_dbase( open_fm->stvfm ) );

            g_ptr_array_remove_index_fast( sojus->arr_open_fm, i );
            break;
        }
    }

    return FALSE;
}


static void
file_manager_cb_dir_einfuegen_p( GtkWidget* item, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    SondTreeviewFM* stvfm = (SondTreeviewFM*) data;

    rc = sond_treeviewfm_create_dir( stvfm, FALSE, &errmsg );
    if ( rc )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(stvfm) ),
                "Fehler bei Erzeugen neues Verzeichnis -\n\n"
                "Bei Aufruf sond_treeviewfm_create_dir:\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


static void
file_manager_cb_dir_einfuegen_up( GtkWidget* item, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    SondTreeviewFM* stvfm = (SondTreeviewFM*) data;

    rc = sond_treeviewfm_create_dir( stvfm, TRUE, &errmsg );
    if ( rc )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(stvfm) ),
                "Fehler bei Erzeugen neues Verzeichnis -\n\n"
                "Bei Aufruf sond_treeviewfm_create_dir:\n", errmsg, NULL );
        g_free( errmsg );
    }

    return;
}


static void
file_manager_cb_kopieren( GtkWidget* item, gpointer data )
{
    SondTreeviewFM* stvfm = (SondTreeviewFM*) data;

    sond_treeview_copy_or_cut_selection( SOND_TREEVIEW(stvfm), FALSE );

    return;
}


static void
file_manager_cb_ausschneiden( GtkWidget* item, gpointer data )
{
    SondTreeviewFM* stvfm = (SondTreeviewFM*) data;

    sond_treeview_copy_or_cut_selection( SOND_TREEVIEW(stvfm), TRUE );

    return;
}


static void
file_manager_cb_einfuegen_p( GtkWidget* item, gpointer data )
{
    gint rc = 0;
    gchar* errmsg = NULL;

    SondTreeviewFM* stvfm = (SondTreeviewFM*) data;

    rc = sond_treeviewfm_paste_clipboard( stvfm, FALSE, &errmsg );
    if ( rc )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(stvfm) ), "Fehler bei Einfügen -\n\nBei Aufruf fm_"
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

    SondTreeviewFM* stvfm = (SondTreeviewFM*) data;

    rc = sond_treeviewfm_paste_clipboard( stvfm, TRUE, &errmsg );
    if ( rc )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(stvfm) ), "Fehler bei Einfügen -\n\nBei Aufruf fm_"
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

    SondTreeviewFM* stvfm = (SondTreeviewFM*) data;

    rc = sond_treeviewfm_selection_loeschen( stvfm, &errmsg );
    if ( rc == -1 )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(stvfm) ), "Fehler bei Löschen -\n\nBei Aufruf "
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

    SondTreeviewFM* stvfm = (SondTreeviewFM*) data;

    rc = eingang_set( stvfm, &errmsg );
    if ( rc == -1 )
    {
        display_message( gtk_widget_get_toplevel( GTK_WIDGET(stvfm) ), "Fehler bei Eingang -\n\nBei Aufruf "
                "fm_set_eingang:\n", errmsg, NULL );
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
file_manager_test( SondTreeviewFM* stvfm, const GFile* file, gpointer data, gchar** errmsg )
{
    gint rc = 0;
    gchar* rel_path = NULL;

    DBase* dbase = (DBase*) data;

    rel_path = get_rel_path_from_file( sond_treeviewfm_get_root( stvfm ), file );

    rc = dbase_test_path( dbase, rel_path, errmsg );
    g_free( rel_path );
    if ( rc == -1 ) ERROR_SOND( "dbase_test_path" )
    else if ( rc == 1 ) return 1; //Datei existiert

    return 0;
}


static gint
file_manager_before_move( SondTreeviewFM*stvfm, const GFile* src, const GFile* dest, gpointer data,
        gchar** errmsg )
{
    gint rc = 0;

    DBase* dbase = (DBase*) data;

    if ( !file_manager_same_project( sond_treeviewfm_get_root( stvfm ), dest ) ) //Verschieben in anderes Projekt
    {
        gint rc = 0;

        rc = file_manager_test( stvfm, src, data, errmsg ); //Datei in Ursprungsprojekt angebunden?
        if ( rc == -1 ) ERROR_SOND( "file_manager_test" )
        else if ( rc == 1 ) return 1; //Wenn ja: überspringen
    }

    rc = dbase_begin( dbase, errmsg );
    if ( rc ) ERROR_SOND( "dbase_begin" )

    gchar* rel_path_source = get_rel_path_from_file( sond_treeviewfm_get_root( stvfm ), src );
    gchar* rel_path_dest = get_rel_path_from_file( sond_treeviewfm_get_root( stvfm ), dest );

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
file_manager_after_move( SondTreeviewFM* stvfm, gint rc_update, gpointer data, gchar** errmsg )
{
    gint rc = 0;

    DBase* dbase = (DBase*) data;

    if ( rc_update == 1 )
    {
        rc = dbase_rollback( dbase, errmsg );
        if ( rc ) ERROR_SOND( "dbase_rollback" )
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
file_manager_open_dbase( SondTreeviewFM* stvfm, DBase** dbase,
        gchar** errmsg )
{
    gint rc = 0;
    gchar* db_name = NULL;

    db_name = g_strconcat( sond_treeviewfm_get_root( stvfm ), "/doc_db.ZND", NULL );

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
file_manager_set_headerbar( GtkWidget* fm_window, SondTreeviewFM* stvfm, const gchar* path )
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
            stvfm );
    g_signal_connect( dir_einfuegen_up, "activate", G_CALLBACK(file_manager_cb_dir_einfuegen_up),
            stvfm );
    g_signal_connect( kopieren, "activate", G_CALLBACK(file_manager_cb_kopieren),
            stvfm );
    g_signal_connect( ausschneiden, "activate", G_CALLBACK(file_manager_cb_ausschneiden),
            stvfm );
    g_signal_connect( einfuegen_p, "activate", G_CALLBACK(file_manager_cb_einfuegen_p),
            stvfm );
    g_signal_connect( einfuegen_up, "activate", G_CALLBACK(file_manager_cb_einfuegen_up),
            stvfm );
    g_signal_connect( loeschen, "activate", G_CALLBACK(file_manager_cb_loeschen),
            stvfm );
    g_signal_connect( eingang, "activate", G_CALLBACK(file_manager_cb_eingang),
            stvfm );

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


static void
file_manager_set_window( Sojus* sojus, GtkWidget* window, SondTreeviewFM* stvfm )
{
    OpenFM* open_fm = g_malloc0( sizeof( OpenFM ) );

    open_fm->window = window;
    open_fm->stvfm = stvfm;
    open_fm->regnr = sojus->regnr_akt;
    open_fm->jahr = sojus->jahr_akt;

    g_ptr_array_add( sojus->arr_open_fm, open_fm );

    return;
}


static GtkWidget*
file_manager_get_window( Sojus* sojus )
{
    for ( gint i = 0; i < sojus->arr_open_fm->len; i++ )
    {
        OpenFM* open_fm = g_ptr_array_index( sojus->arr_open_fm, i );

        if ( open_fm->regnr == sojus->regnr_akt && open_fm->jahr == sojus->jahr_akt )
                return open_fm->window;
    }

    return NULL;
}


void
file_manager_entry_activate( GtkWidget* entry, gpointer data )
{
    GtkWidget* fm_window = NULL;
    Akte* akte = NULL;
    gchar* dokument_dir = NULL;
    gchar* path = NULL;
    gint rc = 0;
    gchar* errmsg = NULL;
    DBase* dbase = NULL;

    Sojus* sojus = (Sojus*) data;

    if ( !auswahl_get_regnr_akt( sojus, GTK_ENTRY(entry) ) ) return;

    if ( (fm_window = file_manager_get_window( sojus )) )
    {
        gtk_window_present( GTK_WINDOW(fm_window) );
        return;
    }

    akte = akte_oeffnen( sojus, sojus->regnr_akt, sojus->jahr_akt );
    if ( !akte ) return;

    fm_window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_default_size( GTK_WINDOW(fm_window), 1200, 700 );

    GtkWidget* swindow = gtk_scrolled_window_new( NULL, NULL );
    gtk_container_add( GTK_CONTAINER(fm_window), swindow );

    dokument_dir = g_settings_get_string( sojus->settings, "dokument-dir" );
    dokument_dir = add_string( dokument_dir, g_strdup( "/" ) );
    path = g_strdelimit( g_strdup( akte->bezeichnung ), "/\\", '-' );
    akte_free( akte );
    path = add_string( dokument_dir, path );
    path = add_string( path, g_strdup_printf( " %i-%i", sojus->regnr_akt,
            sojus->jahr_akt % 100 ) );

    SondTreeviewFM* stvfm = sond_treeviewfm_new( sojus->clipboard );

    file_manager_set_window( sojus, fm_window, stvfm );

    gtk_container_add( GTK_CONTAINER(swindow), GTK_WIDGET(stvfm) );

    g_signal_connect( fm_window, "delete-event", G_CALLBACK(cb_file_manager_delete_event), sojus );

    rc = sond_treeviewfm_set_root( stvfm, path, &errmsg );
    file_manager_set_headerbar( fm_window, stvfm, path );
    g_free( path );
    if ( rc )
    {
        gboolean ret = FALSE;

        display_message( gtk_widget_get_toplevel( GTK_WIDGET(fm_window) ),
                "Fehler -\n\nBei Aufruf sond_treeviewfm_set_root:\n", errmsg, NULL );
        g_free( errmsg );

        g_signal_emit_by_name( fm_window, "delete-event", &ret );

        return;
    }

    rc = file_manager_open_dbase( stvfm, &dbase, &errmsg );
    if ( rc )
    {
        gboolean ret = FALSE;

        display_message( gtk_widget_get_toplevel( GTK_WIDGET(fm_window) ),
                "Fehler FileManager -\n\nBei Aufruf file_manager_create_modify:\n",
                errmsg, NULL );
        g_free( errmsg );

        g_signal_emit_by_name( fm_window, "delete-event", &ret );

        return;
    }

    sond_treeviewfm_set_funcs( stvfm, file_manager_before_move,
            file_manager_after_move, file_manager_test, dbase );

    gtk_widget_show_all( fm_window );

    return;
}
