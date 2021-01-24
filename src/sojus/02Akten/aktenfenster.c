#include "../globals.h"

#include "../../misc.h"

#include "../00misc/auswahl.h"


gboolean
cb_window_delete_event( GtkWidget* window, GdkEvent* event, gpointer user_data )
{
    GtkWidget* button_speichern = g_object_get_data( G_OBJECT(window),
            "button_speichern" );
    if ( gtk_widget_get_sensitive( button_speichern ) )
    {
        gint rc = 0;
        rc = ask_question( (GtkWidget*) window, "Änderungen speichern?", "Ja",
                "Nein" );

        if ( rc == GTK_RESPONSE_CANCEL ) return TRUE;
        if ( rc == GTK_RESPONSE_YES ) akte_speichern( window );
    }

    GPtrArray* arr_aktenbet = g_object_get_data( G_OBJECT(window), "arr_aktenbet" );
    if ( arr_aktenbet ) g_ptr_array_free( arr_aktenbet, TRUE );

    return FALSE;
}


void
cb_button_abbrechen_clicked( GtkButton* button, gpointer user_data )
{
    GtkWidget* akten_window = (GtkWidget*) user_data;
    GtkWidget* button_speichern = g_object_get_data( G_OBJECT(akten_window),
            "button_speichern" );

    if ( gtk_widget_get_sensitive( button_speichern ) )
    {
        gint rc = 0;
        rc = ask_question( akten_window, "Änderungen speichern?", "Ja", "Nein" );

        if ( rc == GTK_RESPONSE_CANCEL ) return;
        if ( rc == GTK_RESPONSE_YES ) akte_speichern( akten_window );
    }

    GPtrArray* arr_aktenbet = g_object_get_data( G_OBJECT(akten_window),
            "arr_aktenbet" );
    if ( arr_aktenbet ) g_ptr_array_free( arr_aktenbet, TRUE );

    gtk_widget_destroy( akten_window );

    return;
}


void
cb_button_speichern_clicked( GtkButton* button, gpointer akten_window )
{
    akte_speichern( (GtkWidget*) akten_window );

    return;
}


void
cb_button_ok_clicked( GtkButton* button, gpointer user_data )
{
    GtkWidget* akten_window = (GtkWidget*) user_data;
    GtkWidget* button_speichern = g_object_get_data( G_OBJECT(akten_window),
            "button_speichern" );

    if ( gtk_widget_get_sensitive( button_speichern ) )
            akte_speichern( akten_window );

    GPtrArray* arr_aktenbet = g_object_get_data( G_OBJECT(akten_window),
            "arr_aktenbet" );
    if ( arr_aktenbet ) g_ptr_array_free( arr_aktenbet, TRUE );

    gtk_widget_destroy( akten_window );

    return;
}


void
cb_button_reakt_clicked( GtkButton* button, gpointer user_data )
{
    GtkWidget* akten_window = (GtkWidget*) user_data;
    GPtrArray* arr_aktenbet = g_object_get_data( G_OBJECT(akten_window),
            "arr_aktenbet" );
    Sojus* sojus = (Sojus*) g_object_get_data( G_OBJECT(akten_window), "sojus" );

    GtkWidget* entry_regnr = g_object_get_data( G_OBJECT(akten_window),
            "entry_regnr" );
    const gchar* regnr_text = gtk_entry_get_text( GTK_ENTRY(entry_regnr) );

    auswahl_parse_entry( akten_window, (gchar*) regnr_text );

    gint regnr = sojus->regnr_akt;
    gint jahr = sojus->jahr_akt;

    GtkWidget* label_ablagenr = g_object_get_data( G_OBJECT(akten_window),
            "label_ablagenr" );

    gchar* sql = g_strdup_printf( "UPDATE Akten SET Ablagenr='' WHERE "
            "RegNr=%i AND RegJahr=%i;", regnr, jahr );

    gint rc = mysql_query( sojus->db.con, sql );
    if ( rc )
    {
        display_message( akten_window, "Fehler bei cb_button_reakt:\n",
                sql, "mysql_query\n", mysql_error( sojus->db.con ), NULL );
        g_free( sql );
        return;
    }

    sql_log( sojus, sql );

    g_free( sql );

    //Änderungen darstellen
    gtk_label_set_text( GTK_LABEL(label_ablagenr), "" );

    widgets_akte_bearbeiten( G_OBJECT(akten_window), TRUE, TRUE );

    if ( arr_aktenbet->len == 0 ) widgets_aktenbeteiligte_vorhanden(
            G_OBJECT(akten_window), FALSE );

    return;
}


void
cb_button_ablegen_clicked( GtkButton* button, gpointer user_data )
{
    GtkWidget* akten_window = (GtkWidget*) user_data;
    Sojus* sojus = (Sojus*) g_object_get_data( G_OBJECT(akten_window), "sojus" );

    GtkWidget* entry_regnr = g_object_get_data( G_OBJECT(akten_window),
            "entry_regnr" );
    const gchar* regnr_text = gtk_entry_get_text( GTK_ENTRY(entry_regnr) );

    auswahl_parse_entry( akten_window, (gchar*) regnr_text );

    gint regnr = sojus->regnr_akt;
    gint jahr = sojus->jahr_akt;

    GtkWidget* label_ablagenr = g_object_get_data( G_OBJECT(akten_window),
            "label_ablagenr" );

    //Prüfen, ob Änderungen
    GtkWidget* button_speichern = g_object_get_data( G_OBJECT(akten_window),
            "button_speichern" );
    if ( gtk_widget_get_sensitive( button_speichern ) )
    {
        gint rc = 0;
        rc = ask_question( akten_window, "Vor Ablage Änderungen speichern?", "Ja",
                "Nein" );

        if ( rc == GTK_RESPONSE_CANCEL ) return;
        if ( rc == GTK_RESPONSE_YES ) akte_speichern( akten_window );
    }

    //Akte ablegen
    gchar* sql = g_strdup_printf( "UPDATE Akten SET Ablagenr=NOW(3) WHERE "
            "RegNr=%i AND RegJahr=%i;", regnr, jahr );

    gint rc = mysql_query( sojus->db.con, sql );
    if ( rc )
    {
        display_message( akten_window, "Fehler bei cb_button_ablegen:\n",
                sql, "mysql_query\n", mysql_error( sojus->db.con ), NULL );
        g_free( sql );
        return;
    }

    sql_log( sojus, sql );

    g_free( sql );

    //Änderungen (Ablagenr) darstellen
    sql = g_strdup_printf( "SELECT Ablagenr FROM Akten WHERE RegNr=%i AND "
            "RegJahr=%i;", regnr, jahr );

    rc = mysql_query( sojus->db.con, sql );
    if ( rc )
    {
        display_message( akten_window, "Fehler bei cb_button_ablegen:\n",
                sql, "mysql_query\n", mysql_error( sojus->db.con ), NULL );
        g_free( sql );
        return;
    }
    g_free( sql );

    MYSQL_RES* mysql_res = mysql_store_result( sojus->db.con );
    if ( !mysql_res )
    {
        display_message( akten_window, "Fehler bei cb_button_ablegen:\n",
                "mysql_store_res:\n", mysql_error( sojus->db.con ), NULL );
        return;
    }

    MYSQL_ROW row = NULL;
    row = mysql_fetch_row( mysql_res );

    gtk_label_set_text( GTK_LABEL(label_ablagenr), row[0] );

    mysql_free_result( mysql_res );

    widgets_akte_bearbeiten( G_OBJECT(akten_window), TRUE, FALSE );


    return;
}


void
cb_button_aktenbet_aendern_clicked( GtkButton* button, gpointer user_data )
{
    GtkWidget* akten_window = (GtkWidget*) user_data;

    //Änderungs-Zustand zwischenspeichern
    if ( gtk_widget_get_sensitive( (GtkWidget*) g_object_get_data(
            G_OBJECT(akten_window), "button_speichern" ) ) ) g_object_set_data(
            G_OBJECT(akten_window), "geaendert", GINT_TO_POINTER(1) );
    else g_object_set_data( G_OBJECT(akten_window), "geaendert", NULL );

    widgets_aktenbeteiligte_bearbeiten( G_OBJECT(akten_window), TRUE, FALSE );
    widgets_akte_bearbeiten( G_OBJECT(akten_window), FALSE, FALSE );

    return;
}


void
cb_button_aktenbet_hinzu_clicked( GtkButton* button, gpointer user_data )
{
    GtkWidget* akten_window = (GtkWidget*) user_data;

    GtkWidget* listbox_aktenbet = g_object_get_data( G_OBJECT(akten_window),
            "listbox_aktenbet" );
    GPtrArray* arr_aktenbet = (GPtrArray*) g_object_get_data(
            G_OBJECT(akten_window), "arr_aktenbet" );

    //neuen Aktenbet erzeugen
    Aktenbet* aktenbet = aktenbet_neu( );

    //Index des aktuell angezeigten Aktenbet herausfinden
    gint index = -1;
    GtkWidget* row = (GtkWidget*) gtk_list_box_get_selected_row(
            GTK_LIST_BOX(listbox_aktenbet) );
    if ( row ) index = gtk_list_box_row_get_index( GTK_LIST_BOX_ROW(row) );

    //neue row erzeugen, einfügen und anzeigen
    GtkWidget* new_row = gtk_list_box_row_new( );
    gtk_list_box_insert( GTK_LIST_BOX(listbox_aktenbet), new_row, index + 1 );

    GtkWidget* label_aktenbet = gtk_label_new( "Neu" );
    gtk_label_set_xalign( GTK_LABEL(label_aktenbet), 0.02 );
    gtk_container_add( GTK_CONTAINER(new_row), label_aktenbet );

    //an row anhängen
    g_object_set_data( G_OBJECT(new_row), "aktenbet", aktenbet );

    //in array einfügen
    g_ptr_array_add( arr_aktenbet, (gpointer) aktenbet );

    gtk_list_box_select_row( GTK_LIST_BOX(listbox_aktenbet), GTK_LIST_BOX_ROW(new_row) );

    gtk_widget_show_all( new_row );

    widgets_aktenbeteiligte_vorhanden( G_OBJECT(akten_window), TRUE );

    //Änderungs-Zustand zwischenspeichern
    if ( gtk_widget_get_sensitive( (GtkWidget*) g_object_get_data(
            G_OBJECT(akten_window), "button_speichern" ) ) ) g_object_set_data(
            G_OBJECT(akten_window), "geaendert", GINT_TO_POINTER(1) );
    else g_object_set_data( G_OBJECT(akten_window), "geaendert", NULL );

    widgets_akte_bearbeiten( G_OBJECT(akten_window), FALSE, FALSE );
    widgets_akte_geaendert( G_OBJECT(akten_window), FALSE );

    widgets_aktenbeteiligte_bearbeiten( G_OBJECT(akten_window), TRUE, TRUE );

    gtk_widget_grab_focus(GTK_WIDGET(g_object_get_data( G_OBJECT(akten_window),
            "entry_adressnr" )) );

    return;
}


void
cb_button_aktenbet_loeschen_clicked( GtkButton* button, gpointer user_data )
{
    GtkWidget* akten_window = (GtkWidget*) user_data;

    gint index = 0;

    GtkListBox* listbox = g_object_get_data( G_OBJECT(akten_window),
            "listbox_aktenbet" );

    GtkWidget* row = (GtkWidget*) gtk_list_box_get_selected_row( listbox );

    //jetzt kann adressnr aus array auf 0 gesetzt werden
    Aktenbet* aktenbet = g_object_get_data( G_OBJECT(row), "aktenbet" );
    aktenbet->adressnr = 0;

    //Zeile löschen
    gtk_widget_destroy( row );

    //Selection auf verbleibende row setzen
    while ( (index >= 0) && !gtk_list_box_get_row_at_index(
            GTK_LIST_BOX(listbox), index ) ) index--;
    if ( index >= 0 ) gtk_list_box_select_row( listbox,
            gtk_list_box_get_row_at_index( listbox, index ) );
    else
    {
        aktenbetwidgets_leeren( akten_window );
        widgets_aktenbeteiligte_vorhanden( G_OBJECT(akten_window), FALSE );
    }

    widgets_akte_geaendert( G_OBJECT(akten_window), TRUE );

    return;
}


void
cb_listbox_aktenbet_geaendert( GtkListBox* listbox, GtkListBoxRow* row, gpointer
        akten_window )
{
    Aktenbet* aktenbet = NULL;

    if ( row )
    {
        aktenbet = g_object_get_data( G_OBJECT(row), "aktenbet" );
        aktenbetwidgets_fuellen( (GtkWidget*) akten_window, aktenbet );
    }

    return;
}


void
cb_akte_geaendert( gpointer window )
{
    widgets_akte_geaendert( (GObject*) window, TRUE );

    return;
}


void
cb_button_neue_akte_clicked( GtkButton* button, gpointer user_data )
{
    GtkWidget* akten_window = (GtkWidget*) user_data;

    aktenfenster_fuellen( akten_window, -1, -1 );

    return;
}


void
cb_entry_regnr_activate( GtkEntry* entry, gpointer user_data )
{
    GtkWidget* akten_window = (GtkWidget*) user_data;
    Sojus* sojus = g_object_get_data( G_OBJECT(akten_window), "sojus" );

    if ( auswahl_regnr_ist_wohlgeformt( gtk_entry_get_text( entry ) ) )
    {
        gint regnr = 0;
        gint jahr = 0;

        auswahl_parse_regnr( gtk_entry_get_text( entry ), &regnr, &jahr );

        aktenfenster_fuellen( gtk_widget_get_toplevel( GTK_WIDGET(entry) ), regnr, jahr );

        return;
    }

    gboolean ret = auswahl_get_regnr_akt( sojus, entry );

    if ( ret ) aktenfenster_fuellen( akten_window, sojus->regnr_akt,
            sojus->jahr_akt );
    else gtk_widget_grab_focus( GTK_WIDGET(entry) );

    return;
}


GtkWidget*
aktenfenster_oeffnen( Sojus* sojus )
{
    GtkWidget* akten_window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_default_size( GTK_WINDOW(akten_window), 500, 300 );

    GtkWidget* akten_headerbar = gtk_header_bar_new( );
    gtk_header_bar_set_show_close_button( GTK_HEADER_BAR(akten_headerbar), TRUE );
    gtk_header_bar_set_title( GTK_HEADER_BAR(akten_headerbar), "Aktenfenster" );
    gtk_window_set_titlebar( GTK_WINDOW(akten_window), akten_headerbar );

    GtkWidget* grid = gtk_grid_new( );
    gtk_container_add( GTK_CONTAINER(akten_window), grid );

    //RegNr
    GtkWidget* frame_regnr = gtk_frame_new( "Register-Nr." );
    GtkWidget* entry_regnr = gtk_entry_new( );
    if ( (sojus->regnr_akt) && (sojus->jahr_akt) )
    {
        gchar* text_regnr = g_strdup_printf( "%i/%i", sojus->regnr_akt,
                sojus->jahr_akt % 100 );
        gtk_entry_set_text( GTK_ENTRY(entry_regnr), text_regnr );
        g_free( text_regnr );
    }

    gtk_container_add( GTK_CONTAINER(frame_regnr), entry_regnr );
    gtk_header_bar_pack_start( GTK_HEADER_BAR(akten_headerbar), frame_regnr );

    //Button Neue Akte
    GtkWidget* button_neue_akte =gtk_button_new_with_label( "Neue Akte" );
    gtk_header_bar_pack_start( GTK_HEADER_BAR(akten_headerbar), button_neue_akte );

    //Anlagedatum
    GtkWidget* label_anlagedatum = gtk_label_new( "angelegt:" );
    gtk_label_set_xalign( GTK_LABEL(label_anlagedatum), 0.02 );
    gtk_grid_attach( GTK_GRID(grid), label_anlagedatum, 1, 0, 1, 1 );

    //Ablagenr
    GtkWidget* label_ablagenr = gtk_label_new( "Ablagenr.: " );
    gtk_label_set_xalign( GTK_LABEL(label_ablagenr), 0.02 );
    gtk_grid_attach( GTK_GRID(grid), label_ablagenr, 2, 0, 1, 1 );

    //Aktenbezeichnung
    GtkWidget* frame_bezeichnung = gtk_frame_new( "Aktenbezeichnung" );
    GtkWidget* entry_bezeichnung = gtk_entry_new( );
    gtk_container_add( GTK_CONTAINER(frame_bezeichnung), entry_bezeichnung );
    gtk_grid_attach( GTK_GRID(grid), frame_bezeichnung, 0, 1, 1, 1 );

    //Gegenstand
    GtkWidget* frame_gegenstand = gtk_frame_new( "Gegenstand" );
    GtkWidget* entry_gegenstand = gtk_entry_new( );
    gtk_container_add( GTK_CONTAINER(frame_gegenstand), entry_gegenstand );
    gtk_grid_attach( GTK_GRID(grid), frame_gegenstand, 0, 2, 1, 1 );

    //Sachgebiet
    GtkWidget* frame_sachgebiets_id = gtk_frame_new( "Sachgebiet" );
    GtkWidget* combo_sachgebiete = gtk_combo_box_text_new( );
    for ( gint i = 0; i < sojus->sachgebiete->len; i++ )
            gtk_combo_box_text_append_text( GTK_COMBO_BOX_TEXT(combo_sachgebiete),
            g_ptr_array_index( sojus->sachgebiete, i ) );
    gtk_container_add( GTK_CONTAINER(frame_sachgebiets_id), combo_sachgebiete );
    gtk_grid_attach( GTK_GRID(grid), frame_sachgebiets_id, 1, 1, 1, 1 );

    //Sachbearbeiter
    GtkWidget* frame_sachbearbeiter_id = gtk_frame_new( "Sachbearbeiter" );
    GtkWidget* combo_sachbearbeiter = gtk_combo_box_text_new( );
    for ( gint i = 0; i < sojus->sachbearbeiter->len; i++ )
            gtk_combo_box_text_append_text( GTK_COMBO_BOX_TEXT(combo_sachbearbeiter),
            g_ptr_array_index( sojus->sachbearbeiter, i ) );
    gtk_container_add( GTK_CONTAINER(frame_sachbearbeiter_id), combo_sachbearbeiter );
    gtk_grid_attach( GTK_GRID(grid), frame_sachbearbeiter_id, 1, 2, 1, 1 );

    //Ablage
    GtkWidget* button_ablegen = gtk_button_new_with_label( "Akte ablegen" );
    gtk_grid_attach( GTK_GRID(grid), button_ablegen, 3, 1, 1, 1 );
    GtkWidget* button_reakt = gtk_button_new_with_label( "Akte reaktivieren" );
    gtk_grid_attach( GTK_GRID(grid), button_reakt, 3, 2, 1, 1 );

    //Aktenbeteiligte
    GtkWidget* button_aktenbet_hinzu = gtk_button_new_with_label(
            "Aktenbeteiligten hinzufügen" );
    gtk_grid_attach( GTK_GRID(grid), button_aktenbet_hinzu, 0, 3, 2, 1 );

    GtkWidget* swindow_aktenbet = gtk_scrolled_window_new( NULL, NULL );
    GtkWidget* listbox_aktenbet = gtk_list_box_new( );
    gtk_list_box_set_selection_mode( GTK_LIST_BOX(listbox_aktenbet),
            GTK_SELECTION_BROWSE );
    gtk_container_add( GTK_CONTAINER(swindow_aktenbet), listbox_aktenbet );
    gtk_grid_attach( GTK_GRID(grid), swindow_aktenbet, 0, 4, 2, 6 );

    GtkWidget* button_aktenbet_aendern = gtk_button_new_with_label(
            "Aktenbeteiligten ändern" );
    gtk_grid_attach( GTK_GRID(grid), button_aktenbet_aendern, 0, 10, 1, 1 );

    GtkWidget* button_aktenbet_loeschen = gtk_button_new_with_label(
            "Aktenbeteiligten löschen" );
    gtk_grid_attach( GTK_GRID(grid), button_aktenbet_loeschen, 1, 10, 1, 1 );

    GtkWidget* button_ok = gtk_button_new_with_label(
            "OK" );
    gtk_grid_attach( GTK_GRID(grid), button_ok, 1, 12, 1, 1 );

    GtkWidget* button_speichern = gtk_button_new_with_label(
            "Speichern" );
    gtk_grid_attach( GTK_GRID(grid), button_speichern, 2, 12, 1, 1 );

    GtkWidget* button_abbrechen = gtk_button_new_with_label(
            "Abbrechen" );
    gtk_grid_attach( GTK_GRID(grid), button_abbrechen, 3, 12, 1, 1 );

/*
**  object mit Daten vollstopfen  */
    GPtrArray* arr_aktenbet = g_ptr_array_new_with_free_func(
            (GDestroyNotify) aktenbet_free );
    g_object_set_data( G_OBJECT(akten_window), "arr_aktenbet", arr_aktenbet );

    g_object_set_data( G_OBJECT(akten_window), "sojus", sojus );

    g_object_set_data( G_OBJECT(akten_window), "entry_regnr", entry_regnr );
    g_object_set_data( G_OBJECT(akten_window), "button_neue_akte", button_neue_akte );

    g_object_set_data( G_OBJECT(akten_window), "grid", grid );

    g_object_set_data( G_OBJECT(akten_window), "label_anlagedatum",
            label_anlagedatum );
    g_object_set_data( G_OBJECT(akten_window), "label_ablagenr", label_ablagenr );
    g_object_set_data( G_OBJECT(akten_window), "entry_bezeichnung", entry_bezeichnung );
    g_object_set_data( G_OBJECT(akten_window), "entry_gegenstand", entry_gegenstand );
    g_object_set_data( G_OBJECT(akten_window), "combo_sachgebiete", combo_sachgebiete );
    g_object_set_data( G_OBJECT(akten_window), "combo_sachbearbeiter",
            combo_sachbearbeiter );
    g_object_set_data( G_OBJECT(akten_window), "button_ablegen", button_ablegen );
    g_object_set_data( G_OBJECT(akten_window), "button_reakt", button_reakt );
    g_object_set_data( G_OBJECT(akten_window), "listbox_aktenbet",
            listbox_aktenbet );
    g_object_set_data( G_OBJECT(akten_window), "button_hinzu",
            button_aktenbet_hinzu );
    g_object_set_data( G_OBJECT(akten_window), "button_aendern",
            button_aktenbet_aendern );
    g_object_set_data( G_OBJECT(akten_window), "button_loeschen",
            button_aktenbet_loeschen );
    g_object_set_data( G_OBJECT(akten_window), "button_ok", button_ok );
    g_object_set_data( G_OBJECT(akten_window), "button_speichern", button_speichern );
    g_object_set_data( G_OBJECT(akten_window), "button_abbrechen", button_abbrechen );

/*
**  Aktenbeteiligtenwidgets erstellen und mit akten_window verknüpfen  */
    aktenbetwidgets_oeffnen( akten_window );

/*
**  Signale verknüpfen  */
    //entry_regnr
    g_signal_connect( entry_regnr, "activate",
            G_CALLBACK(cb_entry_regnr_activate), akten_window );

    //button_neue_akte
    g_signal_connect( button_neue_akte, "clicked",
            G_CALLBACK(cb_button_neue_akte_clicked), akten_window );

    //entry_bezeichnung
    g_signal_connect_swapped( entry_bezeichnung, "activate",
            G_CALLBACK(gtk_widget_grab_focus), entry_gegenstand );

    //combo gegenstand
    g_signal_connect_swapped( entry_gegenstand, "activate",
            G_CALLBACK(gtk_widget_grab_focus), combo_sachgebiete );

    //anderer Aktenbeteiligter ausgewählt
    g_signal_connect( listbox_aktenbet, "row-selected",
            G_CALLBACK(cb_listbox_aktenbet_geaendert), (gpointer) akten_window );

    //button Aktenbeteiligte hinzufügen
    g_signal_connect( button_aktenbet_hinzu, "clicked",
            G_CALLBACK(cb_button_aktenbet_hinzu_clicked), (gpointer) akten_window );

    //button Aktenbeteiligte ändern
    g_signal_connect( button_aktenbet_aendern, "clicked",
            G_CALLBACK(cb_button_aktenbet_aendern_clicked),
            (gpointer) akten_window );

    //button Aktenbeteiligte löschen
    g_signal_connect( button_aktenbet_loeschen, "clicked",
            G_CALLBACK(cb_button_aktenbet_loeschen_clicked),
            (gpointer) akten_window );

    //button Akte ablegen
    g_signal_connect( button_ablegen, "clicked",
            G_CALLBACK(cb_button_ablegen_clicked), (gpointer) akten_window );

    //button Akte reaktivieren
    g_signal_connect( button_reakt, "clicked", G_CALLBACK(cb_button_reakt_clicked),
            (gpointer) akten_window );

    //button ok
    g_signal_connect( button_ok, "clicked", G_CALLBACK(cb_button_ok_clicked),
            (gpointer) akten_window );

    //button speichern
    g_signal_connect( button_speichern, "clicked",
            G_CALLBACK(cb_button_speichern_clicked), (gpointer) akten_window );

    //button abbrechen
    g_signal_connect( button_abbrechen, "clicked",
            G_CALLBACK(cb_button_abbrechen_clicked), (gpointer) akten_window );

    //X angeclickt
    g_signal_connect( akten_window, "delete-event",
            G_CALLBACK(cb_window_delete_event), NULL );

    return akten_window;
}


void
aktenfenster_fuellen( GtkWidget* akten_window, gint regnr, gint jahr )
{
    Sojus* sojus = (Sojus*) g_object_get_data( G_OBJECT(akten_window), "sojus" );

    if ( (regnr > 0) && (jahr > 0) && auswahl_regnr_existiert( akten_window, sojus->db.con, regnr, jahr ) )
    {
        GtkWidget* listbox_aktenbet = GTK_WIDGET(g_object_get_data(
                G_OBJECT(akten_window), "listbox_aktenbet" ));

    /*
    **  Akte holen  */
        Akte* akte = NULL;
        if ( !(akte = akte_oeffnen( sojus, regnr, jahr )) )
        {
            display_message( akten_window, "Akteninhalt ändern:\n "
                    "Kein Datensatz vorhanden", NULL );
            return;
        }

    /*
    **  Aktenfenster mit Inhalten füllen*/
        //regnr
        gchar* text = g_strdup_printf( "%i/%i", regnr, jahr % 100 );
        gtk_entry_set_text( GTK_ENTRY(g_object_get_data( G_OBJECT(akten_window),
                "entry_regnr" )), text );
        g_free( text );

        //Anlagedatum
        text = g_strdup_printf( "angelegt: %s", akte->anlagedatum );
        gtk_label_set_text( GTK_LABEL(g_object_get_data( G_OBJECT(akten_window),
                "label_anlagedatum" )), text );
        g_free( text );

        //Ablagenr
        text = g_strdup_printf( "Ablagenr.: %s", akte->ablagenr );
        gtk_label_set_text( GTK_LABEL(g_object_get_data( G_OBJECT(akten_window),
                "label_ablagenr" )), text );
        g_free( text );

        //Aktenbezeichnung
        gtk_entry_set_text( GTK_ENTRY(g_object_get_data( G_OBJECT(akten_window),
                "entry_bezeichnung" )), akte->bezeichnung );

        //Gegenstand
        gtk_entry_set_text( GTK_ENTRY(g_object_get_data( G_OBJECT(akten_window),
                "entry_gegenstand" )), akte->gegenstand);

        //Sachgebiet
        gtk_combo_box_set_active( GTK_COMBO_BOX(g_object_get_data(
                G_OBJECT(akten_window), "combo_sachgebiete")),
                allg_string_array_index_holen( sojus->sachgebiete,
                akte->sachgebiet ) );

        //Sachbearbeiter
        gtk_combo_box_set_active( GTK_COMBO_BOX(g_object_get_data(
                G_OBJECT(akten_window), "combo_sachbearbeiter")),
                allg_string_array_index_holen( sojus->sachbearbeiter,
                akte->sachbearbeiter_id ) );

        //Aktenbeteiligte
        GPtrArray* arr_aktenbet = aktenbet_oeffnen( akten_window, regnr, jahr );

        if ( arr_aktenbet->len > 0 )
        {
            for ( gint i = 0; i < arr_aktenbet->len; i++ )
            {
                Aktenbet* aktenbet = g_ptr_array_index( arr_aktenbet, i );

                GtkWidget* row = gtk_list_box_row_new( );
                GtkWidget* label_listbox = gtk_label_new( NULL );
                gtk_label_set_xalign( GTK_LABEL(label_listbox), 0.02 );
                g_object_set_data( G_OBJECT(row), "aktenbet", aktenbet );
                gtk_container_add( GTK_CONTAINER(row), label_listbox );
                gtk_list_box_insert( GTK_LIST_BOX(listbox_aktenbet), row, -1 );

                listboxlabel_fuellen( akten_window, row, aktenbet );
            }
            gtk_widget_show_all( listbox_aktenbet );

            gtk_list_box_select_row( GTK_LIST_BOX(listbox_aktenbet),
                    gtk_list_box_get_row_at_index( GTK_LIST_BOX(listbox_aktenbet), 0 ) );
            //aktenbetwidgets_fuellen muß hier nicht aufgerufen werden -
            //wird über cb erledigt
        }

    /*
    **  akte freigeben  */
        akte_free( akte );

    }
    else
    {
        if ( regnr > 0 && jahr > 0 )
        {
            gint rc = abfrage_frage( akten_window, "Akte exsitiert nicht", "Anlegen?", NULL );
            if ( rc == GTK_RESPONSE_NO )
            {
                gtk_widget_destroy( akten_window );

                return;
            }

            gchar* entry_text = g_strdup_printf( "%i/%i", regnr, jahr % 100 );
            gtk_entry_set_text( GTK_ENTRY(g_object_get_data( G_OBJECT(akten_window), "entry_regnr" )), entry_text );
            g_free( entry_text );
        }
        else gtk_entry_set_text( GTK_ENTRY(g_object_get_data( G_OBJECT(akten_window),
                "entry_regnr" )), "- neu -" );

        //Sachgebiet
        gtk_combo_box_set_active( GTK_COMBO_BOX(g_object_get_data(
                G_OBJECT(akten_window), "combo_sachgebiete")), 0 );
        //Sachbearbeiter
        gtk_combo_box_set_active( GTK_COMBO_BOX(g_object_get_data(
                G_OBJECT(akten_window), "combo_sachbearbeiter")), 0 );
    }

    /*
**  Speichern-Überwachung scharf schalten  */
    GtkEntryBuffer* entry_buffer_bezeichnung = gtk_entry_get_buffer( GTK_ENTRY(
            g_object_get_data( G_OBJECT(akten_window), "entry_bezeichnung" )) );
    g_signal_connect_swapped( entry_buffer_bezeichnung, "deleted-text",
            G_CALLBACK(cb_akte_geaendert), (gpointer) akten_window );
    g_signal_connect_swapped( entry_buffer_bezeichnung, "inserted-text",
            G_CALLBACK(cb_akte_geaendert), (gpointer) akten_window );

    GtkEntryBuffer* entry_buffer_gegenstand = gtk_entry_get_buffer( GTK_ENTRY(
            g_object_get_data( G_OBJECT(akten_window), "entry_gegenstand" )) );
    g_signal_connect_swapped( entry_buffer_gegenstand, "deleted-text",
            G_CALLBACK(cb_akte_geaendert), (gpointer) akten_window );
    g_signal_connect_swapped( entry_buffer_gegenstand, "inserted-text",
            G_CALLBACK(cb_akte_geaendert), (gpointer) akten_window );

    GtkWidget* combo_sachgebiete = (GtkWidget*) g_object_get_data( G_OBJECT(
            akten_window), "combo_sachgebiete" );
    g_signal_connect_swapped( combo_sachgebiete, "changed",
            G_CALLBACK(cb_akte_geaendert), (gpointer) akten_window );

    GtkWidget* combo_sachbearbeiter = (GtkWidget*) g_object_get_data( G_OBJECT(
            akten_window), "combo_sachbearbeiter" );
    g_signal_connect_swapped( combo_sachbearbeiter, "changed",
            G_CALLBACK(cb_akte_geaendert), (gpointer) akten_window );

/*
**  Widgets einstellen  */
    widgets_akte_waehlen( G_OBJECT(akten_window), FALSE );

    //Akte aktiv?
    const gchar* ablagenr_text = gtk_label_get_text( GTK_LABEL(g_object_get_data(
            G_OBJECT(akten_window), "label_ablagenr" )) );
    gboolean aktiv = !g_strcmp0( ablagenr_text, "Ablagenr.: " );
    widgets_akte_bearbeiten( G_OBJECT(akten_window), TRUE, aktiv );
    widgets_aktenbeteiligte_bearbeiten( G_OBJECT(akten_window), FALSE, FALSE );

    // Falls noch gar nicht angelegt: button_ablage ausgrauen
    const gchar* regnr_text = gtk_entry_get_text( GTK_ENTRY(g_object_get_data(
            G_OBJECT(akten_window), "entry_regnr" )) );
    if ( !g_strcmp0( regnr_text, "- neu -" ) ) gtk_widget_set_sensitive(
            GTK_WIDGET(g_object_get_data( G_OBJECT(akten_window),
            "button_ablegen" )), FALSE );

    GtkListBoxRow* row = gtk_list_box_get_row_at_index( g_object_get_data(
            G_OBJECT(akten_window), "listbox_aktenbet" ), 0 );
    if ( !row ) widgets_aktenbeteiligte_vorhanden( G_OBJECT(akten_window), FALSE );

    gtk_widget_grab_focus( GTK_WIDGET(g_object_get_data( G_OBJECT(akten_window),
            "entry_bezeichnung" )) ) ;

    return;
}


void
listboxlabel_fuellen( GtkWidget* akten_window, GtkWidget* row, Aktenbet* aktenbet )
{
    Sojus* sojus = (Sojus*) g_object_get_data( G_OBJECT(akten_window), "sojus" );

    GtkWidget* label_aktenbet = gtk_bin_get_child( GTK_BIN(row) );

    Adresse* adresse = NULL;
    if ( aktenbet->adressnr )   //Abfrage überflüssig, wenn nur "wohlgeformte"
                                //aktenbet (gültige Adressnr) im Umlauf
    {
        adresse = adresse_oeffnen( akten_window, aktenbet->adressnr );
        gchar* text = g_strconcat( g_ptr_array_index( sojus->beteiligtenart,
                allg_string_array_index_holen( sojus->beteiligtenart,
                aktenbet->betart ) ), ": ", adresse->adresszeile1, " ",
                adresse->vorname, " ", adresse->name, NULL );
        adresse_free( adresse );

        gtk_label_set_text( GTK_LABEL(label_aktenbet), text );
        g_free( text );
    }

    return;
}


