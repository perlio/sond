/*
 sond (sond_server_auth.h) - Akten, Beweisstücke, Unterlagen
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
 * @file sond_server_auth.h
 * @brief Session-Management und Authentifizierung
 */

#ifndef SOND_SERVER_AUTH_H
#define SOND_SERVER_AUTH_H

#include <glib.h>

G_BEGIN_DECLS

/**
 * SessionData:
 *
 * Enthält Informationen zu einer aktiven Session.
 */
typedef struct {
    gchar *username;           /* Seafile-Username */
    GDateTime *created;        /* Wann wurde Session erstellt */
    GDateTime *last_activity;  /* Letzte Aktivität */
} SessionData;

/**
 * SessionManager:
 *
 * Verwaltet alle aktiven Sessions.
 */
typedef struct {
    GHashTable *sessions;              /* session_token -> SessionData* */
    guint session_lifetime_hours;      /* Aus Config: 0 = unlimited */
    guint session_inactivity_minutes;  /* Aus Config: 0 = disabled */
    GMutex mutex;                      /* Thread-safe access */
} SessionManager;

/**
 * session_manager_new:
 * @session_lifetime_hours: Session-Lifetime (0 = unlimited)
 * @session_inactivity_minutes: Auto-Logout nach Inaktivität (0 = disabled)
 *
 * Erstellt neuen SessionManager.
 *
 * Returns: (transfer full): Neuer SessionManager
 */
SessionManager* session_manager_new(guint session_lifetime_hours,
                                     guint session_inactivity_minutes);

/**
 * session_manager_free:
 * @manager: SessionManager
 *
 * Gibt SessionManager frei.
 */
void session_manager_free(SessionManager *manager);

/**
 * session_manager_create:
 * @manager: SessionManager
 * @username: Seafile-Username
 *
 * Erstellt neue Session.
 *
 * Returns: (transfer full): Session-Token (UUID)
 */
gchar* session_manager_create(SessionManager *manager,
                               const gchar *username);

/**
 * session_manager_validate:
 * @manager: SessionManager
 * @session_token: Session-Token
 * @username: (out) (transfer none) (nullable): Username zurückgeben
 *
 * Validiert Session und prüft Expiry.
 *
 * Returns: TRUE wenn gültig, FALSE wenn expired/ungültig
 */
gboolean session_manager_validate(SessionManager *manager,
                                   const gchar *session_token,
                                   const gchar **username);

/**
 * session_manager_update_activity:
 * @manager: SessionManager
 * @session_token: Session-Token
 *
 * Aktualisiert last_activity.
 */
void session_manager_update_activity(SessionManager *manager,
                                      const gchar *session_token);

/**
 * session_manager_invalidate:
 * @manager: SessionManager
 * @session_token: Session-Token
 *
 * Entfernt Session (Logout).
 */
void session_manager_invalidate(SessionManager *manager,
                                 const gchar *session_token);

/**
 * session_manager_cleanup_expired:
 * @manager: SessionManager
 *
 * Entfernt alle expired Sessions.
 * Sollte regelmäßig aufgerufen werden.
 *
 * Returns: Anzahl entfernter Sessions
 */
guint session_manager_cleanup_expired(SessionManager *manager);

G_END_DECLS

#endif /* SOND_SERVER_AUTH_H */
