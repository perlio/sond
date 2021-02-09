#include <sqlite3.h>
#include <gtk/gtk.h>

#include "dbase.h"

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


static void
dbase_finalize_stmts( DBase* dbase )
{
    sqlite3_stmt* stmt = NULL;

    if ( dbase == NULL || dbase->db == NULL ) return;

    while ( (stmt = sqlite3_next_stmt( dbase->db, stmt )) ) sqlite3_finalize( stmt );

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


static gint
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

        zaehler++;
    }

    return 0;
}


static gint
dbase_create_db( sqlite3* db, gchar** errmsg )
{
    gchar* errmsg_ii = NULL;
    gchar* sql = NULL;
    gint rc = 0;

    //Tabellenstruktur erstellen
    sql = //Haupttabelle
            "DROP TABLE IF EXISTS baum_auswertung; "
            "DROP TABLE IF EXISTS dateien;"
            "DROP TABLE IF EXISTS ziele;"
            "DROP TABLE IF EXISTS baum_inhalt;"

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

            "CREATE TABLE eingang ("
                "rel_path VARCHAR(200), "
                "eingangsdatum DATE, "
                "transport VARCHAR(30), " //Post, Fax, pers.,
                "traeger VARCHAR(30), " //CD, Papier, USB
                "ort VARCHAR(30), " //Kanzlei, HV-Saal
                "absender VARCHAR(30), " //LG Buxtehude, RA Meier
                "absendedatum DATE, "
                "erfassungsdatum DATE "
            "); "

            "CREATE TABLE baum_inhalt ("
                "node_id INTEGER PRIMARY KEY,"
                "parent_id INTEGER NOT NULL,"
                "older_sibling_id INTEGER NOT NULL,"
                "icon_name VARCHAR(50),"
                "node_text VARCHAR(200), "
                "FOREIGN KEY (eingang) REFERENCES eingang (ID) "
                "ON DELETE CASCADE ON UPDATE CASCADE, "
                "FOREIGN KEY (parent_id) REFERENCES baum_inhalt (node_id) "
                "ON DELETE CASCADE ON UPDATE CASCADE, "
                "FOREIGN KEY (older_sibling_id) REFERENCES baum_inhalt (node_id) "
                "ON DELETE CASCADE ON UPDATE CASCADE "
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


static DBase*
dbase_new( void )
{
    return g_malloc0( sizeof( DBase ) );
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

    dbase->db = db;

    gchar* sql = "PRAGMA foreign_keys = ON; PRAGMA case_sensitive_like = ON";
    rc = sqlite3_exec( dbase->db, sql, NULL, NULL, errmsg );
    if ( rc != SQLITE_OK ) ERROR( "sqlite3_exec (PRAGMA)" )

    rc = dbase_prepare_stmts( dbase, errmsg );
    if ( rc ) ERROR( "dbase_prepare_stmts" )

    return 0;
}


gint
dbase_create( const gchar* path, DBase** dbase, gboolean create,
        gboolean overwrite,gchar** errmsg )
{
    gint rc = 0;

    *dbase = dbase_new( );

    rc = dbase_open( path, *dbase, create, overwrite, errmsg );
    if ( rc )
    {
        dbase_destroy( *dbase );
        if ( rc == -1 ) ERROR( "dbase_open" )
        else return 1;
    }

    return 0;
}

