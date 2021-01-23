#ifndef DB_H_INCLUDED
#define DB_H_INCLUDED

typedef struct st_mysql MYSQL;
typedef struct _GtkWidget GtkWidget;
typedef char gchar;
typedef int gint;
typedef struct _Sojus Sojus;


gint db_get_connection( Sojus* );

gint db_active( GtkWidget*, const gchar*, gchar** );

void db_select( Sojus* );

void db_create( Sojus* );

gint db_connect_database( Sojus*, gboolean );

#endif // DB_H_INCLUDED
