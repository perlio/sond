#ifndef PROJECT_DB_H_INCLUDED
#define PROJECT_DB_H_INCLUDED

typedef int gboolean;
typedef struct sqlite3 sqlite3;
typedef char gchar;
typedef int gint;

typedef struct _Database Database;


gint project_db_finish_database( Database*, gchar** );

gint project_db_backup( sqlite3*, sqlite3*, gchar** );

gboolean project_db_create( sqlite3*, gchar** );

Database* project_db_init_database( gchar*, gchar*, gboolean, gchar** );

#endif // PROJECT_DB_H_INCLUDED
