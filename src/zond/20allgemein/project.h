#ifndef PROJECT_H_INCLUDED
#define PROJECT_H_INCLUDED

typedef struct _Projekt Projekt;
typedef struct _GtkMenuItem GtkMenuItem;
typedef struct _GFile GFile;
typedef struct _DBase_Zond DBaseZond;
typedef struct _Anbindung Anbindung;
typedef struct _Displayed_Document DisplayedDocument;

typedef void *gpointer;
typedef int gint;
typedef char gchar;

gint dbase_zond_begin(DBaseZond*, GError**);

gint dbase_zond_rollback(DBaseZond*, GError**);

gint dbase_zond_commit(DBaseZond*, GError**);

gint dbase_zond_update_section(DBaseZond*, DisplayedDocument*, GError** );

gint dbase_zond_update_path(DBaseZond*, gchar const*, gchar const*, GError**);

void project_set_changed(gpointer);

void project_reset_changed(Projekt*, gboolean);

void projekt_set_widgets_sensitiv(Projekt*, gboolean);

gint projekt_schliessen(Projekt*, gchar**);

gint project_speichern(Projekt*, gchar**);

gboolean project_timeout_autosave(gpointer);

void cb_menu_datei_speichern_activate(GtkMenuItem*, gpointer);

void cb_menu_datei_schliessen_activate(GtkMenuItem*, gpointer);

gint project_load_baeume(Projekt*, GError**);

gint project_oeffnen(Projekt*, const gchar*, gboolean, gchar**);

void cb_menu_datei_oeffnen_activate(GtkMenuItem*, gpointer);

void cb_menu_datei_neu_activate(GtkMenuItem*, gpointer);

#endif // PROJECT_H_INCLUDED
