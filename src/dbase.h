#ifndef DBASE_H_INCLUDED
#define DBASE_H_INCLUDED

typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;
typedef char gchar;
typedef int gint;
typedef struct _Eingang Eingang;

typedef struct _DBase
{
    sqlite3* db;
    sqlite3_stmt* get_eingang_for_rel_path;
    sqlite3_stmt* insert_eingang;
    sqlite3_stmt* update_eingang;
    sqlite3_stmt* insert_eingang_rel_path;
    sqlite3_stmt* update_eingang_rel_path;
    sqlite3_stmt* delete_eingang_rel_path;
    sqlite3_stmt* get_num_of_refs_to_eingang;
    sqlite3_stmt* delete_eingang;
} DBase;


gint dbase_get_eingang_for_rel_path( DBase*, const gchar*, gint*, Eingang*, gint*, gchar** );

gint dbase_insert_eingang( DBase*, Eingang*, gchar** );

gint dbase_update_eingang( DBase*, const gint, Eingang*, gchar** );

gint dbase_insert_eingang_rel_path( DBase*, const gint, const gchar*, gchar** );

gint dbase_update_eingang_rel_path( DBase*, const gint, const gint, const gchar*,
        gchar** );

gint dbase_delete_eingang_rel_path( DBase*, const gint, gchar** );

gint dbase_get_num_of_refs_to_eingang( DBase*, const gint, gchar** );

gint dbase_delete_eingang( DBase*, const gint, gchar** );

gint dbase_create_with_stmts( const gchar*, DBase**, sqlite3*, gchar** );

#endif // DBASE_H_INCLUDED
