/*
sojus (sojus_database.c) - Akten, Beweisstücke, Unterlagen
Copyright (C) 2021  pelo america

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/


#include <stdlib.h>
#include <stdio.h>
#include <glib.h>
#include <mysql/mysql.h>

#include "../misc.h"


gint
sojus_database_update_property( MYSQL* con, gint ID_property, const gchar* value, gchar** errmsg )
{
    gchar* sql = NULL;
    gint rc = 0;

    sql = g_strdup_printf( "UPDATE properties SET value=%s WHERE entity=%i;",
            value, ID_property );

    rc = mysql_query( con, sql );
    g_free( sql );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf mysql_query:\n",
                mysql_error( con ), NULL );

        return -1;
    }

    return 0;
}


gint
sojus_database_delete_property( MYSQL* con, gint ID_property, gchar** errmsg )
{
    gchar* sql = NULL;
    gint rc = 0;
    MYSQL_RES* mysql_res = NULL;
    MYSQL_ROW row = NULL;

    sql = g_strdup_printf( "DELETE FROM properties WHERE entity=%i; "
            "DELETE vom rels WHERE object=%i;", ID_property, ID_property );

    rc = mysql_query( con, sql );
    g_free( sql );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf mysql_query:\n",
                mysql_error( con ), NULL );

        return -1;
    }

    sql = g_strdup_printf( "SELECT object FROM rels WHERE subject=%i;", ID_property );

    rc = mysql_query( con, sql );
    g_free( sql );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf mysql_query:\n",
                mysql_error( con ), NULL );

        return -1;
    }

    mysql_res = mysql_store_result( con );
    if ( !mysql_res )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf mysql_store_result:\n",
                mysql_error( con ), NULL );

        return -1;
    }

    while ( (row = mysql_fetch_row( mysql_res )) )
    {
        gint rc = 0;

        rc = sojus_database_delete_property( con, atoi( row[0] ), errmsg );
        if ( rc ) ERROR_SOND( "sojus_database_detete_property" )
    }

    mysql_free_result( mysql_res );

    return 0;
}
