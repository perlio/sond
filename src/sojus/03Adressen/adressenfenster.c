#include "../globals.h"

#include "../../misc.h"


gboolean
cb_window_adr_delete_event( GtkWidget* window, GdkEvent* event, gpointer user_data )
{
    GtkWidget* button_speichern = g_object_get_data( G_OBJECT(window),
            "button_speichern" );
    if ( gtk_widget_get_sensitive( button_speichern ) )
    {
        gint rc = 0;
        rc = ask_question( window, "Änderungen speichern?", "Ja", "Nein" );

        if ( rc == GTK_RESPONSE_CANCEL ) return TRUE;
        if ( rc == GTK_RESPONSE_YES ) adresse_speichern( window );
    }

    return FALSE;
}


void
cb_button_abbrechen_adr_clicked( GtkButton* button, gpointer user_data )
{
    GtkWidget* adressen_window = (GtkWidget*) user_data;
    GtkWidget* button_speichern = g_object_get_data( G_OBJECT(adressen_window),
            "button_speichern" );

    if ( gtk_widget_get_sensitive( button_speichern ) )
    {
        gint rc = 0;
        rc = ask_question( adressen_window, "Änderungen speichern?", "Ja", "Nein" );

        if ( rc == GTK_RESPONSE_CANCEL ) return;
        if ( rc == GTK_RESPONSE_YES ) adresse_speichern( adressen_window );
    }

    gtk_widget_destroy( adressen_window );

    return;
}


void
cb_button_speichern_adr_clicked( GtkButton* button, gpointer adressen_window )
{
    adresse_speichern( (GtkWidget*) adressen_window );

    return;
}


void
cb_button_ok_adr_clicked( GtkButton* button, gpointer user_data )
{
    GtkWidget* adressen_window = (GtkWidget*) user_data;
    GtkWidget* button_speichern = g_object_get_data( G_OBJECT(adressen_window),
            "button_speichern" );

    if ( gtk_widget_get_sensitive( button_speichern ) )
            adresse_speichern( adressen_window );

    gtk_widget_destroy( adressen_window );

    return;
}


void
cb_adresse_geaendert( gpointer window )
{
    widgets_adresse_geaendert( (GObject*) window, TRUE );

    return;
}


void
cb_button_neue_adresse_clicked( GtkButton* button, gpointer user_data )
{
    GtkWidget* adressen_window = (GtkWidget*) user_data;

    adressenfenster_fuellen( adressen_window, -1 );

    return;
}


void
cb_entry_adressnr_activate( GtkEntry* entry, gpointer user_data )
{
    GtkWidget* adressen_window = (GtkWidget*) user_data;
    Sojus* sojus = g_object_get_data( G_OBJECT(adressen_window), "sojus" );

    gchar* entry_text = NULL;

    entry_text = (gchar*) gtk_entry_get_text( entry );

    gboolean ret = parse_adressnr( adressen_window, entry_text );

    if ( ret ) adressenfenster_fuellen( adressen_window, sojus->adressnr_akt );
    else
    {
        //alte AdressNr einfügen
        if ( sojus->adressnr_akt ) entry_text = g_strdup_printf( "%i",
                sojus->adressnr_akt );
        else entry_text = g_strdup( "" );
        gtk_entry_set_text( entry, entry_text );
        g_free( entry_text );

        gtk_widget_grab_focus( GTK_WIDGET(entry) );
    }

    return;
}


GtkWidget*
adressenfenster_oeffnen( Sojus* sojus )
{
    //Window erzeugen
    GtkWidget* adressen_window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_title( GTK_WINDOW(adressen_window), "Adresse" );
    gtk_window_set_default_size( GTK_WINDOW(adressen_window), 1200, 700 );

    GtkWidget* adressen_headerbar = gtk_header_bar_new( );
    gtk_header_bar_set_show_close_button( GTK_HEADER_BAR(adressen_headerbar), TRUE );
    gtk_header_bar_set_title( GTK_HEADER_BAR(adressen_headerbar), "Adressen" );
    gtk_window_set_titlebar( GTK_WINDOW(adressen_window), adressen_headerbar );

    GtkWidget* grid = gtk_grid_new( );
    gtk_container_add( GTK_CONTAINER(adressen_window), grid );

/*
**  Widgets erzeugen  */
    //Adressnr
    GtkWidget* frame_adressnr = gtk_frame_new( "Adressnr." );
    GtkWidget* entry_adressnr = gtk_entry_new( );
    gtk_container_add( GTK_CONTAINER(frame_adressnr), entry_adressnr );
    gtk_header_bar_pack_start( GTK_HEADER_BAR(adressen_headerbar), frame_adressnr );
    if ( sojus->adressnr_akt )
    {
        gchar* text_adressnr = g_strdup_printf( "%i", sojus->adressnr_akt );
        gtk_entry_set_text( GTK_ENTRY(entry_adressnr), text_adressnr );
        g_free( text_adressnr );
    }

    //Button Neue Adresse
    GtkWidget* button_neue_adresse = gtk_button_new_with_label( "Neue Adresse" );
    gtk_header_bar_pack_start( GTK_HEADER_BAR(adressen_headerbar), button_neue_adresse );

    //Adresszeile1
    GtkWidget* frame_adresszeile1 = gtk_frame_new( "Adresszeile 1" );
    GtkWidget* entry_adresszeile1 = gtk_entry_new( );
    GtkEntryBuffer* entry_buffer_adresszeile1 = gtk_entry_get_buffer(
            GTK_ENTRY(entry_adresszeile1) );
    gtk_container_add( GTK_CONTAINER(frame_adresszeile1), entry_adresszeile1 );
    gtk_grid_attach( GTK_GRID(grid), frame_adresszeile1, 0, 1, 2, 1 );

    //Titel
    GtkWidget* frame_titel = gtk_frame_new( "Titel" );
    GtkWidget* entry_titel = gtk_entry_new( );
    GtkEntryBuffer* entry_buffer_titel = gtk_entry_get_buffer(
            GTK_ENTRY(entry_titel) );
    gtk_container_add( GTK_CONTAINER(frame_titel), entry_titel );
    gtk_grid_attach( GTK_GRID(grid), frame_titel, 0, 2, 1, 1 );

    //Vorname
    GtkWidget* frame_vorname = gtk_frame_new( "Vorname" );
    GtkWidget* entry_vorname = gtk_entry_new( );
    GtkEntryBuffer* entry_buffer_vorname = gtk_entry_get_buffer(
            GTK_ENTRY(entry_vorname) );
    gtk_container_add( GTK_CONTAINER(frame_vorname), entry_vorname );
    gtk_grid_attach( GTK_GRID(grid), frame_vorname, 1, 2, 1, 1 );

    //Name
    GtkWidget* frame_name = gtk_frame_new( "Name" );
    GtkWidget* entry_name = gtk_entry_new( );
    GtkEntryBuffer* entry_buffer_name = gtk_entry_get_buffer(
            GTK_ENTRY(entry_name) );
    gtk_container_add( GTK_CONTAINER(frame_name), entry_name );
    gtk_grid_attach( GTK_GRID(grid), frame_name, 0, 3, 2, 1 );

    //Adresszusatz
    GtkWidget* frame_adresszusatz = gtk_frame_new( "Adresszusatz" );
    GtkWidget* entry_adresszusatz = gtk_entry_new( );
    GtkEntryBuffer* entry_buffer_adresszusatz = gtk_entry_get_buffer(
            GTK_ENTRY(entry_adresszusatz) );
    gtk_container_add( GTK_CONTAINER(frame_adresszusatz), entry_adresszusatz );
    gtk_grid_attach( GTK_GRID(grid), frame_adresszusatz, 0, 4, 2, 1 );

    //Straße
    GtkWidget* frame_strasse = gtk_frame_new( "Straße" );
    GtkWidget* entry_strasse = gtk_entry_new( );
    GtkEntryBuffer* entry_buffer_strasse = gtk_entry_get_buffer(
            GTK_ENTRY(entry_strasse) );
    gtk_container_add( GTK_CONTAINER(frame_strasse), entry_strasse );
    gtk_grid_attach( GTK_GRID(grid), frame_strasse, 0, 5, 1, 1 );

    //Hausnr
    GtkWidget* frame_hausnr = gtk_frame_new( "Hausnr." );
    GtkWidget* entry_hausnr = gtk_entry_new( );
    GtkEntryBuffer* entry_buffer_hausnr = gtk_entry_get_buffer(
            GTK_ENTRY(entry_hausnr) );
    gtk_container_add( GTK_CONTAINER(frame_hausnr), entry_hausnr );
    gtk_grid_attach( GTK_GRID(grid), frame_hausnr, 1, 5, 1, 1 );

    //PLZ
    GtkWidget* frame_plz = gtk_frame_new( "PLZ" );
    GtkWidget* entry_plz = gtk_entry_new( );
    GtkEntryBuffer* entry_buffer_plz = gtk_entry_get_buffer(
            GTK_ENTRY(entry_plz) );
    gtk_container_add( GTK_CONTAINER(frame_plz), entry_plz );
    gtk_grid_attach( GTK_GRID(grid), frame_plz, 0, 6, 1, 1 );

    //Ort
    GtkWidget* frame_ort = gtk_frame_new( "Ort" );
    GtkWidget* entry_ort = gtk_entry_new( );
    GtkEntryBuffer* entry_buffer_ort = gtk_entry_get_buffer(
            GTK_ENTRY(entry_ort) );
    gtk_container_add( GTK_CONTAINER(frame_ort), entry_ort );
    gtk_grid_attach( GTK_GRID(grid), frame_ort, 1, 6, 1, 1 );

    //Land
    GtkWidget* frame_land = gtk_frame_new( "Land" );
    GtkWidget* entry_land = gtk_entry_new( );
    GtkEntryBuffer* entry_buffer_land = gtk_entry_get_buffer(
            GTK_ENTRY(entry_land) );
    gtk_container_add( GTK_CONTAINER(frame_land), entry_land );
    gtk_grid_attach( GTK_GRID(grid), frame_land, 0, 7, 2, 1 );

    //Telefon1
    GtkWidget* frame_telefon1 = gtk_frame_new( "Telefon 1" );
    GtkWidget* entry_telefon1 = gtk_entry_new( );
    GtkEntryBuffer* entry_buffer_telefon1 = gtk_entry_get_buffer(
            GTK_ENTRY(entry_telefon1) );
    gtk_container_add( GTK_CONTAINER(frame_telefon1), entry_telefon1 );
    gtk_grid_attach( GTK_GRID(grid), frame_telefon1, 0, 8, 2, 1 );

    //Telefon2
    GtkWidget* frame_telefon2 = gtk_frame_new( "Telefon 2" );
    GtkWidget* entry_telefon2 = gtk_entry_new( );
    GtkEntryBuffer* entry_buffer_telefon2 = gtk_entry_get_buffer(
            GTK_ENTRY(entry_telefon2) );
    gtk_container_add( GTK_CONTAINER(frame_telefon2), entry_telefon2 );
    gtk_grid_attach( GTK_GRID(grid), frame_telefon2, 0, 9, 2, 1 );

    //Telefon3
    GtkWidget* frame_telefon3 = gtk_frame_new( "Telefon 3" );
    GtkWidget* entry_telefon3 = gtk_entry_new( );
    GtkEntryBuffer* entry_buffer_telefon3 = gtk_entry_get_buffer(
            GTK_ENTRY(entry_telefon3) );
    gtk_container_add( GTK_CONTAINER(frame_telefon3), entry_telefon3 );
    gtk_grid_attach( GTK_GRID(grid), frame_telefon3, 0, 9, 2, 1 );

    //Fax
    GtkWidget* frame_fax = gtk_frame_new( "Fax" );
    GtkWidget* entry_fax = gtk_entry_new( );
    GtkEntryBuffer* entry_buffer_fax = gtk_entry_get_buffer(
            GTK_ENTRY(entry_fax) );
    gtk_container_add( GTK_CONTAINER(frame_fax), entry_fax );
    gtk_grid_attach( GTK_GRID(grid), frame_fax, 0, 10, 2, 1 );

    //EMail
    GtkWidget* frame_email = gtk_frame_new( "EMail" );
    GtkWidget* entry_email = gtk_entry_new( );
    GtkEntryBuffer* entry_buffer_email = gtk_entry_get_buffer(
            GTK_ENTRY(entry_email) );
    gtk_container_add( GTK_CONTAINER(frame_email), entry_email );
    gtk_grid_attach( GTK_GRID(grid), frame_email, 0, 11, 2, 1 );

    //Homepage
    GtkWidget* frame_homepage = gtk_frame_new( "Homepage" );
    GtkWidget* entry_homepage = gtk_entry_new( );
    GtkEntryBuffer* entry_buffer_homepage = gtk_entry_get_buffer(
            GTK_ENTRY(entry_homepage) );
    gtk_container_add( GTK_CONTAINER(frame_homepage), entry_homepage );
    gtk_grid_attach( GTK_GRID(grid), frame_homepage, 0, 12, 2, 1 );

    //IBAN
    GtkWidget* frame_iban = gtk_frame_new( "IBAN" );
    GtkWidget* entry_iban = gtk_entry_new( );
    GtkEntryBuffer* entry_buffer_iban = gtk_entry_get_buffer(
            GTK_ENTRY(entry_iban) );
    gtk_container_add( GTK_CONTAINER(frame_iban), entry_iban );
    gtk_grid_attach( GTK_GRID(grid), frame_iban, 0, 13, 1, 1 );

    //BIC
    GtkWidget* frame_bic = gtk_frame_new( "BIC" );
    GtkWidget* entry_bic = gtk_entry_new( );
    GtkEntryBuffer* entry_buffer_bic = gtk_entry_get_buffer(
            GTK_ENTRY(entry_bic) );
    gtk_container_add( GTK_CONTAINER(frame_bic), entry_bic );
    gtk_grid_attach( GTK_GRID(grid), frame_bic, 1, 13, 1, 1 );

    //Anrede
    GtkWidget* frame_anrede = gtk_frame_new( "Anrede" );
    GtkWidget* entry_anrede = gtk_entry_new( );
    GtkEntryBuffer* entry_buffer_anrede = gtk_entry_get_buffer(
            GTK_ENTRY(entry_anrede) );
    gtk_container_add( GTK_CONTAINER(frame_anrede), entry_anrede );
    gtk_grid_attach( GTK_GRID(grid), frame_anrede, 0, 14, 2, 1 );

    //Bemerkungen
    GtkWidget* frame_bemerkungen = gtk_frame_new( "Bemerkungen" );
    GtkWidget* swindow_bemerkungen = gtk_scrolled_window_new( NULL, NULL );
    GtkWidget* textview_bemerkungen = gtk_text_view_new( );
    GtkTextBuffer* textview_buffer_bemerkungen = gtk_text_view_get_buffer(
            GTK_TEXT_VIEW(textview_bemerkungen) );
    gtk_text_view_set_wrap_mode( GTK_TEXT_VIEW(textview_bemerkungen), GTK_WRAP_WORD );
    gtk_text_view_set_accepts_tab( GTK_TEXT_VIEW(textview_bemerkungen), FALSE );
    gtk_container_add( GTK_CONTAINER(frame_bemerkungen), swindow_bemerkungen );
    gtk_container_add( GTK_CONTAINER(swindow_bemerkungen), textview_bemerkungen );
    gtk_grid_attach( GTK_GRID(grid), frame_bemerkungen, 3, 4, 2, 8 );

    GtkWidget* button_ok = gtk_button_new_with_label(
            "OK" );
    gtk_grid_attach( GTK_GRID(grid), button_ok, 1, 16, 1, 1 );

    GtkWidget* button_speichern = gtk_button_new_with_label(
            "Speichern" );
    gtk_grid_attach( GTK_GRID(grid), button_speichern, 2, 16, 1, 1 );

    GtkWidget* button_abbrechen = gtk_button_new_with_label(
            "Abbrechen" );
    gtk_grid_attach( GTK_GRID(grid), button_abbrechen, 3, 17, 1, 1 );

/*
**  object mit Daten vollstopfen  */
    g_object_set_data( G_OBJECT(adressen_window), "sojus", sojus );

    g_object_set_data( G_OBJECT(adressen_window), "entry_adressnr", entry_adressnr );
    g_object_set_data( G_OBJECT(adressen_window), "button_neue_adresse",
            button_neue_adresse );

    g_object_set_data( G_OBJECT(adressen_window), "entry_adresszeile1", entry_adresszeile1 );
    g_object_set_data( G_OBJECT(adressen_window), "entry_titel", entry_titel );
    g_object_set_data( G_OBJECT(adressen_window), "entry_vorname", entry_vorname );
    g_object_set_data( G_OBJECT(adressen_window), "entry_name", entry_name );
    g_object_set_data( G_OBJECT(adressen_window), "entry_adresszusatz", entry_adresszusatz );
    g_object_set_data( G_OBJECT(adressen_window), "entry_strasse", entry_strasse );
    g_object_set_data( G_OBJECT(adressen_window), "entry_hausnr", entry_hausnr );
    g_object_set_data( G_OBJECT(adressen_window), "entry_plz", entry_plz );
    g_object_set_data( G_OBJECT(adressen_window), "entry_ort", entry_ort );
    g_object_set_data( G_OBJECT(adressen_window), "entry_land", entry_land );
    g_object_set_data( G_OBJECT(adressen_window), "entry_telefon1", entry_telefon1 );
    g_object_set_data( G_OBJECT(adressen_window), "entry_telefon2", entry_telefon2 );
    g_object_set_data( G_OBJECT(adressen_window), "entry_telefon3", entry_telefon3 );
    g_object_set_data( G_OBJECT(adressen_window), "entry_fax", entry_fax );
    g_object_set_data( G_OBJECT(adressen_window), "entry_email", entry_email );
    g_object_set_data( G_OBJECT(adressen_window), "entry_homepage", entry_homepage );
    g_object_set_data( G_OBJECT(adressen_window), "entry_iban", entry_iban );
    g_object_set_data( G_OBJECT(adressen_window), "entry_bic", entry_bic );
    g_object_set_data( G_OBJECT(adressen_window), "entry_anrede", entry_anrede );
    g_object_set_data( G_OBJECT(adressen_window), "textview_bemerkungen", textview_bemerkungen );

    g_object_set_data( G_OBJECT(adressen_window), "button_ok", button_ok );
    g_object_set_data( G_OBJECT(adressen_window), "button_speichern", button_speichern );

/*
**  Signale verknüpfen  */
    //entry_regnr
    g_signal_connect( entry_adressnr, "activate",
            G_CALLBACK(cb_entry_adressnr_activate), adressen_window );

    //button_neue_adresse
    g_signal_connect( button_neue_adresse, "clicked",
            G_CALLBACK(cb_button_neue_adresse_clicked), adressen_window );

    //entry_adresszeile1
    g_signal_connect_swapped( entry_adresszeile1, "activate",
            G_CALLBACK(gtk_widget_grab_focus), entry_titel );

    g_signal_connect_swapped( entry_buffer_adresszeile1, "deleted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );
    g_signal_connect_swapped( entry_buffer_adresszeile1, "inserted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );

    //entry_titel
    g_signal_connect_swapped( entry_titel, "activate",
            G_CALLBACK(gtk_widget_grab_focus), entry_vorname );

    g_signal_connect_swapped( entry_buffer_titel, "deleted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );
    g_signal_connect_swapped( entry_buffer_titel, "inserted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );

    //entry_adresszeile1
    g_signal_connect_swapped( entry_vorname, "activate",
            G_CALLBACK(gtk_widget_grab_focus), entry_name );

    g_signal_connect_swapped( entry_buffer_vorname, "deleted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );
    g_signal_connect_swapped( entry_buffer_vorname, "inserted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );

    //entry_name
    g_signal_connect_swapped( entry_name, "activate",
            G_CALLBACK(gtk_widget_grab_focus), entry_adresszusatz );

    g_signal_connect_swapped( entry_buffer_name, "deleted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );
    g_signal_connect_swapped( entry_buffer_name, "inserted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );

    //entry_adresszusatz
    g_signal_connect_swapped( entry_adresszusatz, "activate",
            G_CALLBACK(gtk_widget_grab_focus), entry_strasse );

    g_signal_connect_swapped( entry_buffer_adresszusatz, "deleted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );
    g_signal_connect_swapped( entry_buffer_adresszusatz, "inserted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );

    //entry_strasse
    g_signal_connect_swapped( entry_strasse, "activate",
            G_CALLBACK(gtk_widget_grab_focus), entry_hausnr );

    g_signal_connect_swapped( entry_buffer_strasse, "deleted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );
    g_signal_connect_swapped( entry_buffer_strasse, "inserted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );

    //entry_hausnr
    g_signal_connect_swapped( entry_hausnr, "activate",
            G_CALLBACK(gtk_widget_grab_focus), entry_plz );

    g_signal_connect_swapped( entry_buffer_hausnr, "deleted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );
    g_signal_connect_swapped( entry_buffer_hausnr, "inserted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );

    //entry_plz
    g_signal_connect_swapped( entry_plz, "activate",
            G_CALLBACK(gtk_widget_grab_focus), entry_ort );

    g_signal_connect_swapped( entry_buffer_plz, "deleted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );
    g_signal_connect_swapped( entry_buffer_plz, "inserted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );

    //entry_ort
    g_signal_connect_swapped( entry_ort, "activate",
            G_CALLBACK(gtk_widget_grab_focus), entry_land );

    g_signal_connect_swapped( entry_buffer_ort, "deleted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );
    g_signal_connect_swapped( entry_buffer_land, "inserted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );

    //entry_land
    g_signal_connect_swapped( entry_land, "activate",
            G_CALLBACK(gtk_widget_grab_focus), entry_telefon1 );

    g_signal_connect_swapped( entry_buffer_land, "deleted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );
    g_signal_connect_swapped( entry_buffer_land, "inserted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );

    //entry_telefon1
    g_signal_connect_swapped( entry_telefon1, "activate",
            G_CALLBACK(gtk_widget_grab_focus), entry_telefon2 );

    g_signal_connect_swapped( entry_buffer_telefon1, "deleted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );
    g_signal_connect_swapped( entry_buffer_telefon1, "inserted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );

    //entry_telefon2
    g_signal_connect_swapped( entry_telefon2, "activate",
            G_CALLBACK(gtk_widget_grab_focus), entry_telefon3 );

    g_signal_connect_swapped( entry_buffer_telefon2, "deleted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );
    g_signal_connect_swapped( entry_buffer_telefon2, "inserted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );

    //entry_telefon3
    g_signal_connect_swapped( entry_telefon3, "activate",
            G_CALLBACK(gtk_widget_grab_focus), entry_fax);

    g_signal_connect_swapped( entry_buffer_telefon3, "deleted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );
    g_signal_connect_swapped( entry_buffer_telefon3, "inserted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );

    //entry_fax
    g_signal_connect_swapped( entry_fax, "activate",
            G_CALLBACK(gtk_widget_grab_focus), entry_email);

    g_signal_connect_swapped( entry_buffer_fax, "deleted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );
    g_signal_connect_swapped( entry_buffer_fax, "inserted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );

    //entry_email
    g_signal_connect_swapped( entry_email, "activate",
            G_CALLBACK(gtk_widget_grab_focus), entry_homepage );

    g_signal_connect_swapped( entry_buffer_email, "deleted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );
    g_signal_connect_swapped( entry_buffer_email, "inserted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );

    //entry_homepage
    g_signal_connect_swapped( entry_homepage, "activate",
            G_CALLBACK(gtk_widget_grab_focus), entry_iban );

    g_signal_connect_swapped( entry_buffer_homepage, "deleted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );
    g_signal_connect_swapped( entry_buffer_homepage, "inserted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );

    //entry_iban
    g_signal_connect_swapped( entry_iban, "activate",
            G_CALLBACK(gtk_widget_grab_focus), entry_bic );

    g_signal_connect_swapped( entry_buffer_iban, "deleted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );
    g_signal_connect_swapped( entry_buffer_iban, "inserted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );

    //entry_bic
    g_signal_connect_swapped( entry_bic, "activate",
            G_CALLBACK(gtk_widget_grab_focus), entry_anrede );

    g_signal_connect_swapped( entry_buffer_bic, "deleted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );
    g_signal_connect_swapped( entry_buffer_bic, "inserted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );

    //entry_anrede
    g_signal_connect_swapped( entry_anrede, "activate",
            G_CALLBACK(gtk_widget_grab_focus), textview_bemerkungen );

    g_signal_connect_swapped( entry_buffer_anrede, "deleted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );
    g_signal_connect_swapped( entry_buffer_anrede, "inserted-text",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );

    //textview_bemerkungen
    g_signal_connect_swapped( textview_buffer_bemerkungen, "changed",
            G_CALLBACK(cb_adresse_geaendert), (gpointer) adressen_window );



    //button ok
    g_signal_connect( button_ok, "clicked", G_CALLBACK(cb_button_ok_adr_clicked),
            (gpointer) adressen_window );

    //button speichern
    g_signal_connect( button_speichern, "clicked",
            G_CALLBACK(cb_button_speichern_adr_clicked), (gpointer) adressen_window );

    //button abbrechen
    g_signal_connect( button_abbrechen, "clicked",
            G_CALLBACK(cb_button_abbrechen_adr_clicked), (gpointer) adressen_window );

    //X angeclickt
    g_signal_connect( adressen_window, "delete-event",
            G_CALLBACK(cb_window_adr_delete_event), NULL );

    return adressen_window;
}


void
adressenfenster_fuellen( GtkWidget* adressen_window, gint adressnr )
{
    if ( adressnr > 0 )
    {
    /*
    **  Adresse holen  */
        Adresse* adresse = NULL;
        if ( !(adresse = adresse_oeffnen( adressen_window, adressnr )) )
        {
            display_message( adressen_window, "Adresse_oeffnen:\n "
                    "Kein Datensatz vorhanden", NULL );
            gtk_widget_destroy( adressen_window );

            return;
        }

    /*
    **  Aktenfenster mit Inhalten füllen*/
        gchar* text = NULL;

        //entry_adressnr
        text = g_strdup_printf( "%i", adressnr );
        gtk_entry_set_text( GTK_ENTRY(g_object_get_data( G_OBJECT(adressen_window),
                "entry_adressnr" )), text );
        g_free( text );

        //entry_adresszeile1
        gtk_entry_set_text( GTK_ENTRY(g_object_get_data( G_OBJECT(adressen_window),
                "entry_adresszeile1" )), adresse->adresszeile1 );

        //entry_titel
        gtk_entry_set_text( GTK_ENTRY(g_object_get_data( G_OBJECT(adressen_window),
                "entry_titel" )), adresse->titel );

        //entry_vorname
        gtk_entry_set_text( GTK_ENTRY(g_object_get_data( G_OBJECT(adressen_window),
                "entry_vorname" )), adresse->vorname );

        //entry_name
        gtk_entry_set_text( GTK_ENTRY(g_object_get_data( G_OBJECT(adressen_window),
                "entry_name" )), adresse->name );

        //entry_adresszusatz
        gtk_entry_set_text( GTK_ENTRY(g_object_get_data( G_OBJECT(adressen_window),
                "entry_adresszusatz" )), adresse->adresszusatz );

        //entry_strasse
        gtk_entry_set_text( GTK_ENTRY(g_object_get_data( G_OBJECT(adressen_window),
                "entry_strasse" )), adresse->strasse );

        //entry_hausnr
        gtk_entry_set_text( GTK_ENTRY(g_object_get_data( G_OBJECT(adressen_window),
                "entry_hausnr" )), adresse->hausnr);

        //entry_plz
        gtk_entry_set_text( GTK_ENTRY(g_object_get_data( G_OBJECT(adressen_window),
                "entry_plz" )), adresse->plz );

        //entry_ort
        gtk_entry_set_text( GTK_ENTRY(g_object_get_data( G_OBJECT(adressen_window),
                "entry_ort" )), adresse->ort );

        //entry_land
        gtk_entry_set_text( GTK_ENTRY(g_object_get_data( G_OBJECT(adressen_window),
                "entry_land" )), adresse->land );

        //entry_telefon1
        gtk_entry_set_text( GTK_ENTRY(g_object_get_data( G_OBJECT(adressen_window),
                "entry_telefon1" )), adresse->telefon1 );

        //entry_telefon2
        gtk_entry_set_text( GTK_ENTRY(g_object_get_data( G_OBJECT(adressen_window),
                "entry_telefon2" )), adresse->telefon2);

        //entry_telefon3
        gtk_entry_set_text( GTK_ENTRY(g_object_get_data( G_OBJECT(adressen_window),
                "entry_telefon3" )), adresse->telefon3 );

        //entry_fax
        gtk_entry_set_text( GTK_ENTRY(g_object_get_data( G_OBJECT(adressen_window),
                "entry_fax" )), adresse->fax );

        //entry_email
        gtk_entry_set_text( GTK_ENTRY(g_object_get_data( G_OBJECT(adressen_window),
                "entry_email" )), adresse->email );

        //entry_homepage
        gtk_entry_set_text( GTK_ENTRY(g_object_get_data( G_OBJECT(adressen_window),
                "entry_homepage" )), adresse->homepage );

        //entry_iban
        gtk_entry_set_text( GTK_ENTRY(g_object_get_data( G_OBJECT(adressen_window),
                "entry_iban" )), adresse->iban );

        //entry_bic
        gtk_entry_set_text( GTK_ENTRY(g_object_get_data( G_OBJECT(adressen_window),
                "entry_bic" )), adresse->bic );

        //entry_anrede
        gtk_entry_set_text( GTK_ENTRY(g_object_get_data( G_OBJECT(adressen_window),
                "entry_anrede" )), adresse->anrede );

        //textview_buffer_bemerkugen
        GtkTextBuffer* textview_buffer_bemerkungen = gtk_text_view_get_buffer(
                g_object_get_data( G_OBJECT(adressen_window),
                "textview_bemerkungen" ) );
        gtk_text_buffer_set_text( textview_buffer_bemerkungen, adresse->bemerkungen,
                -1 );
    /*
    **  Adresse freigeben  */
        adresse_free( adresse );
    }
    else gtk_entry_set_text( GTK_ENTRY(g_object_get_data( G_OBJECT(adressen_window),
                "entry_adressnr" )), "- neu -" );

/*
**  Widgets einstellen  */
    widgets_adresse_waehlen( G_OBJECT(adressen_window), FALSE );

    widgets_adresse_geaendert( G_OBJECT(adressen_window), FALSE );

    gtk_widget_grab_focus( GTK_WIDGET(g_object_get_data( G_OBJECT(adressen_window),
            "entry_adresszeile1" )) ) ;

    return;
}
