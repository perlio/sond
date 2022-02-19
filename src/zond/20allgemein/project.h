#ifndef PROJECT_H_INCLUDED
#define PROJECT_H_INCLUDED


typedef struct _Projekt Projekt;
typedef struct _GtkMenuItem GtkMenuItem;
typedef struct _GFile GFile;
typedef struct _DBase DBase;
typedef struct _DBase_Full DBaseFull;
typedef struct _ZondDBase ZondDBase;
typedef struct _SondTreeviewFM SondTreeviewFM;

typedef void* gpointer;
typedef int gint;
typedef char gchar;

typedef struct _DBase_Zond
{
    DBase* dbase_store;
    DBase* dbase_work;
    ZondDBase* zond_dbase_store;
    ZondDBase* zond_dbase_work;
    gchar* project_name;
    gchar* project_dir;
    gboolean changed;
} DBaseZond;


void project_reset_changed( Projekt* );

void projekt_set_widgets_sensitiv( Projekt*, gboolean );

gint projekt_schliessen( Projekt*, gchar** );

void cb_menu_datei_speichern_activate( GtkMenuItem*, gpointer );

void cb_menu_datei_schliessen_activate( GtkMenuItem*, gpointer );

gint project_oeffnen( Projekt*, const gchar*, gboolean, gchar** );

void cb_menu_datei_oeffnen_activate( GtkMenuItem*, gpointer );

void cb_menu_datei_neu_activate( GtkMenuItem*, gpointer );

#endif // PROJECT_H_INCLUDED
