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

//Nächstes Error-Reporting-System!
//SondError-Struktur enthält GError-Struktur, da die hierin enthaltene Info (Error-Quark und - Code
//erhalten werden soll.
//member "origin" gint an, in welche Funktion GError
//zurückgibt bzw. in welcher Funktion GError gesetzt wird.
//Da mir nicht bekannt ist, wie in C der Name der aufrufenden Funktion
//herausgefunden werden kann, enthält "function_stack" eine durch "\n" getrennte Liste
//der aufrufenden Funktionen.
//Error wird grundsätzlich bis zum aufrufenden Callback "durchgereicht",
//da erst da entschieden werden kann, wie Error zu behandeln ist.

#define SOND_ERROR_VAL(ret) {(*sond_error)->function_stack = \
                    add_string( g_strconcat( __func__, "\n", NULL ), \
                    (*sond_error)->function_stack ); \
                    return ret;}

#define SOND_ERROR SOND_ERROR_VAL(-1)

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

typedef struct _SondError
{
    GError* error;
    gchar* origin;
    gchar* function_stack;
} SondError;

void sond_error_free( SondError* );

SondError* sond_error_new( GError*, const gchar* );

SondError* sond_error_new_full( GQuark, gint, const gchar*, const gchar* );


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

gchar* get_rel_path_from_file( const gchar*, const GFile* );

void misc_set_calendar( GtkCalendar*, const gchar* );

gchar* misc_get_calendar( GtkCalendar* );

gchar* get_base_dir( void );

GtkWidget* result_listbox_new( GtkWindow*, const gchar* );

#endif // MISC_H_INCLUDED
