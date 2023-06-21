/*
sojus (main.c) - softkanzlei
Copyright (C) 2021  pelo america

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


#include <gtk/gtk.h>
#include "global_types_sojus.h"
#include "sojus_init.h"



static void
activate_app( GtkApplication* app, gpointer data )
{
    Sojus* sojus = *((Sojus**) data);

    gtk_window_present( GTK_WINDOW(sojus->app_window) );

    return;
}


static void
startup_app( GtkApplication* app, gpointer data )
{
    Sojus** sojus = (Sojus**) data;

    *sojus = sojus_init( app );
    if ( !(*sojus) ) return;

    return;
}


gint
main( int argc, char **argv)
{
    GtkApplication* app = NULL;
    Sojus* sojus = NULL;
    gint status = 0;

    //ApplicationApp erzeugen
    app = gtk_application_new ( "de.rubarth-krieger.sojus", G_APPLICATION_DEFAULT_FLAGS );

    //und starten
    g_signal_connect( app, "startup", G_CALLBACK(startup_app), &sojus );
    g_signal_connect( app, "activate", G_CALLBACK (activate_app), &sojus );

    status = g_application_run( G_APPLICATION (app), argc, argv );

    g_object_unref( app );

    return status;
}
