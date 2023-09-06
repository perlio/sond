#include <gtk/gtk.h>

#include "../../misc.h"
#include "../../sond_treeviewfm.h"

#include "sond_client.h"
#include "sond_client_connection.h"
#include "sond_client_misc.h"

typedef struct _FileManager
{
    GtkWidget* window;
    GtkWidget* sond_treeviewfm;
    gint jahr;
    gint reg_nr;
} FileManager;


void
sond_client_file_manager_free( FileManager* file_manager )
{

    g_free( file_manager );

    return;
}


static void
sond_client_file_manager_set_headerbar( GtkWidget* fm_window, SondTreeviewFM* stvfm, const gchar* path )
{/*
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
*/
    return;
}


static gboolean
sond_client_file_manager_create( SondClient* sond_client, gint jahr, gint regnr,
        GError** error )
{
    FileManager* file_manager = NULL;
    GtkWidget* swindow = NULL;
    gchar* dir = NULL;
    gchar* root = NULL;

    dir = g_strdup_printf( "%2i-%i", jahr, regnr );
    root = g_build_filename( sond_client->seafile_root, dir, NULL );
    g_free( dir );

    file_manager = g_malloc0( sizeof( FileManager ) );
    file_manager->jahr = jahr;
    file_manager->reg_nr = regnr;

    file_manager->window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_default_size( GTK_WINDOW(file_manager->window), 1200, 700 );

    swindow = gtk_scrolled_window_new( NULL, NULL );
    gtk_container_add( GTK_CONTAINER(file_manager->window), swindow );

    file_manager->sond_treeviewfm = g_object_new( SOND_TYPE_TREEVIEWFM, NULL );


    return TRUE;
}


void
sond_client_file_manager_entry_activate( GtkEntry* entry, gpointer data )
{
    gint regnr = 0;
    gint jahr = 0;

    SondClient* sond_client = (SondClient*) data;

    if ( sond_client_misc_regnr_wohlgeformt( gtk_entry_get_text( entry ) ) )
    {
        gint rc = 0;
        GError* error = NULL;

        sond_client_misc_parse_regnr( gtk_entry_get_text( entry ), &regnr, &jahr );

        //prüfen, ob File-Manager zu dieser Akte bereits geöffnet
        for ( gint i = 0; i < sond_client->arr_file_manager->len; i++ )
        {
            FileManager* file_manager =
                    g_ptr_array_index( sond_client->arr_file_manager, i );
            if ( file_manager->jahr == jahr && file_manager->reg_nr == regnr )
            {
                gtk_window_present( GTK_WINDOW(file_manager->window) );
                return;
            }
        }

        //ansonsten: File_manager-Fenster öffnen
        if ( !sond_client_file_manager_create( sond_client, jahr, regnr, &error ) )
        {
            if ( 0 ) //error->code == SOND_CLIENT_ERROR_KEINEAKTE )
            {
                g_clear_error( &error );

                //Abfrage, ob Akte angelegt werden soll
                if ( sond_client_connection_ping( sond_client, &error ) )
                {


                }
                else
                {
                    display_message( sond_client->app_window, "Akte "
                            "ist nicht angelegt -\n\nNeuanlage nicht "
                            "möglich: Verbindung zum Server fehlerhaft\n\n"
                            , error->message, NULL );
                    g_error_free( error );

                    return;

                }
            }
            else
            {
                gchar* text = NULL;

                text = g_strdup_printf( "%i/%i", regnr, jahr );
                display_message( sond_client->app_window, "Fehler - "
                        "Dokumentenmanager zur Akte ", text, "\nkann nicht "
                        "geöffnet werden:\n", error->message, NULL );
                g_free( text );
                g_error_free( error );

                return;
            }
        }
    }
    else //Text
    {
        //Prüfen, ob online
            //Falls ja:
                //auf Server alle passenden Akten suchen, auswählen,
                //Auf Synchro?
                    //Falls ja: dort öffnen
                    //Falls nein: auf seadrive öffnen
            //Falls nein: Auswahl in syncho suchen u ggf. öffnen
    }

    return;
}
