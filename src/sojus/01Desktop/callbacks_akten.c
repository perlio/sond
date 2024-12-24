#include "../globals.h"

void cb_button_aktenfenster_clicked(GtkButton *button, gpointer user_data) {
	GtkWidget *akten_window = aktenfenster_oeffnen((Sojus*) user_data);

	widgets_akte_bearbeiten(G_OBJECT(akten_window), FALSE, FALSE);
	widgets_aktenbeteiligte_bearbeiten(G_OBJECT(akten_window), FALSE, FALSE);
	widgets_aktenbeteiligte_geaendert(G_OBJECT(akten_window), FALSE);
	widgets_akte_waehlen(G_OBJECT(akten_window), TRUE);
	widgets_akte_geaendert(G_OBJECT(akten_window), FALSE);

	gtk_widget_grab_focus(
			GTK_WIDGET(
					g_object_get_data(G_OBJECT(akten_window), "entry_regnr")));

	gtk_widget_show_all(akten_window);

	return;
}

void cb_button_akte_suchen_clicked(GtkButton *button, gpointer sojus) {

	return;
}

