/*
 sond (sond_server.h) - Akten, Beweisst�cke, Unterlagen
 Copyright (C) 2026  pelo america

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

/**
 * @file sond_server.h
 * @brief HTTP REST Server für Graph-Datenbank
 *
 * Stellt REST-Endpoints für Node-Operationen bereit.
 */

#ifndef SOND_SERVER_H
#define SOND_SERVER_H

#include <glib-object.h>
#include <libsoup/soup.h>
#include <mysql/mysql.h>

G_BEGIN_DECLS

/* ========================================================================
 * SondServer
 * ======================================================================== */
#define SOND_SERVER_ERROR sond_server_error_quark()
GQuark sond_server_error_quark(void);

/* Error Codes */
typedef enum {
    SOND_SERVER_ERROR_GENERAL = 0,
    SOND_SERVER_ERROR_NOT_FOUND,
    SOND_SERVER_ERROR_SEAFILE,
    SOND_SERVER_ERROR_DUPLICATE
} SondServerError;

typedef struct _SondServer SondServer;

#define SOND_TYPE_SERVER (sond_server_get_type())
G_DECLARE_FINAL_TYPE(SondServer, sond_server, SOND, SERVER, GObject)


G_END_DECLS

#endif /* SOND_SERVER_H */
