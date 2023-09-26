/*
sond (sond_server_akte.c) - Akten, Beweisstücke, Unterlagen
Copyright (C) 2023  pelo america

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

#include <mariadb/mysql.h>
#include <json-glib/json-glib.h>

#include "../../sond_database.h"
#include "../../misc.h"

#include "../sond_akte.h"

#include "sond_server.h"
#include "sond_server_akte.h"


static gint
sond_server_akte_write( SondServer* sond_server, MYSQL* con, SondAkte* sond_akte, GError** error )
{
    gchar* sql_1 = NULL;
    gchar* sql_2 = NULL;
    gchar* sql_3 = NULL;
    gint rc = 0;
    gint ID_akte = 0;
    MYSQL_RES* mysql_res = NULL;
    MYSQL_ROW row = NULL;

    sql_1 = g_strdup_printf( "SELECT t1.ID_reg_jahr AS ID_akte FROM "
            "(SELECT rel_subject AS ID_reg_jahr FROM entities WHERE type=%i AND prop_value='%i') AS t1 "
            "JOIN "
            "(SELECT rel_subject AS ID_reg_nr FROM entities WHERE type=%i AND prop_value='%i') AS t2 "
            "ON t1.ID_reg_jahr=t2.ID_reg_nr; ", _REG_JAHR_, sond_akte->reg_jahr, _REG_NR_, sond_akte->reg_nr );

    rc = mysql_query( con, sql_1 );
    g_free( sql_1 );
    if ( rc )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ),
                mysql_errno( con ), "%s\n%s\n\nFehlermeldung: %s",
                __func__, "mysql_query", mysql_error( con ) );

        return -1;
    }

    //abfrägen
    mysql_res = mysql_store_result( con );
    if ( !mysql_res )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ),
                mysql_errno( con ), "%s\n%s\n\nFehlermeldung: %s",
                __func__, "mysql_store_results", mysql_error( con ) );

        return -1;
    }

    row = mysql_fetch_row( mysql_res );
    if ( row ) ID_akte = atoi( row[0] );
    else
    {
        g_error_new( SOND_SERVER_ERROR, SOND_SERVER_ERROR_NOTFOUND, "Keine Akte zur Registernummer" );
        mysql_free_result( mysql_res );

        return -1;
    }

    mysql_free_result( mysql_res );

    sql_2 = g_strdup_printf( "UPDATE entities SET prop_value='%s' "
            "WHERE type=%i AND rel_subject=%i; ", sond_akte->aktenrubrum, _AKTENRUBRUM_, ID_akte );
    rc = mysql_query( con, sql_2 );
    g_free( sql_2 );
    if ( rc )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ),
                mysql_errno( con ), "%s\n%s\nError: %s",
                __func__, "mysql_query", mysql_error( con ) );

        return -1;
    }

    sql_3 = g_strdup_printf( "UPDATE entities SET prop_value='%s' "
            "WHERE type=%i AND rel_subject=%i; ", sond_akte->aktenkurzbez, _AKTENKURZBEZ_, ID_akte );
    rc = mysql_query( con, sql_3 );
    g_free( sql_3 );
    if ( rc )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ),
                mysql_errno( con ), "%s\n%s\nError: %s",
                __func__, "mysql_query", mysql_error( con ) );

        return -1;
    }

    return 0;
}


static gint
sond_server_akte_create( SondServer* sond_server, MYSQL* con, SondAkte* sond_akte, GError** error )
{
    gint rc = 0;
    gchar* sql = NULL;
    GDateTime* date_time = NULL;
    gint year = 0;
    gint num = 0;
    gchar* year_text = NULL;
    gchar* num_text = NULL;
    MYSQL_RES* mysql_res = NULL;
    MYSQL_ROW row = NULL;
    gint ID_akte = 0;

    date_time = g_date_time_new_now_local( );
    year = g_date_time_get_year( date_time );
    g_date_time_unref( date_time );

    sql = g_strdup_printf( "SELECT MAX(t2.reg_nr) FROM "
            "(SELECT rel_subject AS ID_reg_jahr from entities WHERE type=%i AND prop_value='%i') AS t1 "
            "JOIN "
            "(SELECT prop_value AS reg_nr, rel_subject AS ID_reg_nr FROM entities WHERE type=%i) t2 "
            "ON t1.ID_reg_jahr=t2.ID_reg_nr; ", _REG_JAHR_, year, _REG_NR_ );

    rc = mysql_query( con, sql );
    g_free( sql );
    if ( rc )
    {
        *error = g_error_new( g_quark_from_static_string( "MARIADB" ),
                mysql_errno( con ), "%s\n%s\n\nFehlermeldung: %s",
                __func__, "mysql_query", mysql_error( con ) );
        g_warning( (*error)->message );
        return -1;
    }

    //abfrägen
    mysql_res = mysql_store_result( con );
    if ( !mysql_res )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ),
                mysql_errno( con ), "%s\n%s\n\nFehlermeldung: %s",
                __func__, "mysql_store_results", mysql_error( con ) );
        g_warning( (*error)->message );
        return -1;
    }

    row = mysql_fetch_row( mysql_res );
    if ( row ) num = atoi( row[0] ) + 1;
    else num = 1; //noch keine Akte in diesem Jahr
    mysql_free_result( mysql_res );

    ID_akte = sond_database_insert_entity( con, AKTE, error );
    if ( ID_akte == -1 )
    {
        g_prefix_error( error, "%s\n", __func__ );
        return -1;
    }

    rc = sond_database_insert_property( con, _AKTENRUBRUM_,
            ID_akte, sond_akte->aktenrubrum, error );
    if ( rc == -1 )
    {
        g_prefix_error( error, "%s\n", __func__ );
        return -1;
    }

    rc = sond_database_insert_property( con, _AKTENKURZBEZ_,
            ID_akte, sond_akte->aktenkurzbez, error );
    if ( rc == -1 )
    {
        g_prefix_error( error, "%s\n", __func__ );
        return -1;
    }

    year_text = g_strdup_printf( "%i", year );
    rc = sond_database_insert_property( con, _REG_JAHR_,
            ID_akte, year_text, error );
    g_free( year_text );
    if ( rc == -1 )
    {
        g_prefix_error( error, "%s\n", __func__ );
        return -1;
    }

    num_text = g_strdup_printf( "%i", num );
    rc = sond_database_insert_property( con, _REG_NR_,
            ID_akte, num_text, error );
    g_free( num_text );
    if ( rc == -1 )
    {
        g_prefix_error( error, "%s\n", __func__ );
        return -1;
    }

    sond_akte->reg_jahr = year;
    sond_akte->reg_nr = num;

    return 0;
}


void
sond_server_akte_schreiben( SondServer* sond_server,
        const gchar** imessage_strv, gchar** omessage )
{
    GError* error = NULL;
    gint rc = 0;
    SondAkte* sond_akte = NULL;
    MYSQL* con = NULL;
    gint reg_jahr = 0;
    gint reg_nr = 0;

    sond_akte = sond_akte_new_from_json( imessage_strv[3], &error );
    if ( !sond_akte )
    {
        *omessage = g_strconcat( "ERROR *** Nachricht konnte nicht geparst werden\n\n",
                error->message, NULL );
        g_error_free( error );

        return;
    }

    con = sond_server_get_mysql_con( sond_server, &error );
    if ( !con )
    {
        *omessage = g_strconcat( "ERROR *** Keine Verbindung zu MYSQL-Server\n\n",
                error->message, NULL );
        g_warning( "Conn zum MariaDB-Server konnte nicht hergestellt werden\n\n%s",
                error->message );
        g_error_free( error );
        sond_akte_free( sond_akte );

        return;
    }

    rc = sond_database_begin( con, &error );
    if ( rc )
    {
        *omessage = g_strconcat( "ERROR *** Akte konnte nicht angelegt werden\n\n",
                error->message, NULL );
        g_warning( "Transaction konnte nicht gestartet werden\n\n%s",
                error->message );
        g_error_free( error );
        sond_akte_free( sond_akte );

        return;
    }

    if ( sond_akte->reg_nr == 0 )
    {
        rc = sond_server_akte_create( sond_server, con, sond_akte, &error);
        reg_jahr = sond_akte->reg_jahr;
        reg_nr = sond_akte->reg_nr;
    }
    else rc = sond_server_akte_write( sond_server, con, sond_akte, &error );
    sond_akte_free( sond_akte );
    if ( rc )
    {
        gint res = 0;
        GError* error_tmp = NULL;

        *omessage = g_strconcat( "ERROR *** Akte konnte nicht angelegt/geändert werden\n\n",
                error->message, NULL );
        g_clear_error( &error );

        res = sond_database_rollback( con, &error_tmp );
        if ( res )
        {
            g_message( "Rollback gescheitert\n\n%s", error_tmp->message );
            mysql_close( con );
        }

        return;
    }

    rc = sond_database_commit( con, &error );
    if ( rc )
    {
        gint res = 0;

        *omessage = g_strconcat( "ERROR *** Akte konnte nicht angelegt/geändert werden\n\n",
                error->message, NULL );
        g_clear_error( &error );

        res = sond_database_rollback( con, &error );
        if ( res )
        {
            g_message( "Rollback gescheitert\n\n%s", error->message );
            g_error_free( error );
            mysql_close( con );
        }

        return;
    }

//    if ( reg_nr )
        *omessage = g_strdup_printf( "%i-%i", reg_nr, reg_jahr );

    return;
}


static gint
sond_server_akte_laden( SondServer* sond_server, SondAkte* sond_akte, GError** error )
{
    MYSQL* con = NULL;
    MYSQL_RES* mysql_res = NULL;
    MYSQL_ROW row = NULL;
    gchar* sql_1 = NULL;
    gchar* sql_2 = NULL;
    gchar* sql_3 = NULL;
    gint ID_akte = 0;
    gint rc = 0;

    con = sond_server_get_mysql_con( sond_server, error );
    if ( !con )
    {
        g_prefix_error( error, "%s\n", __func__ );
        return -1;
    }

    sql_1 = g_strdup_printf( "SELECT t1.ID_reg_jahr FROM "
            "(SELECT rel_subject AS ID_reg_jahr FROM entities WHERE type=%i AND prop_value='%i') AS t1 "
            "JOIN "
            "(SELECT rel_subject AS ID_reg_nr FROM entities WHERE type=%i AND prop_value='%i') AS t2 "
            "ON t1.ID_reg_jahr=t2.ID_reg_nr; ", _REG_JAHR_, sond_akte->reg_jahr, _REG_NR_, sond_akte->reg_nr );

    rc = mysql_query( con, sql_1 );
    g_free( sql_1 );
    if ( rc )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ),
                mysql_errno( con ), "%s\n%s\n\nFehlermeldung: %s",
                __func__, "mysql_query", mysql_error( con ) );
        mysql_close( con );
        g_warning( (*error)->message );
        return -1;
    }

    //abfrägen
    mysql_res = mysql_store_result( con );
    if ( !mysql_res )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ),
                mysql_errno( con ), "%s\n%s\n\nFehlermeldung: %s",
                __func__, "mysql_store_results", mysql_error( con ) );
        mysql_close( con );
        g_warning( (*error)->message );
        return -1;
    }

    row = mysql_fetch_row( mysql_res );
    if ( row ) ID_akte = atoi( row[0] );
    else
    {
        *error = g_error_new( SOND_SERVER_ERROR, SOND_SERVER_ERROR_NOTFOUND, "Keine Akte zur Registernummer" );
        mysql_free_result( mysql_res );
        mysql_close( con );

        return -1;
    }

    mysql_free_result( mysql_res );

    sql_2 = g_strdup_printf( "SELECT prop_value FROM entities WHERE rel_subject=%i AND type=%i; ",
            ID_akte, _AKTENRUBRUM_ );

    rc = mysql_query( con, sql_2 );
    g_free( sql_2 );
    if ( rc )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ),
                mysql_errno( con ), "%s\n%s\n\nFehlermeldung: %s",
                __func__, "mysql_query", mysql_error( con ) );
        mysql_close( con );
        g_warning( (*error)->message );
        return -1;
    }

    //abfrägen
    mysql_res = mysql_store_result( con );
    if ( !mysql_res )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ),
                mysql_errno( con ), "%s\n%s\n\nFehlermeldung: %s",
                __func__, "mysql_store_results", mysql_error( con ) );
        mysql_close( con );
        g_warning( (*error)->message );
        return -1;
    }

    row = mysql_fetch_row( mysql_res );
    if ( row ) sond_akte->aktenrubrum = g_strdup( row[0] );
    else
    {
        *error = g_error_new( SOND_SERVER_ERROR, SOND_SERVER_ERROR_NOTFOUND, "Keine Aktenrubrum gespeichert" );
        mysql_free_result( mysql_res );
        mysql_close( con );

        return -1;
    }

    mysql_free_result( mysql_res );

    sql_3 = g_strdup_printf( "SELECT prop_value FROM entities WHERE rel_subject=%i AND type=%i; ",
            ID_akte, _AKTENKURZBEZ_);

    rc = mysql_query( con, sql_3 );
    g_free( sql_3 );
    if ( rc )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ),
                mysql_errno( con ), "%s\n%s\n\nFehlermeldung: %s",
                __func__, "mysql_query", mysql_error( con ) );
        mysql_close( con );
        g_warning( (*error)->message );
        return -1;
    }

    //abfrägen
    mysql_res = mysql_store_result( con );
    if ( !mysql_res )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ),
                mysql_errno( con ), "%s\n%s\n\nFehlermeldung: %s",
                __func__, "mysql_store_results", mysql_error( con ) );
        mysql_close( con );
        g_warning( (*error)->message );
        return -1;
    }

    row = mysql_fetch_row( mysql_res );
    if ( row ) sond_akte->aktenkurzbez = g_strdup( row[0] );

    mysql_free_result( mysql_res );

    mysql_close( con );

    return 0;
}


void
sond_server_akte_holen( SondServer* sond_server, const gchar** imessage_strv,
        gchar** omessage )
{
    gint reg_nr = 0;
    gint reg_jahr = 0;
    gboolean already_locked = FALSE;
    SondAkte* sond_akte = NULL;
    gint rc = 0;
    GError* error = NULL;

    reg_jahr = atoi( g_strrstr( imessage_strv[3], "-" ) + 1 );
    reg_nr = atoi( g_strndup( imessage_strv[3], sizeof( g_strrstr( imessage_strv[3], "-" ) ) ) );

    //lock
    g_mutex_lock( &sond_server->mysql_mutex_con );

    for ( gint i = 0; i < sond_server->arr_locks->len; i++ )
    {
        RegNrJahr reg_nr_jahr = g_array_index( sond_server->arr_locks, RegNrJahr, i );

        if ( reg_nr_jahr.reg_nr == reg_nr && reg_nr_jahr.reg_jahr == reg_jahr )
        {
            already_locked = TRUE;
            break;
        }
    }

    if ( !already_locked )
    {
        RegNrJahr reg_nr_jahr = { reg_nr, reg_jahr };

        g_array_append_val( sond_server->arr_locks, reg_nr_jahr );
    }

    g_mutex_unlock( &sond_server->mysql_mutex_con );

    sond_akte = sond_akte_new( );

    sond_akte->reg_nr = reg_nr;
    sond_akte->reg_jahr = reg_jahr;

    rc = sond_server_akte_laden( sond_server, sond_akte, &error );
    if ( rc )
    {
        *omessage = g_strconcat( "ERROR *** Akte kann nicht geladen werden\n\n",
                error->message, NULL );
        g_error_free( error );
        sond_akte_free( sond_akte );

        return;
    }

    if ( !already_locked ) *omessage = g_strdup( "LOCKED" );

    *omessage = add_string( *omessage, sond_akte_to_json( sond_akte ) );

    sond_akte_free( sond_akte );

    return;
}
