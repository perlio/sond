#include "../global_types_sojus.h"

#include "../../misc.h"

#include <stdlib.h>
#include <gtk/gtk.h>
#include <mariadb/mysql.h>


void
cb_row_activated( GtkListBox* listbox, GtkListBoxRow* row, gpointer dialog )
{
    gtk_dialog_response( GTK_DIALOG(dialog), -5 );

    return;
}


GtkDialog*
auswahl_dialog_oeffnen( GtkWidget* window, const gchar* fenstertitel, const gchar*
        spaltentitel )
{
/*  Dialog erzeugen  */
    GtkDialog* dialog = GTK_DIALOG(gtk_dialog_new_with_buttons( fenstertitel,
            GTK_WINDOW(window), GTK_DIALOG_MODAL, "Ok", GTK_RESPONSE_OK,
            "Abbrechen", GTK_RESPONSE_CANCEL, NULL ));
    gtk_window_set_default_size( GTK_WINDOW(dialog), 300, 200 );

    GtkWidget* content_area = gtk_dialog_get_content_area( dialog );

    GtkWidget* label_spaltentitel = gtk_label_new( spaltentitel );
    gtk_widget_set_halign( GTK_WIDGET(label_spaltentitel), GTK_ALIGN_START );
    gtk_box_pack_start( GTK_BOX(content_area), label_spaltentitel, FALSE, FALSE,
            0 );

    GtkWidget* swindow = gtk_scrolled_window_new( NULL, NULL );
    gtk_box_pack_start( GTK_BOX(content_area), swindow, TRUE, TRUE, 0 );

    GtkWidget* list_box = gtk_list_box_new( );
    gtk_list_box_set_selection_mode( GTK_LIST_BOX(list_box),
            GTK_SELECTION_SINGLE );
    g_signal_connect( list_box, "row_activated", G_CALLBACK(cb_row_activated),
            (gpointer) dialog );
    gtk_container_add( GTK_CONTAINER(swindow), list_box );

    return dialog;
}


GtkListBox*
auswahl_dialog_get_listbox( GtkDialog* dialog )
{
    GtkWidget* content_area = gtk_dialog_get_content_area( dialog );
    GList* list = gtk_container_get_children( GTK_CONTAINER(content_area) );
    list = list->next;
    GtkWidget* viewport = gtk_bin_get_child( GTK_BIN(list->data) );
    g_list_free( list );
    GtkWidget* list_box = gtk_bin_get_child( GTK_BIN(viewport) );

    return GTK_LIST_BOX(list_box);
}


void
auswahl_dialog_zeile_einfuegen( GtkDialog* dialog, const gchar* zeile )
{
    GtkListBox* list_box = auswahl_dialog_get_listbox( dialog );
    GtkWidget* label = gtk_label_new( zeile );
    gtk_widget_set_halign( GTK_WIDGET(label), GTK_ALIGN_START );

    gtk_list_box_insert( GTK_LIST_BOX(list_box), label, -1 );

    return;
}


gint
auswahl_dialog_run( GtkDialog* dialog )
{
    gint index = -1;
    gtk_widget_show_all( GTK_WIDGET(dialog) );
    gint rc = gtk_dialog_run( dialog );

    if ( rc == GTK_RESPONSE_OK )
    {
        GtkListBox* list_box = auswahl_dialog_get_listbox( dialog );
        GtkListBoxRow* row = gtk_list_box_get_selected_row( list_box );
        index = gtk_list_box_row_get_index( row );
    }

    gtk_widget_destroy( GTK_WIDGET(dialog) );

    return index;
}


gboolean
auswahl_regnr_existiert( GtkWidget* window, MYSQL* con, gint regnr, gint year )
{
    gboolean ret = FALSE;
    gchar* sql = NULL;
    gint rc = 0;

    sql = g_strdup_printf( "SELECT COUNT(*) FROM akten WHERE RegNr = %i AND "
            "RegJahr = %i", regnr, year );
    if ( (rc = mysql_query( con, sql )) )
    {
        display_message( window, "Fehler bei auswahl_regnr_existiert\n", sql, "\n",
                mysql_error( con ), NULL );
        return ret;
    }

    MYSQL_RES* mysql_res = mysql_store_result( con );
    if ( !mysql_res )
    {
        display_message( window, "Fehler bei auswahl_regnr_existiert:\n", sql, ":\n",
                mysql_error( con ), NULL );
        return ret;
    }

    MYSQL_ROW row = mysql_fetch_row( mysql_res );

    if ( atoi( row[0] ) != 0 ) ret = TRUE;

    mysql_free_result( mysql_res );

    return ret;
}


void
auswahl_parse_regnr( const gchar* entry, gint* regnr, gint* jahr )
{
    gint strlen_vor_slash = 0;

    gchar* regnr_str = NULL;
    gchar* year_str = NULL;

    strlen_vor_slash = strlen( entry ) - strlen( g_strstr_len( entry, -1,
            "/" ) );

    year_str = g_strstr_len( entry, -1, "/" ) + 1;
    regnr_str = g_strndup( entry, strlen_vor_slash );

    *jahr = (gint) g_ascii_strtoll( year_str, NULL, 10 );
    *regnr = (gint) g_ascii_strtoll( regnr_str, NULL, 10 );
    g_free( regnr_str );

    if ( *jahr < JAHRHUNDERT_GRENZE ) *jahr += 2000;
    else *jahr += 1900;

    return;
}


gboolean
auswahl_regnr_ist_wohlgeformt( const gchar* entry )
{
    if ( (*entry < 48) || (*entry > 57) ) return FALSE; //erstes Zeichen muß Ziffer sein

    gint slashes = 0;
    gint pos = 1;
    while ( *(entry + pos) != 0 )
    {
        if ( *(entry + pos) == 47 ) slashes++;
        else if ( (*entry < 48) || (*entry > 57) ) return FALSE;
        if ( slashes > 1 ) return FALSE;
        pos++;
    }

    if ( slashes == 0 ) return FALSE;

    if ( strlen( g_strrstr( entry, "/" ) ) != 3 ) return FALSE;

    return TRUE;
}


gboolean
auswahl_parse_entry( GtkWidget* window, const gchar* entry )
{
    Sojus* sojus = (Sojus*) g_object_get_data( G_OBJECT(window), "sojus" );

    if ( auswahl_regnr_ist_wohlgeformt( entry ) )
    {
        gint jahr = 0;
        gint regnr = 0;
        auswahl_parse_regnr( entry, &regnr, &jahr );

        if ( !auswahl_regnr_existiert( window, sojus->db.con, regnr, jahr ) )
                return FALSE;

        sojus->jahr_akt = jahr;
        sojus->regnr_akt = regnr;

        return TRUE;
    }

    //prüfen, ob entry Teil der Aktenbezeichnung ist
    gchar* sql = NULL;
    gint rc = 0;

    sql = g_strdup_printf( "SELECT * FROM akten WHERE "
            "LOWER(Bezeichnung) LIKE LOWER('%%%s%%')", entry );
    if ( (rc = mysql_query( sojus->db.con, sql )) )
    {
        display_message( window, "Fehler bei auswahl_parse_regnr\n", sql,
                "\n", mysql_error( sojus->db.con ), NULL );
        return FALSE;
    }

    MYSQL_RES* mysql_res = mysql_store_result( sojus->db.con );
    if ( !mysql_res )
    {
        display_message( window, "Fehler bei auswahl_parse_regnr:\n", sql,
                ":\n", mysql_error( sojus->db.con ), NULL );
        return FALSE;
    }

    GPtrArray* rows = NULL;
    rows = g_ptr_array_new( );

    MYSQL_ROW row = NULL;
    while ( (row = mysql_fetch_row( mysql_res )) ) g_ptr_array_add( rows, row );

    gboolean ret = FALSE;

    if ( rows->len > 0 )
    {
        GtkDialog* dialog = auswahl_dialog_oeffnen( window,
                "Akte auswählen", "   Reg.-Nr.  Aktenbezeichnung" );

        for ( gint i = 0; i < rows->len; i++ )
        {
            row = g_ptr_array_index( rows, i );
            gchar* text = g_strdup_printf( "%8s/%s   %s", row[0], row[1] + 2,
                    row[2] );
            auswahl_dialog_zeile_einfuegen( dialog, text );
            g_free( text );
        }

        gint index = auswahl_dialog_run( dialog );
        if ( index >= 0 )
        {
            row = g_ptr_array_index( rows, index );
            sojus->regnr_akt = (gint) g_ascii_strtoll( row[0], NULL, 10 );
            sojus->jahr_akt = (gint) g_ascii_strtoll( row[1], NULL, 10 );

            ret = TRUE;
        }
    }

    g_ptr_array_free( rows, TRUE );
    mysql_free_result( mysql_res );

    return ret;
}


gboolean
auswahl_get_regnr_akt( Sojus* sojus, GtkEntry* entry )
{
    const gchar* entry_text = gtk_entry_get_text( entry );

    if ( !auswahl_parse_entry( sojus->app_window, entry_text ) )
    {
        gchar* text = NULL;
        //alte RegNr einfügen
        if ( (sojus->regnr_akt) && (sojus->jahr_akt) ) text = g_strdup_printf(
                "%i/%i", sojus->regnr_akt, sojus->jahr_akt % 100 );
        else text = g_strdup( "" );
        gtk_entry_set_text( entry, text );
        g_free( text );

        return FALSE;
    }

    return TRUE;
}




