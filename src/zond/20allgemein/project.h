#ifndef PROJECT_H_INCLUDED
#define PROJECT_H_INCLUDED

typedef struct _Projekt Projekt;
typedef struct _GtkMenuItem GtkMenuItem;
typedef struct _DBase_Zond DBaseZond;
typedef struct _Displayed_Document DisplayedDocument;
typedef struct _ZondPdfDocument ZondPdfDocument;

typedef void *gpointer;
typedef int gint;
typedef char gchar;

// Database transaction functions
gint dbase_zond_begin(DBaseZond*, GError**);
void dbase_zond_rollback(DBaseZond*, GError**);
gint dbase_zond_commit(DBaseZond*, GError**);

// Database update functions
gint dbase_zond_update_sections(DBaseZond*, DisplayedDocument*, GError**);
gint dbase_zond_update_path(DBaseZond*, gchar const*, gchar const*, GError**);
gint dbase_zond_update_gmessage_index(DBaseZond*, gchar const*, gint, gboolean, GError**);

// Project state management
void project_reset_changed(Projekt*, gboolean);
void project_set_widgets_sensitive(Projekt*, gboolean);
gboolean project_timeout_autosave(gpointer);

// Project operations
gint project_close(Projekt*, gchar**);
gint project_save(Projekt*, gchar**);
gint project_load_trees(Projekt*, GError**);
gint project_open(Projekt*, const gchar*, gboolean, gchar**);
gint project_load(Projekt* zond, gchar** errmsg);
gint project_new(Projekt* zond, gchar** errmsg);

#endif // PROJECT_H_INCLUDED
