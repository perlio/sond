#include "../globals.h"

#include "../../misc.h"


gboolean
naechste_adressnr( Sojus* sojus )
{
    gint adressnr = 0;
    gchar* sql = NULL;
    gint rc = 0;

    sql = "SELECT MAX(Adressnr) FROM Adressen";
    rc = mysql_query( sojus->db.con, sql );
    if ( rc )
    {
        display_message( sojus->app_window, "Fehler bei Adresse anlegen\n "
                "SELECT MAX(Adressnr):\n", mysql_error( sojus->db.con ), NULL );
        return FALSE;
    }

    MYSQL_RES* mysql_res = mysql_store_result( sojus->db.con );
    if ( !mysql_res )
    {
        display_message( sojus->app_window, "Fehler bei Adresse anlegen:\n "
                "mysql_store_results:\n", mysql_error( sojus->db.con ), NULL );
        return FALSE;
    }

    MYSQL_ROW row = mysql_fetch_row( mysql_res );

    if ( !(row[0]) ) adressnr = 1;
    else adressnr = atoi( row[0] ) + 1;

    mysql_free_result( mysql_res );

    sql = g_strdup_printf( "INSERT INTO Adressen VALUES (%i, '', '', '', '', "
            "'', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '')",
            adressnr );

    rc = mysql_query( sojus->db.con, sql );
    if ( rc )
    {
        display_message( sojus->app_window, "Fehler bei Akte anlegen\n "
                "INSERT INTO akten:\n", mysql_error( sojus->db.con ), NULL );
        g_free( sql );

        return FALSE;
    }

    sql_log( sojus, sql );

    g_free( sql );

    sojus->adressnr_akt = adressnr;

    return TRUE;
}


gboolean
adressnr_ist_wohlgeformt( gchar* text )
{
    gint pos = 0;
    while ( (*(text + pos)) != 0 )
    {
        if ( ((*(text + pos)) < 48) || ((*(text + pos)) > 57) ) return FALSE;
        pos++;
    }

    return TRUE;
}


gboolean
adressnr_existiert( GtkWidget* window, MYSQL* con, gint adressnr )
{
    gchar* sql = NULL;
    gint rc = 0;

    sql = g_strdup_printf( "SELECT COUNT(*) FROM Adressen WHERE AdressNr = %i;",
            adressnr );
    rc = mysql_query( con, sql );
    if ( rc )
    {
        display_message( window, "Fehler bei adressnr_existiert\n", sql, "\n",
                mysql_error( con ), NULL );
        return FALSE;
    }

    MYSQL_RES* mysql_res = mysql_store_result( con );
    if ( !mysql_res )
    {
        display_message( window, "Fehler bei adressnr_existiert:\n", sql, ":\n",
                mysql_error( con ), NULL );
        mysql_free_result( mysql_res );
        return FALSE;
    }

    MYSQL_ROW row = mysql_fetch_row( mysql_res );

    if ( atoi( row[0] ) == 0 ) return FALSE;

    mysql_free_result( mysql_res );

    return TRUE;
}


gboolean
parse_adressnr( GtkWidget* window, gchar* text )
{
    Sojus* sojus = (Sojus*) g_object_get_data( G_OBJECT(window), "sojus" );

    //mind. eine und nur Ziffern?
    if ( strlen( text ) == 0 ) return FALSE; //mind. ein Zeichen

    if ( adressnr_ist_wohlgeformt( text ) )
    {
        gint adressnr = (gint) g_ascii_strtoll( text, NULL, 10 );

        if ( !adressnr_existiert( window, sojus->db.con, adressnr ) ) return FALSE;

        sojus->adressnr_akt = adressnr;

        return TRUE;
    }

/*
**  Prüfen, ob String in Adresse->Name steckt  */
    gchar* sql = NULL;
    gint rc = 0;

    sql = g_strdup_printf( "SELECT * FROM Adressen WHERE "
            "LOWER(Name) LIKE LOWER('%%%s%%')", text );
    rc = mysql_query( sojus->db.con, sql );
    if ( rc )
    {
        display_message( sojus->app_window, "Fehler bei parse_adressnr\n", sql,
                "\n", mysql_error( sojus->db.con ), NULL );
        return FALSE;
    }

    MYSQL_RES* mysql_res = mysql_store_result( sojus->db.con );
    if ( !mysql_res )
    {
        display_message( sojus->app_window, "Fehler bei parse_adressnr:\n", sql,
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
                "Adresse auswählen", "   Adressnr.  Name, Vorname" );

        for ( gint i = 0; i < rows->len; i++ )
        {
            row = g_ptr_array_index( rows, i );
            gchar* text = g_strdup_printf( "%6s   %s, %s", row[0], row[4], row[3] );
            auswahl_dialog_zeile_einfuegen( dialog, text );
            g_free( text );
        }

        gint index = auswahl_dialog_run( dialog );
        if ( index >= 0 )
        {
            row = g_ptr_array_index( rows, index );
            sojus->adressnr_akt = (gint) g_ascii_strtoll( row[0], NULL, 10 );

            ret = TRUE;
        }
    }

    g_ptr_array_free( rows, TRUE );
    mysql_free_result( mysql_res );

    return ret;
}
