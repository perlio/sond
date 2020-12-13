/*
zond (fs_tree.c) - Akten, Beweisstücke, Unterlagen
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

#include "../enums.h"
#include "../global_types.h"
#include "../error.h"

#include "../99conv/db_read.h"
#include "../99conv/db_write.h"
#include "../99conv/baum.h"
#include "../99conv/general.h"
#include "../../misc.h"
#include "../../fm.h"

#include "../20allgemein/oeffnen.h"

#include <sqlite3.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gstdio.h>

/*
gint
fs_tree_dir_foreach( Projekt* zond, GFile* dir, GtkTreeIter* iter,
        gint (*foreach) ( Projekt*, GFile*, GtkTreeIter* iter, gpointer, gchar** ),
        gchar** errmsg )
{
    GError* error = NULL;

    GFileEnumerator* enumer = g_file_enumerate_children( dir, "*", G_FILE_QUERY_INFO_NONE, NULL, &error );
    if ( !enumer )
    {
        if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerate_children:\n",
                error->message, NULL );
        g_error_free( error );

        return -1;
    }

    while ( 1 )
    {
        GFile* child = NULL;
        GFileInfo* info_child = NULL;

        if ( !g_file_enumerator_iterate( enumer, &info_child, &child, NULL, &error ) )
        {
            if ( errmsg ) *errmsg = g_strconcat( "Bei Aufruf g_file_enumerator_iterate:\n",
                    error->message, NULL );
            g_error_free( error );
            g_object_unref( enumer );

            return -1;
        }

        if ( child ) //es gibt noch Datei in Verzeichnis
        {
            gint rc = 0;

            rc = foreach( zond, child, iter, data, errmsg );
            if ( rc == -1 )
            {
                g_object_unref( enumer );
                ERROR_PAO( "foreach" )
            }
            else if ( rc == 1 ) break;//Abbruch gewählt
        } //ende if ( child )
        else break;
    }

    g_object_unref( enumer );

    return 0;
}
*/

