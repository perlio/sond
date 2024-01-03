/*
sond (sond_server_seafile.h) - Akten, Beweisstücke, Unterlagen
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

#ifndef SOND_SERVER_SEAFILE_H_INCLUDED
#define SOND_SERVER_SEAFILE_H_INCLUDED

gint sond_server_seafile_delete_lib( SondServer*, const gchar*, GError** );

gint sond_server_seafile_create_akte( SondServer*, gint, gint, GError** );

#endif // SOND_SERVER_SEAFILE_H_INCLUDED
