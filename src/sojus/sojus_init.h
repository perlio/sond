/*
sojus (sojus_init.h) - softkanzlei
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


#ifndef SOJUS_INIT_H_INCLUDED
#define SOJUS_INIT_H_INCLUDED

#include <gtk/gtk.h>
#include <mysql/mysql.h>


typedef struct _Sojus
{
    GtkWidget* app_window;

    GSettings* settings;
    GSocketService* socket;
//    Clipboard* clipboard;

    MYSQL* con;

    gint regnr_akt;
    gint jahr_akt;
    gint adressnr_akt;

    GPtrArray* arr_open_fm;
} Sojus;


Sojus* sojus_init( void );

#endif // SOJUS_INIT_H_INCLUDED
