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
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <ftw.h>
#include <ctype.h>
#include <cairo.h>
#include <mupdf/fitz.h>
//#include <lexbor/html/html.h>
#include <magic.h>

#ifdef _WIN32
#include <windows.h>
#include <shlwapi.h>
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#endif // _WIN32

#include "sond_log_and_error.h"

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

static gchar*
choose_file(const GtkWidget *window, const gchar *path,
		const gchar *title_text, gchar const* accept_text, gint action,
		const gchar *ext) {
	GtkWidget *dialog = NULL;
	gint rc = 0;
	gchar *dir_start = NULL;
	gchar* filename = NULL;

	dialog = gtk_file_chooser_dialog_new(title_text, GTK_WINDOW(window), action,
			"_Abbrechen", GTK_RESPONSE_CANCEL, accept_text, GTK_RESPONSE_ACCEPT,
			NULL);

	if (!path || !g_strcmp0(path, ""))
		dir_start = g_get_current_dir();
	else
		dir_start = g_strdup(path);

	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), dir_start);
	g_free(dir_start);
	gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), FALSE);
	gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog),
	TRUE);
	if (action == GTK_FILE_CHOOSER_ACTION_SAVE && ext)
		gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), ext);

	rc = gtk_dialog_run(GTK_DIALOG(dialog));
	if (rc == GTK_RESPONSE_ACCEPT) {
		filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		for (gchar *p = filename; *p; p++) {
		    if (*p == '\\') *p = '/';
		}
	}

	//Dialog schließen
	gtk_widget_destroy(dialog);

	return filename;
}

gchar*
filename_speichern(GtkWindow *window, const gchar *titel, const gchar *ext) {
	gchar* filename = NULL;

	filename = choose_file(GTK_WIDGET(window), NULL, titel, "Speichern",
			GTK_FILE_CHOOSER_ACTION_SAVE, ext);

	if (!filename)
		return NULL;

	return filename; //muß g_freed werden
}

gchar*
filename_oeffnen(GtkWindow *window) {
	gchar* filename = NULL;

	filename = choose_file(GTK_WIDGET(window), NULL, "Datei auswählen",
			"Öffnen", GTK_FILE_CHOOSER_ACTION_OPEN, NULL);

	if (!filename)
		return NULL;

	return filename; //muß g_freed werden
}

GtkWidget*
result_listbox_new(GtkWindow *parent_window, const gchar *titel) {
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
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(listbox), GTK_SELECTION_MULTIPLE);
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

void info_window_kill(InfoWindow *info_window) {
	gtk_widget_destroy(info_window->dialog);

	g_free(info_window);

	return;
}

void info_window_close(InfoWindow *info_window) {
	GtkWidget *button = g_object_get_data(
			G_OBJECT(info_window->dialog), "abbrechen-button");
	gtk_button_set_label(GTK_BUTTON(button), "Schließen");
	gtk_widget_grab_focus(button);

	gtk_dialog_run(GTK_DIALOG(info_window->dialog));

	info_window_kill(info_window);

	return;
}

void info_window_set_progress_bar_fraction(InfoWindow *info_window,
		gdouble fraction) {
	gtk_progress_bar_set_fraction(
			GTK_PROGRESS_BAR(info_window->progress_bar), fraction);

	while (gtk_events_pending())
		gtk_main_iteration();

	return;
}

static void on_content_size_allocate(GtkWidget *widget, GdkRectangle *allocation, gpointer data) {
	InfoWindow *info_window = (InfoWindow*) data;
	GtkWidget *viewport = NULL;
	GtkWidget *swindow = NULL;
	GtkAdjustment *adj = NULL;

	viewport = gtk_widget_get_parent(info_window->content);
	swindow = gtk_widget_get_parent(viewport);
	adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(swindow));
	gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj));

	// Signal wieder abkoppeln, nur einmal scrollen
	g_signal_handlers_disconnect_by_func(widget, on_content_size_allocate, data);
}

static void info_window_scroll(InfoWindow *info_window) {
	g_signal_connect(info_window->content, "size-allocate",
	                 G_CALLBACK(on_content_size_allocate), info_window);
	return;
}

void info_window_set_progress_bar(InfoWindow *info_window) {
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(info_window->progress_bar), 0.0);
	gtk_widget_show(info_window->progress_bar);

	while (gtk_events_pending())
		gtk_main_iteration();

	return;
}

void info_window_display_progress(InfoWindow *info_window, gint progress) {
	if (progress == -1)
		info_window_set_progress_bar(info_window);
	else
		info_window_set_progress_bar_fraction(info_window,
				((gdouble) progress) / 100.);

	return;
}

void info_window_set_message(InfoWindow *info_window, const gchar *format, ...) {
	va_list args;
	gchar *message = NULL;

	va_start(args, format);
	message = g_strdup_vprintf(format, args);
	va_end(args);

	info_window->last_inserted_widget = gtk_label_new(message);
	g_free(message);
	gtk_widget_set_halign(info_window->last_inserted_widget, GTK_ALIGN_START);

	gtk_widget_show(info_window->last_inserted_widget);
	gtk_box_pack_start(GTK_BOX(info_window->content),
			info_window->last_inserted_widget, FALSE, FALSE, 0);

	while (gtk_events_pending())
		gtk_main_iteration();

	info_window_scroll(info_window);

	return;
}

static gboolean show_message_main(gpointer data) {
    MessageData *md = (MessageData*) data;
	md->info_window->last_inserted_widget = gtk_label_new(md->message);
    g_free(md->message);

	gtk_widget_set_halign(md->info_window->last_inserted_widget, GTK_ALIGN_START);
	gtk_widget_show(md->info_window->last_inserted_widget);
	gtk_box_pack_start(GTK_BOX(md->info_window->content),
			md->info_window->last_inserted_widget, FALSE, FALSE, 0);

	info_window_scroll(md->info_window);
    g_free(md);

    return G_SOURCE_REMOVE;
}

// Im Worker-Thread aufrufen:
void info_window_set_message_from_thread(InfoWindow *info_window, const gchar *format, ...) {
    MessageData *md = g_new(MessageData, 1);
    md->info_window = info_window;

    va_list args;
    va_start(args, format);
    md->message = g_strdup_vprintf(format, args);
    va_end(args);

    g_idle_add(show_message_main, md);
}

static void cb_abbrechen_clicked(GtkButton *button, gpointer data) {
	gtk_dialog_response(GTK_DIALOG(data), GTK_RESPONSE_CANCEL);
	return;
}

static void cb_info_window_response(GtkDialog *dialog, gint id, gpointer data) {
	InfoWindow *info_window = (InfoWindow*) data;

	if (info_window->cancel)
		return;

	info_window_set_message(info_window, "...abgebrochen");
	g_atomic_int_set(&info_window->cancel, 1);

	return;
}

InfoWindow*
info_window_open(GtkWidget *window, const gchar *title) {
	GtkWidget *content = NULL;
	GtkWidget *swindow = NULL;
	GtkWidget *bottom_box = NULL;
	GtkWidget *button = NULL;

	InfoWindow *info_window = g_malloc0(sizeof(InfoWindow));

	info_window->dialog = gtk_dialog_new_with_buttons(title, GTK_WINDOW(window),
			GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL, NULL, NULL);

	gtk_window_set_default_size(GTK_WINDOW(info_window->dialog), 900, 190);

	content = gtk_dialog_get_content_area(GTK_DIALOG(info_window->dialog));
	swindow = gtk_scrolled_window_new( NULL, NULL);
	gtk_box_pack_start(GTK_BOX(content), swindow, TRUE, TRUE, 0);

	info_window->content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(swindow), info_window->content);

	/* Untere Leiste: Progress-Bar + Abbrechen-Button nebeneinander, nicht scrollbar */
	bottom_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_margin_start(bottom_box, 6);
	gtk_widget_set_margin_end(bottom_box, 6);
	gtk_widget_set_margin_top(bottom_box, 4);
	gtk_widget_set_margin_bottom(bottom_box, 4);
	gtk_box_pack_end(GTK_BOX(content), bottom_box, FALSE, FALSE, 0);

	info_window->progress_bar = gtk_progress_bar_new();
	gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(info_window->progress_bar), TRUE);
	gtk_widget_set_valign(info_window->progress_bar, GTK_ALIGN_CENTER);
	gtk_widget_set_no_show_all(info_window->progress_bar, TRUE);
	gtk_box_pack_start(GTK_BOX(bottom_box), info_window->progress_bar, TRUE, TRUE, 0);

	button = gtk_button_new_with_label("Abbrechen");
	gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
	gtk_box_pack_end(GTK_BOX(bottom_box), button, FALSE, FALSE, 0);
	g_signal_connect(button, "clicked",
			G_CALLBACK(cb_abbrechen_clicked),
			info_window->dialog);
	g_object_set_data(G_OBJECT(info_window->dialog), "abbrechen-button", button);

	gtk_widget_show_all(info_window->dialog);
	while (gtk_events_pending())
		gtk_main_iteration();

	g_signal_connect(GTK_DIALOG(info_window->dialog), "response",
			G_CALLBACK(cb_info_window_response), info_window);

	return info_window;
}

#include <string.h>
/*
 * Einfach: fz_pixmap in neuem Fenster mit ScrolledWindow anzeigen
 */

// Pixmap zu GdkPixbuf konvertieren
static GdkPixbuf* pixmap_to_pixbuf(fz_context *ctx, fz_pixmap *pix)
{
    GdkPixbuf *pixbuf = NULL;
    int width, height, stride;
    guchar *pixels;
    gboolean has_alpha;

    width = pix->w;
    height = pix->h;

    // Nur RGB (n=3) oder RGBA (n=4) unterstützt
    if (pix->n != 3 && pix->n != 4) {
        // Konvertiere zu RGB
        fz_pixmap *rgb_pix = fz_convert_pixmap(ctx, pix,
                                                fz_device_rgb(ctx), NULL,
                                                NULL, fz_default_color_params, 1);
        pixbuf = pixmap_to_pixbuf(ctx, rgb_pix);
        fz_drop_pixmap(ctx, rgb_pix);
        return pixbuf;
    }

    has_alpha = (pix->n == 4);

    // GdkPixbuf erstellen
    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, has_alpha, 8, width, height);
    if (!pixbuf)
        return NULL;

    pixels = gdk_pixbuf_get_pixels(pixbuf);
    stride = gdk_pixbuf_get_rowstride(pixbuf);

    // Pixel kopieren
    for (int y = 0; y < height; y++) {
        unsigned char *src = pix->samples + y * pix->stride;
        unsigned char *dst = pixels + y * stride;
        memcpy(dst, src, width * pix->n);
    }

    return pixbuf;
}

// ✅ HAUPTFUNKTION: Zeigt Pixmap in neuem Fenster
void show_pixmap(fz_context *ctx, fz_pixmap *pix)
{
    GtkWidget *window = NULL;
    GtkWidget *scrolled_window = NULL;
    GtkWidget *image = NULL;
    GdkPixbuf *pixbuf = NULL;

    // Pixbuf erstellen
    pixbuf = pixmap_to_pixbuf(ctx, pix);
    if (!pixbuf) {
        LOG_WARN("Failed to convert pixmap");
        return;
    }

    // Fenster erstellen
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "PDF Page");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);

    // ScrolledWindow
    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);

    // Image
    image = gtk_image_new_from_pixbuf(pixbuf);
    g_object_unref(pixbuf);

    // Zusammenbauen
    gtk_container_add(GTK_CONTAINER(scrolled_window), image);
    gtk_container_add(GTK_CONTAINER(window), scrolled_window);

    // Fenster beim Schließen zerstören (nicht die ganze App beenden)
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_widget_destroyed), &window);

    // Anzeigen
    gtk_widget_show_all(window);
}

