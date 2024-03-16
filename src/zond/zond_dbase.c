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

#include "20allgemein/ziele.c"

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
zond_dbase_create_db_maj_1( sqlite3* db, gchar** errmsg )
{
    gchar* errmsg_ii = NULL;
    gchar* sql = NULL;
    gint rc = 0;

/*
type = BAUM_ROOT
ID = 1 (Inhalt) oder 2 (Auswertung)
parent_ID = 0 und older_sibling_ID = 0
Rest = NULL

type = BAUM_STRUKT (inhalt und auswertung)
parent_ID und older_sibling_ID
icon_name
node_text
text

type = BAUM_INHALT_FILE
parent_ID und older_sibling_ID
rel_path
icon_name
node_text
text

type = BAUM_INHALT_FILE_PART
parent_ID und older_sibling_ID
index_von = Part-Nr.
Rest = NULL

type = BAUM_INHALT_PDF_ABSCHNITT
link = PDF_ABSCHNITT
parent_ID und older_sibling_ID

type = BAUM_INHALT_VIRT_PDF
link = BAUM_INHALT_VIRT_PDF_SECTION
parent_ID und older_sibling_ID
rel_path = UUID
icon_name
node_text
text

type = BAUM_INHALT_VIRT_PDF_SECTION
link = next oder 0
parent_ID und older_sibling_ID = 0
rel_path
seite_von, index_von, seite_bis, index_bis nach Abschnitt

type = PDF_ABSCHNITT
parent_ID und older_sibling_ID, angebunden an 0,0
rel_path
ziel_id_von, index_von, ziel_id_bis, index_bis nach Abschnitt
icon_name
node_text
text

type = PDF_PUNKT
parent_ID und older_sibling_ID je nach parent/older_sibling
ziel_id_von, index_von nach Abschnitt
icon_name
node_text
text

type = BAUM_AUSWERTUNG_COPY
parent_ID und older_sibling_ID
link = ID von BAUM_INHALT_FILE, _FILE_PART, _PDF, _PDF_ABSCHNITT, _VIRT_PDF oder PDF_ABSCHNITT oder PDF_PUNKT
rel_path
icon_name
node_text
text

type = BAUM_AUSWERTUNG_LINK
parent_ID und older_sibling_ID
link = ID von STRUKT, BAUM_INHALT_FILE, BAUM_INHALT_FILE_PART oder BAUM_AUSWERTUNG_COPY
Rest = 0

*/
    //Tabellenstruktur erstellen
    sql = //Haupttabelle
            "DROP TABLE IF EXISTS knoten; "

            "CREATE TABLE knoten ("
            "ID INTEGER PRIMARY KEY, "
            "parent_ID INTEGER NOT NULL, "
            "older_sibling_ID INTEGER NOT NULL, "
            "type INTEGER, "
            "link INTEGER, "
            "rel_path VARCHAR(250), "
            "seite_von INTEGER, "
            "index_von INTEGER, "
            "seite_bis INTEGER, "
            "index_bis INTEGER, "
            "icon_name VARCHAR(50), "
            "node_text VARCHAR(50), "
            "text VARCHAR, "
            "FOREIGN KEY (parent_ID) REFERENCES knoten (ID) "
            "ON DELETE CASCADE ON UPDATE CASCADE, "
            "FOREIGN KEY (older_sibling_ID) REFERENCES knoten (ID) "
            "ON DELETE CASCADE ON UPDATE CASCADE, "
            "FOREIGN KEY (link) REFERENCES knoten (ID) "
            "ON DELETE CASCADE ON UPDATE CASCADE "
            "); "

            "INSERT INTO knoten (ID, parent_id, older_sibling_id, "
            "node_text) VALUES (0, 0, 0, '" MAJOR "');"
            "INSERT INTO knoten (ID, parent_id, older_sibling_id, type) "
            "VALUES (1, 0, 0, 0);" //root baum_inhalt
            "INSERT INTO knoten (ID, parent_id, older_sibling_id, type) "
            "VALUES (2, 0, 0, 0);" //root baum_auswertung


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
zond_dbase_convert_from_legacy_to_maj_0( const gchar* path, gchar* v_string,
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
zond_dbase_convert_from_maj_0_to_1( const gchar* path, gchar** errmsg )
{

    return 0;
}


static gint
zond_dbase_create_db( sqlite3* db, gchar** errmsg )
{
    gint rc = 0;

    rc = zond_dbase_create_db_maj_1( db, errmsg );
    if ( rc ) ERROR_S

    return 0;
}


static gchar*
zond_dbase_get_version( sqlite3* db, gchar** errmsg )
{
    gint rc = 0;
    sqlite3_stmt* stmt = NULL;
    gchar* v_string = NULL;

    rc = sqlite3_prepare_v2( db, "SELECT node_text FROM knoten WHERE ID = 0;", -1, &stmt, NULL );
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

            rc = zond_dbase_convert_from_legacy_to_maj_0( path, v_string, errmsg );
            g_free( v_string );
            if ( rc ) ERROR_S

            rc = sqlite3_open_v2( path, db, SQLITE_OPEN_READWRITE, NULL );
            if ( rc ) ERROR_S

            v_string = g_strdup( "0" );
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

        if ( atoi( v_string ) == 0 )
        {
            gint rc = 0;

            g_free( v_string );

            //aktewalisieren von maj_0 auf maj_1
            rc = zond_dbase_convert_from_maj_0_to_1( path, errmsg );
            if ( rc )
            {
                sqlite3_close( *db );
                ERROR_S
            }

            v_string = g_strdup( "1" );
        }
        //später: if ( atoi( v_string ) == 1 ) ...
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
        if ( errmsg ) *errmsg = g_strconcat( __func__, "\nsqlite3_backup_init\n",
                sqlite3_errmsg( db_dst ), NULL);

        return -1;
    }
    rc = sqlite3_backup_step( backup, -1 );
    sqlite3_backup_finish( backup );
    if ( rc != SQLITE_DONE )
    {
        if ( errmsg && rc == SQLITE_NOTADB ) *errmsg = g_strdup( "Datei ist "
                "keine SQLITE-Datenbank" );
        else if ( errmsg ) *errmsg = g_strconcat( __func__,
                "\nsqlite3_backup_step:\n", sqlite3_errmsg( db_dst ), NULL );

        return -1;
    }

    return 0;
}


static gint
zond_dbase_prepare_stmts( ZondDBase* zond_dbase, gint num, const gchar** sql,
        sqlite3_stmt** stmt, GError** error )
{
    for ( gint i = 0; i < num; i++ )
    {
        gint rc = 0;

        ZondDBasePrivate* priv = zond_dbase_get_instance_private( zond_dbase );

        rc = sqlite3_prepare_v2( priv->dbase, sql[i], -1, &stmt[i], NULL );
        if ( rc != SQLITE_OK )
        {
            //aufräumen
            for ( gint u = 0; u < i; u++ ) sqlite3_finalize( stmt[u] );

            if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                    sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                    "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

            return -1;
        }
    }

    return 0;
}


gint
zond_dbase_prepare( ZondDBase* zond_dbase, const gchar* func, const gchar** sql,
        gint num_stmts, sqlite3_stmt*** stmt, GError** error )
{
    if ( !(*stmt = g_object_get_data( G_OBJECT(zond_dbase), func )) )
    {
        gint rc = 0;

        *stmt = g_malloc0( sizeof( sqlite3_stmt* ) * num_stmts );

        rc = zond_dbase_prepare_stmts( zond_dbase, num_stmts, sql, *stmt, error );
        if ( rc )
        {
            g_free( *stmt );
            g_prefix_error( error, "%s\n", __func__ );

            return -1;
        }

        g_object_set_data_full( G_OBJECT(zond_dbase), func, *stmt, g_free );
    }
    else for ( gint i = 0; i < num_stmts; i++ )
            sqlite3_reset( (*stmt)[i] );

    return 0;
}


gint
zond_dbase_begin( ZondDBase* zond_dbase, GError** error )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "BEGIN; "
        };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    return 0;
}


gint
zond_dbase_commit( ZondDBase* zond_dbase, GError** error )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "COMMIT; "
        };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    return 0;
}


gint
zond_dbase_rollback( ZondDBase* zond_dbase, GError** error )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "ROLLBACK; "
        };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    return 0;
}


gint
zond_dbase_test_path( ZondDBase* zond_dbase, const gchar* rel_path, GError** error )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "SELECT ID FROM knoten WHERE rel_path=?1 AND (type=2 OR type=4);"
    };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, error );
    if ( rc ) ERROR_Z

    rc = sqlite3_bind_text( stmt[0], 1, rel_path, -1, NULL );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_step( stmt[0] );
    if ( (rc != SQLITE_ROW) && (rc != SQLITE_DONE) )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    if ( rc == SQLITE_ROW ) return 1;

    return 0;
}


gint
zond_dbase_insert_node( ZondDBase* zond_dbase, gint anchor_ID, gboolean child,
        gint type, gint link, const gchar* rel_path, gint seite_von, gint index_von,
        gint seite_bis, gint index_bis, const gchar* icon_name, const gchar* node_text,
        const gchar* text, GError** error )
{
    gint rc = 0;
    gint new_node_id = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "INSERT INTO knoten "
            "(parent_id, older_sibling_id, type, link, rel_path, seite_von, index_von, "
            "seite_bis, index_bis, icon_name, node_text, text ) "
            "VALUES ("
                "CASE ?1 " //child
                    "WHEN 0 THEN (SELECT parent_ID FROM knoten WHERE ID=?2) "
                    "WHEN 1 THEN ?2 " //anchor_id
                "END, "
                "CASE ?1 " //child
                    "WHEN 0 THEN ?2 "
                    "WHEN 1 THEN 0 "
                "END, "
                "?3, " //type
                "?4, " //link
                "?5, " //rel_path
                "?6, " //seite_von
                "?7, " //index_von
                "?8, " //seite_bis
                "?9, " //index_bis
                "?10, " //icon_name
                "?11, " //node_text
                "?12 " //text

                "); ",

            "UPDATE knoten SET older_sibling_ID=last_insert_rowid() "
                "WHERE "
                    "parent_ID=(SELECT parent_ID FROM knoten WHERE ID=last_insert_rowid()) "
                "AND "
                    "older_sibling_ID=(SELECT older_sibling_ID FROM knoten WHERE ID=last_insert_rowid()) "
                "AND "
                    "ID!=last_insert_rowid() "
                "AND "
                    "ID!=0; ",

            "VALUES (last_insert_rowid()); " };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    //Damit alles auf NULL gestellt wird
    sqlite3_clear_bindings( stmt[0] );

    rc = sqlite3_bind_int( stmt[0], 1, child );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_bind_int( stmt[0], 2, anchor_ID );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_bind_int( stmt[0], 3, type );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    if ( link )
    {
        rc = sqlite3_bind_int( stmt[0], 4, link );
        if ( rc != SQLITE_OK )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                    sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                    "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

            return -1;
        }
    }

    rc = sqlite3_bind_text( stmt[0], 5,
            rel_path, -1, NULL );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    if ( type == ZOND_DBASE_TYPE_PDF_ABSCHNITT ||
            type == ZOND_DBASE_TYPE_PDF_PUNKT ||
            type == ZOND_DBASE_TYPE_BAUM_INHALT_VIRT_PDF_SECTION ||
            type == ZOND_DBASE_TYPE_BAUM_INHALT_FILE_PART )
    {
        rc = sqlite3_bind_int( stmt[0], 6, seite_von );
        if ( rc != SQLITE_OK )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                    sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                    "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

            return -1;
        }

        rc = sqlite3_bind_int( stmt[0], 7, index_von );
        if ( rc != SQLITE_OK )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                    sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                    "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

            return -1;
        }
    }

    if ( type == ZOND_DBASE_TYPE_PDF_ABSCHNITT )
    {
        rc = sqlite3_bind_int( stmt[0], 8, seite_bis );
        if ( rc != SQLITE_OK )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                    sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                    "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

            return -1;
        }

        rc = sqlite3_bind_int( stmt[0], 9, index_bis );
        if ( rc != SQLITE_OK )
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                    sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                    "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

            return -1;
        }
    }

    rc = sqlite3_bind_text( stmt[0], 10, icon_name,
            -1, NULL );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_bind_text( stmt[0], 11, node_text,
            -1, NULL );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_bind_text( stmt[0], 12, text,
            -1, NULL );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_step( stmt[1] );
    if ( rc != SQLITE_DONE )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_step( stmt[2] );
    if ( rc != SQLITE_ROW )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    new_node_id = sqlite3_column_int( stmt[2], 0 );

    return new_node_id;
}


gint
zond_dbase_insert_pdf_root( ZondDBase* zond_dbase,
        const gchar* rel_path, gint* pdf_root, GError** error )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "INSERT INTO knoten "
            "(parent_id, older_sibling_id, type, rel_path) "
            "VALUES (0, 0, 9, ?1); ",

            "VALUES (last_insert_rowid()); "
            };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    rc = sqlite3_bind_text( stmt[0], 1, rel_path, -1, NULL );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_step( stmt[1] );
    if ( rc == SQLITE_DONE )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "ZOND_DBASE" ),
                0, "%s\nKnoten konnte nicht eingefügt werden", __func__ );

        return -1;
    }
    else if ( rc != SQLITE_ROW )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    if ( pdf_root ) *pdf_root = sqlite3_column_int( stmt[1], 0 );

    return 0;
}


gint
zond_dbase_update_icon_name( ZondDBase* zond_dbase, gint node_id,
        const gchar* icon_name, GError** error )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "UPDATE knoten "
            "SET icon_name=?2 WHERE ID=?1; "
            };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    rc = sqlite3_bind_int( stmt[0], 1, node_id );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_bind_text( stmt[0], 2,
            icon_name, -1, NULL );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    return 0;
}


gint
zond_dbase_update_node_text( ZondDBase* zond_dbase, gint node_id,
        const gchar* node_text, GError** error )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "UPDATE knoten "
            "SET node_text=?2 WHERE ID=?1; "
            };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    rc = sqlite3_bind_int( stmt[0], 1, node_id );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_bind_text( stmt[0], 2,
            node_text, -1, NULL );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    return 0;
}


gint
zond_dbase_update_text( ZondDBase* zond_dbase, gint node_id,
        const gchar* text, GError** error )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "UPDATE knoten "
            "SET text=?2 WHERE ID=?1; "
            };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    rc = sqlite3_bind_int( stmt[0], 1, node_id );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_bind_text( stmt[0], 2,
            text, -1, NULL );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    return 0;
}


gint
zond_dbase_update_path( ZondDBase* zond_dbase, const gchar* old_path,
        const gchar* new_path, GError** error )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "UPDATE knoten SET rel_path = "
            "REPLACE( SUBSTR( rel_path, 1, LENGTH( ?1 ) ), ?1, ?2 ) || "
            "SUBSTR( rel_path, LENGTH( ?1 ) + 1 );"
    };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, error );
    if ( rc ) ERROR_Z

    rc = sqlite3_bind_text( stmt[0], 1, old_path, -1, NULL );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_bind_text( stmt[0], 2, new_path, -1, NULL );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    return 0;
}


gint
zond_dbase_verschieben_knoten( ZondDBase* zond_dbase, gint node_id, gint anchor_id,
        gboolean child, GError** error )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "UPDATE knoten SET older_sibling_id="
            "(SELECT older_sibling_ID FROM knoten WHERE ID=?1)" //node_id
            "WHERE older_sibling_ID=?1; ",

            //zunächst Knoten, vor den eingefügt wird, ändern
            "UPDATE knoten SET "
                "older_sibling_ID=?1 WHERE "
                    "parent_ID= "
                        "CASE ?3 "
                            "WHEN 0 THEN (SELECT parent_ID FROM knoten WHERE ID=?2) " //anchor
                            "WHEN 1 THEN ?2 "
                        "END "
                    "AND "
                    "older_sibling_ID= "
                        "CASE ?3 "
                            "WHEN 0 THEN ?2 "
                            "WHEN 1 THEN ?1 "
                        "END; ",

            //zu verschiebenden Knoten einfügen
            "UPDATE knoten SET "
                "parent_ID= "
                    "CASE ?3 " //child
                    "WHEN 0 THEN (SELECT parent_ID FROM knoten WHERE ID=?2) " //anchor_id
                    "WHEN 1 THEN ?2 "
                    "END, "
                "older_sibling_ID="
                    "CASE ?3 "
                    "WHEN 0 THEN ?2 "
                    "WHEN 1 THEN 0 "
                    "END "
                "WHERE ID=?1; "
            };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, error );
    if ( rc )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_bind_int( stmt[0], 1, node_id );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_bind_int( stmt[1], 1, node_id );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_bind_int( stmt[1], 2, anchor_id );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_bind_int( stmt[1], 3, child );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_step( stmt[1] );
    if ( rc != SQLITE_DONE )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_bind_int( stmt[2], 1, node_id );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_bind_int( stmt[2], 2, anchor_id );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_bind_int( stmt[2], 3, child );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_step( stmt[2] );
    if ( rc != SQLITE_DONE )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    return 0;
}


gint
zond_dbase_remove_node( ZondDBase* zond_dbase, gint node_id, GError** error )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "UPDATE knoten SET older_sibling_ID=(SELECT older_sibling_ID FROM knoten "
            "WHERE ID=?1) WHERE "
            "older_sibling_ID=?1; ",

            "DELETE FROM knoten WHERE ID=?1; "
        };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    rc = sqlite3_bind_int( stmt[0], 1, node_id );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_DONE )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_bind_int( stmt[1], 1, node_id );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_step( stmt[1] );
    if ( rc != SQLITE_DONE )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    return 0;
}


gint
zond_dbase_get_node( ZondDBase* zond_dbase, gint node_id, gint* type, gint* link,
        gchar** rel_path, gint* seite_von, gint* index_von, gint* seite_bis, gint* index_bis,
        gchar** icon_name, gchar** node_text, gchar** text, GError** error )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "SELECT type, link, rel_path, seite_von, index_von, seite_bis, index_bis, "
            "icon_name, node_text, text "
            "FROM knoten WHERE ID=?1;"
            };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    rc = sqlite3_bind_int( stmt[0], 1, node_id );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_step( stmt[0] );
    if ( rc == SQLITE_DONE )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "ZOND_DBASE" ),
                0, "%s\n%s", __func__, "node_id nicht gefunden" );

        return -1;
    }
    else if ( rc != SQLITE_ROW ) //richtiger Fähler
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    if ( type ) *type = sqlite3_column_int( stmt[0], 0 );
    if ( link ) *link= sqlite3_column_int( stmt[0], 1 );
    if ( rel_path ) *rel_path = g_strdup( (const gchar*) sqlite3_column_text( stmt[0], 2 ) );
    if ( seite_von ) *seite_von = sqlite3_column_int( stmt[0], 3 );
    if ( index_von ) *index_von = sqlite3_column_int( stmt[0], 4 );
    if ( seite_bis ) *seite_bis = sqlite3_column_int( stmt[0], 5 );
    if ( index_bis ) *index_bis = sqlite3_column_int( stmt[0], 6 );
    if ( icon_name ) *icon_name = g_strdup( (const gchar*) sqlite3_column_text( stmt[0], 7 ) );
    if ( node_text ) *node_text = g_strdup( (const gchar*) sqlite3_column_text( stmt[0], 8 ) );
    if ( text ) *text = g_strdup( (const gchar*) sqlite3_column_text( stmt[0], 9 ) );

    return 0;
}


gint
zond_dbase_get_type_and_link( ZondDBase* zond_dbase, gint node_id, gint* type,
        gint* link, GError** error )
{
    gint rc = 0;

    rc = zond_dbase_get_node( zond_dbase, node_id, type, link, NULL, NULL,
            NULL, NULL, NULL, NULL, NULL, NULL, error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );
        return -1;
    }

    return 0;
}


gint
zond_dbase_get_rel_path( ZondDBase* zond_dbase, gint node_id,
        gchar** rel_path, GError** error )
{
    gint rc = 0;

    rc = zond_dbase_get_node( zond_dbase, node_id, NULL, NULL, rel_path, NULL,
            NULL, NULL, NULL, NULL, NULL, NULL, error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );
        return -1;
    }

    return 0;
}


gint
zond_dbase_get_text( ZondDBase* zond_dbase, gint node_id,
        gchar** text, GError** error )
{
    gint rc = 0;

    rc = zond_dbase_get_node( zond_dbase, node_id, NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL, NULL, text, error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );
        return -1;
    }

    return 0;
}


gint
zond_dbase_get_pdf_root( ZondDBase* zond_dbase, const gchar* rel_path, gint* pdf_root, GError** error )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "SELECT (ID) "
            "FROM knoten WHERE parent_ID=0 AND older_sibling_id=0 AND rel_path=?1;"
            };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    rc = sqlite3_bind_text( stmt[0], 1, rel_path, -1, NULL );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_ROW && rc != SQLITE_DONE ) //richtiger Fähler
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    if ( pdf_root ) *pdf_root = sqlite3_column_int( stmt[0], 0 );

    return 0;
}


gint
zond_dbase_get_tree_root( ZondDBase* zond_dbase, gint node_id, gint* root, GError** error )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "WITH RECURSIVE cte_knoten (ID, parent_ID, older_sibling_ID) AS ( "
                "VALUES (?1,(SELECT parent_ID FROM knoten WHERE ID=?1),(SELECT older_sibling_ID FROM knoten WHERE ID=?1)) "
                "UNION ALL "
                "SELECT knoten.ID, knoten.parent_id, knoten.older_sibling_ID FROM knoten JOIN cte_knoten "
                    "WHERE knoten.ID = cte_knoten.parent_ID "
                ") SELECT ID AS ID_CTE FROM cte_knoten "
                "WHERE cte_knoten.parent_ID=0 AND cte_knoten.older_sibling_ID=0; "
            };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    rc = sqlite3_bind_int( stmt[0], 1, node_id );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_step( stmt[0] );
    if ( rc == SQLITE_DONE )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "ZOND_DBASE" ),
                0, "%s\n%s", __func__, "node_id nicht gefunden" );

        return -1;
    }
    else if ( rc != SQLITE_ROW ) //richtiger Fähler
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    if ( root ) *root = sqlite3_column_int( stmt[0], 0 );

    return 0;
}


gint
zond_dbase_get_parent( ZondDBase* zond_dbase, gint node_id, gint* parent_id, GError** error )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "SELECT (parent_ID) "
            "FROM knoten WHERE ID=?1;"
            };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    rc = sqlite3_bind_int( stmt[0], 1, node_id );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_step( stmt[0] );
    if ( rc == SQLITE_DONE )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "ZOND_DBASE" ),
                0, "%s\n%s", __func__, "node_id nicht gefunden" );

        return -1;
    }
    else if ( rc != SQLITE_ROW ) //richtiger Fähler
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    if ( parent_id ) *parent_id = sqlite3_column_int( stmt[0], 0 );

    return 0;
}


gint
zond_dbase_get_first_child( ZondDBase* zond_dbase, gint node_id, gint* first_child, GError** error )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
        //...
            "SELECT knoten1.ID, knoten2.ID FROM knoten AS knoten1 "
                "LEFT JOIN knoten AS knoten2 "
                "ON knoten1.ID = knoten2.parent_ID AND knoten2.older_sibling_ID = 0 "
                    "AND knoten2.ID!= 0 "
                "WHERE knoten1.ID=?1;"
        };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, error );
    if ( rc )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_bind_int( stmt[0], 1, node_id );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_step( stmt[0] );
    if ( rc == SQLITE_DONE )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "ZOND_DBASE" ),
                0, "%s\nKnoten mit dieser ID existiert nicht", __func__ );

        return -1;
    }
    else if ( rc != SQLITE_ROW )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    if ( first_child ) *first_child =
            sqlite3_column_int( stmt[0], 1 );

    return 0;
}


gint
zond_dbase_get_older_sibling( ZondDBase* zond_dbase, gint node_id, gint* older_sibling_id, GError** error )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "SELECT (older_sibling_ID) "
            "FROM knoten WHERE ID=?1;"
            };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    rc = sqlite3_bind_int( stmt[0], 1, node_id );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_step( stmt[0] );
    if ( rc == SQLITE_DONE )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "ZOND_DBASE" ),
                0, "%s\n%s", __func__, "node_id nicht gefunden" );

        return -1;
    }
    else if ( rc != SQLITE_ROW ) //richtiger Fähler
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    if ( older_sibling_id ) *older_sibling_id = sqlite3_column_int( stmt[0], 0 );

    return 0;
}


gint
zond_dbase_get_younger_sibling( ZondDBase* zond_dbase, gint node_id, gint* younger_sibling_id, GError** error )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "SELECT knoten2.ID FROM knoten AS knoten1 "
                "LEFT JOIN knoten AS knoten2 "
                "ON knoten1.ID = knoten2.older_sibling_ID "
                "WHERE knoten1.ID = ?1;"
            };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    rc = sqlite3_bind_int( stmt[0], 1, node_id );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_ROW && rc != SQLITE_DONE ) //richtiger Fähler
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    if ( rc == SQLITE_ROW && younger_sibling_id ) *younger_sibling_id = sqlite3_column_int( stmt[0], 0 );

    return 0;
}


gint
zond_dbase_get_baum_inhalt_pdf_abschnitt_from_pdf_abschnitt( ZondDBase* zond_dbase,
                    gint pdf_abschnitt, gint* baum_inhalt_pdf_abschnitt, GError** error )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "SELECT ID FROM knoten WHERE type=4 AND link=?1;"
            };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    rc = sqlite3_bind_int( stmt[0], 1, pdf_abschnitt );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_ROW && rc != SQLITE_DONE ) //richtiger Fähler
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    if ( rc == SQLITE_ROW &&baum_inhalt_pdf_abschnitt )
            *baum_inhalt_pdf_abschnitt = sqlite3_column_int( stmt[0], 0 );

    return 0;
}


gint
zond_dbase_get_baum_inhalt_file_from_rel_path( ZondDBase* zond_dbase,
        const gchar* rel_path, gint* baum_inhalt_file, GError** error )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "SELECT ID FROM knoten WHERE type=2 AND rel_path=?1;"
            };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    rc = sqlite3_bind_text( stmt[0], 1, rel_path, -1, NULL );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    rc = sqlite3_step( stmt[0] );
    if ( rc != SQLITE_ROW && rc != SQLITE_DONE ) //richtiger Fähler
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    if ( rc == SQLITE_ROW && baum_inhalt_file )
            *baum_inhalt_file = sqlite3_column_int( stmt[0], 0 );

    return 0;
}


gint
zond_dbase_get_baum_inhalt_pdf_abschnitt( ZondDBase* zond_dbase,
        gchar const* rel_path, Anbindung anbindung, gint* id_baum_inhalt_pdf_abschnitt, GError** error )
{
    gint rc = 0;
    sqlite3_stmt** stmt = NULL;

    const gchar* sql[] = {
            "SELECT ID, seite_von, index_von, seite_bis, index_bis "
            "FROM knoten WHERE type=4 AND rel_path=?1;"
            };

    rc = zond_dbase_prepare( zond_dbase, __func__, sql, nelem( sql ), &stmt, error );
    if ( rc )
    {
        g_prefix_error( error, "%s\n", __func__ );

        return -1;
    }

    rc = sqlite3_bind_text( stmt[0], 1, rel_path, -1, NULL );
    if ( rc != SQLITE_OK )
    {
        if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

        return -1;
    }

    do
    {
        gint rc = 0;
        Anbindung anbindung_baum_inhalt_pdf_abschnitt = { 0 };

        rc = sqlite3_step( stmt[0] );
        if ( rc != SQLITE_ROW && rc != SQLITE_DONE ) //richtiger Fähler
        {
            if ( error ) *error = g_error_new( g_quark_from_static_string( "SQLITE3" ),
                    sqlite3_errcode( zond_dbase_get_dbase( zond_dbase ) ),
                    "%s\n%s", __func__, sqlite3_errmsg( zond_dbase_get_dbase( zond_dbase ) ) );

            return -1;
        }

        if ( rc == SQLITE_DONE ) break;

        anbindung_baum_inhalt_pdf_abschnitt.von.seite = sqlite3_column_int( stmt[0], 1 );
        anbindung_baum_inhalt_pdf_abschnitt.von.index = sqlite3_column_int( stmt[0], 2 );
        anbindung_baum_inhalt_pdf_abschnitt.bis.seite = sqlite3_column_int( stmt[0], 3 );
        anbindung_baum_inhalt_pdf_abschnitt.bis.index = sqlite3_column_int( stmt[0], 4 );

        if ( ziele_1_eltern_von_2( anbindung_baum_inhalt_pdf_abschnitt, anbindung ) )
        {
            if ( id_baum_inhalt_pdf_abschnitt )
                    *id_baum_inhalt_pdf_abschnitt = sqlite3_column_int( stmt[0], 0 );

            return 0;
        }
    }
    while ( 1 );

    return 0;
}
