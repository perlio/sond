#include <gtk/gtk.h>

#include "../global_types_sojus.h"

#include "db.h"


void
einstellungen( GtkWidget* button, gpointer data )
{
    Sojus* sojus = (Sojus*) data;

    GtkWidget* window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_modal( GTK_WINDOW(window), TRUE );
    gtk_window_set_title( GTK_WINDOW(window), "Einstellungen" );

    GtkWidget* grid = gtk_grid_new( );
    gtk_container_add( GTK_CONTAINER(window), grid );

    GtkWidget* bu_sql = gtk_button_new_with_label( "SQL-Server" );
    GtkWidget* bu_db = gtk_button_new_with_label( "Datenbank" );

    gtk_grid_attach( GTK_GRID(grid), bu_sql, 0, 3, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), bu_db, 0, 4, 1, 1 );

    g_signal_connect_swapped( bu_sql, "clicked", G_CALLBACK(db_get_connection), sojus );

    gtk_widget_show_all( window );

    return;
}
/*
    GtkWidget* bu_db_con = gtk_button_new_with_mnemonic( "_Verbindung zu "
            "SQL-Server" );
    GtkWidget* bu_db_waehlen = gtk_button_new_with_mnemonic( "Daten_bank wählen" );
    GtkWidget* bu_db_erstellen = gtk_button_new_with_mnemonic( "Datenban_k erstellen" );
    GtkWidget* bu_sachbearbeiterverwaltung = gtk_button_new_with_mnemonic(
            "Sachbearbeiter_verwaltung" );
    sojus->widgets.Einstellungen.bu_dokument_dir = gtk_button_new_with_mnemonic(
            "Dokumentenver_zeichnis" );

*/
