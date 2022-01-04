#include <sqlite3.h>
#include <gtk/gtk.h>
#include <glib/gstdio.h>

#include "sond_database.h"

#include "zond/error.h"

#include "dbase.h"
#include "eingang.h"

#include "misc.h"

#define DB_VERSION "v0.9"


gint
dbase_begin( DBase* dbase, gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( dbase->begin_transaction );

    rc = sqlite3_step( dbase->begin_transaction );
    if ( rc != SQLITE_DONE ) ERROR_DBASE( "sqlite3_step" )

    return 0;
}


gint
dbase_commit( DBase* dbase, gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( dbase->commit );

    rc = sqlite3_step( dbase->commit );
    if ( rc != SQLITE_DONE ) ERROR_DBASE( "sqlite3_step" )

    return 0;
}


gint
dbase_rollback( DBase* dbase, gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( dbase->rollback );

    rc = sqlite3_step( dbase->rollback );
    if ( rc != SQLITE_DONE ) ERROR_DBASE( "sqlite3_step(rollback)" )

    return 0;
}


gint
dbase_update_path( DBase* dbase, const gchar* old_path, const gchar* new_path, gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( dbase->update_path );

    rc = sqlite3_bind_text( dbase->update_path, 1, old_path, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_DBASE( "sqlite3_bind_text (old_path)" )

    rc = sqlite3_bind_text( dbase->update_path, 2, new_path, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_DBASE( "sqlite3_bind_text (new_path)" )

    rc = sqlite3_step( dbase->update_path );
    if ( rc != SQLITE_DONE ) ERROR_DBASE( "sqlite3_step" )

    return 0;
}


/*  Gibt 0 zurück, wenn rel_path in db nicht vorhanden, wenn doch: 1
**  Bei Fehler: -1  */
gint
dbase_test_path( DBase* dbase, const gchar* rel_path, gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( dbase->test_path );

    rc = sqlite3_bind_text( dbase->test_path, 1, rel_path, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_DBASE( "sqlite3_bind_text" )

    rc = sqlite3_step( dbase->test_path );
    if ( (rc != SQLITE_ROW) && rc != SQLITE_DONE ) ERROR_DBASE( "sqlite3_step" )

    if ( rc == SQLITE_ROW ) return 1;

    return 0;
}


/*  eingang: Zeiger auf initialisierte Eingang-Struktur
*/
gint
dbase_get_eingang_for_rel_path( DBase* dbase, const gchar* rel_path, gint* ID,
        Eingang* eingang, gint* ID_eingang_rel_path, gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( dbase->get_eingang_for_rel_path );

    rc = sqlite3_bind_text( dbase->get_eingang_for_rel_path, 1, rel_path, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_DBASE( "sqlite3_bind_text" )

    rc = sqlite3_step( dbase->get_eingang_for_rel_path );
    if ( (rc != SQLITE_ROW) && rc != SQLITE_DONE ) ERROR_DBASE( "sqlite3_step" )

    if ( rc == SQLITE_DONE ) return 1; //rel_path nicht vorhanden

    if ( eingang )
    {
        eingang->eingangsdatum = g_strdup( (const gchar*) sqlite3_column_text( dbase->get_eingang_for_rel_path, 0 ) );
        eingang->transport = g_strdup( (const gchar*) sqlite3_column_text( dbase->get_eingang_for_rel_path, 1 ) );
        eingang->traeger = g_strdup( (const gchar*) sqlite3_column_text( dbase->get_eingang_for_rel_path, 2 ) );
        eingang->ort = g_strdup( (const gchar*) sqlite3_column_text( dbase->get_eingang_for_rel_path, 3 ) );
        eingang->absender = g_strdup( (const gchar*) sqlite3_column_text( dbase->get_eingang_for_rel_path, 4 ) );
        eingang->absendedatum = g_strdup( (const gchar*) sqlite3_column_text( dbase->get_eingang_for_rel_path, 5 ) );
        eingang->erfassungsdatum = g_strdup( (const gchar*) sqlite3_column_text( dbase->get_eingang_for_rel_path, 6 ) );
    }
    if ( ID_eingang_rel_path ) *ID_eingang_rel_path =
            sqlite3_column_int( dbase->get_eingang_for_rel_path, 7 );

    if ( ID ) *ID = sqlite3_column_int( dbase->get_eingang_for_rel_path, 8 );

    return 0;
}


gint
dbase_insert_eingang( DBase* dbase, Eingang* eingang, gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( dbase->insert_eingang );

    rc = sqlite3_bind_text( dbase->insert_eingang, 1, eingang->eingangsdatum, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_DBASE( "sqlite3_bind_text (eingangsdatum)" )

    rc = sqlite3_bind_text( dbase->insert_eingang, 2, eingang->transport, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_DBASE( "sqlite3_bind_text (transport)" )

    rc = sqlite3_bind_text( dbase->insert_eingang, 3, eingang->traeger, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_DBASE( "sqlite3_bind_text (traeger)" )

    rc = sqlite3_bind_text( dbase->insert_eingang, 4, eingang->ort, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_DBASE( "sqlite3_bind_text (ort)" )

    rc = sqlite3_bind_text( dbase->insert_eingang, 5, eingang->absender, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_DBASE( "sqlite3_bind_text (absender)" )

    rc = sqlite3_bind_text( dbase->insert_eingang, 6, eingang->absendedatum, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_DBASE( "sqlite3_bind_text (absendedatum)" )

    rc = sqlite3_bind_text( dbase->insert_eingang, 7, eingang->erfassungsdatum, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_DBASE( "sqlite3_bind_text (erfassungsdatum)" )

    rc = sqlite3_step( dbase->insert_eingang );
    if ( rc != SQLITE_DONE ) ERROR_DBASE( "sqlite3_step" )

    return (gint) sqlite3_last_insert_rowid( dbase->db );
}


gint
dbase_update_eingang( DBase* dbase, const gint ID, Eingang* eingang, gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( dbase->update_eingang );

    rc = sqlite3_bind_text( dbase->update_eingang, 1, eingang->eingangsdatum, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_DBASE( "sqlite3_bind_text (eingangsdatum)" )

    rc = sqlite3_bind_text( dbase->update_eingang, 2, eingang->transport, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_DBASE( "sqlite3_bind_text (transport)" )

    rc = sqlite3_bind_text( dbase->update_eingang, 3, eingang->traeger, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_DBASE( "sqlite3_bind_text (traeger)" )

    rc = sqlite3_bind_text( dbase->update_eingang, 4, eingang->ort, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_DBASE( "sqlite3_bind_text (ort)" )

    rc = sqlite3_bind_text( dbase->update_eingang, 5, eingang->absender, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_DBASE( "sqlite3_bind_text (absender)" )

    rc = sqlite3_bind_text( dbase->update_eingang, 6, eingang->absendedatum, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_DBASE( "sqlite3_bind_text (absendedatum)" )

    rc = sqlite3_bind_text( dbase->update_eingang, 7, eingang->erfassungsdatum, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_DBASE( "sqlite3_bind_text (erfassungsdatum)" )

    rc = sqlite3_bind_int( dbase->update_eingang, 8, ID );
    if ( rc != SQLITE_OK ) ERROR_DBASE( "sqlite3_bind_int (ID)" )

    rc = sqlite3_step( dbase->update_eingang );
    if ( rc != SQLITE_DONE ) ERROR_DBASE( "sqlite3_step" )

    return 0;
}


gint
dbase_delete_eingang( DBase* dbase, const gint eingang_id, gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( dbase->delete_eingang );

    rc = sqlite3_bind_int( dbase->delete_eingang, 1, eingang_id );
    if ( rc != SQLITE_OK ) ERROR_DBASE( "sqlite3_bind_int (eingang_id)" )

    rc = sqlite3_step( dbase->delete_eingang );
    if ( rc != SQLITE_ROW ) ERROR_DBASE( "sqlite3_step" )

    return 0;
}


gint
dbase_insert_eingang_rel_path( DBase* dbase, const gint ID, const gchar* rel_path,
        gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( dbase->insert_eingang_rel_path );

    rc = sqlite3_bind_int( dbase->insert_eingang_rel_path, 1, ID );
    if ( rc != SQLITE_OK ) ERROR_DBASE( "sqlite3_bind_int (ID)" )

    rc = sqlite3_bind_text( dbase->insert_eingang_rel_path, 2, rel_path, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_DBASE( "sqlite3_bind_text (rel_path)" )

    rc = sqlite3_step( dbase->insert_eingang_rel_path );
    if ( rc != SQLITE_DONE ) ERROR_DBASE( "sqlite3_step" )

    return (gint) sqlite3_last_insert_rowid( dbase->db );

}


gint
dbase_update_eingang_rel_path( DBase* dbase, const gint ID, const gint eingang_id,
        const gchar* rel_path, gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( dbase->update_eingang_rel_path );

    rc = sqlite3_bind_int( dbase->update_eingang_rel_path, 1, eingang_id );
    if ( rc != SQLITE_OK ) ERROR_DBASE( "sqlite3_bind_int (eingang_id)" )

    rc = sqlite3_bind_text( dbase->update_eingang_rel_path, 2, rel_path, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_DBASE( "sqlite3_bind_text (rel_path)" )

    rc = sqlite3_bind_int( dbase->update_eingang_rel_path, 3, ID );
    if ( rc != SQLITE_OK ) ERROR_DBASE( "sqlite3_bind_int (ID)" )

    rc = sqlite3_step( dbase->update_eingang_rel_path );
    if ( rc != SQLITE_DONE ) ERROR_DBASE( "sqlite3_step" )

    return 0;
}


gint
dbase_delete_eingang_rel_path( DBase* dbase, const gint ID, gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( dbase->delete_eingang_rel_path );

    rc = sqlite3_bind_int( dbase->delete_eingang_rel_path, 1, ID );
    if ( rc != SQLITE_OK ) ERROR_DBASE( "sqlite3_bind_int (ID)" )

    rc = sqlite3_step( dbase->delete_eingang_rel_path );
    if ( rc != SQLITE_ROW ) ERROR_DBASE( "sqlite3_step" )

    return 0;
}


gint
dbase_get_num_of_refs_to_eingang( DBase* dbase, const gint eingang_id, gchar** errmsg )
{
    gint rc = 0;
    gint count = 0;

    sqlite3_reset( dbase->get_num_of_refs_to_eingang );

    rc = sqlite3_bind_int( dbase->get_num_of_refs_to_eingang, 1, eingang_id );
    if ( rc != SQLITE_OK ) ERROR_DBASE( "sqlite3_bind_int (eingang_id)" )

    rc = sqlite3_step( dbase->get_num_of_refs_to_eingang );
    if ( rc != SQLITE_ROW && rc != SQLITE_DONE ) ERROR_DBASE( "sqlite3_step" )

    if ( rc == SQLITE_ROW ) count = sqlite3_column_int( dbase->get_num_of_refs_to_eingang, 0 );

    return count;
}


sqlite3_stmt*
dbase_prepare_stmt( sqlite3* db, const gchar* sql, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt* stmt = NULL;

    rc = sqlite3_prepare_v2( db, sql, -1, &stmt, NULL );
    if ( rc != SQLITE_OK && errmsg )
    {
        *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2 (", sql, "):\n",
                sqlite3_errstr( rc ), NULL );

        return NULL;
    }

    return stmt;
}


gint
dbase_prepare_stmts( DBase* dbase, gchar** errmsg )
{
    gint zaehler = 0;
    sqlite3_stmt* stmt = NULL;
    gchar* sql[] = {
            "BEGIN;",

            "COMMIT;",

            "ROLLBACK;",

            "UPDATE dateien SET rel_path = "
            "REPLACE( SUBSTR( rel_path, 1, LENGTH( ?1 ) ), ?1, ?2 ) || "
            "SUBSTR( rel_path, LENGTH( ?1 ) + 1 );",

            "SELECT node_id FROM dateien WHERE rel_path=?1;",

            //get_eingang_for_rel_path
            "SELECT eingangsdatum, transport, traeger, ort, "
            "absender, absendedatum, erfassungsdatum, eingang_rel_path.ID, eingang.ID "
            "FROM eingang LEFT JOIN "
            "eingang_rel_path "
            "ON eingang.ID=eingang_rel_path.eingang_id WHERE rel_path=?1;",

            //dbase_insert_eingang
            "INSERT INTO eingang (eingangsdatum, transport, traeger, ort, "
            "absender, absendedatum, erfassungsdatum) "
            "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7); ",

            //dbase_update_eingang
            "UPDATE eingang SET eingangsdatum=?1,transport=?2,traeger=?3,ort=?4,"
            "absender=?5,absendedatum=?6,erfassungsdatum=?7 WHERE ID=?8; ",

            //dbase_delete_eingang
            "DELETE FROM eingang WHERE ID=?1; ",

            //dbase_insert_eingang_rel_path
            "INSERT INTO eingang_rel_path (eingang_id, rel_path) "
            "VALUES (?1, ?2); ",

            //dbase_update_eingang_rel_path
            "UPDATE eingang_rel_path SET eingang_id=?1, rel_path=?2 "
            "WHERE ID=?3; ",

            //dbase_delete_eingang_rel_path
            "DELETE FROM eingang_rel_path WHERE ID=?1; ",

            //dbase_get_num_of_refs_to_eingang
            "SELECT COUNT(*) FROM eingang_rel_path LEFT JOIN eingang "
            "ON eingang.ID = eingang_rel_path.eingang_id WHERE eingang.ID=?1; ",

            NULL };

    while ( sql[zaehler] != NULL )
    {
        stmt = dbase_prepare_stmt( dbase->db, sql[zaehler], errmsg );

        if ( !stmt )
        {
            if ( errmsg ) *errmsg = add_string( g_strdup( "Bei Aufruf dbase_prepare_stmt:\n" ),
                    *errmsg );

            return -1;
        }

        if ( zaehler == 0 ) dbase->begin_transaction = stmt;
        else if ( zaehler == 1 ) dbase->commit = stmt;
        else if ( zaehler == 2  ) dbase->rollback = stmt;
        else if ( zaehler == 3  ) dbase->update_path = stmt;
        else if ( zaehler == 4 ) dbase->test_path = stmt;
        else if ( zaehler == 5 ) dbase->get_eingang_for_rel_path = stmt;
        else if ( zaehler == 6 ) dbase->insert_eingang = stmt;
        else if ( zaehler == 7 ) dbase->update_eingang = stmt;
        else if ( zaehler == 8 ) dbase->delete_eingang = stmt;
        else if ( zaehler == 9 ) dbase->insert_eingang_rel_path = stmt;
        else if ( zaehler == 10 ) dbase->update_eingang_rel_path = stmt;
        else if ( zaehler == 11 ) dbase->delete_eingang_rel_path = stmt;
        else if ( zaehler == 12 ) dbase->get_num_of_refs_to_eingang = stmt;

        zaehler++;
    }

    return 0;
}



gint
dbase_create_with_stmts( const gchar* path, DBase** dbase, sqlite3* db, gchar** errmsg )
{
    gint rc = 0;

    *dbase = g_malloc0( sizeof( DBase ) );

    (*dbase)->db = db;

    rc = dbase_prepare_stmts( *dbase, errmsg );
    if ( rc )
    {
        g_free( *dbase );
        ERROR_SOND( "dbase_prepare_stmts" )
    }

    return 0;
}

