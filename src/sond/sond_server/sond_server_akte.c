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

#include <stdio.h>
#include <mariadb/mysql.h>
#include <json-glib/json-glib.h>

#include "../../sond_database.h"
#include "../../misc.h"

#include "../sond_akte.h"

#include "sond_server.h"
#include "sond_server_akte.h"


static SondAkte*
sond_server_akte_from_ID( MYSQL* con, gint ID_entity, GError** error )
{
    SondAkte* sond_akte = NULL;
    GArray* arr_props = NULL;

    arr_props = sond_database_get_properties( con, ID_entity, error );
    if ( !arr_props )
    {
        if ( error ) g_prefix_error( error, "%s\n", __func__ );

        return NULL;
    }
    else if ( arr_props->len == 0 )
    {
        g_array_unref( arr_props );
        *error = g_error_new( SOND_SERVER_ERROR, SOND_SERVER_ERROR_NOTFOUND,
                "%s\nZur Akte konnten keine Eigenschaften gefunden werden", __func__ );

        return NULL;
    }

    sond_akte = sond_akte_new( );

    for ( gint i = 0; i < arr_props->len; i++ )
    {
        Property property = { 0 };

        property = g_array_index( arr_props, Property, i );

        sond_akte->ID_entity = ID_entity;
        if ( property.entity.type == _REG_JAHR_ ) sond_akte->reg_jahr = atoi( property.value );
        else if ( property.entity.type == _REG_NR_ ) sond_akte->reg_nr = atoi( property.value );
        else if ( property.entity.type == _AKTENRUBRUM_) sond_akte->aktenrubrum = g_strdup( property.value );
        else if ( property.entity.type == _AKTENKURZBEZ_ ) sond_akte->aktenkurzbez = g_strdup( property.value );

        g_free( property.value );
    }

    g_array_unref( arr_props );

    return sond_akte;
}


static GArray*
sond_server_akte_search_rubrum( MYSQL* con, const gchar* params, GError** error )
{
    gint rc = 0;
    gchar* sql = NULL;
    MYSQL_RES* mysql_res = NULL;
    MYSQL_ROW row = NULL;
    GArray* arr_res = NULL;

    sql = g_strdup_printf( "SELECT rel_subject FROM entities WHERE type=%i AND prop_value LIKE '%%%s%%'; ",
            _AKTENRUBRUM_, params );

    rc = mysql_query( con, sql );
    g_free( sql );
    if ( rc )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ),
                mysql_errno( con ), "%s\nmysql_query\n%s",
                __func__, mysql_error( con ) );

        return NULL;
    }

    //abfrägen
    mysql_res = mysql_store_result( con );
    if ( !mysql_res )
    {
        if ( !mysql_field_count( con ) ) return g_array_new( FALSE, FALSE, sizeof( gint ) );
        else if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ),
                mysql_errno( con ), "%s\nmysql_store_results\n%s",
                __func__, mysql_error( con ) );

        return NULL;
    }

    arr_res = g_array_new( FALSE, FALSE, sizeof( gint ) );

    while ( (row = mysql_fetch_row( mysql_res )) )
    {
        gint ID_akte = 0;

        ID_akte = atoi( row[0] );
        g_array_append_val( arr_res, ID_akte );
    }

    mysql_free_result( mysql_res );

    return arr_res;
}


void
sond_server_akte_suchen( SondServer* sond_server, const gchar* params, gchar** omessage )
{
    GError* error = NULL;
    GArray* arr_res = NULL;
    MYSQL* con = NULL;
    JsonArray* jarray = NULL;
    JsonNode* jnode = NULL;

    con = sond_server_get_mysql_con( sond_server, &error );
    if ( !con )
    {
        *omessage = g_strconcat( "ERROR *** Keine Verbindung zu MYSQL-Server\n\n",
                error->message, NULL );
        g_warning( "Conn zum MariaDB-Server konnte nicht hergestellt werden\n\n%s",
                error->message );
        g_error_free( error );

        return;
    }

    arr_res = sond_server_akte_search_rubrum( con, params, &error );
    if ( !arr_res )
    {
        *omessage = g_strconcat( "ERROR *** Suche in DB\n\n",
                error->message, NULL );
        g_warning( "Fehler Suche in DB\n\n%s",
                error->message );
        g_error_free( error );
        mysql_close( con );

        return;
    }

    if ( arr_res->len == 0 ) //einfach kein Treffer
    {
        g_array_unref( arr_res );
        *omessage = g_strdup( "NOT_FOUND" );
        mysql_close( con );

        return;
    }

    jarray = json_array_new( );

    for ( gint i = 0; i < arr_res->len; i++ )
    {
        SondAkte* sond_akte = NULL;
        JsonObject* jobject = NULL;

        sond_akte = sond_server_akte_from_ID( con, g_array_index( arr_res, gint, i ), &error );
        if ( !sond_akte )
        {
            g_array_unref( arr_res );
            *omessage = g_strdup_printf( "ERROR *** Akten können nicht eingelesen werden - %s",
                    error->message );
            g_error_free( error );
            json_array_unref( jarray );
            mysql_close( con );

            return;
        }

        jobject = sond_akte_to_json_object( sond_akte );
        sond_akte_free( sond_akte );

        json_array_add_object_element( jarray, jobject );
    }

    mysql_close( con );
    g_array_unref( arr_res );

    jnode = json_node_alloc( );
    json_node_init_array( jnode, jarray );
    *omessage = json_to_string( jnode, FALSE );
    json_node_unref( jnode );

    return;
}


static gint
sond_server_akte_update( SondServer* sond_server, MYSQL* con, SondAkte* sond_akte, GError** error )
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
sond_server_akte_schreiben( SondServer* sond_server, gint auth,
        const gchar* params, gchar** omessage )
{
    GError* error = NULL;
    gint rc = 0;
    SondAkte* sond_akte = NULL;
    MYSQL* con = NULL;
    gint reg_jahr = 0;
    gint reg_nr = 0;
    gboolean create = FALSE;

    sond_akte = sond_akte_new_from_json( params, &error );
    if ( !sond_akte )
    {
        *omessage = g_strconcat( "ERROR *** Nachricht konnte nicht geparst werden\n\n",
                error->message, NULL );
        g_error_free( error );

        return;
    }

    if ( sond_akte->reg_nr == 0 ) create = TRUE;
    else //Update - prüfen, ob lock besteht
    {
        Lock lock = { 0 };

        lock = sond_server_has_lock( sond_server, sond_akte->ID_entity );
        if ( lock.ID_entity == 0 || lock.index != auth )
        {
            sond_akte_free( sond_akte );

            if ( lock.ID_entity == 0 ) *omessage = g_strdup( "NO_LOCK" );
            else *omessage = g_strdup_printf( "NO_LOCK%s", lock.user );

            return;
        }
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

    if ( create ) rc = sond_server_akte_create( sond_server, con, sond_akte, &error);
    else rc = sond_server_akte_update( sond_server, con, sond_akte, &error );
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

    if ( create ) *omessage = g_strdup_printf( "NEU%i-%i", reg_nr, reg_jahr );
    else *omessage = g_strdup( "OK" );

    return;
}


static gint
sond_server_akte_get_ID_from_regnr( SondServer* sond_server, MYSQL* con,
        gint reg_nr, gint reg_jahr, GError** error )
{
    gint rc = 0;
    MYSQL_RES* mysql_res = NULL;
    MYSQL_ROW row = NULL;
    gchar* sql = NULL;
    gint ID_entity = 0;

    sql = g_strdup_printf( "SELECT t1.ID_reg_jahr FROM "
            "(SELECT rel_subject AS ID_reg_jahr FROM entities WHERE type=%i AND prop_value='%i') AS t1 "
            "JOIN "
            "(SELECT rel_subject AS ID_reg_nr FROM entities WHERE type=%i AND prop_value='%i') AS t2 "
            "ON t1.ID_reg_jahr=t2.ID_reg_nr; ", _REG_JAHR_, reg_jahr, _REG_NR_, reg_nr );

    rc = mysql_query( con, sql );
    g_free( sql );
    if ( rc )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ),
                mysql_errno( con ), "%s\nmysql_query\n%s",
                __func__, mysql_error( con ) );
        g_warning( (*error)->message );
        return -1;
    }

    //abfrägen
    mysql_res = mysql_store_result( con );
    if ( !mysql_res )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ),
                mysql_errno( con ), "%s\nmysql_store_results\n%s",
                __func__, mysql_error( con ) );
        g_warning( (*error)->message );

        return -1;
    }

    row = mysql_fetch_row( mysql_res );
    if ( row ) ID_entity = atoi( row[0] );
    else
    {
        *error = g_error_new( SOND_SERVER_ERROR, SOND_SERVER_ERROR_NOTFOUND, "Keine Akte zur Registernummer" );
        mysql_free_result( mysql_res );

        return -1;
    }

    mysql_free_result( mysql_res );

    return ID_entity;
}


static SondAkte*
sond_server_akte_laden( SondServer* sond_server, gint reg_nr, gint reg_jahr, GError** error )
{
    MYSQL* con = NULL;
    MYSQL_RES* mysql_res = NULL;
    MYSQL_ROW row = NULL;
    gchar* sql_2 = NULL;
    gchar* sql_3 = NULL;
    gint rc = 0;
    gint ID_entity = 0;
    SondAkte* sond_akte = NULL;

    con = sond_server_get_mysql_con( sond_server, error );
    if ( !con )
    {
        g_prefix_error( error, "%s\n", __func__ );
        return NULL;
    }

    ID_entity = sond_server_akte_get_ID_from_regnr( sond_server,
            con, reg_nr, reg_jahr, error );
    if ( !ID_entity )
    {
        *error = g_error_new( SOND_SERVER_ERROR, SOND_SERVER_ERROR_NOTFOUND,
                "Keine Akte zur Registernummer" );
        mysql_close( con );

        return NULL;
    }

    sond_akte = sond_akte_new( );
    sond_akte->reg_jahr = reg_jahr;
    sond_akte->reg_nr = reg_nr;
    sond_akte->ID_entity = ID_entity;

    sql_2 = g_strdup_printf( "SELECT prop_value FROM entities WHERE rel_subject=%i AND type=%i; ",
            sond_akte->ID_entity, _AKTENRUBRUM_ );

    rc = mysql_query( con, sql_2 );
    g_free( sql_2 );
    if ( rc )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ),
                mysql_errno( con ), "%s\n%s\n\nFehlermeldung: %s",
                __func__, "mysql_query", mysql_error( con ) );
        mysql_close( con );
        g_warning( (*error)->message );
        sond_akte_free( sond_akte );

        return NULL;
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
        sond_akte_free( sond_akte );

        return NULL;
    }

    row = mysql_fetch_row( mysql_res );
    if ( row ) sond_akte->aktenrubrum = g_strdup( row[0] );
    else
    {
        *error = g_error_new( SOND_SERVER_ERROR, SOND_SERVER_ERROR_NOTFOUND, "Kein Aktenrubrum gespeichert" );
        mysql_free_result( mysql_res );
        mysql_close( con );
        sond_akte_free( sond_akte );

        return NULL;
    }

    mysql_free_result( mysql_res );

    sql_3 = g_strdup_printf( "SELECT prop_value FROM entities WHERE rel_subject=%i AND type=%i; ",
            sond_akte->ID_entity, _AKTENKURZBEZ_);

    rc = mysql_query( con, sql_3 );
    g_free( sql_3 );
    if ( rc )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "MARIADB" ),
                mysql_errno( con ), "%s\n%s\n\nFehlermeldung: %s",
                __func__, "mysql_query", mysql_error( con ) );
        mysql_close( con );
        g_warning( (*error)->message );
        sond_akte_free( sond_akte );

        return NULL;
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
        sond_akte_free( sond_akte );

        return NULL;
    }

    row = mysql_fetch_row( mysql_res );
    if ( row ) sond_akte->aktenkurzbez = g_strdup( row[0] );

    mysql_free_result( mysql_res );

    mysql_close( con );

    return sond_akte;
}


void
sond_server_akte_holen( SondServer* sond_server, gint auth,
        const gchar* params, gchar** omessage )
{
    gint reg_nr = 0;
    gint reg_jahr = 0;
    SondAkte* sond_akte = NULL;
    gint rc = 0;
    GError* error = NULL;
    const gchar* user = NULL;

    reg_jahr = atoi( g_strrstr( params, "-" ) + 1 );
    reg_nr = atoi( g_strndup( params, sizeof( g_strrstr( params, "-" ) ) ) );

    sond_akte = sond_server_akte_laden( sond_server, reg_nr, reg_jahr, &error );
    if ( !sond_akte )
    {
        *omessage = g_strconcat( "ERROR *** Akte kann nicht geladen werden\n\n",
                error->message, NULL );
        g_error_free( error );

        return;
    }

    //lock
    rc = sond_server_get_lock( sond_server, sond_akte->ID_entity, auth, FALSE, &user );
    if ( rc == 1 ) *omessage = g_strdup_printf( "LOCKED%s&", user );

    *omessage = add_string( *omessage, sond_akte_to_json_string( sond_akte ) );

    sond_akte_free( sond_akte );

    return;
}
