#ifndef MISC_H_INCLUDED
#define MISC_H_INCLUDED

#include <mupdf/fitz.h>

#ifdef __WIN32
typedef void* GPid;
#elif defined __linux__
typedef int GPid;
#endif // __win32__

typedef char gchar;
typedef int gint;
typedef unsigned int guint;
typedef int gboolean;
typedef double gdouble;
typedef size_t gsize;

typedef struct _SondFilePart SondFilePart;
typedef struct _GtkWidget GtkWidget;
typedef struct _GtkWindow GtkWindow;
typedef struct _GtkTextMark GtkTextMark;
typedef struct _GtkCalendar GtkCalendar;
typedef struct _GPtrArray GPtrArray;
typedef struct _GFile GFile;
typedef struct _GError GError;
typedef struct _GtkDialog GtkDialog;

gchar* add_string(gchar *old_string, gchar *add_string);

gint my_dialog_run(GtkDialog*);

void display_message(GtkWidget*, ...);

gint dialog_with_buttons(GtkWidget*, const gchar*, const gchar*, gchar**, ...);

gint abfrage_frage(GtkWidget*, const gchar*, const gchar*, gchar**);

gchar* add_string(gchar*, gchar*);

gint string_to_guint(const gchar*, guint*);

gchar* filename_speichern(GtkWindow*, const gchar*, const gchar*);

/* start_path (kann NULL sein): Ordner, in dem der Dialog initial öffnet -
 * s. Kommentar an filename_oeffnen() (misc.c) zum Hintergrund (langsames
 * Öffnen des Datei-Dialogs bei SeaDrive-Projekten). */
gchar* filename_oeffnen(GtkWindow*, const gchar *start_path);

GtkWidget* result_listbox_new(GtkWindow*, const gchar*);

/*  info_window  */
typedef struct _Info_Window {
	GtkWidget *dialog;
	/* Ein GtkTextView/GtkTextBuffer (text_view/end_mark) statt eines
	 * GtkLabels pro Nachricht: bei sehr vielen Nachrichten (z.B. eine Zeile
	 * pro Datei beim Anbinden tausender Dateien) wäre die
	 * GtkBox-Größenberechnung O(Anzahl Kinder) pro neuer Nachricht,
	 * verschärft durch das UI-Pumping (gtk_main_iteration() nach jeder
	 * Nachricht erzwingt den Resize sofort statt ihn zu bündeln) - in Summe
	 * O(n²). Text anhängen ist beim GtkTextView dafür gebaut und bleibt auch
	 * bei vielen Zeilen günstig. */
	GtkWidget *text_view;
	GtkTextMark *end_mark;
	GtkWidget *progress_bar;
	gint* cancel;
} InfoWindow;

void info_window_kill(InfoWindow*);

void info_window_close(InfoWindow*);

void info_window_set_progress_bar_fraction(InfoWindow*, gdouble);

void info_window_set_progress_bar(InfoWindow*);

void info_window_display_progress(InfoWindow*, gint);

void info_window_set_message(InfoWindow*, const gchar*, ...);

void info_window_set_message_thread_safe(InfoWindow *info_window, const gchar *format, ...);

InfoWindow* info_window_open(GtkWidget*, gint*, const gchar*);

GtkWidget* show_html_window(fz_context*, fz_buffer*, const char*);

void show_pixmap(fz_context*, fz_pixmap*);


#endif // MISC_H_INCLUDED
