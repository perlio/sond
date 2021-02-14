#ifndef PROJECT_DB_H_INCLUDED
#define PROJECT_DB_H_INCLUDED

typedef char gchar;
typedef int gint;

typedef struct _Database Database;


gint project_db_create_stmts( Database*, gchar** );

#endif // PROJECT_DB_H_INCLUDED
