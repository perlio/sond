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
#include "fm.h"


void
eingang_free( Eingang* eingang )
{
    if ( !eingang ) return;

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
eingang_for_rel_path( DBase* dbase, const gchar* rel_path, gint* ID,
        Eingang** eingang, gint* ID_eingang_rel_path, gchar** errmsg )
{
    gchar* buf = NULL;
    gint zaehler = 1;

    buf = g_strdup( rel_path );

    do
    {
        gint rc = 0;
        gchar* buf_int = NULL;
        gchar* ptr_buf = NULL;

        rc = dbase_get_eingang_for_rel_path( dbase, buf, ID, eingang,
                ID_eingang_rel_path, errmsg );
        if ( rc == -1 )
        {
            g_free( buf );
            ERROR( "dbase_get_eingang_for_rel_path" )
        }
        else if ( rc == 0 ) //*eingang wurde gesetzt
        {
            g_free( buf );
            if ( zaehler > 1 && ID_eingang_rel_path ) *ID_eingang_rel_path = 0; //buf != rel_path

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


static gint
eingang_fenster( GtkWidget* widget, Eingang* eingang, const gboolean sens,
        const gchar* title, const gchar* secondary )
{
    gint ret = 0;

    GtkWidget* dialog = gtk_dialog_new_with_buttons( "Eingang",
            GTK_WINDOW(gtk_widget_get_toplevel(widget)), GTK_DIALOG_MODAL,
            "Ok", GTK_RESPONSE_OK, "Nein", GTK_RESPONSE_NO, "Abbrechen",
            GTK_RESPONSE_CANCEL, NULL );

    GtkWidget* headerbar = gtk_header_bar_new( );
    gtk_header_bar_set_title( GTK_HEADER_BAR(headerbar), title );
    if ( secondary ) gtk_header_bar_set_subtitle( GTK_HEADER_BAR(headerbar), secondary );

    gtk_window_set_titlebar( GTK_WINDOW(dialog), headerbar );

    GtkWidget* content_area = gtk_dialog_get_content_area( GTK_DIALOG(dialog) );

    GtkWidget* grid = gtk_grid_new( );
    gtk_box_pack_start( GTK_BOX( content_area ), grid, TRUE, TRUE, 0 );

    //Eingangsdatum
    GtkWidget* frame_eingang = gtk_frame_new( "Eingangsdatum" );
    GtkWidget* calendar_eingang = gtk_calendar_new( );
    misc_set_calendar( GTK_CALENDAR(calendar_eingang), eingang->eingangsdatum );
    gtk_container_add( GTK_CONTAINER(frame_eingang), calendar_eingang );

    GtkWidget* frame_transport = gtk_frame_new( "Transportweg" );
    GtkWidget* entry_transport = gtk_entry_new( );
    if ( eingang->transport ) gtk_entry_set_text( GTK_ENTRY(entry_transport), eingang->transport );
    gtk_container_add( GTK_CONTAINER(frame_transport), entry_transport);

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

    gtk_grid_attach( GTK_GRID(grid), frame_eingang, 0, 0, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), frame_transport, 0, 1, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), entry_traeger, 0, 2, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), entry_ort, 0, 3, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), entry_absender, 0, 4, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), calendar_absendedatum, 0, 5, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), calendar_erfassungsdatum, 0, 6, 1, 1 );

    gtk_widget_show_all( content_area );
    gtk_widget_grab_focus( calendar_eingang );

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


static gint
eingang_insert_or_update( GtkTreeView* fm_treeview, DBase* dbase,
        const gchar* rel_path, Eingang** eingang_loop, gchar** errmsg )
{
    gint rc = 0;
    gint ret = 0;
    gint ID = 0;
    gint ID_eingang_rel_path = 0;
    Eingang* eingang = NULL;

    ret = eingang_for_rel_path( dbase, rel_path, &ID, &eingang, &ID_eingang_rel_path,
            errmsg );
    if ( ret == -1 ) ERROR( "eingang_for_rel_path" )
    else if ( ret > 0 )
    {
        gint rc = 0;
        gchar* title = NULL;

        title = g_strconcat( "Zur Datei ", rel_path, " wurden bereits Eingangsdaten gespeichert", NULL );

        rc = eingang_fenster( GTK_WIDGET(fm_treeview), eingang, FALSE, title, "Überschreiben?" );
        g_free( title );

        eingang_free( eingang );

        if ( rc == GTK_RESPONSE_NO ) return 0;
        else if ( rc != GTK_RESPONSE_OK ) return 1; //abgebrochen
    }

    if ( !(*eingang_loop) )
    {
        gint rc = 0;

        //Eingang abfragen
        *eingang_loop = eingang_new( );

        rc = eingang_fenster( GTK_WIDGET(fm_treeview), *eingang_loop, TRUE,
                "Bitte Eingangsdaten eingeben", NULL );
        if ( rc != GTK_RESPONSE_OK ) return 1;
    }

    rc = dbase_begin( dbase, errmsg );
    if ( rc ) ERROR_ROLLBACK( dbase, "dbase_begin" )

    //hier Abfrage, was bei drohender Kollision gemacht werden soll
    if ( ret == 1 ) //zu rel_path selbst ist Eingang gespeichert
    {
        gint rc = 0;
        //Abfragen, ob Eingang von mehreren eingang_rel_pathes
        //referenziert wird (dbase_get_ID_eingang_rel_path( dbase, ID, &id_eingang_rel_path )

        if ( rc == 1 )
        {
            gint rc = 0;

            rc = dbase_update_eingang( dbase, ID, *eingang_loop, errmsg );
            if ( rc ) ERROR_ROLLBACK( dbase, "dbase_update_eingang" );
        }
        else //müßte > 1 sein...
        {//wenn mehrere:
            gint ID_new = 0;

            ID_new = dbase_insert_eingang( dbase, *eingang_loop, errmsg );
            if ( ID_new == -1 ) ERROR_ROLLBACK( dbase, "dbase_insert_eingang" )

            rc = dbase_update_eingang_rel_path( dbase, ID_eingang_rel_path, ID_new, rel_path, errmsg );
            if ( rc ) ERROR_ROLLBACK( dbase, "dbase_update_eingang" )
        }
    }
    else //wenn Eltern oder gar nicht: kann gleich behandelt werden
            //wenn Vermittlung über Eltern, dann ist Datei noch nicht angebunden
    {
        gint ID_new = 0;
        gint ID_eingang_rel_path = 0;

        ID_new = dbase_insert_eingang( dbase, *eingang_loop, errmsg );
        if ( ID_new == -1 ) ERROR_ROLLBACK( dbase, "dbase_insert_eingang" )

        ID_eingang_rel_path = dbase_insert_eingang_rel_path( dbase, ID_new, rel_path, errmsg );
        if ( ID_eingang_rel_path == -1 ) ERROR_ROLLBACK( dbase, "dbase_insert_eingang_rel_path" )
    }

    rc = dbase_commit( dbase, errmsg );
    if ( rc ) ERROR_ROLLBACK( dbase, "dbase_commit" );

    return 0;
}


gint
eingang_set( GtkTreeView* fm_treeview, GtkTreeIter* iter, gpointer data,
        gchar** errmsg )
{
    gchar* rel_path = NULL;
    gint rc = 0;

    DBase* dbase = g_object_get_data( G_OBJECT(fm_treeview), "dbase" );

    Eingang** eingang_loop = (Eingang**) data;

    rel_path = fm_get_rel_path( gtk_tree_view_get_model( fm_treeview ), iter );

    rc = eingang_insert_or_update( fm_treeview, dbase, rel_path, eingang_loop, errmsg );
    g_free( rel_path );
    if ( rc == -1 ) ERROR( "eingang_insert_or_update" )

    return 0;
}
