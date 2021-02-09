#ifndef PROJECT_DB_H_INCLUDED
#define PROJECT_DB_H_INCLUDED

typedef int gboolean;
typedef struct sqlite3 sqlite3;
typedef char gchar;
typedef int gint;

typedef struct _Database Database;


void project_db_destroy_stmts( Database* );

gint project_db_create_stmts( Database*, gchar** );

gboolean project_db_create( sqlite3*, gchar** );

#endif // PROJECT_DB_H_INCLUDED
