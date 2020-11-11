/*
zond (convert.c) - Akten, Beweisstücke, Unterlagen
Copyright (C) 2020  pelo america

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

#include "../error.h"
#include "../global_types.h"

#include "../99conv/general.h"

#include "project.h"

#include <gtk/gtk.h>
#include <sqlite3.h>


static gint
convert_copy( Projekt* zond, sqlite3* db_convert, gchar** errmsg )
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


static sqlite3*
convert_datei_oeffnen( Projekt* zond, gchar** errmsg )
{
    gint rc = 0;
    sqlite3* db_convert = NULL;

    if ( zond->project_name != NULL )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Projekt muß geschlossen sein", NULL );

        return NULL;
    }

    gchar* abs_path = filename_oeffnen( GTK_WINDOW(zond->app_window) );
    if ( !abs_path )
    {
        if ( errmsg ) *errmsg = g_strdup( "Keine Datei zum konvertieren ausgewählt" );

        return NULL;
    }

    //ToDo: abfragen, ob v1-Datei schon existiert und ggf. überschrieben werden soll

    //Ursprungsdatei
    gchar* origin = g_strconcat( abs_path, "v1", NULL );
    rc = sqlite3_open( origin, &(db_convert) );
    g_free( origin );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf "
                "sqlite3_open (origin):\n", sqlite3_errmsg( db_convert ), NULL );
        g_free( abs_path );

        return NULL;
    }

    if ( !db_create( db_convert, errmsg ) )
    {
        sqlite3_close( db_convert );
        g_free( abs_path );

        ERROR_PAO_R( "db_create", NULL );
    }

    gchar* sql =  g_strdup_printf(
            "ATTACH DATABASE `%s` AS old;", abs_path );
    g_free( abs_path );

    rc = sqlite3_exec( db_convert, sql, NULL, NULL, errmsg );
    if ( rc != SQLITE_OK )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf sqlite3_exec:\n"
                "result code: ", sqlite3_errstr( rc ), "\nerrmsg: ",
                errmsg, NULL );
        sqlite3_free( errmsg );
        sqlite3_close( db_convert );

        return NULL;
    }

    return db_convert;
}


gint
convert_09_to_1( Projekt* zond, gchar** errmsg )
{
    gint rc = 0;

    sqlite3* db_convert = convert_datei_oeffnen( zond, errmsg );
    if ( !db_convert ) ERROR_PAO( "convert_datei_oeffnen" )

    rc = convert_copy( zond, db_convert, errmsg );
    if ( rc ) ERROR_PAO( "convert_copy" )

    sqlite3_close( db_convert );

    return 0;

}


