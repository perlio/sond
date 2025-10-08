#ifndef MISC_H_INCLUDED
#define MISC_H_INCLUDED

#define ERROR_S_VAL(y) { if ( errmsg ) *errmsg = add_string( \
                         g_strconcat( __func__, "\n", NULL ), *errmsg ); \
                         return y; }

#define ERROR_S ERROR_S_VAL(-1)

#define ERROR_S_MESSAGE_VAL(x,y) { if ( errmsg ) *errmsg = add_string( \
                         g_strconcat( "Bei Aufruf ",__func__, ":\n", x, NULL ), *errmsg ); \
                         return y; }

#define ERROR_S_MESSAGE(x) ERROR_S_MESSAGE_VAL(x,-1)

#define ERROR_Z_VAL(y) {g_prefix_error(error, "%s\n", __func__); return y;}
#define ERROR_Z ERROR_Z_VAL(-1)

#include <gtk/gtk.h>

typedef struct _GSList GSList;
typedef struct _GPtrArray GPtrArray;
typedef struct _GtkWidget GtkWidget;
typedef struct _GtkCalendar GtkCalendar;
typedef struct _SondTreeview SondTreeview;
typedef struct _GFile GFile;
typedef struct _GtkWindow GtkWindow;
typedef struct _GError GError;
typedef unsigned int guint32;
typedef guint32 GQuark;
typedef char gchar;
typedef int gint;
typedef unsigned int guint;
typedef int gboolean;
#ifdef __WIN32
typedef void* GPid;
#elif defined __linux__
typedef int GPid;
#endif // __win32__
typedef void *gpointer;
typedef struct _SondFilePart SondFilePart;

gchar* change_basename(gchar const*, gchar const*);

void display_message(GtkWidget*, ...);

void display_error(GtkWidget*, gchar const*, gchar const*);

gint dialog_with_buttons(GtkWidget*, const gchar*, const gchar*, gchar**, ...);

gint abfrage_frage(GtkWidget*, const gchar*, const gchar*, gchar**);

gint ask_question(GtkWidget*, const gchar*, const gchar*, const gchar*);

gint allg_string_array_index_holen(GPtrArray*, gchar*);

gchar* add_string(gchar*, gchar*);

gchar* utf8_to_local_filename(const gchar*);

gint string_to_guint(const gchar*, guint*);

gchar* filename_speichern(GtkWindow*, const gchar*, const gchar*);

gchar* filename_oeffnen(GtkWindow*);

gchar* get_rel_path_from_file(const gchar*, const GFile*);

void misc_set_calendar(GtkCalendar*, const gchar*);

gchar* misc_get_calendar(GtkCalendar*);

GtkWidget* result_listbox_new(GtkWindow*, const gchar*, GtkSelectionMode);

gint misc_datei_oeffnen(gchar const*, gboolean, GError**);

gchar const* get_mime_type_from_content_type(gchar const*);

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

void info_window_set_message(InfoWindow*, const gchar*);

InfoWindow* info_window_open(GtkWidget*, const gchar*);

#endif // MISC_H_INCLUDED
