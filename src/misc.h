#ifndef MISC_H_INCLUDED
#define MISC_H_INCLUDED

typedef struct _GSList GSList;
typedef struct _GPtrArray GPtrArray;
typedef struct _GtkWidget GtkWidget;

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

#endif // MISC_H_INCLUDED
