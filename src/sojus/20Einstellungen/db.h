#ifndef DB_H_INCLUDED
#define DB_H_INCLUDED

typedef struct _GtkWidget GtkWidget;
typedef struct st_mysql MYSQL;
typedef int gint;
typedef struct _Sojus Sojus;


gint db_connect_database( Sojus*, GtkWidget*, MYSQL* );

gint db_get_connection( Sojus* );

#endif // DB_H_INCLUDED
