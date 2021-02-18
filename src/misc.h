#ifndef MISC_H_INCLUDED
#define MISC_H_INCLUDED

#define ERROR(x) { if ( errmsg ) *errmsg = add_string( \
                       g_strdup( "Bei Aufruf " x ":\n" ), *errmsg ); \
                       return -1; }

typedef struct _GSList GSList;
typedef struct _GPtrArray GPtrArray;
typedef struct _GtkWidget GtkWidget;
typedef struct _GtkCalendar GtkCalendar;

typedef char gchar;
typedef int gint;
typedef int gboolean;


void display_message( GtkWidget*, const gchar*, ... );

gint dialog_with_buttons( GtkWidget*, const gchar*, const gchar*, gchar**,
        gchar*, ... );

gint abfrage_frage( GtkWidget*, const gchar*, const gchar*, gchar** );

gint ask_question(GtkWidget*, const gchar*, const gchar*, const gchar* );

gint allg_string_array_index_holen( GPtrArray*, gchar* );

gchar* add_string( gchar*, gchar* );

GSList* choose_files( const GtkWidget*, const gchar*, const gchar*, gchar*, gint, gboolean );

gchar* get_path_from_base( const gchar*, gchar** );

void misc_set_calendar( GtkCalendar*, const gchar* );

gchar* misc_get_calendar( GtkCalendar* );

#endif // MISC_H_INCLUDED
