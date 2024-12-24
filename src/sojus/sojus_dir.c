/*
 sojus (sojus_dir.c) - softkanzlei
 Copyright (C) 2023  pelo america

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU Affero General Public License as
 published by the Free Software Foundation, either version 3 of the
 License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU Affero General Public License for more details.

 You should have received a copy of the GNU Affero General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "global_types_sojus.h"
#include "sojus_init.h"

#include "../misc.h"

#include "../sond/sond_client/sond_client_misc.h"

#include <gtk/gtk.h>
#include <windows.h>
#include <shlwapi.h>

static void sojus_dir_fill_window(Sojus *sojus, GtkWidget *list_box,
		GArray *arr_hits) {
	for (gint i = 0; i < arr_hits->len; i++) {
		gint hit = 0;
		const gchar *dir_text = NULL;
		GtkWidget *label = NULL;
		GtkWidget *list_box_row = NULL;

		hit = g_array_index(arr_hits, gint, i);
		dir_text = g_ptr_array_index(sojus->arr_dirs, hit);

		label = gtk_label_new(dir_text);

		gtk_list_box_insert(GTK_LIST_BOX(list_box), label, -1);
		list_box_row = gtk_widget_get_parent(label);

		g_object_set_data(G_OBJECT(list_box_row), "hit", GINT_TO_POINTER(hit));
	}

	gtk_widget_show_all(list_box);

	return;
}

static void sojus_dir_cb_delete_event(GtkWidget *window, GdkEvent *event,
		gpointer data) {
	gtk_widget_destroy(window);

	return;
}

static void sojus_dir_cb_row_activated(GtkWidget *listbox, GtkWidget *row,
		gpointer user_data) {
	gint hit = 0;
	HINSTANCE ret = 0;
	gchar *path = NULL;
	gboolean ret_place = FALSE;

	Sojus *sojus = (Sojus*) user_data;

	hit = GPOINTER_TO_INT(g_object_get_data( G_OBJECT(row), "hit" ));

	//Verzeichnis öffnen
	path = g_build_filename(sojus->root,
			g_ptr_array_index(sojus->arr_dirs, hit), NULL);

	/*
	 //utf8 in filename konvertieren
	 gsize written;
	 gchar* charset = g_get_codeset();
	 gchar* local_filename = g_convert( path_win32, -1, charset, "UTF-8", NULL, &written,
	 NULL );
	 g_free( charset );

	 g_free( path_win32 );
	 */

	ret = ShellExecute( NULL, NULL, path, NULL, NULL, SW_SHOWNORMAL);
	g_free(path);
	if (ret == (HINSTANCE) 31) //no app associated
		display_message(sojus->app_window, "Bei Aufruf ShellExecute:\n"
				"Keine Anwendung mit Datei verbunden", NULL);
	else if (ret <= (HINSTANCE) 32)
		display_message(sojus->app_window,
				"Bei Aufruf ShellExecute:\nErrCode: %p", ret);

	g_signal_emit_by_name(gtk_widget_get_toplevel(listbox), "delete-event",
			NULL, &ret_place);

	return;
}

static void sojus_dir_show_hits(Sojus *sojus, GArray *arr_hits) {
	//Fenster erzeugen
	GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(window), 300, 100);
	gtk_window_set_transient_for(GTK_WINDOW(window),
			GTK_WINDOW(sojus->app_window));

	GtkWidget *swindow = gtk_scrolled_window_new( NULL, NULL);
	GtkWidget *list_box = gtk_list_box_new();
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(list_box),
			GTK_SELECTION_SINGLE);
	gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(list_box), FALSE);

	gtk_container_add(GTK_CONTAINER(swindow), list_box);
	gtk_container_add(GTK_CONTAINER(window), swindow);

	gtk_widget_show_all(window);

	g_signal_connect(list_box, "row-activated",
			G_CALLBACK(sojus_dir_cb_row_activated), (gpointer ) sojus);
	g_signal_connect(window, "delete-event",
			G_CALLBACK(sojus_dir_cb_delete_event), sojus);

	sojus_dir_fill_window(sojus, list_box, arr_hits);

	return;
}

static GArray*
sojus_dir_get_hits(Sojus *sojus, const gchar *needle) {
	GArray *arr_hits = NULL;

	arr_hits = g_array_new( FALSE, FALSE, sizeof(gint));

	for (gint i = 0; i < sojus->arr_dirs->len; i++)
		if (g_str_match_string(needle, g_ptr_array_index(sojus->arr_dirs, i),
				FALSE))
			g_array_append_val(arr_hits, i);

	if (!arr_hits->len) {
		g_array_unref(arr_hits);

		return NULL;
	}

	return arr_hits;
}

void sojus_dir_open(Sojus *sojus, const gchar *text) {
	gchar *needle = NULL;
	GArray *arr_hits = NULL;

	if (sond_client_misc_regnr_wohlgeformt(text)) {
		gint regnr = 0;
		gint jahr = 0;

		sond_client_misc_parse_regnr(text, &regnr, &jahr);
		needle = g_strdup_printf("%i-%i", regnr, jahr % 100);
	} else
		needle = g_strdup(text);

	arr_hits = sojus_dir_get_hits(sojus, needle);
	g_free(needle);
	if (!arr_hits)
		return;

	sojus_dir_show_hits(sojus, arr_hits);

	g_array_unref(arr_hits);

	return;
}
