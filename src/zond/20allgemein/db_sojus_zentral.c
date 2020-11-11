/*
zond (db_sojus_zentral.c) - Akten, Beweisstücke, Unterlagen
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


#include "../global_types.h"
#include "../error.h"

#include <sqlite3.h>
#include <gtk/gtk.h>


/** Rückgabewert:
    Fehler: -1
    guuid nicht gefunden: 1
    ok: 0
**/
gint
db_sz_get_path_for_guuid( Projekt* zond, const gchar* guuid, gchar** path, gchar** errmsg )
{
    gint rc = 0;
    gchar* text = NULL;

    sqlite3_reset( zond->stmts_db_sz.db_sz_get_path[0] );

    rc = sqlite3_bind_text( zond->stmts_db_sz.db_sz_get_path[0], 1, guuid, -1, NULL );
    if ( rc != SQLITE_OK ) ERROR_SQL( "sqlite3_bind_int (node_id)" )

    rc = sqlite3_step( zond->stmts_db_sz.db_sz_get_path[0] );
    if ( rc != SQLITE_DONE && rc != SQLITE_ROW ) ERROR_SQL( "sqlite3_step" )
    else if ( rc == SQLITE_DONE ) return 1;

    text = (gchar*) sqlite3_column_text( zond->stmts_db_sz.db_sz_get_path[0], 1 );

    if ( !text || !g_strcmp0( text, "" ) )
    {
        if ( errmsg ) *errmsg = g_strdup( "Kein path zur (vorhandenen) guuid eingetragen -\n"
                "Datenbank möglicherweise korrupt" );

        return -1;
    }

    *path = g_strdup( (const gchar*) text );

    return 0;
}




