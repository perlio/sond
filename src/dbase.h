#ifndef DBASE_H_INCLUDED
#define DBASE_H_INCLUDED

#define ERROR_DBASE(x) { if ( errmsg ) *errmsg = add_string( g_strconcat( "Bei Aufruf " x ":\n", \
                       sqlite3_errmsg(dbase->db), NULL ), *errmsg ); \
                       return -1; }

#define ERROR_ROLLBACK(dbase,x) \
          { if ( errmsg ) *errmsg = add_string( \
            g_strdup( "Bei Aufruf " x ":\n" ), *errmsg ); \
            \
            gchar* err_rollback = NULL; \
            gint rc_rollback = 0; \
            rc_rollback = dbase_rollback( dbase, &err_rollback ); \
            if ( errmsg ) \
            { \
                if ( !rc_rollback ) *errmsg = add_string( *errmsg, \
                        g_strdup( "\n\nRollback durchgeführt" ) ); \
                else *errmsg = add_string( *errmsg, g_strconcat( "\n\nRollback " \
                        "fehlgeschlagen\n\nBei Aufruf dbase_rollback:\n", \
                        err_rollback, "\n\nDatenbankverbindung trennen", NULL ) ); \
            } \
            g_free( err_rollback ); \
            \
            return -1; }


typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;
typedef char gchar;
typedef int gint;
typedef struct _Eingang Eingang;

typedef struct _DBase
{
    sqlite3* db;
    sqlite3_stmt* begin_transaction;
    sqlite3_stmt* commit;
    sqlite3_stmt* rollback;
    sqlite3_stmt* update_path;
    sqlite3_stmt* test_path;
    sqlite3_stmt* get_eingang_for_rel_path;
    sqlite3_stmt* insert_eingang;
    sqlite3_stmt* update_eingang;
    sqlite3_stmt* insert_eingang_rel_path;
    sqlite3_stmt* update_eingang_rel_path;
} DBase;


gint dbase_begin( DBase*, gchar** );

gint dbase_commit( DBase*, gchar** );

gint dbase_rollback( DBase*, gchar** );

gint dbase_update_path( DBase*, const gchar*, const gchar*, gchar** );

gint dbase_test_path( DBase*, const gchar*, gchar** );

gint dbase_get_eingang_for_rel_path( DBase*, const gchar*, gint*, Eingang**, gint*, gchar** );

gint dbase_insert_eingang( DBase*, Eingang*, gchar** );

gint dbase_update_eingang( DBase*, const gint, Eingang*, gchar** );

gint dbase_insert_eingang_rel_path( DBase*, const gint, const gchar*, gchar** );

gint dbase_update_eingang_rel_path( DBase*, const gint, const gint, const gchar*,
        gchar** );

void dbase_destroy( DBase* );

sqlite3_stmt* dbase_prepare_stmt( sqlite3*, const gchar*, gchar** );

gint dbase_prepare_stmts( DBase*, gchar** );

gint dbase_create_db( sqlite3*, gchar** );

gint dbase_open( const gchar*, DBase*, gboolean, gboolean, gchar** );

gint dbase_create_with_stmts( const gchar*, DBase**, gboolean, gboolean, gchar** );

#endif // DBASE_H_INCLUDED