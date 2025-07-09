#ifndef ZOND_DBASE_H_INCLUDED
#define ZOND_DBASE_H_INCLUDED

#include <glib-object.h>
#include "global_types.h"

typedef enum {
	ZOND_DBASE_TYPE_BAUM_ROOT,
	ZOND_DBASE_TYPE_BAUM_STRUKT, //1
	ZOND_DBASE_TYPE_BAUM_INHALT_FILE,
	ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_COPY, //3
	ZOND_DBASE_TYPE_BAUM_AUSWERTUNG_LINK,
	ZOND_DBASE_TYPE_FILE_PART, //5
	ZOND_DBASE_TYPE_VIRT_PDF,
	NUM_ZOND_DBASE_TYPES
} NodeType;

#define ERROR_Z_DBASE { if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ), \
                    sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ), \
                    "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) ); \
                    return -1; }

#define ERROR_Z_ROLLBACK { \
        gint res = 0; \
        GError* error_tmp = NULL; \
        \
        res = zond_dbase_rollback_to_statement( zond_dbase, &error_tmp ); \
        \
        if ( error ) \
        { \
            *error = g_error_new( g_quark_from_static_string( "SQLITE3" ), \
                    sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ), \
                    "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) ); \
            \
            if ( res ) \
            { \
                (*error)->message = add_string( (*error)->message, \
                        g_strdup_printf( "\n\nRollback fehlgeschlagen\n%s", error_tmp->message ) ); \
                g_error_free( error_tmp ); \
            } \
            else (*error)->message = add_string( (*error)->message, \
                    g_strdup_printf( "\n\nRollback durchgeführt" ) ); \
        } \
        else \
        { \
            if ( res ) g_error_free( error_tmp ); \
        }\
        \
        return -1; \
    }

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

typedef struct _Section {
	gint ID;
	gchar* section;
} Section;

void section_free(gpointer data) {
	g_free(((Section*) data)->section);

	return;
}

G_BEGIN_DECLS

#define ZOND_TYPE_DBASE zond_dbase_get_type( )
G_DECLARE_DERIVABLE_TYPE(ZondDBase, zond_dbase, ZOND, DBASE, GObject)

struct _ZondDBaseClass {
	GObjectClass parent_class;
};

void zond_dbase_finalize_stmts(sqlite3*);

gint zond_dbase_create_db_maj_1(sqlite3*, GError**);

ZondDBase* zond_dbase_new(const gchar*, gboolean, gboolean, gchar**);

void zond_dbase_close(ZondDBase*);

sqlite3* zond_dbase_get_dbase(ZondDBase*);

const gchar* zond_dbase_get_path(ZondDBase*);

gint zond_dbase_backup(ZondDBase*, ZondDBase*, gchar**);

gint zond_dbase_prepare(ZondDBase*, const gchar*, const gchar**, gint,
		sqlite3_stmt***, GError**);

gint zond_dbase_begin(ZondDBase*, GError**);

gint zond_dbase_commit(ZondDBase*, GError**);

gint zond_dbase_rollback(ZondDBase*, GError**);

gint zond_dbase_test_path(ZondDBase*, const gchar*, GError**);

gint zond_dbase_insert_node(ZondDBase*, gint, gboolean, gint, gint,
		const gchar*, gchar const*, const gchar*, const gchar*, const gchar*,
		GError**);

gint zond_dbase_create_file_root(ZondDBase*, gchar const*, gchar const*,
		gchar const*, gchar const*, gint*, GError**);

gint zond_dbase_update_icon_name(ZondDBase*, gint, const gchar*, GError**);

gint zond_dbase_update_node_text(ZondDBase*, gint, const gchar*, GError**);

gint zond_dbase_update_text(ZondDBase*, gint, const gchar*, GError**);

gint zond_dbase_update_path(ZondDBase*, const gchar*, const gchar*, GError**);

gint zond_dbase_verschieben_knoten(ZondDBase*, gint, gint, gboolean, GError**);

gint zond_dbase_remove_node(ZondDBase*, gint, GError**);

gint zond_dbase_get_node(ZondDBase*, gint, gint*, gint*, gchar**, gchar**,
		gchar**, gchar**, gchar**, GError**);

gint zond_dbase_get_type_and_link(ZondDBase*, gint, gint*, gint*, GError**);

gint zond_dbase_get_text(ZondDBase*, gint, gchar**, GError**);

gint zond_dbase_get_file_part_root(ZondDBase*, const gchar*, gint*, GError**);

gint zond_dbase_get_tree_root(ZondDBase*, gint, gint*, GError**);

gint zond_dbase_get_parent(ZondDBase*, gint, gint*, GError**);

gint zond_dbase_get_first_child(ZondDBase*, gint, gint*, GError**);

gint zond_dbase_get_older_sibling(ZondDBase*, gint, gint*, GError**);

gint zond_dbase_get_younger_sibling(ZondDBase*, gint, gint*, GError**);

gint zond_dbase_get_baum_inhalt_file_from_file_part(ZondDBase*, gint, gint*,
		GError**);

gint zond_dbase_get_baum_auswertung_copy(ZondDBase*, gint, gint*, GError**);

gint zond_dbase_get_first_baum_inhalt_file_child(ZondDBase*, gint, gint*, gint*,
		GError**);

gint zond_dbase_get_section(ZondDBase*, gchar const*, gchar const*, GError**);

gint zond_dbase_find_baum_inhalt_file(ZondDBase*, gint, gint*, gint*, gchar**,
		GError**);

gint zond_dbase_is_file_part_copied(ZondDBase*, gint, gboolean*, GError**);

gint zond_dbase_get_arr_sections(ZondDBase*, gchar const*, GArray**, GError** );

gint zond_dbase_update_section(ZondDBase*, gint, const gchar*, GError**);

G_END_DECLS

#endif // ZOND_DBASE_H_INCLUDED

