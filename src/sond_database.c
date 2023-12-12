/*
sond (sond_database.c) - Akten, Beweisstücke, Unterlagen
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


#include <gtk/gtk.h>
#include <sqlite3.h>
#include <mariadb/mysql.h>

#include "misc.h"
#include "sond_database.h"
#include "zond/zond_dbase.h"


/*
gboolean
sond_database_is_valid_time( const gchar* time )
{
    gchar* ptr = NULL;
    gboolean res = TRUE;

    if ( !time ) return FALSE;
    if ( !(strlen( time ) == 4 ||
           strlen( time ) == 7 ||
           strlen( time ) == 10 ||
           strlen( time ) == 13 ||
           strlen( time ) == 16 ||
           strlen( time ) == 19) ) return FALSE;

    ptr = time;

    for (gint i = 0; i < 4; i++ )
    {
        if ( time[i] < '0' || time[i] > '9' ) return FALSE;
    }

    if ( time[4] = NULL )
    {
        if ( precision ) *precision = 1;
        return TRUE;
    }
    else if ( time[4] != '-' ) return FALSE;

    if ( !(time[5] == '0' && time[6] >= '1' && time[6] <= '9') &&
            !(time[5] == '1' && time[6] >= '0' && time[6] <= 2) ) return FALSE;

    if ( time[7] == NULL )
    {
        if ( precision ) *precision = 2;
        return TRUE;
    }
    else if ( time[7] != '-' ) return FALSE;
}*/

gint
sond_database_add_to_database( gpointer database, GError** error )
{

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        gchar* errmsg = NULL;
        const gchar* sql = "CREATE TABLE IF NOT EXISTS entities ( "
                "ID INTEGER, "
                "type INTEGER NOT NULL, "
                "ID_subject INTEGER, "
                "ID_object INTEGER, "
                "prop_value TEXT, "
                "FOREIGN KEY (ID_subject) REFERENCES entities (ID), "
                "FOREIGN KEY (ID_object) REFERENCES entities (ID), "
                "PRIMARY KEY (ID) "
                "); ";

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = sqlite3_exec( zond_dbase_get_dbase( zond_dbase ), sql, NULL, NULL, &errmsg );
        if ( rc != SQLITE_OK )
        {

        }
    }
    else //mysql
    {
        gint rc = 0;
        MYSQL* con = (MYSQL*) database;
        const gchar* sql = "CREATE TABLE IF NOT EXISTS entities( "
                "ID INTEGER PRIMARY KEY AUTO_INCREMENT, "
                "type INTEGER NOT NULL, "
                "ID_subject INTEGER, "
                "ID_object INTEGER, "
                "prop_value TEXT, "
                "FOREIGN KEY (ID_subject) REFERENCES entities (ID), "
                "FOREIGN KEY (ID_object) REFERENCES entities (ID) "
                "); ";

        rc = mysql_query( con, sql );
        if ( rc )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "MYSQL" ),
                    mysql_errno( con ), "%s\n%s\n\nFehlermeldung: %s", __func__,
                    "mysql_query (CREATE TABLE)", mysql_error( con ) );
            return -1;
        }
   }

    return 0;
}


gint
sond_database_begin( gpointer database, GError** error )
{
    gint rc = 0;

    MYSQL* con = (MYSQL*) database;

    rc = mysql_query( con, "BEGIN;" );
    if ( rc )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "MYSQL" ),
                mysql_errno( con ), "%s\n%s\n\nFehlermeldung: %s", __func__,
                "mysql_query (BEGIN)", mysql_error( con ) );
        return -1;
    }

    return 0;
}


gint
sond_database_commit( gpointer database, GError** error )
{
    gint rc = 0;

    MYSQL* con = (MYSQL*) database;

    rc = mysql_query( con, "COMMIT;" );
    if ( rc )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "MYSQL" ),
                mysql_errno( con ), "%s\n%s\n\nFehlermeldung: %s", __func__,
                "mysql_query (COMMIT)", mysql_error( con ) );
        return -1;
    }

    return 0;
}


gint
sond_database_rollback( gpointer database, GError** error )
{
    gint rc = 0;

    MYSQL* con = (MYSQL*) database;

    rc = mysql_query( con, "ROLLBACK;" );
    if ( rc )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "MYSQL" ),
                mysql_errno( con ), "%s\n%s\n\nFehlermeldung: %s", __func__,
                "mysql_query (ROLLBACK)", mysql_error( con ) );
        return -1;
    }

    return 0;
}


static gint
sond_database_insert_row( gpointer database, Type type, gint ID_subject, gint ID_object,
        const gchar* prop_value, GError** error )
{
    gint new_node_id = 0;

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        gchar* errmsg = NULL;
        const gchar* sql[] = {
                "INSERT INTO entities (type, ID_subject, ID_object, prop_value) "
                "VALUES (?1,?2,?3,?4);",

                "SELECT (last_insert_rowid()); " };

        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, &errmsg );
        if ( rc )
        {
            if ( error ) *error = g_error_new( ZOND_ERROR, 0,
                    "%s\n%s\n\nFehlermeldung: %s", __func__, "zond_dbase_prepare", errmsg );
            g_free( errmsg );

            return -1;
        }

        rc = sqlite3_bind_int( stmt[0], 1, type );
        if ( rc != SQLITE_OK )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                    rc, "%s\n%s\n\nFehlermeldung: %s", __func__, "sqlite3_bind_int (type)", sqlite3_errmsg( database ) );

            return -1;
        }

        if ( ID_subject )
        {
            gint rc = 0;
            rc = sqlite3_bind_int( stmt[0], 2, ID_subject );
            if ( rc != SQLITE_OK )
            {
                if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                        rc, "%s\n%s\n\nFehlermeldung: %s", __func__, "sqlite3_bind_int (ID_subject)",
                        sqlite3_errmsg( database ) );
                return -1;
            }
        }
        else
        {
            rc = sqlite3_bind_null( stmt[0], 2 );
            if ( rc != SQLITE_OK )
            {
                if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                        rc, "%s\n%s\n\nFehlermeldung: %s", __func__, "sqlite3_bind_null", sqlite3_errmsg( database ) );
                return -1;
            }
        }

        if ( ID_object )
        {
            rc = sqlite3_bind_int( stmt[0], 3, ID_object );
            if ( rc != SQLITE_OK )
            {
                if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                        rc, "%s\n%s\n\nFehlermeldung: %s", __func__, "sqlite3_bind_int (ID_object)", sqlite3_errmsg( database ) );
                return -1;
            }
        }
        else
        {
            rc = sqlite3_bind_null( stmt[0], 3 );
            if ( rc != SQLITE_OK )
            {
                if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                        rc, "%s\n%s\n\nFehlermeldung: %s", __func__, "sqlite3_bind_null", sqlite3_errmsg( database ) );
                return -1;
            }
        }

        //bind_null, wenn prop_value == NULL
        rc = sqlite3_bind_text( stmt[0], 4, prop_value, -1, NULL );
        if ( rc != SQLITE_OK )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                    rc, "%s\n%s\n\nFehlermeldung: %s", __func__, "sqlite3_bind_text", sqlite3_errmsg( database ) );
            return -1;
        }


        rc = sqlite3_step( stmt[0] );
        if ( rc != SQLITE_DONE )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                    rc, "%s\n%s\n\nFehlermeldung: %s", __func__, "sqlite3_step", sqlite3_errmsg( database ) );
            return -1;
        }

        rc = sqlite3_step( stmt[1] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                    rc, "%s\n%s\n\nFehlermeldung: %s", __func__, "sqlite3_step", sqlite3_errmsg( database ) );
            return -1;
        }


        if ( rc == SQLITE_ROW ) new_node_id = sqlite3_column_int( stmt[1], 0 );
        else
        {
            if ( error ) *error = g_error_new( SOND_DATABASE_ERROR, SOND_DATABASE_ERROR_NORESULT,
                    "%s\n\nFehlermeldung: %s", __func__, "Ergebnis der Operation konnte nicht abgefragt werden" );
            return -1;
        }
    }
    else //mySQL-con wird übergeben
    {
        gint rc = 0;
        gchar* sql_1 = NULL;
        const gchar* sql_2 = "SELECT (LAST_INSERT_ID()); ";
        MYSQL_RES* mysql_res = NULL;
        MYSQL_ROW mysql_row = NULL;
        gchar* string_subject = NULL;
        gchar* string_object = NULL;
        gchar* prop_value_sql = NULL;

        MYSQL* con = (MYSQL*) database;

        if ( ID_subject ) string_subject = g_strdup_printf( "%i", ID_subject );
        else string_subject = g_strdup( "NULL" );

        if ( ID_object ) string_object = g_strdup_printf( "%i", ID_object );
        else string_object = g_strdup( "NULL" );

        if ( prop_value ) prop_value_sql = g_strdup_printf( "'%s'", prop_value );

        sql_1 = g_strdup_printf( "INSERT INTO entities (type, ID_subject, ID_object, prop_value) "
                "VALUES (%i,%s,%s,%s);", type, string_subject, string_object, (prop_value) ? prop_value_sql : "NULL" );

        g_free( prop_value_sql );

        g_free( string_subject );
        g_free( string_object );

        rc = mysql_query( con, sql_1 );
        g_free( sql_1 );
        if ( rc )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ), mysql_errno( database ),
                    "%s\n%s\n\nFehlermeldung: %s", __func__, "mysql_query (sql_1)", mysql_error( con ) );
            return -1;
        }

        rc = mysql_query( con, sql_2 );
        if ( rc )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ), mysql_errno( database ),
                    "%s\n%s\n\nFehlermeldung: %s", __func__, "mysql_query (sql_2)", mysql_error( con ) );
            return -1;
        }

        mysql_res = mysql_store_result( con );
        if ( !mysql_res )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ), mysql_errno( database ),
                    "%s\n%s\n\nFehlermeldung: %s", __func__, "mysql_store_result (sql_2)", mysql_error( con ) );
            return -1;
        }

        mysql_row = mysql_fetch_row( mysql_res );
        if ( !mysql_row )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ), mysql_errno( database ),
                    "%s\n%s\n\nFehlermeldung: %s", __func__, "mysql_fetch_row (sql_2)", mysql_error( con ) );
            return -1;
        }

        new_node_id = atoi( mysql_row[0] );

        mysql_free_result( mysql_res );

    }

    return new_node_id;
}


gint
sond_database_insert_entity( gpointer database, Type type, GError** error )
{
    gint rc = 0;

    rc = sond_database_insert_row( database, type, 0, 0, NULL, error );
    g_prefix_error( error, "%s\n", __func__ );

    return rc;
}


gint
sond_database_insert_rel( gpointer database, Type type, gint ID_subject,
        gint ID_object, GError** error )
{
    gint rc = 0;

    rc = sond_database_insert_row( database, type, ID_subject, ID_object, NULL, error );
    g_prefix_error( error, "%s\n", __func__ );

    return rc;
}


gint
sond_database_insert_property( gpointer database, Type type, gint ID_subject,
        const gchar* prop_value, GError** error )
{
    gint rc = 0;

    rc = sond_database_insert_row( database, type, ID_subject, 0, prop_value, error );
    g_prefix_error( error, "%s\n", __func__ );

    return rc;
}


GArray*
sond_database_get_properties( gpointer database, gint ID_subject, GError** error )
{
    GArray* arr_properties = NULL;

    const gchar* sql[] = {
            "SELECT ID, type, prop_value FROM entities WHERE ID_subject=@1; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        gchar* errmsg = NULL;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, &errmsg );
        if ( rc )
        {
            if ( error ) *error = g_error_new( ZOND_ERROR, 0,
                    "%s\nzond_dbase_prepare\n%s", __func__, errmsg );
            g_free( errmsg );

            return NULL;
        }

        rc = sqlite3_bind_int( stmt[0], 1, ID_subject );
        if ( rc != SQLITE_OK )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                    sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                    "%s\nzond_dbase_prepare\n%s", __func__,
                    sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

            return NULL;
        }

        arr_properties = g_array_new( FALSE, FALSE, sizeof( Property ) );
        g_array_set_clear_func( arr_properties, (GDestroyNotify) sond_database_clear_property );
        do
        {
            Property property = { 0 };

            rc = sqlite3_step( stmt[0] );
            if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
            {
                g_array_unref( arr_properties );
                *error = g_error_new( g_quark_from_static_string( "SQLITE" ),
                        sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                        "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

                return NULL;
            }
            else if ( rc == SQLITE_DONE ) break;

            property.entity.ID = sqlite3_column_int( stmt[0], 0 );
            property.entity.type = sqlite3_column_int( stmt[0], 1 );
            property.ID_subject = ID_subject;
            property.value = g_strdup( (const gchar*) sqlite3_column_text( stmt[0], 2 ) );
            g_array_append_val( arr_properties, property );
        } while ( rc == SQLITE_ROW );
    }
    else //mysql
    {
        gint rc = 0;
        MYSQL* con = NULL;
        MYSQL_RES* mysql_res = NULL;
        MYSQL_ROW row = NULL;
        gchar* sql_mariadb = NULL;

        con = (MYSQL*) database;

        sql_mariadb = g_strdup_printf( "SET @1=%i; ", ID_subject );
        sql_mariadb = add_string( sql_mariadb, g_strdup( sql[0] ) );

        rc = mysql_query( con, sql_mariadb );
        g_free( sql_mariadb );
        if ( rc )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ),
                    mysql_errno( con ), "%s\nmysql_query\n%s", __func__, mysql_error( con ) );

            return NULL;
        }

        mysql_res = mysql_store_result( con );
        if ( !mysql_res && mysql_field_count( con ) != 0 )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ),
                    mysql_errno( con ), "%s\nmysql_store_results\n%s", __func__, mysql_error( con ) );

            return NULL;
        }

        mysql_free_result( mysql_res );

        rc = mysql_next_result( con );
        if ( rc == - 1 )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "SOND" ), 0,
                    "%s\nKein weiteres result-set vorhanden", __func__ );

            return NULL;
        }
        else if ( rc > 0 )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ),
                    mysql_errno( con ), "%s\nmysql_next_result\n%s", __func__, mysql_error( con ) );

            return NULL;
        }

        mysql_res = mysql_store_result( con );
        if ( !mysql_res && mysql_field_count( con ) != 0 )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ),
                    mysql_errno( con ), "%s\nmysql_store_results\n%s", __func__, mysql_error( con ) );

            return NULL;
        }
        else if ( mysql_field_count( con ) == 0 )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "SOND" ), 0,
                    "%s\nresult_set ist leer", __func__ ); //heißt nicht, daß keine Ergebnisse!

            return NULL;
        }

        arr_properties = g_array_new( FALSE, FALSE, sizeof( Property ) );
        g_array_set_clear_func( arr_properties, (GDestroyNotify) sond_database_clear_property );
        while ( (row = mysql_fetch_row( mysql_res )) )
        {
            Property property = { 0 };

            property.entity.ID = atoi( row[0] );
            property.entity.type = atoi( row[1] );
            property.ID_subject = ID_subject;
            property.value = g_strdup( row[2] );
            g_array_append_val( arr_properties, property );
        }

        mysql_free_result( mysql_res );
    }

    return arr_properties;
}


GArray*
sond_database_get_properties_of_type( gpointer database, gint type, gint ID_subject, GError** error )
{
    GArray* arr_properties = NULL;

    const gchar* sql[] = {
            "SELECT ID, prop_value FROM entities WHERE type=@1 AND rel_subject=@2; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        gchar* errmsg = NULL;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, &errmsg );
        if ( rc )
        {
            if ( error ) *error = g_error_new( ZOND_ERROR, 0,
                    "%s\nzond_dbase_prepare\n%s", __func__, errmsg );
            g_free( errmsg );

            return NULL;
        }

        rc = sqlite3_bind_int( stmt[0], 1, type );
        if ( rc != SQLITE_OK )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                    sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                    "%s\nzond_dbase_prepare\n%s", __func__,
                    sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

            return NULL;
        }

        rc = sqlite3_bind_int( stmt[0], 2, ID_subject );
        if ( rc != SQLITE_OK )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                    sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                    "%s\nzond_dbase_prepare\n%s", __func__,
                    sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

            return NULL;
        }

        arr_properties = g_array_new( FALSE, FALSE, sizeof( Property ) );
        g_array_set_clear_func( arr_properties, (GDestroyNotify) sond_database_clear_property );
        do
        {
            Property property = { 0 };

            rc = sqlite3_step( stmt[0] );
            if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
            {
                g_array_unref( arr_properties );
                *error = g_error_new( g_quark_from_static_string( "SQLITE" ),
                        sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                        "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

                return NULL;
            }
            else if ( rc == SQLITE_DONE ) break;

            property.entity.ID = sqlite3_column_int( stmt[0], 0 );
            property.entity.type = type;
            property.ID_subject = ID_subject;
            property.value = g_strdup( (const gchar*) sqlite3_column_text( stmt[0], 1 ) );
            g_array_append_val( arr_properties, property );
        } while ( rc == SQLITE_ROW );
    }
    else //mysql
    {
        gint rc = 0;
        MYSQL* con = NULL;
        MYSQL_RES* mysql_res = NULL;
        MYSQL_ROW row = NULL;
        gchar* sql_mariadb = NULL;

        con = (MYSQL*) database;

        sql_mariadb = g_strdup_printf( "SET @1=%i; SET @2=%i; ", type, ID_subject );
        sql_mariadb = add_string( sql_mariadb, g_strdup( sql[0] ) );

        rc = mysql_query( con, sql_mariadb );
        g_free( sql_mariadb );
        if ( rc )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ),
                    mysql_errno( con ), "%s\nmysql_query\n%s", __func__, mysql_error( con ) );

            return NULL;
        }

        //SET = @1;
        mysql_res = mysql_store_result( con );
        if ( !mysql_res && mysql_field_count( con ) != 0 )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ),
                    mysql_errno( con ), "%s\nmysql_store_results\n%s", __func__, mysql_error( con ) );

            return NULL;
        }

        mysql_free_result( mysql_res );

        rc = mysql_next_result( con );
        if ( rc == - 1 )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "SOND" ), 0,
                    "%s\nKein weiteres result-set vorhanden", __func__ );

            return NULL;
        }
        else if ( rc > 0 )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ),
                    mysql_errno( con ), "%s\nmysql_next_result\n%s", __func__, mysql_error( con ) );

            return NULL;
        }

        //SET = @2;
        mysql_res = mysql_store_result( con );
        if ( !mysql_res && mysql_field_count( con ) != 0 )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ),
                    mysql_errno( con ), "%s\nmysql_store_results\n%s", __func__, mysql_error( con ) );

            return NULL;
        }

        mysql_free_result( mysql_res );

        rc = mysql_next_result( con );
        if ( rc == - 1 )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "SOND" ), 0,
                    "%s\nKein weiteres result-set vorhanden", __func__ );

            return NULL;
        }
        else if ( rc > 0 )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ),
                    mysql_errno( con ), "%s\nmysql_next_result\n%s", __func__, mysql_error( con ) );

            return NULL;
        }

        //SELECT ...
        mysql_res = mysql_store_result( con );
        if ( !mysql_res && mysql_field_count( con ) != 0 )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ),
                    mysql_errno( con ), "%s\nmysql_store_results\n%s", __func__, mysql_error( con ) );

            return NULL;
        }
        else if ( mysql_field_count( con ) == 0 )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "SOND" ), 0,
                    "%s\nresult_set ist leer", __func__ ); //heißt nicht, daß keine Ergebnisse!

            return NULL;
        }

        arr_properties = g_array_new( FALSE, FALSE, sizeof( Property ) );
        g_array_set_clear_func( arr_properties, (GDestroyNotify) sond_database_clear_property );
        while ( (row = mysql_fetch_row( mysql_res )) )
        {
            Property property = { 0 };

            property.entity.ID = atoi( row[0] );
            property.entity.type = type;
            property.ID_subject = ID_subject;
            property.value = g_strdup( row[1] );
            g_array_append_val( arr_properties, property );
        }

        mysql_free_result( mysql_res );
    }

    return arr_properties;
}












gint
sond_database_update_label( gpointer database, gint ID_entity, gint ID_label,
        gchar** errmsg )
{
    const gchar* sql[] = {
            "UPDATE entities SET ID_label=@1 WHERE ID=@2; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_label );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label)" )

        rc = sqlite3_bind_int( stmt[0], 2, ID_entity );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_entity)" )

        rc = sqlite3_step( stmt[0] );
        if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )
    }
    else //mysql
    {

    }

    return 0;
}


gint
sond_database_label_is_equal_or_parent( gpointer database, gint ID_label_parent, gint ID_label_test, gchar** errmsg )
{
    const gchar* sql[] = {
            "(WITH RECURSIVE cte_labels (ID) AS ( "
                "VALUES (@1) "
                "UNION ALL "
                "SELECT labels.ID "
                    "FROM labels JOIN cte_labels WHERE "
                    "labels.parent = cte_labels.ID "
                ") SELECT ID AS ID_CTE FROM cte_labels) "
                "WHERE ID=@2; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_label_parent );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label_parent)" )

        rc = sqlite3_bind_int( stmt[0], 2, ID_label_test );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label_test)" )

        rc = sqlite3_step( stmt[0] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )

        if ( rc == SQLITE_DONE ) return 1;
    }
    else //mysql
    {

    }

    return 0;
}


gint
sond_database_get_ID_label_for_entity( gpointer database, gint ID_entity, gchar** errmsg )
{
    const gchar* sql[] = {
            "SELECT ID_label FROM entities WHERE ID=?1; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_entity );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_entity)" )

        rc = sqlite3_step( stmt[0] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )
        else if ( rc == SQLITE_DONE ) ERROR_S_MESSAGE( "ID_entity existiert nicht" )

        return sqlite3_column_int( stmt[0], 0 );
    }
    else //mysql
    {

    }

    return 0;
}


gint
sond_database_get_entities_for_label( gpointer database, gint ID_label,
        GArray** arr_entities, gchar** errmsg )
{
    const gchar* sql[] = {
            "SELECT entities.ID FROM entities JOIN "
            "(WITH RECURSIVE cte_labels (ID) AS ( "
                "VALUES (@1) "
                "UNION ALL "
                "SELECT labels.ID "
                    "FROM labels JOIN cte_labels WHERE "
                    "labels.parent = cte_labels.ID "
                ") SELECT ID AS ID_CTE FROM cte_labels) "
                "ON entities.ID_label = ID_CTE; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_label );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label)" )

        *arr_entities = g_array_new( FALSE, FALSE, sizeof( gint ) );
        do
        {
            rc = sqlite3_step( stmt[0] );
            if ( rc != SQLITE_ROW && rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )
            else if ( rc == SQLITE_ROW )
            {
                gint ID_entity = 0;

                ID_entity = sqlite3_column_int( stmt[0], 0 );
                g_array_append_val( *arr_entities, ID_entity );
            }
        } while ( rc == SQLITE_ROW );
    }
    else //mysql
    {

    }

    return 0;
}


/** findet zu subject alle obejcts mit
    - ID_label_rel und
    - ID_label_object (einschließlich dessen Kinder) heraus **/
static gint
sond_database_get_object_for_subject_one_step( gpointer database, gint ID_entity_subject,
        gint ID_label_rel, gint ID_label_object, GArray** arr_objects, gchar** errmsg )
{
    gint cnt = 0;
    const gchar* sql[] = {
            "SELECT rels.entity_object FROM "
                "rels JOIN "
                "entities AS e1 JOIN "
                "entities AS e2 JOIN "
                "(WITH RECURSIVE cte_labels (ID) AS ( "
                    "VALUES (@1) "
                    "UNION ALL "
                    "SELECT labels.ID "
                        "FROM labels JOIN cte_labels WHERE "
                        "labels.parent = cte_labels.ID "
                    ") SELECT ID AS ID_CTE FROM cte_labels) "
                "WHERE "
                    "rels.entity_subject=@2 AND "
                    "rels.entity_rel=e1.ID AND "
                    "e1.ID_label=@3 AND "
                    "rels.entity_object=e2.ID AND "
                    "e2.ID_label=ID_CTE; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_label_object );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label_object)" )

        rc = sqlite3_bind_int( stmt[0], 2, ID_entity_subject );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_entity_subject)" )

        rc = sqlite3_bind_int( stmt[0], 3, ID_label_rel );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label_rel)" )

        do
        {
            rc = sqlite3_step( stmt[0] );
            if ( rc != SQLITE_ROW && rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )
            else if ( rc == SQLITE_ROW )
            {
                gint ID_entity = 0;

                ID_entity = sqlite3_column_int( stmt[0], 0 );
                g_array_append_val( *arr_objects, ID_entity );
                cnt++;
            }
        } while ( rc == SQLITE_ROW );
    }
    else //mysql
    {

    }

    return cnt;

}


/** findet zu subject alle objects mit
    - ID_label_rel und
    - ID_label_object (einschließlich dessen Kinder) heraus

    Sofern mehrere Parameter-Paare angegeben sind, werden Ketten gebildet **/
gint
sond_database_get_object_for_subject( gpointer database, gint ID_entity_subject,
        GArray** arr_objects, gchar** errmsg, ... )
{
    va_list ap;
    gint ID_label_rel = 0;
    GArray* arr_objects_tmp = NULL;

    arr_objects_tmp = g_array_new( FALSE, FALSE, sizeof( gint ) );
    g_array_append_val( arr_objects_tmp, ID_entity_subject );

    va_start( ap, errmsg );

    while ( (ID_label_rel = va_arg( ap, gint )) != -1 )
    {
        gint ID_label_object = 0;
        GArray* arr_results = NULL;

        arr_results = g_array_new( FALSE, FALSE, sizeof( gint ) );

        ID_label_object = va_arg( ap, gint );

        for ( gint i = 0; i < arr_objects_tmp->len; i++ )
        {
            gint rc = 0;
            gint ID_entity_subject_tmp = 0;

            ID_entity_subject_tmp = g_array_index( arr_objects_tmp, gint, i );

            rc = sond_database_get_object_for_subject_one_step( database,
                    ID_entity_subject_tmp, ID_label_rel, ID_label_object, &arr_results, errmsg );
            if ( rc == -1 )
            {
                g_array_unref( arr_results );
                g_array_unref( arr_objects_tmp );
                va_end( ap );
                ERROR_S
            }
        }
        g_array_unref( arr_objects_tmp );
        arr_objects_tmp = arr_results;
    }

    va_end( ap );

    *arr_objects = arr_objects_tmp;

    return 0;
}


gint
sond_database_get_entities_for_property( gpointer database,
        gint ID_label_property, const gchar* value, GArray** arr_res, gchar** errmsg )
{
    gint cnt = 0;
    const gchar* sql[] = {
            "SELECT properties.entity_subject FROM entities JOIN properties "
            "ON properties.entity_property=entities.ID "
            "AND entities.ID_label=?1 AND properties.value=?2 "
            "ORDER BY properties.entity_subject ASC; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_label_property );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label_property)" )

        rc = sqlite3_bind_text( stmt[0], 2, value, -1, NULL );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_text (value)" )

        if ( arr_res ) *arr_res = g_array_new( FALSE, FALSE, sizeof( gint ) );

        do
        {
            rc = sqlite3_step( stmt[0] );
            if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
            {
                if ( arr_res )
                {
                    g_array_unref( *arr_res );
                    *arr_res = NULL;
                }
                ERROR_ZOND_DBASE( "sqlite3_step" )
            }
            else if ( rc == SQLITE_ROW )
            {
                gint ID_entity = 0;

                ID_entity = sqlite3_column_int( stmt[0], 0 );
                if ( arr_res ) g_array_append_val( *arr_res, ID_entity );
                cnt++;
            }
        } while ( rc == SQLITE_ROW );
    }
    else //mysql
    {

    }

    return cnt;
}


static gint
sond_database_get_entities_for_no_property( gpointer database,
        gint ID_label_property, GArray** arr_res, gchar** errmsg )
{
    gint cnt = 0;
    const gchar* sql[] = {
            "SELECT properties.entity_subject FROM entities JOIN properties "
            "ON properties.entity_property=entities.ID "
            "AND entities.ID_label IS NOT ?1; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_label_property );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label_property)" )

        if ( arr_res ) *arr_res = g_array_new( FALSE, FALSE, sizeof( gint ) );

        do
        {
            rc = sqlite3_step( stmt[0] );
            if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
            {
                if ( arr_res )
                {
                    g_array_unref( *arr_res );
                    *arr_res = NULL;
                }
                ERROR_ZOND_DBASE( "sqlite3_step" )
            }
            else if ( rc == SQLITE_ROW )
            {
                gint ID_entity = 0;

                ID_entity = sqlite3_column_int( stmt[0], 0 );
                if ( arr_res ) g_array_append_val( *arr_res, ID_entity );
                cnt++;
            }
        } while ( rc == SQLITE_ROW );
    }
    else //mysql
    {

    }

    return cnt;
}


//Value einer property darf nicht NULL sein, macht ja auch keinen Sinn
//wenn value == NULL übergeben wird dann soll das heißen: entity must not eine solche property haben!
gint
sond_database_get_entities_for_properties_and( gpointer database,
        GArray** arr_res, gchar** errmsg, ... )
{
    va_list arg_pointer;
    GArray* arr_first = NULL;
    gint ID_label = 0;
    GPtrArray* arr_arrays = NULL;

    arr_arrays = g_ptr_array_new_full( 0, (GDestroyNotify) g_array_unref );

    va_start( arg_pointer, errmsg );

    while ( (ID_label = va_arg( arg_pointer, gint )) != -1 )
    {
        const gchar* value = NULL;
        gint rc = 0;
        GArray* arr_prop = NULL;

        value = va_arg( arg_pointer, const gchar* );

        if ( value ) rc = sond_database_get_entities_for_property( database, ID_label, value,
                &arr_prop, errmsg );
        else rc = sond_database_get_entities_for_no_property( database, ID_label,
                &arr_prop, errmsg );
        if ( rc == -1 )
        {
            va_end( arg_pointer );
            g_ptr_array_unref( arr_arrays );
            ERROR_S
        }

        g_ptr_array_add( arr_arrays, arr_prop );
    }

    va_end( arg_pointer );

    if ( arr_arrays->len == 0 ) return 0;

    arr_first = g_ptr_array_index( arr_arrays, 0 );
    for ( gint i = 0; i < arr_first->len; i++ )
    {
        gint ID_first_array = 0;

        ID_first_array = g_array_index( arr_first, gint, i );

        for ( gint a = 1; a < arr_arrays->len; a++ )
        {
            GArray* arr_n = NULL;
            gboolean found = FALSE;

            arr_n = g_ptr_array_index( arr_arrays, a );

            for ( gint u = 0; u < arr_n->len; u++ )
            {
                gint ID = 0;

                ID = g_array_index( arr_n, gint, u );
                if ( ID == ID_first_array )
                {
                    found = TRUE;
                    break;
                }
            }

            if ( found == FALSE )
            {
                g_array_remove_index( arr_first, i );
                i--;
                break;
            }
        }
    }

    if ( arr_res ) *arr_res = g_array_ref( arr_first );

    g_ptr_array_unref( arr_arrays ); //arr_first wird nicht gelöscht, da ref == 2 (hoffe ich)

    return 0;
}


gint
sond_database_get_property_value( gpointer database, gint ID_property, gchar** value, gchar** errmsg )
{
    const gchar* sql[] = {
            "SELECT value FROM properties WHERE entity_property=@1; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_property );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_property)" )

        rc = sqlite3_step( stmt[0] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )
        else if ( rc == SQLITE_DONE ) ERROR_S_MESSAGE( "ID_property existiert nicht" )

        if ( value ) *value = g_strdup( (const gchar*) sqlite3_column_text( stmt[0], 0 ) );
    }
    else //mysql
    {
        gint rc = 0;
        MYSQL* con = NULL;
        MYSQL_RES* mysql_res = NULL;
        MYSQL_ROW row = NULL;
        gchar* sql_mariadb = NULL;

        con = (MYSQL*) database;

        sql_mariadb = g_strdup_printf( "SET @1=%i; ", ID_property );
        sql_mariadb = add_string( sql_mariadb, g_strdup( sql[0] ) );

        rc = mysql_query( con, sql_mariadb );
        g_free( sql_mariadb );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf ", __func__, ":\n",
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

        row = mysql_fetch_row( mysql_res );
        if ( !row )
        {
            mysql_free_result( mysql_res );
            ERROR_S_MESSAGE( "ID_property existiert nicht" )
        }

        if ( value ) *value = g_strdup( row[0] );

        mysql_free_result( mysql_res );
    }

    return 0;
}


gint
sond_database_get_first_property_value_for_subject( gpointer database,
        gint ID_entity_subject, gint ID_label_property, gchar** value, gchar** errmsg )
{
    const gchar* sql[] = {
            "SELECT properties.value FROM properties JOIN entities  "
            "ON properties.entity_property=entities.ID "
            "AND properties.entity_subject=@1 "
            "AND entities.ID_label=@2; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_entity_subject );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_entity_subject)" )

        rc = sqlite3_bind_int( stmt[0], 2, ID_label_property );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label_property)" )

        rc = sqlite3_step( stmt[0] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )
        else if ( rc == SQLITE_DONE ) return 1;

        if ( value ) *value = g_strdup( (const gchar*) sqlite3_column_text( stmt[0], 0 ) );
    }
    else //mysql
    {
        gint rc = 0;
        MYSQL* con = NULL;
        MYSQL_RES* mysql_res = NULL;
        MYSQL_ROW row = NULL;
        gchar* sql_mariadb = NULL;

        con = (MYSQL*) database;

        sql_mariadb = g_strdup_printf( "SET @1=%i; ", ID_entity_subject );
        sql_mariadb = add_string( sql_mariadb, g_strdup_printf( "SET @2=%i; ", ID_label_property ) );
        sql_mariadb = add_string( sql_mariadb, g_strdup( sql[0] ) );

        rc = mysql_query( con, sql_mariadb );
        g_free( sql_mariadb );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf ", __func__, ":\n",
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

        row = mysql_fetch_row( mysql_res );
        if ( !row )
        {
            mysql_free_result( mysql_res );
            return 1;
        }

        if ( value ) *value = g_strdup( row[0] );

        mysql_free_result( mysql_res );
    }

    return 0;
}


gint
sond_database_get_subject_and_first_property_value_for_labels( gpointer database,
        gint ID_label_subject, gint ID_label_property, GArray** arr_IDs, GPtrArray** arr_values, gchar** errmsg )
{
    const gchar* sql[] = {
            "SELECT e1.ID, properties.value "
            "FROM properties JOIN entities AS e1 JOIN entities AS e2 WHERE "
            "e1.ID_label=@1 AND "
            "e1.ID=properties.entity_subject AND "
            "properties.entity_property=e2.ID AND "
            "e2.ID_label=@2 "
            "ORDER BY properties.value ASC; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_label_subject );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label_subject)" )

        rc = sqlite3_bind_int( stmt[0], 2, ID_label_property );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label_property)" )

        *arr_IDs = g_array_new( FALSE, FALSE, sizeof( gint ) );
        *arr_values = g_ptr_array_new_full( 0, g_free );

        do
        {
            rc = sqlite3_step( stmt[0] );
            if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
            {
                g_array_unref( *arr_IDs );
                g_ptr_array_unref( *arr_values );
                ERROR_ZOND_DBASE( "sqlite3_step" )
            }

            if ( rc == SQLITE_ROW )
            {
                gchar* value = NULL;
                gint ID = 0;

                ID = sqlite3_column_int( stmt[0], 0 );
                g_array_append_val( *arr_IDs, ID );

                value = g_strdup( (const gchar*) sqlite3_column_text( stmt[0], 1 ) );
                g_ptr_array_add( *arr_values, value );
            }
        } while ( rc == SQLITE_ROW );
    }
    else //mysql
    {
    }

    return 0;
}


gint
sond_database_get_objects_from_labels( gpointer database, gint ID_label_subject,
        gint ID_label_rel, gint ID_label_object, GArray** arr_objects,
        gchar** errmsg )
{
    const gchar* sql[] = {
            "SELECT DISTINCT e3.ID FROM "
                "rels JOIN "
                "entities AS e1 JOIN "
                "(WITH RECURSIVE cte_labels (ID) AS ( "
                    "VALUES (@1) "
                    "UNION ALL "
                    "SELECT labels.ID "
                        "FROM labels JOIN cte_labels WHERE "
                        "labels.parent = cte_labels.ID "
                    ") SELECT ID AS ID_CTE_1 FROM cte_labels) "

                "JOIN entities AS e2 JOIN "
                "(WITH RECURSIVE cte_labels (ID) AS ( "
                    "VALUES (@2) "
                    "UNION ALL "
                    "SELECT labels.ID "
                        "FROM labels JOIN cte_labels WHERE "
                        "labels.parent = cte_labels.ID "
                    ") SELECT ID AS ID_CTE_2 FROM cte_labels) "

                "JOIN entities AS e3 JOIN"
                "(WITH RECURSIVE cte_labels (ID) AS ( "
                    "VALUES (@3) "
                    "UNION ALL "
                    "SELECT labels.ID "
                        "FROM labels JOIN cte_labels WHERE "
                        "labels.parent = cte_labels.ID "
                    ") SELECT ID AS ID_CTE_3 FROM cte_labels) "
            "WHERE "
                "e1.ID_label=ID_CTE_1 AND "
                "e1.ID=rels.entity_subject AND "
                "e2.ID_label=ID_CTE_2 AND "
                "e2.ID=rels.entity_rel AND "
                "e3.ID_label=ID_CTE_3 AND "
                "e3.ID=rels.entity_object; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_label_subject );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label_subject)" )

        rc = sqlite3_bind_int( stmt[0], 2, ID_label_rel );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label_rel)" )

        rc = sqlite3_bind_int( stmt[0], 3, ID_label_object );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_label_object)" )

        *arr_objects = g_array_new( FALSE, FALSE, sizeof( gint ) );

        do
        {
            rc = sqlite3_step( stmt[0] );
            if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
            {
                g_array_unref( *arr_objects );
                ERROR_ZOND_DBASE( "sqlite3_step" )
            }

            if ( rc == SQLITE_ROW )
            {
                gint ID_entity_object = 0;

                ID_entity_object = sqlite3_column_int( stmt[0], 0 );
                g_array_append_val( *arr_objects, ID_entity_object );
            }
        } while ( rc == SQLITE_ROW );
    }
    else //mysql
    {

    }

    return 0;
}


gint
sond_database_get_outgoing_rels( gpointer database, gint ID_entity, GArray** arr_o_rels, gchar** errmsg )
{
    const gchar* sql[] = {
            "SELECT entity_rel FROM rels WHERE entity_subject=@1; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_entity );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_entity)" )

        *arr_o_rels = g_array_new( FALSE, FALSE, sizeof( gint ) );
        do
        {
            gint ID_entity_rel = 0;

            rc = sqlite3_step( stmt[0] );
            if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
            {
                g_array_unref( *arr_o_rels );
                ERROR_ZOND_DBASE( "sqlite3_step" )
            }
            else if ( rc == SQLITE_DONE ) break;

            ID_entity_rel = sqlite3_column_int( stmt[0], 0 );
            g_array_append_val( *arr_o_rels, ID_entity_rel );
        } while ( rc == SQLITE_ROW );
    }
    else //mysql
    {
        gint rc = 0;
        MYSQL* con = NULL;
        MYSQL_RES* mysql_res = NULL;
        MYSQL_ROW row = NULL;
        gchar* sql_mariadb = NULL;

        con = (MYSQL*) database;

        sql_mariadb = g_strdup_printf( "SET @1=%i; ", ID_entity );
        sql_mariadb = add_string( sql_mariadb, g_strdup( sql[0] ) );

        rc = mysql_query( con, sql_mariadb );
        g_free( sql_mariadb );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf ", __func__, ":\n",
                    "Bei Aufruf mysql_query:\n", mysql_error( con ), NULL );
            return -1;
        }

        mysql_res = mysql_store_result( con );
        if ( !mysql_res )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf ", __func__, ":\n"
                    "Bei Aufruf mysql_store_result:\n",
                    mysql_error( con ), NULL );

            return -1;
        }

        *arr_o_rels = g_array_new( FALSE, FALSE, sizeof( gint ) );
        while ( (row = mysql_fetch_row( mysql_res )) )
        {
            gint ID_entity_rel = 0;

            ID_entity_rel = atoi( row[0] );
            g_array_append_val( *arr_o_rels, ID_entity_rel );
        }

        mysql_free_result( mysql_res );
    }

    return 0;
}


gint
sond_database_get_incoming_rels( gpointer database, gint ID_entity, GArray** arr_i_rels, gchar** errmsg )
{
    const gchar* sql[] = {
            "SELECT entity_rel FROM rels WHERE entity_object=@1; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_entity );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_entity)" )

        *arr_i_rels = g_array_new( FALSE, FALSE, sizeof( gint ) );
        do
        {
            gint ID_entity_rel = 0;

            rc = sqlite3_step( stmt[0] );
            if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
            {
                g_array_unref( *arr_i_rels );
                ERROR_ZOND_DBASE( "sqlite3_step" )
            }
            else if ( rc == SQLITE_DONE ) break;

            ID_entity_rel = sqlite3_column_int( stmt[0], 0 );
            g_array_append_val( *arr_i_rels, ID_entity_rel );
        } while ( rc == SQLITE_ROW );
    }
    else //mysql
    {
        gint rc = 0;
        MYSQL* con = NULL;
        MYSQL_RES* mysql_res = NULL;
        MYSQL_ROW row = NULL;
        gchar* sql_mariadb = NULL;

        con = (MYSQL*) database;

        sql_mariadb = g_strdup_printf( "SET @1=%i; ", ID_entity );
        sql_mariadb = add_string( sql_mariadb, g_strdup( sql[0] ) );

        rc = mysql_query( con, sql_mariadb );
        g_free( sql_mariadb );
        if ( rc )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf ", __func__, ":\n",
                    "Bei Aufruf mysql_query:\n", mysql_error( con ), NULL );
            return -1;
        }

        mysql_res = mysql_store_result( con );
        if ( !mysql_res )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf ", __func__, ":\n"
                    "Bei Aufruf mysql_store_result:\n",
                    mysql_error( con ), NULL );

            return -1;
        }

        *arr_i_rels = g_array_new( FALSE, FALSE, sizeof( gint ) );
        while ( (row = mysql_fetch_row( mysql_res )) )
        {
            gint ID_entity_rel = 0;

            ID_entity_rel = atoi( row[0] );
            g_array_append_val( *arr_i_rels, ID_entity_rel );
        }

        mysql_free_result( mysql_res );
    }

    return 0;
}


gint
sond_database_get_subject_from_rel( gpointer database, gint ID_entity_rel, gchar** errmsg )
{
    gint ID_entity_object = 0;
    const gchar* sql[] = {
            "SELECT entity_subject FROM rels WHERE entity_rel=@1; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_entity_rel );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_entity_rel)" )

        rc = sqlite3_step( stmt[0] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )
        else if ( rc == SQLITE_DONE ) ERROR_S_MESSAGE( "Rel nicht vorhanden" );

        ID_entity_object = sqlite3_column_int( stmt[0], 0 );
    }
    else //mysql
    {

    }

    return ID_entity_object;
}


gint
sond_database_get_object_from_rel( gpointer database, gint ID_entity_rel, gchar** errmsg )
{
    gint ID_entity_object = 0;
    const gchar* sql[] = {
            "SELECT entity_object FROM rels WHERE entity_rel=@1; "
    };

    if ( ZOND_IS_DBASE(database) )
    {
        gint rc = 0;
        sqlite3_stmt** stmt = NULL;

        ZondDBase* zond_dbase = ZOND_DBASE(database);

        rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
        if ( rc ) ERROR_S

        rc = sqlite3_bind_int( stmt[0], 1, ID_entity_rel );
        if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (ID_entity_rel)" )

        rc = sqlite3_step( stmt[0] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )
        else if ( rc == SQLITE_DONE ) ERROR_S_MESSAGE( "Rel nicht vorhanden" );

        ID_entity_object = sqlite3_column_int( stmt[0], 0 );
    }
    else //mysql
    {

    }

    return ID_entity_object;
}


/*
gint
dbase_full_get_label_text_for_entity( DBaseFull* dbase_full, gint ID_entity, gchar** label, gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( dbase_full->stmts[44] );

    rc = sqlite3_bind_int( dbase_full->stmts[44], 1, ID_entity );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (ID_entity)" )

    rc = sqlite3_step( dbase_full->stmts[44] );
    if ( rc != SQLITE_ROW ) ERROR_DBASE_FULL( "sqlite3_step" )

    if ( label ) *label =
            g_strdup( (const gchar*) sqlite3_column_text( dbase_full->stmts[44], 0 ) );

    return 0;
}


static void
dbase_full_clear_property( gpointer data )
{
    Property* property = (Property*) data;

    g_free( property->label );
    g_free( property->value );
    g_array_unref( property->arr_properties );

    return;
}


gint
dbase_full_get_properties( DBaseFull* dbase_full, gint ID_entity,
        GArray** arr_properties, gchar** errmsg )
{
    gint rc = 0;

    if ( !arr_properties ) return 0;

    sqlite3_reset( dbase_full->stmts[45] );

    rc = sqlite3_bind_int( dbase_full->stmts[45], 1, ID_entity );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (ID_entity)" )

    *arr_properties = g_array_new( FALSE, FALSE, sizeof( Property ) );
    g_array_set_clear_func( *arr_properties, dbase_full_clear_property );

    do
    {
        Property property = { 0 };

        rc = sqlite3_step( dbase_full->stmts[45] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
        {
            g_array_unref( *arr_properties );
            ERROR_DBASE_FULL( "sqlite3_step" )
        }
        else if ( rc == SQLITE_ROW )
        {
            gint ret = 0;

            property.ID = sqlite3_column_int( dbase_full->stmts[45], 0 );
            property.label = g_strdup( (const gchar*) sqlite3_column_text( dbase_full->stmts[45], 1 ) );
            property.value = g_strdup( (const gchar*) sqlite3_column_text( dbase_full->stmts[45], 2 ) );

            ret = dbase_full_get_properties( dbase_full, property.ID, &(property.arr_properties), errmsg );
            if ( ret )
            {
                g_array_unref( *arr_properties );
                ERROR_PAO( "dbase_full_get_properties" );
            }

            g_array_append_val( *arr_properties, property );
        }
    } while ( rc == SQLITE_ROW );

    return 0;
}


gint
dbase_full_get_outgoing_edges( DBaseFull* dbase_full, gint ID_entity, GArray** arr_edges,
        gchar** errmsg )
{
    gint rc = 0;

    if ( !arr_edges ) return 0;

    sqlite3_reset( dbase_full->stmts[46] );

    rc = sqlite3_bind_int( dbase_full->stmts[46], 1, ID_entity );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (ID_entity)" )

    *arr_edges = g_array_new( FALSE, FALSE, sizeof( Edge ) );

    do
    {
        rc = sqlite3_step( dbase_full->stmts[46] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
        {
            g_array_unref( *arr_edges );
            ERROR_DBASE_FULL( "sqlite3_step" )
        }
        else if ( rc == SQLITE_ROW )
        {
            Edge edge = { 0 };

            edge.ID = sqlite3_column_int( dbase_full->stmts[46], 0 );
            edge.subject = sqlite3_column_int( dbase_full->stmts[46], 1 );
            edge.object = sqlite3_column_int( dbase_full->stmts[46], 2 );

            g_array_append_val( *arr_edges, edge );
        }
    } while ( rc == SQLITE_ROW );

    return 0;
}


gint
dbase_full_get_label_text( DBaseFull* dbase_full, gint ID_label, gchar** label_text, gchar** errmsg )
{
    gint rc = 0;

    if ( !label_text ) return 0;

    sqlite3_reset( dbase_full->stmts[47] );

    rc = sqlite3_bind_int( dbase_full->stmts[47], 1, ID_label );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (ID_label)" )

    rc = sqlite3_step( dbase_full->stmts[47] );
    if ( rc != SQLITE_ROW ) ERROR_DBASE_FULL( "sqlite3_step" )

    *label_text = g_strdup( (const gchar*) sqlite3_column_text( dbase_full->stmts[47], 0 ) );

    return 0;
}


//Array von Kindern von label; nur ID (gint)
gint
dbase_full_get_array_children_label( DBaseFull* dbase_full, gint label,
        GArray** arr_children, gchar** errmsg )
{
    gint rc = 0;

    if ( !arr_children ) return 0;

    sqlite3_reset( dbase_full->stmts[48] );

    rc = sqlite3_bind_int( dbase_full->stmts[48], 1, label );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (label)" )

    *arr_children = g_array_new( FALSE, FALSE, sizeof( gint ) );

    do
    {
        gint child = 0;

        rc = sqlite3_step( dbase_full->stmts[48] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
        {
            g_array_unref( *arr_children );
            ERROR_DBASE_FULL( "sqlite3_step" )
        }
        else if ( rc == SQLITE_ROW )
        {
            child = sqlite3_column_int( dbase_full->stmts[48], 0 );
            g_array_append_val( *arr_children, child );
        }
    } while ( rc == SQLITE_ROW );

    return 0;
}


//Array von nodes mit dem label "nomen" oder eines Kindes von "nomen"; nur ID (gint)
gint
dbase_full_get_array_nodes( DBaseFull* dbase_full, gint nomen, GArray** arr_nodes,
        gchar** errmsg )
{
    gint rc = 0;

    if ( !arr_nodes ) return 0;

    sqlite3_reset( dbase_full->stmts[49] );

    rc = sqlite3_bind_int( dbase_full->stmts[49], 1, nomen );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (nomen)" )

    *arr_nodes = g_array_new( FALSE, FALSE, sizeof( gint ) );

    do
    {
        gint ID_entity = 0;

        rc = sqlite3_step( dbase_full->stmts[49] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
        {
            g_array_unref( *arr_nodes );
            ERROR_DBASE_FULL( "sqlite3_step" )
        }
        else if ( rc == SQLITE_ROW )
        {
            ID_entity = sqlite3_column_int( dbase_full->stmts[49], 0 );
            g_array_append_val( *arr_nodes, ID_entity );
        }
    } while ( rc == SQLITE_ROW );

    return 0;
}


gint
dbase_full_get_incoming_edges( DBaseFull* dbase_full, gint ID_entity, GArray** arr_edges,
        gchar** errmsg )
{
    gint rc = 0;

    if ( !arr_edges ) return 0;

    sqlite3_reset( dbase_full->stmts[50] );

    rc = sqlite3_bind_int( dbase_full->stmts[50], 1, ID_entity );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (ID_entity)" )

    *arr_edges = g_array_new( FALSE, FALSE, sizeof( Edge ) );

    do
    {
        rc = sqlite3_step( dbase_full->stmts[50] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE )
        {
            g_array_unref( *arr_edges );
            ERROR_DBASE_FULL( "sqlite3_step" )
        }
        else if ( rc == SQLITE_ROW )
        {
            Edge edge = { 0 };

            edge.ID = sqlite3_column_int( dbase_full->stmts[50], 0 );
            edge.subject = sqlite3_column_int( dbase_full->stmts[50], 1 );
            edge.object = sqlite3_column_int( dbase_full->stmts[50], 2 );

            g_array_append_val( *arr_edges, edge );
        }
    } while ( rc == SQLITE_ROW );

    return 0;
}


gint
dbase_full_get_label_for_entity( DBaseFull* dbase_full, gint ID_entity, gchar** errmsg )
{
    gint rc = 0;
    gint label = 0;

    sqlite3_reset( dbase_full->stmts[52] );

    rc = sqlite3_bind_int( dbase_full->stmts[52], 1, ID_entity );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (ID_entity)" )

    rc = sqlite3_step( dbase_full->stmts[52] );
    if ( rc != SQLITE_ROW ) ERROR_DBASE_FULL( "sqlite3_step" )

    label = sqlite3_column_int( dbase_full->stmts[52], 0 );

    return label;
}


gint
dbase_full_insert_edge( DBaseFull* dbase_full, gint entity, gint subject,
        gint object, gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( dbase_full->stmts[53] );

    rc = sqlite3_bind_int( dbase_full->stmts[53], 1, entity );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (entity)" )

    rc = sqlite3_bind_int( dbase_full->stmts[53], 2, subject );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (subject)" )

    rc = sqlite3_bind_int( dbase_full->stmts[53], 3, object );
    if ( rc != SQLITE_OK ) ERROR_DBASE_FULL( "sqlite3_bind_int (value)" )

    rc = sqlite3_step( dbase_full->stmts[53] );
    if ( rc != SQLITE_DONE ) ERROR_DBASE_FULL( "sqlite3_step" )

    return 0;
}


gint
dbase_full_get_entity( DBaseFull* dbase_full, gint ID_entity, Entity** entity, gchar** errmsg )
{
    gchar* label = NULL;
    GArray* arr_properties = NULL;
    gint rc = 0;

    if ( !entity ) return 0;

    rc = dbase_full_get_label_text_for_entity( dbase_full, ID_entity, &label, errmsg );
    if ( rc ) ERROR_PAO( "dbase_full_get_label_text_for_entity" )

    rc = dbase_full_get_properties( dbase_full, ID_entity, &arr_properties, errmsg );
    if ( rc )
    {
        g_free( label );
        ERROR_PAO( "dbase_full_get_properties" )
    }

    *entity = g_malloc0( sizeof( Entity ) );
    (*entity)->ID = ID_entity;
    (*entity)->label = label;
    (*entity)->arr_properties = arr_properties;

    return 0;
}



//  get_label_entity (44)
            "SELECT labels.label FROM labels JOIN entities "
                "ON entities.label = labels.ID WHERE entities.ID = ?1;",

//  get_properties (45)
            "SELECT ID_entity, label_text, properties.value FROM "
                "(SELECT entities.ID AS ID_entity,labels.label AS label_text "
                    "FROM entities JOIN labels ON entities.label = labels.ID) "
                    "JOIN properties ON ID_entity = properties.entity "
                    "WHERE properties.subject = ?1; ",

//  get_outgoint_edges (46)
            "SELECT ID_subject, ID_edge, labels.label, ID_object "
                "FROM labels JOIN "
                "(SELECT edges.subject AS ID_subject, edges.entity AS ID_edge, entities.label AS ID_label_edge, edges.object AS ID_object "
                "FROM edges JOIN entities ON edges.entity = entities.ID WHERE edges.subject = ?1) "
                "ON ID_label_edge = labels.ID; ",

//  get_label_text (47)
            "SELECT labels.label FROM labels WHERE labels.ID = ?1; ",

//  get_array_children (48)
            "SELECT labels.ID FROM labels WHERE labels.parent = ?1; ",

//  get_array_nodes (49)
            "SELECT entities.ID FROM entities JOIN "
            "(WITH RECURSIVE cte_labels (ID) AS ( "
                "VALUES (?1) "
                "UNION ALL "
                "SELECT labels.ID "
                    "FROM labels JOIN cte_labels WHERE "
                    "labels.parent = cte_labels.ID "
                ") SELECT ID AS ID_CTE FROM cte_labels) "
                "ON entities.label = ID_CTE; ",

//  get_incoming_edges (50)
            "SELECT edges.ID, edges.subject, edges.object"
                "FROM edges JOIN entities ON edges.entity = entities.ID WHERE edges.object = ?1; "

//  get_adm_entities (51)
            "SELECT adm_entities.rentity FROM adm_entities JOIN "
            "(WITH RECURSIVE cte_labels (ID) AS ( "
                "VALUES (?1) "
                "UNION ALL "
                "SELECT labels.ID "
                    "FROM labels JOIN cte_labels WHERE "
                    "labels.parent = cte_labels.ID "
                ") SELECT ID AS ID_CTE FROM cte_labels) "
                "ON adm_entities.entity = ID_CTE; ",

//  get_label_for_entity (52)
            "SELECT entities.label FROM entities WHERE entities.ID = ?1; ",

//  insert_edge (53)
            "INSERT INTO edges (entity,subject,object) VALUES (?1,?2,?3); ",


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
        if ( rc ) ERROR_S
    }

    mysql_free_result( mysql_res );

    return 0;
}

*/




