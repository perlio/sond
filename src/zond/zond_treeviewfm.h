#ifndef ZOND_TREEVIEWFM_H_INCLUDED
#define ZOND_TREEVIEWFM_H_INCLUDED

#include <glib-object.h>
#include <gtk/gtk.h>

#include "../sond_treeviewfm.h"

#include "global_types.h"

//#include "20allgemein/project.h"


#define ERROR_ROLLBACK_BOTH(zond_dbase_work, zond_dbase_store) \
          { g_prefix_error( error, "%s\n", __func__ ); \
            \
            gint rc_rollback1 = 0; \
            gint rc_rollback2 = 0; \
            GError* err_rollback = NULL; \
            \
            rc_rollback1 = zond_dbase_rollback( zond_dbase_work, &err_rollback ); \
            if ( error ) \
            { \
                if ( !rc_rollback1 ) (*error)->message = add_string( (*error)->message, \
                        g_strdup( "\n\nRollback dbase_store durchgeführt" ) ); \
                else (*error)->message = add_string( (*error)->message, g_strconcat( "\n\nRollback " \
                        "zond_dbase_work fehlgeschlagen\n\nBei Aufruf dbase_rollback:\n", \
                        err_rollback->message, NULL ) ); \
            } \
            g_clear_error( &err_rollback ); \
            \
            rc_rollback2 = zond_dbase_rollback( zond_dbase_store, &err_rollback ); \
            { \
                if ( !rc_rollback2 ) (*error)->message = add_string( (*error)->message, \
                        g_strdup( "\n\nRollback dbase_work durchgeführt" ) ); \
                else (*error)->message = add_string( (*error)->message, g_strconcat( "\n\nRollback " \
                        "zond_dbase_store fehlgeschlagen\n\nBei Aufruf dbase_rollback:\n", \
                        err_rollback->message, NULL ) ); \
            } \
            g_error_free( err_rollback ); \
            \
            if ( error ) \
            { \
                if ( rc_rollback1 || rc_rollback2 ) (*error)->message = \
                        add_string( (*error)->message, g_strdup( "\n\nDatenbank inkonsistent" ) ); \
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

void zond_pdf_abschnitt_set( ZondPdfAbschnitt*, gint, const gchar*,
        Anbindung, const gchar*, const gchar* );

gint zond_pdf_abschnitt_get_ID( ZondPdfAbschnitt* );

void zond_pdf_abschnitt_get( ZondPdfAbschnitt*, gint*, gchar const **,
        Anbindung*, gchar const**, gchar const** );

//ZOND_TYPE_TREEVIEWFM definieren
#define ZOND_TYPE_TREEVIEWFM zond_treeviewfm_get_type( )
G_DECLARE_DERIVABLE_TYPE (ZondTreeviewFM, zond_treeviewfm, ZOND, TREEVIEWFM, SondTreeviewFM)


struct _ZondTreeviewFMClass
{
    SondTreeviewFMClass parent_class;
};

void zond_treeviewfm_set_pda_node_text( ZondTreeviewFM*, gint, gchar const* );

void zond_treeviewfm_set_pda_icon_name( ZondTreeviewFM*, gint, gchar const* );

gint zond_treeviewfm_insert_section( ZondTreeviewFM*, gint, GtkTreeIter*,
        gboolean, GtkTreeIter*, GError** );

gint zond_treeviewfm_section_visible( ZondTreeviewFM*, gchar const*,
        gchar const*, gboolean, gboolean*, GtkTreeIter*, gboolean*, gboolean*, GError** );

gint zond_treeviewfm_set_cursor_on_section( ZondTreeviewFM*, gchar const*,
        gchar const*, GError** );

void zond_treeviewfm_kill_parent( ZondTreeviewFM*, GtkTreeIter* );

gint zond_treeviewfm_get_id_pda( ZondTreeviewFM*, GtkTreeIter*, gint*, GError** );

G_END_DECLS

#endif // ZOND_TREEVIEWFM_H_INCLUDED
