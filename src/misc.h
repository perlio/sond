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
typedef struct _GtkCalendar GtkCalendar;
typedef struct _GPtrArray GPtrArray;
typedef struct _GFile GFile;
typedef struct _GError GError;
//extern GtkSelectionMode;

void display_message(GtkWidget*, ...);

void display_error(GtkWidget*, gchar const*, gchar const*);

gint dialog_with_buttons(GtkWidget*, const gchar*, const gchar*, gchar**, ...);

gint abfrage_frage(GtkWidget*, const gchar*, const gchar*, gchar**);

gchar* add_string(gchar*, gchar*);

gint string_to_guint(const gchar*, guint*);

gchar* filename_speichern(GtkWindow*, const gchar*, const gchar*);

gchar* filename_oeffnen(GtkWindow*);

GtkWidget* result_listbox_new(GtkWindow*, const gchar*);

/*  info_window  */
typedef struct _Info_Window {
	GtkWidget *dialog;
	GtkWidget *content;
	GtkWidget *last_inserted_widget;
	gboolean cancel;
} InfoWindow;

void info_window_kill(InfoWindow*);

void info_window_close(InfoWindow*);

void info_window_set_progress_bar_fraction(InfoWindow*, gdouble);

void info_window_set_progress_bar(InfoWindow*);

void info_window_display_progress(InfoWindow*, gint);

void info_window_set_message(InfoWindow*, const gchar*, ...);

InfoWindow* info_window_open(GtkWidget*, const gchar*);

GtkWidget* show_html_window(fz_context*, fz_buffer*, const char*);

void show_pixmap(fz_context*, fz_pixmap*);


#endif // MISC_H_INCLUDED
