#ifndef ZOND_TREEVIEWFM_H_INCLUDED
#define ZOND_TREEVIEWFM_H_INCLUDED

#include <glib-object.h>
#include <gtk/gtk.h>

#include "../sond_treeviewfm.h"

#include "20allgemein/project.h"


#define ROLLBACK_BOTH(dbase,dbase_work) \
          { gint rc_rollback1 = 0; \
            gchar* err_rollback1 = NULL; \
            gint rc_rollback2 = 0; \
            gchar* err_rollback2 = NULL; \
            \
            rc_rollback1 = dbase_rollback( dbase, &err_rollback1 ); \
            if ( rc_rollback1 && errmsg ) \
                    *errmsg = add_string( g_strdup( "Rollback (store) " \
                    "fehlgeschlagen\n\nBei Aufruf dbase_rollback:\n" ), *errmsg ); \
            rc_rollback2 = dbase_rollback( dbase_work, &err_rollback2 ); \
            \
            if ( rc_rollback2 && errmsg && *errmsg ) \
                    *errmsg = add_string( *errmsg, g_strdup( "\n\n" ) ); \
            \
            if ( rc_rollback1 && errmsg ) \
            { \
                *errmsg = add_string( *errmsg, g_strdup( "Rollback (store) " \
                        "fehlgeschlagen\n\nBei Aufruf dbase_rollback:\n" ) ); \
                *errmsg = add_string( *errmsg, err_rollback2 ); \
            } \
            if ( rc_rollback1 || rc_rollback2 ) return -1; \
          }

#define ERROR_ROLLBACK_BOTH(x) \
          { if ( errmsg ) *errmsg = add_string( \
            g_strdup( "Bei Aufruf " x ":\n" ), *errmsg ); \
            \
            gint rc_rollback1 = 0; \
            gint rc_rollback2 = 0; \
            gchar* err_rollback = NULL; \
            \
            rc_rollback1 = dbase_rollback( dbase, &err_rollback ); \
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


G_BEGIN_DECLS

typedef struct _DBase DBase;
typedef struct _Clipboard Clipboard;


#define ZOND_TYPE_TREEVIEWFM zond_treeviewfm_get_type( )
G_DECLARE_DERIVABLE_TYPE (ZondTreeviewFM, zond_treeviewfm, ZOND, TREEVIEWFM, SondTreeviewFM)


struct _ZondTreeviewFMClass
{
    SondTreeviewFMClass parent_class;
};


ZondTreeviewFM* zond_treeviewfm_new( Clipboard* clipboard );

void zond_treeviewfm_set_zond( ZondTreeviewFM*, Projekt* );




#endif // ZOND_TREEVIEWFM_H_INCLUDED
