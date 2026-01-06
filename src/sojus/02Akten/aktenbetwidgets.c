#include "../globals.h"
#include "../global_types_sojus.h"

#include "../../misc.h"

void cb_button_verwerfen_clicked(GtkButton *button, gpointer user_data) {
	GtkWidget *akten_window = (GtkWidget*) user_data;
	GtkWidget *listbox_aktenbet = (GtkWidget*) g_object_get_data(
			G_OBJECT(akten_window), "listbox_aktenbet");

	//Abfrage, welcher Aktenbet gewählt ist  */
	GtkWidget *row = (GtkWidget*) gtk_list_box_get_selected_row(
			GTK_LIST_BOX(listbox_aktenbet));
	gint index = gtk_list_box_row_get_index(GTK_LIST_BOX_ROW(row));

	Aktenbet *aktenbet = (Aktenbet*) g_object_get_data(G_OBJECT(row),
			"aktenbet");

	widgets_aktenbeteiligte_bearbeiten((GObject*) akten_window, FALSE, FALSE);
	widgets_aktenbeteiligte_geaendert(G_OBJECT(akten_window), FALSE);
	widgets_akte_bearbeiten(G_OBJECT(akten_window), TRUE, TRUE);

	//wenn neu erzeugte row
	if (!(aktenbet->adressnr)) {
		gtk_widget_destroy(row);

		aktenbetwidgets_leeren(akten_window);

		//Selection auf verbleibende row setzen
		while ((index >= 0)
				&& !gtk_list_box_get_row_at_index(
						GTK_LIST_BOX(listbox_aktenbet), index))
			index--;

		if (index >= 0)
			gtk_list_box_select_row(GTK_LIST_BOX(listbox_aktenbet),
					gtk_list_box_get_row_at_index(
							GTK_LIST_BOX(listbox_aktenbet), index));
		else
			widgets_aktenbeteiligte_vorhanden(G_OBJECT(akten_window), FALSE);
	} else
		aktenbetwidgets_fuellen(akten_window, aktenbet);

	gpointer geaendert = g_object_get_data(G_OBJECT(akten_window), "geaendert");

	if (geaendert)
		widgets_akte_geaendert(G_OBJECT(akten_window), TRUE);

	return;
}

void cb_button_uebernehmen_clicked(GtkButton *button, gpointer user_data) {
	GtkWidget *akten_window = (GtkWidget*) user_data;
	GtkWidget *listbox_aktenbet = (GtkWidget*) g_object_get_data(
			G_OBJECT(akten_window), "listbox_aktenbet");

	//Abfrage, welcher Aktenbet gewählt ist  */
	GtkWidget *row = (GtkWidget*) gtk_list_box_get_selected_row(
			GTK_LIST_BOX(listbox_aktenbet));

	Aktenbet *aktenbet = aktenbet_einlesen(akten_window);
	listboxlabel_fuellen(akten_window, row, aktenbet);

	aktenbet->geaendert = TRUE;

	widgets_aktenbeteiligte_bearbeiten((GObject*) akten_window, FALSE, FALSE);
	widgets_aktenbeteiligte_geaendert(G_OBJECT(akten_window), FALSE);

	widgets_akte_bearbeiten(G_OBJECT(akten_window), TRUE, TRUE);
	widgets_akte_geaendert(G_OBJECT(akten_window), TRUE);

	return;
}

void cb_adressenfenster_destroy(GtkWidget *widget, gpointer user_data) {
	GtkWidget *akten_window = (GtkWidget*) user_data;
	Sojus *sojus = (Sojus*) g_object_get_data(G_OBJECT(akten_window), "sojus");

	GtkWidget *entry_adressnr = g_object_get_data(G_OBJECT(widget),
			"entry_adressnr");
	const gchar *entry_text = gtk_entry_get_text(GTK_ENTRY(entry_adressnr));
	if (!g_strcmp0(entry_text, "- neu -")) {
		widgets_aktenbeteiligte_bearbeiten(G_OBJECT(akten_window), TRUE, TRUE);
		return;
	}

	gchar *text = g_strdup_printf("%i", sojus->adressnr_akt);
	gtk_entry_set_text(
			GTK_ENTRY(
					g_object_get_data( G_OBJECT(akten_window), "entry_adressnr" )),
			text);
	g_free(text);

	widgets_aktenbeteiligte_bearbeiten(G_OBJECT(akten_window), TRUE, FALSE);
	widgets_aktenbeteiligte_geaendert(G_OBJECT(akten_window), TRUE);

	return;
}

void cb_button_aktenbet_neue_adresse_clicked(GtkButton *button,
		gpointer user_data) {
	GtkWidget *akten_window = (GtkWidget*) user_data;
	Sojus *sojus = (Sojus*) g_object_get_data(G_OBJECT(akten_window), "sojus");

	widgets_aktenbeteiligte_bearbeiten(G_OBJECT(akten_window), FALSE, FALSE);

	GtkWidget *adressen_window = adressenfenster_oeffnen(sojus);

	g_signal_connect(adressen_window, "destroy",
			G_CALLBACK(cb_adressenfenster_destroy), (gpointer ) akten_window);

	adressenfenster_fuellen(adressen_window, -1);

	gtk_widget_show_all(adressen_window);

	return;
}

void cb_entry_aktenbet_adressnr_activate(GtkEntry *entry, gpointer user_data) {
	GtkWidget *akten_window = (GtkWidget*) user_data;
	Sojus *sojus = (Sojus*) g_object_get_data(G_OBJECT(akten_window), "sojus");

	gchar *entry_text = NULL;
	entry_text = (gchar*) gtk_entry_get_text(entry);

	gboolean ret = parse_adressnr(akten_window, entry_text);

	if (ret)
		entry_text = g_strdup_printf("%i", sojus->adressnr_akt);
	else
		entry_text = g_strdup("");
	gtk_entry_set_text(entry, entry_text);
	g_free(entry_text);

	if (ret) {
		widgets_aktenbeteiligte_bearbeiten(G_OBJECT(akten_window), TRUE, FALSE);
		widgets_aktenbeteiligte_geaendert(G_OBJECT(akten_window), TRUE);

		gtk_widget_grab_focus(
				(GtkWidget*) g_object_get_data(G_OBJECT(akten_window),
						"combo_beteiligtenart"));
	} else
		gtk_widget_grab_focus(GTK_WIDGET(entry));

	return;
}

void aktenbetwidgets_oeffnen(GtkWidget *akten_window) {
	Sojus *sojus = (Sojus*) g_object_get_data(G_OBJECT(akten_window), "sojus");
	GtkWidget *grid = (GtkWidget*) g_object_get_data(G_OBJECT(akten_window),
			"grid");

	GtkWidget *frame_adressnr = gtk_frame_new("Adressnr.");
	GtkWidget *entry_adressnr = gtk_entry_new();
	gtk_container_add(GTK_CONTAINER(frame_adressnr), entry_adressnr);
	gtk_grid_attach(GTK_GRID(grid), frame_adressnr, 2, 5, 1, 1);

	GtkWidget *button_neue_adresse = gtk_button_new_with_label("Neue Adresse");
	gtk_grid_attach(GTK_GRID(grid), button_neue_adresse, 3, 5, 1, 1);

	GtkWidget *frame_beteiligtenart = gtk_frame_new("Beteiligtenart");
	GtkWidget *combo_beteiligtenart = gtk_combo_box_text_new();
	for (gint i = 0; i < sojus->beteiligtenart->len; i++)
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_beteiligtenart),
				g_ptr_array_index(sojus->beteiligtenart, i));
	gtk_container_add(GTK_CONTAINER(frame_beteiligtenart),
			combo_beteiligtenart);
	gtk_grid_attach(GTK_GRID(grid), frame_beteiligtenart, 2, 6, 1, 1);

	GtkWidget *frame_betreff1 = gtk_frame_new("Betreff 1");
	GtkWidget *entry_betreff1 = gtk_entry_new();
	gtk_container_add(GTK_CONTAINER(frame_betreff1), entry_betreff1);
	gtk_grid_attach(GTK_GRID(grid), frame_betreff1, 2, 7, 2, 1);

	GtkWidget *frame_betreff2 = gtk_frame_new("Betreff 2");
	GtkWidget *entry_betreff2 = gtk_entry_new();
	gtk_container_add(GTK_CONTAINER(frame_betreff2), entry_betreff2);
	gtk_grid_attach(GTK_GRID(grid), frame_betreff2, 2, 8, 2, 1);

	GtkWidget *frame_betreff3 = gtk_frame_new("Betreff 3");
	GtkWidget *entry_betreff3 = gtk_entry_new();
	gtk_container_add(GTK_CONTAINER(frame_betreff3), entry_betreff3);
	gtk_grid_attach(GTK_GRID(grid), frame_betreff3, 2, 9, 2, 1);

	GtkWidget *button_uebernehmen = gtk_button_new_with_label("Übernehmen");
	gtk_grid_attach(GTK_GRID(grid), button_uebernehmen, 2, 10, 1, 1);

	GtkWidget *button_verwerfen = gtk_button_new_with_label("Verwerfen");
	gtk_grid_attach(GTK_GRID(grid), button_verwerfen, 3, 10, 1, 1);

	g_object_set_data(G_OBJECT(akten_window), "entry_adressnr", entry_adressnr);
	g_object_set_data(G_OBJECT(akten_window), "button_neue_adresse",
			button_neue_adresse);

	g_object_set_data(G_OBJECT(akten_window), "combo_beteiligtenart",
			combo_beteiligtenart);
	g_object_set_data(G_OBJECT(akten_window), "entry_betreff1", entry_betreff1);
	g_object_set_data(G_OBJECT(akten_window), "entry_betreff2", entry_betreff2);
	g_object_set_data(G_OBJECT(akten_window), "entry_betreff3", entry_betreff3);

	g_object_set_data(G_OBJECT(akten_window), "button_uebernehmen",
			button_uebernehmen);
	g_object_set_data(G_OBJECT(akten_window), "button_verwerfen",
			button_verwerfen);

	g_signal_connect(entry_adressnr, "activate",
			G_CALLBACK(cb_entry_aktenbet_adressnr_activate), (gpointer )
			akten_window);
	g_signal_connect(button_neue_adresse, "clicked",
			G_CALLBACK(cb_button_aktenbet_neue_adresse_clicked), (gpointer )
			akten_window);

	g_signal_connect(button_uebernehmen, "clicked",
			G_CALLBACK(cb_button_uebernehmen_clicked),
			(gpointer ) akten_window);

	g_signal_connect(button_verwerfen, "clicked",
			G_CALLBACK(cb_button_verwerfen_clicked), (gpointer ) akten_window);

	return;
}

void cb_aktenbet_geaendert(gpointer window) {
	widgets_aktenbeteiligte_geaendert((GObject*) window, TRUE);

	return;
}

void aktenbetwidgets_fuellen(GtkWidget *akten_window, Aktenbet *aktenbet) {
	Sojus *sojus = (Sojus*) g_object_get_data(G_OBJECT(akten_window), "sojus");

	gchar *text = NULL;

	//Adressnr
	if (aktenbet->adressnr)
		text = g_strdup_printf("%i", aktenbet->adressnr);
	else
		text = g_strdup("");

	gtk_entry_set_text(
			GTK_ENTRY(
					g_object_get_data( G_OBJECT(akten_window), "entry_adressnr" )),
			text);
	g_free(text);

	//Beteiligtenart
	gtk_combo_box_set_active(
			GTK_COMBO_BOX(
					g_object_get_data( G_OBJECT(akten_window), "combo_beteiligtenart")),
			find_string_in_array(sojus->beteiligtenart,
					aktenbet->betart));

	//Betreff1
	gtk_entry_set_text(
			GTK_ENTRY(
					g_object_get_data( G_OBJECT(akten_window), "entry_betreff1" )),
			aktenbet->betreff1);

	//Betreff2
	gtk_entry_set_text(
			GTK_ENTRY(
					g_object_get_data( G_OBJECT(akten_window), "entry_betreff2" )),
			aktenbet->betreff2);

	//Betreff3
	gtk_entry_set_text(
			GTK_ENTRY(
					g_object_get_data( G_OBJECT(akten_window), "entry_betreff3" )),
			aktenbet->betreff3);

	/*
	 **  Speichern-Überwachung scharf schalten  */
	GtkWidget *combo_beteiligtenart = (GtkWidget*) g_object_get_data(
			G_OBJECT(akten_window), "combo_beteiligtenart");
	g_signal_connect_swapped(combo_beteiligtenart, "changed",
			G_CALLBACK(cb_aktenbet_geaendert), (gpointer ) akten_window);

	GtkEntryBuffer *entry_buffer_betreff1 =
			gtk_entry_get_buffer(
					GTK_ENTRY(
							g_object_get_data( G_OBJECT(akten_window), "entry_betreff1" )));
	g_signal_connect_swapped(entry_buffer_betreff1, "deleted-text",
			G_CALLBACK(cb_aktenbet_geaendert), (gpointer ) akten_window);
	g_signal_connect_swapped(entry_buffer_betreff1, "inserted-text",
			G_CALLBACK(cb_aktenbet_geaendert), (gpointer ) akten_window);

	GtkEntryBuffer *entry_buffer_betreff2 =
			gtk_entry_get_buffer(
					GTK_ENTRY(
							g_object_get_data( G_OBJECT(akten_window), "entry_betreff2" )));
	g_signal_connect_swapped(entry_buffer_betreff2, "deleted-text",
			G_CALLBACK(cb_aktenbet_geaendert), (gpointer ) akten_window);
	g_signal_connect_swapped(entry_buffer_betreff2, "inserted-text",
			G_CALLBACK(cb_aktenbet_geaendert), (gpointer ) akten_window);

	GtkEntryBuffer *entry_buffer_betreff3 =
			gtk_entry_get_buffer(
					GTK_ENTRY(
							g_object_get_data( G_OBJECT(akten_window), "entry_betreff3" )));
	g_signal_connect_swapped(entry_buffer_betreff3, "deleted-text",
			G_CALLBACK(cb_aktenbet_geaendert), (gpointer ) akten_window);
	g_signal_connect_swapped(entry_buffer_betreff3, "inserted-text",
			G_CALLBACK(cb_aktenbet_geaendert), (gpointer ) akten_window);

	return;
}

void aktenbetwidgets_leeren(GtkWidget *akten_window) {
	//Adressnr
	gtk_entry_set_text(
			GTK_ENTRY(
					g_object_get_data( G_OBJECT(akten_window), "entry_adressnr" )),
			"");

	gtk_combo_box_set_active(
			GTK_COMBO_BOX(
					g_object_get_data( G_OBJECT( akten_window), "combo_beteiligtenart" )),
			-1);

	//Betreff1
	gtk_entry_set_text(
			GTK_ENTRY(
					g_object_get_data( G_OBJECT(akten_window), "entry_betreff1" )),
			"");

	//Betreff2
	gtk_entry_set_text(
			GTK_ENTRY(
					g_object_get_data( G_OBJECT(akten_window), "entry_betreff2" )),
			"");

	//Betreff3
	gtk_entry_set_text(
			GTK_ENTRY(
					g_object_get_data( G_OBJECT(akten_window), "entry_betreff3" )),
			"");

	return;
}

