#ifndef DB_H_INCLUDED
#define DB_H_INCLUDED

typedef struct st_mysql MYSQL;
typedef struct _GtkWidget GtkWidget;
typedef char gchar;
typedef int gint;
typedef struct _Sojus Sojus;


MYSQL* db_connect( GtkWidget*, const gchar*, const gchar*, const gchar*, gint,
        gchar** );

gint db_active( GtkWidget*, const gchar*, gchar** );

void db_connection_window( Sojus* );

void db_select( Sojus* );

void db_create( Sojus* );

#endif // DB_H_INCLUDED
