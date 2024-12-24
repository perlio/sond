/*
 sond (misc.c) - Akten, Beweisstücke, Unterlagen
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

#include "misc.h"

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <ftw.h>

#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#endif // _WIN32

/** Zeigt Fenster, in dem Liste übergebener strings angezeigt wird.
 *   parent-window kann NULL sein, dann Warnung
 *   text1 darf nicht NULL sein
 *   Abschluß der Liste mit NULL
 */
void display_message(GtkWidget *window, ...) {
	va_list ap;
	gchar *message = NULL;
	const gchar *str = NULL;

	va_start(ap, window);
	while ((str = va_arg(ap, const gchar*)))
		message = add_string(message, g_strdup(str));

	va_end(ap);

	GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
			GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
			message);
	g_free(message);

	gtk_dialog_run(GTK_DIALOG(dialog));

	gtk_widget_destroy(dialog);

	return;
}

void display_error(GtkWidget *window, gchar const *error, gchar const *errmsg) {
	GtkWidget *message_area = NULL;
	GtkWidget *swindow = NULL;
	GtkWidget *textview = NULL;
	GtkTextBuffer *textbuffer = NULL;

	GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
			GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
			error);

	message_area = gtk_message_dialog_get_message_area(
			GTK_MESSAGE_DIALOG(dialog));
	swindow = gtk_scrolled_window_new( NULL, NULL);
	textbuffer = gtk_text_buffer_new( NULL);
	gtk_text_buffer_set_text(textbuffer, errmsg, -1);

	textview = gtk_text_view_new_with_buffer(textbuffer);

	gtk_container_add(GTK_CONTAINER(swindow), textview);
	gtk_box_pack_start(GTK_BOX(message_area), swindow, FALSE, FALSE, 0);
	gtk_scrolled_window_set_propagate_natural_height(
			GTK_SCROLLED_WINDOW(swindow), TRUE);
	gtk_scrolled_window_set_propagate_natural_width(
			GTK_SCROLLED_WINDOW(swindow), TRUE);

	gtk_widget_show_all(message_area);

	gtk_dialog_run(GTK_DIALOG(dialog));

	gtk_widget_destroy(dialog);

	return;
}

static void cb_entry_text(GtkEntry *entry, gpointer data) {
	GtkWidget *dialog = NULL;

	dialog = GTK_WIDGET(data);

	gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_YES);

	return;
}

/** ... response_id, next_button_text, next_response_id, ..., NULL
 **/
gint dialog_with_buttons(GtkWidget *window, const gchar *message,
		const gchar *secondary, gchar **text, ...) {
	gint res;
	GtkWidget *entry = NULL;
	va_list arg_pointer;
	const gchar *button_text = NULL;

	GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
			GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_QUESTION,
			GTK_BUTTONS_NONE, message, NULL);
	gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s",
			secondary);

	//buttons einfügen
	va_start(arg_pointer, text);

	while ((button_text = va_arg(arg_pointer, const gchar*))) {
		gint response_id = 0;

		response_id = va_arg(arg_pointer, gint);

		gtk_dialog_add_button(GTK_DIALOG(dialog), button_text, response_id);
	}

	va_end(arg_pointer);

	if (text) {
		GtkWidget *content = gtk_message_dialog_get_message_area(
				GTK_MESSAGE_DIALOG(dialog));
		entry = gtk_entry_new();
		gtk_container_add(GTK_CONTAINER(content), entry);
		if (*text)
			gtk_entry_set_text(GTK_ENTRY(entry), *text);
		g_free(*text);
		*text = NULL;

		gtk_widget_show_all(content);

		g_signal_connect(entry, "activate", G_CALLBACK(cb_entry_text),
				(gpointer ) dialog);
	}

	res = gtk_dialog_run(GTK_DIALOG(dialog));

	if (text)
		*text = g_strdup(gtk_entry_get_text( GTK_ENTRY(entry) ));

	gtk_widget_destroy(dialog);

	return res;
}

/** wrapper
 **/
gint abfrage_frage(GtkWidget *window, const gchar *message,
		const gchar *secondary, gchar **text) {
	gint res;

	res = dialog_with_buttons(window, message, secondary, text, "Ja",
			GTK_RESPONSE_YES, "Nein", GTK_RESPONSE_NO, NULL);

	return res;
}

gint ask_question(GtkWidget *window, const gchar *title, const gchar *ja,
		const gchar *nein) {
	gint res;
	GtkWidget *dialog = gtk_dialog_new_with_buttons(title, GTK_WINDOW(window),
			GTK_DIALOG_DESTROY_WITH_PARENT, ja, GTK_RESPONSE_YES, nein,
			GTK_RESPONSE_NO, "_Abbrechen", GTK_RESPONSE_CANCEL, NULL);

	res = gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);

	return res;
}

gint allg_string_array_index_holen(GPtrArray *array, gchar *element) {
	for (gint i = 0; i < array->len; i++)
		if (!g_strcmp0(g_ptr_array_index(array, i), element))
			return i;

	return -1;
}

gchar*
add_string(gchar *old_string, gchar *add_string) {
	gchar *new_string = NULL;

	if (old_string)
		new_string = g_strconcat(old_string, add_string, NULL);
	else
		new_string = g_strdup(add_string);
	g_free(old_string);
	g_free(add_string);

	return new_string;
}

/** String Utilities **/
gchar*
utf8_to_local_filename(const gchar *utf8) {
	//utf8 in filename konvertieren
	gsize written;
	gchar *charset = g_get_codeset();
	gchar *local_filename = g_convert(utf8, -1, charset, "UTF-8", NULL,
			&written,
			NULL);
	g_free(charset);

	return local_filename; //muß g_freed werden!
}

gint string_to_guint(const gchar *string, guint *zahl) {
	gboolean is_guint = TRUE;
	if (!strlen(string))
		is_guint = FALSE;
	gint i = 0;
	while (i < (gint) strlen(string) && is_guint) {
		if (!isdigit((int) *(string + i)))
			is_guint = FALSE;
		i++;
	}

	if (is_guint) {
		*zahl = atoi(string);
		return 0;
	} else
		return -1;
}

GSList*
choose_files(const GtkWidget *window, const gchar *path,
		const gchar *title_text, gchar *accept_text, gint action,
		const gchar *ext, gboolean multiple) {
	GtkWidget *dialog = NULL;
	gint rc = 0;
	gchar *dir_start = NULL;
	GSList *list = NULL;

	dialog = gtk_file_chooser_dialog_new(title_text, GTK_WINDOW(window), action,
			"_Abbrechen", GTK_RESPONSE_CANCEL, accept_text, GTK_RESPONSE_ACCEPT,
			NULL);

	if (!path || !g_strcmp0(path, ""))
		dir_start = g_get_current_dir();
	else
		dir_start = g_strdup(path);

	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), dir_start);
	g_free(dir_start);
	gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), multiple);
	gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog),
	TRUE);
	if (action == GTK_FILE_CHOOSER_ACTION_SAVE && ext)
		gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), ext);

	rc = gtk_dialog_run(GTK_DIALOG(dialog));
	if (rc == GTK_RESPONSE_ACCEPT)
		list = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));

	//Dialog schließen
	gtk_widget_destroy(dialog);

	return list;
}

gchar*
filename_speichern(GtkWindow *window, const gchar *titel, const gchar *ext) {
	GSList *list = choose_files(GTK_WIDGET(window), NULL, titel, "Speichern",
			GTK_FILE_CHOOSER_ACTION_SAVE, ext, FALSE);

	if (!list)
		return NULL;

	gchar *abs_path = g_strdup(list->data);
	g_slist_free_full(list, g_free);

	return abs_path; //muß g_freed werden
}

gchar*
filename_oeffnen(GtkWindow *window) {
	GSList *list = choose_files(GTK_WIDGET(window), NULL, "Datei auswählen",
			"Öffnen", GTK_FILE_CHOOSER_ACTION_OPEN, NULL, FALSE);

	if (!list)
		return NULL;

	gchar *abs_path = g_strdup(list->data);
	g_slist_free_full(list, g_free);

	return abs_path; //muß g_freed werden
}

gchar*
get_rel_path_from_file(const gchar *root, const GFile *file) {
	if (!file)
		return NULL;

	//Überprüfung, ob schon angebunden
	gchar *rel_path = NULL;
	gchar *abs_path = g_file_get_path((GFile*) file);

#ifdef _WIN32
	abs_path = g_strdelimit(abs_path, "\\", '/');
#endif // _WIN32

	rel_path = g_strdup(abs_path + ((root) ? strlen(root) + 1 : 0));
	g_free(abs_path);

	return rel_path; //muß freed werden
}

void misc_set_calendar(GtkCalendar *calendar, const gchar *date) {
	if (!date)
		return;

	gchar *year_text = g_strndup(date, 4);
	gint year = g_ascii_strtoll(year_text, NULL, 10);
	g_free(year_text);

	gchar *month_text = g_strndup(date + 5, 2);
	gint month = g_ascii_strtoll(month_text, NULL, 10);
	g_free(month_text);

	gchar *day_text = g_strdup(date + 8);
	gint day = g_ascii_strtoll(day_text, NULL, 10);
	g_free(day_text);

	gtk_calendar_select_month(calendar, month - 1, year);
	gtk_calendar_select_day(calendar, 0);
	gtk_calendar_mark_day(calendar, day);

	return;
}

gchar*
misc_get_calendar(GtkCalendar *calendar) {
	guint year = 0;
	guint month = 0;
	guint day = 0;
	gchar *string = NULL;

	gtk_calendar_get_date(calendar, &year, &month, &day);

	string = g_strdup_printf("%04d-%02d-%02d", year, month + 1, day);

	return string;
}

GtkWidget*
result_listbox_new(GtkWindow *parent_window, const gchar *titel,
		GtkSelectionMode mode) {
	GtkWidget *window = NULL;
	GtkWidget *scrolled_window = NULL;
	GtkWidget *listbox = NULL;
	GtkWidget *headerbar = NULL;

	//Fenster erzeugen
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(window), 800, 350);
	if (parent_window) {
		gtk_window_set_transient_for(GTK_WINDOW(window), parent_window);
		gtk_window_set_modal(GTK_WINDOW(window), FALSE);
	}

	scrolled_window = gtk_scrolled_window_new( NULL, NULL);
	listbox = gtk_list_box_new();
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(listbox), mode);
	gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(listbox), FALSE);

	gtk_container_add(GTK_CONTAINER(scrolled_window), listbox);
	gtk_container_add(GTK_CONTAINER(window), scrolled_window);

	//Headerbar erzeugen
	headerbar = gtk_header_bar_new();
	gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(headerbar),
			":minimize,close");
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(headerbar), TRUE);
	gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), titel);
	gtk_window_set_titlebar(GTK_WINDOW(window), headerbar);

	g_object_set_data(G_OBJECT(window), "listbox", listbox);
	g_object_set_data(G_OBJECT(window), "headerbar", headerbar);

	return window;
}

gint misc_datei_oeffnen(const gchar *path, gboolean open_with, gchar **errmsg) {
#ifdef _WIN32 //glib funktioniert nicht; daher Windows-Api verwenden
	gboolean ret = FALSE;

	gchar *path_win32 = g_strdelimit(g_strdup(path), "/", '\\');

	//utf8 in filename konvertieren
	gchar *charset = g_get_codeset();
	gchar *local_filename = g_convert(path_win32, -1, charset, "UTF-8", NULL,
			NULL,
			NULL);
	g_free(charset);
	g_free(path_win32);

//    public const uint SEE_MASK_INVOKEIDLIST = 12;

	CoInitializeEx( NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	SHELLEXECUTEINFO sei = { sizeof(sei) };
	sei.nShow = SW_SHOWNORMAL;
	sei.lpVerb = (open_with) ? "openas" : NULL;
	sei.lpFile = local_filename;
	sei.fMask = SEE_MASK_INVOKEIDLIST;

	ret = ShellExecuteEx(&sei);
	g_free(local_filename);
	if (!ret) //Fähler
	{
		if (errmsg) {
			LPVOID lpMsgBuf = NULL;
			DWORD dw = 0;

			dw = GetLastError();
			FormatMessage(
					FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
							|
							FORMAT_MESSAGE_IGNORE_INSERTS,
					NULL, dw, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
					(LPTSTR) &lpMsgBuf, 0, NULL);
			*errmsg = g_strdup_printf("Bei Aufruf ShellExecuteEx:\n"
					"Fehlercode: %li\n%s", dw, (LPTSTR) lpMsgBuf);

			LocalFree(lpMsgBuf);
		}

		return -1;
	}
#else //Linux/Mac
/*
    gchar* exe = NULL;
    gchar* argv[3] = { NULL };

    //exe herausfinden, vielleicht mit xdgopen???!

    argv[0] = exe;
    argv[1] = path;

    gboolean rc = g_spawn_async( NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD,
            NULL, NULL, &g_pid, &error );
    if ( !rc )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_spawn_async:\n",
                error->message, NULL );
        g_error_free( error );

        return -1;
    }

    g_child_watch_add( g_pid, (GChildWatchFunc) g_spawn_close_pid, NULL );
*/
#endif // _WIN32

	return 0;
}

