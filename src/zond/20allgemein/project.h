#ifndef PROJECT_H_INCLUDED
#define PROJECT_H_INCLUDED

typedef struct _Projekt Projekt;
typedef struct _GtkMenuItem GtkMenuItem;
typedef struct _Displayed_Document DisplayedDocument;
typedef struct _ZondPdfDocument ZondPdfDocument;
typedef struct _ZondDBase ZondDBase;

typedef void *gpointer;
typedef int gint;
typedef char gchar;

typedef struct _DBase_Zond {
	ZondDBase *zond_dbase_store;
	ZondDBase *zond_dbase_work;
	gboolean changed;
} DBaseZond;

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
gint project_close(Projekt*, GError**);
gint project_save(Projekt*, GError**);
gint project_load_trees(Projekt*, GError**);
gint project_open(Projekt*, const gchar*, gboolean, GError**);
gint project_load(Projekt* zond, GError** error);
gint project_new(Projekt* zond, GError** error);

#endif // PROJECT_H_INCLUDED
