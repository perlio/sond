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
    sqlite3_stmt* next_stmt = NULL;

    stmt = sqlite3_next_stmt( db, NULL );

    if ( !stmt ) return;

    do
    {
        next_stmt = sqlite3_next_stmt( db, stmt );
        sqlite3_finalize( stmt );
        stmt = next_stmt;
    }
    while ( stmt );

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
    ZondDBasePrivate* priv = zond_dbase_get_instance_private( self );

    return;
}


static gint
zond_dbase_create_db( sqlite3* db, gchar** errmsg )
{
    gchar* errmsg_ii = NULL;
    gchar* sql = NULL;
    gint rc = 0;

    //Tabellenstruktur erstellen
    sql = //Haupttabelle
            "DROP TABLE IF EXISTS eingang; "
            "DROP TABLE IF EXISTS eingang_rel_path; "
            "DROP TABLE IF EXISTS dateien;"
            "DROP TABLE IF EXISTS ziele;"
            "DROP TABLE IF EXISTS baum_inhalt;"
            "DROP TABLE IF EXISTS baum_auswertung; "
            "DROP TABLE IF EXISTS links; "

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
                "rel_path VARCHAR(200), "
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
                "FOREIGN KEY (parent_id) REFERENCES baum_inhalt (node_id) "
                "ON DELETE CASCADE ON UPDATE CASCADE, "
                "FOREIGN KEY (older_sibling_id) REFERENCES baum_inhalt (node_id) "
                "ON DELETE RESTRICT ON UPDATE CASCADE "
            "); "

            "INSERT INTO baum_inhalt (node_id, parent_id, older_sibling_id, "
            "node_text) VALUES (0, 0, 0, 'v0.9');"

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
            "baum_id INTEGER, "
            "node_id INTEGER, "
            "projekt_target VARCHAR (200), "
            "baum_id_target INTEGER, "
            "node_id_target INTEGER "
            " ); ";

    rc = sqlite3_exec( db, sql, NULL, NULL, &errmsg_ii );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_exec\nsql: ",
                sql, "\nresult code: ", sqlite3_errstr( rc ), "\nerrmsg: ",
                errmsg_ii, NULL );
        sqlite3_free( errmsg_ii );

        return -1;
    }
/*
    sql = sond_database_sql_create_database( );
    rc = sqlite3_exec( db, sql, NULL, NULL, &errmsg_ii );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_exec\nsql: ",
                sql, "\nresult code: ", sqlite3_errstr( rc ), "\nerrmsg: ",
                errmsg_ii, NULL );
        sqlite3_free( errmsg_ii );

        return -1;
    }

    sql = sond_database_sql_create_database( );
    rc = sqlite3_exec( db, sql, NULL, NULL, &errmsg_ii );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_exec\nsql: ",
                sql, "\nresult code: ", sqlite3_errstr( rc ), "\nerrmsg: ",
                errmsg_ii, NULL );
        sqlite3_free( errmsg_ii );

        return -1;
    }

    sql = sond_database_sql_insert_labels( );
    rc = sqlite3_exec( db, sql, NULL, NULL, &errmsg_ii );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_exec\nsql: ",
                sql, "\nresult code: ", sqlite3_errstr( rc ), "\nerrmsg: ",
                errmsg_ii, NULL );
        sqlite3_free( errmsg_ii );

        return -1;
    }

    sql = sond_database_sql_insert_adm_rels( );
    rc = sqlite3_exec( db, sql, NULL, NULL, &errmsg_ii );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_exec\nsql: ",
                sql, "\nresult code: ", sqlite3_errstr( rc ), "\nerrmsg: ",
                errmsg_ii, NULL );
        sqlite3_free( errmsg_ii );

        return -1;
    }
*/
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


//v0.8 Tabellen eingang und eingang_rel_path fehlen ggü. v0.9
static gint
zond_dbase_convert_from_v0_8( sqlite3* db_convert, gchar** errmsg )
{
    gint rc = 0;

    gchar* sql =
            "INSERT INTO baum_inhalt SELECT node_id, parent_id, older_sibling_id, "
                    "icon_name, node_text FROM old.baum_inhalt WHERE node_id != 0; "
            "INSERT INTO baum_auswertung SELECT * FROM old.baum_auswertung WHERE node_id != 0; "
            "INSERT INTO dateien SELECT * FROM old.dateien; "
            "INSERT Into ziele SELECT * FROM old.ziele; ";

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
zond_dbase_convert_to_actual_version( const gchar* path, gchar* v_string,
        gchar** errmsg ) //eingang hinzugefügt
{
    gint rc = 0;
    sqlite3* db = NULL;
    gchar* sql = NULL;
    gchar* path_old = NULL;
    sqlite3_stmt* stmt = NULL;
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

    if ( zond_dbase_create_db( db, errmsg ) )
    {
        sqlite3_close( db);
        g_free( path_new );

        ERROR_SOND( "zond_dbase_create_db" );
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

            ERROR_SOND( "convert_from_v0_7" )
        }
    }
    else if ( !g_strcmp0( v_string , "v0.8" ) )
    {
        rc = zond_dbase_convert_from_v0_8( db, errmsg );
        if ( rc )
        {
            sqlite3_close( db );
            g_free( path_new );
            ERROR_SOND( "convert_from_v0_8" )
        }
    }
    else
    {
        //Mitteilung, daß keine Versionsangabe
        if ( errmsg ) *errmsg = g_strdup( "Keine Version erkannt - ggf. händisch überprüfen" );
        sqlite3_close( db );
        g_free( path_new );

        return -1;
    }

    //updaten#
    sql = "UPDATE baum_inhalt SET node_text = '"ZOND_DBASE_VERSION"' WHERE node_id = 0;";
    rc = sqlite3_prepare_v2( db, sql, -1, &stmt, NULL );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2:\n",
                sqlite3_errstr( rc ), NULL );
        sqlite3_close( db );
        g_free( path_new );

        return -1;
    }

    rc = sqlite3_step( stmt );
    sqlite3_finalize( stmt );
    if ( rc != SQLITE_DONE )
    {
        sqlite3_close( db );
        g_free( path_new );
        if ( errmsg ) *errmsg = add_string( g_strconcat( "Bei Aufruf sqlite3_step:\n",
                sqlite3_errmsg( db ), NULL ), *errmsg );

        return -1;
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

    v_string = g_strdup( (const gchar*) sqlite3_column_text( stmt, 0 ) );

    sqlite3_finalize( stmt );

    return v_string;
}


static gint
zond_dbase_open( const gchar* path, gboolean create_file, gboolean create, sqlite3** db, gchar** errmsg )
{
    gint rc = 0;

    //wenn Datei nicht vorhanden und !create_file: Error
    rc = sqlite3_open_v2( path, db, SQLITE_OPEN_READWRITE | ((create_file == FALSE) ? 0 : SQLITE_OPEN_CREATE), NULL );

    if ( rc != SQLITE_OK ) sqlite3_close( *db );
    else if ( !create_file ) //rc == SQLITE_OK: Datei war vorhanden - soll nicht überschrieben werden
    {
        gchar* v_string = NULL;

        v_string = zond_dbase_get_version( *db, errmsg );
        if ( !v_string )
        {
            sqlite3_close( *db );
            ERROR_SOND( "zond_dbase_get_version" );
        }

        if ( g_strcmp0( v_string, ZOND_DBASE_VERSION ) ) //alte version
        {
            sqlite3_close( *db );

            rc = zond_dbase_convert_to_actual_version( path, v_string, errmsg );
            if ( rc ) ERROR_SOND( "zond_dbase_convert_to_actual_version" )
            else display_message( NULL, "Datei von ", v_string, " zu "ZOND_DBASE_VERSION
                    "konvertiert", NULL );

            rc = sqlite3_open_v2( path, db, SQLITE_OPEN_READWRITE, NULL );
            if ( rc ) ERROR_SOND( "sqlite3_open_v2" )
        }

    }

    if ( rc != SQLITE_OK && !create ) //db gibts noch nicht oder kann nicht geöffnet werden und soll auch nicht erzeugt werden
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_open_v2:\n",
                sqlite3_errstr( rc ), NULL );

        return -1;
    }
    else if ( create && rc == SQLITE_CANTOPEN ) //db gibts noch nicht
    {
        gint rc = 0;

        rc = sqlite3_open( path, db );
        if ( rc != SQLITE_OK )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_open:\n",
                    sqlite3_errstr( rc ), NULL );
            sqlite3_close( *db );

            return -1;
        }

        rc = zond_dbase_create_db( *db, errmsg );
        if ( rc )
        {
            sqlite3_close( *db );
            ERROR_SOND( "zond_dbase_create_db" )
        }
    }
    else if ( create ) //Datei existiert - überschreiben?
    {
        gint rc = 0;

        rc = abfrage_frage( NULL, "Datei existiert bereits", "Überschreiben?",
                NULL );
        if ( rc != GTK_RESPONSE_YES )
        {
            sqlite3_close( *db );
            return 1; //Abbruch gewählt
        }

        rc = zond_dbase_create_db( *db, errmsg );
        if ( rc )
        {
            sqlite3_close( *db );
            ERROR_SOND( "zond_dbase_create_db" )
        }
    }

    rc = sqlite3_exec( *db, "PRAGMA foreign_keys = ON; PRAGMA case_sensitive_like "
            "= ON", NULL, NULL, errmsg );
    if ( rc != SQLITE_OK )
    {
        sqlite3_close( *db );
        ERROR_SOND( "sqlite3_exec (PRAGMA)" )
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
    if ( rc == -1 ) ERROR_SOND( "zond_dbase_open" )
    else if ( rc == 1 ) return 1; //Abbruch gewählt

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
            *errmsg = g_strconcat( "Bei Aufruf sqlite3_prepare_v2 (", sql, "):\n",
                    sqlite3_errstr( rc ), NULL );

            //aufräumen
            for ( gint u = 0; u < i; u++ ) sqlite3_finalize( stmt[u] );

            return -1;
        }
    }

    return 0;
}


gint
zond_dbase_insert_node( ZondDBase* zond_dbase, Baum baum, gint node_id, gboolean child,
        const gchar* icon_name, const gchar* node_text, gchar** errmsg )
{
    gint rc = 0;
    const gint num_stmts = 5;
    gint new_node_id = 0;
    sqlite3_stmt** stmt = NULL;

    if ( !(stmt = g_object_get_data( G_OBJECT(zond_dbase), __func__ )) )
    {
        gint rc = 0;

        stmt = g_malloc0( sizeof( sqlite3_stmt* ) * num_stmts );

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

                "UPDATE baum_inhalt SET "
                    "older_sibling_id=last_insert_rowid() "
                "WHERE "
                    "parent_id=(SELECT parent_id FROM baum_inhalt WHERE node_id=last_insert_rowid()) "
                "AND "
                    "older_sibling_id=(SELECT older_sibling_id FROM baum_inhalt WHERE node_id=last_insert_rowid()) "
                "AND "
                    "node_id!=last_insert_rowid() "
                "AND "
                    "node_id!=0; ",

                "UPDATE baum_auswertung SET "
                    "older_sibling_id=last_insert_rowid() "
                "WHERE "
                    "parent_id=(SELECT parent_id FROM baum_auswertung WHERE node_id=last_insert_rowid()) "
                "AND "
                    "older_sibling_id=(SELECT older_sibling_id FROM baum_auswertung WHERE node_id=last_insert_rowid()) "
                "AND "
                    "node_id!=last_insert_rowid() "
                "AND "
                    "node_id!=0; ",

                "VALUES (last_insert_rowid()); " };

        rc = zond_dbase_prepare_stmts( zond_dbase, num_stmts, sql, stmt, errmsg );
        if ( rc )
        {
            g_free( stmt );
            ERROR_SOND( "zond_dbase_prepare_stmts" )
        }

        g_object_set_data_full( G_OBJECT(zond_dbase), __func__, stmt, g_free );
    }

    for ( gint i = 0; i < num_stmts; i++ ) sqlite3_reset( stmt[i] );

    rc = sqlite3_bind_int( stmt[0 + (gint) baum], 1, child );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (child)" )

    rc = sqlite3_bind_int( stmt[0 + (gint) baum], 2, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_bind_text( stmt[0 + (gint) baum], 3,
            icon_name, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (icon_name)" )

    rc = sqlite3_bind_text( stmt[0 + (gint) baum], 4, node_text,
            -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_text (node_text)" )

    rc = sqlite3_step( stmt[0 + (gint) baum] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step [0/1]" )

    rc = sqlite3_step( stmt[2 + (gint) baum] );
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
    const gint num_stmts = 4;

    if ( !(stmt = g_object_get_data( G_OBJECT(zond_dbase), __func__ )) )
    {
        gint rc = 0;

        stmt = g_malloc0( sizeof( sqlite3_stmt* ) * num_stmts );

        const gchar* sql[] = {
            "UPDATE baum_inhalt SET older_sibling_id=(SELECT older_sibling_id FROM baum_inhalt "
            "WHERE node_id=?1) WHERE "
            "older_sibling_id=?1; ",

            "UPDATE baum_auswertung SET older_sibling_id=(SELECT older_sibling_id FROM baum_auswertung "
            "WHERE node_id=?1) WHERE "
            "older_sibling_id=?1; ",

            "DELETE FROM baum_inhalt WHERE node_id = ?;",

            "DELETE FROM baum_auswertung WHERE node_id = ?; "};

        rc = zond_dbase_prepare_stmts( zond_dbase, num_stmts, sql, stmt, errmsg );
        if ( rc )
        {
            g_free( stmt );
            ERROR_SOND( "zond_dbase_prepare_stmts" )
        }

        g_object_set_data_full( G_OBJECT(zond_dbase), __func__, stmt, g_free );
    }

    for ( gint i = 0; i < num_stmts; i++ ) sqlite3_reset( stmt[i] );

    rc = sqlite3_bind_int( stmt[0 + (gint) baum], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[0 + (gint) baum] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step [0/1]" )

    rc = sqlite3_bind_int( stmt[2 + (gint) baum], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( stmt[2 + (gint) baum] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step [2/3]" )

    return 0;
}


gint
zond_dbase_kopieren_nach_auswertung( ZondDBase* zond_dbase, Baum baum_von, gint node_id_von,
        gint node_id_nach, gboolean child, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;
    const gint num_stmts = 3;

    if ( !(stmt = g_object_get_data( G_OBJECT(zond_dbase), __func__ )) )
    {
        gint rc = 0;

        stmt = g_malloc0( sizeof( sqlite3_stmt* ) * num_stmts );

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

        rc = zond_dbase_prepare_stmts( zond_dbase, num_stmts, sql, stmt, errmsg );
        if ( rc )
        {
            g_free( stmt );
            ERROR_SOND( "zond_dbase_prepare_stmts" )
        }

        g_object_set_data_full( G_OBJECT(zond_dbase), __func__, stmt, g_free );
    }

    for ( gint i = 0; i < num_stmts; i++ ) sqlite3_reset( stmt[i] );

    rc = sqlite3_bind_int( stmt[0 + (gint) baum_von], 1, child );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (child)" )

    rc = sqlite3_bind_int( stmt[0 + (gint) baum_von], 2, node_id_nach );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id_nach)" )

    rc = sqlite3_bind_int( stmt[0 + (gint) baum_von], 3, node_id_von );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int (node_id_von)" )

    rc = sqlite3_step( stmt[0 + (gint) baum_von] );
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
    const gint num_stmts = 6;

    if ( !(stmt = g_object_get_data( G_OBJECT(zond_dbase), __func__ )) )
    {
        gint rc = 0;

        stmt = g_malloc0( sizeof( sqlite3_stmt* ) * num_stmts );

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

        rc = zond_dbase_prepare_stmts( zond_dbase, num_stmts, sql, stmt, errmsg );
        if ( rc )
        {
            g_free( stmt );
            ERROR_SOND( "zond_dbase_prepare_stmts" )
        }

        g_object_set_data_full( G_OBJECT(zond_dbase), __func__, stmt, g_free );
    }

    for ( gint i = 0; i < num_stmts; i++ ) sqlite3_reset( stmt[i] );

    rc = sqlite3_bind_int( stmt[0 + (gint) baum], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int [0, 1]" )

    rc = sqlite3_step( stmt[0 + (gint) baum] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step [0]" )

    rc = sqlite3_bind_int( stmt[2 + (gint) baum], 1, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int [2, 1]" )

    rc = sqlite3_bind_int( stmt[2 + (gint) baum], 2, new_parent_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int [2, 2]" )

    rc = sqlite3_bind_int( stmt[2 + (gint) baum], 3, new_older_sibling_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int [2, 3]" )

    rc = sqlite3_step( stmt[2 + (gint) baum] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step [2]" )

    rc = sqlite3_bind_int( stmt[4 + (gint) baum], 1, new_parent_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int [4, 1]" )

    rc = sqlite3_bind_int( stmt[4 + (gint) baum], 2, new_older_sibling_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int [4, 2]" )

    rc = sqlite3_bind_int( stmt[4 + (gint) baum], 3, node_id );
    if ( rc != SQLITE_OK ) ERROR_ZOND_DBASE( "sqlite3_bind_int [4, 3]" )

    rc = sqlite3_step( stmt[4 + (gint) baum] );
    if ( rc != SQLITE_DONE ) ERROR_ZOND_DBASE( "sqlite3_step [4]" )

    return 0;
}



