#ifndef PROJECT_H_INCLUDED
#define PROJECT_H_INCLUDED

typedef struct _Projekt Projekt;
typedef struct _GtkMenuItem GtkMenuItem;

typedef void* gpointer;
typedef int gint;
typedef char gchar;


gboolean db_create( sqlite3*, gchar** );

void project_set_changed( gpointer );

void reset_project_changed( Projekt* );

gint create_stmts( Projekt*, gchar** );

void projekt_set_widgets_sensitiv( Projekt*, gboolean );

gint projekt_aktivieren( Projekt*, gchar*, gboolean, gchar** );

void projekt_schliessen( Projekt* );

void cb_menu_datei_speichern_activate( GtkMenuItem*, gpointer );

void cb_menu_datei_schliessen_activate( GtkMenuItem*, gpointer );

void cb_menu_datei_oeffnen_activate( GtkMenuItem*, gpointer );

void cb_menu_datei_neu_activate( GtkMenuItem*, gpointer );

#endif // PROJECT_H_INCLUDED
