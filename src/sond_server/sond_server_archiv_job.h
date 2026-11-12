/*
 sond (sond_server_archiv_job.h) - Akten, Beweisstücke, Unterlagen
 Copyright (C) 2026 pelo america

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
 * @file sond_server_archiv_job.h
 * @brief HTTP-Endpunkte zum Anstoßen und Abfragen von Archivierungen
 */

#ifndef SOND_SERVER_ARCHIV_JOB_H
#define SOND_SERVER_ARCHIV_JOB_H

#include <glib.h>
#include "sond_server.h"

G_BEGIN_DECLS

/**
 * sond_server_archiv_job_register_handlers:
 * @server: SondServer Instanz
 *
 * Registriert die Endpunkte (alle mit Anmeldung):
 *
 * - POST /archiv/start   Body {"library_name": "...", "archiv_name": "..."}
 *                        Startet die Archivierung im Hintergrund (Thread).
 *                        409, wenn bereits eine läuft (es läuft höchstens eine).
 * - GET  /archiv/status?library_name=X
 *                        {"state": "none"|"running"|"ok"|"error", "message",
 *                         "archiv_name", "user", "start_time", "end_time"}
 * - GET  /archiv/status  ohne Parameter: {"jobs": [...]} aller bekannten Jobs
 *
 * Der Jobstand liegt nur im Arbeitsspeicher und geht beim Neustart verloren;
 * der Start ist wiederholbar (s. sond_server_archiv_library).
 */
void sond_server_archiv_job_register_handlers(SondServer *server);

G_END_DECLS

#endif /* SOND_SERVER_ARCHIV_JOB_H */
