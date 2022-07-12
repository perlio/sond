#ifndef MISC_H_INCLUDED
#define MISC_H_INCLUDED

#define ERROR_SOND_VAL(x,y) { if ( errmsg ) *errmsg = add_string( \
                       g_strdup( "Bei Aufruf " x ":\n" ), *errmsg ); \
                       return y; }

#define ERROR_SOND(x) ERROR_SOND_VAL(x,-1)

#define ERROR_S_VAL(y) { if ( errmsg ) *errmsg = add_string( \
                         g_strconcat( "Bei Aufruf ", __func__, ":\n", NULL ), *errmsg ); \
                         return y; }

#define ERROR_S ERROR_S_VAL(-1)

#define ERROR_S_MESSAGE_VAL(x,y) { if ( errmsg ) *errmsg = add_string( \
                         g_strconcat( "Bei Aufruf ",__func__, ":\n", x, NULL ), *errmsg ); \
                         return y; }

#define ERROR_S_MESSAGE(x) ERROR_S_MESSAGE_VAL(x,-1)

typedef struct _GSList GSList;
typedef struct _GPtrArray GPtrArray;
typedef struct _GtkWidget GtkWidget;
typedef struct _GtkCalendar GtkCalendar;
typedef struct _SondTreeview SondTreeview;
typedef struct _GFile GFile;
typedef struct _GtkWindow GtkWindow;

typedef char gchar;
typedef int gint;
typedef unsigned int guint;
typedef int gboolean;


void display_message( GtkWidget*, ... );

gint dialog_with_buttons( GtkWidget*, const gchar*, const gchar*, gchar**,
        ... );

gint abfrage_frage( GtkWidget*, const gchar*, const gchar*, gchar** );

gint ask_question(GtkWidget*, const gchar*, const gchar*, const gchar* );

gint allg_string_array_index_holen( GPtrArray*, gchar* );

gchar* add_string( gchar*, gchar* );

gchar* utf8_to_local_filename( const gchar* );

gint string_to_guint( const gchar*, guint* );

GSList* choose_files( const GtkWidget*, const gchar*, const gchar*, gchar*, gint, const gchar*, gboolean );

gchar* filename_speichern( GtkWindow*, const gchar*, const gchar* );

gchar* filename_oeffnen( GtkWindow* );

gchar* get_path_from_base( const gchar*, gchar** );

gchar* get_rel_path_from_file( const gchar*, const GFile* );

void misc_set_calendar( GtkCalendar*, const gchar* );

gchar* misc_get_calendar( GtkCalendar* );

#endif // MISC_H_INCLUDED
