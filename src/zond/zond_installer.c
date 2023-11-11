/*
zond (zond_installer.c) - Akten, Beweisstücke, Unterlagen
Copyright (C) 2023  pelo america

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

typedef struct _Installer
{
    GtkWidget* window;
} Installer;


static void
activate_app( GtkApplication* app, gpointer data )
{
    Installer* inst = (Installer*) data;

    gtk_window_present( GTK_WINDOW(inst->window) );

    return;
}


static void
startup_app( GtkApplication* app, gpointer data )
{
    Installer* inst = (Installer*) data;

    inst->window = gtk_application_window_new( app );

    return;
}


int
main( int argc, char **argv)
{
    GtkApplication* app = NULL;
    Installer inst = { 0 };

    //ApplicationApp erzeugen
    app = gtk_application_new ( "de.perlio.zond_updater", G_APPLICATION_DEFAULT_FLAGS );

    //und starten
    g_signal_connect( app, "startup", G_CALLBACK (startup_app), &inst );
    g_signal_connect( app, "activate", G_CALLBACK (activate_app), &inst );

    if ( argc == 2 || !g_strcmp0( argv[1], "-update" ) ) inst.update = TRUE;
    else if ( argc != 1 ) return -1;

    gint status = g_application_run( G_APPLICATION(app), argc, argv );

    g_object_unref(app);

    return status;
}
