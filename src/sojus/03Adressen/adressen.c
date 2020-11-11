#include "../globals.h"

#include "../../misc.h"


Adresse*
adresse_oeffnen( GtkWidget* window, gint id )
{
    Sojus* sojus = (Sojus*) g_object_get_data( G_OBJECT(window), "sojus" );

    //Datensatz aus Tabelle Adressen holen
    gchar* sql = NULL;
    gint rc = 0;

    sql = g_strdup_printf( "SELECT * FROM Adressen WHERE Adressnr = %i;", id );
    rc = mysql_query( sojus->db.con, sql );
    g_free( sql );
    if ( rc )
    {
        display_message( window, "Fehler bei adresse_oeffnen:\n"
                "mysql_query\n", mysql_error( sojus->db.con ), NULL );
        return NULL;
    }

    MYSQL_RES* mysql_res = mysql_store_result( sojus->db.con );
    if ( !mysql_res )
    {
        display_message( window, "Fehler bei adresse_oeffnen:\n",
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

    Adresse* adresse = g_malloc0( sizeof( Adresse ) );

    adresse->adresszeile1 = g_strdup( row[1] );
    adresse->titel = g_strdup( row[2] );
    adresse->vorname = g_strdup( row[3] );
    adresse->name = g_strdup( row[4] );
    adresse->adresszusatz = g_strdup( row[5] );
    adresse->strasse = g_strdup( row[6] );
    adresse->hausnr = g_strdup( row[7] );
    adresse->plz = g_strdup( row[8] );
    adresse->ort = g_strdup( row[9] );
    adresse->land = g_strdup( row[10] );
    adresse->telefon1 = g_strdup( row[11] );
    adresse->telefon2 = g_strdup( row[12] );
    adresse->telefon3 = g_strdup( row[13] );
    adresse->fax = g_strdup( row[14] );
    adresse->email = g_strdup( row[15] );
    adresse->homepage = g_strdup( row[16] );
    adresse->iban = g_strdup( row[17] );
    adresse->bic = g_strdup( row[18] );
    adresse->anrede = g_strdup( row[19] );
    adresse->bemerkungen = g_strdup( row[20] );

    mysql_free_result( mysql_res );

    return adresse;
}


void
adresse_free( Adresse* adresse )
{
    g_free( adresse->adresszeile1 );
    g_free( adresse->titel );
    g_free( adresse->vorname );
    g_free( adresse->name );
    g_free( adresse->adresszusatz );
    g_free( adresse->strasse );
    g_free( adresse->hausnr );
    g_free( adresse->plz );
    g_free( adresse->ort );
    g_free( adresse->land );
    g_free( adresse->telefon1 );
    g_free( adresse->telefon2 );
    g_free( adresse->telefon3 );
    g_free( adresse->fax );
    g_free( adresse->email );
    g_free( adresse->homepage );
    g_free( adresse->iban );
    g_free( adresse->bic );
    g_free( adresse->anrede );
    g_free( adresse->bemerkungen );

    g_free( adresse );

    return;
}


void
adresse_speichern( GtkWidget* adressen_window )
{
    Sojus* sojus = (Sojus*) g_object_get_data( G_OBJECT(adressen_window), "sojus" );
/*
**  aktuelle Werte holen  */
    gint adressnr = 0;
    GtkWidget* entry_adressnr = g_object_get_data( G_OBJECT(adressen_window),
            "entry_adressnr" );
    const gchar* adressnr_text = gtk_entry_get_text( GTK_ENTRY(entry_adressnr) );
    if ( g_strcmp0( adressnr_text, "- neu -" ) ) adressnr = atoi( adressnr_text );
    else
    {
        naechste_adressnr( sojus );
        adressnr = sojus->adressnr_akt;

        //adressnr in entry
        gchar* entry_text = g_strdup_printf( "%i", adressnr );
        gtk_entry_set_text( GTK_ENTRY(entry_adressnr), entry_text );
        g_free( entry_text );
    }

    GtkWidget* entry_adresszeile1 = g_object_get_data( G_OBJECT(adressen_window),
            "entry_adresszeile1" );
    const gchar* adresszeile1 = gtk_entry_get_text( GTK_ENTRY(entry_adresszeile1) );

    GtkWidget* entry_titel = g_object_get_data( G_OBJECT(adressen_window),
            "entry_titel" );
    const gchar* titel = gtk_entry_get_text( GTK_ENTRY(entry_titel) );

    GtkWidget* entry_vorname = g_object_get_data( G_OBJECT(adressen_window),
            "entry_vorname" );
    const gchar* vorname = gtk_entry_get_text( GTK_ENTRY(entry_vorname) );

    GtkWidget* entry_name = g_object_get_data( G_OBJECT(adressen_window),
            "entry_name" );
    const gchar* name = gtk_entry_get_text( GTK_ENTRY(entry_name) );

    GtkWidget* entry_adresszusatz = g_object_get_data( G_OBJECT(adressen_window),
            "entry_adresszusatz" );
    const gchar* adresszusatz = gtk_entry_get_text( GTK_ENTRY(entry_adresszusatz) );

    GtkWidget* entry_strasse = g_object_get_data( G_OBJECT(adressen_window),
            "entry_strasse" );
    const gchar* strasse = gtk_entry_get_text( GTK_ENTRY(entry_strasse) );

    GtkWidget* entry_hausnr = g_object_get_data( G_OBJECT(adressen_window),
            "entry_hausnr" );
    const gchar* hausnr = gtk_entry_get_text( GTK_ENTRY(entry_hausnr) );

    GtkWidget* entry_plz = g_object_get_data( G_OBJECT(adressen_window),
            "entry_plz" );
    const gchar* plz = gtk_entry_get_text( GTK_ENTRY(entry_plz) );

    GtkWidget* entry_ort = g_object_get_data( G_OBJECT(adressen_window),
            "entry_ort" );
    const gchar* ort = gtk_entry_get_text( GTK_ENTRY(entry_ort) );

    GtkWidget* entry_land = g_object_get_data( G_OBJECT(adressen_window),
            "entry_land" );
    const gchar* land = gtk_entry_get_text( GTK_ENTRY(entry_land) );

    GtkWidget* entry_telefon1 = g_object_get_data( G_OBJECT(adressen_window),
            "entry_telefon1" );
    const gchar* telefon1 = gtk_entry_get_text( GTK_ENTRY(entry_telefon1) );

    GtkWidget* entry_telefon2 = g_object_get_data( G_OBJECT(adressen_window),
            "entry_telefon2" );
    const gchar* telefon2 = gtk_entry_get_text( GTK_ENTRY(entry_telefon2) );

    GtkWidget* entry_telefon3 = g_object_get_data( G_OBJECT(adressen_window),
            "entry_telefon3" );
    const gchar* telefon3 = gtk_entry_get_text( GTK_ENTRY(entry_telefon3) );

    GtkWidget* entry_fax = g_object_get_data( G_OBJECT(adressen_window),
            "entry_fax" );
    const gchar* fax = gtk_entry_get_text( GTK_ENTRY(entry_fax) );

    GtkWidget* entry_email = g_object_get_data( G_OBJECT(adressen_window),
            "entry_email" );
    const gchar* email = gtk_entry_get_text( GTK_ENTRY(entry_email) );

    GtkWidget* entry_homepage = g_object_get_data( G_OBJECT(adressen_window),
            "entry_homepage" );
    const gchar* homepage = gtk_entry_get_text( GTK_ENTRY(entry_homepage) );

    GtkWidget* entry_iban = g_object_get_data( G_OBJECT(adressen_window),
            "entry_iban" );
    const gchar* iban = gtk_entry_get_text( GTK_ENTRY(entry_iban) );

    GtkWidget* entry_bic = g_object_get_data( G_OBJECT(adressen_window),
            "entry_bic" );
    const gchar* bic = gtk_entry_get_text( GTK_ENTRY(entry_bic) );

    GtkWidget* entry_anrede = g_object_get_data( G_OBJECT(adressen_window),
            "entry_anrede" );
    const gchar* anrede = gtk_entry_get_text( GTK_ENTRY(entry_anrede) );

    //Textview_bemerkungen
    GtkTextView* textview_bemerkungen = g_object_get_data( G_OBJECT(adressen_window),
            "textview_bemerkungen" );
    GtkTextBuffer* buffer_bemerkungen = gtk_text_view_get_buffer(
            textview_bemerkungen );

    GtkTextIter start;
    GtkTextIter end;

    gtk_text_buffer_get_start_iter( buffer_bemerkungen, &start );
    gtk_text_buffer_get_end_iter( buffer_bemerkungen, &end );

    gchar* bemerkungen = gtk_text_buffer_get_text( buffer_bemerkungen, &start,
            &end, FALSE );

/*
**  in sql speichern  */
    gchar* sql = NULL;
    gint rc = 0;

    sql = g_strdup_printf( "UPDATE Adressen SET `Adresszeile1`='%s', "
            "`Titel`='%s', `Vorname`='%s', `Name`='%s', Adresszusatz='%s', "
            "Strasse='%s', Hausnr='%s', PLZ='%s', Ort='%s', Land='%s', "
            "Telefon1='%s', Telefon2='%s', Telefon3='%s', Fax='%s', Email='%s', "
            "Homepage='%s', IBAN='%s', BIC='%s', Anrede='%s', Bemerkungen='%s' "
            "WHERE AdressNr=%i;",
            adresszeile1, titel, vorname, name, adresszusatz, strasse, hausnr,
            plz, ort, land, telefon1, telefon2, telefon3, fax, email, homepage,
            iban, bic, anrede, bemerkungen, adressnr );

    rc = mysql_query( sojus->db.con, sql );
    if ( rc )
    {
        display_message( adressen_window, "Fehler bei adresse_speichern:\n"
                "mysql_query\n", mysql_error( sojus->db.con ), NULL );
        g_free( sql );
        return;
    }

    sql_log( adressen_window, sojus->db.con, sql, sojus->db.user );

    g_free( sql );

    widgets_adresse_geaendert( G_OBJECT(adressen_window), FALSE );

    return;
}
