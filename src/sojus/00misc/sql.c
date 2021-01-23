#include <gtk/gtk.h>
#include <mariadb/mysql.h>

#include "../global_types_sojus.h"

#include "../../misc.h"


gint
sql_log( Sojus* sojus, const gchar* sql_log_str )
{
    gchar* sql = NULL;
    gint rc = 0;
    gchar* user = g_settings_get_string( sojus->settings, "user" );

    sql = g_strconcat( "INSERT INTO log VALUES (NOW(3), \"",
            sql_log_str, "\", '", user, "' )", NULL );

    g_free( user );

    rc = mysql_query( sojus->db.con, sql );
    g_free( sql );
    if ( rc ) display_message( sojus->app_window, "Fehler bei sql_log:\n",
            mysql_error( sojus->db.con ), NULL );


    return rc;
}


