#include "../../misc.h"

#include <gtk/gtk.h>
#include <mariadb/mysql.h>


gint
sql_log( GtkWidget* window, MYSQL* con, gchar* sql_log_str, gchar* nutzer )
{
    gchar* sql = NULL;
    gint rc = 0;

    sql = g_strconcat( "INSERT INTO log VALUES (NOW(3), \"",
            sql_log_str, "\", '", nutzer, "' )", NULL );

    rc = mysql_query( con, sql );
    if ( rc ) display_message( window, "Fehler bei sql_log:\n", mysql_error(
            con ), NULL );

    g_free( sql );

    return rc;
}


