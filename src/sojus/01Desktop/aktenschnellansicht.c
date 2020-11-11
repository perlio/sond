#include "../global_types.h"

#include "../00misc/auswahl.h"
#include "../../misc.h"

#include "../../fm.h"

#include <gtk/gtk.h>


#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#endif // _WIN32


static void
cb_bu_doc_erzeugen( GtkButton* button, gpointer data )
{
    Sojus* sojus = (Sojus*) data;

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
        display_message( sojus->app_window, "Fehler in doc erzeugen: assoc", NULL );
        return;
    }
#else
    //für Linux etc: Pfad von soffice suchen
#endif // _WIN32
    gboolean ret = FALSE;

    gchar* argv[6] = { NULL };
    argv[0] = soffice_exe;
    argv[1] = "C:/Users/pkrieger/AppData/Roaming/LibreOffice/4/user/template/vorlagen/Briefkopf.ott";
    argv[2] = "macro:///Standard.Module.Dokument_erzeugen(2,2020,3,0,0)";

    ret = g_spawn_async( NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL, NULL,
            &error );
    if ( !ret )
    {
        display_message( sojus->app_window, "Fehler bei Dokument erzeugen",
                error->message, NULL );
        g_error_free( error );
    }

    return;
}


static void
cb_entry_regnr_activate( GtkEntry* entry, gpointer data )
{
    Sojus* sojus = (Sojus*) data;

    if ( !auswahl_get_regnr_akt( sojus, entry ) ) return;

    //path-data in tree_view_fs schreiben

    //Verzeichnis in fs_tree einlesen

    //bu_dokument_erzeugen anschalten

}


GtkWidget*
aktenschnellansicht_create_window( Sojus* sojus )
{
    GtkWidget* grid_in_frame = gtk_grid_new( );

    //entry regnr
    sojus->widgets.AppWindow.AktenSchnellansicht.entry_regnr = gtk_entry_new( );
    gtk_grid_attach( GTK_GRID(grid_in_frame), sojus->widgets.AppWindow.
            AktenSchnellansicht.entry_regnr, 0, 0, 3, 1 );

    //button dokument erzeugen
    sojus->widgets.AppWindow.AktenSchnellansicht.button_doc_erzeugen =
            gtk_button_new_with_label( "Dokument\nerzeugen" );
    gtk_grid_attach( GTK_GRID(grid_in_frame),
            sojus->widgets.AppWindow.AktenSchnellansicht.button_doc_erzeugen,
            0, 1, 1, 1 );

    //tree_view dokument_dir
    GtkWidget* swindow = gtk_scrolled_window_new( NULL, NULL );

    g_signal_connect( sojus->widgets.AppWindow.AktenSchnellansicht.entry_regnr,
            "activate", G_CALLBACK(cb_entry_regnr_activate), sojus );
    g_signal_connect( sojus->widgets.AppWindow.AktenSchnellansicht.button_doc_erzeugen,
            "clicked", G_CALLBACK(cb_bu_doc_erzeugen), sojus );

    sojus->widgets.AppWindow.AktenSchnellansicht.treeview_fm =
            GTK_WIDGET(fm_create_tree_view( sojus->app_window, G_CALLBACK(cb_fm_row_text_edited) ));
    gtk_container_add( GTK_CONTAINER(swindow),
            sojus->widgets.AppWindow.AktenSchnellansicht.treeview_fm );

    gtk_grid_attach( GTK_GRID(grid_in_frame), swindow, 0, 2, 10, 10 );
    gtk_grid_set_row_homogeneous( GTK_GRID(grid_in_frame), TRUE );

    return grid_in_frame;
}
