#ifndef ZOND_TREEVIEWFM_H_INCLUDED
#define ZOND_TREEVIEWFM_H_INCLUDED

#include <glib-object.h>
#include <gtk/gtk.h>

#include "../sond_treeviewfm.h"

//#include "20allgemein/project.h"


#define ERROR_ROLLBACK_BOTH(zond_dbase_work,zond_dbase_store) \
          { if ( errmsg ) *errmsg = add_string( \
            g_strconcat( "Bei Aufruf ", __func__, ":\n", NULL ), *errmsg ); \
            \
            gint rc_rollback1 = 0; \
            gint rc_rollback2 = 0; \
            gchar* err_rollback = NULL; \
            \
            rc_rollback1 = zond_dbase_rollback( zond_dbase_work, &err_rollback ); \
            if ( errmsg ) \
            { \
                if ( !rc_rollback1 ) *errmsg = add_string( *errmsg, \
                        g_strdup( "\n\nRollback dbase_store durchgeführt" ) ); \
                else *errmsg = add_string( *errmsg, g_strconcat( "\n\nRollback " \
                        "zond_dbase_work fehlgeschlagen\n\nBei Aufruf dbase_rollback:\n", \
                        err_rollback, NULL ) ); \
            } \
            g_free( err_rollback ); \
            \
            rc_rollback2 = zond_dbase_rollback( zond_dbase_store, &err_rollback ); \
            { \
                if ( !rc_rollback2 ) *errmsg = add_string( *errmsg, \
                        g_strdup( "\n\nRollback dbase_work durchgeführt" ) ); \
                else *errmsg = add_string( *errmsg, g_strconcat( "\n\nRollback " \
                        "zond_dbase_store fehlgeschlagen\n\nBei Aufruf dbase_rollback:\n", \
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


G_BEGIN_DECLS

//ZOND_PDF_ABSCHNITT definieren - lokales GObject-Derivat
#define ZOND_TYPE_PDF_ABSCHNITT zond_pdf_abschnitt_get_type( )
G_DECLARE_DERIVABLE_TYPE (ZondPdfAbschnitt, zond_pdf_abschnitt, ZOND, PDF_ABSCHNITT, GObject)

struct _ZondPdfAbschnittClass
{
    GObjectClass parent_class;
};


//ZOND_TYPE_TREEVIEWFM definieren
#define ZOND_TYPE_TREEVIEWFM zond_treeviewfm_get_type( )
G_DECLARE_DERIVABLE_TYPE (ZondTreeviewFM, zond_treeviewfm, ZOND, TREEVIEWFM, SondTreeviewFM)


struct _ZondTreeviewFMClass
{
    SondTreeviewFMClass parent_class;
};

G_END_DECLS

#endif // ZOND_TREEVIEWFM_H_INCLUDED
