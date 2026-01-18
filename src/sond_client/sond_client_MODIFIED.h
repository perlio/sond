/*
 sond (sond_client.h) - Akten, Beweisstücke, Unterlagen
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

#ifndef SOND_CLIENT_H
#define SOND_CLIENT_H

#include <gtk/gtk.h>
#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _SondClient SondClient;

#define SOND_TYPE_CLIENT (sond_client_get_type())
G_DECLARE_FINAL_TYPE(SondClient, sond_client, SOND, CLIENT, GObject)

/**
 * sond_client_new:
 * @config_file: Pfad zur Konfigurationsdatei
 * @error: (nullable): Fehler-Rückgabe
 *
 * Erstellt einen neuen SondClient.
 *
 * Returns: (transfer full) (nullable): Neuer Client oder NULL bei Fehler
 */
SondClient* sond_client_new(const gchar *config_file, GError **error);

/**
 * sond_client_get_server_url:
 * @client: SondClient
 *
 * Returns: Server-URL
 */
const gchar* sond_client_get_server_url(SondClient *client);

/**
 * sond_client_is_connected:
 * @client: SondClient
 *
 * Returns: TRUE wenn verbunden
 */
gboolean sond_client_is_connected(SondClient *client);

/**
 * sond_client_connect:
 * @client: SondClient
 * @error: (nullable): Fehler-Rückgabe
 *
 * Stellt Verbindung zum Server her.
 *
 * Returns: TRUE bei Erfolg
 */
gboolean sond_client_connect(SondClient *client, GError **error);

/**
 * sond_client_get_user_id:
 * @client: SondClient
 *
 * Gibt die User-ID zurück (Seafile-Username).
 *
 * Returns: (transfer none): User-ID String
 */
const gchar* sond_client_get_user_id(SondClient *client);

/**
 * sond_client_set_auth:
 * @client: SondClient
 * @username: Seafile-Username
 * @session_token: Session-Token vom Server
 *
 * Setzt Authentifizierungsinformationen.
 * Wird nach erfolgreichem Login aufgerufen.
 */
void sond_client_set_auth(SondClient *client,
                          const gchar *username,
                          const gchar *session_token);

/**
 * sond_client_set_auth_failed_callback:
 * @client: SondClient
 * @callback: Callback-Funktion bei 401-Fehler
 * @user_data: User-Data für Callback
 *
 * Setzt Callback der bei 401-Fehler aufgerufen wird.
 * Wird verwendet um UI-Benachrichtigungen zu senden.
 * HINWEIS: Für Re-Login wird login_callback verwendet.
 */
void sond_client_set_auth_failed_callback(SondClient *client,
                                          void (*callback)(SondClient*, gpointer),
                                          gpointer user_data);

/**
 * sond_client_set_login_callback:
 * @client: SondClient
 * @callback: Callback-Funktion für Login (wird bei Lazy-Auth und Re-Login aufgerufen)
 * @user_data: User-Data für Callback
 *
 * Setzt Callback der für Login/Re-Login aufgerufen wird.
 * Callback muss TRUE zurückgeben bei Erfolg, FALSE bei Abbruch.
 * Nach erfolgreichem Login muss der Callback sond_client_set_auth() aufrufen.
 */
void sond_client_set_login_callback(SondClient *client,
                                    gboolean (*callback)(SondClient*, gpointer),
                                    gpointer user_data);

/**
 * sond_client_create_and_lock_node:
 * @client: SondClient
 * @node: Node zum Anlegen
 * @lock_reason: (nullable): Grund für Lock
 * @error: (nullable): Fehler-Rückgabe
 *
 * Erstellt Node und setzt Lock atomar auf Server.
 * Node-ID wird aktualisiert.
 * Führt automatisch Login durch falls nicht authentifiziert.
 * Bei abgelaufenem Token wird automatisch Re-Login durchgeführt.
 *
 * Returns: TRUE bei Erfolg
 */
gboolean sond_client_create_and_lock_node(SondClient *client,
                                           gpointer node,
                                           const gchar *lock_reason,
                                           GError **error);

/**
 * sond_client_lock_node:
 * @client: SondClient
 * @node_id: Node-ID
 * @lock_reason: (nullable): Grund für Lock
 * @error: (nullable): Fehler-Rückgabe
 *
 * Setzt Lock auf existierenden Node.
 * Führt automatisch Login durch falls nicht authentifiziert.
 * Bei abgelaufenem Token wird automatisch Re-Login durchgeführt.
 *
 * Returns: TRUE bei Erfolg
 */
gboolean sond_client_lock_node(SondClient *client,
                                gint64 node_id,
                                const gchar *lock_reason,
                                GError **error);

/**
 * sond_client_unlock_node:
 * @client: SondClient
 * @node_id: Node-ID
 * @error: (nullable): Fehler-Rückgabe
 *
 * Entfernt Lock von Node.
 * Führt automatisch Login durch falls nicht authentifiziert.
 * Bei abgelaufenem Token wird automatisch Re-Login durchgeführt.
 *
 * Returns: TRUE bei Erfolg
 */
gboolean sond_client_unlock_node(SondClient *client,
                                  gint64 node_id,
                                  GError **error);

/**
 * sond_client_check_lock:
 * @client: SondClient
 * @node_id: Node-ID
 * @error: (nullable): Fehler-Rückgabe
 *
 * Prüft ob Node gelockt ist.
 *
 * Returns: (transfer full) (nullable): User-ID des Lockers oder NULL wenn nicht gelockt
 */
gchar* sond_client_check_lock(SondClient *client,
                               gint64 node_id,
                               GError **error);

G_END_DECLS

#endif /* SOND_CLIENT_H */
