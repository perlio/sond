#include <sqlite3.h>
#include <gtk/gtk.h>

#include "dbase.h"
#include "eingang.h"

#include "misc.h"


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

    rc = sqlite3_step( dbase->update_path );
    if ( rc != SQLITE_DONE ) ERROR_DBASE( "sqlite3_step (rollback)" )

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


gint
dbase_get_eingang_for_rel_path( DBase* dbase, const gchar* rel_path, gint* ID,
        Eingang** eingang, gint* ID_eingang_rel_path, gchar** errmsg )
{
    gint rc = 0;

    sqlite3_reset( dbase->get_eingang_for_rel_path );

    rc = sqlite3_bind_text( dbase->get_eingang_for_rel_path, 1, rel_path, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_DBASE( "sqlite3_bind_text" )

    rc = sqlite3_step( dbase->get_eingang_for_rel_path );
    if ( (rc != SQLITE_ROW) && rc != SQLITE_DONE ) ERROR_DBASE( "sqlite3_step" )

    if ( rc == SQLITE_DONE ) return 1; //rel_path nicht vorhanden

    if ( ID ) *ID = sqlite3_column_int( dbase->get_eingang_for_rel_path, 8 );
    *eingang = g_malloc0( sizeof( Eingang ) );

    (*eingang)->eingangsdatum = g_strdup( (const gchar*) sqlite3_column_text( dbase->get_eingang_for_rel_path, 0 ) );
    (*eingang)->transport = g_strdup( (const gchar*) sqlite3_column_text( dbase->get_eingang_for_rel_path, 1 ) );
    (*eingang)->traeger = g_strdup( (const gchar*) sqlite3_column_text( dbase->get_eingang_for_rel_path, 2 ) );
    (*eingang)->ort = g_strdup( (const gchar*) sqlite3_column_text( dbase->get_eingang_for_rel_path, 3 ) );
    (*eingang)->absender = g_strdup( (const gchar*) sqlite3_column_text( dbase->get_eingang_for_rel_path, 4 ) );
    (*eingang)->absendedatum = g_strdup( (const gchar*) sqlite3_column_text( dbase->get_eingang_for_rel_path, 5 ) );
    (*eingang)->erfassungsdatum = g_strdup( (const gchar*) sqlite3_column_text( dbase->get_eingang_for_rel_path, 6 ) );
    if ( ID_eingang_rel_path ) *ID_eingang_rel_path =
            sqlite3_column_int( dbase->get_eingang_for_rel_path, 7 );

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

    return 0;

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


static void
dbase_finalize_stmts( DBase* dbase )
{
    sqlite3_stmt* stmt = NULL;
    sqlite3_stmt* next_stmt = NULL;

    if ( dbase == NULL || dbase->db == NULL ) return;

    stmt = sqlite3_next_stmt( dbase->db, NULL );

    if ( !stmt ) return;

    do
    {
        next_stmt = sqlite3_next_stmt( dbase->db, stmt );
        sqlite3_finalize( stmt );
        stmt = next_stmt;
    }
    while ( stmt );

    return;
}


void
dbase_destroy( DBase* dbase )
{
    gint rc = 0;

    if ( !dbase ) return;

    dbase_finalize_stmts( dbase );

    rc = sqlite3_close( dbase->db );
    if ( rc != SQLITE_OK )
    {
        //log: time, "Database (dbase->path) konnte nicht geschlossen werden->BUSY
    }

    g_free( dbase );

    return;
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
            "absender, absendedatum, erfassungsdatum, eingang_rel_path.ID "
            "FROM eingang LEFT JOIN "
            "eingang_rel_path "
            "ON eingang.ID=eingang_rel_path.eingang_id WHERE rel_path=?1;",

            "INSERT INTO eingang (eingangsdatum, transport, traeger, ort, "
            "absender, absendedatum, erfassungsdatum) "
            "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7); ",

            "UPDATE eingang SET eingangsdatum=?1,transport=?2,traeger=?3,ort=?4,"
            "absender=?5,absendedatum=?6,erfassungsdatum=?7 WHERE ID=?8; ",

            "INSERT INTO eingang_rel_path (eingang_id, rel_path) "
            "VALUES (?1, ?2); ",

            "UPDATE eingang_rel_path SET eingang_id=?1, rel_path=?2 "
            "WHERE ID=?3; ",

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
        else if ( zaehler == 8 ) dbase->insert_eingang_rel_path = stmt;
        else if ( zaehler == 9 ) dbase->update_eingang_rel_path = stmt;

        zaehler++;
    }

    return 0;
}


gint
dbase_create_db( sqlite3* db, gchar** errmsg )
{
    gchar* errmsg_ii = NULL;
    gchar* sql = NULL;
    gint rc = 0;

    //Tabellenstruktur erstellen
    sql = //Haupttabelle
            "DROP TABLE IF EXISTS baum_auswertung; "
            "DROP TABLE IF EXISTS eingang; "
            "DROP TABLE IF EXISTS eingang_rel_path; "
            "DROP TABLE IF EXISTS dateien;"
            "DROP TABLE IF EXISTS ziele;"
            "DROP TABLE IF EXISTS baum_inhalt;"
/*
            "CREATE TABLE IF NOT EXISTS Entities ( "
                "ID INTEGER NOT NULL, "
                "Bezeichnung TEXT, "
                "PRIMARY KEY(ID) "
            "); "
            "CREATE TABLE IF NOT EXISTS Properties ( "
                "ID INTEGER NOT NULL, "
                "Bezeichnung TEXT, "
                "adm_entity TEXT, "
                "adm_value TEXT, "
                "PRIMARY KEY(ID) "
            "); "
            "CREATE TABLE IF NOT EXISTS Statements ( "
                "ID INTEGER NOT NULL, "
                "entity INTEGER NOT NULL, "
                "property INTEGER NOT NULL, "
                "value TEXT, "
                "FOREIGN KEY (property) REFERENCES Properties (ID), "
                "PRIMARY KEY(ID) "
            "); "
*/
            "CREATE TABLE eingang ("
                "ID INTEGER NOT NULL, "
                "eingangsdatum VARCHAR(20), "
                "transport VARCHAR(30), " //Post, Fax, pers.,
                "traeger VARCHAR(30), " //CD, Papier, USB
                "ort VARCHAR(30), " //Kanzlei, HV-Saal
                "absender VARCHAR(30), " //LG Buxtehude, RA Meier
                "absendedatum VARCHAR(20), "
                "erfassungsdatum VARCHAR(20), "
                "PRIMARY KEY(ID) "
            "); "

            "CREATE TABLE eingang_rel_path ( "
                "ID INTEGER NOT NULL, "
                "eingang_id INTEGER NOT NULL, "
                "rel_path VARCHAR(200) NOT NULL, "
                "PRIMARY KEY(ID), "
                "FOREIGN KEY (eingang_id) REFERENCES eingang (ID) "
                "ON DELETE CASCADE ON UPDATE CASCADE "
            "); "

            "CREATE TABLE baum_inhalt ("
                "node_id INTEGER PRIMARY KEY,"
                "parent_id INTEGER NOT NULL,"
                "older_sibling_id INTEGER NOT NULL,"
                "icon_name VARCHAR(50),"
                "node_text VARCHAR(200), "
                "eingang_id INTEGER NULL DEFAULT NULL, "
                "FOREIGN KEY (eingang_id) REFERENCES eingang_rel_path (ID) "
                "ON DELETE CASCADE ON UPDATE CASCADE, "
                "FOREIGN KEY (parent_id) REFERENCES baum_inhalt (node_id) "
                "ON DELETE CASCADE ON UPDATE CASCADE, "
                "FOREIGN KEY (older_sibling_id) REFERENCES baum_inhalt (node_id) "
                "ON DELETE RESTRICT ON UPDATE CASCADE "
            "); "

            "INSERT INTO baum_inhalt (node_id, parent_id, older_sibling_id, "
            "node_text ) VALUES (0, 0, 0, 'zondv1');"

            //Hilfstabelle "dateien"
            //hier werden angebundene Dateien erfaßt
            "CREATE TABLE dateien ("
            "rel_path VARCHAR(200) PRIMARY KEY,"
            "node_id INTEGER NOT NULL, "
            "FOREIGN KEY (node_id) REFERENCES baum_inhalt (node_id) "
            "ON DELETE CASCADE ON UPDATE CASCADE);"

            //Hilfstabelle "ziele"
            //hier werden Anbindungen an Dateien mit Zusatzinfo abgelegt
            "CREATE TABLE ziele ("
            "ziel_id_von VARCHAR(50), "
            "index_von INTEGER, "
            "ziel_id_bis VARCHAR(50), "
            "index_bis INTEGER, "
            "rel_path VARCHAR(200) NOT NULL, "
            "node_id INTEGER NOT NULL, "
            "PRIMARY KEY (ziel_id_von, index_von, ziel_id_bis, index_bis), "
            "FOREIGN KEY (rel_path) REFERENCES dateien (rel_path) "
            "ON DELETE CASCADE ON UPDATE CASCADE,"
            "FOREIGN KEY (node_id) REFERENCES baum_inhalt (node_id) "
            "ON DELETE CASCADE ON UPDATE CASCADE );"

            //Auswertungs-Baum
            "CREATE TABLE baum_auswertung ( "
            "node_id INTEGER PRIMARY KEY,"
            "parent_id INTEGER NOT NULL,"
            "older_sibling_id INTEGER NOT NULL,"
            "icon_name VARCHAR(50),"
            "node_text VARCHAR(200),"
            "text VARCHAR, "
            "ref_id INTEGER NULL DEFAULT NULL,"
            "FOREIGN KEY (parent_id) REFERENCES baum_auswertung (node_id) "
            "ON DELETE CASCADE ON UPDATE CASCADE, "
            "FOREIGN KEY (older_sibling_id) REFERENCES baum_auswertung (node_id) "
            "ON DELETE CASCADE ON UPDATE CASCADE, "
            "FOREIGN KEY (ref_id) REFERENCES baum_inhalt (node_id) "
            "ON DELETE RESTRICT ON UPDATE RESTRICT );"

            "INSERT INTO baum_auswertung (node_id, parent_id, older_sibling_id, "
            "node_text) VALUES (0, 0, 0, 'root')";

    rc = sqlite3_exec( db, sql, NULL, NULL, &errmsg_ii );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_exec\nsql: ",
                sql, "\nresult code: ", sqlite3_errstr( rc ), "\nerrmsg: ",
                errmsg_ii, NULL );
        sqlite3_free( errmsg_ii );

        return -1;
    }

    return 0;
}


gint
dbase_open( const gchar* path, DBase* dbase, gboolean create, gboolean overwrite,
        gchar** errmsg )
{
    gint rc = 0;
    sqlite3* db = NULL;

    if ( !dbase ) return 0;

    rc = sqlite3_open_v2( path, &db, SQLITE_OPEN_READWRITE | ((overwrite == FALSE) ? 0 : SQLITE_OPEN_CREATE), NULL );

    if ( rc != SQLITE_OK ) sqlite3_close( db );

    if ( rc != SQLITE_OK && !create )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_open_v2:\n",
                sqlite3_errstr( rc ), NULL );

        return -1;
    }
    else if ( create && rc == SQLITE_CANTOPEN )
    {
        gint rc = 0;

        rc = sqlite3_open( path, &db );
        if ( rc != SQLITE_OK )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_open:\n",
                    sqlite3_errstr( rc ), NULL );
            sqlite3_close( db );

            return -1;
        }

        rc = dbase_create_db( db, errmsg );
        if ( rc )
        {
            sqlite3_close( db );
            ERROR( "dbase_create_db" )
        }
    }
    else if ( create ) //Datei existiert - überschreiben?
    {
        gint rc = 0;

        rc = abfrage_frage( NULL, "Datei existiert bereits", "Überschreiben?",
                NULL );
        if ( rc != GTK_RESPONSE_YES )
        {
            sqlite3_close( db );
            return 1; //Abbruch gewählt
        }

        rc = dbase_create_db( db, errmsg );
        if ( rc )
        {
            sqlite3_close( db );
            ERROR( "dbase_create_db" )
        }
    }

    gchar* sql = "PRAGMA foreign_keys = ON; PRAGMA case_sensitive_like = ON";
    rc = sqlite3_exec( db, sql, NULL, NULL, errmsg );
    if ( rc != SQLITE_OK )
    {
        sqlite3_close( db );
        ERROR( "sqlite3_exec (PRAGMA)" )
    }

    dbase->db = db;

    return 0;
}


static DBase*
dbase_new( void )
{
    return g_malloc0( sizeof( DBase ) );
}


gint
dbase_create_with_stmts( const gchar* path, DBase** dbase, gboolean create,
        gboolean overwrite, gchar** errmsg )
{
    gint rc = 0;

    *dbase = dbase_new( );

    rc = dbase_open( path, *dbase, create, overwrite, errmsg );
    if ( rc )
    {
        dbase_destroy( *dbase );
        *dbase = NULL;
        if ( rc == -1 ) ERROR( "dbase_open" )
        else return 1;
    }

    rc = dbase_prepare_stmts( *dbase, errmsg );
    if ( rc )
    {
        dbase_destroy( *dbase );
        *dbase = NULL;
        ERROR( "dbase_prepare_stmts" )
    }

    return 0;
}

