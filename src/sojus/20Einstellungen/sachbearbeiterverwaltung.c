#include "../globals.h"

void sachbearbeiter_oeffnen(GtkWidget *sb_window, gchar **errmsg) {
	Sojus *sojus = (Sojus*) g_object_get_data(G_OBJECT(sb_window), "sojus");
	/*
	 //Aktenbeteiligte
	 gchar* sql = g_strdup_printf( "SELECT * FROM Sachbearbeiter;");
	 gint rc = mysql_query( sojus->con, sql );
	 g_free( sql );
	 if ( rc )
	 {
	 *errmsg = g_strconcat( "Fehler bei sachbearbeiter_oeffnen\n",
	 "mysql_query:\n", mysql_error( sojus->con ), NULL );
	 return NULL;
	 }

	 MYSQL_RES* mysql_res = mysql_store_result( sojus->con );
	 if ( !mysql_res )
	 {
	 *errmsg = g_strconcat( "Fehler bei sachbearbeiter_oeffnen:\n",
	 "mysql_store_result:\n", mysql_error( sojus->con ), NULL );
	 return NULL;
	 }

	 GPtrArray* arr_sb = g_object_get_data( G_OBJECT(sb_window),
	 "arr_sb" );

	 MYSQL_ROW row = NULL;
	 while ( (row = mysql_fetch_row( mysql_res )) )
	 {
	 SB* sb = g_malloc0( sizeof( SB ) );
	 g_ptr_array_add( arr_sb, sb );

	 sb->sachbearbeiter_id = g_strdup( row[0] );
	 sb->name = g_strdup( row[1] );
	 }

	 mysql_free_result( mysql_res );
	 */
	return;
}

void sachbearbeiterfenster_oeffnen(Sojus *sojus) {
	GtkWidget *sb_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(sb_window), 400, 250);

	GtkWidget *sb_headerbar = gtk_header_bar_new();
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(sb_headerbar), TRUE);
	gtk_header_bar_set_title(GTK_HEADER_BAR(sb_headerbar),
			"Sachbearbeiterverwaltung");
	gtk_window_set_titlebar(GTK_WINDOW(sb_window), sb_headerbar);

	GtkWidget *grid = gtk_grid_new();
	gtk_container_add(GTK_CONTAINER(sb_window), grid);

	//Listbox und Buttons
	GtkWidget *button_sb_hinzu = gtk_button_new_with_label(
			"Sachbearbeiter hinzufügen");
	gtk_grid_attach(GTK_GRID(grid), button_sb_hinzu, 0, 3, 2, 1);

	GtkWidget *swindow_sb = gtk_scrolled_window_new(NULL, NULL);
	GtkWidget *listbox_sb = gtk_list_box_new();
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(listbox_sb),
			GTK_SELECTION_BROWSE);
	gtk_container_add(GTK_CONTAINER(swindow_sb), listbox_sb);
	gtk_grid_attach(GTK_GRID(grid), swindow_sb, 0, 4, 2, 6);

	GtkWidget *button_sb_loeschen = gtk_button_new_with_label(
			"Sachbearbeiter löschen");
	gtk_grid_attach(GTK_GRID(grid), button_sb_loeschen, 0, 10, 2, 1);

	//object vollstopfen
	g_object_set_data(G_OBJECT(sb_window), "sb_window", (gpointer) sojus);

	//Signale

	gtk_widget_show_all(sb_window);

	return;
}

void sachbearbeiterfenster_fuellen(Sojus *sojus) {
	//Sachbearbeiter holen

	return;
}
