#ifndef DB_H_INCLUDED
#define DB_H_INCLUDED

typedef struct _GtkWidget GtkWidget;
typedef struct st_mysql MYSQL;
typedef int gint;
typedef struct _Sojus Sojus;


gint db_real_connect_database( GtkWidget*, MYSQL*, gchar** );

void db_connect_database( Sojus* );

#endif // DB_H_INCLUDED
