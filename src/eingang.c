/*
zond (eingang.c) - Akten, Beweisstücke, Unterlagen
Copyright (C) 2020  pelo america

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/


#include <gtk/gtk.h>

#include "misc.h"
#include "eingang.h"
#include "dbase.h"


void
eingang_free( Eingang* eingang )
{
    g_free( eingang->eingangsdatum );
    g_free( eingang->traeger );
    g_free( eingang->transport );
    g_free( eingang->absender );
    g_free( eingang->ort );
    g_free( eingang->absendedatum );
    g_free( eingang->erfassungsdatum );

    g_free( eingang );
}


Eingang*
eingang_new( void )
{
    Eingang* eingang = g_malloc0( sizeof( Eingang ) );

    return eingang;
}


/** -1: Fehler
     0: nicht gefunden
     1: row in Tabelle eingang mit Verweis von rel_path
     2ff.: Grad der Elternverzeichnisse eingetragen         **/
gint
eingang_for_rel_path( DBase* dbase, const gchar* rel_path,
        Eingang** eingang, gchar** errmsg )
{
    gchar* buf = NULL;
    gint zaehler = 1;

    buf = g_strdup( rel_path );

    do
    {
        gint rc = 0;
        gchar* buf_int = NULL;
        gchar* ptr_buf = NULL;

        rc = dbase_get_eingang_for_rel_path( dbase, buf, eingang, errmsg );
        if ( rc == -1 )
        {
            g_free( buf );
            ERROR( "dbase_get_eingang_for_rel_path" )
        }
        else if ( rc == 0 ) //*eingang wurde gesetzt
        {
            g_free( buf );
            return zaehler;
        }

        ptr_buf = g_strrstr( buf, "/" );
        if ( !ptr_buf ) break;

        buf_int = g_strndup( buf, strlen( buf ) - strlen( ptr_buf ) );
        g_free( buf );
        buf = buf_int;

        zaehler++;
    }
    while ( 1 );

    g_free( buf );

    return 0;
}


gint
eingang_fenster( GtkWidget* widget, Eingang* eingang, gboolean sens )
{
    gint ret = 0;

    GtkWidget* dialog = gtk_dialog_new_with_buttons( "Eingang",
            GTK_WINDOW(gtk_widget_get_toplevel(widget)), GTK_DIALOG_MODAL,
            "Ok", GTK_RESPONSE_OK, "Abbrechen", GTK_RESPONSE_CANCEL, NULL );

    GtkWidget* content_area = gtk_dialog_get_content_area( GTK_DIALOG(dialog) );

    GtkWidget* grid = gtk_grid_new( );
    gtk_box_pack_start( GTK_BOX( content_area ), grid, TRUE, TRUE, 0 );

    //Eingangsdatum
    GtkWidget* calendar_eingang = gtk_calendar_new( );
    misc_set_calendar( GTK_CALENDAR(calendar_eingang), eingang->eingangsdatum );

    GtkWidget* entry_transport = gtk_entry_new( );
    if ( eingang->transport ) gtk_entry_set_text( GTK_ENTRY(entry_transport), eingang->transport );

    GtkWidget* entry_traeger = gtk_entry_new( );
    if ( eingang->traeger ) gtk_entry_set_text( GTK_ENTRY(entry_traeger), eingang->traeger );

    GtkWidget* entry_ort = gtk_entry_new( );
    if ( eingang->ort ) gtk_entry_set_text( GTK_ENTRY(entry_ort), eingang->ort );

    GtkWidget* entry_absender = gtk_entry_new( );
    if ( eingang->absender ) gtk_entry_set_text( GTK_ENTRY(entry_absender), eingang->absender );

    GtkWidget* calendar_absendedatum = gtk_calendar_new( );
    misc_set_calendar( GTK_CALENDAR(calendar_absendedatum), eingang->absendedatum);

    GtkWidget* calendar_erfassungsdatum = gtk_calendar_new( );
    misc_set_calendar( GTK_CALENDAR(calendar_erfassungsdatum), eingang->erfassungsdatum );

    if ( !sens )
    {
        gtk_widget_set_sensitive( calendar_eingang, FALSE );
        gtk_widget_set_sensitive( entry_transport, FALSE );
        gtk_widget_set_sensitive( entry_traeger, FALSE );
        gtk_widget_set_sensitive( entry_ort, FALSE );
        gtk_widget_set_sensitive( entry_absender, FALSE );
        gtk_widget_set_sensitive( calendar_absendedatum, FALSE );
        gtk_widget_set_sensitive( calendar_erfassungsdatum, FALSE );
    }

    gtk_grid_attach( GTK_GRID(grid), calendar_eingang, 0, 0, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), entry_traeger, 0, 1, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), entry_transport, 0, 2, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), entry_ort, 0, 3, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), entry_absender, 0, 4, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), calendar_absendedatum, 0, 5, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), calendar_erfassungsdatum, 0, 6, 1, 1 );

    gtk_widget_show_all( content_area );

    g_signal_connect_swapped( calendar_eingang, "day-selected-double-click",
            G_CALLBACK(gtk_widget_grab_focus),
            gtk_dialog_get_widget_for_response( GTK_DIALOG(dialog), GTK_RESPONSE_OK ) );
    g_signal_connect_swapped( entry_traeger, "activate",
            G_CALLBACK(gtk_widget_grab_focus),
            gtk_dialog_get_widget_for_response( GTK_DIALOG(dialog), GTK_RESPONSE_OK ) );
    g_signal_connect_swapped( entry_transport, "activate",
            G_CALLBACK(gtk_widget_grab_focus),
            gtk_dialog_get_widget_for_response( GTK_DIALOG(dialog), GTK_RESPONSE_OK ) );
    g_signal_connect_swapped( entry_ort, "activate",
            G_CALLBACK(gtk_widget_grab_focus),
            gtk_dialog_get_widget_for_response( GTK_DIALOG(dialog), GTK_RESPONSE_OK ) );
    g_signal_connect_swapped( entry_absender, "activate",
            G_CALLBACK(gtk_widget_grab_focus),
            gtk_dialog_get_widget_for_response( GTK_DIALOG(dialog), GTK_RESPONSE_OK ) );
    g_signal_connect_swapped( calendar_absendedatum, "day-selected-double-click",
            G_CALLBACK(gtk_widget_grab_focus),
            gtk_dialog_get_widget_for_response( GTK_DIALOG(dialog), GTK_RESPONSE_OK ) );
    g_signal_connect_swapped( calendar_erfassungsdatum, "day-selected-double-click",
            G_CALLBACK(gtk_widget_grab_focus),
            gtk_dialog_get_widget_for_response( GTK_DIALOG(dialog), GTK_RESPONSE_OK ) );

    ret = gtk_dialog_run( GTK_DIALOG(dialog) );

    //Einlesen, wenn ok und freigeschaltet
    if ( sens && ret == GTK_RESPONSE_OK )
    {
        eingang->eingangsdatum = misc_get_calendar( GTK_CALENDAR(calendar_eingang) );
        eingang->transport = g_strdup( gtk_entry_get_text( GTK_ENTRY(entry_transport) ) );
        eingang->traeger = g_strdup( gtk_entry_get_text( GTK_ENTRY(entry_traeger) ) );
        eingang->ort = g_strdup( gtk_entry_get_text( GTK_ENTRY(entry_ort) ) );
        eingang->absender = g_strdup( gtk_entry_get_text( GTK_ENTRY(entry_absender) ) );
        eingang->absendedatum = misc_get_calendar( GTK_CALENDAR(calendar_absendedatum) );
        eingang->erfassungsdatum = misc_get_calendar( GTK_CALENDAR(calendar_erfassungsdatum) );
    }

    gtk_widget_destroy( dialog );

    return ret;
}


