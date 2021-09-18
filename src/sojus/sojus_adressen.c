/*
sojus (sojus_adressen.c) - softkanzlei
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

#include "sojus_init.h"


void
sojus_adressen_cb_fenster( GtkButton* button, gpointer data )
{
    Sojus* sojus = (Sojus*) data;

    GtkWidget* adressen_window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_title( GTK_WINDOW(adressen_window), "Adresse" );
    gtk_window_set_default_size( GTK_WINDOW(adressen_window), 1200, 700 );

    GtkWidget* adressen_headerbar = gtk_header_bar_new( );
    gtk_header_bar_set_show_close_button( GTK_HEADER_BAR(adressen_headerbar), TRUE );
    gtk_header_bar_set_title( GTK_HEADER_BAR(adressen_headerbar), "Adressen" );
    gtk_window_set_titlebar( GTK_WINDOW(adressen_window), adressen_headerbar );

    GtkWidget* grid = gtk_grid_new( );
    gtk_container_add( GTK_CONTAINER(adressen_window), grid );


    return;
}
