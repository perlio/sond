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
#include "sond_treeviewfm.h"


static void
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


/** -1: Fehler
     0: nicht gefunden
     1: row in Tabelle eingang mit Verweis von rel_path
     2ff.: Grad der Elternverzeichnisse eingetragen         **/
gint
eingang_for_rel_path( DBase* dbase, const gchar* rel_path, gint* ID,
        Eingang* eingang, gint* ID_eingang_rel_path, gchar** errmsg )
{
    gchar* buf = NULL;
    gint zaehler = 1;

    if ( !dbase ) return 0;

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
            ERROR_SOND( "dbase_get_eingang_for_rel_path" )
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


gint
eingang_update_rel_path( DBase* dbase_src, const gchar* rel_path_src,
        DBase* dbase_dest, const gchar* rel_path_dest, gboolean del, gchar** errmsg )
{
    gint rc_src = 0, rc_dest = 0;
    gint ID_src = 0, ID_dest = 0;
    Eingang eingang_src = { 0 };
    gint ID_eingang_rel_path_src = 0;

    rc_dest = eingang_for_rel_path( dbase_dest, rel_path_dest, &ID_dest, NULL, NULL, errmsg );
    if ( rc_dest == -1 ) ERROR_SOND( "eingang_for_rel_path(dest)" )

    rc_src = eingang_for_rel_path( dbase_src, rel_path_src, &ID_src, &eingang_src, &ID_eingang_rel_path_src, errmsg );
    if ( rc_src == -1 ) ERROR_SOND( "eingang_for_rel_path (source)" )

    if ( dbase_dest == dbase_src )
    {
        if ( rc_src == 1 && ID_src == ID_dest && del ) //unmittelbar markierte Datei in identischen scope verschieben
        {
            gint rc = 0;

            rc = dbase_delete_eingang_rel_path( dbase_src, ID_eingang_rel_path_src, errmsg );
            if ( rc ) ERROR_SOND( "dbase_delete_eingang_rel_path" )
        }
        else if ( rc_src == 1 && del ) //unmittelbar markierte Datei in anderen scope verschieben
        {
            gint rc = 0;

            //Markierung bleibt, nur rel_path wird angepaßt
            rc = dbase_update_eingang_rel_path( dbase_dest, ID_eingang_rel_path_src, ID_src, rel_path_dest, errmsg );
            if ( rc ) ERROR_SOND( "dbase_update_eingang_rel_path" )
        }
        else if ( (rc_src == 0 && rc_dest != 0) || (rc_src > 1 && ID_src != ID_dest )
                || (!del && (rc_src == 1 && ID_src != ID_dest)) )
        { //nicht markierte Datei in markierten scope oder mittelbar markierte Datei in anderen scope
            gint rc = 0;

            //Datei muß eigene Markierung enthalten
            rc = dbase_insert_eingang_rel_path( dbase_dest, ID_src, rel_path_dest, errmsg );
            if ( rc ) ERROR_SOND( "dbase_insert_eingang_rel_path" )
        }
    }
    else
    {
        gint ID_eingang_dest_new = 0;
        gint ID_eingang_rel_path_dest_new = 0;

        if ( rc_src == 1 && del )
        {
            gint rc = 0;

            rc = dbase_delete_eingang_rel_path( dbase_src, ID_eingang_rel_path_src, errmsg );
            if ( rc ) ERROR_SOND( "dbase_delete_eingang_rel_path" )

            //letzte reference auf eingang ID_scr -> dann ID_scr löschen
            rc = dbase_get_num_of_refs_to_eingang( dbase_src, ID_src, errmsg );
            if ( rc == -1 ) ERROR_SOND( "dbase_eingang_ist_waise" )

            if ( rc > 0 )
            {
                gint rc = 0;

                rc = dbase_delete_eingang( dbase_src, ID_src, errmsg );
                if ( rc == -1 ) ERROR_SOND( "dbase_delete_eingang" )
            }
        }

        //neuen eingang einfügen
        ID_eingang_dest_new = dbase_insert_eingang( dbase_dest, &eingang_src, errmsg );
        if ( ID_eingang_dest_new == -1 ) ERROR_SOND( "dbase_insert_eingang" )

        //neuen eingang_rel_path einfügen
        ID_eingang_rel_path_dest_new = dbase_insert_eingang_rel_path( dbase_dest,
                ID_eingang_dest_new, rel_path_dest, errmsg );
        if ( ID_eingang_rel_path_dest_new == -1 )
                ERROR_SOND( "dbase_insert_eingang_rel_path" )
    }

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

    GtkWidget* content_area = gtk_dialog_get_content_area( GTK_DIALOG(dialog) );

    GtkWidget* label_title = gtk_label_new( title );
    GtkWidget* label_secondary = gtk_label_new( secondary );

    gtk_box_pack_start( GTK_BOX(content_area), label_title, TRUE, TRUE, 0 );
    gtk_box_pack_start( GTK_BOX(content_area), label_secondary, TRUE, TRUE, 0 );

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

    GtkWidget* frame_traeger = gtk_frame_new( "Trägermedium" );
    GtkWidget* entry_traeger = gtk_entry_new( );
    if ( eingang->traeger ) gtk_entry_set_text( GTK_ENTRY(entry_traeger), eingang->traeger );
    gtk_container_add( GTK_CONTAINER(frame_traeger), entry_traeger );

    GtkWidget* frame_ort = gtk_frame_new( "Übergabeort" );
    GtkWidget* entry_ort = gtk_entry_new( );
    if ( eingang->ort ) gtk_entry_set_text( GTK_ENTRY(entry_ort), eingang->ort );
    gtk_container_add( GTK_CONTAINER(frame_ort), entry_ort );

    GtkWidget* frame_absender = gtk_frame_new( "Absender" );
    GtkWidget* entry_absender = gtk_entry_new( );
    if ( eingang->absender ) gtk_entry_set_text( GTK_ENTRY(entry_absender), eingang->absender );
    gtk_container_add( GTK_CONTAINER(frame_absender), entry_absender );

    GtkWidget* frame_absendedatum = gtk_frame_new( "Absendefatum" );
    GtkWidget* calendar_absendedatum = gtk_calendar_new( );
    misc_set_calendar( GTK_CALENDAR(calendar_absendedatum), eingang->absendedatum);
    gtk_container_add( GTK_CONTAINER(frame_absendedatum), calendar_absendedatum );

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

    gtk_grid_attach( GTK_GRID(grid), frame_eingang, 0, 0, 1, 3 );
    gtk_grid_attach( GTK_GRID(grid), frame_transport, 1, 0, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), frame_traeger, 1, 1, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), frame_ort, 1, 2, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), frame_absendedatum, 0, 3, 1, 2 );
    gtk_grid_attach( GTK_GRID(grid), frame_absender, 1, 3, 1, 1 );
    gtk_grid_attach( GTK_GRID(grid), calendar_erfassungsdatum, 0, 5, 1, 1 );

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
eingang_insert_or_update( SondTreeviewFM* stvfm, EingangDBase* eingang_dbase,
        const gchar* rel_path, gchar** errmsg )
{
    gint rc = 0;
    gint ret = 0;
    gint eingang_id = 0;
    gint eingang_rel_path_id = 0;
    Eingang eingang = { 0 };
    DBase* dbase = NULL;
    Eingang** eingang_loop = NULL;
    gint* last_inserted_ID = NULL;

    dbase = eingang_dbase->dbase;
    eingang_loop = eingang_dbase->eingang;
    last_inserted_ID = eingang_dbase->last_inserted_ID;

    ret = eingang_for_rel_path( dbase, rel_path, &eingang_id, &eingang, &eingang_rel_path_id, errmsg );
    if ( ret == -1 ) ERROR_SOND( "eingang_for_rel_path" )
    else if ( ret > 0 ) //es gibt - mittelbar oder unmittelbar - Eingangsdaten
    {
        gint rc = 0;
        gchar* title = NULL;

        title = g_strconcat( "Zur Datei ", rel_path, "\nwurden bereits Eingangsdaten gespeichert", NULL );

        rc = eingang_fenster( GTK_WIDGET(stvfm), &eingang, FALSE, title, "Überschreiben?" );
        g_free( title );

        if ( rc == GTK_RESPONSE_NO ) return 0;
        else if ( rc != GTK_RESPONSE_OK ) return 1; //abgebrochen
    }

    if ( !(*eingang_loop) )
    {
        gint rc = 0;

        //Eingang abfragen
        *eingang_loop = g_malloc0( sizeof( Eingang ) );

        rc = eingang_fenster( GTK_WIDGET(stvfm), *eingang_loop, TRUE,
                "Bitte Eingangsdaten eingeben", NULL );
        if ( rc != GTK_RESPONSE_OK ) return 1;
    }

    rc = dbase_begin( dbase, errmsg );
    if ( rc ) ERROR_SOND( "dbase_begin" )

    //hier Abfrage, was bei drohender Kollision gemacht werden soll
    if ( ret == 1 ) //zu rel_path selbst ist Eingang gespeichert
    {
        gint refs = 0;

        refs = dbase_get_num_of_refs_to_eingang( dbase, eingang_id, errmsg );
        if ( refs == -1 ) ERROR_ROLLBACK( dbase, "dbase_get_num_of_refs_to_eingang" )

        if ( !(*last_inserted_ID) && refs == 1 )
        {
            gint rc = 0;

            rc = dbase_update_eingang( dbase, eingang_id, *eingang_loop, errmsg );
            if ( rc ) ERROR_ROLLBACK( dbase, "dbase_update_eingang" );

            *last_inserted_ID = eingang_id;
        }
        else //*last_inserted_ID oder refs > 1
        {
            gint rc = 0;

            if ( !(*last_inserted_ID) ) //&& refs > 1
            {
                *last_inserted_ID = dbase_insert_eingang( dbase, *eingang_loop, errmsg );
                if ( *last_inserted_ID == -1 ) ERROR_ROLLBACK( dbase, "dbase_insert_eingang" )
            }

            rc = dbase_update_eingang_rel_path( dbase, eingang_rel_path_id,
                    *last_inserted_ID, rel_path, errmsg );
            if ( rc ) ERROR_ROLLBACK( dbase, "dbase_update_eingang" )

            if ( refs == 1 )
            {
                gint rc = 0;

                rc = dbase_delete_eingang( dbase, eingang_id, errmsg );
                if ( rc == -1 ) ERROR_ROLLBACK( dbase, "dbase_delete_eingang" )
            }
        }
    }
    else //wenn Eltern oder gar nicht: kann gleich behandelt werden
    {//eingang_rel_path_id ist 0, wenn ret > 1
        if ( !(*last_inserted_ID) )
        {
            *last_inserted_ID = dbase_insert_eingang( dbase, *eingang_loop, errmsg );
            if ( *last_inserted_ID == -1 ) ERROR_ROLLBACK( dbase, "dbase_insert_eingang" )
        }

        eingang_rel_path_id = dbase_insert_eingang_rel_path( dbase,
                *last_inserted_ID, rel_path, errmsg );
        if ( eingang_rel_path_id == -1 ) ERROR_ROLLBACK( dbase, "dbase_insert_eingang_rel_path" )
    }

    rc = dbase_commit( dbase, errmsg );
    if ( rc ) ERROR_ROLLBACK( dbase, "dbase_commit" );

    return 0;
}


static gint
eingang_set_for_rel_path( SondTreeview* stv, GtkTreeIter* iter, gpointer data,
        gchar** errmsg )
{
    gchar* rel_path = NULL;
    gint rc = 0;

    EingangDBase* eingang_dbase = (EingangDBase*) data;

    rel_path = sond_treeviewfm_get_rel_path( SOND_TREEVIEWFM(stv), iter );

    rc = eingang_insert_or_update( SOND_TREEVIEWFM(stv), eingang_dbase, rel_path, errmsg );
    g_free( rel_path );
    if ( rc == -1 ) ERROR_SOND( "eingang_insert_or_update" )

    return 1;
}


gint
eingang_set( SondTreeviewFM* stvfm, gchar** errmsg )
{
    gint rc = 0;
    Eingang* eingang = NULL;
    gint last_inserted_id = 0;

    EingangDBase eingang_dbase = { &eingang, sond_treeviewfm_get_dbase( stvfm ),
            &last_inserted_id };

    rc = sond_treeview_selection_foreach( SOND_TREEVIEW(stvfm),
            eingang_set_for_rel_path, &eingang_dbase, errmsg );
    eingang_free( *(eingang_dbase.eingang) );
    if ( rc == -1 ) ERROR_SOND( "sond_treeview_selection_foreach" )

    return 0;
}


