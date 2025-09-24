#include <gtk/gtk.h>

#include "../sojus_init.h"
#include "../../misc.h"

#include "../20Einstellungen/db.h"
#include "../20Einstellungen/sachbearbeiterverwaltung.h"

void cb_button_sachbearbeiterverwaltung(GtkButton *button, gpointer user_data) {
	sachbearbeiterfenster_oeffnen((Sojus*) user_data);
	sachbearbeiterfenster_fuellen((Sojus*) user_data);

	return;

}

void cb_button_dokument_dir(GtkButton *button, gpointer data) {
	gchar *path = NULL;
	GSList *list = NULL;

	Sojus *sojus = (Sojus*) data;

	path = g_settings_get_string(sojus->settings, "dokument-dir");
/*
	list = choose_files(sojus->app_window, path,
			"Dokumentenverzeichnis auswählen", "Ok",
			GTK_FILE_CHOOSER_ACTION_CREATE_FOLDER, NULL, FALSE);
			*/
	if (list) {
		g_settings_set_string(sojus->settings, "dokument-dir", list->data);
		g_slist_free_full(list, g_free);
	}

	return;

}
