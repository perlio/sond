/*
zond (main.c) - Akten, Beweisstücke, Unterlagen
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

#include "init.h"

#include <gtk/gtk.h>


void
open_app( GtkApplication* app, gpointer files, gint n_files, gchar *hint,
        gpointer user_data )
{
    Projekt** zond = (Projekt**) user_data;

    if ( !(*zond) ) return;

    if ( (*zond)->project_name ) return;

    open_file( *zond, files );

    return;
}


void activate_app (GtkApplication* app, gpointer user_data)
{
    return;
}


void startup_app( GtkApplication* app, gpointer user_data )
{
    Projekt** zond = (Projekt**) user_data;

    *zond = init( app );

    return;
}


int main(int argc, char **argv)
{
    GtkApplication* app = NULL;
    Projekt* zond = NULL;

    //ApplicationApp erzeugen
    app = gtk_application_new ( "de.perlio.zond", G_APPLICATION_HANDLES_OPEN );

    //und starten
    g_signal_connect( app, "startup", G_CALLBACK (startup_app), &zond );
    g_signal_connect( app, "activate", G_CALLBACK (activate_app), &zond );
    g_signal_connect( app, "open", G_CALLBACK (open_app), &zond );

    gint status = g_application_run( G_APPLICATION(app), argc, argv );

    g_object_unref(app);

    return status;
}
