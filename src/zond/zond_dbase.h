#ifndef ZOND_DBASE_H_INCLUDED
#define ZOND_DBASE_H_INCLUDED

#include <glib-object.h>
#include "global_types.h"

typedef enum
{
    ZOND_DBASE_TYPE_BAUM_ROOT,
    ZOND_DBASE_TYPE_BAUM_STRUKT,
    ZOND_DBASE_TYPE_BAUM_INHALT_FILE,
    ZOND_DBASE_TYPE_BAUM_INHALT_FILE_PART,
    ZOND_DBASE_TYPE_BAUM_INHALT_PDF_ABSCHNITT,
    ZOND_DBASE_TYPE_BAUM_INHALT_VIRT_PDF,
    ZOND_DBASE_TYPE_BAUM_INHALT_VIRT_PDF_SECTION,
    ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_COPY,
    ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_LINK,
    ZOND_DBASE_TYPE_PDF_ABSCHNITT,
    ZOND_DBASE_TYPE_PDF_PUNKT,
    NUM_ZOND_DBASE_TYPES
} NodeType;



#define ERROR_ZOND_DBASE(x) { if ( errmsg ) *errmsg = add_string( g_strconcat( "Bei Aufruf ", __func__, ":\n", \
                       "Bei Aufruf " x ":\n", \
                       sqlite3_errmsg(zond_dbase_get_dbase(zond_dbase)), NULL ), *errmsg ); \
                       return -1; }

#define ERROR_ROLLBACK(zond_dbase) \
          { if ( errmsg ) *errmsg = add_string( \
            g_strconcat( "Bei Aufruf ", __func__, ":\n", NULL ), *errmsg ); \
            \
            GError* err_rollback = NULL; \
            gint rc_rollback = 0; \
            rc_rollback = zond_dbase_rollback( zond_dbase, &err_rollback ); \
            if ( errmsg ) \
            { \
                if ( !rc_rollback ) *errmsg = add_string( *errmsg, \
                        g_strdup( "\n\nRollback durchgeführt" ) ); \
                else *errmsg = add_string( *errmsg, g_strconcat( "\n\nRollback " \
                        "fehlgeschlagen\n\nBei Aufruf dbase_rollback:\n", \
                        err_rollback->message, "\n\nDatenbankverbindung trennen", NULL ) ); \
            } \
            g_error_free( err_rollback ); \
            \
            return -1; }

#define ERROR_ROLLBACK_Z(zond_dbase) \
          { GError* error_tmp; \
            \
            gint rc_rollback = 0; \
            g_prefix_error( error, "%s\n", __func__ ); \
            \
            rc_rollback = zond_dbase_rollback( zond_dbase, &error_tmp); \
            if ( error ) \
            { \
                if ( !rc_rollback ) (*error)->message = add_string( (*error)->message, \
                        g_strdup( "\n\nRollback durchgeführt" ) ); \
                else \
                { \
                    (*error)->message = add_string( (*error)->message, \
                            g_strdup_printf( "\n\nRollback fehlgeschlagen\n\n" \
                            "Bei Aufruf dbase_rollback:\n%s\n\nDatenbankverbindung trennen", \
                            error_tmp->message ) ); \
                    g_error_free( error_tmp ); \
                } \
            } \
            \
            return -1; }

G_BEGIN_DECLS

#define ZOND_TYPE_DBASE zond_dbase_get_type( )
G_DECLARE_DERIVABLE_TYPE (ZondDBase, zond_dbase, ZOND, DBASE, GObject)

struct _ZondDBaseClass
{
    GObjectClass parent_class;
};


gint zond_dbase_new( const gchar*, gboolean, gboolean, ZondDBase**, gchar** );

void zond_dbase_close( ZondDBase* );

sqlite3* zond_dbase_get_dbase( ZondDBase* );

const gchar* zond_dbase_get_path( ZondDBase* );

gint zond_dbase_backup( ZondDBase*, ZondDBase*, gchar** );

gint zond_dbase_prepare( ZondDBase*, const gchar*, const gchar**,gint,
        sqlite3_stmt***, GError** );

gint zond_dbase_begin( ZondDBase*, GError** );

gint zond_dbase_commit( ZondDBase*, GError** );

gint zond_dbase_rollback( ZondDBase*, GError** );

gint zond_dbase_test_path( ZondDBase*, const gchar*, GError** );

gint zond_dbase_insert_node( ZondDBase*, gint, gboolean, gint, gint,
        const gchar*, gint, gint, gint, gint, const gchar*, const gchar*,
        const gchar*, GError** );

gint zond_dbase_insert_pdf_root( ZondDBase*, const gchar*, gint*, GError** );

gint zond_dbase_update_icon_name( ZondDBase*, gint, const gchar*, GError** );

gint zond_dbase_update_node_text( ZondDBase*, gint, const gchar*, GError** );

gint zond_dbase_update_text( ZondDBase*, gint, const gchar*, GError** );

gint zond_dbase_update_path( ZondDBase*, const gchar*, const gchar*, GError** );

gint zond_dbase_verschieben_knoten( ZondDBase*, gint, gint, gboolean, GError** );

gint zond_dbase_remove_node( ZondDBase*, gint, GError** );

gint zond_dbase_get_node( ZondDBase*, gint, gint*, gint*, gchar**, gint*,
        gint*, gint*, gint*, gchar**, gchar**, gchar**, GError** );

gint zond_dbase_get_type_and_link( ZondDBase*, gint, gint*, gint*, GError** );

gint zond_dbase_get_rel_path( ZondDBase*, gint, gchar**, GError** );

gint zond_dbase_get_text( ZondDBase*, gint, gchar**, GError** );

gint zond_dbase_get_pdf_root( ZondDBase*, const gchar*, gint*, GError** );

gint zond_dbase_get_tree_root( ZondDBase*, gint, gint*, GError** );

gint zond_dbase_get_parent( ZondDBase*, gint, gint*, GError** );

gint zond_dbase_get_first_child( ZondDBase*, gint, gint*, GError** );

gint zond_dbase_get_older_sibling( ZondDBase*, gint, gint*, GError** );

gint zond_dbase_get_younger_sibling( ZondDBase*, gint, gint*, GError** );

gint zond_dbase_get_baum_inhalt_pdf_abschnitt_from_pdf_abschnitt( ZondDBase*,
                    gint, gint*, GError** );

gint zond_dbase_get_baum_inhalt_file_from_rel_path( ZondDBase*,
        const gchar*, gint*, GError** );

gint zond_dbase_get_baum_inhalt_pdf_abschnitt( ZondDBase*,
        gchar const*, Anbindung, gint*, GError** );

G_END_DECLS

#endif // ZOND_DBASE_H_INCLUDED



