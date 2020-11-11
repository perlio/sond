#include "../globals.h"

#include "../../misc.h"


void
akte_free( Akte* akte )
{
    g_free( akte->bezeichnung );
    g_free( akte->gegenstand );
    g_free( akte->sachgebiet );
    g_free( akte->sachbearbeiter_id );
    g_free( akte->anlagedatum );
    g_free( akte->ablagenr );

    g_free( akte );

    return;
}


Akte*
akte_oeffnen( GtkWidget* akten_window, gint regnr, gint jahr )
{
    Sojus* sojus = g_object_get_data( G_OBJECT(akten_window), "sojus" );

    //Datensatz aus Tabelle akten holen
    gchar* sql = NULL;
    gint rc = 0;

    sql = g_strdup_printf( "SELECT * FROM akten WHERE "
            "RegNr = %i AND RegJahr = %i;", regnr, jahr );
    rc = mysql_query( sojus->db.con, sql );
    g_free( sql );
    if ( rc )
    {
        display_message( akten_window, "Fehler bei akte_oeffnen:\n"
                "mysql_query\n", mysql_error( sojus->db.con ), NULL );
        return NULL;
    }

    MYSQL_RES* mysql_res = mysql_store_result( sojus->db.con );
    if ( !mysql_res )
    {
        display_message( akten_window, "Fehler bei akte_oeffnen:\n",
                "mysql_store_res:\n", mysql_error( sojus->db.con ), NULL );
        return NULL;
    }

    MYSQL_ROW row = NULL;
    row = mysql_fetch_row( mysql_res );

    if ( !row )
    {
        mysql_free_result( mysql_res );
        return NULL;
    }

    Akte* akte = g_malloc0( sizeof( Akte ) );

    akte->bezeichnung = g_strdup( row[2] );
    akte->gegenstand = g_strdup( row[3] );
    akte->sachgebiet = g_strdup( row[4] );
    akte->sachbearbeiter_id = g_strdup( row[5] );
    akte->anlagedatum = g_strdup( row[6] );
    akte->ablagenr = g_strdup( row[7] );

    mysql_free_result( mysql_res );

    return akte;
}


static gboolean
akte_anlegen( Sojus* sojus )
{
    gint year = 0;
    gchar* sql = NULL;
    gint rc = 0;
    gint regnr = 0;

    GDateTime* time = g_date_time_new_now_local( );
    year = g_date_time_get_year( time );
    g_date_time_unref( time );

    sql = "SELECT MAX(RegJahr) FROM akten";
    if ( (rc = mysql_query( sojus->db.con, sql )) )
    {
        display_message( sojus->app_window, "Fehler bei Akte anlegen\n "
                "SELECT MAX(RegJahr):\n", mysql_error( sojus->db.con ), NULL );
        return FALSE;
    }

    MYSQL_RES* mysql_res = mysql_store_result( sojus->db.con );
    if ( !mysql_res )
    {
        display_message( sojus->app_window, "Fehler bei Akte anlegen:\n "
                "mysql_store_results:\n", mysql_error( sojus->db.con ), NULL );
        return FALSE;
    }

    MYSQL_ROW row = mysql_fetch_row( mysql_res );

    if ( !(row[0]) || (atoi(row[0]) < year) ) regnr = 1;
    else
    {
        sql = g_strdup_printf( "SELECT MAX(RegNr) FROM akten WHERE RegJahr = %i;",
                year );
        rc = mysql_query( sojus->db.con, sql );
        g_free( sql );
        if ( rc )
        {
            display_message( sojus->app_window, "Fehler bei Akte anlegen\n "
                    "SELECT MAX(RegNr):\n", mysql_error( sojus->db.con ), NULL );
            return FALSE;
        }

        MYSQL_RES* mysql_res = mysql_store_result( sojus->db.con );
        if ( !mysql_res )
        {
            display_message( sojus->app_window, "Fehler bei Akte anlegen:\n "
                    "mysql_store_results (SELECT MAX(RegNr):\n",
                    mysql_error( sojus->db.con ), NULL );
            return FALSE;
        }

        MYSQL_ROW row = mysql_fetch_row( mysql_res );

        regnr = atoi( row[0] ) + 1;

        mysql_free_result( mysql_res );
    }

    mysql_free_result( mysql_res );

    sql = g_strdup_printf( "INSERT INTO akten VALUES (%i, %i, '', '', '', '', "
            "NOW(3), '')", regnr, year );
    rc = mysql_query( sojus->db.con, sql );
    if ( rc )
    {
        display_message( sojus->app_window, "Fehler bei nächste_regnr\n "
                "INSERT INTO akten:\n", mysql_error( sojus->db.con ), NULL );
        g_free( sql );

        return FALSE;
    }

    sql_log( sojus->app_window, sojus->db.con, sql, sojus->db.user );

    g_free( sql );

    sojus->regnr_akt = regnr;
    sojus->jahr_akt = year;

    return TRUE;
}


void
akte_speichern( GtkWidget* akten_window )
{
    Sojus* sojus = (Sojus*) g_object_get_data( G_OBJECT(akten_window), "sojus" );

/*
**  aktuelle Werte holen  */
    //regnr
    GtkWidget* entry_regnr = g_object_get_data( G_OBJECT(akten_window),
            "entry_regnr" );
    const gchar* regnr_text = gtk_entry_get_text( GTK_ENTRY(entry_regnr) );

    if ( !g_strcmp0( regnr_text, "- neu -" ) )
    {
        akte_anlegen( sojus );

        //adressnr in entry
        gchar* entry_text = g_strdup_printf( "%i/%i", sojus->regnr_akt,
                sojus->jahr_akt % 100 );
        gtk_entry_set_text( GTK_ENTRY(entry_regnr), entry_text );
        g_free( entry_text );
    }

    //aktenbezeichnung
    GtkWidget* entry_bezeichnung = g_object_get_data( G_OBJECT(akten_window),
            "entry_bezeichnung" );
    const gchar* bezeichnung = gtk_entry_get_text( GTK_ENTRY(entry_bezeichnung) );

    GtkWidget* entry_gegenstand = g_object_get_data( G_OBJECT(akten_window),
            "entry_gegenstand" );
    const gchar* gegenstand = gtk_entry_get_text( GTK_ENTRY(entry_gegenstand) );

    GtkWidget* combo_sachgebiete = g_object_get_data( G_OBJECT(akten_window),
            "combo_sachgebiete" );
    gchar* sachgebiet = gtk_combo_box_text_get_active_text(
            GTK_COMBO_BOX_TEXT(combo_sachgebiete) );

    GtkWidget* combo_sachbearbeiter = g_object_get_data( G_OBJECT(akten_window),
            "combo_sachbearbeiter" );
    gchar* sachbearbeiter_id = gtk_combo_box_text_get_active_text(
            GTK_COMBO_BOX_TEXT(combo_sachbearbeiter) );

/*
**  in sql speichern  */
    gchar* sql = NULL;
    gint rc = 0;

    sql = g_strdup_printf( "UPDATE Akten SET `Bezeichnung`='%s', "
            "`Gegenstand`='%s', `Sachgebiet`='%s', `Sachbearbeiter-ID`='%s' "
            "WHERE RegNr=%i AND RegJahr=%i;",
            bezeichnung, gegenstand, sachgebiet, sachbearbeiter_id,
            sojus->regnr_akt, sojus->jahr_akt );

    g_free( sachgebiet );
    g_free( sachbearbeiter_id );

    rc = mysql_query( sojus->db.con, sql );
    if ( rc )
    {
        display_message( akten_window, "Fehler bei akte_speichern:\n"
                "mysql_query\n", mysql_error( sojus->db.con ), NULL );
        g_free( sql );
        return;
    }

    sql_log( akten_window, sojus->db.con, sql, sojus->db.user );

    g_free( sql );

    aktenbet_speichern( akten_window );

    widgets_akte_geaendert( G_OBJECT(akten_window), FALSE );

    return;
}


