/*
sond (sond_server.h) - Akten, Beweisstücke, Unterlagen
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

#ifndef SOND_SERVER_H_INCLUDED
#define SOND_SERVER_H_INCLUDED

#ifdef G_LOG_DOMAIN
#undef G_LOG_DOMAIN
#endif // G_LOG_DOMAIN

#define G_LOG_DOMAIN "SondServer"

#include <glib.h>

#define SOND_SERVER_ERROR sond_server_error_quark()
G_DEFINE_QUARK(sond-server-error-quark,sond_server_error)

enum SondServerError
{
    SOND_SERVER_ERROR_NOTFOUND,
    NUM_SOND_CLIENT_ERROR
};

typedef struct st_mysql MYSQL;

typedef struct _RegNrJahr
{
    gint reg_nr;
    gint reg_jahr;
} RegNrJahr;

typedef struct _SondServer
{
    GMainLoop* loop;
    gchar* password;
    gchar* base_dir;
    gchar* log_file;

    GArray* arr_creds;
    GMutex mutex_arr_creds;

    gchar* mysql_host;
    gint mysql_port;
    gchar* mysql_user;
    gchar* mysql_password;
    gchar* mysql_db;
    gchar* mysql_path_ca;
    MYSQL* mysql_con;
    GArray* arr_locks;
    GMutex mysql_mutex_con;

    gchar* seafile_user;
    gchar* seafile_password;
    gchar* seafile_url;
    gchar* auth_token;

    GThreadPool* thread_pool;
} SondServer;


gint sond_server_init_mysql_con( SondServer*, GError** );


#endif // SOND_SERVER_H_INCLUDED
