#ifndef SOJUS_DATABASE_H_INCLUDED
#define SOJUS_DATABASE_H_INCLUDED

#include <mysql/mysql.h>

typedef int gint;
typedef char gchar;

gint sojus_adressen_insert_node( MYSQL*, gint, gchar** );

gint sojus_database_insert_property( MYSQL*, gint, gint, gint, const gchar*, gchar** );

gint sojus_database_update_property( MYSQL*, gint, const gchar*, gchar** );

gint sojus_database_delete_property( MYSQL*, gint, gchar** );


#endif // SOJUS_DATABASE_H_INCLUDED
