/*
zond (zond_dbase.c) - Akten, Beweisstücke, Unterlagen
Copyright (C) 2022  pelo america

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

#include "zond_dbase.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <sqlite3.h>
#include <gtk/gtk.h>

#include "../misc.h"

#define OFFSET ((baum == BAUM_INHALT) ? 0 : 1)

typedef enum
{
    PROP_PATH = 1,
    PROP_DBASE,
    N_PROPERTIES
} ZondDBaseProperty;

typedef struct
{
    gchar* path;
    sqlite3* dbase;
} ZondDBasePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(ZondDBase, zond_dbase, G_TYPE_OBJECT)


static void
zond_dbase_set_property (GObject      *object,
                          guint         property_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    ZondDBase* self = ZOND_DBASE(object);
    ZondDBasePrivate* priv = zond_dbase_get_instance_private( self );

    switch ((ZondDBaseProperty) property_id)
    {
        case PROP_PATH:
          priv->path = g_strdup( g_value_get_string(value) );
          break;

        case PROP_DBASE:
          priv->dbase = g_value_get_pointer(value);
          break;

        default:
          /* We don't have any other property... */
          G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
          break;
    }
}


static void
zond_dbase_get_property (GObject    *object,
                          guint       property_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    ZondDBase* self = ZOND_DBASE(object);
    ZondDBasePrivate* priv = zond_dbase_get_instance_private( self );

    switch ((ZondDBaseProperty) property_id)
    {
        case PROP_PATH:
                g_value_set_string( value, priv->path );
                break;

        case PROP_DBASE:
                g_value_set_pointer( value, priv->dbase );
                break;

        default:
                /* We don't have any other property... */
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
                break;
    }
}


static void
zond_dbase_finalize_stmts( sqlite3* db)
{
    sqlite3_stmt* stmt = NULL;

    while ( (stmt = sqlite3_next_stmt( db, NULL )) ) sqlite3_finalize( stmt );

    return;
}


static void
zond_dbase_finalize( GObject* self )
{
    ZondDBasePrivate* priv = zond_dbase_get_instance_private( ZOND_DBASE(self) );

    zond_dbase_finalize_stmts( priv->dbase);
    sqlite3_close( priv->dbase );

    g_free( priv->path );

    G_OBJECT_CLASS(zond_dbase_parent_class)->finalize( self );

    return;
}


static void
zond_dbase_class_init( ZondDBaseClass* klass )
{
    GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->finalize = zond_dbase_finalize;

    object_class->set_property = zond_dbase_set_property;
    object_class->get_property = zond_dbase_get_property;

    obj_properties[PROP_PATH] =
            g_param_spec_string( "path",
                                 "gchar*",
                                 "Pfad zur Datei.",
                                 NULL,
                                  G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

    obj_properties[PROP_DBASE] =
            g_param_spec_pointer( "dbase",
                                 "sqlite3*",
                                 "Datenbankverbindung.",
                                  G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE );

    g_object_class_install_properties(object_class,
                                      N_PROPERTIES,
                                      obj_properties);

    return;
}


static void
zond_dbase_init( ZondDBase* self )
{
//    ZondDBasePrivate* priv = zond_dbase_get_instance_private( self );

    return;
}


static gint
zond_dbase_create_db_maj_0( sqlite3* db, gchar** errmsg )
{
    gchar* errmsg_ii = NULL;
    gchar* sql = NULL;
    gint rc = 0;

    //Tabellenstruktur erstellen
    sql = //Haupttabelle
            "DROP TABLE IF EXISTS links; "
            "DROP TABLE IF EXISTS dateien;"
            "DROP TABLE IF EXISTS ziele;"
            "DROP TABLE IF EXISTS baum_inhalt;"
            "DROP TABLE IF EXISTS baum_auswertung; "

            "CREATE TABLE baum_inhalt ("
                "node_id INTEGER PRIMARY KEY,"
                "parent_id INTEGER NOT NULL,"
                "older_sibling_id INTEGER NOT NULL,"
                "icon_name VARCHAR(50),"
                "node_text VARCHAR(200), "
                "FOREIGN KEY (parent_id) REFERENCES baum_inhalt (node_id) "
                "ON DELETE CASCADE ON UPDATE CASCADE, "
                "FOREIGN KEY (older_sibling_id) REFERENCES baum_inhalt (node_id) "
                "ON DELETE CASCADE ON UPDATE CASCADE "
            "); "

            "INSERT INTO baum_inhalt (node_id, parent_id, older_sibling_id, "
            "node_text) VALUES (0, 0, 0, '" MAJOR "');"

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

            "INSERT INTO baum_auswertung (node_id, parent_id, older_sibling_id) "
            "VALUES (0, 0, 0); " //mit eingang

            "CREATE TABLE links ( "
            "ID INTEGER PRIMARY KEY AUTOINCREMENT, " //order of appe...
            "baum_id INTEGER, "
            "node_id INTEGER, "
            "projekt_target VARCHAR (200), "
            "baum_id_target INTEGER, "
            "node_id_target INTEGER "
            " ); "

            "CREATE TRIGGER delete_links_baum_inhalt_trigger BEFORE DELETE ON baum_inhalt "
            "BEGIN "
            "DELETE FROM links WHERE node_id=old.node_id AND baum_id=1; "
            //etwaige Links, die auf gelöschten Knoten zeigen, auch löschen
            "DELETE FROM baum_inhalt WHERE node_id = "
                "(SELECT node_id FROM links WHERE node_id_target = old.node_id);"
            "END; "

            "CREATE TRIGGER delete_links_baum_auswertung_trigger BEFORE DELETE ON baum_auswertung "
            "BEGIN "
            "DELETE FROM links WHERE node_id=old.node_id AND baum_id=2; "
            "DELETE FROM baum_auswertung WHERE node_id = "
                "(SELECT node_id FROM links WHERE node_id_target = old.node_id);"
            "END; "
            ;

    rc = sqlite3_exec( db, sql, NULL, NULL, &errmsg_ii );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_exec (create db): "
                "\nresult code: ", sqlite3_errstr( rc ), "\nerrmsg: ",
                errmsg_ii, NULL );
        sqlite3_free( errmsg_ii );

        return -1;
    }

    return 0;
}


//v0.7: in Tabellen baum_inhalt und baum_auswertung Spalte icon_id statt icon_name in v0.8
static gint
zond_dbase_convert_from_v0_7( sqlite3* db_convert, gchar** errmsg )
{
    gint rc = 0;
    gchar* errmsg_ii = NULL;

    gchar* sql = "INSERT INTO main.baum_inhalt "
                    "SELECT node_id, parent_id, older_sibling_id, "
                            "CASE icon_id "
                            "WHEN 0 THEN 'dialog-error' "
                            "WHEN 1 THEN 'emblem-new' "
                            "WHEN 2 THEN 'folder' "
                            "WHEN 3 THEN 'document-open' "
                            "WHEN 4 THEN 'pdf' "
                            "WHEN 5 THEN 'anbindung' "
                            "WHEN 6 THEN 'akte' "
                            "WHEN 7 THEN 'application-x-executable' "
                            "WHEN 8 THEN 'text-x-generic' "
                            "WHEN 9 THEN 'x-office-document' "
                            "WHEN 10 THEN 'x-office-presentation' "
                            "WHEN 11 THEN 'x-office-spreadsheet' "
                            "WHEN 12 THEN 'emblem-photo' "
                            "WHEN 13 THEN 'video-x-generic' "
                            "WHEN 14 THEN 'audio-x-generic' "
                            "WHEN 15 THEN 'mail-unread' "
                            "WHEN 16 THEN 'emblem-web' "
                            "WHEN 25 THEN 'system-log-out' "
                            "WHEN 26 THEN 'mark-location' "
                            "WHEN 27 THEN 'phone' "
                            "WHEN 28 THEN 'emblem-important' "
                            "WHEN 29 THEN 'camera-web' "
                            "WHEN 30 THEN 'media-optical' "
                            "WHEN 31 THEN 'user-info' "
                            "WHEN 32 THEN 'system-users' "
                            "WHEN 33 THEN 'orange' "
                            "WHEN 34 THEN 'blau' "
                            "WHEN 35 THEN 'rot' "
                            "WHEN 36 THEN 'gruen' "
                            "WHEN 37 THEN 'tuerkis' "
                            "WHEN 38 THEN 'magenta' "
                            "ELSE 'process-stop' "
                            "END, "
                        "node_text FROM old.baum_inhalt WHERE node_id!=0; "
            "INSERT INTO main.dateien SELECT uri, node_id FROM old.dateien; "
            "INSERT INTO main.ziele SELECT ziel_id_von, index_von, ziel_id_bis, index_bis, "
            "(SELECT uri FROM old.dateien WHERE datei_id=old.ziele.datei_id), "
            "node_id FROM old.ziele; "
            "INSERT INTO main.baum_auswertung "
                    "SELECT node_id, parent_id, older_sibling_id, "
                            "CASE icon_id "
                            "WHEN 0 THEN 'dialog-error' "
                            "WHEN 1 THEN 'emblem-new' "
                            "WHEN 2 THEN 'folder' "
                            "WHEN 3 THEN 'document-open' "
                            "WHEN 4 THEN 'pdf' "
                            "WHEN 5 THEN 'anbindung' "
                            "WHEN 6 THEN 'akte' "
                            "WHEN 7 THEN 'application-x-executable' "
                            "WHEN 8 THEN 'text-x-generic' "
                            "WHEN 9 THEN 'x-office-document' "
                            "WHEN 10 THEN 'x-office-presentation' "
                            "WHEN 11 THEN 'x-office-spreadsheet' "
                            "WHEN 12 THEN 'emblem-photo' "
                            "WHEN 13 THEN 'video-x-generic' "
                            "WHEN 14 THEN 'audio-x-generic' "
                            "WHEN 15 THEN 'mail-unread' "
                            "WHEN 16 THEN 'emblem-web' "
                            "WHEN 25 THEN 'system-log-out' "
                            "WHEN 26 THEN 'mark-location' "
                            "WHEN 27 THEN 'phone' "
                            "WHEN 28 THEN 'emblem-important' "
                            "WHEN 29 THEN 'camera-web' "
                            "WHEN 30 THEN 'media-optical' "
                            "WHEN 31 THEN 'user-info' "
                            "WHEN 32 THEN 'system-users' "
                            "WHEN 33 THEN 'orange' "
                            "WHEN 34 THEN 'blau' "
                            "WHEN 35 THEN 'rot' "
                            "WHEN 36 THEN 'gruen' "
                            "WHEN 37 THEN 'tuerkis' "
                            "WHEN 38 THEN 'magenta' "
                            "ELSE 'process-stop' "
                            "END, "
                        "node_text, text, ref_id FROM old.baum_auswertung WHERE node_id!=0; ";

    rc = sqlite3_exec( db_convert, sql, NULL, NULL, &errmsg_ii );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_exec:\n"
                "result code: ", sqlite3_errstr( rc ), "\nerrmsg: ",
                errmsg_ii, NULL );
        sqlite3_free( errmsg_ii );

        return -1;
    }

    return 0;
}


//v0.8 ist wie Maj_0, aber ohne links
//v0.9 ist wie 0.8, aber mit Tabelle eingang
static gint
zond_dbase_convert_from_v0_8_or_v0_9( sqlite3* db_convert, gchar** errmsg )
{
    gint rc = 0;

    gchar* sql =
            "INSERT INTO baum_inhalt SELECT node_id, parent_id, older_sibling_id, "
                    "icon_name, node_text FROM old.baum_inhalt WHERE node_id != 0; "
            "INSERT INTO baum_auswertung SELECT * FROM old.baum_auswertung WHERE node_id != 0; "
            "INSERT INTO dateien SELECT * FROM old.dateien; "
            "INSERT INTO ziele SELECT * FROM old.ziele; ";

    rc = sqlite3_exec( db_convert, sql, NULL, NULL, errmsg );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = add_string( g_strdup( "Bei Aufruf sqlite3_exec:\n" ),
                *errmsg );

        return -1;
    }

    return 0;
}


//v0.10 ist wie v0.9, aber mit links
static gint
zond_dbase_convert_from_v0_10( sqlite3* db_convert, gchar** errmsg )
{
    gint rc = 0;

    gchar* sql =
            "INSERT INTO baum_inhalt SELECT node_id, parent_id, older_sibling_id, "
                    "icon_name, node_text FROM old.baum_inhalt WHERE node_id != 0; "
            "INSERT INTO baum_auswertung SELECT * FROM old.baum_auswertung WHERE node_id != 0; "
            "INSERT INTO links SELECT * FROM old.links; "
            "INSERT INTO dateien SELECT * FROM old.dateien; "
            "INSERT INTO ziele SELECT * FROM old.ziele; ";

    rc = sqlite3_exec( db_convert, sql, NULL, NULL, errmsg );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = add_string( g_strdup( "Bei Aufruf sqlite3_exec:\n" ),
                *errmsg );

        return -1;
    }

    return 0;
}


static gint
zond_dbase_convert_to_maj_0( const gchar* path, gchar* v_string,
        gchar** errmsg ) //eingang hinzugefügt
{
    gint rc = 0;
    sqlite3* db = NULL;
    gchar* sql = NULL;
    gchar* path_old = NULL;
    gchar* path_new = NULL;

    path_new = g_strconcat( path, ".tmp", NULL );
    rc = sqlite3_open( path_new, &db);
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf "
                "sqlite3_open:\n", sqlite3_errmsg( db ), NULL );
        g_free( path_new );

        return -1;
    }

    if ( zond_dbase_create_db_maj_0( db, errmsg ) )
    {
        sqlite3_close( db);
        g_free( path_new );

        ERROR_S
    }

    sql = g_strdup_printf( "ATTACH DATABASE '%s' AS old;", path );
    rc = sqlite3_exec( db, sql, NULL, NULL, errmsg );
    g_free( sql );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_exec:\n"
                "result code: ", sqlite3_errstr( rc ), "\nerrmsg: ",
                *errmsg, NULL );
        sqlite3_close( db );
        g_free( path_new );

        return -1;
    }

    if ( !g_strcmp0( v_string , "v0.7" ) )
    {
        rc = zond_dbase_convert_from_v0_7( db, errmsg );
        if ( rc )
        {
            sqlite3_close( db );
            g_free( path_new );

            ERROR_S
        }
    }
    else if ( !g_strcmp0( v_string , "v0.8" ) || !g_strcmp0( v_string , "v0.9" ) )
    {
        rc = zond_dbase_convert_from_v0_8_or_v0_9( db, errmsg );
        if ( rc )
        {
            sqlite3_close( db );
            g_free( path_new );
            ERROR_S
        }
    }
    else if ( !g_strcmp0( v_string , "v0.10" ) )
    {
        rc = zond_dbase_convert_from_v0_10( db, errmsg );
        if ( rc )
        {
            sqlite3_close( db );
            g_free( path_new );
            ERROR_S
        }
    }

    sqlite3_close( db );

    path_old = g_strconcat( path, v_string, NULL );
    rc = g_rename( path, path_old);
    g_free( path_old );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_rename:\n",
                strerror( errno ), NULL );
        g_free( path_new );

        return -1;
    }

    rc = g_rename ( path_new, path );
    g_free( path_new );
    if ( rc )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_rename:\n",
                strerror( errno ), NULL );

        return -1;
    }

    return 0;
}


static gint
zond_dbase_create_db( sqlite3* db, gchar** errmsg )
{
    gint rc = 0;

    rc = zond_dbase_create_db_maj_0( db, errmsg );
    if ( rc ) ERROR_S

    return 0;
}


static gchar*
zond_dbase_get_version( sqlite3* db, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt* stmt = NULL;
    gchar* v_string = NULL;

    rc = sqlite3_prepare_v2( db, "SELECT node_text FROM baum_inhalt WHERE node_id = 0;", -1, &stmt, NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2:\n",
                sqlite3_errstr( rc ), NULL );

        return NULL;
    }

    rc = sqlite3_step( stmt );
    if ( rc != SQLITE_ROW )
    {
        if ( errmsg ) *errmsg = add_string( g_strconcat( "Bei Aufruf sqlite3_step:\n",
                sqlite3_errmsg( db ), NULL ), *errmsg );
        sqlite3_finalize( stmt );

        return NULL;
    }

    if ( !sqlite3_column_text( stmt, 0 ) || !g_strcmp0( (const gchar*) sqlite3_column_text( stmt, 0 ), "" ) )
    {
        sqlite3_finalize( stmt );
        ERROR_S_MESSAGE_VAL( "ZND-Datei enthält keine Versionsbezeichnung", NULL )
    }

    v_string = g_strdup( (const gchar*) sqlite3_column_text( stmt, 0 ) );

    sqlite3_finalize( stmt );

    return v_string;
}


static gint
zond_dbase_open( const gchar* path, gboolean create_file, gboolean create, sqlite3** db, gchar** errmsg )
{
    gint rc = 0;

    rc = sqlite3_open_v2( path, db, SQLITE_OPEN_READWRITE |
            ((create_file || create) ? SQLITE_OPEN_CREATE : 0), NULL );
    if ( rc != SQLITE_OK ) //Datei nicht vorhanden und weder create_file noch create
    {
        sqlite3_close( *db );

        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_open_v2:\n",
                sqlite3_errstr( rc ), NULL );

        return -1;
    }
    else if ( !(create_file || create) ) //Alt-Datei war vorhanden - Versions-Check
    {
        gchar* v_string = NULL;

        v_string = zond_dbase_get_version( *db, errmsg );
        if ( !v_string )
        {
            sqlite3_close( *db );
            ERROR_S
        }

        if ( v_string[0] == 'v' ) //legacy...
        {
            gint rc = 0;

            sqlite3_close( *db );

            rc = zond_dbase_convert_to_maj_0( path, v_string, errmsg );
            g_free( v_string );
            if ( rc ) ERROR_S

            rc = sqlite3_open_v2( path, db, SQLITE_OPEN_READWRITE, NULL );
            if ( rc ) ERROR_S

            v_string = g_strdup( MAJOR );
        }

        if ( !g_ascii_isdigit( v_string[0] ) )
        {
            g_free( v_string );
            sqlite3_close( *db );
            ERROR_S_MESSAGE( "Unbekannte Versionsbezeichnung" )
        }
        else if ( atoi( v_string ) > atoi( MAJOR ) )
        {
            g_free( v_string );
            sqlite3_close( *db );
            ERROR_S_MESSAGE( "Version gibt's noch gar nicht" )
        }
        else if ( atoi( v_string ) < atoi( MAJOR ) )
        {
            gint rc = 0;

            //aktewalisieren
        }

    }
    else if ( create ) //Datenbank soll neu angelegt werden
    {
        gint rc = 0;

        //Abfrage, ob überschrieben werden soll, überflüssig - schon im filechooser
        rc = zond_dbase_create_db( *db, errmsg );
        if ( rc )
        {
            sqlite3_close( *db );
            ERROR_S
        }
    }

    rc = sqlite3_exec( *db, "PRAGMA foreign_keys = ON; PRAGMA recursive_triggers = 1; ",
            NULL, NULL, errmsg );
    if ( rc != SQLITE_OK )
    {
        sqlite3_close( *db );
        ERROR_S
    }

    return 0;
}


gint
zond_dbase_new( const gchar* path, gboolean create_file, gboolean create,
        ZondDBase** zond_dbase, gchar** errmsg )
{
    gint rc = 0;
    sqlite3* db = NULL;

    g_return_val_if_fail( zond_dbase, -1 );

    rc = zond_dbase_open( path, create_file, create, &db, errmsg );
    if ( rc ) ERROR_S

    *zond_dbase = g_object_new( ZOND_TYPE_DBASE, "path", path, "dbase", db, NULL );

    return 0;
}


void
zond_dbase_close( ZondDBase* zond_dbase )
{
    g_object_unref( zond_dbase );

    return;
}


sqlite3*
zond_dbase_get_dbase( ZondDBase* zond_dbase )
{
    ZondDBasePrivate* priv = zond_dbase_get_instance_private( zond_dbase );

    return priv->dbase;
}


const gchar*
zond_dbase_get_path( ZondDBase* zond_dbase )
{
    ZondDBasePrivate* priv = zond_dbase_get_instance_private( zond_dbase );

    return priv->path;
}


gint
zond_dbase_backup( ZondDBase* src, ZondDBase* dst, gchar** errmsg )
{
    gint rc = 0;
    sqlite3* db_src = NULL;
    sqlite3* db_dst = NULL;
    sqlite3_backup* backup = NULL;

    db_src = zond_dbase_get_dbase( src );
    db_dst = zond_dbase_get_dbase( dst );

    //Datenbank öffnen
    backup = sqlite3_backup_init( db_dst, "main", db_src, "main" );
    if ( !backup )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf ", __func__,
                ":\nBei Aufruf sqlite3_backup_init\nresult code: ",
                sqlite3_errstr( sqlite3_errcode( db_dst ) ), "\n",
                sqlite3_errmsg( db_dst ), NULL);

        return -1;
    }
    rc = sqlite3_backup_step( backup, -1 );
    sqlite3_backup_finish( backup );
    if ( rc != SQLITE_DONE )
    {
        if ( errmsg && rc == SQLITE_NOTADB ) *errmsg = g_strdup( "Datei ist "
                "keine SQLITE-Datenbank" );
        else if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf ", __func__,
                "Bei Aufruf sqlite3_backup_step:\nresult code: ",
                sqlite3_errstr( rc ), "\n", sqlite3_errmsg( db_dst ), NULL );

        return -1;
    }

    return 0;
}


static gint
zond_dbase_prepare_stmts( ZondDBase* zond_dbase, gint num, const gchar** sql,
        sqlite3_stmt** stmt, gchar** errmsg )
{
    for ( gint i = 0; i < num; i++ )
    {
        gint rc = 0;

        ZondDBasePrivate* priv = zond_dbase_get_instance_private( zond_dbase );

        rc = sqlite3_prepare_v2( priv->dbase, sql[i], -1, &stmt[i], NULL );
        if ( rc != SQLITE_OK && errmsg )
        {
            //aufräumen
            for ( gint u = 0; u < i; u++ ) sqlite3_finalize( stmt[u] );
            ERROR_ZOND_DBASE( "sqlite3_prepare_v2" )
        }
    }

    return 0;
}


gint
zond_dbase_prepare( ZondDBase* zond_dbase, const gchar* func, const gchar** sql,
        gint num_stmts, sqlite3_stmt*** stmt, gchar** errmsg )
{
    if ( !(*stmt = g_object_get_data( G_OBJECT(zond_dbase), func )) )
    {
        gint rc = 0;

        *stmt = g_malloc0( sizeof( sqlite3_stmt* ) * num_stmts );

        rc = zond_dbase_prepare_stmts( zond_dbase, num_stmts, sql, *stmt, errmsg );
        if ( rc )
        {
            g_free( *stmt );
            ERROR_S
        }

        g_object_set_data_full( G_OBJECT(zond_dbase), func, *stmt, g_free );
    }

    for ( gint i = 0; i < num_stmts; i++ ) sqlite3_reset( (*stmt)[i] );

    return 0;
}


gint
zond_dbase_begin( ZondDBase* zond_dbase, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "BEGIN; "
        };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )

    return 0;
}


gint
zond_dbase_commit( ZondDBase* zond_dbase, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "COMMIT; "
        };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )

    return 0;
}


gint
zond_dbase_rollback( ZondDBase* zond_dbase, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "ROLLBACK; "
        };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )

    return 0;
}


gint
zond_dbase_insert_node( ZondDBase* zond_dbase, Baum baum, gint node_id, gboolean child,
        const gchar* icon_name, const gchar* node_text, gchar** errmsg )
{
    gint rc = 0;
    gint new_node_id = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "INSERT INTO baum_inhalt "
            "(parent_id, older_sibling_id, icon_name, node_text) "
            "VALUES ("
                "CASE ?1 " //child
                    "WHEN 0 THEN (SELECT parent_id FROM baum_inhalt WHERE node_id=?2) "
                    "WHEN 1 THEN ?2 " //node_id
                "END, "
                "CASE ?1 "
                    "WHEN 0 THEN ?2 "
                    "WHEN 1 THEN 0 "
                "END, "
                "?3, " //icon_name
                "?4); ", //node_text

            "INSERT INTO baum_auswertung "
            "(parent_id, older_sibling_id, icon_name, node_text) "
            "VALUES ("
                "CASE ?1 " //child
                    "WHEN 0 THEN (SELECT parent_id FROM baum_auswertung WHERE node_id=?2) "
                    "WHEN 1 THEN ?2 " //node_id
                "END, "
                "CASE ?1 "
                    "WHEN 0 THEN ?2 "
                    "WHEN 1 THEN 0 "
                "END, "
                "?3, " //icon_name
                "?4); ", //node_text

            "UPDATE baum_inhalt SET older_sibling_id=last_insert_rowid() "
                "WHERE "
                    "parent_id=(SELECT parent_id FROM baum_inhalt WHERE node_id=last_insert_rowid()) "
                "AND "
                    "older_sibling_id=(SELECT older_sibling_id FROM baum_inhalt WHERE node_id=last_insert_rowid()) "
                "AND "
                    "node_id!=last_insert_rowid() "
                "AND "
                    "node_id!=0; ",

            "UPDATE baum_auswertung SET older_sibling_id=last_insert_rowid() "
                "WHERE "
                    "parent_id=(SELECT parent_id FROM baum_auswertung WHERE node_id=last_insert_rowid()) "
                "AND "
                    "older_sibling_id=(SELECT older_sibling_id FROM baum_auswertung WHERE node_id=last_insert_rowid()) "
                "AND "
                    "node_id!=last_insert_rowid() "
                "AND "
                    "node_id!=0; ",

                "VALUES (last_insert_rowid()); " };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_int( stmt[0 + OFFSET], 1, child );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (child)" )

    rc = sqlite3_bind_int( stmt[0 + OFFSET], 2, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_bind_text( stmt[0 + OFFSET], 3,
            icon_name, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (icon_name)" )

    rc = sqlite3_bind_text( stmt[0 + OFFSET], 4, node_text,
            -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_text (node_text)" )

    rc = sqlite3_step( stmt[0 + OFFSET] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step [0/1]" )

    rc = sqlite3_step( stmt[2 + OFFSET] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step ([2/3])" )

    rc = sqlite3_step( stmt[4] );
    if ( rc != SQLITE_ROW ) ERROR_ZOND_DBASE( "sqlite3_step ([4])" )

    new_node_id = sqlite3_column_int( stmt[4], 0 );

    return new_node_id;
}


gint
zond_dbase_remove_node( ZondDBase* zond_dbase, Baum baum, gint node_id, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "UPDATE baum_inhalt SET older_sibling_id=(SELECT older_sibling_id FROM baum_inhalt "
            "WHERE node_id=?1) WHERE "
            "older_sibling_id=?1; ",

            "UPDATE baum_auswertung SET older_sibling_id=(SELECT older_sibling_id FROM baum_auswertung "
            "WHERE node_id=?1) WHERE "
            "older_sibling_id=?1; ",

            "DELETE FROM baum_inhalt WHERE node_id = ?;",

            "DELETE FROM baum_auswertung WHERE node_id = ?; ",

            //etwaige Links, die auf gelöschten Knoten zeigen, auch löschen
            "DELETE FROM baum_inhalt WHERE node_id = "
                "(SELECT node_id FROM links WHERE node_id_target = ?);",

            "DELETE FROM baum_auswertung WHERE node_id = "
                "(SELECT node_id FROM links WHERE node_id_target = ?); "
        };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_int( stmt[0 + OFFSET], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[0 + OFFSET] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step [0/1]" )

    rc = sqlite3_bind_int( stmt[2 + OFFSET], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[2 + OFFSET] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step [2/3]" )

    rc = sqlite3_bind_int( stmt[4 + OFFSET], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[4 + OFFSET] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step [4/5]" )

    return 0;
}


//Baum ist Baum Von!
gint
zond_dbase_kopieren_nach_auswertung( ZondDBase* zond_dbase, Baum baum, gint node_id_von,
        gint node_id_nach, gboolean child, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "INSERT INTO baum_auswertung "
            "(parent_id, older_sibling_id, icon_name, node_text, ref_id) "
            "VALUES ("
                "CASE ?1 " //child
                    "WHEN 0 THEN (SELECT parent_id FROM baum_auswertung WHERE node_id=?2) "
                    "WHEN 1 THEN ?2 " //node_id_nach
                "END, "
                "CASE ?1 " //older_sibling_id
                    "WHEN 0 THEN ?2 "
                    "WHEN 1 THEN 0 "
                "END, "
                "(SELECT icon_name FROM baum_inhalt WHERE node_id=?3), " //node_id_von
                "(SELECT node_text FROM baum_inhalt WHERE node_id=?3), "//node_text
                "(CASE "
                    "WHEN " //datei_id zu node_id_von?
                        "(SELECT dateien.rel_path FROM dateien LEFT JOIN ziele "
                        "ON dateien.rel_path=ziele.rel_path "
                        "JOIN baum_inhalt "
                        "ON baum_inhalt.node_id=dateien.node_id OR "
                        "baum_inhalt.node_id=ziele.node_id "
                        "WHERE baum_inhalt.node_id=?3)"
                            "IS NULL THEN 0 "
                    "ELSE ?3 "
                "END)); ",

            "INSERT INTO baum_auswertung "
            "(parent_id, older_sibling_id, icon_name, node_text, text, ref_id) "
            "VALUES ("
                "CASE ?1 " //child
                    "WHEN 0 THEN (SELECT parent_id FROM baum_auswertung WHERE node_id=?2) "
                    "WHEN 1 THEN ?2 " //node_id_nach
                "END, "
                "CASE ?1 " //older_sibling_id
                    "WHEN 0 THEN ?2 "
                    "WHEN 1 THEN 0 "
                "END, "
                "(SELECT icon_name FROM baum_auswertung WHERE node_id=?3), " //node_id_von
                "(SELECT node_text FROM baum_auswertung WHERE node_id=?3), "//node_text
                "(SELECT text FROM baum_auswertung WHERE node_id=?3), "//text
                "(CASE "
                    "WHEN " //datei_id zu node_id_von?
                        "(SELECT dateien.rel_path FROM dateien LEFT JOIN ziele "
                        "ON dateien.rel_path=ziele.rel_path "
                        "JOIN baum_inhalt "
                        "ON baum_inhalt.node_id=dateien.node_id OR "
                        "baum_inhalt.node_id=ziele.node_id "
                        "WHERE baum_inhalt.node_id="
                            "(SELECT ref_id FROM baum_auswertung WHERE node_id=?3)"
                            ") IS NULL THEN 0 "
                    "ELSE "
                        "(SELECT ref_id FROM baum_auswertung WHERE node_id=?3) "
                "END)); ",

            "UPDATE baum_auswertung SET "
                "older_sibling_id=last_insert_rowid() "
            "WHERE "
                "parent_id=(SELECT parent_id FROM baum_auswertung WHERE node_id=last_insert_rowid()) "
            "AND "
                "older_sibling_id=(SELECT older_sibling_id FROM baum_auswertung WHERE node_id=last_insert_rowid()) "
            "AND "
                "node_id!=last_insert_rowid() "
            "AND "
                "node_id!=0; " };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_int( stmt[0 + OFFSET], 1, child );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (child)" )

    rc = sqlite3_bind_int( stmt[0 + OFFSET], 2, node_id_nach );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id_nach)" )

    rc = sqlite3_bind_int( stmt[0 + OFFSET], 3, node_id_von );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id_von)" )

    rc = sqlite3_step( stmt[0 + OFFSET] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step [0/1]" )

    rc = sqlite3_step( stmt[2] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step ([2])" )

    return sqlite3_last_insert_rowid( zond_dbase_get_dbase( zond_dbase ) );
}


gint
zond_dbase_verschieben_knoten( ZondDBase* zond_dbase, Baum baum, gint node_id, gint new_parent_id,
        gint new_older_sibling_id, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "UPDATE baum_inhalt SET older_sibling_id="
            "(SELECT older_sibling_id FROM baum_inhalt WHERE node_id=?1)" //node_id
            "WHERE older_sibling_id=?1; ",

            "UPDATE baum_auswertung SET older_sibling_id="
            "(SELECT older_sibling_id FROM baum_auswertung WHERE node_id=?1)"
            "WHERE older_sibling_id=?1; ",

            "UPDATE baum_inhalt SET older_sibling_id=?1 WHERE node_id=" //node_id
                "(SELECT node_id FROM baum_inhalt WHERE parent_id=?2 AND older_sibling_id=?3); ", //new_parent_id/new_older_s_id

            "UPDATE baum_auswertung SET older_sibling_id=?1 WHERE node_id=" //node_id
                "(SELECT node_id FROM baum_auswertung WHERE parent_id=?2 AND older_sibling_id=?3); ", //new_parent_id/new_older_s_id

            "UPDATE baum_inhalt SET parent_id=?1, older_sibling_id=?2 WHERE node_id=?3; ",

            "UPDATE baum_auswertung SET parent_id=?1, older_sibling_id=?2 WHERE node_id=?3; " };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_int( stmt[0 + OFFSET], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int [0, 1]" )

    rc = sqlite3_step( stmt[0 + OFFSET] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step [0]" )

    rc = sqlite3_bind_int( stmt[2 + OFFSET], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int [2, 1]" )

    rc = sqlite3_bind_int( stmt[2 + OFFSET], 2, new_parent_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int [2, 2]" )

    rc = sqlite3_bind_int( stmt[2 + OFFSET], 3, new_older_sibling_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int [2, 3]" )

    rc = sqlite3_step( stmt[2 + OFFSET] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step [2]" )

    rc = sqlite3_bind_int( stmt[4 + OFFSET], 1, new_parent_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int [4, 1]" )

    rc = sqlite3_bind_int( stmt[4 + OFFSET], 2, new_older_sibling_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int [4, 2]" )

    rc = sqlite3_bind_int( stmt[4 + OFFSET], 3, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int [4, 3]" )

    rc = sqlite3_step( stmt[4 + OFFSET] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step [4]" )

    return 0;
}


/** gibt auch dann 0 zurück, wenn der Knoten gar nicht existiert   **/
gint
zond_dbase_set_icon_name( ZondDBase* zond_dbase, Baum baum, gint node_id, const gchar* icon_name,
        gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
/*  full_set_icon_id  */
            "UPDATE baum_inhalt SET icon_name = ?1 WHERE node_id = ?2;",

            "UPDATE baum_auswertung SET icon_name = ?1 WHERE node_id = ?2;"
       };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_text( stmt[0 + OFFSET], 1,
            icon_name, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_text (icon_name)" )

    rc = sqlite3_bind_int( stmt[0 + OFFSET], 2, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[0 + OFFSET] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )

    return 0;
}


gint
zond_dbase_get_icon_name_and_node_text( ZondDBase* zond_dbase, Baum baum, gint node_id, gchar** icon_name,
        gchar** node_text, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
        //...
            "SELECT icon_name, node_text, node_id FROM baum_inhalt WHERE node_id=?;",

            "SELECT icon_name, node_text, node_id FROM baum_auswertung WHERE node_id=?;"
        };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_int( stmt[0 + OFFSET], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[0 + OFFSET] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_ZOND_DBASE( "sqlite3_step" )

    if ( rc == SQLITE_ROW )
    {
        gchar* buf_icon_name = (gchar*)
                sqlite3_column_text( stmt[0 + OFFSET], 0 );
        if ( buf_icon_name && icon_name ) *icon_name= g_strdup( buf_icon_name );
        else if ( node_text ) *node_text = g_strdup( "" );

        gchar* buf_node_text = (gchar*)
                sqlite3_column_text( stmt[0 + OFFSET], 1 );
        if ( buf_node_text && node_text ) *node_text = g_strdup( buf_node_text );
        else if ( node_text ) *node_text = g_strdup( "" );
    }
    else if ( rc == SQLITE_DONE ) return 1;

    return 0;
}



/** Die folgende Funktion, mit denen einzelnen Felder eines Knotens
*** verändert werden können, geben auch dann 0 zurück, wenn der Knoten gar nicht
*** existiert   **/
gint
zond_dbase_set_text( ZondDBase* zond_dbase, gint node_id, gchar* text, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "UPDATE baum_auswertung SET text=? WHERE node_id=?;"
       };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_text( stmt[0], 1, text, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_text (text)" )

    rc = sqlite3_bind_int( stmt[0], 2, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int ( node_id)" )

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )

    return 0;
}


//betrifft nur tabelle baum_auswertung!
gint
zond_dbase_get_text( ZondDBase* zond_dbase, gint node_id, gchar** text, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
        //...
            "SELECT node_id, text FROM baum_auswertung WHERE node_id = ?;"
       };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_int( stmt[0], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_ZOND_DBASE( "sqlite3_step" )
    else if ( rc == SQLITE_DONE )
    {
        if ( errmsg ) *errmsg = add_string( *errmsg,
                g_strconcat( "node_id existiert nicht", NULL ) );

        return -2;
    }

    if ( rc == SQLITE_ROW && text ) *text =
            g_strdup( (const gchar*) sqlite3_column_text( stmt[0], 1 ) );

    return 0;
}


gint
zond_dbase_set_ziel( ZondDBase* zond_dbase, Ziel* ziel, gint anchor_id, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
        //...
            "INSERT INTO ziele (ziel_id_von, index_von, "
            "ziel_id_bis, index_bis, rel_path, node_id) "
            "VALUES (?, ?, ?, ?,"
            "(SELECT dateien.rel_path FROM dateien "
            "LEFT JOIN ziele "
            "ON dateien.rel_path=ziele.rel_path "
            "JOIN baum_inhalt "
            "ON baum_inhalt.node_id=dateien.node_id OR "
            "baum_inhalt.node_id=ziele.node_id "
            "WHERE baum_inhalt.node_id=?), "
            "last_insert_rowid())"
        };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_text( stmt[0], 1,
            ziel->ziel_id_von, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_text [0,1]" )

    rc = sqlite3_bind_int( stmt[0], 2, ziel->index_von );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int [0,2]" )

    rc = sqlite3_bind_text( stmt[0], 3, ziel->ziel_id_bis,
            -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_text [0,3]" )

    rc = sqlite3_bind_int( stmt[0], 4, ziel->index_bis );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_text [0,4]" )

    rc = sqlite3_bind_int( stmt[0], 5, anchor_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int [0,5]" )

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step [0]" )

    return 0;
}


gint
zond_dbase_set_datei( ZondDBase* zond_dbase, gint node_id, const gchar* rel_path, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
    /*  set_datei (10) */
            "INSERT INTO dateien (rel_path, node_id) VALUES (?, ?); "
        };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_text( stmt[0], 1, rel_path,
            -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_text (rel_path)" )

    rc = sqlite3_bind_int( stmt[0], 2, node_id);
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step [0]" )

    return 0;
}


/** Rückgabe:
*** Fehler: -1
*** node_id nicht vorhanden: -2
*** ansonsten: parent_id */
gint
zond_dbase_get_parent( ZondDBase* zond_dbase, Baum baum, gint node_id, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;
    gint parent_id = 0;

    const gchar* sql[] = {
        //...
            "SELECT parent_id FROM baum_inhalt WHERE node_id = ?;",

            "SELECT parent_id FROM baum_auswertung WHERE node_id = ?;"
        };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_int( stmt[0 + OFFSET], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[0 + OFFSET] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_ZOND_DBASE( "sqlite3_step" )

    if ( rc == SQLITE_ROW ) parent_id =
            sqlite3_column_int( stmt[0 + OFFSET], 0 );

    if ( rc == SQLITE_DONE )
    {
        if ( errmsg ) *errmsg = add_string( *errmsg,
                g_strconcat( "node_id existiert nicht", NULL ) );
        //Denn jeder Knoten hat Eltern
        return -2;
    }

    return parent_id;
}


/**
*** Rückgabe:
*** Fehler: -1
*** node_id existiert nicht: -2
*** ansonsten: older_sibling_id
**/
gint
zond_dbase_get_older_sibling( ZondDBase* zond_dbase, Baum baum, gint node_id, gchar** errmsg )
{
    gint rc = 0;
    gint older_sibling_id = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
        //...
            "SELECT older_sibling_id FROM baum_inhalt WHERE node_id = ?;",

            "SELECT older_sibling_id FROM baum_auswertung WHERE node_id = ?;"
        };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_int( stmt[0 + OFFSET], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[0 + OFFSET] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_ZOND_DBASE( "sqlite3_step" )

    if ( rc == SQLITE_ROW ) older_sibling_id =
            sqlite3_column_int( stmt[0 + OFFSET], 0 );

    if ( rc == SQLITE_DONE )
    {
        if ( errmsg ) *errmsg = add_string( *errmsg,
                g_strconcat( "node_id existiert nicht", NULL ) );
        //Denn jeder Knoten hat ein älteres Geschwister, auch wenn es 0 ist

        return -2;
    }

    return older_sibling_id;
}


gint
zond_dbase_get_younger_sibling( ZondDBase* zond_dbase, Baum baum, gint node_id, gchar** errmsg )
{
    gint rc = 0;
    gint younger_sibling_id = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
        //...
            "SELECT inhalt1.node_id, inhalt2.node_id FROM baum_inhalt AS inhalt1 "
                "LEFT JOIN baum_inhalt AS inhalt2 "
                "ON inhalt1.node_id = inhalt2.older_sibling_id "
                "WHERE inhalt1.node_id > 0 AND inhalt1.node_id = ?;",

            "SELECT inhalt1.node_id, inhalt2.node_id FROM baum_auswertung AS inhalt1 "
                "LEFT JOIN baum_auswertung AS inhalt2 "
                "ON inhalt1.node_id = inhalt2.older_sibling_id "
                "WHERE inhalt1.node_id > 0 AND inhalt1.node_id = ?;"
        };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_int( stmt[0 + OFFSET], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[0 + OFFSET] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_ZOND_DBASE( "sqlite3_step" )

    if ( rc == SQLITE_ROW ) younger_sibling_id =
            sqlite3_column_int( stmt[0 + OFFSET], 1 );

    if ( rc == SQLITE_DONE )
    {
        if ( errmsg ) *errmsg = g_strdup( "node_id existiert nicht" );

        return -2;
    }

    return younger_sibling_id;
}


gint
zond_dbase_get_first_child( ZondDBase* zond_dbase, Baum baum, gint node_id, gchar** errmsg )
{
    gint rc = 0;
    gint first_child_id = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
        //...
            "SELECT inhalt1.node_id, inhalt2.node_id FROM baum_inhalt AS inhalt1 "
                "LEFT JOIN baum_inhalt AS inhalt2 "
                "ON inhalt1.node_id = inhalt2.parent_id AND inhalt2.older_sibling_id = 0 "
                    "AND inhalt2.node_id != 0 "
                "WHERE inhalt1.node_id = ?;",

            "SELECT inhalt1.node_id, inhalt2.node_id FROM baum_auswertung AS inhalt1 "
                "LEFT JOIN baum_auswertung AS inhalt2 "
                "ON inhalt1.node_id = inhalt2.parent_id AND inhalt2.older_sibling_id = 0 "
                    "AND inhalt2.node_id != 0 "
                "WHERE inhalt1.node_id = ?;"
        };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_int( stmt[0 + OFFSET], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[0 + OFFSET] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_ZOND_DBASE( "sqlite3_step" )

    if ( rc == SQLITE_ROW ) first_child_id =
            sqlite3_column_int( stmt[0 + OFFSET], 1 );

    if ( rc == SQLITE_DONE ) ERROR_S_MESSAGE_VAL( "node_id existiert nicht", -2 )

    return first_child_id;
}


gint
zond_dbase_get_ref_id( ZondDBase* zond_dbase, gint node_id, gchar** errmsg )
{
    gint rc = 0;
    gint ref_id = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
        //...
            "SELECT ref_id FROM baum_auswertung WHERE node_id=?"
        };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_int( stmt[0], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_ZOND_DBASE( "sqlite3_step" )

    if ( rc == SQLITE_ROW ) ref_id =
            sqlite3_column_int( stmt[0], 0 );
    else if ( rc == SQLITE_DONE )
    {
        if ( errmsg ) *errmsg = add_string( *errmsg,
                g_strconcat( "node_id existiert nicht", NULL ) );

        return -2;
    }

    return ref_id;
}


/** Rückgabewert:
    Wenn alles ok: 0
    Wenn Fehler (inkl. node_id existiert nicht: -1 - errmsg wird - wenn != NULL - gesetzt
    Wenn kein rel_path zur node_id: 1
**/
gint
zond_dbase_get_rel_path( ZondDBase* zond_dbase, Baum baum, gint node_id, gchar** rel_path,
        gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;
    gchar* text = NULL;

    const gchar* sql[] = {
        //...
            "SELECT baum_inhalt.node_id, rel_path FROM baum_inhalt "
            "LEFT JOIN "
            "(SELECT dateien.rel_path AS rel_path, dateien.node_id AS d_node_id, "
            "ziele.node_id AS z_node_id FROM dateien LEFT JOIN ziele "
            "ON dateien.rel_path=ziele.rel_path) "
            "ON baum_inhalt.node_id=d_node_id OR "
            "baum_inhalt.node_id=z_node_id "
            "WHERE baum_inhalt.node_id=?;"
        };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    if ( baum == BAUM_AUSWERTUNG )
    {
        node_id = zond_dbase_get_ref_id( zond_dbase, node_id, errmsg );
        if ( node_id < 0 ) ERROR_S
    }

    rc = sqlite3_bind_int( stmt[0], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_ZOND_DBASE( "sqlite3_step" )
    else if ( rc == SQLITE_DONE )
    {
        if ( errmsg ) *errmsg = g_strdup( "node_id existiert nicht" );

        return -1;
    }

    text = (gchar*) sqlite3_column_text( stmt[0], 1 );

    if ( !text || !g_strcmp0( text, "" ) ) return 1;

    if ( rel_path ) *rel_path = g_strdup( (const gchar*) text );

    return 0;
}


/** Rückgabewert:
    Falls kein ziel: 1, *ziel unverändert
    Fall ziel: 0, *ziel wird allociert, falls != NULL
    Fehler: -1
**/
gint
zond_dbase_get_ziel( ZondDBase* zond_dbase, Baum baum, gint node_id, Ziel* ziel, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
        //...
            "SELECT ziel_id_von, index_von, ziel_id_bis, index_bis FROM ziele "
            "WHERE node_id=?;"
        };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    if ( baum == BAUM_AUSWERTUNG )
    {
        node_id = zond_dbase_get_ref_id( zond_dbase, node_id, errmsg );
        if ( node_id < 0 ) ERROR_S
    }

    rc = sqlite3_bind_int( stmt[0], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_ZOND_DBASE( "sqlite3_step" )
    else if ( rc == SQLITE_DONE ) return 1;

    if ( !ziel ) return 0;

    gchar* buf = NULL;

    //ziel_id_von
    buf = (gchar*) sqlite3_column_text( stmt[0], 0 );
    ziel->ziel_id_von = g_strdup( buf );

    //ziel_id_bis
    buf = (gchar*) sqlite3_column_text( stmt[0], 2 );
    ziel->ziel_id_bis = g_strdup( buf );

    //index_von und -bis
    ziel->index_von = sqlite3_column_int( stmt[0], 1 );
    ziel->index_bis = sqlite3_column_int( stmt[0], 3 );

    return 0;
}


/*  Gibt 0 zurück, wenn rel_path nicht in Tabelle vorhanden
**  Bei Fehler: -1  */
gint
zond_dbase_get_node_id_from_rel_path( ZondDBase* zond_dbase, const gchar* rel_path, gchar** errmsg )
{
    gint rc = 0;
    gint node_id = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
    //...
        "SELECT node_id FROM dateien WHERE rel_path=?;"
    };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_text( stmt[0], 1, rel_path, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_text (rel_path)" )

    rc = sqlite3_step( stmt[0] );
    if ( (rc != SQLITE_ROW) && rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )

    if ( rc == SQLITE_ROW ) node_id = sqlite3_column_int( stmt[0], 0 );

    return node_id;
}


gint
zond_dbase_check_id( ZondDBase* zond_dbase, const gchar* id, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
    //...
        "SELECT ziel_id_von FROM ziele WHERE ziel_id_von=?1;"
        "UNION "
        "SELECT ziel_id_bis FROM ziele WHERE ziel_id_bis=?1;"
    };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_text( stmt[0], 1, id, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_text (id)" )

    rc = sqlite3_step( stmt[0] );
    if ( (rc != SQLITE_ROW) && rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )

    if ( rc == SQLITE_ROW ) return 1;

    return 0;
}


gint
zond_dbase_set_node_text( ZondDBase* zond_dbase, Baum baum, gint node_id,
        const gchar* node_text, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
    //...
        "UPDATE baum_inhalt SET node_text = ?1 WHERE node_id = ?2;",

        "UPDATE baum_auswertung SET node_text = ?1 WHERE node_id = ?2;"
    };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_text( stmt[0 + OFFSET], 1,
            node_text, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_text (text)" )

    rc = sqlite3_bind_int( stmt[0 + OFFSET], 2, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[0 + OFFSET] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )

    return 0;
}


gint
zond_dbase_set_link( ZondDBase* zond_dbase, const gint baum_id,
        const gint node_id, const gchar* projekt_dest, const gint baum_id_dest,
        const gint node_id_dest, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

/*  set_link  */
    const gchar* sql[] = {
            "INSERT INTO links (baum_id, node_id, projekt_target, "
                "baum_id_target, node_id_target) "
                "VALUES (?, ?, ?, ?, ?); "
            };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_int( stmt[0], 1, baum_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (baum_id)" )

    rc = sqlite3_bind_int( stmt[0], 2, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_bind_text( stmt[0], 3, projekt_dest,
            -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_text (projekt)" )

    rc = sqlite3_bind_int( stmt[0], 4, baum_id_dest );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (baum_id_dest)" )

    rc = sqlite3_bind_int( stmt[0], 5, node_id_dest );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id_dest)" )

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )

    return 0;
}


gint
zond_dbase_check_link( ZondDBase* zond_dbase, Baum baum, gint node_id, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
    //...
        "SELECT ID FROM links WHERE baum_id=?1 AND node_id=?2;"
    };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_int( stmt[0], 1, baum );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (baum)" )

    rc = sqlite3_bind_int( stmt[0], 2, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[0] );
    if ( (rc != SQLITE_ROW) && rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )

    if ( rc == SQLITE_ROW ) return sqlite3_column_int( stmt[0], 0 );

    return 0;
}


gint
zond_dbase_get_link( ZondDBase* zond_dbase, gint ID, Baum* baum,
        gint* node_id, gchar** project, Baum* baum_target, gint* node_id_target,
        gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
    //...
        "SELECT baum_id, node_id, projekt_target, baum_id_target, node_id_target FROM links "
            "WHERE ID=?1;"
    };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_int( stmt[0], 1, ID );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[0] );
    if ( (rc != SQLITE_ROW) && rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )

    if ( rc == SQLITE_DONE ) return 1;

    if ( baum ) *baum = sqlite3_column_int( stmt[0], 0 );
    if ( node_id ) *node_id = sqlite3_column_int( stmt[0], 1 );
    if ( project ) *project = g_strdup( (const gchar*) sqlite3_column_text( stmt[0], 2 ) );
    if ( baum_target ) *baum_target = sqlite3_column_int( stmt[0], 3 );
    if ( node_id_target ) *node_id_target = sqlite3_column_int( stmt[0], 4 );

    return 0;
}


gint
zond_dbase_remove_link( ZondDBase* zond_dbase, const gint baum_id, const gint
        node_id, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
    /*  remove_link */
            "DELETE FROM links WHERE baum_id=? AND node_id=?; "
    };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_int( stmt[0], 1, baum_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (baum_id)" )

    rc = sqlite3_bind_int( stmt[0], 2, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )

    return 0;
}


gint
zond_dbase_update_path( ZondDBase* zond_dbase, const gchar* old_path, const gchar* new_path, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "UPDATE dateien SET rel_path = "
            "REPLACE( SUBSTR( rel_path, 1, LENGTH( ?1 ) ), ?1, ?2 ) || "
            "SUBSTR( rel_path, LENGTH( ?1 ) + 1 );"
    };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_text( stmt[0], 1, old_path, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_text (old_path)" )

    rc = sqlite3_bind_text( stmt[0], 2, new_path, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_text (new_path)" )

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )

    return 0;
}


/*  Gibt 0 zurück, wenn rel_path in db nicht vorhanden, wenn doch: 1
**  Bei Fehler: -1  */
gint
zond_dbase_test_path( ZondDBase* zond_dbase, const gchar* rel_path, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "SELECT node_id FROM dateien WHERE rel_path=?1;"
    };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, errmsg );
    if ( rc ) ERROR_S

    rc = sqlite3_bind_text( stmt[0], 1, rel_path, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_text" )

    rc = sqlite3_step( stmt[0] );
    if ( (rc != SQLITE_ROW) && rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step" )

    if ( rc == SQLITE_ROW ) return 1;

    return 0;
}


