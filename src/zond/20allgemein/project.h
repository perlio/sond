#ifndef PROJECT_H_INCLUDED
#define PROJECT_H_INCLUDED

#define ERROR_ROLLBACK_BOTH(dbase_store,dbase_work,x) \
          { if ( errmsg ) *errmsg = add_string( \
            g_strdup( "Bei Aufruf " x ":\n" ), *errmsg ); \
            \
            gint rc_rollback1 = 0; \
            gint rc_rollback2 = 0; \
            gchar* err_rollback = NULL; \
            \
            rc_rollback1 = dbase_rollback( dbase_store, &err_rollback ); \
            if ( errmsg ) \
            { \
                if ( !rc_rollback1 ) *errmsg = add_string( *errmsg, \
                        g_strdup( "\n\nRollback dbase_store durchgeführt" ) ); \
                else *errmsg = add_string( *errmsg, g_strconcat( "\n\nRollback " \
                        "dbase_store fehlgeschlagen\n\nBei Aufruf dbase_rollback:\n", \
                        err_rollback, NULL ) ); \
            } \
            g_free( err_rollback ); \
            \
            rc_rollback2 = dbase_rollback( dbase_work, &err_rollback ); \
            { \
                if ( !rc_rollback2 ) *errmsg = add_string( *errmsg, \
                        g_strdup( "\n\nRollback dbase_work durchgeführt" ) ); \
                else *errmsg = add_string( *errmsg, g_strconcat( "\n\nRollback " \
                        "dbase_work fehlgeschlagen\n\nBei Aufruf dbase_rollback:\n", \
                        err_rollback, NULL ) ); \
            } \
            g_free( err_rollback ); \
            \
            if ( errmsg ) \
            { \
                if ( rc_rollback1 || rc_rollback2 ) *errmsg = \
                        add_string( *errmsg, g_strdup( "\n\nDatenbank inkonsistent" ) ); \
            } \
          }


typedef struct _Projekt Projekt;
typedef struct _GtkMenuItem GtkMenuItem;
typedef struct _GFile GFile;
typedef struct _DBase DBase;
typedef struct _DBase_Full DBaseFull;

typedef void* gpointer;
typedef int gint;
typedef char gchar;

typedef struct _DBase_Zond
{
    DBaseFull* dbase_work;
    DBase* dbase_store;
    gchar* project_name;
    gchar* project_dir;
    gboolean changed;
} DBaseZond;


gint project_test_rel_path( const GFile*, gpointer, gchar** );

gint project_before_move( const GFile*, const GFile*, gpointer, gchar** );

gint project_after_move( const gint, gpointer, gchar** );

void projekt_set_widgets_sensitiv( Projekt*, gboolean );

gint projekt_schliessen( Projekt*, gchar** );

void cb_menu_datei_speichern_activate( GtkMenuItem*, gpointer );

void cb_menu_datei_schliessen_activate( GtkMenuItem*, gpointer );

gint project_oeffnen( Projekt*, const gchar*, gboolean, gchar** );

void cb_menu_datei_oeffnen_activate( GtkMenuItem*, gpointer );

void cb_menu_datei_neu_activate( GtkMenuItem*, gpointer );

#endif // PROJECT_H_INCLUDED
